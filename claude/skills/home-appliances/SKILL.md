---
name: home-appliances
description: Use when you must read or change home appliances on the local network from the command line - Gree air conditioners (status, temperature, power, mode, fan), the Roborock robot vacuum, the Samsung smart TV (key codes, volume, source) or the Brother printer (toner, page count, state) - or discover which of them are on the network. Do not use for devices of other makers, for anything outside the local network, or to change the program itself.
---

# home-appliances

`home-appliances` controls appliances on the local network (`192.168.1.0/24`
by default) over their LAN protocols. Nothing is sent to a cloud. There is no
read-only variant: `status`, `temp`, `list`, `probe`, `toner`, `pages` and
`consumables` only read; everything else changes a device.

Complete reference: `man home-appliances`. `home-appliances --help` lists the
same commands.

## Commands

General:

```
home-appliances list              # known devices from the device stores
home-appliances discover          # scan the network for all types, update the lists
home-appliances --version
```

Gree AC (`<ip...>` accepts several addresses; output is then headed `[ip]`):

```
home-appliances ac list                     # broadcast scan
home-appliances ac bind <ip>                # pair once, saves the device key
home-appliances ac status <ip...>
home-appliances ac temp <ip...>             # room temperature, integer degrees C
home-appliances ac on|off <ip...>
home-appliances ac set <ip> temp=22 mode=cool fan=auto ...
home-appliances ac swing-v|xfan|health|air|antifrost <ip> on|off
home-appliances ac swing-h <ip> off|full|0-6
home-appliances ac unit <ip> c|f
```

`ac set` keys: `power=on|off temp=16-30 mode=auto|cool|dry|fan|heat
fan=auto|low|med-low|medium|med-high|high sleep quiet turbo swing_v xfan health
air antifrost lights=on|off swing_h=off|full|1-6 unit=c|f`.

Vacuum (Roborock):

```
home-appliances vacuum list
home-appliances vacuum status|consumables <ip>
home-appliances vacuum start|stop|pause|dock|spot|find <ip>
home-appliances vacuum fan <ip> silent|balanced|turbo|max|gentle
home-appliances vacuum reset-brush|reset-side-brush|reset-filter|reset-sensor <ip>
```

TV (Samsung):

```
home-appliances tv list
home-appliances tv probe <ip>
home-appliances tv key <ip> KEY_A [KEY_B ...]    # 300 ms between keys
home-appliances tv volup|voldown <ip> [N]
home-appliances tv mute|power|off|source|hdmi <ip>
home-appliances tv channel <ip> up|down
```

Printer (Brother):

```
home-appliances printer list
home-appliances printer probe <ip>
home-appliances printer status <ip> [--quiet]
home-appliances printer toner <ip> [--raw]       # --raw: the integer only
home-appliances printer pages <ip>
home-appliances printer consumables <ip>
```

Global options: `-v` / `--verbose` (debug output).

## Exit codes

- `0` success.
- `1` error: bad arguments, device unreachable, network failure.
- `2` only `printer status --quiet`: the printer reports a warning or error.

## Traps

- **Bind an AC first.** `ac status`/`set` on an address that was never bound
  fails; run `ac bind <ip>` once. `ac list` finds units but does not bind them.
- **Discovery needs the right subnet.** `ac list` and `discover` scan the
  `network=` value of `~/.config/home-appliances/config.ini`; a device outside
  it is not found, but can still be addressed by IP.
- **The device stores are caches** in `~/.config/home-appliances/`; they hold
  device keys and tokens (mode 0600). Do not print or commit them.
- **`xfan` works only in cool or dry mode**; the tool warns otherwise.
- **Changing commands act on real devices at once** (`ac off`, `vacuum start`,
  `tv power`). Use a read command first and act on the address you were given.
- `home-appliances-discovery run|cron-add|cron-status|cron-remove` is the
  separate scanner that keeps the device lists fresh from cron; it is not
  needed for ordinary use.
