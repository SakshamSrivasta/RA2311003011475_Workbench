# Round 2 blueprint: three solvers, one thermal problem, two geometries

Status: draft for confirmation, no code written yet. Deadline 30 Nov 2026. Deliverable is one PDF report (no source code), but the code behind it has to be real, because every number and picture in the report comes from it and the interview may ask for any of it to be reproduced.

## 0. What the notice asks for, read carefully

1. Apply three different solvers to "the same cube", compare efficiency.
2. If the cube gives similar results for all solvers "due to linearity", change the geometry to something more complex so that the differences show.
3. Justify how each of the three solvers was built, and which one wins in which scenario.
4. Report only, PDF, pictures essential.

Two things need saying up front in the report:

- **Round 1 did not solve a cube.** It solved a 1D rod. The notice is a common text sent to everyone, so Round 2 starts by doing the cube properly in 3D (the baseline the notice assumes exists), then moves to the complex geometry. Saying this plainly is better than pretending.
- **"Similar results due to linearity" is expected and is not a problem to fix.** For a linear SPD system all three solvers converge to the *same* discrete solution; the geometry cannot change that. What the geometry changes is the *cost* and *robustness* of getting there: condition number, bandwidth, fill-in, iteration counts. The report has to make this distinction in its first page, because it is the conceptual trap in the brief.

The evaluators' email ("work on the inner things as well") sets the bar: the report must show predicted-versus-measured behaviour for every solver, not just timings. Every section below ends with the prediction we will check.

## 1. What carries over from Round 1

| Round 1 piece | Round 2 use |
|---|---|
| `fea/ThermalRod1D` (exact solution, midpoint error) | Verification of the 3D code: a bar or cube with hot/cold end faces and insulated sides must reproduce the rod solution at the nodes to round-off. Rod stays untouched. |
| `common/Errors`, `Cancellation`, `Stopwatch` | Same error codes, same cancel token inside the iterative loops, same timer for the benchmarks. |
| `platform/SystemInfo` | Benchmark header (CPU, RAM, compiler, flags), as in Round 1. |
| `io/CsvWriter`, `TextFile` | All result CSVs (round-trip-exact numbers, atomic writes). |
| `io/StlReader`, `NodalCsvReader` and the Geometry view | Round 2 **writes** the Round 1 formats: the mesh surface as ASCII STL and the temperature field as `node_id,x_m,y_m,z_m,temperature_C`. The Round 1 viewer then renders the 3D result for the report. The synthetic CSV from Round 1 is replaced by a computed field. |
| `data/heatsink_fins_mm.stl` geometry (base 60×40×5 mm, 6 fins 2×25 mm) | Becomes the complex geometry. Same dimensions, now actually solved. |
| `batch/` thread pool | Optional: parallel SpMV / dot products for one extra experiment ("which solver parallelises"). Not on the critical path. |
| `tools/SvgPlot.h` | Extended with a cell/heat-map renderer (temperature slices, matrix sparsity "spy" plots) and fitted-slope labels. |
| `tools/generate_results` pattern, `wb_results` | New tool `wb_solvers` follows the same style: one binary, writes a results folder, deterministic. |
| `tests/TestHarness.h` | New test files plug in. |
| `scripts/ui_walkthrough.py` (headless Chrome) | Reused to screenshot the 3D renders. |
| `scripts/build_report_pdf.py` | Reused for the Round 2 PDF (Markdown → Chrome → A4 PDF). LaTeX is not installed and we have no package manager; this pipeline already works. |
| `app/` (Launch, JobManager, routes) | Unchanged. A "Solver study" tab is optional (priority 3). |

Nothing in Round 1 changes behaviour. Round 2 is additive: new core modules, one new tool, new tests, new docs under `docs/round2/`.

## 2. Physics and discretisation

**Equation.** Steady conduction −∇·(k∇T) = q in 3D, k piecewise constant per material.

**Boundary conditions supported.**
- Dirichlet (prescribed T) on tagged faces → eliminated symmetrically, as in Round 1, so the matrix stays SPD. The penalty / "big number" method is deliberately *not* used; one experiment shows what it does to the condition number.
- Neumann heat flux q″ on tagged faces (the chip footprint).
- Robin / convection h(T − T∞) on tagged faces (all exposed heat-sink surfaces). Adds h·(face mass matrix) to the diagonal blocks: keeps the matrix SPD and removes the need for any Dirichlet node on the heat sink.
- Uniform volumetric q per material (the Round 1 case).

**Element.** 8-node trilinear hexahedron on an axis-aligned box a×b×c. The stiffness matrix has an exact Kronecker form, because the integrand separates:

K = k ( S_a ⊗ M_b ⊗ M_c + M_a ⊗ S_b ⊗ M_c + M_a ⊗ M_b ⊗ S_c )

