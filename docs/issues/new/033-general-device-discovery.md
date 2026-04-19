# Issue 033 — General device discovery for Roborock, Samsung TV, Brother printer

**Type**: feature  
**Priority**: medium  
**Component**: `libappliances/src/infrastructure/`, `src/main.c`

## Summary

Only Gree AC units support broadcast-based network discovery (`ac list`).
Roborock, Samsung TV and Brother printer require the user to know the IP address
in advance. This issue adds best-effort discovery for all three device types.

## Proposed approach per device

### Roborock vacuum
- Send the miio hello packet (`0x21 0x31 ... 0xFF...`) as UDP broadcast on
  `<network broadcast>:54321` with a short timeout (e.g. 2 s).
- Responding devices reply with their `device_id` and `token` in the hello response.
- CLI: `home-appliances vacuum list`

### Samsung TV (legacy protocol)
- Samsung TVs respond to a plain TCP probe on port 55000.
- Iterate over the subnet (or use ARP cache from `/proc/net/arp`) and attempt
  a short-timeout TCP connect; treat success as "TV found".
- CLI: `home-appliances tv list`

### Brother printer (SNMP)
- Send an SNMPv1 GetRequest for `sysDescr` (OID 1.3.6.1.2.1.1.1.0) as UDP
  broadcast on port 161. Printers that respond and whose sysDescr contains
  "Brother" are reported.
- CLI: `home-appliances printer list`

### General `list` command
Extend the existing `home-appliances list` to call all four discovery routines
in parallel (pthreads) and print a unified table.

## Implementation notes

- Broadcast scans must respect `cfg->network` and `cfg->discovery_timeout_ms`
  from `~/.config/home-appliances/config.ini` — no hardcoded subnets.
- After discovery, Roborock tokens are saved to `roborock_devices` automatically
  (same as the current `hello` flow).
- TV and printer do not require pairing/binding; their IPs can be printed without
  saving to a store file.

## Acceptance criteria

- `vacuum list`, `tv list`, `printer list` all complete without error on the local
  network (devices may or may not be found).
- `list` calls all four scanners and prints a unified result.
- No IP addresses hardcoded anywhere in the implementation.
- Functional tests: mock broadcast servers for each protocol; verify the list
  command finds and prints the mock device.
