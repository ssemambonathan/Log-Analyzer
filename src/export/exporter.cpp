#include "export/exporter.hpp"

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <ostream>
#include <string_view>

#include "core/format.hpp"
#include "core/timestamp.hpp"
#include "io/file_line_reader.hpp"
#include "parser/log_parser.hpp"

namespace hpla {
namespace {

void json_escape(std::ostream& out, std::string_view text) {
    for (const char raw : text) {
        const auto c = static_cast<unsigned char>(raw);
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20) {
                    out << "\\u00" << std::hex << std::setw(2) << std::setfill('0')
                        << static_cast<unsigned>(c) << std::dec << std::setfill(' ');
                } else {
                    out << raw;
                }
                break;
        }
    }
}

void json_string(std::ostream& out, std::string_view text) {
    out << '"';
    json_escape(out, text);
    out << '"';
}

bool csv_needs_quotes(std::string_view text) {
    for (const char c : text) {
        if (c == ',' || c == '"' || c == '\n' || c == '\r') {
            return true;
        }
    }
    return false;
}

void csv_field(std::ostream& out, std::string_view text) {
    if (csv_needs_quotes(text)) {
        out << '"';
        for (const char c : text) {
            if (c == '"') {
                out << "\"\"";
            } else {
                out << c;
            }
        }
        out << '"';
    } else {
        out << text;
    }
}

template <typename Callback>
void for_each_matching_record(const std::vector<FileInfo>& files, const FilterSet& filter,
                              std::size_t buffer_size, Callback&& callback) {
    for (const FileInfo& file : files) {
        if (file.size == 0) {
            continue;
        }
        FileLineReader reader(file.path, 0, file.size, buffer_size);
        std::string_view line;
        LogEntry entry;
        while (reader.next(line)) {
            const ParseResult parsed = parse_log_line(line, entry);
            if (!parsed.ok || !filter.matches(entry)) {
                continue;
            }
            if (!callback(entry)) {
                return;
            }
        }
    }
}

void write_record_json(std::ostream& out, const LogEntry& entry) {
    out << "    {\"client_ip\": ";
    json_string(out, entry.client_ip);
    out << ", \"ident\": ";
    json_string(out, entry.remote_ident);
    out << ", \"user\": ";
    json_string(out, entry.authenticated_user);
    out << ", \"timestamp_epoch\": " << entry.timestamp_epoch;
    out << ", \"timestamp_iso\": ";
    json_string(out, format_iso8601_utc(entry.timestamp_epoch));
    out << ", \"method\": ";
    json_string(out, entry.method);
    out << ", \"target\": ";
    json_string(out, entry.target);
    out << ", \"protocol\": ";
    json_string(out, entry.protocol);
    out << ", \"status\": " << entry.status;
    out << ", \"bytes\": ";
    if (entry.has_response_bytes) {
        out << entry.response_bytes;
    } else {
        out << "null";
    }
    out << ", \"referrer\": ";
    json_string(out, entry.referrer);
    out << ", \"user_agent\": ";
    json_string(out, entry.user_agent);
    out << '}';
}

void write_records_json(std::ostream& out, const std::vector<FileInfo>& files,
                        const FilterSet& filter, std::size_t buffer_size) {
    out << ",\n  \"records\": [\n";
    bool first = true;
    for_each_matching_record(files, filter, buffer_size, [&](const LogEntry& entry) {
        if (!first) {
            out << ",\n";
        }
        first = false;
        write_record_json(out, entry);
        return true;
    });
    out << "\n  ]";
}

void write_records_csv(std::ostream& out, const std::vector<FileInfo>& files,
                       const FilterSet& filter, std::size_t buffer_size) {
    out << "client_ip,ident,user,timestamp_epoch,timestamp_iso,method,target,protocol,status,bytes,"
           "referrer,user_agent\n";
    for_each_matching_record(files, filter, buffer_size, [&](const LogEntry& entry) {
        csv_field(out, entry.client_ip);
        out << ',';
        csv_field(out, entry.remote_ident);
        out << ',';
        csv_field(out, entry.authenticated_user);
        out << ',' << entry.timestamp_epoch << ',';
        csv_field(out, format_iso8601_utc(entry.timestamp_epoch));
        out << ',';
        csv_field(out, entry.method);
        out << ',';
        csv_field(out, entry.target);
        out << ',';
        csv_field(out, entry.protocol);
        out << ',' << entry.status << ',';
        if (entry.has_response_bytes) {
            out << entry.response_bytes;
        }
        out << ',';
        csv_field(out, entry.referrer);
        out << ',';
        csv_field(out, entry.user_agent);
        out << '\n';
        return true;
    });
}

void write_filters_json(std::ostream& out, const FilterSet& filter) {
    out << "  \"filters\": {\n    \"rule\": \"AND\",\n    \"active\": "
        << (filter.active() ? "true" : "false") << ",\n    \"items\": [";
    bool first = true;
    for (const FilterInfo& info : filter.descriptions()) {
        if (!first) {
            out << ", ";
        }
        first = false;
        out << "{\"name\": ";
        json_string(out, info.name);
        out << ", \"value\": ";
        json_string(out, info.value);
        out << '}';
    }
    out << "]\n  },\n";
}

}  // namespace

