# Physical AI Workshop · Hiveflow

**Physical AI + MCP + Swarm Protocol: Claude operando hardware real, de un agente a un enjambre.**

Una placa ESP32-S3 con cámara manda una foto cada vez que cambia la escena. Claude la describe dentro de un
flow de [Hiveflow](https://hiveflow.ai), y un flow coordinador junta lo que ven todas las cámaras para decidir
si hay que avisar a una persona.

```
ESP32 + cámara ──foto──> Hiveflow ──> flow "Vigía" (Claude describe) ──reporte──┐
Teléfono ──foto por chat──> flow "Cámara virtual" (Claude describe) ──reporte──┴──> flow "Enjambre" ──> alerta
```

---

## Guía del alumno

Tiempo total: unos 10 minutos. Necesitas un teléfono y la placa de tu mesa.

### A. En Hiveflow (5 min)

1. Regístrate en **[app.hiveflow.ai/signup](https://app.hiveflow.ai/signup)**.
2. Ve a **Perfil → Token API**, crea una key y **cópiala**. Solo se muestra una vez.
3. Ve a **Plantillas**, busca **"Vigía"** y usa **Vigía con cámara · ESP32**.
4. Abre el flow que se creó y ponlo en marcha con **▶ play**.

### B. Tu placa (3 min)

5. Conecta la placa a la corriente por el conector marcado **USB**. El LED respira en **violeta**.
6. En el wifi de tu teléfono, únete a la red **`Hive-mesa-N`** (la de tu etiqueta). No tiene clave.
7. Se abre sola la página de la placa. Si no, entra a **`http://192.168.4.1`**.
   En iPhone, si pregunta, elige **"Usar sin internet"**.
8. **Primero, la sección HiveFlow:** pega tu API key y toca **Guardar API key**.
9. **Después, la sección Wifi:** elige la red del evento, escribe su clave y toca **Guardar y conectar**.
10. En unos segundos aparece **"Conectado"** y un recuadro con **la dirección de tu placa**
    (por ejemplo `http://192.168.1.42`). **Tómale foto:** con ella vuelves a entrar.
11. El LED queda en **verde fijo** y la placa apaga su red. Tu teléfono vuelve solo al wifi del evento.

> Pega la API key **antes** de poner el wifi. Al conectarse, la placa apaga su red a los 20 segundos.

### C. Comprueba (1 min)

12. Pon algo frente a la cámara (una tarjeta, tu mano) y déjalo quieto un segundo.
13. En Hiveflow, abre la ejecución de tu flow Vigía: ahí está el reporte de Claude.

### ¿Sin placa? Cámara virtual

Si no te tocó placa, tu teléfono hace de cámara:

1. Haz los pasos 1 y 2.
2. En **Plantillas**, busca **"Cámara virtual"** y usa **Cámara virtual · Vigía por chat**.
3. Pon el flow en **▶ play**. Abre el nodo **Foto por chat**, publica el chat y abre su enlace en tu teléfono.
4. Escribe tu nombre, adjunta una foto y envía.

Claude describe la foto en el mismo formato del Vigía y el reporte llega al mismo Enjambre que las placas.
Lo ves en la ejecución de tu flow.

### Si algo sale mal

| LED | Qué significa | Qué hacer |
|---|---|---|
| Violeta respirando | La red de la placa está abierta, sin wifi | Sigue desde el paso 6 |
| Azul respirando | Intentando conectarse al wifi | Espera. Si a los 45 s vuelve a violeta, la clave del wifi está mal: repite el paso 9 |
| Verde respirando | Tiene wifi pero no llega a Hiveflow | Revisa la API key y que tu flow esté en ▶ play |
| Verde fijo | En línea, mandando a Hiveflow | Todo bien |
| Rojo | La cámara falla | Llama al organizador |

**Volver a entrar a la página de la placa**

- Desde el wifi del evento: abre la dirección del paso 10.
- Si la perdiste: sostén **BOOT 3 segundos** (LED violeta), únete a `Hive-mesa-N` y entra a `http://192.168.4.1`.
- **BOOT 10 segundos** borra el wifi y la API key de la placa.

> El nombre `http://mesa-N.local` funciona en redes de casa con Mac o iPhone, pero muchas redes de oficinas y
> eventos lo bloquean. La dirección numérica y la red `Hive-mesa-N` funcionan siempre.

---

## Cómo funciona

| Pieza | Dónde vive | Qué hace |
|---|---|---|
| **Placa** (`firmware/hive-node`) | ESP32-S3 | Detecta cambios de escena, manda la foto a `POST /api/hardware/events` con tu API key. Sirve su propia página de configuración |
| **Vigía con cámara · ESP32** | Plantilla pública de Hiveflow | Nodo Hardware (Nodo cámara, Cambio de escena) → Claude describe la foto en formato fijo → reporte al Enjambre |
| **Cámara virtual · Vigía por chat** | Plantilla pública de Hiveflow | Chat input (foto desde el teléfono) → el mismo Claude vigía → reporte al Enjambre. Para quien no tiene placa |
| **Enjambre · coordinador** | Flow del organizador | Guarda cada reporte. Si trae riesgo, Claude revisa los recientes de todos los nodos y, si hay alerta, manda un correo |

La placa **no ejecuta a Claude**: solo ve y manda. La visión y las decisiones ocurren en Hiveflow.

**Seguridad**

- La API key se guarda en la placa y nunca se muestra completa (solo `hf_ab12…xyz`).
- La placa no recibe órdenes de internet: solo manda fotos. Su página solo se abre desde tu red local o desde su propia red.
- Cada foto gasta créditos de **tu** cuenta de Hiveflow. Pausa tu flow cuando no lo uses.

---

## Hardware

- **Freenove ESP32-S3-WROOM** (N8R8 o N16R8) con cámara **OV3660** u **GC0308**.
  El GC0308 no comprime en JPEG por hardware: el firmware lo hace en la placa (fotos de 320×240).
- Cable USB-C con datos. Conector **USB** (nativo), no el **UART**.

## Para el organizador

**Grabar placas nuevas** (necesitas Python 3 y `pip install pyserial esptool`):

```bash
python3 tools/provision.py --start 1     # graba el firmware y nombra mesa-1, mesa-2, …
```

Pon cada placa en modo grabación la primera vez: mantén **BOOT**, pulsa y suelta **RST**, suelta **BOOT**.
Las placas salen **sin wifi ni API key**. `--with-wifi` y `--with-key` existen, pero úsalos solo con placas propias.

**Identificar placas ya grabadas** (dice cuál mesa es y la deja verde 5 s para etiquetarla):

```bash
python3 tools/identify.py
```

**Compilar el firmware** (ESP-IDF 5.3):

```bash
cd firmware/hive-node
idf.py set-target esp32s3 && idf.py build
```

**Consola USB de la placa** (`tools/hive_node.py info | scan | snap | forget | name mesa-3`). En macOS, abrir el
puerto USB reinicia la placa.

**Receptor local** (`gateway/`, opcional): panel con todas las cámaras sin internet, en `http://localhost:8790`.

---

Hecho por [Hiveflow](https://hiveflow.ai) para el workshop *Physical AI + MCP + Swarm Protocol*. Licencia MIT.
