# Issue 026 — Security: restrict device store file permissions

**Type**: security  
**Priority**: medium  
**Component**: `libappliances/src/infrastructure/gree_client.c`, `libappliances/src/infrastructure/roborock_client.c`  
**Covers**: security

## Summary

Device store files contain sensitive data (cryptographic keys, auth tokens) and
are currently created with default umask permissions (0644). They should be
restricted to owner-read-write only (0600).

## Changes

### `gree_client.c` — `gree_client_save_bound()`

After `fopen(path, "w")`, set permissions:
```c
chmod(path, 0600);
```
Also change directory creation: `mkdir(dir, 0700)` (already done) but verify.

### `roborock_client.c` — `roborock_save()`

After successful `fclose()`:
```c
chmod(path, 0600);
```

### New header include

Both files need `#include <sys/stat.h>` if not already present.

## Acceptance criteria

- After `ac bind`, the `~/.config/home-appliances/gree_devices` file has mode 0600.
- After first vacuum command, `~/.config/home-appliances/roborock_devices` has mode 0600.
- Existing files are NOT retroactively changed (out of scope).
