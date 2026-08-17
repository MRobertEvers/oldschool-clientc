# God Wars Dungeon implementation plan

Last audited: **2026-08-17** against the OSRS revision-239 cache and the live
OSRS Wiki revisions named below.

## Scope and completion rule

This plan covers the main [God Wars Dungeon](https://oldschool.runescape.wiki/w/God_Wars_Dungeon),
all four original strongholds and boss rooms, the Frozen Door and Ancient
Prison, and [Nex](https://oldschool.runescape.wiki/w/Nex). It does not cover the
[Wilderness God Wars Dungeon](https://oldschool.runescape.wiki/w/Wilderness_God_Wars_Dungeon).

“Implemented” means all of the following are true for **every** NPC variant in
the roster, not merely that it can spawn and use the default melee handler:

- [ ] Its cache ID/gameval, display version, spawn, faction, combat level,
  Slayer requirement, aggression rule, movement, respawn, and kill-count
  behavior are recorded and tested.
- [ ] Every player-facing and NPC-vs-NPC attack uses the correct style, range,
  accuracy stat, maximum hit, cadence, target-selection rule, protection-prayer
  reduction, and secondary effect.
- [ ] Every attack, defend, death, transform, movement, projectile, spot
  animation, graphic, overhead icon, sound, forced movement, and hitsplat is
  mapped to a symbolic cache name and occurs on the correct tick.
- [ ] Every special attack has deterministic tests for selection, safe tiles,
  target caps, damage, state changes, cleanup, and multiplayer behavior.
- [ ] Every death uses its own exact 100%, unique, regular, shared subtable, and
  independently rolled tertiary drops. Quantity ranges, noted state,
  conditions, loot ownership, MVP/contribution rules, and mutually exclusive
  rolls must be preserved.
- [ ] No GWD NPC silently falls through to a generic animation, generic combat
  style, `death_drop=bones`, `gwd_drop_main`, or `gwd_minion_drop_body`.
- [ ] A compile check, focused deterministic tests, statistical drop tests, and
  an in-game multiplayer smoke test pass for the NPC.

The Wiki is the behavior and drop-table reference. Cache configs are the
authority for IDs and audiovisual assets. Each implemented script must cite the
Wiki page, exact revision ID, and fetch date in the convention established by
`docs/NPC_WIKI_DROPTABLES_PLAN.md`; names alone are never sufficient to join a
cache NPC to a Wiki version.

## Implementation snapshot

This document remains the acceptance plan; checked boxes below are not waived
by the existence of code. As of the audit date, the repository now has:

- [x] A generated [classic combat ledger](../OSRS-Content/osrs239-content/wiki/godwars_combat_manifest.csv)
  covering all **69** main-dungeon gamevals and **126** distinct player/NPC
  attack paths, with a build-failing one-handler-per-NPC check.
- [x] Explicit original-general/bodyguard combat, room AOE/assist/reset logic,
  individual regular/unique/tertiary tables, faction KC, altars, and audiovisual
  bindings in `areas/area_godwars`; the public doors also report exact bounded
  room occupancy through their cache-defined Peek option.
- [x] Explicit ambient four-way NPC warfare, player attribution, exact Gorak
  prayer/stat-drain behavior, Aviansie melee rules, generated versioned drops,
  and applicable non-Wilderness tertiaries.
- [x] Frozen Door progression, Ancient Prison population, Zaros essence,
  Ashuelot services, Ancient Forge, death bank, the four Zarosian soldier drop
  tables, and their specials/Ancient Magicks effects.
- [x] A reachable public Nex actor with spawn transform, phase floors/mages,
  partial-prayer autos, three-hit melee sequences, all eight named phase
  specials, contribution loot, overhead rotation, Wrath, scoreboard, cleanup,
  and the cache animations/projectiles/spotanims/sounds named in this plan.
- [x] Script compilation in an isolated content tree plus deterministic
  `check-godwars-manifest` and `check-godwars-contract` build gates.
- [x] Four-map-square lifecycle triggers which keep the KC overlay present
  while moving within the dungeon and atomically clear all five counters plus
  the overlay only after a real departure.
- [x] The frozen surface's ten-tick all-skill/run/special drain and the complete
  permanent [Fire of Unseasonal Warmth](https://oldschool.runescape.wiki/w/Fire_of_Unseasonal_Warmth)
  build: quest, 60 Construction/66 Firemaking, tool alternatives, exact salts,
  cache animation/sound, 600/300 XP, transform varbit, and chill immunity.
- [x] All six [Ghommal's hilt](https://oldschool.runescape.wiki/w/Ghommal%27s_hilt)
  Trollheim options, with the documented 3/5/unlimited daily allowances,
  runeday reset, large-boulder arrival, and intentional Troll Stronghold bypass.
- [x] Zamorak's ice-river crossing uses the cache-native double-swim sequence,
  swim/splash sounds and a three-tick exact move before applying the
  northbound Prayer drain.
- [x] [Saradomin's light](https://oldschool.runescape.wiki/w/Saradomin%27s_light)
  has its confirmation transcript, single-item consumption, duplicate guard,
  and permanent cache varbit; it removes the client-driven fortress darkness
  without suppressing the crossing's Prayer drain.
- [x] Armadyl's grapple shortcut keeps its unboostable level/equipment gates
  and now uses the cache's fast fire-and-climb sequence, player graphic,
  grapple projectile/sound, and a seven-tick exact move instead of teleporting.
- [x] Bandos's gong accepts the normal and Imcando hammers, every metal
  warhammer, Dragon warhammer variants, and both Elder mauls; each selects its
  held-model-correct `godwars_hammer_gong_*` sequence with embedded gong sound.

The remaining acceptance work is runtime-focused: nearest-player/defence
tie-breaking is scripted but still needs multiplayer trace validation;
Turmoil's unpublished drain amount/transfer timing and Ice Prison's unpublished
fixed defence roll need primary-source evidence; private-room and high-player-count
teardown require a room-instance owner; and the statistical,
tick-trace, relog/restart, and multiplayer soak matrix at the end of this plan
must still be executed. These are intentionally left unchecked below.

## Audit-start repository baseline and known gaps

At the start of this audit, the implementation lived in
`OSRS-Content/osrs239-content/server/scripts/areas/area_godwars/`, with classic
spawns in `areas/world/configs/m44_82.spawn`, `m44_83.spawn`, `m45_82.spawn`, and
`m45_83.spawn`. It is useful scaffolding, but it is not the acceptance baseline:

- `godwars_entrance.rs2` has rope access, four faction counters, 40-KC doors,
  altars, and the four wing shortcuts. It does not implement faction-item
  aggression suppression, faction-vs-faction combat, reduced KC from Combat
  Achievements, ecumenical keys, complete altar rules, private rooms, the Frozen
  Door, Zaros KC, death/logout relocation, or the Ancient Prison.
- `godwars_bosses.rs2` has preliminary attack loops for the four original
  generals. Graardor's 7/10 style split is not the Wiki's 2/3 split; Kree'arra
  lacks whirlwind knockback/freeze; Zilyana incorrectly uses a chamber-wide
  magic AOE; K'ril's style split and prayer special are incomplete; target
  selection, room reset, ownership, and several animation/effect timings remain
  unverified.
- The generated NPC style overlay supplies a basic ranged or magic attack for
  some ambient NPCs and bodyguards. That is not a substitute for their exact
  projectile, animation, sound, secondary effects, NPC-vs-NPC variant, or
  Slayer/aggression contract. Several cache gamevals have no generated
  animation overlay at all.
- `godwars_drops.rs2` is deliberately simplified and must be replaced. All four
  bosses share Graardor-like regular loot; boss remains are wrong; shard odds
  are wrong; Zilyana is missing the Armadyl crossbow and Saradomin's light;
  K'ril is missing the Staff of the dead; bodyguards currently have a 1/8 shard
  roll instead of 1/508 for any shard and lack their real unique/regular tables.
- There is no Ancient Prison, Nex encounter, contribution loot, death bank, or
  Nex scoreboard implementation even though revision 239 contains the relevant
  NPCs, locs, interfaces, sequences, spot animations, sounds, and items.

Do not preserve a simplified behavior merely because it is already scripted.
Replace it behind focused tests, and keep unrelated changes in the dirty parent
and content worktrees untouched.

## Authoritative roster and combat contract

### Original generals and bodyguards

The following 16 NPCs are the complete original boss-room roster. “Visual
contract” includes the named attack animation plus an exact cache audit of the
associated projectile/spotanim/sound and impact tick; a plain generic
`~npc_rangeattack` or `~npc_magicattack` is insufficient.

| Faction | NPC (cache gameval) | Attack contract | Visual contract | Drop-table source |
|---|---|---|---|---|
| Bandos | [General Graardor](https://oldschool.runescape.wiki/w/General_Graardor) (`godwars_bandos_avatar`) | 6 ticks; 2/3 crush up to 60 or 1/3 room-wide ranged 15–35; he must be in melee distance of his primary target even for ranged; independent roll for every player | `godwars_bandos_attack`, `godwars_bandos_ranged`, defend/death, ranged projectile/impact, cries | Graardor rev. 15298421 |
| Bandos | [Sergeant Strongstack](https://oldschool.runescape.wiki/w/Sergeant_Strongstack) (`godwars_sergeant_goblin1`) | 5 ticks; crush, max 15 | sergeant melee attack/defend/death and sounds | rev. 15298419 |
| Bandos | [Sergeant Steelwill](https://oldschool.runescape.wiki/w/Sergeant_Steelwill) (`godwars_sergeant_goblin2`) | 5 ticks; magic, max 15 | magic cast, projectile, impact, defend/death | rev. 15298417 |
| Bandos | [Sergeant Grimspike](https://oldschool.runescape.wiki/w/Sergeant_Grimspike) (`godwars_sergeant_goblin3`) | 5 ticks; ranged, max 21 | ranged attack, projectile, impact, defend/death | rev. 15298415 |
| Armadyl | [Kree'arra](https://oldschool.runescape.wiki/w/Kree%27arra) (`godwars_armadyl_avatar`) | 3 ticks; room-wide ranged 69 or ranged-magic 21 while attacked; the latter rolls Magic accuracy against Ranged defence and Protect from Missiles; magical melee 25 only when not under attack; both wind attacks can knock back | wind/claw attacks, grey ranged bolt and blue magic whirlwind visuals, impact sound, forced movement, defend/death, cry | rev. 15298481 |
| Armadyl | [Flight Kilisa](https://oldschool.runescape.wiki/w/Flight_Kilisa) (`godwars_armadyl_bodyguard_kilisa`) | 5 ticks; slash, max 15 | melee swoop/strike, defend/death | rev. 15298482 |
| Armadyl | [Wingman Skree](https://oldschool.runescape.wiki/w/Wingman_Skree) (`godwars_armadyl_bodyguard_skree`) | 5 ticks; magic, max 16 | cast, projectile/impact, defend/death | rev. 15298484 |
| Armadyl | [Flockleader Geerin](https://oldschool.runescape.wiki/w/Flockleader_Geerin) (`godwars_armadyl_bodyguard_geerin`) | 5 ticks; ranged, max 25 | ranged attack, projectile/impact, defend/death | rev. 15298486 |
| Saradomin | [Commander Zilyana](https://oldschool.runescape.wiki/w/Commander_Zilyana) (`godwars_saradomin_avatar`) | 2 ticks; single-target crush up to 27 and magic 10–20, both only at melee distance; very high accuracy; retarget nearby attackers when her target is unreachable | melee and magic attack sequences, magic graphic/sound, defend/death, cries | rev. 15298455 |
| Saradomin | [Starlight](https://oldschool.runescape.wiki/w/Starlight) (`godwars_saradomin_unicorn`) | 5 ticks; crush, max 15 | unicorn melee, defend/death, sounds | rev. 15298454 |
| Saradomin | [Growler](https://oldschool.runescape.wiki/w/Growler) (`godwars_saradomin_lion`) | 5 ticks; magic, max 16 | magic cast/projectile/impact, defend/death | rev. 15298447 |
| Saradomin | [Bree](https://oldschool.runescape.wiki/w/Bree) (`godwars_saradomin_centaur`) | 5 ticks; ranged, max 16 | centaur ranged attack/projectile/impact, defend/death | rev. 15298452 |
| Zamorak | [K'ril Tsutsaroth](https://oldschool.runescape.wiki/w/K%27ril_Tsutsaroth) (`godwars_zamorak_avatar`) | 6 ticks; 2/3 slash up to 46 or 1/3 magic 10–30; 1/9 of melee choices against Protect from Melee become a 35–49 through-prayer special (2/27 overall) which drains half current Prayer, rounded down; poison starts at 16 | melee/magic/special sequences, projectile/impact, poison, defend/death, cries | rev. 15298476 |
| Zamorak | [Tstanon Karlak](https://oldschool.runescape.wiki/w/Tstanon_Karlak) (`godwars_ancient_greater_demon`) | 5 ticks; crush, max 15 | demon melee, defend/death | rev. 15298479 |
| Zamorak | [Balfrug Kreeyath](https://oldschool.runescape.wiki/w/Balfrug_Kreeyath) (`godwars_ancient_black_demon`) | 5 ticks; magic, max 16 | magic cast/projectile/impact, defend/death | rev. 15298477 |
| Zamorak | [Zakl'n Gritch](https://oldschool.runescape.wiki/w/Zakl%27n_Gritch) (`godwars_ancient_lesser_demon`) | 5 ticks; ranged, max 21 | ranged attack/projectile/impact, defend/death | rev. 15298478 |

Bodyguards must acquire an eligible player who attacked their boss, beginning
with the boss's current opponent on spawn and while idle, remain independently
targetable, use only their listed style, and share the room reset/respawn
lifecycle. If a guard dies while its general is dead, delay it so the next
quartet begins together. Test corner pathing and reacquisition; do not teleport
a stuck guard through players.

The current Wiki has a one-point Kree'arra melee discrepancy: the main infobox
lists 25 while its strategy prose says 26. Phase 0 must resolve this against the
revision-239 NPC stats/formula or an additional primary source and record the
decision; do not silently choose whichever number is already in code.

### Main-dungeon combatants

The four map files contain **69 gamevals**, which collapse into the families
below. Every listed gameval must appear in the generated coverage ledger even
where several variants share behavior and loot.

| Allegiance | NPC family and revision-239 gamevals | Required attacks and special rules | Drop table |
|---|---|---|---|
| Armadyl | [Aviansie](https://oldschool.runescape.wiki/w/Aviansie): 15 `godwars_armadyl_{male,female}_armor*_{blue,green,red}` variants, levels 69–148 | Ranged only; exact per-version max hit/stats; flying movement; reject ordinary melee while allowing the Wiki/cache-supported exceptions; Armadyl aggression immunity | Exact Aviansie version table, including feathers, conditional noted adamantite bars, RDT/tertiaries |
| Armadyl | `godwars_spiritual_armadyl_warrior`, `_ranger`, `_mage` | Warrior and ranger use ranged; mage uses magic; Aviansie melee restriction; 68/63/83 Slayer gates respectively | ID-matched [Spiritual warrior](https://oldschool.runescape.wiki/w/Spiritual_warrior#Drops), [ranger](https://oldschool.runescape.wiki/w/Spiritual_ranger#Drops), and [mage](https://oldschool.runescape.wiki/w/Spiritual_mage#Drops) version tables |
| Bandos | [Goblin](https://oldschool.runescape.wiki/w/Goblin#Drops) `godwars_goblin1..5` | Melee; variant stats/animations; Bandos immunity | ID-matched GWD goblin table |
| Bandos | [Hobgoblin](https://oldschool.runescape.wiki/w/Hobgoblin#Drops) `godwars_ancient_hobgoblin` | Melee | Exact level-47 version table |
| Bandos | [Ogre](https://oldschool.runescape.wiki/w/Ogre#Drops) `godwars_ancient_ogre` and [Jogre](https://oldschool.runescape.wiki/w/Jogre#Drops) `godwars_ancient_jogre` | Melee; 2x2 pathing and own attack/defend/death sequences | Exact GWD version tables |
| Bandos | [Cyclops](https://oldschool.runescape.wiki/w/Cyclops#Drops) `godwars_ancient_cyclops`, `godwars_ancient_cyclops2` | Melee; max hit/cadence from the ID-matched level-81 version | Exact GWD cyclops table |
| Bandos | [Ork](https://oldschool.runescape.wiki/w/Ork#Drops) `godwars_ancient_ork1..4` | Melee; four visual variants share verified behavior | Exact Ork table |
| Bandos | `godwars_spiritual_bandos_warrior`, `_ranger`, `_mage` | Melee, ranged, magic; Slayer 68/63/83; correct stationary-idle rule for the mage | ID-matched spiritual-creature tables |
| Saradomin | [Knight of Saradomin](https://oldschool.runescape.wiki/w/Knight_of_Saradomin#Drops) `godwars_saradomin_knight_1`, `_2` | Melee; variant stats and sequences | Exact levels 101/103 tables |
| Saradomin | [Saradomin priest](https://oldschool.runescape.wiki/w/Saradomin_priest#Drops) `godwars_ancient_saradomin_wizard` | Magic with exact projectile/impact | Exact priest table |
| Saradomin | `godwars_spiritual_saradomin_warrior`, `_ranger`, `_mage` | Melee, ranged, magic; Slayer 68/63/83 | ID-matched spiritual-creature tables |
| Zamorak | [Imp](https://oldschool.runescape.wiki/w/Imp#Drops) `godwars_ancient_imp` | Melee | Exact GWD imp version table |
| Zamorak | [Werewolf](https://oldschool.runescape.wiki/w/Werewolf#Drops) `godwars_ancient_werewolf1`, `_2` | Melee; both cache variants | Exact level-93 table |
| Zamorak | [Feral Vampyre](https://oldschool.runescape.wiki/w/Feral_Vampyre#Drops) `godwars_ancient_vampire` | Melee | Exact GWD version table |
| Zamorak | [Hellhound](https://oldschool.runescape.wiki/w/Hellhound#Drops) `godwars_ancient_hellhound` | Melee; GWD dog animations | Exact level-127 table |
| Zamorak | [Bloodveld](https://oldschool.runescape.wiki/w/Bloodveld#Drops) `godwars_bloodveld` | Magic-based melee attack and 50 Slayer requirement | Exact level-81 GWD version table |
| Zamorak | [Gorak](https://oldschool.runescape.wiki/w/Gorak#Drops) `godwars_gorak` | Melee with typeless/defence interaction verified against the Wiki and cache | Exact level-149 GWD table |
| Zamorak | `godwars_spiritual_zamorak_warrior`, `_ranger`, `_mage` | Melee, ranged, Flames-of-Zamorak magic; successful mage hit drains Magic; Slayer 68/63/83 | ID-matched spiritual-creature tables |
| Unaligned | [Icefiend](https://oldschool.runescape.wiki/w/Icefiend#Drops) `godwars_icefiend_1`; [Pyrefiend](https://oldschool.runescape.wiki/w/Pyrefiend#Drops) `godwars_pyrefiend_1` | Melee, correct Slayer gate for pyrefiend, no faction KC or god-item immunity unless cache/Wiki evidence says otherwise | Exact ID-matched tables |

All aligned soldiers must participate in the dungeon war: they aggress players
unless an equipped item protects the player from that allegiance, acquire
opposing NPC factions, do not abandon an NPC fight merely because a player is
nearby, and award KC only to an eligible player who actually receives kill
credit. Protection checks must cover every Wiki-listed god item and update when
equipment changes. Neutral creatures neither protect nor award faction KC.

### Ancient Prison and Nex roster

Revision 239 contains the following records, which form the implemented Ancient
Prison/Nex roster and the acceptance contract for later regression testing:

| Role | Cache NPC(s) | Complete combat/interaction contract | Drops |
|---|---|---|---|
| General | `nex`, `nex_spawning`, `nex_soulsplit`, `nex_deflect`, `nex_dying` | One stateful Nex actor transformed only for the correct overhead/spawn/death visual; five phases described below | Contribution table below |
| Phase mages | [Fumus, Umbra, Cruor, Glacies](https://oldschool.runescape.wiki/w/Nex#Bodyguards): `nex_smokemage`, `_shadowmage`, `_bloodmage`, `_icemage` | Level 285; magic; initially immune; each becomes attackable at 2,720/2,040/1,360/680 Nex HP; phase cannot advance until the active mage dies; exact Ancient Barrage projectile/effect, animations, sounds, and reset | No personal loot or KC |
| Prison warrior | `nex_prison_warrior` | Melee, 5 ticks, level 158; Zaros faction aggression and 68 Slayer gate | ID-matched [Spiritual warrior](https://oldschool.runescape.wiki/w/Spiritual_warrior#Zaros) table |
| Prison ranger | `nex_prison_ranger` | Ranged, 5 ticks, level 158; exact projectile/impact and 63 Slayer gate | ID-matched [Spiritual ranger](https://oldschool.runescape.wiki/w/Spiritual_ranger#Zaros) table |
| Prison mage | `nex_prison_mage` | Magic, 5 ticks, level 182; rotate smoke/shadow/blood/ice spells and implement poison, Attack drain, 20% heal, and six-tick freeze even where prayer blocks damage; 83 Slayer gate | ID-matched [Spiritual mage](https://oldschool.runescape.wiki/w/Spiritual_mage#Zaros) table, including ancient ceremonial pieces, dragon boots, blood essence, and nihil shards |
| Prison monster/add | [Blood Reaver](https://oldschool.runescape.wiki/w/Blood_Reaver) `nex_prison_blood_reaver`, `_boss` | Prison variant attacks normally; boss-summoned variant is encounter-owned, obeys siphon/sacrifice lifecycle, and despawns without KC/loot | Exact prison table for the normal variant; no loot for summoned adds |
| Utility NPC | [Ashuelot Reis](https://oldschool.runescape.wiki/w/Ashuelot_Reis) `nex_story_npc_outer`, `_inner`, `_1op`, `_3op`; `nex_messenger` | Frozen Door dialogue progression, safe-room bank/collect operations, correct varbit transforms; never attackable | None |

Nex attack checklist, including every animation/effect path:

- [ ] **Shared targeting:** spawn intro; nearest/lowest-relevant-defence target
  selection; melee triple follow-up while the target remains adjacent; magic in
  Smoke/Blood/Ice/Zaros and ranged in Shadow; correct partial prayer reduction;
  spectral-spirit-shield halving with odd-drain random rounding; 8-tick
  out-of-combat leap; run/leap pathing over the room pits.
- [ ] **Smoke (3,400–2,720 HP):** Smoke Barrage and poison; opening “Let the
  virus flow through you” cough application, adjacency spread, stat drain,
  duration reset, raw highest-attack-bonus selection and cough graphics/sounds;
  random six-tile Drag with a 1/4 base or protected 1/8 chance; “There is no escape” centre teleport and
  dash/trample for up to 50 with forced movement and safe-line handling; Fumus
  activation.
- [ ] **Shadow (2,720–2,040):** ranged shadow shot whose damage and darkness
  scale with distance; prayer drain on a successful shot; “Embrace darkness”
  room darkening and adjacent damage; one Shadow Smash warning tile per player
  (including overlapping stacked shadows) and delayed eruption up to 50; Umbra
  activation.
- [ ] **Blood (2,040–1,360):** 3x3 Blood Barrage, healing and prayer drain;
  “A siphon will solve this” kneel/immunity window, team-size-capped Blood
  Reaver summons, damage-to-Nex healing, and surviving-reaver consumption;
  Blood Sacrifice red mark, escape radius/timer, up-to-80 damage, prayer drain,
  and healing; Cruor activation and add cleanup.
- [ ] **Ice (1,360–680):** Ice Barrage, 15-tick freeze, prayer drain, immunity;
  “Contain this” 5x5 icicle burst up to 60 and protection-prayer deactivation;
  targeted Ice Prison stalagmites, ally break interaction, timeout hit up to 75,
  and graphics/collision cleanup; Glacies activation.
- [ ] **Zaros (680–0):** stat restoration/empowerment, Turmoil, magic/melee
  selection, Soul Split healing, Deflect Melee reflection, overhead rotation
  after four player attacks, and correct reduced protection-prayer values.
- [ ] **Death:** `nex_dying`/Wrath warning, radius and delayed damage, all phase
  object/add/overlay cleanup, scoreboard, loot contribution, respawn, empty-room
  reset, logout/death handling, and re-entry safety.

Reference the main [Nex mechanics](https://oldschool.runescape.wiki/w/Nex#Fight_overview)
and [Nex strategy mechanics](https://oldschool.runescape.wiki/w/Nex/Strategies)
pages. The reviewed Nex page was revision **15293911**, fetched 2026-08-12.

## Required drop tables

### Four original generals

Each boss gets its own 127-roll regular table; never share one approximation.
The following list is the minimum exact ledger. Also implement the page's GWD
rare/gem table and independent tertiary rolls.

- [ ] **General Graardor:** big bones; Bandos chestplate/tassets/boots each
  1/381, Bandos hilt 1/508, each shard 1/762; rune longsword, rune 2h,
  rune platebody 8/127 each, rune pickaxe 6/127; grimy snapdragon x3,
  snapdragon seed, super restore(4) x3, noted adamantite ore x15–20, noted coal
  x115–120, noted magic logs x15–20, nature runes x65–70 at 8/127 each; exact
  two coin bands; elite clue 1/250, long bone 1/400, pet 1/5,000, curved bone
  1/5,012.5, and conditional Brimstone/frozen-key drops. Source:
  [Graardor drops](https://oldschool.runescape.wiki/w/General_Graardor#Drops),
  revision 15298421.
- [ ] **Kree'arra:** big bones, feathers x1–16; Armadyl helmet/chestplate/
  chainskirt each 1/381, hilt 1/508, each shard 1/762; black d'hide body, rune
  crossbow, mind runes x586–601, rune arrows x100–105, runite bolts x20–25,
  dragonstone bolts (e) x5–10, ranging potion(3) x3 plus super defence(3) x3,
  noted grimy dwarf weed x8–13, dwarf weed seeds x2 at 8/127; crystal key and
  yew seed 1/127; exact coin bands and independent tertiaries. Source:
  [Kree'arra drops](https://oldschool.runescape.wiki/w/Kree%27arra#Drops),
  revision 15298481.
- [ ] **Commander Zilyana:** bones; Saradomin sword 1/127, Saradomin's light
  1/254, Armadyl crossbow and Saradomin hilt 1/508, each shard 1/762; adamant
  platebody, rune darts x35–40, rune kiteshield, rune plateskirt, prayer
  potion(4) x3, paired super defence(3)/magic potion(3), noted diamonds x6, law
  runes x95–100, noted grimy ranarr x5, ranarr seeds x2 at 8/127; paired
  Saradomin brew(3)/super restore(4) x3 at 6/127; magic seed 1/127; exact coins
  and tertiaries. Source:
  [Zilyana drops](https://oldschool.runescape.wiki/w/Commander_Zilyana#Drops),
  revision 15298455.
- [ ] **K'ril Tsutsaroth:** infernal ashes; steam battlestaff and Zamorakian
  spear 1/127, Staff of the dead and Zamorak hilt 1/508, each shard 1/762;
  adamant arrows(p++) x295–300, rune scimitar, adamant platebody, paired super
  attack(3)/super strength(3), paired super restore(3)/Zamorak brew(3), noted
  grimy lantadyme x10, lantadyme seeds x3, death runes x120–125, blood runes
  x80–85 at 8/127; rune platelegs 7/127, dragon dagger(p++) 2/127; exact coins
  and tertiaries. Source:
  [K'ril drops](https://oldschool.runescape.wiki/w/K%27ril_Tsutsaroth#Drops),
  revision 15298476.

Frozen key pieces are conditional on [The Frozen Door](https://oldschool.runescape.wiki/w/The_Frozen_Door)
miniquest state. Brimstone keys require the correct Konar task. Clues, champion
scrolls, long/curved bones, and pets are independent tertiary rolls and require
their own prerequisite/collection behavior; unsupported prerequisites must be
implemented or explicitly fail the phase gate, never silently omitted.

### Original bodyguards

The three guards in each faction share a regular table but keep the listed
per-NPC tertiary difference.

- [ ] **Bandos trio:** bones; BCP/tassets/boots each 1/16,256; each shard
  1/1,524; steel arrows x95–100 7/127; steel darts, nature runes x15–20,
  cosmic runes x25–30, sharks x2, chilli potatoes x3, noted limpwurt roots x5
  at 8/127; combat potion(3) and super strength(3) 2/127; exact 1,400–1,500
  coin fallback; hard clue 1/128, key piece 1/20, champion scroll 1/5,000;
  Strongstack kebab, Steelwill beer, or Grimspike right-eye patch at 1/6.
- [ ] **Armadyl trio:** bones and feathers x1–11; helmet/chestplate/chainskirt
  each 1/16,129; each shard 1/1,524; steel arrows x91–101 7/127; steel darts,
  smoke runes x10–15, manta rays x2, mushroom potatoes x3, noted crushed nests
  x2, noted grimy kwuarm at 8/127; exact 1,000–1,100 coin fallback; hard clue
  1/128, key piece 1/20, and conditional Brimstone key. Arrow/dart quantity is
  the feather roll plus 90.
- [ ] **Saradomin trio:** bones; Saradomin sword 3/16,129; each shard 1/1,524;
  steel arrows, steel darts, law runes x5–10, monkfish x3, summer pie, noted
  grimy ranarr, and noted unicorn horns x6 at 8/127; noted snape grass x5 at
  7/127; exact 1,300–1,400/1,400–1,500 coin fallbacks; hard clue 1/128 and key
  piece 1/20.
- [ ] **Zamorak trio:** malicious ashes; Zamorakian spear 3/16,129; each shard
  1/1,524; steel arrows x95–100 7/127; steel darts, death/blood runes x5–10,
  sharks x3, tuna potatoes x2, noted wines of Zamorak x5–10 at 8/127; combat
  super attack(3) and super strength(3) at 2/127 each; exact 1,300–1,400 coin
  fallback; hard clue 1/128 and key piece 1/20; Zakl'n also has the conditional
  lesser-demon champion scroll.

The individual Wiki pages in the boss/bodyguard table above are the sources;
the reviewed revisions are recorded there. Build faction-shared procedures only
after a row-for-row comparison proves them identical.

### Ambient combatants

- [ ] Generate an ID-version ledger for all 69 classic gamevals from the four
  spawn files and the four spiritual/Nex-prison variants. Include every
  `DropsLine`, shared table, quantity expression, notes flag, and condition from
  the linked family page above.
- [ ] Reuse the existing verified `npc_stats/<shard>/*.stats` ID joins and
  `wiki/manifest.tsv`; do not join by display name or bind all of category 422.
- [ ] Reconcile existing `wiki_aviansie.rs2`, `wiki_bloodveld.rs2`,
  `wiki_feral_vampyre.rs2`, `wiki_hobgoblin.rs2`, `wiki_icefiend.rs2`,
  `wiki_jogre.rs2`, `wiki_ork.rs2`, `wiki_pyrefiend.rs2`, and
  `wiki_werewolf.rs2` with the GWD-specific version IDs and conditional rolls.
- [ ] Give every remaining family an exact binding. Add a test which fails if
  any roster gameval has zero or multiple death handlers or reaches the generic
  bones fallback.

### Nex contribution loot

Nex does not make one ordinary ground roll. Implement the Wiki's contribution
distribution, per-player eligibility floor, MVP 10% modifier, multiple
simultaneous unique recipients, quantity scaling, ownership/visibility, and
scoreboard before enabling her drops.

- [ ] MVP-only big bones.
- [ ] Effective per-kill unique table: Ancient hilt 1/516; nihil horn and each
  damaged Torva piece 1/258; Zaryte vambraces 1/172 (1/43 total before
  contribution division, with 1:2:2:2:2:3 weights).
- [ ] Scaled regular categories: air/fire/water/blood/death/soul runes, unfinished
  dragon bolts, onyx bolts (e), steel cannonballs, noted air orbs/coal/runite
  ore/rubies/diamonds/wines of Zamorak, paired sharks/prayer potions, paired
  Saradomin brews/super restores, coins, rune sword, ecumenical key shards, and
  both nihil-shard bands exactly as listed on
  [Nex's drop table](https://oldschool.runescape.wiki/w/Nex#Drops).
- [ ] Blood essence 1/82, elite clue 1/48, and independently rolled Nexling
  1/500, each modified/distributed exactly as documented.

Source: Nex revision 15293911, plus Jagex's
[Nex Drop Table Changes & Monster Examine](https://oldschool.runescape.wiki/w/Update:Nex_Drop_Table_Changes_%26_Monster_Examine).

## Dungeon systems checklist

- [ ] **Entrance and environment:** partial Troll Stronghold (Dad defeated) or
  Ghommal's-hilt teleport route, dying knight dialogue, permanent rope state,
  cold-area stat/run/special drain and Fire of Unseasonal Warmth immunity,
  boulder/crack routes, dungeon overlay open/close, and KC reset on leaving.
- [ ] **Faction simulation:** complete four-way allegiance matrix, Zamorak
  presence inside the other wings, NPC-vs-NPC accuracy/damage without player
  side effects or loot exploits, god-item protection, and reacquisition.
- [ ] **Strongholds:** Armadyl 70 Ranged plus crossbow/mith grapple; Bandos 70
  Strength plus hammer and bang-door behavior; Saradomin 70 Agility and both
  rope transitions; Zamorak 70 Hitpoints, river crossing, prayer drain, and
  darkness/Saradomin's-light state. Enforce boostability exactly as the Wiki
  specifies.
- [ ] **Boss doors:** base 40 KC; Hard/Elite/Master/Grandmaster reductions to
  35/30/25/15; prefer sufficient KC before consuming a one-use ecumenical key;
  atomic entry, no double charge, peek count, public/private room
  selection, room capacity, re-entry/death behavior, and no door escape from
  inside.
- [ ] **Boss rooms:** multi-combat targeting, damage attribution, kill credit,
  loot ownership, empty-room reset/despawn, synchronized quartet respawn,
  private-instance teardown, logout and death relocation, and gravestone or
  death-bank placement outside the encounter.
- [ ] **Altars:** only while out of combat; ten-minute account cooldown; Prayer
  restoration plus the correct per-equipped-faction-item overboost; right-click
  teleport allowed in combat; Nex altar also restores HP, run, and special
  energy when an eligible Zaros item is worn.
- [ ] **Frozen Door/Ancient Prison:** four conditional key pieces, key assembly,
  one-time door unlock, permanent access state, 70 Agility/Hitpoints/Ranged/
  Strength requirements with only Hitpoints boostable, Zaros KC, ecumenical-key
  access, and a regression proving ancient ceremonial robes do **not** bypass
  KC in OSRS; safe rooms, Ashuelot bank/collect, ancient forge, logout
  relocation, chest/death-bank rules, barriers, and Nex instance entry.
- [ ] **Godsword loop:** shard drops, shard combinations in all valid orders,
  blade completion, all four original hilts, Ancient hilt, smithing/anvil and
  dismantle behavior, and special-attack compatibility tests.

## Animation, projectile, sound, and timing audit

The cache already exposes the following symbolic asset families. They are the
starting inventory, not proof that the current scripts use them correctly:

| Actor/attack | Sequences and spot animations which must be wired and timed |
|---|---|
| Graardor | `godwars_bandos_attack`, `_ranged`, `_defend`, `_death`, `_walk`, `_ready`, `_proj`, `_spot`; attack/hit/death/punch and magic cast/impact sounds |
| Bandos sergeants | `slice_surface_goblin_sergent_attack`, `_defend`, `_death`; Steelwill `godwars_sergeant_goblin2_sonic`, sonic launch/projectile/impact; Grimspike `godwars_sergeant_goblin3_ranged` and `godwars_goblin3_handaxe_proj` |
| Kree'arra | `godwars_armadyl_avatar_wind_attack`, `_claw_attack`, `_defend`, `_death`, `_walk`, `_ready`; wind/magic/ranged spot animations and bolt projectile/impact; avatar attack, whirlwind, wings, ranged, hit, and death sounds |
| Aviansie/bodyguards | `godwars_armadyl_{cannon,spear,sword}_attack`, `_defend`, `_death`; spear/axe/bolt launch, projectile, and hit assets; the exact variant/style mapping must replace the current shared Saradomin attack sequence |
| Zilyana | `godwars_saradomin_attack`, `_magic_attack`, `_magic_attack_spotanim`, `_defend`, `_death`, `_walk`, `_ready`; sword glow, cast/impact, hit, shield, special, and death sounds |
| Starlight/Growler/Bree | `unicorn_rework_{attack,defend,death}`; `godwars_lion_{attack,magic_attack,magic_spot,magic_proj,magic_impact,defend,death}`; `godwars_centaur_{attack_ranged,arrow_launch,arrow_proj,defend,death}` and matching sounds |
| K'ril | `godwars_zamorak_attack`, `_magic_attack`, `_magic_attack_spotanim`, `_defend`, `_death`, `_walk`, `_ready`; magic start/projectile/impact and avatar attack/death sounds; explicitly identify whether the prayer slam reuses melee or has its own cache event |
| Zamorak guards | demon melee set; `godwars_black_demon_fireball_spot`/`_proj`; `godwars_zamarok_bdygrd_ranged_spot`, `godwars_zamorak_bdygrd_ranged`, `_proj`; demon attack/death and fire cast/impact sounds |
| Spiritual/ambient NPCs | Per-family generated attack/defend/death sets plus `godwars_spiritual_ork_{ranger_ranged_attack,mage_magic_attack}` and Armadyl spiritual launch assets; resolve every missing overlay named above |
| Nex core | `nex_ready`, `_run`, `_attack`, `_alternate_cast_attack`, `_cast_attack`, `_dash_attack`, `_smash_attack`, `_blast_away`, `_spin_out`, `_blood_siphon`, `_summon`, `_turmoil`, `_defend`, `_death` |
| Nex magic/effects | Smoke, shadow, blood, ice, finale, ice-prison, siphon, Soul Split, leech, mushroom-cloud, summon, and Turmoil projectile/impact spot animations; `nex_soulsplit`/`nex_deflect` overhead transforms and corresponding heal/deflect sounds |
| Blood Reavers | `blood_reaver_{ready,walk,attack,defend,death}` and the three matching `nex2021_blood_reaver_*` sounds |

Create a generated `wiki/godwars_combat_manifest.csv` (or equivalent checked-in
ledger) with one row per cache NPC version and one row per distinct attack. At
minimum record:

`npc_id, gameval, wiki_version, attack_name, style, max_hit, attack_speed,
range, attack_seq, defend_seq, death_seq, move_seq, projectile, start_spotanim,
impact_spotanim, sound, launch_tick, impact_tick, hitsplat_tick, aoe_shape,
forced_move, secondary_effect, prayer_rule, player_or_npc_target`.

- [ ] Inventory symbolic assets from `configs/all.npc`, `all.seq`, spotanim,
  projectile, sound, and DB rows. Never hard-code numeric cache IDs in scripts.
- [ ] Reconcile the current generated animation file. In particular, explicitly
  resolve missing overlays for the Saradomin priest, Feral Vampyre, some GWD
  goblins/knights, and Saradomin/Zamorak spiritual variants.
- [ ] Verify that visually shared bodyguard animations still launch distinct
  magic/ranged effects. Replace the current generic Armadyl bodyguard attack
  binding where it cannot represent melee, magic, and ranged faithfully.
- [ ] Record and test every Graardor/Kree/Zilyana/K'ril cry and sound, Kree
  whirlwind, K'ril slam/poison, all Nex voice lines, phase transitions, cough,
  dash, shadow tiles, blood marks/reavers, ice objects, overheads, Wrath, and
  scoreboard effects.
- [ ] Add a test-only trace that logs selected attack, animation, projectile,
  target UID, launch/impact/damage ticks, effect, and final cooldown. Use it to
  compare a deterministic replay against the manifest without making production
  combat noisy.

## Implementation sequence

### Phase 0 — Freeze sources and generate coverage

1. Generate the complete NPC/spawn/attack/drop/asset crosswalk described above,
   including exact cache IDs, Wiki version IDs, revisions, and current handler
   locations.
2. Add failing coverage tests for missing/duplicate attack, animation, spawn,
   faction, KC, and drop bindings.
3. Capture deterministic reference fixtures for every boss attack and drop
   table before changing shared combat code.

Exit gate: every scoped NPC and attack has exactly one reviewed ledger row; all
unknown assets or behaviors are named blockers rather than implicit defaults.

### Phase 1 — Shared encounter and faction primitives

1. Add NPC-vs-NPC allegiance targeting, faction-item protection, correct player
   kill attribution, and neutral-NPC behavior.
2. Add reusable room membership, encounter ownership, AOE target snapshots,
   forced movement, delayed impact, synchronized respawn, reset, and instance
   teardown primitives.
3. Add data-driven attack definitions where they reduce duplication without
   hiding boss-specific state machines.

Exit gate: a mixed central-room fixture fights indefinitely without cross-room
targets, false KC, duplicate loot, or stalled combat.

### Phase 2 — Entrance, wings, KC, rooms, and altars

Implement and test every item in the dungeon-systems checklist through the four
original public rooms. Keep private rooms disabled until their ownership and
teardown tests pass.

Exit gate: a fresh account can enter, traverse every wing, earn/consume/reset
KC, use each altar/exit, die/logout safely, and cannot bypass a requirement.

### Phase 3 — All ambient combatants

Implement the main-dungeon roster family by family: exact stats/styles first,
then animations/effects, Slayer and flying gates, faction combat, and exact
drops. Run the coverage checker after each family.

Exit gate: all 69 classic gamevals have tested combat, audiovisual, KC/faction,
and drop behavior with no generic fallthrough.

### Phase 4 — Four original boss encounters

Implement one complete quartet at a time—Bandos, Armadyl, Saradomin, then
Zamorak—including boss attacks, three distinct bodyguard styles, room reset,
respawn, drops, altar, and multiplayer attribution. Do not mark a faction done
while its bodyguards still use generic visuals or loot.

Exit gate: every row in the original boss-room table and all four bodyguard
drop ledgers pass deterministic and multiplayer tests.

### Phase 5 — Frozen Door and Ancient Prison population

Implement key acquisition/assembly/unlock, maps and barriers, Zaros faction
combatants and exact drops, KC/ecumenical-key access (including the OSRS robes
non-bypass rule), safe rooms, Ashuelot, bank/collect, death bank, forge, logout
rules, and Nex-room instance allocation.

Exit gate: permanent unlock and a complete Ancient Prison trip survive relog,
restart, death, reclaim, and concurrent-player tests.

### Phase 6 — Nex encounter and contribution loot

Implement the shared targeting loop, then Smoke, Shadow, Blood, Ice, and Zaros
as explicit state-machine phases. Add each phase's mage, specials, visuals,
sounds, cleanup, and tests before moving to the next. Finish with Wrath,
scoreboard, contribution/MVP loot, pet/tertiary rolls, reset, and death bank.

Exit gate: solo test harness plus 2-, 5-, and high-player-count simulations can
complete or wipe in every phase without stale NPCs/locs, and deterministic
contribution fixtures award the exact expected recipients and quantities.

### Phase 7 — Statistical, visual, and regression sign-off

1. For every table, exhaustively test threshold coverage and mutually exclusive
   branches; run seeded high-volume simulations with confidence bounds for
   uniques, shards, tertiaries, paired drops, and Nex contribution outcomes.
2. Record video/trace captures for every distinct attack and special. Verify
   animation start, projectile travel, sound, impact, hitsplat, movement, and
   cooldown on the intended ticks.
3. Run script compilation, combat/drop coverage checkers, server unit tests,
   save/reload tests, and client compatibility on the revision-239 client.
4. Perform two-player and multi-team public/private-room soak tests, including
   simultaneous kills, door entry, logout, death, disconnect, instance owner
   loss, and server restart.

Exit gate: all completion-rule boxes are checked, all linked Wiki tables have a
revision-pinned implementation, and the repository has no documented GWD
exceptions beyond the explicitly out-of-scope Wilderness dungeon.

## Required test matrix

- [ ] One deterministic success, miss, protected hit, unprotected hit, maximum
  hit, and effect-immunity case for every attack style/effect.
- [ ] One animation/projectile/sound tick trace for every distinct attack and
  every boss special.
- [ ] Solo, two-player, and many-player targeting/AOE tests for every general.
- [ ] Bodyguard assist, corner pathing, death-before-boss, death-after-boss,
  synchronized respawn, and empty-room reset for all four original quartets.
- [ ] All five Nex phases entered from both threshold-crossing damage and exact
  threshold HP; phase mage cannot be skipped; simultaneous damage cannot skip a
  phase; every temporary add/loc/overlay is cleaned on victory, wipe, logout,
  and reset.
- [ ] Every faction item combination and equipment-change edge case; every
  Slayer gate; ordinary melee rejection against all Aviansie variants.
- [ ] Every access requirement at level below/exact/boosted-above as applicable;
  atomic KC/key consumption under duplicate inputs.
- [ ] Every always/unique/regular/shared/tertiary drop row reachable at boundary
  values; every noted/stack/paired quantity correct; every prerequisite toggled.
- [ ] Nex equal/unequal contribution, MVP tie policy, eligibility threshold,
  multiple unique winners, death/disconnect, inventory-full, iron/group mode,
  and pet/tertiary independence.
- [ ] No cross-instance targeting, projectile, floor object, KC, altar cooldown,
  death-bank, scoreboard, or reset leakage.

## Reference index

- [God Wars Dungeon overview, access, factions, rooms, and combatants](https://oldschool.runescape.wiki/w/God_Wars_Dungeon)
- Boss strategy/mechanics pages: [Graardor](https://oldschool.runescape.wiki/w/General_Graardor/Strategies), [Kree'arra](https://oldschool.runescape.wiki/w/Kree%27arra/Strategies), [Zilyana](https://oldschool.runescape.wiki/w/Commander_Zilyana/Strategies), and [K'ril](https://oldschool.runescape.wiki/w/K%27ril_Tsutsaroth/Strategies)
- [The Frozen Door](https://oldschool.runescape.wiki/w/The_Frozen_Door)
- [Ancient Prison](https://oldschool.runescape.wiki/w/Ancient_Prison)
- [Nex](https://oldschool.runescape.wiki/w/Nex) and [Nex strategies](https://oldschool.runescape.wiki/w/Nex/Strategies)
- [God equipment and aggression protection](https://oldschool.runescape.wiki/w/God_Wars_Dungeon#God_equipment)
- [Fire of Unseasonal Warmth](https://oldschool.runescape.wiki/w/Fire_of_Unseasonal_Warmth)
  and the [old fire-pit recipe](https://oldschool.runescape.wiki/w/Old_fire_pit)
- [Ghommal's hilt teleport tiers](https://oldschool.runescape.wiki/w/Ghommal%27s_hilt)
- [Saradomin's light and Zamorakian darkness](https://oldschool.runescape.wiki/w/Saradomin%27s_light)
- [Godsword shard 1](https://oldschool.runescape.wiki/w/Godsword_shard_1), [shard 2](https://oldschool.runescape.wiki/w/Godsword_shard_2), and [shard 3](https://oldschool.runescape.wiki/w/Godsword_shard_3)
- Original boss/bodyguard and ambient family pages linked directly in the roster
  tables above; those links, not this summary, define the complete drop rows.
