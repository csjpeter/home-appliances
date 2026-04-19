# Issue 023 — Brother: extended functional tests

**Type**: test  
**Priority**: medium  
**Component**: `tests/functional/test_brother_functional.c`  
**Covers**: US-P01, US-P02, US-P05, US-P06, US-P07

## Summary

Brother functional tests are missing coverage for the --quiet exit codes,
--raw toner output, probe model string, and error state detection.

## Missing tests

### Toner warning (US-P01)

- `test_status_toner_low_warning` — toner_low=2 (absent) → verify "[LOW]" in stdout
- `test_status_stopped_state` — state=5 → verify "stopped" in stdout

### Probe model extraction (US-P07)

- `test_probe_model_string` — mock responds with "Brother HL-L2370DN Series" →
  verify brother_probe returns 1 and model string matches

### --quiet exit codes (US-P06)

These tests call brother_get_status directly and verify exit-code logic:
- `test_quiet_idle_ok` — state=3, toner_low=0 → exit code 0
- `test_quiet_toner_low` — state=3, toner_low=1 → exit code 2
- `test_quiet_stopped` — state=5, toner_low=0 → exit code 2
- `test_quiet_unreachable` — no mock → exit code 1

### --raw toner output (US-P02)

- `test_toner_raw_output` — brother_get_status returns toner_pct=68 →
  verify cmd_printer_toner(ip, raw=1) prints "68\n" only

### Exit code 2 on error (US-P05)

- `test_status_exit_code_stopped` — state=5 maps to exit 2 in --quiet mode

## Implementation notes

The quiet-mode tests can unit-test the logic directly by calling brother_get_status
with a mock server and verifying the exit-code determination logic.

For --raw and output format tests, use the PTY-based test framework (Issue 025)
once available, or redirect stdout to a pipe with dup2 for now.

## Acceptance criteria

- All new tests pass under ASAN.
- Quiet mode exit codes verified against spec (0/1/2).
- Probe model test verifies exact string copy.
