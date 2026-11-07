#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "io/file_line_reader.hpp"
#include "test_helpers.hpp"

namespace {

std::vector<std::string> read_range(const std::string& path, std::uint64_t start, std::uint64_t end,
                                    std::size_t buffer_size) {
    hpla::FileLineReader reader(path, start, end, buffer_size);
    std::vector<std::string> lines;
    std::string_view line;
    while (reader.next(line)) {
        lines.emplace_back(line);
    }
    return lines;
}

}  // namespace

TEST(LineReader, ReadsLfAndCrlfWithAndWithoutFinalNewline) {
    const hpla_test::TempFile lf(std::string("alpha\nbeta\ngamma"), "lf");
    EXPECT_EQ(read_range(lf.path_string(), 0, lf.size(), 16),
              (std::vector<std::string>{"alpha", "beta", "gamma"}));

    const hpla_test::TempFile crlf(std::string("alpha\r\nbeta\r\ngamma\r\n"), "crlf");
    EXPECT_EQ(read_range(crlf.path_string(), 0, crlf.size(), 16),
              (std::vector<std::string>{"alpha", "beta", "gamma"}));
}

TEST(LineReader, HandlesEmptyFileAndEmptyLines) {
    const hpla_test::TempFile empty(std::string(""), "empty");
    EXPECT_TRUE(read_range(empty.path_string(), 0, empty.size(), 16).empty());

    const hpla_test::TempFile blanks(std::string("\n\n"), "blanks");
    EXPECT_EQ(read_range(blanks.path_string(), 0, blanks.size(), 16),
              (std::vector<std::string>{"", ""}));

    const hpla_test::TempFile mixed(std::string("a\n\nb\n"), "mixed");
    EXPECT_EQ(read_range(mixed.path_string(), 0, mixed.size(), 16),
              (std::vector<std::string>{"a", "", "b"}));
}

TEST(LineReader, GrowsBufferForLongLines) {
    std::string content(5000, 'x');
    content += "\ntail\n";
    const hpla_test::TempFile file(content, "long");
    const auto lines = read_range(file.path_string(), 0, file.size(), 16);
    ASSERT_EQ(lines.size(), 2U);
    EXPECT_EQ(lines[0].size(), 5000U);
    EXPECT_EQ(lines[1], "tail");
}

TEST(LineReader, ReadsSingleLineWithoutNewline) {
    const hpla_test::TempFile file(std::string("only-one-line"), "single");
    EXPECT_EQ(read_range(file.path_string(), 0, file.size(), 16),
              (std::vector<std::string>{"only-one-line"}));
}

TEST(LineReader, PartitionsFileAtEveryBoundaryWithoutLossOrDuplication) {
    std::string content;
    for (int index = 0; index < 40; ++index) {
        content.append("line-");
        content.append(std::to_string(index));
        content.append(index % 3 == 0 ? "\n" : (index % 3 == 1 ? "\r\n" : "\n"));
    }
    const hpla_test::TempFile file(content, "partition");
    const auto all = read_range(file.path_string(), 0, file.size(), 32);

    for (std::uint64_t boundary = 0; boundary <= file.size(); ++boundary) {
        std::vector<std::string> combined =
            read_range(file.path_string(), 0, boundary, 32);
        std::vector<std::string> tail =
            read_range(file.path_string(), boundary, file.size(), 32);
        combined.insert(combined.end(), tail.begin(), tail.end());
        EXPECT_EQ(combined, all) << "boundary " << boundary;
    }
}

TEST(LineReader, DoesNotReturnExtraLineAtEnd) {
    const hpla_test::TempFile file(std::string("a\nb\n"), "trailing");
    EXPECT_EQ(read_range(file.path_string(), 0, file.size(), 16),
              (std::vector<std::string>{"a", "b"}));
}

TEST(LineReader, PreservesBinaryBytes) {
    const std::string content("key=value\xE2\x9C\x93\n", 13);
    const hpla_test::TempFile file(content, "utf8");
    const auto lines = read_range(file.path_string(), 0, file.size(), 16);
    ASSERT_EQ(lines.size(), 1U);
    EXPECT_EQ(lines[0], std::string("key=value\xE2\x9C\x93"));
}
