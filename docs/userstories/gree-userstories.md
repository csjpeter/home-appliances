# Gree AC — user stories

## US-A01: Check AC state before entering a room

**As a** home user,  
**I want to** see the AC's current power, mode, temperature, and room temperature,  
**So that** I know whether the room is already comfortable before I go in.

### Acceptance criteria

- `ac status <ip>` prints a table: power, mode, target temp, room temp, fan speed, swing, quiet, turbo, sleep, lights.
- Room temperature shown in °C regardless of AC's unit setting.
- If device is offline, clear error message with exit code 1.

### Example output

```
Power:       on
Mode:        cool
Target temp: 22 °C
Room temp:   25 °C
Fan speed:   medium
Swing V:     full
Quiet:       off
Turbo:       off
Sleep:       off
Lights:      on
```

---

## US-A02: Turn the AC on and off quickly

**As a** home user,  
**I want to** power the AC on or off from the terminal with a short command,  
**So that** I can include it in scripts or do it from my desk without opening an app.

### Acceptance criteria

- `ac on <ip>` sends `Pow=1`, prints "AC on".
- `ac off <ip>` sends `Pow=0`, prints "AC off".
- Both commands also print the resulting room temperature (from a status query).

---

## US-A03: Set temperature and mode for the season

**As a** home user,  
**I want to** set the operating mode (cool / heat / dry / fan) and target temperature,  
**So that** the AC operates correctly for the current season.

### Acceptance criteria

- `ac set <ip> mode=cool temp=22` works as a combined command.
- Friendly mode names: `auto`, `cool`, `dry`, `fan`, `heat`.
- Temperature validated: 16–30 °C; out-of-range value prints error without sending.
- Current mode shown in `ac status`.

---

## US-A04: Adjust fan speed

**As a** home user,  
**I want to** set the fan speed without changing other settings,  
**So that** I can reduce noise or improve airflow independently.

### Acceptance criteria

- `ac set <ip> fan=low|med-low|medium|med-high|high|auto` accepted.
- Numeric shorthand: `fan=1` through `fan=5` also accepted.
- Current fan speed shown in `ac status`.

---

## US-A05: Enable sleep mode for overnight comfort

**As a** home user,  
**I want to** activate sleep mode before going to bed,  
**So that** the AC gradually adjusts temperature during the night without waking me.

### Acceptance criteria

- `ac set <ip> sleep=on` sends `SwhSlp=1`.
- `ac set <ip> sleep=off` sends `SwhSlp=0`.
- Sleep state visible in `ac status`.

---

## US-A06: Control vertical and horizontal airflow direction

**As a** home user,  
**I want to** set the swing direction (vertical and horizontal),  
**So that** air is distributed evenly without blowing directly at me.

### Acceptance criteria

- `ac swing-v <ip> on|off` controls vertical swing.
- `ac swing-h <ip> off|full|1|2|3|4|5|6` controls horizontal position (if supported).
- If horizontal swing is not supported by the device, print "horizontal swing not supported" and exit 0.
- Both swing states visible in `ac status`.

---

## US-A07: Use X-Fan to prevent mould after cooling

**As a** home user,  
**I want to** enable X-Fan (blow-dry) mode,  
**So that** the AC fan keeps running after shutdown to dry the heat exchanger and prevent mould.

### Acceptance criteria

- `ac xfan <ip> on|off` sends `Blo=1/0`.
- X-Fan only works in cool and dry modes — warn if current mode is incompatible.
- State visible in `ac status`.

---

## US-A08: Enable anti-freeze mode in winter

**As a** home user,  
**I want to** activate the 8 °C steady heat (anti-freeze) mode when I'm away in winter,  
**So that** pipes don't freeze while keeping energy use low.

### Acceptance criteria

- `ac antifrost <ip> on|off` sends `StHt=1/0`.
- When on, `ac status` shows "Mode: anti-freeze (8 °C)".

---

## US-A09: Discover and bind new AC units automatically

**As a** home user / installer,  
**I want to** discover all Gree AC units on my network and bind them in one step,  
**So that** I can start controlling them without manual configuration.

### Acceptance criteria

- `ac list` broadcasts on the local subnet, lists found devices (IP, MAC, name, firmware).
- `ac bind <ip>` performs the bind handshake and saves the device key.
- `ac list` shows `[bound]` next to already-bound devices.
- Device keys stored in `~/.config/home-appliances/gree_devices` with 0600 permissions.

---

## US-A10: Control multiple AC units in one script

**As a** power user,  
**I want to** send the same command to multiple AC units by passing multiple IPs,  
**So that** I can turn off all units in the house with one line.

### Acceptance criteria

- `ac off <ip1> <ip2> <ip3>` sends the command to each in sequence.
- Each device result reported separately: "192.168.x.y: off" or error.
- Exit code: 0 if all succeeded, 1 if any failed.

---

## US-A11: Scripted temperature-based automation

**As a** power user,  
**I want to** read the current room temperature as a plain number,  
**So that** I can use it in shell scripts (e.g., turn off if room < 20 °C).

### Acceptance criteria

- `ac temp <ip>` prints just the integer room temperature in °C.
- Exit code 0 on success, 1 if temperature unavailable.
- Usable as: `[ $(ac temp 192.168.x.y) -lt 20 ] && ac off 192.168.x.y`
