#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
// Una foto en JPEG lista para mandar. Si el sensor da JPEG por hardware (OV3660, OV2640) es el propio
// framebuffer; si no (GC0308 y otros), es una copia comprimida por software que hay que liberar.
typedef struct { uint8_t *buf; size_t len; void *fb; bool owned; } hjpg_t;
bool hcam_init(void);
bool hcam_ok(void);
const char *hcam_sensor(void);
bool hcam_jpeg(hjpg_t *out);
void hcam_release(hjpg_t *j);
