# Issue 021 — Roborock: extended functional tests

**Type**: test  
**Priority**: high  
**Component**: `tests/functional/test_roborock_functional.c`  
**Covers**: US-R01, US-R02, US-R03, US-R04, US-R05

## Summary

Roborock functional tests are missing negative paths, edge cases, and
integration-level tests for the `vacuum dock` 3-second-wait flow.

## Missing tests

### Status edge cases (US-R01)

- `test_status_low_battery` — battery=15 (< 20) → mock response, verify warning
  output on stderr: "Warning: battery low (15%)"
- `test_status_error_state` — error_code=5 → verify "Error: code 5" printed

### Control command output (US-R02)

- `test_start_prints_ok` — roborock_start() succeeds → verify stdout "Cleaning started."
- `test_stop_prints_ok` — verify stdout "Cleaning stopped."
- `test_pause_prints_ok` — verify stdout "Cleaning paused."
- `test_find_prints_ok` — verify stdout "Finding robot..."

### Dock with wait (US-R03)

- `test_dock_wait_and_status` — mock answers roborock_dock with "ok", then
  after 3s answers get_status with state=6 (returning). Verify:
  1. "Returning to dock..." printed
  2. "State: returning" printed

### Consumables warning (US-R04)

- `test_consumables_low_warning` — main_brush used 970000s (> 90% threshold)
  → verify "[!]" appears in output

### Fan speed validation (US-R07)

- `test_set_fan_invalid` — level=100 → verify roborock_set_fan returns -1
- `test_set_fan_valid_all` — test all 5 levels (101-105)

### Reset confirmation (US-R05)

- `test_reset_brush_prints_ok` — after reset, verify stdout "main brush reset."

### Hello timeout (US-R08)

- `test_hello_timeout` — no mock server → verify roborock_hello returns -1

## Acceptance criteria

- All new tests pass under ASAN.
- Low battery warning test verifies exact stderr message.
- Dock wait test uses a 2-response mock (dock response + status response).
