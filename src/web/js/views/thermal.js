// Thermal Analysis view: rod parameters, convergence study, single solve,
// plots and CSV export. The browser only checks that fields are filled in
// sensibly; the C++ side does the authoritative validation.

import { api, downloadCsv } from '../api.js';
import { linePlot } from '../plot.js';
import { fillTable, fmt, setStatus, showMsg, withButton } from '../ui.js';

const $ = (id) => document.getElementById(id);
const MESH_COLORS = ['--mesh-1', '--mesh-2', '--mesh-3', '--mesh-4'];
const DEFAULTS = { L: 1, A: 0.0001, k: 10, q: 10000, T0: 100, TL: 20, counts: '10, 20, 40, 80', N: 10 };

// field -> [rule, message]
const RULES = {
  L: [(v) => v > 0, 'Length must be greater than 0 m.'],
  A: [(v) => v > 0, 'Area must be greater than 0 m².'],
  k: [(v) => v > 0, 'Conductivity must be greater than 0 W/(m·K).'],
  q: [() => true, ''],
  T0: [(v) => v >= -273.15, 'T(0) cannot be below absolute zero.'],
  TL: [(v) => v >= -273.15, 'T(L) cannot be below absolute zero.'],
  N: [(v) => Number.isInteger(v) && v >= 1 && v <= 100000, 'N must be a whole number from 1 to 100 000.'],
};

function readForm() {
  const form = $('fea-form');
  const out = {};
  const problems = [];
  for (const [name, [rule, message]] of Object.entries(RULES)) {
    const input = form.elements[name];
    const raw = input.value.trim();
    const v = Number(raw);
    const bad = raw === '' || !Number.isFinite(v) || !rule(v);
    input.setAttribute('aria-invalid', bad);
    if (bad) problems.push(raw === '' ? `${name} is empty.` : message || `${name} must be a number.`);
    out[name] = v;
  }
  const countsInput = form.elements.counts;
  const counts = countsInput.value.split(/[\s,;]+/).filter(Boolean).map(Number);
  const countsBad = !counts.length || counts.length > 8 || counts.some((n) => !Number.isInteger(n) || n < 1 || n > 100000);
  countsInput.setAttribute('aria-invalid', countsBad);
  if (countsBad) problems.push('Mesh sizes must be 1 to 8 whole numbers between 1 and 100 000, e.g. "10, 20, 40, 80".');
  out.counts = counts;
  return { params: out, problems };
}

function paramsOrShowErrors() {
  const { params, problems } = readForm();
  if (problems.length) {
    showMsg('fea-msg', problems.join(' '), 'error');
    return null;
  }
  return params;
}

function plotProfiles(solutions, exact, title) {
  $('fea-profile-title').textContent = title;
  const series = solutions.map((s, i) => ({
    label: `FE, N = ${s.N}`,
    x: s.x,
    y: s.T,
    color: MESH_COLORS[Math.min(i, MESH_COLORS.length - 1)],
    markers: s.x.length <= 41,
  }));
  series.push({ label: 'Exact T(x)', x: exact.x, y: exact.T, color: '--ink', dashed: true });
  linePlot($('fea-profile'), { xLabel: 'Position x (m)', yLabel: 'Temperature T (°C)', series, height: 340 });
}

