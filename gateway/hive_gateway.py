#!/usr/bin/env python3
"""Gateway mínimo del enjambre físico (v0.1): recibe las fotos de los nodos hive-node y las muestra en una cuadrícula.

    python3 hive_gateway.py            # escucha en 0.0.0.0:8787
    abrir http://localhost:8787

Cada nodo hace POST /frame con el JPEG y las cabeceras X-Node-Mac, X-Node-Name, X-Node-Ip, X-Changed.
Solo librería estándar. Siguientes pasos: descripción con un modelo de visión, sobre SwarmMind y tools MCP.
"""
import json, os, shutil, socket, subprocess, sys, threading, time
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler

PORT = int(os.environ.get("HIVE_GW_PORT", "8787"))
SAVE_DIR = os.environ.get("HIVE_GW_SAVE")      # si se define, guarda cada foto con cambio ahí
nodes = {}                                     # mac -> {name, ip, fw, last, changed_at, frames, jpeg, seen}
lock = threading.Lock()
events = []                                    # últimos eventos de visión (los más nuevos al final)

def on_vision(mac, desc):
    with lock:
        d = nodes.get(mac)
        if d is None: return
        d["seen"] = desc; d["seen_at"] = time.time()
        events.append({"t": time.time(), "mac": mac, "name": d["name"], **desc}); del events[:-200]
    if "error" in desc:
        print(f"[!] visión {d['name']}: {desc['error']}", flush=True)
    else:
        extra = f" · riesgos: {', '.join(desc['hazards'])}" if desc["hazards"] else ""
        card = f" · tarjeta: {desc['card']}" if desc.get("card") else ""
        print(f"[👁] {d['name']}: {desc['summary']}{extra}{card} ({desc['latency_s']} s)", flush=True)

import vision as _vision
import scene as _scene
import hiveflow_sink as _hf
VISION = _vision.Vision(on_vision)
HIVEFLOW = _hf.HiveFlowSink(lambda m: print(m, flush=True))
SCENES = _scene.Settler()
if HIVEFLOW.enabled: SCENES.add(HIVEFLOW)
if VISION.enabled: SCENES.add(lambda mac, jpeg, meta: VISION.describe_now(mac, jpeg))

PAGE = """<!doctype html><html lang=es><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>Hive Gateway</title><style>
:root{--bg:#0B0A12;--panel:#171421;--ink:#ECE9F5;--soft:#A49DBD;--violet:#9B7BFF;--green:#4ADE80;--amber:#F0B254}
body{margin:0;background:var(--bg);color:var(--ink);font:14px system-ui,sans-serif}
header{padding:14px 18px;display:flex;gap:12px;align-items:baseline}h1{font-size:16px;margin:0;letter-spacing:.12em}
header span{color:var(--soft);font-size:12px}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(300px,1fr));gap:12px;padding:0 18px 18px}
.card{background:var(--panel);border:1px solid #2a2440;border-radius:10px;overflow:hidden}
.card img{width:100%;aspect-ratio:4/3;object-fit:cover;display:block;background:#000}
.card .m{padding:8px 10px;display:flex;justify-content:space-between;gap:8px;font-size:12px;color:var(--soft)}
.card b{color:var(--ink)} .card.flash{border-color:var(--violet);box-shadow:0 0 18px #7951ee66}
.dot{display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:6px;background:var(--green)}
.stale .dot{background:var(--amber)} .empty{padding:40px 18px;color:var(--soft)}\n.seen{padding:0 10px 10px;font-size:13px;color:var(--ink);min-height:20px}.seen .hz{color:#FF5C7A}.seen .cd{color:var(--amber)}.seen .er{color:var(--soft)}
</style><header><h1>HIVE GATEWAY</h1><span id=s></span><a href="/setup" style="margin-left:auto;color:#9B7BFF">+ conectar nodo</a></header><div class=grid id=g></div>
<script>
const g=document.getElementById('g'),s=document.getElementById('s'),seen={};
async function tick(){try{const r=await fetch('/nodes');const ns=await r.json();s.textContent=ns.length+' nodos';
if(!ns.length){g.innerHTML='<div class=empty>Esperando nodos… conecta una Freenove con hive-node y configura el gateway.</div>';return}
if(g.querySelector('.empty'))g.innerHTML='';
for(const n of ns){let c=document.getElementById('n'+n.id);if(!c){c=document.createElement('div');c.className='card';c.id='n'+n.id;
c.innerHTML='<img><div class=m><span><i class=dot></i><b></b></span><span class=t></span></div><div class=seen></div>';g.appendChild(c)}
const age=n.age;const sv=c.querySelector('.seen'),x=n.seen;if(x){sv.innerHTML='';const p=document.createElement('div');if(x.error){p.className='er';p.textContent='visión: '+x.error}else{p.textContent='👁 '+x.summary;if(x.hazards.length){const h=document.createElement('div');h.className='hz';h.textContent='⚠ '+x.hazards.join(', ');p.appendChild(h)}if(x.card){const k=document.createElement('div');k.className='cd';k.textContent='🂠 tarjeta: '+x.card;p.appendChild(k)}}sv.appendChild(p)}c.classList.toggle('stale',age>12);c.querySelector('b').textContent=n.name;
c.querySelector('.t').textContent=n.ip+' · '+(age<1?'ahora':Math.round(age)+' s');
if(seen[n.id]!==n.frames){seen[n.id]=n.frames;c.querySelector('img').src='/frame/'+n.id+'.jpg?'+n.frames;
if(n.changed_age<1.5){c.classList.add('flash');setTimeout(()=>c.classList.remove('flash'),700)}}}}catch(e){s.textContent='sin conexión'}}
setInterval(tick,700);tick();
</script></html>"""

