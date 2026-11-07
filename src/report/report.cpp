#include "report/report.hpp"

#include <algorithm>
#include <iomanip>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/format.hpp"
#include "core/timestamp.hpp"

namespace hpla {
namespace {

class Style {
public:
    explicit Style(bool enabled) : enabled_(enabled) {}

    const char* heading() const { return enabled_ ? "\x1b[1;36m" : ""; }
    const char* label() const { return enabled_ ? "\x1b[0;37m" : ""; }
    const char* good() const { return enabled_ ? "\x1b[1;32m" : ""; }
    const char* warning() const { return enabled_ ? "\x1b[1;33m" : ""; }
    const char* reset() const { return enabled_ ? "\x1b[0m" : ""; }

private:
    bool enabled_;
};

std::string percent(std::uint64_t part, std::uint64_t whole) {
    if (whole == 0) {
        return "0.00%";
    }
    return format_double(static_cast<double>(part) * 100.0 / static_cast<double>(whole), 2) + "%";
}

void row(std::ostream& out, std::string_view label, const std::string& value) {
    out << "  " << std::left << std::setw(34) << label << value << '\n';
}

void section(std::ostream& out, const Style& style, std::string_view title) {
    out << '\n' << style.heading() << title << style.reset() << '\n';
}

std::string key_for_display(std::string_view key) {
    std::string text = sanitize_for_terminal(key);
    if (text.empty()) {
        return "(requests without a target)";
    }
    return text;
}

}  // namespace

void write_text_report(std::ostream& out, const AnalysisResult& result,
                       const std::vector<FileInfo>& files, const FilterSet& filter, bool color) {
    const Style style(color);
    const Statistics& stats = result.statistics;
    const Performance& perf = result.performance;

    out << "============================================================\n";
    out << "                 HIGH-PERFORMANCE LOG ANALYZER  v1.0.0\n";
    out << "============================================================\n";

    section(out, style, "INPUT");
    row(out, "Files processed:", format_uint(stats.files_processed));
    row(out, "Total bytes examined:",
        format_uint(stats.bytes_processed) + "  (" + format_human_bytes(stats.bytes_processed) + ")");
    for (const FileInfo& file : files) {
        out << "    " << sanitize_for_terminal(file.path) << "  (" << format_human_bytes(file.size)
            << ")\n";
    }

    if (filter.active()) {
        section(out, style, "FILTERS (all conditions must match)");
        for (const FilterInfo& info : filter.descriptions()) {
            row(out, info.name + ":", info.value);
        }
    } else {
        section(out, style, "FILTERS");
        out << "  None - every successfully parsed record is counted.\n";
    }

    section(out, style, "PROCESSING");
    row(out, "Lines examined:", format_uint(stats.lines_examined));
    row(out, "Successfully parsed:",
        format_uint(stats.parsed_records) + "  (" + percent(stats.parsed_records, stats.lines_examined) +
            " of lines)");
    row(out, "Malformed records:", format_uint(stats.malformed_records));
    row(out, "Matching records:",
        format_uint(stats.matched_records) + "  (" + percent(stats.matched_records, stats.parsed_records) +
            " of parsed)");

    if (stats.matched_records == 0) {
        out << '\n';
        out << style.warning() << "No records matched the active filters." << style.reset() << '\n';
    } else {
        section(out, style, "RESPONSE SIZE (matching records with a byte count)");
        row(out, "Sized records:", format_uint(stats.sized_records) + " of " +
                                       format_uint(stats.matched_records));
        row(out, "Total response bytes:", format_uint(stats.response_bytes) + "  (" +
                                             format_human_bytes(stats.response_bytes) + ")");
        if (stats.sized_records > 0) {
            const double average = static_cast<double>(stats.response_bytes) /
                                   static_cast<double>(stats.sized_records);
            row(out, "Average:", format_double(average, 2) + " bytes");
            row(out, "Minimum:", format_uint(stats.min_response_size) + " bytes");
            row(out, "Maximum:", format_uint(stats.max_response_size) + " bytes");
        } else {
            row(out, "Average:", "n/a");
            row(out, "Minimum:", "n/a");
            row(out, "Maximum:", "n/a");
        }

        section(out, style, "HTTP STATUS DISTRIBUTION");
        std::vector<std::pair<std::uint16_t, std::uint64_t>> statuses;
        for (std::uint16_t status = 100; status < 600; ++status) {
            if (stats.status_counts[status - 100] != 0) {
                statuses.emplace_back(status, stats.status_counts[status - 100]);
            }
        }
        std::sort(statuses.begin(), statuses.end(), [](const auto& left, const auto& right) {
            if (left.second != right.second) {
                return left.second > right.second;
            }
            return left.first < right.first;
        });
        for (const auto& [status, count] : statuses) {
            const std::string label =
                std::to_string(status) + " " + status_text(status);
            row(out, label + ":", format_uint(count) + "  (" +
                                     percent(count, stats.matched_records) + ")");
        }

        section(out, style, "HTTP METHOD DISTRIBUTION");
        std::vector<std::pair<std::size_t, std::uint64_t>> methods;
        for (std::size_t index = 0; index < kMethodNames.size(); ++index) {
            if (stats.method_counts[index] != 0) {
                methods.emplace_back(index, stats.method_counts[index]);
            }
        }
        std::sort(methods.begin(), methods.end(), [](const auto& left, const auto& right) {
            if (left.second != right.second) {
                return left.second > right.second;
            }
            return kMethodNames[left.first] < kMethodNames[right.first];
        });
        for (const auto& [index, count] : methods) {
            row(out, std::string(kMethodNames[index]) + ":", format_uint(count) + "  (" +
                                                                percent(count, stats.matched_records) +
                                                                ")");
        }

        section(out, style, "TOP CLIENT IP ADDRESSES");
        if (stats.top_ips.empty()) {
            out << "  n/a\n";
        }
        for (std::size_t i = 0; i < stats.top_ips.size(); ++i) {
            const CountEntry& entry = stats.top_ips[i];
            out << "  " << std::right << std::setw(3) << (i + 1) << ". " << std::left
                << std::setw(38) << key_for_display(entry.key) << format_uint(entry.count) << "  ("
                << percent(entry.count, stats.matched_records) << ")\n";
        }

        section(out, style, "TOP REQUESTED RESOURCES");
        if (stats.top_paths.empty()) {
            out << "  n/a\n";
        }
        for (std::size_t i = 0; i < stats.top_paths.size(); ++i) {
            const CountEntry& entry = stats.top_paths[i];
            out << "  " << std::right << std::setw(3) << (i + 1) << ". " << std::left
                << std::setw(46) << key_for_display(entry.key) << format_uint(entry.count) << "  ("
                << percent(entry.count, stats.matched_records) << ")\n";
        }

        section(out, style, "TRAFFIC");
        if (stats.has_timestamps) {
            row(out, "First record:", format_iso8601_utc(stats.first_timestamp));
            row(out, "Last record:", format_iso8601_utc(stats.last_timestamp));
        } else {
            row(out, "First record:", "n/a");
            row(out, "Last record:", "n/a");
        }
        row(out, "Average requests/minute:", format_double(stats.requests_per_minute, 2));
        if (!stats.busiest_minutes.empty()) {
            out << "  Busiest minutes:\n";
            for (const CountEntry& entry : stats.busiest_minutes) {
                out << "    " << std::left << std::setw(28) << entry.key << format_uint(entry.count)
                    << '\n';
            }
        }

        section(out, style, "UNIQUE CLIENT IP ADDRESSES");
        row(out, stats.unique_ips_approximate ? "Approximate count (HLL):" : "Exact count:",
            format_uint(stats.unique_ips));
        if (stats.unique_ips_approximate) {
            out << "  The distinct-key limit was reached; the count is a HyperLogLog estimate\n"
                   "  (about 1.6% relative error). Use --max-distinct 0 for exact counting.\n";
        }
    }

    section(out, style, "PERFORMANCE");
    row(out, "Elapsed time:", format_double(perf.elapsed_seconds, 3) + " s");
    row(out, "Throughput:", format_rate(perf.throughput_bytes_per_second));
    row(out, "Processing rate:", format_uint(static_cast<std::uint64_t>(perf.lines_per_second)) +
                                     " lines/s");
    row(out, "Worker threads:", std::to_string(perf.threads));
    row(out, "I/O mode:", perf.io_mode);

    bool warned = false;
    if (perf.cancelled) {
        if (!warned) {
            section(out, style, "WARNINGS");
            warned = true;
        }
        out << style.warning() << "  - Processing was cancelled; statistics are partial."
            << style.reset() << '\n';
    }
    if (stats.distinct_limit_reached) {
        if (!warned) {
            section(out, style, "WARNINGS");
            warned = true;
        }
        out << style.warning()
            << "  - Distinct-key limit reached; top-IP/top-resource and exact unique-IP counts are "
               "incomplete."
            << style.reset() << '\n';
    }
    if (stats.counter_overflow) {
        if (!warned) {
            section(out, style, "WARNINGS");
            warned = true;
        }
        out << style.warning() << "  - One or more counters saturated at the 64-bit limit."
            << style.reset() << '\n';
    }

    out << "\n============================================================\n";
}

}  // namespace hpla
