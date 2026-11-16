idf_component_register(SRCS "test_sensor_provider.c"
                    PRIV_REQUIRES unity unity_main sensor_provider chip_temperature analog_input
                    WHOLE_ARCHIVE)
