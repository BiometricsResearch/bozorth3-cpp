//
// Created by Dariusz Niedoba on 08.01.2019.
//

#include "utils.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <chrono>

constexpr std::size_t MAX_FILE_MINUTIAE = 1000;


using namespace bz3;

std::optional<std::vector<Minutia>> load_minutiae(
	std::string_view xyt_path,
	std::optional<std::string_view> min_path,
	u32 max_minutiae
)
{
	std::ifstream xyt_file{xyt_path.data(), std::ios::in};
	if (xyt_file.fail())
	{
		return {};
	}

	std::vector<RawMinutia> minutiae{};
	minutiae.reserve(MAX_BOZORTH_MINUTIAE);
	while (!xyt_file.eof())
	{
		RawMinutia min{};
		xyt_file >> min.x;
		if (xyt_file.eof()) break;
		xyt_file >> min.y;
		if (xyt_file.eof()) break;
		xyt_file >> min.t;
		if (xyt_file.eof()) break;
		xyt_file >> min.q;
		if (xyt_file.eof()) break;
		minutiae.push_back(min);
		if (minutiae.size() == MAX_FILE_MINUTIAE)
		{
			break;
		}
	}

	if (min_path)
	{
		std::ifstream min_file{min_path.value().data(), std::ios::in};
		if (min_file.is_open())
		{
			auto min_index = 0u;
			for (auto i = 0u; !min_file.eof(); i++)
			{
				std::string line{};
				std::getline(min_file, line);

				if (i < 4)
				{
					continue;
				}
				const auto kind = line.substr(33, 3);
				auto& minutia = minutiae.at(min_index);
				if (kind == "BIF")
				{
					minutia.kind = MinutiaKind::Bif;
				}
				else if (kind == "RIG")
				{
					minutia.kind = MinutiaKind::Rig;
				}
				else
				{
					minutia.kind = std::nullopt;
				}
			}
		}
	}

	return prune_minutiae(minutiae, max_minutiae);
}


struct Cell
{
	int value; /* pointer to an array of pointers to index arrays */
	int index; /* pointer to an item array */
};

/***********************************************************************/
/*******************************************************************
select_pivot()
selects a pivot from a list being sorted using the Singleton Method.
*******************************************************************/
static int select_pivot(Cell v[], int left, int right)
{
	int midpoint = (left + right) / 2;

	int ileft = v[left].value;
	int imidpoint = v[midpoint].value;
	int iright = v[right].value;
	if (ileft <= imidpoint)
	{
		if (imidpoint <= iright)
		{
			return midpoint;
		}
		if (iright > ileft)
		{
			return right;
		}
		return left;
	}
	if (ileft < iright)
	{
		return left;
	}
	if (iright < imidpoint)
	{
		return midpoint;
	}
	return right;
}

/***********************************************************************/
/********************************************************
partition_dec()
Inputs a pivot element making comparisons and swaps with other elements in a list,
until pivot resides at its correct position in the list.
********************************************************/
static void
partition_dec(Cell v[], int* llen, int* rlen, int* ll, int* lr, int* rl, int* rr, int p, int l, int r)
{
	*ll = l;
	*rr = r;
	while (true)
	{
		if (l < p)
		{
			if (v[l].value < v[p].value)
			{
				std::swap(v[l], v[p]);
				p = l;
			}
			else
			{
				l++;
			}
		}
		else
		{
			if (r > p)
			{
				if (v[r].value > v[p].value)
				{
					std::swap(v[r], v[p]);
					p = r;
					l++;
				}
				else
				{
					r--;
				}
			}
			else
			{
				*lr = p - 1;
				*rl = p + 1;
				*llen = *lr - *ll + 1;
				*rlen = *rr - *rl + 1;
				return;
			}
		}
	}
}

/***********************************************************************/
/********************************************************
qsort_decreasing()
This procedure inputs a pointer to an index_struct, the subscript of an index array to be
sorted, a left subscript pointing to where the  sort is to begin in the index array, and a right
subscript where to end. This module invokes a  decreasing quick-sort sorting the index array  from l to r.
********************************************************/


static void qsort_decreasing(Cell v[], int left, int right)
{
	std::vector<int> stack{};
	stack.push_back(left);
	stack.push_back(right);
	while (!stack.empty())
	{
		right = stack.back();
		stack.pop_back();
		left = stack.back();
		stack.pop_back();
		if (left < right)
		{
			int pivot = select_pivot(v, left, right);
			int llen, rlen;
			int lleft, lright, rleft, rright;
			partition_dec(v, &llen, &rlen, &lleft, &lright, &rleft, &rright, pivot, left, right);
			if (llen > rlen)
			{
				stack.push_back(lleft);
				stack.push_back(lright);
				stack.push_back(rleft);
				stack.push_back(rright);
			}
			else
			{
				stack.push_back(rleft);
				stack.push_back(rright);
				stack.push_back(lleft);
				stack.push_back(lright);
			}
		}
	}
}

static
void sort_order_decreasing(
	const int values[], /* INPUT:  the unsorted values themselves */
	int num, /* INPUT:  the number of values */
	int order[] /* OUTPUT: the order for each of the values if sorted */
)
{
	std::vector<Cell> cells; // index, item
	cells.reserve(static_cast<unsigned long>(num));

	for (int i = 0; i < num; i++)
	{
		cells.push_back({values[i], i});
	}

	qsort_decreasing(cells.data(), 0, num - 1);

	for (int i = 0; i < num; i++)
	{
		order[i] = cells[i].index;
	}
}


