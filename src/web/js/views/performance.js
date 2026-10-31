// Performance view: benchmark and single batch runs as background jobs with
// progress and cancel, plus host details for the report.

import { api, downloadCsv } from '../api.js';
import { linePlot } from '../plot.js';
import { badge, fillTable, fmt, setFacts, setStatus, showMsg } from '../ui.js';

const $ = (id) => document.getElementById(id);
let activeJob = null;
let lastBenchmarkJob = null;
let pollTimer = null;

function readForm() {
  const form = $('bench-form');
  const out = {};
  const problems = [];
  const limits = { cases: [1, 100000], elements: [1, 10000000], seed: [0, 9e15], maxWorkers: [1, 16], warmup: [0, 5], repetitions: [1, 15] };
  for (const [name, [lo, hi]] of Object.entries(limits)) {
    const input = form.elements[name];
    const v = Number(input.value);
    const bad = input.value.trim() === '' || !Number.isInteger(v) || v < lo || v > hi;
    input.setAttribute('aria-invalid', bad);
    if (bad) problems.push(`${input.closest('label').querySelector('span').textContent} must be a whole number from ${lo} to ${hi}.`);
    out[name] = v;
  }
  if (!problems.length && out.cases * (out.elements + 1) > 20_000_000) {
    problems.push(`Cases × (elements + 1) = ${(out.cases * (out.elements + 1)).toLocaleString()} values; keep it under 20 million (memory guard).`);
  }
  if (problems.length) {
    showMsg('bench-msg', problems.join(' '), 'error');
    return null;
  }
  showMsg('bench-msg', '');
  return out;
}

function setJobUi(running) {
  $('bench-run').disabled = running;
  $('batch-run').disabled = running;
  $('job-cancel').disabled = !running;
  $('job-card').hidden = false;
}

async function startJob(path, body, label) {
  try {
    const { jobId } = await api.post(path, body);
    activeJob = { id: jobId, label };
    setJobUi(true);
    setStatus(`${label} started (${jobId}). The UI stays usable while it runs.`);
    poll();
  } catch (err) {
    showMsg('bench-msg', err.message, 'error');
    setStatus(`${label} could not start: ${err.message}`, 'error');
  }
}

async function poll() {
  clearTimeout(pollTimer);
  if (!activeJob) return;
  let s;
  try {
    s = await api.get(`/api/jobs/${activeJob.id}`);
  } catch (err) {
    $('job-text').textContent = err.message;
    pollTimer = setTimeout(poll, 1000);
    return;
  }
  const pct = s.total ? (100 * s.done) / s.total : 0;
  $('job-bar').style.width = `${pct}%`;
  const what = activeJob.label === 'Benchmark' ? 'runs' : 'cases';
  $('job-text').textContent = `${activeJob.label}: ${s.state} · ${s.done}/${s.total || '?'} ${what} · ${fmt(s.elapsed_s, 3)} s`;

  if (s.state === 'running') {
    pollTimer = setTimeout(poll, 250);
    return;
  }
  const label = activeJob.label;
  activeJob = null;
  setJobUi(false);
  $('job-cancel').disabled = true;
  if (s.state === 'done') {
    if (s.kind === 'benchmark') {
      lastBenchmarkJob = s.id;
      showBenchmark(s.result);
    } else {
      showBatch(s.result);
    }
    setStatus(`${label} finished in ${fmt(s.elapsed_s, 3)} s`);
  } else if (s.state === 'cancelled') {
    $('job-bar').style.width = '0%';
    showMsg('bench-msg', s.error, 'busy');
    setStatus(`${label} cancelled. ${s.error}`);
  } else {
    showMsg('bench-msg', s.error, 'error');
    setStatus(`${label} failed: ${s.error}`, 'error');
  }
}

