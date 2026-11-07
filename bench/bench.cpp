#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "core/format.hpp"
#include "core/timestamp.hpp"
#include "engine/engine.hpp"
#include "parser/log_parser.hpp"

namespace {

struct BenchOptions {
    std::uint64_t lines = 500'000;
    unsigned threads = 0;
    unsigned repeat = 3;
};

std::string timestamp_for(std::int64_t epoch) {
    return hpla::format_http_timestamp(epoch);
}

std::string build_corpus(std::uint64_t lines, std::vector<std::string_view>& views) {
    static constexpr std::string_view kPaths[] = {
        "/", "/index.html", "/login", "/api/v1/users", "/api/v1/orders", "/static/app.js",
        "/images/logo.png", "/search?q=logs", "/checkout", "/health",
    };
    static constexpr std::string_view kStatuses[] = {"200", "200", "200", "200", "301",
                                                     "304", "404", "500"};
    static constexpr std::string_view kAgents[] = {
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/124.0.0.0 Safari/537.36",
        "curl/8.6.0",
        "python-requests/2.31.0",
        "Mozilla/5.0 (compatible; Googlebot/2.1; +http://www.google.com/bot.html)",
    };

    std::mt19937_64 rng(12345);
    std::string corpus;
    corpus.reserve(static_cast<std::size_t>(lines) * 180 + 64);
    std::int64_t epoch = 1793318400;  // 2026-10-30T00:00:00Z

    char line[512];
    for (std::uint64_t i = 0; i < lines; ++i) {
        epoch += static_cast<std::int64_t>(rng() % 2);
        const std::string timestamp = timestamp_for(epoch);
        const std::uint64_t address = rng() % 50000;
        const std::string_view path = kPaths[rng() % std::size(kPaths)];
        const std::string_view status = kStatuses[rng() % std::size(kStatuses)];
        const std::string_view agent = kAgents[rng() % std::size(kAgents)];
        const std::uint64_t bytes = rng() % 100000;

        const int length = std::snprintf(
            line, sizeof(line),
            "10.%u.%u.%u - - [%s] \"GET %.*s HTTP/1.1\" %.*s %llu \"-\" \"%.*s\"",
            static_cast<unsigned>((address >> 16) & 0xFF),
            static_cast<unsigned>((address >> 8) & 0xFF), static_cast<unsigned>(address & 0xFF),
            timestamp.c_str(), static_cast<int>(path.size()), path.data(),
            static_cast<int>(status.size()), status.data(),
            static_cast<unsigned long long>(bytes), static_cast<int>(agent.size()), agent.data());
        corpus.append(line, static_cast<std::size_t>(length));
        corpus.push_back('\n');
    }

    views.clear();
    views.reserve(static_cast<std::size_t>(lines));
    std::size_t start = 0;
    while (start < corpus.size()) {
        const std::size_t newline = corpus.find('\n', start);
        const std::size_t end = newline == std::string::npos ? corpus.size() : newline;
        views.push_back(std::string_view(corpus).substr(start, end - start));
        start = end + 1;
    }
    return corpus;
}

std::uint64_t run_parser_benchmark(const std::vector<std::string_view>& lines, unsigned repeat) {
    double best_ns_per_line = 1e300;
    std::uint64_t checksum = 0;

    for (unsigned iteration = 0; iteration < repeat; ++iteration) {
        const auto start = std::chrono::steady_clock::now();
        std::uint64_t local = 0;
        std::uint64_t malformed = 0;
        for (const std::string_view line : lines) {
            hpla::LogEntry entry;
            const hpla::ParseResult result = hpla::parse_log_line(line, entry);
            if (result.ok) {
                local += entry.status;
            } else {
                ++malformed;
            }
        }
        const auto end = std::chrono::steady_clock::now();
        const double seconds = std::chrono::duration<double>(end - start).count();
        const double ns_per_line =
            seconds * 1e9 / static_cast<double>(std::max<std::size_t>(lines.size(), 1));
        best_ns_per_line = std::min(best_ns_per_line, ns_per_line);
        checksum += local + malformed;
    }

    std::cout << "  parser: " << hpla::format_double(best_ns_per_line, 1) << " ns/line, "
              << hpla::format_double(1e9 / best_ns_per_line / 1e6, 2) << " M lines/s best of "
              << repeat << "\n";
    return checksum;
}

}  // namespace

