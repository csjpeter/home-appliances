# Issue 010 — Gree: complete CLI subcommand set

**Type**: feature  
**Priority**: medium  
**Component**: `src/main.c`

## Summary

The current `ac set` command accepts a fixed set of parameters. Extend the CLI
to cover all settable parameters and add convenience shortcut subcommands.

## Current state (src/main.c)

`ac set` accepts: `power`, `temp`, `mode`, `fan`, `lights`, `quiet`, `turbo`, `sleep`

## Missing shortcut subcommands

| CLI command                | Maps to                              |
|----------------------------|--------------------------------------|
| `ac swing-v <ip> on\|off`  | `SwUpDn` 1 / 0                       |
| `ac swing-h <ip> off\|full\|1..6` | `SwingLfRig` 0 / 1 / 2–6   |
| `ac xfan <ip> on\|off`     | `Blo` 1 / 0                          |
| `ac health <ip> on\|off`   | `Health` 1 / 0                       |
| `ac air <ip> on\|off`      | `Air` 1 / 0                          |
| `ac antifrost <ip> on\|off`| `StHt` 1 / 0                         |
| `ac unit <ip> c\|f`        | `TemUn` 0 / 1                        |
| `ac on <ip>`               | shortcut for `ac set <ip> power=1`   |
| `ac off <ip>`              | shortcut for `ac set <ip> power=0`   |

## Extend `ac set` parameters

Add to the `parse_ac_param()` function in `main.c`:

| Key string   | Gree param   | Values                |
|--------------|--------------|-----------------------|
| `swing_v`    | `SwUpDn`     | 0 / 1                 |
| `swing_h`    | `SwingLfRig` | 0–6                   |
| `xfan`       | `Blo`        | 0 / 1                 |
| `health`     | `Health`     | 0 / 1                 |
| `air`        | `Air`        | 0 / 1                 |
| `antifrost`  | `StHt`       | 0 / 1                 |
| `unit`       | `TemUn`      | 0 (c) / 1 (f)         |

## `ac status` display improvements

After issue 007: display all new fields in a table:

```
Power:       on
Mode:        cool
Target temp: 22 °C
Room temp:   25 °C
Fan speed:   medium
Swing V:     full
Swing H:     fixed
Quiet:       off
Turbo:       off
Sleep:       off
X-Fan:       off
Health:      on
Lights:      on
Unit:        Celsius
Type:        heat+cool
```

## Acceptance criteria

- All shortcut subcommands work end-to-end.
- `ac set <ip> swing_h=full` and `swing_h=3` both accepted.
- `ac on/off` are the fastest path to power control.
- `ac status` table covers all GreeStatus fields from issue 007.
- `print_usage()` updated with all new subcommands.

## References

- `docs/gree-protocol.md` — parameter reference
- Issues 007, 008
