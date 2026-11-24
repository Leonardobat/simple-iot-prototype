idf_component_register(SRCS "test_integration.c"
                    PRIV_REQUIRES unity unity_menu_main connectivity_provider firebase_rtdb
                                  firebase_auth firebase_paths data_repository device_listener
                                  sensor_provider actuator_controller analog_input utils flash_fs
                                  reading_store config_store reading_uploader esp_wifi
                    WHOLE_ARCHIVE)
