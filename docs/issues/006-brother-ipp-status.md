# Issue 006 — Brother printer: IPP status query

**Type**: feature  
**Priority**: medium  
**Component**: `libappliances/src/infrastructure/brother_client`

## Summary

Implement a minimal IPP Get-Printer-Attributes request over TCP/631 to retrieve
printer state and marker (toner) levels as an alternative/complement to SNMP.

## IPP request structure

Operation: `Get-Printer-Attributes` (0x000b)  
Transport: HTTP POST to `http://<ip>:631/ipp` with `Content-Type: application/ipp`

### Binary encoding (big-endian)

```
01 01                  -- IPP version 1.1
00 0B                  -- Operation: Get-Printer-Attributes
00 00 00 01            -- Request ID

01                     -- begin-attribute-group (operation attributes)
47 00 12               -- charset, length 18
  "attributes-charset"
47 00 05 "utf-8"
48 00 1B               -- natural-language, length 27
  "attributes-natural-language"
48 00 02 "en"
45 00 0B               -- printer-uri, length 11
  "printer-uri"
45 00 xx <uri>         -- http://<ip>:631/ipp

44 00 14               -- requested-attributes, length 20
  "requested-attributes"
  ... list of attribute names ...

03                     -- end-of-attributes
```

### Requested attributes

```
printer-state
printer-state-reasons
marker-levels
marker-names
marker-types
marker-colors
printer-make-and-model
printer-is-accepting-jobs
```

## Response parsing

Response binary: IPP attribute groups, each attribute prefixed with type tag + name length + name + value length + value.

Key attributes to extract:

| Attribute              | Type    | Meaning                           |
|------------------------|---------|-----------------------------------|
| `printer-state`        | integer | 3=idle, 4=printing, 5=stopped     |
| `printer-state-reasons`| string  | e.g. "toner-low", "door-open"     |
| `marker-levels`        | integer | % per cartridge (array)           |
| `marker-names`         | string  | e.g. "Black Cartridge"            |

## Acceptance criteria

- `printer ipp-status <ip>` prints state + marker levels in human-readable form.
- No external library (libcups) — raw TCP socket + minimal binary encoding.
- IPP failure falls back with a clear error; SNMP remains the primary path.

## References

- [RFC 8011 — IPP/1.1](https://www.rfc-editor.org/rfc/rfc8011)
- [RFC 3998 — IPP Administrative Operations](https://www.rfc-editor.org/rfc/rfc3998)
- `docs/brother-printer-protocol.md`
