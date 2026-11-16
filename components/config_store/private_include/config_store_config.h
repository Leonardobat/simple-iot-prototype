#ifndef CONFIG_STORE_CONFIG_H
#define CONFIG_STORE_CONFIG_H

/** File in the flash file system. */
#define CONFIG_STORE_FILE_NAME "config.bin"

/** Changes whenever the layout of the file changes: a file of another version is ignored. */
#define CONFIG_STORE_FORMAT_VERSION 1U

#endif /* CONFIG_STORE_CONFIG_H */
