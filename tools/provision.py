#!/usr/bin/env python3
"""Prepara placas Freenove para el enjambre, una tras otra: graba hive-node, les pone nombre, API key y wifi.

    python3 provision.py                 # numera desde mesa-1
    python3 provision.py --start 3       # siguiente placa = mesa-3
    python3 provision.py --reflash       # regraba aunque ya tenga hive-node

Conecta cada placa por el conector USB. Si trae el firmware de fábrica, entra al modo de grabación:
mantén BOOT, pulsa y suelta RST, suelta BOOT. El script la detecta solo y te avisa cuándo desconectarla.

Lee la API key de ~/.config/hive/hiveflow_token y el wifi (opcional) de ~/.config/hive/wifi
(primera línea la red, segunda la clave). Nunca imprime la key ni la clave.
"""
import argparse, glob, json, os, re, subprocess, sys, time
import serial

HERE = os.path.dirname(os.path.abspath(__file__))
IMG = os.path.join(HERE, "..", "firmware", "bin", "hive-node-0.3.1-esp32s3.bin")
INV = os.path.join(HERE, "..", "inventario.md")   # se crea al aprovisionar; no se versiona
CFG = os.path.expanduser("~/.config/hive")


def ports():
    return set(glob.glob("/dev/cu.usbmodem*"))


def open_port(p):
    s = serial.Serial(); s.port = p; s.baudrate = 115200; s.timeout = 0.2
    s.dtr = False; s.rts = False          # no tocar las líneas de reset
    s.open(); return s


def read_for(s, secs, until=None):
    t = time.time(); buf = b""
    while time.time() - t < secs:
        try:
            buf += s.read(4096)
        except (serial.SerialException, OSError):
            break
        if until and until in buf:
            break
    return buf.decode(errors="replace")


def esptool(port, *args, timeout=180):
    cmd = [sys.executable, "-m", "esptool", "--chip", "esp32s3", "--port", port, *args]
    return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)


def state(port):
    """'hive-node' si ya corre nuestro firmware, 'bootloader' si espera grabación, 'unknown' si no responde."""
    try:
        s = open_port(port)
    except serial.SerialException:
        return "busy", ""
    out = read_for(s, 1.5)
    if "waiting for download" in out:
        s.close(); return "bootloader", out
    s.write(b'{"t":"info"}\n'); out += read_for(s, 3, b'"t":"info"')
    s.close()
    if '"fw":"hive-node"' in out:
        return "hive-node", out
    return "unknown", out


def send(port, obj, want, secs=4):
    s = open_port(port); time.sleep(0.3); s.reset_input_buffer()
    s.write((json.dumps(obj) + "\n").encode())
    out = read_for(s, secs, f'"t":"{want}"'.encode()); s.close()
    for line in out.splitlines():
        if line.startswith("{") and f'"t":"{want}"' in line:
            try: return json.loads(line)
            except ValueError: pass
    return None


def wait_boot(secs=15):
    t = time.time()
    while time.time() - t < secs:
        ps = ports()
        if ps:
            p = sorted(ps)[0]
            st, out = state(p)
            if st == "hive-node":
                return p
        time.sleep(1)
    return None


def inventory_row(mac, flash_mb, name):
    try:
        lines = open(INV).read().split("\n")
    except OSError:
        return
    today = time.strftime("%Y-%m-%d")
    row = f"| {name} | Freenove ESP32-S3-WROOM | {mac} | {flash_mb} | 8 MB | {name} · enjambre | hive-node 0.3.0 | {today} |"
    for i, l in enumerate(lines):
        if mac in l:
            lines[i] = row; break
    else:
        for i, l in enumerate(lines):
            if re.match(r"^\| \d+ \| Freenove ESP32-S3-WROOM \|\s*\|", l):
                lines[i] = row; break
        else:
            lines.append(row)
    open(INV, "w").write("\n".join(lines))


