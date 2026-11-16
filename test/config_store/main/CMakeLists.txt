idf_component_register(SRCS "test_config_store.c"
                    PRIV_REQUIRES unity unity_main config_store flash_fs
                    WHOLE_ARCHIVE)
