// Wifi, portal de configuración, servidor HTTP propio y envío de fotos al gateway del enjambre.
//
// Sin red configurada (o sin poder conectarse 45 s, o BOOT sostenido 3 s) la placa abre su propia red
// abierta "Hive-xxxx". El teléfono que se une ve la página de configuración servida por la placa.
// Una foto sale al gateway cuando la imagen cambia (> CHANGE_PCT en tamaño de JPEG) o cada HEARTBEAT_MS.
// El gateway por defecto es http://hive.local:8790 (se resuelve por mDNS), así sobrevive a cambios de IP.
#include "net.h"
#include "config.h"
#include "led.h"
#include "hcam.h"
#include "hiveflow.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_camera.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "mdns.h"
#include "driver/gpio.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define FW_VER          "0.3.1"
#define CHANGE_PCT      6
#define HEARTBEAT_MS    5000
#define TICK_MS         600
#define PORTAL_AFTER_S  45
#define SETTLE_MS       1200    // la escena debe quedarse quieta este tiempo antes de mandarla a HiveFlow
#define SCENE_GAP_MS    4000    // mínimo entre escenas

extern void console_printf(const char *fmt, ...);
void dns_start(void); void dns_stop(void);
extern const char portal_html_start[] asm("_binary_portal_html_start");
extern const char portal_html_end[]   asm("_binary_portal_html_end");

static const char *status = "no-config";
static char ip[20];
static EventGroupHandle_t evg;
#define EV_IP BIT0
static int64_t last_ok_us, down_since_us;
static bool portal, portal_forced;
static volatile bool hold_reconnect;          // el escaneo pausa los reintentos de conexión          // forced = abierto a mano con BOOT
static int64_t portal_since_us, last_client_us, saved_at_us;
#define FORCED_IDLE_S 300                    // portal abierto a mano: se cierra tras 5 min sin nadie conectado
static esp_netif_t *ap_netif;

static const char *gw(void) { return g_cfg.gw; }   // receptor local opcional (organizador)
static void set_led(void) {
    if (portal) led_state(LED_PORTAL);
    else if (!hcam_ok()) led_state(LED_ERROR);
    else if (!g_cfg.ssid[0]) led_state(LED_NOCONF);
    else if (!ip[0]) led_state(LED_CONNECTING);
    else led_state(!strcmp(status, "online") ? LED_ONLINE : LED_WIFI);
}

static void evt(void *a, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) { if (g_cfg.ssid[0]) esp_wifi_connect(); }
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *d = data;
        static uint8_t last_reason; static int64_t last_log;
        int64_t now = esp_timer_get_time();
        if (d->reason != last_reason || now - last_log > 10000000LL) {   // sin inundar la consola
            console_printf("{\"t\":\"net\",\"wifi\":\"disconnected\",\"ssid\":\"%s\",\"reason\":%d,\"rssi\":%d}\n", g_cfg.ssid, d->reason, d->rssi);
            last_reason = d->reason; last_log = now;
        }
        if (ip[0]) down_since_us = esp_timer_get_time();
        ip[0] = 0; xEventGroupClearBits(evg, EV_IP);
        if (g_cfg.ssid[0] && !hold_reconnect) { if (!portal) status = "connecting"; vTaskDelay(pdMS_TO_TICKS(1000)); esp_wifi_connect(); }
        set_led();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = data; snprintf(ip, sizeof ip, IPSTR, IP2STR(&e->ip_info.ip));
        status = "wifi"; down_since_us = 0; xEventGroupSetBits(evg, EV_IP); set_led();
        console_printf("{\"t\":\"net\",\"wifi\":\"connected\",\"ssid\":\"%s\",\"ip\":\"%s\"}\n", g_cfg.ssid, ip);
    }
}

