/* SPDX-License-Identifier: LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Proprietary; the owner is the author. */
/* Created: 2026-04-19 */
/* RAII cleanup-attribute helpers for strings and files. */

#ifndef RAII_H
#define RAII_H

#include <stdio.h>
#include <stdlib.h>

static inline void raii_free(void *p)   { free(*(void **)p); }
static inline void raii_fclose(void *p) { if (*(FILE **)p) fclose(*(FILE **)p); }

#define RAII_STRING __attribute__((cleanup(raii_free)))
#define RAII_FILE   __attribute__((cleanup(raii_fclose)))
#define RAII_WITH_CLEANUP(fn) __attribute__((cleanup(fn)))

#endif /* RAII_H */
