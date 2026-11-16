idf_component_register(SRCS "src/config_store.c"
                    INCLUDE_DIRS "include"
                    PRIV_INCLUDE_DIRS "private_include"
                    PRIV_REQUIRES flash_fs utils log)
