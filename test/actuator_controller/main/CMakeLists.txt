idf_component_register(SRCS "test_actuator_controller.c"
                    PRIV_REQUIRES unity unity_main actuator_controller rgb_led gpio_output
                    WHOLE_ARCHIVE)