with 1D stiffness S_L = (1/L)[[1,−1],[−1,1]] and 1D mass M_L = (L/6)[[2,1],[1,2]]. Load from uniform q is q·abc/8 at each node; convection on a face is h·(M_u ⊗ M_v) with RHS h·T∞·area/4 per node. A unit test checks the closed form against 2×2×2 Gauss quadrature. Only a handful of distinct box sizes exist on a graded grid, so element matrices are computed once per size class: assembly is a few hundred ms even at 2 M nodes.

**Mesh.** Structured, graded Cartesian grid with an active-cell mask:
- geometry = list of axis-aligned boxes with material IDs, plus cut-out boxes;
- grid lines include every box face coordinate, so a box-union geometry (cube, finned heat sink, L-bracket, perforated block) is represented **exactly**, with no stair-stepping;
- spacing between key coordinates is chosen from a target h, with optional refinement around the chip and the fin roots;
- node numbering lexicographic with the shortest axis fastest (this sets the bandwidth, see §4.1), plus an optional RCM reordering.

Why not an unstructured tetrahedral mesh: it needs a mesher we would have to write or import, and it adds nothing to the solver comparison. The structured grid gives exact geometry, cheap assembly and a bandwidth we can *predict*, which is the point of the study. An STL voxeliser (ray-parity) that reuses `StlReader` is listed as priority 3; it is not required because the heat sink is already a box union.

**Sparse matrix.** CSR, full symmetric storage (both triangles, sorted column indices, duplicates summed in a deterministic order). 27 non-zeros per interior row → about 330 bytes per row; 2 M nodes ≈ 700 MB with vectors. The direct solver uses a separate skyline (envelope) copy.

**Prediction to check:** row sums of K are zero (constant field is reproduced); a linear field is reproduced exactly on an irregular graded mesh (patch test); manufactured-solution error is O(h²) in L2 and L∞; the 3D bar reproduces the Round 1 rod to ~1e-10 °C.

## 3. Geometries and the mesh levels

### 3.1 Cube (the baseline the notice assumes)

1 m aluminium-like cube with exactly the Round 1 rod data: k = 10 W/(m·K), q = 10⁴ W/m³, T = 100 °C on x = 0, T = 20 °C on x = L, other faces insulated. The solution is the Round 1 rod formula, so the 3D code is verified against a known answer first. A second cube case uses a manufactured solution T = sin πx · sin πy · sin πz + linear part, with q derived analytically and Dirichlet data from T on all faces, to measure a genuinely 3D error.

Levels N = 8, 16, 24, 32, 48 for all three solvers; 64, 96, 128 (2.1 M nodes) for the iterative solvers only. The direct solver is *predicted* to need ≈ 9 GB at N = 64 (see §4.1) and the tool refuses to run it above a configurable cap (default 3 GB) and prints the prediction instead. That refusal, with its number, is a result.

### 3.2 Finned heat sink (the complex geometry)

The Round 1 part: base 60 × 40 × 5 mm, six fins 2 mm thick, 25 mm tall, running along y. Aluminium k = 200 W/(m·K). A 20 × 20 mm chip footprint centred under the base receives 50 W (q″ = 125 kW/m²). All other exposed faces convect with h = 50 W/(m²·K) to 25 °C. Expected chip-region temperature ≈ 70–100 °C; fin efficiency ≈ 0.95 (checked against the 1D fin formula tanh(mL)/(mL)).

Variant B ("ill-conditioned"): same part plus a 0.5 mm thermal pad (k = 3) between the chip and the base and a copper spreader (k = 400) in the base. Coefficient contrast 400/3 ≈ 130 and a one-cell-thick layer with aspect ratio 4–8. Costs nothing in code (materials are per box) and is the case where Jacobi preconditioning visibly earns its keep.

Levels by target h = 2, 1, 0.5, 0.25 mm → roughly 6 k, 30 k, 220 k, 1.7 M nodes (33 % of the 60×40×30 mm bounding box is solid). Direct solver at h = 2 and 1 mm; predicted ≈ 8 GB at 0.5 mm → refused with the prediction printed.

**Why this geometry exposes the solvers.** Thin fins force small or anisotropic elements (condition number grows with h⁻² and with aspect ratio²); the pad adds coefficient contrast (κ grows with k_max/k_min); the fins make the node graph "wide" so the envelope of the direct factor is far larger than the cube's at equal node count; and the convection-only boundary conditions make the smallest eigenvalue depend on h·A/(k·V), a different regime from Dirichlet. Each of these is a measurable prediction, not a story.

## 4. The three solvers, and how each is built

All three take the same CSR matrix and RHS, a start vector, a tolerance, an iteration cap, the Round 1 `CancelToken`, and return a `SolveReport` (iterations, residual history, setup/solve times, bytes allocated, status). Single-threaded by default so the comparison is of algorithms, not of thread counts.

