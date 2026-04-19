# Issue 019 — Man page: home-appliances(1)

**Type**: documentation  
**Priority**: medium  
**Component**: `docs/man/home-appliances.1`, `CMakeLists.txt`  
**Covers**: all user stories

## Summary

Create a UNIX man page (`man 1 home-appliances`) that documents all commands,
options, exit codes, file locations, and examples.

## Changes

### `docs/man/home-appliances.1` (NEW)

Standard troff/groff man page with sections:
- NAME
- SYNOPSIS
- DESCRIPTION
- COMMANDS (ac, vacuum, tv, printer subsections)
- OPTIONS (--quiet, --raw, --verbose)
- FILES (~/.config/home-appliances/)
- EXIT STATUS (0 success, 1 error, 2 device error)
- EXAMPLES
- SEE ALSO

### `CMakeLists.txt`

Add:
```cmake
install(FILES docs/man/home-appliances.1
        DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
```

## Acceptance criteria

- `man ./docs/man/home-appliances.1` renders without errors.
- All subcommands documented with correct synopsis.
- Exit codes section matches implementation.
- FILES section lists ~/.config/home-appliances/gree_devices and roborock_devices.