int main(int argc, char** argv) {
    BenchOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        auto require = [&]() -> std::string {
            if (index + 1 >= argc) {
                std::cerr << "hpla_bench: " << arg << " requires a value\n";
                std::exit(2);
            }
            return argv[++index];
        };
        if (arg == "--lines") {
            std::uint64_t value = 0;
            if (!hpla::parse_uint64(require(), value) || value == 0) {
                std::cerr << "hpla_bench: invalid --lines\n";
                return 2;
            }
            options.lines = value;
        } else if (arg == "--threads" || arg == "-t") {
            std::uint64_t value = 0;
            if (!hpla::parse_uint64(require(), value) || value > 256) {
                std::cerr << "hpla_bench: invalid --threads\n";
                return 2;
            }
            options.threads = static_cast<unsigned>(value);
        } else if (arg == "--repeat") {
            std::uint64_t value = 0;
            if (!hpla::parse_uint64(require(), value) || value == 0 || value > 100) {
                std::cerr << "hpla_bench: invalid --repeat\n";
                return 2;
            }
            options.repeat = static_cast<unsigned>(value);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: hpla_bench [--lines N] [--threads N] [--repeat N]\n";
            return 0;
        } else {
            std::cerr << "hpla_bench: unknown option '" << arg << "'\n";
            return 2;
        }
    }

    std::cout << "HPLA benchmark\n";

    std::vector<std::string_view> views;
    const std::string corpus = build_corpus(options.lines, views);
    std::cout << "Corpus: " << hpla::format_uint(options.lines) << " lines, "
              << hpla::format_human_bytes(corpus.size()) << "\n";

    const std::uint64_t parser_checksum = run_parser_benchmark(views, options.repeat);

    const std::filesystem::path temp_path =
        std::filesystem::temp_directory_path() / "hpla_bench_corpus.log";
    {
        std::ofstream out(temp_path, std::ios::binary | std::ios::trunc);
        out.write(corpus.data(), static_cast<std::streamsize>(corpus.size()));
    }

    const std::vector<hpla::FileInfo> files{{temp_path.string(),
                                             static_cast<std::uint64_t>(corpus.size())}};
    const hpla::FilterSet filter;

    hpla::EngineOptions engine;
    engine.threads = options.threads;
    engine.buffer_size = 1U << 20;

    engine.sequential = true;
    const hpla::AnalysisResult sequential = hpla::LogAnalyzer::run(files, filter, engine);
    std::cout << "  sequential buffered: " << hpla::format_rate(sequential.performance.throughput_bytes_per_second)
              << ", " << hpla::format_uint(static_cast<std::uint64_t>(sequential.performance.lines_per_second))
              << " lines/s, elapsed "
              << hpla::format_double(sequential.performance.elapsed_seconds, 3) << " s\n";

    engine.sequential = false;
    const hpla::AnalysisResult parallel = hpla::LogAnalyzer::run(files, filter, engine);
    std::cout << "  parallel buffered (" << parallel.performance.threads << " threads): "
              << hpla::format_rate(parallel.performance.throughput_bytes_per_second) << ", "
              << hpla::format_uint(static_cast<std::uint64_t>(parallel.performance.lines_per_second))
              << " lines/s, elapsed "
              << hpla::format_double(parallel.performance.elapsed_seconds, 3) << " s, speedup "
              << hpla::format_double(
                     sequential.performance.elapsed_seconds / parallel.performance.elapsed_seconds,
                     2)
              << "x\n";

    engine.use_mmap = true;
    const hpla::AnalysisResult mapped = hpla::LogAnalyzer::run(files, filter, engine);
    std::cout << "  parallel " << mapped.performance.io_mode << " ("
              << mapped.performance.threads << " threads): "
              << hpla::format_rate(mapped.performance.throughput_bytes_per_second) << ", "
              << hpla::format_uint(static_cast<std::uint64_t>(mapped.performance.lines_per_second))
              << " lines/s, elapsed "
              << hpla::format_double(mapped.performance.elapsed_seconds, 3) << " s\n";

    const bool consistent = sequential.statistics.lines_examined == parallel.statistics.lines_examined &&
                            sequential.statistics.matched_records == parallel.statistics.matched_records &&
                            parallel.statistics.lines_examined == mapped.statistics.lines_examined &&
                            parser_checksum != 0;
    std::cout << "  consistency check: " << (consistent ? "OK" : "MISMATCH") << "\n";

    std::error_code error;
    std::filesystem::remove(temp_path, error);
    return consistent ? 0 : 1;
}
