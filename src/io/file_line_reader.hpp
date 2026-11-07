#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace hpla {

// Buffered sequential line reader over a byte range of a file.
//
// The range [start, end) is a chunk assignment, not necessarily a set of whole
// lines: `start` is aligned forward to the first line beginning at or after it
// (unless the previous byte is already '\n'), and reading stops after the last
// line whose first byte lies before `end`. Lines that begin before `end` are
// returned in full, even when they end beyond it. This makes adjacent chunks
// partition a file without duplicating or splitting records.
//
// The returned view points into the reader's internal buffer and is valid only
// until the next call to next(). Both LF and CRLF are accepted; a trailing
// carriage return is stripped. A final line without a newline is returned.
class FileLineReader {
public:
    FileLineReader(std::string path, std::uint64_t start, std::uint64_t end,
                   std::size_t buffer_size);

    // Returns false at the end of the assigned range. Throws std::runtime_error
    // on open or read failure.
    bool next(std::string_view& line);

    std::uint64_t bytes_read() const noexcept { return bytes_read_; }
    std::uint64_t file_size() const noexcept { return file_size_; }

private:
    std::string path_;
    std::ifstream input_;
    std::vector<char> buffer_;
    std::size_t fill_ = 0;     // valid bytes at the front of buffer_
    std::size_t consume_ = 0;  // start of the current line within buffer_
    std::uint64_t file_size_ = 0;
    std::uint64_t limit_ = 0;   // raw end offset of the assigned chunk
    std::uint64_t line_abs_ = 0;  // absolute file offset of the next line start
    std::uint64_t bytes_read_ = 0;
    bool eof_ = false;
};

}  // namespace hpla
