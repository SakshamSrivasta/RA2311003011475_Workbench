// Extra SVG figure types for the solver study: a temperature slice (cells
// coloured by a one-hue sequential ramp with a labelled colour bar) and a
// sparsity "spy" plot. Same look as SvgPlot.h.
#pragma once

#include "SvgPlot.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

namespace wb::tools {

// One warm hue, light -> dark, for temperature magnitude.
inline std::string warmRamp(double t) {
    static const double stops[][3] = {{253, 238, 228}, {246, 190, 156}, {235, 104, 52}, {176, 64, 20}, {104, 34, 8}};
    t = std::clamp(t, 0.0, 1.0) * 4.0;
    const int i = std::min(3, static_cast<int>(t));
    const double f = t - i;
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", static_cast<int>(std::lround(stops[i][0] + f * (stops[i + 1][0] - stops[i][0]))),
                  static_cast<int>(std::lround(stops[i][1] + f * (stops[i + 1][1] - stops[i][1]))),
                  static_cast<int>(std::lround(stops[i][2] + f * (stops[i + 1][2] - stops[i][2]))));
    return buf;
}

struct HeatCell {
    double u0, u1, v0, v1;  // physical extent
    double value;
};

struct HeatmapSpec {
    std::string title, subtitle, uLabel, vLabel, unit;
    std::vector<HeatCell> cells;
    double scale = 1.0;  // multiply coordinates for the axis labels (e.g. 1e3 for mm)
    int width = 760;
};

inline std::string renderHeatmap(const HeatmapSpec& spec) {
    using namespace detail;
    double u0 = 1e300, u1 = -1e300, v0 = 1e300, v1 = -1e300, lo = 1e300, hi = -1e300;
    for (const auto& c : spec.cells) {
        u0 = std::min(u0, c.u0); u1 = std::max(u1, c.u1);
        v0 = std::min(v0, c.v0); v1 = std::max(v1, c.v1);
        lo = std::min(lo, c.value); hi = std::max(hi, c.value);
    }
    const double left = 70, right = 110, top = spec.subtitle.empty() ? 52 : 70, bottom = 56;
    const double pw = spec.width - left - right;
    const double ph = pw * (v1 - v0) / (u1 - u0);  // equal aspect: geometry is not distorted
    const int height = static_cast<int>(top + ph + bottom);
    auto tx = [&](double u) { return left + (u - u0) / (u1 - u0) * pw; };
    auto ty = [&](double v) { return top + ph - (v - v0) / (v1 - v0) * ph; };

    std::ostringstream o;
    o << "<svg xmlns='http://www.w3.org/2000/svg' width='" << spec.width << "' height='" << height << "' viewBox='0 0 "
      << spec.width << ' ' << height << "' font-family='Segoe UI, Helvetica, Arial, sans-serif'>\n"
      << "<rect width='100%' height='100%' fill='#fcfcfb'/>\n"
      << "<text x='" << left << "' y='26' font-size='17' font-weight='600' fill='#0b0b0b'>" << esc(spec.title) << "</text>\n";
    if (!spec.subtitle.empty())
        o << "<text x='" << left << "' y='46' font-size='12.5' fill='#52514e'>" << esc(spec.subtitle) << "</text>\n";
    for (const auto& c : spec.cells) {
        const double t = hi > lo ? (c.value - lo) / (hi - lo) : 0.5;
        // Slight overlap hides anti-aliasing seams between neighbouring cells.
        o << "<rect x='" << tx(c.u0) << "' y='" << ty(c.v1) << "' width='" << tx(c.u1) - tx(c.u0) + 0.4 << "' height='"
          << ty(c.v0) - ty(c.v1) + 0.4 << "' fill='" << warmRamp(t) << "'/>\n";
    }
    for (double v : linearTicks(u0 * spec.scale, u1 * spec.scale)) {
        o << "<text x='" << tx(v / spec.scale) << "' y='" << top + ph + 18 << "' font-size='12' fill='#52514e' text-anchor='middle'>"
          << fmt(v) << "</text>\n";
    }
    for (double v : linearTicks(v0 * spec.scale, v1 * spec.scale)) {
        o << "<text x='" << left - 8 << "' y='" << ty(v / spec.scale) + 4 << "' font-size='12' fill='#52514e' text-anchor='end'>"
          << fmt(v) << "</text>\n";
    }
    o << "<text x='" << left + pw / 2 << "' y='" << height - 16 << "' font-size='13' fill='#0b0b0b' text-anchor='middle'>"
      << esc(spec.uLabel) << "</text>\n"
      << "<text transform='translate(20," << top + ph / 2 << ") rotate(-90)' font-size='13' fill='#0b0b0b' text-anchor='middle'>"
      << esc(spec.vLabel) << "</text>\n";
    // Colour bar with min / max labels.
    const double bx = left + pw + 24, bw = 16, bh = std::min(ph, 260.0);
    for (int i = 0; i < 64; ++i) {
        o << "<rect x='" << bx << "' y='" << top + bh - (i + 1) * bh / 64 << "' width='" << bw << "' height='" << bh / 64 + 0.5
          << "' fill='" << warmRamp(i / 63.0) << "'/>\n";
    }
    char buf[64];
    o << "<text x='" << bx << "' y='" << top - 8 << "' font-size='11' fill='#52514e'>this plane</text>\n";
    std::snprintf(buf, sizeof(buf), "%.1f %s", hi, spec.unit.c_str());
    o << "<text x='" << bx + bw + 6 << "' y='" << top + 10 << "' font-size='12' fill='#0b0b0b'>" << esc(buf) << "</text>\n";
    std::snprintf(buf, sizeof(buf), "%.1f %s", lo, spec.unit.c_str());
    o << "<text x='" << bx + bw + 6 << "' y='" << top + bh << "' font-size='12' fill='#0b0b0b'>" << esc(buf) << "</text>\n";
    o << "</svg>\n";
    return o.str();
}

