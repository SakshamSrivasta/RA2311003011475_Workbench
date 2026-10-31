#include "core/io/TextFile.h"

#include "core/common/Errors.h"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace wb::io {

std::string readTextFile(const std::string& path, const ReadLimits& limits) {
    std::error_code ec;
    const fs::path p = fs::u8path(path);
    if (!fs::exists(p, ec)) {
        throw WorkbenchError(ErrorCode::MalformedFile, "File not found: " + path + ". Check the path and try again.");
    }
    const auto size = fs::file_size(p, ec);
    if (ec) {
        throw WorkbenchError(ErrorCode::MalformedFile, "Cannot read the size of " + path + ": " + ec.message());
    }
    if (size > limits.maxBytes) {
        std::ostringstream msg;
        msg << "File is " << size / (1024 * 1024) << " MB, above the " << limits.maxBytes / (1024 * 1024)
            << " MB limit. Decimate or split the model before importing.";
        throw WorkbenchError(ErrorCode::MalformedFile, msg.str());
    }

    std::ifstream in(p, std::ios::binary);
    if (!in) {
        throw WorkbenchError(ErrorCode::MalformedFile, "Cannot open " + path + ". Is it open in another program?");
    }
    std::string text(static_cast<std::size_t>(size), '\0');
    in.read(text.data(), static_cast<std::streamsize>(size));
    text.resize(static_cast<std::size_t>(in.gcount()));
    return text;
}

void writeTextFileAtomic(const std::string& path, std::string_view contents) {
    const fs::path target = fs::u8path(path);
    fs::path tmp = target;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw WorkbenchError(ErrorCode::MalformedFile,
                                 "Cannot write " + path + ". Check that the folder exists and is writable.");
        }
        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        if (!out) throw WorkbenchError(ErrorCode::MalformedFile, "Disk write failed for " + path + ".");
    }
    std::error_code ec;
    fs::rename(tmp, target, ec);  // replaces an existing file on POSIX and Windows
    if (ec) {
        fs::remove(tmp, ec);
        throw WorkbenchError(ErrorCode::MalformedFile, "Could not replace " + path + ". Close it in Excel and retry.");
    }
}

std::string_view trim(std::string_view s) {
    const char* ws = " \t\r\n";
    const auto first = s.find_first_not_of(ws);
    if (first == std::string_view::npos) return {};
    const auto last = s.find_last_not_of(ws);
    return s.substr(first, last - first + 1);
}

bool parseDouble(std::string_view token, double& out) {
    if (token.empty() || token.size() > 64) return false;
    // strtod needs a NUL-terminated buffer. We never call setlocale(), so the
    // decimal separator is always '.' regardless of the user's Windows locale.
    char buf[65];
    token.copy(buf, token.size());
    buf[token.size()] = '\0';
    char* end = nullptr;
    errno = 0;
    const double v = std::strtod(buf, &end);
    if (end != buf + token.size()) return false;
    if (errno == ERANGE && (v == HUGE_VAL || v == -HUGE_VAL)) return false;
    out = v;
    return true;
}

}  // namespace wb::io
