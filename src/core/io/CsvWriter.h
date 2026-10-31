#pragma once

#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace wb::io {

// Builds CSV text in memory. Doubles are written with max_digits10 so the
// exported numbers read back bit-for-bit (needed for the 1e-8 agreement check).
class CsvWriter {
public:
    explicit CsvWriter(const std::vector<std::string>& header) {
        out_ << std::setprecision(std::numeric_limits<double>::max_digits10);
        writeRow(header);
    }

    void comment(const std::string& text) { out_ << "# " << text << '\n'; }

    template <typename... Ts>
    void row(const Ts&... values) {
        bool first = true;
        ((out_ << (first ? "" : ",") << values, first = false), ...);
        out_ << '\n';
    }

    void writeRow(const std::vector<std::string>& cells) {
        for (std::size_t i = 0; i < cells.size(); ++i) out_ << (i ? "," : "") << cells[i];
        out_ << '\n';
    }

    std::string str() const { return out_.str(); }

private:
    std::ostringstream out_;
};

}  // namespace wb::io
