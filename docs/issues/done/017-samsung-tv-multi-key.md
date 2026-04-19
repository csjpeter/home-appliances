# Issue 017 — Samsung TV: multi-key and repeat count

**Type**: feature  
**Priority**: medium  
**Component**: `libappliances/src/infrastructure/samsung_tv_client`, `src/main.c`  
**Covers**: US-T02, US-T05

## Summary

Allow sending multiple key codes in one TCP session (authentication done once),
and add a repeat-count option for volume/channel commands.

## Library addition (`samsung_tv_client.h/.c`)

```c
/**
 * @brief Send multiple key codes in one TCP session.
 *        Authenticates once, sends each key with delay_ms between them.
 * @param ip       TV IPv4 address.
 * @param keys     NULL-terminated array of KEY_* strings.
 * @param delay_ms Delay between keys in milliseconds (0 = no delay).
 * @return 0 on success, -1 on error.
 */
int samsung_tv_send_keys(const char *ip, const char **keys, int delay_ms);
```

Implementation: refactor `samsung_tv_send_key` to extract
`tv_connect_and_auth(ip, fd_out)` → reuse in both functions.

## CLI additions (`main.c`)

### `tv key <ip> KEY1 [KEY2 KEY3 ...]`
Extend `cmd_tv_key` to accept additional key args; send all with 300 ms gap.

### `tv volup <ip> [N]` / `tv voldown <ip> [N]`
Optional repeat count N (default 1); send KEY_VOLUP/KEY_VOLDOWN N times
in one session with 200 ms gap.

### Also fix: Samsung TV pairing stdout message
Currently the library only logs via LOG_WARN. Add `fprintf(stderr, ...)` on
the first WAITING response so the user sees:
```
Waiting for TV to accept connection — please approve on screen...
```

## Acceptance criteria

- `tv key <ip> KEY_MENU KEY_DOWN KEY_ENTER` works (single TCP session, 300 ms gaps).
- `tv volup <ip> 5` sends KEY_VOLUP five times with 200 ms gaps.
- On first pairing: "Waiting for TV to accept connection..." printed to stderr.
- Existing single-key `tv key <ip> KEY_X` unchanged.

## References

- Issue 004, 011
- `docs/samsung-tv-protocol.md`
