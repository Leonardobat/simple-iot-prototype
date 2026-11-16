idf_component_register(SRCS "src/actuator_controller.c"
                    INCLUDE_DIRS "include"
                    PRIV_INCLUDE_DIRS "private_include"
                    PRIV_REQUIRES log json_writer utils rgb_led gpio_output)
