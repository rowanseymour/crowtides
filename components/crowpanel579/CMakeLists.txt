idf_component_register(SRCS "epd.c" "epd_fb.c" "text.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_driver_gpio esp_driver_spi)
