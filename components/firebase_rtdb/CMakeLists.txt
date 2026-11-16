idf_component_register(SRCS "src/firebase_rtdb.c"
                    INCLUDE_DIRS "include"
                    PRIV_INCLUDE_DIRS "private_include"
                    REQUIRES sse_parser
                    PRIV_REQUIRES http_client firebase_auth utils)
