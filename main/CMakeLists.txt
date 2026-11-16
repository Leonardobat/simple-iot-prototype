idf_component_register(SRCS "iot_device.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES connectivity_provider firebase_rtdb flash_fs config_store
                                  reading_store reading_uploader device_listener sensor_provider
                                  actuator_controller utils)
