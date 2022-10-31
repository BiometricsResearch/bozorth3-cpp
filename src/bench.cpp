//
// Created by Host on 25.10.2018.
//

#include <filesystem>
#include <chrono>
#include <iostream>
#include <fstream>
#include <map>
#include <cppitertools/enumerate.hpp>
#include "bozorth3/bozorth3.h"
#include "bozorth3/utils.hpp"
#include "utils.h"

using bz3::Format;
using bz3::find_edges;
using bz3::match_edges_into_pairs;
using bz3::limit_edges_by_length;

u32 max_minutiae = 150;

#ifdef _WIN32
const static std::string_view dir2_path = "E:/xxxx/backup/xyt";
const static std::string_view dir_path = "C:/Users/Host/Documents/all";
#else
const static std::string_view dir2_path = "/mnt/c/Users/Host/Downloads/drive-download-20181024T101143Z-001/Wyj/Wyj";
const static std::string_view dir_path = "/mnt/c/Users/Host/Downloads/drive-download-20181024T101143Z-001/XYT/XYT";
#endif

static void limit_edges(std::vector<Edge> &edges) {
    const u32 calculated_limit = bz3::limit_edges_by_length(edges);
    const u32 actual_limit = calculated_limit >= MIN_NUMBER_OF_EDGES
                             ? calculated_limit
                             : std::min(static_cast<u32>(edges.size()), static_cast<u32>(MIN_NUMBER_OF_EDGES));
    edges.erase(edges.begin() + actual_limit, edges.end());
    edges.shrink_to_fit();
}

struct Fingerprint {
    std::vector<Minutia> minutiae{};
    std::vector<Edge> edges{};
//    Edges soa_edges{0};
};

static Fingerprint &
my_cache_data(std::unordered_map<std::string, Fingerprint> &items, const std::string &file_name) {
    if (auto it = items.find(file_name); it != items.end()) {
        return it->second;
    }

    auto path = std::filesystem::path{dir2_path};
    path.append(file_name);
    const auto p = path.generic_string();
    auto p_min = p;
    p_min.erase(p_min.length() - 3);
    p_min += "min";

    auto minutia = load_minutiae(p, p_min, max_minutiae);
    if (!minutia.has_value()) {
        std::cout << path << "\n";
        printf("Cannot load!");
        exit(-1);
    }

    std::vector<Edge> edges{};
    find_edges(minutia.value(), edges, Format::NistInternal);
    limit_edges(edges);
//    Edges ev{edges.size()};
//    for (auto &item : edges) {
//        ev.push(item);
//    }

    auto &item = items[file_name];
    item.minutiae = std::move(minutia.value());
    item.edges = std::move(edges);
    return item;
}

using namespace std::chrono;

int main() {
    std::unordered_map<std::string, Fingerprint> items;

    std::filesystem::directory_iterator dir_iter{dir2_path};
    std::vector<std::string> paths{};
    for (const auto &entry : dir_iter) {
        auto f_path = entry.path().filename().generic_string();
        std::string file_name{};
        std::copy(f_path.begin(), f_path.end() - 3, std::back_inserter(file_name));
        file_name.append("xyt");
        my_cache_data(items, file_name);
        paths.push_back(file_name);
    }

    std::vector<u32> scores{};
    std::ifstream file{dir_path};
    assert(file.is_open());

    u32 score;
    std::string name;
    for (; file >> name >> name >> score;) {
        scores.push_back(score);
        name.clear();
    }

    std::cout << scores.size() << "\n";
    std::cout << paths.size() << "\n";

    auto start = high_resolution_clock::now();
    auto t1 = start;
    auto total = 0u;
    for (auto i = 0u; i < paths.size(); i++) {
        for (auto j = 0u; j < paths.size(); j++) {
            const auto &pfp = my_cache_data(items, paths[i]);
            const auto &gfp = my_cache_data(items, paths[j]);
            const u32 actual_score = match(
                    pfp.minutiae, pfp.edges,
                    gfp.minutiae, gfp.edges, Format::NistInternal
            );
            auto expected_score = scores[i * paths.size() + j];
            if (expected_score != actual_score) {
                std::cout << i << " "
                          << paths[i] << " "
                          << paths[j] << " "
                          << expected_score << " "
                          << actual_score << "\n";
            }

            if (total % 10000 == 0) {
                const auto t2 = high_resolution_clock::now();
                auto duration = duration_cast<microseconds>(t2 - t1).count();
                auto total_duration = duration_cast<milliseconds>(t2 - start).count();
                t1 = high_resolution_clock::now();

                std::cout << (duration / 10000) << " us/cmp -- "
                          << total << " "
                          << (static_cast<double>(total_duration) / 1000.0) << "\n";
            }

            total++;
        }
    }

    return 0;
}
