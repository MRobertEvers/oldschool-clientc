# Fight Arena parity brief

Status: `lostcity-parity` -- the port follows LostCity leg by leg (stage writers, spawns, gates,
dialogue), with the OSRS-era form winning where the Wiki says the quest changed. Every leg below
is driven through the real client by a scratch driver (108 ledger rows, 0 fail).

## 1. References (pinned)

| Reference | Pinned revision | Use |
| --- | --- | --- |
| [Fight Arena](https://oldschool.runescape.wiki/w/Fight_Arena?oldid=15240956) | 15240956 | Requirements, rewards, fights, the 2023-01 change |
| [Fight Arena/Quick guide](https://oldschool.runescape.wiki/w/Fight_Arena/Quick_guide?oldid=14886724) | 14886724 | Ordered interactions |
| [Transcript:Fight Arena](https://oldschool.runescape.wiki/w/Transcript:Fight_Arena?oldid=15263253) | 15263253 | Dialogue branches |
| LostCity `scripts/quests/quest_arena/` | local checkout | Stage writers, jm2 spawns, disguise branches |

## 2. Stages (`arenaquest`, transmit=yes scope=perm)

0 not_started, 1 started, 2 obtained_armour, 3 spoken_drunkguard, 5 given_khali_brew, 6 entered_ogre_fight,
8 defeated_ogre, 9 sent_jail, 10 defeated_scorpion, 11 defeated_bouncer, 12 freed_servils,
13 defeated_genkhazard, 14 complete, 15 complete_defeated_genkhazard.

Rewards: 12,175 Attack XP, 2,175 Thieving XP, 1,000 coins, the Khazard armour set, 2 quest points.

## 3. Route as ported

1. Lady Servil (2566,3199) starts the quest.
2. Chest in the north-east house (door `poordoor`): the SHUT chest carries Search; the open chest has no op.
3. Wear the Khazard helmet and platemail. `~wearing_khazard_armour` gates the guard disguise lines
   (fight slave, spectator, Hengrad, Sammy) and door 1.
4. Drunk guard (2615,3141) -> Khali brew from the barman -> guard bribed, cell keys.
5. Cell gate (`arena_jeremydoor`): op1 says "The cell gate is securely locked."; the keys used on it free Sammy.
6. Fights: ogre, scorpion (after Hengrad's jail cell), Bouncer, then the optional General.
7. Lady Servil completes it (stage 14, or 15 if the General fell).

## 4. LostCity legs closed by this pass

- Spawns moved to LC's m40_49.jm2 coordinates: guard2, both Sammys, Justin, three plain fight slaves, spectator.
- Door 1 (`fightarena_door1`): armour worn between obtained_armour and defeated_ogre lets you through
  ("Nice observation guard..."); otherwise "This door appears to be locked." plus the guard1 lines by stage.
- Cell door op1 is only the locked message; the key use frees Sammy.
- Disguise branches for fight slaves (Kelvin/Joe and plain), spectator, Hengrad, Sammy.
- The General fight works: see section 6.
- Ogre/scorpion/Bouncer/General death handlers queue their `arena_defeat_*` (a `@label;` is a jump,
  so `arena_spawn_general` and `arena_enter_current_round` became procs).
- Jail telejump lands inside the cell with Hengrad (40_6).

## 5. Deliberate differences

| Detail | LostCity | Port | Why |
| --- | --- | --- | --- |
| Fights | shared npcs in the arena | owner-private `npc_add` + `npc_setowner` | contract-pinned (`tools/check_quest_combat_contract.py`) |
| Return to the cell between fights | yes | no | OSRS 2023-01 change wins |
| Cages / gate loc animations | scripted | skipped, fighters `npc_add`ed in the ring | private encounters |
| Barman Sons of the Twilight line | n/a | soft-skip | era |
| General attack option | multinpc | `npc_changetype` to `general_khazard` | see below |

## 6. The General has no Attack in the cache

The client draws npc types from `cache.osrs239`. There `general_khazard_arena` is a multivarp with only
the ops-less `general_khazard_vis` forms, and a `multinpc` override in a server `.npc` file never reaches the
client. So `arena_general_engage` (arena_encounter.rs2) does `npc_changetype(general_khazard, 32000)`, the
cache's own attackable record (op2=Attack, level 142), then `npc_setmode(opplayer2)`. A server overlay
`[general_khazard]` in `quest_arena.npc` carries the arena stats (maxrange 25, 170 hitpoints). Both
`[ai_queue3,general_khazard_arena]` and `[ai_queue3,general_khazard]` share `@arena_general_death`.

## 7. Proof

- `::arenarun` (`arena_selftest.rs2`) and the C stanza in `torirs_server_world_selftest.c`: ogre kill to
  sent_jail, General kill to defeated_genkhazard, disguise proc.
- Scratch quest-gate driver: full route to stage 15 with the General fight, door 1 both ways, cell gate message.

## 8. Open

- Justin's cell-side legs and the cage animations are not modelled (private encounters).
