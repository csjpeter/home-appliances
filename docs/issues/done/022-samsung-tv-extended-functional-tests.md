# Issue 022 — Samsung TV: extended functional tests

**Type**: test  
**Priority**: medium  
**Component**: `tests/functional/test_samsung_tv_functional.c`  
**Covers**: US-T02, US-T04, US-T05, US-T06

## Summary

The Samsung TV test suite is missing tests for the repeat-count feature
(tv volup N), the offline-TV error message, and the pairing help text.

## Missing tests

### Repeat count (US-T02, Issue 017)

- `test_volup_repeat_3` — samsung_tv_send_keys with KEY_VOLUP × 3 (200ms delay 0 for test),
  verify mock receives 3 KEY_VOLUP packets in order.
- `test_voldown_repeat_1` — single voldown, same as existing test_send_key_correct_key.

### Offline / connection refused (US-T04)

- `test_probe_connection_refused` — no server listening → samsung_tv_probe returns 0.
  (Already covered by test_probe_offline — confirm it tests connection refused, not timeout.)

### Key not in known list warning (US-T03)

- `test_unknown_key_warning` — send "KEY_UNKNOWN_XYZ", verify:
  - Function still returns 0 (warning is non-fatal)
  - stderr contains "Warning: 'KEY_UNKNOWN_XYZ' not in known key list"

### Pairing timeout (US-T06)

- `test_pairing_timeout` — mock sends WAITING 15 times → verify samsung_tv_send_key
  returns -1 and stderr shows "Waiting for TV..." exactly once.

## Implementation notes

The mock_tv_server needs a new GRANT_TYPE_WAITING_TIMEOUT that sends WAITING
for all 15 retry attempts and then closes the connection.

## Acceptance criteria

- All new tests pass under ASAN.
- repeat-count test verifies key order in mock received_keys array.
- pairing timeout test verifies stderr output count (printed only once).
