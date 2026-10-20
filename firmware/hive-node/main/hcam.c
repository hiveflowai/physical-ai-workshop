// Cámara en el conector de la Freenove ESP32-S3-WROOM (pines del pinout oficial).
// Freenove trae dos sensores según el lote: OV3660 (JPEG por hardware) y GC0308 (sin JPEG).
// Se intenta JPEG; si el sensor no lo soporta, se reinicia en RGB565 y cada foto se comprime en la placa.
#include "hcam.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>

static bool ok, soft_jpeg; static char sensor[24] = "none";

static camera_config_t cfg(pixformat_t fmt, framesize_t size) {
    camera_config_t c = {
        .pin_pwdn = -1, .pin_reset = -1, .pin_xclk = 15,
        .pin_sccb_sda = 4, .pin_sccb_scl = 5,
        .pin_d7 = 16, .pin_d6 = 17, .pin_d5 = 18, .pin_d4 = 12,
        .pin_d3 = 10, .pin_d2 = 8,  .pin_d1 = 9,  .pin_d0 = 11,
        .pin_vsync = 6, .pin_href = 7, .pin_pclk = 13,
        .xclk_freq_hz = 20000000, .ledc_timer = LEDC_TIMER_0, .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = fmt, .frame_size = size, .jpeg_quality = 12,
        .fb_count = 2, .fb_location = CAMERA_FB_IN_PSRAM, .grab_mode = CAMERA_GRAB_LATEST,
    };
    return c;
}

bool hcam_init(void) {
    camera_config_t c = cfg(PIXFORMAT_JPEG, FRAMESIZE_VGA);
    ok = esp_camera_init(&c) == ESP_OK;
    if (!ok) {                                        // sensor sin JPEG: modo crudo + compresión en la placa
        esp_camera_deinit();
        c = cfg(PIXFORMAT_RGB565, FRAMESIZE_QVGA);
        ok = esp_camera_init(&c) == ESP_OK;
        soft_jpeg = ok;
    }
    if (ok) {
        sensor_t *s = esp_camera_sensor_get();
        camera_sensor_info_t *i = esp_camera_sensor_get_info(&s->id);
        snprintf(sensor, sizeof sensor, "%s%s", i ? i->name : "unknown", soft_jpeg ? " (jpeg sw)" : "");
        s->set_vflip(s, 1);                           // la Freenove monta el sensor invertido
    }
    return ok;
}

bool hcam_jpeg(hjpg_t *out) {
    out->buf = NULL; out->len = 0; out->fb = NULL; out->owned = false;
    if (!ok) return false;
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) return false;
    if (fb->format == PIXFORMAT_JPEG) { out->buf = fb->buf; out->len = fb->len; out->fb = fb; return true; }
    uint8_t *jb = NULL; size_t jl = 0;
    bool conv = frame2jpg(fb, 70, &jb, &jl);
    esp_camera_fb_return(fb);
    if (!conv) { free(jb); return false; }
    out->buf = jb; out->len = jl; out->owned = true;
    return true;
}

void hcam_release(hjpg_t *j) {
    if (j->owned) free(j->buf);
    else if (j->fb) esp_camera_fb_return((camera_fb_t *)j->fb);
    j->buf = NULL; j->fb = NULL; j->len = 0;
}

bool hcam_ok(void) { return ok; }
const char *hcam_sensor(void) { return sensor; }
