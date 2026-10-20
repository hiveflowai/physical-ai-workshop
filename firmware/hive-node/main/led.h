#pragma once
#include <stdint.h>
typedef enum { LED_NOCONF, LED_CONNECTING, LED_WIFI, LED_ONLINE, LED_ERROR, LED_PORTAL } led_state_t;
void led_init(void);
void led_state(led_state_t s);
void led_flash(void);                          // destello blanco: foto enviada
void led_override(int on, uint8_t r, uint8_t g, uint8_t b);   // color fijo pedido por el enjambre
