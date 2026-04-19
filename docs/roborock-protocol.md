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

## Essential commands

| Method              | Params | Description                   |
|---------------------|--------|-------------------------------|
| `app_start`         | `[]`   | Start cleaning                |
| `app_stop`          | `[]`   | Stop                          |
| `app_pause`         | `[]`   | Pause                         |
| `app_charge_home`   | `[]`   | Return to dock                |
| `get_status`        | `[]`   | Full status (see below)       |

## Status response fields

`get_status` returns an object with (among others):

| Key         | Type | Description                          |
|-------------|------|--------------------------------------|
| `battery`   | int  | Battery percentage 0–100             |
| `state`     | int  | 1=initiating, 2=sleeping, 3=idle, 5=cleaning, 6=returning, 8=charging, 11=error, 16=shutdown |
| `error_code`| int  | 0=no error                           |
| `clean_time`| int  | Seconds of current/last clean        |
| `clean_area`| int  | Area cleaned in cm²                  |
| `in_cleaning`| int | 1 if currently cleaning              |

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

## Dependency on binding

The token from the hello handshake is device-specific and does not change. Store it in `~/.config/home-appliances/roborock_devices` alongside the device IP and MAC.
