# AI-use log

The brief allows AI assistance and asks for an honest record. This log covers what the assistant did, what it got wrong and how that was caught. Sections 4 and 5 are for the candidate to complete in their own words; they are deliberately left empty rather than filled in on the candidate's behalf.

## 1. Tools

| Tool | Model | Where | Dates |
|---|---|---|---|
| Claude Code (VS Code extension), agentic mode with file and shell access on the candidate's Mac | Claude Opus 5.5 (`claude-opus-5-5`) | Whole project: architecture, C++ core, tests, server, UI, tools, docs, packaging | 2026-10-31 |

No paid solver, CAD tool or other AI tool was used. *(Candidate: add any other assistants you used, e.g. for learning the FEA or backprop theory.)*

## 2. Representative prompts and what came back

**Prompt 1 (candidate → Claude Code), summarised faithfully.** The candidate attached the assignment PDF and a detailed specification restating every task (A–F), the Windows one-click requirement, the folder layout, and a git commit/push after each task. They asked to "start by setting up the folder architecture and generating CMakeLists.txt and src/core/".

*Output:* folder layout; `CMakeLists.txt`; core modules for errors and cancellation, the 1D FEA solver, the batch generator, the serial/parallel runner and benchmark, system info, and backprop. Before committing, the assistant compiled them with strict warnings and ran a scratch check (midpoint errors 1.25 → 0.0195, order 2; gradient errors ≈ 1e-10; parallel speedup ≈ 4×). The `git push` failed because GitHub rejected the stored credentials; the assistant reported that instead of working around it.

**Prompt 2 (candidate → Claude Code), summarised.** "Proceed with the local web server approach for Launch.exe…". The candidate asked for all code, comments, commit messages and docs to read naturally, like an engineering student's work. The tasks: tests, results, UI with four views, report and AI log, ZIP packaging, commit and push.

*Response to the style request:* the assistant agreed to write plainly, but said this log would record the AI involvement truthfully, because the brief forbids fabricated interactions. That is why this file exists in this form.

*Output:* STL/CSV readers, hardware mocks, 53 unit tests, HTTP server and job manager, browser UI, data and results generators, packaging script, CI workflow, README and docs.

**Toolchain decisions the assistant made and reported:** no Homebrew on the machine, so CMake came from `pip --user` and the Windows cross-compiler from the llvm-mingw GitHub release. It deliberately picked a release about a month old rather than the newest one, as a supply-chain precaution. Third-party libraries were pinned to exact versions with checksums (`docs/DEPENDENCIES.md`).

## 3. Incorrect or unsuitable AI output that was caught

Each of these was produced by the assistant and caught during the session by a test, a compile, or a visual check of the running UI. They are listed because "the AI wrote it" is not evidence that it is right.

| # | What the AI produced | How it was caught | Fix / lesson |
|---|---|---|---|
| 1 | Backprop test reference values typed from memory: J = 1.18254281 and ∂J/∂v = −0.71068206 (wrong in the 8th digit) | 3 unit tests failed | Recomputed with the program and checked by hand (J = ½·1.537883² = 1.182542). Reference numbers must come from a calculation, not recall |
| 2 | The "pinned first batch case" test used **invented** numbers (k = 312.02…, q = 51373.4…) presented as if known | Test failed: the generator really gives k = 74.9928…, q = 34307.05… | Replaced with values printed by the generator. Never accept AI-supplied "expected values" without computing them independently |
| 3 | `respondCsv()` helper: returned the CSV, then threw an exception to exit the lambda | Spotted on review before first build: the catch-all would have turned every CSV export into HTTP 500 | Rewritten with a shared error writer; exports tested with curl |
| 4 | CSS `.viewer-empty { display: grid }` overrode the `hidden` attribute, so "Load an STL…" never disappeared over the 3D view | Headless-Chrome screenshot of the Geometry view | Added `[hidden] { display: none !important; }` |
| 5 | Log-axis plots labelled every decade: 21 overlapping labels on the loss plot | Screenshot of the Backprop view | Ticks thinned to ≤ 7 and labelled `1e-n` |
| 6 | First draft of `Exports.cpp` had a dead placeholder loop and a hack that built a whole CSV writer to format one number | Self-review right after writing | Replaced with `formatNumber()` using `std::to_chars` |
| 7 | Served UI files with httplib's `set_mount_point`, which opens files via narrow paths and would likely break on Windows in a non-ASCII folder | Design review of Windows path handling (not reproduced on Windows) | Own static handler using `std::filesystem::path`. Path-traversal requests checked: 404 |
| 8 | Wrote an **untested** claim into `data/PROVENANCE.md` ("rendering stays interactive up to ~1 M points…") | Re-read against what was actually measured | Replaced with the measured parser limits and an explicit "not benchmarked" |
| 9 | Report draft quoted a thread start-up cost ("~50 µs") that was never measured, and said CI "builds with MSVC" before it had ever run | Re-read against the evidence | Reworded to what is known |
| 10 | Benchmark form had an input named `elements`. In HTML a named input shadows the form's own `.elements` collection, so the "Run benchmark" button threw a TypeError and **silently did nothing** | Scripted UI walkthrough: no request reached the server. Diagnosed by evaluating the form in headless Chrome ("f.elements is not iterable") | Renamed the field; added a global handler so any unexpected UI exception shows in the status bar instead of failing silently. API tests alone missed this, so clicking through the real UI matters |

**A near-miss worth recording (not a code bug).** In an API test, cancelling a running benchmark seemed to do nothing: the cancel call took 5 s and the job finished normally. Before changing any code, the assistant ran the JobManager in isolation (it cancelled correctly at case 124/1000), then repeated the HTTP call from Python (it cancelled within 0.2 s). The cause was the test command: `curl -X POST` with no body sends no `Content-Length`, so the server waited 5 s for a body. The browser always sends one. No code was changed. Changing the code would have "fixed" a bug that did not exist.

## 4. Candidate's own edits and review *(to be completed by the candidate)*

List what you changed by hand after the AI output, and what you checked yourself. Examples of useful entries: a function you rewrote, a value you recomputed on paper, a UI behaviour you tested on Windows, something you rejected.

| Date | File / feature | What you changed or verified | Why |
|---|---|---|---|
| | | | |

## 5. Deliberate challenge test *(to be completed by the candidate)*

The brief asks for at least one incorrect suggestion you identified yourself, or a deliberate challenge test. Suggested challenge (takes 10 minutes):

1. Ask an AI assistant: *"For this rod problem, is checking FE error at the nodes enough to verify convergence?"*
2. Compare its answer with `results/fea_convergence.csv`: nodal error ≈ 1e-13 for every N, midpoint error = q·h²/(8k).
3. Record the prompt, the answer, and whether it was right, here.

| Prompt | AI answer (short) | Correct? | How you checked |
|---|---|---|---|
| | | | |

## 6. Independent verification used

- **Analytical checks:** midpoint error equals q·h²/(8k) to 1e-9; order 2.000; nodal exactness; gradients hand-computed.
- **Unit tests:** 53/53 passing, including negative tests for every reader and parameter.
- **Numerical cross-checks:** central differences vs analytic gradients; serial vs parallel (0 °C difference); pinned generator output.
- **End-to-end:** `Launch --smoke-test`, curl/Python API tests, screenshots of every view.
- **Still to do by the candidate:** double-click test on a Windows 11 PC, and the handwritten derivation (`docs/handwritten/`).