def provision(port, name, key, wifi, reflash):
    st, _ = state(port)
    flash_mb = "?"
    if st == "bootloader" or (reflash and st == "hive-node"):
        r = esptool(port, "--before", "no-reset" if st == "bootloader" else "default-reset", "--after", "no-reset", "flash-id")
        m = re.search(r"Detected flash size:\s*(\d+MB)", r.stdout); flash_mb = m.group(1).replace("MB", " MB") if m else "?"
        print(f"  grabando hive-node 0.3.0 (flash {flash_mb})…", flush=True)
        r = esptool(port, "--before", "no-reset", "--after", "watchdog-reset", "--baud", "921600",
                    "write-flash", "-z", "0x0", IMG, timeout=240)
        if "Hash of data verified" not in r.stdout:
            print("  ✗ la grabación falló:", (r.stdout + r.stderr).strip().splitlines()[-1:]); return False
        time.sleep(3)
        port = wait_boot(20)
        if not port:
            print("  ✗ grabada, pero no arrancó. Pulsa RST una vez."); return False
    elif st == "busy":
        print("  ✗ el puerto está ocupado por otro programa (¿Chrome con la página de configuración?)"); return False
    elif st == "unknown":
        print("  ✗ la placa no responde. Ponla en modo grabación: mantén BOOT, pulsa RST, suelta BOOT."); return False

    # En macOS abrir el puerto reinicia la placa: se abre UNA vez, se espera su "hello" y todo va por esa conexión.
    sp = open_port(port); read_for(sp, 10, b'"t":"hello"'); time.sleep(1.5)
    def cmd(obj, want, secs=4):
        sp.reset_input_buffer(); sp.write((json.dumps(obj) + "\n").encode())
        out = read_for(sp, secs, f'"t":"{want}"'.encode())
        for line in out.splitlines():
            if line.startswith("{") and f'"t":"{want}"' in line:
                try: return json.loads(line)
                except ValueError: pass
        return None
    info = cmd({"t": "info"}, "info") or {}
    mac = info.get("mac", "?")
    snap = {}
    for _ in range(3):                                          # foto de prueba: confirma la cámara de verdad
        snap = cmd({"t": "snap"}, "snap", 6) or {}
        if snap.get("ok") and snap.get("jpeg"): break
        time.sleep(1.5)
    cam = f"{snap.get('camera','?')} ✓ ({snap.get('bytes',0)} B)" if snap.get("ok") and snap.get("jpeg") else f"✗ {snap.get('camera', info.get('camera','?'))} · revisa el cable de la cámara"
    ok = cmd({"t": "name", "v": name}, "ack")
    ok2 = cmd({"t": "hiveflow", "key": key, "url": ""}, "ack") if key else None
    ok3 = cmd({"t": "wifi", "ssid": wifi[0], "pass": wifi[1]}, "ack") if wifi else None
    cmd({"t": "led", "r": 0, "g": 255, "b": 120}, "ack")         # verde fijo: lista
    inventory_row(mac, flash_mb, name)
    print(f"  ✓ {name} · MAC {mac} · cámara {cam} · nombre {'✓' if ok else '✗'} · "
          f"API key {'✓' if ok2 else '—'} · wifi {('✓ ' + wifi[0]) if ok3 else '— (se configura en el lugar)'}")
    cmd({"t": "led", "off": True}, "ack")
    sp.close()
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--start", type=int, default=1)
    ap.add_argument("--prefix", default="mesa-")
    ap.add_argument("--reflash", action="store_true")
    ap.add_argument("--with-key", action="store_true", help="cargar también la API key de ~/.config/hive/hiveflow_token")
    ap.add_argument("--with-wifi", action="store_true", help="cargar también el wifi de ~/.config/hive/wifi")
    a = ap.parse_args()
    # Por defecto las placas salen limpias: sin API key ni wifi de nadie. Cada quien los pone en su página.
    tok = os.path.join(CFG, "hiveflow_token"); wf = os.path.join(CFG, "wifi")
    key = open(tok).read().strip() if a.with_key and os.path.exists(tok) else ""
    wifi = tuple((open(wf).read().split("\n") + ["", ""])[:2]) if a.with_wifi and os.path.exists(wf) else None
    print(f"API key: {'se carga' if key else 'ninguna'} · wifi: {wifi[0] if wifi else 'ninguno'}")
    n = a.start
    print(f"Conecta la placa para {a.prefix}{n}…", flush=True)
    while True:
        before = ports()
        while not ports():
            time.sleep(0.5)
        port = sorted(ports())[0]
        time.sleep(1.5)
        print(f"• placa detectada en {port}", flush=True)
        if provision(port, f"{a.prefix}{n}", key, wifi, a.reflash):
            print(f"  → desconéctala. Siguiente: {a.prefix}{n + 1}", flush=True); n += 1
        else:
            print("  → desconéctala y vuelve a intentar.", flush=True)
        while ports():
            time.sleep(0.5)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
