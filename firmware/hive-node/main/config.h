// Ajustes persistentes en NVS: wifi, gateway del enjambre y nombre del nodo.
#pragma once
typedef struct {
    char ssid[33], pass[65];
    char gw[96];     // opcional: receptor local del organizador (http://hive.local:8790); vacío = no se usa
    char hftoken[200]; // API key hf_… de HiveFlow (scope execute): la placa manda las escenas directo
    char hfurl[96];    // vacío = https://api.hiveflow.ai
    char name[24];   // nombre legible; vacío = hive-<4 últimos hex de la MAC>
} hive_cfg_t;
extern hive_cfg_t g_cfg;
extern char g_mac[18];
void config_load(void);
void config_save(void);
