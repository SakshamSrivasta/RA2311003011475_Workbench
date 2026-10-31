// Small SVG line plot with linear/log axes, a legend, and a hover crosshair
// with a tooltip. Enough for the workbench without a charting dependency.
//
// spec = {
//   xLabel, yLabel, logX, logY, height,
//   series: [{ label, x: [], y: [], color: '--css-var' | '#hex', dashed, markers }],
//   empty: 'text shown when there is no data'
// }

import { cssVar, fmt } from './ui.js';

const plots = new Set();
const NS = 'http://www.w3.org/2000/svg';

export function linePlot(container, spec) {
  container._spec = spec;
  if (!container._observed) {
    container._observed = true;
    new ResizeObserver(() => render(container)).observe(container);
    plots.add(container);
  }
  render(container);
}

// Colours come from CSS tokens, so plots must redraw when the theme flips.
export function redrawAllPlots() {
  plots.forEach(render);
}

function color(c) {
  return c.startsWith('--') ? cssVar(c) : c;
}

function niceLinearTicks(lo, hi, count = 6) {
  const span = hi - lo || 1;
  const raw = span / count;
  const mag = 10 ** Math.floor(Math.log10(raw));
  const step = [1, 2, 5, 10].map((m) => m * mag).find((s) => s >= raw);
  const ticks = [];
  for (let v = Math.ceil(lo / step) * step; v <= hi + span * 1e-9; v += step) ticks.push(Math.abs(v) < span * 1e-12 ? 0 : v);
  return ticks;
}

// One tick per decade, thinned to at most ~7 labels for very wide ranges.
function logTicks(lo, hi) {
  const e0 = Math.ceil(Math.log10(lo) - 1e-9), e1 = Math.floor(Math.log10(hi) + 1e-9);
  const stride = Math.max(1, Math.ceil((e1 - e0 + 1) / 7));
  const ticks = [];
  for (let e = e1; e >= e0; e -= stride) ticks.unshift(10 ** e);
  return ticks;
}

function logLabel(v) {
  const e = Math.round(Math.log10(v));
  return Math.abs(e) >= 3 ? `1e${e}` : String(v);
}

function domain(values, log, pad) {
  // Plain loop: Math.min(...v) blows the call stack on 100k-node solutions.
  let lo = Infinity, hi = -Infinity;
  for (const x of values) {
    if (!Number.isFinite(x) || (log && x <= 0)) continue;
    if (x < lo) lo = x;
    if (x > hi) hi = x;
  }
  if (lo === Infinity) return [log ? 1 : 0, log ? 10 : 1];
  if (log) return [10 ** Math.floor(Math.log10(lo)), 10 ** Math.ceil(Math.log10(hi))];
  if (lo === hi) { lo -= 1; hi += 1; }
  const p = (hi - lo) * pad;
  return [lo - p, hi + p];
}

function el(name, attrs = {}, text) {
  const node = document.createElementNS(NS, name);
  for (const [k, v] of Object.entries(attrs)) node.setAttribute(k, v);
  if (text !== undefined) node.textContent = text;
  return node;
}

