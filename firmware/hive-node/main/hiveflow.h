#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
void hf_init(void);
bool hf_enabled(void);
void hf_offer_scene(const uint8_t *jpeg, size_t len);   // copia la foto y la manda en segundo plano
const char *hf_status(void);                            // off | idle | sending | ok | error <código>
const char *hf_url(void);
void hf_masked_token(char *out, size_t n);              // hf_ab12…xyz para mostrar sin revelar la clave
