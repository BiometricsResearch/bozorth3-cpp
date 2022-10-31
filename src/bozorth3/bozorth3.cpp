#include "constants.h"
#include "utils.hpp"
#include <cppitertools/itertools.hpp>
#include <optional>
#include <vector>
#include <bitset>
#include <numeric>
#include "bozorth3.h"

namespace bz3
{
	static int average_angles(int angle1, int angle2)
	{
		AngleAverager averager{};
		averager.push(angle1);
		averager.push(angle2);
		return averager.average();
	}

	void find_edges(
		std::span<const Minutia> minutiae,
		std::vector<Edge>& edges, Format format
	)
	{
		for (u32 k = 0; k < minutiae.size() - 1; k++)
		{
			for (u32 j = k + 1; j < minutiae.size(); j++)
			{
				if (check(minutiae[k].t, minutiae[j].t))
				{
					continue;
				}

				const int dx = minutiae[j].x - minutiae[k].x;
				const int dy = minutiae[j].y - minutiae[k].y;
				const int distance_squared = squared(dx) + squared(dy);
				if (distance_squared > squared(MAX_MINUTIA_DISTANCE))
				{
					if (dx > MAX_MINUTIA_DISTANCE)
					{
						break;
					}
					continue;
				}

				const int theta_kj = atan2_round_degree(dx, format == Format::Ansi ? -dy : dy);
				const int beta_k = normalize_angle(theta_kj - minutiae[k].t);
				const int beta_j = normalize_angle(theta_kj - minutiae[j].t + 180);

				Edge edge{};
				if (beta_k < beta_j)
				{
					edge.distance_squared = distance_squared;
					edge.min_beta = beta_k;
					edge.max_beta = beta_j;
					edge.endpoint_k = k;
					edge.endpoint_j = j;
					edge.theta_kj = theta_kj;
					edge.beta_order = KJ;
				}
				else
				{
					edge.distance_squared = distance_squared;
					edge.min_beta = beta_j;
					edge.max_beta = beta_k;
					edge.endpoint_k = k;
					edge.endpoint_j = j;
					edge.theta_kj = theta_kj;
					edge.beta_order = JK;
				}

				edges.push_back(edge);
				if (edges.size() == MAX_NUMBER_OF_EDGES - 1)
				{
					goto sort;
				}
			}
		}

	sort:
		std::stable_sort(edges.begin(), edges.end(), [](const Edge& left, const Edge& right)
		{
			if (left.distance_squared < right.distance_squared)
			{
				return true;
			}
			if (left.distance_squared > right.distance_squared)
			{
				return false;
			}
			if (left.min_beta < right.min_beta)
			{
				return true;
			}
			if (left.min_beta > right.min_beta)
			{
				return false;
			}
			return left.max_beta < right.max_beta;
		});
	}

	u32 limit_edges_by_length(std::span<Edge> edges)
	{
		u32 lower = 0;
		u32 upper = static_cast<u32>(edges.size() + 1);
		u32 current = 1;

		while (upper - lower > 1)
		{
			u32 midpoint = (lower + upper) / 2;
			if (edges[midpoint - 1].distance_squared > MAX_MINUTIA_DISTANCE_SQUARED)
			{
				upper = midpoint;
			}
			else
			{
				lower = midpoint;
				current = midpoint + 1;
			}
		}

		return std::min(current, static_cast<u32>(edges.size()));
	}

	static inline bool are_angles_equal_with_tolerance(int a, int b)
	{
		const int difference = abs(a - b);
		return !(difference > ANGLE_LOWER_BOUND && difference < ANGLE_UPPER_BOUND);
	}

