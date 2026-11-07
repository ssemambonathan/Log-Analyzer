#pragma once

#include <iosfwd>
#include <vector>

#include "filter/filter.hpp"
#include "stats/accumulator.hpp"

namespace hpla {

void write_text_report(std::ostream& out, const AnalysisResult& result,
                       const std::vector<FileInfo>& files, const FilterSet& filter, bool color);

}  // namespace hpla
