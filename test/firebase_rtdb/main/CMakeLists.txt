idf_component_register(SRCS "test_firebase_rtdb.c"
                    PRIV_REQUIRES unity unity_main firebase_rtdb http_client firebase_auth
                    WHOLE_ARCHIVE)
