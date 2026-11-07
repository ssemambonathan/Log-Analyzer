#include "io/mapped_file.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace hpla {
namespace {

std::runtime_error map_error(const std::string& path, const char* what) {
    return std::runtime_error(std::string("file '") + path + "': " + what);
}

}  // namespace

MappedFile::MappedFile(const std::string& path) {
#if defined(_WIN32)
    const HANDLE file = CreateFileA(path.c_str(), GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        throw map_error(path, "cannot open for reading");
    }
    LARGE_INTEGER file_size{};
    if (GetFileSizeEx(file, &file_size) == 0) {
        CloseHandle(file);
        throw map_error(path, "cannot determine size");
    }
    size_ = static_cast<std::uint64_t>(file_size.QuadPart);
    if (size_ == 0) {
        CloseHandle(file);
        return;
    }
    const HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (mapping == nullptr) {
        CloseHandle(file);
        throw map_error(path, "cannot create file mapping");
    }
    void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (view == nullptr) {
        CloseHandle(mapping);
        CloseHandle(file);
        throw map_error(path, "cannot map view of file");
    }
    view_ = view;
    data_ = static_cast<const char*>(view);
    os_file_ = reinterpret_cast<std::intptr_t>(file);
    os_mapping_ = reinterpret_cast<std::intptr_t>(mapping);
#else
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        throw map_error(path, "cannot open for reading");
    }
    struct stat status {};
    if (::fstat(fd, &status) != 0) {
        ::close(fd);
        throw map_error(path, "cannot determine size");
    }
    size_ = static_cast<std::uint64_t>(status.st_size);
    if (size_ == 0) {
        ::close(fd);
        return;
    }
    void* view = ::mmap(nullptr, static_cast<std::size_t>(size_), PROT_READ, MAP_PRIVATE, fd, 0);
    if (view == MAP_FAILED) {
        ::close(fd);
        throw map_error(path, "mmap failed");
    }
    view_ = view;
    data_ = static_cast<const char*>(view);
    os_file_ = fd;
#endif
}

MappedFile::~MappedFile() {
    release();
}

MappedFile::MappedFile(MappedFile&& other) noexcept {
    *this = std::move(other);
}

MappedFile& MappedFile::operator=(MappedFile&& other) noexcept {
    if (this != &other) {
        release();
        view_ = other.view_;
        os_file_ = other.os_file_;
        os_mapping_ = other.os_mapping_;
        data_ = other.data_;
        size_ = other.size_;
        other.view_ = nullptr;
        other.os_file_ = -1;
        other.os_mapping_ = -1;
        other.data_ = nullptr;
        other.size_ = 0;
    }
    return *this;
}

void MappedFile::release() noexcept {
#if defined(_WIN32)
    if (view_ != nullptr) {
        UnmapViewOfFile(view_);
        view_ = nullptr;
    }
    if (os_mapping_ != -1) {
        CloseHandle(reinterpret_cast<HANDLE>(os_mapping_));
        os_mapping_ = -1;
    }
    if (os_file_ != -1) {
        CloseHandle(reinterpret_cast<HANDLE>(os_file_));
        os_file_ = -1;
    }
#else
    if (view_ != nullptr) {
        ::munmap(view_, static_cast<std::size_t>(size_));
        view_ = nullptr;
    }
    if (os_file_ != -1) {
        ::close(static_cast<int>(os_file_));
        os_file_ = -1;
    }
#endif
    data_ = nullptr;
    size_ = 0;
}

MappedLineReader::MappedLineReader(const MappedFile& file, std::uint64_t start, std::uint64_t end)
    : data_(file.data()), size_(file.size()) {
    limit_ = std::min(end, size_);
    pos_ = std::min(start, size_);

    if (pos_ > 0 && pos_ < size_ && data_[pos_ - 1] != '\n') {
        const char* base = data_ + pos_;
        const void* newline = std::memchr(base, '\n', static_cast<std::size_t>(size_ - pos_));
        if (newline == nullptr) {
            pos_ = size_;
        } else {
            const auto* found = static_cast<const char*>(newline);
            pos_ += static_cast<std::uint64_t>(found - base) + 1;
        }
    }

    if (pos_ >= limit_ || pos_ >= size_) {
        ended_ = true;
    }
}

bool MappedLineReader::next(std::string_view& line) {
    if (ended_ || pos_ >= limit_ || pos_ >= size_) {
        return false;
    }

    const char* begin = data_ + pos_;
    const std::size_t remaining = static_cast<std::size_t>(size_ - pos_);
    const void* newline = std::memchr(begin, '\n', remaining);
    if (newline != nullptr) {
        const auto* found = static_cast<const char*>(newline);
        const std::size_t length = static_cast<std::size_t>(found - begin);
        line = std::string_view(begin, length);
        pos_ += static_cast<std::uint64_t>(length) + 1;
    } else {
        line = std::string_view(begin, remaining);
        pos_ = size_;
        ended_ = true;
    }

    if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
    }
    return true;
}

}  // namespace hpla
