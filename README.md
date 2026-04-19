# home-appliances

Otthoni készülékek helyi hálózati vezérlése parancssori eszközzel, C11-ben írva.

## Támogatott készülékek

| Készülék | Protokoll | Állapot |
|----------|-----------|---------|
| Gree klíma (LAN v2) | UDP/7000, AES-128-ECB | scan kész, vezérlés folyamatban |
| robot vacuum | — | tervezett |
| smart TV | — | tervezett |
| nyomtató | — | tervezett |

## Követelmények

- Linux, GCC ≥ 9 vagy Clang ≥ 11
- CMake ≥ 3.10
- Valgrind, lcov (csak fejlesztéshez)

```bash
./manage.sh deps   # Ubuntu 24.04 / Rocky 9 esetén
```

## Build

```bash
./manage.sh build      # Release → bin/home-appliances
./manage.sh debug      # Debug + AddressSanitizer
./manage.sh test       # Unit tesztek ASAN-nal
./manage.sh valgrind   # Valgrind leak ellenőrzés
./manage.sh coverage   # GCOV/LCOV lefedettség
./manage.sh clean      # Build artefaktek törlése
```

## Használat

```bash
home-appliances list           # Hálózati scan, készülékek listázása
home-appliances version        # Verziószám
home-appliances help           # Súgó
```

## Architektúra

Tiszta rétegelt modell, nulla körköros függőséggel:

```
Application    src/main.c
Domain         libappliances/src/domain/
Infrastructure libappliances/src/infrastructure/
Core           libappliances/src/core/
Platform       libappliances/src/platform/posix/
```

## Konfiguráció

Az első indításkor alapértékek érvényesek. Manuális szerkesztés:

```ini
# ~/.config/home-appliances/config.ini
network=192.168.1.0/24
iface=wlan0
discovery_timeout_ms=5000
```

## Licenc

MIT
