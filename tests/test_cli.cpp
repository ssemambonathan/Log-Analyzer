#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "cli/cli.hpp"
#include "core/ip.hpp"
#include "core/timestamp.hpp"

namespace {

hpla::CliParseResult parse(std::vector<std::string> args) {
    std::vector<const char*> argv;
    argv.reserve(args.size() + 1);
    argv.push_back("hpla");
    for (const std::string& argument : args) {
        argv.push_back(argument.c_str());
    }
    return hpla::parse_cli(static_cast<int>(argv.size()), argv.data());
}

bool contains(const std::string& text, std::string_view needle) {
    return text.find(needle) != std::string::npos;
}

}  // namespace

TEST(Cli, Defaults) {
    const hpla::CliParseResult result = parse({"access.log"});
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_EQ(result.options.files.size(), 1U);
    EXPECT_EQ(result.options.files[0], "access.log");
    EXPECT_EQ(result.options.threads, 0U);
    EXPECT_EQ(result.options.buffer_size, 1U << 20);
    EXPECT_FALSE(result.options.sequential);
    EXPECT_FALSE(result.options.use_mmap);
    EXPECT_EQ(result.options.top_n, 10U);
    EXPECT_EQ(result.options.max_distinct, 2'000'000U);
    EXPECT_EQ(result.options.format, hpla::OutputFormat::Text);
    EXPECT_FALSE(result.options.output_path.has_value());
    EXPECT_FALSE(result.options.include_records);
    EXPECT_EQ(result.options.color, hpla::ColorMode::Auto);
    EXPECT_FALSE(result.options.filter.active());
}

TEST(Cli, MultipleFilesAndDoubleDash) {
    hpla::CliParseResult result = parse({"a.log", "b.log"});
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.options.files.size(), 2U);

    result = parse({"--", "--weird-name.log", "-x.log"});
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_EQ(result.options.files.size(), 2U);
    EXPECT_EQ(result.options.files[0], "--weird-name.log");
    EXPECT_EQ(result.options.files[1], "-x.log");
}

TEST(Cli, StatusListAndRange) {
    const hpla::CliParseResult result =
        parse({"access.log", "--status", "200,301", "--status", "404", "--status-range",
               "500", "503"});
    ASSERT_TRUE(result.ok) << result.error;
    const auto& lookup = result.options.filter.status_lookup();
    EXPECT_TRUE(lookup[200]);
    EXPECT_TRUE(lookup[301]);
    EXPECT_TRUE(lookup[404]);
    EXPECT_TRUE(lookup[500]);
    EXPECT_TRUE(lookup[503]);
    EXPECT_FALSE(lookup[302]);
    EXPECT_FALSE(lookup[504]);
    EXPECT_EQ(result.options.filter.status_count(), 7U);
    ASSERT_EQ(result.options.filter.descriptions().size(), 3U);
    EXPECT_EQ(result.options.filter.descriptions()[0].name, "status");
    EXPECT_EQ(result.options.filter.descriptions()[0].value, "200,301");
    EXPECT_EQ(result.options.filter.descriptions()[2].value, "500-503");
}

TEST(Cli, StatusRangeInlineValues) {
    const hpla::CliParseResult result =
        parse({"access.log", "--status-range=400,499"});
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_TRUE(result.options.filter.status_lookup()[400]);
    EXPECT_TRUE(result.options.filter.status_lookup()[499]);
    EXPECT_EQ(result.options.filter.status_count(), 100U);
}

TEST(Cli, RejectsInvalidStatuses) {
    EXPECT_FALSE(parse({"a.log", "--status", "99"}).ok);
    EXPECT_FALSE(parse({"a.log", "--status", "600"}).ok);
    EXPECT_FALSE(parse({"a.log", "--status", "abc"}).ok);
    EXPECT_FALSE(parse({"a.log", "--status", "200,bad"}).ok);
    EXPECT_FALSE(parse({"a.log", "--status-range", "500", "400"}).ok);
    EXPECT_FALSE(parse({"a.log", "--status-range", "400"}).ok);
    EXPECT_FALSE(parse({"a.log", "--status-range", "99", "200"}).ok);
}

