#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace hpla {

// Address parsed into a fixed 16-byte representation. IPv4 addresses are
// stored in the first four bytes with `is_v4` set. IPv4-mapped IPv6 addresses
// (::ffff:a.b.c.d) are normalized to plain IPv4 so both spellings compare equal.
struct IpAddress {
    std::array<std::uint8_t, 16> bytes{};
    bool is_v4 = false;
    bool ok = false;

    friend bool operator==(const IpAddress&, const IpAddress&) = default;
};

// Parses an IPv4 or IPv6 address. Returns std::nullopt when the input is not a
// valid address (hostnames, zone indices, CIDR suffixes are rejected).
std::optional<IpAddress> parse_ip(std::string_view text) noexcept;

// Canonical textual form for display ("1.2.3.4", "2001:db8::1"). Only meaningful
// for a successfully parsed address.
std::string format_ip(const IpAddress& address);

}  // namespace hpla
