# Fake flash_fs (HAL of the flash file system): same public header as the real component, files in RAM.
idf_component_register(SRCS "fake_flash_fs.c"
                    INCLUDE_DIRS "include"
                                 "${CMAKE_CURRENT_LIST_DIR}/../../../components/flash_fs/include")
