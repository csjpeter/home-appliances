# home-appliances

Command-line tool for local network control of home appliances, written in C11.

## Supported devices

| Device | Protocol | Status |
|--------|----------|--------|
| Gree AC (LAN v2) | UDP/7000, AES-128-ECB | scan done, control in progress |
| robot vacuum | — | planned |
| smart TV | — | planned |
| nyomtató printer | — | planned |

## Requirements

- Linux, GCC ≥ 9 or Clang ≥ 11
- CMake ≥ 3.10
- Valgrind, lcov (development only)

```bash
./manage.sh deps   # Ubuntu 24.04 / Rocky 9
```

## Build

```bash
./manage.sh build      # Release → bin/home-appliances
./manage.sh debug      # Debug + AddressSanitizer
./manage.sh test       # Unit tests with ASAN
./manage.sh valgrind   # Valgrind leak check
./manage.sh coverage   # GCOV/LCOV coverage report
./manage.sh clean      # Remove build artifacts
```

## Usage

```bash
home-appliances list           # Scan network and list appliances
home-appliances version        # Print version
home-appliances help           # Show help
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

## License

MIT
