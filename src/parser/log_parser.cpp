#include "parser/log_parser.hpp"

#include <cstddef>

#include "core/format.hpp"
#include "core/timestamp.hpp"

namespace hpla {
namespace {

bool is_space(char c) noexcept {
    return c == ' ' || c == '\t';
}

class Cursor {
public:
    explicit Cursor(std::string_view text) noexcept : text_(text) {}

    bool eof() const noexcept { return pos_ >= text_.size(); }
    std::size_t pos() const noexcept { return pos_; }
    void set_pos(std::size_t pos) noexcept { pos_ = pos; }

    void skip_spaces() noexcept {
        while (pos_ < text_.size() && is_space(text_[pos_])) {
            ++pos_;
        }
    }

    std::string_view token() noexcept {
        const std::size_t start = pos_;
        while (pos_ < text_.size() && !is_space(text_[pos_])) {
            ++pos_;
        }
        return text_.substr(start, pos_ - start);
    }

    bool consume(char expected) noexcept {
        if (pos_ < text_.size() && text_[pos_] == expected) {
            ++pos_;
            return true;
        }
        return false;
    }

    // Reads a double-quoted field. Backslash-escaped characters (including
    // \" ) are skipped when searching for the closing quote. The returned view
    // keeps escape sequences untouched.
    bool quoted(std::string_view& out) noexcept {
        if (!consume('"')) {
            return false;
        }
        const std::size_t start = pos_;
        bool escaped = false;
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == '"' && !escaped) {
                out = text_.substr(start, pos_ - start);
                ++pos_;
                return true;
            }
            if (c == '\\' && !escaped) {
                escaped = true;
            } else {
                escaped = false;
            }
            ++pos_;
        }
        return false;
    }

private:
    std::string_view text_;
    std::size_t pos_ = 0;
};

bool parse_status(std::string_view token, std::uint16_t& status) noexcept {
    if (token.size() != 3) {
        return false;
    }
    unsigned value = 0;
    for (const char c : token) {
        if (c < '0' || c > '9') {
            return false;
        }
        value = value * 10 + static_cast<unsigned>(c - '0');
    }
    if (value < 100 || value > 599) {
        return false;
    }
    status = static_cast<std::uint16_t>(value);
    return true;
}

bool parse_bytes(std::string_view token, std::uint64_t& bytes) noexcept {
    return parse_uint64(token, bytes);
}

bool split_request(std::string_view request, LogEntry& entry) noexcept {
    std::size_t pos = 0;
    const auto skip = [&]() {
        while (pos < request.size() && is_space(request[pos])) {
            ++pos;
        }
    };

    skip();
    const std::size_t method_start = pos;
    while (pos < request.size() && !is_space(request[pos])) {
        ++pos;
    }
    entry.method = request.substr(method_start, pos - method_start);
    if (entry.method.empty()) {
        return false;
    }

    skip();
    const std::size_t target_start = pos;
    while (pos < request.size() && !is_space(request[pos])) {
        ++pos;
    }
    entry.target = request.substr(target_start, pos - target_start);
    if (entry.target.empty()) {
        return false;
    }

    skip();
    const std::size_t protocol_start = pos;
    while (pos < request.size() && !is_space(request[pos])) {
        ++pos;
    }
    entry.protocol = request.substr(protocol_start, pos - protocol_start);
    if (entry.protocol.empty()) {
        return false;
    }

    // Anything after the protocol must be whitespace only.
    skip();
    return pos == request.size();
}

}  // namespace

std::string_view parse_error_text(ParseError error) noexcept {
    switch (error) {
        case ParseError::None: return "ok";
        case ParseError::EmptyLine: return "empty line";
        case ParseError::MissingClientIp: return "missing client IP";
        case ParseError::MissingIdent: return "missing remote identity field";
        case ParseError::MissingUser: return "missing authenticated user field";
        case ParseError::MissingTimestamp: return "missing timestamp";
        case ParseError::BadTimestamp: return "malformed timestamp";
        case ParseError::MissingRequest: return "missing request";
        case ParseError::BadRequest: return "malformed request";
        case ParseError::MissingStatus: return "missing status code";
        case ParseError::BadStatus: return "invalid status code";
        case ParseError::MissingBytes: return "missing response size";
        case ParseError::BadBytes: return "invalid response size";
        case ParseError::BadQuoting: return "unterminated or missing quote";
        case ParseError::TrailingGarbage: return "trailing data after record";
    }
    return "unknown parse error";
}

