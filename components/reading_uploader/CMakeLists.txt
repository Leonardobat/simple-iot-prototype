idf_component_register(SRCS "src/reading_uploader.c"
                    INCLUDE_DIRS "include"
                    PRIV_INCLUDE_DIRS "private_include"
                    PRIV_REQUIRES reading_store data_repository sensor_provider
                                  actuator_controller backoff log freertos)
