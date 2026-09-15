#!/bin/zsh
# The eight permutations every Porcelain port is judged on.
#
#   capture_set.sh <outdir> <binary> [worktree]
#
# <worktree> is the tree whose revconfig and scripts are under test; it
# defaults to the tree this script lives in. The DATA -- caches, content,
# saves -- always comes from TORIRS_GATE_REPO (default: the same tree), because
# a cache is gigabytes and nobody copies one per agent.
#
# That split is the whole reason this script writes its own manifests instead
# of passing the repo's. The checked-in manifests use paths relative to the
# checkout root, which resolve against the DATA tree once the client has cd'd
# there -- so a worktree's revconfig edits would silently not be under test.
# Here the cache and content lines are pinned to the data tree and the
# revconfig lines to the worktree, which is what makes a gate run mean
# "this worktree's code against the shared data".
set -u

here=${0:A:h}
out=${1:A} bin=${2:A}
worktree=${3:-${here:h:h}}; worktree=${worktree:A}
repo=${TORIRS_GATE_REPO:-${here:h:h}}; repo=${repo:A}

mkdir -p $out/manifests
for m in manifest_osrs239_curses manifest_rs254lc; do
  python3 - "$repo/manifests/$m.ini" "$out/manifests/$m.ini" "$repo" "$worktree" <<'PY'
import re, sys
src, dst, repo, worktree = sys.argv[1:5]
text = open(src).read()
def absolutise(match):
    key, path = match.group(1), match.group(2)
    if not path.startswith('../'):
        return match.group(0)
    # revconfig is the code under test; everything else is shared data.
    base = worktree if 'revconfig' in path else repo
    return f"{key}={base}/{path[3:]}"
open(dst, 'w').write(re.sub(r'(?m)^(\w+)=(\.\./\S*)$', absolutise, text))
PY
done
M=$out/manifests

# The Lua plugins under test come from the worktree too, for exactly the reason
# revconfig does. Without this the client resolves its script directory against
# the DATA tree, so a worktree's ported plugins are never loaded and the gate
# happily compares the data tree's plugins against themselves -- a PASS that
# measured nothing. That is not hypothetical: six Lua ports were gated this way
# before anyone noticed the captures contained no trace of them.
export TORIRS_GATE_SCRIPT_DIR=${TORIRS_GATE_SCRIPT_DIR:-$worktree/script}

$here/lane.sh $out native548  $bin 0 auto                              0 $M/manifest_osrs239_curses.ini
$here/lane.sh $out classic548 $bin 0 gameframe-layout/classic-fixed    0 $M/manifest_osrs239_curses.ini
$here/lane.sh $out classic161 $bin 1 gameframe-layout/classic-fixed    0 $M/manifest_osrs239_curses.ini
$here/lane.sh $out modern164  $bin 2 gameframe-layout/modern-resizable 0 $M/manifest_osrs239_curses.ini
$here/lane.sh $out stone601   $bin 0 mobile-gameframe/stone-drawer     1 $M/manifest_osrs239_curses.ini
$here/lane.sh $out dat1_254   $bin 0 auto                              0 $M/manifest_rs254lc.ini --offline

# The CS1 lane above runs `auto`, which is the lane's own frame and no provider
# -- so for the whole life of this gate NO lane gated a frame provider on CS1 at
# all, and a CS1-only frame regression was invisible to all eight.
#
# It went unnoticed because the two questions look like one. `dat1_254` proves a
# port does not read a dat2 fact; it says nothing about what a PROVIDER does on
# a lane whose chrome it has to suppress and redraw. A provider that staged
# nothing there would pass every lane in the set.
#
# Both providers, because both were asked for on both lanes and because they
# fail differently: classic-fixed reproduces the 2004 frame closely enough that
# "it drew correctly" and "it never ran" are the same picture -- which is
# exactly the confusion that cost a day -- while stone-drawer replaces the
# chrome outright and shows a lane mismatch immediately.
$here/lane.sh $out dat1_classic $bin 0 gameframe-layout/classic-fixed  0 $M/manifest_rs254lc.ini --offline
$here/lane.sh $out dat1_stone   $bin 0 mobile-gameframe/stone-drawer   1 $M/manifest_rs254lc.ini --offline

# The seventh lane switches layout half way through, and it is here because a
# regression got past the first six. A port can be byte-identical on every
# static lane and still lose every control it owns the moment the frame root is
# replaced: the layer moves a control through its setters and has no arm that
# re-creates one whose parent died. That is invisible to a capture that never
# remounts anything.
#
# 500 of 620 frames leaves 120 for the description to come back, which is far
# more than it needs and little enough that a lane costs the same as the others.
TORIRS_GATE_SIM_CMD='500,layout 2' \
$here/lane.sh $out remount164 $bin 0 gameframe-layout/classic-fixed    0 $M/manifest_osrs239_curses.ini

# And the same switch the OTHER way, because a remount is not symmetric and the
# lane above could not see it.
#
# 548 to 164 replaces a fixed toplevel with a resizable one, and the only thing
# that went stale was one of the plugin's OWN controls -- which the layer
# already absorbs and repairs. 164 to 548 replaces a floating panel with a
# frame that owns the whole right column, and there it is the LANE's nodes that
# die under the description: measured on this lane before it existed,
# twenty-five stale references in one fence -- every live-surface move, all
# fourteen sidebar mounts, the three orb members, the panel backing's opacity
# and the left pillar's creation -- and a frame drawn with the old toplevel's
# collapsed geometry standing on the new one.
#
# Same frame, same offer, same 500-of-620 split as its twin: only `mode` and
# the command differ, which is what makes the pair a pair.
TORIRS_GATE_SIM_CMD='500,layout 0' \
$here/lane.sh $out remount548 $bin 2 gameframe-layout/modern-resizable 0 $M/manifest_osrs239_curses.ini

for t in native548 classic548 classic161 modern164 stone601 dat1_254 dat1_classic dat1_stone remount164 remount548; do
  printf "%-12s exit=%s bounds=%s roles=%s owned=%s\n" $t \
    "$(cat $out/$t/exit-status)" \
    "$(grep -c '^BOUNDS' $out/$t/log.txt)" \
    "$(grep -c '^ROLE_WIDGET' $out/$t/log.txt)" \
    "$(grep -c '^OWNED_WIDGET' $out/$t/log.txt)"
done
