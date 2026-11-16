idf_component_register(SRCS "test_firebase_auth.c"
                    PRIV_REQUIRES unity unity_main firebase_auth http_client
                    WHOLE_ARCHIVE)
