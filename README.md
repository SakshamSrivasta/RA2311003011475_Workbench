# Unified Engineering Workbench

PASS Science Works R&D internship assignment 2026–27 · Candidate **RA2311003011475**

A small engineering workbench with a C++17 core and a browser UI served locally by `Launch.exe`:

| View | What it does |
|---|---|
| **Geometry** | Imports ASCII STL and ANSYS-style nodal temperature CSV, shows them in 3D (rotate / pan / zoom / fit), colours nodes by temperature with a legend, hover/click to inspect nodes |
| **Thermal Analysis** | 1D steady-state FEA of a heated rod with linear elements, compared with the exact solution; midpoint-error convergence for N = 10, 20, 40, 80; CSV export |
| **Performance** | Serial vs multi-threaded batch of independent rod solves; warm-up + 5 timed runs; speedup, efficiency and a 1e-8 °C agreement check; cancellable background job |
| **Hardware** | Simulated MCU board, FPGA board and JTAG adapter: discovery, capabilities, connect/disconnect, live telemetry, injected timeout / link-drop with recovery |
| **Backprop** | The one-neuron backprop exercise: forward pass, chain-rule gradients, finite-difference check, one update and the 100-step loss curve |

All computation, file parsing, validation and threading is C++ (`src/core`). The browser only displays results.

---

## 1. Run it (Windows 11 x64, no install)

1. Extract `RA2311003011475_Workbench.zip` anywhere (Desktop, Downloads… any folder works, including non-English names).
2. Double-click **`Launch.exe`** in the extracted folder.
3. Your default browser opens `http://127.0.0.1:8765/`. A console window shows the server log; **close it (or press *Quit* in the UI) to stop**.

No admin rights, no installer, no internet, no first-run build. The exe links the C++ runtime statically and only needs DLLs that ship with Windows 10/11 (KERNEL32, WS2_32, SHELL32, ADVAPI32 and the Universal C Runtime).

**If Windows SmartScreen says "Windows protected your PC"**: the exe is not code-signed (this is a student prototype). Click *More info → Run anyway*. This is a one-time prompt per download.

**If the browser does not open**: the console prints the URL; open it manually. If port 8765 is busy the next free port is used and printed.

Quick self-check without a browser: `Launch.exe --smoke-test` (starts, runs 10 API checks, exits with 0 on success).

### From VS Code

Open the extracted folder in VS Code, then either:

- **Run and Debug** → select **"Workbench: packaged Launch.exe (Windows)"** → F5. Needs the *C/C++* extension (`ms-vscode.cpptools`, recommended automatically via `.vscode/extensions.json`).
- or, with no extension at all: **Terminal → Run Task → "Workbench: run packaged Launch.exe"**.

Both start the same `Launch.exe` as double-clicking. The standalone launcher never needs VS Code or the internet.

---

## 2. Folder layout

```
RA2311003011475_Workbench/
├── Launch.exe              double-click entry point (packaged build only)
├── app/web/                UI files served by Launch.exe (copy of src/web)
├── tools/                  wb_tests.exe, wb_results.exe, wb_make_sample_data.exe
├── src/
│   ├── core/               C++17 engine - no UI, no HTTP, no JSON
│   │   ├── common/         errors, cancellation token, stopwatch
│   │   ├── fea/            1D thermal FEA solver
│   │   ├── batch/          batch generator, serial/parallel runner, benchmark
│   │   ├── io/             ASCII STL reader, nodal CSV reader, CSV writer
│   │   ├── hardware/       device adapter interface + simulated adapters
│   │   ├── ml/             hand-derived backpropagation
│   │   ├── platform/       CPU / RAM / compiler detection
│   │   └── report/         CSV exports shared by UI and results generator
│   ├── app/                Launch: HTTP server, routes (JSON <-> core), job manager
│   ├── web/                browser UI (HTML/CSS/ES modules, three.js vendored)
│   └── tools/              example-data and results generators
├── tests/                  53 unit tests (positive, negative, numerical)
├── data/                   example STL + SYNTHETIC nodal CSV, PROVENANCE.md
├── results/                raw CSVs and SVG plots used in the report
├── docs/                   report, embedded design, AI-use log, ownership, deps
├── third_party/            cpp-httplib, nlohmann/json (MIT, header-only)
├── cmake/                  llvm-mingw cross-compile toolchain file
├── scripts/package.sh      builds the Windows release and the ZIP
├── vcs/                    git bundle + readable commit log (in the ZIP)
├── .vscode/                launch + task configurations
└── CMakeLists.txt
```

## 3. Architecture

```
 Browser (src/web)  --HTTP/JSON on 127.0.0.1-->  Launch.exe (src/app)  --C++ calls-->  wb_core (src/core)
  views, plots, 3D        presentation              routes, validation of           FEA, parsers, threads,
                                                     request fields, job thread       mocks, backprop
```

