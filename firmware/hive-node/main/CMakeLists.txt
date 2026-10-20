idf_component_register(
    SRCS "main.c" "config.c" "led.c" "hcam.c" "net.c" "console.c" "dns.c" "hiveflow.c"
    INCLUDE_DIRS "."
    EMBED_TXTFILES "portal.html"
    REQUIRES nvs_flash esp_wifi esp_netif esp_event esp_http_client esp_http_server esp_timer json
             esp_driver_usb_serial_jtag esp_driver_gpio lwip mbedtls esp-tls
)
