#pragma once

#include "core/io/Geometry.h"

#include <string>
#include <string_view>

namespace wb::io {

// Reads a whole file into memory. Throws WorkbenchError with a message the
// user can act on (missing file, too large, empty).
std::string readTextFile(const std::string& path, const ReadLimits& limits = {});

// Writes to "<path>.tmp" first and renames it over the target, so a crash or
// cancelled export never leaves a half-written CSV behind.
void writeTextFileAtomic(const std::string& path, std::string_view contents);

// Small helpers used by both parsers.
std::string_view trim(std::string_view s);
bool parseDouble(std::string_view token, double& out);  // whole token must be a number

}  // namespace wb::io
