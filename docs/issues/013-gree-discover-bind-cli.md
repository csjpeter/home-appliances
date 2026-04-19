# Issue 013 — Gree: discover and bind CLI commands

**Type**: feature  
**Priority**: high  
**Component**: `src/main.c`, `libappliances/src/infrastructure/gree_client`  
**User story**: US-A09

## Summary

Implement `ac list` (discovery) and `ac bind` (bind handshake) as first-class CLI
subcommands so users can set up new AC units without manual configuration.

## Current state

`gree_client_scan` and `gree_client_bind` exist in the library. The main.c
`cmd_ac_list` and `cmd_ac_bind` functions exist but need review for completeness
and user-facing output quality.

## `ac list` requirements

```
$ ac list
Scanning 192.168.1.255… (timeout 3 s)

Found 2 device(s):
  192.168.x.1   MAC: aabbccddee01   Name: Bedroom AC       Firmware: 1.20   [bound]
  192.168.x.2   MAC: aabbccddee02   Name: Living Room AC   Firmware: 1.23   [not bound]
```

- Broadcast on the subnet derived from `wlan0` (or from config `broadcast` key).
- Show `[bound]` if MAC exists in `~/.config/home-appliances/gree_devices`.
- Show firmware version (warn if v1.23+ → GCM required, see issue 009).

## `ac bind` requirements

```
$ ac bind 192.168.x.2
Binding 192.168.x.2 (aabbccddee02)… OK
Device key saved to ~/.config/home-appliances/gree_devices
```

- If already bound, ask for confirmation before overwriting.
- On failure, print protocol error and exit 1.

## `ac list --all` / `ac list --unbound`

- `--all`: show all discovered devices (default).
- `--unbound`: show only devices not yet in the store.

## Acceptance criteria

- `ac list` output shows all fields above.
- `ac bind <ip>` saves key; subsequent `ac status <ip>` works without re-binding.
- Firmware v1.23+ shown with warning: `[GCM — not yet supported, see issue 009]`.
- Config `broadcast` key respected for non-standard subnet.

## References

- `gree_client_scan`, `gree_client_bind` in `gree_client.c`
- `gree_client_load_bound`, `gree_client_save_bound`, `gree_client_merge_bound`
- Issue 009 — GCM firmware compat
- `docs/gree-protocol.md`
