// Tiny SVG line-plot writer for the report figures in results/.
// Supports linear or log10 axes, solid/dashed lines, markers and a legend.
// Deliberately minimal - the interactive plots live in the web UI.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace wb::tools {

struct Series {
    std::string label;
    std::vector<double> x, y;
    std::string color = "#2a78d6";
    bool dashed = false;
    bool markers = false;
    bool line = true;
};

struct PlotSpec {
    std::string title, subtitle, xLabel, yLabel;
    bool logX = false, logY = false;
    std::vector<Series> series;
    int width = 760, height = 460;
};

namespace detail {

inline std::string fmt(double v) {
    char buf[32];
    const double a = std::abs(v);
    if (v == 0.0) return "0";
    if (a >= 1e5 || a < 1e-3) std::snprintf(buf, sizeof(buf), "%.0e", v);
    else std::snprintf(buf, sizeof(buf), "%g", v);
    return buf;
}

inline std::string esc(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '&') out += "&amp;";
        else out += c;
    }
    return out;
}

// About 5-7 "nice" ticks (1, 2, 5 x 10^n) covering [lo, hi].
inline std::vector<double> linearTicks(double lo, double hi) {
    const double span = hi - lo;
    const double raw = span / 6.0;
    const double mag = std::pow(10.0, std::floor(std::log10(raw)));
    double step = mag;
    for (double m : {1.0, 2.0, 5.0, 10.0}) {
        if (m * mag >= raw) { step = m * mag; break; }
    }
    std::vector<double> t;
    for (double v = std::ceil(lo / step) * step; v <= hi + 1e-9 * span; v += step) t.push_back(std::abs(v) < 1e-12 * span ? 0.0 : v);
    return t;
}

// One tick per decade, thinned to at most ~7 labels for very wide ranges.
inline std::vector<double> logTicks(double lo, double hi) {
    const int e0 = static_cast<int>(std::ceil(std::log10(lo) - 1e-9));
    const int e1 = static_cast<int>(std::floor(std::log10(hi) + 1e-9));
    const int stride = std::max(1, (e1 - e0 + 7) / 7);
    std::vector<double> t;
    for (int e = e1; e >= e0; e -= stride) t.insert(t.begin(), std::pow(10.0, e));
    return t;
}

}  // namespace detail

