# Issue 001 — Roborock: status and consumables query

**Type**: feature  
**Priority**: high  
**Component**: `libappliances/src/infrastructure/roborock_client`

## Summary

Implement `get_status` and `get_consumable` miio commands so the CLI can display
the robot vacuum's current state, battery, cleaned area, and consumable life.

## Background

The Roborock uses the miio UDP/54321 binary protocol with AES-128-CBC encryption.
A 32-byte hello handshake retrieves the device token; subsequent commands are
JSON payloads encrypted with keys derived from the token.

## Status fields to expose (`get_status`)

| Field        | Type | Description                                                      |
|--------------|------|------------------------------------------------------------------|
| `battery`    | int  | Battery % (0–100)                                                |
| `state`      | int  | 1=init, 2=sleep, 3=idle, 5=cleaning, 6=returning, 8=charging, 11=error, 16=shutdown |
| `error_code` | int  | 0 = no error; see error code table below                         |
| `clean_time` | int  | Seconds of current/last clean                                    |
| `clean_area` | int  | Area in cm²                                                      |
| `fan_power`  | int  | 101=Silent, 102=Balanced, 103=Turbo, 104=Max, 105=Gentle        |
| `in_cleaning`| int  | 1 if currently cleaning                                          |
| `map_present`| int  | 1 if a map has been created                                      |
| `dnd_enabled`| int  | Do Not Disturb active                                            |

## Consumables (`get_consumable`)

| Field                    | Description                          | Replacement threshold |
|--------------------------|--------------------------------------|-----------------------|
| `main_brush_work_time`   | Seconds of main brush use            | 300 h (1 080 000 s)  |
| `side_brush_work_time`   | Seconds of side brush use            | 200 h (720 000 s)    |
| `filter_work_time`       | Seconds of filter use                | 150 h (540 000 s)    |
| `sensor_dirty_time`      | Seconds since last sensor clean      | 30 h (108 000 s)     |

## Error codes

| Code | Cause                       |
|------|-----------------------------|
| 0    | No error                    |
| 1    | Laser/LDS sensor fault      |
| 2    | Bumper stuck                |
| 3    | Wheel stuck                 |
| 4    | Cliff sensor blocked        |
| 5    | Main brush blocked          |
| 12   | Low battery                 |
| 13   | Charging error              |
| 14   | Battery temperature fault   |
| 18   | Fan error                   |
| 22   | Recharge sensor blocked     |
| 23   | Charging dock jammed        |

## Acceptance criteria

- `vacuum status <ip>` prints all status fields in a human-readable table.
- Battery shown as percentage, state as a string label.
- `vacuum consumables <ip>` shows remaining life as percentage and hours.
- Command exits with 0 on success, 1 on communication error.

## References

- [python-miio RoborockVacuum](https://python-miio.readthedocs.io/en/latest/api/miio.integrations.roborock.vacuum.vacuum.html)
- [XiaomiRobotVacuumProtocol](https://github.com/marcelrv/XiaomiRobotVacuumProtocol)
- `docs/roborock-protocol.md`
