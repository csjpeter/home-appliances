# Issue 032 — Increase combined core+infrastructure test coverage to 90 %

**Type**: test  
**Priority**: low  
**Component**: `libappliances/src/core/`, `libappliances/src/infrastructure/`

## Summary

Current coverage is approximately 43 % overall. The project target is > 90 % for the
combined core + infrastructure layer. This issue identifies and fills the gaps.

## Coverage gaps (approximate, as of Issue 030 completion)

### Core
| File | Estimated coverage | Gap |
|------|--------------------|-----|
| `config.c` | ~60 % | Error paths: file parse errors, unknown keys |
| `logger.c` | ~40 % | Levels: WARN, DEBUG, file output |
| `base64.c` | ~90 % | Padding edge cases |

### Infrastructure
| File | Estimated coverage | Gap |
|------|--------------------|-----|
| `gree_client.c` | ~55 % | `gree_client_merge_bound`, multi-device scan, set with >1 param |
| `roborock_client.c` | ~50 % | `build_packet` error paths, `send_recv` failures |
| `brother_client.c` | ~45 % | BER encode/decode edge cases, OID parsing |
| `samsung_tv_client.c` | ~55 % | Auth retry loop, `samsung_tv_send_keys` multi-key |

## Approach

1. Run `./manage.sh coverage` and open `build-Coverage/lcov/index.html` to identify
   uncovered lines precisely.
2. Add unit tests for the top uncovered functions — prioritise pure functions
   (no network required) such as BER encoding helpers, base64 edge cases,
   JSON parsing helpers.
3. For network-dependent paths: extend the existing mock servers with error-response
   scenarios (short packet, malformed JSON, wrong `r` value).

## Acceptance criteria

- `./manage.sh coverage` reports ≥ 90 % line coverage for `libappliances/src/core/`
  and `libappliances/src/infrastructure/` combined.
- No new Valgrind errors introduced.
