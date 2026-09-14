#!/bin/zsh
# The six permutations every Porcelain port is judged on.
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

$here/lane.sh $out native548  $bin 0 auto                              0 $M/manifest_osrs239_curses.ini
$here/lane.sh $out classic548 $bin 0 gameframe-layout/classic-fixed    0 $M/manifest_osrs239_curses.ini
$here/lane.sh $out classic161 $bin 1 gameframe-layout/classic-fixed    0 $M/manifest_osrs239_curses.ini
$here/lane.sh $out modern164  $bin 2 gameframe-layout/modern-resizable 0 $M/manifest_osrs239_curses.ini
$here/lane.sh $out stone601   $bin 0 mobile-gameframe/stone-drawer     1 $M/manifest_osrs239_curses.ini
$here/lane.sh $out dat1_254   $bin 0 auto                              0 $M/manifest_rs254lc.ini --offline

for t in native548 classic548 classic161 modern164 stone601 dat1_254; do
  printf "%-12s exit=%s bounds=%s roles=%s owned=%s\n" $t \
    "$(cat $out/$t/exit-status)" \
    "$(grep -c '^BOUNDS' $out/$t/log.txt)" \
    "$(grep -c '^ROLE_WIDGET' $out/$t/log.txt)" \
    "$(grep -c '^OWNED_WIDGET' $out/$t/log.txt)"
done
