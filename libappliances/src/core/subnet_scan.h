#ifndef SUBNET_SCAN_H
#define SUBNET_SCAN_H

/**
 * @file subnet_scan.h
 * @brief Parallel per-host subnet probe utility using pthreads.
 *
 * Spawns one pthread per host address. All threads run concurrently.
 * Supported prefix lengths: 24 through 30 (2 to 254 hosts).
 */

/** Probe callback. Returns 1 if host responded, 0 if not. Must be thread-safe. */
typedef int (*subnet_probe_fn)(const char *ip, void *ctx);

/**
 * @brief Probe all usable host addresses in a CIDR subnet in parallel.
 * @param cidr     CIDR notation, e.g. "192.168.1.0/24". Only /24..30 supported.
 * @param probe_fn Thread-safe callback invoked per host IP.
 * @param ctx      Opaque pointer passed through to probe_fn.
 * @return Number of hosts for which probe_fn returned 1, or -1 on error.
 */
int subnet_scan(const char *cidr, subnet_probe_fn probe_fn, void *ctx);

#endif /* SUBNET_SCAN_H */
