idf_component_register(SRCS "src/chip_temperature.c"
                    INCLUDE_DIRS "include"
                    PRIV_INCLUDE_DIRS "private_include"
                    PRIV_REQUIRES esp_driver_tsens log)
