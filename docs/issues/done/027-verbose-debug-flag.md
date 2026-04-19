# Issue 027 — CLI: --verbose / --debug flag for log level control

**Type**: enhancement  
**Priority**: low  
**Component**: `src/main.c`  
**Covers**: general usability

## Summary

The log level is hardcoded to `LOG_INFO`. Add `--verbose` / `-v` and `--debug` / `-d`
flags to allow users to see DEBUG messages and troubleshoot connectivity issues.

## Changes

### `src/main.c`

Parse flags before the command dispatcher:
```c
int log_level = LOG_INFO;
/* scan argv for --verbose / -v / --debug / -d */
for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0)
        log_level = LOG_DEBUG;
    else if (strcmp(argv[i], "--debug") == 0 || strcmp(argv[i], "-d") == 0)
        log_level = LOG_DEBUG;
}
logger_init(NULL, log_level);
```

Remove matched flags from argv before command parsing, or skip them in the dispatcher.

### `print_usage()`

Add:
```
Options:
  -v, --verbose    Enable verbose/debug logging
  --version        Print version
  --help           Show this help
```

## Acceptance criteria

- `home-appliances --verbose ac status <ip>` shows DEBUG log lines.
- `home-appliances -v` alone shows usage (not a crash).
- Existing behavior unchanged when flag is absent.