	void match_edges_into_pairs(std::span<const Edge> probe_edges, std::span<const Minutia> probe_minutiae,
	                            std::span<const Edge> gallery_edges, std::span<const Minutia> gallery_minutiae,
	                            PairHolder& pairs)
	{
		assert(!probe_edges.empty());
		assert(!gallery_edges.empty());

		u32 start = 0;
		// CHECKME: raczej nie powinno pomijać ostatniej pary...
		for (u32 k = 0; k < probe_edges.size() - 1; k++)
		{
			const Edge& probe = probe_edges[k];

			for (u32 j = start; j < gallery_edges.size(); j++)
			{
				const Edge& gallery = gallery_edges[j];

				int dz = gallery.distance_squared - probe.distance_squared;
				const float fi = (2.0f * FACTOR) * static_cast<float>(gallery.distance_squared + probe.
					distance_squared);
				if (static_cast<float>(std::abs(dz)) > fi)
				{
					if (dz < 0)
					{
						start = j + 1;
						continue;
					}
					break;
				}

				if (!(are_angles_equal_with_tolerance(probe.min_beta, gallery.min_beta) &&
					are_angles_equal_with_tolerance(probe.max_beta, gallery.max_beta)))
				{
					continue;
				}

				int delta_theta = probe.theta_kj - gallery.theta_kj;
				if (probe.beta_order != gallery.beta_order)
				{
					delta_theta -= 180;
				}

				Pair pair{};
				pair.delta_theta = normalize_angle(delta_theta);
				pair.probe_k = probe.endpoint_k;
				pair.probe_j = probe.endpoint_j;

				if (probe.beta_order != gallery.beta_order)
				{
					pair.gallery_k = gallery.endpoint_j;
					pair.gallery_j = gallery.endpoint_k;
				}
				else
				{
					pair.gallery_k = gallery.endpoint_k;
					pair.gallery_j = gallery.endpoint_j;
				}

				const auto pkk = probe_minutiae[pair.probe_k].kind;
				const auto pkj = probe_minutiae[pair.probe_j].kind;
				const auto gkk = gallery_minutiae[pair.gallery_k].kind;
				const auto gkj = gallery_minutiae[pair.gallery_j].kind;

				if (pkk.has_value() && pkj.has_value() && gkk.has_value() && gkj.has_value())
				{
					const auto matching = static_cast<u32>(pkk == gkk) + static_cast<u32>(pkj == gkj);
					switch (matching)
					{
					case 0:
						pair.points = 1;
						break;
					case 1:
						pair.points = 2;
						break;
					case 2:
						pair.points = 3;
						break;
					default:
						break;
					}
				}
				else
				{
					pair.points = 1;
				}
				pairs.add(pair);
			}
		}
	}

	static std::pair<u32, u32> get_current_pair(const EndpointGroup& group)
	{
		u32 probe_endpoint;
		u32 gallery_endpoint;

		if (group.endpoint_type == EndpointType::Gallery)
		{
			probe_endpoint = group.endpoints[group.endpoint_index];
			gallery_endpoint = group.endpoint;
		}
		else
		{
			probe_endpoint = group.endpoint;
			gallery_endpoint = group.endpoints[group.endpoint_index];
		}

		return std::make_pair(probe_endpoint, gallery_endpoint);
	}

	static bool associate_endpoints_of_all_groups(
		std::span<EndpointGroup> groups,
		EndpointAssociator<MAX_NUMBER_OF_ENDPOINTS>& associator
	)
	{
		for (i32 group_index = static_cast<i32>(groups.size() - 1); group_index >= 0; group_index--)
		{
			const auto [probe_endpoint, gallery_endpoint] = get_current_pair(groups[static_cast<u32>(group_index)]);

			if (associator.are_clear_or_mutually_associated(probe_endpoint, gallery_endpoint))
			{
				associator.associate_endpoints(probe_endpoint, gallery_endpoint);
				groups[static_cast<u32>(group_index)].to_clear = std::make_optional(probe_endpoint);
			}
			else
			{
				for (u32 i = static_cast<u32>(group_index + 1); i < groups.size(); i++)
				{
					const auto old_probe = std::exchange(groups[i].to_clear, std::nullopt);
					if (old_probe.has_value())
					{
						associator.clear_by_probe(old_probe.value());
					}
				}

				return false;
			}
		}
		return true;
	}

	static bool try_associate_ambiguous_endpoints(
		std::span<EndpointGroup> groups,
		EndpointAssociator<MAX_NUMBER_OF_ENDPOINTS>& associator
	)
	{
		for (auto group = groups.rbegin(); group != groups.rend();)
		{
			// pomijamy pierwszy endpoint, bo został już powiązany, gdy nie było jeszcze grupy
			if (group->endpoint_index + 1 < group->endpoints.size())
			{
				group->endpoint_index++;

				if (associate_endpoints_of_all_groups(groups, associator))
				{
					return true;
				}

				group = groups.rbegin();
			}
			else
			{
				group->endpoint_index = 0;
				group++;
			}
		}
		return false;
	}


