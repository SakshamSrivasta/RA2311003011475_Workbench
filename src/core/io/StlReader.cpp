#include "core/io/StlReader.h"

#include "core/common/Errors.h"
#include "core/io/TextFile.h"

#include <cctype>
#include <cmath>
#include <sstream>

namespace wb::io {

namespace {

// Walks the text one whitespace-separated token at a time and remembers the
// line number, so every error can point at the exact spot in the file.
class Tokenizer {
public:
    explicit Tokenizer(std::string_view text) : text_(text) {}

    bool next(std::string_view& token) {
        while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) {
            if (text_[pos_] == '\n') ++line_;
            ++pos_;
        }
        if (pos_ >= text_.size()) return false;
        const std::size_t start = pos_;
        while (pos_ < text_.size() && !std::isspace(static_cast<unsigned char>(text_[pos_]))) ++pos_;
        token = text_.substr(start, pos_ - start);
        tokenLine_ = line_;
        return true;
    }

    // Rest of the current line (used for solid names, which may contain spaces).
    std::string_view restOfLine() {
        const std::size_t end = text_.find('\n', pos_);
        const std::size_t stop = (end == std::string_view::npos) ? text_.size() : end;
        std::string_view rest = trim(text_.substr(pos_, stop - pos_));
        pos_ = stop;
        return rest;
    }

    std::size_t line() const { return tokenLine_; }

private:
    std::string_view text_;
    std::size_t pos_ = 0;
    std::size_t line_ = 1;
    std::size_t tokenLine_ = 1;
};

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

[[noreturn]] void fail(std::size_t line, const std::string& what) {
    std::ostringstream msg;
    msg << "STL line " << line << ": " << what;
    throw WorkbenchError(ErrorCode::MalformedFile, msg.str());
}

// Binary STL is an 80-byte header followed by raw floats, so it is full of
// NUL and high bytes. Some exporters even put "solid" in the binary header,
// which is why checking the first word alone is not enough.
bool looksBinary(std::string_view text) {
    const std::size_t n = std::min<std::size_t>(text.size(), 1024);
    for (std::size_t i = 0; i < n; ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c == 0 || (c < 32 && c != '\n' && c != '\r' && c != '\t') || c > 126) return true;
    }
    return false;
}

class StlParser {
public:
    StlParser(std::string_view text, const ReadLimits& limits) : tok_(text), limits_(limits) {}

    StlMesh run() {
        std::string_view t;
        while (tok_.next(t)) {
            if (!equalsIgnoreCase(t, "solid")) {
                fail(tok_.line(), "expected 'solid' but found '" + std::string(t) + "'.");
            }
            ++mesh_.solidCount;
            const std::string_view name = tok_.restOfLine();
            if (mesh_.name.empty()) mesh_.name = std::string(name);
            parseSolidBody();
        }
        if (mesh_.solidCount == 0) {
            throw WorkbenchError(ErrorCode::EmptyFile, "STL file contains only whitespace. Pick a file with facets.");
        }
        if (mesh_.triangleCount() == 0) {
            throw WorkbenchError(ErrorCode::EmptyFile,
                                 "STL file has a 'solid' block but no facets, so there is nothing to draw. "
                                 "Re-export the part from CAD.");
        }
        return std::move(mesh_);
    }

private:
    std::string_view expectToken(const char* context) {
        std::string_view t;
        if (!tok_.next(t)) fail(tok_.line(), std::string("file ended early while reading ") + context + ".");
        return t;
    }

    void expectKeyword(const char* keyword) {
        const std::string_view t = expectToken(keyword);
        if (!equalsIgnoreCase(t, keyword)) {
            fail(tok_.line(), std::string("expected '") + keyword + "' but found '" + std::string(t) + "'.");
        }
    }

    double expectNumber(const char* what) {
        const std::string_view t = expectToken(what);
        double v = 0.0;
        if (!parseDouble(t, v)) {
            fail(tok_.line(), std::string("'") + std::string(t) + "' is not a valid number for " + what + ".");
        }
        if (!std::isfinite(v)) {
            fail(tok_.line(), std::string(what) + " is " + std::string(t) + "; NaN and Inf are not allowed.");
        }
        return v;
    }

