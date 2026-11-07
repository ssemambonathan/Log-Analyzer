#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "engine/engine.hpp"
#include "test_helpers.hpp"

namespace {

hpla::AnalysisResult run(const std::vector<std::string>& paths, bool sequential,
                         std::uint64_t chunk_size, unsigned threads, bool use_mmap,
                         const hpla::FilterSet& filter = hpla::FilterSet{}) {
    const std::vector<hpla::FileInfo> files = hpla::stat_input_files(paths);
    hpla::EngineOptions options;
    options.sequential = sequential;
    options.chunk_size = chunk_size;
    options.threads = threads;
    options.buffer_size = 64;
    options.use_mmap = use_mmap;
    return hpla::LogAnalyzer::run(files, filter, options);
}

void expect_equal_statistics(const hpla::Statistics& left, const hpla::Statistics& right) {
    EXPECT_EQ(left.files_processed, right.files_processed);
    EXPECT_EQ(left.bytes_processed, right.bytes_processed);
    EXPECT_EQ(left.lines_examined, right.lines_examined);
    EXPECT_EQ(left.parsed_records, right.parsed_records);
    EXPECT_EQ(left.malformed_records, right.malformed_records);
    EXPECT_EQ(left.matched_records, right.matched_records);
    EXPECT_EQ(left.response_bytes, right.response_bytes);
    EXPECT_EQ(left.sized_records, right.sized_records);
    EXPECT_EQ(left.min_response_size, right.min_response_size);
    EXPECT_EQ(left.max_response_size, right.max_response_size);
    EXPECT_EQ(left.status_counts, right.status_counts);
    EXPECT_EQ(left.method_counts, right.method_counts);
    EXPECT_EQ(left.status_class_counts, right.status_class_counts);
    EXPECT_EQ(left.unique_ips, right.unique_ips);
    EXPECT_EQ(left.unique_ips_approximate, right.unique_ips_approximate);
    EXPECT_EQ(left.distinct_limit_reached, right.distinct_limit_reached);
    EXPECT_EQ(left.first_timestamp, right.first_timestamp);
    EXPECT_EQ(left.last_timestamp, right.last_timestamp);
    EXPECT_DOUBLE_EQ(left.requests_per_minute, right.requests_per_minute);
    EXPECT_EQ(left.counter_overflow, right.counter_overflow);
    ASSERT_EQ(left.top_ips.size(), right.top_ips.size());
    for (std::size_t i = 0; i < left.top_ips.size(); ++i) {
        EXPECT_EQ(left.top_ips[i].key, right.top_ips[i].key);
        EXPECT_EQ(left.top_ips[i].count, right.top_ips[i].count);
    }
    ASSERT_EQ(left.top_paths.size(), right.top_paths.size());
    for (std::size_t i = 0; i < left.top_paths.size(); ++i) {
        EXPECT_EQ(left.top_paths[i].key, right.top_paths[i].key);
        EXPECT_EQ(left.top_paths[i].count, right.top_paths[i].count);
    }
    ASSERT_EQ(left.busiest_minutes.size(), right.busiest_minutes.size());
    for (std::size_t i = 0; i < left.busiest_minutes.size(); ++i) {
        EXPECT_EQ(left.busiest_minutes[i].key, right.busiest_minutes[i].key);
        EXPECT_EQ(left.busiest_minutes[i].count, right.busiest_minutes[i].count);
    }
}

}  // namespace

TEST(Engine, ParallelMatchesSequential) {
    const hpla_test::TempFile file(hpla_test::make_log(3000), "parallel");
    const hpla::AnalysisResult sequential =
        run({file.path_string()}, true, 4096, 4, false);
    const hpla::AnalysisResult parallel = run({file.path_string()}, false, 4096, 4, false);

    SCOPED_TRACE("parallel vs sequential");
    expect_equal_statistics(sequential.statistics, parallel.statistics);
    EXPECT_EQ(parallel.statistics.lines_examined, 3000U);
    EXPECT_GT(parallel.statistics.parsed_records, 0U);
    EXPECT_EQ(parallel.statistics.malformed_records, (3000U + 16U) / 17U);
}