inline std::string renderSvg(const PlotSpec& spec) {
    using namespace detail;
    const double left = 78, right = 24, top = spec.subtitle.empty() ? 52 : 70, bottom = 62;
    const double pw = spec.width - left - right, ph = spec.height - top - bottom;

    double xmin = 1e300, xmax = -1e300, ymin = 1e300, ymax = -1e300;
    for (const auto& s : spec.series) {
        for (std::size_t i = 0; i < s.x.size(); ++i) {
            if ((spec.logX && s.x[i] <= 0) || (spec.logY && s.y[i] <= 0)) continue;
            xmin = std::min(xmin, s.x[i]); xmax = std::max(xmax, s.x[i]);
            ymin = std::min(ymin, s.y[i]); ymax = std::max(ymax, s.y[i]);
        }
    }
    if (spec.logX) { xmin = std::pow(10, std::floor(std::log10(xmin))); xmax = std::pow(10, std::ceil(std::log10(xmax))); }
    if (spec.logY) { ymin = std::pow(10, std::floor(std::log10(ymin))); ymax = std::pow(10, std::ceil(std::log10(ymax))); }
    if (!spec.logY) { const double pad = 0.05 * (ymax - ymin); ymin -= pad; ymax += pad; }
    if (xmax == xmin) { xmin -= 1; xmax += 1; }
    if (ymax == ymin) { ymin -= 1; ymax += 1; }

    auto tx = [&](double v) {
        const double f = spec.logX ? (std::log10(v) - std::log10(xmin)) / (std::log10(xmax) - std::log10(xmin)) : (v - xmin) / (xmax - xmin);
        return left + f * pw;
    };
    auto ty = [&](double v) {
        const double f = spec.logY ? (std::log10(v) - std::log10(ymin)) / (std::log10(ymax) - std::log10(ymin)) : (v - ymin) / (ymax - ymin);
        return top + ph - f * ph;
    };

    std::ostringstream o;
    o << "<svg xmlns='http://www.w3.org/2000/svg' width='" << spec.width << "' height='" << spec.height
      << "' viewBox='0 0 " << spec.width << ' ' << spec.height << "' font-family='Segoe UI, Helvetica, Arial, sans-serif'>\n"
      << "<rect width='100%' height='100%' fill='#fcfcfb'/>\n"
      << "<text x='" << left << "' y='26' font-size='17' font-weight='600' fill='#0b0b0b'>" << esc(spec.title) << "</text>\n";
    if (!spec.subtitle.empty()) {
        o << "<text x='" << left << "' y='46' font-size='12.5' fill='#52514e'>" << esc(spec.subtitle) << "</text>\n";
    }

    // Grid and ticks - kept light so the data reads first.
    const auto xt = spec.logX ? logTicks(xmin, xmax) : linearTicks(xmin, xmax);
    const auto yt = spec.logY ? logTicks(ymin, ymax) : linearTicks(ymin, ymax);
    for (double v : xt) {
        o << "<line x1='" << tx(v) << "' x2='" << tx(v) << "' y1='" << top << "' y2='" << top + ph
          << "' stroke='#e6e5e0' stroke-width='1'/>\n"
          << "<text x='" << tx(v) << "' y='" << top + ph + 18 << "' font-size='12' fill='#52514e' text-anchor='middle'>"
          << fmt(v) << "</text>\n";
    }
    for (double v : yt) {
        o << "<line x1='" << left << "' x2='" << left + pw << "' y1='" << ty(v) << "' y2='" << ty(v)
          << "' stroke='#e6e5e0' stroke-width='1'/>\n"
          << "<text x='" << left - 8 << "' y='" << ty(v) + 4 << "' font-size='12' fill='#52514e' text-anchor='end'>"
          << fmt(v) << "</text>\n";
    }
    o << "<line x1='" << left << "' x2='" << left + pw << "' y1='" << top + ph << "' y2='" << top + ph
      << "' stroke='#8a8984' stroke-width='1'/>\n";
    o << "<text x='" << left + pw / 2 << "' y='" << spec.height - 18 << "' font-size='13' fill='#0b0b0b' text-anchor='middle'>"
      << esc(spec.xLabel) << "</text>\n";
    o << "<text transform='translate(18," << top + ph / 2 << ") rotate(-90)' font-size='13' fill='#0b0b0b' text-anchor='middle'>"
      << esc(spec.yLabel) << "</text>\n";

    for (const auto& s : spec.series) {
        if (s.line && s.x.size() > 1) {
            o << "<polyline fill='none' stroke='" << s.color << "' stroke-width='2' stroke-linejoin='round'"
              << (s.dashed ? " stroke-dasharray='6 4'" : "") << " points='";
            for (std::size_t i = 0; i < s.x.size(); ++i) {
                if ((spec.logX && s.x[i] <= 0) || (spec.logY && s.y[i] <= 0)) continue;
                o << tx(s.x[i]) << ',' << ty(s.y[i]) << ' ';
            }
            o << "'/>\n";
        }
        if (s.markers) {
            for (std::size_t i = 0; i < s.x.size(); ++i) {
                if ((spec.logX && s.x[i] <= 0) || (spec.logY && s.y[i] <= 0)) continue;
                o << "<circle cx='" << tx(s.x[i]) << "' cy='" << ty(s.y[i]) << "' r='4.5' fill='" << s.color
                  << "' stroke='#fcfcfb' stroke-width='2'/>\n";
            }
        }
    }

    // Legend (top-right, inside the plot area) whenever there is more than one series.
    if (spec.series.size() > 1) {
        std::size_t longest = 0;
        for (const auto& s : spec.series) longest = std::max(longest, s.label.size());
        const double lw = 40 + 6.6 * static_cast<double>(longest), lh = 20.0 * static_cast<double>(spec.series.size()) + 10;
        const double lx = left + pw - lw - 8, ly = top + 8;
        o << "<rect x='" << lx << "' y='" << ly << "' width='" << lw << "' height='" << lh
          << "' fill='#fcfcfb' fill-opacity='0.92' stroke='#e6e5e0' rx='4'/>\n";
        for (std::size_t i = 0; i < spec.series.size(); ++i) {
            const auto& s = spec.series[i];
            const double y = ly + 18 + 20.0 * static_cast<double>(i);
            o << "<line x1='" << lx + 8 << "' x2='" << lx + 30 << "' y1='" << y - 4 << "' y2='" << y - 4 << "' stroke='"
              << s.color << "' stroke-width='2'" << (s.dashed ? " stroke-dasharray='6 4'" : "") << "/>\n";
            if (s.markers) o << "<circle cx='" << lx + 19 << "' cy='" << y - 4 << "' r='4' fill='" << s.color << "'/>\n";
            o << "<text x='" << lx + 36 << "' y='" << y << "' font-size='12' fill='#0b0b0b'>" << esc(s.label) << "</text>\n";
        }
    }
    o << "</svg>\n";
    return o.str();
}

}  // namespace wb::tools
