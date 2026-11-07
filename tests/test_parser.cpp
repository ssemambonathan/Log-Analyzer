#include <gtest/gtest.h>

#include <string>

#include "core/timestamp.hpp"
#include "parser/log_parser.hpp"

namespace {

constexpr std::string_view kCombined =
    "127.0.0.1 - - [30/Oct/2026:12:45:10 +0000] \"GET /index.html HTTP/1.1\" 200 1024 \"-\" "
    "\"Mozilla/5.0\"";

}  // namespace

TEST(Parser, ParsesCombinedLine) {
    hpla::LogEntry entry;
    const hpla::ParseResult result = hpla::parse_log_line(kCombined, entry);
    ASSERT_TRUE(result.ok) << hpla::parse_error_text(result.error);
    EXPECT_EQ(entry.client_ip, "127.0.0.1");
    EXPECT_EQ(entry.remote_ident, "-");
    EXPECT_EQ(entry.authenticated_user, "-");
    EXPECT_EQ(entry.method, "GET");
    EXPECT_EQ(entry.target, "/index.html");
    EXPECT_EQ(entry.protocol, "HTTP/1.1");
    EXPECT_EQ(entry.status, 200);
    EXPECT_TRUE(entry.has_response_bytes);
    EXPECT_EQ(entry.response_bytes, 1024U);
    EXPECT_EQ(entry.referrer, "-");
    EXPECT_EQ(entry.user_agent, "Mozilla/5.0");
    EXPECT_TRUE(entry.combined);
    EXPECT_EQ(entry.timestamp_epoch,
              hpla::days_from_civil(2026, 10, 30) * 86400 + 12 * 3600 + 45 * 60 + 10);
}

TEST(Parser, ParsesCommonLogFormat) {
    hpla::LogEntry entry;
    const auto result = hpla::parse_log_line(
        "10.0.0.1 - frank [30/Oct/2026:12:45:10 +0000] \"POST /api/v1/users HTTP/2\" 201 512",
        entry);
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(entry.client_ip, "10.0.0.1");
    EXPECT_EQ(entry.authenticated_user, "frank");
    EXPECT_EQ(entry.method, "POST");
    EXPECT_EQ(entry.target, "/api/v1/users");
    EXPECT_EQ(entry.protocol, "HTTP/2");
    EXPECT_EQ(entry.status, 201);
    EXPECT_EQ(entry.response_bytes, 512U);
    EXPECT_TRUE(entry.referrer.empty());
    EXPECT_TRUE(entry.user_agent.empty());
    EXPECT_FALSE(entry.combined);
}

TEST(Parser, AcceptsMissingByteCount) {
    hpla::LogEntry entry;
    const auto result = hpla::parse_log_line(
        "10.0.0.1 - - [30/Oct/2026:12:45:10 +0000] \"GET / HTTP/1.1\" 304 - \"-\" \"agent\"",
        entry);
    ASSERT_TRUE(result.ok);
    EXPECT_FALSE(entry.has_response_bytes);
    EXPECT_EQ(entry.response_bytes, 0U);
}

TEST(Parser, AcceptsMissingRequest) {
    hpla::LogEntry entry;
    const auto result = hpla::parse_log_line(
        "10.0.0.1 - - [30/Oct/2026:12:45:10 +0000] \"-\" 400 0", entry);
    ASSERT_TRUE(result.ok);
    EXPECT_TRUE(entry.method.empty());
    EXPECT_TRUE(entry.target.empty());
    EXPECT_TRUE(entry.protocol.empty());
    EXPECT_EQ(entry.status, 400);
}

TEST(Parser, AcceptsIpv6Client) {
    hpla::LogEntry entry;
    const auto result = hpla::parse_log_line(
        "2001:db8::1 - - [30/Oct/2026:12:45:10 +0000] \"GET / HTTP/1.1\" 200 5", entry);
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(entry.client_ip, "2001:db8::1");
}

TEST(Parser, HandlesEscapedQuotesInUserAgent) {
    hpla::LogEntry entry;
    const auto result = hpla::parse_log_line(
        R"(1.2.3.4 - - [30/Oct/2026:12:45:10 +0000] "GET / HTTP/1.1" 200 10 "-" "Agent \"quoted\" end")",
        entry);
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(entry.user_agent, R"(Agent \"quoted\" end)");
    EXPECT_TRUE(entry.combined);
}

TEST(Parser, ToleratesExtraWhitespaceAndTrailingSpaces) {
    hpla::LogEntry entry;
    const auto result = hpla::parse_log_line(
        "  1.2.3.4   -   -   [30/Oct/2026:12:45:10 +0000]   \"GET  /a.txt   HTTP/1.1\"   200   12  "
        " \"-\" \"ua\"   ",
        entry);
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(entry.client_ip, "1.2.3.4");
    EXPECT_EQ(entry.method, "GET");
    EXPECT_EQ(entry.target, "/a.txt");
    EXPECT_EQ(entry.protocol, "HTTP/1.1");
    EXPECT_EQ(entry.status, 200);
    EXPECT_EQ(entry.response_bytes, 12U);
}

