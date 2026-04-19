/**
 * @file subnet_scan.c
 * @brief Parallel per-host subnet probe utility using pthreads.
 */

#include "subnet_scan.h"
#include "logger.h"

#include <arpa/inet.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Internal types ───────────────────────────────────────────────────────── */

typedef struct
{
    char            ip[16];
    subnet_probe_fn fn;
    void           *ctx;
    int             result;
} ProbeArgs;

/* ── Thread function ──────────────────────────────────────────────────────── */

static void *run_probe(void *arg)
{
    ProbeArgs *a = arg;
    a->result = a->fn(a->ip, a->ctx);
    return NULL;
}

/* ── Public API ───────────────────────────────────────────────────────────── */

int subnet_scan(const char *cidr, subnet_probe_fn probe_fn, void *ctx)
{
    if (!cidr || !probe_fn)
    {
        LOG_ERROR_MSG("subnet_scan: NULL argument");
        return -1;
    }

    char addr_str[16];
    int  prefix = 0;

    if (sscanf(cidr, "%15[^/]/%d", addr_str, &prefix) != 2)
    {
        LOG_ERROR_MSG("subnet_scan: cannot parse CIDR '%s'", cidr);
        return -1;
    }

    if (prefix < 24 || prefix > 30)
    {
        LOG_ERROR_MSG("subnet_scan: prefix /%d not in supported range /24..30", prefix);
        return -1;
    }

    struct in_addr base_addr;
    if (inet_pton(AF_INET, addr_str, &base_addr) != 1)
    {
        LOG_ERROR_MSG("subnet_scan: invalid address '%s'", addr_str);
        return -1;
    }

    uint32_t host_bits = (uint32_t)(32 - prefix);
    uint32_t host_mask = (uint32_t)((1u << host_bits) - 1u);
    uint32_t net_base  = ntohl(base_addr.s_addr) & ~host_mask;

    /* usable hosts: net_base+1 .. net_base+host_mask-1 */
    int host_count = (int)(host_mask - 1u);
    if (host_count <= 0)
    {
        LOG_WARN_MSG("subnet_scan: no usable hosts for /%d", prefix);
        return 0;
    }

    ProbeArgs  *args    = calloc((size_t)host_count, sizeof(*args));
    pthread_t  *threads = calloc((size_t)host_count, sizeof(*threads));
    int        *created = calloc((size_t)host_count, sizeof(*created));

    if (!args || !threads || !created)
    {
        LOG_ERROR_MSG("subnet_scan: allocation failed");
        free(args);
        free(threads);
        free(created);
        return -1;
    }

    for (int i = 0; i < host_count; i++)
    {
        uint32_t      host_ip = net_base + (uint32_t)(i + 1);
        struct in_addr ia;
        ia.s_addr = htonl(host_ip);
        inet_ntop(AF_INET, &ia, args[i].ip, sizeof(args[i].ip));
        args[i].fn  = probe_fn;
        args[i].ctx = ctx;

        if (pthread_create(&threads[i], NULL, run_probe, &args[i]) == 0)
            created[i] = 1;
        else
            LOG_WARN_MSG("subnet_scan: pthread_create failed for %s", args[i].ip);
    }

    int sum = 0;
    for (int i = 0; i < host_count; i++)
    {
        if (created[i])
        {
            pthread_join(threads[i], NULL);
            sum += args[i].result;
        }
    }

    free(args);
    free(threads);
    free(created);

    return sum;
}
