// Backprop view: shows the C++ forward pass, gradients, finite-difference
// check, one update and the 100-step loss curve.

import { api } from '../api.js';
import { linePlot } from '../plot.js';
import { badge, fillTable, fmt, setStatus } from '../ui.js';

const $ = (id) => document.getElementById(id);
let loaded = false;

export async function showBackprop() {
  if (loaded) return;
  try {
    const r = await api.get('/api/backprop');
    loaded = true;
    const f = r.forward;
    fillTable($('bp-forward'), ['Quantity', 'Expression', 'Value'], [
      ['z', 'w·x + b', fmt(f.z, 8)],
      ['h', 'tanh(z)', fmt(f.h, 8)],
      ['ŷ', 'v·h + c', fmt(f.yHat, 8)],
      ['J', '½(ŷ − y)²', fmt(f.loss, 8)],
    ], [2]);

    const p0 = r.initial, p1 = r.afterOneStep, g = r.gradients;
    fillTable($('bp-step'), ['Param', 'Before', 'Gradient', 'After (θ − 0.1·g)'],
      ['w', 'b', 'v', 'c'].map((k) => [k, fmt(p0[k], 6), fmt(g[k], 8), fmt(p1[k], 8)]), [1, 2, 3]);
    const lossRow = document.createElement('p');
    lossRow.className = 'hint';
    lossRow.textContent = `Loss: ${fmt(f.loss, 8)} → ${fmt(r.lossAfterOneStep, 8)} after one simultaneous update.`;
    $('bp-step').after(lossRow);

    const formula = { w: 'e·v·(1 − h²)·x', b: 'e·v·(1 − h²)', v: 'e·h', c: 'e' };
    fillTable($('bp-grads'), ['Param', 'Chain rule (e = ŷ − y)', 'Analytic', 'Central difference', '|Δ|', 'Check'],
      r.gradientCheck.map((row) => [row.param, formula[row.param], fmt(row.analytic, 10), fmt(row.numerical, 10),
        fmt(row.absError, 2), row.passed ? badge('≤ 1e-5', 'ok') : badge('FAIL', 'bad')]), [2, 3, 4]);

    linePlot($('bp-plot'), {
      xLabel: 'Training step', yLabel: 'Loss J (log scale)', logY: true, height: 300,
      series: [{ label: 'Loss J', x: r.lossHistory.map((_, i) => i), y: r.lossHistory, color: '--series-1' }],
    });
    setStatus(`Backprop: all ${r.gradientCheck.filter((x) => x.passed).length}/4 gradient checks pass; ` +
      `loss after ${r.lossHistory.length - 1} steps = ${fmt(r.lossHistory[r.lossHistory.length - 1], 3)}`);
  } catch (err) {
    setStatus(`Backprop data failed to load: ${err.message}`, 'error');
  }
}
