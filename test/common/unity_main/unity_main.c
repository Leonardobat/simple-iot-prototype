#include "unity.h"

/* Shared entry point of every test project: runs all registered TEST_CASEs. */
void app_main(void)
{
    UNITY_BEGIN();
    unity_run_all_tests();
    UNITY_END();
}
