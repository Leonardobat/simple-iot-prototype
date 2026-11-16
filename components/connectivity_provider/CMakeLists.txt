idf_component_register(SRCS "src/connectivity_provider.c"
                            "src/wifi_diagnostics.c"
                    INCLUDE_DIRS "include"
                    PRIV_INCLUDE_DIRS "private_include"
                    PRIV_REQUIRES esp_wifi esp_netif esp_event esp_timer nvs_flash backoff)
