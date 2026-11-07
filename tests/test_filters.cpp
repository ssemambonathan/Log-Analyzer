#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "core/timestamp.hpp"
#include "filter/filter.hpp"
#include "parser/log_parser.hpp"

namespace {

hpla::LogEntry parse(std::string_view line) {
    hpla::LogEntry entry;
    const auto result = hpla::parse_log_line(line, entry);
    EXPECT_TRUE(result.ok) << hpla::parse_error_text(result.error);
    return entry;
}

std::int64_t epoch_of(const char* timestamp) {
    std::int64_t epoch = 0;
    EXPECT_TRUE(hpla::parse_http_timestamp(timestamp, epoch));
    return epoch;
}

const hpla::LogEntry& get_entry() {
    static const std::string line =
        "10.0.0.1 - bob [30/Oct/2026:12:00:00 +0000] \"POST /api/v1/users?id=7 HTTP/1.1\" 200 512 "
        "\"https://example.com/\" \"Mozilla/5.0 TestAgent\"";
    static const hpla::LogEntry entry = parse(line);
    return entry;
}

}  // namespace

TEST(Filters, InactiveByDefault) {
    const hpla::FilterSet filter;
    EXPECT_FALSE(filter.active());
    EXPECT_TRUE(filter.matches(get_entry()));
}

TEST(Filters, StatusSetAndRange) {
    hpla::FilterSet filter;
    filter.set_statuses({200, 404});
    EXPECT_TRUE(filter.active());
    EXPECT_TRUE(filter.matches(get_entry()));

    filter.set_status_range(400, 499);
    EXPECT_TRUE(filter.matches(get_entry()));  // 200 is still in the explicit set

    hpla::FilterSet range_only;
    range_only.set_status_range(200, 299);
    EXPECT_TRUE(range_only.matches(get_entry()));
    EXPECT_EQ(range_only.status_count(), 100U);
}

TEST(Filters, IPMatchesCanonicalAddress) {
    hpla::FilterSet filter;
    const auto address = hpla::parse_ip("10.0.0.1");
    ASSERT_TRUE(address.has_value());
    filter.add_ip(*address);
    EXPECT_TRUE(filter.matches(get_entry()));

    hpla::FilterSet other;
    const auto other_address = hpla::parse_ip("10.0.0.2");
    ASSERT_TRUE(other_address.has_value());
    other.add_ip(*other_address);
    EXPECT_FALSE(other.matches(get_entry()));

    hpla::LogEntry v6 = [] {
        static const std::string line =
            "2001:db8::1 - - [30/Oct/2026:12:00:00 +0000] \"GET / HTTP/1.1\" 200 1";
        return parse(line);
    }();
    hpla::FilterSet v6_filter;
    const auto v6_address = hpla::parse_ip("2001:db8::1");
    ASSERT_TRUE(v6_address.has_value());
    v6_filter.add_ip(*v6_address);
    EXPECT_TRUE(v6_filter.matches(v6));

    hpla::LogEntry invalid_ip = [] {
        static const std::string line =
            "not-an-ip - - [30/Oct/2026:12:00:00 +0000] \"GET / HTTP/1.1\" 200 1";
        return parse(line);
    }();
    EXPECT_FALSE(v6_filter.matches(invalid_ip));
}

TEST(Filters, MethodIsCaseInsensitive) {
    hpla::FilterSet filter;
    filter.add_method("post");
    EXPECT_TRUE(filter.matches(get_entry()));

    hpla::FilterSet other;
    other.add_method("GET");
    EXPECT_FALSE(other.matches(get_entry()));
}

TEST(Filters, ExactPathsAreOred) {
    hpla::FilterSet filter;
    filter.add_exact_path("/other");
    filter.add_exact_path("/api/v1/users?id=7");
    EXPECT_TRUE(filter.matches(get_entry()));

    filter.add_exact_path("/never");
    EXPECT_TRUE(filter.matches(get_entry()));

    hpla::FilterSet mismatch;
    mismatch.add_exact_path("/api/v1/users");
    EXPECT_FALSE(mismatch.matches(get_entry()));
}

TEST(Filters, PathAndUserAgentSubstrings) {
    hpla::FilterSet filter;
    filter.add_path_contains("api");
    EXPECT_TRUE(filter.matches(get_entry()));

    filter.add_path_contains("missing");
    EXPECT_TRUE(filter.matches(get_entry()));

    hpla::FilterSet missing_only;
    missing_only.add_path_contains("missing");
    EXPECT_FALSE(missing_only.matches(get_entry()));

    hpla::FilterSet agent_filter;
    agent_filter.add_user_agent_contains("TestAgent");
    EXPECT_TRUE(agent_filter.matches(get_entry()));

    hpla::LogEntry clf = [] {
        static const std::string line =
            "10.0.0.1 - - [30/Oct/2026:12:00:00 +0000] \"GET / HTTP/1.1\" 200 1";
        return parse(line);
    }();
    EXPECT_FALSE(agent_filter.matches(clf));
}

TEST(Filters, TimeRangeIsInclusive) {
    const auto from = epoch_of("30/Oct/2026:12:00:00 +0000");
    const auto to = epoch_of("30/Oct/2026:12:00:00 +0000");

    hpla::FilterSet filter;
    filter.set_from(from);
    filter.set_to(to);
    EXPECT_TRUE(filter.matches(get_entry()));

    filter.set_from(from + 1);
    EXPECT_FALSE(filter.matches(get_entry()));

    hpla::FilterSet before;
    before.set_to(from - 1);
    EXPECT_FALSE(before.matches(get_entry()));
}

TEST(Filters, SizeRangeRequiresKnownSize) {
    hpla::FilterSet filter;
    filter.set_min_size(512);
    filter.set_max_size(512);
    EXPECT_TRUE(filter.matches(get_entry()));

    filter.set_min_size(513);
    EXPECT_FALSE(filter.matches(get_entry()));

    hpla::FilterSet max_filter;
    max_filter.set_max_size(100);
    EXPECT_FALSE(max_filter.matches(get_entry()));

    hpla::LogEntry no_size = [] {
        static const std::string line =
            "10.0.0.1 - - [30/Oct/2026:12:00:00 +0000] \"GET / HTTP/1.1\" 200 -";
        return parse(line);
    }();
    hpla::FilterSet size_filter;
    size_filter.set_max_size(100);
    EXPECT_FALSE(size_filter.matches(no_size));
}

TEST(Filters, AllFiltersAreAnded) {
    hpla::FilterSet filter;
    filter.set_statuses({200});
    filter.add_method("POST");
    filter.add_path_contains("api");
    filter.set_min_size(100);
    EXPECT_TRUE(filter.matches(get_entry()));

    hpla::FilterSet wrong_status;
    wrong_status.set_statuses({404});
    wrong_status.add_method("POST");
    wrong_status.add_path_contains("api");
    wrong_status.set_min_size(100);
    EXPECT_FALSE(wrong_status.matches(get_entry()));
}

TEST(Filters, DescriptionsAreRecorded) {
    hpla::FilterSet filter;
    filter.add_description("status", "200");
    filter.add_description("method", "GET");
    ASSERT_EQ(filter.descriptions().size(), 2U);
    EXPECT_EQ(filter.descriptions()[0].name, "status");
    EXPECT_EQ(filter.descriptions()[1].value, "GET");
}
