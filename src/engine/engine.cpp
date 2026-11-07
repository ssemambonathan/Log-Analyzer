#include "engine/engine.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <fstream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

#include "io/file_line_reader.hpp"
#include "io/mapped_file.hpp"

namespace hpla {
namespace {

std::atomic<bool> g_interrupted{false};

void handle_interrupt(int) {
    g_interrupted.store(true, std::memory_order_relaxed);
}

struct Chunk {
    std::size_t file_index = 0;
    std::uint64_t begin = 0;
    std::uint64_t end = 0;
};

unsigned default_thread_count() {
    const unsigned hardware = std::thread::hardware_concurrency();
    if (hardware == 0) {
        return 4;
    }
    return std::min(hardware, 128U);
}

unsigned resolve_threads(const EngineOptions& options) {
    if (options.sequential) {
        return 1;
    }
    const unsigned requested = options.threads == 0 ? default_thread_count() : options.threads;
    return std::clamp(requested, 1U, 256U);
}

std::vector<Chunk> make_chunks(const std::vector<FileInfo>& files, unsigned threads,
                               const EngineOptions& options) {
    const std::size_t target_parallelism =
        std::max<std::size_t>(1, static_cast<std::size_t>(threads) * 8U);

    std::vector<Chunk> chunks;
    for (std::size_t file_index = 0; file_index < files.size(); ++file_index) {
        const std::uint64_t size = files[file_index].size;
        if (size == 0) {
            continue;
        }
        std::uint64_t chunk_size = options.chunk_size;
        if (chunk_size == 0) {
            const std::uint64_t per_file =
                (size + target_parallelism - 1) / target_parallelism;
            chunk_size = std::max<std::uint64_t>(1U << 20, per_file);
        }
        chunk_size = std::max<std::uint64_t>(chunk_size, 1);
        for (std::uint64_t begin = 0; begin < size; begin += chunk_size) {
            const std::uint64_t end = std::min(size, begin + chunk_size);
            chunks.push_back(Chunk{file_index, begin, end});
            if (end <= begin) {
                break;
            }
        }
    }
    return chunks;
}

template <typename Reader>
void consume(Reader& reader, Accumulator& accumulator) {
    std::string_view line;
    std::uint64_t lines = 0;
    while (reader.next(line)) {
        accumulator.add_line(line);
        ++lines;
        if ((lines & 0x3FFU) == 0 && interrupted()) {
            break;
        }
    }
}

std::unique_ptr<MappedFile> try_map(const std::string& path) {
    try {
        return std::make_unique<MappedFile>(path);
    } catch (const std::exception&) {
        return nullptr;
    }
}

std::string io_mode_text(bool used_mmap, bool sequential) {
    std::string mode = used_mmap ? "mmap" : "buffered";
    mode += sequential ? " (sequential)" : " (parallel chunks)";
    return mode;
}

}  // namespace

std::vector<FileInfo> stat_input_files(const std::vector<std::string>& paths) {
    std::vector<FileInfo> files;
    files.reserve(paths.size());
    for (const std::string& path : paths) {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            throw std::runtime_error("cannot open input file '" + path + "'");
        }
        input.seekg(0, std::ios::end);
        const std::streamoff size = input.tellg();
        if (size < 0) {
            throw std::runtime_error("cannot determine size of '" + path + "'");
        }
        files.push_back(FileInfo{path, static_cast<std::uint64_t>(size)});
    }
    return files;
}

void install_interrupt_handler() {
    std::signal(SIGINT, handle_interrupt);
}

bool interrupted() noexcept {
    return g_interrupted.load(std::memory_order_relaxed);
}

