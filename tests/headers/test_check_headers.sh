#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-Proprietary
# Copyright (c) 2026 Peter Csaszar (Császár Péter)
# Proprietary; the owner is the author.
# Created: 2026-10-11
# Tests check_headers.sh: it must pass a headed tree and fail on each defect.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
CHECK="$HERE/check_headers.sh"
T="/tmp/ha-hdrtest-$$"
fail=0

good() {
    cat <<'H'
/* SPDX-License-Identifier: LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Proprietary; the owner is the author. */
/* Created: 2026-04-19 */
/* A file. */
int x;
H
}

expect() { # name want_rc
    local rc=0
    "$CHECK" "$T" >/dev/null || rc=$?
    if [ "$rc" -ne "$2" ]; then echo "FAIL: $1: exit $rc, wanted $2"; fail=1; fi
}

mkdir -p "$T"
good > "$T/a.c"
expect "headed file passes" 0

printf 'int x;\n' > "$T/a.c";                                   expect "no header" 1
good | sed 's/LicenseRef-Proprietary \*/MIT */' > "$T/a.c";     expect "foreign licence tag" 1
good | sed '2d' > "$T/a.c";                                     expect "no copyright line" 1
good | sed '3d' > "$T/a.c";                                     expect "no proprietary statement" 1
good | sed 's/2026-04-19/2026-13-45/' > "$T/a.c";               expect "invalid date" 1
good | sed 's/2026-04-19/2999-01-01/' > "$T/a.c";               expect "future date" 1
good | sed '4d' > "$T/a.c";                                     expect "no Created line" 1

good > "$T/a.c"
printf 'GNU GENERAL PUBLIC LICENSE\n' > "$T/LICENSE"
expect "tag disagrees with LICENSE" 1
printf 'Proprietary. The owner is the author.\n' > "$T/LICENSE"
expect "tag agrees with LICENSE" 0

{ printf '#!/bin/sh\n'; good | sed 's#^/\* \(.*\) \*/$#\# \1#'; } > "$T/a.sh"
expect "shebang then header passes" 0

case "$T" in /tmp/ha-hdrtest-*) rm -rf "$T" ;; *) echo "unsafe path"; exit 1 ;; esac
[ "$fail" -eq 0 ] && echo "check_headers tests OK"
exit "$fail"
