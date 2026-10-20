// DNS mínimo para el portal: responde cualquier consulta A con la IP de la placa (192.168.4.1),
// así el teléfono detecta "red con inicio de sesión" y abre la página de configuración solo.
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static volatile int run;
static TaskHandle_t th;

static void task(void *a) {
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(53), .sin_addr.s_addr = htonl(INADDR_ANY) };
    bind(s, (struct sockaddr *)&addr, sizeof addr);
    struct timeval tv = { .tv_sec = 1 }; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    uint8_t b[512];
    while (run) {
        struct sockaddr_in from; socklen_t fl = sizeof from;
        int n = recvfrom(s, b, sizeof b - 16, 0, (struct sockaddr *)&from, &fl);
        if (n < 12) continue;
        int q = 12; while (q < n && b[q]) q += b[q] + 1;          // fin del nombre consultado
        if (q + 5 > n) continue;
        uint16_t qtype = (b[q + 1] << 8) | b[q + 2];
        int end = q + 5;
        b[2] = 0x81; b[3] = 0x80;                                  // respuesta, recursión disponible, sin error
        b[6] = 0; b[7] = qtype == 1 ? 1 : 0; b[8] = b[9] = b[10] = b[11] = 0;
        if (qtype == 1) {
            const uint8_t ans[] = { 0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4, 192, 168, 4, 1 };
            memcpy(b + end, ans, sizeof ans); end += sizeof ans;
        }
        sendto(s, b, end, 0, (struct sockaddr *)&from, fl);
    }
    close(s); th = NULL; vTaskDelete(NULL);
}
void dns_start(void) { if (th) return; run = 1; xTaskCreate(task, "dns", 3072, NULL, 4, &th); }
void dns_stop(void) { run = 0; }
