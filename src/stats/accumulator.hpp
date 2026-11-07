#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "filter/filter.hpp"
#include "stats/hyperloglog.hpp"

namespace hpla {

enum class HttpMethod : std::uint8_t {
    Get = 0,
    Post,
    Put,
    Delete,
    Patch,
    Head,
    Options,
    Other,
};

inline constexpr std::array<std::string_view, 8> kMethodNames{
    "GET", "POST", "PUT", "DELETE", "PATCH", "HEAD", "OPTIONS", "OTHER",
};

HttpMethod classify_method(std::string_view method) noexcept;
std::string_view method_name(HttpMethod method) noexcept;

struct StringHash {
    using is_transparent = void;
    std::size_t operator()(std::string_view text) const noexcept {
        return static_cast<std::size_t>(hash_bytes(text));
    }
    std::size_t operator()(const std::string& text) const noexcept {
        return static_cast<std::size_t>(hash_bytes(text));
    }
};

struct StringEqual {
    using is_transparent = void;
    bool operator()(std::string_view left, std::string_view right) const noexcept {
        return left == right;
    }
};

using CountMap = std::unordered_map<std::string, std::uint64_t, StringHash, StringEqual>;

struct FileInfo {
    std::string path;
    std::uint64_t size = 0;
};

struct CountEntry {
    std::string key;
    std::uint64_t count = 0;
};

// Per-worker statistics. Each worker owns one Accumulator, so the hot loop
// needs no locks; results are merged once all workers finish.
struct Accumulator {
    Accumulator(const FilterSet* filter, std::size_t max_distinct_keys);

    void add_line(std::string_view line);
    void merge(const Accumulator& other);

    const FilterSet* filter = nullptr;
    std::size_t max_distinct = 0;

    std::uint64_t lines_examined = 0;
    std::uint64_t parsed_records = 0;
    std::uint64_t malformed_records = 0;
    std::uint64_t matched_records = 0;

    std::uint64_t response_bytes = 0;
    std::uint64_t sized_records = 0;
    std::uint64_t min_response_size = UINT64_MAX;
    std::uint64_t max_response_size = 0;

    std::array<std::uint64_t, 500> status_counts{};   // index = status - 100
    std::array<std::uint64_t, 8> method_counts{};     // index = HttpMethod
    std::array<std::uint64_t, 5> status_class_counts{};  // 1xx..5xx

    CountMap ip_counts;
    CountMap path_counts;
    std::unordered_map<std::int64_t, std::uint64_t> minute_counts;
    HllSketch ip_cardinality;

    bool ip_map_limited = false;
    bool path_map_limited = false;
    bool counter_overflow = false;

    std::int64_t first_timestamp = 0;
    std::int64_t last_timestamp = 0;
    bool has_timestamps = false;
};

struct StatusEntry {
    std::uint16_t status = 0;
    std::uint64_t count = 0;
};

struct Statistics {
    std::uint64_t files_processed = 0;
    std::uint64_t bytes_processed = 0;

    std::uint64_t lines_examined = 0;
    std::uint64_t parsed_records = 0;
    std::uint64_t malformed_records = 0;
    std::uint64_t matched_records = 0;

    std::uint64_t response_bytes = 0;
    std::uint64_t sized_records = 0;
    std::uint64_t min_response_size = 0;
    std::uint64_t max_response_size = 0;
    bool has_response_size = false;

    std::array<std::uint64_t, 500> status_counts{};
    std::array<std::uint64_t, 8> method_counts{};
    std::array<std::uint64_t, 5> status_class_counts{};

    std::vector<CountEntry> top_ips;
    std::vector<CountEntry> top_paths;

    std::uint64_t unique_ips = 0;
    bool unique_ips_approximate = false;
    bool distinct_limit_reached = false;

    std::vector<CountEntry> busiest_minutes;  // key = ISO minute text, count = requests
    std::int64_t first_timestamp = 0;
    std::int64_t last_timestamp = 0;
    bool has_timestamps = false;
    double requests_per_minute = 0.0;

    bool counter_overflow = false;
};

struct Performance {
    double elapsed_seconds = 0.0;
    double throughput_bytes_per_second = 0.0;
    double lines_per_second = 0.0;
    std::uint64_t bytes_processed = 0;
    std::uint64_t lines_examined = 0;
    unsigned threads = 0;
    std::string io_mode;
    bool cancelled = false;
};

struct AnalysisResult {
    Statistics statistics;
    Performance performance;
};

// Merges per-worker accumulators and materializes the report structures.
Statistics build_statistics(const std::vector<Accumulator>& per_worker, std::size_t top_n,
                            std::uint64_t files_processed, std::uint64_t bytes_processed);

}  // namespace hpla
