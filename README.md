[![CI](https://github.com/csjpeter/home-appliances/actions/workflows/ci.yml/badge.svg)](https://github.com/csjpeter/home-appliances/actions/workflows/ci.yml)
[![Valgrind](https://github.com/csjpeter/home-appliances/actions/workflows/valgrind.yml/badge.svg)](https://github.com/csjpeter/home-appliances/actions/workflows/valgrind.yml)
[![Coverage](https://csjpeter.github.io/home-appliances/coverage-badge.svg)](https://csjpeter.github.io/home-appliances/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

# home-appliances

Command-line tool for local network control of home appliances, written in C11.

## Supported devices

| Device | Protocol | Status |
|--------|----------|--------|
| Gree AC (LAN v2) | UDP/7000, AES-128-ECB | fully implemented |
| Roborock vacuum | UDP/54321, AES-128-CBC, miio | fully implemented |
| Samsung TV (legacy) | TCP/55000, base64 auth | fully implemented |
| Brother printer | SNMPv1 UDP/161, BER | fully implemented |

## Requirements

- Linux, GCC ≥ 9 or Clang ≥ 11
- CMake ≥ 3.10
- OpenSSL (libssl-dev)
- Valgrind, lcov (development only)

```bash
./manage.sh deps   # Ubuntu 24.04 / Rocky 9
```

## Build & install

```bash
./manage.sh build      # Release → bin/home-appliances
./manage.sh debug      # Debug + AddressSanitizer
./manage.sh test       # Unit + functional + PTY tests with ASAN
./manage.sh valgrind   # Valgrind leak check
./manage.sh coverage   # GCOV/LCOV coverage report → build-Coverage/lcov/index.html
./manage.sh clean      # Remove build artifacts

# Install to ~/.local (default — no sudo needed)
./manage.sh install

# Or to a custom prefix (e.g. system-wide)
sudo PREFIX=/usr/local ./manage.sh install

# Uninstall (removes files listed in build-Release/install_manifest.txt)
./manage.sh uninstall
```

Installed files (default prefix `~/.local`):
- `~/.local/bin/home-appliances`
- `~/.local/share/man/man1/home-appliances.1`
- `~/.local/share/applications/home-appliances.desktop`

The install step runs `mandb --user-db` automatically so `man home-appliances` works
immediately. If it still does not, add to `~/.bashrc`:

```bash
export MANPATH="$HOME/.local/share/man:$MANPATH"
```

## Usage

```bash
# General
home-appliances list           # Scan network and list appliances
home-appliances --help         # Show full command reference
home-appliances --version      # Print version
home-appliances --verbose …    # Enable debug output for any command
home-appliances -v …           # Short form of --verbose

# Gree AC
home-appliances ac list                         # Scan for AC units
home-appliances ac bind <ip>                    # Bind (pair) with device
home-appliances ac status <ip>                  # Show full status
home-appliances ac on|off <ip>                  # Power on/off
home-appliances ac set <ip> temp=24 mode=cool   # Set parameters

# Roborock vacuum
home-appliances vacuum status <ip>              # Show status
home-appliances vacuum start|stop|pause <ip>    # Start/stop/pause
home-appliances vacuum dock|spot|find <ip>      # Dock / spot clean / locate
home-appliances vacuum fan <ip> turbo           # Set fan speed
home-appliances vacuum consumables <ip>         # Brush/filter life

# Samsung TV
home-appliances tv key <ip> KEY_VOLUMEUP        # Send key
home-appliances tv volup|voldown <ip> [N]       # Volume up/down N times
home-appliances tv mute|power <ip>              # Mute / toggle power

# Brother printer
home-appliances printer status <ip>             # Toner, state, page count
home-appliances printer toner <ip>              # Toner level only
home-appliances printer status <ip> --quiet     # Silent; exit 2 if warning
```

## Architecture

Clean layered model with zero circular dependencies:

```
Application    src/main.c
Domain         libappliances/src/domain/
Infrastructure libappliances/src/infrastructure/
Core           libappliances/src/core/
Platform       libappliances/src/platform/posix/
```

## Configuration

Defaults apply on first run. Edit manually if needed:

```ini
# ~/.config/home-appliances/config.ini
network=192.168.1.0/24
iface=wlan0
discovery_timeout_ms=5000
```

Device credentials are stored automatically after first bind/pair:

```
~/.config/home-appliances/gree_devices        # mode 0600
~/.config/home-appliances/roborock_devices    # mode 0600
```

## Man page

```bash
man home-appliances          # after install
man ./docs/man/home-appliances.1   # from source tree
```

## License

MIT