void write_json_report(std::ostream& out, const AnalysisResult& result,
                       const std::vector<FileInfo>& files, const FilterSet& filter,
                       const EngineOptions& options, bool include_records) {
    const Statistics& stats = result.statistics;
    const Performance& perf = result.performance;

    out << std::setprecision(6);
    out << "{\n";
    out << "  \"tool\": \"hpla\",\n";
    out << "  \"version\": \"1.0.0\",\n";
    out << "  \"generated_at\": ";
    json_string(out, format_iso8601_utc(static_cast<std::int64_t>(
                          std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()))));
    out << ",\n";

    out << "  \"input\": {\n    \"files\": [";
    bool first = true;
    for (const FileInfo& file : files) {
        if (!first) {
            out << ", ";
        }
        first = false;
        out << "{\"path\": ";
        json_string(out, file.path);
        out << ", \"size\": " << file.size << '}';
    }
    out << "],\n    \"files_processed\": " << stats.files_processed
        << ",\n    \"bytes_processed\": " << stats.bytes_processed << "\n  },\n";

    write_filters_json(out, filter);

    out << "  \"processing\": {\n"
        << "    \"lines_examined\": " << stats.lines_examined << ",\n"
        << "    \"parsed_records\": " << stats.parsed_records << ",\n"
        << "    \"malformed_records\": " << stats.malformed_records << ",\n"
        << "    \"matched_records\": " << stats.matched_records << "\n  },\n";

    out << "  \"status_distribution\": [";
    first = true;
    for (std::uint16_t status = 100; status < 600; ++status) {
        const std::uint64_t count = stats.status_counts[status - 100];
        if (count == 0) {
            continue;
        }
        if (!first) {
            out << ", ";
        }
        first = false;
        out << "{\"status\": " << status << ", \"count\": " << count << ", \"label\": ";
        json_string(out, status_text(status));
        out << '}';
    }
    out << "],\n";

    out << "  \"method_distribution\": [";
    first = true;
    for (std::size_t index = 0; index < kMethodNames.size(); ++index) {
        const std::uint64_t count = stats.method_counts[index];
        if (count == 0) {
            continue;
        }
        if (!first) {
            out << ", ";
        }
        first = false;
        out << "{\"method\": ";
        json_string(out, kMethodNames[index]);
        out << ", \"count\": " << count << '}';
    }
    out << "],\n";

    out << "  \"response_sizes\": {\n"
        << "    \"sized_records\": " << stats.sized_records << ",\n"
        << "    \"total_bytes\": " << stats.response_bytes << ",\n"
        << "    \"average\": "
        << (stats.sized_records == 0
                ? 0.0
                : static_cast<double>(stats.response_bytes) /
                      static_cast<double>(stats.sized_records))
        << ",\n"
        << "    \"minimum\": " << stats.min_response_size << ",\n"
        << "    \"maximum\": " << stats.max_response_size << "\n  },\n";

    out << "  \"top_client_ips\": [";
    first = true;
    for (const CountEntry& entry : stats.top_ips) {
        if (!first) {
            out << ", ";
        }
        first = false;
        out << "{\"ip\": ";
        json_string(out, entry.key);
        out << ", \"count\": " << entry.count << '}';
    }
    out << "],\n";

    out << "  \"top_resources\": [";
    first = true;
    for (const CountEntry& entry : stats.top_paths) {
        if (!first) {
            out << ", ";
        }
        first = false;
        out << "{\"resource\": ";
        json_string(out, entry.key);
        out << ", \"count\": " << entry.count << '}';
    }
    out << "],\n";

    out << "  \"unique_client_ips\": {\n"
        << "    \"counting_method\": \"" << (stats.unique_ips_approximate ? "approximate_hll" : "exact")
        << "\",\n"
        << "    \"count\": " << stats.unique_ips << ",\n"
        << "    \"distinct_limit_reached\": " << (stats.distinct_limit_reached ? "true" : "false")
        << "\n  },\n";

    out << "  \"traffic\": {\n";
    if (stats.has_timestamps) {
        out << "    \"first_timestamp\": ";
        json_string(out, format_iso8601_utc(stats.first_timestamp));
        out << ",\n    \"last_timestamp\": ";
        json_string(out, format_iso8601_utc(stats.last_timestamp));
        out << ",\n";
    } else {
        out << "    \"first_timestamp\": null,\n    \"last_timestamp\": null,\n";
    }
    out << "    \"requests_per_minute\": " << stats.requests_per_minute << ",\n";
    out << "    \"busiest_minutes\": [";
    first = true;
    for (const CountEntry& entry : stats.busiest_minutes) {
        if (!first) {
            out << ", ";
        }
        first = false;
        out << "{\"minute\": ";
        json_string(out, entry.key);
        out << ", \"requests\": " << entry.count << '}';
    }
    out << "]\n  },\n";

    out << "  \"status_classes\": {\n"
        << "    \"1xx\": " << stats.status_class_counts[0] << ",\n"
        << "    \"2xx\": " << stats.status_class_counts[1] << ",\n"
        << "    \"3xx\": " << stats.status_class_counts[2] << ",\n"
        << "    \"4xx\": " << stats.status_class_counts[3] << ",\n"
        << "    \"5xx\": " << stats.status_class_counts[4] << "\n  },\n";

    out << "  \"performance\": {\n"
        << "    \"elapsed_seconds\": " << perf.elapsed_seconds << ",\n"
        << "    \"throughput_bytes_per_second\": " << perf.throughput_bytes_per_second << ",\n"
        << "    \"lines_per_second\": " << perf.lines_per_second << ",\n"
        << "    \"worker_threads\": " << perf.threads << ",\n"
        << "    \"io_mode\": ";
    json_string(out, perf.io_mode);
    out << ",\n    \"cancelled\": " << (perf.cancelled ? "true" : "false") << ",\n"
        << "    \"counter_overflow\": " << (stats.counter_overflow ? "true" : "false") << "\n  }";

    if (include_records) {
        write_records_json(out, files, filter, options.buffer_size);
    }

    out << "\n}\n";
}

