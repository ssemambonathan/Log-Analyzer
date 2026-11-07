#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "core/format.hpp"
#include "core/timestamp.hpp"

namespace {

struct GeneratorOptions {
    std::string output;
    std::uint64_t lines = 1'000'000;
    double malformed_percent = 0.5;
    std::uint64_t seed = 42;
    bool combined = true;
    bool crlf = false;
    bool trailing_newline = true;
    std::uint64_t unique_ips = 50'000;
    std::uint64_t unique_paths = 2'000;
    std::int64_t start_epoch = 0;
};

constexpr std::string_view kUserAgents[] = {
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/124.0.0.0 Safari/537.36",
    "Mozilla/5.0 (X11; Linux x86_64; rv:125.0) Gecko/20100101 Firefox/125.0",
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 14_4) AppleWebKit/605.1.15 (KHTML, like Gecko) "
    "Version/17.4 Safari/605.1.15",
    "curl/8.6.0",
    "python-requests/2.31.0",
    "Mozilla/5.0 (compatible; Googlebot/2.1; +http://www.google.com/bot.html)",
    "Mozilla/5.0 (iPhone; CPU iPhone OS 17_4 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like "
    "Gecko) Mobile/15E148",
    "PostmanRuntime/7.37.0",
    "hpla-test-client/1.0",
    "Mozilla/5.0 (compatible; bingbot/2.0; +http://www.bing.com/bingbot.htm)",
};

constexpr std::string_view kReferrers[] = {
    "-",
    "https://example.com/",
    "https://example.com/login",
    "https://www.google.com/",
    "https://news.example.net/article/42",
    "https://example.com/search?q=logs",
};

constexpr std::string_view kPathPrefixes[] = {
    "/",          "/index.html",        "/about",          "/contact",
    "/login",     "/logout",            "/api/v1/users",   "/api/v1/orders",
    "/api/v1/products", "/api/v1/status", "/static/app.js", "/static/style.css",
    "/images/logo.png", "/search",      "/checkout",       "/health",
};

std::uint64_t parse_u64(const std::string& text, const char* name, std::uint64_t minimum,
                        std::uint64_t maximum) {
    std::uint64_t value = 0;
    if (!hpla::parse_uint64(text, value) || value < minimum || value > maximum) {
        std::cerr << "hpla_genlog: invalid value for " << name << ": '" << text << "'\n";
        std::exit(2);
    }
    return value;
}

GeneratorOptions parse_options(int argc, char** argv) {
    GeneratorOptions options;

    std::string start_text = "30/Oct/2026:00:00:00 +0000";
    if (!hpla::parse_http_timestamp(start_text, options.start_epoch)) {
        options.start_epoch = 0;
    }

    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        auto require = [&](const char* name) -> std::string {
            if (index + 1 >= argc) {
                std::cerr << "hpla_genlog: " << name << " requires a value\n";
                std::exit(2);
            }
            return argv[++index];
        };

        if (arg == "--out" || arg == "-o") {
            options.output = require("--out");
        } else if (arg == "--lines") {
            options.lines = parse_u64(require("--lines"), "--lines", 0, 1'000'000'000);
        } else if (arg == "--malformed" || arg == "--malformed-percent") {
            const std::string text = require("--malformed");
            char* end = nullptr;
            const double value = std::strtod(text.c_str(), &end);
            if (end == text.c_str() || *end != '\0' || value < 0.0 || value > 100.0) {
                std::cerr << "hpla_genlog: invalid --malformed value '" << text << "'\n";
                std::exit(2);
            }
            options.malformed_percent = value;
        } else if (arg == "--seed") {
            options.seed = parse_u64(require("--seed"), "--seed", 0, UINT64_MAX);
        } else if (arg == "--unique-ips") {
            options.unique_ips = parse_u64(require("--unique-ips"), "--unique-ips", 1, 100'000'000);
        } else if (arg == "--unique-paths") {
            options.unique_paths =
                parse_u64(require("--unique-paths"), "--unique-paths", 1, 10'000'000);
        } else if (arg == "--start") {
            const std::string text = require("--start");
            if (!hpla::parse_http_timestamp(text, options.start_epoch)) {
                std::cerr << "hpla_genlog: invalid --start timestamp '" << text << "'\n";
                std::exit(2);
            }
        } else if (arg == "--format") {
            const std::string value = require("--format");
            if (value == "combined") {
                options.combined = true;
            } else if (value == "clf" || value == "common") {
                options.combined = false;
            } else {
                std::cerr << "hpla_genlog: --format expects 'combined' or 'clf'\n";
                std::exit(2);
            }
        } else if (arg == "--crlf") {
            options.crlf = true;
        } else if (arg == "--no-trailing-newline") {
            options.trailing_newline = false;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: hpla_genlog --out FILE [--lines N] [--malformed PERCENT]\n"
                         "                   [--seed N] [--format combined|clf] [--crlf]\n"
                         "                   [--unique-ips N] [--unique-paths N]\n"
                         "                   [--start 'dd/Mon/yyyy:HH:MM:SS +0000']\n"
                         "                   [--no-trailing-newline]\n";
            std::exit(0);
        } else {
            std::cerr << "hpla_genlog: unknown option '" << arg << "'\n";
            std::exit(2);
        }
    }

    if (options.output.empty()) {
        std::cerr << "hpla_genlog: --out FILE is required\n";
        std::exit(2);
    }
    return options;
}

