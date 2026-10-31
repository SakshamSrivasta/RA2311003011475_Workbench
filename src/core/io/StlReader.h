// ASCII STL reader.
//
// Expected grammar (keywords are case-insensitive, whitespace is free-form):
//   solid [name]
//     facet normal nx ny nz
//       outer loop
//         vertex x y z     (exactly three)
//       endloop
//     endfacet
//     ...
//   endsolid [name]
// Several solid blocks in one file are accepted (some exporters do this).
// STL carries no units; the caller decides what one unit means.
#pragma once

#include "core/io/Geometry.h"

#include <string>
#include <string_view>
#include <vector>

namespace wb::io {

struct StlMesh {
    std::string name;
    std::vector<Vec3> vertices;  // 3 per triangle, in file order
    std::vector<Vec3> normals;   // 1 per triangle (recomputed when the file has a zero normal)
    Bounds bounds;
    std::size_t degenerateTriangles = 0;  // zero-area facets, kept but counted
    std::size_t solidCount = 0;

    std::size_t triangleCount() const { return normals.size(); }
};

StlMesh parseAsciiStl(std::string_view text, const ReadLimits& limits = {});
StlMesh readAsciiStlFile(const std::string& path, const ReadLimits& limits = {});

}  // namespace wb::io
