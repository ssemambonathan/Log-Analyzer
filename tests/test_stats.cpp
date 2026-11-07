#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "filter/filter.hpp"
#include "stats/accumulator.hpp"

namespace {

const char* kLineOne =
    "10.0.0.1 - - [30/Oct/2026:12:00:00 +0000] \"GET /a HTTP/1.1\" 200 100";
const char* kLineTwo =
    "10.0.0.1 - - [30/Oct/2026:12:00:30 +0000] \"GET /a HTTP/1.1\" 200 200";
const char* kLineThree =
    "10.0.0.2 - - [30/Oct/2026:12:01:00 +0000] \"POST /b HTTP/1.1\" 404 -";
const char* kBadLine = "this is not a log line";

}  // namespace

TEST(Stats, ComputesCoreMetrics) {
    hpla::Accumulator accumulator(nullptr, 0);
    accumulator.add_line(kLineOne);
    accumulator.add_line(kLineTwo);
    accumulator.add_line(kLineThree);
    accumulator.add_line(kBadLine);

    const hpla::Statistics stats =
        hpla::build_statistics(std::vector<hpla::Accumulator>{accumulator}, 10, 2, 123456);

    EXPECT_EQ(stats.files_processed, 2U);
    EXPECT_EQ(stats.bytes_processed, 123456U);
    EXPECT_EQ(stats.lines_examined, 4U);
    EXPECT_EQ(stats.parsed_records, 3U);
    EXPECT_EQ(stats.malformed_records, 1U);
    EXPECT_EQ(stats.matched_records, 3U);

    EXPECT_EQ(stats.status_counts[200 - 100], 2U);
    EXPECT_EQ(stats.status_counts[404 - 100], 1U);
    EXPECT_EQ(stats.status_class_counts[1], 2U);  // 2xx
    EXPECT_EQ(stats.status_class_counts[3], 1U);  // 4xx

    EXPECT_EQ(stats.method_counts[static_cast<std::size_t>(hpla::HttpMethod::Get)], 2U);
    EXPECT_EQ(stats.method_counts[static_cast<std::size_t>(hpla::HttpMethod::Post)], 1U);

    EXPECT_EQ(stats.sized_records, 2U);
    EXPECT_EQ(stats.response_bytes, 300U);
    EXPECT_TRUE(stats.has_response_size);
    EXPECT_EQ(stats.min_response_size, 100U);
    EXPECT_EQ(stats.max_response_size, 200U);

    EXPECT_EQ(stats.unique_ips, 2U);
    EXPECT_FALSE(stats.unique_ips_approximate);
    ASSERT_EQ(stats.top_ips.size(), 2U);
    EXPECT_EQ(stats.top_ips[0].key, "10.0.0.1");
    EXPECT_EQ(stats.top_ips[0].count, 2U);
    EXPECT_EQ(stats.top_ips[1].key, "10.0.0.2");

    ASSERT_EQ(stats.top_paths.size(), 2U);
    EXPECT_EQ(stats.top_paths[0].key, "/a");
    EXPECT_EQ(stats.top_paths[0].count, 2U);

    EXPECT_TRUE(stats.has_timestamps);
    ASSERT_EQ(stats.busiest_minutes.size(), 2U);
    EXPECT_EQ(stats.busiest_minutes[0].key, "2026-10-30T12:00:00Z");
    EXPECT_EQ(stats.busiest_minutes[0].count, 2U);
    EXPECT_DOUBLE_EQ(stats.requests_per_minute, 3.0);
    EXPECT_FALSE(stats.counter_overflow);
    EXPECT_FALSE(stats.distinct_limit_reached);
}

TEST(Stats, AppliesFiltersBeforeAggregation) {
    hpla::FilterSet filter;
    filter.set_statuses({200});

    hpla::Accumulator accumulator(&filter, 0);
    accumulator.add_line(kLineOne);
    accumulator.add_line(kLineTwo);
    accumulator.add_line(kLineThree);
    accumulator.add_line(kBadLine);

    const hpla::Statistics stats =
        hpla::build_statistics(std::vector<hpla::Accumulator>{accumulator}, 10, 1, 0);
    EXPECT_EQ(stats.lines_examined, 4U);
    EXPECT_EQ(stats.parsed_records, 3U);
    EXPECT_EQ(stats.malformed_records, 1U);
    EXPECT_EQ(stats.matched_records, 2U);
    EXPECT_EQ(stats.status_counts[404 - 100], 0U);
    EXPECT_EQ(stats.top_ips.size(), 1U);
    EXPECT_EQ(stats.response_bytes, 300U);
}

