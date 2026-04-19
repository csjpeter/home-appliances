# Samsung TV — local remote control protocol

The TV responds as "Samsung Swift httpd 1.0", which indicates a **pre-2016 model**.  
Protocol: TCP port 55000 (legacy Samsung remote protocol).  
Reference: [samsungctl](https://github.com/Ape/samsungctl), [SamyGO wiki](https://wiki.samygo.tv/index.php?title=Samsung_TV_network_remote_control_protocol)

---

## Authentication packet

Send immediately after TCP connect to port 55000:

```
[0x00]
[0x00, 0x13]                        -- "iphone.iapp.samsung" length (19), little-endian
"iphone.iapp.samsung"               -- 19 bytes, fixed string
[payload_len_lo, payload_len_hi]    -- payload length, little-endian
[0x64, 0x00]                        -- type: auth
[len_b64_name, 0x00] + b64(name)    -- client name, base64-encoded
[len_b64_ip,   0x00] + b64(ip)      -- client IP, base64-encoded
[len_b64_mac,  0x00] + b64(mac)     -- client MAC (or any 12-char string)
```

All length fields are 2-byte little-endian. Strings are base64-encoded (standard alphabet).

## Auth response

The TV replies with one of:

| Bytes               | Meaning                              |
|---------------------|--------------------------------------|
| `64 00 01 00`       | Access granted — ready for commands  |
| `64 00 00 00`       | Access denied                        |
| `0A 00 02 00 00 00` | Waiting for user to accept on TV     |

On "waiting", retry after ~2 seconds. The user must accept on the TV once; subsequent connections from the same client ID are auto-accepted.

## Key code packet

After auth is granted, send for each key press:

```
[0x00]
[0x00, 0x13]              -- "iphone.iapp.samsung" length
"iphone.iapp.samsung"     -- 19 bytes
[0x00, 0x00]              -- little-endian length of the following
[0x00, 0x00, 0x00]        -- 3 zero bytes
[len_lo, len_hi]          -- base64 key code length, little-endian
b64(key_code)             -- base64-encoded key name string
```

## Key codes (complete list)

### Power
| Key code       | Action                         |
|----------------|--------------------------------|
| `KEY_POWER`    | Power toggle                   |
| `KEY_POWEROFF` | Power off (pre-2016 models)    |
| `KEY_POWERON`  | Power on (pre-2016)            |

### Volume & audio
| Key code     | Action       |
|--------------|--------------|
| `KEY_VOLUP`  | Volume up    |
| `KEY_VOLDOWN`| Volume down  |
| `KEY_MUTE`   | Mute toggle  |

### Navigation
| Key code                        | Action      |
|---------------------------------|-------------|
| `KEY_UP` / `KEY_DOWN`           | Up / Down   |
| `KEY_LEFT` / `KEY_RIGHT`        | Left / Right|
| `KEY_ENTER`                     | OK / Select |
| `KEY_RETURN`                    | Back        |
| `KEY_EXIT`                      | Exit        |

### Channels
| Key code      | Action              |
|---------------|---------------------|
| `KEY_CHUP`    | Channel up          |
| `KEY_CHDOWN`  | Channel down        |
| `KEY_PRECH`   | Previous channel    |
| `KEY_FAVCH`   | Favourite channels  |
| `KEY_CH_LIST` | Channel list        |

### Input / source
| Key code         | Action              |
|------------------|---------------------|
| `KEY_SOURCE`     | Source selection    |
| `KEY_HDMI`       | HDMI input          |
| `KEY_TV`         | TV / antenna input  |
| `KEY_AV1`        | AV1 input           |
| `KEY_AV2`        | AV2 input           |
| `KEY_COMPONENT1` | Component 1         |
| `KEY_COMPONENT2` | Component 2         |
| `KEY_DVI`        | DVI input           |

### Digit keys
`KEY_0` through `KEY_9`, `KEY_11`, `KEY_12`

### Menu navigation
| Key code    | Action    |
|-------------|-----------|
| `KEY_MENU`  | Menu      |
| `KEY_HOME`  | Home      |
| `KEY_TOOLS` | Tools     |
| `KEY_INFO`  | Info      |
| `KEY_GUIDE` | Guide     |
| `KEY_HELP`  | Help      |

### Media playback
| Key code             | Action          |
|----------------------|-----------------|
| `KEY_PLAY`           | Play            |
| `KEY_PAUSE`          | Pause           |
| `KEY_STOP`           | Stop            |
| `KEY_FF`             | Fast forward    |
| `KEY_REWIND`         | Rewind          |
| `KEY_REC`            | Record          |

### Picture
| Key code        | Action           |
|-----------------|------------------|
| `KEY_PMODE`     | Picture mode     |
| `KEY_ASPECT`    | Aspect ratio     |
| `KEY_PANORAMA`  | Panorama mode    |
| `KEY_ZOOM_IN`   | Zoom in          |
| `KEY_ZOOM_OUT`  | Zoom out         |
| `KEY_PIP_ONOFF` | Picture-in-picture toggle |
| `KEY_PIP_SWAP`  | PIP swap         |

### Colour / teletext
| Key code    | Action        |
|-------------|---------------|
| `KEY_RED`   | Red           |
| `KEY_GREEN` | Green         |
| `KEY_YELLOW`| Yellow        |
| `KEY_CYAN`  | Cyan          |
| `KEY_TELETEXT` | Teletext   |

## Capability overview

| Capability                | TCP/55000 (pre-2016) | Notes                                       |
|---------------------------|----------------------|---------------------------------------------|
| Power state detection     | No                   | TCP connect = on/standby; refused = off     |
| Volume / mute read        | No                   | Write-only protocol                         |
| Channel / input read      | No                   | Write-only protocol                         |
| Remote key sending        | Yes (100+ codes)     | Full key set supported                      |
| App launching             | No                   | Only available on 2016+ Tizen REST API      |
| Status feedback per key   | No                   | Fire-and-forget                             |

## References

- [samsungctl](https://github.com/Ape/samsungctl)
- [Home Assistant key codes](https://github.com/ollo69/ha-samsungtv-smart/blob/master/docs/Key_codes.md)
- [SamyGO wiki](https://wiki.samygo.tv/index.php?title=Samsung_TV_network_remote_control_protocol)
- [openHAB Samsung TV binding](https://www.openhab.org/addons/bindings/samsungtv/)

## Implementation notes for C

- Raw TCP socket: `connect()` to TV IP port 55000
- Base64: reuse the project's `base64.h` module
- The "client name" string is arbitrary (e.g., `"home-appliances"`)
- The "client MAC" field is not validated by the TV; use any fixed 12-char hex string
- No keep-alive needed: send auth → send keys → close
- Key presses are fire-and-forget; no response per key
- If the TV is off (standby), it still listens on port 55000 — `KEY_POWER` wakes it

## TV info / current state

The legacy TCP protocol has **no status query**. To detect TV state:
- Attempt TCP connect to port 55000: success = TV is on (or standby)
- Connection refused / timeout = TV is fully off (power-off, not standby)

For Tizen (2016+) TVs only: REST GET `http://IP:8001/api/v2/` returns JSON with TV model, name, and on/off state.
