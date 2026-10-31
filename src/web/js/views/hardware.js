// Hardware view: simulated device discovery, connection, telemetry and fault
// recovery. Telemetry is polled once a second while this view is open.

import { api } from '../api.js';
import { fmt, setStatus, withButton } from '../ui.js';

const $ = (id) => document.getElementById(id);
const KIND_LABEL = { microcontroller: 'Microcontroller board', fpga: 'FPGA board', 'jtag-adapter': 'JTAG debug adapter' };

let devices = [];
const telemetry = new Map();  // id -> last message
const errors = new Map();     // id -> last error text
let timer = null;
let visible = false;

function event(text, kind = '') {
  const li = document.createElement('li');
  li.textContent = `${new Date().toLocaleTimeString([], { hour12: false })}  ${text}`;
  li.className = kind;
  $('hw-log').prepend(li);
}

function render() {
  const grid = $('hw-devices');
  grid.replaceChildren();
  for (const d of devices) {
    const card = document.createElement('article');
    card.className = 'card device';
    const t = telemetry.get(d.id);
    const err = errors.get(d.id);
    card.innerHTML = `
      <div class="device-head">
        <div>
          <div class="device-kind">${KIND_LABEL[d.kind] || d.kind}</div>
          <div class="device-title">${d.id} <span class="badge sim">SIMULATED</span></div>
        </div>
        <span class="state ${d.state}">${d.state}</span>
      </div>
      <dl class="facts">
        <dt>Assumed board</dt><dd></dd>
        <dt>Port</dt><dd></dd>
        <dt>Host link</dt><dd></dd>
        <dt>I/O voltage</dt><dd></dd>
      </dl>
      <ul class="caps"></ul>
      <div class="row wrap actions"></div>
      ${err ? '<div class="err"></div>' : ''}
      <table class="data telemetry"></table>`;
    const dds = card.querySelectorAll('dd');
    [d.model, d.port, d.transport, d.ioVoltage].forEach((v, i) => { dds[i].textContent = v; });
    const caps = card.querySelector('.caps');
    for (const c of d.capabilities) {
      const li = document.createElement('li');
      li.innerHTML = '<b></b>: <span></span>';
      li.querySelector('b').textContent = c.name;
      li.querySelector('span').textContent = c.detail;
      caps.append(li);
    }
    if (err) card.querySelector('.err').textContent = err;

    const actions = card.querySelector('.actions');
    const button = (label, cls, handler, enabled = true) => {
      const b = document.createElement('button');
      b.className = `btn ${cls}`;
      b.textContent = label;
      b.disabled = !enabled;
      b.addEventListener('click', () => withButton(b, handler));
      actions.append(b);
    };
    const connected = d.state === 'connected';
    button('Connect', 'btn-primary', () => act(d.id, 'connect'), d.state === 'disconnected');
    button('Disconnect', '', () => act(d.id, 'disconnect'), d.state !== 'disconnected');
    button('Inject timeout', 'btn-danger', () => fault(d.id, 'timeout'), connected);
    button('Inject link drop', 'btn-danger', () => fault(d.id, 'link-drop'), connected);
    button('Recover', '', () => recover(d.id), d.state === 'faulted');

    const table = card.querySelector('.telemetry');
    if (t && connected) {
      table.innerHTML = `<thead><tr><th>Telemetry #${t.sequence} · t = ${fmt(t.time_s, 3)} s</th><th class="num">Value</th></tr></thead>`;
      const body = document.createElement('tbody');
      for (const f of t.fields) {
        const tr = document.createElement('tr');
        const value = f.text ?? `${fmt(f.value, 5)} ${f.unit}`;
        tr.innerHTML = '<td></td><td class="num"></td>';
        tr.children[0].textContent = f.name;
        tr.children[1].textContent = value;
        body.append(tr);
      }
      table.append(body);
    }
    grid.append(card);
  }
}

function replaceDevice(updated) {
  devices = devices.map((d) => (d.id === updated.id ? updated : d));
}

async function act(id, action) {
  try {
    replaceDevice(await api.post(`/api/hw/${id}/${action}`));
    errors.delete(id);
    if (action === 'disconnect') telemetry.delete(id);
    event(`${id}: ${action} ok`, 'good');
    if (action === 'connect') await readTelemetry(id);
  } catch (err) {
    errors.set(id, err.message);
    event(`${id}: ${action} failed - ${err.message}`, 'bad');
  }
  render();
}

async function fault(id, kind) {
  try {
    replaceDevice(await api.post(`/api/hw/${id}/fault`, { kind }));
    event(`${id}: ${kind} armed - it will trigger on the next telemetry read`);
    await readTelemetry(id);
  } catch (err) {
    errors.set(id, err.message);
  }
  render();
}

async function recover(id) {
  try {
    const r = await api.post(`/api/hw/${id}/recover`);
    devices = r.devices;
    errors.delete(id);
    event(`${id}: recovered after ${r.attempts} reconnect attempt(s)`, 'good');
    setStatus(`${id} recovered (${r.attempts} attempt(s)); telemetry resumed`);
    await readTelemetry(id);
  } catch (err) {
    errors.set(id, err.message);
    event(`${id}: recovery failed - ${err.message}`, 'bad');
  }
  render();
}

async function readTelemetry(id) {
  try {
    telemetry.set(id, await api.get(`/api/hw/${id}/telemetry`));
    errors.delete(id);
  } catch (err) {
    errors.set(id, err.message);
    telemetry.delete(id);
    event(`${id}: ${err.message}`, 'bad');
    setStatus(`${id}: ${err.message}`, 'error');
    devices = await api.get('/api/hw/devices').catch(() => devices);
  }
}

async function tick() {
  clearTimeout(timer);
  if (!visible) return;
  const live = devices.filter((d) => d.state === 'connected');
  if (live.length) {
    await Promise.all(live.map((d) => readTelemetry(d.id)));
    render();
  }
  timer = setTimeout(tick, 1000);
}

async function discover(button) {
  await withButton(button, async () => {
    try {
      devices = await api.post('/api/hw/discover');
      telemetry.clear();
      errors.clear();
      event(`Discovery found ${devices.length} simulated devices: ${devices.map((d) => d.id).join(', ')}`);
      setStatus(`Hardware discovery: ${devices.length} simulated devices`);
      render();
    } catch (err) {
      event(`Discovery failed - ${err.message}`, 'bad');
    }
  });
}

export function initHardware() {
  $('hw-discover').addEventListener('click', (e) => discover(e.target));
}

export function setHardwareVisible(on) {
  visible = on;
  if (on) {
    if (!devices.length) discover($('hw-discover'));
    tick();
  } else {
    clearTimeout(timer);
  }
}
