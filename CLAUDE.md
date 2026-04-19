# home-appliances — development guidelines

## Project goal

Local network control of home appliances from the CLI:
- Gree AC units (UDP port 7000, AES-128-ECB, LAN v2 protocol)
- robot vacuum
- smart TV
- nyomtató printer

Network: `192.168.1.0/24`, this machine: `192.168.1.113` (myhost.local, `wlan0`)

## Build & run

```bash
./manage.sh build       # Release build → bin/home-appliances
./manage.sh debug       # Debug build with ASAN
./manage.sh test        # Unit tests with ASAN
./manage.sh valgrind    # Valgrind leak check
./manage.sh coverage    # GCOV/LCOV coverage report
./manage.sh clean       # Remove build artifacts
```

## Architecture

Clean layered architecture with **zero circular dependencies**:

```
Application    →  src/main.c
Domain         →  libappliances/src/domain/appliance_service.c
Infrastructure →  libappliances/src/infrastructure/ (gree_client, ...)
Core           →  libappliances/src/core/ (logger, config, raii.h, ...)
Platform       →  libappliances/src/platform/ (posix/ implementations)
```

Upper layers depend only on data structures, never on internal implementations.

## Memory management: RAII (GNU cleanup attribute)

```c
RAII_STRING   /* char *, free() */
RAII_FILE     /* FILE *, fclose() */
/* see libappliances/src/core/raii.h */
```

- **No `goto cleanup`** — every early return is automatically safe
- Stack structs always initialized with `= {0}`

## Code style

- C11, `-Wall -Wextra -Werror -pedantic -D_GNU_SOURCE`
- GCC or Clang (MSVC not supported)
- 4-space indentation, Allman brace style
- File names: `lowercase_underscore.c`
- Functions: `<module>_<action>()` (e.g. `gree_client_bind()`)
- Structs: typedef'd, no `_t` suffix (e.g. `GreeClient`, `Config`)
- Macros/constants: `UPPER_CASE`
- Doxygen comment required for every public function in the header

## Return values

- `int` functions: `0` = success, `-1` = error
- Pointer functions: `NULL` = error

## Testing

Custom minimal test framework (`tests/common/test_helpers.h`):
```c
ASSERT(condition, "message")
RUN_TEST(test_func)
```

- No external test libraries
- Must pass ASAN + Valgrind
- Combined core + infrastructure coverage goal: >90%

## Claude Code tool usage

- Targeted changes: use Grep/Read/Edit directly
- Genuinely parallel, independent work: use Agent
- Never read a large file entirely when only a section is needed (use offset/limit)
