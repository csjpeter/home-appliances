# Issue 008 — Gree: TemSen room temperature offset correction

**Type**: bug  
**Priority**: medium  
**Component**: `libappliances/src/infrastructure/gree_client`

## Summary

Some Gree firmware versions return `TemSen` as `(actual_celsius + 40)`.
The current implementation stores the raw value, which causes incorrect room
temperature display (e.g., 65 shown instead of 25 °C).

## Affected code

`gree_client.c`, `gree_client_get_status()`:

```c
out->room_temp = dat[2];   /* raw TemSen — may need −40 correction */
```

## Fix

Apply heuristic offset correction:

```c
int raw_tem = dat[2];
out->room_temp = (raw_tem > 60) ? (raw_tem - 40) : raw_tem;
```

The threshold 60 safely separates the two cases:
- Offset firmware: returns 56–90 for 16–50 °C → after correction 16–50
- Direct firmware: returns 16–50 directly

## Edge case

If the device is not equipped with a temperature sensor, `TemSen` may return 0
or a sentinel value. Add a `room_temp_valid` flag or return -1 for sentinel values.

## Acceptance criteria

- `ac status` always prints room temperature in °C in the range 0–50.
- On devices without sensor, room temperature is shown as "n/a".
- Unit tests cover both firmware cases (raw 65 → 25 °C; raw 25 → 25 °C).

## References

- [greeclimate TemSen handling](https://github.com/cmroche/greeclimate)
- [inwaar/gree-hvac-client TemSen note](https://github.com/inwaar/gree-hvac-client)
- `docs/gree-protocol.md` — "TemSen room temperature decoding"
