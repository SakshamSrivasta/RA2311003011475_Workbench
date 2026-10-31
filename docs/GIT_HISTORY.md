# Git history summary

Readable log of the development history (oldest first). The ZIP also contains the full repository as `vcs/RA2311003011475_Workbench.bundle` (clone with `git clone RA2311003011475_Workbench.bundle`) and a per-commit file list in `vcs/commit_log.txt`.

Every commit carries a `Co-Authored-By: Claude` trailer because the work was done with Claude Code (see AI_USAGE_LOG.md). This file is generated with `git log` and does not list the commit that adds it.

| # | Commit | Date | Summary |
|---|---|---|---|
| 1 | `f2f44bf` | 2026-10-31 | Set up project layout, CMake build and core error/cancellation utilities |
| 2 | `a3ccd39` | 2026-10-31 | Add 1D steady-state thermal FEA solver (Task C) |
| 3 | `0f91d83` | 2026-10-31 | Add reproducible batch generator, serial/parallel runner and benchmark (Task D) |
| 4 | `807c167` | 2026-10-31 | Add hand-derived backpropagation and gradient check (Task F) |
| 5 | `85e9f4b` | 2026-10-31 | Add ASCII STL and nodal CSV readers with strict validation |
| 6 | `5f1adb3` | 2026-10-31 | Add simulated MCU/FPGA/JTAG adapters and device manager |
| 7 | `868ca55` | 2026-10-31 | Add unit tests and Windows cross-compile toolchain |
| 8 | `5f7ca88` | 2026-10-31 | Add shared CSV exports and a batch memory guard |
| 9 | `77e610a` | 2026-10-31 | Vendor cpp-httplib 0.18.3 and nlohmann/json 3.11.3 (MIT) |
| 10 | `71421af` | 2026-10-31 | Add Launch: local HTTP server, job manager and API routes |
| 11 | `d399493` | 2026-10-31 | Add browser UI: geometry, thermal, performance, hardware and backprop views |
| 12 | `c637621` | 2026-10-31 | Add example data generator and results generator |
| 13 | `6651038` | 2026-10-31 | Fix benchmark button that silently did nothing |
| 14 | `8604b37` | 2026-10-31 | Generate results: FEA convergence, benchmark, backprop, file-size tests |
| 15 | `be5fc42` | 2026-10-31 | Add packaging script, VS Code run configs and Windows CI |
| 16 | `a903060` | 2026-10-31 | Add README, report, embedded design, walkthrough and AI-use log |

## How the history is organised

1. Project skeleton and the C++ core, one task at a time (FEA, batch/parallel, backprop).
2. File readers and hardware mocks, then the unit tests (including the corrections the tests forced).
3. Transport layer (Launch), then the browser UI, then example data and results generators.
4. Bug fixes found by the scripted UI walkthrough, generated results, packaging and CI, documentation.
