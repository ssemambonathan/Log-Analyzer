#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace hpla {

// Read-only memory mapping of a whole file. Empty files are valid and report
// size() == 0 with data() == nullptr. Throws std::runtime_error on failure.
class MappedFile {
public:
    explicit MappedFile(const std::string& path);
    ~MappedFile();

    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;
    MappedFile(MappedFile&& other) noexcept;
    MappedFile& operator=(MappedFile&& other) noexcept;

    const char* data() const noexcept { return data_; }
    std::uint64_t size() const noexcept { return size_; }

private:
    void release() noexcept;

    void* view_ = nullptr;
    std::intptr_t os_file_ = -1;
    std::intptr_t os_mapping_ = -1;
    const char* data_ = nullptr;
    std::uint64_t size_ = 0;
};

// Line reader over a memory-mapped file. Range semantics match FileLineReader:
// `start` is aligned to the next line start when it falls inside a line, and
// only lines that begin before `end` are returned.
class MappedLineReader {
public:
    MappedLineReader(const MappedFile& file, std::uint64_t start, std::uint64_t end);

    bool next(std::string_view& line);

private:
    const char* data_ = nullptr;
    std::uint64_t size_ = 0;
    std::uint64_t pos_ = 0;
    std::uint64_t limit_ = 0;
    bool ended_ = false;
};

}  // namespace hpla
