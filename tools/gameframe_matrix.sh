#!/bin/zsh
#
# The gameframe permutation gate: OSRS239 toplevel x frame x window size.
#
#   tools/gameframe_matrix.sh [outdir]
#
# Forty runs, four at a time, about four minutes. Each run asserts the cheap
# decisive facts: the expected root opened, the chatbox bar exists, and the
# lane's own filter count is visible -- eight on the three desktop toplevels,
# SEVEN on the mobile one, because 601 hides Report
# (torirs_chatbox_layout.cs2 skips the Report block when ~on_mobile).
#
# Every run writes its OWN complete preferences.ini from nothing rather than
# copying the repo's: that file is Matthew's live device settings and has
# carried `[device_options] 27=150` (150% interface scale), under which every
# frame clamps to its own minimum and the captures look exactly like a
# regression in whatever change is under test. Two hours went to that once.
#
# Needs the OPT=0 embed binary:
#   make -C src OPT=0 EMBED_SERVER=1 PLATFORM_OBJ_BASE=build_gfmatrix \
#        PLATFORM_TARGET=torirs_gfmatrix torirs_gfmatrix -j8
set -u
TOOLS_DIR=$(cd "$(dirname "$0")" && pwd)
REPO=${REPO:-$(cd "$(dirname "$0")/.." && pwd)}
BIN=${BIN:-$REPO/src/torirs_gfmatrix}
MANIFEST=${MANIFEST:-$REPO/manifests/manifest_osrs239_curses.ini}
OUT=${1:-${TMPDIR:-/tmp}/gfmatrix.$$}
FRAMES_LIST=(auto gameframe-layout/classic-fixed gameframe-layout/modern-fixed \
             gameframe-layout/modern-resizable mobile-gameframe/stone-drawer)
SIZES=(765x503 1200x800)

[ -x "$BIN" ] || { echo "no binary at $BIN -- see the header"; exit 2; }
mkdir -p "$OUT"
: > "$OUT/index.txt"
cat > "$OUT/plugin_prefs.ini" <<EOF
[plugin:gameframe-layout]
enabled=1
[plugin:mobile-gameframe]
enabled=1
art=Classic
EOF

one() {
  local tag=$1 mode=$2 size=$3 frame=$4 mobile=$5
  local run=$OUT/$tag
  rm -rf "$run"; mkdir -p "$run/saves"
  printf '[preferences]\nversion=1\npreferred_frame=%s\nframe_migration_version=1\n' \
    "$frame" > "$run/preferences.ini"
  sed -e "s/^client_layout_mode = .*/client_layout_mode = $mode/" \
    "$REPO/saves/testc.ini" > "$run/saves/testc.ini"
  echo "PINNED tag=$tag mode=$mode size=$size frame=$frame scale=none" > "$run/log.txt"
  local -a env_extra
  env_extra=()
  [ "$mobile" = "1" ] && env_extra=(TORIRS_CLIENTTYPE=7)
  ( cd "$REPO" && env TORIRS_PREFS="$run/preferences.ini" \
      TORIRSSERVER_SAVES="$run/saves" \
      TORIRS_PLUGIN_PREFS="$OUT/plugin_prefs.ini" TORIRS_PLUGINS=1 \
      TORIRSSERVER_ALLOW_STALE_SCRIPTS=1 TORIRS_STDERR_UNBUFFERED=1 \
      SDL_VIDEODRIVER=dummy TORIRS_MAX_FRAMES=620 \
      TORIRS_EXIT_BMP="$run/out.bmp" TORIRS_DUMP_BOUNDS=162 "${env_extra[@]}" \
      "$BIN" --manifest "$MANIFEST" --windowmode resizable --window "$size" \
      >> "$run/log.txt" 2>&1 )
}

i=0
for m in 0 1 2 M; do for f in $FRAMES_LIST; do for s in $SIZES; do
  i=$((i+1)); tag="m$(printf '%02d' $i)"
  if [[ -n "${GF_MATRIX_TAGS:-}" && ",${GF_MATRIX_TAGS}," != *",${tag},"* ]]; then continue; fi
  mode=$m; mobile=0
  [ "$m" = "M" ] && { mode=1; mobile=1; }
  echo "$tag|$m|$f|$s" >> "$OUT/index.txt"
  one "$tag" "$mode" "$s" "$f" "$mobile" &
  [ $((i % 4)) -eq 0 ] && wait
done; done; done
wait

fail=0
printf "%-5s %-4s %-38s %-9s %-5s %-8s %s\n" TAG TOP FRAME SIZE ROOT FILTERS VERDICT
while IFS='|' read tag m f s; do
  L=$OUT/$tag/log.txt
  rt=$(grep -o 'switching root [-0-9]* -> [0-9]*' "$L" 2>/dev/null | tail -1 | grep -o '[0-9]*$')
  n=$(grep '^BOUNDS' "$L" 2>/dev/null | awk '{g=$3;gsub(/[()]/,"",g);split(g,p,"|");x=p[2]+0;
        if((x==5||x==8||x==12||x==16||x==20||x==24||x==28||x==32) && $0!~/hidden=1/) print}' | wc -l | tr -d ' ')
  bar=$(grep '^BOUNDS' "$L" 2>/dev/null | grep -c '(162|3)')
  want=8; [ "$rt" = "601" ] && want=7
  v=ok
  [ -z "$rt" ] && { v="NO ROOT"; fail=$((fail+1)); }
  [ -n "$rt" ] && [ "$n" != "$want" ] && { v="FILTERS $n want $want"; fail=$((fail+1)); }
  [ -n "$rt" ] && [ "$bar" = "0" ] && { v="NO CHAT BAR"; fail=$((fail+1)); }
  if [ -n "$rt" ]; then
    python3 "$TOOLS_DIR/gameframe_pixels.py" "$OUT/$tag/out.bmp" --frame "$f" --root "$rt" > "$OUT/$tag/pixels.txt" 2>&1 || { v="PIXELS"; fail=$((fail+1)); }
    cat "$OUT/$tag/pixels.txt"
  fi
  printf "%-5s %-4s %-38s %-9s %-5s %-8s %s\n" "$tag" "$m" "$f" "$s" "${rt:--}" "$n" "$v"
done < "$OUT/index.txt"
echo "--- $fail failures / $(wc -l < "$OUT/index.txt" | tr -d ' ') --- captures in $OUT"
exit $(( fail > 0 ))
