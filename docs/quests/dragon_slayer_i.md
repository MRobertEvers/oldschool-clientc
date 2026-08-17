# Dragon Slayer I modernization audit

Status: `audit-pending` — the quest is discoverable and much of the 2001-era
route has recognisable dialogue, map-piece, ship, maze, combat, journal, and
reward code. It is not a revision-239 implementation. The current scripts
replace the cache's 21-field `dragonquestvar` carrier with an old integer ship
repair counter, route the Guildmaster's canonical briefing through Oziach,
consume 12 rather than 90 steel nails, omit the voyage and lair scenes, have no
handler for the barrier into Elvarg's chamber, and complete the quest directly
from an unowned Elvarg death. Elvarg's head, the Oziach turn-in, Oziach's shop,
and several advertised unlocks are absent.

Audited: 2026-08-17

Governing plan: [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md). This
record applies Gates A–D to discovery, native persistence, every preparation
branch, item ownership and recovery, the Lady Lumbridge, both Crandor routes,
Elvarg, completion, rewards, shared NPCs, downstream consumers, migration,
debug tooling, and tests. It is an implementation specification, not evidence
that the quest is complete.

## 1. Authoritative references

The article and quick guide define the current route, materials, combat,
recovery, rewards, and unlocks. The transcript defines offers, re-talks,
optional branches, item hand-offs, and both cutscenes. Revisions were resolved
through the OSRS Wiki API on 2026-08-17.

