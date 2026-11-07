#include "cli/cli.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string_view>
#include <system_error>
#include <utility>

#include "core/format.hpp"
#include "core/ip.hpp"
#include "core/timestamp.hpp"

namespace hpla {
namespace {

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    return text;
}

std::vector<std::string> split_list(std::string_view text) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
        const std::size_t comma = text.find(',', start);
        const std::string_view part =
            trim(text.substr(start, comma == std::string_view::npos ? std::string_view::npos
                                                                   : comma - start));
        parts.emplace_back(part);
        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }
    return parts;
}

std::string lowercase_ascii(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (const char c : text) {
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return result;
}

bool parse_status_value(std::string_view text, std::uint16_t& status, std::string& error) {
    std::uint64_t value = 0;
    if (!parse_uint64(trim(text), value) || value < 100 || value > 599) {
        error = "invalid HTTP status '" + std::string(text) + "' (expected 100-599)";
        return false;
    }
    status = static_cast<std::uint16_t>(value);
    return true;
}

bool take_value(int argc, const char* const* argv, int& index, const std::string& option,
                const std::optional<std::string>& inline_value, std::string& out,
                std::string& error) {
    if (inline_value.has_value()) {
        out = *inline_value;
        return true;
    }
    if (index + 1 >= argc) {
        error = "option " + option + " requires a value";
        return false;
    }
    out = argv[++index];
    return true;
}

bool take_two_values(int argc, const char* const* argv, int& index, const std::string& option,
                     const std::optional<std::string>& inline_value, std::string& first,
                     std::string& second, std::string& error) {
    if (inline_value.has_value()) {
        const std::vector<std::string> parts = split_list(*inline_value);
        if (parts.size() != 2) {
            error = "option " + option + " expects two values";
            return false;
        }
        first = parts[0];
        second = parts[1];
        return true;
    }
    if (index + 2 >= argc) {
        error = "option " + option + " expects two values";
        return false;
    }
    first = argv[++index];
    second = argv[++index];
    return true;
}

bool is_known_method(std::string_view method) {
    if (method.empty()) {
        return false;
    }
    for (const char c : method) {
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::string usage_text() {
    return
        "hpla - High-Performance Log Analyzer\n"
        "\n"
        "USAGE:\n"
        "  hpla [OPTIONS] FILE [FILE ...]\n"
        "\n"
        "Parses Common Log Format and Combined Log Format access logs, computes\n"
        "statistics, and prints a report or exports JSON/CSV.\n"
        "\n"
        "FILTERS (all active filters are combined with AND):\n"
        "  --status LIST             Status codes, comma separated (e.g. 200,301,302)\n"
        "  --status-range MIN MAX    Inclusive status range (e.g. --status-range 400 499)\n"
        "  --ip LIST                 Client IPs, comma separated (IPv4 or IPv6)\n"
        "  --method LIST             HTTP methods, comma separated (GET,POST,...)\n"
        "  --path PATH               Exact request target (repeatable, OR within option)\n"
        "  --path-contains TEXT      Target substring (repeatable, OR within option)\n"
        "  --user-agent TEXT         User-agent substring (repeatable, OR within option)\n"
        "  --from TIMESTAMP          Start time, inclusive (dd/Mon/yyyy:HH:MM:SS +ZZZZ)\n"
        "  --to TIMESTAMP            End time, inclusive (same format)\n"
        "  --min-size BYTES          Minimum response size (suffixes K/M/G allowed)\n"
        "  --max-size BYTES          Maximum response size (suffixes K/M/G allowed)\n"
        "\n"
        "PROCESSING:\n"
        "  -t, --threads N           Worker threads (0 or omitted = hardware concurrency)\n"
        "  --sequential              Force the single-threaded engine\n"
        "  --mmap                    Use memory-mapped I/O when available\n"
        "  --buffer-size BYTES       Per-reader buffer size (default 1 MiB)\n"
        "  --top N                   Top-IP/top-resource entries in reports (default 10)\n"
        "  --max-distinct N          Distinct IP/path keys per worker (default 2,000,000;\n"
        "                            0 = unlimited, overflow switches unique IPs to HLL)\n"
        "\n"
        "OUTPUT:\n"
        "  --format text|json|csv    Output format (default text; inferred from --output)\n"
        "  -o, --output FILE         Write the report to FILE\n"
        "  --records                 Include matching records (JSON or CSV only)\n"
        "  --color auto|always|never ANSI colors (default auto)\n"
        "  -h, --help                Show this help\n"
        "  -v, --version             Show version\n"
        "\n"
        "EXAMPLES:\n"
        "  hpla access.log\n"
        "  hpla access.log --status 404 --top 20\n"
        "  hpla access.log --status-range 400 499 --method POST --path-contains api\n"
        "  hpla a.log b.log --from \"30/Oct/2026:08:00:00 +0000\" \\\n"
        "                   --to \"30/Oct/2026:18:00:00 +0000\" --format json -o report.json\n"
        "  hpla access.log --status 500 --format csv --records -o errors.csv\n"
        "\n"
        "EXIT CODES:\n"
        "  0  success\n"
        "  1  input/output or processing error\n"
        "  2  invalid command line\n"
        "  130 cancelled with Ctrl+C\n";
}

CliParseResult parse_cli(int argc, const char* const* argv) {
    CliParseResult result;
    CliOptions options;

    std::vector<std::uint16_t> statuses;
    bool has_range = false;
    std::uint16_t range_min = 0;
    std::uint16_t range_max = 0;

    auto fail = [&](std::string message) {
        result.ok = false;
        result.error = std::move(message);
        return result;
    };

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];

        if (argument == "--") {
            for (++index; index < argc; ++index) {
                options.files.emplace_back(argv[index]);
            }
            break;
        }
        if (argument == "-h" || argument == "--help") {
            options.show_help = true;
            continue;
        }
        if (argument == "-v" || argument == "--version") {
            options.show_version = true;
            continue;
        }
        if (argument.empty() || argument[0] != '-') {
            options.files.push_back(argument);
            continue;
        }
        if (argument.rfind("--", 0) != 0 && argument != "-t" && argument != "-o") {
            return fail("unknown option '" + argument + "'");
        }

        std::string name = argument;
        std::optional<std::string> inline_value;
        if (const std::size_t equals = argument.find('='); equals != std::string::npos) {
            name = argument.substr(0, equals);
            inline_value = argument.substr(equals + 1);
        }

        std::string value;
        std::string error;

        if (name == "--status") {
            if (!take_value(argc, argv, index, name, inline_value, value, error)) {
                return fail(error);
            }
            for (const std::string& part : split_list(value)) {
                std::uint16_t status = 0;
                if (!parse_status_value(part, status, error)) {
                    return fail(error);
                }
                statuses.push_back(status);
            }
            options.filter.add_description("status", value);
        } else if (name == "--status-range") {
            std::string first;
            std::string second;
            if (!take_two_values(argc, argv, index, name, inline_value, first, second, error)) {
                return fail(error);
            }
            std::uint16_t minimum = 0;
            std::uint16_t maximum = 0;
            if (!parse_status_value(first, minimum, error) ||
                !parse_status_value(second, maximum, error)) {
                return fail(error);
            }
            if (minimum > maximum) {
                return fail("--status-range minimum is greater than maximum");
            }
            range_min = minimum;
            range_max = maximum;
            has_range = true;
            options.filter.add_description("status-range",
                                           std::to_string(minimum) + "-" + std::to_string(maximum));
        } else if (name == "--ip") {
            if (!take_value(argc, argv, index, name, inline_value, value, error)) {
                return fail(error);
            }
            for (const std::string& part : split_list(value)) {
                const auto address = parse_ip(trim(part));
                if (!address.has_value()) {
                    return fail("invalid IP address '" + part + "'");
                }
                options.filter.add_ip(*address);
            }
            options.filter.add_description("ip", value);
        } else if (name == "--method") {
            if (!take_value(argc, argv, index, name, inline_value, value, error)) {
                return fail(error);
            }
            for (const std::string& part : split_list(value)) {
                if (!is_known_method(trim(part))) {
                    return fail("invalid HTTP method '" + part + "'");
                }
                options.filter.add_method(uppercase_ascii(trim(part)));
            }
            options.filter.add_description("method", value);
        } else if (name == "--path") {
            if (!take_value(argc, argv, index, name, inline_value, value, error)) {
                return fail(error);
            }
            options.filter.add_exact_path(value);
            options.filter.add_description("path", value);
        } else if (name == "--path-contains") {
            if (!take_value(argc, argv, index, name, inline_value, value, error)) {
                return fail(error);
            }
            options.filter.add_path_contains(value);
            options.filter.add_description("path-contains", value);
        } else if (name == "--user-agent") {
            if (!take_value(argc, argv, index, name, inline_value, value, error)) {
                return fail(error);
            }
            options.filter.add_user_agent_contains(value);
            options.filter.add_description("user-agent", value);
        } else if (name == "--from") {
            if (!take_value(argc, argv, index, name, inline_value, value, error)) {
                return fail(error);
            }
            std::int64_t epoch = 0;
            if (!parse_http_timestamp(value, epoch)) {
                return fail("invalid --from timestamp '" + value +
                            "' (expected dd/Mon/yyyy:HH:MM:SS +ZZZZ)");
            }
            options.filter.set_from(epoch);
            options.filter.add_description("from", value);
        } else if (name == "--to") {
            if (!take_value(argc, argv, index, name, inline_value, value, error)) {
                return fail(error);
            }
            std::int64_t epoch = 0;
            if (!parse_http_timestamp(value, epoch)) {
                return fail("invalid --to timestamp '" + value +
                            "' (expected dd/Mon/yyyy:HH:MM:SS +ZZZZ)");
            }
            options.filter.set_to(epoch);
            options.filter.add_description("to", value);
        } else if (name == "--min-size" || name == "--max-size") {
            if (!take_value(argc, argv, index, name, inline_value, value, error)) {
                return fail(error);
            }
            std::uint64_t bytes = 0;
            if (!parse_size(value, bytes)) {
                return fail("invalid size '" + value + "' for " + name);
            }
            if (name == "--min-size") {
                options.filter.set_min_size(bytes);
            } else {
                options.filter.set_max_size(bytes);
            }
            options.filter.add_description(name.substr(2), value);
        } else if (name == "--threads" || name == "-t") {
            if (!take_value(argc, argv, index, name, inline_value, value, error)) {
                return fail(error);
            }
            std::uint64_t threads = 0;
            if (!parse_uint64(value, threads) || threads > 256) {
                return fail("invalid --threads value '" + value + "' (expected 0-256)");
            }
            options.threads = static_cast<unsigned>(threads);
        } else if (name == "--buffer-size") {
            if (!take_value(argc, argv, index, name, inline_value, value, error)) {
                return fail(error);
            }
            std::uint64_t bytes = 0;
            if (!parse_size(value, bytes) || bytes < 4096 ||
                bytes > (std::uint64_t{1} << 30)) {
                return fail("invalid --buffer-size '" + value +
                            "' (expected 4 KiB to 1 GiB)");
            }
            options.buffer_size = static_cast<std::size_t>(bytes);
        } else if (name == "--top") {
            if (!take_value(argc, argv, index, name, inline_value, value, error)) {
                return fail(error);
            }
            std::uint64_t top = 0;
            if (!parse_uint64(value, top) || top == 0 || top > 1000) {
                return fail("invalid --top value '" + value + "' (expected 1-1000)");
            }
            options.top_n = static_cast<std::size_t>(top);
        } else if (name == "--max-distinct") {
            if (!take_value(argc, argv, index, name, inline_value, value, error)) {
                return fail(error);
            }
            std::uint64_t limit = 0;
            if (!parse_size(value, limit)) {
                return fail("invalid --max-distinct value '" + value + "'");
            }
            options.max_distinct = static_cast<std::size_t>(limit);
        } else if (name == "--format") {
            if (!take_value(argc, argv, index, name, inline_value, value, error)) {
                return fail(error);
            }
            const std::string normalized = lowercase_ascii(value);
            if (normalized == "text") {
                options.format = OutputFormat::Text;
            } else if (normalized == "json") {
                options.format = OutputFormat::Json;
            } else if (normalized == "csv") {
                options.format = OutputFormat::Csv;
            } else {
                return fail("invalid --format '" + value + "' (expected text, json, or csv)");
            }
            options.format_explicit = true;
        } else if (name == "-o" || name == "--output") {
            if (!take_value(argc, argv, index, name, inline_value, value, error)) {
                return fail(error);
            }
            if (value.empty()) {
                return fail("--output requires a non-empty file name");
            }
            options.output_path = value;
        } else if (name == "--records" || name == "--include-records") {
            options.include_records = true;
        } else if (name == "--sequential" || name == "--seq") {
            options.sequential = true;
        } else if (name == "--mmap") {
            options.use_mmap = true;
        } else if (name == "--color") {
            if (!take_value(argc, argv, index, name, inline_value, value, error)) {
                return fail(error);
            }
            const std::string normalized = lowercase_ascii(value);
            if (normalized == "auto") {
                options.color = ColorMode::Auto;
            } else if (normalized == "always") {
                options.color = ColorMode::Always;
            } else if (normalized == "never") {
                options.color = ColorMode::Never;
            } else {
                return fail("invalid --color '" + value + "' (expected auto, always, or never)");
            }
        } else {
            return fail("unknown option '" + name + "'");
        }
    }

    if (options.show_help || options.show_version) {
        result.ok = true;
        result.options = std::move(options);
        return result;
    }

    if (!statuses.empty()) {
        options.filter.set_statuses(statuses);
    }
    if (has_range) {
        options.filter.set_status_range(range_min, range_max);
    }

    if (options.files.empty()) {
        return fail("no input files (try --help)");
    }

    if (options.filter.from().has_value() && options.filter.to().has_value() &&
        *options.filter.from() > *options.filter.to()) {
        return fail("--from is later than --to");
    }
    if (options.filter.min_size().has_value() && options.filter.max_size().has_value() &&
        *options.filter.min_size() > *options.filter.max_size()) {
        return fail("--min-size is greater than --max-size");
    }

    if (!options.format_explicit && options.output_path.has_value()) {
        const std::string extension =
            lowercase_ascii(std::filesystem::path(*options.output_path).extension().string());
        if (extension == ".json") {
            options.format = OutputFormat::Json;
        } else if (extension == ".csv") {
            options.format = OutputFormat::Csv;
        }
    }

    if (options.include_records && options.format == OutputFormat::Text) {
        return fail("--records requires --format json or csv");
    }

    if (options.output_path.has_value()) {
        std::error_code error_code;
        const auto output_absolute =
            std::filesystem::weakly_canonical(*options.output_path, error_code);
        if (!error_code) {
            for (const std::string& input : options.files) {
                std::error_code input_error;
                const auto input_absolute = std::filesystem::weakly_canonical(input, input_error);
                if (!input_error && input_absolute == output_absolute) {
                    return fail("output file must not overwrite input file '" + input + "'");
                }
            }
        }
    }

    result.ok = true;
    result.options = std::move(options);
    return result;
}

}  // namespace hpla
