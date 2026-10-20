// Manda escenas a HiveFlow: POST {hfurl}/api/hardware/events con la foto en base64.
// HiveFlow sube la foto, arranca los flows con un nodo Hardware (nodo cámara, evento "scene") y el nodo
// LLM que siga recibe la imagen. Sin API key configurada no hace nada.
#include "hiveflow.h"
#include "config.h"
#include "net.h"
#include "led.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "mbedtls/base64.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>

#define FW_VER "0.3.1"
#define MAX_JPEG (200 * 1024)
extern void console_printf(const char *fmt, ...);

static uint8_t *scene; static size_t scene_len;
static volatile bool busy;
static SemaphoreHandle_t go;
static char st[24] = "off";

bool hf_enabled(void) { return g_cfg.hftoken[0] != 0; }
const char *hf_url(void) { return g_cfg.hfurl[0] ? g_cfg.hfurl : "https://api.hiveflow.ai"; }
const char *hf_status(void) { return hf_enabled() ? st : "off"; }
void hf_masked_token(char *out, size_t n) {
    size_t l = strlen(g_cfg.hftoken);
    if (!l) { out[0] = 0; return; }
    if (l < 12) snprintf(out, n, "%.3s…", g_cfg.hftoken);
    else snprintf(out, n, "%.6s…%s", g_cfg.hftoken, g_cfg.hftoken + l - 3);
}

void hf_offer_scene(const uint8_t *jpeg, size_t len) {
    if (!hf_enabled() || busy || !scene || len > MAX_JPEG) return;
    memcpy(scene, jpeg, len); scene_len = len; busy = true;
    xSemaphoreGive(go);
}

static void send_scene(void) {
    size_t b64len = 0;
    mbedtls_base64_encode(NULL, 0, &b64len, scene, scene_len);
    size_t cap = b64len + 512;
    char *body = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
    if (!body) { snprintf(st, sizeof st, "error mem"); return; }
    int k = snprintf(body, cap, "{\"device\":\"hive_node\",\"deviceId\":\"%s\",\"name\":\"%s\",\"event\":\"scene\","
                     "\"data\":{\"fw\":\"%s\",\"ip\":\"%s\",\"bytes\":%u},\"image\":{\"mimeType\":\"image/jpeg\",\"data\":\"",
                     g_mac, g_cfg.name, FW_VER, net_ip(), (unsigned)scene_len);
    size_t w = 0;
    mbedtls_base64_encode((unsigned char *)body + k, cap - k, &w, scene, scene_len);
    k += w; k += snprintf(body + k, cap - k, "\"}}");

    char url[140]; snprintf(url, sizeof url, "%s/api/hardware/events", hf_url());
    esp_http_client_config_t c = { .url = url, .method = HTTP_METHOD_POST, .timeout_ms = 15000,
                                   .crt_bundle_attach = esp_crt_bundle_attach, .buffer_size_tx = 1024 };
    esp_http_client_handle_t h = esp_http_client_init(&c);
    char auth[220]; snprintf(auth, sizeof auth, "Bearer %s", g_cfg.hftoken);
    esp_http_client_set_header(h, "Content-Type", "application/json");
    esp_http_client_set_header(h, "Authorization", auth);
    esp_http_client_set_post_field(h, body, k);
    snprintf(st, sizeof st, "sending");
    esp_err_t e = esp_http_client_perform(h);
    int code = e == ESP_OK ? esp_http_client_get_status_code(h) : -1;
    esp_http_client_cleanup(h);
    heap_caps_free(body);
    if (code >= 200 && code < 300) { snprintf(st, sizeof st, "ok"); led_flash(); }
    else if (code < 0) snprintf(st, sizeof st, "error red");
    else snprintf(st, sizeof st, "error %d", code);
    console_printf("{\"t\":\"hiveflow\",\"scene\":%u,\"status\":\"%s\"}\n", (unsigned)scene_len, st);
}

static void task(void *a) {
    for (;;) {
        xSemaphoreTake(go, portMAX_DELAY);
        send_scene();
        busy = false;
    }
}

void hf_init(void) {
    scene = heap_caps_malloc(MAX_JPEG, MALLOC_CAP_SPIRAM);
    go = xSemaphoreCreateBinary();
    snprintf(st, sizeof st, "idle");
    xTaskCreatePinnedToCore(task, "hiveflow", 8192, NULL, 4, NULL, 0);
}
