# Fake analog_input (HAL of the ADC input): same public header as the real component.
idf_component_register(SRCS "fake_analog_input.c"
                    INCLUDE_DIRS "include"
                                 "${CMAKE_CURRENT_LIST_DIR}/../../../components/analog_input/include")
