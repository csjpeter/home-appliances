# Issue 016 — Brother: extended CLI commands + quiet mode

**Type**: feature  
**Priority**: high  
**Component**: `src/main.c`, `libappliances/src/infrastructure/brother_client`  
**Covers**: US-P02, US-P03, US-P04, US-P05, US-P06

## Summary

Add standalone `printer toner`, `printer pages`, `printer consumables` CLI commands,
`--quiet` mode with proper exit codes, and extend `brother_get_status` to query
Brother proprietary OIDs for drum/maintenance data.

## Library additions (`brother_client.h/.c`)

Add `BrotherConsumables` struct and `brother_get_consumables()`:

```c
typedef struct {
    int toner_pct;          /* 0-100, -1=unavailable */
    int drum_pct;           /* 0-100, -1=unavailable */
    int pages_until_maint;  /* pages remaining, -1=unavailable */
} BrotherConsumables;

int brother_get_consumables(const char *ip, BrotherConsumables *out);
```

Query OIDs (one SNMP packet with 3 OIDs):
- `1.3.6.1.2.1.43.11.1.1.9.1.1` + `.8.1.1` (toner cur/max for %)
- `1.3.6.1.4.1.2435.2.3.9.4.2.1.5.5.8.0` (brInfoMaintenance OctetString)
- `1.3.6.1.4.1.2435.2.3.9.4.2.1.5.5.11.0` (brInfoNextCare OctetString)

For brInfoMaintenance: extract raw OctetString bytes; return first 2-byte LE uint16
as `raw / 100` percent (units of 0.01%). If OctetString < 2 bytes, return -1.
For brInfoNextCare: same approach — first uint16 as pages remaining.

## CLI additions (`main.c`)

### `printer toner <ip> [--raw]`
- Calls `brother_get_status`, prints toner %
- `--raw`: print only the integer (for shell scripts)
- Exit code 1 if unreachable or unavailable

### `printer pages <ip>`
- Calls `brother_get_status`, prints page count
- Exit code 1 if unavailable

### `printer consumables <ip>`
- Calls `brother_get_consumables`, prints table:
  ```
  Toner:       68%
  Drum:        54%  (or "n/a" if unavailable)
  Maintenance: 8200 pages remaining  (or "n/a")
  ```
- Warning `[!]` on items below 20%

### `printer status <ip> [--quiet]`
- `--quiet`: no output, exit codes only:
  - 0 = state 3 (idle) and no toner_low
  - 1 = unreachable / unknown state
  - 2 = state 5 (stopped) or toner_low ≥ 1

## Acceptance criteria
- All three new commands work end-to-end against real SNMP.
- `--raw` outputs plain integer only.
- Exit code 2 on error state.
- Drum/maintenance shows "n/a" gracefully if OIDs not supported by model.

## References
- [BRN-Printer-sCounters-Info](https://github.com/saper-2/BRN-Printer-sCounters-Info)
- Issues 005, 012
