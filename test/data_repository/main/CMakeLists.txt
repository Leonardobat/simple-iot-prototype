idf_component_register(SRCS "test_data_repository.c"
                    PRIV_REQUIRES unity unity_main data_repository http_client firebase_rtdb
                    WHOLE_ARCHIVE)
