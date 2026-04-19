# Issue 002 — Roborock: control commands

**Type**: feature  
**Priority**: high  
**Component**: `libappliances/src/infrastructure/roborock_client`

## Summary

Implement the control command set: start, stop, pause, dock return, spot clean,
fan speed change, and consumable reset.

## Commands to implement

| CLI subcommand              | miio method          | Params                        |
|-----------------------------|----------------------|-------------------------------|
| `vacuum start`              | `app_start`          | `[]`                          |
| `vacuum stop`               | `app_stop`           | `[]`                          |
| `vacuum pause`              | `app_pause`          | `[]`                          |
| `vacuum dock`               | `app_charge`         | `[]`                          |
| `vacuum spot`               | `app_spot`           | `[]`                          |
| `vacuum find`               | `find_device`        | `[]`                          |
| `vacuum fan <level>`        | `set_custom_mode`    | `[101–105]`                   |
| `vacuum reset-brush`        | `reset_consumable`   | `["main_brush_work_time"]`    |
| `vacuum reset-side-brush`   | `reset_consumable`   | `["side_brush_work_time"]`    |
| `vacuum reset-filter`       | `reset_consumable`   | `["filter_work_time"]`        |
| `vacuum reset-sensor`       | `reset_consumable`   | `["sensor_dirty_time"]`       |

## Fan speed levels

| Value | Label     |
|-------|-----------|
| 101   | Silent    |
| 102   | Balanced  |
| 103   | Turbo     |
| 104   | Max       |
| 105   | Gentle    |

## Acceptance criteria

- Each command sends the correct miio JSON, receives a response, checks result = `"ok"`.
- `vacuum fan silent|balanced|turbo|max|gentle` accepted as friendly names.
- `vacuum reset-*` commands print confirmation and new consumable lifetime.
- All commands exit 0/1 as per project convention.

## Out of scope (for now)

- Zone cleaning (`app_zoned_clean`) — requires coordinate maps
- Room/segment cleaning (`app_segment_clean`) — requires map segment IDs
- Remote control mode (`app_rc_*`)

## References

- [XiaomiRobotVacuumProtocol commands](https://github.com/marcelrv/XiaomiRobotVacuumProtocol)
- [python-miio source](https://github.com/rytilahti/python-miio)
- `docs/roborock-protocol.md`
