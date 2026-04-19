# Brother printer — local status protocol

Three usable protocols, in order of recommendation:

| Protocol | Port    | Info available             | Complexity |
|----------|---------|----------------------------|------------|
| SNMP     | UDP 161 | Toner, status, page count  | Low        |
| HTTP     | TCP 80  | HTML status page           | Low        |
| IPP      | TCP 631 | Printer state, markers     | Medium     |

---

## SNMP (recommended)

Community string: `public` (read-only, default on Brother printers).  
Version: SNMPv1 or SNMPv2c.  
Reference: [BRN-Printer-sCounters-Info](https://github.com/saper-2/BRN-Printer-sCounters-Info)

### Standard MIB-II OIDs

| OID                             | Description                      |
|---------------------------------|----------------------------------|
| `1.3.6.1.2.1.43.10.2.1.4.1.1`  | Total page count                 |
| `1.3.6.1.2.1.25.3.5.1.1.1`     | Printer status (3=idle, 4=printing, 5=error) |
| `1.3.6.1.2.1.43.11.1.1.9.1.1`  | Black toner remaining %          |

### Standard MIB additional OIDs

| OID                              | Description                                     |
|----------------------------------|-------------------------------------------------|
| `1.3.6.1.2.1.25.3.2.1.5.1`      | hrPrinterStatus: 3=idle, 4=printing, 5=stopped  |
| `1.3.6.1.2.1.43.11.1.1.8.1.1`   | prtMarkerSuppliesMaxCapacity (for % calc)       |
| `1.3.6.1.2.1.1.1.0`              | sysDescr — model string, use for probe/detect   |

### Brother proprietary OIDs

| OID                                    | Description                       |
|----------------------------------------|-----------------------------------|
| `1.3.6.1.4.1.2435.2.3.9.4.2.1.5.5.8`  | Maintenance / consumable remaining % |
| `1.3.6.1.4.1.2435.2.3.9.4.2.1.5.5.10` | Counter data (hex-encoded records) |
| `1.3.6.1.4.1.2435.2.3.9.4.2.1.5.5.11` | Pages until next maintenance       |
| `1.3.6.1.4.1.2435.2.3.9.1.1.2.10.1`   | Toner low flag (0=ok, 1=low, 2=absent, 3=empty) |

### OctetString decoding (brInfoMaintenance / brInfoCounter)

Brother returns `5.5.8` and `5.5.10` as raw OctetStrings (hex byte arrays).
The byte layout is model-specific. For many laser models:
- Each consumable is a 16-bit LE integer in units of 0.01 % (10000 = 100%).
- Specific byte offsets documented in:
  [BRN-Printer-sCounters-Info](https://github.com/saper-2/BRN-Printer-sCounters-Info)

Fallback: use standard MIB `1.3.6.1.2.1.43.11.1.1.9.1.1` / `.8.1.1` for toner %.

### SNMP packet (raw UDP, no library)

SNMP v1 GetRequest over UDP. Minimal implementation requires:
- BER/DER encoding of OID and PDU — roughly 50–80 bytes per request
- Simpler: shell out to `snmpget -v1 -c public <ip> <oid>` or link `libnetsnmp`

### C implementation options

1. **Link `net-snmp`** (`libsnmp-dev`): full SNMP API, reliable, large dependency
2. **Raw UDP BER encoding**: self-contained, ~200 lines — feasible for a handful of fixed OIDs
3. **Shell to `snmpget`**: simplest, but requires `net-snmp-utils` installed

---

## HTTP status page

The printer's embedded web server (port 80) requires no authentication by default.

```
GET http://<ip>/  →  HTML status overview
GET http://<ip>/general/status.html  →  Printer status
```

Parsing requires HTML scraping. Not recommended for programmatic use — fragile across firmware versions.

---

## IPP (Internet Printing Protocol)

Port: TCP 631. Protocol: HTTP POST with `Content-Type: application/ipp`.  
Operation: `Get-Printer-Attributes` (0x000b).

IPP uses custom binary attribute encoding. Practical C implementation requires either:
- **libcups** (CUPS library) — full IPP client, large dependency
- Manual binary encoding — ~300 lines for a minimal status query

Useful attributes returned:
- `printer-state`: 3=idle, 4=printing, 5=stopped
- `printer-state-reasons`: string array (e.g., `"toner-low"`, `"door-open"`)
- `marker-levels`: integer array, percent remaining per toner cartridge
- `media-ready`: available paper types

IPP endpoint URL: `http://<ip>:631/ipp` or `http://<ip>:631/ipp/print`

---

## Capability overview

| Capability               | SNMP | IPP | HTTP | Notes                                  |
|--------------------------|------|-----|------|----------------------------------------|
| Printer state            | Yes  | Yes | Yes  | Standard MIB + IPP `printer-state`    |
| Toner level %            | Yes  | Yes | Yes  | Standard MIB; IPP `marker-levels`     |
| Page count               | Yes  | No  | Yes  | `prtMarkerLifeCount`                   |
| Drum / consumable life   | Yes  | No  | Yes  | Brother OIDs `5.5.8`, `5.5.11`        |
| Error / jam state        | Yes* | Yes | Yes  | *Requires OctetString decoding         |
| Toner low flag           | Yes  | Yes | Yes  | `brToner1Low`; IPP `printer-state-reasons` |
| Counter reset            | No   | No  | No   | Manual control panel only              |
| Print job submission     | No   | Yes | No   | IPP Print-Job                          |
| Settings change          | No   | No  | Yes* | *Only via EWS web UI, not programmatic |
| SNMP traps               | Yes  | No  | No   | Configurable via EWS → UDP 162         |

## References

- [BRN-Printer-sCounters-Info](https://github.com/saper-2/BRN-Printer-sCounters-Info)
- [Monitoring Brother SNMP toner](https://www.claudiokuenzler.com/blog/1422/monitoring-brother-printer-snmp-alert-low-toner)
- [BROTHER-MIB browser](https://mibbrowser.online/mibdb_search.php?mib=BROTHER-MIB)
- [OID reference 2435](http://oidref.com/1.3.6.1.4.1.2435)
- [Home Assistant Brother integration](https://www.home-assistant.io/integrations/brother/)
- [Domotz Brother SNMP sensors](https://www.domotz.com/integrations/hardware/printers/brother.php)
- [RFC 8011 — IPP/1.1](https://www.rfc-editor.org/rfc/rfc8011)

## Implementation plan for C

Recommended minimal implementation:

1. **Status query**: raw SNMPv1 GetRequest over UDP (no library needed for 3–5 fixed OIDs)
   - OIDs: `1.3.6.1.2.1.25.3.5.1.1.1` (status), `1.3.6.1.2.1.43.11.1.1.9.1.1` (toner %)
2. **Page count**: same SNMP request, OID `1.3.6.1.2.1.43.10.2.1.4.1.1`
3. **No printing support needed** (out of scope for this CLI tool)

The SNMP raw encoding for a GetRequest with one OID is ~45 bytes; the response is ~60 bytes. Fully feasible without external libraries.
