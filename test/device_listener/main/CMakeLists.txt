idf_component_register(SRCS "test_device_listener.c"
                    PRIV_REQUIRES unity unity_main device_listener sensor_provider
                                  actuator_controller http_client firebase_rtdb rgb_led
                    WHOLE_ARCHIVE)
