# Issue 012 — Brother printer: probe / reachability check

**Type**: feature  
**Priority**: medium  
**Component**: `libappliances/src/infrastructure/brother_client`  
**User story**: US-P07

## Summary

Implement `printer probe <ip>` — a fast single-OID SNMP query that reports
whether the printer is on the network and returns the model name.

## Implementation

Query OID `1.3.6.1.2.1.1.1.0` (`sysDescr`) via SNMPv1 UDP/161.
This is the lightest possible SNMP request and always available on any
SNMP-enabled device.

Expected response for a Brother printer:
```
Brother NC-XXXX, Firmware Ver. X.XX
```

## CLI

```
$ printer probe 192.168.x.y
online — Brother NC-340 (Firmware Ver. H.13)

$ printer probe 192.168.x.z
offline (unreachable)
```

## Public API to add (`brother_client.h`)

```c
/**
 * @brief Probe printer reachability via SNMP sysDescr.
 * @param ip      Printer IPv4 address.
 * @param model   Output buffer for model string (or NULL to ignore).
 * @param model_len  Size of model buffer.
 * @return 1 if reachable, 0 if unreachable.
 */
int brother_probe(const char *ip, char *model, size_t model_len);
```

## Acceptance criteria

- `printer probe <ip>` prints "online — <model>" or "offline (unreachable)".
- Timeout: 2 s. Exit 0 in both cases; exit 1 on unexpected socket error.
- Model string extracted from sysDescr response.
- `--quiet` flag: no output, exit code 0=online / 1=offline.

## References

- Issue 005 — raw SNMP implementation (reuse packet builder)
- `docs/brother-printer-protocol.md`