	template <Format F>
	static bool are_clusters_compatible(const ClusterAverages& averages1, const ClusterAverages& averages2)
	{
		if (!are_angles_equal_with_tolerance(averages2.delta_theta, averages1.delta_theta))
		{
			return false;
		}

		const int probe_dx = averages2.probe_x - averages1.probe_x;
		const int probe_dy = averages2.probe_y - averages1.probe_y;
		const int gallery_dx = averages2.gallery_x - averages1.gallery_x;
		const int gallery_dy = averages2.gallery_y - averages1.gallery_y;

		const auto probe_distance_squared = static_cast<float>(squared(probe_dx) + squared(probe_dy));
		const auto gallery_distance_squared = static_cast<float>(squared(gallery_dy) + squared(gallery_dx));

		const auto a = (2.0F * FACTOR) * (probe_distance_squared + gallery_distance_squared);
		const auto b = std::fabsf(probe_distance_squared - gallery_distance_squared);
		if (b > a)
		{
			return false;
		}

		const int average = average_angles(averages1.delta_theta, averages2.delta_theta);
		const int difference = F == Format::Ansi
			                       ? calculate_slope_in_degrees(probe_dx, -probe_dy) -
			                       calculate_slope_in_degrees(gallery_dx, -gallery_dy)
			                       : calculate_slope_in_degrees(probe_dx, probe_dy) -
			                       calculate_slope_in_degrees(gallery_dx, gallery_dy);
		return are_angles_equal_with_tolerance(average, normalize_angle(difference));
	}

	inline static bool have_common_endpoints(const ClusterEndpoints& first, const ClusterEndpoints& second)
	{
		return ((first.probe & second.probe) | (first.gallery & second.gallery)).any();
	}

	template <Format M>
	static void merge_compatible_clusters(Clusters& clusters)
	{
		for (auto cluster = 0u; cluster < clusters.size(); ++cluster)
		{
			u32 points_from_others = 0;
			std::vector<u32> compatible_clusters{};

			for (auto other_cluster = cluster + 1; other_cluster < clusters.size(); ++other_cluster)
			{
				if (have_common_endpoints(clusters.endpoints[cluster], clusters.endpoints[other_cluster]))
				{
					continue;
				}

				if (!are_clusters_compatible<M>(clusters.averages[cluster], clusters.averages[other_cluster]))
				{
					continue;
				}

				points_from_others += clusters.clusters[other_cluster].points;
				compatible_clusters.push_back(other_cluster);
			}

			clusters.clusters[cluster].points_from_compatible = clusters.clusters[cluster].points + points_from_others;
			clusters.clusters[cluster].compatible = std::move(compatible_clusters);
		}
	}

	static ClusterAverages calculate_averages(
		std::span<const Minutia> probe_minutiae,
		std::span<const Minutia> gallery_minutiae,
		std::span<const Pair> pairs,
		std::span<u32> selected_pairs
	)
	{
		ClusterAverages average{};
		AngleAverager averager{};

		for (const auto pair_index : selected_pairs)
		{
			const auto& pair = pairs[pair_index];
			averager.push(pair.delta_theta);

			const u32 probe_endpoint = pair.probe_k;
			average.probe_x += probe_minutiae[probe_endpoint].x;
			average.probe_y += probe_minutiae[probe_endpoint].y;

			const u32 gallery_endpoint = pair.gallery_k;
			average.gallery_x += gallery_minutiae[gallery_endpoint].x;
			average.gallery_y += gallery_minutiae[gallery_endpoint].y;
		}

		average.delta_theta = averager.average();
		average.probe_x /= selected_pairs.size();
		average.probe_y /= selected_pairs.size();
		average.gallery_x /= selected_pairs.size();
		average.gallery_y /= selected_pairs.size();

		return average;
	}

