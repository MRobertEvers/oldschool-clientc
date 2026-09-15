#!/usr/bin/env bash
#
# Annotate a MEMPROF report with source locations.
#
# The report prints LINK-time addresses -- memprof subtracts the load bias
# itself -- so addr2line reads them straight off the executable with no ASLR
# arithmetic in between. That is the whole reason the report bothers to print a
# bias anchor line: if these names come out wrong, compare the anchor against
# `nm torirs_win64.exe | grep memprof_dump` before suspecting the symbols.
#
# The lane must be built with MEMPROF=1, which carries -g. A report resolved
# against a DIFFERENT binary than the one that produced it will still print
# plausible names -- addr2line has no way to know -- so pass the exact exe.
#
#   tools/mem/resolve_sites.sh report.peak src/torirs_win64.exe
#
set -u

report="${1:-}"
exe="${2:-}"

if [ -z "$report" ] || [ -z "$exe" ]; then
    echo "usage: $0 <memprof-report> <exe>" >&2
    exit 2
fi
if [ ! -f "$report" ]; then
    echo "no such report: $report" >&2
    exit 2
fi
if [ ! -f "$exe" ]; then
    echo "no such executable: $exe" >&2
    exit 2
fi

addr2line=$(command -v addr2line || true)
if [ -z "$addr2line" ]; then
    echo "addr2line not on PATH (add the mingw toolchain bin)" >&2
    exit 2
fi

# Header lines carry the totals and the collision counters, which is the
# context that says whether the ranking below can be trusted at all.
grep -E "^memprof: (live|site-slot|top|===)" "$report"
echo

while IFS= read -r line; do
    case "$line" in
        *"memprof: #"*) ;;
        *) continue ;;
    esac

    addr=$(echo "$line" | grep -oE '0x[0-9a-f]+$')
    [ -z "$addr" ] && continue

    # -i follows inlining, which LTO makes the common case: without it every
    # site in a unity TU resolves to the same outer function.
    where=$("$addr2line" -f -C -i -e "$exe" "$addr" 2>/dev/null \
        | paste - - \
        | sed 's#[^ ]*[/\\]src[/\\]#src/#' \
        | paste -sd' | ' -)

    stats=$(echo "$line" | sed 's/^memprof: //; s/  *0x[0-9a-f]*$//')
    printf '%s\n    %s\n' "$stats" "${where:-<unresolved>}"
done < "$report"
