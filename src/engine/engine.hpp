#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "filter/filter.hpp"
#include "stats/accumulator.hpp"

namespace hpla {

struct EngineOptions {
    unsigned threads = 0;             // 0 = hardware concurrency
    std::size_t buffer_size = 1U << 20;  // per reader
    bool sequential = false;
    bool use_mmap = false;
    std::size_t top_n = 10;
    std::size_t max_distinct = 2'000'000;  // per worker map limit; 0 = unlimited
    std::uint64_t chunk_size = 0;          // 0 = automatic; mainly for tests
};

// Opens every input file to validate readability and reads its size.
// Throws std::runtime_error when a file cannot be opened.
std::vector<FileInfo> stat_input_files(const std::vector<std::string>& paths);

// Installs a SIGINT handler so long runs can be cancelled with Ctrl+C.
void install_interrupt_handler();
bool interrupted() noexcept;

class LogAnalyzer {
public:
    // Processes all files concurrently in line-aligned chunks (or sequentially
    // when requested) and returns merged statistics plus measured performance.
    static AnalysisResult run(const std::vector<FileInfo>& files, const FilterSet& filter,
                              const EngineOptions& options);
};

}  // namespace hpla
