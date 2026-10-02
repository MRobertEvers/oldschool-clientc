#!/bin/bash
# Two machines, one origin: proves tools/quest_gate/claim.py's batch-branch flow.
# Usage: tools/quest_gate/claim_test/run.sh [claim.py under test] [work dir]
# Builds two bare remotes (parent + OSRS-Content-like submodule), clones A, B, C,
# a fake gh (bin/gh) and a fake pack rebuild (bin/fake_rebuild), and drives the
# batch lifecycle of docs/QUEST_ORCHESTRATOR.md end to end. Read the transcript.
set -u
T=$(cd "$(dirname "$0")" && pwd)
CLAIM_SRC=${1:-$T/../claim.py}
SRC_DIR=$(dirname "$CLAIM_SRC")
W=${2:-$(mktemp -d -t claim_test)}
rm -rf "$W"; mkdir -p "$W"
export PATH="$T/bin:$PATH" GH_SHIM_STATE=$W/gh_state.json QUEST_PACK_REBUILD="python3 $T/bin/fake_rebuild"
export GIT_CONFIG_COUNT=2 GIT_CONFIG_KEY_0=protocol.file.allow GIT_CONFIG_VALUE_0=always \
       GIT_CONFIG_KEY_1=advice.detachedHead GIT_CONFIG_VALUE_1=false
export GIT_AUTHOR_NAME=test GIT_AUTHOR_EMAIL=t@t GIT_COMMITTER_NAME=test GIT_COMMITTER_EMAIL=t@t
q() { git -C "$1" -c core.pager=cat "${@:2}"; }
hdr() { echo; echo "=================== $*"; }
run() { echo "\$ [$1] ${*:2}"; (cd "$W/$1" && QUEST_HOST=mac$1 "${@:2}"); echo "(exit $?)"; }
CLAIM="python3 tools/quest_gate/claim.py"
row() { git -C "$W/$1" show "$2:test/quests/QUEUE.tsv" | awk -F'\t' -v id="$3" '$2==id {printf "    %-3s %-8s %-14s claimed_at=%s last_failure=%s\n", $2, $6, $7, $9, $8}'; }
setrow() { (cd "$W/$1" && python3 - "$2" "$3" "$4" <<'EOF'
import sys; sys.path.insert(0, "tools/quest_gate"); import queue as q
from pathlib import Path
rows = q.load_rows(Path("test/quests/QUEUE.tsv")); r = q.find_row(rows, sys.argv[1])
r["status"], r["last_failure"], r["claimed_at"], r["claim_prev"] = sys.argv[2], sys.argv[3], "", ""
q.write_rows(Path("test/quests/QUEUE.tsv"), rows)
EOF
); }

hdr "seed: bare remotes parent.git + content.git, v3 in both"
git init -q --bare "$W/parent.git"; git init -q --bare "$W/content.git"
git -C "$W/parent.git" symbolic-ref HEAD refs/heads/v3; git -C "$W/content.git" symbolic-ref HEAD refs/heads/v3
S=$W/seed_content; git init -q -b v3 "$S"; mkdir -p "$S/osrs239-content/pack" "$S/osrs239-content/server/scripts/quests/q/configs"
printf '// header\n// --- allocated below this line by tools/ss_allocate.py; do not hand-edit ---\n7211=varp7211_viking_lock_4\n7212=varp7212_viking_name\n' > "$S/osrs239-content/pack/varp.alloc"
printf '// header\n// --- allocated below this line by tools/ss_allocate.py; do not hand-edit ---\n9000=seed_row\n' > "$S/osrs239-content/pack/dbrow.alloc"
printf '[varp7212_viking_name]\n' > "$S/osrs239-content/server/scripts/quests/q/configs/q.varp"
printf '[seed_row]\n' > "$S/osrs239-content/server/scripts/quests/q/configs/q.dbrow"
q "$S" add -A && q "$S" commit -qm "content seed" && q "$S" push -q "$W/content.git" v3
P=$W/seed_parent; git init -q -b v3 "$P"; mkdir -p "$P/tools/quest_gate" "$P/test/quests"
cp "$CLAIM_SRC" "$SRC_DIR/queue.py" "$SRC_DIR/ledger.py" "$P/tools/quest_gate/"
printf 'build/\n__pycache__/\n' > "$P/.gitignore"
{ printf 'quest_dir\ttest_id\thelper_dir\thelper_file\ttier\tstatus\towner\tlast_failure\tclaimed_at\tclaim_prev\n'
  for i in 1 2 3 4 5 6 7 8 9 10; do printf 'quest_q%s\tq%s\tq%s\t\t1\ttodo\t\t\t\t\n' $i $i $i; done; } > "$P/test/quests/QUEUE.tsv"
