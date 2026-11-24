"""Unit tests in QEMU (esp32s3), one pytest case per test project in test/<componente>/.

The projects are built by test/run_unit_tests.sh; pytest-embedded boots each one in QEMU and reads the
Unity output. A project that runs no test fails (the DUT never prints a summary of tests).
"""
from pathlib import Path

import pytest

TEST_DIR = Path(__file__).resolve().parent
# Not unit test projects: the board-only tests (see test/integration/test_integration.py).
BOARD_ONLY = {'integration'}
QEMU_TIMEOUT_S = 120

PROJECTS = sorted(
    path.name
    for path in TEST_DIR.iterdir()
    if (path / 'CMakeLists.txt').is_file() and path.name not in BOARD_ONLY
)


@pytest.mark.parametrize('app_path', [str(TEST_DIR / name) for name in PROJECTS], ids=PROJECTS, indirect=True)
def test_unit(dut):
    dut.expect_unity_test_output(timeout=QEMU_TIMEOUT_S)
    assert dut.testsuite.testcases, 'The project ran no test'
