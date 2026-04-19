# Issue 025 — CLI output consistency: silent success and error hints

**Type**: enhancement  
**Priority**: medium  
**Component**: `src/main.c`  
**Covers**: US-A03, US-R02, US-R05

## Summary

Several commands succeed silently (no stdout) while others print confirmation.
Error messages are inconsistent about providing actionable hints.

## Changes

### Silent success fixes (`src/main.c`)

1. `ac set <ip> key=val` — add `printf("Set.\n")` on success (line ~410)
2. `vacuum start` — add `printf("Cleaning started.\n")`
3. `vacuum stop` — add `printf("Cleaning stopped.\n")`
4. `vacuum pause` — add `printf("Cleaning paused.\n")`
5. `vacuum spot` — add `printf("Spot cleaning started.\n")`
6. `vacuum find` — add `printf("Locating robot...\n")`
7. `vacuum reset-brush/side-brush/filter/sensor` — add `printf("<item> reset.\n")`
8. `vacuum fan <ip> <level>` — add `printf("Fan speed set to %s.\n", level_str)`

### Error hint consistency

All "Device not bound" messages must include the hint:
```
Device <ip> not bound. Run: ac bind <ip>
```
Fix `ac_set_single()` (line ~368) and `cmd_ac_temp_one()` (line ~239).

### "Device not found at <ip>" during bind

When `ac bind <ip>` fails to find device during scan, add:
```
Device not found at <ip>. Is the AC on and on the same network?
```

## Acceptance criteria

- Every mutating command prints a confirmation line on success.
- All "not bound" errors include the `ac bind <ip>` hint.
- No change to exit codes (0 on success, 1 on error).
