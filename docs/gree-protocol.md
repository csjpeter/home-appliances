# Gree AC — LAN v2 protocol

Port: UDP 7000  
Encryption: AES-128-ECB + Base64, device-specific key obtained via bind  
Discovery key (public): `a3K8Bx%2r8Y7#xDh`  
References: [gree-remote](https://github.com/tomikaa87/gree-remote), [greeclimate](https://github.com/cmroche/greeclimate), [RobHofmann/HomeAssistant-GreeClimateComponent](https://github.com/RobHofmann/HomeAssistant-GreeClimateComponent), [openHAB Gree binding](https://www.openhab.org/addons/bindings/gree/)

---

## Message structure

All messages are UDP datagrams containing a JSON object:

```json
{"cid":"app","i":0,"pack":"<base64-AES>","t":"pack","tcid":"<device_mac>","uid":0}
```

- `i=1` on bind request, `i=0` on all other requests.
- `pack` field: AES-128-ECB encrypted JSON, zero-padded to 16-byte boundary, base64-encoded.
- Discovery and bind use the public key `a3K8Bx%2r8Y7#xDh`.
- All subsequent commands use the per-device key obtained during bind.

---

## Discovery (broadcast)

Send `{"t":"scan"}` to broadcast address on UDP/7000.

Each responding device replies with an outer JSON containing a `pack` field encrypted
with the public discovery key. Decrypted pack contains:

| Field  | Description                   |
|--------|-------------------------------|
| `cid`  | Device MAC address (12 hex)   |
| `name` | Human-readable device name    |
| `ver`  | Firmware version string       |
| `brand`| Brand identifier              |

---

## Bind

Request pack (encrypted with discovery key):
```json
{"mac":"<device_mac>","t":"bind","uid":0}
```

Response pack (also encrypted with discovery key) on success:
```json
{"t":"bindok","mac":"<device_mac>","key":"<16-char-device-key>"}
```

The `key` is permanent — it does not change between sessions or reboots.
Store it alongside IP and MAC for reuse.

---

## Status query

Request pack (encrypted with device key):
```json
{"cols":["Pow","SetTem","TemSen",...],"mac":"<device_mac>","t":"status"}
```

Response pack:
```json
{"cols":["Pow","SetTem","TemSen",...],"dat":[1,24,65,...],"t":"dat"}
```

The `dat` array corresponds positionally to the `cols` array.

---

## Command (set)

Request pack (encrypted with device key):
```json
{"opt":["Pow","SetTem"],"p":[1,22],"t":"cmd"}
```

Response pack on success:
```json
{"opt":["Pow","SetTem"],"p":[1,22],"r":200,"t":"res"}
```

`r:200` = success. Any other value indicates an error.

---

## Full parameter reference

### Settable parameters

| Parameter   | Values | Description                                                          |
|-------------|--------|----------------------------------------------------------------------|
| `Pow`       | 0/1    | Power: 0=off, 1=on                                                   |
| `Mod`       | 0–4    | Mode: 0=auto, 1=cool, 2=dry, 3=fan, 4=heat                         |
| `SetTem`    | 16–30  | Target temperature in Celsius (or Fahrenheit if TemUn=1)            |
| `TemUn`     | 0/1    | Temperature unit: 0=Celsius, 1=Fahrenheit                           |
| `TemRec`    | 0/1    | Fahrenheit half-degree bit (used when TemUn=1)                      |
| `WdSpd`     | 0–5    | Fan speed: 0=auto, 1=low, 2=med-low, 3=medium, 4=med-high, 5=high  |
| `Air`       | 0/1    | Air circulation (fresh air intake): 0=off, 1=on                     |
| `Blo`       | 0/1    | X-Fan / blow dry: runs fan after shutdown (cool/dry modes only)     |
| `Health`    | 0/1    | Health / cold-plasma anion generator (if equipped): 0=off, 1=on    |
| `SwhSlp`    | 0/1    | Sleep mode: 0=off, 1=on (gradual temp adjustment overnight)         |
| `Lig`       | 0/1    | Display lights: 0=off, 1=on                                         |
| `SwUpDn`    | 0/1    | Vertical swing: 0=fixed, 1=full swing                               |
| `SwingLfRig`| 0–6    | Horizontal swing: 0=fixed, 1=full swing, 2–6=fixed left→right positions |
| `Quiet`     | 0/1    | Quiet / silent mode: 0=off, 1=on                                    |
| `Tur`       | 0/1    | Turbo mode (max power): 0=off, 1=on (cool/dry only)                |
| `StHt`      | 0/1    | Steady heat (8°C anti-freeze mode): 0=off, 1=on                    |

### Read-only parameters

| Parameter    | Description                                                              |
|--------------|--------------------------------------------------------------------------|
| `TemSen`     | Room temperature from internal sensor. **Subtract 40** for actual °C.   |
| `HeatCoolType`| Device capability flag: 0=cool-only, 1=heat+cool                       |

### Notes

- `SvSt` appears in the protocol but its meaning is not definitively documented.
  Likely a "save state" flag used internally.
- `Wnd` is a legacy alias seen in some firmware; prefer `WdSpd` for fan speed.
- `Quiet` and `Tur` are mutually exclusive. Setting one should clear the other.
- `SwingLfRig` is not supported on all models (mainly Cooper & Hunter compatible units).

---

## TemSen room temperature decoding

Some firmware versions return `TemSen` as `(actual_temp_celsius + 40)`.
Example: TemSen = 65 → actual = 65 − 40 = 25 °C.

Other firmware versions return the raw value directly.
Heuristic: if TemSen > 60, subtract 40.

---

## Encryption compatibility

| Firmware       | Encryption         | Notes                                   |
|----------------|--------------------|-----------------------------------------|
| < v1.21        | AES-128-ECB        | Standard, fully supported               |
| v1.21–v1.22    | AES-128-ECB        | Works with current implementation       |
| v1.23+         | AES-128-GCM        | Breaking change — ECB clients fail      |

Firmware v1.23+ switched to AES-128-GCM. Detection: if ECB decryption of the
pack field yields garbage, retry with GCM. The GCM key is the same device key;
nonce and tag are prepended to the ciphertext.

Reference: [greeclimate GCM support](https://github.com/cmroche/greeclimate/pull/92)

---

## Capability overview

| Capability                  | Supported | Notes                                     |
|-----------------------------|-----------|-------------------------------------------|
| Power on/off                | Yes       | `Pow`                                     |
| Mode selection              | Yes       | `Mod` 0–4                                 |
| Temperature set             | Yes       | `SetTem` 16–30 °C                         |
| Room temperature read       | Yes       | `TemSen` (read-only, offset correction)  |
| Fan speed                   | Yes       | `WdSpd` 0–5                               |
| Vertical swing              | Yes       | `SwUpDn`                                  |
| Horizontal swing            | Partial   | `SwingLfRig` — not all models             |
| Sleep mode                  | Yes       | `SwhSlp`                                  |
| Turbo mode                  | Yes       | `Tur` (cool/dry only)                     |
| Quiet mode                  | Yes       | `Quiet`                                   |
| X-Fan / blow-dry            | Yes       | `Blo`                                     |
| Health / anion              | Partial   | `Health` — hardware-dependent             |
| Anti-freeze / steady heat   | Yes       | `StHt`                                    |
| Display lights              | Yes       | `Lig`                                     |
| Timer / scheduling          | No        | Not exposed in LAN protocol               |
| Error code readout          | No        | Not documented in LAN protocol            |
| GCM firmware support        | Planned   | Issue 009                                 |
