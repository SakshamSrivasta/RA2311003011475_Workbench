#include "core/report/Exports.h"

#include "core/io/CsvWriter.h"

#include <cmath>

namespace wb::report {

namespace {

void problemComments(io::CsvWriter& csv, const fea::RodProblem& p) {
    csv.comment("Steady 1D conduction, linear elements: -d/dx(kA dT/dx) = qA, T(0)=T_left, T(L)=T_right");
    using io::formatNumber;
    csv.comment("L_m=" + formatNumber(p.length_m) + " A_m2=" + formatNumber(p.area_m2) +
                " k_W_mK=" + formatNumber(p.conductivity_W_mK) + " q_W_m3=" + formatNumber(p.heatGeneration_W_m3) +
                " T_left_C=" + formatNumber(p.leftTemperature_C) + " T_right_C=" + formatNumber(p.rightTemperature_C));
}

}  // namespace

std::string feaSolutionsCsv(const fea::RodProblem& base, const std::vector<int>& counts) {
    io::CsvWriter csv({"elements", "node", "x_m", "T_fe_C", "T_exact_C", "abs_error_C"});
    problemComments(csv, base);
    for (int n : counts) {
        fea::RodProblem p = base;
        p.elements = n;
        const auto s = fea::solve(p);
        for (std::size_t i = 0; i < s.x_m.size(); ++i) {
            const double exact = fea::exactTemperature(p, s.x_m[i]);
            csv.row(n, i, s.x_m[i], s.temperature_C[i], exact, std::abs(s.temperature_C[i] - exact));
        }
    }
    return csv.str();
}

std::string feaMidpointsCsv(const fea::RodProblem& base, const std::vector<int>& counts) {
    io::CsvWriter csv({"elements", "element", "x_mid_m", "T_fe_interp_C", "T_exact_C", "abs_error_C"});
    problemComments(csv, base);
    for (int n : counts) {
        fea::RodProblem p = base;
        p.elements = n;
        const auto s = fea::solve(p);
        for (std::size_t e = 0; e + 1 < s.x_m.size(); ++e) {
            const double xm = 0.5 * (s.x_m[e] + s.x_m[e + 1]);
            const double fe = 0.5 * (s.temperature_C[e] + s.temperature_C[e + 1]);
            const double exact = fea::exactTemperature(p, xm);
            csv.row(n, e, xm, fe, exact, std::abs(fe - exact));
        }
    }
    return csv.str();
}

std::string feaConvergenceCsv(const fea::RodProblem& base, const std::vector<int>& counts) {
    io::CsvWriter csv({"elements", "h_m", "max_midpoint_error_C", "max_nodal_error_C", "observed_order",
                       "theory_qh2_over_8k_C"});
    problemComments(csv, base);
    for (const auto& r : fea::convergenceStudy(base, counts)) {
        const double theory = base.heatGeneration_W_m3 * r.elementSize_m * r.elementSize_m / (8.0 * base.conductivity_W_mK);
        csv.row(r.elements, r.elementSize_m, r.maxMidpointError_C, r.maxNodalError_C, r.observedOrder, theory);
    }
    return csv.str();
}

std::string benchmarkCsv(const batch::BenchmarkConfig& cfg, const batch::BenchmarkReport& rep,
                         const platform::SystemInfo& sys) {
    io::CsvWriter csv({"mode", "workers", "median_s", "speedup_T1_over_Tp", "efficiency", "max_abs_diff_vs_serial_C",
                       "run1_s", "run2_s", "run3_s", "run4_s", "run5_s"});
    csv.comment("cpu=" + sys.cpuName + " physical_cores=" + std::to_string(sys.physicalCores) +
                " logical_cpus=" + std::to_string(sys.logicalCpus) +
                " ram_GiB=" + std::to_string(static_cast<double>(sys.totalRamBytes) / (1024.0 * 1024.0 * 1024.0)));
    csv.comment("os=" + sys.os + " compiler=" + sys.compiler + " build=" + sys.buildType + " flags=" + sys.buildFlags);
    csv.comment("cases=" + std::to_string(cfg.batch.caseCount) + " elements_per_case=" +
                std::to_string(cfg.batch.elementsPerCase) + " seed=" + std::to_string(cfg.batch.seed) +
                " warmup_runs=" + std::to_string(cfg.warmupRuns) + " measured_runs=" + std::to_string(cfg.measuredRuns));
    csv.comment(std::string("speedup_assessable=") + (rep.speedupAssessable ? "yes" : "no (single logical CPU)") +
                " all_agree_within_1e-8_C=" + (rep.allAgree ? "yes" : "NO"));

    auto write = [&](const char* mode, const batch::TimingRow& row) {
        const bool serial = row.workers == 0;  // reference row: speedup is defined against the 1-worker pool
        std::vector<std::string> cells = {mode, std::to_string(row.workers), io::formatNumber(row.median_s),
                                          serial ? "n/a" : io::formatNumber(row.speedup),
                                          serial ? "n/a" : io::formatNumber(row.efficiency),
                                          io::formatNumber(row.maxAbsDiffVsSerial_C)};
        for (double r : row.runs_s) cells.push_back(io::formatNumber(r));
        csv.writeRow(cells);
    };
    write("serial_loop", rep.serial);
    for (const auto& row : rep.parallel) write("worker_pool", row);
    return csv.str();
}

std::string gradientCheckCsv(const std::vector<ml::GradientCheckRow>& rows) {
    io::CsvWriter out({"parameter", "analytic", "central_difference", "abs_error", "pass_le_1e-5"});
    out.comment("x=1 y=2 w=0.5 b=0 v=1 c=0 eps=1e-6");
    for (const auto& r : rows) out.row(r.name, r.analytic, r.numerical, r.absError, r.passed ? "yes" : "no");
    return out.str();
}

std::string lossHistoryCsv(const std::vector<double>& history) {
    io::CsvWriter csv({"step", "loss_J"});
    csv.comment("step 0 = initial parameters; learning rate 0.1; full-batch gradient descent on one sample");
    for (std::size_t i = 0; i < history.size(); ++i) csv.row(i, history[i]);
    return csv.str();
}

}  // namespace wb::report
