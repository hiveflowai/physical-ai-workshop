#!/usr/bin/env python3
"""Configura un nodo hive-node por USB (consola JSON del firmware). Requiere pyserial.

    hive_node.py info
    hive_node.py scan
    hive_node.py wifi "MiRed"              # pide la clave sin mostrarla (o HIVE_WIFI_PASS)
    hive_node.py wifi "MiRed" --keychain   # la toma del llavero de macOS; nunca se imprime
    hive_node.py gateway http://192.168.1.10:8790
    hive_node.py name gas
    hive_node.py led 0 0 255 | led off
    hive_node.py forget                    # borra red y clave guardadas
    hive_node.py restart | monitor

El puerto se autodetecta (/dev/cu.usbmodem*); fíjalo con HIVE_PORT.
"""
import getpass, glob, json, os, subprocess, sys, time
import serial

def port():
    p = os.environ.get("HIVE_PORT") or next(iter(sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/ttyACM*"))), None)
    if not p: sys.exit("No encuentro ningún nodo por USB.")
    s = serial.Serial(); s.port = p; s.baudrate = 115200; s.timeout = 0.2
    s.dtr = False; s.rts = False   # no tocar las líneas de reset: el S3 entraría al bootloader
    try:
        s.open()
    except serial.SerialException as e:
        if "busy" in str(e).lower():
            sys.exit(f"El puerto {p} está ocupado por otro programa (por ejemplo la página de configuración abierta en Chrome). "
                     "Ciérrala o pulsa Desconectar y vuelve a intentar. No se cambió nada en la placa.")
        raise
    return s

def send(s, obj, wait=2.5, want=None):
    s.reset_input_buffer(); s.write((json.dumps(obj) + "\n").encode())
    end = time.time() + wait; buf = b""
    while time.time() < end:
        buf += s.read(4096)
        for line in buf.decode(errors="replace").splitlines():
            if line.startswith("{") and (want is None or f'"t":"{want}"' in line):
                return json.loads(line)
    return None

def main(a):
    if not a: sys.exit(__doc__)
    s = port(); c = a[0]
    if c == "info": print(json.dumps(send(s, {"t": "info"}, want="info"), indent=2, ensure_ascii=False))
    elif c == "scan":
        r = send(s, {"t": "scan"}, 10, "scan") or {}
        for ap in r.get("aps", []): print(f'{ap["rssi"]:>4} dBm  {"abierta " if ap["open"] else "        "} {ap["ssid"]}')
    elif c == "wifi":
        ssid = a[1]
        if "--keychain" in a:
            pw = subprocess.run(["security", "find-generic-password", "-wa", ssid], capture_output=True, text=True)
            if pw.returncode: sys.exit(f'No hay clave para "{ssid}" en el llavero.')
            pw = pw.stdout.strip()
        else:
            pw = os.environ.get("HIVE_WIFI_PASS") or getpass.getpass(f'Clave de "{ssid}": ')
        print(send(s, {"t": "wifi", "ssid": ssid, "pass": pw}, want="ack") and f'Guardada la red "{ssid}". Conectando…')
        end = time.time() + 20; buf = b""
        while time.time() < end:
            buf += s.read(4096)
            if b'"wifi":"connected"' in buf:
                line = next(l for l in buf.decode(errors="replace").splitlines() if '"wifi":"connected"' in l)
                print("Conectado, IP", json.loads(line)["ip"]); return
        print("Aún sin conexión: revisa la clave o el alcance (el LED queda azul mientras intenta).")
    elif c == "gateway": print(send(s, {"t": "gateway", "url": a[1]}, want="ack"))
    elif c == "name": print(send(s, {"t": "name", "v": a[1]}, want="ack"))
    elif c == "led": print(send(s, {"t": "led", "off": True} if a[1] == "off" else {"t": "led", "r": int(a[1]), "g": int(a[2]), "b": int(a[3])}, want="ack"))
    elif c == "forget":
        r = send(s, {"t": "forget"}, want="forget")
        print("Red y clave borradas. La placa abre su red Hive; el LED queda en violeta." if r else "La placa no confirmó. ¿Tiene hive-node 0.2 o más?")
    elif c == "restart": print(send(s, {"t": "restart"}, want="ack"))
    elif c == "monitor":
        while True:
            l = s.readline().decode(errors="replace").rstrip()
            if l: print(l)
    else: sys.exit(__doc__)

if __name__ == "__main__":
    main(sys.argv[1:])
