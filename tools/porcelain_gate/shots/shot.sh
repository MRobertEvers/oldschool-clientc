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
wt=${TORIRS_SHOT_WORKTREE:-/Users/matthewevers/Documents/git_repos/3draster/.claude/worktrees/agent-a0428158f40a99477}
bin=${TORIRS_SHOT_BIN:-$wt/src/torirs_plain}

name=$1 lane=$2; shift 2

run=$here/runs/$name; rm -rf $run; mkdir -p $run/saves
case $lane in
  cs2) manifest=m239.ini; mode=0; frame='gameframe-layout/classic-fixed'; extra_args=() ;;
  cs1) manifest=m254.ini; mode=0; frame=auto;                             extra_args=(--offline) ;;
  *) echo "lane must be cs2 or cs1"; exit 2 ;;
esac

# The manifests are generated once by shot_manifests.sh, with the caches and
# content pinned to the data checkout and revconfig to the worktree under test.
[ -f $here/manifests/$manifest ] || { echo "run shot_manifests.sh first"; exit 2; }

printf '[preferences]\nversion=1\npreferred_frame=%s\nframe_migration_version=1\n' "$frame" \
    > $run/preferences.ini
sed -e "s/^client_layout_mode = .*/client_layout_mode = $mode/" \
    -e "s/^x = .*/x = 3210/" -e "s/^z = .*/z = 3424/" -e "s/^level = .*/level = 0/" \
    $repo/saves/testc.ini > $run/saves/testc.ini

env_extra=()
for kv in "$@"; do env_extra+=("$kv"); done

( cd $repo && env \
    TORIRS_PREFS=$run/preferences.ini \
    TORIRSSERVER_SAVES=$run/saves \
    TORIRS_PLUGIN_PREFS=${TORIRS_SHOT_PREFS:-$here/../gate/plugins_all.ini} \
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
    $bin --manifest $here/manifests/$manifest --windowmode resizable \
        --window ${TORIRS_SHOT_WINDOW:-765x503} "${extra_args[@]}" \
    > $run/log.txt 2>&1 )
rc=$?

if [ ! -s $run/out.bmp ]; then
  echo "$name/$lane: NO IMAGE (exit $rc)"; tail -3 $run/log.txt; exit 1
fi
sips -s format png $run/out.bmp --out $here/$name.png >/dev/null 2>&1
printf "%-28s %-4s exit=%s bounds=%s %s\n" "$name" "$lane" "$rc" \
  "$(grep -c '^BOUNDS' $run/log.txt)" "$here/$name.png"
