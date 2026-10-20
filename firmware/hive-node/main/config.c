#include "config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_mac.h"
#include <string.h>
#include <stdio.h>

hive_cfg_t g_cfg;
char g_mac[18];
#define NS "hive"

static void get_str(nvs_handle_t h, const char *k, char *dst, size_t n) { size_t len = n; if (nvs_get_str(h, k, dst, &len) != ESP_OK) dst[0] = 0; }

void config_load(void) {
    uint8_t m[6]; esp_read_mac(m, ESP_MAC_WIFI_STA);
    snprintf(g_mac, sizeof g_mac, "%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
    memset(&g_cfg, 0, sizeof g_cfg);
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        get_str(h, "ssid", g_cfg.ssid, sizeof g_cfg.ssid);
        get_str(h, "pass", g_cfg.pass, sizeof g_cfg.pass);
        get_str(h, "gw", g_cfg.gw, sizeof g_cfg.gw);
        get_str(h, "name", g_cfg.name, sizeof g_cfg.name);
        get_str(h, "hftok", g_cfg.hftoken, sizeof g_cfg.hftoken);
        get_str(h, "hfurl", g_cfg.hfurl, sizeof g_cfg.hfurl);
        nvs_close(h);
    }
    if (!g_cfg.name[0]) snprintf(g_cfg.name, sizeof g_cfg.name, "hive-%02x%02x", m[4], m[5]);
}

void config_save(void) {
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "ssid", g_cfg.ssid); nvs_set_str(h, "pass", g_cfg.pass);
    nvs_set_str(h, "gw", g_cfg.gw); nvs_set_str(h, "name", g_cfg.name);
    nvs_set_str(h, "hftok", g_cfg.hftoken); nvs_set_str(h, "hfurl", g_cfg.hfurl);
    nvs_commit(h); nvs_close(h);
}
