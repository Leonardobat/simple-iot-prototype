idf_component_register(SRCS "src/device_listener.c" "src/firebase_event.c"
                    INCLUDE_DIRS "include"
                    PRIV_INCLUDE_DIRS "private_include"
                    PRIV_REQUIRES firebase_rtdb firebase_paths backoff sensor_provider
                                  actuator_controller log utils)