### 4.1 Direct: skyline (envelope) Cholesky with RCM ordering

- Storage: for each column j, entries from the first non-zero row down to the diagonal. Profile size = Σ column heights. Factorisation LLᵀ in place, column by column (Jennings' active-column scheme). Forward/back substitution for the solve.
- Ordering: natural lexicographic (shortest axis fastest) and reverse Cuthill–McKee (BFS from a pseudo-peripheral node). Both measured; "spy" pictures of A and L before and after.
- SPD guard: a non-positive pivot aborts with `NumericalFailure` and the row index, instead of producing NaNs.
- Memory guard: profile × 8 bytes is computed *before* allocating; if it exceeds the cap the run is refused and the prediction is recorded in the CSV.
- Why not a general sparse multifrontal solver with nested dissection: it is a month of work on its own, and the envelope solver's failure mode (fill ∝ N⁵ in 3D) *is* the lesson about direct solvers in 3D. The report explains what PARDISO/MUMPS do differently and why that changes the exponent, with the cube numbers as evidence.

**Predictions:** bandwidth b ≈ n_z·n_y for natural ordering; profile ≈ n·b; factor flops ≈ n·b²; solve flops ≈ 2·profile. For the cube: N = 32 → 36 k nodes, b ≈ 1 k, factor ≈ 300 MB and ≈ 40 Gflop; N = 64 → 9 GB. For the heat sink at h = 1 mm: 30 k nodes, b ≈ 1.2 k, ≈ 290 MB. Measured profile, time and the fitted exponent of time vs n go in the report next to these.

### 4.2 Stationary: Gauss–Seidel and SOR

- One sweep in CSR order (deterministic); ω = 1 is Gauss–Seidel. Jacobi is kept as a one-function baseline because it is the PCG preconditioner anyway and it shows the factor-of-two and the parallelism argument.
- Stopping on the *true* relative residual ‖b − Ax‖/‖b‖, computed every 10 sweeps (a sweep does not produce the residual for free; the report says so). Iteration cap and a stagnation/divergence detector.
- ω sweep experiment: iterations vs ω on a fixed mesh for the cube and the heat sink; the model-problem optimum 2/(1 + sin πh) is drawn as the prediction. Ostrowski–Reich: convergence for 0 < ω < 2 on SPD, divergence at ω ≥ 2 — tested, and shown.

**Predictions:** GS iterations ∝ h⁻² (κ), SOR with ω_opt ∝ h⁻¹; GS ≈ half of Jacobi; the heat-sink optimum ω is not the model-problem one.

### 4.3 Krylov: conjugate gradient, plain / Jacobi / incomplete Cholesky

- Textbook CG with the recursively updated residual, plus an explicit residual every 50 iterations so that round-off drift is visible (both are logged and plotted).
- Preconditioners behind one interface: none, Jacobi (diagonal), IC(0) (zero-fill incomplete Cholesky on the CSR pattern). Trilinear hex matrices are not M-matrices for stretched elements, so IC(0) can hit a non-positive pivot; the implementation detects it and retries with a diagonal shift (Manteuffel), and the report shows a case where this was needed.
- Condition-number estimate for free: the CG coefficients define the Lanczos tridiagonal; its extreme eigenvalues (Sturm-sequence bisection on a small tridiagonal) give κ(A) and κ(M⁻¹A). Cross-checked on small systems against power / inverse iteration using the direct solver.
- Error versus residual: ‖x_k − x_direct‖ is recorded too, to show that a small residual does not mean a small error when κ is large (the factor between them is the measured κ).

**Predictions:** iterations ∝ √κ ∝ h⁻¹; Jacobi removes most of the coefficient-contrast penalty in variant B; IC(0) cuts iterations by 3–5× at about 2× the cost per iteration; κ from Lanczos matches the direct estimate to a few %.

### 4.4 Deliberate "known pitfall" experiments (one figure each)

1. Dirichlet by penalty (big number) vs elimination: κ and CG iterations.
2. Stopping on the recursive CG residual only: drift at tight tolerances.
3. IC(0) without the shift on variant B: breakdown.
4. SOR at ω = 1.95 / 2.0 / 2.05.
5. Natural vs RCM ordering: profile size and factor time.

These are cheap, each is a prediction with a measurement, and together they are the "inner things".

## 5. Benchmark harness and evidence

`wb_solvers <outdir> [--geometry cube|heatsink|heatsink-b] [--levels …] [--solvers …] [--reps 3] [--tol 1e-8] [--mem-cap-gb 3]`

- Each (geometry, level, solver, variant) runs in a **fresh child process** (`--one <case>`), so peak memory and timings are isolated and repeatable. Peak memory = bytes we allocate (counted, deterministic) and the OS high-water mark (`ru_maxrss` on macOS/Linux, `PeakWorkingSetSize` on Windows, in `SystemInfo`'s style).
- Timing as in Round 1: warm-up, then median of 3 (5 for the small cases) with the raw repetitions kept; assembly, ordering, setup/factor and solve timed separately; launch and file I/O excluded.
- Agreement: every solver's solution is compared with the direct solution (max |ΔT|) where the direct solver ran, and with the finest tolerance PCG result where it did not.
- Outputs: `solver_runs.csv`, `residual_history_*.csv`, `omega_sweep.csv`, `pitfalls.csv`, SVG figures, slice heat maps, spy plots, `heatsink_result.csv` + `heatsink_mesh.stl` for the 3D renders in the Round 1 viewer, and `machine.txt`.
- Figures for the report: residual vs iteration (all solvers, log-y); time vs n and memory vs n (log-log with fitted slopes; the direct solver's predicted line continues past the point where it was refused); iterations vs n; iterations vs ω; κ vs h and vs contrast; error vs h (L2, L∞); time breakdown bars; temperature slices through the chip centre; 3D renders; spy plots.

## 6. Tests (added to `wb_tests`)

Element and assembly: Kronecker vs quadrature; symmetry; zero row sums; patch test on a graded mesh; manufactured-solution order ≈ 2; Round 1 rod reproduced by a 3D bar; energy balance on the heat sink (generated + flux in = convected out). Solvers: each against a dense reference on a small random SPD matrix; three-way agreement on FE systems; direct residual ≈ 1e-13; SPD guard; memory-cap refusal; SOR theorem (converges for ω < 2, not for ω ≥ 2); CG finite termination on a tiny system; IC(0) breakdown handled; bitwise determinism across two runs; cancellation mid-solve reports "cancelled", never a half result.

## 7. Report plan (single PDF, target 12–16 pages, figures on nearly every page)

1. Summary and the winner-by-scenario table.
2. From Round 1 to Round 2 (rod → cube → heat sink; what was reused; the honest note about the cube).
3. Problem, element, assembly, why the matrix is SPD (with a spy figure).
4. Geometries and mesh levels; why the heat sink raises κ and bandwidth (measured κ table).
5. The three solvers, how each is built: storage, algorithm, complexity, stopping rule, the pitfalls section.
6. Verification: rod reproduction, manufactured-solution convergence, energy balance, agreement table.
7. Results: cube, then heat sink A and B.
8. Trade-off matrix: small vs large n, well vs ill-conditioned, memory-bound vs time-bound, one solve vs many right-hand sides, serial vs parallel hardware — each cell pointing at a figure. Then what commercial codes do and why.
9. Limitations and next steps (nested dissection, multigrid, matrix-free, threads).
10. Appendix: machine and build, raw tables, one-paragraph AI-use note consistent with Round 1's log.

Companion file not for submission: `docs/round2/DEFENCE_NOTES.md`, the derivations and the questions an interviewer would ask, so every claim in the report can be reproduced on request.

## 8. Schedule (14 days)

| Days | Work |
|---|---|
| 1–2 | Grid, geometry, element, assembly, BCs; tests (rod reproduction, patch, manufactured) |
| 3–4 | CSR, skyline Cholesky, RCM, predictions vs measurements; tests |
| 5 | GS/SOR with ω sweep; CG and Jacobi-PCG; tests |
| 6 | IC(0) with shift; κ estimate; residual drift; agreement and determinism tests |
| 7–8 | `wb_solvers` harness, CSV, SVG extensions (heat map, spy, slopes), 3D renders via the viewer |
| 9–10 | Full experiment matrix (big runs overnight), data review, re-runs |
| 11–13 | Report, figures, trade-off matrix, defence notes; your review loop |
| 14 | Buffer, final PDF, email |

Your own time during this: re-derive the element matrix and CG by hand from the defence notes, run `wb_solvers` yourself on one case, and read every figure before it goes in.

## 9. Decisions to confirm

1. Geometry: reuse the Round 1 heat sink with chip flux + convection, plus variant B (pad + copper) as the ill-conditioned case. Alternatives (L-bracket, perforated block) are the same code with a different box list, so one could be added later if wanted.
2. Discretisation: trilinear hex FEM on a graded structured grid with an active mask (exact for box geometries). Not FVM, not tets.
3. Direct solver: skyline Cholesky + RCM, with the memory-cap refusal as a reported result. Nested dissection only if time is left.
4. Krylov preconditioners: Jacobi (must) and IC(0) with shift (should).
5. Main comparison single-threaded; parallel SpMV as one optional extra experiment.
6. No UI work on the critical path; the Round 1 viewer renders the 3D results for the report.
7. Report via the existing Markdown → PDF pipeline, 12–16 pages.
8. One-paragraph AI-use note in the report appendix.