	static ClusterEndpoints encode_endpoints(
		std::span<const Pair> pairs,
		std::span<const u32> selected_pairs
	)
	{
		ClusterEndpoints endpoints{};
		for (const auto idx : selected_pairs)
		{
			const auto& pair = pairs[idx];
			endpoints.probe.set(pair.probe_k);
			endpoints.probe.set(pair.probe_j);
			endpoints.gallery.set(pair.gallery_k);
			endpoints.gallery.set(pair.gallery_j);
		}
		return endpoints;
	}

	static void cleanup_selected(
		ClusterAssigner<MAX_NUMBER_OF_PAIRS>& cluster_assigner,
		std::span<u32> selected_pairs
	)
	{
		for (const auto pair : selected_pairs)
		{
			cluster_assigner.restore(pair);
		}
	}

	static int calculate_average_delta_theta_for_pairs(
		std::span<u32> selected_pairs,
		std::span<const Pair> pairs
	)
	{
		AngleAverager averager{};
		for (const auto pair : selected_pairs)
		{
			averager.push(pairs[pair].delta_theta);
		}
		return averager.average();
	}

	static void filter_selected(
		std::vector<u32>& selected_pairs,
		std::span<const Pair> pairs
	)
	{
		const auto average = calculate_average_delta_theta_for_pairs(selected_pairs, pairs);
		const auto begin = std::remove_if(
			selected_pairs.begin(),
			selected_pairs.end(),
			[average, &pairs](const u32 pair_index)
			{
				return !are_angles_equal_with_tolerance(pairs[pair_index].delta_theta, average);
			});
		selected_pairs.erase(begin, selected_pairs.end());
	}

	static void associate_multiple_compatible_endpoints(
		EndpointType endpoint_type,
		u32 endpoint,
		u32 existing_endpoint,
		u32 new_endpoint,
		std::vector<EndpointGroup>& groups
	)
	{
		assert(existing_endpoint != new_endpoint);

		auto existing_group = std::find_if(groups.begin(), groups.end(), [=](const EndpointGroup& group)
		{
			return group.endpoint_type == endpoint_type && group.endpoint == endpoint;
		});

		if (existing_group != groups.end())
		{
			auto& group = *existing_group;
			if (std::find(group.endpoints.begin(), group.endpoints.end(), new_endpoint) == group.endpoints.end())
			{
				group.endpoints.push_back(new_endpoint);
			}
		}
		else
		{
			EndpointGroup group;
			group.endpoint_type = endpoint_type;
			group.endpoint = endpoint;
			group.endpoint_index = 0;
			group.endpoints = {existing_endpoint, new_endpoint};
			groups.push_back(std::move(group));
		}
	}

	static void assign_cluster_to_endpoints(
		const u32 cluster,
		const u32 pair_index,
		const u32 probe_endpoint,
		const u32 gallery_endpoint,
		std::vector<EndpointGroup>& groups,
		ClusterAssigner<MAX_NUMBER_OF_PAIRS>& assigner,
		EndpointAssociator<MAX_NUMBER_OF_ENDPOINTS>& associator,
		std::vector<u32>& endpoints,
		std::vector<u32>& selected_pairs
	)
	{
		const auto associated_gallery_endpoint = associator.get_associated_gallery_endpoint(probe_endpoint);
		const auto associated_probe_endpoint = associator.get_associated_probe_endpoint(gallery_endpoint);

		// żaden z endpointów nie ma przypisanego odpowiadającego endpointa
		if (!associated_gallery_endpoint.has_value() && !associated_probe_endpoint.has_value())
		{
			if (!assigner.has_cluster(pair_index, cluster))
			{
				selected_pairs.push_back(pair_index);
				assigner.assign_cluster(pair_index, cluster);
			}

			endpoints.push_back(probe_endpoint);
			associator.associate_endpoints(probe_endpoint, gallery_endpoint);
			return;
		}

		// zostały już wcześniej powiązane
		if (associated_gallery_endpoint.has_value() && associated_gallery_endpoint.value() == gallery_endpoint)
		{
			if (assigner.has_cluster(pair_index, cluster))
			{
				return;
			}

			selected_pairs.push_back(pair_index);
			assigner.assign_cluster(pair_index, cluster);
			// CHECKME: prawdopodobnie powinno byc probe_endpoint zamiast pair_index
			if (std::find(endpoints.begin(), endpoints.end(), pair_index) == endpoints.end())
			{
				endpoints.push_back(probe_endpoint);
			}
			return;
		}

		if (groups.size() >= MAX_NUMBER_OF_GROUPS)
		{
			return;
		}

		if (associated_gallery_endpoint.has_value())
		{
			associate_multiple_compatible_endpoints(
				EndpointType::Probe, probe_endpoint,
				associated_gallery_endpoint.value(), gallery_endpoint,
				groups);
		}

		if (associated_probe_endpoint.has_value())
		{
			associate_multiple_compatible_endpoints(
				EndpointType::Gallery, gallery_endpoint,
				associated_probe_endpoint.value(), probe_endpoint,
				groups);
		}
	}