/************************************************************************
Load a XYTQ structure and return a XYT struct.
Row 3's value is an angle which is normalized to the interval (-180,180].
A maximum of MAX_BOZORTH_MINUTIAE minutiae can be returned -- fewer if
"max_minutiae" is smaller.  If the file contains more minutiae than are
to be returned, the highest-quality minutiae are returned.
*************************************************************************/
std::vector<Minutia> prune_minutiae(std::span<RawMinutia> minutiae, u32 max_minutiae)
{
	std::array<RawMinutia, MAX_FILE_MINUTIAE> m{};

	u32 length = static_cast<u32>(minutiae.size());
	for (u32 i = 0u; i < length; i++)
	{
		m[i].x = minutiae[i].x;
		m[i].y = minutiae[i].y;
		m[i].t = minutiae[i].t > 180
			         ? minutiae[i].t - 360
			         : minutiae[i].t;
		m[i].q = minutiae[i].q;
	}

	std::array<RawMinutia, MAX_FILE_MINUTIAE> c{};
	if (length > max_minutiae)
	{
		std::array<int, MAX_FILE_MINUTIAE> q{};
		for (u32 j = 0u; j < length; j++)
		{
			q[j] = minutiae[j].q;
		}

		std::array<int, MAX_FILE_MINUTIAE> order{};
		sort_order_decreasing(q.data(), static_cast<int>(length), order.data());

		for (u32 j = 0u; j < max_minutiae; j++)
		{
			c[j] = m[static_cast<unsigned int>(order[j])];
		}
		length = max_minutiae;
	}
	else
	{
		for (u32 j = 0u; j < length; j++)
		{
			c[j] = m[j];
		}
	}

	std::sort(c.begin(), c.begin() + static_cast<ptrdiff_t>(length), [](const auto& l, const auto& r)
	{
		if (l.x < r.x)
		{
			return true;
		}
		if (l.x > r.x)
		{
			return false;
		}
		return l.y < r.y;
	});

	std::vector<Minutia> xyt_s{};
	xyt_s.reserve(length);
	for (u32 j = 0u; j < length; j++)
	{
		xyt_s.emplace_back(Minutia{c[j].x, c[j].y, c[j].t, c[j].kind});
	}
	return xyt_s;
}


static void limit_edges(std::vector<Edge>& edges)
{
	const int calculated_limit = static_cast<const int>(limit_edges_by_length(edges));
	const int actual_limit = calculated_limit >= MIN_NUMBER_OF_EDGES
		                         ? calculated_limit
		                         : std::min<int>(static_cast<const int>(edges.size()), MIN_NUMBER_OF_EDGES);
	edges.erase(edges.begin() + actual_limit, edges.end());
}

std::optional<std::pair<std::vector<Minutia>, std::vector<Edge>>>
prepare_data(const std::string& file_name, u32 max_minutiae, Format mode)
{
	auto minutiae = load_minutiae(file_name, std::nullopt, max_minutiae);
	if (!minutiae.has_value())
	{
		std::cerr << "error: cannot load minutiae from file " << file_name << "\n";
		return std::nullopt;
	}

	std::vector<Edge> edges{};
	find_edges(minutiae.value(), edges, mode);
	limit_edges(edges);

	return std::make_pair(std::move(minutiae.value()), std::move(edges));
}

std::optional<std::pair<std::span<const Minutia>, std::span<const Edge>>>
cache_data(
	std::map<std::string, std::pair<std::vector<Minutia>, std::vector<Edge>>>& items,
	const std::string& file_name,
	u32 max_minutiae
)
{
	if (auto it = items.find(file_name); it != items.end()
	)
	{
		const auto& [minutiae, edges] = it->second;
		return std::make_pair(std::span(minutiae), std::span(edges));
	}

	if (auto value = prepare_data(file_name, max_minutiae); value.has_value()
	)
	{
		const auto& [minutiae, edges] = items[file_name] = value.value();
		return std::make_pair(std::span(minutiae), std::span(edges));
	}
	else
	{
		return std::nullopt;
	}
}

thread_local PairHolder pair_holder{};
thread_local BozorthState state{};

constexpr std::size_t MIN_COMPUTABLE_BOZORTH_MINUTIAE = 10;

u32 match(std::span<const Minutia> probe_minutiae, std::span<const Edge> probe_edges,
          std::span<const Minutia> gallery_minutiae, std::span<const Edge> gallery_edges, Format format)
{
	if (probe_minutiae.size() < MIN_COMPUTABLE_BOZORTH_MINUTIAE ||
		gallery_minutiae.size() < MIN_COMPUTABLE_BOZORTH_MINUTIAE)
	{
		return 0;
	}
	pair_holder.clear();
	match_edges_into_pairs(probe_edges, probe_minutiae, gallery_edges, gallery_minutiae,
	                       pair_holder);
	pair_holder.prepare();
	state.clear();
	return match_score(pair_holder, state, probe_minutiae, gallery_minutiae, format);
}
