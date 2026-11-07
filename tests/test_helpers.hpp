#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace hpla_test {

inline std::filesystem::path make_temp_path(std::string_view tag) {
    static std::atomic<unsigned> counter{0};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("hpla_test_" + std::string(tag) + "_" + std::to_string(stamp) + "_" +
            std::to_string(counter.fetch_add(1)) + ".log");
}

class TempFile {
public:
    explicit TempFile(std::string_view contents, std::string_view tag = "data")
        : path_(make_temp_path(tag)) {
        std::ofstream out(path_, std::ios::binary | std::ios::trunc);
        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        out.close();
        size_ = std::filesystem::file_size(path_);
    }

    ~TempFile() {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;

    const std::filesystem::path& path() const { return path_; }
    std::string path_string() const { return path_.string(); }
    std::uint64_t size() const { return size_; }

private:
    std::filesystem::path path_;
    std::uint64_t size_ = 0;
};

// Generates a deterministic mixed valid/malformed corpus. Timestamps are one
// second apart starting at 2026-10-30T00:00:00Z.
inline std::string make_log(std::size_t lines, bool trailing_newline = true,
                            bool crlf = false) {
    static constexpr const char* kPaths[] = {"/", "/index.html", "/login", "/api/v1/users",
                                             "/search?q=logs", "/missing"};
    static constexpr const char* kStatuses[] = {"200", "200", "301", "404", "500"};
    static constexpr const char* kIps[] = {"127.0.0.1", "10.0.0.1", "10.0.0.2", "192.168.1.10",
                                           "2001:db8::1"};

    std::string text;
    const std::string_view newline = crlf ? "\r\n" : "\n";
    for (std::size_t i = 0; i < lines; ++i) {
        const std::uint64_t seconds = static_cast<std::uint64_t>(i);
        const std::uint64_t day = seconds / 86400;
        (void)day;
        char line[512];
        int length = 0;
        if (i % 17 == 0) {
            length = std::snprintf(line, sizeof(line), "malformed line %zu", i);
        } else {
            const int hour = static_cast<int>((seconds / 3600 + 8) % 24);
            const int minute = static_cast<int>((seconds / 60) % 60);
            const int second = static_cast<int>(seconds % 60);
            const std::uint64_t bytes = (i % 13) == 0 ? 0 : (i * 977) % 100000;
            length = std::snprintf(
                line, sizeof(line),
                "%s - - [30/Oct/2026:%02d:%02d:%02d +0000] \"GET %s HTTP/1.1\" %s %llu \"-\" "
                "\"test-agent/%zu\"",
                kIps[i % 5], hour, minute, second, kPaths[i % 6], kStatuses[i % 5],
                static_cast<unsigned long long>(bytes), i % 4);
        }
        text.append(line, static_cast<std::size_t>(length));
        text.append(newline);
    }
    if (!trailing_newline && !text.empty()) {
        text.resize(text.size() - newline.size());
    }
    return text;
}

}  // namespace hpla_test
