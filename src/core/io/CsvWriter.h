#pragma once

#include <charconv>
#include <sstream>
#include <string>
#include <vector>

namespace wb::io {

// Shortest text that parses back to exactly the same double (std::to_chars
// guarantees the round trip), so exports lose no precision and stay readable.
inline std::string formatNumber(double v) {
    char buf[32];
    const auto r = std::to_chars(buf, buf + sizeof(buf), v);
    return std::string(buf, r.ptr);
}

// Builds CSV text in memory. Doubles go through formatNumber, so values read
// back bit-for-bit (the serial/parallel 1e-8 check relies on that).
// '#' comments go above the header, which pandas (comment='#') and our own
// nodal reader both skip.
class CsvWriter {
public:
    explicit CsvWriter(std::vector<std::string> header) : header_(std::move(header)) {}

    void comment(const std::string& text) { out_ << "# " << text << '\n'; }

    template <typename... Ts>
    void row(const Ts&... values) {
        ensureHeader();
        bool first = true;
        ((out_ << (first ? "" : ",") << cell(values), first = false), ...);
        out_ << '\n';
    }

    void writeRow(const std::vector<std::string>& cells) {
        ensureHeader();
        writeCells(cells);
    }

    std::string str() {
        ensureHeader();
        return out_.str();
    }

private:
    static std::string cell(double v) { return formatNumber(v); }
    template <typename T>
    static const T& cell(const T& v) { return v; }

    void ensureHeader() {
        if (headerWritten_) return;
        headerWritten_ = true;
        writeCells(header_);
    }

    void writeCells(const std::vector<std::string>& cells) {
        for (std::size_t i = 0; i < cells.size(); ++i) out_ << (i ? "," : "") << cells[i];
        out_ << '\n';
    }

    std::vector<std::string> header_;
    bool headerWritten_ = false;
    std::ostringstream out_;
};

}  // namespace wb::io
