# Desert Treasure I: the Stranger

The assassin (npc 685, cache symbol `fourdiamonds_assasin`, "Stranger") who comes for a player
carrying a Diamond of Azzanadra. Content: `quest_deserttreasure/scripts/dt_stranger.rs2`, his
block in `configs/deserttreasure.npc`, `configs/dt_stranger.varn` (npc vars 84..87). Test:
`test/quests/_dt_stranger.lua`.

The first rebuild (2026-11-21) left him out. Index 4628 had recorded him the whole time, from
[2962] to [3170], at the Grand Exchange between the ice and blood sections. No section's window
covered that stretch, and nothing listed the npc types the sessions added. The only trace in our
tree was two unread constants (`^dt_stranger_interval`, `^dt_stranger_odds`). See "Why it was
missed" at the end.

## Sources

| source | what it shows |
|---|---|
| 4628 [1680] | Kamil dies (`fd_icewarrior_dead`) |
| 4628 [2962..3170] | the recorder's own Stranger: the add, the Ice line, 90 ticks following other players, the fight, the death, the flag and the page |
| 413 [892..1161] (rev 231) | another player's Stranger: the Blood line on the add, ~220 ticks following, then a fight with a player (hits seen as `damage_other`) |
| 352, 443, 700, 2246, 2594, 4244 | ownerless Strangers at the Grand Exchange following one player after another; 4244 sees one live 600+ ticks |
| wiki `Stranger` | the timers (quoting Mod Ash), the restrictions, "follow (but not attack) the player closest to him", 115% max, poisoned dagger, "He has no interest in fighting you." / "He's not after you..." |
| wiki `Transcript:Stranger` | the four lines, one per diamond |

## When he comes

One attempt per event, after a wait drawn from that event's range (Mod Ash: "1.5 - 2.0 mins after
killing the fire warrior or blood warrior. 5-10 mins after the troll child or Malak returns a lost
one, or after using the mirrors. 10-15 mins after killing the ice warrior.").

| event | ours (call site) | wait (ticks) |
|---|---|---|
| Fareed dies | `dt_smoke.rs2` `[queue,dt_smoke_fareed_slain]` | 150..200 |
| Dessous dies | `dt_blood.rs2` `~dt_dessous_killed` | 150..200 |
| Kamil dies | `dt_kamil.rs2` `[queue,dt_kamil_dead]` | 1000..1500 |
| the troll child hands the diamond over (first time, or a lost one) | `dt_ice.rs2` `~dt_ice_child_gives_diamond` and the "I lost that diamond" branch | 500..1000 |
| Malak returns a lost Blood diamond | `dt_blood.rs2` `~dt_malak_done` | 500..1000 |
| a look in a scrying mirror | `dt_start.rs2` `~dt_mirror_look` | 500..1000 |

Recorded once: Kamil dead at [1680], the Stranger at [2962]. That is 1,282 ticks, inside
1000..1500. The add came on the tick the Grand Exchange closed ([2962], `busy` 1 -> 0). So the
attempt is a normal `queue`, which waits out a modal: our engine gates it the same way
(`player_can_access`).

An attempt does nothing, and is never retried, if any of these holds:

- the Stranger was killed before (`fd_killed_ninja`, varbit 375, saved: varp 442 is `perm`);
- the pack holds no diamond;
- the player is off level 0, underground (z or x >= 6400), or within 16 tiles of Kamil's spawn
  or 10 of Dessous's tomb (the wiki's "boss areas"; the radii are ours);
- the roll fails.

The roll is 1 in 2 (`^dt_stranger_odds`, UNVERIFIED). No source gives the chance, and the
recordings hold one attempt that could spawn him (it did).

## The add (4628 [2962], 413 [892])

| | recorded | ours |
|---|---|---|
| tile | 2 from the owner ((3167,3486) by (3165,3487)) | `map_findsquare(coord, 1, 2, lineofwalk)`; runs: 2 from the owner |
| spawn angle | south (2/2 fresh adds) | `npc_facedir` south |
| line | `[say]` on the add tick: Ice "That Ice Diamond is mine, thief!" (4628), Blood "You are not worthy of holding that Diamond of Blood!" (413) | the same; Smoke "Hand over the Diamond of Smoke, or suffer!" and Shadow "The Diamond of Shadow is not yours to take!" from `Transcript:Stranger` |
| which line | the event's diamond (Kamil -> Ice) | the event's diamond if it is in the pack, else the first held of Smoke, Shadow, Ice, Blood (UNVERIFIED) |

## Who he goes for

| tick | recorded (4628) | rule |
|---|---|---|
| A+1 [2963] | the owner clicked a banker that tick; the bank opened. The Stranger steps toward and faces shahzain4734, 5 tiles off; the owner was 2 | his owner is checked once, on his first tick. A free owner is attacked (`npc_attackplayer`). A busy one (a modal up) is given up for good |
| [2963..3052] | follows shahzain4734 to [3011], then Samsvt76. Never swings, including the bank-free windows [2979..2995] (16 ticks) and [3006..3015] (9 ticks) with the owner 2-3 tiles off | follows the closest player but his owner within 10 tiles (`playerfollow`), keeps one while they stay within 10, stands when nobody is near |
| [3053] | the owner shoots him; first swing [3058] | the engine's retaliation takes the hitter; from then the engine's attack clock owns him |

