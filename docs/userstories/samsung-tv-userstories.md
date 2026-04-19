# Samsung TV — user stories

## US-T01: Check whether the TV is on

**As a** home user,  
**I want to** know if the TV is powered on or off without physically looking at it,  
**So that** I can decide whether to send commands.

### Acceptance criteria

- `tv probe <ip>` attempts TCP connect to port 55000.
- Prints "online" (connection succeeded) or "offline" (refused/timeout).
- Timeout: 2 seconds. Exit 0 either way; exit 1 on unexpected error.

### Limitation note

The legacy TCP/55000 protocol cannot distinguish "on" from "standby" — both respond.
"Offline" means the TV is completely powered off at the wall.

---

## US-T02: Control volume from the CLI

**As a** home user,  
**I want to** raise, lower, or mute the TV volume with a terminal command,  
**So that** I don't have to leave my desk when a notification blares.

### Acceptance criteria

- `tv volup <ip>` sends `KEY_VOLUP`.
- `tv voldown <ip>` sends `KEY_VOLDOWN`.
- `tv mute <ip>` sends `KEY_MUTE`.
- Repeat count option: `tv volup <ip> 5` sends the key 5 times with 200 ms gap.
- Exit 0 on success, 1 on connection/auth error.

---

## US-T03: Change channel or input source

**As a** home user,  
**I want to** switch channels or select HDMI input from the terminal,  
**So that** I can set up the TV from across the room before sitting down.

### Acceptance criteria

- `tv key <ip> KEY_HDMI` and any other KEY_* code accepted.
- `tv source <ip>` sends `KEY_SOURCE`.
- `tv hdmi <ip>` sends `KEY_HDMI`.
- `tv channel <ip> up|down` sends `KEY_CHUP` / `KEY_CHDOWN`.

---

## US-T04: Power the TV on or off

**As a** home user,  
**I want to** turn the TV on or off from the CLI,  
**So that** I can include it in shell scripts (e.g. "turn off at midnight").

### Acceptance criteria

- `tv power <ip>` sends `KEY_POWER`.
- `tv off <ip>` sends `KEY_POWEROFF` (pre-2016) with fallback to `KEY_POWER`.
- If TCP connect fails (TV fully off), `tv on <ip>` is a no-op with a note:
  "TV is fully off — use Wake-on-LAN or the physical remote to power on."

### Limitation note

`KEY_POWER` is a toggle. There is no "power on" command in the legacy protocol —
the TV must already be in standby for the key to wake it.

---

## US-T05: Send arbitrary remote key codes

**As a** power user,  
**I want to** send any KEY_* code to the TV,  
**So that** I can control any function the remote has.

### Acceptance criteria

- `tv key <ip> <KEY_CODE>` sends the raw key string.
- Key code validated against a known list; unknown codes print a warning but are sent anyway.
- Multiple keys in one call: `tv key <ip> KEY_MENU KEY_DOWN KEY_ENTER` with 300 ms gap.

---

## US-T06: On-screen TV acceptance — first pairing

**As a** new user,  
**I want to** pair the CLI with the TV without manual configuration,  
**So that** I only need to accept once on the TV screen and subsequent commands work silently.

### Acceptance criteria

- On first connection, if TV responds "waiting for acceptance", the CLI prints:
  `"Please accept the connection on the TV screen…"` and retries for up to 30 s.
- Once accepted, subsequent commands are auto-accepted (same client name).
- Client name is fixed: `"home-appliances"`, client MAC is a fixed dummy string.
