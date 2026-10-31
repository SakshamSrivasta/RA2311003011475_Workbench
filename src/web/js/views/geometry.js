// Geometry view: STL import, nodal CSV import, 3D display and node inspection.
// All parsing and validation happens in C++; this file only displays results.

import { api } from '../api.js';
import { cssVar, fmt, fmtBytes, setFacts, setStatus, showMsg, withButton } from '../ui.js';
import { Viewer3D, rampFor } from '../viewer3d.js';

const MAX_UPLOAD = 64 * 1024 * 1024;  // same limit as the C++ readers
let viewer;
let stl = null;    // last STL response
let nodes = null;  // last nodal CSV response

const $ = (id) => document.getElementById(id);
const currentTheme = () => document.documentElement.dataset.resolvedTheme || 'light';

function ensureViewer() {
  if (viewer) return viewer;
  viewer = new Viewer3D($('viewer'), { onHover: showTip, onPick: showNode });
  viewer.setBackground(cssVar('--viewport'));
  return viewer;
}

function boundsText(b, scale = 1, unit = 'm') {
  const size = b.max.map((v, i) => (v - b.min[i]) * scale);
  return `${size.map((s) => fmt(s, 4)).join(' × ')} ${unit}`;
}

function unitScale() {
  return Number($('stl-units').value);
}

function unitName() {
  return $('stl-units').selectedOptions[0].textContent;
}

// ---------------------------------------------------------------- STL

async function loadStl(request, label) {
  showMsg('stl-msg', `Parsing ${label}…`, 'busy');
  try {
    stl = await request();
    drawStl();
    const deg = stl.degenerateTriangles;
    showMsg('stl-msg', `Loaded ${stl.triangles.toLocaleString()} triangles.`, 'ok');
    setStatus(`STL "${stl.fileName}" parsed in ${fmt(stl.parseMs, 3)} ms (${stl.triangles} triangles)`);
    if (deg) setStatus(`STL has ${deg} zero-area triangle(s); they are drawn but carry no surface.`);
  } catch (err) {
    showMsg('stl-msg', err.message, 'error');
    setStatus(`STL import failed: ${err.message}`, 'error');
  }
}

function drawStl() {
  if (!stl) return;
  const v = ensureViewer();
  v.setMesh(stl.positions, unitScale());
  $('viewer-empty').hidden = true;
  setFacts('stl-facts', [
    ['File', `${stl.fileName} (${fmtBytes(stl.bytes)})`],
    ['Solid name', stl.solidName || '—'],
    ['Triangles', stl.triangles.toLocaleString()],
    ['Size in file units', boundsText(stl.bounds, 1, 'units')],
    ['Size in metres', boundsText(stl.bounds, unitScale(), 'm') + ` (as ${unitName()})`],
    ['Zero-area facets', String(stl.degenerateTriangles), stl.degenerateTriangles ? 'warn' : ''],
    ['Parse time (C++)', `${fmt(stl.parseMs, 3)} ms`],
  ]);
  v.fitToView();
}

// ---------------------------------------------------------------- nodal CSV

async function loadCsv(request, label) {
  showMsg('csv-msg', `Parsing ${label}…`, 'busy');
  try {
    nodes = await request();
    drawNodes();
    showMsg('csv-msg', `Loaded ${nodes.nodes.toLocaleString()} nodes.`, 'ok');
    setStatus(`Nodal CSV "${nodes.fileName}" parsed in ${fmt(nodes.parseMs, 3)} ms (${nodes.nodes} nodes)`);
  } catch (err) {
    showMsg('csv-msg', err.message, 'error');
    setStatus(`CSV import failed: ${err.message}`, 'error');
  }
}

