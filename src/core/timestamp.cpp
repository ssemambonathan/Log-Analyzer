#include "core/timestamp.hpp"

#include <array>
#include <cstdio>

namespace hpla {
namespace {

constexpr std::array<std::string_view, 12> kMonthNames{
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
};

bool is_leap(int year) noexcept {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

unsigned days_in_month(int year, unsigned month) noexcept {
    constexpr std::array<unsigned, 12> kDays{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) {
        return 0;
    }
    return month == 2 && is_leap(year) ? 29U : kDays[month - 1];
}

int month_index(std::string_view name) noexcept {
    for (std::size_t i = 0; i < kMonthNames.size(); ++i) {
        if (kMonthNames[i] == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool read_digits(std::string_view text, std::size_t& pos, int count, int& value) noexcept {
    if (pos + static_cast<std::size_t>(count) > text.size()) {
        return false;
    }
    int result = 0;
    for (int i = 0; i < count; ++i) {
        const char c = text[pos++];
        if (c < '0' || c > '9') {
            return false;
        }
        result = result * 10 + (c - '0');
    }
    value = result;
    return true;
}

}  // namespace

bool parse_http_timestamp(std::string_view text, std::int64_t& epoch_seconds) noexcept {
    // dd/Mon/yyyy:HH:MM:SS +HHMM  (26 characters)
    if (text.size() != 26) {
        return false;
    }

    std::size_t pos = 0;
    int day = 0;
    int year = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;

    if (!read_digits(text, pos, 2, day) || text[pos++] != '/') {
        return false;
    }
    const int month = month_index(text.substr(pos, 3));
    if (month < 0) {
        return false;
    }
    pos += 3;
    if (text[pos++] != '/' || !read_digits(text, pos, 4, year)) {
        return false;
    }
    if (text[pos++] != ':' || !read_digits(text, pos, 2, hour)) {
        return false;
    }
    if (text[pos++] != ':' || !read_digits(text, pos, 2, minute)) {
        return false;
    }
    if (text[pos++] != ':' || !read_digits(text, pos, 2, second)) {
        return false;
    }
    if (text[pos++] != ' ') {
        return false;
    }

    int sign = 0;
    const char sign_char = text[pos++];
    if (sign_char == '+') {
        sign = 1;
    } else if (sign_char == '-') {
        sign = -1;
    } else {
        return false;
    }

    int offset_hours = 0;
    int offset_minutes = 0;
    if (!read_digits(text, pos, 2, offset_hours) || !read_digits(text, pos, 2, offset_minutes)) {
        return false;
    }
    if (pos != text.size()) {
        return false;
    }

    if (year < 1000 || year > 9999 || day < 1 ||
        static_cast<unsigned>(day) > days_in_month(year, static_cast<unsigned>(month + 1)) ||
        hour > 23 || minute > 59 || second > 60 || offset_hours > 23 || offset_minutes > 59) {
        return false;
    }

    const std::int64_t offset = static_cast<std::int64_t>(offset_hours) * 3600 +
                                static_cast<std::int64_t>(offset_minutes) * 60;
    const std::int64_t days =
        days_from_civil(year, static_cast<unsigned>(month + 1), static_cast<unsigned>(day));
    epoch_seconds = days * 86400 + hour * 3600 + minute * 60 + second - sign * offset;
    return true;
}

std::string format_iso8601_utc(std::int64_t epoch_seconds) {
    std::int64_t days = epoch_seconds / 86400;
    std::int64_t rem = epoch_seconds % 86400;
    if (rem < 0) {
        rem += 86400;
        --days;
    }

    std::int64_t year = 0;
    unsigned month = 0;
    unsigned day = 0;
    civil_from_days(days, year, month, day);

    const auto hour = static_cast<unsigned>(rem / 3600);
    const auto minute = static_cast<unsigned>((rem % 3600) / 60);
    const auto second = static_cast<unsigned>(rem % 60);

    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%04lld-%02u-%02uT%02u:%02u:%02uZ",
                  static_cast<long long>(year), month, day, hour, minute, second);
    return std::string(buffer);
}

std::string format_http_timestamp(std::int64_t epoch_seconds) {
    std::int64_t days = epoch_seconds / 86400;
    std::int64_t rem = epoch_seconds % 86400;
    if (rem < 0) {
        rem += 86400;
        --days;
    }

    std::int64_t year = 0;
    unsigned month = 0;
    unsigned day = 0;
    civil_from_days(days, year, month, day);

    const auto hour = static_cast<unsigned>(rem / 3600);
    const auto minute = static_cast<unsigned>((rem % 3600) / 60);
    const auto second = static_cast<unsigned>(rem % 60);

    const std::string_view month_name = kMonthNames[month - 1];
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%02u/%.3s/%04lld:%02u:%02u:%02u +0000", day,
                  month_name.data(), static_cast<long long>(year), hour, minute, second);
    return std::string(buffer);
}

}  // namespace hpla