// Spy plot: which (row, column) bins hold non-zeros. `envelope` optionally
// gives, per row, the first column of the skyline factor, drawn as a shaded
// band so the fill-in a direct solver must store is visible.
struct SpySpec {
    std::string title, subtitle;
    std::size_t n = 0;
    std::vector<std::pair<std::size_t, std::size_t>> entries;  // (row, col)
    std::vector<std::size_t> envelopeFirst;                    // size n or empty
    int size = 360;
};

inline std::string renderSpy(const SpySpec& spec) {
    using namespace detail;
    const int bins = 180;
    const double left = 20, top = spec.subtitle.empty() ? 44 : 60, side = spec.size - 2 * left;
    const double cell = side / bins;
    std::vector<int> hits(static_cast<std::size_t>(bins) * bins, 0);
    const double scale = static_cast<double>(bins) / static_cast<double>(spec.n);
    for (const auto& [r, c] : spec.entries) {
        const int br = std::min(bins - 1, static_cast<int>(static_cast<double>(r) * scale));
        const int bc = std::min(bins - 1, static_cast<int>(static_cast<double>(c) * scale));
        ++hits[static_cast<std::size_t>(br) * bins + bc];
    }
    const int height = static_cast<int>(top + side + 16);
    std::ostringstream o;
    o << "<svg xmlns='http://www.w3.org/2000/svg' width='" << spec.size << "' height='" << height << "' viewBox='0 0 "
      << spec.size << ' ' << height << "' font-family='Segoe UI, Helvetica, Arial, sans-serif'>\n"
      << "<rect width='100%' height='100%' fill='#fcfcfb'/>\n"
      << "<text x='" << left << "' y='24' font-size='15' font-weight='600' fill='#0b0b0b'>" << esc(spec.title) << "</text>\n";
    if (!spec.subtitle.empty())
        o << "<text x='" << left << "' y='42' font-size='11.5' fill='#52514e'>" << esc(spec.subtitle) << "</text>\n";
    o << "<rect x='" << left << "' y='" << top << "' width='" << side << "' height='" << side
      << "' fill='none' stroke='#c3c2b7'/>\n";
    if (!spec.envelopeFirst.empty()) {
        // Lower envelope polygon: from the diagonal out to the first column, row by row.
        o << "<polygon fill='#cde2fb' stroke='none' points='";
        const std::size_t step = std::max<std::size_t>(1, spec.n / 400);
        const double perRow = side / static_cast<double>(spec.n);
        for (std::size_t i = 0; i < spec.n; i += step)
            o << left + static_cast<double>(spec.envelopeFirst[i]) * perRow << ',' << top + static_cast<double>(i) * perRow << ' ';
        o << left + side << ',' << top + side << ' ';
        for (std::size_t i = spec.n; i > 0; i -= std::min(i, step))
            o << left + static_cast<double>(i) * perRow << ',' << top + static_cast<double>(i) * perRow << ' ';
        o << "'/>\n";
    }
    for (int r = 0; r < bins; ++r) {
        for (int c = 0; c < bins; ++c) {
            if (!hits[static_cast<std::size_t>(r) * bins + c]) continue;
            o << "<rect x='" << left + c * cell << "' y='" << top + r * cell << "' width='" << cell << "' height='" << cell
              << "' fill='#0d366b'/>\n";
        }
    }
    o << "</svg>\n";
    return o.str();
}

}  // namespace wb::tools
