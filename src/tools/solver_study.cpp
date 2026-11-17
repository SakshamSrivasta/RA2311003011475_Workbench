// wb_solvers: the Round 2 solver study.
//
//   wb_solvers <outdir> [--quick]
//       Runs the experiment matrix and writes CSVs and SVG figures to <outdir>.
//       Every timed configuration runs in a fresh child process
//       (`wb_solvers --one ...`), so its peak memory and timings are its own.
//   wb_solvers --one <geometry> <param> <solver> <omega> [history.csv]
//       Runs one configuration and prints one "ROW k=v;..." line.
//
// Geometries: cube (param = cells per side; manufactured solution, so the
// discretisation error is known), hsA / hsB (param = grid spacing in mm; the
// heat sink without / with the thermal pad and copper spreader).
// Solvers: direct-rcm, direct-natural, jacobi, gs, sor, cg, pcg-jacobi, pcg-ic0.

#include "SvgFigures.h"
#include "SvgPlot.h"

#include "core/common/Errors.h"
#include "core/common/Stopwatch.h"
#include "core/fem3d/Assembler.h"
#include "core/fem3d/Cases.h"
#include "core/fem3d/Postprocess.h"
#include "core/io/CsvWriter.h"
#include "core/io/TextFile.h"
#include "core/platform/SystemInfo.h"
#include "core/solvers/ConjugateGradient.h"
#include "core/solvers/SkylineCholesky.h"
#include "core/solvers/Stationary.h"
#include "core/sparse/Ordering.h"
#include "core/sparse/VectorOps.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace wb;
namespace fs = std::filesystem;

