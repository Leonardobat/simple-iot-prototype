#include <stdint.h>

#include "gpio_output.h"
#include "unity.h"

/* Real gpio_output over the ESP-IDF GPIO driver, in QEMU (register writes only, no pin to read). */

#define TEST_GPIO     6
#define NOT_A_GPIO    49
#define NEGATIVE_GPIO (-1)

void setUp(void)
{
}

void tearDown(void)
{
}

TEST_CASE("test_gpio_output_init_valid_gpio_returns_ok", "[gpio_output]")
{
    TEST_ASSERT_EQUAL_INT32(ESP_OK, gpio_output_init(TEST_GPIO, true, false));
    gpio_output_deinit(TEST_GPIO);
}

TEST_CASE("test_gpio_output_init_without_pull_up_returns_ok", "[gpio_output]")
{
    TEST_ASSERT_EQUAL_INT32(ESP_OK, gpio_output_init(TEST_GPIO, false, false));
    gpio_output_deinit(TEST_GPIO);
}

TEST_CASE("test_gpio_output_init_starting_high_returns_ok", "[gpio_output]")
{
    TEST_ASSERT_EQUAL_INT32(ESP_OK, gpio_output_init(TEST_GPIO, true, true));
    gpio_output_deinit(TEST_GPIO);
}

TEST_CASE("test_gpio_output_init_negative_gpio_returns_invalid_arg", "[gpio_output]")
{
    TEST_ASSERT_EQUAL_INT32(ESP_ERR_INVALID_ARG, gpio_output_init(NEGATIVE_GPIO, true, false));
}

TEST_CASE("test_gpio_output_init_out_of_range_gpio_returns_invalid_arg", "[gpio_output]")
{
    TEST_ASSERT_EQUAL_INT32(ESP_ERR_INVALID_ARG, gpio_output_init(NOT_A_GPIO, true, false));
}

TEST_CASE("test_gpio_output_set_level_high_and_low_return_ok", "[gpio_output]")
{
    TEST_ASSERT_EQUAL_INT32(ESP_OK, gpio_output_init(TEST_GPIO, true, false));

    TEST_ASSERT_EQUAL_INT32(ESP_OK, gpio_output_set_level(TEST_GPIO, true));
    TEST_ASSERT_EQUAL_INT32(ESP_OK, gpio_output_set_level(TEST_GPIO, false));
    gpio_output_deinit(TEST_GPIO);
}

TEST_CASE("test_gpio_output_set_level_invalid_gpio_returns_invalid_arg", "[gpio_output]")
{
    TEST_ASSERT_EQUAL_INT32(ESP_ERR_INVALID_ARG, gpio_output_set_level(NOT_A_GPIO, true));
}

TEST_CASE("test_gpio_output_deinit_then_init_works", "[gpio_output]")
{
    TEST_ASSERT_EQUAL_INT32(ESP_OK, gpio_output_init(TEST_GPIO, true, false));
    gpio_output_deinit(TEST_GPIO);

    TEST_ASSERT_EQUAL_INT32(ESP_OK, gpio_output_init(TEST_GPIO, true, false));
    gpio_output_deinit(TEST_GPIO);
}
