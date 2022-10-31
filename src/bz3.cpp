//
// Created by Dariusz Niedoba on 03.01.2019.
//

#include <filesystem>
#include <chrono>
#include <iostream>
#include <fstream>
#include <map>
#include <cppitertools/itertools.hpp>
#include <cxxopts.hpp>
#include "bozorth3/bozorth3.h"
#include "bozorth3/utils.hpp"
#include "utils.h"
#include "ThreadPool.h"

#define MIN_BOZORTH_MINUTIAE 0
#define MAX_BOZORTH_MINUTIAE 200


class Range
{
private:
	u32 first_{};
	u32 last_{};

public:
	Range(u32 first, u32 last) : first_{first}, last_{last}
	{
		assert(first <= last);
	}

	[[nodiscard]] u32 first() const { return first_; }

	[[nodiscard]] u32 last() const { return last_; }

	[[nodiscard]] u32 length() const { return last_ - first_ + 1; }
};

enum class MatchMode
{
	All,
	OnlyFirstMatch,
	AllMatches
};

struct Options
{
	bool use_ansi = false;
	MatchMode mode = MatchMode::All;
	int threshold = 40;
	bool dry_run = false;
	int max_minutiae = 150;
	u32 threads = 1;

	std::string pair_file{};
	std::string probe_files{};
	std::string gallery_files{};

	std::string fixed_probe{};
	std::string fixed_gallery{};

	std::optional<Range> probe_range = std::nullopt;
	std::optional<Range> gallery_range = std::nullopt;

	bool only_scores = false;
	std::optional<std::string> output_file{};
	//    std::string output_directory{};
};

std::pair<std::vector<std::string>, std::vector<std::string>>
find_items_from_pairs(const std::string& file_name)
{
	std::ifstream file{file_name};
	if (file.fail())
	{
		std::cerr << "error: cannot load pairs from file " << file_name << "\n";
		return {};
	}

	std::vector<std::string> probes{};
	std::vector<std::string> galleries{};

	std::string line{};
	for (auto i = 0u; std::getline(file, line); i++)
	{
		if (i % 2 == 0)
		{
			probes.push_back(line);
		}
		else
		{
			galleries.push_back(line);
		}
	}

	if (probes.size() != galleries.size())
	{
		std::cerr << "warning: there are " << probes.size() << " probe files and " << galleries.size()
			<< " gallery files (these numbers should be equal), skipping last gallery file \n";
		galleries.pop_back();
	}

	return {probes, galleries};
}

std::vector<std::string> get_items_from_file(const std::string& file_name)
{
	std::ifstream file{file_name};
	if (file.fail())
	{
		std::cerr << "error: cannot load pairs from file " << file_name << "\n";
		return {};
	}

	std::vector<std::string> files{};
	std::string line{};
	while (std::getline(file, line))
	{
		files.push_back(line);
	}

	return std::move(files);
}

