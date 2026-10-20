// hive-node 0.1: nodo del enjambre físico HiveFlow sobre Freenove ESP32-S3-WROOM + OV2640.
// Arranca, se conecta al wifi guardado y manda fotos al gateway cuando la escena cambia.
#include "config.h"
#include "led.h"
#include "hcam.h"
#include "net.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void console_init(void);
void console_printf(const char *fmt, ...);

void app_main(void) {
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) { nvs_flash_erase(); nvs_flash_init(); }
    config_load();
    led_init();
    console_init();
    bool cam = hcam_init();
    if (!cam) led_state(LED_ERROR);
    net_init();
    vTaskDelay(pdMS_TO_TICKS(300));
    console_printf("{\"t\":\"hello\",\"fw\":\"hive-node\",\"ver\":\"0.3.1\",\"mac\":\"%s\",\"name\":\"%s\",\"camera\":\"%s\",\"ssid\":\"%s\"}\n",
                   g_mac, g_cfg.name, hcam_sensor(), g_cfg.ssid);
}
