#include "core/io/NodalCsvReader.h"

#include "core/common/Errors.h"
#include "core/io/TextFile.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <sstream>
#include <unordered_set>

namespace wb::io {

namespace {

[[noreturn]] void fail(std::size_t line, const std::string& what) {
    std::ostringstream msg;
    msg << "CSV line " << line << ": " << what;
    throw WorkbenchError(ErrorCode::MalformedFile, msg.str());
}

// Splits on commas. Quoted fields never appear in this schema, so a quote is
// treated as an error rather than half-supported.
std::size_t splitFields(std::string_view line, std::array<std::string_view, 6>& out) {
    std::size_t count = 0, start = 0;
    while (true) {
        const std::size_t comma = line.find(',', start);
        const std::string_view field = line.substr(start, comma == std::string_view::npos ? line.npos : comma - start);
        if (count < out.size()) out[count] = trim(field);
        ++count;
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }
    return count;
}

const char* kColumnNames[] = {"node_id", "x_m", "y_m", "z_m", "temperature_C"};

}  // namespace

NodalDataset parseNodalCsv(std::string_view text, const ReadLimits& limits) {
    if (text.size() > limits.maxBytes) {
        throw WorkbenchError(ErrorCode::MalformedFile, "CSV is larger than the import limit. Export fewer nodes.");
    }
    // Excel and some ANSYS exports add a UTF-8 byte-order mark.
    if (text.size() >= 3 && text.substr(0, 3) == "\xEF\xBB\xBF") text.remove_prefix(3);
    if (trim(text).empty()) {
        throw WorkbenchError(ErrorCode::EmptyFile,
                             std::string("CSV file is empty. Expected the header '") + kNodalCsvHeader +
                                 "' followed by one row per node.");
    }

    NodalDataset data;
    std::unordered_set<std::int64_t> seenIds;
    bool headerSeen = false;
    std::size_t lineNo = 0, pos = 0;
    data.minTemperature_C = std::numeric_limits<double>::max();
    data.maxTemperature_C = std::numeric_limits<double>::lowest();

    while (pos <= text.size()) {
        const std::size_t nl = text.find('\n', pos);
        const std::string_view raw = text.substr(pos, nl == std::string_view::npos ? text.npos : nl - pos);
        pos = (nl == std::string_view::npos) ? text.size() + 1 : nl + 1;
        ++lineNo;

        const std::string_view line = trim(raw);
        if (line.empty()) continue;
        if (line.front() == '#') {
            data.comments.emplace_back(trim(line.substr(1)));
            continue;
        }
        if (line.find('"') != std::string_view::npos) {
            fail(lineNo, "quoted fields are not supported. Save the CSV without quotes.");
        }

        std::array<std::string_view, 6> f;
        const std::size_t count = splitFields(line, f);

        if (!headerSeen) {
            bool ok = count == 5;
            for (std::size_t i = 0; ok && i < 5; ++i) ok = (f[i] == kColumnNames[i]);
            if (!ok) {
                fail(lineNo, "header must be exactly '" + std::string(kNodalCsvHeader) + "' but was '" +
                                 std::string(line) + "'. Rename the columns (units are part of the name).");
            }
            headerSeen = true;
            continue;
        }

        if (count != 5) {
            fail(lineNo, "expected 5 values but found " + std::to_string(count) + ".");
        }
        if (data.nodes.size() >= limits.maxRecords) {
            fail(lineNo, "more than " + std::to_string(limits.maxRecords) + " nodes, above the tested limit.");
        }

        NodalRecord rec;
        double idValue = 0.0;
        // node_id must be a plain positive integer; "12.5" or "1e3" are rejected.
        const bool idDigits = !f[0].empty() && std::all_of(f[0].begin(), f[0].end(), [](char c) { return c >= '0' && c <= '9'; });
        if (!idDigits || f[0].size() > 15 || !parseDouble(f[0], idValue) || idValue < 1.0) {
            fail(lineNo, "node_id '" + std::string(f[0]) + "' must be a positive whole number.");
        }
        rec.nodeId = static_cast<std::int64_t>(idValue);
        if (!seenIds.insert(rec.nodeId).second) {
            fail(lineNo, "node_id " + std::to_string(rec.nodeId) + " appears twice. Each node must be listed once.");
        }

        double values[4];
        for (std::size_t i = 1; i < 5; ++i) {
            if (!parseDouble(f[i], values[i - 1])) {
                fail(lineNo, std::string(kColumnNames[i]) + " value '" + std::string(f[i]) + "' is not a number.");
            }
            if (!std::isfinite(values[i - 1])) {
                fail(lineNo, std::string(kColumnNames[i]) + " is " + std::string(f[i]) +
                                 "; NaN and Inf are not valid results. Check the export.");
            }
        }
        rec.position_m = {values[0], values[1], values[2]};
        rec.temperature_C = values[3];
        if (rec.temperature_C < -273.15) {
            fail(lineNo, "temperature " + std::string(f[4]) + " C is below absolute zero.");
        }

        data.bounds_m.expand(rec.position_m);
        data.minTemperature_C = std::min(data.minTemperature_C, rec.temperature_C);
        data.maxTemperature_C = std::max(data.maxTemperature_C, rec.temperature_C);
        data.nodes.push_back(rec);
    }

    if (!headerSeen) {
        throw WorkbenchError(ErrorCode::EmptyFile,
                             std::string("CSV has only comments. Add the header '") + kNodalCsvHeader + "' and node rows.");
    }
    if (data.nodes.empty()) {
        throw WorkbenchError(ErrorCode::EmptyFile, "CSV has a valid header but no node rows, so there is nothing to plot.");
    }
    return data;
}

NodalDataset readNodalCsvFile(const std::string& path, const ReadLimits& limits) {
    return parseNodalCsv(readTextFile(path, limits), limits);
}

}  // namespace wb::io
