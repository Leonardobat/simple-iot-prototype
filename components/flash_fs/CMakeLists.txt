idf_component_register(SRCS "src/flash_fs.c"
                    INCLUDE_DIRS "include"
                    PRIV_INCLUDE_DIRS "private_include"
                    PRIV_REQUIRES joltwallet__littlefs utils log)