| Reference | Pinned revision | Use in this audit |
| --- | --- | --- |
| [Dragon Slayer I](https://oldschool.runescape.wiki/w/Dragon_Slayer_I?oldid=15266013) | 15266013, 2026-07-17 | Identity, requirements, complete route, combat, rewards, and unlocks |
| [Dragon Slayer I/Quick guide](https://oldschool.runescape.wiki/w/Dragon_Slayer_I/Quick_guide?oldid=15266871) | 15266871, 2026-07-18 | Exact critical path, dialogue choices, items, and recovery route |
| [Transcript:Dragon Slayer I](https://oldschool.runescape.wiki/w/Transcript%3ADragon_Slayer_I?oldid=15263213) | 15263213, 2026-07-14 | Guildmaster, Oziach, shared NPCs, ship, voyage, Elvarg, and turn-in dialogue |
| [Guildmaster](https://oldschool.runescape.wiki/w/Guildmaster?oldid=14768164) | 14768164, 2024-10-13 | Start, briefing, Maze key, and replacement service |
| [Oziach](https://oldschool.runescape.wiki/w/Oziach?oldid=15230619) | 15230619, 2026-06-09 | Offer, proof checkpoint, completion, and shop |
| [Duke Horacio](https://oldschool.runescape.wiki/w/Duke_Horacio?oldid=15169395) | 15169395, 2026-04-08 | Shield dialogue and shared-NPC ownership |
| [Ned](https://oldschool.runescape.wiki/w/Ned?oldid=15154845) | 15154845, 2026-03-24 | Captain recruitment, map hand-off, and shared dialogue |
| [Klarense](https://oldschool.runescape.wiki/w/Klarense?oldid=14918006) | 14918006, 2025-06-11 | Ship purchase, repair advice, crash recovery, and post-quest dialogue |
| [Oracle](https://oldschool.runescape.wiki/w/Oracle?oldid=14899748) | 14899748, 2025-05-14 | Thalzar clue and ordinary dialogue |
| [Wormbrain](https://oldschool.runescape.wiki/w/Wormbrain?oldid=15223460) | 15223460, 2026-06-01 | Attack gate, payment route, drop, and dialogue |
| [Melzar the Mad](https://oldschool.runescape.wiki/w/Melzar_the_Mad?oldid=15199366) | 15199366, 2026-04-28 | Maze encounter and magenta-key contract |
| [Melzar's Maze](https://oldschool.runescape.wiki/w/Melzar%27s_Maze?oldid=15107284) | 15107284, 2026-01-16 | Exact monster, door, ladder, and chest sequence |
| [Maze key](https://oldschool.runescape.wiki/w/Maze_key?oldid=15279258) | 15279258, 2026-07-29 | Retention and pre-/post-quest replacement |
| [Map part (Melzar)](https://oldschool.runescape.wiki/w/Map_part_%28Melzar%29?oldid=15273572) | 15273572, 2026-07-23 | Identity, study text, combination, and source |
| [Map part (Thalzar)](https://oldschool.runescape.wiki/w/Map_part_%28Thalzar%29?oldid=15273571) | 15273571, 2026-07-23 | Identity, magic-door route, combination, and source |
| [Map part (Lozar)](https://oldschool.runescape.wiki/w/Map_part_%28Lozar%29?oldid=15273573) | 15273573, 2026-07-23 | Identity, Wormbrain routes, combination, and source |
| [Crandor map](https://oldschool.runescape.wiki/w/Crandor_map?oldid=15286733) | 15286733, 2026-08-03 | Assembly and Ned hand-off |
| [Lady Lumbridge](https://oldschool.runescape.wiki/w/Lady_Lumbridge?oldid=15292947) | 15292947, 2026-08-11 | Repairs, wreck, return-to-port behavior, and Jenkins continuity |
| [Anti-dragon shield](https://oldschool.runescape.wiki/w/Anti-dragon_shield?oldid=15288317) | 15288317, 2026-08-05 | Acquisition, protection, and duplicate/replacement behavior |
| [Antifire potion](https://oldschool.runescape.wiki/w/Antifire_potion?oldid=15185102) | 15185102, 2026-04-22 | Partial-completion use gate and protection stacking |
| [Elvarg](https://oldschool.runescape.wiki/w/Elvarg?oldid=15233362) | 15233362, 2026-06-14 | Stats, exact fire matrix, Prayer drain, weaknesses, and special exclusions |
| [Dragonfire/Elvarg](https://oldschool.runescape.wiki/w/Dragonfire/Elvarg?oldid=15001712) | 15001712, 2025-10-11 | Exact maximum-damage matrix |
| [Elvarg's head](https://oldschool.runescape.wiki/w/Elvarg%27s_head?oldid=15183427) | 15183427, 2026-04-22 | Decapitation, optional proof item, destruction, and bank cleanup |
| [Crandor](https://oldschool.runescape.wiki/w/Crandor?oldid=15293774) | 15293774, 2026-08-12 | Island access and post-quest world state |
| [Crandor and Karamja Dungeon](https://oldschool.runescape.wiki/w/Crandor_and_Karamja_Dungeon?oldid=15292322) | 15292322, 2026-08-10 | Hole, rope, shortcut, lair, and return route |
| [Corsair Cove Resource Area](https://oldschool.runescape.wiki/w/Corsair_Cove_Resource_Area?oldid=15285147) | 15285147, 2026-08-01 | Completion-gated resource-area access |

The article records a 14 January 2026 journal rewrite and a 5 November 2025
Sailing-dialogue update. The local journal and ship dialogue predate both, so
an older transcript match is not sufficient.

Transition aid only: the local Quest Helper implementation at commit
[`5ea99d5`](https://github.com/Zoinkwiz/quest-helper/tree/5ea99d5ea9ba3fb096ebe7b5ed02d80883e9819d/src/main/java/com/questhelper/helpers/quests/dragonslayer)
maps primary values 0, 1, 2, 3, 6, 7, 8, and 9; the five briefing bits; four
magic-door offering bits; three ship-repair bits; the shortcut; 23 loc types;
15 NPC types; and the route coordinates. `python3
tools/questhelper_extract.py dragonslayer --check` resolves every referenced
gameval but exits non-zero because its naming heuristic guesses the obsolete
`quest_dragonslayer` rather than cache row `quest_dragonslayer1`. That single
dbrow alias is an extractor limitation, not a missing cache row. Quest Helper
is a route/state oracle, not server behavior evidence.

## 2. Canonical contract

Dragon Slayer I is a free-to-play, experienced, medium quest released 23
September 2001. It starts with the Guildmaster in the Champions' Guild and
requires 32 quest points to start. The player must be able to defeat Elvarg,
combat level 83. The required materials are an unfired bowl, wizard's mind
bomb, lobster pot, silk, either the 10,000-coin Wormbrain route or the means to
kill and telegrab from him, a hammer, 90 steel nails, three normal planks,
2,000 coins, and combat supplies. The anti-dragon shield is obtained during
the route.

A canonical run must:

1. enforce 32 quest points at both guild access and the offer, then send the
   player from the Guildmaster to Oziach;
2. let Oziach assign Elvarg and return the player to the Guildmaster for the
   Crandor history, map, ship, and dragonfire briefing;
3. record each briefing topic independently, grant or replace the Maze key,
   and leave the three map pieces, shield, ship, and captain branches open in
   any sensible order;
4. obtain Melzar's part through the exact six-colour Maze chain, Thalzar's part
   by offering four items independently to the magic door, and Lozar's part by
   paying Wormbrain or killing and telegrabbing from him;
5. combine exactly one of each part into one Crandor map and give exactly one
   assembled map to Ned after the ship/captain prerequisites are satisfied;
6. buy the Lady Lumbridge for 2,000 coins, repair its hull three times with one
   plank and 30 steel nails per repair, and preserve all unrelated quest bits;
7. recruit Ned, board with Ned and Jenkins present, play the full voyage/attack/
   crash scene, kill Jenkins, wreck the ship, and place the player on Crandor;
8. enter the Crandor ruins, play the arrival scene, make the return rope work,
   optionally open the Karamja secret wall, and cross the lair barrier;
9. fight a player-owned Elvarg with the exact dragonfire matrix, 10% remaining
   Prayer drain, current water weakness, and correct poison, cannon, dragonbane,
   leave/re-entry, death, and reconnect behavior;
10. on the credited kill, play the decapitation scene, advance to proof state
    9, and add Elvarg's head only when space permits; and
11. complete exactly once by speaking to Oziach, whether or not the optional
    head is still held, clean up any banked head, award XP/QP once, and expose
    every promised unlock.

Completion awards 2 quest points, 18,650 Strength XP, and 18,650 Defence XP.
Starting or partially progressing the quest, rather than completing it,
unlocks dragon Slayer assignments and use of dragonfire protection. Completion
unlocks the relevant body armour, Crandor, the Corsair Cove Resource Area,
Elvarg in Nightmare Zone, and later quest/diary consumers. The current reward
contract also includes the Sailing-era ability to recruit Cabin Boy Jenkins's
ghost at level 60 Sailing.

## 3. Native identity and persistence

| Field | Native value / expected behavior |
| --- | --- |
| Quest metadata ID / packed dbrow index | 17 / 31 |
| Dbrow | `quest_dragonslayer1` |
| Type / difficulty / length | Free-to-play; experienced; medium |
| Release | 23 September 2001 |
| Start | `guildmaster` (NPC 814), native start coordinate encoded in dbrow |
| Requirement / recommendation | 32 QP; 33 Magic recommended in metadata |
| Primary field | `%dragonquest`, clean varp 176 |
| Side carrier | `dragonquestvar`, varp 177, 21 named native fields |
| Native end state | 10 |
| Reward | 2 QP; 18,650 Strength XP; 18,650 Defence XP; unlocks |

### 3.1 Native primary ladder

The cache dbrow establishes end state 10 and Quest Helper observes the route
below. Values 4 and 5 are not emitted as top-level helper steps, while values
2, 3, and 6 are all preparation states. Their exact transition meanings must
be confirmed with a real-client trace before constants are renamed.

| `%dragonquest` | Native checkpoint visible to Quest Helper | Current local meaning |
| ---: | --- | --- |
| 0 | Start at Guildmaster | Not started |
| 1 | Speak to Oziach | Spoken to Guildmaster |
| 2 | Return briefing / preparations | Spoken to Oziach |
| 3 | Preparations; ship is considered bought | Bought ship |
| 6 | Preparations continue | Unused by local constants |
| 7 | Board and speak to Ned to sail | Ship repaired locally |
| 8 | On/returning to Crandor; kill Elvarg | Map given to Ned locally |
| 9 | Return to Oziach | Sailed to Crandor locally |
| 10 | Complete | Complete |

The values are therefore not interchangeable even where the endpoints match.
In particular, current local value 9 transforms Elvarg away in the native
cache and should mean that Elvarg has already been slain, while the local
scripts use it to make Elvarg available and complete immediately on her death.

### 3.2 Native `dragonquestvar` fields

| Native field | Bits | Canonical responsibility | Current result |
| --- | ---: | --- | --- |
| `dragonslayer_secret_told` | 2 | Oracle clue learned | Separate `%dragon_oracle`; never written |
| `dragonslayer_crandor_found_secret_door` | 6 | Karamja shortcut | Separate `%dragon_wall`; never written |
| `dragonslayer_preliminary_statdown` | 7 | Elvarg preliminary stat effect | Never written |
| `dragonslayer_ship_onethird_fixed` | 8 | First hull repair | Whole carrier is set to integer 1 instead |
| `dragonslayer_ship_twothird_fixed` | 9 | Second hull repair | Whole carrier is set to integer 2 instead |
| `dragonslayer_ship_fullyfixed` | 10 | Third hull repair / hull transform | Whole carrier is set to integer 3; bit 10 remains clear |
| `dragonslayer_instructions_melzar` | 11 | Guildmaster Melzar briefing | Separate/no durable equivalent |
| `dragonslayer_instructions_oracle` | 12 | Guildmaster Thalzar briefing | Separate `%dragon_oracle` approximation |
| `dragonslayer_instructions_wormbrain` | 13 | Guildmaster Lozar briefing | Separate `%dragon_goblin` approximation |
| `dragonslayer_instructions_ship` | 14 | Guildmaster ship briefing | No durable equivalent |
| `dragonslayer_instructions_shield` | 15 | Guildmaster shield briefing | Separate `%dragon_shield` approximation |
| `dragonslayer_used_silk` | 17 | Incremental magic-door offering | Never written |
| `dragonslayer_used_bowl_unfired` | 18 | Incremental magic-door offering | Never written |
| `dragonslayer_used_lobster_pot` | 19 | Incremental magic-door offering | Never written |
| `dragonslayer_used_wizards_mind_bomb` | 20 | Incremental magic-door offering | Never written |
| `dragonslayer_asked_ned` | 21 | Captain recruitment | Separate `%dragon_ned_hired` approximation |
| `dragonslayer_ned_knows_ship_is_fixed` | 22 | Ned's ship checkpoint | No native write |
| `dragonslayer_asked_generals` | 23 | Optional Goblin Village investigation | Never written |
| `dragonslayer_champion_explained_elvarg` | 24 | Guildmaster history delivered | Never written |
| `dragonslayer_met_sawmill` | 25 | New F2P plank route | Never written |
| `dragonslayer_planks_made` | 26–29 | Up to nine quest planks | Never written |

The carrier is declared `wholewrite=allow`, and every plank repair, voyage,
and completion overwrites all 32 bits. This is a correctness and save-integrity
failure. Setting the whole carrier to 3 does not set the native fully-fixed bit
at bit 10, so the cache-authored `dragonslayer_shiphole` transform continues
to show the broken hull even while local stage logic calls the ship repaired.

### 3.3 Save migration

Migration must precede any modern handler interpreting either field. Add a
dedicated, versioned migration marker. Snapshot `%dragonquest`, the complete
raw carrier, all five authored side vars, and relevant owned quest items before
writing a native field.

For saves explicitly identified as this legacy implementation, the likely
primary conversion is 0→0, 1→1, 2→2, 3→3, 7→6, 8→7, 9→8, and 10→10. Values
4–6 and unexpected values require quarantine/telemetry, not guessing. Confirm
the 7→6 boundary and all reconnect presentations against a revision-239 client
trace before shipping the converter.

Interpret legacy raw carrier values 0–3 as a repair count only for saves known
to predate the native migration, mapping them monotonically to bits 8–10. Do
not treat an arbitrary modern raw carrier value as that counter. Then map:

- `%dragon_wall` to the native shortcut bit;
- `%dragon_oracle` knowledge/spoken/door states to the Oracle instruction,
  clue, and all-four-offerings-complete fields where evidence warrants it;
- `%dragon_goblin` and `%dragon_shield` to their instruction bits;
- `%dragon_ned_hired` plus primary stage to the Ned fields; and
- owned Maze keys, coloured keys, map parts/map, ship stage, and route stage as
  corroborating evidence for already completed briefing checkpoints.

Do not invent consumed map pieces, a head, a shortcut, a ship repair, or a
reward from ambiguous evidence. Migration must be idempotent, preserve all
unrelated carrier bits, and be tested across every old primary value, carrier
value 0–3, authored side-state combination, inventory/bank/ground ownership
combination, and repeated login. Remove all whole-carrier writes after it
lands.

## 4. Implementation and ownership surface

The quest root contains three config files and eleven scripts, 1,204 lines.
The eight principal shared area files add 835 lines, for 2,039 directly audited
lines before generic doors, maplinks, item handling, combat, Slayer, shops,
Nightmare Zone, diaries, downstream quests, and the journal dispatcher.

| Surface | Current responsibility | Audit result |
| --- | --- | --- |
| `quest_dragon.constant` | Old stage and side-state aliases | Conflicts with the native route at 6–9 |
| `quest_dragon.varp` | Primary, whole carrier, five authored vars | Destructive carrier writes and duplicate state model |
| `quest_dragon.npc` | Melzar and Elvarg overlays | Useful baseline stats; old shared-world combat lifecycle |
| `guild_master.rs2` | Start | Missing the entire post-Oziach canonical briefing and Maze-key service |
| `oziach.rs2` | Offer, old briefing, post-quest talk | Briefing belongs to Guildmaster; kill turn-in and shop absent |
| `oracle.rs2` / `magic_door.rs2` | Thalzar clue, door, chest | All-at-once offering replaces four native incremental fields |
| `melzars_maze.rs2` / `melzar_the_mad.rs2` | Maze doors, keys, chest, combat | Recognisable route; old movement/combat and unsafe item/result boundaries |
| `wormbrain.rs2` | Lozar payment/drop route | Attack/drop not instruction-gated; inventory-only ownership on payment |
| `duke_horacio.rs2` | Shield plus three shared quests | Useful branch; no capacity result and shared-topic precedence risk |
| `klarense.rs2` / `lady_lumbridge.rs2` | Purchase, boarding, repair | 12-nail legacy recipe, invalid persistence, incomplete retry lifecycle |
| `ned.rs2` / `dragonslayer_ned.rs2` | Recruitment, map, travel | Separate old flag, deletes all maps, no voyage scene |
| `cabin_boy_jenkins.rs2` / `sailors.rs2` | Ship/sailor hints | Dialogue mostly present; scene/death/modern Sailing continuity absent |
| `crandor.rs2` plus generic maplinks | Secret wall, hole, rope | Hole/rope maplinks exist; wall uses wrong field; lair barrier has no handler |
| `elvarg.rs2` / `quest_dragon.rs2` | Boss AI and completion | Wrong protection model and terminal transition on any found-hero death |
| `dragon_journal.rs2` | Dynamic journal | Old state model, stale title/text, inventory-only map observations |
| shared equip, Slayer, shop, NMZ, quest, diary systems | Unlock consumers | Mixed: armour/quest consumers work, several promised gates do not |

The cache already includes the native ship-hole transform, full voyage actors,
Ned and Jenkins cutscene variants, Elvarg flight/fire/death assets, Crandor
ruins, arrival hole and climbing rope, lair barrier, dragon corpses with and
without head, Elvarg's head item, magic-door forms, and all native side fields.
Modernization should bind these assets by symbolic revision-239 gameval instead
of retaining comments that say their older LostCity names are absent.

## 5. Start, Guildmaster, and Oziach

The Champions' Guild door correctly checks 32 quest points and blocks entry,
although it uses a walk-through/teleport approximation rather than its normal
door presentation. The Guildmaster itself does not recheck the requirement, so
the offer should still enforce it defensively for alternate entry, teleport,
debug, or future map changes.

State 0 offers only the place description and rune-plate question. Selecting
the latter writes state 1 and directs the player to Oziach. That narrow slice
works. After state 1, however, the Guildmaster permanently falls back to only
"What is this place?" The canonical post-Oziach history, map/ship/shield topic
menu, re-talk menu, five instruction bits, Maze-key grant, Maze-key replacement,
post-Elvarg congratulation, and post-quest replacement service are all absent.

Oziach accepts the task and writes state 2, but the dialogue is an older route:
it explains the shield and all three map pieces itself. Current OSRS sends the
player back to the Guildmaster. The local branch also changes several exact
choices, lacks current Murder Mystery/Pandemonium conditionals, has weak
interruption semantics, and grants the Maze key from the wrong NPC without an
inventory-capacity/result check.

Before Elvarg is slain, current Oziach should ask whether the dragon has been
slain and refuse Trade. The local Talk-to instead exposes old directions for
the full quest. After the credited kill, native state 9 must route to the proof
dialogue and completion. Local state 9 still means merely arrived on Crandor,
and the Elvarg death skips Oziach entirely.

Modernize this hand-off as two explicit routers:

- Guildmaster owns start, return briefing, instruction/recovery topics,
  post-kill direction, and Maze-key replacement.
- Oziach owns ordinary prequest talk, task acceptance/refusal, active-quest
  refusal, proof/completion, and post-quest shop access.

Every grant must check the canonical ownership scope, reserve capacity, add
the item, verify success, and only then commit durable dialogue state.

## 6. Map pieces and ownership ledger

The three sources may be completed in any order after the corresponding
Guildmaster briefing. Modern code should use the native instruction bits and a
single ownership helper covering inventory, bank, worn where meaningful,
player-owned ground items, and pending transaction/scene storage.

| Item | Source and canonical recovery | Current behavior | Required change |
| --- | --- | --- | --- |
| Maze key | Guildmaster; replace before or after quest; key is retained on maze entry | Oziach grants only during active quest; Guildmaster never grants | Move ownership to Guildmaster, support full recovery contract, preserve one key |
| Melzar part (`mappart1`) | Final Maze chest; repeat after genuine loss | Chest uses global total, then unchecked `inv_add` | Gate source correctly and make claim capacity-safe |
| Thalzar part (`mappart3`) | Dwarven Mine chest after four offerings | Same global-total pattern | Preserve native offering progress and make claim capacity-safe |
| Lozar part (`mappart2`) | 10,000 coins or gated kill + Telekinetic Grab | Payment checks inventory only; kill always creates part | Gate attack/drop, check ownership/capacity, consume exactly 10,000 once |
| Crandor map | Combine exactly one of all three | Deletes every copy of every part | Delete exactly one of each and add one map atomically |
| Map hand-off | Give exactly one map to Ned | Deletes `inv_total`, consuming duplicates | Delete exactly one only after all preconditions pass |

The current combine handlers can run outside the intended quest window and
remove all duplicate pieces. The chest grants have no explicit free-space or
`inv_add` result handling. Map study text is broadly recognisable, but modern
names are Melzar, Thalzar, and Lozar and the current 2026 journal should be the
source of truth for progress presentation.

Test loss and recovery separately from intentional duplicates. A banked piece
must not make the journal claim it is missing, a player-owned ground copy must
not allow an unsafe second claim, and an expired/destroyed copy must leave the
canonical source available again.

## 7. Melzar's Maze

The local route includes the correct entrance, retained Maze key, red/orange/
yellow/blue/magenta/green sequence, coloured-key consumption, ladders, six
key-bearing NPC forms, final chest, and exit. The selected NPC forms are the
cache's visually distinct key variants, which is important because only one
rat, ghost, and skeleton is correct. The route enemies are zombie rat 3,
ghost 19, skeleton 22, zombie 24, Melzar 43, and lesser demon 82.

The current handlers still need a modern pass:

- door movement teleports through the door and does not play/cache-transition
  the normal open/close presentation;
- every closed coloured door only responds to item-on-object; exact option and
  failure behavior should be traced, including wrong door and reused key;
- key drops are authored in death queues with only `npc_findhero`, not an
  explicit owner/credited-kill/room contract;
- Melzar uses a LostCity-derived approximation with omitted attack-state
  variables and scene/sound details;
- chest opening and claiming are separate as expected, but the claim is not a
  protected transaction; and
- multiplayer kill contention, logout on a floor, death, re-entry, duplicate
  keys, ground visibility, and respawn/reset behavior have no quest tests.

Keep the front Maze key, consume exactly one coloured key at its matching door,
and ensure the key belongs to the player who earned it. Do not instance the
whole maze unless a client trace proves OSRS does so; owner-scoped drops and
safe shared-world state are sufficient.

## 8. Thalzar, the Oracle, and the magic door

The Oracle appears and exposes the map clue only when the local authored
knowledge flag says the player knows about her. That must instead follow the
native Thalzar instruction bit. Ordinary Oracle sayings should remain
available as a separate topic.

Current OSRS lets the player use silk, lobster pot, unfired bowl, and wizard's
mind bomb on the magic door independently and in any order. Each successful
offering is consumed and stored in bits 17–20; leaving, logging out, or fetching
a forgotten item does not reset progress. The local implementation requires
all four items simultaneously, consumes all four on a normal door click, and
stores only `%dragon_oracle = 3`. Quest Helper's four sequential conditions and
the native carrier prove this is not merely a presentation difference.

Bind item-on-object handlers for each ingredient, commit only that ingredient's
bit after deletion succeeds, retain progress across reconnect, and transform/
animate the cache-authored door when all four are present. Opening/searching
the chest must produce one Thalzar part when globally absent and leave a safe
retry on full inventory or interrupted interaction.

## 9. Lozar and Wormbrain

Current OSRS does not allow the player to attack Wormbrain until the
Guildmaster's Lozar briefing is recorded. Local Wormbrain has an ordinary
attackable form, and his death queue always creates bones and `mappart2` for a
found hero without checking quest stage, instruction, or existing ownership.
This permits early or repeated map-part drops.

The alternative dialogue correctly converges on 10,000 coins, but its final
transaction checks only the inventory copy of the part. A banked or grounded
copy can be duplicated, and the code deletes coins before verifying that the
part can be added. Gate both combat and dialogue on the native instruction bit.
For payment, verify global absence and capacity, delete exactly 10,000 coins,
add exactly one part, and roll back or avoid commitment if either operation
fails. For combat, require credited ownership, make the drop player-owned, and
preserve the jail-bar Telekinetic Grab route.

The optional Goblin Village generals conversation has a native bit but no
local implementation. It is not required for the critical path, yet should be
implemented because the transcript and native state explicitly support it.

## 10. Anti-dragon shield and protection unlock

Duke Horacio's shared router currently prioritizes Death to the Dorgeshuun,
The Lost Tribe, and The Ides of Milk before ordinary/Dragon Slayer dialogue.
The shield branch appears when the authored knowledge flag is set or the quest
is complete and no shield is held or worn. The ability to drop-trick multiple
shields is canonical, so absence of a bank/ground check must not automatically
be "fixed" into global uniqueness. It does need exact current dialogue,
inventory-capacity handling, and a matrix test against every shared quest arm.

Starting/partially progressing Dragon Slayer I is required to use dragonfire
protection. The local shield equip path and antifire-potion consumption path do
not check `%dragonquest`; a player can use them at state 0. Add one shared
eligibility predicate used by shield equip, ordinary/super antifire consumption,
and any other protection item that OSRS gates. Do not wait until completion.

The local completion screen advertises "Dragonfire protection" as though it
were a completion reward, which is misleading. The journal/reward UI should
describe the actual partial-completion unlock point.

## 11. Lady Lumbridge, sawmill, and Ned

Klarense's 2,000-coin purchase and much of the ship dialogue are recognisable.
The hull contract is not. Each of three repairs requires one normal plank, 30
steel nails, and a hammer; local code requires only four nails per plank and
therefore consumes 12 total. It increments the whole carrier from 0 to 3,
never sets native repair bits 8–10, and advances the primary directly from 3
to old state 7. Repair presentation, animation/sound, interruption, material
transactionality, and the cache-authored hull transform are absent.

Revision 239 also includes the current F2P sawmill route introduced in the
2026 dialogue: after purchasing the ship, the Varrock sawmill operator can
make up to nine normal planks from logs for 100 coins each. Native bits 25 and
26–29 track meeting the operator and how many were made. The operator is
spawned, but its only local Talk-to owner is a Bone Voyage branch; Dragon
Slayer's additive branch is absent.

Ned can be asked before the ship is ready and should remember the conversation.
The local `%dragon_ned_hired` approximates that, but native bits 21 and 22 own
the actual checkpoints. The current map hand-off deletes every map in the
inventory, writes old state 8, and has no protected transaction. Modernize
recruitment and hand-off around the native bits and exact one-item semantics.

After the first crash, returning to the mainland without opening the shortcut
causes the Lady Lumbridge to wash back to Port Sarim, broken. The player must
spend another three planks and 90 nails, and can sail again. If the shortcut
was opened, the game refuses further ship repairs because the Karamja route is
available. Local code resets the whole carrier to zero and offers an old
whale-tow/re-hire route, but it does not model the complete native repair,
shortcut exclusion, Jenkins-dead, map-already-known, and repeat-voyage matrix.

## 12. Voyage, Crandor, and return routes

Speaking to shipboard Ned should play the voyage: Ned and Jenkins converse,
clouds gather, Elvarg flies over and attacks, Jenkins dies, the ship crashes,
and the player wakes on Crandor. The local implementation emits two messages,
telejumps to Crandor, clears two vars, and shows a joke exchange. It never uses
the cache's cutscene Ned, Jenkins, flying Elvarg, fire, ship, camera, or sound
assets.

The Crandor ruin entrance and climbing rope are already connected through the
generic maplink table. The local `crandor.rs2` comment claiming the route is
absent is stale; modernization should preserve those verified maplinks and add
their quest/scene conditions where needed. Entering the ruins also has a short
canonical scene that is presently absent.

The secret wall works as a walk-through approximation but stores state in
`%dragon_wall`, not native bit 6. From the Crandor side it should unlock during
the quest; from Karamja it should reject an active player who has not discovered
it, while completion permits the route regardless. The current axis logic and
all four state/direction combinations need coordinate tests.

The actual barrier into Elvarg's chamber is
`dragon_slayer_qip_stalagtite_jump`. Quest Helper targets it at (2846, 9635),
the cache gives it a `Climb-over` option, and no local or generic handler owns
that gameval. With normal collision this is a hard progression blocker even
though Elvarg is spawned behind it. Implement the exact animation/movement,
state gate, immediate entry fire behavior, exit behavior, and interruption
cleanup.

## 13. Elvarg encounter

The cache parent transforms to `elvarg_alive` for active native states and
away after state 9. The local overlay supplies 80 Hitpoints, level-70 combat
stats, size 4, aggressive behavior, and alternating melee/fire attacks. Those
are useful scaffolding, but the encounter is neither owned nor complete.

### 13.1 Dragonfire matrix

Current maximum dragonfire values are exact and Elvarg-specific:

| Shield | Protect from Magic | Potion | Maximum damage |
| --- | --- | --- | ---: |
| No | No | None | 70 |
| Yes | No | None | 10 |
| No | Yes | None | 55 |
| No | No | Antifire | 55 |
| No | Yes | Antifire | 40 |
| Yes | Yes | None | 7 |
| Yes | No | Antifire | 7 |
| Yes | Yes | Antifire | 4 |
| Either | Either | Super antifire | 0 |

Local code implements only 70, 55, 10, and 7 and ignores both potion effects.
The generic local `~dragonfire_maxhit` is also not a valid drop-in solution:
it halves ordinary-antifire damage and makes shield plus ordinary antifire
fully immune, while Elvarg requires 55 and 7 respectively. Implement an
Elvarg-specific shared table/proc covered by all nine combinations.

Every fire attack canonically drains 10% of remaining Prayer, rounded down.
The local `stat_drain(prayer, 0, 10)` is a fixed argument and is not visibly a
percentage calculation. Replace it with an explicit remaining-value formula
after confirming the server's tenths/stat units. Prayer drains even when super
antifire reduces damage to zero.

### 13.2 Ownership, lifecycle, and combat parity

Both `ai_queue3` handlers merely call `npc_findhero` and queue completion for
any hero with `%dragonquest < 10`. They do not require native state 8, verify
the credited killer/last hit, bind Elvarg to one player's encounter, prevent
another player from advancing, or guard the reward queue against duplication.
The completion queue even accepts states 0–7 if a hero can reach or kill the
actor.

Modernization must provide one authoritative encounter owner and require:

- native state 8 and valid chamber entry;
- credited kill by that owner, including poison/recoil edge cases;
- safe leave, teleport, death, logout, reconnect, and re-entry;
- no cross-player damage, kill credit, head, state, or reward theft;
- no stale delayed damage after cleanup; and
- exactly one state-9 transition.

Elvarg has a 30% weakness to water spells as of 25 June 2025, is poisonable
and venomable, is not dragonbane-vulnerable, and cannot be attacked by a dwarf
multicannon. Verify each against the shared combat pipeline. Use the cache
death animation, full-body corpse, headless corpse, player decapitation, head
item, and `A Slayer's Feat` jingle rather than an immediate teleport/reward.

If the inventory is full, no head is added; this must not block state 9 or
completion. The head's Destroy action needs its canonical warning and must not
be treated as an ordinary public ground drop by the generic op-5 handler.

## 14. Completion, rewards, and unlocks

Current completion is queued directly by Elvarg's death. It teleports a player
out of part of the chamber, clears the whole side carrier, writes state 10,
awards both XP rewards, and opens the shared completion UI. This bypasses the
canonical state-9 return to Oziach and makes the reward vulnerable to the boss
ownership defects.

The modern terminal transaction belongs to Oziach. It must accept state 9
with or without the optional head, run once, remove a banked head as current
OSRS does, preserve unrelated native side bits, award 18,650 Strength and
Defence XP plus 2 QP once, write state 10 last, and recover safely from UI
close/logout at every boundary.

| Unlock / consumer | Current evidence | Required result |
| --- | --- | --- |
| Rune platebody and variants | `levelrequire.rs2` checks completion | Retain and expand tests to every relevant variant |
| Green d'hide body | Same gate for `dragonhide_body` | Retain; verify trimmed/ornament variants use the shared family |
| Dragon platebody | Reward claims it; no explicit local completion gate found | Trace current item requirements and add correct shared gate if absent |
| Oziach shop | No shop definition or opener; Trade replays dialogue | Implement current stock/access and open it only after completion |
| Dragon Slayer assignments | Slayer weighted picker has no `%dragonquest` filter | Gate dragon tasks at quest start/required partial state, not completion |
| Dragonfire protection | Shield/potion use has no quest gate | Gate at start/partial completion as canonical |
| Crandor | Surface arrival, dungeon maplinks, and shortcut partly work | Make all active/retry/post-quest route states exact |
| Corsair Cove Resource Area | No Dragon Slayer gate found | Add completion requirement alongside Corsair Cove ownership rules |
| Nightmare Zone Elvarg | Stub always serves Count Draynor then Elvarg | Require completed Dragon Slayer and integrate real boss selection |
| Dragon POH crest | `poh_crest.rs2` checks completion | Retain and test |
| Heroes' Quest | Local prerequisite checks completion | Retain and test migrated state |
| Diaries / indirect quests | No complete audited consumer set | Enumerate and test all article-listed direct and indirect consumers |
| Jenkins ghost crewmate | No quest/Sailing continuity owner found | Implement at 60 Sailing using the crash/death state |

The reward string currently says dragons as a Slayer task as though completion
unlocks them. Both the Wiki and current routing guidance say the unlock occurs
immediately after starting. Fix behavior first, then make the completion UI
accurately distinguish prior partial unlocks from completion rewards.

## 15. Shared NPC and system routing

Dragon Slayer touches some of the game's busiest shared actors. Additive topic
routers are required; a single top-level `if` chain must not silently hide a
recoverable quest item.

| Owner | Shared responsibilities to preserve | Required matrix |
| --- | --- | --- |
| Guildmaster | Champions' Guild ordinary dialogue, Dragon Slayer, later guild content | QP<32, state 0, 1, 2–8, 9, 10; each instruction/key ownership state |
| Oziach | Ordinary dialogue, Dragon Slayer, Trade/shop | prequest, offer/refusal, active, state 9, complete, Talk-to vs Trade |
| Duke Horacio | Rune Mysteries, Lost Tribe, DTTD tour, Ides of Milk, shield | Cartesian active/complete combinations plus shield/drop-trick/capacity states |
| Ned | Rope, Prince Ali wig, Dragon Slayer captain/map/retry | Both quests active in every captain/map/ship state |
| Oracle | Ordinary sayings, Dragon Slayer clue, clue-scroll consumers | instruction absent/present, clue delivered, door progress, other topic active |
| Sawmill operator | Ordinary sawmill, Bone Voyage, Dragon Slayer planks | both quests active, 0–9 planks, coins/logs/capacity failures |
| Port Sarim sailors | Karamja fare plus Crandor refusal | before/active/complete and all sailing multinpc forms |
| Klarense / Jenkins | Ship ownership, crash/retry, post-quest/Sailing dialogue | states 0–10, repairs 0–3, shortcut, Jenkins alive/dead/ghost |

Use explicit topic selection where more than one quest can reasonably be
discussed. Item recovery must remain reachable even when another quest's
high-priority scene is active.

## 16. Journal, UI, debug, and cheat behavior

The dynamic journal is substantial and uses the modern shared journal adapter,
but its title is still `Dragon Slayer`, not `Dragon Slayer I`. It follows the
old local state ladder, tells the player to return to Oziach rather than the
Guildmaster for instructions, reads only inventory for map pieces/map, reads
the whole carrier as a repair count, has no state-9 proof entry, and predates
the 14 January 2026 journal rewrite.

Rebuild journal conditions from native fields and global item ownership. The
journal must remain useful after banking a piece, partially feeding the magic
door, completing one/two repairs, crashing and returning to port, unlocking
the shortcut, killing Elvarg without head space, destroying the head, and
reaching state 9. Verify exact current text against a real client because the
Wiki does not publish a dedicated journal transcript.

The generic quest cheat writes state 10 directly. Replace or supplement it
with a Dragon Slayer fixture command that constructs every valid checkpoint
without awarding live rewards: all primary states, all five briefing bits,
each of 16 door-offering combinations, repairs 0–3, map-item ownership states,
Ned states, first crash, shortcut, active encounter, proof state, and complete.
Invalid combinations should be deliberately available only through a separate
validation command.

Add a durable audit/debug dump that prints primary state, migration version,
all 21 native fields, legacy authored vars while migration exists, relevant
item totals by container, active scene/encounter owner, and one-time reward
markers. Never repair live state merely by opening the journal.

## 17. Modernization sequence

Implement in dependency order and keep each slice independently testable.

1. **Trace and migration foundation.** Capture real-client primary transitions,
   native bit writes, loc/NPC transforms, and exact shop/unlock behavior. Add
   the versioned migration and remove whole-carrier writes.
2. **State and transaction helpers.** Introduce native aliases, item ownership,
   capacity-safe exchange helpers, one-time completion/reward guard, and
   Dragon Slayer fixture/debug support.
3. **Guildmaster/Oziach.** Restore the canonical hand-off, all briefing bits,
   Maze-key recovery, state-9 proof turn-in, post-quest shop, and shared routing.
4. **Map branches.** Modernize Maze ownership, incremental magic door,
   Wormbrain attack/payment, exact map combination, loss, and recovery.
5. **Shield and partial unlocks.** Repair Duke routing and centralize the
   quest-start gate for dragonfire protection and Slayer assignment.
6. **Ship preparation.** Implement 30 nails per plank, native hull transforms,
   sawmill plank service, captain/map transactions, boarding, and retry state.
7. **Scenes and navigation.** Build the owned voyage/crash and ruins-arrival
   scenes from cache assets; validate hole, rope, wall, and lair barrier.
8. **Elvarg.** Add encounter ownership, exact fire/Prayer matrix, modern magic
   weakness/exclusions, lifecycle cleanup, death/decapitation, head, and state 9.
9. **Completion and consumers.** Complete at Oziach, add shop and every unlock,
   remove banked head, and verify all downstream quests/diaries/systems.
10. **Journal and polish.** Port the 2026 journal, exact current transcript,
    animations, camera, audio, door/ship presentation, and accessibility.

Do not mark a later slice complete because an earlier debug skip can reach it.
Each slice must be proven through the normal world route with migrated and new
accounts.

## 18. Test matrix

### 18.1 Persistence and migration

- Legacy primary states 0, 1, 2, 3, 7, 8, 9, 10 migrate deterministically.
- Unexpected legacy 4–6 and corrupt values are reported and never rewarded.
- Legacy carrier 0–3 maps to the correct native repair bits only under the
  legacy migration version.
- Every authored Oracle/shield/goblin/wall/Ned combination preserves maximum
  evidenced progress without inventing items or completion.
- Repeated migration/login is idempotent; native bits never regress or clear.
- Client transforms for ship hole, Ned, Jenkins, Elvarg, door, and shortcut
  match server state after login and every transition.

### 18.2 Offers, topics, and recovery

- QP 31/32 at guild door and Guildmaster offer; alternate-entry attempt.
- Oziach accept, refuse, interrupt, re-talk, Trade before/active/state9/complete.
- Each Guildmaster briefing topic in all orders; history only once where
  canonical; Maze key inventory/full/bank/ground/lost/post-quest cases.
- Duke, Ned, Oracle, sawmill, sailors, Klarense, and Jenkins cross-quest matrices.
- Journal at every primary and side checkpoint, including banked items.

### 18.3 Map and Maze

- Correct and incorrect key-bearing NPC on every floor; simultaneous killers;
  private drop visibility; key pickup, drop, death, logout, and respawn.
- Every correct/wrong coloured key-door pair; exact-one consumption; backtrack,
  exit, re-entry, ladder, logout, and death on every floor.
- Chest closed/open/search, full inventory, held/banked/ground part/map, and
  genuine-loss reclaim.
- All 16 magic-door offering combinations, any order, wrong item, drink-warning
  path, logout between offerings, inside/outside crossing, and chest recovery.
- Wormbrain before/after instruction, melee/ranged/magic targeting across bars,
  Telekinetic Grab, payment at 9,999/10,000 coins, full inventory, banked part,
  death, and repeated attempt.
- Map combine via all six ordered item pairs, missing part, duplicates, full
  inventory, interrupt, and exact-one deletion.

### 18.4 Ship and navigation

- Klarense purchase at 1,999/2,000 coins and every refusal/re-talk branch.
- Repair with 0/29/30/59/60/89/90 nails, 0–3 planks, wrong planks, no hammer,
  each repair order, interrupt/logout, and visual transform.
- Sawmill at 0–9 made planks, mixed logs/coins, capacity failure, Bone Voyage
  overlap, and limit enforcement.
- Ned asked before/after purchase/repair/map; map item-on and dialogue hand-off;
  duplicate maps; full inventory changes; Prince Ali overlap.
- Boarding/disembarking each repair/captain state and every ship form/plane.
- Voyage cancel and every scene tick interrupted by logout, death, teleport,
  reconnect, second player, or server restart; actor/camera cleanup.
- Crandor hole/rope, secret wall from both sides in states 0–10, ship retry with
  shortcut open/closed, and lair barrier enter/exit/cancel.

### 18.5 Elvarg and completion

- All nine dragonfire protection rows at deterministic max rolls.
- Prayer values 0, 1, 9, 10, and high values confirm 10%-remaining rounding;
  drain still occurs under super antifire.
- Melee, ranged, Magic, water weakness, poison, venom, recoil, prayer, teleport,
  flinch, immediate re-entry fire, cannon exclusion, and dragonbane exclusion.
- Two players enter/attack/kill simultaneously; non-owner last hit; owner dies
  on kill tick; poison/recoil kill while leaving/logging; reconnect mid-fight.
- Inventory full/one free slot at decapitation; head destroy/drop semantics;
  bank head before completion; no-head Oziach completion.
- Guildmaster at state 9, Oziach Talk-to/Trade at state 9, completion UI close,
  logout at each reward boundary, repeated Talk-to, and concurrent trigger.
- XP, QP, primary state, carrier bits, bank cleanup, shop, equipment, Slayer,
  protection, Crandor, Corsair Cove, NMZ, crest, Jenkins, quests, and diaries
  are each verified independently after completion/start as appropriate.

## 19. Acceptance gates

### Gate A — source completeness

- All pinned Wiki route, transcript, item, combat, recovery, reward, and unlock
  behavior is represented in tests or explicitly documented as non-applicable.
- Quest Helper refs resolve; the extractor's `quest_dragonslayer` alias is fixed
  or explicitly mapped to `quest_dragonslayer1`.
- Every cache-authored Dragon Slayer actor, item, loc, sequence, transform, and
  var field used by the current route has a named owner.

### Gate B — persistence and transactions

- No code writes whole `dragonquestvar`; migration is versioned and idempotent.
- Every item/coin/material exchange is capacity-safe and consumes exact counts.
- Recovery uses explicit ownership across containers and ground/scene state.
- Primary state, side fields, items, scenes, and rewards survive cancellation,
  death, logout, reconnect, and restart without duplication or regression.

### Gate C — route and encounter parity

- A fresh 32-QP account can complete every canonical branch through ordinary
  world interactions; a 31-QP account cannot start.
- Ship transforms, voyage/crash, ruins, rope, shortcut, lair barrier, Elvarg,
  decapitation, and proof return use native cache assets and exact checkpoints.
- Elvarg is owner-safe and matches the fire matrix, Prayer drain, weakness,
  exclusions, and retry behavior.

### Gate D — completion and consumers

- Only Oziach at state 9 performs the terminal transition, exactly once.
- Rewards and all start/partial/completion unlocks occur at their canonical
  boundary, including Oziach's shop and banked-head cleanup.
- Shared NPCs retain every other quest/service topic.
- The 2026 journal and current quest title are correct at every tested state.
- Static checks, server build, focused automated tests, migration fixtures, and
  two independent live playthroughs (new and migrated account) are green.

Until all four gates pass, keep the master-plan entry `audit-pending`. The
existing scripts are valuable source material, but their successful compile or
ability to teleport through selected checkpoints is not completion evidence.
