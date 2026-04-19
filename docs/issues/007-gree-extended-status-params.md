# Issue 007 — Gree: extended status parameters

**Type**: enhancement  
**Priority**: high  
**Component**: `libappliances/src/infrastructure/gree_client`

## Summary

The current `GreeStatus` struct and `GREE_STATUS_COLS` query are missing several
documented parameters. Extend both so all readable AC state is exposed.

## Currently queried (GREE_STATUS_COLS in gree_client.c)

```
Pow, SetTem, TemSen, Mod, Lig, SwUpDn, Wnd, Quiet, Tur, SvSt, TemUn
```

Note: `Wnd` is a legacy alias. The correct fan-speed parameter is `WdSpd`.
`SvSt` purpose is unclear; `SwhSlp` is the documented sleep-mode switch.

## Parameters to add

| Parameter    | Description                                      | Values                         |
|--------------|--------------------------------------------------|--------------------------------|
| `WdSpd`      | Fan speed (replaces/supplements `Wnd`)           | 0=auto, 1–5 (low→high)        |
| `SwingLfRig` | Horizontal swing                                 | 0=fixed, 1=full, 2–6=position |
| `Air`        | Fresh air circulation                            | 0=off, 1=on                   |
| `Blo`        | X-Fan / blow-dry after shutdown                  | 0=off, 1=on                   |
| `Health`     | Cold-plasma anion generator                      | 0=off, 1=on                   |
| `SwhSlp`     | Sleep mode switch                                | 0=off, 1=on                   |
| `StHt`       | Anti-freeze / steady heat (8 °C mode)           | 0=off, 1=on                   |
| `HeatCoolType`| Device capability: cool-only or heat+cool       | 0=cool-only, 1=heat+cool      |

## Changes required

### `gree_client.h`

Add fields to `GreeStatus`:

```c
typedef struct {
    int         power;        /* Pow */
    int         set_temp;     /* SetTem: 16–30 */
    int         room_temp;    /* TemSen: decoded (offset corrected) */
    GreeMode    mode;         /* Mod */
    GreeFanSpeed fan;         /* WdSpd */
    int         swing_v;      /* SwUpDn: vertical swing */
    int         swing_h;      /* SwingLfRig: horizontal swing 0–6 */
    int         lights;       /* Lig */
    int         quiet;        /* Quiet */
    int         turbo;        /* Tur */
    int         sleep;        /* SwhSlp */
    int         xfan;         /* Blo: X-Fan */
    int         air;          /* Air: fresh air */
    int         health;       /* Health: anion */
    int         steady_heat;  /* StHt: anti-freeze */
    GreeTempUnit temp_unit;   /* TemUn */
    int         heat_cool_type; /* HeatCoolType: read-only capability */
} GreeStatus;
```

### `gree_client.c`

Update `GREE_STATUS_COLS` and `GREE_STATUS_COL_COUNT`, and map the new `dat[]`
positions in `gree_client_get_status`.

## Acceptance criteria

- `ac status <ip>` shows all new fields.
- `heat_cool_type` displayed as "cool-only" or "heat+cool".
- `room_temp` always shown in °C regardless of `temp_unit`.
- No regression in existing `ac set` tests.

## References

- [RobHofmann/HomeAssistant-GreeClimateComponent — params](https://github.com/RobHofmann/HomeAssistant-GreeClimateComponent)
- [tomikaa87/gree-remote](https://github.com/tomikaa87/gree-remote)
- `docs/gree-protocol.md`
