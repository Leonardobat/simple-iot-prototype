# Fake chip_temperature (HAL of the internal sensor): same public header as the real component.
idf_component_register(SRCS "fake_chip_temperature.c"
                    INCLUDE_DIRS "include"
                                 "${CMAKE_CURRENT_LIST_DIR}/../../../components/chip_temperature/include")
