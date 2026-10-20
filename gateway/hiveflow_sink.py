"""Manda cada escena asentada a HiveFlow como evento de dispositivo con foto (POST /api/hardware/events).

HiveFlow sube la foto, arranca los flows con un nodo Hardware en modo Disparador (nodo cámara, evento
"scene") y el nodo LLM que siga la recibe como imagen.

Configuración (archivos privados, chmod 600):
    ~/.config/hive/hiveflow_token   API key hf_… con scope execute
    ~/.config/hive/hiveflow_url     opcional; por defecto https://api.hiveflow.ai
"""
import base64, json, os, urllib.error, urllib.request

CFG = os.path.expanduser(os.environ.get("HIVE_CONFIG_DIR", "~/.config/hive"))


def _read(name):
    p = os.path.join(CFG, name)
    return open(p).read().strip() if os.path.exists(p) else ""


class HiveFlowSink:
    def __init__(self, log):
        self.log = log
        self.token = os.environ.get("HIVEFLOW_TOKEN") or _read("hiveflow_token")
        self.url = (os.environ.get("HIVEFLOW_API_URL") or _read("hiveflow_url") or "https://api.hiveflow.ai").rstrip("/")
        self.last_status = None

    @property
    def enabled(self):
        return bool(self.token)

    def __call__(self, mac, jpeg, meta):
        if not self.enabled:
            return
        body = {
            "device": "hive_node", "deviceId": mac, "name": meta.get("name"), "event": "scene",
            "data": {"fw": meta.get("fw"), "ip": meta.get("ip"), "bytes": len(jpeg)},
            "image": {"mimeType": "image/jpeg", "data": base64.b64encode(jpeg).decode()},
        }
        req = urllib.request.Request(f"{self.url}/api/hardware/events", data=json.dumps(body).encode(), method="POST",
                                     headers={"Content-Type": "application/json", "Authorization": f"Bearer {self.token}"})
        try:
            with urllib.request.urlopen(req, timeout=15) as r:
                out = json.load(r)
            flows = out.get("flows") or []
            self.last_status = "ok"
            names = ", ".join(f["flowName"] or f["flowId"] for f in flows) or "ningún flow casó"
            self.log(f"[⇢] HiveFlow · {meta.get('name')}: escena enviada · {names}")
        except urllib.error.HTTPError as e:
            try: msg = json.load(e).get("message", "")
            except Exception: msg = ""
            self.last_status = f"http {e.code}"
            hint = {401: "API key inválida", 403: "a la API key le falta el scope execute", 404: "el backend aún no tiene /api/hardware/events",
                    429: "límite de eventos o del plan"}.get(e.code, "")
            self.log(f"[!] HiveFlow {e.code} {hint} {msg}".strip())
        except (urllib.error.URLError, TimeoutError) as e:
            self.last_status = "sin conexión"
            self.log(f"[!] HiveFlow sin conexión: {e}")
