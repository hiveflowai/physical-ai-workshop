"""Describe lo que ve cada cámara con Claude cuando la escena cambia y se estabiliza.

Se usa desde hive_gateway.py. Sin el paquete `anthropic` o sin credenciales, queda apagado y el gateway sigue
funcionando solo con fotos.
"""
import base64, json, os, threading, time

try:
    import anthropic
except ImportError:          # el gateway funciona sin visión
    anthropic = None

KEY_FILE = os.path.expanduser(os.environ.get("HIVE_ANTHROPIC_KEY_FILE", "~/.config/hive/anthropic_key"))
MODEL = os.environ.get("HIVE_VISION_MODEL", "claude-opus-5-5")
SETTLE_S = 1.2               # la escena debe quedarse quieta este tiempo antes de describirla
MIN_GAP_S = 4.0              # mínimo entre descripciones del mismo nodo

SYSTEM = (
    "Eres los ojos de un enjambre de agentes físicos (HiveFlow). Recibes una foto de una cámara pequeña "
    "OV3660 montada en un nodo ESP32 dentro de un taller o laboratorio. Describe en español, de forma breve y "
    "factual, lo que se ve. Señala riesgos solo si son visibles (humo, fuego, derrame, persona sin equipo de "
    "protección, puerta abierta, objeto caído). Si ves una tarjeta impresa con un dibujo o texto, transcribe "
    "su tema en `card`. No inventes lo que no se ve; si la imagen está oscura o borrosa, dilo."
)
SCHEMA = {
    "type": "object",
    "properties": {
        "summary": {"type": "string", "description": "Una frase, máximo 20 palabras"},
        "people": {"type": "integer", "description": "Personas visibles"},
        "hazards": {"type": "array", "items": {"type": "string"}},
        "objects": {"type": "array", "items": {"type": "string"}},
        "card": {"type": ["string", "null"], "description": "Tema de una tarjeta impresa visible, o null"},
    },
    "required": ["summary", "people", "hazards", "objects", "card"],
    "additionalProperties": False,
}


class Vision:
    def __init__(self, on_event):
        self.on_event = on_event              # callback(mac, description_dict)
        self.client = None
        self.reason = ""
        if anthropic is None:
            self.reason = "falta el paquete anthropic (pip install anthropic)"
            return
        try:
            # La clave se toma de ~/.config/hive/anthropic_key si existe (chmod 600); si no, del entorno.
            key = open(KEY_FILE).read().strip() if os.path.exists(KEY_FILE) else None
            self.client = anthropic.Anthropic(api_key=key) if key else anthropic.Anthropic()
        except Exception as e:                # sin credenciales
            self.reason = f"sin credenciales de Anthropic: {e}"
        if os.environ.get("HIVE_LOCAL_VISION", "0") != "1":   # por defecto la visión la hace HiveFlow
            self.client = None; self.reason = "la visión la hace HiveFlow (HIVE_LOCAL_VISION=1 para usar Claude directo)"
        self.pending = {}                     # mac -> (changed_at, jpeg)
        self.last = {}                        # mac -> time of last description
        self.busy = set()
        self.lock = threading.Lock()


    @property
    def enabled(self):
        return self.client is not None

    def frame(self, mac, jpeg, changed):
        """Cada foto que llega. Las de cambio arman el temporizador; las demás actualizan la última imagen."""
        if not self.enabled:
            return
        now = time.time()
        with self.lock:
            if changed:
                self.pending[mac] = (now, jpeg)
            elif mac in self.pending:
                self.pending[mac] = (self.pending[mac][0], jpeg)

    def describe_now(self, mac, jpeg):
        if self.enabled:
            threading.Thread(target=self._describe, args=(mac, jpeg), daemon=True).start()

    def _loop(self):
        while True:
            time.sleep(0.3)
            now = time.time()
            ready = []
            with self.lock:
                for mac, (t, jpeg) in list(self.pending.items()):
                    if now - t >= SETTLE_S and now - self.last.get(mac, 0) >= MIN_GAP_S and mac not in self.busy:
                        ready.append((mac, jpeg)); del self.pending[mac]; self.busy.add(mac)
            for mac, jpeg in ready:
                threading.Thread(target=self._describe, args=(mac, jpeg), daemon=True).start()

    def _describe(self, mac, jpeg):
        t0 = time.time()
        try:
            resp = self.client.beta.messages.create(
                model=MODEL,
                max_tokens=1024,
                betas=["server-side-fallback-2026-07-01"],
                fallbacks="default",
                output_config={"effort": "low", "format": {"type": "json_schema", "schema": SCHEMA}},
                system=SYSTEM,
                messages=[{"role": "user", "content": [
                    {"type": "image", "source": {"type": "base64", "media_type": "image/jpeg",
                                                 "data": base64.standard_b64encode(jpeg).decode()}},
                    {"type": "text", "text": "¿Qué ve esta cámara?"},
                ]}],
            )
            if resp.stop_reason == "refusal":
                desc = {"error": "el modelo declinó describir esta imagen"}
            else:
                text = next((b.text for b in resp.content if b.type == "text"), "{}")
                desc = json.loads(text)
            desc["latency_s"] = round(time.time() - t0, 1)
            desc["model"] = resp.model
        except anthropic.RateLimitError:
            desc = {"error": "límite de uso de la API, se reintenta con el próximo cambio"}
        except anthropic.APIStatusError as e:
            desc = {"error": f"API {e.status_code}: {e.message}"}
        except anthropic.APIConnectionError:
            desc = {"error": "sin conexión con la API"}
        except (json.JSONDecodeError, StopIteration):
            desc = {"error": "respuesta sin JSON válido"}
        finally:
            with self.lock:
                self.busy.discard(mac); self.last[mac] = time.time()
        self.on_event(mac, desc)
