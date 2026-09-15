#!/bin/zsh
# shot.sh <name> <cs2|cs1> [EXTRA_ENV=value ...]
#
# One screenshot of one plugin, driven into the state where it actually DRAWS.
#
# The port gate answers "did anything move". This answers "does it look right",
# which is a different question and the one nobody had asked: half the ported
# plugins have a page, a tooltip, a label or a beam that no gate lane ever puts
# on screen, so their pixels had never been seen at all.
set -u

here=${0:A:h}
repo=/Users/matthewevers/Documents/git_repos/3draster
# The tree under test defaults to the one this script lives in, as lane.sh and
# capture_set.sh do. It used to name one agent's worktree, which made every
# shot anyone else took a shot of somebody else's scripts.
wt=${TORIRS_SHOT_WORKTREE:-${here:h:h:h}}
bin=${TORIRS_SHOT_BIN:-$wt/src/torirs_plain}

name=$1 lane=$2; shift 2

run=${TORIRS_SHOT_RUNS:-$here/runs}/$name; rm -rf $run; mkdir -p $run/saves

# A toplevel is not a knob the client takes: it is a consequence of the save's
# client_layout_mode and of whether the lane logs in as a phone, which is how
# lane.sh picks one for the gate and so how it is picked here. 0 -> 548,
# 1 -> 161, 2 -> 164, and TORIRS_CLIENTTYPE=7 -> 601. The frame under test has
# to be the one that OWNS that toplevel, or the shot is of the wrong provider.
mobile=0
case $lane in
  cs2|classic548)
        manifest=manifest_osrs239_curses.ini; mode=0; frame='gameframe-layout/classic-fixed';    extra_args=() ;;
  classic161)
        manifest=manifest_osrs239_curses.ini; mode=1; frame='gameframe-layout/classic-fixed';    extra_args=() ;;
  modern164)
        manifest=manifest_osrs239_curses.ini; mode=2; frame='gameframe-layout/modern-resizable'; extra_args=() ;;
  stone601)
        manifest=manifest_osrs239_curses.ini; mode=0; frame='mobile-gameframe/stone-drawer';     extra_args=(); mobile=1 ;;
  native548)
        manifest=manifest_osrs239_curses.ini; mode=0; frame=auto;                                extra_args=() ;;
  cs1|dat1_254)
        manifest=manifest_rs254lc.ini;        mode=0; frame=auto;                                extra_args=(--offline) ;;
  # The CS1 lane LOGGED IN, against a real LostCity server. @see lostcity.sh
  # for the three gates that all answer login reply 6. `cs1` above boots the
  # world and never logs in, so a skill has no reading, the inventory is empty
  # and nothing ever speaks in the chat pane -- half of every plugin's output
  # is missing from those shots and none of it looks broken, which is worse.
  cs1live)
        manifest=manifest_rs289lc.ini;        mode=0; frame=auto;                                extra_args=() ;;
  *) echo "lane must be one of cs2|classic548 classic161 modern164 stone601 native548 cs1|dat1_254 cs1live"; exit 2 ;;
esac

# A live lane needs an account and the server's current checksums. The user has
# to differ per run: LostCity answers login reply 5 ("already logged in") while
# a previous run's session is still held, and a shot sweep runs back to back.
if [ "$lane" = cs1live ]; then
  # base37, so a-z 0-9 and underscore only, and twelve characters at the most.
  # A shot name is neither -- `live-orbs` has a hyphen and `loottracker-loot`
  # is sixteen -- and both come back as login reply 3 (invalid username), which
  # reads exactly like a wrong password and is not one.
  #
  # ELEVEN, not twelve. Twelve characters only fit in the base37 word when the
  # FIRST one is a..f: the pack is c1*37^11 + ... and the decoder rejects
  # anything from 0x1000000000000000 up, so `szzrephitliv` -- an `s` and eleven
  # more, which is what this line used to build -- decodes to the literal
  # string "invalid_name" on the way back in. Every cs1live shot in the
  # programme therefore logged in under a name the client could not read, and
  # the screenshot plugin filed its captures in a folder called
  # `invalid-name`. Eleven characters always fit, whatever the first one is.
  lcuser=${TORIRS_LC_USER:-s$(printf '%s' "$name" | tr -c 'a-z0-9' '_' | cut -c1-10)}
  extra_args+=(--user "$lcuser" --pass "${TORIRS_LC_PASS:-zuk}")
  : ${TORIRS_JAG_CRC:=$(zsh $here/lostcity.sh)} || exit 2
  export TORIRS_JAG_CRC