printf 'batch\tdate\tauthor_model\ttests\tgreen\tblocked\tcontent_bug\trejected\tartifact\n' > "$P/test/quests/BATCHES.tsv"
printf 'quest_dir\ttest_id\tsource\tstatus\tsha\tlegs_left\tnotes\n' > "$P/tools/quest_gate/PARITY.tsv"
: > "$P/test/quests/CONTENT_LOCK"
q "$P" submodule add -q -b v3 "$W/content.git" OSRS-Content
q "$P" add -A && q "$P" commit -qm "parent seed" && q "$P" push -q "$W/parent.git" v3
for m in A B C; do
  git clone -q -b v3 "$W/parent.git" "$W/$m" && q "$W/$m" submodule update -q --init
  q "$W/$m/OSRS-Content" checkout -q v3
done
echo "remote v3: $(q "$W/parent.git" log --oneline -1 v3)"

hdr "1. A starts batch macA-b1 on a branch and claims q1 q2 q3 (written to v3 through build/v3_coord)"
run A $CLAIM start macA-b1 q1 q2 q3
echo "A parent branch: $(q $W/A branch --show-current); A content branch: $(q $W/A/OSRS-Content branch --show-current)"
echo "remote v3 now: $(q "$W/parent.git" log --oneline -1 v3)"
echo "coord worktree: $(q $W/A worktree list | grep v3_coord)"; echo "coord files: $(cd $W/A/build/v3_coord && find . -type f -not -path "./.git" | tr "\n" " ")"

hdr "2. B fetches: A's claims are visible on v3"
run B $CLAIM status

hdr "3. B starts macB-b1 asking for q3 q4 q5: q3 is A's and is dropped"
run B $CLAIM start macB-b1 q3 q4 q5
hdr "3b. B asks for A's q1 by name: refused (exit 3)"
run B $CLAIM batch macB-b1 q1

hdr "3c. the cards' gate: status --batch on B's branch (ok), with A's quest (exit 3), on the wrong branch (exit 2)"
run B $CLAIM status --batch macB-b1 --require q4 q5 | tail -2
run B $CLAIM status --batch macB-b1 --require q4 q1 | tail -2
run A $CLAIM status --batch macB-b1 --require q4 | tail -2

hdr "4. A works on its branch: content (varp 7213=a_thing, dbrow a_row), a test, branch QUEUE q1 green, q2 blocked"
CA=$W/A/OSRS-Content/osrs239-content
echo '7213=varp7213_a_thing' >> $CA/pack/varp.alloc; echo '9001=a_row' >> $CA/pack/dbrow.alloc
printf '[varp7213_a_thing]\n' > $CA/server/scripts/quests/q/configs/a.varp; printf '[a_row]\n' > $CA/server/scripts/quests/q/configs/a.dbrow
q $W/A/OSRS-Content add osrs239-content && q $W/A/OSRS-Content commit -qm "content: a_thing, a_row"
echo 'return { varp = "varp7213_a_thing" }' > $W/A/test/quests/q1.lua
setrow A q1 green ""; setrow A q2 blocked "seam: the door in leg 2"
q $W/A add OSRS-Content test/quests/q1.lua test/quests/QUEUE.tsv && q $W/A commit -qm "quests: q1 green, q2 blocked [macA-b1]"
run A $CLAIM pr-prepare macA-b1 --push
echo "A's branch QUEUE:"; row A HEAD q1; row A HEAD q2
echo "v3 QUEUE:";        row A origin/v3 q1; row A origin/v3 q2

hdr "5. B works too: content (varp 7213=b_thing -- the SAME id -- and dbrow 9001=b_row), q4.lua uses varp7213_b_thing; q4 green"
CB=$W/B/OSRS-Content/osrs239-content
echo '7213=varp7213_b_thing' >> $CB/pack/varp.alloc; echo '9001=b_row' >> $CB/pack/dbrow.alloc
printf '[varp7213_b_thing]\n' > $CB/server/scripts/quests/q/configs/b.varp; printf '[b_row]\n' > $CB/server/scripts/quests/q/configs/b.dbrow
printf '%%varp7213_b_thing = 1\n' > $CB/server/scripts/quests/q/b.rs2
q $W/B/OSRS-Content add osrs239-content && q $W/B/OSRS-Content commit -qm "content: b_thing, b_row"
echo 'return { varp = "varp7213_b_thing" }' > $W/B/test/quests/q4.lua
setrow B q4 green ""
q $W/B add OSRS-Content test/quests/q4.lua test/quests/QUEUE.tsv && q $W/B commit -qm "quests: q4 green [macB-b1]"
run B $CLAIM pr-prepare macB-b1 --push

hdr "6. A is done: two PRs (content first), v3 rows carry the PR note, q3 (no verdict) released on the branch"
run A $CLAIM done macA-b1
echo "gh calls:"; sed 's/^/    /' $GH_SHIM_STATE.log
echo "v3 QUEUE after done:"; for i in 1 2 3; do row A origin/v3 q$i; done
echo "parent PR #1 body:"; python3 -c "import json;print(json.load(open('$GH_SHIM_STATE'))['parent']['1']['body'])" | sed 's/^/    | /'