void write_csv_report(std::ostream& out, const AnalysisResult& result,
                      const std::vector<FileInfo>& files, const FilterSet& filter,
                      const EngineOptions& options, bool include_records) {
    const Statistics& stats = result.statistics;
    const Performance& perf = result.performance;

    if (include_records) {
        write_records_csv(out, files, filter, options.buffer_size);
        return;
    }

    out << "section,metric,value\n";
    out << "input,files_processed," << stats.files_processed << '\n';
    out << "input,bytes_processed," << stats.bytes_processed << '\n';
    out << "processing,lines_examined," << stats.lines_examined << '\n';
    out << "processing,parsed_records," << stats.parsed_records << '\n';
    out << "processing,malformed_records," << stats.malformed_records << '\n';
    out << "processing,matched_records," << stats.matched_records << '\n';

    for (const FilterInfo& info : filter.descriptions()) {
        out << "filter,";
        csv_field(out, info.name);
        out << ',';
        csv_field(out, info.value);
        out << '\n';
    }

    out << "response_size,sized_records," << stats.sized_records << '\n';
    out << "response_size,total_bytes," << stats.response_bytes << '\n';
    out << "response_size,average,"
        << (stats.sized_records == 0
                ? 0.0
                : static_cast<double>(stats.response_bytes) /
                      static_cast<double>(stats.sized_records))
        << '\n';
    out << "response_size,minimum," << stats.min_response_size << '\n';
    out << "response_size,maximum," << stats.max_response_size << '\n';

    for (std::uint16_t status = 100; status < 600; ++status) {
        const std::uint64_t count = stats.status_counts[status - 100];
        if (count != 0) {
            out << "status," << status << ',' << count << '\n';
        }
    }
    for (std::size_t index = 0; index < kMethodNames.size(); ++index) {
        if (stats.method_counts[index] != 0) {
            out << "method,";
            csv_field(out, kMethodNames[index]);
            out << ',' << stats.method_counts[index] << '\n';
        }
    }
    for (const CountEntry& entry : stats.top_ips) {
        out << "top_ip,";
        csv_field(out, entry.key);
        out << ',' << entry.count << '\n';
    }
    for (const CountEntry& entry : stats.top_paths) {
        out << "top_resource,";
        csv_field(out, entry.key);
        out << ',' << entry.count << '\n';
    }

    out << "unique_client_ips,counting_method,"
        << (stats.unique_ips_approximate ? "approximate_hll" : "exact") << '\n';
    out << "unique_client_ips,count," << stats.unique_ips << '\n';
    out << "unique_client_ips,distinct_limit_reached,"
        << (stats.distinct_limit_reached ? "true" : "false") << '\n';

    if (stats.has_timestamps) {
        out << "traffic,first_timestamp," << format_iso8601_utc(stats.first_timestamp) << '\n';
        out << "traffic,last_timestamp," << format_iso8601_utc(stats.last_timestamp) << '\n';
    }
    out << "traffic,requests_per_minute," << stats.requests_per_minute << '\n';
    for (const CountEntry& entry : stats.busiest_minutes) {
        out << "traffic,busiest_minute,";
        csv_field(out, entry.key);
        out << ',' << entry.count << '\n';
    }

    out << "performance,elapsed_seconds," << perf.elapsed_seconds << '\n';
    out << "performance,throughput_bytes_per_second," << perf.throughput_bytes_per_second << '\n';
    out << "performance,lines_per_second," << perf.lines_per_second << '\n';
    out << "performance,worker_threads," << perf.threads << '\n';
    out << "performance,io_mode,";
    csv_field(out, perf.io_mode);
    out << '\n';
    out << "performance,cancelled," << (perf.cancelled ? "true" : "false") << '\n';
    out << "performance,counter_overflow," << (stats.counter_overflow ? "true" : "false") << '\n';
}

}  // namespace hpla
