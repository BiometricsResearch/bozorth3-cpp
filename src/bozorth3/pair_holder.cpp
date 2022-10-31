//
// Created by Dariusz Niedoba on 21.12.2018.
//

#include <iostream>
#include "pair_holder.h"


void PairHolder::prepare()
{
	assert(!forward_.empty());
	assert(backward_.empty());

	if (!dirty_)
	{
		return;
	}

	std::stable_sort(forward_.begin(), forward_.end(), [](const Pair& left, const Pair& right)
	{
		if (left.probe_k < right.probe_k)
		{
			return true;
		}
		if (left.probe_k > right.probe_k)
		{
			return false;
		}
		if (left.gallery_k < right.gallery_k)
		{
			return true;
		}
		if (left.gallery_k > right.gallery_k)
		{
			return false;
		}
		return left.probe_j < right.probe_j;
	});

	{
		std::optional<std::tuple<u32, u32>> previous = std::nullopt;
		u32 range_start = 0;
		for (auto i = 0u; i < forward_.size(); i++)
		{
			const auto current = std::make_optional(std::make_tuple(forward_[i].probe_k, forward_[i].gallery_k));

			if (previous.has_value())
			{
				if (previous != current)
				{
					const auto [ppk, pgk] = previous.value();
					forward_cache_[ppk * MAX_BOZORTH_MINUTIAE + pgk] = OptionalRange<u32>{range_start, i};
					previous = current;
					range_start = i;
				}
			}
			else
			{
				previous = current;
			}
		}
		if (previous.has_value())
		{
			const auto [probe_k, gallery_k] = previous.value();
			forward_cache_[probe_k * MAX_BOZORTH_MINUTIAE + gallery_k] = OptionalRange<u32>{
				range_start, static_cast<u32>(forward_.size())
			};
		}
	}

	backward_.reserve(forward_.size());
	for (auto i = 0u; i < forward_.size(); i++)
	{
		backward_.push_back(i);
	}

	std::sort(backward_.begin(), backward_.end(), [&](const u32 left, const u32 right) -> bool
	{
		const auto& l = forward_[left];
		const auto& r = forward_[right];

		if (l.probe_j < r.probe_j)
		{
			return true;
		}
		if (l.probe_j > r.probe_j)
		{
			return false;
		}
		if (l.gallery_j < r.gallery_j)
		{
			return true;
		}
		if (l.gallery_j > r.gallery_j)
		{
			return false;
		}
		return left < right;
	});

	{
		std::optional<std::tuple<u32, u32>> previous = std::nullopt;
		u32 range_start = 0;
		for (auto i = 0u; i < backward_.size(); i++)
		{
			const auto current = std::make_optional(
				std::make_tuple(forward_[backward_[i]].probe_j, forward_[backward_[i]].gallery_j));

			if (previous.has_value())
			{
				if (previous != current)
				{
					const auto [ppj, pgj] = previous.value();
					backward_cache_[MAX_BOZORTH_MINUTIAE * ppj + pgj] = OptionalRange<u32>{range_start, i};
					previous = current;
					range_start = i;
				}
			}
			else
			{
				previous = current;
			}
		}
		if (previous.has_value())
		{
			const auto [pj, gj] = previous.value();
			backward_cache_[pj * MAX_BOZORTH_MINUTIAE + gj] = OptionalRange<u32>{
				range_start, static_cast<u32>(backward_.size())
			};
		}
	}

	dirty_ = false;
}

void PairHolder::clear()
{
	backward_.clear();
	forward_.clear();

	std::fill(forward_cache_.begin(), forward_cache_.end(), OptionalRange<u32>::empty());
	std::fill(backward_cache_.begin(), backward_cache_.end(), OptionalRange<u32>::empty());

	dirty_ = true;
}

void PairHolder::add(Pair pair)
{
	forward_.emplace_back(pair);
	dirty_ = true;
}
