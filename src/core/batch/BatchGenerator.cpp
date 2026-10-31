#include "core/batch/BatchGenerator.h"

#include "core/common/Errors.h"

#include <random>
#include <sstream>

namespace wb::batch {

namespace {

// Maps the top 53 bits of a 64-bit draw to [0, 1). Portable and exact.
double toUnitInterval(std::uint64_t raw) {
    return static_cast<double>(raw >> 11) * 0x1.0p-53;
}

void requireRange(double lo, double hi, const char* name, const char* unit) {
    requirePositiveFinite(lo, name, unit);
    requirePositiveFinite(hi, name, unit);
    if (lo > hi) {
        std::ostringstream msg;
        msg << name << " minimum (" << lo << ") is larger than its maximum (" << hi
            << "). Swap the two values.";
        throw WorkbenchError(ErrorCode::InvalidParameter, msg.str());
    }
}

}  // namespace

void validate(const BatchConfig& c) {
    if (c.caseCount < 1 || c.caseCount > kMaxBatchCases) {
        std::ostringstream msg;
        msg << "Case count must be between 1 and " << kMaxBatchCases << " (got " << c.caseCount << ").";
        throw WorkbenchError(ErrorCode::InvalidParameter, msg.str());
    }
    fea::RodProblem probe;
    probe.elements = c.elementsPerCase;
    fea::validate(probe);  // reuses the element-count check and message
    const long long values = static_cast<long long>(c.caseCount) * (static_cast<long long>(c.elementsPerCase) + 1);
    if (values > kMaxBatchValues) {
        std::ostringstream msg;
        msg << "Batch is too large: " << c.caseCount << " cases x " << c.elementsPerCase << " elements needs about "
            << values * 8 / (1024 * 1024) << " MB per run (limit " << kMaxBatchValues * 8 / (1024 * 1024)
            << " MB). Reduce the case count or mesh size.";
        throw WorkbenchError(ErrorCode::InvalidParameter, msg.str());
    }
    requireRange(c.minConductivity_W_mK, c.maxConductivity_W_mK, "Conductivity k", "W/(m K)");
    requireRange(c.minHeatGeneration_W_m3, c.maxHeatGeneration_W_m3, "Heat generation q", "W/m^3");
}

std::vector<fea::RodProblem> generateBatch(const BatchConfig& c) {
    validate(c);

    std::mt19937_64 rng(c.seed);
    std::vector<fea::RodProblem> cases;
    cases.reserve(static_cast<std::size_t>(c.caseCount));

    for (int i = 0; i < c.caseCount; ++i) {
        fea::RodProblem p;  // assignment defaults for L, A and end temperatures
        p.elements = c.elementsPerCase;
        // Draw k then q, always in this order, so case i depends only on the seed.
        const double uk = toUnitInterval(rng());
        const double uq = toUnitInterval(rng());
        p.conductivity_W_mK = c.minConductivity_W_mK + uk * (c.maxConductivity_W_mK - c.minConductivity_W_mK);
        p.heatGeneration_W_m3 = c.minHeatGeneration_W_m3 + uq * (c.maxHeatGeneration_W_m3 - c.minHeatGeneration_W_m3);
        cases.push_back(p);
    }
    return cases;
}

}  // namespace wb::batch
