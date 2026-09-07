#!/bin/zsh
#
# The gameframe permutation and native contract gate for OSRS239 / rs289lc.
# GF_MATRIX_BASELINE=1 disables plugins. GF_MATRIX_REVISION=rs289lc selects
# revconfig/CS1 with a unique account per connection; GF_MATRIX_LC_SAVE pins
# gameplay state, and GF_MATRIX_LC_SERVER names the matching server checkout.
# GF_MATRIX_SCENARIOS=1 selects that revision's native interaction scenarios.
# Freshness failures stop captures. GF_MATRIX_DIAGNOSTIC=1 may capture them
# for investigation, but always returns a nonzero, non-acceptance result.
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
# Uses an embedded-server binary; OPT=0 and OPT=1 both retain opt-in traces:
#   make -C src OPT=0 EMBED_SERVER=1 PLATFORM_OBJ_BASE=build_gfmatrix \
#        PLATFORM_TARGET=torirs_gfmatrix torirs_gfmatrix -j8
set -u
TOOLS_DIR=$(cd "$(dirname "$0")" && pwd)
REPO=${REPO:-$(cd "$(dirname "$0")/.." && pwd)}
BIN=${BIN:-$REPO/src/torirs_gfmatrix}
REVISION=${GF_MATRIX_REVISION:-osrs239}
BASELINE=${GF_MATRIX_BASELINE:-0}
# A C-only probe must not inherit the default shipped Lua manifest. Otherwise
# unrelated overlays make its supposedly isolated reference capture misleading.
if [[ "${GF_MATRIX_WIDGET_DEMO:-0}" == 1 ]]; then
  export TORIRS_PLUGIN_MANIFEST=''
elif [[ "${GF_MATRIX_WIDGET_DEMO:-0}" == lua && -z "${TORIRS_PLUGIN_MANIFEST:-}" ]]; then
  echo 'Lua widget probe requires its explicit TORIRS_PLUGIN_MANIFEST' >&2
  exit 2
fi
if [[ "$REVISION" == rs289lc ]]; then
  MANIFEST=${MANIFEST:-$REPO/manifests/manifest_rs289lc.ini}
else
  MANIFEST=${MANIFEST:-$REPO/manifests/manifest_osrs239_curses.ini}
fi
OUT=${1:-${GF_MATRIX_OUT:-${TMPDIR:-/tmp}/gfmatrix.$$}}
FRAMES_LIST=(auto gameframe-layout/classic-fixed gameframe-layout/modern-fixed \
             gameframe-layout/modern-resizable mobile-gameframe/stone-drawer)
SIZES=(765x503 1200x800)

[ -x "$BIN" ] || { echo "no binary at $BIN -- see the header"; exit 2; }
if [[ "${GF_MATRIX_SCORE_ONLY:-0}" != 1 && -e "$OUT" ]]; then
  echo "output already exists: $OUT (use a new directory or GF_MATRIX_SCORE_ONLY=1)" >&2
  exit 2
fi
mkdir -p "$OUT"
if [[ -n "${GF_MATRIX_PLUGIN:-}" ]]; then
  if [[ "$GF_MATRIX_PLUGIN" == lua:* ]]; then
    export TORIRS_PLUGIN_ONLY=lua
    export TORIRS_SCRIPT_DIR="$OUT/scripts"
    export TORIRS_PLUGIN_MANIFEST=capture.ini
    if [[ "${GF_MATRIX_SCORE_ONLY:-0}" != 1 ]]; then
      python3 - "${GF_MATRIX_SCRIPT_SOURCE:-$TOOLS_DIR/../script}" "$TORIRS_SCRIPT_DIR" "${GF_MATRIX_PLUGIN#lua:}" <<'PY_PLUGIN'
from pathlib import Path
import configparser, re, shutil, sys
source,dest,plugin=Path(sys.argv[1]),Path(sys.argv[2]),sys.argv[3]
shutil.copytree(source,dest)
files=[]
for p in (source/'plugins').glob('*.lua'):
    if re.search(r"\bid\s*=\s*['\"]"+re.escape(plugin)+r"['\"]",p.read_text()): files.append(p)
if len(files)!=1: raise SystemExit('selected Lua plugin must resolve to one source')
config=configparser.ConfigParser();config['plugin:'+plugin]={'source':'plugins/'+files[0].name,'enabled':'1'}
with (dest/'capture.ini').open('w') as out: config.write(out)
PY_PLUGIN
      [[ $? == 0 ]] || exit 2
    fi
  else
    export TORIRS_PLUGIN_ONLY="$GF_MATRIX_PLUGIN"
    export TORIRS_PLUGIN_MANIFEST=''
  fi