TEST(Engine, HandlesNoTrailingNewlineAndCrlf) {
    const hpla_test::TempFile file(hpla_test::make_log(777, false, true), "no-newline");
    const hpla::AnalysisResult sequential = run({file.path_string()}, true, 1024, 3, false);
    const hpla::AnalysisResult parallel = run({file.path_string()}, false, 1024, 3, false);
    expect_equal_statistics(sequential.statistics, parallel.statistics);
    EXPECT_EQ(sequential.statistics.lines_examined, 777U);
}

TEST(Engine, HandlesEmptyFile) {
    const hpla_test::TempFile file(std::string(""), "empty");
    const hpla::AnalysisResult result = run({file.path_string()}, false, 128, 8, false);
    EXPECT_EQ(result.statistics.files_processed, 1U);
    EXPECT_EQ(result.statistics.bytes_processed, 0U);
    EXPECT_EQ(result.statistics.lines_examined, 0U);
    EXPECT_EQ(result.statistics.matched_records, 0U);
}

TEST(Engine, HandlesSingleVeryLongLineAcrossChunks) {
    const std::string content(20000, 'x');
    const hpla_test::TempFile file(content, "long-line");
    const hpla::AnalysisResult sequential = run({file.path_string()}, true, 512, 2, false);
    const hpla::AnalysisResult parallel = run({file.path_string()}, false, 512, 8, false);
    expect_equal_statistics(sequential.statistics, parallel.statistics);
    EXPECT_EQ(parallel.statistics.lines_examined, 1U);
    EXPECT_EQ(parallel.statistics.parsed_records, 0U);
    EXPECT_EQ(parallel.statistics.malformed_records, 1U);
}

TEST(Engine, ProcessesMultipleFiles) {
    const hpla_test::TempFile first(hpla_test::make_log(500), "first");
    const hpla_test::TempFile second(hpla_test::make_log(250), "second");
    const hpla::AnalysisResult sequential =
        run({first.path_string(), second.path_string()}, true, 2048, 4, false);
    const hpla::AnalysisResult parallel =
        run({first.path_string(), second.path_string()}, false, 2048, 4, false);
    expect_equal_statistics(sequential.statistics, parallel.statistics);
    EXPECT_EQ(parallel.statistics.files_processed, 2U);
    EXPECT_EQ(parallel.statistics.lines_examined, 750U);
    EXPECT_EQ(parallel.statistics.bytes_processed, first.size() + second.size());
}

TEST(Engine, MmapMatchesBuffered) {
    const hpla_test::TempFile file(hpla_test::make_log(1500), "mmap");
    const hpla::AnalysisResult buffered = run({file.path_string()}, false, 4096, 4, false);
    const hpla::AnalysisResult mapped = run({file.path_string()}, false, 4096, 4, true);
    expect_equal_statistics(buffered.statistics, mapped.statistics);
}

TEST(Engine, FilteredStatisticsMatch) {
    const hpla_test::TempFile file(hpla_test::make_log(2000), "filtered");
    hpla::FilterSet filter;
    filter.set_statuses({200, 404});
    filter.add_path_contains("api");
    const hpla::AnalysisResult sequential =
        run({file.path_string()}, true, 2048, 3, false, filter);
    const hpla::AnalysisResult parallel =
        run({file.path_string()}, false, 2048, 3, false, filter);
    expect_equal_statistics(sequential.statistics, parallel.statistics);
    EXPECT_GT(parallel.statistics.matched_records, 0U);
    EXPECT_LT(parallel.statistics.matched_records, parallel.statistics.parsed_records);
}

TEST(Engine, InvalidFileThrows) {
    EXPECT_THROW(hpla::stat_input_files({"definitely-not-a-real-file-12345.log"}),
                 std::runtime_error);
}