hdr "7. the owner merges A's PRs (content first, merge commits) -- simulated in clone M; both must be conflict-free"
git clone -q -b v3 "$W/content.git" "$W/Mc"; git clone -q -b v3 "$W/parent.git" "$W/Mp"
q $W/Mc merge --no-ff --no-edit origin/macA-b1 >/dev/null && q $W/Mc push -q origin v3 && echo "content PR #1 merged clean"
gh shim-set content 1 MERGED
echo "-- only the content PR is merged: the batch is still pending"
run B $CLAIM pr-sync
q $W/Mp merge --no-ff --no-edit origin/macA-b1 >/dev/null && q $W/Mp push -q origin v3 && echo "parent PR #1 merged clean"
gh shim-set parent 1 MERGED
echo "-- both merged: the merge brought the branch's rows, nothing is left claimed"
run B $CLAIM pr-sync
echo "v3 QUEUE after the merge (as B sees it):"; q $W/B fetch -q; for i in 1 2 3; do row B origin/v3 q$i; done
run B $CLAIM status

hdr "8. A starts macA-b2 (q6 q7), then releases q7 early -- B claims it"
q $W/A/OSRS-Content fetch -q; q $W/A fetch -q
run A $CLAIM start macA-b2 q6 q7
run A $CLAIM release macA-b2 q7 --note "macA-b2 cannot finish q7: needs a seam pass"
run B $CLAIM batch macB-b1 q7
row B origin/v3 q7

hdr "9. B is done AFTER A merged: pr-prepare meets the alloc conflict -- v3's copy, varp renumbered, dbrow rebuilt"
run B $CLAIM done macB-b1
echo "B content branch varp.alloc tail:"; q $W/B/OSRS-Content show HEAD:osrs239-content/pack/varp.alloc | tail -3 | sed 's/^/    /'
echo "B content branch dbrow.alloc tail:"; q $W/B/OSRS-Content show HEAD:osrs239-content/pack/dbrow.alloc | tail -3 | sed 's/^/    /'
echo "renamed:"; q $W/B/OSRS-Content grep -n 'b_thing' HEAD -- osrs239-content | sed 's/^/    /'; q $W/B grep -n b_thing HEAD -- test | sed 's/^/    /'
q $W/Mc fetch -q; q $W/Mp fetch -q
q $W/Mc merge --no-ff --no-edit origin/macB-b1 >/dev/null && q $W/Mc push -q origin v3 && echo "content PR #2 (macB-b1) merges clean onto A's"
q $W/Mp merge --no-ff --no-edit origin/macB-b1 >/dev/null && q $W/Mp push -q origin v3 && echo "parent PR #2 (macB-b1) merges clean onto A's"
echo "v3 gitlink is a commit on content v3: $(q $W/Mp ls-tree v3 OSRS-Content | awk '{print $3}' | xargs -I{} git -C $W/Mc merge-base --is-ancestor {} origin/v3 && echo yes)"
gh shim-set content 2 MERGED; gh shim-set parent 2 MERGED

hdr "10. macA-b2 is done, its PR is closed unmerged: pr-sync releases q6"
setrow A q6 green ""; q $W/A add test/quests/QUEUE.tsv && q $W/A commit -qm "quests: q6 green [macA-b2]"
run A $CLAIM done macA-b2
gh shim-set parent 3 CLOSED
run B $CLAIM pr-sync
q $W/B fetch -q; row B origin/v3 q6

hdr "10b. a PR reported merged while its rows are still claimed on v3 (merged from a stale branch): pr-sync releases them"
q $W/A fetch -q; q $W/A/OSRS-Content fetch -q
run A $CLAIM start macA-b3 q9
run A $CLAIM done macA-b3
gh shim-set parent 4 MERGED
run B $CLAIM pr-sync
q $W/B fetch -q; row B origin/v3 q9

hdr "10c. a race: A and B claim q10 at the same moment -- exactly one holds it"
q $W/B fetch -q
( cd $W/A && QUEST_HOST=macA $CLAIM batch macA-b3 q10 > $W/raceA.txt 2>&1; echo "A exit $?" >> $W/raceA.txt ) &
( cd $W/B && QUEST_HOST=macB $CLAIM batch macB-b1 q10 > $W/raceB.txt 2>&1; echo "B exit $?" >> $W/raceB.txt ) &
wait
sed 's/^/    A| /' $W/raceA.txt; sed 's/^/    B| /' $W/raceB.txt
q $W/B fetch -q; row B origin/v3 q10

hdr "11. on v3, claim.py behaves as before: C claims q8 in its own checkout, no coordination worktree"
q $W/C fetch -q && q $W/C merge -q --ff-only origin/v3   # the launch checklist: level first
run C $CLAIM batch macC-b1 q8
echo "C HEAD: $(q $W/C log --oneline -1) ; C branch $(q $W/C branch --show-current); build/v3_coord exists: $([ -d $W/C/build/v3_coord ] && echo yes || echo no)"
hdr "11b. a batch branch whose name is not <host>-<batch> is refused"
q $W/C switch -q -c wrongname
run C $CLAIM batch macC-b1 q9
q $W/C switch -q v3

hdr "final: v3 QUEUE"
q $W/Mp fetch -q; git -C $W/Mp show origin/v3:test/quests/QUEUE.tsv | awk -F'\t' '{printf "    %-8s %-8s %-14s %s\n", $2, $6, $7, $8}'
