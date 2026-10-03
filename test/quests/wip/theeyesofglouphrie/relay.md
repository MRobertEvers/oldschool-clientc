## Kill leg (killCreature1..6): Evil Creatures DO hit back, at most 1 hp a swing (seam matthew-mbp-m4-b55-seam1 eyeglo_creatures_retaliate_vs_wiki)

- **Settled from the source. No behaviour changed.** Wiki Evil_Creature oldid 15349482
  (fetched with `api.php?action=query&prop=revisions&revids=15349482&rvslots=main`). Its infobox says
  `max hit = 1`, `attack style = Crush`, `attack speed = 4`, `aggressive = No`, `hitpoints = 1`. The page
  also lists an "Attacking" sound effect (`evil creature attack.flac`). No line on the page says they do
  not fight back. The cache agrees: `all.npc [eyeglo_fluffie_evil_N]` has `op2=Attack`, and
  `all.seq [eyeglo_fluffie_attack]` exists and is wired by `npc_anims.generated.npc`. The parity
  manifest's "retaliate off" and "a creature never swings back" were wrong, and so was the round-3
  note in gaps-combat that "the wiki says they do not fight back". All three are corrected.
- **Why the hits are at most 1.** `configs/eyeglo.npc` has `strength=1` and `param=strengthbonus,0`.
  `[proc,npc_melee_maxhit]` then gives (1 + 9) * 64 = 640, and (640 + 320) / 640 = 1.
  `check_theeyesofglouphrie` now refuses `retaliate=no` and any strength other than 1.
- **`~npc_retaliate(0)` is not the cause.** The engine starts retaliation on every hit
  (`torirs_server_combat.c` `ToriRSServer_CombatHitNpc`). The proc in each `[opnpc2,eyeglo_fluffie_N]`
  only starts the fight on the Attack click instead of on the first splat. It is kept, as the code was.
- **Measured** (scratch `build/seam_state/matthew-mbp-m4-b55-seam1/scratch/b55s1_eyeglo_c.lua`, ledger
  `build/quest_gate/b55s1_eyeglo_c`). hitpoints 40, bronze sword, attack/strength/defence 1. The six
  creatures were revealed with `::setvar` (scratch only), and hp was read with `t.skill.read` every tick
  of each fight. Results:
  - Kills took 16, 16, 8, 28, 5 and 28 ticks (creatures 6, 5, 4, 2, 3, 1).
  - The creatures swung 19 times (`[ai_opplayer2,eyeglo_fluffie_N]` count in client.log: 3/3/1/6/0/6).
  - One swing did damage: `kill1.dead` reads `hp before 40, hp after 39, drops [t+3 40->39], biggest
    single drop 1`.
  - `creatures.hit_back` PASS `hp 40 -> 39 over 6 kills`, and `kill*.maxhit1` PASS on all six. Shot
    `029-kill1.dead.png` shows the orb at 39.
  - Each npc has its own seeded RNG stream (`torirs_server_scripts.c` `script_execute`), so a fight
    replays the same rolls on every run. Runs a/b, with four kills, read 40 -> 40 throughout, although
    the creatures swung 13 times. The per-tick shots of b (`build/quest_gate/b55s1_eyeglo_b/shots/*kill6.t*`)
    show the player's health bar up from t+3, with 0-splats every 4 ticks. Round 3 of the real file read
    40 -> 35 over the six kills.
- **What the author writes.**
  - Keep `hitpoints 40` and a few lobsters in setup, and keep each kill row's measured hp before and
    after.
  - Expect the orb to drop by 0 to about 3 per kill.
  - Fix the round-3 kill comment, which says "Wiki ... says 1 hp and no retaliation". Write instead:
    `Wiki Evil_Creature oldid 15349482: 1 hp, max hit 1 Crush, speed 4 -- they hit back, at most 1 a swing`.
  - Never write "hp untouched" or "30 hp" (`30/30` is the bar width).
  - A check you may add: `hp_before - hp_after <= ceil(ticks / 4) + 1` for each kill. Never assert that
    hp is unchanged.
- **Where a copy of the round-3 file stops now.** There is no committed `test/quests/theeyesofglouphrie.lua`
  (65805d323 reverted it). A copy of `round3_rejected.lua` (`--name b55s1_eyeglo_round3copy`) ran 708
  rows, pass=673 fail=35. It stopped at row 642 `operate.rows` FAIL `row sums 12,20,2 want 12,20,58`.
  - The operate targets are random (`eyeglo_puzzle.rs2:664-715`). This roll needed 130 exchanger
    rounds (`earnDiscs` row 626), and the file's disc planner then could not lay the 58 row.
  - The stage therefore stayed 35, the creatures stayed Cute, and every `killCreatureN.attack` read
    `covered ... menu has no row` (Examine Cute Creature only).
  - This is the round-3 file's planner, not content, and not this seam. The round-3 run had targets
    16,27,59 and passed. Read `operate.rows` and the exchanger rows of
    `build/quest_gate/b55s1_eyeglo_round3copy/ledger.tsv` before you change the planner. This seam did
    not diagnose it.
