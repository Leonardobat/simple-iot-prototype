#include "chip_temperature.h"
#include "unity.h"

/* Real chip_temperature over the ESP-IDF driver, in QEMU: lifecycle only (QEMU has no real die). */

void setUp(void)
{
}

void tearDown(void)
{
}

TEST_CASE("test_chip_temperature_init_then_deinit_succeeds", "[chip_temperature]")
{
    TEST_ASSERT_EQUAL_INT32(ESP_OK, chip_temperature_init());
    chip_temperature_deinit();
}

TEST_CASE("test_chip_temperature_deinit_then_init_works", "[chip_temperature]")
{
    TEST_ASSERT_EQUAL_INT32(ESP_OK, chip_temperature_init());
    chip_temperature_deinit();

    TEST_ASSERT_EQUAL_INT32(ESP_OK, chip_temperature_init());
    chip_temperature_deinit();
}

TEST_CASE("test_chip_temperature_deinit_when_not_initialized_is_harmless", "[chip_temperature]")
{
    chip_temperature_deinit();

    TEST_ASSERT_EQUAL_INT32(ESP_OK, chip_temperature_init());
    chip_temperature_deinit();
}
