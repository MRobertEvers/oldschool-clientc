#!/bin/bash
# usage: mk_before_pack.sh <outdir> <file.rs2>... : compile the CURRENT content tree with the named
# minigame_tob/scripts files taken from the content repo's HEAD, into <outdir>/out, off-tree
# (a symlink farm; the shared tree is never touched).
set -e
R=/Users/matthewevers/Documents/git_repos/3draster/build/orchestrator/worktrees/raid
CT=$R/OSRS-Content/osrs239-content; SC=$CT/server/scripts
B=$1; shift
rm -rf $B; mkdir -p $B/root/server/scripts/minigames/minigame_tob/scripts $B/out
for e in $(ls -A $CT); do [ "$e" = server ] || ln -s $CT/$e $B/root/$e; done
for e in $(ls -A $CT/server); do [ "$e" = scripts ] || ln -s $CT/server/$e $B/root/server/$e; done
for e in $(ls -A $SC); do [ "$e" = minigames ] || [ "$e" = build ] || ln -s $SC/$e $B/root/server/scripts/$e; done
for e in $(ls -A $SC/minigames); do [ "$e" = minigame_tob ] || ln -s $SC/minigames/$e $B/root/server/scripts/minigames/$e; done
for e in $(ls -A $SC/minigames/minigame_tob); do [ "$e" = scripts ] || ln -s $SC/minigames/minigame_tob/$e $B/root/server/scripts/minigames/minigame_tob/$e; done
for e in $(ls -A $SC/minigames/minigame_tob/scripts); do ln -s $SC/minigames/minigame_tob/scripts/$e $B/root/server/scripts/minigames/minigame_tob/scripts/$e; done
for f in "$@"; do
  rm $B/root/server/scripts/minigames/minigame_tob/scripts/$f
  git -C $R/OSRS-Content show HEAD:osrs239-content/server/scripts/minigames/minigame_tob/scripts/$f > $B/root/server/scripts/minigames/minigame_tob/scripts/$f
done
cd $R/src && ./build_opt/sscompile --src $B/root/server/scripts --out $B/out --content-root $B/root > $B/compile.log 2>&1
tail -2 $B/compile.log | cut -c1-160
