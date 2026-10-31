#include "TestHarness.h"

#include "core/common/Errors.h"
#include "core/io/NodalCsvReader.h"
#include "core/io/StlReader.h"
#include "core/io/TextFile.h"

#include <cstdio>
#include <string>

using namespace wb;
using namespace wb::io;

namespace {

const char* kTwoFacets =
    "solid plate\n"
    "  facet normal 0 0 1\n"
    "    outer loop\n"
    "      vertex 0 0 0\n"
    "      vertex 1 0 0\n"
    "      vertex 1 1 0\n"
    "    endloop\n"
    "  endfacet\n"
    "  FACET NORMAL 0 0 0\n"  // upper case and zero normal both show up in real exports
    "    OUTER LOOP\n"
    "      VERTEX 0 0 0\n"
    "      VERTEX 1 1 0\n"
    "      VERTEX 0 1 0\n"
    "    ENDLOOP\n"
    "  ENDFACET\n"
    "endsolid plate\n";

std::string facet(const std::string& v3) {
    return "solid s\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 1 0 0\nvertex " + v3 +
           "\nendloop\nendfacet\nendsolid s\n";
}

}  // namespace

// ---------------------------------------------------------------- STL

TEST(stl_parses_valid_file) {
    const StlMesh m = parseAsciiStl(kTwoFacets);
    CHECK(m.name == "plate");
    CHECK(m.triangleCount() == 2);
    CHECK(m.vertices.size() == 6);
    CHECK(m.bounds.max.x == 1.0 && m.bounds.max.y == 1.0 && m.bounds.min.z == 0.0);
    CHECK_NEAR(m.normals[1].z, 1.0, 1e-12);  // recomputed from winding
    CHECK(m.degenerateTriangles == 0);
}

TEST(stl_accepts_crlf_scientific_and_multiple_solids) {
    std::string text = facet("1e0 1.0E+00 -0.0") + facet("0 1 0");
    for (std::size_t i = 0; (i = text.find('\n', i)) != std::string::npos; i += 2) text.insert(i, "\r");
    const StlMesh m = parseAsciiStl(text);
    CHECK(m.triangleCount() == 2);
    CHECK(m.solidCount == 2);
}

TEST(stl_counts_degenerate_triangles) {
    const StlMesh m = parseAsciiStl(facet("2 0 0"));  // all three points on a line
    CHECK(m.degenerateTriangles == 1);
}

TEST(stl_rejects_empty_and_facetless_files) {
    CHECK_THROWS_CODE(parseAsciiStl(""), ErrorCode::EmptyFile);
    CHECK_THROWS_CODE(parseAsciiStl("  \n\t\n"), ErrorCode::EmptyFile);
    CHECK_THROWS_CODE(parseAsciiStl("solid empty\nendsolid empty\n"), ErrorCode::EmptyFile);
}

TEST(stl_rejects_non_finite_coordinates) {
    CHECK_THROWS_CODE(parseAsciiStl(facet("nan 0 0")), ErrorCode::MalformedFile);
    CHECK_THROWS_CODE(parseAsciiStl(facet("inf 0 0")), ErrorCode::MalformedFile);
    CHECK_THROWS_CODE(parseAsciiStl(facet("1e999 0 0")), ErrorCode::MalformedFile);
}

TEST(stl_rejects_structural_errors) {
    CHECK_THROWS_CODE(parseAsciiStl(facet("abc 0 0")), ErrorCode::MalformedFile);
    CHECK_THROWS_CODE(parseAsciiStl(facet("1 1")), ErrorCode::MalformedFile);  // missing coordinate
    CHECK_THROWS_CODE(parseAsciiStl("solid s\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 1 0 0\nendloop\nendfacet\nendsolid\n"),
                      ErrorCode::MalformedFile);  // only two vertices
    CHECK_THROWS_CODE(parseAsciiStl("solid s\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\n"),
                      ErrorCode::MalformedFile);  // truncated
    CHECK_THROWS_CODE(parseAsciiStl("hello world\n"), ErrorCode::MalformedFile);
}

TEST(stl_error_message_has_line_number) {
    try {
        parseAsciiStl("solid s\nfacet normal 0 0 1\nouter loop\nvertex 0 0 x\n");
        CHECK(false);
    } catch (const WorkbenchError& e) {
        CHECK(std::string(e.what()).find("line 4") != std::string::npos);
    }
}

TEST(stl_rejects_binary_stl) {
    std::string bin = "solid binary-header-pretending-to-be-ascii";
    bin.resize(84, '\0');
    bin += std::string(50, '\x01');
    CHECK_THROWS_CODE(parseAsciiStl(bin), ErrorCode::MalformedFile);
}

TEST(stl_enforces_triangle_limit) {
    ReadLimits tight;
    tight.maxRecords = 1;
    CHECK_THROWS_CODE(parseAsciiStl(kTwoFacets, tight), ErrorCode::MalformedFile);
}

