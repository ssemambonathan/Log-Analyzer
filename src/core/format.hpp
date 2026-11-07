#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace hpla {

std::string format_uint(std::uint64_t value);
std::string format_human_bytes(std::uint64_t bytes);
std::string format_double(double value, int decimals = 2);
std::string format_rate(double megabytes_per_second);
std::string status_text(std::uint16_t status);
std::string sanitize_for_terminal(std::string_view text);
std::string uppercase_ascii(std::string_view text);
bool parse_uint64(std::string_view text, std::uint64_t& value) noexcept;
// Accepts plain byte counts and binary suffixes (K, KiB, M, MB, G, GiB, T, TiB).
bool parse_size(std::string_view text, std::uint64_t& value) noexcept;

}  // namespace hpla