TEST(Parser, RejectsEmptyLine) {
    hpla::LogEntry entry;
    const auto result = hpla::parse_log_line("", entry);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error, hpla::ParseError::EmptyLine);

    const auto spaces = hpla::parse_log_line("    ", entry);
    EXPECT_FALSE(spaces.ok);
    EXPECT_EQ(spaces.error, hpla::ParseError::EmptyLine);
}

TEST(Parser, RejectsMissingFields) {
    hpla::LogEntry entry;
    EXPECT_EQ(hpla::parse_log_line("1.2.3.4", entry).error, hpla::ParseError::MissingIdent);
    EXPECT_EQ(hpla::parse_log_line("1.2.3.4 -", entry).error, hpla::ParseError::MissingUser);
    EXPECT_EQ(hpla::parse_log_line("1.2.3.4 - -", entry).error,
              hpla::ParseError::MissingTimestamp);
    EXPECT_EQ(hpla::parse_log_line("1.2.3.4 - - [30/Oct/2026:12:45:10 +0000]", entry).error,
              hpla::ParseError::MissingRequest);
}

TEST(Parser, RejectsBadTimestamp) {
    hpla::LogEntry entry;
    const auto result = hpla::parse_log_line(
        "1.2.3.4 - - [30/Foo/2026:12:45:10 +0000] \"GET / HTTP/1.1\" 200 10", entry);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error, hpla::ParseError::BadTimestamp);

    const auto unterminated = hpla::parse_log_line(
        "1.2.3.4 - - [30/Oct/2026:12:45:10 +0000 \"GET / HTTP/1.1\" 200 10", entry);
    EXPECT_FALSE(unterminated.ok);
    EXPECT_EQ(unterminated.error, hpla::ParseError::BadTimestamp);
}

TEST(Parser, RejectsBadStatus) {
    hpla::LogEntry entry;
    for (const char* status : {"abc", "20", "2000", "099", "600"}) {
        const std::string line = std::string("1.2.3.4 - - [30/Oct/2026:12:45:10 +0000] \"GET / "
                                             "HTTP/1.1\" ") +
                                 status + " 10";
        const auto result = hpla::parse_log_line(line, entry);
        EXPECT_FALSE(result.ok) << status;
        EXPECT_EQ(result.error, hpla::ParseError::BadStatus) << status;
    }
}

TEST(Parser, RejectsBadByteCount) {
    hpla::LogEntry entry;
    const auto negative = hpla::parse_log_line(
        "1.2.3.4 - - [30/Oct/2026:12:45:10 +0000] \"GET / HTTP/1.1\" 200 -5", entry);
    EXPECT_FALSE(negative.ok);
    EXPECT_EQ(negative.error, hpla::ParseError::BadBytes);

    const auto overflow = hpla::parse_log_line(
        "1.2.3.4 - - [30/Oct/2026:12:45:10 +0000] \"GET / HTTP/1.1\" 200 "
        "99999999999999999999999999",
        entry);
    EXPECT_FALSE(overflow.ok);
    EXPECT_EQ(overflow.error, hpla::ParseError::BadBytes);
}

TEST(Parser, RejectsBadQuoting) {
    hpla::LogEntry entry;
    const auto missing_open = hpla::parse_log_line(
        "1.2.3.4 - - [30/Oct/2026:12:45:10 +0000] GET / HTTP/1.1 200 10", entry);
    EXPECT_FALSE(missing_open.ok);
    EXPECT_EQ(missing_open.error, hpla::ParseError::MissingRequest);

    const auto unterminated = hpla::parse_log_line(
        "1.2.3.4 - - [30/Oct/2026:12:45:10 +0000] \"GET / HTTP/1.1 200 10", entry);
    EXPECT_FALSE(unterminated.ok);
    EXPECT_EQ(unterminated.error, hpla::ParseError::MissingRequest);

    const auto missing_agent = hpla::parse_log_line(
        "1.2.3.4 - - [30/Oct/2026:12:45:10 +0000] \"GET / HTTP/1.1\" 200 10 \"-\"", entry);
    EXPECT_FALSE(missing_agent.ok);
    EXPECT_EQ(missing_agent.error, hpla::ParseError::BadQuoting);
}

TEST(Parser, RejectsTrailingGarbage) {
    hpla::LogEntry entry;
    const auto result = hpla::parse_log_line(
        "1.2.3.4 - - [30/Oct/2026:12:45:10 +0000] \"GET / HTTP/1.1\" 200 10 \"-\" \"ua\" extra",
        entry);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error, hpla::ParseError::TrailingGarbage);
}

TEST(Parser, RejectsMalformedRequest) {
    hpla::LogEntry entry;
    const auto one_word = hpla::parse_log_line(
        "1.2.3.4 - - [30/Oct/2026:12:45:10 +0000] \"GET\" 200 10", entry);
    EXPECT_FALSE(one_word.ok);
    EXPECT_EQ(one_word.error, hpla::ParseError::BadRequest);

    const auto two_words = hpla::parse_log_line(
        "1.2.3.4 - - [30/Oct/2026:12:45:10 +0000] \"GET /\" 200 10", entry);
    EXPECT_FALSE(two_words.ok);
    EXPECT_EQ(two_words.error, hpla::ParseError::BadRequest);
}
