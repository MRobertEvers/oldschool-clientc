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
OUT=${1:-${GF_MATRIX_OUT:-${TMPDIR:-/tmp}/gfmatrix.$$}}
FRAMES_LIST=(auto gameframe-layout/classic-fixed gameframe-layout/modern-fixed \
             gameframe-layout/modern-resizable mobile-gameframe/stone-drawer)
SIZES=(765x503 1200x800)

[ -x "$BIN" ] || { echo "no binary at $BIN -- see the header"; exit 2; }
mkdir -p "$OUT"
# The same capture/scoring path exercises native transitions. Keep this in the
# reference harness so a new frame provider can run the contract with one target.
if [[ "${GF_MATRIX_SCENARIOS:-0}" == 1 ]]; then
  failures=0
  scenario() {
    local name=$1
    shift
    env GF_MATRIX_SCENARIOS=0 "$@" "$TOOLS_DIR/gameframe_matrix.sh" "$OUT/$name" > "$OUT/$name.log" 2>&1
    local result=$?
    cat "$OUT/$name.log"
    [[ $result == 0 ]] || failures=$((failures+1))
  }
  for state in 0 1 2 3 4 5; do
    scenario "minimap-$state" GF_MATRIX_TAGS=m03 GF_MATRIX_MINIMAP_STATE=$state \
      TORIRS_NET_DEBUG=1 TORIRS_SIM_CMD="500,minimap $state" TORIRS_SIM_CLICK_AT='550,630,80'
  done
  for hidden in 0 1; do
    scenario "server-hide-$hidden" GF_MATRIX_TAGS=m03 GF_MATRIX_SERVER_HIDE="35913750:$hidden" \
      TORIRS_NET_DEBUG=1 TORIRS_SIM_CMD="490,ifhide 35913750 1;550,ifhide 35913750 $hidden"
  done
  scenario resize-tabs GF_MATRIX_TAGS=m03,m13,m23 GF_MATRIX_EXPECT_IFACE=320 \
    TORIRS_SIM_RESIZE='500,1200x800' TORIRS_SIM_CLICK_AT='540,574,182'
  scenario remount GF_MATRIX_TAGS=m03,m13 GF_MATRIX_EXPECT_ROOT=164 TORIRS_SIM_CMD='500,layout 2'
  echo "NATIVE CONTRACT: $failures failed scenario groups / 10 (13 captures)"
  exit $((failures > 0))
fi
if [[ "${GF_MATRIX_SCORE_ONLY:-0}" != 1 ]]; then
: > "$OUT/index.txt"
cat > "$OUT/plugin_prefs.ini" <<EOF
[plugin:gameframe-layout]
enabled=1
[plugin:mobile-gameframe]
enabled=1
art=Classic
[plugin:minimap-orbs]
enabled=1
show_hp=1
show_prayer=1
show_run=1
show_spec=1
EOF

