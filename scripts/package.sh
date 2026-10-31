#!/usr/bin/env bash
# Builds the Windows x64 release with llvm-mingw and assembles
#   dist/RA2311003011475_Workbench/            (folder that goes in the ZIP)
#   dist/RA2311003011475_Workbench.zip
#
# Packaging only: copies files and runs the normal CMake build. No backend
# logic lives here.
#
# Usage: LLVM_MINGW_ROOT=/path/to/llvm-mingw scripts/package.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NAME="RA2311003011475_Workbench"
DIST="$ROOT/dist"
OUT="$DIST/$NAME"
BUILD="$ROOT/build-win"

: "${LLVM_MINGW_ROOT:?Set LLVM_MINGW_ROOT to the extracted llvm-mingw release}"

echo "== Building Windows x64 release"
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/llvm-mingw-x86_64.cmake" -DLLVM_MINGW_ROOT="$LLVM_MINGW_ROOT" >/dev/null
cmake --build "$BUILD" --parallel

echo "== Assembling $OUT"
rm -rf "$OUT" "$DIST/$NAME.zip"
mkdir -p "$OUT/app" "$OUT/tools" "$OUT/vcs"

# The double-click entry point and the UI it serves.
cp "$BUILD/Launch.exe" "$OUT/Launch.exe"
cp -R "$ROOT/src/web" "$OUT/app/web"

# Command-line tools for reproducing tests/results on Windows without building.
cp "$BUILD/wb_tests.exe" "$BUILD/wb_results.exe" "$BUILD/wb_make_sample_data.exe" "$OUT/tools/"

# Source, data, evidence and docs.
for item in src tests data results docs cmake third_party scripts .vscode .github CMakeLists.txt README.md .gitignore; do
  cp -R "$ROOT/$item" "$OUT/$item"
done

# Version-control evidence: full history as a bundle plus a readable log.
git -C "$ROOT" bundle create "$OUT/vcs/RA2311003011475_Workbench.bundle" --all
git -C "$ROOT" log --reverse --stat --date=iso >"$OUT/vcs/commit_log.txt"
git -C "$ROOT" log --reverse --format='%h %ad %s' --date=short >"$OUT/vcs/commit_summary.txt"

# No macOS metadata or build leftovers in the archive.
find "$OUT" -name '.DS_Store' -delete
find "$OUT" -name '*.tmp' -delete

echo "== Zipping"
(cd "$DIST" && zip -qr -X "$NAME.zip" "$NAME")
ls -lh "$DIST/$NAME.zip"
echo "Done. Test on Windows: extract, double-click $NAME/Launch.exe"