TEST(Cli, IpAndMethodFilters) {
    const hpla::CliParseResult result =
        parse({"access.log", "--ip", "192.168.1.10, ::1", "--method", "post,GET"});
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_EQ(result.options.filter.ips().size(), 2U);
    EXPECT_EQ(result.options.filter.ips()[0],
              *hpla::parse_ip("192.168.1.10"));
    EXPECT_EQ(result.options.filter.ips()[1], *hpla::parse_ip("::1"));
    ASSERT_EQ(result.options.filter.methods().size(), 2U);
    EXPECT_EQ(result.options.filter.methods()[0], "POST");
    EXPECT_EQ(result.options.filter.methods()[1], "GET");

    EXPECT_FALSE(parse({"a.log", "--ip", "not-an-ip"}).ok);
    EXPECT_FALSE(parse({"a.log", "--ip", "10.0.0.0/8"}).ok);
    EXPECT_FALSE(parse({"a.log", "--method", "BAD METHOD"}).ok);
    EXPECT_FALSE(parse({"a.log", "--method", ""}).ok);
}

TEST(Cli, PathAndUserAgentFilters) {
    const hpla::CliParseResult result =
        parse({"access.log", "--path", "/login", "--path", "/logout", "--path-contains",
               "api", "--user-agent", "Mozilla"});
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_EQ(result.options.filter.exact_paths().size(), 2U);
    EXPECT_EQ(result.options.filter.exact_paths()[1], "/logout");
    ASSERT_EQ(result.options.filter.path_substrings().size(), 1U);
    EXPECT_EQ(result.options.filter.path_substrings()[0], "api");
    ASSERT_EQ(result.options.filter.user_agent_substrings().size(), 1U);
    EXPECT_EQ(result.options.filter.user_agent_substrings()[0], "Mozilla");
}

TEST(Cli, TimeRange) {
    const hpla::CliParseResult result =
        parse({"access.log", "--from", "30/Oct/2026:08:00:00 +0000", "--to",
               "30/Oct/2026:18:00:00 +0200"});
    ASSERT_TRUE(result.ok) << result.error;

    std::int64_t expected_from = 0;
    std::int64_t expected_to = 0;
    ASSERT_TRUE(hpla::parse_http_timestamp("30/Oct/2026:08:00:00 +0000", expected_from));
    ASSERT_TRUE(hpla::parse_http_timestamp("30/Oct/2026:18:00:00 +0200", expected_to));
    ASSERT_TRUE(result.options.filter.from().has_value());
    ASSERT_TRUE(result.options.filter.to().has_value());
    EXPECT_EQ(*result.options.filter.from(), expected_from);
    EXPECT_EQ(*result.options.filter.to(), expected_to);

    EXPECT_FALSE(parse({"a.log", "--from", "2026-10-30"}).ok);
    EXPECT_FALSE(parse({"a.log", "--to", "30/Foo/2026:08:00:00 +0000"}).ok);
    EXPECT_FALSE(parse({"a.log", "--from", "30/Oct/2026:18:00:00 +0000", "--to",
                        "30/Oct/2026:08:00:00 +0000"})
                     .ok);
}

TEST(Cli, SizeFilters) {
    const hpla::CliParseResult result =
        parse({"access.log", "--min-size", "1K", "--max-size", "2M"});
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_TRUE(result.options.filter.min_size().has_value());
    ASSERT_TRUE(result.options.filter.max_size().has_value());
    EXPECT_EQ(*result.options.filter.min_size(), 1024U);
    EXPECT_EQ(*result.options.filter.max_size(), 2U * 1024U * 1024U);

    EXPECT_FALSE(parse({"a.log", "--min-size", "-1"}).ok);
    EXPECT_FALSE(parse({"a.log", "--max-size", "lots"}).ok);
    EXPECT_FALSE(parse({"a.log", "--min-size", "20", "--max-size", "10"}).ok);
}

