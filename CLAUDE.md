# home-appliances — fejlesztési irányelvek

## Projekt célja

Otthoni készülékek helyi hálózati vezérlése CLI-ből:
- Gree klímák (UDP port 7000, AES-128-ECB, LAN v2 protokoll)
- robot vacuum
- smart TV
- nyomtató

Hálózat: `192.168.1.0/24`, ez a gép: `192.168.1.113` (myhost.local, `wlan0`)

## Build & futtatás

```bash
./manage.sh build       # Release build → bin/home-appliances
./manage.sh debug       # Debug build ASAN-nal
./manage.sh test        # Unit tesztek ASAN-nal
./manage.sh valgrind    # Valgrind leak ellenőrzés
./manage.sh coverage    # GCOV/LCOV lefedettség
./manage.sh clean       # Build artefaktek törlése
```

## Architektúra

Tiszta rétegelt architektúra, **nulla körköros függőséggel**:

```
Application    →  src/main.c
Domain         →  libappliances/src/domain/appliance_service.c
Infrastructure →  libappliances/src/infrastructure/ (gree_client, roborock_client, ...)
Core           →  libappliances/src/core/ (logger, config, raii.h, ...)
Platform       →  libappliances/src/platform/ (posix/ implementációk)
```

Felső rétegek **csak adatstruktúrákra** támaszkodnak, nem implementáció-belső dolgokra.

## Memóriakezelés: RAII (GNU cleanup attribútum)

```c
RAII_STRING   /* char *, free() */
RAII_FILE     /* FILE *, fclose() */
/* stb. — lásd libappliances/src/core/raii.h */
```

- **Nincs `goto cleanup`** — korai return mindig biztonságos
- Stack struct-ok mindig `= {0}` inicializálással

## Kódstílus

- C11, `-Wall -Wextra -Werror -pedantic -D_GNU_SOURCE`
- GCC vagy Clang (MSVC nem támogatott)
- 4 szóköz behúzás, Allman kapcsos zárójel stílus
- Fájlnév: `lowercase_underscore.c`
- Függvény: `<modul>_<akció>()` (pl. `gree_client_bind()`)
- Struct: typedef'd, `_t` utótag nélkül (pl. `GreeClient`, `Config`)
- Makró/konstans: `UPPER_CASE`
- Minden publikus függvényhez Doxygen komment a headerben

## Visszatérési értékek

- `int` függvény: `0` = siker, `-1` = hiba
- Pointer függvény: `NULL` = hiba

## Tesztelés

Saját minimális tesztkeretrendszer (`tests/common/test_helpers.h`):
```c
ASSERT(condition, "üzenet")
RUN_TEST(test_func)
```

- Nincs külső tesztkönyvtár
- ASAN + Valgrind kötelező átmenni
- Core + infrastructure kombinált lefedettség célja: >90%

## Claude Code eszközhasználat

- Célzott változtatásokhoz: Grep/Read/Edit eszközök közvetlenül
- Valóban párhuzamos, független munkához: Agent
- Nagy fájlokat ne olvass teljesen, ha csak egy rész kell (offset/limit)