static void apply_cfg(void) {
    wifi_config_t wc = { 0 };
    strncpy((char *)wc.sta.ssid, g_cfg.ssid, sizeof wc.sta.ssid - 1);
    strncpy((char *)wc.sta.password, g_cfg.pass, sizeof wc.sta.password - 1);
    wc.sta.threshold.authmode = g_cfg.pass[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    esp_wifi_set_config(WIFI_IF_STA, &wc);
}

// Cambia de red sin pelear con el manejador de eventos: si hay conexión, se desconecta y el evento
// DISCONNECTED reconecta con la config nueva; si no la hay, conecta directo. Se ejecuta en su propia tarea
// para que la respuesta HTTP de /api/save salga antes de soltar la red.
static void reconnect_task(void *a) {
    vTaskDelay(pdMS_TO_TICKS(400));
    apply_cfg(); down_since_us = esp_timer_get_time();
    if (!g_cfg.ssid[0]) { status = "no-config"; ip[0] = 0; esp_wifi_disconnect(); set_led(); vTaskDelete(NULL); }
    status = "connecting";
    if (ip[0]) esp_wifi_disconnect(); else esp_wifi_connect();
    set_led(); vTaskDelete(NULL);
}
void net_reconnect(void) { xTaskCreate(reconnect_task, "reconnect", 3072, NULL, 5, NULL); }
const char *net_status(void) { return portal && !ip[0] ? "portal" : status; }
const char *net_ip(void) { return ip; }
bool net_portal_on(void) { return portal; }

void net_portal(bool on) {
    if (on == portal) return;
    portal = on;
    if (on) {
        wifi_ap_record_t cur; uint8_t ch = 1;
        if (esp_wifi_sta_get_ap_info(&cur) == ESP_OK) ch = cur.primary;   // el AP comparte radio con el STA
        wifi_config_t ap = { .ap = { .channel = ch, .max_connection = 4, .authmode = WIFI_AUTH_OPEN } };
        int n = snprintf((char *)ap.ap.ssid, sizeof ap.ap.ssid, "Hive-%s", g_cfg.name + (strncmp(g_cfg.name, "hive-", 5) ? 0 : 5));
        ap.ap.ssid_len = n;
        esp_wifi_set_mode(WIFI_MODE_APSTA); esp_wifi_set_config(WIFI_IF_AP, &ap);
        dns_start(); portal_since_us = last_client_us = esp_timer_get_time(); saved_at_us = 0;
        console_printf("{\"t\":\"portal\",\"on\":true,\"ssid\":\"%s\",\"url\":\"http://192.168.4.1\"}\n", ap.ap.ssid);
    } else {
        dns_stop(); esp_wifi_set_mode(WIFI_MODE_STA); portal_forced = false;
        console_printf("{\"t\":\"portal\",\"on\":false}\n");
    }
    set_led();
}

int net_scan_json(char *out, int n) {
    wifi_scan_config_t sc = { .show_hidden = false };
    esp_err_t se = esp_wifi_scan_start(&sc, true);
    if (se != ESP_OK && !ip[0]) {          // estaba reintentando conectar: pausa, escanea y retoma
        hold_reconnect = true; esp_wifi_disconnect(); vTaskDelay(pdMS_TO_TICKS(300));
        se = esp_wifi_scan_start(&sc, true);
        hold_reconnect = false;
        if (g_cfg.ssid[0]) esp_wifi_connect();
    }
    if (se != ESP_OK) return snprintf(out, n, "{\"t\":\"scan\",\"aps\":[],\"error\":\"busy %d\"}", se);
    uint16_t cnt = 20; wifi_ap_record_t *aps = calloc(cnt, sizeof *aps);
    esp_wifi_scan_get_ap_records(&cnt, aps);
    int k = snprintf(out, n, "{\"t\":\"scan\",\"aps\":[");
    for (int i = 0; i < cnt && k < n - 90; i++) {
        char esc[70]; int e = 0;
        for (const char *p = (char *)aps[i].ssid; *p && e < 66; p++) { if (*p == '"' || *p == '\\') esc[e++] = '\\'; esc[e++] = *p; }
        esc[e] = 0;
        k += snprintf(out + k, n - k, "%s{\"ssid\":\"%s\",\"rssi\":%d,\"open\":%s}", i ? "," : "", esc, aps[i].rssi,
                      aps[i].authmode == WIFI_AUTH_OPEN ? "true" : "false");
    }
    k += snprintf(out + k, n - k, "]}");
    free(aps);
    return k;
}

// Borra red y clave guardadas; la placa queda sin wifi y abre su portal.
void net_forget(void) {
    g_cfg.ssid[0] = 0; g_cfg.pass[0] = 0; config_save();
    console_printf("{\"t\":\"forget\",\"ok\":true}\n");
    net_reconnect();
}

// ---- servidor HTTP de la placa ----
static esp_err_t send_json(httpd_req_t *r, const char *s) {
    httpd_resp_set_type(r, "application/json"); httpd_resp_set_hdr(r, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store"); return httpd_resp_sendstr(r, s);
}
static esp_err_t h_root(httpd_req_t *r) {
    httpd_resp_set_type(r, "text/html; charset=utf-8");
    return httpd_resp_send(r, portal_html_start, portal_html_end - portal_html_start);
}
static esp_err_t h_capture(httpd_req_t *r) {
    hjpg_t fb;
    if (!hcam_jpeg(&fb)) { httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "camera"); return ESP_FAIL; }
    httpd_resp_set_type(r, "image/jpeg"); httpd_resp_set_hdr(r, "Access-Control-Allow-Origin", "*"); httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    esp_err_t e = httpd_resp_send(r, (const char *)fb.buf, fb.len);
    hcam_release(&fb); return e;
}
static esp_err_t h_info(httpd_req_t *r) {
    char b[560], tok[24]; hf_masked_token(tok, sizeof tok);
    snprintf(b, sizeof b, "{\"fw\":\"hive-node\",\"ver\":\"%s\",\"mac\":\"%s\",\"name\":\"%s\",\"camera\":\"%s\",\"ssid\":\"%s\",\"net\":\"%s\",\"ip\":\"%s\",\"gateway\":\"%s\",\"portal\":%s,"
             "\"hiveflow\":{\"url\":\"%s\",\"key\":\"%s\",\"status\":\"%s\"}}",
             FW_VER, g_mac, g_cfg.name, hcam_sensor(), g_cfg.ssid, net_status(), ip, gw(), portal ? "true" : "false",
             hf_url(), tok, hf_status());
    return send_json(r, b);
}
static esp_err_t h_scan(httpd_req_t *r) { static char b[1400]; net_scan_json(b, sizeof b); return send_json(r, b); }
static esp_err_t h_save(httpd_req_t *r) {
    char body[400]; int n = httpd_req_recv(r, body, sizeof body - 1);
    if (n <= 0) return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "body");
    body[n] = 0; cJSON *j = cJSON_Parse(body);
    if (!j) return httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "json");
    #define CP(k, dst) do { cJSON *v = cJSON_GetObjectItem(j, k); if (cJSON_IsString(v)) { strncpy(dst, v->valuestring, sizeof dst - 1); dst[sizeof dst - 1] = 0; } } while (0)
    char oldssid[33], oldpass[65]; strcpy(oldssid, g_cfg.ssid); strcpy(oldpass, g_cfg.pass);
    CP("ssid", g_cfg.ssid); CP("pass", g_cfg.pass); CP("name", g_cfg.name); CP("gateway", g_cfg.gw);
    CP("hiveflowKey", g_cfg.hftoken); CP("hiveflowUrl", g_cfg.hfurl);
    bool wifi_changed = strcmp(oldssid, g_cfg.ssid) || strcmp(oldpass, g_cfg.pass);
    cJSON_Delete(j); config_save(); if (wifi_changed) saved_at_us = esp_timer_get_time();
    send_json(r, "{\"ok\":true}");
    if (wifi_changed) net_reconnect();   // cambiar solo la API key o el nombre no tumba el wifi
    return ESP_OK;
}
static esp_err_t h_forget(httpd_req_t *r) { send_json(r, "{\"ok\":true}"); net_forget(); return ESP_OK; }
// Cualquier otra ruta (detección de portal de iOS/Android/Windows) → a la página de configuración.
static esp_err_t h_404(httpd_req_t *r, httpd_err_code_t e) {
    httpd_resp_set_status(r, "302 Found"); httpd_resp_set_hdr(r, "Location", "http://192.168.4.1/");
    return httpd_resp_send(r, NULL, 0);
}
static void start_httpd(void) {
    httpd_config_t c = HTTPD_DEFAULT_CONFIG(); c.max_uri_handlers = 12; c.lru_purge_enable = true; httpd_handle_t s;
    if (httpd_start(&s, &c) != ESP_OK) return;
    const httpd_uri_t u[] = {
        { .uri = "/", .method = HTTP_GET, .handler = h_root },
        { .uri = "/capture.jpg", .method = HTTP_GET, .handler = h_capture },
        { .uri = "/info", .method = HTTP_GET, .handler = h_info },
        { .uri = "/api/scan", .method = HTTP_GET, .handler = h_scan },
        { .uri = "/api/save", .method = HTTP_POST, .handler = h_save },
        { .uri = "/api/forget", .method = HTTP_POST, .handler = h_forget },
    };
    for (int i = 0; i < sizeof u / sizeof *u; i++) httpd_register_uri_handler(s, &u[i]);
    httpd_register_err_handler(s, HTTPD_404_NOT_FOUND, h_404);
}

