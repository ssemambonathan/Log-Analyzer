#include "stats/hyperloglog.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

namespace hpla {
namespace {
constexpr std::uint64_t kFnvOffset = 1469598103934665603ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
}  // namespace

std::uint64_t hash_bytes(std::string_view data) noexcept {
    std::uint64_t hash = kFnvOffset;
    for (const char c : data) {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
        hash *= kFnvPrime;
    }
    // Final avalanche so that the low/high bits are well mixed.
    hash ^= hash >> 33;
    hash *= 0xff51afd7ed558ccdULL;
    hash ^= hash >> 33;
    hash *= 0xc4ceb9fe1a85ec53ULL;
    hash ^= hash >> 33;
    return hash;
}

void HllSketch::add(std::string_view value) noexcept {
    const std::uint64_t hash = hash_bytes(value);
    const std::size_t index = static_cast<std::size_t>(hash >> (64 - kPrecision));
    const std::uint64_t remaining = hash << kPrecision;
    const unsigned rank =
        remaining == 0 ? 64U - kPrecision : static_cast<unsigned>(std::countl_zero(remaining)) + 1U;
    auto& slot = registers_[index];
    if (rank > slot) {
        slot = static_cast<std::uint8_t>(rank);
    }
}

void HllSketch::merge(const HllSketch& other) noexcept {
    for (std::size_t i = 0; i < registers_.size(); ++i) {
        registers_[i] = std::max(registers_[i], other.registers_[i]);
    }
}

std::uint64_t HllSketch::estimate() const noexcept {
    const double m = static_cast<double>(kRegisterCount);
    double harmonic = 0.0;
    std::size_t zeros = 0;
    for (const std::uint8_t value : registers_) {
        harmonic += std::ldexp(1.0, -static_cast<int>(value));
        if (value == 0) {
            ++zeros;
        }
    }

    const double alpha = 0.7213 / (1.0 + 1.079 / m);
    const double raw = alpha * m * m / harmonic;

    if (raw <= 2.5 * m && zeros > 0) {
        const double linear = m * std::log(m / static_cast<double>(zeros));
        return static_cast<std::uint64_t>(linear + 0.5);
    }
    return static_cast<std::uint64_t>(raw + 0.5);
}

}  // namespace hpla
