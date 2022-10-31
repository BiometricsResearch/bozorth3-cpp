//
// Created by Dariusz Niedoba on 21.10.2018.
//

#pragma once

#include <span>
#include <bitset>
#include <vector>
#include "constants.h"
#include "pair_holder.h"

namespace bz3
{
	enum class Format
	{
		Ansi,
		NistInternal
	};

	u32 limit_edges_by_length(
		std::span<Edge> edges
	);

	void find_edges(std::span<const Minutia> minutiae, std::vector<Edge>& edges, Format format);

	void match_edges_into_pairs(std::span<const Edge> probe_edges, std::span<const Minutia> probe_minutiae,
	                            std::span<const Edge> gallery_edges, std::span<const Minutia> gallery_minutiae,
	                            PairHolder& pairs);

	struct ClusterAverages
	{
		int delta_theta;
		int probe_x;
		int probe_y;
		int gallery_x;
		int gallery_y;
	};

	struct Cluster
	{
		u32 points = 0;
		u32 points_from_compatible = 0;
		std::vector<u32> compatible;
	};

	struct ClusterEndpoints
	{
		std::bitset<MAX_BOZORTH_MINUTIAE> probe{};
		std::bitset<MAX_BOZORTH_MINUTIAE> gallery{};
	};

	struct Clusters
	{
		std::vector<Cluster> clusters{};
		std::vector<ClusterAverages> averages{};
		std::vector<ClusterEndpoints> endpoints{};

		void clear()
		{
			clusters.clear();
			averages.clear();
			endpoints.clear();
		}

		[[nodiscard]] std::size_t size() const
		{
			return clusters.size();
		}

		void push_back(Cluster&& cluster, ClusterAverages&& average, ClusterEndpoints&& endpoint)
		{
			clusters.push_back(cluster);
			averages.push_back(average);
			endpoints.push_back(endpoint);
		}
	};

	enum class EndpointType
	{
		Probe,
		Gallery
	};

	struct EndpointGroup
	{
		u32 endpoint{};
		EndpointType endpoint_type{};
		u32 endpoint_index{};
		std::vector<u32> endpoints;
		std::optional<u32> to_clear{};
	};

	struct BozorthState
	{
		Clusters clusters{};
		EndpointAssociator<MAX_NUMBER_OF_ENDPOINTS> associator{};
		ClusterAssigner<MAX_NUMBER_OF_PAIRS> cluster_assigner{};
		std::vector<EndpointGroup> groups{};
		std::vector<u32> selected_pairs{};

		void clear()
		{
			clusters.clear();
			associator.clear();
			cluster_assigner.clear();
			groups.clear();
			selected_pairs.clear();
		}
	};

	u32 match_score(const PairHolder& holder, BozorthState& state, std::span<const Minutia> probe_minutiae,
	                std::span<const Minutia> gallery_minutiae, Format format);
}
