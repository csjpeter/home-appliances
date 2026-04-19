# Issue 014 — Multi-device command support

**Type**: feature  
**Priority**: low  
**Component**: `src/main.c`  
**User story**: US-A10

## Summary

Allow commands to accept multiple IP addresses so the same operation can be
applied to several devices in one invocation (e.g., turn off all AC units).

## Scope

Initially for Gree AC; the pattern is reusable for future devices.

## CLI syntax

```
ac off 192.168.x.1 192.168.x.2 192.168.x.3
ac set 192.168.x.1 192.168.x.2 power=0 temp=24
```

IPs are positional arguments before any `key=value` parameters.

## Parsing rule

In `cmd_ac_*` functions: collect all arguments that match an IPv4 pattern
as the device list; remaining arguments are parameters.

Simple IPv4 heuristic: argument contains at least two dots and starts with a digit.

## Execution

Sequential (not parallel): send to each IP in order, print result per device.

```
192.168.x.1: off
192.168.x.2: off
192.168.x.3: error (unreachable)
```

## Exit code

- 0 if all devices succeeded.
- 1 if any device failed.
- Per-device errors printed to stderr; summary to stdout.

## Acceptance criteria

- `ac off <ip1> <ip2>` works.
- `ac status <ip1> <ip2>` prints a labelled block per device.
- Single-IP invocation unchanged (no regression).
- At least one unit test for IP list parsing.

## Out of scope

- Parallel execution (POSIX threads or fork) — not needed for a home LAN.
- Wildcard or range notation (`192.168.x.*`) — add separately if needed.