TEST(Cli, ThreadsBufferTopAndDistinctLimits) {
    hpla::CliParseResult result =
        parse({"a.log", "-t", "8", "--buffer-size", "64K", "--top", "25",
               "--max-distinct", "100000"});
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.options.threads, 8U);
    EXPECT_EQ(result.options.buffer_size, 64U * 1024U);
    EXPECT_EQ(result.options.top_n, 25U);
    EXPECT_EQ(result.options.max_distinct, 100'000U);

    result = parse({"a.log", "--threads=0", "--top=1000", "--max-distinct=0"});
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.options.threads, 0U);
    EXPECT_EQ(result.options.top_n, 1000U);
    EXPECT_EQ(result.options.max_distinct, 0U);

    EXPECT_FALSE(parse({"a.log", "--threads", "257"}).ok);
    EXPECT_FALSE(parse({"a.log", "--threads", "many"}).ok);
    EXPECT_FALSE(parse({"a.log", "--buffer-size", "1024"}).ok);
    EXPECT_FALSE(parse({"a.log", "--buffer-size", "2G"}).ok);
    EXPECT_FALSE(parse({"a.log", "--top", "0"}).ok);
    EXPECT_FALSE(parse({"a.log", "--top", "1001"}).ok);
    EXPECT_FALSE(parse({"a.log", "--max-distinct", "nope"}).ok);
}

TEST(Cli, FormatInference) {
    hpla::CliParseResult result = parse({"a.log", "-o", "report.json"});
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.options.format, hpla::OutputFormat::Json);
    EXPECT_FALSE(result.options.format_explicit);

    result = parse({"a.log", "--output", "report.CSV"});
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.options.format, hpla::OutputFormat::Csv);

    result = parse({"a.log", "--format", "text", "-o", "report.json"});
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.options.format, hpla::OutputFormat::Text);
    EXPECT_TRUE(result.options.format_explicit);

    result = parse({"a.log", "--format=JSON"});
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.options.format, hpla::OutputFormat::Json);

    EXPECT_FALSE(parse({"a.log", "--format", "xml"}).ok);
}

TEST(Cli, RecordsRequireStructuredFormat) {
    EXPECT_FALSE(parse({"a.log", "--records"}).ok);
    EXPECT_FALSE(parse({"a.log", "--records", "--format", "text"}).ok);

    hpla::CliParseResult result = parse({"a.log", "--records", "--format", "json"});
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_TRUE(result.options.include_records);

    result = parse({"a.log", "--records", "-o", "filtered.csv"});
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_TRUE(result.options.include_records);
    EXPECT_EQ(result.options.format, hpla::OutputFormat::Csv);
}

TEST(Cli, RejectsOutputOverwritingInput) {
    const hpla::CliParseResult result = parse({"access.log", "-o", "access.log"});
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(contains(result.error, "must not overwrite"));
}

TEST(Cli, ColorModes) {
    EXPECT_EQ(parse({"a.log", "--color", "always"}).options.color,
              hpla::ColorMode::Always);
    EXPECT_EQ(parse({"a.log", "--color=never"}).options.color,
              hpla::ColorMode::Never);
    EXPECT_EQ(parse({"a.log", "--color", "auto"}).options.color,
              hpla::ColorMode::Auto);
    EXPECT_FALSE(parse({"a.log", "--color", "rainbow"}).ok);
}

TEST(Cli, SequentialAndMmapFlags) {
    const hpla::CliParseResult result =
        parse({"a.log", "--sequential", "--mmap"});
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_TRUE(result.options.sequential);
    EXPECT_TRUE(result.options.use_mmap);
}

TEST(Cli, HelpAndVersionDoNotRequireFiles) {
    hpla::CliParseResult result = parse({"--help"});
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_TRUE(result.options.show_help);

    result = parse({"-v"});
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_TRUE(result.options.show_version);

    result = parse({"--help", "--bogus"});
    EXPECT_FALSE(result.ok);
}

TEST(Cli, RejectsUnknownOptionsAndMissingValues) {
    EXPECT_FALSE(parse({"a.log", "--bogus"}).ok);
    EXPECT_FALSE(parse({"a.log", "-x"}).ok);
    EXPECT_FALSE(parse({"a.log", "--status"}).ok);
    EXPECT_FALSE(parse({"a.log", "--from"}).ok);
    EXPECT_FALSE(parse({"a.log", "--output="}).ok);
    EXPECT_FALSE(parse({"a.log", "--output"}).ok);
    EXPECT_FALSE(parse({}).ok);
}

TEST(Cli, UsageMentionsCoreOptions) {
    const std::string usage = hpla::usage_text();
    EXPECT_TRUE(contains(usage, "--status-range"));
    EXPECT_TRUE(contains(usage, "--path-contains"));
    EXPECT_TRUE(contains(usage, "--max-distinct"));
    EXPECT_TRUE(contains(usage, "--records"));
    EXPECT_TRUE(contains(usage, "EXIT CODES"));
}
