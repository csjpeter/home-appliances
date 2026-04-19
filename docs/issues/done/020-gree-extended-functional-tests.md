# Issue 020 — Gree: extended functional tests for all set parameters

**Type**: test  
**Priority**: high  
**Component**: `tests/functional/test_gree_functional.c`  
**Covers**: US-A03, US-A04, US-A05, US-A06, US-A07, US-A08, US-A10, US-A11

## Summary

The existing Gree functional test only covers scan, bind, get_status, TemSen offset,
and power (Pow=1). All other set parameters and error paths are untested.

## Missing tests

### Parameter set tests (mock server verifies correct key+value in `opt` JSON)

- `test_set_temp` — SetTem=22, verify JSON payload
- `test_set_mode` — Mod=1 (cool), Mod=4 (heat)
- `test_set_fan` — WdSpd=0 (auto), WdSpd=5 (high)
- `test_set_sleep` — SwhSlp=1, SwhSlp=0
- `test_set_swing_v` — SwUpDn=1, SwUpDn=0
- `test_set_swing_h` — SwingLfRig=0, SwingLfRig=1
- `test_set_xfan` — Blo=1
- `test_set_health` — Health=1
- `test_set_antifrost` — StHt=1
- `test_set_multi_param` — 3 parameters in one call

### Error path tests

- `test_set_temp_out_of_range` — SetTem=15 (< 16) → expect error from parse_ac_param
- `test_scan_timeout` — no mock server running → gree_client_scan returns after timeout
- `test_bind_device_not_found` — scan finds device but bind fails (mock returns error JSON)
- `test_status_device_error` — mock returns `"r": 500` in status response

### Post-feedback test (Issue 018)

- `test_power_on_room_temp` — after Pow=1, mock serves status, verify room temp printed to stdout

### Multi-IP test (US-A10)

- `test_multi_ip_set` — 2 mock servers on different ports, ac off <ip1> <ip2>

## Implementation notes

The mock server must inspect the incoming `opt` JSON to verify the correct
key/value pair was sent. Use `strstr(decrypted_payload, "\"SetTem\":22")`.

## Acceptance criteria

- All 15 new tests pass under ASAN.
- Mock server verifies exact JSON payload for each parameter.
- Timeout tests complete within 2× the timeout value.
