// Consola JSON por el USB nativo (una línea por mensaje). Es lo que usará la página de configuración y la CLI.
//   {"t":"info"}                                  → estado del nodo
//   {"t":"scan"}                                  → redes que ve la placa
//   {"t":"wifi","ssid":"MiRed","pass":"clave"}    → guarda y conecta
//   {"t":"gateway","url":"http://192.168.1.10:8787"}
//   {"t":"name","v":"gas"}
//   {"t":"hiveflow","key":"hf_…","url":"https://api.hiveflow.ai"}   (key "" la borra)
//   {"t":"led","r":0,"g":0,"b":255} · {"t":"led","off":true}
//   {"t":"snap"}                                  → toma una foto y reporta tamaño (prueba de cámara)
//   {"t":"forget"}                                → borra red y clave
//   {"t":"restart"}
#include "config.h"
#include "net.h"
#include "led.h"
#include "hcam.h"
#include "hiveflow.h"
#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void console_printf(const char *fmt, ...) {
    char b[1200]; va_list ap; va_start(ap, fmt); int n = vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    if (n > 0) usb_serial_jtag_write_bytes(b, n < (int)sizeof b ? n : (int)sizeof b - 1, pdMS_TO_TICKS(100));
}

static void copy(cJSON *j, const char *k, char *dst, size_t n) {
    cJSON *v = cJSON_GetObjectItem(j, k);
    if (cJSON_IsString(v)) { strncpy(dst, v->valuestring, n - 1); dst[n - 1] = 0; }
}

static void info(void) {
    console_printf("{\"t\":\"info\",\"fw\":\"hive-node\",\"ver\":\"0.3.1\",\"board\":\"freenove-esp32-s3-wroom\",\"mac\":\"%s\",\"name\":\"%s\","
                   "\"camera\":\"%s\",\"ssid\":\"%s\",\"net\":\"%s\",\"ip\":\"%s\",\"gateway\":\"%s\",\"hiveflow\":\"%s\",\"psram\":%u}\n",
                   g_mac, g_cfg.name, hcam_sensor(), g_cfg.ssid, net_status(), net_ip(), g_cfg.gw, hf_status(),
                   (unsigned)heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
}

static void handle(const char *line) {
    cJSON *j = cJSON_Parse(line);
    if (!j) { console_printf("{\"t\":\"error\",\"error\":\"json\"}\n"); return; }
    cJSON *t = cJSON_GetObjectItem(j, "t");
    const char *c = cJSON_IsString(t) ? t->valuestring : "";
    if (!strcmp(c, "info")) info();
    else if (!strcmp(c, "scan")) { static char b[1100]; net_scan_json(b, sizeof b); console_printf("%s\n", b); }
    else if (!strcmp(c, "wifi")) {
        copy(j, "ssid", g_cfg.ssid, sizeof g_cfg.ssid); copy(j, "pass", g_cfg.pass, sizeof g_cfg.pass);
        config_save(); net_reconnect(); console_printf("{\"t\":\"ack\",\"wifi\":\"%s\"}\n", g_cfg.ssid);
    } else if (!strcmp(c, "gateway")) {
        copy(j, "url", g_cfg.gw, sizeof g_cfg.gw); config_save(); console_printf("{\"t\":\"ack\",\"gateway\":\"%s\"}\n", g_cfg.gw);
    } else if (!strcmp(c, "hiveflow")) {
        copy(j, "key", g_cfg.hftoken, sizeof g_cfg.hftoken); copy(j, "url", g_cfg.hfurl, sizeof g_cfg.hfurl); config_save();
        char m[24]; hf_masked_token(m, sizeof m);
        console_printf("{\"t\":\"ack\",\"hiveflow\":\"%s\",\"key\":\"%s\"}\n", hf_url(), m);
    } else if (!strcmp(c, "name")) {
        copy(j, "v", g_cfg.name, sizeof g_cfg.name); config_save(); console_printf("{\"t\":\"ack\",\"name\":\"%s\"}\n", g_cfg.name);
    } else if (!strcmp(c, "led")) {
        if (cJSON_IsTrue(cJSON_GetObjectItem(j, "off"))) led_override(0, 0, 0, 0);
        else led_override(1, cJSON_GetObjectItem(j, "r") ? cJSON_GetObjectItem(j, "r")->valueint : 0,
                             cJSON_GetObjectItem(j, "g") ? cJSON_GetObjectItem(j, "g")->valueint : 0,
                             cJSON_GetObjectItem(j, "b") ? cJSON_GetObjectItem(j, "b")->valueint : 0);
        console_printf("{\"t\":\"ack\"}\n");
    } else if (!strcmp(c, "forget")) { net_forget();
    } else if (!strcmp(c, "snap")) {                     // prueba de cámara sin red: toma una foto y da su tamaño
        hjpg_t j; bool okk = hcam_jpeg(&j);
        console_printf("{\"t\":\"snap\",\"ok\":%s,\"camera\":\"%s\",\"bytes\":%u,\"jpeg\":%s}\n", okk ? "true" : "false", hcam_sensor(),
                       (unsigned)j.len, okk && j.len > 3 && j.buf[0] == 0xFF && j.buf[1] == 0xD8 ? "true" : "false");
        if (okk) hcam_release(&j);
    } else if (!strcmp(c, "restart")) { console_printf("{\"t\":\"ack\"}\n"); vTaskDelay(pdMS_TO_TICKS(100)); esp_restart(); }
    else console_printf("{\"t\":\"error\",\"error\":\"unknown\",\"cmd\":\"%s\"}\n", c);
    cJSON_Delete(j);
}

static void task(void *a) {
    static char line[512]; int k = 0; uint8_t buf[64];
    for (;;) {
        int n = usb_serial_jtag_read_bytes(buf, sizeof buf, pdMS_TO_TICKS(50));
        for (int i = 0; i < n; i++) {
            char ch = buf[i];
            if (ch == '\n' || ch == '\r') { if (k) { line[k] = 0; handle(line); k = 0; } }
            else if (k < (int)sizeof line - 1) line[k++] = ch;
        }
    }
}

void console_init(void) {
    usb_serial_jtag_driver_config_t c = { .tx_buffer_size = 2048, .rx_buffer_size = 1024 };
    usb_serial_jtag_driver_install(&c);
    xTaskCreate(task, "console", 6144, NULL, 5, NULL);
}
