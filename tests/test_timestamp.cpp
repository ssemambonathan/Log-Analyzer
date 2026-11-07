#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "core/timestamp.hpp"

namespace {

std::int64_t expected_epoch(int year, unsigned month, unsigned day, int hour, int minute,
                            int second) {
    return hpla::days_from_civil(year, month, day) * 86400 + hour * 3600 + minute * 60 + second;
}

}  // namespace

TEST(Timestamp, ParsesUtc) {
    std::int64_t epoch = 0;
    ASSERT_TRUE(hpla::parse_http_timestamp("30/Oct/2026:12:45:10 +0000", epoch));
    EXPECT_EQ(epoch, expected_epoch(2026, 10, 30, 12, 45, 10));
}

TEST(Timestamp, AppliesPositiveOffset) {
    std::int64_t epoch = 0;
    ASSERT_TRUE(hpla::parse_http_timestamp("30/Oct/2026:12:45:10 +0200", epoch));
    EXPECT_EQ(epoch, expected_epoch(2026, 10, 30, 12, 45, 10) - 7200);
}

TEST(Timestamp, AppliesNegativeOffset) {
    std::int64_t epoch = 0;
    ASSERT_TRUE(hpla::parse_http_timestamp("01/Jan/2024:00:30:00 -0530", epoch));
    EXPECT_EQ(epoch, expected_epoch(2024, 1, 1, 0, 30, 0) + 5 * 3600 + 30 * 60);
}

TEST(Timestamp, LeapYears) {
    std::int64_t epoch = 0;
    EXPECT_TRUE(hpla::parse_http_timestamp("29/Feb/2024:00:00:00 +0000", epoch));
    EXPECT_FALSE(hpla::parse_http_timestamp("29/Feb/2026:00:00:00 +0000", epoch));
}

TEST(Timestamp, RejectsInvalidComponents) {
    std::int64_t epoch = 0;
    EXPECT_FALSE(hpla::parse_http_timestamp("31/Apr/2026:00:00:00 +0000", epoch));
    EXPECT_FALSE(hpla::parse_http_timestamp("30/Oct/2026:24:00:00 +0000", epoch));
    EXPECT_FALSE(hpla::parse_http_timestamp("30/Oct/2026:12:60:00 +0000", epoch));
    EXPECT_FALSE(hpla::parse_http_timestamp("30/Foo/2026:12:00:00 +0000", epoch));
    EXPECT_FALSE(hpla::parse_http_timestamp("30/Oct/2026:12:00:00", epoch));
    EXPECT_FALSE(hpla::parse_http_timestamp("30/Oct/2026:12:00:00 +0000 extra", epoch));
    EXPECT_FALSE(hpla::parse_http_timestamp("", epoch));
}

TEST(Timestamp, AcceptsLeapSecond) {
    std::int64_t epoch = 0;
    EXPECT_TRUE(hpla::parse_http_timestamp("31/Dec/2026:23:59:60 +0000", epoch));
}

TEST(Timestamp, RoundTripsThroughFormatting) {
    const std::int64_t epoch = expected_epoch(2026, 10, 30, 12, 45, 10);
    EXPECT_EQ(hpla::format_http_timestamp(epoch), "30/Oct/2026:12:45:10 +0000");
    EXPECT_EQ(hpla::format_iso8601_utc(epoch), "2026-10-30T12:45:10Z");
}

TEST(Timestamp, FormatsEpochAndPreEpoch) {
    EXPECT_EQ(hpla::format_iso8601_utc(0), "1970-01-01T00:00:00Z");
    EXPECT_EQ(hpla::format_iso8601_utc(-1), "1969-12-31T23:59:59Z");
}

TEST(Timestamp, CivilDateRoundTrip) {
    for (int year = 1900; year <= 2100; year += 7) {
        for (unsigned month = 1; month <= 12; ++month) {
            const std::int64_t days = hpla::days_from_civil(year, month, 15);
            std::int64_t round_year = 0;
            unsigned round_month = 0;
            unsigned round_day = 0;
            hpla::civil_from_days(days, round_year, round_month, round_day);
            EXPECT_EQ(round_year, year);
            EXPECT_EQ(round_month, month);
            EXPECT_EQ(round_day, 15U);
        }
    }
}
