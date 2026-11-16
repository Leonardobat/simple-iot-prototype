# Fake rgb_led (HAL of the RGB LED): same public header as the real component, no RMT.
idf_component_register(SRCS "fake_rgb_led.c"
                    INCLUDE_DIRS "include"
                                 "${CMAKE_CURRENT_LIST_DIR}/../../../components/rgb_led/include")
