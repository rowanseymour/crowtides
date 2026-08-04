idf_component_register(SRCS "main.c" "net_time.c" "tides.c" "chart.c" "weather.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES crowpanel579
                                     esp_driver_gpio esp_psram
                                     esp_wifi esp_netif nvs_flash
                                     esp_http_client json mbedtls)
