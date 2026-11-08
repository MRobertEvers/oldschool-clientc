# The Lumbridge tidy-up (3 November 2026, rev 242)

Jagex's "Quest Month Kick Off, TFA Tweaks & More" update
(oldschool.runescape.wiki `Update:Quest_Month_Kick_Off,_TFA_Tweaks_&_More`,
"Lumbridge Tidy-up") renamed, merged, removed and relocated the Lumbridge
tutors. The rev-242 cache deleted the old records (every `aide_tutor_*` and
`hatius_lumbridge_diary` has a compack name and no `all.npc` block) and added
the new types (16804..16821).

## Sessions

| rev | index | used for |
|---|---|---|
| 242.1 | 4465, 4466, 4478 | the courtyard (respawn on 3221,3221), the plane-2 bank |
| 242.1 | 4474 | the Grand Exchange's north-west corner |
| 242.1 | 4466, 4471, 4473, 4475, 4477, 4478 | Arthur, Varrock square, the Rising Sun Inn |
| 241.1 | 4435..4464 (10 sessions), 4336, 4339 | the courtyard and swamp before the update |

Jagex's npc indices are identical on every world (the cook is 26271 in 4465,
4466 and 4478), so one index is one spawn and lives pool across sessions.
An npc counts as absent only when a recorder stood within 15 tiles of its tile.

## What changed, and what we did

| npc | rev 241 | rev 242.1 | ours |
|---|---|---|---|
| Leagues tutor `league_tutor` | 3226,3215, 20/20 lives, never moved | 3155,3512 (4474, 2/2); courtyard in view, never sent | `NPC_SPAWN_RELOCATIONS` -> 3155,3512; `wanderrange` 5 -> 0 |
| Nigel `deadman_nigel` | x 3242-3243, z 3201-3203 | 13 tiles in view (4478), never sent; type is -1 off Deadman | `NPC_SPAWN_EXCLUSIONS` |
| Nigel `deadman_nigel_varrock` (new) | -- | x 3151-3152, z 3512-3513 (4474) | varrock.spawn 3152,3512, wanderrange 1 |
| Arthur `clue_hunter_arthur` (new) | `aide_tutor_clues` 3229,3236 | 3158,3469, 8/8 lives | varrock.spawn, wanderrange 0 |
| Nevet `events_shop` | 3209,3435 (4408, 4464) | 3154,3514 (4474, 2/2); square passed at 1-2 tiles, never sent | varrock.spawn moved |
| Mikasi `mikasi_lumbridge_diary` (new) | Hatius x 3233-3235, z 3213-3215 | x 3232-3234, z 3214-3216, 5 lives (4465 [35], 4466 [692], 4478 [1447]) | lumbridge.spawn 3233,3215, wanderrange 1 |
| banker `banker2_new` | `aide_tutor_banker` 3208,3222,2 | 3208,3222,2 (4466 [751]) | lumbridge.spawn |
| Ironman tutor `ironman_tutor_1` | x 3228-3230, z 3227-3229 | 6-9 tiles in view, never sent (non-Ironman recorder) | unchanged: `multinpc1=-1` hides it from non-Ironmen |

The old tutors' rows were already out of `areas/world/configs` by hand;
`gen_spawns.py` now drops any row whose record the cache deleted, so a
regeneration agrees (and the four touched squares' header counts are right again).

## Not seen by any rev-242.1 recording: UNVERIFIED

No recorder came within 15 tiles. Each is its predecessor ("renamed"/"combined")
on the predecessor's rev-241 tile, which the wiki's map marker sits beside;
`lumbridge.spawn` and `lumbridge.npc` carry the rows.

| npc | predecessor, rev 241 recorded | ours |
|---|---|---|
| Captain Harlan `captain_harlan` | three combat tutors, x 3216-3220, z 3237-3240 | 3218,3238 (wiki), wanderrange 1 |
| Wilfred `wilfred` | woodsman tutor, x 3227-3228, z 3244-3246 | 3228,3245, wanderrange 1 |
| Feoras `lumbridge_blacksmith` | smithing apprentice, x 3226-3228, z 3252-3254 | 3227,3253, wanderrange 1 |
| Cadmus `cadmus` | crafting tutor, x 3209-3211, z 3212-3213, plane 1 | 3210,3215,1 (wiki; his room was rearranged), wanderrange 1 |
| Finlay `finlay` | fishing tutor, 3245,3157, 3/3 still (4336, 4339) | 3245,3157, wanderrange 0 |
| Monlum `monlum` | mining tutor, x 3226-3228, z 3145-3147 | 3227,3146, wanderrange 1 |

## Open

- **Hatius Cosaintus** (`hatius_cosaintus`, "relocated to the Falador pub"):
  the wiki puts him on 2956,3376,0; index 4477 passed 4 tiles from it with the
  Rising Sun's barmaids sent and no Hatius. Not spawned until a recording shows him.
- **Dialogue and ops**: none of the new types has a script. Mikasi (task master),
  Harlan (Claim-weapons), Feoras (Trade: the new combat-gear shop), Wilfred,
  Cadmus, Finlay, Monlum, Arthur, and the rewritten Doomsayer / Count Check /
  Lumbridge Guide pages all need recordings that talk to them.
- **The ironman tutor is still sent to non-Ironmen** (as an invisible -1 npc);
  Jagex sends nothing.
- Recorded gaps outside the update: the Grand Exchange's emblem trader
  (3163,3504) and lost property merchant (3166,3504), recorded in rev 241 and
  242 and absent from our roster; Edgeville's emblem trader is 3096,3504 in
  14/14 lives, ours 3096,3505.
- `general/scripts/misc/tele_destinations.rs2` (generated) still sends
  `::tele hatius_cosaintus` to Lumbridge.

## Test

`test/lumbridge/lumbridge_tidy_up.lua` (scriptrun, 15 rows): the courtyard, the
plane-2 bank, the Grand Exchange and Varrock square, each stand tile checked
before its absent rows. `test/quests/childrenofthesun.lua`'s Nevet leg now goes
to the Grand Exchange.
