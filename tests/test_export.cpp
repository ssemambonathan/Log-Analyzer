#include <gtest/gtest.h>

#include <cctype>
#include <cstddef>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "engine/engine.hpp"
#include "export/exporter.hpp"
#include "test_helpers.hpp"

namespace {

// Minimal strict JSON parser used to prove that exported reports are valid
// RFC 8259 documents. It accepts objects, arrays, strings, numbers, booleans,
// and null, and rejects control characters and malformed escapes.
class JsonValidator {
public:
    explicit JsonValidator(std::string_view text) : text_(text) {}

    bool valid() {
        skip_whitespace();
        if (!parse_value()) {
            return false;
        }
        skip_whitespace();
        return pos_ == text_.size();
    }

private:
    void skip_whitespace() {
        while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t' ||
                                       text_[pos_] == '\n' || text_[pos_] == '\r')) {
            ++pos_;
        }
    }

    bool consume(char expected) {
        if (pos_ < text_.size() && text_[pos_] == expected) {
            ++pos_;
            return true;
        }
        return false;
    }

    bool parse_value() {
        if (pos_ >= text_.size()) {
            return false;
        }
        switch (text_[pos_]) {
            case '{': return parse_object();
            case '[': return parse_array();
            case '"': {
                std::string_view unused;
                return parse_string(unused);
            }
            case 't': return parse_literal("true");
            case 'f': return parse_literal("false");
            case 'n': return parse_literal("null");
            default: return parse_number();
        }
    }

    bool parse_literal(std::string_view literal) {
        if (text_.substr(pos_, literal.size()) != literal) {
            return false;
        }
        pos_ += literal.size();
        return true;
    }

    bool parse_string(std::string_view& out) {
        if (!consume('"')) {
            return false;
        }
        const std::size_t start = pos_;
        while (pos_ < text_.size()) {
            const auto c = static_cast<unsigned char>(text_[pos_]);
            if (c == '"') {
                out = text_.substr(start, pos_ - start);
                ++pos_;
                return true;
            }
            if (c < 0x20) {
                return false;
            }
            if (c == '\\') {
                ++pos_;
                if (pos_ >= text_.size()) {
                    return false;
                }
                const char escape = text_[pos_];
                if (escape == '"' || escape == '\\' || escape == '/' || escape == 'b' ||
                    escape == 'f' || escape == 'n' || escape == 'r' || escape == 't') {
                    ++pos_;
                } else if (escape == 'u') {
                    ++pos_;
                    for (int digit = 0; digit < 4; ++digit) {
                        if (pos_ >= text_.size() ||
                            !std::isxdigit(static_cast<unsigned char>(text_[pos_]))) {
                            return false;
                        }
                        ++pos_;
                    }
                } else {
                    return false;
                }
            } else {
                ++pos_;
            }
        }
        return false;
    }

    bool parse_number() {
        const std::size_t start = pos_;
        consume('-');
        if (pos_ >= text_.size()) {
            return false;
        }
        if (text_[pos_] == '0') {
            ++pos_;
        } else if (text_[pos_] >= '1' && text_[pos_] <= '9') {
            while (pos_ < text_.size() &&
                   std::isdigit(static_cast<unsigned char>(text_[pos_])) != 0) {
                ++pos_;
            }
        } else {
            return false;
        }
        if (consume('.')) {
            if (pos_ >= text_.size() ||
                std::isdigit(static_cast<unsigned char>(text_[pos_])) == 0) {
                return false;
            }
            while (pos_ < text_.size() &&
                   std::isdigit(static_cast<unsigned char>(text_[pos_])) != 0) {
                ++pos_;
            }
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) {
                ++pos_;
            }
            if (pos_ >= text_.size() ||
                std::isdigit(static_cast<unsigned char>(text_[pos_])) == 0) {
                return false;
            }
            while (pos_ < text_.size() &&
                   std::isdigit(static_cast<unsigned char>(text_[pos_])) != 0) {
                ++pos_;
            }
        }
        return pos_ > start;
    }

    bool parse_object() {
        if (!consume('{')) {
            return false;
        }
        skip_whitespace();
        if (consume('}')) {
            return true;
        }
        while (true) {
            std::string_view key;
            if (!parse_string(key)) {
                return false;
            }
            skip_whitespace();
            if (!consume(':')) {
                return false;
            }
            skip_whitespace();
            if (!parse_value()) {
                return false;
            }
            skip_whitespace();
            if (consume('}')) {
                return true;
            }
            if (!consume(',')) {
                return false;
            }
            skip_whitespace();
        }
    }

    bool parse_array() {
        if (!consume('[')) {
            return false;
        }
        skip_whitespace();
        if (consume(']')) {
            return true;
        }
        while (true) {
            if (!parse_value()) {
                return false;
            }
            skip_whitespace();
            if (consume(']')) {
                return true;
            }
            if (!consume(',')) {
                return false;
            }
            skip_whitespace();
        }
    }

    std::string_view text_;
    std::size_t pos_ = 0;
};