The six ownerless sessions show the same following behaviour. In 4244 one followed AGA-XP, then
Akribis pure, for 600+ ticks.

## His attack (4628 [3058..3166]; 413 [1115..1154])

| | recorded | ours |
|---|---|---|
| cadence | every 4 ticks, 28/28 swing gaps, through the recorder's eats | engine clock, `attackrate` 4; gaps 4 on every run after the fix below |
| swing | `human_ddagger_hack` (377) + synth `stabsword_slash` (2548), splat on the same tick | the same |
| special | `puncture` (1062) + `sp_attack_puncture_spotanim` (252) height 96 + synth `puncture` (2537), two splats on the tick, no slash synth; 5 of 28 (4628), 3 of 10 (413) | 1 in 5 (`^dt_stranger_spec_odds`) |
| damage | slash; under Protect from Melee 13/13 zero; unprayed 4 hits of 22 swings, 2, 7, 11, 7 | slash, 115% of the target's own max per swing (the wiki; the four values do not decide it) |
| poison | none of the 4 landed hits poisoned | 1 in 4 on a landed hit, severity 30 through `~npc_poison_player` (UNVERIFIED: the wiki says poisonous) |
| his block | `human_ddagger_block` (378) on the tick before each splat on him, `swordclash4_mail` (18) with every splat (31/31) | the same (`defend_anim`, `defend_sound`) |
| levels | | the cache's: attack 90, strength 80, defence 80, hitpoints 80; stab 40 / slash 25 attack; stab 81 / slash 93 / crush 98 / magic 1 defence; ranged 82 the wiki's |

The generated block had him punching (`human_unarmedpunch`), silent, crush, and wandering 5
tiles. All of that is replaced.

## His death (4628 [3167..3170])

| | recorded | ours (runs H..N) |
|---|---|---|
| K | the last splat [3167] | |
| K+1 | `human_death` + synth `human_death` (512), ops `0b00000`, a face reset [3168] | death seq K+1 (e.g. 100, 109, 180) |
| K+3 | the del, `fd_killed_ninja` 0 -> 1, the player's page "I wonder what that was all about?" (chatcon1, `^chat_confused`) [3170] | del and flag on the same tick (102/102, 111/111, 182/182) |
| drop | none | none |

## Only his owner can fight him (wiki, UNVERIFIED)

`[opnpc2]`/`[apnpc2]`: "He has no interest in fighting you." A spell (`~pvm_combat_spell_checks`):
"He's not after you...". No recording has someone else attack one.

## Tests

`_dt_stranger.lua` (scriptrun, private pack and server build): 11 rows.

- **busy**: the bank open on his first tick; then 20 ticks with no swing beside the owner.
- **fight**: he swings once attacked; the kill; the flag and the page.
- **rules**: no Stranger after the kill, with no diamond, on level 1, or underground; one on the
  ground with the Smoke diamond.
- **free**: an owner with nothing open is attacked.

Runs H, I, J, K2, L, M, N pass. Earlier runs C, G and K died at 99 hitpoints without prayer: his
max is 115% of a 99-Strength scimitar's, twice on a special. The test now prays Protect from
Melee, as the recorder did from [3129].

`deserttreasure.lua` (the whole quest) with the attempts wired in: 447/447 on one roll. That run's
artefacts do not show whether a Stranger came.

Two faults the runs found and fixed:

- **The flag a tick late.** A `queue(.., 1)` from `[ai_queue3]` landed a tick after the del;
  it is `0` now.
- **A face reset after every swing, and 7-tick gaps.** When nobody else was near, the follow
  state's `npc_setmode(none)` cut the fight the engine had latched. Now a held target (`npc_hastarget`)
  is left alone, and only a `playerfollow` is dropped.

## Open

- **The chance per attempt** (1 in 2) and the boss-area radii: no source.
- **Which line an event brings when its own diamond is banked** and another is carried.
- **Poison**: wiki only.
- **Someone else attacking him**: the two refusals are the wiki's.
- **A target lost mid-fight**: he follows again; unrecorded.
- **An attempt pending at logout** is lost (our queues do not survive a logout); unrecorded either
  way.
- **The player's block with a scimitar was `human_unarmedblock`**: fixed 2026-11-23 for every
  weapon and shield, from the recordings (`tools/port_weapon_defend.py`,
  `skill_combat/configs/bas/defend_anims_recorded.obj`); the scimitar now blocks with
  `human_sword_def` (388) on all 26 of the Stranger's hits (run strangerBlk), as at 4628 [3082].
- **The pyramid's mummies** ("Rawr!", the emerge seq): fixed 2026-11-23; see
  `desert_treasure_i_pyramid.md`.

## Why it was missed

The port worked from sections: one per boss, scene and area, each a window of ticks. The Stranger
fell between sections (GE shopping between the ice and blood hunts). His npc type was never in a
section's `--match`, and step 1's action inventory lists the recorder's clicks, not the npcs the
server adds.

The cache inventory would have caught him. At the rebuild's commit his only mention was the
generated stats block, which `content_asset_inventory.py` does not count as use, so
`--names 'fourdiamonds|deserttreasure'` prints `UNUSED 685 fourdiamonds_assasin`. It was never
run: the skill required it only for LostCity ports (§16), and this quest came from a
2009scape/wiki port.

The rsprox-content skill now requires three things for every port:

- the inventory, whatever the port came from;
- a per-session list of every npc type the server added, each with a verdict;
- a grep for declared-but-unread names.
