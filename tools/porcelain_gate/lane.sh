#!/bin/zsh
# One capture of one lane/frame permutation, with every plugin enabled.
#
#   lane.sh <outdir> <tag> <binary> <mode 0|1|2> <frame> <mobile 0|1> <manifest> [extra client args]
#
# Every path this takes is resolved to an absolute one before the client runs,
# because the client is run from the DATA checkout and a relative path would
# then mean somewhere else entirely. That is not a hypothetical: passing a
# relative outdir here produced six lanes that each exited 1 with no log file
# at all, because the shell opened the redirect after the cd.
set -u

here=${0:A:h}
repo=${TORIRS_GATE_REPO:-${here:h:h}}       # the DATA checkout: caches, saves, content

out=${1:A} tag=$2 bin=${3:A} mode=$4 frame=$5 mobile=$6 manifest=${7:A}; shift 7

run=$out/$tag
mkdir -p $run/saves

printf '[preferences]\nversion=1\npreferred_frame=%s\nframe_migration_version=1\n' "$frame" \
    > $run/preferences.ini

# The save carries the layout mode and the spot the client stands in. Both are
# fixed here rather than left to whatever the checkout's save happens to hold,
# because the gate compares component boxes and a different position is a
# different scene.
sed -e "s/^client_layout_mode = .*/client_layout_mode = $mode/" \
    -e "s/^x = .*/x = 3210/" \
    -e "s/^z = .*/z = 3424/" \
    -e "s/^level = .*/level = 0/" \
    $repo/saves/testc.ini > $run/saves/testc.ini

env_extra=()
[ "$mobile" = 1 ] && env_extra+=(TORIRS_CLIENTTYPE=7)

(
  cd $repo && env \
    TORIRS_PREFS=$run/preferences.ini \
    TORIRSSERVER_SAVES=$run/saves \
    TORIRS_PLUGIN_PREFS=$here/plugins_all.ini \
    TORIRS_PLUGINS=1 \
    TORIRS_STDERR_UNBUFFERED=1 \
    TORIRS_TRACE_NATIVE_UI=1 \
    TORIRS_PLUGIN_LOG=1 \
    TORIRS_SIM_AFTER_READY=1 \
    SDL_VIDEODRIVER=dummy \
    TORIRS_MAX_FRAMES=${TORIRS_GATE_FRAMES:-620} \
    TORIRS_DUMP_ROLES=1 \
    TORIRS_DUMP_BOUNDS=all \
    TORIRS_EXIT_BMP=$run/out.bmp \
    TORIRSSERVER_ALLOW_STALE_SCRIPTS=1 \
    ${TORIRS_GATE_SCRIPT_DIR:+TORIRS_SCRIPT_DIR=$TORIRS_GATE_SCRIPT_DIR} \
    "${env_extra[@]}" \
    $bin --manifest $manifest --windowmode resizable --window 765x503 "$@" \
    > $run/log.txt 2>&1
)
echo $? > $run/exit-status
