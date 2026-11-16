# Fake http_client (the network HAL): same public header as the real component, no network.
idf_component_register(SRCS "fake_http_client.c"
                    INCLUDE_DIRS "include"
                                 "${CMAKE_CURRENT_LIST_DIR}/../../../components/http_client/include"
                    REQUIRES sse_parser)