ParseResult parse_log_line(std::string_view line, LogEntry& entry) noexcept {
    entry = LogEntry{};
    Cursor cursor(line);
    cursor.skip_spaces();

    if (cursor.eof()) {
        return ParseResult::failure(ParseError::EmptyLine, 0);
    }

    entry.client_ip = cursor.token();
    if (entry.client_ip.empty()) {
        return ParseResult::failure(ParseError::MissingClientIp, cursor.pos());
    }

    cursor.skip_spaces();
    entry.remote_ident = cursor.token();
    if (entry.remote_ident.empty()) {
        return ParseResult::failure(ParseError::MissingIdent, cursor.pos());
    }

    cursor.skip_spaces();
    entry.authenticated_user = cursor.token();
    if (entry.authenticated_user.empty()) {
        return ParseResult::failure(ParseError::MissingUser, cursor.pos());
    }

    cursor.skip_spaces();
    if (!cursor.consume('[')) {
        return ParseResult::failure(ParseError::MissingTimestamp, cursor.pos());
    }
    const std::size_t close = line.find(']', cursor.pos());
    if (close == std::string_view::npos) {
        return ParseResult::failure(ParseError::BadTimestamp, cursor.pos());
    }
    const std::string_view timestamp = line.substr(cursor.pos(), close - cursor.pos());
    if (!parse_http_timestamp(timestamp, entry.timestamp_epoch)) {
        return ParseResult::failure(ParseError::BadTimestamp, cursor.pos());
    }
    cursor.set_pos(close + 1);

    cursor.skip_spaces();
    std::string_view request;
    if (!cursor.quoted(request)) {
        return ParseResult::failure(ParseError::MissingRequest, cursor.pos());
    }
    if (request == "-") {
        // Servers emit this for requests they could not parse at all. The
        // surrounding record is still structurally valid.
        entry.method = {};
        entry.target = {};
        entry.protocol = {};
    } else if (!split_request(request, entry)) {
        return ParseResult::failure(ParseError::BadRequest, cursor.pos());
    }

    cursor.skip_spaces();
    const std::string_view status_token = cursor.token();
    if (status_token.empty()) {
        return ParseResult::failure(ParseError::MissingStatus, cursor.pos());
    }
    if (!parse_status(status_token, entry.status)) {
        return ParseResult::failure(ParseError::BadStatus, cursor.pos());
    }

    cursor.skip_spaces();
    const std::string_view bytes_token = cursor.token();
    if (bytes_token.empty()) {
        return ParseResult::failure(ParseError::MissingBytes, cursor.pos());
    }
    if (bytes_token == "-") {
        entry.has_response_bytes = false;
        entry.response_bytes = 0;
    } else {
        if (!parse_bytes(bytes_token, entry.response_bytes)) {
            return ParseResult::failure(ParseError::BadBytes, cursor.pos());
        }
        entry.has_response_bytes = true;
    }

    cursor.skip_spaces();
    if (cursor.eof()) {
        return ParseResult::success();
    }

    std::string_view referrer;
    if (!cursor.quoted(referrer)) {
        return ParseResult::failure(ParseError::BadQuoting, cursor.pos());
    }
    cursor.skip_spaces();
    std::string_view user_agent;
    if (!cursor.quoted(user_agent)) {
        return ParseResult::failure(ParseError::BadQuoting, cursor.pos());
    }

    entry.referrer = referrer;
    entry.user_agent = user_agent;
    entry.combined = true;

    cursor.skip_spaces();
    if (!cursor.eof()) {
        return ParseResult::failure(ParseError::TrailingGarbage, cursor.pos());
    }

    return ParseResult::success();
}

}  // namespace hpla
