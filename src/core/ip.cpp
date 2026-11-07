#include "core/ip.hpp"

#include <cstdio>
#include <string>

namespace hpla {
namespace {

int hex_value(char c) noexcept {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

bool parse_ipv4(std::string_view text, std::uint8_t out[4]) noexcept {
    int octets = 0;
    std::size_t pos = 0;
    while (octets < 4) {
        if (pos >= text.size()) {
            return false;
        }
        unsigned value = 0;
        int digits = 0;
        while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
            if (digits == 3) {
                return false;
            }
            value = value * 10 + static_cast<unsigned>(text[pos] - '0');
            if (value > 255) {
                return false;
            }
            ++pos;
            ++digits;
        }
        if (digits == 0) {
            return false;
        }
        out[octets++] = static_cast<std::uint8_t>(value);
        if (octets == 4) {
            break;
        }
        if (pos >= text.size() || text[pos] != '.') {
            return false;
        }
        ++pos;
    }
    return pos == text.size();
}

bool parse_groups(std::string_view part, std::uint16_t* groups, std::size_t& count,
                  bool allow_ipv4_tail) noexcept {
    if (part.empty()) {
        return true;
    }
    std::size_t start = 0;
    while (true) {
        const std::size_t colon = part.find(':', start);
        const std::string_view token =
            part.substr(start, colon == std::string_view::npos ? std::string_view::npos : colon - start);
        if (token.empty()) {
            return false;
        }
        if (token.find('.') != std::string_view::npos) {
            std::uint8_t v4[4];
            if (!allow_ipv4_tail || colon != std::string_view::npos || count > 6 || !parse_ipv4(token, v4)) {
                return false;
            }
            groups[count++] = static_cast<std::uint16_t>((v4[0] << 8) | v4[1]);
            groups[count++] = static_cast<std::uint16_t>((v4[2] << 8) | v4[3]);
            return true;
        }
        if (token.size() > 4) {
            return false;
        }
        std::uint16_t value = 0;
        for (const char c : token) {
            const int digit = hex_value(c);
            if (digit < 0) {
                return false;
            }
            value = static_cast<std::uint16_t>((static_cast<unsigned>(value) << 4U) |
                                               static_cast<unsigned>(digit));
        }
        if (count >= 8) {
            return false;
        }
        groups[count++] = value;
        if (colon == std::string_view::npos) {
            return true;
        }
        start = colon + 1;
    }
}

bool parse_ipv6(std::string_view text, std::array<std::uint8_t, 16>& out) noexcept {
    if (text.empty()) {
        return false;
    }
    if (text.front() == ':' && (text.size() < 2 || text[1] != ':')) {
        return false;
    }

    const std::size_t compress = text.find("::");
    const bool has_compress = compress != std::string_view::npos;
    if (has_compress && text.find("::", compress + 2) != std::string_view::npos) {
        return false;
    }

    std::array<std::uint16_t, 8> groups{};
    std::size_t total = 0;

    if (!has_compress) {
        std::size_t count = 0;
        if (!parse_groups(text, groups.data(), count, true) || count != 8) {
            return false;
        }
        total = count;
    } else {
        const std::string_view left = text.substr(0, compress);
        const std::string_view right = text.substr(compress + 2);

        std::array<std::uint16_t, 8> left_groups{};
        std::array<std::uint16_t, 8> right_groups{};
        std::size_t left_count = 0;
        std::size_t right_count = 0;
        if (!parse_groups(left, left_groups.data(), left_count, false) ||
            !parse_groups(right, right_groups.data(), right_count, true)) {
            return false;
        }
        if (left_count + right_count > 7) {
            return false;
        }
        for (std::size_t i = 0; i < left_count; ++i) {
            groups[total++] = left_groups[i];
        }
        const std::size_t zeros = 8 - left_count - right_count;
        for (std::size_t i = 0; i < zeros; ++i) {
            groups[total++] = 0;
        }
        for (std::size_t i = 0; i < right_count; ++i) {
            groups[total++] = right_groups[i];
        }
    }

    for (std::size_t i = 0; i < groups.size(); ++i) {
        out[i * 2] = static_cast<std::uint8_t>(groups[i] >> 8);
        out[i * 2 + 1] = static_cast<std::uint8_t>(groups[i] & 0xFFU);
    }
    return true;
}

}  // namespace

std::optional<IpAddress> parse_ip(std::string_view text) noexcept {
    IpAddress address;
    if (text.empty()) {
        return std::nullopt;
    }

    if (text.find(':') == std::string_view::npos) {
        std::uint8_t v4[4];
        if (!parse_ipv4(text, v4)) {
            return std::nullopt;
        }
        for (std::size_t i = 0; i < 4; ++i) {
            address.bytes[i] = v4[i];
        }
        address.is_v4 = true;
        address.ok = true;
        return address;
    }

    std::array<std::uint8_t, 16> bytes{};
    if (!parse_ipv6(text, bytes)) {
        return std::nullopt;
    }

    // Normalize ::ffff:a.b.c.d to IPv4.
    bool mapped = true;
    for (std::size_t i = 0; i < 10; ++i) {
        if (bytes[i] != 0) {
            mapped = false;
            break;
        }
    }
    if (mapped && bytes[10] == 0xFF && bytes[11] == 0xFF) {
        std::uint8_t v4[4]{bytes[12], bytes[13], bytes[14], bytes[15]};
        address.bytes = {};
        for (std::size_t i = 0; i < 4; ++i) {
            address.bytes[i] = v4[i];
        }
        address.is_v4 = true;
        address.ok = true;
        return address;
    }

    address.bytes = bytes;
    address.is_v4 = false;
    address.ok = true;
    return address;
}

std::string format_ip(const IpAddress& address) {
    char buffer[64];
    if (address.is_v4) {
        std::snprintf(buffer, sizeof(buffer), "%u.%u.%u.%u", static_cast<unsigned>(address.bytes[0]),
                      static_cast<unsigned>(address.bytes[1]), static_cast<unsigned>(address.bytes[2]),
                      static_cast<unsigned>(address.bytes[3]));
        return std::string(buffer);
    }

    std::array<unsigned, 8> groups{};
    for (std::size_t i = 0; i < 8; ++i) {
        groups[i] = (static_cast<unsigned>(address.bytes[i * 2]) << 8) | address.bytes[i * 2 + 1];
    }

    int best_start = -1;
    int best_len = 0;
    for (int i = 0; i < 8;) {
        if (groups[static_cast<std::size_t>(i)] != 0) {
            ++i;
            continue;
        }
        int j = i;
        while (j < 8 && groups[static_cast<std::size_t>(j)] == 0) {
            ++j;
        }
        if (j - i > best_len) {
            best_len = j - i;
            best_start = i;
        }
        i = j;
    }
    if (best_len < 2) {
        best_start = -1;
        best_len = 0;
    }

    std::string result;
    for (int i = 0; i < 8;) {
        if (best_start >= 0 && i == best_start) {
            result += "::";
            i += best_len;
            continue;
        }
        if (!result.empty() && result.back() != ':') {
            result += ':';
        }
        char group[8];
        std::snprintf(group, sizeof(group), "%x", groups[static_cast<std::size_t>(i)]);
        result += group;
        ++i;
    }
    if (result.empty()) {
        result = "::";
    }
    return result;
}

}  // namespace hpla