namespace {

// ---- settings shared by every run ------------------------------------------

constexpr double kTolerance = 1e-8;  // relative residual for every iterative solver
constexpr double kReferenceTolerance = 1e-13;
constexpr double kMaxSolveSeconds = 300.0;  // per stationary or CG solve
constexpr std::size_t kDirectCapBytes = std::size_t{3} << 30;

// Fixed solver -> colour (validated categorical order); the same in every figure.
const std::map<std::string, std::string> kColor = {
    {"direct-rcm", "#2a78d6"}, {"gs", "#eb6834"},      {"sor", "#1baf7a"},    {"cg", "#eda100"},
    {"pcg-jacobi", "#e87ba4"}, {"pcg-ic0", "#008300"}, {"jacobi", "#6250d6"}, {"direct-natural", "#e34948"}};
const std::map<std::string, std::string> kLabel = {
    {"direct-rcm", "Skyline Cholesky (RCM)"}, {"direct-natural", "Skyline Cholesky (natural)"},
    {"jacobi", "Jacobi"}, {"gs", "Gauss-Seidel"}, {"sor", "SOR (fitted omega)"}, {"cg", "CG"},
    {"pcg-jacobi", "PCG + Jacobi"}, {"pcg-ic0", "PCG + IC(0)"}};
const std::vector<std::string> kSolverOrder = {"direct-rcm", "direct-natural", "jacobi", "gs", "sor", "cg", "pcg-jacobi", "pcg-ic0"};

fem3d::StudyCase makeCase(const std::string& geometry, double param) {
    if (geometry == "cube") return fem3d::manufacturedCube(static_cast<int>(param));
    if (geometry == "hsA") return fem3d::heatSink(param * 1e-3, false);
    if (geometry == "hsB") return fem3d::heatSink(param * 1e-3, true);
    throw WorkbenchError(ErrorCode::InvalidParameter, "unknown geometry " + geometry);
}

// Characteristic cell size [m], used to fit the SOR optimum.
double cellSize(const std::string& geometry, double param) { return geometry == "cube" ? 1.0 / param : param * 1e-3; }

std::string num(double v) { return io::formatNumber(v); }

using Row = std::map<std::string, std::string>;

double getd(const Row& r, const std::string& k, double fallback = std::nan("")) {
    const auto it = r.find(k);
    if (it == r.end() || it->second.empty()) return fallback;
    try {
        return std::stod(it->second);
    } catch (...) {
        return fallback;
    }
}
std::string gets(const Row& r, const std::string& k) {
    const auto it = r.find(k);
    return it == r.end() ? std::string() : it->second;
}

struct System {
    fem3d::StudyCase c;
    mesh::HexMesh mesh;
    fem3d::FullSystem full;
    fem3d::ReducedSystem reduced;
};

System buildSystem(const std::string& geometry, double param) {
    System s;
    s.c = makeCase(geometry, param);
    s.mesh = fem3d::buildMesh(s.c);
    s.full = fem3d::assemble(s.mesh, s.c.problem);
    s.reduced = fem3d::eliminateDirichlet(s.full);
    return s;
}

// ---- one configuration (child process) --------------------------------------

struct Outcome {
    solvers::SolveReport report;
    std::vector<double> x;
    double icShift = 0.0, icMinPivot = 1.0;
    solvers::SkylinePlan plan;
    bool direct = false;
};

Outcome runSolver(const sparse::CsrMatrix& A, const std::vector<double>& b, const std::string& solver, double omega,
                  bool record) {
    Outcome out;
    const std::size_t n = A.rows();
    out.x.assign(n, 0.0);
    solvers::SolveOptions o;
    o.relativeTolerance = kTolerance;
    o.maxSeconds = kMaxSolveSeconds;
    o.recordHistory = record;
    o.recordCoefficients = record;
    if (solver == "direct-rcm" || solver == "direct-natural") {
        out.direct = true;
        solvers::SkylineOptions so;
        so.ordering = solver == "direct-rcm" ? solvers::Ordering::ReverseCuthillMcKee : solvers::Ordering::Natural;
        so.memoryCapBytes = kDirectCapBytes;
        const Stopwatch t;
        const auto perm = so.ordering == solvers::Ordering::Natural ? sparse::identityPermutation(n)
                                                                    : sparse::reverseCuthillMcKee(A);
        out.plan = solvers::planSkyline(A, perm);
        const double planSeconds = t.elapsedSeconds();
        out.report = solvers::skylineSolve(A, b, out.x, so);
        (void)planSeconds;  // the solver repeats the ordering inside its own setup time
    } else if (solver == "jacobi" || solver == "gs" || solver == "sor") {
        o.trueResidualEvery = 10;
        out.report = solver == "jacobi" ? solvers::jacobi(A, b, out.x, o)
                                        : solvers::sor(A, b, out.x, solver == "gs" ? 1.0 : omega, o);
    } else {
        const Stopwatch s;
        std::unique_ptr<solvers::Preconditioner> M;
        if (solver == "pcg-jacobi") M = std::make_unique<solvers::JacobiPreconditioner>(A);
        if (solver == "pcg-ic0") {
            auto ic = std::make_unique<solvers::IncompleteCholesky>(A, solvers::IcOptions{});
            out.icShift = ic->shift();
            out.icMinPivot = ic->minPivotRatio();
            M = std::move(ic);
        }
        const double setupSeconds = s.elapsedSeconds();
        o.trueResidualEvery = 25;
        out.report = solvers::conjugateGradient(A, b, out.x, o, M.get());
        out.report.setupSeconds = setupSeconds;
    }
    return out;
}

void writeHistory(const std::string& path, const std::string& solver, const solvers::SolveReport& rep) {
    io::CsvWriter csv({"iteration", "relative_residual", "kind"});
    csv.comment("solver=" + solver + "  status=" + solvers::toString(rep.status));
    const bool stationary = solver == "jacobi" || solver == "gs" || solver == "sor";
    if (stationary) {
        // SOR/Jacobi history holds the explicit checks (every 10 sweeps) after the initial value.
        if (!rep.history.empty()) csv.row(0.0, rep.history[0], "true");
        for (const auto& [it, r] : rep.trueResidualHistory) csv.row(static_cast<double>(it), r, "true");
    } else {
        for (std::size_t i = 0; i < rep.history.size(); ++i) csv.row(static_cast<double>(i), rep.history[i], "recursive");
        for (const auto& [it, r] : rep.trueResidualHistory) csv.row(static_cast<double>(it), r, "true");
    }
    io::writeTextFileAtomic(path, csv.str());
}

int runOne(const std::string& geometry, double param, const std::string& solver, double omega,
           const std::string& historyPath) {
    Row row{{"geometry", geometry}, {"param", num(param)}, {"solver", solver}, {"omega", num(omega)}};
    Stopwatch t;
    const System s = buildSystem(geometry, param);
    row["assembly_s"] = num(t.elapsedSeconds());
    const auto& A = s.reduced.A;
    const auto& b = s.reduced.b;
    const std::size_t n = A.rows();
    row["n"] = std::to_string(n);
    row["nnz"] = std::to_string(A.nnz());
    row["matrix_bytes"] = std::to_string(A.bytes());
    row["natural_half_bandwidth"] = std::to_string(A.halfBandwidth());

    // Reference solution for the agreement check (not timed).
    std::vector<double> reference;
    {
        const solvers::IncompleteCholesky ic(A, {});
        solvers::SolveOptions o;
        o.relativeTolerance = kReferenceTolerance;
        solvers::conjugateGradient(A, b, reference, o, &ic);
    }

    // First run records the residual history and CG coefficients. If it took
    // under 2 s it counts as the warm-up and three more runs are timed; the
    // median is reported. Every run starts from x = 0.
    const bool isCg = solver == "cg" || solver.rfind("pcg", 0) == 0;
    const Outcome first = runSolver(A, b, solver, omega, isCg || !historyPath.empty());
    std::vector<double> setup{first.report.setupSeconds}, solve{first.report.solveSeconds};
    Outcome last = first;
    if (first.report.status != solvers::SolveStatus::Refused && setup[0] + solve[0] < 2.0) {
        setup.clear();
        solve.clear();
        for (int r = 0; r < 3; ++r) {
            last = runSolver(A, b, solver, omega, false);
            setup.push_back(last.report.setupSeconds);
            solve.push_back(last.report.solveSeconds);
        }
    }
    const auto median = [](std::vector<double> v) {
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    };
    const auto& rep = last.report;
    row["reps"] = std::to_string(setup.size());
    row["status"] = solvers::toString(rep.status);
    row["detail"] = rep.detail;
    row["iterations"] = std::to_string(rep.iterations);
    row["setup_s"] = num(median(setup));
    row["solve_s"] = num(median(solve));
    row["total_s"] = num(median(setup) + median(solve));
    row["rel_residual"] = num(rep.relativeResidual);
    row["bytes_allocated"] = std::to_string(rep.bytesAllocated);
    row["residual_replacements"] = std::to_string(rep.residualReplacements);
    if (first.direct) {
        row["profile"] = std::to_string(first.plan.profile);
        row["half_bandwidth"] = std::to_string(first.plan.halfBandwidth);
        row["planned_flops"] = num(first.plan.factorFlops);
        row["planned_bytes"] = num(first.plan.factorBytes);
    }
    if (solver == "pcg-ic0") {
        row["ic_shift"] = num(first.icShift);
        row["ic_min_pivot_ratio"] = num(first.icMinPivot);
    }
    if (!first.report.alphas.empty()) {
        const auto est = solvers::lanczosExtremeEigenvalues(first.report.alphas, first.report.betas);
        row["lambda_min"] = num(est.lambdaMin);
        row["lambda_max"] = num(est.lambdaMax);
        row["kappa_est"] = num(est.condition());
    }
    if (rep.status != solvers::SolveStatus::Refused && rep.status != solvers::SolveStatus::Breakdown) {
        double diff = 0.0;
        for (std::size_t i = 0; i < n; ++i) diff = std::max(diff, std::abs(last.x[i] - reference[i]));
        row["max_abs_diff_vs_reference_C"] = num(diff);
        const auto T = fem3d::expandSolution(s.reduced, last.x);
        row["T_max_C"] = num(*std::max_element(T.begin(), T.end()));
        if (s.c.exact) {
            const auto err = fem3d::errorNorms(s.mesh, T, s.c.exact);
            row["err_rms_C"] = num(err.rms_C);
            row["err_max_nodal_C"] = num(err.maxNodal_C);
        }
    }
    row["peak_rss_bytes"] = std::to_string(platform::peakResidentBytes());
    if (!historyPath.empty()) writeHistory(historyPath, solver, first.report);

    std::cout << "ROW";
    for (const auto& [k, v] : row) {
        std::string clean = v;
        std::replace(clean.begin(), clean.end(), ';', ',');
        std::replace(clean.begin(), clean.end(), '\n', ' ');
        std::cout << ' ' << k << '=' << clean << ';';
    }
    std::cout << std::endl;
    return 0;
}

Row parseRow(const std::string& text) {
    Row row;
    const auto pos = text.find("ROW ");
    if (pos == std::string::npos) return row;
    std::string line = text.substr(pos + 4);
    line = line.substr(0, line.find('\n'));
    std::stringstream ss(line);
    std::string item;
    while (std::getline(ss, item, ';')) {
        const auto eq = item.find('=');
        if (eq == std::string::npos) continue;
        std::string key = item.substr(0, eq);
        key.erase(0, key.find_first_not_of(' '));
        row[key] = item.substr(eq + 1);
    }
    return row;
}

// ---- parent: experiment plan ---------------------------------------------

struct Plan {
    std::vector<double> cubeLevels, hsLevels;
    std::vector<double> cubeOmegaLevels, hsOmegaLevels;
};

bool solverRunsAt(const std::string& solver, const std::string& geometry, double param, bool quick) {
    if (quick) return true;
    const bool cube = geometry == "cube";
    if (solver == "direct-natural") return cube ? param <= 32 : param >= 1.0;
    if (solver == "jacobi") return cube ? param <= 32 : param >= 1.0;
    if (solver == "gs") return cube ? param <= 64 : param >= 0.5;
    return true;
}

std::string quoteArg(const std::string& s) { return "\"" + s + "\""; }

Row spawn(const std::string& self, const fs::path& scratch, const std::string& geometry, double param,
          const std::string& solver, double omega, const std::string& historyPath) {
    const fs::path out = scratch / "child.txt";
    std::string cmd = quoteArg(self) + " --one " + geometry + " " + num(param) + " " + solver + " " + num(omega);
    if (!historyPath.empty()) cmd += " " + quoteArg(historyPath);
    cmd += " > " + quoteArg(out.string()) + " 2>&1";
#if defined(_WIN32)
    cmd = "\"" + cmd + "\"";  // cmd.exe strips one outer pair of quotes
#endif
    const int rc = std::system(cmd.c_str());
    std::string text;
    try {
        text = io::readTextFile(out.string());
    } catch (...) {
    }
    Row row = parseRow(text);
    if (row.empty()) {
        row = {{"geometry", geometry}, {"param", num(param)}, {"solver", solver}, {"status", "child-failed"},
               {"detail", "exit " + std::to_string(rc) + ": " + text.substr(0, 200)}};
    }
    return row;
}

// ---- in-process experiments ------------------------------------------------

struct OmegaPoint {
    double omega;
    std::size_t sweeps;
    std::string status;
};

std::vector<OmegaPoint> omegaSweep(const sparse::CsrMatrix& A, const std::vector<double>& b, const std::vector<double>& omegas,
                                   std::size_t cap) {
    std::vector<OmegaPoint> out;
    for (double w : omegas) {
        std::vector<double> x;
        solvers::SolveOptions o;
        o.relativeTolerance = kTolerance;
        o.trueResidualEvery = 1;
        o.maxIterations = cap;
        const auto rep = solvers::sor(A, b, x, w, o);
        out.push_back({w, rep.iterations, solvers::toString(rep.status)});
    }
    return out;
}

// Golden-section search for the omega with the fewest sweeps (unimodal in practice).
double bestOmega(const sparse::CsrMatrix& A, const std::vector<double>& b, std::size_t cap, std::vector<OmegaPoint>& log) {
    auto cost = [&](double w) {
        std::vector<double> x;
        solvers::SolveOptions o;
        o.relativeTolerance = kTolerance;
        o.trueResidualEvery = 1;
        o.maxIterations = cap;
        const auto rep = solvers::sor(A, b, x, w, o);
        log.push_back({w, rep.iterations, solvers::toString(rep.status)});
        return rep.converged() ? static_cast<double>(rep.iterations) : 1e30;
    };
    double lo = 1.0, hi = 1.999;
    const double g = 0.6180339887498949;
    double x1 = hi - g * (hi - lo), x2 = lo + g * (hi - lo);
    double f1 = cost(x1), f2 = cost(x2);
    while (hi - lo > 0.002) {
        if (f1 <= f2) {
            hi = x2;
            x2 = x1;
            f2 = f1;
            x1 = hi - g * (hi - lo);
            f1 = cost(x1);
        } else {
            lo = x1;
            x1 = x2;
            f1 = f2;
            x2 = lo + g * (hi - lo);
            f2 = cost(x2);
        }
    }
    return f1 <= f2 ? x1 : x2;
}

double slopeLogLog(const std::vector<double>& x, const std::vector<double>& y) {
    if (x.size() < 2) return std::nan("");
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    const double m = static_cast<double>(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        const double lx = std::log(x[i]), ly = std::log(y[i]);
        sx += lx;
        sy += ly;
        sxx += lx * lx;
        sxy += lx * ly;
    }
    return (m * sxy - sx * sy) / (m * sxx - sx * sx);
}

std::string fmt2(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", v);
    return buf;
}

void writeSvg(const fs::path& path, const std::string& svg) { io::writeTextFileAtomic(path.string(), svg); }

std::string geometryTitle(const std::string& g) {
    if (g == "cube") return "Unit cube, manufactured solution";
    if (g == "hsA") return "Heat sink A (aluminium)";
    return "Heat sink B (thermal pad + copper spreader)";
}

// ---- figures from the run table -------------------------------------------

void scalingFigures(const fs::path& dir, const std::vector<Row>& rows, const std::string& g) {
    using tools::PlotSpec;
    using tools::Series;
    PlotSpec time, mem, its;
    time.title = geometryTitle(g) + ": time to solution";
    time.subtitle = "Setup + solve, median of runs; relative residual 1e-8. Dashed: skyline cost predicted from its plan (run refused).";
    time.xLabel = "unknowns n";
    time.yLabel = "seconds";
    time.logX = time.logY = true;
    mem = time;
    mem.title = geometryTitle(g) + ": memory beyond the matrix";
    mem.subtitle = "Work vectors, preconditioner and factor (counted). Dashed: predicted factor size of refused skyline runs.";
    mem.yLabel = "bytes";
    its = time;
    its.title = geometryTitle(g) + ": iterations to 1e-8";
    its.subtitle = "Sweeps for Jacobi / Gauss-Seidel / SOR, iterations for CG. Slopes from a log-log fit.";
    its.yLabel = "iterations";

    // Flop rate of the skyline factorisation, from the largest completed run.
    double flopRate = 0.0;
    for (const auto& r : rows) {
        if (gets(r, "geometry") == g && gets(r, "solver") == "direct-rcm" && gets(r, "status") == "converged") {
            const double f = getd(r, "planned_flops"), s = getd(r, "setup_s");
            if (s > 0.05) flopRate = f / s;
        }
    }
    for (const auto& solver : kSolverOrder) {
        Series st, sm, si, pt, pm;
        for (const auto& r : rows) {
            if (gets(r, "geometry") != g || gets(r, "solver") != solver) continue;
            const double n = getd(r, "n");
            if (gets(r, "status") == "converged") {
                st.x.push_back(n);
                st.y.push_back(getd(r, "total_s"));
                sm.x.push_back(n);
                sm.y.push_back(getd(r, "bytes_allocated"));
                if (solver.rfind("direct", 0) != 0) {
                    si.x.push_back(n);
                    si.y.push_back(getd(r, "iterations"));
                }
            } else if (gets(r, "status") == "refused" && flopRate > 0) {
                pt.x.push_back(n);
                pt.y.push_back(getd(r, "planned_flops") / flopRate);
                pm.x.push_back(n);
                pm.y.push_back(getd(r, "planned_bytes"));
            }
        }
        if (st.x.empty()) continue;
        const std::string color = kColor.at(solver), label = kLabel.at(solver);
        for (Series* s : {&st, &sm, &si}) {
            s->color = color;
            s->markers = true;
        }
        st.label = label + "  (slope " + fmt2(slopeLogLog(st.x, st.y)) + ")";
        sm.label = label + "  (slope " + fmt2(slopeLogLog(sm.x, sm.y)) + ")";
        si.label = label + "  (slope " + fmt2(slopeLogLog(si.x, si.y)) + ")";
        time.series.push_back(st);
        mem.series.push_back(sm);
        if (!si.x.empty()) its.series.push_back(si);
        if (!pt.x.empty()) {
            // Join the prediction to the last measured point so the line continues.
            pt.x.insert(pt.x.begin(), st.x.back());
            pt.y.insert(pt.y.begin(), st.y.back());
            pm.x.insert(pm.x.begin(), sm.x.back());
            pm.y.insert(pm.y.begin(), sm.y.back());
            for (Series* s : {&pt, &pm}) {
                s->color = color;
                s->dashed = true;
                s->label = label + " (predicted, refused)";
            }
            time.series.push_back(pt);
            mem.series.push_back(pm);
        }
    }
    time.legendTopLeft = mem.legendTopLeft = its.legendTopLeft = true;
    time.height = mem.height = its.height = 520;
    writeSvg(dir / ("time_vs_n_" + g + ".svg"), tools::renderSvg(time));
    writeSvg(dir / ("memory_vs_n_" + g + ".svg"), tools::renderSvg(mem));
    writeSvg(dir / ("iterations_vs_n_" + g + ".svg"), tools::renderSvg(its));
}

void historyFigure(const fs::path& dir, const std::string& g, double param, const std::vector<std::string>& solvers,
                   const std::string& title) {
    tools::PlotSpec p;
    p.title = title;
    p.subtitle = "True relative residual ||b - Ax|| / ||b|| (CG: recursive residual). Same system, same start x = 0.";
    p.xLabel = "iteration (sweep for Jacobi / Gauss-Seidel / SOR)";
    p.yLabel = "relative residual";
    p.logY = true;
    p.logX = true;
    p.height = 500;
    for (const auto& solver : solvers) {
        const fs::path file = dir / "residual_histories" / (g + "_" + num(param) + "_" + solver + ".csv");
        if (!fs::exists(file)) continue;
        std::stringstream ss(io::readTextFile(file.string()));
        std::string line;
        tools::Series s;
        const bool cg = solver == "cg" || solver.rfind("pcg", 0) == 0;
        while (std::getline(ss, line)) {
            if (line.empty() || line[0] == '#' || line.rfind("iteration", 0) == 0) continue;
            std::stringstream ls(line);
            std::string a, b, kind;
            std::getline(ls, a, ',');
            std::getline(ls, b, ',');
            std::getline(ls, kind, ',');
            if (cg && kind != "recursive") continue;
            const double it = std::stod(a) + 1.0;  // log axis: plot iteration + 1
            s.x.push_back(it);
            s.y.push_back(std::max(std::stod(b), 1e-16));
        }
        if (s.x.empty()) continue;
        s.label = kLabel.at(solver);
        s.color = kColor.at(solver);
        p.series.push_back(s);
    }
    writeSvg(dir / ("residual_history_" + g + "_" + num(param) + ".svg"), tools::renderSvg(p));
}

// Temperature slices through the heat sink and Round 1-format exports.
void heatSinkPictures(const fs::path& dir, bool contrast, double spacing_mm) {
    const std::string g = contrast ? "hsB" : "hsA";
    const System s = buildSystem(g, spacing_mm);
    std::vector<double> x;
    const solvers::IncompleteCholesky ic(s.reduced.A, {});
    solvers::SolveOptions o;
    o.relativeTolerance = 1e-12;
    solvers::conjugateGradient(s.reduced.A, s.reduced.b, x, o, &ic);
    const auto T = fem3d::expandSolution(s.reduced, x);
    const auto& m = s.mesh;
    const auto hb = fem3d::heatBalance(m, s.c.problem, s.full, T);

    // Slice 1: vertical plane y = 20 mm through the chip centre (x-z).
    // Slice 2: the board side z = 0 (x-y).
    const auto nodeT = [&](int i, int j, int k) {
        const std::size_t id = m.gridNodeToNode[m.gridNodeIndex(i, j, k)];
        return id == mesh::kNoNode ? std::nan("") : T[id];
    };
    const auto lineIndex = [&](int axis, double v) {
        const auto& L = m.grid.lines[axis];
        return static_cast<int>(std::min_element(L.begin(), L.end(), [&](double a, double c) { return std::abs(a - v) < std::abs(c - v); }) - L.begin());
    };
    const double tMax = *std::max_element(T.begin(), T.end()), tMin = *std::min_element(T.begin(), T.end());
    char sub[256];
    std::snprintf(sub, sizeof(sub), "%.2g mm grid, %zu unknowns. 50 W chip, h = 50 W/(m^2 K), 25 C air. Whole part %.1f to %.1f C. Heat balance %.0e W.",
                  spacing_mm, s.reduced.A.rows(), tMin, tMax, hb.imbalance_W);
    {
        tools::HeatmapSpec h;
        h.title = std::string("Temperature on the mid-plane y = 20 mm (heat sink ") + (contrast ? "B" : "A") + ")";
        h.subtitle = sub;
        h.uLabel = "x [mm]";
        h.vLabel = "z [mm]";
        h.unit = "C";
        h.scale = 1e3;
        const int j = lineIndex(1, 0.020);
        for (int k = 0; k < m.grid.cells(2); ++k) {
            for (int i = 0; i < m.grid.cells(0); ++i) {
                const double v[4] = {nodeT(i, j, k), nodeT(i + 1, j, k), nodeT(i, j, k + 1), nodeT(i + 1, j, k + 1)};
                if (std::isnan(v[0]) || std::isnan(v[1]) || std::isnan(v[2]) || std::isnan(v[3])) continue;
                // Only draw where a solid cell touches the plane.
                const bool solid = m.cellMaterial[m.gridCellIndex(i, std::min(j, m.grid.cells(1) - 1), k)] != mesh::kVoid ||
                                   (j > 0 && m.cellMaterial[m.gridCellIndex(i, j - 1, k)] != mesh::kVoid);
                if (!solid) continue;
                h.cells.push_back({m.grid.lines[0][i], m.grid.lines[0][i + 1], m.grid.lines[2][k], m.grid.lines[2][k + 1],
                                   0.25 * (v[0] + v[1] + v[2] + v[3])});
            }
        }
        writeSvg(dir / ("temperature_slice_xz_" + g + ".svg"), tools::renderHeatmap(h));
    }
    {
        tools::HeatmapSpec h;
        h.title = std::string("Temperature on the board side z = 0 (heat sink ") + (contrast ? "B" : "A") + ")";
        h.subtitle = sub;
        h.uLabel = "x [mm]";
        h.vLabel = "y [mm]";
        h.unit = "C";
        h.scale = 1e3;
        for (int j = 0; j < m.grid.cells(1); ++j) {
            for (int i = 0; i < m.grid.cells(0); ++i) {
                const double v[4] = {nodeT(i, j, 0), nodeT(i + 1, j, 0), nodeT(i, j + 1, 0), nodeT(i + 1, j + 1, 0)};
                if (std::isnan(v[0]) || std::isnan(v[1]) || std::isnan(v[2]) || std::isnan(v[3])) continue;
                h.cells.push_back({m.grid.lines[0][i], m.grid.lines[0][i + 1], m.grid.lines[1][j], m.grid.lines[1][j + 1],
                                   0.25 * (v[0] + v[1] + v[2] + v[3])});
            }
        }
        writeSvg(dir / ("temperature_board_xy_" + g + ".svg"), tools::renderHeatmap(h));
    }

    // Round 1 formats, so the Round 1 viewer renders the computed field in 3D.
    io::CsvWriter csv({"node_id", "x_m", "y_m", "z_m", "temperature_C"});
    csv.comment(std::string("COMPUTED by wb_solvers (3D FEM, PCG + IC(0), relative residual 1e-12), heat sink ") + (contrast ? "B" : "A") +
                ", " + num(spacing_mm) + " mm grid. Not an ANSYS export.");
    for (std::size_t i = 0; i < m.nodeCount(); ++i) {
        const auto p = m.nodePosition(i);
        csv.row(static_cast<double>(i + 1), p[0], p[1], p[2], T[i]);
    }
    io::writeTextFileAtomic((dir / ("heatsink_" + g + "_nodal.csv")).string(), csv.str());

    std::ostringstream stl;
    stl << "solid heatsink_" << g << "_surface_m\n";
    for (const auto& f : m.faces) {
        const auto P = [&](int q) { return m.nodePosition(f.nodes[static_cast<std::size_t>(q)]); };
        double nrm[3] = {0, 0, 0};
        nrm[f.axis] = f.side ? 1.0 : -1.0;
        // Face-local order: 0 (u0,v0), 1 (u1,v0), 2 (u0,v1), 3 (u1,v1); split along 0-3.
        const int tri[2][3] = {{0, 1, 3}, {0, 3, 2}};
        for (const auto& t : tri) {
            stl << "  facet normal " << nrm[0] << ' ' << nrm[1] << ' ' << nrm[2] << "\n    outer loop\n";
            for (int q : t) {
                const auto p = P(q);
                stl << "      vertex " << p[0] << ' ' << p[1] << ' ' << p[2] << '\n';
            }
            stl << "    endloop\n  endfacet\n";
        }
    }
    stl << "endsolid heatsink_" << g << "_surface_m\n";
    io::writeTextFileAtomic((dir / ("heatsink_" + g + "_surface_m.stl")).string(), stl.str());
}

void spyFigures(const fs::path& dir) {
    const System s = buildSystem("hsA", 2.0);
    const auto& A = s.reduced.A;
    for (auto ordering : {solvers::Ordering::Natural, solvers::Ordering::ReverseCuthillMcKee}) {
        const bool rcm = ordering == solvers::Ordering::ReverseCuthillMcKee;
        const auto perm = rcm ? sparse::reverseCuthillMcKee(A) : sparse::identityPermutation(A.rows());
        const auto B = sparse::permuteSymmetric(A, perm);
        const auto plan = solvers::planSkyline(A, perm);
        tools::SpySpec spec;
        spec.n = B.rows();
        for (std::size_t r = 0; r < B.rows(); ++r)
            for (std::size_t p = B.rowPtr[r]; p < B.rowPtr[r + 1]; ++p) spec.entries.emplace_back(r, B.col[p]);
        spec.envelopeFirst.resize(B.rows());
        for (std::size_t r = 0; r < B.rows(); ++r) spec.envelopeFirst[r] = std::min<std::size_t>(r, B.col[B.rowPtr[r]]);
        char sub[200];
        std::snprintf(sub, sizeof(sub), "n = %zu, nnz = %zu. Shaded: skyline envelope, %.2f M entries (%.0fx nnz/2)", B.rows(), B.nnz(),
                      static_cast<double>(plan.profile) / 1e6, static_cast<double>(plan.profile) / (0.5 * static_cast<double>(B.nnz())));
        spec.title = std::string("Heat sink A, 2 mm: ") + (rcm ? "RCM ordering" : "natural ordering");
        spec.subtitle = sub;
        spec.size = 420;
        writeSvg(dir / (std::string("spy_hsA_2mm_") + (rcm ? "rcm" : "natural") + ".svg"), tools::renderSpy(spec));
    }
}

// Plain CG run far past convergence: the recursive residual keeps falling,
// the true residual stops at the round-off floor.
void driftExperiment(const fs::path& dir) {
    const System s = buildSystem("hsB", 1.0);
    std::vector<double> x;
    solvers::SolveOptions o;
    o.relativeTolerance = 0.0;  // never stop on the residual
    o.maxIterations = 4000;
    o.trueResidualEvery = 10;
    o.recordHistory = true;
    const auto rep = solvers::conjugateGradient(s.reduced.A, s.reduced.b, x, o);
    io::CsvWriter csv({"iteration", "recursive_residual", "true_residual"});
    csv.comment("Plain CG on heat sink B, 1 mm grid, never stopped: recursive vs explicit residual.");
    std::map<std::size_t, double> trueAt(rep.trueResidualHistory.begin(), rep.trueResidualHistory.end());
    tools::Series rec, tru;
    for (std::size_t i = 0; i < rep.history.size(); ++i) {
        const auto it = trueAt.find(i);
        if (it != trueAt.end()) {
            csv.row(static_cast<double>(i), rep.history[i], it->second);
            tru.x.push_back(static_cast<double>(i));
            tru.y.push_back(it->second);
        }
        if (i % 5 == 0) {
            rec.x.push_back(static_cast<double>(i));
            rec.y.push_back(std::max(rep.history[i], 1e-30));
        }
    }
    io::writeTextFileAtomic((dir / "cg_residual_drift.csv").string(), csv.str());
    tools::PlotSpec p;
    p.title = "Plain CG, heat sink B (1 mm): the recursive residual lies";
    p.subtitle = "Run without a stopping test. The updated residual keeps falling; the true b - Ax stalls at the round-off floor.";
    p.xLabel = "iteration";
    p.yLabel = "relative residual";
    p.logY = true;
    rec.label = "recursive residual r_k";
    rec.color = "#2a78d6";
    tru.label = "true residual b - A x_k";
    tru.color = "#eb6834";
    tru.dashed = true;
    p.series = {rec, tru};
    writeSvg(dir / "cg_residual_drift.svg", tools::renderSvg(p));
}

// Dirichlet by penalty: K_ii += P, b_i += P T_i on the fixed nodes.
void penaltyExperiment(const fs::path& dir) {
    const System s = buildSystem("cube", 16);
    const auto& K = s.full.K;
    double maxDiag = 0.0;
    for (double d : K.diagonal()) maxDiag = std::max(maxDiag, d);
    io::CsvWriter csv({"method", "penalty_over_max_diag", "n", "cg_iterations", "status", "kappa_est", "max_error_vs_elimination_C"});
    csv.comment("Unit cube, manufactured solution, 16 cells per side. Plain CG to relative residual 1e-8.");
    csv.comment("kappa_est: Lanczos estimate from a separate 300-iteration CG run (elimination: from the solve itself).");
    csv.comment("A large penalty dominates ||b||, so the relative-residual test is met while the interior is still wrong.");
    std::vector<double> xElim;
    solvers::SolveOptions o;
    o.relativeTolerance = kTolerance;
    o.recordCoefficients = true;
    o.maxIterations = 200000;
    const auto elim = solvers::conjugateGradient(s.reduced.A, s.reduced.b, xElim, o);
    const auto Telim = fem3d::expandSolution(s.reduced, xElim);
    const auto eElim = solvers::lanczosExtremeEigenvalues(elim.alphas, elim.betas);
    csv.writeRow({"elimination", "", std::to_string(s.reduced.A.rows()), std::to_string(elim.iterations), solvers::toString(elim.status),
                  num(eElim.condition()), "0"});
    tools::Series kap, its;
    for (double factor : {1e2, 1e4, 1e6, 1e8, 1e10, 1e12}) {
        sparse::CsrMatrix A = K;
        std::vector<double> b = s.full.b;
        const double P = factor * maxDiag;
        for (std::size_t i = 0; i < s.full.dirichletNodes.size(); ++i) {
            const std::size_t node = s.full.dirichletNodes[i];
            A.add(node, static_cast<std::uint32_t>(node), P);
            b[node] += P * s.full.dirichletValues[i];
        }
        std::vector<double> x;
        solvers::SolveOptions plainStop = o;
        plainStop.recordCoefficients = false;
        const auto rep = solvers::conjugateGradient(A, b, x, plainStop);
        // The condition number comes from a separate fixed-length run: with a
        // big penalty the convergence run above may stop after one iteration,
        // which says nothing about the spectrum.
        std::vector<double> scratch;
        solvers::SolveOptions probe;
        probe.relativeTolerance = 0.0;
        probe.maxIterations = 300;
        probe.trueResidualEvery = 0;
        probe.recordCoefficients = true;
        probe.maxResidualReplacements = 0;
        const auto spectral = solvers::conjugateGradient(A, b, scratch, probe);
        const auto e = solvers::lanczosExtremeEigenvalues(spectral.alphas, spectral.betas);
        double err = 0.0;
        for (std::size_t i = 0; i < x.size(); ++i) err = std::max(err, std::abs(x[i] - Telim[i]));
        csv.writeRow({"penalty", num(factor), std::to_string(A.rows()), std::to_string(rep.iterations), solvers::toString(rep.status),
                      num(e.condition()), num(err)});
        kap.x.push_back(factor);
        kap.y.push_back(e.condition());
        its.x.push_back(factor);
        its.y.push_back(static_cast<double>(rep.iterations));
    }
    io::writeTextFileAtomic((dir / "dirichlet_penalty.csv").string(), csv.str());
    tools::PlotSpec p;
    p.title = "Prescribed temperatures by penalty: condition number";
    p.subtitle = "Cube, 16 cells. Lanczos estimate (300 CG steps). Dashed: symmetric elimination, which the study uses.";
    p.xLabel = "penalty / largest diagonal entry";
    p.yLabel = "estimated condition number";
    p.logX = p.logY = true;
    kap.label = "penalty method";
    kap.color = "#eb6834";
    kap.markers = true;
    tools::Series ref;
    ref.label = "elimination";
    ref.color = "#2a78d6";
    ref.dashed = true;
    ref.x = {kap.x.front(), kap.x.back()};
    ref.y = {eElim.condition(), eElim.condition()};
    p.series = {kap, ref};
    p.legendTopLeft = true;
    writeSvg(dir / "dirichlet_penalty_kappa.svg", tools::renderSvg(p));
}

void sorDivergenceExperiment(const fs::path& dir) {
    const System s = buildSystem("cube", 16);
    tools::PlotSpec p;
    p.title = "SOR on the cube (16 cells): omega below and above 2";
    p.subtitle = "Ostrowski-Reich: SOR on an SPD matrix converges exactly for 0 < omega < 2.";
    p.xLabel = "sweep";
    p.yLabel = "relative residual";
    p.logY = true;
    io::CsvWriter csv({"omega", "sweep", "relative_residual"});
    const char* colors[] = {"#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4"};
    int c = 0;
    for (double w : {1.0, 1.6, 1.9, 2.0, 2.05}) {
        std::vector<double> x;
        solvers::SolveOptions o;
        o.relativeTolerance = 1e-12;
        o.trueResidualEvery = 1;
        o.maxIterations = 600;
        o.recordHistory = true;
        const auto rep = solvers::sor(s.reduced.A, s.reduced.b, x, w, o);
        tools::Series ser;
        for (std::size_t i = 0; i < rep.history.size(); ++i) {
            csv.row(w, static_cast<double>(i), rep.history[i]);
            ser.x.push_back(static_cast<double>(i));
            ser.y.push_back(std::min(rep.history[i], 1e8));
        }
        ser.label = "omega = " + num(w) + " (" + solvers::toString(rep.status) + ")";
        ser.color = colors[c++];
        ser.dashed = w >= 2.0;
        p.series.push_back(ser);
    }
    io::writeTextFileAtomic((dir / "sor_omega_range.csv").string(), csv.str());
    writeSvg(dir / "sor_omega_range.svg", tools::renderSvg(p));
}

// How close IC(0) gets to breaking down on stretched cells and high contrast.
void icRobustnessExperiment(const fs::path& dir) {
    io::CsvWriter csv({"case", "aspect_ratio", "n", "ic_ok_without_shift", "min_pivot_ratio", "shift_needed", "pcg_ic0_iterations",
                       "pcg_jacobi_iterations", "cg_iterations"});
    csv.comment("min_pivot_ratio = min_i L_ii^2 / a_ii (1 = untouched diagonal, 0 = breakdown).");
    const auto run = [&](const std::string& name, double aspect, const sparse::CsrMatrix& A, const std::vector<double>& b) {
        solvers::IcOptions noShift;
        noShift.allowShift = false;
        const solvers::IncompleteCholesky raw(A, noShift), ic(A, {});
        const solvers::JacobiPreconditioner jac(A);
        solvers::SolveOptions o;
        o.relativeTolerance = kTolerance;
        o.maxIterations = 100000;
        std::vector<double> x1, x2, x3;
        const auto r1 = solvers::conjugateGradient(A, b, x1, o, &ic);
        const auto r2 = solvers::conjugateGradient(A, b, x2, o, &jac);
        const auto r3 = solvers::conjugateGradient(A, b, x3, o);
        csv.writeRow({name, num(aspect), std::to_string(A.rows()), raw.ok() ? "yes" : "no", num(ic.minPivotRatio()), num(ic.shift()),
                      std::to_string(r1.iterations), std::to_string(r2.iterations), std::to_string(r3.iterations)});
    };
    mesh::BoxGeometry cube;
    cube.boxes = {mesh::makeBox(0, 0, 0, 1, 1, 1)};
    for (double r : {1.0, 4.0, 16.0, 64.0}) {
        const auto c = fem3d::manufacturedOn("flat", cube, {1.0 / 12, 1.0 / 12, 1.0 / (12 * r)}, 10.0);
        auto m = fem3d::buildMesh(c);
        const auto red = fem3d::eliminateDirichlet(fem3d::assemble(m, c.problem));
        run("cube, flat cells (z refined)", r, red.A, red.b);
    }
    for (double r : {4.0, 16.0}) {
        const auto c = fem3d::manufacturedOn("long", cube, {1.0 / (8 * r), 1.0 / (8 * r), 1.0 / 8}, 10.0);
        auto m = fem3d::buildMesh(c);
        const auto red = fem3d::eliminateDirichlet(fem3d::assemble(m, c.problem));
        run("cube, long cells (x,y refined)", r, red.A, red.b);
    }
    for (double contrast : {1.0, 1e2, 1e4, 1e6}) {
        auto c = fem3d::heatSink(2e-3, true);
        c.problem.materials[1].conductivity_W_mK = 200.0 / contrast;
        c.spacing_m[2] = 0.5e-3;
        auto m = fem3d::buildMesh(c);
        const auto red = fem3d::eliminateDirichlet(fem3d::assemble(m, c.problem));
        run("heat sink B, pad k = 200/" + num(contrast) + ", 2 x 2 x 0.5 mm cells", 4.0, red.A, red.b);
    }
    io::writeTextFileAtomic((dir / "ic0_robustness.csv").string(), csv.str());
}

int runStudy(const std::string& self, const fs::path& dir, bool quick) {
    fs::create_directories(dir / "residual_histories");
    const fs::path scratch = dir / ".tmp";
    fs::create_directories(scratch);
    const auto info = platform::querySystemInfo();
    {
        std::ostringstream m;
        m << "cpu=" << info.cpuName << "\nphysical_cores=" << info.physicalCores << "\nlogical_cpus=" << info.logicalCpus
          << "\nram_GiB=" << static_cast<double>(info.totalRamBytes) / (1024.0 * 1024 * 1024) << "\nos=" << info.os
          << "\ncompiler=" << info.compiler << "\nbuild=" << info.buildType << "\nflags=" << info.buildFlags
          << "\nsolver_threads=1 (all solvers single-threaded)\ntolerance=" << kTolerance << "\nmax_solve_seconds=" << kMaxSolveSeconds
          << "\ndirect_memory_cap_GiB=" << static_cast<double>(kDirectCapBytes) / (1024.0 * 1024 * 1024) << '\n';
        io::writeTextFileAtomic((dir / "machine.txt").string(), m.str());
    }

    Plan plan;
    plan.cubeLevels = quick ? std::vector<double>{8, 16} : std::vector<double>{8, 16, 24, 32, 48, 64, 96, 128};
    plan.hsLevels = quick ? std::vector<double>{2} : std::vector<double>{2, 1, 0.5, 0.25};
    plan.cubeOmegaLevels = quick ? std::vector<double>{8, 12} : std::vector<double>{8, 12, 16, 24, 32};
    plan.hsOmegaLevels = quick ? std::vector<double>{4, 3} : std::vector<double>{4, 3, 2};

    // 1. Optimal SOR omega: golden-section search on small levels, then fit
    //    2 - omega* = c h (the model-problem law) and use it on every level.
    std::map<std::string, double> fitC;
    {
        io::CsvWriter sweep({"geometry", "param", "h_m", "omega", "sweeps", "status"});
        io::CsvWriter fit({"geometry", "param", "h_m", "omega_best", "two_minus_omega_over_h", "model_omega_opt"});
        for (const std::string g : {"cube", "hsA", "hsB"}) {
            const auto& levels = g == "cube" ? plan.cubeOmegaLevels : plan.hsOmegaLevels;
            double sumC = 0.0;
            for (double param : levels) {
                const System s = buildSystem(g, param);
                std::vector<OmegaPoint> log;
                const double best = bestOmega(s.reduced.A, s.reduced.b, 200000, log);
                const double h = cellSize(g, param);
                for (const auto& pt : log) sweep.row(g, param, h, pt.omega, static_cast<double>(pt.sweeps), pt.status);
                fit.row(g, param, h, best, (2.0 - best) / h,
                        g == "cube" ? solvers::modelProblemOptimalOmega(static_cast<int>(param)) : std::nan(""));
                sumC += (2.0 - best) / h;
                std::cerr << "omega* " << g << " " << param << " -> " << best << '\n';
            }
            fitC[g] = sumC / static_cast<double>(levels.size());
        }
        io::writeTextFileAtomic((dir / "sor_omega_search.csv").string(), sweep.str());
        io::writeTextFileAtomic((dir / "sor_omega_fit.csv").string(), fit.str());

        // Sweeps-vs-omega curves on a few cube levels for the figure.
        tools::PlotSpec p;
        p.title = "SOR sweeps to 1e-8 against omega (unit cube)";
        p.subtitle = "The minimum moves towards omega = 2 as the mesh is refined (2 - omega* ~ c h).";
        p.xLabel = "omega";
        p.yLabel = "sweeps";
        p.logY = true;
        const char* ramp[] = {"#86b6ef", "#3987e5", "#1c5cab", "#0d366b"};
        int k = 0;
        for (double N : quick ? std::vector<double>{8, 12} : std::vector<double>{8, 16, 24, 32}) {
            const System s = buildSystem("cube", N);
            std::vector<double> omegas;
            for (double w = 1.0; w < 1.995; w += 0.025) omegas.push_back(w);
            const auto pts = omegaSweep(s.reduced.A, s.reduced.b, omegas, 100000);
            tools::Series ser;
            for (const auto& pt : pts) {
                ser.x.push_back(pt.omega);
                ser.y.push_back(static_cast<double>(pt.sweeps));
            }
            ser.label = std::to_string(static_cast<int>(N)) + " cells per side";
            ser.color = ramp[k++ % 4];
            p.series.push_back(ser);
        }
        p.legendTopLeft = true;
        writeSvg(dir / "sor_sweeps_vs_omega.svg", tools::renderSvg(p));
    }

    // 2. The timed matrix of runs, each in a fresh process.
    std::vector<Row> rows;
    const auto historyAt = [&](const std::string& g, double param) {
        return (g == "cube" && param == (quick ? 16 : 32)) || (g == "hsB" && param == (quick ? 2 : 1)) ||
               (g == "hsA" && param == (quick ? 2 : 1));
    };
    for (const std::string g : {"cube", "hsA", "hsB"}) {
        for (double param : g == "cube" ? plan.cubeLevels : plan.hsLevels) {
            const double omega = std::min(1.999, 2.0 - fitC[g] * cellSize(g, param));
            for (const auto& solver : kSolverOrder) {
                if (!solverRunsAt(solver, g, param, quick)) continue;
                const std::string hist =
                    historyAt(g, param) ? (dir / "residual_histories" / (g + "_" + num(param) + "_" + solver + ".csv")).string() : "";
                const Stopwatch t;
                Row r = spawn(self, scratch, g, param, solver, omega, hist);
                std::cerr << g << ' ' << param << ' ' << solver << " n=" << gets(r, "n") << ' ' << gets(r, "status")
                          << " it=" << gets(r, "iterations") << " t=" << gets(r, "total_s") << "s (wall " << t.elapsedSeconds() << " s)\n";
                rows.push_back(r);
            }
        }
    }
    const std::vector<std::string> cols = {
        "geometry", "param", "solver", "status", "n", "nnz", "omega", "iterations", "reps", "assembly_s", "setup_s", "solve_s",
        "total_s", "rel_residual", "max_abs_diff_vs_reference_C", "err_rms_C", "err_max_nodal_C", "T_max_C", "bytes_allocated",
        "matrix_bytes", "peak_rss_bytes", "profile", "half_bandwidth", "natural_half_bandwidth", "planned_flops", "planned_bytes",
        "kappa_est", "lambda_min", "lambda_max", "ic_shift", "ic_min_pivot_ratio", "residual_replacements", "detail"};
    io::CsvWriter table(cols);
    table.comment("Round 2 solver study. One row per (geometry, level, solver); each row ran in its own process. See machine.txt.");
    table.comment("param: cells per side (cube) or grid spacing in mm (hsA, hsB). Times in seconds, memory in bytes.");
    for (const auto& r : rows) {
        std::vector<std::string> cells;
        for (const auto& c : cols) cells.push_back(gets(r, c));
        table.writeRow(cells);
    }
    io::writeTextFileAtomic((dir / "solver_runs.csv").string(), table.str());

    // 3. Figures from the table.
    for (const std::string g : {"cube", "hsA", "hsB"}) scalingFigures(dir, rows, g);
    historyFigure(dir, "cube", quick ? 16 : 32, kSolverOrder, "Convergence on the unit cube");
    historyFigure(dir, "hsB", quick ? 2 : 1, kSolverOrder, "Convergence on heat sink B (pad + copper)");
    historyFigure(dir, "hsA", quick ? 2 : 1, kSolverOrder, "Convergence on heat sink A (aluminium)");

    // 4. Single-purpose experiments.
    std::cerr << "experiments: drift, penalty, SOR range, IC(0) robustness, spy, pictures\n";
    driftExperiment(dir);
    penaltyExperiment(dir);
    sorDivergenceExperiment(dir);
    icRobustnessExperiment(dir);
    spyFigures(dir);
    heatSinkPictures(dir, false, quick ? 2.0 : 1.0);
    heatSinkPictures(dir, true, quick ? 2.0 : 1.0);
    fs::remove_all(scratch);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc >= 6 && std::string(argv[1]) == "--one") {
            return runOne(argv[2], std::stod(argv[3]), argv[4], std::stod(argv[5]), argc >= 7 ? argv[6] : "");
        }
        if (argc >= 2 && argv[1][0] != '-') {
            const bool quick = argc >= 3 && std::string(argv[2]) == "--quick";
            return runStudy(fs::absolute(argv[0]).string(), argv[1], quick);
        }
        std::cerr << "usage: wb_solvers <outdir> [--quick]\n       wb_solvers --one <geometry> <param> <solver> <omega> [history.csv]\n";
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
