#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "engine/engine.hpp"
#include "export/exporter.hpp"
#include "filter/filter.hpp"

namespace hpla {

enum class ColorMode { Auto, Always, Never };

struct CliOptions {
    std::vector<std::string> files;
    FilterSet filter;

    unsigned threads = 0;  // 0 = automatic
    std::size_t buffer_size = 1U << 20;
    bool sequential = false;
    bool use_mmap = false;
    std::size_t top_n = 10;
    std::size_t max_distinct = 2'000'000;

    OutputFormat format = OutputFormat::Text;
    bool format_explicit = false;
    std::optional<std::string> output_path;
    bool include_records = false;
    ColorMode color = ColorMode::Auto;

    bool show_help = false;
    bool show_version = false;
};

struct CliParseResult {
    bool ok = false;
    CliOptions options;
    std::string error;
};

CliParseResult parse_cli(int argc, const char* const* argv);
std::string usage_text();
constexpr std::string_view kVersion = "1.0.0";

}  // namespace hpla
