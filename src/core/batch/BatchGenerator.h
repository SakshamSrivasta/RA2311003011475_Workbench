// Reproducible generator for a batch of independent rod problems.
//
// The same seed gives bit-identical cases on every compiler and platform:
// std::mt19937_64's output sequence is fixed by the C++ standard, but
// std::uniform_real_distribution is not (libstdc++, libc++ and MSVC may map
// the same raw numbers to different doubles). So we convert raw 64-bit draws
// to doubles ourselves.
#pragma once

#include "core/fea/ThermalRod1D.h"

#include <cstdint>
#include <vector>

namespace wb::batch {

struct BatchConfig {
    std::uint64_t seed = 20260101;
    int caseCount = 400;
    int elementsPerCase = 5000;
    // Ranges for the varied parameters; both bounds must be positive.
    double minConductivity_W_mK = 1.0;
    double maxConductivity_W_mK = 400.0;
    double minHeatGeneration_W_m3 = 1.0e3;
    double maxHeatGeneration_W_m3 = 1.0e5;
};

constexpr int kMaxBatchCases = 100'000;

// Throws WorkbenchError(InvalidParameter) on a bad configuration.
void validate(const BatchConfig& config);

// Cases share the assignment rod (L, A, end temperatures) and differ in k and
// q, which are drawn uniformly from the configured ranges.
std::vector<fea::RodProblem> generateBatch(const BatchConfig& config);

}  // namespace wb::batch
