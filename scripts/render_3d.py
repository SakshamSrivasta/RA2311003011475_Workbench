"""Renders the computed heat-sink temperature fields in the Round 1 Geometry
view and saves screenshots of the 3D viewer for the Round 2 report.
Documentation tooling only (stdlib Python + Chrome); the files are loaded
through the same upload path a user's file picker uses, so the C++ readers
validate them exactly as they would any other file.

Usage: start `Launch --no-browser --port 8790`, then
       python3 scripts/render_3d.py <results-dir> <out-dir> http://127.0.0.1:8790/"""
import base64, json, os, socket, struct, subprocess, sys, time, urllib.request

RESULTS, OUT, APP = sys.argv[1], sys.argv[2], sys.argv[3]
CHROME = os.environ.get("CHROME", "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome")
PORT = 9334
os.makedirs(OUT, exist_ok=True)

proc = subprocess.Popen([CHROME, "--headless=new", f"--remote-debugging-port={PORT}", "--user-data-dir=" + OUT + "/.profile",
                         "--enable-unsafe-swiftshader", "--use-angle=swiftshader", "--hide-scrollbars",
                         "--window-size=1440,1000", "about:blank"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
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
    mask = b"\0\0\0\0"  # an all-zero mask keeps the payload as is (valid, and fast for multi-MB files)
    sock.sendall(bytes(hdr) + mask + data)


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


def wait_for(expr, timeout=60):
    end = time.time() + timeout
    while time.time() < end:
        if js(expr): return True
        time.sleep(0.2)
    return False


def upload(input_id, path):
    text = open(path, encoding="utf-8").read()
    js(f"""(() => {{
      const f = new File([{json.dumps(text)}], {json.dumps(os.path.basename(path))});
      const dt = new DataTransfer(); dt.items.add(f);
      const input = document.getElementById('{input_id}'); input.files = dt.files;
      input.dispatchEvent(new Event('change')); return true; }})()""")


def shot_viewer(name):
    js("document.getElementById('node-tip').hidden = true; true")
    r = js("(() => { const b = document.getElementById('viewer').getBoundingClientRect(); return [b.x, b.y, b.width, b.height]; })()")
    data = call("Page.captureScreenshot", format="png", clip={"x": r[0], "y": r[1], "width": r[2], "height": r[3], "scale": 2})["data"]
    with open(f"{OUT}/{name}.png", "wb") as f:
        f.write(base64.b64decode(data))
    print("saved", name)


def drag(dx, dy):
    """Rotate the orbit camera with a synthetic left-button drag across the canvas."""
    js(f"""(() => {{
      const c = document.querySelector('#viewer canvas'); const r = c.getBoundingClientRect();
      const x0 = r.left + r.width / 2, y0 = r.top + r.height / 2;
      const ev = (type, x, y) => c.dispatchEvent(new PointerEvent(type, {{clientX: x, clientY: y, button: 0, buttons: 1,
                                                  pointerId: 1, pointerType: 'mouse', bubbles: true}}));
      ev('pointerdown', x0, y0);
      for (let i = 1; i <= 10; ++i) ev('pointermove', x0 + {dx} * i / 10, y0 + {dy} * i / 10);
      ev('pointerup', x0 + {dx}, y0 + {dy}); return true; }})()""")


call("Emulation.setEmulatedMedia", features=[{"name": "prefers-color-scheme", "value": "light"}])
call("Page.enable")
for g, label in (("hsA", "A"), ("hsB", "B")):
    call("Page.navigate", url="about:blank")  # a hash-only change would keep the old page state
    time.sleep(0.3)
    call("Page.navigate", url=APP + "#geometry")
    time.sleep(1.5)
    js("document.getElementById('stl-units').value = '1'; document.getElementById('stl-units').dispatchEvent(new Event('change')); true")
    upload("stl-file", os.path.join(RESULTS, f"heatsink_{g}_surface_m.stl"))
    wait_for("document.getElementById('stl-msg').textContent.startsWith('Loaded')")
    upload("csv-file", os.path.join(RESULTS, f"heatsink_{g}_surface_nodal.csv"))
    wait_for("document.getElementById('csv-msg').textContent.startsWith('Loaded')")
    print(g, js("document.getElementById('stl-msg').textContent"), "|", js("document.getElementById('csv-msg').textContent"))
    time.sleep(1.5)
    shot_viewer(f"render_{g}_iso")
    # Underside: the chip footprint and the hot spot on the board side.
    drag(0, -260)
    # Mesh off: from below, its translucent edges hide the colours; the nodes alone read better.
    js("const m = document.getElementById('v-mesh'); if (m.checked) m.click(); true")
    time.sleep(1.0)
    shot_viewer(f"render_{g}_underside")

proc.terminate()
