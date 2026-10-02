# In Search of Knowledge: sent back by the matthew-mbp-m4-b52 sampler

`sent_back.lua` is the reverted green file (388dc78a1, reverted in 8ea9791ec). Everything up to the
kill loop is sound: the real food feed, the accept branch ("Who are you?"), the three bookcase
searches, the tome hand-in and Logosia. The pages are the problem. It gave
4 + 4 + 4 tattered pages with `::give`, and the one real drop was left over (the temple insert
went 5->1).

The drops are live: `isok_page_drop` (insearchofknowledge_locs.rs2:166) is called from
red_dragon.rs2:10 and npc_combat.rs2:145. It rolls 1/10 on a red dragon, 1/20 on an Undead Druid
and 1/25 on a baby red dragon, inside 28_155 only, and each hit is one of the three pages at random.
Next round: drive every page for real against red dragons (expect about 150 kills, so check
`max_frames`), or have a seam pass add a sanctioned page debugproc. See
docs/quest_authoring/coverage-and-gate.md, "A `-- GUIDE-GAP:` over a `::give` of a quest drop".

## Round 2 (rejected by the reviewer)

`rejected_round2.lua` is round 2's file (71/0, gate green, coverage FULL; never committed). The
page kills are real now: `::passive` on every Forthos type and `osmumtens_fang` with the
`dragonfire_shield` (docs/quest_authoring/gaps-combat.md, "A drop hunt lands too few drops"). The
reviewer rejected it because `goto-aimeri` teleports past the knife/slash web the guide names
(`bigweb_slashable`) and `goto-logosia` leaves the dungeon by teleport. Walk from the ladder to
Aimeri, slash the web by click, and use the exit ladder.
