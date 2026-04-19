# Issue 004 — Samsung TV: probe and key sending

**Type**: feature  
**Priority**: high  
**Component**: `libappliances/src/infrastructure/samsung_tv_client`

## Summary

Implement the two core functions declared in `samsung_tv_client.h`:
- `samsung_tv_probe` — check reachability on TCP/55000
- `samsung_tv_send_key` — authenticate and send a KEY_* code

## Protocol recap (TCP/55000, pre-2016 models)

### Authentication packet (sent once per connection)

```
0x00
[0x13, 0x00]                        2-byte LE: length of "iphone.iapp.samsung" = 19
"iphone.iapp.samsung"               19 bytes, fixed
[payload_len_lo, payload_len_hi]    2-byte LE: total payload length
[0x64, 0x00]                        type: auth
[len_lo, 0x00] + b64(client_name)   base64 of "home-appliances"
[len_lo, 0x00] + b64(client_ip)     base64 of local IP
[len_lo, 0x00] + b64(client_mac)    base64 of any fixed 12-char hex string
```

### Auth response codes

| Bytes (hex)          | Meaning                                |
|----------------------|----------------------------------------|
| `64 00 01 00`        | Access granted                         |
| `64 00 00 00`        | Access denied                          |
| `0A 00 02 00 00 00`  | Waiting for user to accept on TV       |

On "waiting": retry after 2 s (max 10 attempts = ~20 s).

### Key code packet

```
0x00
[0x13, 0x00]             LE length of "iphone.iapp.samsung"
"iphone.iapp.samsung"
[len_lo, len_hi]         LE: length of inner section
[0x00, 0x00, 0x00]       3 zero bytes
[len_lo, len_hi]         LE: base64 key length
b64(key_code)            base64-encoded key string
```

## Complete key code reference

| Key code           | Action                  |
|--------------------|-------------------------|
| `KEY_POWER`        | Power toggle            |
| `KEY_POWEROFF`     | Power off (pre-2016)    |
| `KEY_VOLUP`        | Volume up               |
| `KEY_VOLDOWN`      | Volume down             |
| `KEY_MUTE`         | Mute toggle             |
| `KEY_CHUP`         | Channel up              |
| `KEY_CHDOWN`       | Channel down            |
| `KEY_PRECH`        | Previous channel        |
| `KEY_SOURCE`       | Input source            |
| `KEY_HDMI`         | HDMI input              |
| `KEY_0`–`KEY_9`    | Digit keys              |
| `KEY_ENTER`        | OK / Select             |
| `KEY_RETURN`       | Back                    |
| `KEY_EXIT`         | Exit                    |
| `KEY_MENU`         | Menu                    |
| `KEY_HOME`         | Home                    |
| `KEY_UP/DOWN/LEFT/RIGHT` | Navigation        |
| `KEY_PLAY`         | Play                    |
| `KEY_PAUSE`        | Pause                   |
| `KEY_STOP`         | Stop                    |
| `KEY_FF`           | Fast forward            |
| `KEY_REWIND`       | Rewind                  |
| `KEY_INFO`         | Info                    |
| `KEY_GUIDE`        | Guide                   |
| `KEY_TOOLS`        | Tools                   |
| `KEY_RED/GREEN/YELLOW/CYAN` | Colour keys  |
| `KEY_PIP_ONOFF`    | Picture-in-picture      |
| `KEY_PMODE`        | Picture mode            |
| `KEY_ASPECT`       | Aspect ratio            |

## Status query limitation

The legacy TCP/55000 protocol is **write-only**. No status is readable:
no channel, no input source, no volume, no app. The only observable state
is whether TCP connect succeeds (= TV on/standby) or fails (= TV off).

## Acceptance criteria

- `samsung_tv_probe(ip)` returns 1 if port 55000 is open, 0 otherwise (2 s timeout).
- `samsung_tv_send_key(ip, key)` connects, authenticates (with retry on waiting),
  sends the key, closes connection. Returns 0 or -1.
- `tv key KEY_VOLUP` CLI command works end-to-end.
- `tv probe` prints "online" / "offline".

## References

- [samsungctl](https://github.com/Ape/samsungctl)
- [Home Assistant key codes](https://github.com/ollo69/ha-samsungtv-smart/blob/master/docs/Key_codes.md)
- [SamyGO wiki](https://wiki.samygo.tv/index.php?title=Samsung_TV_network_remote_control_protocol)
- `docs/samsung-tv-protocol.md`