fi
if [[ "${GF_MATRIX_SCORE_ONLY:-0}" != 1 ]]; then
  python3 "$TOOLS_DIR/gameframe_fixture.py" --repo "$REPO" --binary "$BIN" \
    --manifest "$MANIFEST" --revision "$REVISION" --out "$OUT/fixture.json"
  fixture_result=$?
  if [[ $fixture_result != 0 && "${GF_MATRIX_DIAGNOSTIC:-0}" != 1 ]]; then exit 2; fi
fi
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
  if [[ "$REVISION" == rs289lc ]]; then
    [[ -f "${GF_MATRIX_LC_SAVE:-}" ]] || { echo 'rs289 scenarios require GF_MATRIX_LC_SAVE: an isolated gameplay seed save'; exit 2; }
    scenario stats GF_MATRIX_RS289_SCENARIO=stats GF_MATRIX_MAX_FRAMES=900 \
      TORIRS_SIM_CLICK_AT='500,585,184' TORIRS_SIM_CMD='480,setstat strength 1;650,setstat strength 20'
    scenario skill-guide GF_MATRIX_RS289_SCENARIO=skill-guide GF_MATRIX_MAX_FRAMES=900 \
      TORIRS_SIM_CLICK_AT='500,585,184;650,584,250'
    # The sidebar's find_all numbering on the classic-fixed frame: 14 members
    # with tab 7 a hole, reported by the bridge's last PLUGIN_FIND_ALL line.
    scenario find-all-holes GF_MATRIX_RS289_FRAME=gameframe-layout/classic-fixed \
      GF_MATRIX_FIND_ALL_HOLES=sidebar:14:7 GF_MATRIX_MAX_FRAMES=700
    echo "RS289 NATIVE CONTRACT: $failures failed scenario groups / 3"
    exit $((failures > 0))
  fi
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
  scenario focus-native-hide GF_MATRIX_TAGS=m01 GF_MATRIX_BASELINE=1 GF_MATRIX_EXPECT_IFACE=894 \
    GF_MATRIX_INPUT_STATE=58589197:abc GF_MATRIX_NATIVE_FOCUS_HIDE=1 GF_MATRIX_MAX_FRAMES=1040 \
    TORIRS_NET_DEBUG=1 TORIRS_SIM_CLICK_AT='500,785,88;650,820,70' \
    TORIRS_SIM_TYPE='700,c97,c98;800,c120;900,c99' \
    TORIRS_SIM_CMD='750,ifhide 58589197 1;850,ifhide 58589197 0'
  echo "NATIVE CONTRACT: $failures failed scenario groups / 11 (14 captures)"
  exit $((failures > 0))
fi
if [[ "${GF_MATRIX_SCORE_ONLY:-0}" != 1 ]]; then
[[ ! -e "$OUT/index.txt" ]] || { echo "capture index already exists: $OUT/index.txt" >&2; exit 2; }
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
[plugin:xp-drop-orbs]
enabled=1
EOF

