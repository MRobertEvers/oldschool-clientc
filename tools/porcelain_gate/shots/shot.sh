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
  *) echo "lane must be one of cs2|classic548 classic161 modern164 stone601 native548 cs1|dat1_254"; exit 2 ;;
esac

# The manifests are generated once by shot_manifests.sh, with the caches and
# content pinned to the data checkout and revconfig to the worktree under test.
# TORIRS_SHOT_MANIFESTS selects a second set, which is what a BEFORE shot needs:
# its revconfig must come from the tree the BEFORE binary was built from.
mdir=${TORIRS_SHOT_MANIFESTS:-$here/manifests}
[ -f $mdir/$manifest ] || { echo "run shot_manifests.sh first ($mdir/$manifest)"; exit 2; }

printf '[preferences]\nversion=1\npreferred_frame=%s\nframe_migration_version=1\n' "$frame" \
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
