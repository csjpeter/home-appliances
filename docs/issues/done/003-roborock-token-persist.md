# Issue 003 — Roborock: token persistence

**Type**: feature  
**Priority**: medium  
**Component**: `libappliances/src/infrastructure/roborock_client`

## Summary

After the first hello handshake retrieves the 16-byte device token, persist it
to `~/.config/home-appliances/roborock_devices` so subsequent commands do not
need to repeat the discovery handshake.

## File format

One device per line, space-separated:

```
<ip> <token_hex32> <device_id_hex8>
```

Example:
```
192.168.50.25 a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6 12ab34cd
```

- `token_hex32`: 32 hex chars = 16 raw bytes from hello response bytes 16–31
- `device_id_hex8`: 8 hex chars = 4 raw bytes from hello response bytes 8–11

## Behaviour

1. On any command: load store, try matching by IP.
2. If found: skip hello, use stored token and device ID.
3. If not found: send hello, extract token + device ID, save to store, proceed.
4. `vacuum discover` command: always send hello, update or add entry.

## Token extraction reminder

Hello response layout (big-endian):
```
Bytes  0–1   Magic 0x2131
Bytes  2–3   Packet length
Bytes  4–7   0xFFFFFFFF (hello marker)
Bytes  8–11  Device ID (4 bytes)
Bytes 12–15  Timestamp
Bytes 16–31  Token (16 raw bytes)
```

## Acceptance criteria

- First run: hello sent, token stored, file created with 0600 permissions.
- Second run: no hello, token loaded from file.
- `vacuum discover` refreshes token in store.
- File missing or unreadable: fall back to hello handshake.

## References

- `docs/roborock-protocol.md`
- Gree precedent: `gree_client_load_bound` / `gree_client_save_bound`
