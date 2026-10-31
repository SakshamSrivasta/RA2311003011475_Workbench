// Shared UI helpers: status bar, activity log, inline messages, formatting.

const statusText = () => document.getElementById('status-text');
const logList = () => document.getElementById('log-list');

function timestamp() {
  return new Date().toLocaleTimeString([], { hour12: false });
}

export function log(message, level = 'info') {
  const li = document.createElement('li');
  li.textContent = `${timestamp()}  ${message}`;
  if (level === 'error') li.className = 'error';
  logList().prepend(li);
  while (logList().children.length > 300) logList().lastChild.remove();
}

export function setStatus(message, level = 'info') {
  const el = statusText();
  el.textContent = message;
  el.className = level === 'error' ? 'error' : '';
  log(message, level);
}

// Inline message under a form. kind: 'ok' | 'error' | 'busy' | '' (clear)
export function showMsg(id, text, kind = '') {
  const el = document.getElementById(id);
  el.textContent = text || '';
  el.className = `msg ${kind}`;
}

export function fmt(v, digits = 4) {
  if (v === null || v === undefined || Number.isNaN(v)) return '—';
  if (v === 0) return '0';
  const a = Math.abs(v);
  if (a >= 1e5 || a < 1e-3) return v.toExponential(Math.max(1, digits - 1));
  return Number(v.toPrecision(digits)).toString();
}

export function fmtBytes(n) {
  if (n < 1024) return `${n} B`;
  if (n < 1024 * 1024) return `${(n / 1024).toFixed(1)} KB`;
  return `${(n / 1048576).toFixed(1)} MB`;
}

export function setFacts(id, pairs) {
  const dl = document.getElementById(id);
  dl.replaceChildren();
  for (const [k, v, cls] of pairs) {
    const dt = document.createElement('dt');
    dt.textContent = k;
    const dd = document.createElement('dd');
    dd.textContent = v;
    if (cls) dd.className = cls;
    dl.append(dt, dd);
  }
}

// Builds a table from header labels and rows of cell values. Numbers are
// right-aligned; cells can be { html } for badges.
export function fillTable(table, headers, rows, numericCols = []) {
  const thead = document.createElement('thead');
  const hr = document.createElement('tr');
  headers.forEach((h, i) => {
    const th = document.createElement('th');
    th.textContent = h;
    if (numericCols.includes(i)) th.className = 'num';
    hr.append(th);
  });
  thead.append(hr);
  const tbody = document.createElement('tbody');
  for (const row of rows) {
    const tr = document.createElement('tr');
    row.forEach((cell, i) => {
      const td = document.createElement('td');
      if (cell && typeof cell === 'object' && 'html' in cell) td.innerHTML = cell.html;
      else td.textContent = cell;
      if (numericCols.includes(i)) td.className = 'num';
      tr.append(td);
    });
    tbody.append(tr);
  }
  table.replaceChildren(thead, tbody);
}

export const badge = (text, kind) => ({ html: `<span class="badge ${kind}">${text}</span>` });

// Reads a CSS custom property (used to colour plots per theme).
export const cssVar = (name) => getComputedStyle(document.documentElement).getPropertyValue(name).trim();

// Runs an async action with the button disabled, so double clicks can't queue
// duplicate requests.
export async function withButton(button, action) {
  if (button.disabled) return;
  button.disabled = true;
  try {
    await action();
  } finally {
    button.disabled = false;
  }
}
