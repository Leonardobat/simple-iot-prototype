idf_component_register(SRCS "src/gpio_output.c"
                    INCLUDE_DIRS "include"
                    PRIV_REQUIRES esp_driver_gpio log)
