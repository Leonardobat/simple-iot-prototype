# Fake gpio_output (HAL of plain outputs): same public header as the real component, no GPIO.
idf_component_register(SRCS "fake_gpio_output.c"
                    INCLUDE_DIRS "include"
                                 "${CMAKE_CURRENT_LIST_DIR}/../../../components/gpio_output/include")
