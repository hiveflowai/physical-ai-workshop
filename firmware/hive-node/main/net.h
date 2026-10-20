#pragma once
#include <stdbool.h>
void net_init(void);
void net_reconnect(void);       // tras cambiar ssid/pass
const char *net_status(void);   // no-config | connecting | wifi | online | portal
const char *net_ip(void);
int net_scan_json(char *out, int n);
void net_portal(bool on);       // red propia Hive-xxxx con página de configuración
bool net_portal_on(void);
void net_forget(void);          // borra red y clave guardadas
