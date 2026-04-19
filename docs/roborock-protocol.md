# Roborock — miio protocol

Port: UDP 54321  
Encryption: AES-128-CBC + MD5  
Reference: [mihome-binary-protocol](https://github.com/OpenMiHome/mihome-binary-protocol), [python-miio](https://github.com/rytilahti/python-miio)

---

## Packet format (big-endian)

```
Offset  Len  Field
  0       2   Magic number: 0x2131
  2       2   Total packet length (header + payload)
  4       4   Unknown: 0x00000000 (normal), 0xFFFFFFFF (Hello)
  8       4   Device ID (from hello response)
 12       4   Timestamp counter (incrementing)
 16      16   MD5 checksum (see below)
 32+    var   AES-128-CBC encrypted JSON payload
```

## Hello / discovery

Send 32 bytes, all fields 0xFF except magic and length:

```
21 31 00 20 FF FF FF FF FF FF FF FF 00 00 00 00
FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF
```

The device responds with the same structure. **Bytes 16–31 of the response contain the 16-byte raw device token.**

## Key derivation

```
key = MD5(token)              -- 16 bytes
iv  = MD5(key + token)        -- MD5 of 32-byte concat
```

## MD5 checksum field

Computed over the entire packet (token replaces the checksum field):

```
MD5(header_bytes[0..15] + token + encrypted_payload)
```

Write the 16-byte result into bytes 16–31 before sending.

## Payload: JSON commands

Encrypted with AES-128-CBC (PKCS#7 padding), then placed at offset 32.

```json
{"id": 1, "method": "app_start", "params": []}
```

`id` is an incrementing request counter. Response contains the same `id`.

## Commands

### Cleaning control

| Method              | Params              | Description                          |
|---------------------|---------------------|--------------------------------------|
| `app_start`         | `[]`                | Start full-house cleaning            |
| `app_stop`          | `[]`                | Stop                                 |
| `app_pause`         | `[]`                | Pause (can resume)                   |
| `app_charge`        | `[]`                | Return to dock                       |
| `app_spot`          | `[]`                | Spot clean at current location       |
| `find_device`       | `[]`                | Trigger audible/light locator        |
| `set_custom_mode`   | `[101–105]`         | Fan speed: 101=Silent, 102=Balanced, 103=Turbo, 104=Max, 105=Gentle |
| `reset_consumable`  | `["<item>"]`        | Reset consumable counter (see below) |

### Consumable reset items

| Parameter string           | Part                  | Replacement interval |
|----------------------------|-----------------------|----------------------|
| `"main_brush_work_time"`   | Main brush            | 300 h                |
| `"side_brush_work_time"`   | Side brush            | 200 h                |
| `"filter_work_time"`       | Filter                | 150 h                |
| `"sensor_dirty_time"`      | Cliff/wall sensors    | 30 h                 |

### Status and info

| Method           | Params | Description                     |
|------------------|--------|---------------------------------|
| `get_status`     | `[]`   | Full status (see below)         |
| `get_consumable` | `[]`   | Consumable usage in seconds     |

## Status response fields

`get_status` returns an object with (among others):

| Key          | Type | Description                                                         |
|--------------|------|---------------------------------------------------------------------|
| `battery`    | int  | Battery percentage 0–100                                            |
| `state`      | int  | 1=init, 2=sleep, 3=idle, 5=cleaning, 6=returning, 8=charging, 11=error, 16=shutdown |
| `error_code` | int  | 0=no error; see error code table in issue 001                       |
| `clean_time` | int  | Seconds of current/last clean                                       |
| `clean_area` | int  | Area cleaned in cm²                                                 |
| `fan_power`  | int  | 101=Silent, 102=Balanced, 103=Turbo, 104=Max, 105=Gentle           |
| `in_cleaning`| int  | 1 if currently cleaning                                             |
| `map_present`| int  | 1 if a map has been built                                           |
| `dnd_enabled`| int  | Do Not Disturb mode active                                          |

## Consumable response fields

`get_consumable` returns:

| Key                        | Type | Threshold (seconds) |
|----------------------------|------|---------------------|
| `main_brush_work_time`     | int  | 1 080 000 (300 h)  |
| `side_brush_work_time`     | int  | 720 000 (200 h)    |
| `filter_work_time`         | int  | 540 000 (150 h)    |
| `sensor_dirty_time`        | int  | 108 000 (30 h)     |

## Token extraction options

1. **Hello packet** — automatic (works for all devices on LAN)
2. **Xiaomi Mi Home app backup** via `adb backup -noapk com.xiaomi.smarthome`
3. **SSH on device** (if rooted): `cat /mnt/data/miio/device.token | xxd -p`
4. **Cloud tool**: [Xiaomi-cloud-tokens-extractor](https://github.com/PiotrMachowski/Xiaomi-cloud-tokens-extractor)

## Implementation notes for C

- All multi-byte fields: **big-endian** (`htonl`, `htons`, `ntohl`)
- AES: `EVP_aes_128_cbc()` with PKCS#7 padding (OpenSSL default — no need to disable)
- MD5: `EVP_md5()` via OpenSSL EVP
- Token storage: persist after first hello, re-use for all subsequent commands
- Timestamp field: use `time(NULL)` or a monotonic counter
- UDP socket: unicast to device IP (not broadcast), port 54321

## References

- [mihome-binary-protocol](https://github.com/OpenMiHome/mihome-binary-protocol)
- [python-miio](https://github.com/rytilahti/python-miio)
- [XiaomiRobotVacuumProtocol](https://github.com/marcelrv/XiaomiRobotVacuumProtocol)
- [python-roborock API commands](https://python-roborock.readthedocs.io/en/latest/api_commands.html)
- [Home Assistant Roborock integration](https://www.home-assistant.io/integrations/roborock/)
- [Socket API walkthrough](https://www.albertopasca.it/whiletrue/xiaomi-roborock-control-via-socket-api/)

## Dependency on binding

The token from the hello handshake is device-specific and does not change. Store it in `~/.config/home-appliances/roborock_devices` alongside the device IP and MAC.
