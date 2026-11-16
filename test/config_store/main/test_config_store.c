#include <stdint.h>

#include "config_store.h"
#include "fake_flash_fs.h"
#include "flash_fs.h"
#include "unity.h"

#define CONFIG_FILE        "config.bin"
#define SENSORS_MASK       0x15U
#define ACTUATORS_MASK     0x2AU
#define OTHER_SENSORS_MASK 0x03U

void setUp(void)
{
    fake_flash_fs_reset();
    TEST_ASSERT_EQUAL_INT32(ESP_OK, flash_fs_mount());
}

void tearDown(void)
{
    flash_fs_unmount();
    fake_flash_fs_reset();
}

TEST_CASE("test_config_store_load_without_saved_config_reports_not_found_and_all_clear",
          "[config_store]")
{
    config_store_enabled_t loaded = {.sensors = 0xFFU, .actuators = 0xFFU};

    TEST_ASSERT_EQUAL_INT32(ESP_ERR_NOT_FOUND, config_store_load(&loaded));
    TEST_ASSERT_EQUAL_HEX32(0U, loaded.sensors);
    TEST_ASSERT_EQUAL_HEX32(0U, loaded.actuators);
}

TEST_CASE("test_config_store_load_returns_what_was_saved", "[config_store]")
{
    const config_store_enabled_t saved = {.sensors = SENSORS_MASK, .actuators = ACTUATORS_MASK};
    config_store_enabled_t loaded = {0};

    TEST_ASSERT_EQUAL_INT32(ESP_OK, config_store_save(&saved));

    TEST_ASSERT_EQUAL_INT32(ESP_OK, config_store_load(&loaded));
    TEST_ASSERT_EQUAL_HEX32(SENSORS_MASK, loaded.sensors);
    TEST_ASSERT_EQUAL_HEX32(ACTUATORS_MASK, loaded.actuators);
}

TEST_CASE("test_config_store_save_twice_keeps_the_last_flags", "[config_store]")
{
    config_store_enabled_t saved = {.sensors = SENSORS_MASK, .actuators = ACTUATORS_MASK};
    config_store_enabled_t loaded = {0};
    TEST_ASSERT_EQUAL_INT32(ESP_OK, config_store_save(&saved));
    saved.sensors = OTHER_SENSORS_MASK;

    TEST_ASSERT_EQUAL_INT32(ESP_OK, config_store_save(&saved));

    TEST_ASSERT_EQUAL_INT32(ESP_OK, config_store_load(&loaded));
    TEST_ASSERT_EQUAL_HEX32(OTHER_SENSORS_MASK, loaded.sensors);
}

TEST_CASE("test_config_store_load_corrupt_file_reports_invalid_response_and_all_clear",
          "[config_store]")
{
    const config_store_enabled_t saved = {.sensors = SENSORS_MASK, .actuators = ACTUATORS_MASK};
    config_store_enabled_t loaded = {.sensors = 0xFFU, .actuators = 0xFFU};
    TEST_ASSERT_EQUAL_INT32(ESP_OK, config_store_save(&saved));
    TEST_ASSERT_TRUE(fake_flash_fs_corrupt(CONFIG_FILE, 5U));

    TEST_ASSERT_EQUAL_INT32(ESP_ERR_INVALID_RESPONSE, config_store_load(&loaded));
    TEST_ASSERT_EQUAL_HEX32(0U, loaded.sensors);
    TEST_ASSERT_EQUAL_HEX32(0U, loaded.actuators);
}

TEST_CASE("test_config_store_load_truncated_file_reports_invalid_response", "[config_store]")
{
    const config_store_enabled_t saved = {.sensors = SENSORS_MASK, .actuators = ACTUATORS_MASK};
    config_store_enabled_t loaded = {0};
    TEST_ASSERT_EQUAL_INT32(ESP_OK, config_store_save(&saved));
    TEST_ASSERT_TRUE(fake_flash_fs_truncate(CONFIG_FILE, 6U));

    TEST_ASSERT_EQUAL_INT32(ESP_ERR_INVALID_RESPONSE, config_store_load(&loaded));
}

TEST_CASE("test_config_store_save_failure_is_reported_and_keeps_the_previous_flags",
          "[config_store]")
{
    config_store_enabled_t saved = {.sensors = SENSORS_MASK, .actuators = ACTUATORS_MASK};
    config_store_enabled_t loaded = {0};
    TEST_ASSERT_EQUAL_INT32(ESP_OK, config_store_save(&saved));
    saved.sensors = OTHER_SENSORS_MASK;
    fake_flash_fs.replace_result = ESP_FAIL;

    TEST_ASSERT_EQUAL_INT32(ESP_FAIL, config_store_save(&saved));

    fake_flash_fs.replace_result = ESP_OK;
    TEST_ASSERT_EQUAL_INT32(ESP_OK, config_store_load(&loaded));
    TEST_ASSERT_EQUAL_HEX32(SENSORS_MASK, loaded.sensors);
}

TEST_CASE("test_config_store_load_read_error_is_propagated", "[config_store]")
{
    config_store_enabled_t loaded = {0};
    fake_flash_fs.read_result = ESP_FAIL;

    TEST_ASSERT_EQUAL_INT32(ESP_FAIL, config_store_load(&loaded));
}
