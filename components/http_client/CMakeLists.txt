idf_component_register(SRCS "src/http_client.c"
                    INCLUDE_DIRS "include"
                    PRIV_INCLUDE_DIRS "private_include"
                    REQUIRES sse_parser
                    PRIV_REQUIRES esp_http_client mbedtls)
