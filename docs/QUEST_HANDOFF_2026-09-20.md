# Quest suite handoff, 2026-09-20 evening

State of the tier 1 push at the pause. Read this before touching anything under
test/quests, script/plugins/quest_driver or the quest content.

## Where things stand

- Tier 1: 19 of 39 quests truly green (gate.py green bucket over a full run).
  QUEUE.tsv said 25 until today: six rows (blackarmgang, makinghistory, misc,
  prince, seaslug, sheepherder) were marked green while their files end on a
  t.blocked row. They are relabelled, and queue.py now refuses `set --status
  green` for a file that calls t.blocked(.
- The 2026-09-20 seam pass (tools/quest_gate/seam_fixer.workflow.js, triage in
  docs/QUEST_SEAM_TRIAGE_2026-09-20.md) fixed 15 seams, each proved alone by its
  fixer. Its edits are committed as WIP: OSRS-Content 4420b02611 (content) and
  this commit (driver Lua, conformance rows, docs, queue tool). They were
  never proved AS A SET; the first full suite over them found:
  - fluffs (Gertrude's Cat) 53/53 -> 41/16: gertrude.rs2's new Two Cats window
    test runs before the %fluffs ladder. Rework so a fresh character keeps the
    old dialogue and Two Cats still reaches its topic afterwards.
  - scorpcatcher 34/34 -> 31/3: use_on(cage, scorpion npc) no longer catches;
    the pointer.lua far-side retry / step-off rewrite is the suspect.
  - sheepherder 19/0/1 -> 18/1/1: halgrive.feed_granted, poisoned_feed count 0
    after the accept; reproduced alone, so likely the chat.lua readiness wait.
  - conformance 110/112 alone: seam.press_pixel (tree covered from every pose,
    same pointer.lua suspect) and seam.npc_shared_tile (the fixer's own new row:
    duke_of_lumbridge not in the pool after goto_tile; rewrite the row).
- murder (Murder Mystery) is GREEN on disk, 95/95 with the scroll shot, in
  test/quests/murder.lua (committed here as WIP); its queue row still says
  blocked. An author batch reviewer should adopt it: run, gate, commit
  "quests: murder green", set the row green.
- Harness defect reported by two fixers, unfixed: `::give` in a quest file's
  setup list answers ok and lands nothing (inside run() it works). Also two
  run.py runs of the same id share build/quest_gate/<id> and clobber each
  other. Fix both in tools/quest_gate/run.py before the next author batch.
- The batch contact sheets in test/quests/BATCHES.tsv were published under a
  previous claude.ai account and are not visible now; rebuild with
  tools/quest_gate/batch_sheet and republish.

## Resume, in order

1. `make -C src torirsserver-scripts`; `python3 tools/quest_gate/run.py --all --jobs 3 --no-publish`;
   `python3 tools/quest_gate/gate.py --allow-blocked`; `make -C src test-quest-conformance`.
   Expect the five reds above; anything else is new.
2. One Opus agent per red (files: gertrude.rs2 / pointer.lua / chat.lua /
   test/quests/_conformance.lua), each proving its quest AND the seam fix it
   shares a file with (fishingcompo + entertheabyss copies for pointer.lua,
   haunted copy for chat.lua, atailoftwocats copy for gertrude.rs2).
3. Full run + gate + conformance 112/112. Commit "quest-driver: seams the tier 1
   rows named", push both repos.
4. Reopen the rows the fix reports free with `queue.py set <id> --status todo
   --failure "RETRY after <sha>: ..."`: atailoftwocats, betweenarock,
   biohazard, currentaffairs, fishingcompo, hero, mourningsendparti,
   mourningsendpartii, pryingtimes, tearsofguthix, haunted, mortton,
   entertheabyss, murder. Several committed files assert the OLD bug
   (fishingcompo garlicpipe.stash_refused, mourningsendparti
   mourningGnomeRack.blocked_by_stage, tearsofguthix tog.accept_stuck rows,
   currentaffairs binding current_affairs_main instead of the stage varbit)
   and must be rewritten by the author.
5. Fix the two run.py defects above.
6. Author batch: tools/quest_gate/haiku_loop.workflow.js pasted inline, args
   {tests:[...], owner:"sonnet-b9", author_model:"sonnet"}; then its contact
   sheet; then the seam pass again; repeat until only design gaps remain.
   Never overlap a seam pass with an author batch.
7. Phase 4 (tools/quest_gate/skipboss.workflow.js) unblocks tier 2.

New seams the pass uncovered, for the next triage: mourningsendpartii's >255
byte mes at mend2_shared.rs2:97 desyncs the session; betweenarock's
p_teleport into the Arzinian realm is a no-op; atailoftwocats' Reldo spawn
carries the base symbol so [opnpc1,reldo_normal] is dead; entertheabyss'
essence teleport lands after its dialogue closes and overtakes the next goto;
mortton needs a shop-purchase verb; haunted's _settle_after_click has no arm for
the player's tile changing (a teleporting door grades as timeout).