def node_id(mac): return mac.replace(":", "")

def lan_ip():
    """IP de esta máquina en la red local: es la dirección que los nodos usan para llegar al gateway."""
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as u:
            u.connect(("10.255.255.255", 1)); return u.getsockname()[0]
    except OSError:
        return "127.0.0.1"

SETUP = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "setup.html"), "rb").read()

class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass

    def do_POST(self):
        if self.path != "/frame":
            return self.send_error(404)
        n = int(self.headers.get("Content-Length", "0"))
        if not 0 < n < 2_000_000:
            return self.send_error(413)
        jpeg = self.rfile.read(n)
        mac = self.headers.get("X-Node-Mac", "unknown")
        changed = self.headers.get("X-Changed") == "1"
        now = time.time()
        with lock:
            first = mac not in nodes
            d = nodes.setdefault(mac, {"frames": 0, "changed_at": 0})
            d.update(name=self.headers.get("X-Node-Name", mac), ip=self.headers.get("X-Node-Ip", self.client_address[0]),
                     fw=self.headers.get("X-Node-Fw", "?"), last=now, jpeg=jpeg)
            d["frames"] += 1
            if changed: d["changed_at"] = now
        meta = {"name": d["name"], "ip": d["ip"], "fw": d["fw"]}
        SCENES.frame(mac, jpeg, changed, meta)
        if first:
            SCENES.now(mac, jpeg, meta)
            print(f"[+] nodo nuevo {d['name']} {mac} {d['ip']} fw {d['fw']}", flush=True)
        if changed:
            print(f"[~] cambio de escena en {d['name']} ({len(jpeg)//1024} KB)", flush=True)
            if SAVE_DIR:
                os.makedirs(SAVE_DIR, exist_ok=True)
                with open(os.path.join(SAVE_DIR, f"{node_id(mac)}-{int(now*1000)}.jpg"), "wb") as f: f.write(jpeg)
        self.send_response(204); self.end_headers()

    def do_GET(self):
        if self.path in ("/", "/index.html"):
            b = PAGE.encode(); self.send_response(200); self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(b))); self.end_headers(); self.wfile.write(b); return
        if self.path in ("/setup", "/setup/"):
            self.send_response(200); self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(SETUP))); self.end_headers(); self.wfile.write(SETUP); return
        if self.path == "/api/lan":
            b = json.dumps({"gateway": f"http://hive.local:{PORT}", "ip": f"http://{lan_ip()}:{PORT}"}).encode(); self.send_response(200)
            self.send_header("Content-Type", "application/json"); self.send_header("Content-Length", str(len(b))); self.end_headers(); self.wfile.write(b); return
        if self.path == "/events":
            with lock: b = json.dumps(events[-50:]).encode()
            self.send_response(200); self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(b))); self.end_headers(); self.wfile.write(b); return
        if self.path == "/nodes":
            now = time.time()
            with lock:
                out = [{"id": node_id(m), "mac": m, "name": d["name"], "ip": d["ip"], "fw": d["fw"], "frames": d["frames"],
                        "age": now - d["last"], "changed_age": now - d["changed_at"],
                        "seen": d.get("seen"), "seen_age": now - d.get("seen_at", 0)} for m, d in sorted(nodes.items(), key=lambda x: x[1]["name"])]
            b = json.dumps(out).encode(); self.send_response(200); self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(b))); self.end_headers(); self.wfile.write(b); return
        if self.path.startswith("/frame/"):
            nid = self.path[7:].split(".jpg")[0]
            with lock:
                d = next((d for m, d in nodes.items() if node_id(m) == nid), None)
                jpeg = d and d.get("jpeg")
            if not jpeg: return self.send_error(404)
            self.send_response(200); self.send_header("Content-Type", "image/jpeg"); self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(jpeg))); self.end_headers(); self.wfile.write(jpeg); return
        self.send_error(404)

def announce():
    """Publica el nombre hive.local en la red (mDNS/Bonjour) para que los nodos encuentren el gateway aunque cambie la IP."""
    if shutil.which("dns-sd"):   # macOS
        return subprocess.Popen(["dns-sd", "-P", "hive-gateway", "_http._tcp", "local", str(PORT), "hive.local", lan_ip()],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if shutil.which("avahi-publish"):   # Linux
        return subprocess.Popen(["avahi-publish", "-a", "-R", "hive.local", lan_ip()], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print("aviso: sin dns-sd ni avahi-publish; los nodos necesitarán la IP en vez de hive.local", flush=True)

if __name__ == "__main__":
    announce()
    print("HiveFlow:", f"escenas a {HIVEFLOW.url}" if HIVEFLOW.enabled else "apagado (falta ~/.config/hive/hiveflow_token)", flush=True)
    print("visión local:", f"activa con {_vision.MODEL}" if VISION.enabled else f"apagada ({VISION.reason})", flush=True)
    print(f"hive-gateway · panel http://localhost:{PORT} · configurar nodo http://localhost:{PORT}/setup · nodos -> http://{lan_ip()}:{PORT}", flush=True)
    ThreadingHTTPServer(("0.0.0.0", PORT), H).serve_forever()
