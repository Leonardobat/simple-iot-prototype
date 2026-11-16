idf_component_register(SRCS "src/reading_store.c"
                    INCLUDE_DIRS "include"
                    PRIV_INCLUDE_DIRS "private_include"
                    REQUIRES sensor_provider actuator_controller
                    PRIV_REQUIRES flash_fs utils log freertos)
