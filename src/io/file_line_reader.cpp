#include "io/file_line_reader.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace hpla {
namespace {

std::runtime_error io_error(const std::string& path, const char* what) {
    return std::runtime_error(std::string("file '") + path + "': " + what);
}

}  // namespace

FileLineReader::FileLineReader(std::string path, std::uint64_t start, std::uint64_t end,
                               std::size_t buffer_size)
    : path_(std::move(path)), buffer_(std::max<std::size_t>(buffer_size, 16)) {
    input_.open(path_, std::ios::binary);
    if (!input_) {
        throw io_error(path_, "cannot open for reading");
    }

    input_.seekg(0, std::ios::end);
    const std::streamoff size = input_.tellg();
    if (size < 0) {
        throw io_error(path_, "cannot determine size");
    }
    file_size_ = static_cast<std::uint64_t>(size);
    limit_ = std::min(end, file_size_);

    std::uint64_t aligned = std::min(start, file_size_);

    if (aligned > 0 && aligned < file_size_) {
        input_.seekg(static_cast<std::streamoff>(aligned - 1), std::ios::beg);
        char previous = 0;
        input_.read(&previous, 1);
        if (input_.gcount() != 1) {
            throw io_error(path_, "cannot read alignment byte");
        }
        if (previous != '\n') {
            // The chunk begins mid-line: skip to the next line start.
            std::array<char, 8192> scratch{};
            std::uint64_t absolute = aligned;
            bool found = false;
            while (true) {
                input_.read(scratch.data(), static_cast<std::streamsize>(scratch.size()));
                const std::streamsize got = input_.gcount();
                if (got <= 0) {
                    break;
                }
                const void* newline =
                    std::memchr(scratch.data(), '\n', static_cast<std::size_t>(got));
                if (newline != nullptr) {
                    const auto* position = static_cast<const char*>(newline);
                    aligned = absolute + static_cast<std::uint64_t>(position - scratch.data()) + 1;
                    found = true;
                    break;
                }
                absolute += static_cast<std::uint64_t>(got);
            }
            if (!found) {
                line_abs_ = file_size_;
                eof_ = true;
                return;
            }
        }
    }

    input_.clear();
    input_.seekg(static_cast<std::streamoff>(aligned), std::ios::beg);
    if (!input_) {
        throw io_error(path_, "cannot seek to chunk start");
    }
    line_abs_ = aligned;

    if (line_abs_ >= limit_ || line_abs_ >= file_size_) {
        eof_ = true;
    }
}

bool FileLineReader::next(std::string_view& line) {
    if (line_abs_ >= limit_ || line_abs_ >= file_size_ || (eof_ && consume_ >= fill_)) {
        return false;
    }

    for (;;) {
        if (consume_ < fill_) {
            const char* begin = buffer_.data() + consume_;
            const std::size_t available = fill_ - consume_;
            if (const void* found = std::memchr(begin, '\n', available)) {
                const auto* newline = static_cast<const char*>(found);
                const std::size_t newline_index =
                    consume_ + static_cast<std::size_t>(newline - begin);
                line = std::string_view(begin, newline_index - consume_);
                if (!line.empty() && line.back() == '\r') {
                    line.remove_suffix(1);
                }
                line_abs_ += static_cast<std::uint64_t>(newline_index + 1 - consume_);
                consume_ = newline_index + 1;
                if (consume_ == fill_) {
                    consume_ = 0;
                    fill_ = 0;
                }
                return true;
            }
        }

        if (eof_) {
            if (consume_ >= fill_) {
                return false;
            }
            line = std::string_view(buffer_.data() + consume_, fill_ - consume_);
            if (!line.empty() && line.back() == '\r') {
                line.remove_suffix(1);
            }
            line_abs_ += static_cast<std::uint64_t>(fill_ - consume_);
            consume_ = fill_;
            return true;
        }

        // Compact the buffer, growing it if a single line does not fit.
        if (consume_ > 0) {
            const std::size_t remaining = fill_ - consume_;
            if (remaining > 0) {
                std::memmove(buffer_.data(), buffer_.data() + consume_, remaining);
            }
            fill_ = remaining;
            consume_ = 0;
        }
        if (fill_ == buffer_.size()) {
            if (buffer_.size() > buffer_.max_size() / 2) {
                throw io_error(path_, "line is too long");
            }
            buffer_.resize(buffer_.size() * 2);
        }

        input_.read(buffer_.data() + fill_,
                    static_cast<std::streamsize>(buffer_.size() - fill_));
        const std::streamsize got = input_.gcount();
        if (got > 0) {
            fill_ += static_cast<std::size_t>(got);
            bytes_read_ += static_cast<std::uint64_t>(got);
        } else {
            if (input_.bad()) {
                throw io_error(path_, "read error");
            }
            eof_ = true;
        }
    }
}

}  // namespace hpla