// ---- envío al gateway ----
static bool post_frame(hjpg_t *fb, bool changed) {
    if (!gw()[0]) return false;
    char url[128]; snprintf(url, sizeof url, "%s/frame", gw());
    esp_http_client_config_t c = { .url = url, .method = HTTP_METHOD_POST, .timeout_ms = 4000 };
    esp_http_client_handle_t h = esp_http_client_init(&c);
    if (!h) return false;
    esp_http_client_set_header(h, "Content-Type", "image/jpeg");
    esp_http_client_set_header(h, "X-Node-Mac", g_mac);
    esp_http_client_set_header(h, "X-Node-Name", g_cfg.name);
    esp_http_client_set_header(h, "X-Node-Ip", ip);
    esp_http_client_set_header(h, "X-Node-Fw", FW_VER);
    esp_http_client_set_header(h, "X-Changed", changed ? "1" : "0");
    esp_http_client_set_post_field(h, (const char *)fb->buf, fb->len);
    bool ok = esp_http_client_perform(h) == ESP_OK && esp_http_client_get_status_code(h) / 100 == 2;
    esp_http_client_cleanup(h);
    return ok;
}

// Toma fotos cada TICK_MS. Al receptor local (si hay) le manda cambios y latidos; a HiveFlow solo escenas
// "asentadas": hubo un cambio y la imagen se quedó quieta SETTLE_MS, con SCENE_GAP_MS entre escenas.
static void uploader(void *a) {
    size_t last_len = 0; int64_t last_post = 0, pending_at = 0, last_scene = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(TICK_MS));
        if (!(xEventGroupGetBits(evg) & EV_IP) || !hcam_ok()) continue;
        hjpg_t jp; hjpg_t *fb = &jp;
        if (!hcam_jpeg(fb)) continue;
        int64_t now = esp_timer_get_time();
        size_t d = fb->len > last_len ? fb->len - last_len : last_len - fb->len;
        bool changed = last_len && d * 100 > last_len * CHANGE_PCT;
        if (changed) pending_at = now;
        if (!last_scene && hf_enabled() && last_len) pending_at = now - SETTLE_MS * 1000LL;   // primera escena al arrancar
        if (pending_at && !changed && now - pending_at >= SETTLE_MS * 1000LL && now - last_scene >= SCENE_GAP_MS * 1000LL) {
            hf_offer_scene(fb->buf, fb->len); pending_at = 0; last_scene = now;
        }
        if (gw()[0] && (changed || now - last_post > HEARTBEAT_MS * 1000LL)) {
            if (post_frame(fb, changed)) { last_ok_us = now; if (changed) led_flash(); }
            last_post = now;
        }
        if (!strcmp(hf_status(), "ok")) last_ok_us = now;
        bool up = now - last_ok_us < 15000000LL;
        if (up && strcmp(status, "online")) { status = "online"; set_led(); }
        else if (!up && !strcmp(status, "online")) { status = "wifi"; set_led(); }
        last_len = fb->len;
        hcam_release(fb);
    }
}

