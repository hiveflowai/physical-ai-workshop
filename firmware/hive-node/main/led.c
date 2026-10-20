// WS2812 de la Freenove en GPIO48. Ámbar tenue = sin wifi configurado · azul respirando = conectando ·
// verde respirando = wifi sin gateway · verde fijo = en línea · rojo respirando = error de cámara · violeta respirando = portal de configuración abierto.
#include "led.h"
#include "led_strip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include <math.h>

#define PIN_RGB 48
static led_strip_handle_t strip;
static volatile led_state_t st = LED_NOCONF;
static volatile int64_t flash_until;
static volatile int ov; static volatile uint8_t ovr, ovg, ovb;

static void task(void *a) {
    for (;;) {
        float t = esp_timer_get_time() / 1e6f, br = 0.55f + 0.45f * sinf(t * 3.0f), k = 0.18f;
        uint8_t r = 0, g = 0, b = 0;
        if (esp_timer_get_time() < flash_until) { r = g = b = 255; k = 0.35f; }
        else if (ov) { r = ovr; g = ovg; b = ovb; k = 0.3f; }
        else switch (st) {
            case LED_NOCONF:     r = 240; g = 150; b = 30; k = 0.06f; break;
            case LED_CONNECTING: r = 40;  g = 80;  b = 255; k *= br; break;
            case LED_WIFI:       r = 30;  g = 220; b = 90;  k *= br; break;
            case LED_ONLINE:     r = 30;  g = 220; b = 90;  break;
            case LED_ERROR:      r = 255; g = 30;  b = 40;  k *= br; break;
            case LED_PORTAL:     r = 140; g = 80;  b = 255; k *= br; break;
        }
        led_strip_set_pixel(strip, 0, r * k, g * k, b * k);
        led_strip_refresh(strip);
        vTaskDelay(pdMS_TO_TICKS(33));
    }
}

void led_init(void) {
    led_strip_config_t sc = { .strip_gpio_num = PIN_RGB, .max_leds = 1, .led_model = LED_MODEL_WS2812 };
    led_strip_rmt_config_t rc = { .clk_src = RMT_CLK_SRC_DEFAULT, .resolution_hz = 10 * 1000 * 1000 };
    if (led_strip_new_rmt_device(&sc, &rc, &strip) != ESP_OK) return;
    led_strip_clear(strip);
    xTaskCreate(task, "led", 2560, NULL, 3, NULL);
}
void led_state(led_state_t s) { st = s; }
void led_flash(void) { flash_until = esp_timer_get_time() + 60000; }
void led_override(int on, uint8_t r, uint8_t g, uint8_t b) { ovr = r; ovg = g; ovb = b; ov = on; }
