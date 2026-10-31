// CSV builders shared by the UI's Export buttons and the results generator,
// so a file downloaded from the app and the one in results/ are identical.
#pragma once

#include "core/batch/Benchmark.h"
#include "core/fea/ThermalRod1D.h"
#include "core/ml/Backprop.h"
#include "core/platform/SystemInfo.h"

#include <string>
#include <vector>

namespace wb::report {

// Long format: one row per node per mesh, plus the exact value and error.
std::string feaSolutionsCsv(const fea::RodProblem& base, const std::vector<int>& elementCounts);

// One row per mesh, plus midpoint rows (FE interpolation vs exact) per mesh.
std::string feaConvergenceCsv(const fea::RodProblem& base, const std::vector<int>& elementCounts);
std::string feaMidpointsCsv(const fea::RodProblem& base, const std::vector<int>& elementCounts);

std::string benchmarkCsv(const batch::BenchmarkConfig& config, const batch::BenchmarkReport& report,
                         const platform::SystemInfo& sys);

std::string gradientCheckCsv(const std::vector<ml::GradientCheckRow>& rows);
std::string lossHistoryCsv(const std::vector<double>& history);

}  // namespace wb::report
