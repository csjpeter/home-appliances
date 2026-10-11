#!/usr/bin/env bash
# Verifies that claude/skills/home-appliances/SKILL.md has its front matter and
# that every "home-appliances <group> <sub>" command it shows is in the man page.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SKILL="$ROOT/claude/skills/home-appliances/SKILL.md"
MAN="$ROOT/docs/man/home-appliances.1"
fail=0

head -1 "$SKILL" | grep -qx -- '---' || { echo "FAIL: no front matter"; fail=1; }
grep -qx 'name: home-appliances' "$SKILL" || { echo "FAIL: name field missing"; fail=1; }
grep -q '^description: Use when .*Do not use' "$SKILL" \
    || { echo "FAIL: description must say when to use and when not"; fail=1; }

while read -r _ group sub _; do
    [ -n "$sub" ] || continue
    case "$group" in
        ac|vacuum|tv|printer) ;;
        *) continue ;;
    esac
    IFS='|' read -ra alts <<< "$sub"
    for alt in "${alts[@]}"; do
        case "$alt" in \<*|KEY*) continue ;; esac
        if ! grep -Eq "(^|[ \"])$group $alt([ \"]|\$)" "$MAN"; then
            echo "FAIL: '$group $alt' is in the skill but not in the man page"
            fail=1
        fi
    done
done < <(grep '^home-appliances ' "$SKILL")

for g in list discover; do
    grep -q "^home-appliances $g " "$SKILL" || { echo "FAIL: $g missing"; fail=1; }
done

[ "$fail" -eq 0 ] && echo "skill check OK"
exit "$fail"