one() {
  local tag=$1 mode=$2 size=$3 frame=$4 mobile=$5
  local run=$OUT/$tag
  # Never erase a previous run (or an unrelated directory supplied as OUT).
  [[ ! -e "$run" ]] || { echo "capture already exists: $run" >&2; return 2; }
  mkdir -p "$run/saves"
  printf '[preferences]\nversion=1\npreferred_frame=%s\nframe_migration_version=1\n' \
    "$frame" > "$run/preferences.ini"
  sed -e "s/^client_layout_mode = .*/client_layout_mode = $mode/" \
    -e "s/^x = .*/x = 3210/" -e "s/^z = .*/z = 3424/" -e "s/^level = .*/level = 0/" \
    "$REPO/saves/testc.ini" > "$run/saves/testc.ini"
  echo "PINNED tag=$tag mode=$mode size=$size frame=$frame scale=none after_ready=1" > "$run/log.txt"
  local -a env_extra
  env_extra=()
  [ "$mobile" = "1" ] && env_extra=(TORIRS_CLIENTTYPE=7)
  [[ "$BASELINE" == 1 && "${GF_MATRIX_WIDGET_DEMO:-0}" == 0 && -z "${GF_MATRIX_PLUGIN:-}" ]] && env_extra+=(TORIRS_PLUGINS=0)
  [[ "${GF_MATRIX_WIDGET_DEMO:-0}" == 1 ]] && env_extra+=(TORIRS_WIDGET_DEMO=only TORIRS_PLUGIN_LOG=1)
  [[ "${GF_MATRIX_WIDGET_DEMO:-0}" == lua ]] && env_extra+=(TORIRS_WIDGET_DEMO=lua TORIRS_PLUGIN_LOG=1)
  # The PLUGIN_FIND_ALL lines the --find-all-holes rule reads are traced under
  # TORIRS_TRACE_PLUGIN_WORLD.
  [[ -n "${GF_MATRIX_FIND_ALL_HOLES:-}" ]] && env_extra+=(TORIRS_TRACE_PLUGIN_WORLD=1)
  [[ "${GF_MATRIX_PERF:-0}" == 1 ]] && env_extra+=(TORIRS_PERF=1 TORIRS_PERF_CSV="$run/perf.csv" TORIRS_PERF_WINDOW=200)
  local -a client_args
  client_args=()
  if [[ "$REVISION" == rs289lc ]]; then
    local lc_user
    [[ -n "${GF_MATRIX_LC_SERVER:-}" ]] || { echo 'rs289 captures require GF_MATRIX_LC_SERVER for unique account allocation'; return 2; }
    lc_user=$(python3 - "$GF_MATRIX_LC_SERVER" "${GF_MATRIX_LC_SAVE:-}" "$run" <<'PY_ACCOUNT'
import hashlib, json, pathlib, sys, uuid
server, seed_name, run_name = sys.argv[1:]
server, run = pathlib.Path(server), pathlib.Path(run_name)
profile = json.loads((server/'engine/data/config/world.json').read_text())['node']['profile']
ledger = server/'.gameframe-accounts'
ledger.mkdir(exist_ok=True)
for attempt in range(1024):
    user = 'gf' + uuid.uuid4().hex[:10]
    destination = server/'engine/data/players'/profile/(user+'.sav')
    if destination.exists():
        continue
    try:
        with (ledger/user).open('x') as marker:
            marker.write(str(run)+'\n')
    except FileExistsError:
        continue
    break
else:
    raise RuntimeError('could not reserve a fresh LostCity account')
record = {'user':user, 'seed':None, 'save':str(destination), 'reservation':str(ledger/user)}
if seed_name:
    seed = pathlib.Path(seed_name)
    data = seed.read_bytes()
    with destination.open('xb') as stream:
        stream.write(data)
    record.update(seed=str(seed), seed_sha256=hashlib.sha256(data).hexdigest())
(run/'player.json').write_text(json.dumps(record, indent=2)+'\n')
print(user)
PY_ACCOUNT
    ) || return 2
    client_args=(--user "$lc_user" --pass "${GF_MATRIX_LC_PASS:-local}")
  fi
  ( cd "$REPO" && env TORIRS_PREFS="$run/preferences.ini" \
      TORIRSSERVER_SAVES="$run/saves" \
      TORIRS_PLUGIN_PREFS="$OUT/plugin_prefs.ini" TORIRS_PLUGINS=1 \
      TORIRS_STDERR_UNBUFFERED=1 TORIRS_TRACE_NATIVE_UI=1 TORIRS_SIM_AFTER_READY=1 \
      SDL_VIDEODRIVER=${SDL_VIDEODRIVER:-dummy} TORIRS_MAX_FRAMES=${GF_MATRIX_MAX_FRAMES:-620} TORIRS_FRAME_ROLE_AUDIT=1 \
      TORIRS_EXIT_BMP="$run/out.bmp" TORIRS_DUMP_BOUNDS=all TORIRS_DUMP_EMIT_EXIT=all "${env_extra[@]}" \
      "$BIN" --manifest "$MANIFEST" --windowmode resizable --window "$size" "${client_args[@]}" \
      >> "$run/log.txt" 2>&1 )
  echo $? > "$run/exit-status"
}

i=0
if [[ "$REVISION" == rs289lc ]]; then
  rs_frame=${GF_MATRIX_RS289_FRAME:-core/native}
  echo "r01|R|$rs_frame|765x503" >> "$OUT/index.txt"
  [[ "$rs_frame" == core/native ]] && BASELINE=1 || BASELINE=0
  one r01 0 765x503 "$rs_frame" 0
