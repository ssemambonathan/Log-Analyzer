#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace hpla {

std::uint64_t hash_bytes(std::string_view data) noexcept;

// HyperLogLog cardinality sketch (p = 12, 4096 registers). Used only for the
// approximate unique-IP count; the reported relative error is about 1.6%.
class HllSketch {
public:
    static constexpr unsigned kPrecision = 12;
    static constexpr std::size_t kRegisterCount = std::size_t{1} << kPrecision;

    void add(std::string_view value) noexcept;
    void merge(const HllSketch& other) noexcept;
    std::uint64_t estimate() const noexcept;

private:
    std::array<std::uint8_t, kRegisterCount> registers_{};
};

}  // namespace hpla