struct Analysis {
    std::vector<hpla::FileInfo> files;
    hpla::EngineOptions options;
    hpla::AnalysisResult result;
};

Analysis analyze(const std::string& path, const hpla::FilterSet& filter = hpla::FilterSet{}) {
    Analysis analysis;
    analysis.files = hpla::stat_input_files({path});
    analysis.options.buffer_size = 64;
    analysis.options.threads = 2;
    analysis.result = hpla::LogAnalyzer::run(analysis.files, filter, analysis.options);
    return analysis;
}

std::string export_json(const Analysis& analysis, const hpla::FilterSet& filter,
                        bool include_records) {
    std::ostringstream out;
    hpla::write_json_report(out, analysis.result, analysis.files, filter, analysis.options,
                            include_records);
    return out.str();
}

std::string export_csv(const Analysis& analysis, const hpla::FilterSet& filter,
                       bool include_records) {
    std::ostringstream out;
    hpla::write_csv_report(out, analysis.result, analysis.files, filter, analysis.options,
                           include_records);
    return out.str();
}

std::size_t count_substring(std::string_view text, std::string_view needle) {
    std::size_t count = 0;
    std::size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string_view::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

// The first record carries deliberately awkward bytes in its target and user
// agent: a comma, an escaped quote, a tab, and a raw control character.
const std::string& tricky_log() {
    static const std::string log =
        "127.0.0.1 - alice [30/Oct/2026:08:00:00 +0000] "
        "\"GET /a,b\\\"c HTTP/1.1\" 200 10 \"-\" "
        "\"Agent \\\"quoted\\\", inc\t\x01\"\n"
        "10.0.0.1 - - [30/Oct/2026:08:01:00 +0000] "
        "\"POST /x,y?q=1 HTTP/1.1\" 404 - "
        "\"http://example.com/x,y\" \"plain-agent\"\n"
        "malformed record without fields\n";
    return log;
}

}  // namespace

