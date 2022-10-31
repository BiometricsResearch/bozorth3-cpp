//
// Created by Dariusz Niedoba on 08.01.2019.
//

#ifndef BZ_UTILS_H
#define BZ_UTILS_H

#include <memory>
#include <map>
#include <iostream>
#include "bozorth3/bozorth3.h"


struct RawMinutia
{
	int x{};
	int y{};
	int t{};
	int q{};
	std::optional<MinutiaKind> kind{};
};

std::optional<std::vector<Minutia>> load_minutiae(
	std::string_view xyt_path,
	std::optional<std::string_view> min_path,
	u32 max_minutiae
);

std::vector<Minutia> prune_minutiae(std::span<RawMinutia> minutiae, u32 max_minutiae);

u32 match(std::span<const Minutia> probe_minutiae, std::span<const Edge> probe_edges,
          std::span<const Minutia> gallery_minutiae, std::span<const Edge> gallery_edges, bz3::Format format);

std::optional<std::pair<std::vector<Minutia>, std::vector<Edge>>>
prepare_data(const std::string& file_name, u32 max_minutiae, bz3::Format mode = bz3::Format::NistInternal);

std::optional<std::pair<std::span<const Minutia>, std::span<const Edge>>>
cache_data(std::map<std::string, std::pair<std::vector<Minutia>, std::vector<Edge>>>& items,
           const std::string& file_name, u32 max_minutiae);


#endif //BZ_UTILS_H
