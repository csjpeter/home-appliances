# Issue 028 — Install, .desktop, and deployment infrastructure

**Type**: infrastructure  
**Priority**: low  
**Component**: `CMakeLists.txt`, `manage.sh`  
**Covers**: general

## Summary

The project lacks install targets, a `.desktop` entry, and a `manage.sh install`
command. Add basic deployment support.

## Changes

### `CMakeLists.txt`

```cmake
include(GNUInstallDirs)

install(TARGETS home-appliances
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES docs/man/home-appliances.1
        DESTINATION ${CMAKE_INSTALL_MANDIR}/man1
        OPTIONAL)

install(FILES home-appliances.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
        OPTIONAL)
```

### `home-appliances.desktop` (NEW)

```ini
[Desktop Entry]
Version=1.0
Type=Application
Name=Home Appliances
Comment=Control home appliances from the command line
Exec=home-appliances %u
Icon=utilities-terminal
Terminal=true
Categories=Utility;
```

### `manage.sh`

Add `install` target:
```bash
install)
    cmake --build build-Release --target install
    ;;
```

## Acceptance criteria

- `./manage.sh build && sudo cmake --install build-Release` installs binary to `/usr/local/bin`.
- `man home-appliances` works after install (if man page exists).
