#!/usr/bin/env bash
# SPDX-License-Identifier: LicenseRef-Proprietary
# Copyright (c) 2026 Peter Csaszar (Császár Péter)
# Proprietary; the owner is the author.
# Created: 2026-10-11
# Fails on a source file or script without the source-file header.
#
# Usage: check_headers.sh [ROOT]   (default: the repository root)
set -euo pipefail

ROOT="${1:-$(cd "$(dirname "$0")/../.." && pwd)}"
today="$(date +%F)"
fail=0

# Licence the project declares in LICENSE, if there is one.
want=""
if [ -f "$ROOT/LICENSE" ]; then
    if grep -qi 'proprietary' "$ROOT/LICENSE"; then
        want="LicenseRef-Proprietary"
    elif grep -qi 'GNU GENERAL PUBLIC LICENSE' "$ROOT/LICENSE"; then
        want="GPL-3.0-only"
    fi
fi

while IFS= read -r f; do
    rel="${f#"$ROOT"/}"
    head="$(head -n 13 "$f")"
    tag="$(printf '%s\n' "$head" | sed -n 's/.*SPDX-License-Identifier: *\([A-Za-z0-9.-]*\).*/\1/p' | head -n 1)"
    case "$tag" in
        GPL-3.0-only|LicenseRef-Proprietary) ;;
        *) echo "FAIL: $rel: no valid SPDX-License-Identifier line"; fail=1; continue ;;
    esac
    if [ -n "$want" ] && [ "$tag" != "$want" ]; then
        echo "FAIL: $rel: tag $tag disagrees with LICENSE ($want)"; fail=1
    fi
    if ! printf '%s\n' "$head" | grep -Eq 'Copyright \(c\) [0-9]{4} Peter Csaszar'; then
        echo "FAIL: $rel: no copyright line naming the holder"; fail=1
    fi
    if [ "$tag" = "LicenseRef-Proprietary" ] \
        && ! printf '%s\n' "$head" | grep -q 'Proprietary; the owner is the author\.'; then
        echo "FAIL: $rel: proprietary statement missing"; fail=1
    fi
    created="$(printf '%s\n' "$head" | sed -n 's/.*Created: *\([0-9]\{4\}-[0-9]\{2\}-[0-9]\{2\}\).*/\1/p' | head -n 1)"
    if [ -z "$created" ] || ! date -d "$created" +%F >/dev/null 2>&1 \
        || [ "$(date -d "$created" +%F)" != "$created" ]; then
        echo "FAIL: $rel: no valid Created: date"; fail=1
    elif [[ "$created" > "$today" ]]; then
        echo "FAIL: $rel: Created: date is in the future"; fail=1
    fi
done < <(find "$ROOT" \( -name .git -o -name 'build-*' -o -name bin -o -name vendor -o -name libs \) -prune -o \
    -type f \( -name '*.c' -o -name '*.h' -o -name '*.cpp' -o -name '*.hpp' -o -name '*.py' \
    -o -name '*.sh' -o -name '*.js' -o -name '*.cmake' -o -name CMakeLists.txt \
    -o -name '*.desktop' -o -name '*.yml' \) -print | sort)

[ "$fail" -eq 0 ] && echo "header check OK"
exit "$fail"
