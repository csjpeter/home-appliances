# Issue 031 — PTY + mock server combined end-to-end CLI tests

**Type**: test  
**Priority**: medium  
**Component**: `tests/pty/`  
**Depends on**: Issue 024 (libpty done), Issues 020-023 (mock servers done)

## Summary

The current PTY tests (024) only verify CLI help/usage output — they do not exercise
actual device commands through the binary. This issue adds true end-to-end tests that:

1. Start a mock UDP/TCP server in a background pthread (reusing the mock infrastructure
   from the functional test suites).
2. Launch the `home-appliances` binary in a PTY with the mock server IP and port
   (passed via `-DGREE_PORT=` etc., or by overriding the port via env variable or
   compile-time define in a wrapper binary).
3. Send CLI commands and verify the expected output appears on the virtual screen.

## Proposed tests

### AC (Gree)
- `ac list` → mock scan server → screen contains device MAC/name
- `ac status <ip>` → mock status server → screen contains "Power:", "Temp:"
- `ac on <ip>` → mock set server → screen contains "Set."

### Vacuum (Roborock)
- `vacuum status <ip>` → mock → screen contains "Battery:", "State:"
- `vacuum start <ip>` → mock → screen contains "Cleaning started."

### TV (Samsung)
- `tv key <ip> KEY_POWER` → mock → screen contains no error (or "OK")

### Printer (Brother)
- `printer status <ip>` → mock → screen contains "Toner:" and a percentage

## Implementation notes

The main challenge is that the CLI binary links against the production library which
uses compile-time port constants. Options:

**Option A**: Build a separate test-helper binary (`bin/home-appliances-test`) with
`-DGREE_PORT=17000 -DROBOROCK_PORT=15432` etc., and test that binary from PTY.

**Option B**: Accept environment variable overrides for port numbers in the production
code (adds a small runtime cost for a feature only used in tests).

**Option C**: Use network namespaces or iptables redirect to intercept traffic.

Option A is the cleanest: add a `test-cli-binary` CMake target that compiles `src/main.c`
with all client `.c` files directly (like the functional tests do) and overrides the ports.

## Acceptance criteria

- All PTY+mock combined tests pass under ASAN.
- Mock server and PTY session are torn down cleanly after each test.
- Test binary is built separately and not installed.