function render(container) {
  const spec = container._spec;
  if (!spec) return;
  container.replaceChildren();
  const series = (spec.series || []).filter((s) => s.x && s.x.length);
  if (!series.length) {
    const empty = document.createElement('div');
    empty.className = 'plot-empty';
    empty.textContent = spec.empty || 'No data yet.';
    container.append(empty);
    return;
  }

  if (series.length > 1) {
    const legend = document.createElement('div');
    legend.className = 'plot-legend';
    for (const s of series) {
      const item = document.createElement('span');
      const sw = el('svg', { viewBox: '0 0 22 10' });
      sw.append(el('line', { x1: 1, x2: 21, y1: 5, y2: 5, stroke: color(s.color), 'stroke-width': 2,
        'stroke-dasharray': s.dashed ? '5 3' : 'none' }));
      if (s.markers) sw.append(el('circle', { cx: 11, cy: 5, r: 3.5, fill: color(s.color) }));
      item.append(sw, document.createTextNode(s.label));
      legend.append(item);
    }
    container.append(legend);
  }

  const W = Math.max(container.clientWidth, 280);
  const H = spec.height || 320;
  const m = { l: 72, r: 16, t: 10, b: 48 };
  const pw = W - m.l - m.r, ph = H - m.t - m.b;
  const [x0, x1] = domain(series.flatMap((s) => s.x), spec.logX, 0);
  const [y0, y1] = domain(series.flatMap((s) => s.y), spec.logY, 0.06);
  const sx = (v) => m.l + (spec.logX ? (Math.log10(v) - Math.log10(x0)) / (Math.log10(x1) - Math.log10(x0)) : (v - x0) / (x1 - x0)) * pw;
  const sy = (v) => m.t + ph - (spec.logY ? (Math.log10(v) - Math.log10(y0)) / (Math.log10(y1) - Math.log10(y0)) : (v - y0) / (y1 - y0)) * ph;
  const ok = (x, y) => Number.isFinite(x) && Number.isFinite(y) && (!spec.logX || x > 0) && (!spec.logY || y > 0);

  const svg = el('svg', { viewBox: `0 0 ${W} ${H}`, height: H, role: 'img', 'aria-label': `${spec.yLabel} vs ${spec.xLabel}` });
  const grid = cssVar('--grid'), axis = cssVar('--axis'), text2 = cssVar('--text-2'), text = cssVar('--text');

  for (const t of spec.logX ? logTicks(x0, x1) : niceLinearTicks(x0, x1)) {
    svg.append(el('line', { x1: sx(t), x2: sx(t), y1: m.t, y2: m.t + ph, stroke: grid }));
    svg.append(el('text', { x: sx(t), y: m.t + ph + 16, 'font-size': 11.5, fill: text2, 'text-anchor': 'middle' }, spec.logX ? logLabel(t) : fmt(t, 3)));
  }
  for (const t of spec.logY ? logTicks(y0, y1) : niceLinearTicks(y0, y1)) {
    svg.append(el('line', { x1: m.l, x2: m.l + pw, y1: sy(t), y2: sy(t), stroke: grid }));
    svg.append(el('text', { x: m.l - 6, y: sy(t) + 4, 'font-size': 11.5, fill: text2, 'text-anchor': 'end' }, spec.logY ? logLabel(t) : fmt(t, 3)));
  }
  svg.append(el('line', { x1: m.l, x2: m.l + pw, y1: m.t + ph, y2: m.t + ph, stroke: axis }));
  svg.append(el('text', { x: m.l + pw / 2, y: H - 8, 'font-size': 12.5, fill: text, 'text-anchor': 'middle' }, spec.xLabel));
  svg.append(el('text', { transform: `translate(14 ${m.t + ph / 2}) rotate(-90)`, 'font-size': 12.5, fill: text, 'text-anchor': 'middle' }, spec.yLabel));

  for (const s of series) {
    const pts = s.x.map((x, i) => (ok(x, s.y[i]) ? `${sx(x).toFixed(2)},${sy(s.y[i]).toFixed(2)}` : null)).filter(Boolean);
    if (s.line !== false && pts.length > 1) {
      svg.append(el('polyline', { points: pts.join(' '), fill: 'none', stroke: color(s.color), 'stroke-width': 2,
        'stroke-linejoin': 'round', 'stroke-dasharray': s.dashed ? '6 4' : 'none' }));
    }
    if (s.markers) {
      s.x.forEach((x, i) => {
        if (ok(x, s.y[i])) svg.append(el('circle', { cx: sx(x), cy: sy(s.y[i]), r: 4.5, fill: color(s.color), stroke: cssVar('--surface'), 'stroke-width': 2 }));
      });
    }
  }

  // Hover: snap to the nearest x of the series that has the most points.
  const cross = el('line', { y1: m.t, y2: m.t + ph, stroke: axis, 'stroke-dasharray': '3 3', visibility: 'hidden' });
  const dots = el('g', { visibility: 'hidden' });
  const hit = el('rect', { x: m.l, y: m.t, width: pw, height: ph, fill: 'transparent' });
  svg.append(cross, dots, hit);
  container.append(svg);

  const tip = document.createElement('div');
  tip.className = 'tooltip';
  tip.hidden = true;
  container.append(tip);

  const nearest = (arr, v) => {
    let best = 0;
    for (let i = 1; i < arr.length; i++) if (Math.abs(arr[i] - v) < Math.abs(arr[best] - v)) best = i;
    return best;
  };

  hit.addEventListener('mousemove', (ev) => {
    const rect = svg.getBoundingClientRect();
    const px = ((ev.clientX - rect.left) / rect.width) * W;
    const xv = spec.logX ? 10 ** (Math.log10(x0) + ((px - m.l) / pw) * (Math.log10(x1) - Math.log10(x0))) : x0 + ((px - m.l) / pw) * (x1 - x0);
    dots.replaceChildren();
    const lines = [];
    let anchorX = null;
    for (const s of series) {
      const i = nearest(s.x, xv);
      if (!ok(s.x[i], s.y[i])) continue;
      anchorX ??= s.x[i];
      dots.append(el('circle', { cx: sx(s.x[i]), cy: sy(s.y[i]), r: 4, fill: color(s.color), stroke: cssVar('--surface'), 'stroke-width': 2 }));
      lines.push(`<div><span style="display:inline-block;width:8px;height:8px;border-radius:50%;background:${color(s.color)};margin-right:6px"></span>${s.label}: <b>${fmt(s.y[i], 6)}</b> <span style="color:var(--text-2)">@ ${fmt(s.x[i], 5)}</span></div>`);
    }
    if (anchorX === null) return;
    cross.setAttribute('x1', sx(anchorX));
    cross.setAttribute('x2', sx(anchorX));
    cross.setAttribute('visibility', 'visible');
    dots.setAttribute('visibility', 'visible');
    tip.innerHTML = `<div style="color:var(--text-2);margin-bottom:2px">${spec.xLabel}: ${fmt(anchorX, 5)}</div>${lines.join('')}`;
    tip.hidden = false;
    const cw = container.clientWidth;
    const left = (sx(anchorX) / W) * rect.width + 14;
    tip.style.left = `${Math.min(left, cw - tip.offsetWidth - 4)}px`;
    tip.style.top = `${svg.offsetTop + 8}px`;
  });
  hit.addEventListener('mouseleave', () => {
    tip.hidden = true;
    cross.setAttribute('visibility', 'hidden');
    dots.setAttribute('visibility', 'hidden');
  });
}