else
for m in 0 1 2 M; do for f in $FRAMES_LIST; do for s in $SIZES; do
  i=$((i+1)); tag="m$(printf '%02d' $i)"
  [[ "$BASELINE" == 1 && "$f" != auto ]] && continue
  if [[ -n "${GF_MATRIX_TAGS:-}" && ",${GF_MATRIX_TAGS}," != *",${tag},"* ]]; then continue; fi
  mode=$m; mobile=0
  [ "$m" = "M" ] && { mode=1; mobile=1; }
  echo "$tag|$m|$f|$s" >> "$OUT/index.txt"
  one "$tag" "$mode" "$s" "$f" "$mobile" &
  [ $((i % 4)) -eq 0 ] && wait
done; done; done
wait
fi
fi

fail=0
checks=0
printf "%-5s %-4s %-38s %-9s %-5s %-8s %s\n" TAG TOP FRAME SIZE ROOT FILTERS VERDICT
while IFS='|' read tag m f s; do
  L=$OUT/$tag/log.txt
  widget_args=(--public-chat-mode "${GF_MATRIX_PUBLIC_CHAT_MODE:-on}" --owned-count "${GF_MATRIX_OWNED_COUNT:-1}" --widget-offset "${GF_MATRIX_WIDGET_OFFSET:-12}" --widget-moves "${GF_MATRIX_WIDGET_MOVES:-1}" --widget-rune-slot "${GF_MATRIX_WIDGET_RUNE_SLOT:-0}")
  if [[ -n "${GF_MATRIX_PLUGIN:-}" ]]; then
    widget_args+=(--plugin-id "${GF_MATRIX_PLUGIN#lua:}" --plugin-enabled "${GF_MATRIX_PLUGIN_ENABLED:-1}")
    [[ "$GF_MATRIX_PLUGIN" == lua:* ]] && widget_args+=(--plugin-lua)
    widget_args+=(--performance-metrics "${GF_MATRIX_PERFORMANCE_METRICS-fps,frame,effective,memory}"
      --performance-position "${GF_MATRIX_PERFORMANCE_POSITION:-10,25}"
      --performance-color "${GF_MATRIX_PERFORMANCE_COLOR:-FFFFFF}")
  fi
  [[ -n "${GF_MATRIX_NATIVE_GROUND_LABELS:-}" ]] && widget_args+=(--native-ground-labels "$GF_MATRIX_NATIVE_GROUND_LABELS")
  [[ -n "${GF_MATRIX_GROUND_ROW_GAP:-}" ]] && widget_args+=(--ground-row-gap "$GF_MATRIX_GROUND_ROW_GAP")
  [[ -n "${GF_MATRIX_NATIVE_CAPTION:-}" ]] && widget_args+=(--native-caption "$GF_MATRIX_NATIVE_CAPTION")
  [[ -n "${GF_MATRIX_OWNED_TEXT:-}" ]] && widget_args+=(--owned-text "$GF_MATRIX_OWNED_TEXT")
  if [[ -n "${GF_MATRIX_OVERLAY_TEXT:-}" ]]; then
    for expected_text in "${(@s:|:)GF_MATRIX_OVERLAY_TEXT}"; do
      widget_args+=(--overlay-text "$expected_text")
    done
  fi
  [[ "${GF_MATRIX_WIDGET_DEMO:-0}" == 1 ]] && widget_args+=(--widget-demo c)
  [[ "${GF_MATRIX_WIDGET_DEMO:-0}" == lua ]] && widget_args+=(--widget-demo lua)
  # GF_MATRIX_WIDGET_OP=1: the simulated click (TORIRS_SIM_CLICK_AT) must land
  # inside the demo's owned control and its operation must run.
  [[ "${GF_MATRIX_WIDGET_OP:-0}" == 1 ]] && widget_args+=(--widget-op)
  # GF_MATRIX_EXPECT_LOG: pipe-separated regexes the client log must contain.
  if [[ -n "${GF_MATRIX_EXPECT_LOG:-}" ]]; then
    for expected_line in "${(@s:|:)GF_MATRIX_EXPECT_LOG}"; do
      widget_args+=(--expect-log "$expected_line")
    done
  fi
  # GF_MATRIX_FORBID_LOG: pipe-separated regexes the client log must NOT contain.
  if [[ -n "${GF_MATRIX_FORBID_LOG:-}" ]]; then
    for forbidden_line in "${(@s:|:)GF_MATRIX_FORBID_LOG}"; do
      widget_args+=(--forbid-log "$forbidden_line")
    done
  fi
  # GF_MATRIX_HIGHLIGHT_COLOR=RRGGBB[:min]: the engine recorded a live cache
  # highlight group of that colour with members, and the renderer painted it.
  [[ -n "${GF_MATRIX_HIGHLIGHT_COLOR:-}" ]] && widget_args+=(--highlight-color "$GF_MATRIX_HIGHLIGHT_COLOR")
  # GF_MATRIX_PANEL_CUSTOM_INK=ID[:min]: a plugin page custom row has its allotted
  # region and at least min distinct colours painted inside it.
  [[ -n "${GF_MATRIX_PANEL_CUSTOM_INK:-}" ]] && widget_args+=(--panel-custom-ink "$GF_MATRIX_PANEL_CUSTOM_INK")
  # GF_MATRIX_DEST_TILE=1: the tile indicator's yellow destination marker is
  # painted while NATIVE_PLAYER reports the walk still under way.
  [[ "${GF_MATRIX_DEST_TILE:-0}" == 1 ]] && widget_args+=(--dest-tile)
  # GF_MATRIX_MENU_ROW: pipe-separated regexes, each matching one row of the
  # right-click menu TORIRS_MINIMENU_DEBUG=1 reported.
  if [[ -n "${GF_MATRIX_MENU_ROW:-}" ]]; then
    for menu_row in "${(@s:|:)GF_MATRIX_MENU_ROW}"; do
      widget_args+=(--menu-row "$menu_row")
    done
  fi
  # GF_MATRIX_OVERLAY_TEXT_ABSENT / GF_MATRIX_NATIVE_CAPTION_ABSENT: pipe-separated
  # exact label texts that must NOT have been drawn (a hidden ground item).
  if [[ -n "${GF_MATRIX_OVERLAY_TEXT_ABSENT:-}" ]]; then
    for absent_text in "${(@s:|:)GF_MATRIX_OVERLAY_TEXT_ABSENT}"; do
      widget_args+=(--overlay-text-absent "$absent_text")
    done
  fi
  if [[ -n "${GF_MATRIX_NATIVE_CAPTION_ABSENT:-}" ]]; then
    for absent_text in "${(@s:|:)GF_MATRIX_NATIVE_CAPTION_ABSENT}"; do
      widget_args+=(--native-caption-absent "$absent_text")
    done
  fi
  # GF_MATRIX_FIND_ALL_HOLES: pipe-separated ROLE:COUNT:MISSING, each the
  # numbering the bridge's last PLUGIN_FIND_ALL line for ROLE must report --
  # count one past the highest member, MISSING the comma list of holes.
  if [[ -n "${GF_MATRIX_FIND_ALL_HOLES:-}" ]]; then
    for find_all_spec in "${(@s:|:)GF_MATRIX_FIND_ALL_HOLES}"; do
      widget_args+=(--find-all-holes "$find_all_spec")
    done
  fi
  # GF_MATRIX_SCENE_OBJECTS=N: the engine holds exactly N active plugin world
  # objects at exit (loot beams).
  [[ -n "${GF_MATRIX_SCENE_OBJECTS:-}" ]] && widget_args+=(--scene-objects "$GF_MATRIX_SCENE_OBJECTS")
  # GF_MATRIX_SCREENSHOT_SAVED=1: a plugin "captured <path>" line whose file exists.
  [[ "${GF_MATRIX_SCREENSHOT_SAVED:-0}" == 1 ]] && widget_args+=(--screenshot-saved)
  # GF_MATRIX_REPORT_REPLACED=1: the native report control is plugin-hidden (native
  # hide untouched) and the plugin's camera control sits inside its slot.
  [[ "${GF_MATRIX_REPORT_REPLACED:-0}" == 1 ]] && widget_args+=(--report-replaced)
  if [[ "$m" == R ]]; then
    python3 "$TOOLS_DIR/gameframe_pixels.py" "$OUT/$tag/out.bmp" --frame "$f" \
      --root 0 --revision rs289lc --rs289-scenario "${GF_MATRIX_RS289_SCENARIO:-baseline}" \
      --bounds "$L" "${widget_args[@]}" > "$OUT/$tag/pixels.txt" 2>&1
    result=$?
    cat "$OUT/$tag/pixels.txt"
    [[ "$result" == 0 && "$(cat "$OUT/$tag/exit-status" 2>/dev/null)" == 0 ]] || fail=$((fail+1))
    continue
  fi
  rt=$(grep -o 'switching root [-0-9]* -> [0-9]*' "$L" 2>/dev/null | tail -1 | grep -o '[0-9]*$')
  [[ -z "$rt" ]] && rt=$(sed -n 's/^NATIVE_ROOT id=\([0-9]*\)$/\1/p' "$L" | tail -1)
  n=$(grep '^BOUNDS' "$L" 2>/dev/null | awk '{g=$3;gsub(/[()]/,"",g);split(g,p,"|");x=p[2]+0;
        if(p[1]==162 && (x==5||x==8||x==12||x==16||x==20||x==24||x==28||x==32) && $0!~/hidden=1/) print}' | wc -l | tr -d ' ')
  bar=$(grep '^BOUNDS' "$L" 2>/dev/null | grep -c '(162|3)')
  want=8; [ "$rt" = "601" ] && want=7
  v=ok
  before=$checks
  [[ "$(cat "$OUT/$tag/exit-status" 2>/dev/null)" == 0 ]] || { v="CLIENT FAILED"; checks=$((checks+1)); }
  case "$m" in 0) expected_root=548;; 1) expected_root=161;; 2) expected_root=164;; M) expected_root=601;; esac
  expected_root=${GF_MATRIX_EXPECT_ROOT:-$expected_root}
  active=$(awk '/^BOUNDS/{exit} /^frame_selection:/{for(i=1;i<=NF;i++) if($i~/^active=/){value=$i;sub(/^active=/,"",value)}} END{print value}' "$L")
  active=${active:-$f}
  [[ "$BASELINE" == 1 ]] && active=core/native
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
    if [[ "$BASELINE" != 1 && "${GF_MATRIX_EXPECT_NATIVE:-0}" != 1 ]] && { ! grep -q "frameroles: root $rt, .*roles checked, .* absent, 0 unbound, 0 mismatched" "$L" || grep -Eq 'frameroles: .* (MISMATCH|UNBOUND)' "$L"; }; then
      v="ROLE AUDIT"; checks=$((checks+1))
    fi
    local_state_args=("${widget_args[@]}")
    [[ "$BASELINE" == 1 ]] && local_state_args+=(--native-baseline)
    [[ -n "${GF_MATRIX_MINIMAP_STATE:-}" ]] && local_state_args+=(--minimap-state "$GF_MATRIX_MINIMAP_STATE")
    [[ -n "${GF_MATRIX_SERVER_HIDE:-}" ]] && local_state_args+=(--server-hide "$GF_MATRIX_SERVER_HIDE")
    [[ -n "${GF_MATRIX_INPUT_STATE:-}" ]] && local_state_args+=(--input-state "$GF_MATRIX_INPUT_STATE")
    [[ "${GF_MATRIX_NATIVE_FOCUS_HIDE:-0}" == 1 ]] && local_state_args+=(--native-focus-hide)
    python3 "$TOOLS_DIR/gameframe_pixels.py" "$OUT/$tag/out.bmp" --frame "$active" --root "$rt" --bounds "$L" "${local_state_args[@]}" > "$OUT/$tag/pixels.txt" 2>&1 || { v="PIXELS"; checks=$((checks+1)); }
    cat "$OUT/$tag/pixels.txt"
  fi
  [[ "$checks" != "$before" ]] && fail=$((fail+1))
  printf "%-5s %-4s %-38s %-9s %-5s %-8s %s\n" "$tag" "$m" "$f" "$s" "${rt:--}" "$n" "$v"
done < "$OUT/index.txt"
echo "--- $fail failures ($checks checks) / $(wc -l < "$OUT/index.txt" | tr -d ' ') --- captures in $OUT"
[[ -s "$OUT/index.txt" ]] || { echo 'no captures selected'; exit 2; }
if [[ "${GF_MATRIX_DIAGNOSTIC:-0}" == 1 ]]; then
  echo 'DIAGNOSTIC ONLY: fixture acceptance not established'
  exit 3
fi
python3 - "$OUT/fixture.json" <<'PY'
import json, sys
try:
    accepted = json.load(open(sys.argv[1]))['accepted'] is True
except (OSError, ValueError, KeyError):
    accepted = False
if not accepted:
    print('FIXTURE BLOCKED: missing or rejected provenance; pixel scores cannot approve it')
    sys.exit(2)
PY
[[ $? == 0 ]] || exit 2
exit $(( fail > 0 ))
