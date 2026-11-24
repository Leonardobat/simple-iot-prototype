"""Integration tests on the real ESP32-S3 (docs/adr/0006-pytest-embedded-integracao.md).

pytest-embedded flashes the board, picks each Unity case from the menu after a reset, and fails the
run if any case fails. Needs Wi-Fi and the Firebase credentials of the product sdkconfig.
"""

CASE_TIMEOUT_S = 180  # Wi-Fi, SNTP and sign-in come before most cases.


def test_integration(dut):
    dut.run_all_single_board_cases(group='integration', reset=True, timeout=CASE_TIMEOUT_S)

    # pytest-embedded records a case it could not start (the board stopped answering after a reset, the
    # USB port dropped...) as IGNORE, which does not fail the run: demand a PASS for every case.
    expected = [case.name for case in dut.test_menu if 'integration' in case.groups and not case.is_ignored]
    passed = {case.name for case in dut.testsuite.testcases if case.result == 'PASS'}
    not_passed = [name for name in expected if name not in passed]
    assert not not_passed, f'{len(not_passed)} of {len(expected)} cases did not pass: {not_passed}'
