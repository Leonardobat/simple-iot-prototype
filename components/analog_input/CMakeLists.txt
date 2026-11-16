idf_component_register(SRCS "src/analog_input.c"
                    INCLUDE_DIRS "include"
                    PRIV_INCLUDE_DIRS "private_include"
                    PRIV_REQUIRES esp_adc log)
