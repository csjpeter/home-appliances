# Roborock — user stories

## US-R01: Morning cleaning check

**As a** home user,  
**I want to** see the current vacuum state and battery level with one command,  
**So that** I know whether the robot finished cleaning and is back on the dock.

### Acceptance criteria

- `vacuum status <ip>` displays: state label, battery %, cleaned area (m²), duration, error (if any).
- State label is human-readable ("charging", "idle", "cleaning", "returning", "error").
- If battery < 20 % and state = idle, a warning line is printed.

### Example output

```
State:    charging
Battery:  87%
Last run: 32 min, 24.5 m²
Error:    none
```

---

## US-R02: Start or stop cleaning from the terminal

**As a** home user,  
**I want to** start, pause, or stop the vacuum from the CLI,  
**So that** I can trigger or abort cleaning without opening the phone app.

### Acceptance criteria

- `vacuum start` / `vacuum pause` / `vacuum stop` / `vacuum dock` all work.
- Each command prints a one-line confirmation ("Cleaning started", "Returning to dock").
- If already in the target state, a suitable message is shown and exit code is 0.

---

## US-R03: Send it home when I'm leaving

**As a** home user,  
**I want to** send the vacuum to the dock with a single command,  
**So that** it doesn't block the floor while I'm out.

### Acceptance criteria

- `vacuum dock <ip>` sends `app_charge`, waits 3 s, queries state, prints "returning" or "docked".

---

## US-R04: Check consumables before ordering replacements

**As a** home user,  
**I want to** see the remaining life of brushes, filter, and sensors,  
**So that** I know which parts need ordering before they fail.

### Acceptance criteria

- `vacuum consumables <ip>` prints a table:
  ```
  Main brush:  74%  (approx 78 h left)
  Side brush:  52%  (approx 52 h left)
  Filter:      31%  (approx 46 h left)
  Sensors:     OK
  ```
- Warning marker (`!`) on items below 20%.

---

## US-R05: Reset consumable counter after replacement

**As a** home user,  
**I want to** reset a consumable counter after replacing a part,  
**So that** the remaining-life calculation is accurate again.

### Acceptance criteria

- `vacuum reset-brush <ip>` resets main brush counter, prints new value (0 s used).
- Same for `reset-side-brush`, `reset-filter`, `reset-sensor`.
- Command requires no confirmation (irreversible but cheap).

---

## US-R06: Locate the vacuum when it gets stuck

**As a** home user,  
**I want to** trigger the vacuum's sound/light signal,  
**So that** I can find it when it got stuck somewhere.

### Acceptance criteria

- `vacuum find <ip>` sends `find_device`, prints "Locating…".
- Exit 0 on success.

---

## US-R07: Adjust suction for different floor types

**As a** home user,  
**I want to** change the fan speed from the CLI,  
**So that** I can reduce noise on hard floors or boost suction on carpet.

### Acceptance criteria

- `vacuum fan <ip> silent|balanced|turbo|max|gentle` sends `set_custom_mode`.
- Friendly name maps to numeric: silent=101, balanced=102, turbo=103, max=104, gentle=105.
- Current fan speed shown in `vacuum status` output.

---

## US-R08: Token-free subsequent commands

**As a** developer/power user,  
**I want to** run vacuum commands without repeating the hello handshake every time,  
**So that** commands are faster and don't fail when the robot is cleaning.

### Acceptance criteria

- Token persisted after first successful hello to `~/.config/home-appliances/roborock_devices`.
- Subsequent commands skip hello, use stored token.
- `vacuum discover <ip>` forces token refresh.
