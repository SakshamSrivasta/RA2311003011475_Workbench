"""Scripted UI walkthrough: drives headless Chrome over the DevTools protocol,
clicks through every view of a running workbench and saves screenshots.
Test tooling only (stdlib Python, tiny hand-rolled websocket client); it is not
part of the application.

Usage: start `Launch --no-browser --port 8790`, then
       python3 scripts/ui_walkthrough.py <out-dir> http://127.0.0.1:8790/
Set CHROME=/path/to/chrome if Chrome is not in the default macOS location."""
import base64, json, os, socket, struct, subprocess, sys, time, urllib.request

OUT = sys.argv[1]
APP = sys.argv[2]  # e.g. http://127.0.0.1:8790/
CHROME = os.environ.get("CHROME", "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome")
PORT = 9333
os.makedirs(OUT, exist_ok=True)

proc = subprocess.Popen([CHROME, "--headless=new", f"--remote-debugging-port={PORT}", "--user-data-dir=" + OUT + "/.profile",
                         "--enable-unsafe-swiftshader", "--use-angle=swiftshader", "--hide-scrollbars",
                         "--window-size=1440,900", "about:blank"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
for _ in range(50):
    try:
        pages = json.load(urllib.request.urlopen(f"http://127.0.0.1:{PORT}/json"))
        ws_url = [p for p in pages if p["type"] == "page"][0]["webSocketDebuggerUrl"]
        break
    except Exception:
        time.sleep(0.2)

host, path = ws_url[len("ws://"):].split("/", 1)
h, p = host.split(":")
sock = socket.create_connection((h, int(p)))
key = base64.b64encode(os.urandom(16)).decode()
sock.send(f"GET /{path} HTTP/1.1\r\nHost: {host}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
          f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n".encode())
buf = b""
while b"\r\n\r\n" not in buf:
    buf += sock.recv(4096)
pending = buf.split(b"\r\n\r\n", 1)[1]

def recv_exact(n):
    global pending
    while len(pending) < n:
        pending += sock.recv(1 << 20)
    out, pending = pending[:n], pending[n:]
    return out

def send(obj):
    data = json.dumps(obj).encode()
    hdr = bytearray([0x81])
    n = len(data)
    if n < 126: hdr.append(0x80 | n)
    elif n < 65536: hdr += bytes([0x80 | 126]) + struct.pack(">H", n)
    else: hdr += bytes([0x80 | 127]) + struct.pack(">Q", n)
    mask = os.urandom(4)
    sock.send(bytes(hdr) + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))

def recv():
    b1, b2 = recv_exact(2)
    n = b2 & 0x7F
    if n == 126: n = struct.unpack(">H", recv_exact(2))[0]
    elif n == 127: n = struct.unpack(">Q", recv_exact(8))[0]
    return json.loads(recv_exact(n))

msg_id = 0
def call(method, **params):
    global msg_id
    msg_id += 1
    send({"id": msg_id, "method": method, "params": params})
    while True:
        m = recv()
        if m.get("id") == msg_id:
            return m.get("result", m)

def js(expr):
    return call("Runtime.evaluate", expression=expr, awaitPromise=True, returnByValue=True).get("result", {}).get("value")

T0 = time.time()
log = []
def shot(name, note):
    data = call("Page.captureScreenshot", format="png")["data"]
    with open(f"{OUT}/{name}.png", "wb") as f:
        f.write(base64.b64decode(data))
    log.append((round(time.time() - T0, 1), name, note))

def wait_for(expr, timeout=30):
    end = time.time() + timeout
    while time.time() < end:
        if js(expr): return True
        time.sleep(0.2)
    return False

call("Emulation.setEmulatedMedia", features=[{"name": "prefers-color-scheme", "value": "light"}])
call("Page.enable")
call("Page.navigate", url=APP + "#geometry")
time.sleep(1.5)
shot("01_geometry_empty", "App opened by Launch; Geometry view, nothing loaded yet; status bar shows launch timing")

js("document.getElementById('stl-example').click(); document.getElementById('csv-example').click(); true")
wait_for("document.getElementById('csv-msg').textContent.startsWith('Loaded') && document.getElementById('stl-msg').textContent.startsWith('Loaded')")
time.sleep(1.0)
shot("02_geometry_loaded", "Example STL (mm) and SYNTHETIC nodal CSV (m) loaded and parsed in C++; temperature legend in degC")

# Hover a node: find a projected node position by sweeping the canvas centre region.
js("""(async () => {
  const c = document.querySelector('#viewer canvas'); const r = c.getBoundingClientRect();
  for (let y = r.top + r.height*0.55; y < r.top + r.height*0.8; y += 6) {
    for (let x = r.left + r.width*0.35; x < r.left + r.width*0.65; x += 6) {
      c.dispatchEvent(new PointerEvent('pointermove', {clientX: x, clientY: y, bubbles: true}));
      if (!document.getElementById('node-tip').hidden) {
        c.dispatchEvent(new MouseEvent('click', {clientX: x, clientY: y, bubbles: true}));
        c.dispatchEvent(new PointerEvent('pointermove', {clientX: x, clientY: y, bubbles: true}));
        return true; }
    }
  } return false; })()""")
time.sleep(0.6)
shot("03_geometry_node_inspect", "Hovering/clicking a node shows its id, coordinates (m) and temperature (degC)")

# Malformed upload: send a CSV with NaN through the same upload path the file picker uses.
js("""(async () => {
  const f = new File(['node_id,x_m,y_m,z_m,temperature_C\\n1,0,0,0,25\\n2,0.1,0,0,NaN\\n'], 'broken_results.csv');
  const dt = new DataTransfer(); dt.items.add(f);
  const input = document.getElementById('csv-file'); input.files = dt.files;
  input.dispatchEvent(new Event('change')); return true; })()""")
wait_for("document.getElementById('csv-msg').classList.contains('error')")
time.sleep(0.4)
shot("04_geometry_malformed_csv", "A CSV with a NaN temperature is rejected by the C++ reader with the line number and reason")

js("document.querySelector('[data-view=thermal]').click(); true")
wait_for("document.getElementById('fea-msg').textContent.startsWith('Solved')")
time.sleep(0.6)
shot("05_thermal_convergence", "Convergence study N=10,20,40,80: FE vs exact, log-log midpoint error (order 2), table")

js("const f=document.getElementById('fea-form'); f.elements.k.value='-5'; f.elements.counts.value='10, 0'; f.querySelector('[type=submit]').click(); true")
time.sleep(0.5)
shot("06_thermal_validation", "Invalid inputs (k = -5, N = 0) are flagged before anything is sent; the C++ side re-validates too")
js("document.getElementById('fea-reset').click(); true")

js("document.querySelector('[data-view=performance]').click(); true")
time.sleep(0.8)
js("document.getElementById('bench-run').click(); true")
time.sleep(1.0)
shot("07_performance_running", "Benchmark running as a background job: progress bar, Cancel button, UI stays responsive")
wait_for("!document.getElementById('bench-export').disabled", 120)
time.sleep(0.6)
shot("08_performance_results", "Benchmark finished: median times, speedup, efficiency, serial/parallel agreement <= 1e-8 degC")

js("document.getElementById('batch-workers').value='0'; document.getElementById('bench-form').elements.cases.value='20000'; document.getElementById('bench-form').elements.elementsPerCase.value='900'; document.getElementById('batch-run').click(); true")
time.sleep(0.05)
js("document.getElementById('job-cancel').click(); true")
wait_for("document.getElementById('bench-msg').textContent.includes('Cancelled') || document.getElementById('bench-msg').textContent.includes('cases in')", 30)
time.sleep(0.3)
shot("09_performance_cancelled", "A long serial batch cancelled mid-run: partial results are discarded, nothing exported")

js("document.querySelector('[data-view=hardware]').click(); true")
wait_for("document.querySelectorAll('.device').length === 3")
js("[...document.querySelectorAll('.device')].forEach(d => d.querySelector('.btn-primary').click()); true")
time.sleep(2.2)
shot("10_hardware_connected", "Three SIMULATED devices discovered and connected; telemetry refreshes every second")
js("[...document.querySelectorAll('.device')][0].querySelectorAll('button')[2].click(); true")
time.sleep(1.5)
shot("11_hardware_timeout", "Injected timeout on mcu0: link marked faulted with an actionable message; other devices unaffected")
js("[...document.querySelectorAll('.device')][0].querySelectorAll('button')[4].click(); true")
time.sleep(1.5)
shot("12_hardware_recovered", "Recover: mcu0 reconnects, telemetry resumes from sequence 1; event log shows the sequence")

js("document.querySelector('[data-view=backprop]').click(); true")
time.sleep(1.5)
shot("13_backprop", "Backprop: forward pass, one update, gradient check (all <= 1e-5) and 100-step loss curve")

with open(f"{OUT}/shots.json", "w") as f:
    json.dump(log, f, indent=1)
proc.terminate()
print("\n".join(f"{t:6.1f}s  {n}  {d}" for t, n, d in log))