one() {
  local tag=$1 mode=$2 size=$3 frame=$4 mobile=$5
  local run=$OUT/$tag
  rm -rf "$run"; mkdir -p "$run/saves"
  printf '[preferences]\nversion=1\npreferred_frame=%s\nframe_migration_version=1\n' \
    "$frame" > "$run/preferences.ini"
  sed -e "s/^client_layout_mode = .*/client_layout_mode = $mode/" \
    -e "s/^x = .*/x = 3210/" -e "s/^z = .*/z = 3424/" -e "s/^level = .*/level = 0/" \
    "$REPO/saves/testc.ini" > "$run/saves/testc.ini"
  echo "PINNED tag=$tag mode=$mode size=$size frame=$frame scale=none" > "$run/log.txt"
  local -a env_extra
  env_extra=()
  [ "$mobile" = "1" ] && env_extra=(TORIRS_CLIENTTYPE=7)
  ( cd "$REPO" && env TORIRS_PREFS="$run/preferences.ini" \
      TORIRSSERVER_SAVES="$run/saves" \
      TORIRS_PLUGIN_PREFS="$OUT/plugin_prefs.ini" TORIRS_PLUGINS=1 \
      TORIRSSERVER_ALLOW_STALE_SCRIPTS=1 TORIRS_STDERR_UNBUFFERED=1 \
      SDL_VIDEODRIVER=dummy TORIRS_MAX_FRAMES=620 TORIRS_FRAME_ROLE_AUDIT=1 \
      TORIRS_EXIT_BMP="$run/out.bmp" TORIRS_DUMP_BOUNDS=all TORIRS_DUMP_EMIT_EXIT=all "${env_extra[@]}" \
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
fi

fail=0
checks=0
printf "%-5s %-4s %-38s %-9s %-5s %-8s %s\n" TAG TOP FRAME SIZE ROOT FILTERS VERDICT
while IFS='|' read tag m f s; do
  L=$OUT/$tag/log.txt
  rt=$(grep -o 'switching root [-0-9]* -> [0-9]*' "$L" 2>/dev/null | tail -1 | grep -o '[0-9]*$')
  n=$(grep '^BOUNDS' "$L" 2>/dev/null | awk '{g=$3;gsub(/[()]/,"",g);split(g,p,"|");x=p[2]+0;
        if(p[1]==162 && (x==5||x==8||x==12||x==16||x==20||x==24||x==28||x==32) && $0!~/hidden=1/) print}' | wc -l | tr -d ' ')
  bar=$(grep '^BOUNDS' "$L" 2>/dev/null | grep -c '(162|3)')
  want=8; [ "$rt" = "601" ] && want=7
  v=ok
  before=$checks
  case "$m" in 0) expected_root=548;; 1) expected_root=161;; 2) expected_root=164;; M) expected_root=601;; esac
  expected_root=${GF_MATRIX_EXPECT_ROOT:-$expected_root}
  active=$(awk '/^BOUNDS/{exit} /^frame_selection:/{for(i=1;i<=NF;i++) if($i~/^active=/){value=$i;sub(/^active=/,"",value)}} END{print value}' "$L")
  active=${active:-$f}
  [ "$rt" != "$expected_root" ] && { v="WRONG ROOT"; checks=$((checks+1)); }
  if [[ "${GF_MATRIX_EXPECT_NATIVE:-0}" == 1 ]]; then
    [[ "$active" == core/native ]] && grep -q 'active=core/native status=3 reason=.' "$L" || { v="FALLBACK"; checks=$((checks+1)); }
  elif [[ "$f" == gameframe-layout/classic-fixed && "$rt" == 601 ]]; then
    [[ "$active" == core/native ]] && grep -q 'Classic Fixed is a desktop frame' "$L" || { v="FALLBACK"; checks=$((checks+1)); }
  elif [[ "$f" != auto && "$active" != "$f" ]]; then
    v="FRAME NOT ACTIVE"; checks=$((checks+1))
  fi
  [ -z "$rt" ] && { v="NO ROOT"; checks=$((checks+1)); }
  [ -n "$rt" ] && [ "$n" != "$want" ] && { v="FILTERS $n want $want"; checks=$((checks+1)); }
  if [[ -n "${GF_MATRIX_EXPECT_IFACE:-}" ]]; then
    grep -q "EMIT_EXIT.*($GF_MATRIX_EXPECT_IFACE|" "$L" || { v="NO SELECTED TAB PAINT"; checks=$((checks+1)); }
  fi
  [ -n "$rt" ] && [ "$bar" = "0" ] && { v="NO CHAT BAR"; checks=$((checks+1)); }
  if [ -n "$rt" ]; then
    if [[ "${GF_MATRIX_EXPECT_NATIVE:-0}" != 1 ]] && { ! grep -q "frameroles: root $rt, .*roles checked, .* absent, 0 unbound, 0 mismatched" "$L" || grep -Eq 'frameroles: .* (MISMATCH|UNBOUND)' "$L"; }; then
      v="ROLE AUDIT"; checks=$((checks+1))
    fi
    local_state_args=()
    [[ -n "${GF_MATRIX_MINIMAP_STATE:-}" ]] && local_state_args=(--minimap-state "$GF_MATRIX_MINIMAP_STATE")
    [[ -n "${GF_MATRIX_SERVER_HIDE:-}" ]] && local_state_args+=(--server-hide "$GF_MATRIX_SERVER_HIDE")
    python3 "$TOOLS_DIR/gameframe_pixels.py" "$OUT/$tag/out.bmp" --frame "$active" --root "$rt" --bounds "$L" "${local_state_args[@]}" > "$OUT/$tag/pixels.txt" 2>&1 || { v="PIXELS"; checks=$((checks+1)); }
    cat "$OUT/$tag/pixels.txt"
  fi
  [[ "$checks" != "$before" ]] && fail=$((fail+1))
  printf "%-5s %-4s %-38s %-9s %-5s %-8s %s\n" "$tag" "$m" "$f" "$s" "${rt:--}" "$n" "$v"
done < "$OUT/index.txt"
echo "--- $fail failures ($checks checks) / $(wc -l < "$OUT/index.txt" | tr -d ' ') --- captures in $OUT"
exit $(( fail > 0 ))
