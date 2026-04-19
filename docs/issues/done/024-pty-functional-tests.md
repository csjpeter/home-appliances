# Issue 024 — PTY-based functional tests for CLI output

**Type**: test  
**Priority**: high  
**Component**: `tests/libpty/`, `tests/functional/test_cli_pty_*.c`, `tests/CMakeLists.txt`  
**Covers**: US-A01, US-A02, US-R01, US-T01, US-P01 (output format verification)

## Summary

Add PTY (pseudo-terminal) based tests that run the actual `home-appliances` binary
and verify its stdout output, stderr messages, exit codes, and UTF-8 rendering.
Port the libpty framework from `../email-cli/libs/libptytest/` (copy files, no dependency).

## Library addition: `tests/libpty/`

Copy and adapt from `../email-cli/libs/libptytest/`:
- `tests/libpty/ptytest.h`
- `tests/libpty/pty_session.c`
- `tests/libpty/pty_screen.c`
- `tests/libpty/pty_sync.c`

Keep the public API identical. Remove any email-cli-specific references.
Add `#ifndef LIBPTY_H` guards if missing.

## New test executables

### `tests/functional/test_cli_pty_ac.c`

Tests run the real `home-appliances` binary via PTY:

- `test_ac_help` — run `home-appliances help`, verify "Usage:" appears in output
- `test_ac_on_feedback` — with mock Gree server, run `ac on <ip>`, verify:
  - "AC on." appears in output
  - "Room temp: " followed by digits and "°C" appears
- `test_ac_off_feedback` — verify "AC off." and "Room temp:"
- `test_ac_status_format` — verify all 17 fields present in output
- `test_ac_temp_raw` — verify integer-only output (no label)
- `test_xfan_mode_warning` — mock server in heat mode, verify warning on stderr

### `tests/functional/test_cli_pty_vacuum.c`

- `test_vacuum_status_format` — verify "State:", "Battery:", "Fan:" labels
- `test_vacuum_dock_feedback` — verify "Returning to dock..." then "State: returning"
- `test_vacuum_consumables_format` — verify "Main brush:", "Side brush:"

### `tests/functional/test_cli_pty_tv.c`

- `test_tv_probe_online` — verify "<ip>: online"
- `test_tv_probe_offline` — verify "<ip>: offline"
- `test_tv_pairing_prompt` — WAITING response → verify stderr "Waiting for TV to accept..."

### `tests/functional/test_cli_pty_printer.c`

- `test_printer_status_format` — verify "State:", "Toner:", "Pages:"
- `test_printer_toner_raw` — `--raw` flag → verify only "68" in stdout
- `test_printer_quiet_exit0` — idle printer → verify exit code 0
- `test_printer_quiet_exit2` — stopped printer → verify exit code 2

## CMakeLists.txt changes

```cmake
# libpty sources shared by all PTY tests
set(LIBPTY_SRCS
    ${CMAKE_SOURCE_DIR}/tests/libpty/pty_session.c
    ${CMAKE_SOURCE_DIR}/tests/libpty/pty_screen.c
    ${CMAKE_SOURCE_DIR}/tests/libpty/pty_sync.c
)

add_executable(test-cli-pty-ac
    functional/test_cli_pty_ac.c ${LIBPTY_SRCS})
target_include_directories(test-cli-pty-ac PRIVATE
    ${CMAKE_SOURCE_DIR}/tests/libpty
    ${CMAKE_SOURCE_DIR}/tests/common)
target_compile_definitions(test-cli-pty-ac PRIVATE
    HOME_APPLIANCES_BIN="$<TARGET_FILE:home-appliances>"
    GREE_MOCK_PORT=17001)
target_link_libraries(test-cli-pty-ac PRIVATE Threads::Threads util)

# Similar for vacuum, tv, printer...
add_test(NAME cli-pty-ac-tests COMMAND test-cli-pty-ac)
add_test(NAME cli-pty-vacuum-tests COMMAND test-cli-pty-vacuum)
add_test(NAME cli-pty-tv-tests COMMAND test-cli-pty-tv)
add_test(NAME cli-pty-printer-tests COMMAND test-cli-pty-printer)
```

## Acceptance criteria

- All PTY tests pass under ASAN (no memory leaks from pty infra).
- `test_ac_on_feedback` verifies "°C" UTF-8 character in output.
- `test_printer_quiet_exit0/2` verify correct UNIX exit codes.
- PTY tests run in < 10 seconds total.