std::vector<std::string> make_paths(std::uint64_t count) {
    constexpr std::size_t prefix_count = std::size(kPathPrefixes);
    std::vector<std::string> paths;
    paths.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i) {
        std::string path(kPathPrefixes[i % prefix_count]);
        if (i >= prefix_count || path == "/") {
            path += (path.back() == '/' ? "item/" : "/") + std::to_string(i);
        }
        paths.push_back(std::move(path));
    }
    return paths;
}

std::vector<std::string> make_ips(std::uint64_t count) {
    std::vector<std::string> ips;
    ips.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "10.%u.%u.%u",
                      static_cast<unsigned>((i >> 16) & 0xFF), static_cast<unsigned>((i >> 8) & 0xFF),
                      static_cast<unsigned>(i & 0xFF));
        ips.emplace_back(buffer);
    }
    return ips;
}

std::string make_malformed(std::string_view source, std::mt19937_64& rng) {
    switch (rng() % 5) {
        case 0: {
            // Drop the timestamp's closing bracket and everything after it.
            const std::size_t close = source.find(']');
            if (close == std::string_view::npos) {
                return "garbage";
            }
            return std::string(source.substr(0, close));
        }
        case 1:
            return "this is not an access log line";
        case 2:
            return "";
        case 3: {
            std::string broken(source);
            const std::size_t quote = broken.find('"');
            if (quote != std::string::npos) {
                broken.erase(quote, 1);
            } else {
                broken.erase(broken.size() - 1);
            }
            return broken;
        }
        default: {
            std::string broken(source);
            const std::size_t request_open = broken.find('"');
            const std::size_t request_close =
                request_open == std::string::npos
                    ? std::string::npos
                    : broken.find('"', request_open + 1);
            const std::size_t status =
                request_close == std::string::npos
                    ? std::string::npos
                    : broken.find_first_of("0123456789", request_close + 1);
            if (status != std::string::npos && status + 3 <= broken.size()) {
                broken.replace(status, 3, "999");
            } else {
                broken.erase(broken.size() - 1);
            }
            return broken;
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    const GeneratorOptions options = parse_options(argc, argv);

    const std::vector<std::string> ips = make_ips(options.unique_ips);
    const std::vector<std::string> paths = make_paths(options.unique_paths);

    std::mt19937_64 rng(options.seed);
    std::discrete_distribution<std::size_t> status_distribution({70, 8, 4, 5, 3, 1, 1, 6, 1, 1});
    constexpr std::uint16_t kStatuses[] = {200, 301, 302, 304, 400, 401, 403, 404, 500, 503};
    std::discrete_distribution<std::size_t> method_distribution({80, 12, 2, 1, 1.5, 3, 0.5});
    constexpr std::string_view kMethods[] = {"GET", "POST", "PUT", "DELETE", "PATCH", "HEAD",
                                             "OPTIONS"};
    std::bernoulli_distribution malformed(options.malformed_percent / 100.0);
    std::bernoulli_distribution no_bytes(0.01);
    std::uniform_int_distribution<std::uint64_t> pause(0, 3);

    std::ofstream out(options.output, std::ios::binary | std::ios::trunc);
    if (!out) {
        std::cerr << "hpla_genlog: cannot open '" << options.output << "'\n";
        return 1;
    }

    const auto start_time = std::chrono::steady_clock::now();
    std::string buffer;
    buffer.reserve(4U << 20);
    const std::string_view terminator = options.crlf ? "\r\n" : "\n";

    std::int64_t epoch = options.start_epoch;
    std::uint64_t malformed_written = 0;
    std::uint64_t bytes_written = 0;
    std::string last_valid;
    char line[1024];

    for (std::uint64_t i = 0; i < options.lines; ++i) {
        epoch += static_cast<std::int64_t>(pause(rng));

        const std::string& ip = ips[rng() % ips.size()];
        const std::string& path = paths[rng() % paths.size()];
        const std::uint16_t status = kStatuses[status_distribution(rng)];
        const std::string_view method = kMethods[method_distribution(rng)];
        std::uint64_t bytes = rng() % 4096;
        if (rng() % 1000 < 20) {
            bytes = rng() % 8'000'000;
        }

        const std::string timestamp = hpla::format_http_timestamp(epoch);
        const std::string_view agent = kUserAgents[rng() % std::size(kUserAgents)];
        const std::string_view referrer = kReferrers[rng() % std::size(kReferrers)];

        const bool omit_bytes = no_bytes(rng);
        int length = 0;
        if (omit_bytes) {
            length = std::snprintf(line, sizeof(line), "%s - - [%s] \"%.*s %s HTTP/1.1\" %u -",
                                   ip.c_str(), timestamp.c_str(), static_cast<int>(method.size()),
                                   method.data(), path.c_str(), static_cast<unsigned>(status));
        } else {
            length = std::snprintf(line, sizeof(line), "%s - - [%s] \"%.*s %s HTTP/1.1\" %u %llu",
                                   ip.c_str(), timestamp.c_str(), static_cast<int>(method.size()),
                                   method.data(), path.c_str(), static_cast<unsigned>(status),
                                   static_cast<unsigned long long>(bytes));
        }

        std::string record(line, static_cast<std::size_t>(length));
        if (options.combined && !omit_bytes) {
            record += " \"";
            record += referrer;
            record += "\" \"";
            record += agent;
            record += '"';
        }

        buffer.append(record);
        buffer.append(terminator);
        last_valid = record;

        if (malformed(rng)) {
            const std::string bad = make_malformed(last_valid, rng);
            buffer.append(bad);
            buffer.append(terminator);
            ++malformed_written;
        }

        if (buffer.size() >= (4U << 20)) {
            out.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            bytes_written += buffer.size();
            buffer.clear();
        }
    }

    out.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    bytes_written += buffer.size();
    out.flush();
    if (!out) {
        std::cerr << "hpla_genlog: failed while writing '" << options.output << "'\n";
        return 1;
    }
    out.close();

    if (!options.trailing_newline && bytes_written >= terminator.size()) {
        std::error_code error;
        std::filesystem::resize_file(options.output, bytes_written - terminator.size(), error);
        if (error) {
            std::cerr << "hpla_genlog: cannot trim trailing newline: " << error.message() << '\n';
            return 1;
        }
        bytes_written -= terminator.size();
    }

    const double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time).count();

    std::cout << "Wrote " << hpla::format_uint(options.lines + malformed_written) << " lines ("
              << malformed_written << " malformed), " << hpla::format_human_bytes(bytes_written)
              << " to " << options.output << " in " << hpla::format_double(elapsed, 2) << " s\n";
    return 0;
}