function showBenchmark(r) {
  const rows = [r.serial, ...r.parallel].map((row) => [
    row.workers === 0 ? 'Serial loop' : `Pool, p = ${row.workers}`,
    fmt(row.median_s * 1000, 4),
    row.runs_s.map((t) => fmt(t * 1000, 3)).join(', '),
    row.workers === 0 ? '—' : fmt(row.speedup, 3),
    row.workers === 0 ? '—' : fmt(row.efficiency, 3),
    fmt(row.maxAbsDiffVsSerial_C, 2),
    row.maxAbsDiffVsSerial_C <= r.tolerance_C ? badge('≤ 1e-8', 'ok') : badge('MISMATCH', 'bad'),
  ]);
  fillTable($('bench-table'), ['Mode', 'Median (ms)', 'Runs (ms)', 'Speedup', 'Efficiency', 'Max |ΔT| vs serial (°C)', 'Agreement'], rows, [1, 3, 4, 5]);

  const c = r.config;
  $('agree-badge').innerHTML =
    `<p class="hint">${c.cases} cases × ${c.elements} elements, seed ${c.seed}, ${c.warmup} warm-up + ${c.repetitions} measured runs. ` +
    (r.allAgree ? '<span class="badge ok">Serial and parallel agree within 1e-8 °C</span>' : '<span class="badge bad">Results disagree</span>') +
    (r.speedupAssessable ? '' : ' <span class="badge sim">Single logical CPU: parallel path ran, speedup cannot be assessed</span>') +
    '</p>';

  linePlot($('bench-plot'), {
    xLabel: 'Worker threads p', yLabel: 'Speedup T₁ / Tₚ', height: 300,
    series: [
      { label: 'Measured', x: r.parallel.map((p) => p.workers), y: r.parallel.map((p) => p.speedup), color: '--series-1', markers: true },
      { label: 'Ideal (= p)', x: r.parallel.map((p) => p.workers), y: r.parallel.map((p) => p.workers), color: '--text-2', dashed: true },
    ],
  });
  $('bench-export').disabled = false;
}

function showBatch(r) {
  showMsg('bench-msg', `${r.mode === 'serial' ? 'Serial loop' : `Pool with ${r.workers} workers`}: ${r.cases} cases in ` +
    `${fmt(r.compute_s * 1000, 4)} ms. Case 0: k = ${fmt(r.case0.k, 5)} W/(m·K), q = ${fmt(r.case0.q, 5)} W/m³, ` +
    `T(L/2) = ${fmt(r.case0.T_mid_C, 6)} °C.`, 'ok');
}

async function loadSystem() {
  try {
    const s = await api.get('/api/system');
    setFacts('sys-facts', [
      ['CPU', s.cpu],
      ['Cores', `${s.physicalCores || '?'} physical / ${s.logicalCpus} logical`],
      ['RAM', s.ramBytes ? `${(s.ramBytes / 2 ** 30).toFixed(1)} GiB` : 'unknown'],
      ['OS', s.os],
      ['Compiler', s.compiler],
      ['Build', `${s.buildType} ${s.buildFlags}`],
      ['Launch → server ready', `${fmt(s.launchToReady_s * 1000, 4)} ms (from ${s.launchClock})`],
      ['Page → UI ready', s.uiReady_s ? `${fmt(s.uiReady_s * 1000, 4)} ms` : '—'],
    ]);
    const sel = $('batch-workers');
    sel.replaceChildren(new Option('Serial loop', '0'));
    for (let p = 1; p <= Math.min(s.logicalCpus, 16); p++) sel.append(new Option(`Pool, ${p} worker${p > 1 ? 's' : ''}`, String(p)));
    sel.value = String(Math.min(4, s.logicalCpus));
    return s;
  } catch (err) {
    setStatus(`Could not read system info: ${err.message}`, 'error');
    return null;
  }
}

export function initPerformance() {
  $('bench-form').addEventListener('submit', (e) => {
    e.preventDefault();
    const body = readForm();
    if (body) startJob('/api/jobs/benchmark', body, 'Benchmark');
  });
  $('batch-run').addEventListener('click', () => {
    const body = readForm();
    if (body) startJob('/api/jobs/batch', { ...body, workers: Number($('batch-workers').value) }, 'Batch run');
  });
  $('job-cancel').addEventListener('click', async () => {
    if (!activeJob) return;
    $('job-cancel').disabled = true;
    await api.post(`/api/jobs/${activeJob.id}/cancel`).catch(() => {});
    setStatus('Cancel requested; finishing the case in progress…');
  });
  $('bench-export').addEventListener('click', async () => {
    if (!lastBenchmarkJob) return;
    try {
      await downloadCsv('GET', `/api/jobs/${lastBenchmarkJob}/csv`, null, 'benchmark.csv');
      setStatus('Exported benchmark.csv');
    } catch (err) {
      setStatus(`Export failed: ${err.message}`, 'error');
    }
  });
  linePlot($('bench-plot'), { series: [], empty: 'Run the benchmark to plot speedup.' });
}

export { loadSystem };
