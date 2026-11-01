# Dependencies, versions and licences

Everything is vendored in the repository; a build needs no package manager and no network. This file is the lock record: if a checksum changes, the dependency changed.

## Shipped in the source tree / application

| Component | Version | Where | Licence | Use | SHA-256 |
|---|---|---|---|---|---|
| cpp-httplib | 0.18.3 | `third_party/httplib/httplib.h` | MIT (`third_party/httplib/LICENSE`) | Local HTTP server in Launch.exe, client in `--smoke-test` | `a0a0c13dc086663863dbe6730a19716f7d3744904b6205496e8301750814dbd5` |
| nlohmann/json | 3.11.3 | `third_party/nlohmann/json.hpp` | MIT (`third_party/nlohmann/LICENSE.MIT`) | JSON in the transport layer only (not used by the core) | `9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6` |
| three.js | 0.160.0 | `src/web/vendor/three/three.module.min.js` | MIT (`src/web/vendor/three/LICENSE`) | 3D rendering in the Geometry view | `3e690ac7d180b0aadf0891bea39eec643e29e2d3e75c99b18689518665f69ba6` |
| three.js OrbitControls | 0.160.0 | `src/web/vendor/three/OrbitControls.js` | MIT (same) | Rotate / pan / zoom | `5a44a9e86a2a0fb11933eed69bc2cd33c76a496854c1aed6ed776efa87d7b064` |

Sources: GitHub tags `yhirose/cpp-httplib@v0.18.3` and `nlohmann/json@v3.11.3` (single_include); npm package `three@0.160.0` via jsDelivr.

## Statically linked into the Windows executables

Built with **llvm-mingw 20260922** (UCRT, clang 23.1.2), release from https://github.com/mstorsjo/llvm-mingw. `-static` links these runtime parts into the .exe, so no runtime DLLs are shipped:

| Component | Licence | Notice included |
|---|---|---|
| libc++, libc++abi, libunwind, compiler-rt (LLVM project) | Apache-2.0 WITH LLVM-exception (binary redistribution needs no notice under the exception; included anyway) | `third_party/licenses/llvm-project_LICENSE.txt` |
| mingw-w64 CRT start-up code and import libraries | ZPL 2.1 / public domain parts | `third_party/licenses/mingw-w64_COPYING.txt` |

The Universal C Runtime (`api-ms-win-crt-*.dll`) is part of Windows 10/11 and is not redistributed.

## Build tools (not shipped)

| Tool | Version used | Notes |
|---|---|---|
| CMake | 3.31.6 | minimum required 3.20 |
| Apple clang (macOS dev builds/tests) | 21.0.0 | |
| llvm-mingw | 20260922 | Windows cross-compiler for the packaged build |
| MSVC (CI) | whichever Visual Studio the `windows-latest` runner has | `.github/workflows/windows.yml` |

## Not used

No Python, Node or other runtime is needed to build or run. Example data and result plots are produced by the project's own C++ tools. The only exception is optional: `scripts/ui_walkthrough.py` (Python 3 standard library plus Google Chrome) regenerates the walkthrough screenshots.
