# Level-ups (the fireworks, the lines, the box)

Owner report, 2026-10-27: level-up fireworks and messages never came through.

**Cause.** Nothing sent them. The engine fired `[advancestat,<stat>]` on every
level-up (`ToriRSServer_CombatAddXp`), but `levelup/scripts/levelup.rs2` was a
LostCity stub, "trimmed", whose one line pushed the account-summary card. A
placeholder `mes("You feel yourself getting stronger.")` was deleted in
806b37d552b and nothing replaced it. `levelup_display` (interface 233) had no
opener in the tree (QUEST_DRIVER_PLAN U16).

**Sources.** 94 sessions open interface 233 (`rsprox_index.py search interface
233`), 202 level-ups. Rev 241, normal worlds: index 4339, 4341, 4343, 4349,
4350, 4371, 4373, 4393, 4394, 4395, 4396. The single recorded 99 is index 1014
(rev 235). Sessions 208, 211, 213, 1688, 1689 and 1707 are Leagues V (rev 227).

## The recorded shape

One level-up, index 4396 [27..31] (Magic 16 -> 17, xp from a spell):

| tick | packets |
|---|---|
| X [27] | `update_stat_v2 magic` (the xp) |
| X+1 [28] | varbit `levelup_list_magic`, `flashside` 2 |
| X+2 [29] | `message_game` "Congratulations, you've just advanced your Magic level. You are now level 17."; `busy` 1; spotanim `levelup_anim` (199) height 124; `sound_area` `firework` (2396) range 1, `burst` (2384) delay 25 range 1, on the player's tile; `midi_song_v2` 147 (zero envelope); `midi_jingle`; `toplevel_chatbox_resetbackground` (2379); `if_opensub chatbox:chatmodal levelup_display`; `if_settext` text1 "Congratulations, you just advanced a Magic level.", text2 "Your Magic level is now 17.", continue "Click here to continue"; `if_sethide levelup_display:magic` false |
| X+3 [30] | `levelup_chatbox_transmit` (7781) `(4, [11883, 12027, -1, -1])`; `summary_sidepanel_combat_level_transmit` (3954) |
| C [31] | `resume_pausebutton`; `message_game` type 118 "4\|Check the skill guide for more information about Magic."; `busy` 0; `if_sethide ...:magic` true; `if_closesub` |

Rules, with the counts behind them:

- **A queued script.** It waits out an open modal: Fight Arena's scroll closed at
  index 4396 [602], the Attack box opened [603]. One drop is one box however many
  levels it crosses (Woodcutting 2 -> 13, index 4393 [652]).
- **Two drops chain**, each box on the previous one's continue: Attack [603],
  Thieving [605] (Attack's continue), combat [606] (Thieving's continue), index 4396.
- **Combat comes after every pending stat**, the tick after the continue that frees
  it when it is alone (index 4394 [2142] -> [2143], 4373 [685] -> [686], 4341
  [550] -> [551]), and it survives a stat box that an op shut at once (index 4343
  [179] Prayer opened and closed by the next altar op, combat [180]). Its text is
  lower-case "combat" in the box, "Combat" in the line; jingle `combat_level_up` (299).
- **Total level**: "Congratulations, you've reached a total level of N." after the
  stat line, at every multiple of 25 (26 distinct totals recorded, all multiples
  of 25 except the maximum, "the highest possible total level of 2376", 1014 [3754]).
- **Fireworks**: 202 of 202 below 99 as above. At 99 (1014 [3754], one recording):
  "Congratulations, you've reached the highest possible Sailing level of 99." in
  place of the line, `levelup_max` (1389), `firework` loops 2 range 3, `burst`
  delay 25 range 3, `bigbang` (2379) delay 35 range 3.
