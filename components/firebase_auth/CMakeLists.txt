idf_component_register(SRCS "src/firebase_auth.c"
                    INCLUDE_DIRS "include"
                    PRIV_INCLUDE_DIRS "private_include"
                    PRIV_REQUIRES http_client esp_timer utils)
