#pragma once

#include <cstdint>
#include <string_view>

namespace hpla {

// Zero-copy view of one access-log record.
//
// LIFETIME CONTRACT: every std::string_view here points into the line buffer
// passed to parse_log_line(). The views are valid only until that buffer is
// re-read, reused, or destroyed. Do not store a LogEntry past the lifetime of
// the line it was parsed from. Copy to owned strings if a record must outlive
// the input buffer.
struct LogEntry {
    std::string_view client_ip;
    std::string_view remote_ident;   // "-" is preserved as a single dash
    std::string_view authenticated_user;
    std::int64_t timestamp_epoch = 0;  // UTC seconds; always set for valid records
    std::string_view method;
    std::string_view target;
    std::string_view protocol;
    std::uint16_t status = 0;
    std::uint64_t response_bytes = 0;
    bool has_response_bytes = false;  // false when the log contains "-"
    std::string_view referrer;        // empty for Common Log Format
    std::string_view user_agent;      // empty for Common Log Format
    bool combined = false;
};

enum class ParseError : std::uint8_t {
    None = 0,
    EmptyLine,
    MissingClientIp,
    MissingIdent,
    MissingUser,
    MissingTimestamp,
    BadTimestamp,
    MissingRequest,
    BadRequest,
    MissingStatus,
    BadStatus,
    MissingBytes,
    BadBytes,
    BadQuoting,
    TrailingGarbage,
};

std::string_view parse_error_text(ParseError error) noexcept;

struct ParseResult {
    bool ok = false;
    ParseError error = ParseError::None;
    std::size_t error_offset = 0;  // byte index in the input line, 0 when unknown

    static ParseResult success() noexcept { return {true, ParseError::None, 0}; }
    static ParseResult failure(ParseError error, std::size_t offset) noexcept {
        return {false, error, offset};
    }
};

// Parses Common Log Format and Combined Log Format lines.
//
// Accepted grammar:
//   host ident user [dd/Mon/yyyy:HH:MM:SS +HHMM] "METHOD target PROTOCOL" status bytes
//   host ident user [dd/Mon/yyyy:HH:MM:SS +HHMM] "METHOD target PROTOCOL" status bytes "referrer" "user-agent"
//
// `bytes` may be "-" (recorded as has_response_bytes == false). Referrer and
// user-agent are optional; when either is present the line is flagged as
// combined. Backslash-escaped quotes inside quoted fields are honored.
ParseResult parse_log_line(std::string_view line, LogEntry& entry) noexcept;

}  // namespace hpla