// Supervisor del portal.
//  · Abierto solo (sin red o 45 s sin conectar): se cierra 20 s después de conectar.
//  · Abierto a mano (BOOT 3 s): sigue abierto aunque la placa tenga wifi; se cierra 20 s después de guardar
//    una red y conectar, o tras 5 min sin ningún teléfono conectado.
//  · BOOT 10 s: olvida red y clave.
static int ap_clients(void) { wifi_sta_list_t l; return esp_wifi_ap_get_sta_list(&l) == ESP_OK ? l.num : 0; }

static void supervisor(void *a) {
    gpio_config_t io = { .pin_bit_mask = 1ULL << 0, .mode = GPIO_MODE_INPUT, .pull_up_en = 1 }; gpio_config(&io);
    int64_t held = 0, connected_at = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(200));
        int64_t now = esp_timer_get_time();
        if (!gpio_get_level(0)) {
            if (!held) held = now;
            if (now - held > 3000000LL && !portal) { portal_forced = true; net_portal(true); connected_at = 0; }
            if (now - held > 10000000LL) { held = now + 3600000000LL; net_forget(); led_flash(); }
        } else held = 0;
        if (!g_cfg.ssid[0]) { net_portal(true); continue; }
        if (portal && ap_clients()) last_client_us = now;
        if (portal && portal_forced) {
            bool saved_and_up = saved_at_us && ip[0];
            if (saved_and_up) { if (!connected_at) connected_at = now; else if (now - connected_at > 20000000LL) net_portal(false); }
            else if (now - last_client_us > FORCED_IDLE_S * 1000000LL) net_portal(false);
            continue;
        }
        if (ip[0]) {
            if (portal) { if (!connected_at) connected_at = now; else if (now - connected_at > 20000000LL) net_portal(false); }
        } else {
            connected_at = 0;
            if (!portal && down_since_us && now - down_since_us > PORTAL_AFTER_S * 1000000LL) net_portal(true);
        }
    }
}

void net_init(void) {
    evg = xEventGroupCreate();
    esp_netif_init(); esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta(); ap_netif = esp_netif_create_default_wifi_ap();
    wifi_init_config_t ic = WIFI_INIT_CONFIG_DEFAULT(); esp_wifi_init(&ic);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, evt, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, evt, NULL);
    esp_wifi_set_mode(WIFI_MODE_STA); esp_wifi_set_ps(WIFI_PS_NONE);
    apply_cfg(); esp_wifi_start();
    if (g_cfg.ssid[0]) { status = "connecting"; down_since_us = esp_timer_get_time(); }
    if (mdns_init() == ESP_OK) {   // la placa se anuncia como <nombre>.local
        mdns_hostname_set(g_cfg.name); mdns_instance_name_set("HiveFlow node");
        mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    }
    hf_init(); set_led(); start_httpd();
    xTaskCreatePinnedToCore(uploader, "uploader", 6144, NULL, 4, NULL, 1);
    xTaskCreate(supervisor, "supervisor", 3072, NULL, 3, NULL);
}
