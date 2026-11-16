idf_component_register(SRCS "test_reading_store.c"
                    PRIV_REQUIRES unity unity_main reading_store flash_fs
                    WHOLE_ARCHIVE)