    Vec3 expectVec3(const char* what) {
        Vec3 v;
        v.x = expectNumber(what);
        v.y = expectNumber(what);
        v.z = expectNumber(what);
        return v;
    }

    void parseSolidBody() {
        while (true) {
            const std::string_view t = expectToken("a facet or 'endsolid'");
            if (equalsIgnoreCase(t, "endsolid")) {
                tok_.restOfLine();  // optional name after endsolid
                return;
            }
            if (!equalsIgnoreCase(t, "facet")) {
                fail(tok_.line(), "expected 'facet' or 'endsolid' but found '" + std::string(t) + "'.");
            }
            parseFacet();
        }
    }

    void parseFacet() {
        if (mesh_.triangleCount() >= limits_.maxRecords) {
            std::ostringstream msg;
            msg << "STL has more than " << limits_.maxRecords
                << " triangles, which is above the tested limit. Decimate the mesh first.";
            throw WorkbenchError(ErrorCode::MalformedFile, msg.str());
        }
        expectKeyword("normal");
        Vec3 normal = expectVec3("facet normal");
        expectKeyword("outer");
        expectKeyword("loop");

        Vec3 v[3];
        for (int i = 0; i < 3; ++i) {
            const std::string_view t = expectToken("a vertex");
            if (!equalsIgnoreCase(t, "vertex")) {
                fail(tok_.line(), "facet needs exactly 3 vertices but found '" + std::string(t) + "' after " +
                                      std::to_string(i) + ".");
            }
            v[i] = expectVec3("vertex coordinate");
        }
        const std::string_view t = expectToken("'endloop'");
        if (equalsIgnoreCase(t, "vertex")) fail(tok_.line(), "facet has more than 3 vertices; only triangles are valid STL.");
        if (!equalsIgnoreCase(t, "endloop")) fail(tok_.line(), "expected 'endloop' but found '" + std::string(t) + "'.");
        expectKeyword("endfacet");

        // Use the winding-order normal when the exporter wrote 0 0 0 (common).
        const Vec3 e1{v[1].x - v[0].x, v[1].y - v[0].y, v[1].z - v[0].z};
        const Vec3 e2{v[2].x - v[0].x, v[2].y - v[0].y, v[2].z - v[0].z};
        const Vec3 cross{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
        const double area2 = std::sqrt(cross.x * cross.x + cross.y * cross.y + cross.z * cross.z);
        if (area2 == 0.0) ++mesh_.degenerateTriangles;
        const double nlen = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
        if (nlen == 0.0 && area2 > 0.0) normal = {cross.x / area2, cross.y / area2, cross.z / area2};

        mesh_.normals.push_back(normal);
        for (const Vec3& p : v) {
            mesh_.vertices.push_back(p);
            mesh_.bounds.expand(p);
        }
    }

    Tokenizer tok_;
    ReadLimits limits_;
    StlMesh mesh_;
};

}  // namespace

StlMesh parseAsciiStl(std::string_view text, const ReadLimits& limits) {
    if (text.size() > limits.maxBytes) {
        throw WorkbenchError(ErrorCode::MalformedFile, "STL file is larger than the import limit. Decimate the mesh first.");
    }
    if (trim(text).empty()) {
        throw WorkbenchError(ErrorCode::EmptyFile, "STL file is empty (0 facets). Pick a file exported with geometry.");
    }
    if (looksBinary(text)) {
        throw WorkbenchError(ErrorCode::MalformedFile,
                             "This looks like a binary STL. Only ASCII STL is supported - re-export it with "
                             "the 'ASCII' option in your CAD tool.");
    }
    return StlParser(text, limits).run();
}

StlMesh readAsciiStlFile(const std::string& path, const ReadLimits& limits) {
    return parseAsciiStl(readTextFile(path, limits), limits);
}

}  // namespace wb::io