function drawNodes() {
  if (!nodes) return;
  const v = ensureViewer();
  v.setPoints(nodes, rampFor(currentTheme()));
  $('viewer-empty').hidden = true;
  const synthetic = nodes.comments.some((c) => /synthetic/i.test(c));
  setFacts('csv-facts', [
    ['File', `${nodes.fileName} (${fmtBytes(nodes.bytes)})`],
    ['Source', synthetic ? 'SYNTHETIC (labelled in file)' : 'Not stated in file', synthetic ? 'warn' : ''],
    ['Nodes', nodes.nodes.toLocaleString()],
    ['Temperature range', `${fmt(nodes.minTemperature_C, 5)} to ${fmt(nodes.maxTemperature_C, 5)} °C`],
    ['Extent', boundsText(nodes.bounds_m)],
    ['Parse time (C++)', `${fmt(nodes.parseMs, 3)} ms`],
    ...nodes.comments.slice(0, 3).map((c, i) => [i === 0 ? 'Notes' : '', c]),
  ]);
  drawLegend();
  v.fitToView();
}

function drawLegend() {
  if (!nodes) return;
  const ramp = rampFor(currentTheme());
  $('temp-legend').hidden = false;
  $('legend-bar').style.background = `linear-gradient(to top, ${ramp.join(', ')})`;
  const lo = nodes.minTemperature_C, hi = nodes.maxTemperature_C;
  $('legend-ticks').replaceChildren(
    ...[hi, (lo + hi) / 2, lo].map((v) => Object.assign(document.createElement('span'), { textContent: fmt(v, 4) })),
  );
}

function nodeLines(i) {
  return [
    ['Node id', String(nodes.id[i])],
    ['x, y, z', `${fmt(nodes.x[i], 5)}, ${fmt(nodes.y[i], 5)}, ${fmt(nodes.z[i], 5)} m`],
    ['Temperature', `${fmt(nodes.t[i], 6)} °C`],
  ];
}

function showTip(index, at) {
  const tip = $('node-tip');
  if (index === null || index === undefined || !nodes) {
    tip.hidden = true;
    return;
  }
  tip.innerHTML = nodeLines(index).map(([k, v]) => `<div><span style="color:var(--text-2)">${k}:</span> <b>${v}</b></div>`).join('');
  tip.hidden = false;
  tip.style.left = `${Math.min(at.x + 14, $('viewer').clientWidth - tip.offsetWidth - 4)}px`;
  tip.style.top = `${Math.max(at.y - 10, 4)}px`;
}

function showNode(index) {
  $('node-card').hidden = false;
  setFacts('node-facts', nodeLines(index));
}

// ---------------------------------------------------------------- wiring

function pickFile(input, kind) {
  const file = input.files[0];
  input.value = '';  // allow re-selecting the same file after fixing it
  if (!file) return;
  const msgId = kind === 'stl' ? 'stl-msg' : 'csv-msg';
  if (file.size > MAX_UPLOAD) {
    showMsg(msgId, `${file.name} is ${fmtBytes(file.size)}; the import limit is 64 MB. Decimate or split the data first.`, 'error');
    return;
  }
  if (kind === 'stl') loadStl(() => api.upload('/api/geometry/stl', file), file.name);
  else loadCsv(() => api.upload('/api/results/nodal', file), file.name);
}

export function initGeometry() {
  $('stl-file').addEventListener('change', (e) => pickFile(e.target, 'stl'));
  $('csv-file').addEventListener('change', (e) => pickFile(e.target, 'csv'));
  $('stl-example').addEventListener('click', (e) =>
    withButton(e.target, () => loadStl(() => api.post('/api/geometry/stl?sample=heatsink_fins_mm.stl'), 'example STL')));
  $('csv-example').addEventListener('click', (e) =>
    withButton(e.target, () => loadCsv(() => api.post('/api/results/nodal?sample=heatsink_nodal_SYNTHETIC.csv'), 'example CSV')));
  $('stl-units').addEventListener('change', drawStl);

  $('v-fit').addEventListener('click', () => viewer?.fitToView());
  $('v-mesh').addEventListener('change', (e) => viewer?.setVisible('mesh', e.target.checked));
  $('v-points').addEventListener('change', (e) => viewer?.setVisible('points', e.target.checked));
  $('v-wire').addEventListener('change', (e) => viewer?.setWireframe(e.target.checked));
  $('v-size').addEventListener('input', (e) => viewer?.setPointSize(Number(e.target.value)));
}

export function onThemeChange() {
  if (!viewer) return;
  viewer.setBackground(cssVar('--viewport'));
  if (nodes) {
    viewer.recolor(rampFor(currentTheme()));
    drawLegend();
  }
}
