//
// Created by Dariusz Niedoba on 06.10.2018.
//

#include <cstddef>
#include <cstdint>
#include <array>
#include <optional>
#include "types.h"
#include "math.h"

#ifndef BZ_BZ2_HPP
#define BZ_BZ2_HPP

template <std::size_t N>
class EndpointAssociator
{
private:
	std::array<u32, N> probe_by_gallery_{0};
	std::array<u32, N> gallery_by_probe_{0};

public:
	void associate_endpoints(u32 probe_endpoint, u32 gallery_endpoint)
	{
		probe_by_gallery_[gallery_endpoint] = probe_endpoint + 1;
		gallery_by_probe_[probe_endpoint] = gallery_endpoint + 1;
	}

	void clear_by_probe(u32 probe_endpoint)
	{
		if (const auto value = gallery_by_probe_[probe_endpoint]; value != 0
		)
		{
			probe_by_gallery_[value - 1] = 0;
			gallery_by_probe_[probe_endpoint] = 0;
		}
	}

	[[nodiscard]] std::optional<u32> get_associated_probe_endpoint(u32 gallery_endpoint) const
	{
		const auto endpoint = probe_by_gallery_[gallery_endpoint];
		if (endpoint == 0)
		{
			return {};
		}
		return std::make_optional(endpoint - 1);
	}

	[[nodiscard]] std::optional<u32> get_associated_gallery_endpoint(u32 probe_endpoint) const
	{
		const auto endpoint = gallery_by_probe_[probe_endpoint];
		if (endpoint == 0)
		{
			return {};
		}
		return std::make_optional(endpoint - 1);
	}

	[[nodiscard]] bool are_clear_or_mutually_associated(u32 probe_endpoint, u32 gallery_endpoint) const
	{
		const auto associated_gallery = gallery_by_probe_[probe_endpoint];
		const auto associated_probe = probe_by_gallery_[gallery_endpoint];
		if (associated_gallery == 0 && associated_probe == 0)
		{
			return true;
		}
		return associated_gallery == gallery_endpoint + 1 && associated_probe == probe_endpoint + 1;
	}

	void clear()
	{
		for (auto& value : probe_by_gallery_)
		{
			value = 0;
		}

		for (auto& value : gallery_by_probe_)
		{
			value = 0;
		}
	}
};

class AngleAverager
{
private:
	int sum_of_negative_ = 0;
	int number_of_negative_ = 0;
	int sum_of_positive_ = 0;
	int number_of_positive_ = 0;

public:
	void push(int value)
	{
		if (value < 0)
		{
			sum_of_negative_ += value;
			number_of_negative_++;
		}
		else
		{
			sum_of_positive_ += value;
			number_of_positive_++;
		}
	}

	[[nodiscard]] int average() const
	{
		const auto number_of_negative = number_of_negative_ == 0 ? 1 : number_of_negative_;
		const auto number_of_positive = number_of_positive_ == 0 ? 1 : number_of_positive_;
		const auto number_of_all = number_of_positive_ + number_of_negative_;
		float fi = static_cast<float>(sum_of_positive_) / static_cast<float>(number_of_positive) -
			static_cast<float>(sum_of_negative_) / static_cast<float>(number_of_negative);
		if (fi > 180.0F)
		{
			fi = (static_cast<float>(sum_of_positive_ + sum_of_negative_ + number_of_negative * 360)) / static_cast<
				float>(number_of_all);
			if (fi > 180.0F)
			{
				fi -= 360.0F;
			}
		}
		else
		{
			fi = (static_cast<float>(sum_of_positive_ + sum_of_negative_)) / static_cast<float>(number_of_all);
		}

		int average = rounded(fi);
		if (average <= -180)
		{
			average += 360;
		}
		return average;
	}
};

const u32 MARKER_UNASSIGNED = 0xFFFFFFFF;

template <std::size_t N>
class ClusterAssigner
{
private:
	std::array<i32, N> cluster_by_pair_{0};

public:
	[[nodiscard]] std::optional<u32> get_cluster(u32 pair_index) const
	{
		const int cluster = cluster_by_pair_[pair_index];
		if (cluster == 0)
		{
			return {};
		}
		return std::make_optional(cluster - 1);
	}

	[[nodiscard]] bool has_cluster(u32 pair_index, u32 cluster) const
	{
		return cluster_by_pair_[pair_index] == static_cast<i32>(cluster + 1);
	}

	void assign_cluster(u32 pair_index, u32 cluster)
	{
		cluster_by_pair_[pair_index] = cluster + 1;
	}

	void restore(u32 pair_index)
	{
		cluster_by_pair_[pair_index] = MARKER_UNASSIGNED;
	}

	void clear()
	{
		for (auto& value : cluster_by_pair_)
		{
			value = 0;
		}
	}
};

#endif //BZ_BZ2_HPP
