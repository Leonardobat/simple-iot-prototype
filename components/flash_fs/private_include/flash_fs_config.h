#ifndef FLASH_FS_CONFIG_H
#define FLASH_FS_CONFIG_H

/** Label of the LittleFS partition in partitions.csv. */
#define FLASH_FS_PARTITION_LABEL "storage"

/** Mount point; file names are appended after FLASH_FS_PATH_SEPARATOR. */
#define FLASH_FS_BASE_PATH      "/storage"
#define FLASH_FS_PATH_SEPARATOR "/"

/** Suffix of the temporary file used by flash_fs_replace(). */
#define FLASH_FS_TEMP_SUFFIX ".tmp"

/** "/storage/" + name + FLASH_FS_TEMP_SUFFIX + NUL. */
#define FLASH_FS_PATH_MAX_LEN 64U

#endif /* FLASH_FS_CONFIG_H */
