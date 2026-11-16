idf_component_register(SRCS "src/sensor_provider.c" "src/ldr.c" "src/lm35.c" "src/mq135.c"
                    INCLUDE_DIRS "include"
                    PRIV_INCLUDE_DIRS "private_include"
                    PRIV_REQUIRES esp_hw_support log json_writer chip_temperature analog_input)