- **`wb_core`** is a static library with no knowledge of HTTP or JSON. It throws `WorkbenchError` with a code and a message written for the user ("STL line 12: expected 'endloop' but found 'vertex'…").
- **`src/app`** turns requests into core calls and core results/errors into JSON. Long work (benchmarks, big batches) runs in `JobManager` on its own thread; the UI polls progress and can cancel. Only one job runs at a time so timings are not polluted.
- **Cancellation** is cooperative: the core checks a `CancelToken` between independent solves. A cancelled run is reported as cancelled and its partial results are discarded, never exported.
- The parallel batch uses a fixed pool of `std::thread`s pulling indices from an atomic counter; each result lands in its own pre-allocated slot, so output order always equals input order.

## 4. Supported formats

| Format | Rules | Limits (tested) |
|---|---|---|
| **ASCII STL** | `solid / facet normal / outer loop / 3×vertex / endloop / endfacet / endsolid`, case-insensitive, several solids allowed. Binary STL is detected and rejected with a hint. Zero normals are recomputed; zero-area facets are counted. STL has no units: pick mm / m / inch in the UI. | 64 MB, 1 000 000 triangles. 500 000 triangles (53 MB) parsed in 0.24 s |
| **Nodal CSV** | Header exactly `node_id,x_m,y_m,z_m,temperature_C`; `#` comment lines; UTF-8 BOM and CRLF accepted; node ids positive integers and unique; all values finite; T ≥ −273.15 °C | 64 MB, 1 000 000 rows. 1 M rows (28 MB) parsed in 0.19 s |

Rejected with an explanation: empty files, header-only files, wrong header, wrong field count, NaN/Inf, non-numeric values, duplicate ids, quoted fields, files over the limits. Full table: `results/io_size_limits.csv`; data notes: `data/PROVENANCE.md`.

## 5. Build from source (CMake)

Requirements: CMake ≥ 3.20 and a C++17 compiler (MSVC 2022, GCC ≥ 9, Clang ≥ 10). No packages to install; the two header-only libraries are vendored in `third_party/`.

**Windows (Visual Studio 2022):**
```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
build\Release\Launch.exe
```

**macOS / Linux:**
```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/Launch
```

A development build finds the UI in `src/web` and examples in `data/` automatically.

**Packaged Windows build from macOS/Linux** (how the ZIP is produced), using [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) 20260922:
```sh
LLVM_MINGW_ROOT=$HOME/toolchains/llvm-mingw-20260922-ucrt-macos-universal scripts/package.sh
```

**Regenerate results/** on the machine you want to report: `wb_results results` (or `tools\wb_results.exe results` from the ZIP).

## 6. Tests

`wb_tests` (also `tools\wb_tests.exe` in the ZIP) runs 53 tests:

- **FEA**: boundary values exact; analytical formula; nodal exactness; midpoint error = q·h²/(8k) for N = 10…80; observed order 2.000; q = 0 linear case; N = 1; invalid L, A, k, N, NaN/Inf rejected.
- **Backprop**: forward values and gradients against hand-computed numbers; central-difference check (ε = 1e-6, |Δ| ≤ 1e-5) at the assignment point and at a second point; one simultaneous update; monotonic loss decrease; invalid settings rejected.
- **Batch / parallel**: reproducible generator (pinned first case); serial vs 1, 2, 3, 4, 8 workers agree ≤ 1e-8 °C; input order preserved; cancellation before and during a run leaves only complete, correct results; bad configs and oversize batches rejected; benchmark single-CPU path.
- **I/O**: valid STL (upper case, CRLF, scientific notation, multiple solids); empty/facetless/binary/truncated/NaN/Inf STL; CSV with BOM, comments, blanks; wrong header, wrong column count, NaN, duplicates, quotes, below absolute zero; atomic write/read-back.
- **Hardware mocks**: discovery of three device types; telemetry sequence; JTAG IDCODE; timeout → faulted → recover; link drop needs retry; faults isolated per device.

`Launch --smoke-test` exercises the HTTP layer end to end (10 checks).

## 7. Known limitations

- Developed on macOS (Apple M2) and cross-compiled for Windows. The packaged `Launch.exe` was built and its imports checked, and the same code is built and tested with MSVC by `.github/workflows/windows.yml`; it still needs a final double-click test on a Windows 11 machine before submission.
- The exe is unsigned, so SmartScreen may ask once (see §1).
- Benchmark numbers in `results/` come from the development Mac. Run the benchmark in the UI (or `tools\wb_results.exe`) on the grading PC to get its numbers; the CSV records CPU, cores, RAM, compiler and flags.
- STL and point cloud are drawn in one scene but are not registered to each other; the example files share a coordinate frame by construction.
- Interactive FEA plots are capped at N ≤ 100 000 elements (the solver itself accepts 10 M).
- Hardware is simulated only. No real serial/USB/JTAG I/O exists.
- One background job at a time, by design.

## 8. Third-party components

cpp-httplib 0.18.3 (MIT), nlohmann/json 3.11.3 (MIT), three.js 0.160.0 (MIT). Versions, checksums and licence files: `docs/DEPENDENCIES.md`. AI assistance is disclosed in `docs/AI_USAGE_LOG.md`.
