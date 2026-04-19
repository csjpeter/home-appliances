# Issue 018 — Post-command feedback: ac on/off and vacuum dock

**Type**: enhancement  
**Priority**: medium  
**Component**: `src/main.c`  
**Covers**: US-A02, US-A07, US-R03

## Summary

After certain commands, query the device state and print the result so the user
can confirm the action took effect.

## Changes

### `ac on <ip>` / `ac off <ip>` (US-A02)
After sending `Pow=1/0`, query status and print room temperature:
```
AC on.
Room temp: 23°C
```
If status query fails, print "AC on." only (don't fail the command).

### `ac xfan <ip> on` (US-A07)
Before sending `Blo=1`, query current mode. If mode is not cool (1) or dry (2),
print a warning but still send the command:
```
Warning: X-Fan is only effective in cool/dry mode (current: heat)
```

### `vacuum dock <ip>` (US-R03)
After sending `app_charge`, sleep 3 seconds, query status, print result:
```
Returning to dock...
State: returning   (or "charging" if already docked)
```
If status query fails, print "Command sent." only.

## Acceptance criteria

- `ac on/off` prints room temp line after the confirmation.
- `ac xfan on` prints warning if current mode ≠ cool/dry (but still sets it).
- `vacuum dock` prints state after 3-second wait.
- None of these changes affect exit codes (0 if command sent, regardless of post-query).

## Notes

These are UX improvements; command success/failure is determined by the primary
set/control call, not by the subsequent status query.
