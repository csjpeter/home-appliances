# Issue 029 — Config: documentation and error validation tests

**Type**: test + documentation  
**Priority**: low  
**Component**: `libappliances/src/core/config.c`, `tests/unit/test_config.c`  
**Covers**: general

## Summary

The config file format is undocumented and the unit tests only cover defaults.
Add error path tests and document the config file format.

## Config file format (to document in README / man page)

File: `~/.config/home-appliances/config`

```ini
network = 192.168.1.0/24
iface = wlan0
discovery_timeout_ms = 2000
```

## Changes

### `tests/unit/test_config.c`

Add test cases:
- `test_config_missing_file` — file does not exist → uses defaults, returns 0
- `test_config_invalid_network` — `network = not-an-ip` → still loads (config is lenient)
- `test_config_timeout_zero` — `discovery_timeout_ms = 0` → clamp to minimum (if applicable)
- `test_config_extra_keys` — unknown key in config → silently ignored

### `docs/config-format.md` (NEW)

Document:
- File location
- All supported keys and their default values
- Valid ranges

## Acceptance criteria

- All 4 new unit tests pass under ASAN.
- `docs/config-format.md` created with correct key/default table.
