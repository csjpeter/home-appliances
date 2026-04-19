# Issue 011 — Samsung TV: first-time pairing flow (on-screen acceptance)

**Type**: feature  
**Priority**: high  
**Component**: `libappliances/src/infrastructure/samsung_tv_client`  
**User story**: US-T06

## Summary

On the first connection from a new client, the TV shows an on-screen prompt and
the auth response is `0A 00 02 00 00 00` ("waiting"). The CLI must detect this,
print a human-readable message, and retry until the user accepts (or times out).

## Auth response codes (recap)

| Bytes (hex)          | Meaning                              |
|----------------------|--------------------------------------|
| `64 00 01 00`        | Access granted                       |
| `64 00 00 00`        | Access denied                        |
| `0A 00 02 00 00 00`  | Waiting for user to accept on TV     |

## Required behaviour in `samsung_tv_send_key`

```
1. Connect to TCP/55000
2. Send auth packet
3. Read response:
   - granted  → proceed to send key
   - denied   → print "Access denied by TV" and return -1
   - waiting  → print "Please accept the connection on the TV screen…"
                retry every 2 s, up to 15 attempts (30 s total)
                if still waiting after 30 s: return -1 with timeout message
4. Once granted: send key packet, close connection
```

## Acceptance criteria

- `tv key <ip> KEY_VOLUP` on first run: waits with spinner/message, proceeds once accepted.
- Subsequent calls from the same client name: no prompt, immediate grant.
- Client name is fixed: `"home-appliances"` (ensures auto-accept on next run).
- Timeout after 30 s: exit 1 with "Timed out waiting for TV acceptance".
- Access denied: exit 1 with clear message.

## References

- Issue 004 — `samsung_tv_send_key` base implementation
- `docs/samsung-tv-protocol.md` — auth response codes
