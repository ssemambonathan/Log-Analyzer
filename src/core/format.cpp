#include "core/format.hpp"

#include <array>
#include <cctype>
#include <cstdio>
#include <limits>

namespace hpla {
namespace {

std::string group_digits(std::string digits) {
    std::string result;
    result.reserve(digits.size() + digits.size() / 3);
    const std::size_t first = digits.size() % 3 == 0 ? 3 : digits.size() % 3;
    std::size_t pos = 0;
    if (first != 0) {
        result.append(digits, 0, first);
        pos = first;
    }
    while (pos < digits.size()) {
        if (!result.empty()) {
            result.push_back(',');
        }
        result.append(digits, pos, 3);
        pos += 3;
    }
    return result;
}

}  // namespace

std::string format_uint(std::uint64_t value) {
    return group_digits(std::to_string(value));
}

std::string format_human_bytes(std::uint64_t bytes) {
    constexpr std::array<std::string_view, 7> kUnits{"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB"};
    if (bytes < 1024) {
        return std::to_string(bytes) + " B";
    }
    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < kUnits.size()) {
        value /= 1024.0;
        ++unit;
    }
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.2f %s", value, std::string(kUnits[unit]).c_str());
    return std::string(buffer);
}

std::string format_double(double value, int decimals) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
    return std::string(buffer);
}

std::string format_rate(double bytes_per_second) {
    constexpr std::array<std::string_view, 7> kUnits{"B/s", "KiB/s", "MiB/s", "GiB/s", "TiB/s", "PiB/s", "EiB/s"};
    double value = bytes_per_second;
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < kUnits.size()) {
        value /= 1024.0;
        ++unit;
    }
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.2f %s", value, std::string(kUnits[unit]).c_str());
    return std::string(buffer);
}

std::string status_text(std::uint16_t status) {
    switch (status) {
        case 100: return "Continue";
        case 101: return "Switching Protocols";
        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 204: return "No Content";
        case 206: return "Partial Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 303: return "See Other";
        case 304: return "Not Modified";
        case 307: return "Temporary Redirect";
        case 308: return "Permanent Redirect";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 409: return "Conflict";
        case 410: return "Gone";
        case 413: return "Payload Too Large";
        case 414: return "URI Too Long";
        case 418: return "I'm a Teapot";
        case 429: return "Too Many Requests";
        case 451: return "Unavailable For Legal Reasons";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        case 504: return "Gateway Timeout";
        case 505: return "HTTP Version Not Supported";
        default: break;
    }
    const std::uint16_t group = static_cast<std::uint16_t>(status / 100);
    switch (group) {
        case 1: return "Informational";
        case 2: return "Success";
        case 3: return "Redirection";
        case 4: return "Client Error";
        case 5: return "Server Error";
        default: return "Unknown";
    }
}

std::string sanitize_for_terminal(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (const char raw : text) {
        const auto c = static_cast<unsigned char>(raw);
        if (c < 0x20 || c == 0x7F) {
            result.push_back('?');
        } else {
            result.push_back(raw);
        }
    }
    return result;
}

std::string uppercase_ascii(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (const char c : text) {
        result.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    return result;
}

bool parse_uint64(std::string_view text, std::uint64_t& value) noexcept {
    if (text.empty()) {
        return false;
    }
    std::uint64_t result = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
        const auto digit = static_cast<std::uint64_t>(c - '0');
        if (result > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) {
            return false;
        }
        result = result * 10 + digit;
    }
    value = result;
    return true;
}

bool parse_size(std::string_view text, std::uint64_t& value) noexcept {
    if (text.empty()) {
        return false;
    }
    std::size_t pos = 0;
    std::uint64_t number = 0;
    while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
        const auto digit = static_cast<std::uint64_t>(text[pos] - '0');
        if (number > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) {
            return false;
        }
        number = number * 10 + digit;
        ++pos;
    }
    if (pos == 0) {
        return false;
    }

    std::string_view suffix = text.substr(pos);
    while (!suffix.empty() && suffix.back() == ' ') {
        suffix.remove_suffix(1);
    }
    if (suffix.empty()) {
        value = number;
        return true;
    }

    std::string lower = uppercase_ascii(suffix);
    std::uint64_t multiplier = 1;
    if (lower == "B") {
        multiplier = 1;
    } else if (lower == "K" || lower == "KB" || lower == "KIB") {
        multiplier = 1024ULL;
    } else if (lower == "M" || lower == "MB" || lower == "MIB") {
        multiplier = 1024ULL * 1024;
    } else if (lower == "G" || lower == "GB" || lower == "GIB") {
        multiplier = 1024ULL * 1024 * 1024;
    } else if (lower == "T" || lower == "TB" || lower == "TIB") {
        multiplier = 1024ULL * 1024 * 1024 * 1024;
    } else {
        return false;
    }

    if (number > std::numeric_limits<std::uint64_t>::max() / multiplier) {
        return false;
    }
    value = number * multiplier;
    return true;
}

}  // namespace hpla