TEST(Export, JsonReportIsValidAndEscaped) {
    const hpla_test::TempFile file(tricky_log(), "json");
    const Analysis analysis = analyze(file.path_string());
    ASSERT_EQ(analysis.result.statistics.matched_records, 2U);

    const std::string json = export_json(analysis, hpla::FilterSet{}, false);
    EXPECT_TRUE(JsonValidator(json).valid()) << json;
    EXPECT_EQ(count_substring(json, "\"records\""), 0U);
    EXPECT_TRUE(json.find("\"tool\": \"hpla\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"lines_examined\": 3") != std::string::npos);
    EXPECT_TRUE(json.find("\"malformed_records\": 1") != std::string::npos);

    // Backslash and quote from the request target are escaped in the summary.
    EXPECT_TRUE(json.find(R"("resource": "/a,b\\\"c")") != std::string::npos) << json;
    EXPECT_EQ(json.find('\t'), std::string::npos);
    EXPECT_EQ(json.find('\x01'), std::string::npos);

    const std::string records = export_json(analysis, hpla::FilterSet{}, true);
    EXPECT_TRUE(JsonValidator(records).valid()) << records;
    // Backslash, quote, tab, and U+0001 must all be escaped.
    EXPECT_TRUE(records.find(R"("user_agent": "Agent \\\"quoted\\\", inc\t\u0001")") !=
                std::string::npos)
        << records;
    EXPECT_EQ(records.find('\t'), std::string::npos);
    EXPECT_EQ(records.find('\x01'), std::string::npos);
}

TEST(Export, JsonRecordsModeRespectsFilter) {
    const hpla_test::TempFile file(tricky_log(), "json-records");
    hpla::FilterSet filter;
    filter.set_statuses({404});
    filter.add_description("status", "404");
    const Analysis analysis = analyze(file.path_string(), filter);
    ASSERT_EQ(analysis.result.statistics.matched_records, 1U);

    const std::string json = export_json(analysis, filter, true);
    EXPECT_TRUE(JsonValidator(json).valid()) << json;
    EXPECT_TRUE(json.find("\"active\": true") != std::string::npos);
    EXPECT_TRUE(json.find(R"({"name": "status", "value": "404"})") != std::string::npos);
    EXPECT_EQ(count_substring(json, "\"records\""), 1U);
    EXPECT_EQ(count_substring(json, "\"timestamp_epoch\""), 1U);
    EXPECT_TRUE(json.find("\"status\": 404") != std::string::npos);
    EXPECT_TRUE(json.find("\"bytes\": null") != std::string::npos);
}

TEST(Export, JsonWithoutRecordsHasNoRecordArray) {
    const hpla_test::TempFile file(hpla_test::make_log(40), "json-summary");
    const Analysis analysis = analyze(file.path_string());
    const std::string json = export_json(analysis, hpla::FilterSet{}, false);
    EXPECT_TRUE(JsonValidator(json).valid()) << json;
    EXPECT_EQ(count_substring(json, "\"records\""), 0U);
    EXPECT_TRUE(json.find("\"counting_method\": \"exact\"") != std::string::npos);
}

TEST(Export, CsvSummaryHasHeaderMetricsAndFilters) {
    const hpla_test::TempFile file(tricky_log(), "csv-summary");
    hpla::FilterSet filter;
    filter.set_statuses({200, 404});
    filter.add_description("status", "200,404");
    const Analysis analysis = analyze(file.path_string(), filter);

    const std::string csv = export_csv(analysis, filter, false);
    EXPECT_EQ(csv.rfind("section,metric,value\n", 0), 0U);
    EXPECT_TRUE(csv.find("processing,lines_examined,3\n") != std::string::npos);
    EXPECT_TRUE(csv.find("processing,matched_records,2\n") != std::string::npos);
    EXPECT_TRUE(csv.find("filter,status,\"200,404\"\n") != std::string::npos);
    EXPECT_TRUE(csv.find("unique_client_ips,counting_method,exact\n") != std::string::npos);
    EXPECT_TRUE(csv.find("status,200,1\n") != std::string::npos);
    EXPECT_TRUE(csv.find("status,404,1\n") != std::string::npos);
    EXPECT_FALSE(csv.empty());
    EXPECT_EQ(csv.back(), '\n');
}

TEST(Export, CsvRecordsEscapeCommasQuotesAndControls) {
    const hpla_test::TempFile file(tricky_log(), "csv-records");
    const Analysis analysis = analyze(file.path_string());
    const std::string csv = export_csv(analysis, hpla::FilterSet{}, true);

    EXPECT_EQ(csv.rfind("client_ip,ident,user,timestamp_epoch,timestamp_iso,method,target,"
                        "protocol,status,bytes,referrer,user_agent\n",
                        0),
              0U);
    // Header plus two matching records; the malformed line is excluded.
    EXPECT_EQ(count_substring(csv, "\n"), 3U) << csv;

    // Raw field: Agent \"quoted\", inc<TAB><0x01>. Commas and quotes force
    // quoting, embedded quotes are doubled, and the control bytes pass through.
    EXPECT_TRUE(csv.find("\"Agent \\\"\"quoted\\\"\", inc\t\x01\"") != std::string::npos)
        << csv;
    EXPECT_TRUE(csv.find("\"/x,y?q=1\"") != std::string::npos) << csv;
    EXPECT_TRUE(csv.find(",\"/a,b\\\"\"c\",") != std::string::npos) << csv;
}

TEST(Export, EmptyInputProducesValidDocuments) {
    const hpla_test::TempFile file(std::string(""), "empty-export");
    const Analysis analysis = analyze(file.path_string());

    const std::string json = export_json(analysis, hpla::FilterSet{}, true);
    EXPECT_TRUE(JsonValidator(json).valid()) << json;
    EXPECT_TRUE(json.find("\"lines_examined\": 0") != std::string::npos);

    const std::string csv = export_csv(analysis, hpla::FilterSet{}, false);
    EXPECT_EQ(csv.rfind("section,metric,value\n", 0), 0U);
    EXPECT_TRUE(csv.find("processing,lines_examined,0\n") != std::string::npos);
}
