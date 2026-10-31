// Regenerates everything in results/: raw CSVs and SVG plots for the FEA
// study, the serial/parallel benchmark, the backprop check and the file-size
// tests. Run it on the machine whose numbers you want to report.
//
// Usage: wb_results <output-dir> [--cases N] [--elements M] [--skip-io]

#include "SvgPlot.h"

#include "core/batch/Benchmark.h"
#include "core/common/Stopwatch.h"
#include "core/fea/ThermalRod1D.h"
#include "core/io/CsvWriter.h"
#include "core/io/NodalCsvReader.h"
#include "core/io/StlReader.h"
#include "core/io/TextFile.h"
#include "core/ml/Backprop.h"
#include "core/platform/SystemInfo.h"
#include "core/report/Exports.h"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

using namespace wb;
using tools::PlotSpec;
using tools::Series;

namespace {

// Ordinal blue ramp for mesh refinement (coarse = light, fine = dark); the
// exact solution is drawn in text ink so it never competes with a mesh colour.
const char* kMeshColors[] = {"#86b6ef", "#3987e5", "#1c5cab", "#0d366b"};
const char* kInk = "#0b0b0b";
const char* kInkMuted = "#52514e";
const char* kBlue = "#2a78d6";

std::string outDir;

void save(const std::string& name, const std::string& text) {
    io::writeTextFileAtomic(outDir + "/" + name, text);
    std::cout << "  wrote results/" << name << '\n';
}

void feaStudy() {
    std::cout << "FEA convergence study\n";
    const fea::RodProblem base;
    const std::vector<int> counts{10, 20, 40, 80};
    save("fea_solutions.csv", report::feaSolutionsCsv(base, counts));
    save("fea_midpoints.csv", report::feaMidpointsCsv(base, counts));
    save("fea_convergence.csv", report::feaConvergenceCsv(base, counts));

    PlotSpec profile;
    profile.title = "Rod temperature: FE solution vs exact";
    profile.subtitle = "L = 1 m, k = 10 W/(m K), q = 10 000 W/m^3, T(0) = 100 C, T(L) = 20 C";
    profile.xLabel = "Position x (m)";
    profile.yLabel = "Temperature T (C)";
    Series exact{"Exact T(x)", {}, {}, kInk, true, false, true};
    for (int i = 0; i <= 400; ++i) {
        exact.x.push_back(i / 400.0);
        exact.y.push_back(fea::exactTemperature(base, i / 400.0));
    }
    for (std::size_t c = 0; c < counts.size(); ++c) {
        fea::RodProblem p = base;
        p.elements = counts[c];
        const auto s = fea::solve(p);
        profile.series.push_back({"FE, N = " + std::to_string(counts[c]), s.x_m, s.temperature_C, kMeshColors[c], false,
                                  counts[c] <= 10, true});
    }
    profile.series.push_back(exact);
    save("fea_temperature_profiles.svg", tools::renderSvg(profile));

    // Zoom on the peak so the gap between linear segments and the parabola is visible.
    PlotSpec zoom = profile;
    zoom.title = "Zoom near the peak: linear elements cut the parabola's corners";
    zoom.subtitle = "Nodes sit on the exact curve; the error lives between them, worst at element midpoints";
    for (auto& s : zoom.series) {
        Series cut = s;
        cut.x.clear();
        cut.y.clear();
        for (std::size_t i = 0; i < s.x.size(); ++i) {
            if (s.x[i] >= 0.3 && s.x[i] <= 0.62) { cut.x.push_back(s.x[i]); cut.y.push_back(s.y[i]); }
        }
        s = cut;
    }
    save("fea_temperature_zoom.svg", tools::renderSvg(zoom));

    PlotSpec conv;
    conv.title = "Mesh convergence: max midpoint error";
    conv.subtitle = "Slope 2 on log-log axes = second-order convergence; dashed line is q h^2 / (8k)";
    conv.xLabel = "Element size h (m)";
    conv.yLabel = "Max |T_FE - T_exact| at midpoints (C)";
    conv.logX = conv.logY = true;
    Series measured{"FE midpoint error", {}, {}, kBlue, false, true, true};
    Series theory{"Theory q h^2 / (8k)", {}, {}, kInkMuted, true, false, true};
    for (const auto& r : fea::convergenceStudy(base, counts)) {
        measured.x.push_back(r.elementSize_m);
        measured.y.push_back(r.maxMidpointError_C);
    }
    for (double h : {0.2, 0.1, 0.05, 0.025, 0.0125, 0.008}) {
        theory.x.push_back(h);
        theory.y.push_back(base.heatGeneration_W_m3 * h * h / (8 * base.conductivity_W_mK));
    }
    conv.series = {measured, theory};
    save("fea_convergence.svg", tools::renderSvg(conv));
}

void backpropStudy() {
    std::cout << "Backprop gradient check and training\n";
    save("backprop_gradient_check.csv", report::gradientCheckCsv(ml::gradientCheck(ml::Params{}, ml::Sample{})));
    const auto history = ml::trainLossHistory(ml::Params{}, ml::Sample{}, 0.1, 100);
    save("backprop_loss.csv", report::lossHistoryCsv(history));

    PlotSpec plot;
    plot.title = "Training loss over 100 gradient-descent steps";
    plot.subtitle = "One sample (x = 1, y = 2), learning rate 0.1, start w = 0.5, b = 0, v = 1, c = 0";
    plot.xLabel = "Step";
    plot.yLabel = "Loss J (log scale)";
    plot.logY = true;
    Series s{"Loss J", {}, {}, kBlue, false, false, true};
    for (std::size_t i = 0; i < history.size(); ++i) {
        s.x.push_back(static_cast<double>(i));
        s.y.push_back(history[i]);
    }
    plot.series = {s};
    save("backprop_loss.svg", tools::renderSvg(plot));
}

void benchmarkStudy(int cases, int elements) {
    std::cout << "Serial vs parallel benchmark (" << cases << " cases x " << elements << " elements)\n";
    batch::BenchmarkConfig cfg;
    cfg.batch.caseCount = cases;
    cfg.batch.elementsPerCase = elements;
    const auto rep = batch::runBenchmark(cfg);
    const auto sys = platform::querySystemInfo();
    save("benchmark.csv", report::benchmarkCsv(cfg, rep, sys));

    PlotSpec plot;
    plot.title = "Batch solve speedup vs worker threads";
    plot.subtitle = std::to_string(cases) + " rods x " + std::to_string(elements) + " elements, median of 5 runs, " +
                    sys.cpuName;
    plot.xLabel = "Worker threads p";
    plot.yLabel = "Speedup T1 / Tp";
    Series measured{"Measured", {}, {}, kBlue, false, true, true};
    Series ideal{"Ideal (= p)", {}, {}, kInkMuted, true, false, true};
    for (const auto& r : rep.parallel) {
        measured.x.push_back(r.workers);
        measured.y.push_back(r.speedup);
        ideal.x.push_back(r.workers);
        ideal.y.push_back(r.workers);
    }
    plot.series = {measured, ideal};
    save("benchmark_speedup.svg", tools::renderSvg(plot));

    std::cout << "    serial loop median " << rep.serial.median_s << " s\n";
    for (const auto& r : rep.parallel) {
        std::cout << "    p=" << r.workers << "  median " << r.median_s << " s  speedup " << r.speedup << "  efficiency "
                  << r.efficiency << "  max|diff| " << r.maxAbsDiffVsSerial_C << " C\n";
    }
    if (!rep.allAgree) std::cout << "    WARNING: serial and parallel results disagree beyond 1e-8 C\n";
}

// Builds files of increasing size in memory and times the parsers on them.
void ioSizeStudy() {
    std::cout << "File size tests\n";
    io::CsvWriter csv({"format", "records", "megabytes", "parse_ms", "result"});

    for (std::size_t tris : {1'000ul, 10'000ul, 100'000ul, 500'000ul, 800'000ul}) {
        std::ostringstream stl;
        stl << "solid big\n";
        for (std::size_t i = 0; i < tris; ++i) {
            const double x = static_cast<double>(i % 1000), y = static_cast<double>(i / 1000);
            stl << " facet normal 0 0 1\n  outer loop\n   vertex " << x << ' ' << y << " 0\n   vertex " << x + 1 << ' ' << y
                << " 0\n   vertex " << x << ' ' << y + 1 << " 0\n  endloop\n endfacet\n";
        }
        stl << "endsolid big\n";
        const std::string text = stl.str();
        Stopwatch sw;
        std::string result = "ok";
        try {
            io::parseAsciiStl(text);
        } catch (const std::exception& e) {
            result = std::string("rejected: ") + e.what();
        }
        csv.row("ascii_stl", tris, static_cast<double>(text.size()) / 1048576.0, sw.elapsedSeconds() * 1000.0, "\"" + result + "\"");
    }
    for (std::size_t nodes : {1'000ul, 100'000ul, 1'000'000ul, 1'000'001ul}) {
        std::ostringstream c;
        c << "node_id,x_m,y_m,z_m,temperature_C\n";
        for (std::size_t i = 0; i < nodes; ++i) {
            c << i + 1 << ',' << (i % 100) * 0.001 << ',' << (i / 100 % 100) * 0.001 << ',' << (i / 10000) * 0.001 << ','
              << 20.0 + (i % 997) * 0.1 << '\n';
        }
        const std::string text = c.str();
        Stopwatch sw;
        std::string result = "ok";
        try {
            io::parseNodalCsv(text);
        } catch (const std::exception& e) {
            result = std::string("rejected: ") + e.what();
        }
        csv.row("nodal_csv", nodes, static_cast<double>(text.size()) / 1048576.0, sw.elapsedSeconds() * 1000.0, "\"" + result + "\"");
    }
    save("io_size_limits.csv", csv.str());
}

void systemInfo() {
    const auto s = platform::querySystemInfo();
    std::ostringstream o;
    o << "cpu: " << (s.cpuName.empty() ? "unknown" : s.cpuName) << "\nphysical_cores: " << s.physicalCores
      << "\nlogical_cpus: " << s.logicalCpus << "\nram_GiB: " << static_cast<double>(s.totalRamBytes) / (1 << 30)
      << "\nos: " << s.os << "\ncompiler: " << s.compiler << "\nbuild_type: " << s.buildType
      << "\nbuild_flags: " << s.buildFlags << '\n';
    save("system_info.txt", o.str());
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: wb_results <output-dir> [--cases N] [--elements M] [--skip-io]\n";
        return 2;
    }
    outDir = argv[1];
    int cases = 1000, elements = 10'000;
    bool skipIo = false;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--cases" && i + 1 < argc) cases = std::atoi(argv[++i]);
        else if (a == "--elements" && i + 1 < argc) elements = std::atoi(argv[++i]);
        else if (a == "--skip-io") skipIo = true;
    }
    try {
        systemInfo();
        feaStudy();
        backpropStudy();
        benchmarkStudy(cases, elements);
        if (!skipIo) ioSizeStudy();
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
