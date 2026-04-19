# Issue 030 — Device store robustness: corrupt and edge-case handling

**Type**: reliability  
**Priority**: medium  
**Component**: `libappliances/src/infrastructure/gree_client.c`, `libappliances/src/infrastructure/roborock_client.c`  
**Covers**: US-A09, US-R08

## Summary

The device store load functions silently skip malformed lines without logging.
Add validation, warnings, and tests for corrupt/edge-case inputs.

## Changes

### `gree_client.c` — `gree_client_load_bound()`

Add validation for each parsed line:
- MAC length check: must be exactly 12 hex chars → `LOG_WARN_MSG` and skip
- Key length check: must be exactly 16 chars → warn and skip
- IP validity: must pass `inet_pton` check → warn and skip

### `roborock_client.c` — `roborock_load()`

Add validation:
- Token hex string: must be exactly 32 hex chars → return -1 with `LOG_WARN_MSG`
- Device ID: must be valid uint32 hex → return -1 on parse failure

Add `hex_to_bytes()` length check:
```c
if (strlen(hex) != 32) { LOG_WARN_MSG(...); return -1; }
```

### `tests/unit/test_config.c` or new `tests/unit/test_device_store.c`

Add unit tests (no network required):
- `test_gree_load_corrupt_mac` — MAC < 12 chars → line skipped gracefully
- `test_gree_load_empty_file` — empty file → count=0, returns 0
- `test_gree_load_valid` — valid line → device loaded correctly
- `test_roborock_load_short_token` — 16-char hex token → returns -1
- `test_roborock_load_valid` — 32-char hex token → device loaded

## Acceptance criteria

- Corrupt lines logged with LOG_WARN_MSG and skipped.
- Short token in roborock_load returns -1.
- All unit tests pass under ASAN + Valgrind.