async function runStudy(button) {
  const p = paramsOrShowErrors();
  if (!p) return;
  await withButton(button, async () => {
    showMsg('fea-msg', 'Solving…', 'busy');
    try {
      const r = await api.post('/api/thermal/convergence', p);
      plotProfiles(r.solutions, r.exact, 'Temperature along the rod: FE meshes vs exact');

      const k = r.problem.k, q = r.problem.q;
      linePlot($('fea-conv'), {
        xLabel: 'Element size h (m)', yLabel: 'Max midpoint error (°C)', logX: true, logY: true, height: 300,
        series: [
          { label: 'FE midpoint error', x: r.rows.map((x) => x.h_m), y: r.rows.map((x) => x.maxMidpointError_C), color: '--series-1', markers: true },
          { label: 'Theory q·h²/(8k)', x: r.rows.map((x) => x.h_m), y: r.rows.map((x) => (q * x.h_m * x.h_m) / (8 * k)), color: '--text-2', dashed: true },
        ],
        empty: q === 0 ? 'With q = 0 the exact solution is linear, so linear elements have zero error.' : undefined,
      });

      fillTable($('fea-table'), ['N', 'h (m)', 'Max midpoint error (°C)', 'Max nodal error (°C)', 'Order'],
        r.rows.map((x, i) => [x.N, fmt(x.h_m, 4), fmt(x.maxMidpointError_C, 6), fmt(x.maxNodalError_C, 2),
          i === 0 ? '—' : fmt(x.observedOrder, 4)]), [0, 1, 2, 3, 4]);
      const last = r.rows[r.rows.length - 1];
      $('fea-summary').textContent = `Error falls by ~4× each time h halves (order ≈ ${fmt(last.observedOrder, 3)}). ` +
        'Nodal error stays at round-off level: 1D linear elements are exact at the nodes for this problem.';
      showMsg('fea-msg', `Solved ${r.rows.length} meshes.`, 'ok');
      setStatus(`Convergence study done for N = ${p.counts.join(', ')}; finest midpoint error ${fmt(last.maxMidpointError_C, 4)} °C`);
    } catch (err) {
      showMsg('fea-msg', err.message, 'error');
      setStatus(`Thermal solve failed: ${err.message}`, 'error');
    }
  });
}

async function runSingle(button) {
  const p = paramsOrShowErrors();
  if (!p) return;
  await withButton(button, async () => {
    showMsg('fea-msg', 'Solving…', 'busy');
    try {
      const r = await api.post('/api/thermal/solve', p);
      plotProfiles([{ N: r.problem.N, x: r.x, T: r.T }], r.exact, `Single solve, N = ${r.problem.N}`);
      fillTable($('fea-table'), ['Quantity', 'Value'], [
        ['Elements N', r.problem.N],
        ['Solve time (C++)', `${fmt(r.solveMs, 3)} ms`],
        ['Max midpoint error', `${fmt(r.maxMidpointError_C, 6)} °C at x = ${fmt(r.maxMidpointErrorAt_m, 4)} m`],
        ['Max nodal error', `${fmt(r.maxNodalError_C, 3)} °C`],
        ['Peak FE temperature', `${fmt(r.T.reduce((a, b) => Math.max(a, b), -Infinity), 6)} °C`],
      ], [1]);
      $('fea-summary').textContent = '';
      showMsg('fea-msg', `Solved N = ${r.problem.N} in ${fmt(r.solveMs, 3)} ms.`, 'ok');
      setStatus(`Single solve N = ${r.problem.N}: max midpoint error ${fmt(r.maxMidpointError_C, 4)} °C`);
    } catch (err) {
      showMsg('fea-msg', err.message, 'error');
      setStatus(`Thermal solve failed: ${err.message}`, 'error');
    }
  });
}

async function exportCsv(button) {
  const p = paramsOrShowErrors();
  if (!p) return;
  const what = button.dataset.export;
  await withButton(button, async () => {
    try {
      await downloadCsv('POST', `/api/thermal/export?what=${what}`, p, `fea_${what}.csv`);
      setStatus(`Exported fea_${what}.csv`);
    } catch (err) {
      showMsg('fea-msg', err.message, 'error');
      setStatus(`Export failed: ${err.message}`, 'error');
    }
  });
}

function resetForm() {
  const form = $('fea-form');
  for (const [k, v] of Object.entries(DEFAULTS)) {
    form.elements[k].value = v;
    form.elements[k].removeAttribute('aria-invalid');
  }
  showMsg('fea-msg', '');
}

export function initThermal() {
  $('fea-form').addEventListener('submit', (e) => {
    e.preventDefault();
    runStudy(e.submitter || $('fea-form').querySelector('[type=submit]'));
  });
  $('fea-single').addEventListener('click', (e) => runSingle(e.target));
  $('fea-reset').addEventListener('click', resetForm);
  document.querySelectorAll('[data-export]').forEach((b) => b.addEventListener('click', () => exportCsv(b)));

  linePlot($('fea-profile'), { series: [], empty: 'Run the convergence study (left) to plot T(x).' });
  linePlot($('fea-conv'), { series: [], empty: 'Convergence plot appears after the study.' });
}

// Called the first time the view opens, so the assignment case is already on screen.
export function showDefaultStudy() {
  runStudy($('fea-form').querySelector('[type=submit]'));
}