// ---------------------------------------------------------------- nodal CSV

TEST(csv_parses_valid_file_with_comments_and_bom) {
    const std::string text =
        "\xEF\xBB\xBF# SYNTHETIC test data\n"
        "node_id,x_m,y_m,z_m,temperature_C\r\n"
        "1,0,0,0,20.5\r\n"
        "2, 0.1 ,0,0,35\r\n"
        "\n"
        "7,0.1,0.2,0.3,-4.25e1\r\n";
    const NodalDataset d = parseNodalCsv(text);
    CHECK(d.nodes.size() == 3);
    CHECK(d.comments.size() == 1 && d.comments[0] == "SYNTHETIC test data");
    CHECK(d.nodes[2].nodeId == 7);
    CHECK_NEAR(d.minTemperature_C, -42.5, 0.0);
    CHECK_NEAR(d.maxTemperature_C, 35.0, 0.0);
    CHECK_NEAR(d.bounds_m.max.z, 0.3, 0.0);
}

TEST(csv_rejects_empty_and_header_only) {
    CHECK_THROWS_CODE(parseNodalCsv(""), ErrorCode::EmptyFile);
    CHECK_THROWS_CODE(parseNodalCsv("# just a comment\n"), ErrorCode::EmptyFile);
    CHECK_THROWS_CODE(parseNodalCsv("node_id,x_m,y_m,z_m,temperature_C\n"), ErrorCode::EmptyFile);
}

TEST(csv_rejects_wrong_header) {
    CHECK_THROWS_CODE(parseNodalCsv("id,x,y,z,T\n1,0,0,0,1\n"), ErrorCode::MalformedFile);
    CHECK_THROWS_CODE(parseNodalCsv("node_id,x_m,y_m,z_m\n1,0,0,0\n"), ErrorCode::MalformedFile);
    CHECK_THROWS_CODE(parseNodalCsv("node_id,x_mm,y_mm,z_mm,temperature_C\n1,0,0,0,1\n"), ErrorCode::MalformedFile);
}

TEST(csv_rejects_bad_rows) {
    const std::string h = "node_id,x_m,y_m,z_m,temperature_C\n";
    CHECK_THROWS_CODE(parseNodalCsv(h + "1,0,0,0\n"), ErrorCode::MalformedFile);           // too few
    CHECK_THROWS_CODE(parseNodalCsv(h + "1,0,0,0,1,9\n"), ErrorCode::MalformedFile);       // too many
    CHECK_THROWS_CODE(parseNodalCsv(h + "1,0,0,0,NaN\n"), ErrorCode::MalformedFile);
    CHECK_THROWS_CODE(parseNodalCsv(h + "1,inf,0,0,1\n"), ErrorCode::MalformedFile);
    CHECK_THROWS_CODE(parseNodalCsv(h + "1,0,0,0,hot\n"), ErrorCode::MalformedFile);
    CHECK_THROWS_CODE(parseNodalCsv(h + "1,0,0,0,\n"), ErrorCode::MalformedFile);          // blank value
    CHECK_THROWS_CODE(parseNodalCsv(h + "1.5,0,0,0,1\n"), ErrorCode::MalformedFile);       // non-integer id
    CHECK_THROWS_CODE(parseNodalCsv(h + "0,0,0,0,1\n"), ErrorCode::MalformedFile);         // id must be >= 1
    CHECK_THROWS_CODE(parseNodalCsv(h + "1,0,0,0,1\n1,1,1,1,2\n"), ErrorCode::MalformedFile);  // duplicate
    CHECK_THROWS_CODE(parseNodalCsv(h + "1,0,0,0,-300\n"), ErrorCode::MalformedFile);      // below 0 K
    CHECK_THROWS_CODE(parseNodalCsv(h + "\"1\",0,0,0,1\n"), ErrorCode::MalformedFile);     // quotes
}

// ---------------------------------------------------------------- files

TEST(io_atomic_write_then_read_back) {
    const std::string path = "wb_test_roundtrip.csv";
    writeTextFileAtomic(path, "node_id,x_m,y_m,z_m,temperature_C\n1,0,0,0,25\n");
    const NodalDataset d = readNodalCsvFile(path);
    CHECK(d.nodes.size() == 1);
    std::remove(path.c_str());
}

TEST(io_missing_file_is_reported) {
    CHECK_THROWS_CODE(readAsciiStlFile("does/not/exist.stl"), ErrorCode::MalformedFile);
}

TEST(io_parse_double_is_strict) {
    double v = 0.0;
    CHECK(parseDouble("1.5", v) && v == 1.5);
    CHECK(parseDouble("-2e-3", v) && v == -2e-3);
    CHECK(!parseDouble("1.5abc", v));
    CHECK(!parseDouble("1,5", v));  // European decimal comma is not accepted
    CHECK(!parseDouble("", v));
}
