"""Detecta escenas "asentadas": una cámara reporta un cambio y la imagen se queda quieta SETTLE_S segundos.

Cada escena asentada llama a los consumidores registrados (HiveFlow, visión local…) con la última foto.
"""
import threading, time

SETTLE_S = 1.2       # la escena debe quedarse quieta este tiempo
MIN_GAP_S = 4.0      # mínimo entre escenas del mismo nodo


class Settler:
    def __init__(self):
        self.sinks = []                  # callables(mac, jpeg, meta)
        self.pending = {}                # mac -> [changed_at, jpeg, meta]
        self.last = {}
        self.lock = threading.Lock()
        threading.Thread(target=self._loop, daemon=True).start()

    def add(self, sink):
        self.sinks.append(sink)

    def frame(self, mac, jpeg, changed, meta):
        with self.lock:
            if changed:
                self.pending[mac] = [time.time(), jpeg, meta]
            elif mac in self.pending:
                self.pending[mac][1] = jpeg; self.pending[mac][2] = meta

    def now(self, mac, jpeg, meta):
        """Escena inmediata (p. ej. nodo nuevo)."""
        self._emit(mac, jpeg, meta)

    def _emit(self, mac, jpeg, meta):
        for sink in self.sinks:
            threading.Thread(target=sink, args=(mac, jpeg, meta), daemon=True).start()

    def _loop(self):
        while True:
            time.sleep(0.3)
            now = time.time(); ready = []
            with self.lock:
                for mac, (t, jpeg, meta) in list(self.pending.items()):
                    if now - t >= SETTLE_S and now - self.last.get(mac, 0) >= MIN_GAP_S:
                        ready.append((mac, jpeg, meta)); del self.pending[mac]; self.last[mac] = now
            for r in ready:
                self._emit(*r)
