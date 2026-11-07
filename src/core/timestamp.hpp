#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace hpla {

// Parses an HTTP access-log timestamp such as "30/Oct/2026:12:45:10 +0000"
// and converts it to Unix epoch seconds (UTC). The numeric timezone offset is
// applied, so records from different zones compare correctly.
//
// Returns false for any malformed field, out-of-range component, or trailing
// data. Leap seconds (:60) are accepted and preserved as-is.
bool parse_http_timestamp(std::string_view text, std::int64_t& epoch_seconds) noexcept;

// "2026-10-30T12:45:10Z" (UTC).
std::string format_iso8601_utc(std::int64_t epoch_seconds);

// "30/Oct/2026:12:45:10 +0000" (UTC).
std::string format_http_timestamp(std::int64_t epoch_seconds);

// Number of days since 1970-01-01 for a proleptic Gregorian date.
constexpr std::int64_t days_from_civil(std::int64_t y, unsigned m, unsigned d) noexcept {
    y -= m <= 2 ? 1 : 0;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153U * (m + (m > 2 ? -3U : 9U)) + 2U) / 5U + d - 1U;
    const unsigned doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

// Inverse of days_from_civil.
constexpr void civil_from_days(std::int64_t z, std::int64_t& y, unsigned& m, unsigned& d) noexcept {
    z += 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460U + doe / 36524U - doe / 146096U) / 365U;
    y = static_cast<std::int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365U * yoe + yoe / 4U - yoe / 100U);
    const unsigned mp = (5U * doy + 2U) / 153U;
    d = doy - (153U * mp + 2U) / 5U + 1U;
    m = mp < 10U ? mp + 3U : mp - 9U;
    y += m <= 2U ? 1 : 0;
}

}  // namespace hpla
