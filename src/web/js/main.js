// Entry point: tab switching, theme, backend health and the activity log.

import { api } from './api.js';
import { redrawAllPlots } from './plot.js';
import { log, setStatus } from './ui.js';
import { showBackprop } from './views/backprop.js';
import { initGeometry, onThemeChange } from './views/geometry.js';
import { initHardware, setHardwareVisible } from './views/hardware.js';
import { initPerformance, loadSystem } from './views/performance.js';
import { initThermal, showDefaultStudy } from './views/thermal.js';

const $ = (id) => document.getElementById(id);
const opened = new Set();

function showView(name) {
  document.querySelectorAll('.tabs button').forEach((b) => b.setAttribute('aria-selected', b.dataset.view === name));
  document.querySelectorAll('.view').forEach((v) => v.toggleAttribute('data-active', v.id === `view-${name}`));
  setHardwareVisible(name === 'hardware');
  if (location.hash.slice(1).split('/')[0] !== name) history.replaceState(null, '', `#${name}`);
  if (!opened.has(name)) {
    opened.add(name);
    if (name === 'thermal') showDefaultStudy();
    if (name === 'backprop') showBackprop();
  }
  try { localStorage.setItem('wb.view', name); } catch { /* storage may be blocked */ }
}

// ---------------------------------------------------------------- theme

function resolvedTheme() {
  const forced = document.documentElement.dataset.theme;
  if (forced) return forced;
  return matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';
}

function applyTheme(theme) {
  if (theme) document.documentElement.dataset.theme = theme;
  else delete document.documentElement.dataset.theme;
  document.documentElement.dataset.resolvedTheme = resolvedTheme();
  redrawAllPlots();
  onThemeChange();
}

function initTheme() {
  let saved = null;
  try { saved = localStorage.getItem('wb.theme'); } catch { /* ignore */ }
  applyTheme(saved);
  $('theme-toggle').addEventListener('click', () => {
    const next = resolvedTheme() === 'dark' ? 'light' : 'dark';
    try { localStorage.setItem('wb.theme', next); } catch { /* ignore */ }
    applyTheme(next);
  });
  matchMedia('(prefers-color-scheme: dark)').addEventListener('change', () => applyTheme(document.documentElement.dataset.theme));
}

// ---------------------------------------------------------------- backend health

async function checkHealth() {
  const pill = $('backend-pill');
  try {
    await api.get('/api/health');
    pill.className = 'pill pill-ok';
    pill.textContent = 'C++ backend connected';
  } catch {
    pill.className = 'pill pill-bad';
    pill.textContent = 'Backend offline';
  }
}

async function quit() {
  if (!confirm('Stop the workbench server? Running jobs will be cancelled.')) return;
  await api.post('/api/shutdown').catch(() => {});
  setStatus('Workbench stopped. You can close this tab.');
  $('backend-pill').className = 'pill pill-bad';
  $('backend-pill').textContent = 'Stopped';
}

// ---------------------------------------------------------------- start-up

// Last-resort reporting so a bug in a click handler is visible, never silent.
window.addEventListener('error', (e) => setStatus(`Unexpected UI error: ${e.message}`, 'error'));
window.addEventListener('unhandledrejection', (e) => setStatus(`Unexpected UI error: ${e.reason?.message || e.reason}`, 'error'));

async function start() {
  initTheme();
  initGeometry();
  initThermal();
  initPerformance();
  initHardware();

  document.querySelectorAll('.tabs button').forEach((b) => b.addEventListener('click', () => showView(b.dataset.view)));
  $('log-toggle').addEventListener('click', () => { $('logpanel').hidden = !$('logpanel').hidden; });
  $('quit-btn').addEventListener('click', quit);

  // Deep links: #thermal, #performance ... and #geometry/examples to open with the bundled files loaded.
  const [hashView, hashOption] = location.hash.slice(1).split('/');
  let initial = hashView || 'geometry';
  if (!hashView) {
    try { initial = localStorage.getItem('wb.view') || initial; } catch { /* ignore */ }
  }
  showView(document.getElementById(`view-${initial}`) ? initial : 'geometry');
  if (initial === 'geometry' && hashOption === 'examples') {
    $('stl-example').click();
    $('csv-example').click();
  }

  await checkHealth();
  const sys = await loadSystem();
  if (sys) {
    const readyMs = performance.now();  // ms since navigation start
    api.post('/api/ui-ready', { pageReady_ms: readyMs }).catch(() => {});
    setStatus(`Ready. Server started in ${(sys.launchToReady_s * 1000).toFixed(0)} ms, page usable after ${readyMs.toFixed(0)} ms.`);
    loadSystem();  // refresh so the "Page -> UI ready" line is filled in
  }
  setInterval(checkHealth, 5000);
  log('All views initialised');
}

start();