AnalysisResult LogAnalyzer::run(const std::vector<FileInfo>& files, const FilterSet& filter,
                                const EngineOptions& options) {
    const auto start_time = std::chrono::steady_clock::now();

    std::uint64_t total_bytes = 0;
    for (const FileInfo& file : files) {
        total_bytes += file.size;
    }

    const unsigned resolved_threads = resolve_threads(options);
    unsigned active_threads = options.sequential ? 1U : resolved_threads;
    std::vector<Accumulator> accumulators(active_threads,
                                          Accumulator(&filter, options.max_distinct));

    // Memory mapping is optional; when any file cannot be mapped we quietly use
    // the buffered reader for all files instead.
    std::vector<std::unique_ptr<MappedFile>> mappings;
    bool used_mmap = false;
    if (options.use_mmap) {
        mappings.reserve(files.size());
        bool all_mapped = true;
        for (const FileInfo& file : files) {
            auto mapping = try_map(file.path);
            if (!mapping) {
                all_mapped = false;
                break;
            }
            mappings.push_back(std::move(mapping));
        }
        if (all_mapped) {
            used_mmap = true;
        } else {
            mappings.clear();
        }
    }

    if (options.sequential) {
        for (std::size_t index = 0; index < files.size() && !interrupted(); ++index) {
            if (used_mmap) {
                MappedLineReader reader(*mappings[index], 0, files[index].size);
                consume(reader, accumulators[0]);
            } else {
                FileLineReader reader(files[index].path, 0, files[index].size,
                                      options.buffer_size);
                consume(reader, accumulators[0]);
            }
        }
    } else {
        const std::vector<Chunk> chunks = make_chunks(files, resolved_threads, options);
        if (!chunks.empty()) {
            const std::size_t workers_needed = std::min<std::size_t>(chunks.size(), 256);
            active_threads = static_cast<unsigned>(
                std::min<std::size_t>(static_cast<std::size_t>(resolved_threads), workers_needed));
        } else {
            active_threads = 1;
        }

        std::atomic<std::size_t> next_chunk{0};
        std::atomic<bool> failed{false};
        std::mutex error_mutex;
        std::string error_message;

        {
            std::vector<std::jthread> workers;
            workers.reserve(active_threads);
            for (unsigned worker = 0; worker < active_threads; ++worker) {
                workers.emplace_back([&, worker](std::stop_token stop_token) {
                    Accumulator& accumulator = accumulators[worker];
                    try {
                        while (!failed.load(std::memory_order_relaxed) && !interrupted() &&
                               !stop_token.stop_requested()) {
                            const std::size_t index =
                                next_chunk.fetch_add(1, std::memory_order_relaxed);
                            if (index >= chunks.size()) {
                                break;
                            }
                            const Chunk& chunk = chunks[index];
                            const FileInfo& file = files[chunk.file_index];
                            if (used_mmap) {
                                MappedLineReader reader(*mappings[chunk.file_index], chunk.begin,
                                                        chunk.end);
                                consume(reader, accumulator);
                            } else {
                                FileLineReader reader(file.path, chunk.begin, chunk.end,
                                                      options.buffer_size);
                                consume(reader, accumulator);
                            }
                        }
                    } catch (const std::exception& error) {
                        {
                            const std::lock_guard<std::mutex> lock(error_mutex);
                            if (error_message.empty()) {
                                error_message = error.what();
                            }
                        }
                        failed.store(true, std::memory_order_relaxed);
                    } catch (...) {
                        {
                            const std::lock_guard<std::mutex> lock(error_mutex);
                            if (error_message.empty()) {
                                error_message = "unknown worker failure";
                            }
                        }
                        failed.store(true, std::memory_order_relaxed);
                    }
                });
            }
            // join() waits without requesting a stop, unlike ~jthread(), which
            // would cancel the workers as soon as this scope ends.
            for (std::jthread& worker : workers) {
                worker.join();
            }
        }

        if (failed.load(std::memory_order_relaxed)) {
            throw std::runtime_error(error_message.empty() ? "worker failed" : error_message);
        }
    }

    AnalysisResult result;
    result.statistics = build_statistics(accumulators, options.top_n, files.size(), total_bytes);
    result.performance.bytes_processed = total_bytes;
    result.performance.lines_examined = result.statistics.lines_examined;
    result.performance.threads = options.sequential ? 1U : active_threads;
    result.performance.io_mode = io_mode_text(used_mmap, options.sequential);
    result.performance.cancelled = interrupted();

    const auto end_time = std::chrono::steady_clock::now();
    const std::chrono::duration<double> elapsed = end_time - start_time;
    result.performance.elapsed_seconds = elapsed.count();
    if (elapsed.count() > 0.0) {
        result.performance.throughput_bytes_per_second =
            static_cast<double>(total_bytes) / elapsed.count();
        result.performance.lines_per_second =
            static_cast<double>(result.statistics.lines_examined) / elapsed.count();
    }
    return result;
}

}  // namespace hpla
