#!/bin/bash
# usage: ab_room.sh <room> <before|after>
R=/Users/matthewevers/Documents/git_repos/3draster/build/orchestrator/worktrees/raid
B=/private/tmp/claude-501/-Users-matthewevers-Documents-git-repos-3draster/4ea05e8f-ba2e-4d74-9be0-89660033ebfc/scratchpad/before_src
S=$R/build/seam_state/matthew-mbp-m4-raid-b1-seam22/trio
room=$1; which=$2; name=s22e_$room
cd $R
if [ "$which" = before ]; then
  export TORIRSSERVER_SCRIPTS=$B/out TORIRSSERVER_ALLOW_STALE_SCRIPTS=1
fi
start=$(date +%s)
python3 tools/raid_gate/run.py --script test/raids/tob_$room.lua --name $name --no-build --no-publish > $S/ab_${room}_$which.log 2>&1
rc=$?
mkdir -p $S/ab/$which
rm -rf $S/ab/$which/$room; mkdir -p $S/ab/$which/$room
cp build/quest_gate/$name/ledger.tsv $S/ab/$which/$room/ 2>/dev/null
find build/quest_gate/$name -maxdepth 2 -name ticklog.tsv -exec cp {} $S/ab/$which/$room/ \;
grep -h 'scripts loaded from' build/quest_gate/$name/client.log | cut -c1-160
echo "$room $which rc=$rc secs=$(( $(date +%s) - start ))"; tail -1 build/quest_gate/$name/ledger.tsv | cut -c1-120
