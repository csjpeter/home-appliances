# Issue 005 — Brother printer: SNMP status query

**Type**: feature  
**Priority**: high  
**Component**: `libappliances/src/infrastructure/brother_client`

## Summary

Implement raw SNMPv1 GetRequest over UDP/161 to query printer state, toner level,
page count, and drum life — without any external SNMP library.

## OIDs to query

### Standard MIB-II / Printer MIB

| OID                            | Name                  | Description                                        |
|--------------------------------|-----------------------|----------------------------------------------------|
| `1.3.6.1.2.1.25.3.2.1.5.1`    | hrPrinterStatus       | 3=idle, 4=printing, 5=warmup, 6=stopped            |
| `1.3.6.1.2.1.43.10.2.1.4.1.1` | prtMarkerLifeCount    | Total pages printed                                |
| `1.3.6.1.2.1.43.11.1.1.9.1.1` | prtMarkerSupplies (current) | Black toner remaining (integer, vendor-scaled) |
| `1.3.6.1.2.1.43.11.1.1.8.1.1` | prtMarkerSupplies (max)     | Black toner max (for % calculation)          |

### Brother proprietary (vendor OID 1.3.6.1.4.1.2435)

| OID                                       | Description                                              |
|-------------------------------------------|----------------------------------------------------------|
| `1.3.6.1.4.1.2435.2.3.9.4.2.1.5.5.8.0`  | brInfoMaintenance — consumable remaining % (OctetString) |
| `1.3.6.1.4.1.2435.2.3.9.4.2.1.5.5.10.0` | brInfoCounter — page counter data (OctetString)          |
| `1.3.6.1.4.1.2435.2.3.9.4.2.1.5.5.11.0` | brInfoNextCare — pages until next maintenance            |
| `1.3.6.1.4.1.2435.2.3.9.1.1.2.10.1`     | brToner1Low — 0=ok, 1=low, 2=absent, 3=empty            |

## SNMP packet format (raw BER, no library)

SNMPv1 GetRequest over UDP. Minimal structure:

```
30 <len>               -- SEQUENCE (outer)
  02 01 00             -- INTEGER: version = 0 (v1)
  04 06 "public"       -- OCTET STRING: community
  A0 <len>             -- GetRequest-PDU
    02 04 <request-id> -- INTEGER: request ID (4 bytes)
    02 01 00           -- INTEGER: error-status = 0
    02 01 00           -- INTEGER: error-index = 0
    30 <len>           -- SEQUENCE: variable-bindings
      30 <len>         -- SEQUENCE: VarBind
        06 <len> <OID> -- OBJECT IDENTIFIER
        05 00          -- NULL
      [repeat per OID]
```

OID encoding: subtract 40*first_arc from second arc for first byte; then
each arc encoded as base-128 with high bit set on all but last byte.

Total packet for 4–5 OIDs: ~80–120 bytes. Response: ~100–200 bytes.

## OctetString decoding for brInfoMaintenance

The value is a hex byte string where specific byte offsets encode remaining life
as a 16-bit little-endian integer in units of 0.01%. Example: `0x25 0xF3` =
62245 → 622.45% (some models cap at 10000 = 100%).

Model-specific byte offsets documented in:
[BRN-Printer-sCounters-Info](https://github.com/saper-2/BRN-Printer-sCounters-Info)

## Acceptance criteria

- `printer status <ip>` prints: printer state, toner %, page count.
- `printer consumables <ip>` prints drum life, maintenance warning.
- Uses raw UDP socket (no libsnmp dependency).
- Handles timeout gracefully (printer off or unreachable → error message).

## References

- [BRN-Printer-sCounters-Info](https://github.com/saper-2/BRN-Printer-sCounters-Info)
- [Monitoring Brother SNMP toner](https://www.claudiokuenzler.com/blog/1422/monitoring-brother-printer-snmp-alert-low-toner)
- [Domotz Brother SNMP sensors](https://www.domotz.com/integrations/hardware/printers/brother.php)
- [BROTHER-MIB browser](https://mibbrowser.online/mibdb_search.php?mib=BROTHER-MIB)
- `docs/brother-printer-protocol.md`