- **The jingle** (`<skill>_level_up` or `<skill>_level_up_unlocks`):
  - Magic and Prayer play `_unlocks` at every level at rev 241: 47 of 47, levels
    that unlock nothing included (Magic 12, 18; Prayer 35, 38, 41, 42). Rev 231
    (index 378) played plain `prayer_level_up` at 30.
  - Every other paired skill plays `_unlocks` exactly when the levels crossed hold
    a `skill_features` row (the skill guide, dbtable 213) for that skill: 21 of 21
    (Attack 30 and 41 yes, 33 and 38 no; Woodcutting 13 from 2 yes, 14 no; Mining
    15 yes, 18 no; Thieving 10 and 17, Crafting 6..8, Herblore 3, Smithing 29,
    Slayer 9, Defence 30, Ranged 30 yes; Cooking 4 no).
  - Split by level, from the names: Hitpoints and Strength `_2_49` / `_50_99`
    (Strength 30 recorded), Construction `_every_10th` (30 recorded; 2, 3, 4, 6,
    8 plain), Hunter `_odd` / `_even` (9 recorded), Sailing `_99` (1014). Agility
    has one jingle (28).
  - `skill_features:skill` numbers skills in the guide's order (Attack 1, Strength
    2, Ranged 3, Magic 4, Defence 5, Hitpoints 6, Prayer 7, ... Sailing 24), the
    same number 7781's first argument carries.
- `levelup_list_<skill>` is NOT the old level: it is set once at the first
  level-up of a run and stays (Prayer 10 for 11..21, index 4343).

## Ours

`levelup/scripts/levelup.rs2`; the engine passes the base level before the drop
as `last_int` (`torirs_server_combat.c`). `[advancestat,*]` queues
`[queue,levelup_display]` with the stat, the old level and the total-level
milestone (decided at the drop, so a second drop queued behind it cannot move
it). The box queues `[queue,levelup_combat]` before it pauses when the drop
raised combat; that entry waits out pending stat entries (`getqueue`) and runs
one tick after it first could, which reproduces both recorded orders on our
slot-ordered queue. Unlock levels: `levelup_unlocks.generated.dbrow`, written
by `tools/gen_levelup_unlocks.py` from `configs/all.dbrow`.

Test: `test/levelup/levelup.lua` (scriptrun, 16 of 16 rows). Wire, from its
`TORIRS_SCRIPTRUN_WIRELOG`:

| check | R | ours | verdict |
|---|---|---|---|
| line, box texts, icon unhidden / hidden, close | 4396 [29..31] | same, every box | matched |
| Cooking 1 -> 4 jingle | 33 (4339 [177]) | 33 | matched |
| Attack 1 -> 20 / Thieving 1 -> 13 jingles | `_unlocks` (rows crossed) | 30 / 68 | matched |
| Strength 1 -> 3 jingle | 65 (`_2_49`) | 65 | matched |
| song 147, then the jingle | every box | every box | matched |
| `firework` / `burst` | range 1, burst delay 25 | same | matched |
| total level 50 line after the stat line | 4395 [10629] | same | matched |
| Attack, Thieving, combat order and ticks | 4396 [603]/[605]/[606] | box on each continue's tick | matched |
| combat after a single stat | continue + 1 (4394) | continue + 1 | matched |

Also run on the new pack: `test/agility/draynor_rooftop.lua` 17 of 17 (two
Agility level-ups mid-lap, the next obstacle click shuts the box);
`test/quests/arena.lua` 92 pass, the 3 fails are scriptrun's known gaps (no
camera state, no quest list).

## Open

- **xp to box, by source.** Recorded: +2 after spell, woodcutting and the
  Draynor lap xp (4396 [27] -> [29], 4394 [752] -> [754], 4371 [154] -> [156]);
  +0 at the Chaos altar (4343 [177], xp and box in one tick). Ours: +1 for xp
  granted before the queue drain (a cheat, a dialogue reward, the lap's resumed
  script: draynor_rooftop's box opens a tick after its xp), +2 after it.
- **Op-shut stat box, then combat**: recorded the next tick (4343 [180]); ours
  one tick later (the deferral).
- **Not sent**: the skill-guide line (`message_game` type 118 "<guide id>|Check
  the skill guide ...", wording "to see what else your new X level unlocks!" when
  more than three rows were crossed) and `levelup_chatbox_transmit` (7781, the
  first three crossed rows); the `levelup_list_*` varbits and `flashside` 2;
  `chatmodal_unclamp` 0.
- **Extra**: `if_setevents levelup_display:continue` (our `if_addresumebutton`
  arms the resume with it; Jagex sends none). `summary_sidepanel_combat_level_
  transmit` goes out with the drop, Jagex's a tick after the box.
- **Unverified** (no recording): Hitpoints and Strength at 50+, Hunter even,
  Construction past 30, any 99 but Sailing's (rev 235) and its text at rev 241,
  a drop crossing two multiples of 25 (ours prints the higher), one drop raising
  combat twice (ours shows one box at the level reached).
