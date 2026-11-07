#include "stats/accumulator.hpp"

#include <algorithm>
#include <limits>
#include <string>
#include <utility>

#include "core/timestamp.hpp"
#include "parser/log_parser.hpp"

namespace hpla {
namespace {

void sat_add(std::uint64_t& target, std::uint64_t value, bool& overflow) {
    if (target > std::numeric_limits<std::uint64_t>::max() - value) {
        target = std::numeric_limits<std::uint64_t>::max();
        overflow = true;
    } else {
        target += value;
    }
}

std::int64_t floor_div(std::int64_t value, std::int64_t divisor) {
    std::int64_t quotient = value / divisor;
    if (value % divisor != 0 && ((value < 0) != (divisor < 0))) {
        --quotient;
    }
    return quotient;
}

void accumulate_key(CountMap& map, std::string_view key, std::size_t max_distinct, bool& limited) {
    const auto existing = map.find(key);
    if (existing != map.end()) {
        ++existing->second;
        return;
    }
    if (max_distinct != 0 && map.size() >= max_distinct) {
        limited = true;
        return;
    }
    map.emplace(std::string(key), 1);
}

std::vector<CountEntry> top_entries(const CountMap& map, std::size_t limit) {
    std::vector<CountEntry> entries;
    entries.reserve(map.size());
    for (const auto& [key, count] : map) {
        entries.push_back(CountEntry{key, count});
    }
    std::sort(entries.begin(), entries.end(), [](const CountEntry& left, const CountEntry& right) {
        if (left.count != right.count) {
            return left.count > right.count;
        }
        return left.key < right.key;
    });
    if (entries.size() > limit) {
        entries.resize(limit);
    }
    return entries;
}

std::vector<CountEntry> busiest_minutes(const std::unordered_map<std::int64_t, std::uint64_t>& minutes,
                                        std::size_t limit) {
    std::vector<CountEntry> entries;
    entries.reserve(minutes.size());
    for (const auto& [minute, count] : minutes) {
        entries.push_back(CountEntry{format_iso8601_utc(minute * 60), count});
    }
    std::sort(entries.begin(), entries.end(), [](const CountEntry& left, const CountEntry& right) {
        if (left.count != right.count) {
            return left.count > right.count;
        }
        return left.key < right.key;
    });
    if (entries.size() > limit) {
        entries.resize(limit);
    }
    return entries;
}

}  // namespace

HttpMethod classify_method(std::string_view method) noexcept {
    if (method.size() == 3) {
        if (method == "GET") {
            return HttpMethod::Get;
        }
        if (method == "PUT") {
            return HttpMethod::Put;
        }
    } else if (method.size() == 4) {
        if (method == "POST") {
            return HttpMethod::Post;
        }
        if (method == "HEAD") {
            return HttpMethod::Head;
        }
    } else if (method.size() == 5) {
        if (method == "PATCH") {
            return HttpMethod::Patch;
        }
    } else if (method.size() == 6) {
        if (method == "DELETE") {
            return HttpMethod::Delete;
        }
    } else if (method.size() == 7) {
        if (method == "OPTIONS") {
            return HttpMethod::Options;
        }
    }
    return HttpMethod::Other;
}

std::string_view method_name(HttpMethod method) noexcept {
    return kMethodNames[static_cast<std::size_t>(method)];
}

Accumulator::Accumulator(const FilterSet* filter, std::size_t max_distinct_keys)
    : filter(filter), max_distinct(max_distinct_keys) {}

void Accumulator::add_line(std::string_view line) {
    ++lines_examined;

    LogEntry entry;
    const ParseResult parsed = parse_log_line(line, entry);
    if (!parsed.ok) {
        ++malformed_records;
        return;
    }
    ++parsed_records;

    if (filter != nullptr && !filter->matches(entry)) {
        return;
    }
    ++matched_records;

    const std::uint16_t status = entry.status;
    ++status_counts[status - 100];
    ++status_class_counts[static_cast<std::size_t>(status / 100 - 1)];
    ++method_counts[static_cast<std::size_t>(classify_method(entry.method))];

    if (entry.has_response_bytes) {
        ++sized_records;
        sat_add(response_bytes, entry.response_bytes, counter_overflow);
        min_response_size = std::min(min_response_size, entry.response_bytes);
        max_response_size = std::max(max_response_size, entry.response_bytes);
    }

    const std::int64_t minute = floor_div(entry.timestamp_epoch, 60);
    ++minute_counts[minute];

    if (!has_timestamps) {
        first_timestamp = entry.timestamp_epoch;
        last_timestamp = entry.timestamp_epoch;
        has_timestamps = true;
    } else {
        first_timestamp = std::min(first_timestamp, entry.timestamp_epoch);
        last_timestamp = std::max(last_timestamp, entry.timestamp_epoch);
    }

    accumulate_key(ip_counts, entry.client_ip, max_distinct, ip_map_limited);
    if (max_distinct != 0) {
        // Only needed when the exact map may stop accepting new keys.
        ip_cardinality.add(entry.client_ip);
    }
    if (!entry.target.empty()) {
        accumulate_key(path_counts, entry.target, max_distinct, path_map_limited);
    }
}

void Accumulator::merge(const Accumulator& other) {
    sat_add(lines_examined, other.lines_examined, counter_overflow);
    sat_add(parsed_records, other.parsed_records, counter_overflow);
    sat_add(malformed_records, other.malformed_records, counter_overflow);
    sat_add(matched_records, other.matched_records, counter_overflow);

    sat_add(response_bytes, other.response_bytes, counter_overflow);
    sat_add(sized_records, other.sized_records, counter_overflow);
    min_response_size = std::min(min_response_size, other.min_response_size);
    max_response_size = std::max(max_response_size, other.max_response_size);

    for (std::size_t i = 0; i < status_counts.size(); ++i) {
        sat_add(status_counts[i], other.status_counts[i], counter_overflow);
    }
    for (std::size_t i = 0; i < method_counts.size(); ++i) {
        sat_add(method_counts[i], other.method_counts[i], counter_overflow);
    }
    for (std::size_t i = 0; i < status_class_counts.size(); ++i) {
        sat_add(status_class_counts[i], other.status_class_counts[i], counter_overflow);
    }

    for (const auto& [key, count] : other.ip_counts) {
        auto [position, inserted] = ip_counts.try_emplace(key, count);
        if (!inserted) {
            sat_add(position->second, count, counter_overflow);
        }
    }
    for (const auto& [key, count] : other.path_counts) {
        auto [position, inserted] = path_counts.try_emplace(key, count);
        if (!inserted) {
            sat_add(position->second, count, counter_overflow);
        }
    }
    for (const auto& [minute, count] : other.minute_counts) {
        auto [position, inserted] = minute_counts.try_emplace(minute, count);
        if (!inserted) {
            sat_add(position->second, count, counter_overflow);
        }
    }

    ip_cardinality.merge(other.ip_cardinality);

    ip_map_limited = ip_map_limited || other.ip_map_limited;
    path_map_limited = path_map_limited || other.path_map_limited;

    if (other.has_timestamps) {
        if (!has_timestamps) {
            first_timestamp = other.first_timestamp;
            last_timestamp = other.last_timestamp;
            has_timestamps = true;
        } else {
            first_timestamp = std::min(first_timestamp, other.first_timestamp);
            last_timestamp = std::max(last_timestamp, other.last_timestamp);
        }
    }
}

Statistics build_statistics(const std::vector<Accumulator>& per_worker, std::size_t top_n,
                            std::uint64_t files_processed, std::uint64_t bytes_processed) {
    Accumulator merged(nullptr, 0);
    for (const Accumulator& worker : per_worker) {
        merged.merge(worker);
    }

    Statistics stats;
    stats.files_processed = files_processed;
    stats.bytes_processed = bytes_processed;
    stats.lines_examined = merged.lines_examined;
    stats.parsed_records = merged.parsed_records;
    stats.malformed_records = merged.malformed_records;
    stats.matched_records = merged.matched_records;

    stats.response_bytes = merged.response_bytes;
    stats.sized_records = merged.sized_records;
    stats.has_response_size = merged.sized_records > 0;
    stats.min_response_size = stats.has_response_size ? merged.min_response_size : 0;
    stats.max_response_size = stats.has_response_size ? merged.max_response_size : 0;

    stats.status_counts = merged.status_counts;
    stats.method_counts = merged.method_counts;
    stats.status_class_counts = merged.status_class_counts;

    stats.top_ips = top_entries(merged.ip_counts, top_n);
    stats.top_paths = top_entries(merged.path_counts, top_n);

    if (merged.ip_map_limited) {
        stats.unique_ips = merged.ip_cardinality.estimate();
        stats.unique_ips_approximate = true;
    } else {
        stats.unique_ips = merged.ip_counts.size();
    }
    stats.distinct_limit_reached = merged.ip_map_limited || merged.path_map_limited;

    stats.has_timestamps = merged.has_timestamps;
    stats.first_timestamp = merged.first_timestamp;
    stats.last_timestamp = merged.last_timestamp;
    stats.busiest_minutes = busiest_minutes(merged.minute_counts, 5);

    if (stats.has_timestamps && stats.last_timestamp > stats.first_timestamp) {
        const double span_seconds =
            static_cast<double>(stats.last_timestamp - stats.first_timestamp);
        stats.requests_per_minute =
            static_cast<double>(stats.matched_records) * 60.0 / span_seconds;
    } else {
        stats.requests_per_minute = static_cast<double>(stats.matched_records);
    }

    stats.counter_overflow = merged.counter_overflow;
    return stats;
}

}  // namespace hpla
