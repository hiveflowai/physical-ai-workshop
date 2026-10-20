#!/usr/bin/env python3
"""Identifica cada placa al conectarla, le borra el wifi guardado, la actualiza a la última versión y prueba la cámara.

    python3 identify.py

El LED se queda verde 5 s cuando termina: así sabes cuál etiquetar. Espera 2 s entre desconectar una
placa y conectar la siguiente. En macOS abrir el puerto reinicia la placa: una sola conexión por placa.
"""
import glob, json, os, subprocess, sys, time
import serial

HERE = os.path.dirname(os.path.abspath(__file__))
APP = os.path.join(HERE, "..", "firmware", "hive-node", "build", "hive-node.bin")   # compílalo antes: idf.py build
VER = "0.3.1"


def ports(): return sorted(glob.glob("/dev/cu.usbmodem*"))


def read_for(s, secs, until=None):
    t = time.time(); buf = b""
    while time.time() - t < secs:
        try: buf += s.read(4096)
        except (serial.SerialException, OSError): break
        if until and until in buf: break
    return buf


def session(port):
    s = serial.Serial(); s.port = port; s.baudrate = 115200; s.timeout = 0.2; s.dtr = False; s.rts = False; s.open()
    read_for(s, 10, b'"t":"hello"'); time.sleep(1.5)
    def cmd(o, want, secs=5):
        s.reset_input_buffer(); s.write((json.dumps(o) + "\n").encode())
        out = read_for(s, secs, f'"t":"{want}"'.encode()).decode(errors="replace")
        return next((json.loads(l) for l in out.splitlines() if l.startswith("{") and f'"t":"{want}"' in l), None)
    return s, cmd


def handle(port):
    s, cmd = session(port)
    info = cmd({"t": "info"}, "info") or {}
    if info.get("fw") != "hive-node":
        print("• placa sin hive-node (¿de fábrica?): pásala por provision.py", flush=True); s.close(); return
    name, mac = info.get("name", "?"), info.get("mac", "?")
    print(f"• ESTA ES {name.upper()} · MAC {mac} → ponle la etiqueta «{name}»", flush=True)
    if info.get("ver") != VER:
        s.close()
        r = subprocess.run([sys.executable, "-m", "esptool", "--chip", "esp32s3", "--port", port, "--before", "default-reset",
                            "--after", "watchdog-reset", "--baud", "921600", "write-flash", "-z", "0x20000", APP],
                           capture_output=True, text=True, timeout=180)
        print(f"  {'✓ actualizada a ' + VER if 'Hash of data verified' in r.stdout else '✗ no se pudo actualizar'}", flush=True)
        time.sleep(3)
        while not ports(): time.sleep(0.5)
        s, cmd = session(ports()[0])
    if (cmd({"t": "info"}, "info") or {}).get("ssid"):
        cmd({"t": "wifi", "ssid": "", "pass": ""}, "ack")
    info = cmd({"t": "info"}, "info") or {}
    snap = {}
    for _ in range(3):
        snap = cmd({"t": "snap"}, "snap", 6) or {}
        if snap.get("ok"): break
        time.sleep(1.5)
    print(f"  ✓ wifi: {'ninguno' if not info.get('ssid') else '✗ ' + info['ssid']} · API key: {'ninguna' if info.get('hiveflow') == 'off' else '✗ tiene'}"
          f" · cámara {snap.get('camera','?')} {'✓' if snap.get('ok') else '✗'} · firmware {info.get('ver')}", flush=True)
    cmd({"t": "led", "r": 0, "g": 255, "b": 120}, "ack"); time.sleep(5); cmd({"t": "led", "off": True}, "ack")
    s.close()
    print("  → desconéctala, espera 2 s y conecta la siguiente", flush=True)


print("Conecta una placa…", flush=True)
while True:
    while not ports(): time.sleep(0.5)
    time.sleep(1.5)
    try: handle(ports()[0])
    except Exception as e: print(f"  ✗ error: {e}", flush=True)
    while ports(): time.sleep(0.5)