TEST(Stats, MergeIsEquivalentToSinglePass) {
    hpla::Accumulator single(nullptr, 0);
    hpla::Accumulator first(nullptr, 0);
    hpla::Accumulator second(nullptr, 0);

    for (const char* line : {kLineOne, kLineTwo, kLineThree, kBadLine}) {
        single.add_line(line);
    }
    first.add_line(kLineOne);
    first.add_line(kLineTwo);
    second.add_line(kLineThree);
    second.add_line(kBadLine);

    const hpla::Statistics merged =
        hpla::build_statistics(std::vector<hpla::Accumulator>{first, second}, 10, 1, 100);
    const hpla::Statistics direct =
        hpla::build_statistics(std::vector<hpla::Accumulator>{single}, 10, 1, 100);

    EXPECT_EQ(merged.lines_examined, direct.lines_examined);
    EXPECT_EQ(merged.parsed_records, direct.parsed_records);
    EXPECT_EQ(merged.malformed_records, direct.malformed_records);
    EXPECT_EQ(merged.matched_records, direct.matched_records);
    EXPECT_EQ(merged.response_bytes, direct.response_bytes);
    EXPECT_EQ(merged.unique_ips, direct.unique_ips);
    ASSERT_EQ(merged.top_ips.size(), direct.top_ips.size());
    for (std::size_t i = 0; i < merged.top_ips.size(); ++i) {
        EXPECT_EQ(merged.top_ips[i].key, direct.top_ips[i].key);
        EXPECT_EQ(merged.top_ips[i].count, direct.top_ips[i].count);
    }
    EXPECT_EQ(merged.min_response_size, direct.min_response_size);
    EXPECT_EQ(merged.max_response_size, direct.max_response_size);
    EXPECT_EQ(merged.first_timestamp, direct.first_timestamp);
    EXPECT_EQ(merged.last_timestamp, direct.last_timestamp);
}

TEST(Stats, DistinctLimitFallsBackToHll) {
    hpla::Accumulator accumulator(nullptr, 3);
    for (int i = 0; i < 40; ++i) {
        const std::string line = "10.0.0." + std::to_string(i) +
                                 " - - [30/Oct/2026:12:00:00 +0000] \"GET /p" +
                                 std::to_string(i) + " HTTP/1.1\" 200 10";
        accumulator.add_line(line);
    }

    const hpla::Statistics stats =
        hpla::build_statistics(std::vector<hpla::Accumulator>{accumulator}, 5, 1, 0);
    EXPECT_TRUE(stats.distinct_limit_reached);
    EXPECT_TRUE(stats.unique_ips_approximate);
    EXPECT_GE(stats.unique_ips, 35U);
    EXPECT_LE(stats.unique_ips, 45U);
    EXPECT_LE(stats.top_ips.size(), 5U);
    EXPECT_LE(stats.top_paths.size(), 5U);
}

TEST(Stats, TopEntriesSortByCountThenKey) {
    hpla::Accumulator accumulator(nullptr, 0);
    accumulator.add_line("10.0.0.2 - - [30/Oct/2026:12:00:00 +0000] \"GET /z HTTP/1.1\" 200 1");
    accumulator.add_line("10.0.0.1 - - [30/Oct/2026:12:00:00 +0000] \"GET /y HTTP/1.1\" 200 1");
    accumulator.add_line("10.0.0.3 - - [30/Oct/2026:12:00:00 +0000] \"GET /x HTTP/1.1\" 200 1");
    accumulator.add_line("10.0.0.3 - - [30/Oct/2026:12:00:00 +0000] \"GET /x HTTP/1.1\" 200 1");

    const hpla::Statistics stats =
        hpla::build_statistics(std::vector<hpla::Accumulator>{accumulator}, 2, 1, 0);
    ASSERT_EQ(stats.top_ips.size(), 2U);
    EXPECT_EQ(stats.top_ips[0].key, "10.0.0.3");
    EXPECT_EQ(stats.top_ips[0].count, 2U);
    EXPECT_EQ(stats.top_ips[1].key, "10.0.0.1");
}

TEST(Stats, CounterSaturationIsReported) {
    hpla::Accumulator first(nullptr, 0);
    hpla::Accumulator second(nullptr, 0);
    first.response_bytes = std::numeric_limits<std::uint64_t>::max() - 5;
    first.sized_records = 1;
    first.min_response_size = 10;
    second.response_bytes = 10;
    second.sized_records = 1;
    second.min_response_size = 20;

    const hpla::Statistics stats =
        hpla::build_statistics(std::vector<hpla::Accumulator>{first, second}, 10, 1, 0);
    EXPECT_TRUE(stats.counter_overflow);
    EXPECT_EQ(stats.response_bytes, std::numeric_limits<std::uint64_t>::max());
    EXPECT_EQ(stats.sized_records, 2U);
    EXPECT_EQ(stats.min_response_size, 10U);
}

TEST(Stats, EmptyAccumulatorProducesZeroedReport) {
    hpla::Accumulator accumulator(nullptr, 0);
    const hpla::Statistics stats =
        hpla::build_statistics(std::vector<hpla::Accumulator>{accumulator}, 10, 3, 0);
    EXPECT_EQ(stats.lines_examined, 0U);
    EXPECT_EQ(stats.matched_records, 0U);
    EXPECT_FALSE(stats.has_response_size);
    EXPECT_EQ(stats.min_response_size, 0U);
    EXPECT_EQ(stats.max_response_size, 0U);
    EXPECT_FALSE(stats.has_timestamps);
    EXPECT_TRUE(stats.top_ips.empty());
    EXPECT_DOUBLE_EQ(stats.requests_per_minute, 0.0);
}

TEST(Stats, ClassifiesMethods) {
    EXPECT_EQ(hpla::classify_method("GET"), hpla::HttpMethod::Get);
    EXPECT_EQ(hpla::classify_method("OPTIONS"), hpla::HttpMethod::Options);
    EXPECT_EQ(hpla::classify_method("TRACE"), hpla::HttpMethod::Other);
    EXPECT_EQ(hpla::method_name(hpla::HttpMethod::Delete), "DELETE");
}
