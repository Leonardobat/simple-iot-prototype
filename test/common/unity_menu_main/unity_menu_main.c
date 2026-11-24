#include "unity_test_runner.h"

/* Entry point of the board-only integration tests: the Unity menu runs the case that
 * pytest-embedded picks (docs/adr/0006-pytest-embedded-integracao.md). The unit tests use
 * unity_main instead. */
void app_main(void)
{
    unity_run_menu();
}
