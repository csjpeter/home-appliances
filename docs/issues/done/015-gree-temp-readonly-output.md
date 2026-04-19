# Issue 015 — Gree: `ac temp` — machine-readable room temperature

**Type**: feature  
**Priority**: low  
**Component**: `src/main.c`  
**User story**: US-A11

## Summary

Add `ac temp <ip>` subcommand that prints only the integer room temperature
in °C — no labels, no table — for use in shell scripts.

## Implementation

1. Call `gree_client_get_status`.
2. Print `out.room_temp` as a plain integer followed by newline.
3. If room temperature is unavailable (sensor not present, value 0 after offset
   correction), exit 1 with message "room temperature unavailable" on stderr.

## CLI

```bash
$ ac temp 192.168.x.1
25

$ [ $(ac temp 192.168.x.1) -lt 20 ] && ac off 192.168.x.1
```

## Public `--unit` option

```
ac temp <ip> --unit c     # default, always Celsius
ac temp <ip> --unit f     # Fahrenheit
```

## Acceptance criteria

- Output is a plain integer with no trailing spaces or labels.
- Exit 0 on success, 1 if sensor unavailable or device unreachable.
- `--unit f` converts: `F = C * 9/5 + 32`.
- Works correctly with TemSen offset fix from issue 008.

## References

- Issue 007 — extended GreeStatus (room_temp field)
- Issue 008 — TemSen offset correction
