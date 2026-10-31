# Git history summary

Readable log of the development history (oldest first). The ZIP also contains the full repository as `vcs/RA2311003011475_Workbench.bundle` (clone with `git clone RA2311003011475_Workbench.bundle`) and a per-commit file list in `vcs/commit_log.txt`.

Every commit carries a `Co-Authored-By: Claude` trailer because the work was done with Claude Code (see AI_USAGE_LOG.md). This file is generated with `git log` and does not list the commit that adds it.

| # | Commit | Date | Summary |
|---|---|---|---|
| 1 | `9da0c22` | 2026-10-31 | Set up project layout, CMake build and core error/cancellation utilities |
| 2 | `898ca0d` | 2026-10-31 | Add 1D steady-state thermal FEA solver (Task C) |
| 3 | `c885dad` | 2026-10-31 | Add reproducible batch generator, serial/parallel runner and benchmark (Task D) |
| 4 | `e7d1ded` | 2026-10-31 | Add hand-derived backpropagation and gradient check (Task F) |
| 5 | `3aa7640` | 2026-10-31 | Add ASCII STL and nodal CSV readers with strict validation |
| 6 | `cf2777d` | 2026-10-31 | Add simulated MCU/FPGA/JTAG adapters and device manager |
| 7 | `e93920b` | 2026-10-31 | Add unit tests and Windows cross-compile toolchain |
| 8 | `944f547` | 2026-10-31 | Add shared CSV exports and a batch memory guard |
| 9 | `633124d` | 2026-10-31 | Vendor cpp-httplib 0.18.3 and nlohmann/json 3.11.3 (MIT) |
| 10 | `7f4783b` | 2026-10-31 | Add Launch: local HTTP server, job manager and API routes |
| 11 | `e9e3a14` | 2026-10-31 | Add browser UI: geometry, thermal, performance, hardware and backprop views |
| 12 | `af3d637` | 2026-10-31 | Add example data generator and results generator |
| 13 | `4138448` | 2026-10-31 | Fix benchmark button that silently did nothing |
| 14 | `b1e4cbc` | 2026-10-31 | Generate results: FEA convergence, benchmark, backprop, file-size tests |
| 15 | `758ba86` | 2026-10-31 | Add packaging script, VS Code run configs and Windows CI |
| 16 | `e47678c` | 2026-10-31 | Add README, report, embedded design, walkthrough and AI-use log |

## How the history is organised

1. Project skeleton and the C++ core, one task at a time (FEA, batch/parallel, backprop).
2. File readers and hardware mocks, then the unit tests (including the corrections the tests forced).
3. Transport layer (Launch), then the browser UI, then example data and results generators.
4. Bug fixes found by the scripted UI walkthrough, generated results, packaging and CI, documentation.
