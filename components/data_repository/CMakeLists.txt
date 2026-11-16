idf_component_register(SRCS "src/data_repository.c"
                    INCLUDE_DIRS "include"
                    PRIV_INCLUDE_DIRS "private_include"
                    PRIV_REQUIRES firebase_rtdb firebase_paths json_writer)