fi

# The manifests are generated once by shot_manifests.sh, with the caches and
# content pinned to the data checkout and revconfig to the worktree under test.
# TORIRS_SHOT_MANIFESTS selects a second set, which is what a BEFORE shot needs:
# its revconfig must come from the tree the BEFORE binary was built from.
mdir=${TORIRS_SHOT_MANIFESTS:-$here/manifests}
[ -f $mdir/$manifest ] || { echo "run shot_manifests.sh first ($mdir/$manifest)"; exit 2; }

# TORIRS_SHOT_FRAME overrides the lane's provider.
#
# preferred_frame is the MASTER SWITCH for a frame provider, not the ini:
# `[plugin:gameframe-layout] enabled=1` with `preferred_frame=auto` leaves the
# provider off, and `preferred_frame=<provider>/<offer>` turns it on however the
# ini is written. So "photograph the desktop provider on the 2004 lane", which
# the presets cannot express, needs this and not an ini row.
printf '[preferences]\nversion=1\npreferred_frame=%s\nframe_migration_version=1\n' \
    "${TORIRS_SHOT_FRAME:-$frame}" \
    > $run/preferences.ini
sed -e "s/^client_layout_mode = .*/client_layout_mode = $mode/" \
    -e "s/^x = .*/x = 3210/" -e "s/^z = .*/z = 3424/" -e "s/^level = .*/level = 0/" \
    $repo/saves/testc.ini > $run/saves/testc.ini

env_extra=()
[ "$mobile" = 1 ] && env_extra+=(TORIRS_CLIENTTYPE=7)
for kv in "$@"; do env_extra+=("$kv"); done

# The client REWRITES its prefs file on exit. Pointing every run at the shared
# tracked plugins_all.ini therefore made each shot edit the harness and leak its
# own TORIRS_SIM_PLUGIN_CONFIG into every later shot -- a loot-beam capture came
# back wearing the entity highlighter's tags. Each run gets a private copy, and
# the tracked file stays the input it is supposed to be. (Its path was wrong
# too: plugins_all.ini is one level up, not in a gate/ subdirectory.)
cp ${TORIRS_SHOT_PREFS:-$here/../plugins_all.ini} $run/plugins.ini

( cd $repo && env \
    TORIRS_PREFS=$run/preferences.ini \
    TORIRSSERVER_SAVES=$run/saves \
    TORIRS_PLUGIN_PREFS=$run/plugins.ini \
    TORIRS_PLUGINS=1 \
    TORIRS_SCRIPT_DIR=$wt/script \
    TORIRS_STDERR_UNBUFFERED=1 \
    TORIRS_PLUGIN_LOG=1 \
    TORIRS_SIM_AFTER_READY=1 \
    SDL_VIDEODRIVER=dummy \
    TORIRS_MAX_FRAMES=${TORIRS_SHOT_FRAMES:-700} \
    TORIRS_EXIT_BMP=$run/out.bmp \
    TORIRSSERVER_ALLOW_STALE_SCRIPTS=1 \
    "${env_extra[@]}" \
    $bin --manifest $mdir/$manifest --windowmode resizable \
        --window ${TORIRS_SHOT_WINDOW:-765x503} "${extra_args[@]}" \
    > $run/log.txt 2>&1 )
rc=$?

if [ ! -s $run/out.bmp ]; then
  echo "$name/$lane: NO IMAGE (exit $rc)"; tail -3 $run/log.txt; exit 1
fi
sips -s format png $run/out.bmp --out ${TORIRS_SHOT_OUT:-$here}/$name.png >/dev/null 2>&1
printf "%-28s %-4s exit=%s bounds=%s %s\n" "$name" "$lane" "$rc" \
  "$(grep -c '^BOUNDS' $run/log.txt)" "${TORIRS_SHOT_OUT:-$here}/$name.png"
