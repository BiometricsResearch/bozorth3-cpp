//
// Created by Dariusz Niedoba on 21.12.2018.
//

#ifndef BZ_PAIRHOLDER_H
#define BZ_PAIRHOLDER_H

#include "constants.h"
#include "utils.hpp"
#include <algorithm>
#include <array>
#include <cppitertools/enumerate.hpp>
#include <iterator>
#include <vector>

template <typename T>
struct OptionalRange
{
	T begin;
	T end;

	[[nodiscard]] bool has_value() const
	{
		return begin != std::numeric_limits<T>::max() && end != std::numeric_limits<T>::max();
	}

	static OptionalRange empty()
	{
		return OptionalRange{
			.begin = std::numeric_limits<T>::max(),
			.end = std::numeric_limits<T>::max(),
		};
	}
};

class PairHolder
{
private:
	std::vector<Pair> forward_{};
	std::vector<u32> backward_{};
	std::array<OptionalRange<u32>, MAX_BOZORTH_MINUTIAE * MAX_BOZORTH_MINUTIAE> forward_cache_{};
	std::array<OptionalRange<u32>, MAX_BOZORTH_MINUTIAE * MAX_BOZORTH_MINUTIAE> backward_cache_{};
	bool dirty_ = false;

public:
	void add(Pair pair);

	[[nodiscard]] bool empty() const { return forward_.empty(); }

	void clear();

	void prepare();

	[[nodiscard]] std::span<const Pair> pairs() const
	{
		return std::span(forward_);
	}

	void find_pairs_by_second_endpoint(
		std::size_t offset,
		u32 probe_endpoint,
		u32 gallery_endpoint,
		const std::function<void(std::size_t, u32, u32)>& callback
	) const
	{
		assert(!dirty_);

		if (const auto range = backward_cache_[probe_endpoint * MAX_BOZORTH_MINUTIAE + gallery_endpoint]; range.
			has_value()
		)
		{
			const auto begin = backward_.cbegin() + static_cast<i32>(range.begin);
			const auto end = backward_.cbegin() + static_cast<i32>(range.end);

			for (auto item = begin; item != end; item++)
			{
				if (*item >= offset)
				{
					callback(*item, forward_[*item].probe_k, forward_[*item].gallery_k);
				}
			}
		}
	}

	size_t find_pairs_by_first_endpoint(
		std::size_t offset,
		u32 probe_endpoint,
		u32 gallery_endpoint,
		const std::function<void(std::size_t, u32, u32)>& callback
	) const
	{
		assert(!dirty_);

		Pair pair{};
		pair.probe_k = probe_endpoint;
		pair.gallery_k = gallery_endpoint;

		if (const auto range = forward_cache_[probe_endpoint * MAX_BOZORTH_MINUTIAE + gallery_endpoint]; range.
			has_value()
		)
		{
			const auto begin = forward_.cbegin() + static_cast<i32>(range.begin);
			const auto end = forward_.cbegin() + static_cast<i32>(range.end);

			for (auto item = begin; item != end; item++)
			{
				const auto index = static_cast<size_t>(std::distance(forward_.cbegin(), item));
				if (index >= offset)
				{
					callback(index, item->probe_j, item->gallery_j);
				}
			}

			return static_cast<std::size_t>(std::distance(forward_.cbegin(), end));
		}
		else
		{
			return offset;
		}
	}
};


#endif //BZ_PAIRHOLDER_H
