idf_component_register(SRCS "test_reading_uploader.c"
                    PRIV_REQUIRES unity unity_main reading_uploader reading_store flash_fs
                                  http_client firebase_rtdb data_repository
                    WHOLE_ARCHIVE)