static bool ends_with(const std::string& str, const std::string& suffix)
{
	return str.size() >= suffix.size() &&
		str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

namespace fs = std::filesystem;

std::vector<std::string> get_items_from_directory(const std::string& directory)
{
	std::vector<std::string> files{};
	for (const auto& entry : fs::directory_iterator(directory))
	{
		if (!entry.is_regular_file())
		{
			continue;
		}

		const auto path = entry.path().string();
		if (!ends_with(path, ".xyt"))
		{
			continue;
		}
		files.emplace_back(path);
	}
	std::sort(std::begin(files), std::end(files));
	return std::move(files);
}

std::vector<std::string> get_items_from_file_or_directory(const std::string& path)
{
	if (fs::is_regular_file(path))
	{
		return std::move(get_items_from_file(path));
	}
	if (fs::is_directory(path))
	{
		return std::move(get_items_from_directory(path));
	}
	if (fs::exists(path))
	{
		std::cerr << "error: cannot use path '" << path << "' \n";
	}
	else
	{
		std::cerr << "error: path '" << path << "' does not exist\n";
	}
	return {};
}

using Score = int;

enum class CompareMode
{
	OneToOne,
	ManyToMany,
	OneToMany,
};

using CallbackResult = bool;
using ScoreCallback = std::function<static_cast<CallbackResult>(std::optional<Score>)>;
using MatchCallback = std::function<void(const std::string &, const std::string &, std::optional<Score>)>;
using CacheItem = std::pair<std::vector<Minutia>, std::vector<Edge>>;

static auto make_generic_executor(
	const std::string& probe,
	const std::string& gallery,
	std::map<std::string, CacheItem>& cache,
	bz3::Format format,
	u32 max_minutiae
)
{
	const auto gallery_cache = cache_data(cache, gallery, max_minutiae);
	const auto probe_cache = cache_data(cache, probe, max_minutiae);

	return [format, gc = gallery_cache, pc = probe_cache]() -> std::optional<Score>
	{
		if (gc.has_value() && pc.has_value())
		{
			const auto [gallery_minutia, gallery_edges] = gc.value();
			const auto [probe_minutia, probe_edges] = pc.value();
			const auto score = match(probe_minutia, probe_edges, gallery_minutia, gallery_edges,
			                         bz3::Format::NistInternal);
			return std::make_optional(score);
		}
		return std::nullopt;
	};
}


struct ExecuteParallelOptions
{
	MatchMode match_mode = MatchMode::All;
	const std::span<const std::string>& probes;
	const std::span<const std::string>& galleries;
	const ScoreCallback score_callback;
	const MatchCallback match_callback;
	u32 max_minutiae{};
	bz3::Format format = bz3::Format::NistInternal;
	u32 threads{};
	u32 chunk_size = 1000;
};

static void execute_parallel_one_to_one(const ExecuteParallelOptions& options)
{
	std::map<std::string, CacheItem> cache{};
	ThreadPool pool{options.threads};
	std::vector<std::tuple<u32, u32, std::future<std::optional<Score>>>> tasks{};
	tasks.reserve(options.chunk_size);

	for (auto&& chunk : iter::chunked(iter::zip(iter::enumerate(options.probes), iter::enumerate(options.galleries)),
	                                  options.chunk_size))
	{
		for (auto&& [probe_item, gallery_item] : chunk)
		{
			const auto& [probe_index, probe] = probe_item;
			const auto& [gallery_index, gallery] = gallery_item;
			const auto&& task = make_generic_executor(probe, gallery, cache, options.format, options.max_minutiae);
			tasks.emplace_back(probe_index, gallery_index, pool.enqueue(std::move(task)));
		}
		for (auto&& [probe_index, gallery_index, task] : tasks)
		{
			const auto score = task.get();
			if (options.score_callback(score))
			{
				options.match_callback(options.probes[probe_index], options.galleries[gallery_index], score);
				if (options.match_mode == MatchMode::OnlyFirstMatch)
				{
					return;
				}
			}
		}
		tasks.clear();
	}
}


static void execute_parallel_many_to_many(const ExecuteParallelOptions& options)
{
	std::map<std::string, CacheItem> cache{};
	ThreadPool pool{options.threads};
	std::vector<std::tuple<u32, u32, std::future<std::optional<Score>>>> tasks{};
	tasks.reserve(options.chunk_size);

	for (auto&& chunk : iter::chunked(
		     iter::product(iter::enumerate(options.probes), iter::enumerate(options.galleries)),
		     options.chunk_size))
	{
		for (auto&& [probe_item, gallery_item] : chunk)
		{
			const auto& [probe_index, probe] = probe_item;
			const auto& [gallery_index, gallery] = gallery_item;
			const auto&& task = make_generic_executor(probe, gallery, cache, options.format, options.max_minutiae);
			tasks.emplace_back(probe_index, gallery_index, pool.enqueue(task));
		}
		for (auto&& [probe_index, gallery_index, task] : tasks)
		{
			const auto score = task.get();
			if (options.score_callback(score))
			{
				options.match_callback(options.probes[probe_index], options.galleries[gallery_index], score);
				if (options.match_mode == MatchMode::OnlyFirstMatch)
				{
					return;
				}
			}
		}
		tasks.clear();
	}
}


static void execute_parallel_one_to_many(const ExecuteParallelOptions& options)
{
	std::map<std::string, CacheItem> cache{};
	ThreadPool pool{options.threads};
	std::vector<std::tuple<u32, std::future<void>>> tasks{};
	tasks.reserve(options.chunk_size);
	std::vector<std::pair<std::string, Score>> found_galleries{};

	for (auto&& [probe_index, probe] : iter::enumerate(options.probes))
	{
		bool is_done_for_probe = false;
		found_galleries.clear();

		const auto probe_cache = cache_data(cache, probe, options.max_minutiae);
		if (!probe_cache.has_value())
		{
			std::cerr << "error occurred when loading " << probe << "\n";
			continue;
		}

		const auto& [probe_minutia, probe_edges] = probe_cache.value();

		for (auto&& chunk : iter::chunked(iter::enumerate(options.galleries), options.chunk_size))
		{
			std::size_t done_tasks = 0;
			std::mutex mutex;
			std::condition_variable all_done_or_found{};

			for (auto&& [gallery_index, gallery] : chunk)
			{
				const auto gallery_cache = cache_data(cache, gallery, options.max_minutiae);
				if (!gallery_cache.has_value())
				{
					std::cerr << "error occurred when loading " << gallery << "\n";
					continue;
				}

				const auto& [gallery_minutia, gallery_edges] = gallery_cache.value();
				auto task = [&,
						format = options.format,
						probe_minutia = probe_minutia,
						probe_edges = probe_edges,
						gallery = gallery,
						gallery_minutia = gallery_minutia,
						gallery_edges = gallery_edges
					]() -> auto
				{
					const auto score = match(probe_minutia, probe_edges, gallery_minutia, gallery_edges,
					                         bz3::Format::NistInternal);

					{
						std::lock_guard guard{mutex};
						if (options.score_callback(std::make_optional(score)))
						{
							found_galleries.emplace_back(gallery, score);
							if (options.match_mode == MatchMode::OnlyFirstMatch)
							{
								is_done_for_probe = true;
							}
						}

						done_tasks += 1;
					}

					all_done_or_found.notify_all();
				};
				tasks.emplace_back(gallery_index, pool.enqueue(task));
			}

			std::unique_lock lock{mutex};
			all_done_or_found.wait(lock, [&]()
			{
				return done_tasks == tasks.size() || is_done_for_probe;
			});
			lock.unlock();

			pool.drain();
			tasks.clear();

			if (is_done_for_probe)
			{
				break;
			}
		}

		if (found_galleries.empty())
		{
			options.match_callback(options.probes[probe_index], "-", std::nullopt);
		}
		else
		{
			if (options.match_mode == MatchMode::OnlyFirstMatch)
			{
				const auto& [gallery, score] = found_galleries[0];
				options.match_callback(probe, gallery, std::make_optional(score));
			}
			else
			{
				for (const auto& [gallery, score] : found_galleries)
				{
					options.match_callback(probe, gallery, std::make_optional(score));
				}
			}
		}
	}
}


static void execute_parallel(
	CompareMode compare_mode,
	const ExecuteParallelOptions& options
)
{
	switch (compare_mode)
	{
	case CompareMode::OneToOne:
		execute_parallel_one_to_one(options);
		break;
	case CompareMode::ManyToMany:
		execute_parallel_many_to_many(options);
		break;
	case CompareMode::OneToMany:
		execute_parallel_one_to_many(options);
		break;
	}
}

static void execute_sequential(
	CompareMode compare_mode,
	MatchMode match_mode,
	const std::span<const std::string>& probes,
	const std::span<const std::string>& galleries,
	const ScoreCallback& score_callback,
	const MatchCallback& match_callback,
	u32 max_minutiae,
	bz3::Format format
)
{
	std::map<std::string, std::pair<std::vector<Minutia>, std::vector<Edge>>> cache{};

	auto execute = [&](const std::string& probe, const std::string& gallery) -> std::optional<Score>
	{
		const auto& gallery_cache = cache_data(cache, gallery, max_minutiae);
		const auto& probe_cache = cache_data(cache, probe, max_minutiae);

		if (gallery_cache.has_value() && probe_cache.has_value())
		{
			const auto& [gallery_minutia, gallery_edges] = gallery_cache.value();
			const auto& [probe_minutia, probe_edges] = probe_cache.value();
			const auto score = match(probe_minutia, probe_edges, gallery_minutia, gallery_edges,
			                         bz3::Format::NistInternal);
			return std::make_optional(score);
		}
		return std::nullopt;
	};

	if (compare_mode == CompareMode::OneToOne)
	{
		for (auto&& [probe_item, gallery_item] : iter::zip(iter::enumerate(probes), iter::enumerate(galleries)))
		{
			const auto& [probe_index, probe] = probe_item;
			const auto& [gallery_index, gallery] = gallery_item;
			const auto score = execute(probe, gallery);
			if (score_callback(score))
			{
				match_callback(probes[probe_index], galleries[gallery_index], score);
				if (match_mode == MatchMode::OnlyFirstMatch)
				{
					return;
				}
			}
		}
	}
	else if (compare_mode == CompareMode::ManyToMany)
	{
		for (auto&& [probe_item, gallery_item] : iter::product(iter::enumerate(probes), iter::enumerate(galleries)))
		{
			const auto& [probe_index, probe] = probe_item;
			const auto& [gallery_index, gallery] = gallery_item;
			const auto score = execute(probe, gallery);
			if (score_callback(score))
			{
				match_callback(probes[probe_index], galleries[gallery_index], score);
				if (match_mode == MatchMode::OnlyFirstMatch)
				{
					return;
				}
			}
		}
	}
	else if (compare_mode == CompareMode::OneToMany)
	{
		for (auto&& [probe_index, probe] : iter::enumerate(probes))
		{
			for (auto&& [gallery_index, gallery] : iter::enumerate(galleries))
			{
				const auto score = execute(probe, gallery);
				if (score_callback(score))
				{
					match_callback(probes[probe_index], galleries[gallery_index], score);
					if (match_mode == MatchMode::OnlyFirstMatch)
					{
						break;
					}
				}
			}
		}
	}
}


static std::optional<Range> parse_range(const std::string& value)
{
	const std::regex regex("^(\\d+)-(\\d+)$");
	if (std::smatch match{}; std::regex_match(value, match, regex)
	)
	{
		i32 first, last;
		try
		{
			first = std::stoi(match[1]);
			last = std::stoi(match[2]);
		}
		catch (std::out_of_range& e)
		{
			return std::nullopt;
		}

		if (first >= 1 && first <= last)
		{
			return std::make_optional(Range{
				static_cast<u32>(first - 1),
				static_cast<u32>(last - 1)
			});
		}
		return std::nullopt;
	}
	else
	{
		return std::nullopt;
	}
}

template <typename T>
static std::optional<std::span<T>> get_span_by_range(std::span<T> span, Range range)
{
	if (range.first() < span.size() && range.last() <= span.size())
	{
		return span.subspan(range.first(), range.length());
	}
	return std::nullopt;
}

static void dry_run(
	std::span<const std::string> probes,
	std::span<const std::string> galleries,
	CompareMode mode
)
{
	if (mode == CompareMode::OneToOne)
	{
		for (int i = 0; i < probes.size(); i++)
		{
			std::cout << probes[i] << " " << galleries[i] << "\n";
		}
	}
	else if (mode == CompareMode::ManyToMany || mode == CompareMode::OneToMany)
	{
		for (const auto& probe : probes)
		{
			for (const auto& gallery : galleries)
			{
				std::cout << probe << " " << gallery << "\n";
			}
		}
	}
}

static void run(
	const std::span<const std::string> probes,
	const std::span<const std::string> galleries,
	CompareMode mode,
	const Options& options
)
{
	const auto execute_into_stream = [&](std::ostream& output)
	{
		const auto score_callback = [&](const auto score) -> CallbackResult
		{
			if (options.mode == MatchMode::All)
			{
				return true;
			}
			return score.has_value() && score.value() >= options.threshold;
		};

		const auto match_callback = [&](const auto probe, const auto gallery, const auto score)
		{
			if (options.mode == MatchMode::All && options.only_scores)
			{
				output << score.value_or(-1) << "\n";
			}
			else
			{
				output << probe << " " << gallery << " " << score.value_or(-1) << "\n";
			}
		};

		const auto format = options.use_ansi ? bz3::Format::Ansi : bz3::Format::NistInternal;
		if (options.threads > 1)
		{
			ExecuteParallelOptions execute_options{
				options.mode,
				probes,
				galleries,
				score_callback,
				match_callback,
				static_cast<u32>(options.max_minutiae),
				format,
				options.threads
			};
			execute_parallel(mode, execute_options);
		}
		else
		{
			execute_sequential(
				mode, options.mode, probes, galleries, score_callback, match_callback,
				static_cast<u32>(options.max_minutiae), format
			);
		}
	};

	if (options.output_file.has_value())
	{
		std::ofstream file{options.output_file.value(), std::ios::out};
		if (!file.is_open())
		{
			std::cerr << "error: cannot open file '" << options.output_file.value() << "'\n";
			return;
		}
		execute_into_stream(file);
	}
	else
	{
		execute_into_stream(std::cout);
	}
}

cxxopts::ParseResult
parse(int argc, const char* argv[])
{
	Options opt{};

	try
	{
		cxxopts::Options options(argv[0]);
		options.positional_help("[.xyt files]");

		std::string probe_range{};
		std::string gallery_range{};
		std::string match_mode{};
		std::string output_file{};
		int threads{};
		const auto max_threads = std::thread::hardware_concurrency();

		options.add_options("Input")
			("M,pair-list", "file containing list of pairs to compare, one file in each line",
			 cxxopts::value<std::string>(opt.pair_file))
			("p,probe", "single probe file", cxxopts::value<std::string>(opt.fixed_probe))
			("P,probe-list", "file containing list of probe files or directory",
			 cxxopts::value<std::string>(opt.probe_files))
			("g,gallery", "single gallery file", cxxopts::value<std::string>(opt.fixed_gallery))
			("G,gallery-list", "file containing list of gallery files or directory",
			 cxxopts::value<std::string>(opt.gallery_files))

			("positional", "list of files", cxxopts::value<std::vector<std::string>>())
			("probe-range", "subset of files in the probe list to process",
			 cxxopts::value<std::string>(probe_range))
			("gallery-range", "subset of files in the gallery file to process",
			 cxxopts::value<std::string>(gallery_range));

		options.add_options("Output")
			("s,only-scores", "print only scores without filenames (applicable only for -m 'all')",
			 cxxopts::value<bool>(opt.only_scores)->default_value("false"))
			("o,output", "output file", cxxopts::value<std::string>(output_file)->default_value("-"));

		options.add_options("Mode")
		("m,match-mode",
		 "matching mode; supported modes: all, first-match, all-matches",
		 cxxopts::value<std::string>(match_mode)->default_value("all"))
		("t,threshold", "set match score threshold",
		 cxxopts::value<int>(opt.threshold)->default_value("40"));

		options.add_options("Miscellaneous")
			("a,ansi", "all *.xyt files use representation according to ANSI INCITS 378-2004",
			 cxxopts::value<bool>(opt.use_ansi)->default_value("false"))
			("n,max-minutiae",
			 "set maximum number of minutiae to use from any file; allowed range 0-200",
			 cxxopts::value<int>(opt.max_minutiae)->default_value("150"))

			("T,threads", "number of threads to use; supported values: 1-" + std::to_string(max_threads),
			 cxxopts::value<int>(threads)->default_value(std::to_string(std::thread::hardware_concurrency())))
			("d,dry", "only print the filenames between which match scores would be computed",
			 cxxopts::value<bool>(opt.dry_run))

			("h,help", "print this help");

		options.parse_positional({"input", "positional"});

		auto result = options.parse(argc, argv);

		if (result.count("help"))
		{
			std::cout << options.help({"Input", "Output", "Mode", "Miscellaneous"}) << std::endl;
			exit(0);
		}

		std::vector<std::string> errors{};
		if (opt.max_minutiae < MIN_BOZORTH_MINUTIAE || opt.max_minutiae > MAX_BOZORTH_MINUTIAE)
		{
			errors.emplace_back("invalid number of computable minutiae");
		}

		const auto use_probe_range = result.count("probe-range");
		if (use_probe_range)
		{
			if (const auto&& parsed = parse_range(probe_range); parsed.has_value()
			)
			{
				opt.probe_range = parsed;
			}
			else
			{
				errors.emplace_back("invalid probe range format");
			}
		}

		const auto use_gallery_range = result.count("gallery-range");
		if (use_gallery_range)
		{
			if (const auto&& parsed = parse_range(gallery_range); parsed.has_value()
			)
			{
				opt.gallery_range = parsed;
			}
			else
			{
				errors.emplace_back("invalid gallery range format");
			}
		}

		const auto use_threads = result.count("threads");
		if (use_threads)
		{
			if (threads > 0 && threads <= max_threads)
			{
				opt.threads = static_cast<u32>(threads);
			}
			else
			{
				errors.emplace_back("invalid number of threads");
			}
		}
		else
		{
			opt.threads = std::thread::hardware_concurrency();
		}

		if (match_mode == "all")
		{
			opt.mode = MatchMode::All;
		}
		else if (match_mode == "first-match")
		{
			opt.mode = MatchMode::OnlyFirstMatch;
		}
		else if (match_mode == "all-matches")
		{
			opt.mode = MatchMode::AllMatches;
		}
		else
		{
			errors.emplace_back("unsupported match mode '" + match_mode + "'");
		}

		const auto use_pair_list = result.count("pair-list");
		const auto use_probe_list = result.count("probe-list");
		const auto use_gallery_list = result.count("gallery-list");
		const auto use_probe = result.count("probe");
		const auto use_gallery = result.count("gallery");
		const auto use_positional = result.count("positional");
		const auto use_dry_run = result.count("dry");
		const auto use_output_file = result.count("output");

		if (use_pair_list && use_probe_list)
		{
			errors.emplace_back(R"(flags "-M" and "-P" are not compatible)");
		}

		if (use_pair_list && use_gallery_list)
		{
			errors.emplace_back(R"(flags "-M" and "-G" are not compatible)");
		}

		if (use_pair_list && use_probe)
		{
			errors.emplace_back(R"(flags "-M" and "-p" are incompatible)");
		}

		if (use_pair_list && use_gallery)
		{
			errors.emplace_back(R"(flags "-M" and "-g" are incompatible)");
		}

		if (use_probe_list && use_probe)
		{
			errors.emplace_back(R"(flags "-P" and "-p" are incompatible)");
		}

		if (use_gallery_list && use_gallery)
		{
			errors.emplace_back(R"(flags "-G" and "-g" are incompatible)");
		}

		if (opt.mode != MatchMode::All && use_pair_list)
		{
			errors.emplace_back(R"(flag "-M" is not compatible with modes other than "all")");
		}

		if (!errors.empty())
		{
			std::cerr << "Parsing errors: \n";
			for (const auto& error : errors)
			{
				std::cerr << " - " << error << "\n";
			}
			exit(1);
		}

		if (use_output_file)
		{
			opt.output_file = std::make_optional(output_file);
		}

		CompareMode mode = opt.mode == MatchMode::All ? CompareMode::ManyToMany : CompareMode::OneToMany;
		std::vector<std::string> probes{};
		std::vector<std::string> galleries{};

		if (use_pair_list)
		{
			std::tie(probes, galleries) = find_items_from_pairs(opt.pair_file);
			mode = CompareMode::OneToOne;
		}
		else if (use_probe && use_gallery)
		{
			probes = {opt.fixed_probe};
			galleries = {opt.fixed_gallery};
		}
		else if (use_probe)
		{
			probes = {opt.fixed_probe};
			if (use_gallery_list)
			{
				galleries = get_items_from_file_or_directory(opt.gallery_files);
			}
			else if (use_positional)
			{
				galleries = result["positional"].as<std::vector<std::string>>();
			}
			else
			{
				std::cerr << "error: missing gallery files\n";
				exit(1);
			}
		}
		else if (use_gallery)
		{
			galleries = {opt.fixed_gallery};
			if (use_probe_list)
			{
				probes = get_items_from_file_or_directory(opt.probe_files);
			}
			else if (use_positional)
			{
				probes = result["positional"].as<std::vector<std::string>>();
			}
			else
			{
				std::cerr << "error: missing probe files\n";
				exit(1);
			}
		}
		else if (use_probe_list && use_gallery_list)
		{
			probes = get_items_from_file_or_directory(opt.probe_files);
			galleries = get_items_from_file_or_directory(opt.gallery_files);
		}
		else if (use_probe_list && use_positional)
		{
			probes = get_items_from_file_or_directory(opt.probe_files);
			galleries = result["positional"].as<std::vector<std::string>>();
		}
		else if (use_gallery_list && use_positional)
		{
			probes = result["positional"].as<std::vector<std::string>>();
			galleries = get_items_from_file_or_directory(opt.gallery_files);
		}
		else if (use_positional)
		{
			mode = CompareMode::OneToOne;

			const auto items = result["positional"].as<std::vector<std::string>>();
			if (items.size() % 2 == 1)
			{
				std::cout << "Number of files to compare is odd" << "\n";
				exit(1);
			}

			probes.reserve(items.size() / 2);
			galleries.reserve(items.size() / 2);

			for (int i = 0; i < items.size(); i += 2)
			{
				probes.push_back(items[i + 0]);
				galleries.push_back(items[i + 1]);
			}
		}
		else
		{
			std::cerr << "error: missing input data\n";
			exit(1);
		}

		std::span<std::string> probes_range = std::span(probes);
		if (opt.probe_range.has_value())
		{
			if (const auto span = get_span_by_range(probes_range, opt.probe_range.value()); span.has_value()
			)
			{
				probes_range = span.value();
			}
			else
			{
				std::cerr << "error: range for probes out of bounds\n";
				exit(1);
			}
		}

		std::span<std::string> galleries_range = std::span(galleries);
		if (opt.gallery_range.has_value())
		{
			if (const auto span = get_span_by_range(galleries_range, opt.gallery_range.value()); span.has_value()
			)
			{
				galleries_range = span.value();
			}
			else
			{
				std::cerr << "error: range for galleries out of bounds\n";
				exit(1);
			}
		}

		if (use_dry_run)
		{
			dry_run(probes_range, galleries_range, mode);
		}
		else
		{
			run(probes_range, galleries_range, mode, opt);
		}

		return result;
	}
	catch (const cxxopts::exceptions::parsing& e)
	{
		std::cout << "error parsing options: " << e.what() << std::endl;
		exit(1);
	}
}


int main(int argc, const char* argv[])
{
	auto result = parse(argc, argv);
	const auto& arguments = result.arguments();
	return 0;
}