	static void find_pairs(const PairHolder& pair_holder, const u32 start_pair, const u32 cluster_index,
	                       std::vector<EndpointGroup>& groups, std::vector<u32>& selected_pairs,
	                       EndpointAssociator<MAX_NUMBER_OF_ENDPOINTS>& associator,
	                       ClusterAssigner<MAX_NUMBER_OF_PAIRS>& cluster_assigner)
	{
		std::vector<u32> endpoints{};

		const auto assign = [&](u32 pair, u32 probe_endpoint, u32 gallery_endpoint)
		{
			assign_cluster_to_endpoints(
				cluster_index,
				pair,
				probe_endpoint,
				gallery_endpoint,
				groups,
				cluster_assigner,
				associator,
				endpoints,
				selected_pairs
			);
		};

		const auto& start = pair_holder.pairs()[start_pair];
		const auto next_not_connected = pair_holder.find_pairs_by_first_endpoint(
			start_pair,
			start.probe_k, start.gallery_k,
			[&](auto index, auto probe_j, auto gallery2)
			{
				assign(static_cast<u32>(index), probe_j, gallery2);
			}
		);

		for (auto i = 0u; i < endpoints.size(); i++)
		{
			// NOLINT(modernize-loop-convert)
			const auto probe_endpoint = endpoints[i];
			const auto gallery_endpoint = associator.get_associated_gallery_endpoint(probe_endpoint).value();

			pair_holder.find_pairs_by_second_endpoint(
				next_not_connected, probe_endpoint, gallery_endpoint,
				[&](auto index, auto probe_k, auto gallery1)
				{
					// zabezpieczenie przed tworzeniem cykli???
					if (probe_k != start.probe_k && gallery1 != start.gallery_k)
					{
						assign(index, probe_k, gallery1);
					}
				}
			);

			pair_holder.find_pairs_by_first_endpoint(
				next_not_connected, probe_endpoint, gallery_endpoint,
				[&](auto index, auto probe_j, auto gallery2)
				{
					assign(index, probe_j, gallery2);
				});
		}

		for (const auto endpoint : endpoints)
		{
			associator.clear_by_probe(endpoint);
		}
	}

	static u32 combine_clusters(std::span<Cluster> clusters)
	{
		struct Item
		{
			u32 cluster{};
			u32 index{};
			std::vector<u32> connected{};
		};

		std::vector<Item> items{};
		u32 best_score = 0;

		for (const auto [cluster_index, cluster] : iter::enumerate(clusters))
		{
			if (best_score >= cluster.points_from_compatible)
			{
				continue;
			}

			items.emplace_back(Item{
				.cluster = static_cast<u32>(cluster_index),
				.index = 0,
				.connected = cluster.compatible
			});

			while (!items.empty())
			{
				const auto& last = items.back();
				if (last.index < last.connected.size())
				{
					const auto next_cluster = last.connected.at(last.index);
					const auto& compatible = clusters[next_cluster].compatible;

					std::vector<u32> connected_clusters{};
					std::set_intersection(
						last.connected.cbegin(), last.connected.cend(),
						compatible.cbegin(), compatible.cend(),
						std::back_inserter(connected_clusters)
					);

					items.push_back(Item{next_cluster, 0, connected_clusters});
				}
				else
				{
					if (last.connected.empty())
					{
						const u32 score = std::accumulate(
							items.cbegin(), items.cend(),
							0u, [&clusters](u32 total, const Item& item)
							{
								return total + clusters[item.cluster].points;
							}
						);

						if (score > best_score)
						{
							best_score = score;
						}
					}

					items.pop_back();
					if (!items.empty())
					{
						items.back().index += 1;
					}
				}
			}
		}

		return best_score;
	}

