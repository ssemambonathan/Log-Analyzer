#pragma once

#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

#include "engine/engine.hpp"
#include "filter/filter.hpp"
#include "stats/accumulator.hpp"

namespace hpla {

enum class OutputFormat { Text, Json, Csv };

void write_json_report(std::ostream& out, const AnalysisResult& result,
                       const std::vector<FileInfo>& files, const FilterSet& filter,
                       const EngineOptions& options, bool include_records);

void write_csv_report(std::ostream& out, const AnalysisResult& result,
                      const std::vector<FileInfo>& files, const FilterSet& filter,
                      const EngineOptions& options, bool include_records);

}  // namespace hpla
