#!/bin/zsh
# shot_manifests.sh [worktree] [outdir]
#
# shot.sh needs manifests whose cache/content lines point at the DATA checkout
# and whose revconfig lines point at the tree under test. This is capture_set.sh's
# split, and it exists for the same reason: the checked-in manifests use paths
# relative to the checkout root, which resolve against the data tree once the
# client has cd'd there -- so a worktree's revconfig edits would silently not be
# under test, and a BEFORE shot would be taken with the AFTER tree's revconfig.
#
# It was referenced by shot.sh and never committed, so a fresh checkout could
# not take a single shot -- the first agent to try had to write it from
# scratch. Generate one set per tree you photograph:
#   shot_manifests.sh <after worktree>  <shots dir>/manifests
#   shot_manifests.sh <before worktree> <shots dir>/manifests.before
set -u

here=${0:A:h}
repo=${TORIRS_SHOT_REPO:-/Users/matthewevers/Documents/git_repos/3draster}
worktree=${1:-${here:h:h:h}}; worktree=${worktree:A}
out=${2:-$here/manifests}; mkdir -p $out; out=${out:A}

for m in manifest_osrs239_curses manifest_rs254lc; do
  python3 - "$repo/manifests/$m.ini" "$out/$m.ini" "$repo" "$worktree" <<'PY'
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
echo "manifests in $out"