	static inline u32 calculate_points(
		const std::span<u32> selected,
		const std::span<const Pair> pairs
	)
	{
		return std::accumulate(selected.begin(), selected.end(), 0u, [&pairs](const auto total, const auto index)
		{
			return total + pairs[index].points;
		});
	}


	template <Format F>
	static u32
	match_score(
		const PairHolder& pair_holder,
		BozorthState& state,
		std::span<const Minutia> probe_minutia,
		std::span<const Minutia> gallery_minutia
	)
	{
		assert(!pair_holder.empty());

		// XXX: ostatnia para jest pomijana
		for (u32 pair_index = 0; pair_index < pair_holder.pairs().size() - 1; pair_index++)
		{
			if (state.cluster_assigner.get_cluster(pair_index).has_value())
			{
				continue;
			}

			const auto probe_k = pair_holder.pairs()[pair_index].probe_k;
			const auto gallery1 = pair_holder.pairs()[pair_index].gallery_k;
			state.associator.associate_endpoints(probe_k, gallery1);

			state.groups.clear();
			while (true)
			{
				const auto number_of_old_groups = state.groups.size();
				const auto new_cluster_index = static_cast<u32>(state.clusters.size());

				state.selected_pairs.clear();
				find_pairs(pair_holder, pair_index,
				           new_cluster_index, state.groups,
				           state.selected_pairs, state.associator, state.cluster_assigner
				);

				if (state.selected_pairs.size() >= MIN_NUMBER_OF_PAIRS_TO_CLUSTER)
				{
					filter_selected(state.selected_pairs, pair_holder.pairs());
				}

				if (state.selected_pairs.size() < MIN_NUMBER_OF_PAIRS_TO_CLUSTER)
				{
					cleanup_selected(state.cluster_assigner, state.selected_pairs);
				}
				else
				{
					const auto points = calculate_points(std::span(state.selected_pairs), pair_holder.pairs());
					state.clusters.push_back(
						Cluster{
							.points = points,
							.points_from_compatible = points,
							.compatible = {}
						},
						calculate_averages(probe_minutia, gallery_minutia, pair_holder.pairs(), state.selected_pairs),
						encode_endpoints(pair_holder.pairs(), state.selected_pairs)
					);
				}

				if (state.clusters.size() > MAX_NUMBER_OF_CLUSTERS - 1)
				{
					break;
				}

				for (auto i = 0u; i < number_of_old_groups; i++)
				{
					if (state.groups[i].to_clear.has_value())
					{
						state.associator.clear_by_probe(state.groups[i].to_clear.value());
					}
				}

				if (!try_associate_ambiguous_endpoints(state.groups, state.associator))
				{
					break;
				}
			}

			if (state.clusters.size() > MAX_NUMBER_OF_CLUSTERS - 1)
			{
				break;
			}

			state.associator.clear_by_probe(probe_k);
		}

		merge_compatible_clusters<F>(state.clusters);

		u32 match_score = 0;
		for (const auto& cluster : state.clusters.clusters)
		{
			match_score = std::max(match_score, cluster.points_from_compatible);
		}

		if (match_score < SCORE_THRESHOLD)
		{
			return match_score;
		}
		return combine_clusters(state.clusters.clusters);
	}


	u32 match_score(
		const PairHolder& holder,
		BozorthState& state,
		std::span<const Minutia> probe_minutiae,
		std::span<const Minutia> gallery_minutiae,
		Format format
	)
	{
		if (holder.empty())
		{
			return 0;
		}
		state.clear();

		return format == Format::Ansi
			       ? match_score<Format::Ansi>(holder, state, probe_minutiae, gallery_minutiae)
			       : match_score<Format::NistInternal>(holder, state, probe_minutiae, gallery_minutiae);
	}
}
