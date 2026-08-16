# Skills content port queue

Agent-loop state for **finishing incomplete skill systems** in OSRS-Content.
The OSRS wiki (`https://oldschool.runescape.wiki`) is the **gap authority** —
what training methods, unlocks, and systems exist. LostCity / 2009scape /
Kronos remain the **implementation shape** by era. When refs and the osrs239
cache disagree, **the cache wins** for wire and ids; refs win only for
*policy* the cache does not state.

Parallel to:

- [`CONTENT_PORT_QUEUE.md`](CONTENT_PORT_QUEUE.md) — LostCity → tree
- [`SCAPE2009_CONTENT_PORT_QUEUE.md`](SCAPE2009_CONTENT_PORT_QUEUE.md) — mid-era
- [`KRONOS_CONTENT_PORT_QUEUE.md`](KRONOS_CONTENT_PORT_QUEUE.md) — post-2009
- [`QUESTHELPER_CONTENT_PORT_QUEUE.md`](QUESTHELPER_CONTENT_PORT_QUEUE.md) — quests only
- [`HERBLORE_PORT_QUEUE.md`](HERBLORE_PORT_QUEUE.md) — owns Finish-queue #100–106

**Do not steal slices** already `done` / owned on another queue. Route with
`blocked → <queue> §N` instead of duplicating work.

Two phases:

1. **Audit loop** — **complete** (23/23 skills). One skill per tick; wiki vs
   in-tree; emit Finish-queue slices. **Does not port.** Do not re-arm.
2. **Port loop** — **active**. One pending Finish-queue slice per tick per
   `docs/PORTING_GUIDE.md` §4 / §4.7 / §7. Sentinel:
   `AGENT_LOOP_WAKE_skills_port` (~90–120s).

Status: `pending` | `in_progress` | `done` | `blocked`.

## Shared tree — never silence another lane

**Do not ever** `.rs2.skip` / `dirname.skip` / move / delete sibling content
(`skill_construction/`, `minigame_mta/`, or any other live tree) to green
`sscompile`. Fix your slice. See PORTING_GUIDE §7 and
`.cursor/rules/no-park-sibling-content.mdc`.

**Audit loop prompt** (complete — do not re-arm): historical only.

**Port loop prompt** (active): read this file + PORTING_GUIDE §4 / §4.7 / §7;
claim next pending unblocked Finish-queue slice per **Slice selection** below;
Grep LostCity then 2009scape then Kronos; NEVER park sibling lanes; verify
(`mock230_pack --check-only`, `make -C src mock230-scripts`); mark `done` +
Log; re-arm `AGENT_LOOP_WAKE_skills_port` (~120s). **Stop re-arming** when no
`pending` unblocked rows remain (only `blocked` / `done` left).

### Slice selection (fixed)

1. Lowest `#` with Status `pending` (skip `blocked` / `done` / `in_progress`).
2. If that row’s Notes say it **needs** another pending slice (e.g. #18 → #1),
   claim the dependency first instead.
3. Mark claimed row `in_progress` immediately.
4. Do **not** implement `blocked → other queue` rows here.

## Methodology (non-negotiable)

### Audit ticks

1. Claim next unaudited skill in the **Audit roster** (`in_progress`).
2. Fetch wiki: `https://oldschool.runescape.wiki/w/<Skill>` and
   `…/w/<Skill>/Training` when present (cite URLs in Notes). Combat skills
   (Attack/Strength/Defence/Hitpoints) use
   `Free-to-play_Melee_training` / `Pay-to-play_Melee_training` instead —
   `/Attack/Training` etc. 404.
3. Inventory in-tree `skill_*` (file list, deferred comments) and harvest
   deferred Notes for that skill across CONTENT / SCAPE2009 / KRONOS.
4. Grep LostCity `content/scripts/skill_*` for parity baseline; route LC-owned
   gaps as `blocked → CONTENT_PORT_QUEUE` rather than duplicating.
5. Emit **finish slices** as Finish-queue rows (method / system sized), each
   with Era ref `LC` / `2009` / `Kronos` / `wiki+cache`, Status `pending` or
   `blocked → <other queue §N>`, and Notes naming the wiki gap + harvested
   deferred bullets.
6. Mark the skill’s Audit roster row `done`; append Log; re-arm.

### Port ticks (active)

1. Claim per **Slice selection** above.
2. **Grep LostCity first** (`PORTING_GUIDE` §2.2). State in one sentence where
   the behaviour lives. LC shape → port via pack names (this queue owns the
   finish slice; do not bounce to CONTENT unless Notes already say
   `blocked → CONTENT`).
3. **Grep 2009scape second.** Mid-era skill → prefer 2009scape shape.
4. **Kronos only for post-2009** skill gaps neither tree implements.
5. **No game-facing strings / ids / config constants in C.** Express as `.rs2`
   + configs. New Server VM opcodes only when content cannot say it
   (`PORTING_GUIDE` §2.4 / §2.5) — plan + implement in the same slice (opcode
   gap log).
6. **Resolve names through the pack** — never copy reference numeric ids.
7. **Wiki for rates / unlocks / UI contracts** when refs disagree or are
   stub-only. Do not invent training methods the cache cannot express.
8. **Interfaces:** drive the rev-230 panel; do not invent IF1
   (`UI_ERA_PORTING_GUIDE.md`).
9. **Never park sibling lanes** (PORTING_GUIDE §7).
10. Done = behaviour in tree, pack `--check-only` 0 errors, scripts green,
    Finish row `done`, Log line appended.

## Skip list (out of scope)

| Item | Why skip |
|---|---|
| Sailing | Explicitly excluded from this queue |
| Fist of Guthix / RS2-only | not in OSRS |
| ~~Summoning~~ | **no longer skipped** — flagged port, own queue: [`SUMMONING_PORT_QUEUE.md`](SUMMONING_PORT_QUEUE.md) |
| Donor / loyalty / custom private-server skill packs | skip-lists on Kronos/2009 |
| Quest-gated skill arms owned by QUESTHELPER | route there |
| Live POH (`skill_construction/` 4a+4b) / live MTA | do not re-edit from this lane; residual gaps → `blocked` redirects |

## Ownership vs other queues

| Era / topic | Owner |
|---|---|
| Classic F2P skill cores already in CONTENT 7a–8u | CONTENT (done — track *deferred* follow-ups here only) |
| Farming / Hunter / Construction mid-era | SCAPE2009 |
| Slayer masters / rooftops / modern skill areas / Motherlode / Wintertodt | KRONOS |
| Pure wiki/cache gaps neither ref implements | This queue (`wiki+cache`) |

Combat skills (Attack / Strength / Defence / Hitpoints / Ranged) audit as
**separate roster rows** but all map into `skill_combat/`. Magic non-combat
lives in `skill_magic/`; combat casting cross-checks `skill_combat/`.

## Audit roster

Ordered F2P then members; combat skills first within F2P. Sailing omitted.

| # | Skill | Tree | Wiki | Audit status | Notes |
|---|---|---|---|---|---|
| 0 | Queue tracker | — | — | done | This file + PORTING_GUIDE §4.7 |
| 1 | Attack | `skill_combat/` | [Attack](https://oldschool.runescape.wiki/w/Attack) · [F2P Melee](https://oldschool.runescape.wiki/w/Free-to-play_Melee_training) · [P2P Melee](https://oldschool.runescape.wiki/w/Pay-to-play_Melee_training) | done | Core melee + accurate/controlled XP + combat tab live; finish slices 1–5; `/Training` 404 → use Melee training guides |
| 2 | Strength | `skill_combat/` | [Strength](https://oldschool.runescape.wiki/w/Strength) · [F2P Melee](https://oldschool.runescape.wiki/w/Free-to-play_Melee_training) · [P2P Melee](https://oldschool.runescape.wiki/w/Pay-to-play_Melee_training) | done | Aggressive/controlled XP + maxhit + Str reqs live; finish slices 6–8; WG → existing #4 |
| 3 | Defence | `skill_combat/` | [Defence](https://oldschool.runescape.wiki/w/Defence) · [F2P Melee](https://oldschool.runescape.wiki/w/Free-to-play_Melee_training) · [P2P Melee](https://oldschool.runescape.wiki/w/Pay-to-play_Melee_training) | done | Defensive/controlled XP + armour reqs + protect prayers live; magic def 7:3 gap #9; finish #9–11 |
| 4 | Hitpoints | `skill_combat/` | [Hitpoints](https://oldschool.runescape.wiki/w/Hitpoints) · [F2P Melee](https://oldschool.runescape.wiki/w/Free-to-play_Melee_training) · [P2P Melee](https://oldschool.runescape.wiki/w/Pay-to-play_Melee_training) | done | Combat HP XP + basic food + ring of life + monk heal live; finish #12–15 |
| 5 | Ranged | `skill_combat/` | [Ranged](https://oldschool.runescape.wiki/w/Ranged) · [F2P Ranged](https://oldschool.runescape.wiki/w/Free-to-play_Ranged_training) · [P2P Ranged](https://oldschool.runescape.wiki/w/Pay-to-play_Ranged_training) | done | F2P bow+arrow + XP styles live (CONTENT 8u); finish #16–22; specs→#1 |
| 6 | Prayer | `skill_prayer/` | [Prayer](https://oldschool.runescape.wiki/w/Prayer) · [Training](https://oldschool.runescape.wiki/w/Prayer/Training) | done | Toggle/drain/bury/altar/quickprayer/Redemption live; finish #23–29 |
| 7 | Magic | `skill_magic/` + `skill_combat/` | [Magic](https://oldschool.runescape.wiki/w/Magic) · [Training](https://oldschool.runescape.wiki/w/Magic/Training) | done | F2P utility+strike→wave live (8p/8q); finish #30–36; magic def→#9; MTA live do not park |
| 8 | Runecraft | `skill_runecraft/` | [Runecraft](https://oldschool.runescape.wiki/w/Runecraft) · [Training](https://oldschool.runescape.wiki/w/Runecraft/Training) | done | #37–40, #42 landed (tiara, pure/daeyalt essence, Kourend dark essence, pouches, Abyss, Ourania, combo runes, cape). Wrath/true-blood altar rows blocked on real coordinates; #41 (GotR) stays a minigame. See RUNECRAFT_COMPLETION_PLAN.md |
| 9 | Crafting | `skill_crafting/` | [Crafting](https://oldschool.runescape.wiki/w/Crafting) · [Training](https://oldschool.runescape.wiki/w/Crafting/Training) | done | Broad LC suite live (pottery/gems/leather/jewellery/glass/…); finish #43–48 |
| 10 | Mining | `skill_mining/` | [Mining](https://oldschool.runescape.wiki/w/Mining) · [Training](https://oldschool.runescape.wiki/w/Mining/Training) | done | Clay..runite+blurite+prospect live; finish #49–55; Motherlode→KRONOS done |
| 11 | Smithing | `skill_smithing/` | [Smithing](https://oldschool.runescape.wiki/w/Smithing) · [Training](https://oldschool.runescape.wiki/w/Smithing/Training) | done | SMITHING_COMPLETION_PLAN.md S1–S6,S8,S9,S11,S12,S14 landed; S7 quest-lane, S10 blocked (no Barbarian Training miniquest), S13 (IF 312) separable UI project |
| 12 | Fishing | `skill_fishing/` | [Fishing](https://oldschool.runescape.wiki/w/Fishing) · [Training](https://oldschool.runescape.wiki/w/Fishing/Training) | done | Full member roster + barbarian + guild + cape/outfit live (#63-65,68 done); #66 movement lands, #67 blocked on Miscellania; Trawler/Tempoross remain #69-70 |
| 13 | Cooking | `skill_cooking/` | [Cooking](https://oldschool.runescape.wiki/w/Cooking) · [Training](https://oldschool.runescape.wiki/w/Cooking/Training) | done | F2P cook+dough/wine+gnome live; gauntlets/cookomatic wired; finish #71–77 |
| 14 | Firemaking | `skill_firemaking/` | [Firemaking](https://oldschool.runescape.wiki/w/Firemaking) · [Training](https://oldschool.runescape.wiki/w/Firemaking/Training) | done | Normal→magic logs live; finish #78–84; Wintertodt→KRONOS |
| 15 | Woodcutting | `skill_woodcutting/` | [Woodcutting](https://oldschool.runescape.wiki/w/Woodcutting) · [Training](https://oldschool.runescape.wiki/w/Woodcutting/Training) | done | Normal→magic+hollow+WC Guild gates live; finish #85–91 |
| 16 | Agility | `skill_agility/` | [Agility](https://oldschool.runescape.wiki/w/Agility) · [Training](https://oldschool.runescape.wiki/w/Agility/Training) | done | Gnome+8 rooftops+MoG+some shortcuts live; finish #92–99 |
| 17 | Herblore | `skill_herblore/` | [Herblore](https://oldschool.runescape.wiki/w/Herblore) · [Training](https://oldschool.runescape.wiki/w/Herblore/Training) | done | Clean/grind/unf+many finishes live; Drink→#3 family; finish #100–106 |
| 18 | Thieving | `skill_thieving/` | [Thieving](https://oldschool.runescape.wiki/w/Thieving) · [Training](https://oldschool.runescape.wiki/w/Thieving/Training) | done | Classic pickpocket/stalls/chests/doors live; finish #107–114; PP→SCAPE2009 |
| 19 | Fletching | `skill_fletching/` | [Fletching](https://oldschool.runescape.wiki/w/Fletching) · [Training](https://oldschool.runescape.wiki/w/Fletching/Training) | done | Full recipe set landed (bows/crossbows/arrows/bolts/darts/javelins/ogre+brutal/amethyst/mith grapple/ballistae/blowpipe/shields/battlestaff/cape). `map_members` is now a real world flag (`src/net/mock/mock230.h`), members-on by default. `::fletchingrun` selftest passing. Plan: [`FLETCHING_COMPLETION_PLAN.md`](FLETCHING_COMPLETION_PLAN.md); #115–122 done |
| 20 | Slayer | `skill_slayer/` | [Slayer](https://oldschool.runescape.wiki/w/Slayer) · [Training](https://oldschool.runescape.wiki/w/Slayer/Training) | done | Masters/assign/kill/points/ops/rewards IF live (KRONOS); finish #123–130 |
| 21 | Farming | `skill_farming/` | [Farming](https://oldschool.runescape.wiki/w/Farming) · [Training](https://oldschool.runescape.wiki/w/Farming/Training) | done | Classic patches (SCAPE2009 §1a–1g) live; finish #131–138 |
| 22 | Construction | `skill_construction/` | [Construction](https://oldschool.runescape.wiki/w/Construction) · [Training](https://oldschool.runescape.wiki/w/Construction/Training) | done | Live POH 4a+4b; finish #139–145 redirects; do not park tree |
| 23 | Hunter | `skill_hunter/` | [Hunter](https://oldschool.runescape.wiki/w/Hunter) · [Training](https://oldschool.runescape.wiki/w/Hunter/Training) | done | Snare/box/impling/falconry+Puro live (SCAPE2009); finish #146–153; plan: [`HUNTER_COMPLETION_PLAN.md`](HUNTER_COMPLETION_PLAN.md) |

## Finish queue

Slices emitted by audit ticks. Port loop (active) consumes one `pending`
unblocked Finish-queue slice per tick (deps-first selection).
unblocked row per tick.

| # | Slice | Era ref | Status | Notes |
|---|---|---|---|---|
| 0 | Queue tracker | — | done | This file |
| 1 | skill_combat: player special attacks (core) | LC | done | Energy model + toggle (`specwep.rs2`), combat hook, PvM dds/dlong/dmace/claws, instant dbaxe/Excalibur; ranged/spear/halberd → #18 / later; `sa_kind` for trailing-`+` poison names |
| 2 | skill_combat: PvP melee | LC | blocked | Needs secondary-player dialect (`.stat` / `.%varp` / `p_opplayer`) + `MOCK230_PLAYER_MAX>1`; combat_stats.rs2 documents `.` variants deliberately not ported. Host gap — not a content-only finish. Re-open when multi-player active player lands. |
| 3 | Attack potion consume | LC | done | Drink for 1–4dose attack + super attack (`attack_potion.rs2`); wiki +3/+10% and +5/+15%; dose switch ladder (anti_poison pattern). Combat/divine/zamorak → later slices |
| 4 | Warriors' Guild Attack activities remainder | Kronos | blocked | → [`KRONOS_CONTENT_PORT_QUEUE.md`](KRONOS_CONTENT_PORT_QUEUE.md) §11 deferred: animator/dummy/catapult/token earn + basement dragon; cyclops core already done |
| 5 | Attack cape perk | wiki+cache | done | Worn Boost via shared `skillcape_boost.rs2` (+1 Attack); host worn op2 routing |
| 6 | Strength potion consume | LC | done | `strength_potion.rs2` — strength4/+3/+10%, super +5/+15%; dose ladder (#3 pattern) |
| 7 | Strength cape perk | wiki+cache | done | Shared `skillcape_boost.rs2` (+1 Strength); WG Teleport deferred |
| 8 | Non-combat Strength training | SCAPE2009 | blocked | Barbarian/heavy-rod fishing is mid-era (`2009scape` `barbfishing/`); → SCAPE2009 fishing lane. Str door-forcing / barehand remains wiki+cache when locs identified — do not invent methods. |
| 9 | skill_combat: magic defence 7:3 blend | LC | done | `%com_magicdef` uses LC `7*magic+3*defence)/10` effective level +8 (`combat_stats.rs2`) |
| 10 | Defence potion consume | LC | done | `defence_potion.rs2` — def +3/+10%, super +5/+15%; dose ladder |
| 11 | Defence cape perk | wiki+cache | done | Shared `skillcape_boost.rs2` (+1 Defence); Excalibur +8 already in #1 |
| 12 | Hitpoints regen (Rapid Heal / cape / bracelet) | LC | done | `health_regen.rs2` timer 100t / Rapid Heal 50t; cape/bracelet doubling → #15 |
| 13 | Food consume remainder + overheal | LC | done | Kebab random table (`kebab.rs2`); anglerfish + Sara brew overheal via `stat_boost(hitpoints,…)` (heal clamps at base). Members heal-amount table still sparse in `food.rs2` — expand as wiki rates measured. |
| 14 | Life leech (Guthan's / blood / SGS) | SCAPE2009+Kronos | done | Guthan's 25% heal-on-hit (`guthan_set.rs2`, mid-era ArmourSet); SGS Healing Blade `sa_kind=5` (`pvm_sgs.rs2`). Ancient blood spells → #31. |
| 15 | Hitpoints cape / desert heat / phoenix neck | wiki+cache | done | Cape/bracelet half regen interval (`health_regen.rs2`); phoenix necklace ≤20% → heal 30% base (`phoenix_necklace.rs2`). Desert heat already live (`area_desert/desert_heat.rs2`). |
| 16 | skill_combat: crossbow / thrown / chinchompa | LC | done | Bolt + dart/knife rows in `ranged_ammo.dbrow`; combat path already branched for crossbow/thrown. Chin multi-target still absent — follow-up when huntall/AoE ready. |
| 17 | Ranged ammo ground recovery | LC | done | `ranged_dropammo_npc` wired to `inv_dropitem_delayed` + `^dropammo_chance` (LC `player_ranged.rs2`). Holy water / PvP deferred with #16. |
| 18 | Ranged specials (MSB/MLB/dark bow/…) | LC | done | Snapshot (`pvm_magic_shortbow.rs2`) + Powershot (`pvm_magic_longbow.rs2`); sa_kind 6/7. Rune thrownaxe bounce + dark bow deferred (multi-hunt). |
| 19 | Dwarf multicannon | LC | blocked | Setup/fire is a large LC `quest_mcannon` / cannon loc system; smithing cannonballs already noted CONTENT 7g deferred. → CONTENT_PORT_QUEUE when 7g opens; do not invent a thin stub. |
| 20 | Ava's accumulator / ranging cape ammo-save | wiki+cache | done | `~avas_ammo_saved` in `ranged_dropammo_npc` — attractor/accumulator/assembler + ranging cape save rates (wiki). |
| 21 | Ranging potion consume | LC | done | `ranging_potion.rs2` — `4doserangerspotion`… +3/+10% Ranged; dose ladder (#3 pattern). Bastion/divine deferred. |
| 22 | Enchanted bolt tips effects | SCAPE2009 | done | PvM procs from 2009scape `BoltEffect` (`enchanted_bolts.rs2`): opal/pearl/ruby/diamond/dragonstone/onyx. Jade/sapphire/topaz/emerald deferred (PvP / npc poison). |
| 23 | Prayer bone XP table + ashes | LC | done | Expanded bury XP switch (dragon+/wyvern/lava/superior/dagannoth/zogre ancestral/wyrm…). Demonic ash scatter absent in this cache naming — deferred. |
| 24 | Ectofuntus / Chaos altar bone offer | wiki+cache | done | Ectofuntus worship 4× XP + grinder + slime fill (`ectofuntus.rs2`, 2009scape Phasmatys); Chaos Temple `chaosaltar` offer 350% + 50% save (`chaos_altar_offer.rs2`). Shared `bone_xp.rs2`. Disciple ectotokens deferred. |
| 25 | Gilded altar / POH incense | 2009 | blocked | → SCAPE2009 Construction POH remainder (4c owned elsewhere); do not edit live `skill_construction/` from this lane |
| 26 | Prayer potion / restore consume | LC | done | `prayer_potion.rs2` — prayer +7/+25%, super restore +8/+25% all skills; dose ladders. Sanfew → follow-up. |
| 27 | Smite / Piety / Rigour combat effects | LC | done | Chivalry/Piety/Rigour/Augury (+ eagle/mystic ladder) wired in `combat_stats` prayer multipliers. Smite PvP drain still needs #2 multi-player. |
| 28 | Retribution multi-target AoE | Kronos | done | Multi: `map_multiway` → `npc_huntall`/`huntall` within 1 (`prayer_effects.rs2`); single keeps `%aggressive_npc` ≤1. LC none; 2009scape surround + Kronos multi players. |
| 29 | Prayer cape / bones-to-peaches | wiki+cache | done | Prayer cape Boost +1 (`skillcape_boost.rs2`); peaches already live (`convert_bones.rs2` / MAGIC M5). |
| 30 | Magic autocast IF | MAGIC | blocked | → MAGIC **M6** done (`auto_cast.rs2`; strike→wave index map; staff-set/ancient grids deferred) |
| 31 | Ancient Magicks spellbook | MAGIC | blocked | → MAGIC **M7** |
| 32 | Lunar / Arceuus spellbooks | MAGIC | blocked | → MAGIC **M8** / **M9** |
| 33 | God / Iban / Crumble Undead | MAGIC | blocked | → MAGIC **M1**+**M3**+**M4** done |
| 34 | Magic utility remainder | MAGIC | blocked | → MAGIC **M5** done (peaches + Magic Dart; PvP/post-LC teles deferred) |
| 35 | Magic potion consume | MAGIC | blocked | → MAGIC **M2** done (`magic_potion.rs2`); battlemage/divine deferred in plan |
| 36 | Magic cape / surge spells | MAGIC | blocked | → MAGIC **M10** |
| 37 | Runecraft tiara craft + pure essence | wiki+cache | done | Tiara crafting, pure/daeyalt essence ladder, and the `rc_no_tally_required_*` varbit refresh (unblocking tiara `Enter`) landed. See RUNECRAFT_COMPLETION_PLAN.md §1.3, §2 |
| 38 | Blood / Soul / Wrath altars | wiki+cache | done | Kourend dark essence chain (mine→infuse→chisel→bind) fully landed. True wrath/blood altars: param+category landed, dbrow rows blocked on real-world coordinates this port has no way to source from text configs — see §4 |
| 39 | Ourania / Zeah RC + abyss | wiki+cache | done | Ourania altar (approximated output weights, flagged), essence pouches (fill/empty/check/degrade), and the Abyss (obstacle course + rifts + Dark mage repair) all landed. See RUNECRAFT_COMPLETION_PLAN.md §6–§8 |
| 40 | Essence mine teleporter remainder | wiki+cache | done | Correction: all five (Aubury/Sedridor/Distentor/Cromperty/Brimstail) + Aubury's shop were already live in areas/ — queue note was stale. See RUNECRAFT_COMPLETION_PLAN.md §3 |
| 41 | Guardians of the Rift | wiki+cache | blocked | Wiki primary modern RC training; Temple of the Eye quest gate — no minigame tree. Scoped out of RUNECRAFT_COMPLETION_PLAN.md §8 as its own minigame build, not a skill-interaction slice |
| 42 | Runecraft cape / combination runes | wiki+cache | done | Combination runes (mist/dust/mud/smoke/steam/lava) + binding necklace, and the Runecraft cape's +1 boost, landed. Cape's guild-teleport perk deferred, same coordinate gap as #38. See RUNECRAFT_COMPLETION_PLAN.md §5 |
| 43 | Crafting weaving (loom) | wiki+cache | done | `skill_crafting/scripts/weaving/weaving.rs2` — 7 products, `[oploc1,_loom]` on the shared `loom`/`regicide_loom` category (971→2299 in pack/category.pack). See CRAFTING_COMPLETION_PLAN.md §1.2/S4 |
| 44 | Crafting IF + batch craft polish | LC | pending | Interface 270 `skillmulti` still unbuilt — out of scope for this pass. All new S2–S15 menus instead follow the existing `~p_choice*` pagination convention (see leather.rs2's `craft_leather_menu`), which the plan documents as the stopgap until S1 lands |
| 45 | Glass lens / lantern remainder | LC | done | 5 new blows (candle lantern, oil lamp, fishbowl, lantern lens, light orb) + oil lantern assembly. Lens mould→telescope disc and the light-orb wire assembly stay deferred — no "filled" light-orb obj or bullseye-nolens source in this cache. See CRAFTING_COMPLETION_PLAN.md §1.11/S5 |
| 46 | Modern gems / zenyte jewellery | wiki+cache | done | Onyx/zenyte gem cutting; full onyx/zenyte ring/necklace/amulet/bracelet ladder; gold bracelet column; gold tiara; both slayer rings; opal/jade/red-topaz silver jewellery (12 rows). See CRAFTING_COMPLETION_PLAN.md §1.12–§1.14/S6–S8 |
| 47 | Pottery urns / modern pottery | wiki+cache | done | Row's premise was wrong — OSRS has no urns. Added empty cup (4-per-clay) + empty plant pot; fixed the pot lid, which was level 3/2.5+2.5 XP against the wiki's level 25/20+20. See CRAFTING_COMPLETION_PLAN.md §1.3/S2 |
| 48 | Crafting cape | wiki+cache | done | +1 Crafting worn Boost (wired into the shared `skillcape_boost.rs2`, mirroring Herblore's), door alternates (golden apron / cape / max cape), Master Crafter sells cape+hood at 99 for 99,000gp. Guild-teleport perk and the mould/tool respawns stay deferred (no verified in-world coordinate, same gap RUNECRAFT_COMPLETION_PLAN.md §4 already flags). See CRAFTING_COMPLETION_PLAN.md §1.20/S15 |
| 49 | Essence / pure essence rocks | LC | done | blankrunestone/big_essence_rock/lunar_runestone_top wired (get_ore_essence); 30+Mining members branch to blankrune_high |
| 50 | Gem rocks + glory gem table | LC | done | gemrock/gemrock1 (get_ore_gem_rock) + gem_rock_table drop roll (~roll_on_drop_table, new) + 1/256→1/86 gem-find on ordinary rocks; village_gem_rock1-3 deferred (unverified mechanic) |
| 51 | Members ore rocks (limestone/sandstone/granite/amethyst) | wiki+cache | done | limestone×3 + Trahaearn soft clay + Desert Quarry sandstone/granite (weighted tier, get_ore_quarry) + amethyst live; quest_enakhraslament's guaranteed-material shortcut replaced by real mining |
| 52 | Higher pickaxes (black/dragon/crystal/infernal) | wiki+cache | done | full ladder (bronze→crystal, 14 objs) via oc_category=weapon_pickaxe + pickaxes.obj param overlay (mining_rate/mining_animation); Nurmof repair still deferred CONTENT 8x |
| 53 | Mining gear perks (cape/gloves/prospector/Varrock armour/clay bracelet) | wiki+cache | done | mining_gear.rs2: prospector XP (+2.5% cap), Varrock armour 4 double-ore, mining/superior/expert gloves (deterministic per-rock saves), bracelet of clay (28-charge player varp), cape +1 boost, unidentified minerals (amethyst only). Gem bag/coal bag storage deferred — see MINING_COMPLETION_PLAN.md §6 |
| 54 | Mining special activities (Blast/Volcanic/Stars) | wiki → Kronos | blocked | Motherlode already KRONOS §13 done (veins/paydirt/hopper/sack) + Mining Guild 60 gate (mining_guild.rs2); remaining post-2009 activities stay KRONOS lane. See MINING_COMPLETION_PLAN.md §9 |
| 55 | Miscellania mining intercept | LC | done | ~magnus_intercept_ore (miner_magnus.rs2), called from get_ore_normal/get_ore_fast before inv_add — ore-only, same scope as LostCity's own version |
| 56 | Cannonballs | LC | done | XP bug fixed (375→256, was the steel bar rate not the cannonball rate) + Dwarf Cannon quest gate added + looped; SMITHING_COMPLETION_PLAN.md S8 |
| 57 | Anvil members products (darts/knives/arrows/wire/studs/bolts/limbs/claws) | LC | done | 56 new dbrows via S2's kind-column refactor (deleted six 17-case switches); SMITHING_COMPLETION_PLAN.md S2–S5 |
| 58 | Dragon sq shield + special anvil products | LC | done | Dragon sq already lived; dragonfire shield assembly new, godsword blade combine chain found already implemented (missing level-80 gate + XP, now fixed) — SMITHING_COMPLETION_PLAN.md S12 |
| 59 | Members bars (blurite/elemental/lovakite) | wiki+cache | done | Blurite smelt+anvil products landed (SMITHING_COMPLETION_PLAN.md S6); elemental metal scoped to quest_elemental_workshop (S7, not this lane); lovakite is a Kourend-lane furnace, out of scope |
| 60 | Smithing IF / batch polish | LC | pending | p_choice menus now cover all 31 product kinds (S2's menu chain); real IF 312 client UI still not wired — SMITHING_COMPLETION_PLAN.md S13 |
| 61 | Smithing gear perks (gauntlets/cape/Smiths' Uniform) | wiki+cache | done | Imcando hammer, Smithing cape gold-XP bonus, Varrock armour double-smelt landed; Smiths' Uniform is a 2022 Giants' Foundry reward, out of era — SMITHING_COMPLETION_PLAN.md S9 |
| 62 | Blast Furnace remainder | SCAPE2009 | blocked | Enter+machine already SCAPE2009 §13/13b done; breakage/belt NPCs stay that lane |
| 63 | Members fish (shark/monkfish/big-net/karambwan/angler/dark crab/eels) | LC | done | FISHING_COMPLETION_PLAN.md S1-S9,S13; monkfish/anglerfish/dark crab/cave eel/frogspawn/infernal eel/sacred eel/rainbow fish live, table-driven; shark/karambwan/big-net already live |
| 64 | Barbarian / barehand fishing | wiki+cache | done | FISHING_COMPLETION_PLAN.md S9; leaping trout/salmon/sturgeon live (dual Fishing+Agility+Strength gate, off-skill xp); barehand harpoon not wiki-documented, not attempted |
| 65 | Fishing Guild | LC | done | Door/gate/Master Fisher/shop already live; S13 fixed 6 mis-mechanic'd guild-area spots (were shark+junk, now correct monkfish/tuna/swordfish) |
| 66 | Fishing spot movement / whirlpools | LC | pending | FISHING_COMPLETION_PLAN.md S10 landed (ai_timer relocation, coords generated from this tree's own spawn roster via tools/gen_fishing_movement.py — not ported); live-session relocation unconfirmed. Whirlpools/afk macros still deferred tree-wide (era decision, not fishing-specific) |
| 67 | Miscellania fishing intercept | LC | blocked | Needs a worker/resource allocation system that does not exist anywhere in area_miscellania/ yet — not a fishing-sized task, see FISHING_COMPLETION_PLAN.md S15 |
| 68 | Fishing cape / angler outfit | wiki+cache | done | FISHING_COMPLETION_PLAN.md S14; skillcape_boost.rs2 +1 Fishing, angler's outfit 0.4/0.8/0.6/0.2%+0.5% set xp bonus (wiki-verified). Fish barrel/spirit flakes not implemented |
| 69 | Fishing Trawler remainder | CONTENT | blocked | Murphy+hull/net/bail live (19d/19f); control timer+%npc_* varn blocked on CONTENT skip list |
| 70 | Tempoross / aerial / drift-net fishing | wiki → Kronos | blocked | Post-2009 activities stay KRONOS lane |
| 71 | Members fish cookables (shark/monkfish/karambwan/angler/…) | LC | pending | cooking.rs2 + cooking_generic deferred members fish; table stops at swordfish |
| 72 | Members recipes (stew/curry/potato toppings/ugthanki/snails/spit) | wiki+cache | pending | F2P pies/pizza/cake/bread live; wiki stews/toppings/spit meats absent |
| 73 | Cooking Guild | LC | pending | cooking.rs2 deferred guild; Chef's hat gate absent |
| 74 | Cooking cape / Hosidius burn rates | wiki+cache | pending | Gauntlets+cookomatic live for F2P rows; cape never-burn + Hosidius diary range absent |
| 75 | Wine / dough polish remainder | LC | pending | CONTENT 10v/10w deferred bad-wine polish, reverse Use, swamp_tar/murder_proofobj |
| 76 | Chompy / brew / viking-fire cooking | LC | pending | gnome_seasoning deferred chompy/brew; cooking.rs2 viking fires |
| 77 | Restaurant jobs cooking skill wire | LC | pending | CONTENT 16b restaurant jobs live but "cooking skill deferred" |
| 78 | Members logs (teak/mahogany/achey/arctic/redwood) | wiki+cache | pending | firemaking_log_level normal→magic only; achey deferred in header |
| 79 | Light sources | LC | pending | CONTENT 7c + firemaking.rs2 deferred light sources |
| 80 | Pyre logs / Mort'ton cremation | CONTENT | blocked | CONTENT skip list pyre; shade chests/AI live (9s/10a) without pyre→keys |
| 81 | Barbarian bow firemaking / pyre ships | wiki+cache | pending | Wiki Otto barb FM; no bow-on-log or pyre-ship scripts |
| 82 | Firemaking cape / pyromancer outfit | wiki+cache | pending | Cape perk absent; pyromancer via Wintertodt rewards |
| 83 | Forester's Campfire / bank-zone FM polish | wiki+cache | pending | firemaking.rs2 deferred bank/party zone tables; wiki continuous campfire absent |
| 84 | Wintertodt remainder | wiki → Kronos | blocked | Core loop KRONOS §12 done; storm/cold/HUD/crate loot stay that lane |
| 85 | Members trees (teak/mahogany/achey/arctic/redwood) | wiki+cache | pending | trees.dbrow normal→magic+hollow only; redwood chop deferred off WC Guild ropes |
| 86 | Higher axes (dragon/crystal/infernal) | LC | pending | woodcut.rs2 deferred dragon/crystal; axe Use bronze→rune |
| 87 | Bird nests while chopping | wiki+cache | pending | Nest drops during chop absent; shrine eggs→seed nest already in WC Guild |
| 88 | Miscellania lumberjack intercept | LC | pending | CONTENT 20f Leif Talk live; woodcut intercept deferred |
| 89 | WC Guild redwood / invisible +2 boost | Kronos | pending | KRONOS §83 gates/ropes/shrine done; redwood plane Enter + guild +2 boost deferred |
| 90 | Forestry events | wiki → Kronos | blocked | Post-2009 Forestry stays KRONOS lane |
| 91 | Woodcutting cape / lumberjack outfit | wiki+cache | pending | Skillcape perk + lumberjack XP bonus absent |
| 92 | Barbarian Outpost Agility course | LC | pending | CONTENT 8s deferred; SCAPE2009 §35 → LC; not in skill_agility tree |
| 93 | Wilderness Agility course | LC | pending | Same ownership as #92; not in tree |
| 94 | Mid-era courses (Pyramid/Brimhaven/Ape/Werewolf) | LC/2009 | pending | Wiki classic courses absent; Agility Pyramid ≠ Pyramid Plunder (SCAPE2009 §7) |
| 95 | Pollnivneach rooftop | wiki+cache | pending | 8 rooftops live (Draynor→Ardougne); Pollnivneach (70) missing |
| 96 | Shortcuts remainder (grapple/stiles/mid-era) | LC | pending | agility_shortcuts_osrs grapple/stiles deferred; mid-era → CONTENT/SCAPE2009 |
| 97 | Agility cape / graceful outfit | wiki+cache | pending | Cape perk + graceful energy restore absent |
| 98 | Modern courses (Sepulchre/Prif/Shayzien/Wyrm) | wiki → Kronos | blocked | Post-2009 courses stay KRONOS lane |
| 99 | Agility pet | wiki+cache | pending | Rooftop headers defer agility pet |
| 100 | Decanting | LC | blocked → HERBLORE_PORT_QUEUE §4 | CONTENT 8t deferred decant; no dose combine scripts |
| 101 | Missing classic potions (sara brew/stamina/combat/sanfew/hunter/serum/relicym/coconut+) | LC | blocked → HERBLORE_PORT_QUEUE §17–19 | brew.dbrow through magic/zam/antifire; wiki sara/stamina/combat/sanfew/antidote+/++ absent; coconut unfinisheds deferred |
| 102 | Barbarian mixes / herb tar | LC | blocked → HERBLORE_PORT_QUEUE §29–30 | CONTENT 8t deferred barbarian mixes + tar |
| 103 | Quest mixes (Mort'ton serum / Eadgar / ogre) | LC | blocked → HERBLORE_PORT_QUEUE §35 | CONTENT 8t deferred mort/eadgar/ogre quest potions |
| 104 | Modern potions (divine/raids/Mixology) | wiki → Kronos | blocked → HERBLORE_PORT_QUEUE §18,24,36,37 | Post-2009 divine/raids/Mastering Mixology stay KRONOS lane; divine potions themselves tracked in HERBLORE_PORT_QUEUE (era-agnostic, cache already has them) |
| 105 | Herblore cape / chemistry amulet / goggles | wiki+cache | blocked → HERBLORE_PORT_QUEUE §31–34 | Cape perk + 4-dose / secondary-save gear absent |
| 106 | Huasca + prayer regen | wiki+cache | blocked → HERBLORE_PORT_QUEUE §14,18 | identify.dbrow deferred huasca; wiki prayer regeneration potion |
| 107 | Expanded pickpockets (HAM/Master Farmer/elf/vyre/bandits/pirate) | wiki+cache | pending | pickpocket.dbrow man→hero; missing HAM/master farmer/modern targets; viking deferred |
| 108 | Members/misc stalls (fruit/seed/viking markets) | LC | pending | CONTENT 8n/8o deferred viking/misc stalls; Ardougne bakery→gem live |
| 109 | Blackjacking | wiki+cache | pending | Wiki The Feud bandit blackjack path absent |
| 110 | Rogues' Den / rogue outfit | wiki+cache | pending | Maze + outfit double-loot absent (Rogues' Castle chests already KRONOS §76) |
| 111 | Stall guard retaliate polish | LC | pending | stealing.rs2 deferred ~npc_retaliate (npc_aggressive_player varn) |
| 112 | Thieving cape / gloves of silence / dodgy necklace | wiki+cache | pending | Success/stun/double-loot gear absent |
| 113 | Coin pouches | wiki+cache | pending | Wiki pouch stack loot model vs direct coin drops |
| 114 | Pyramid Plunder remainder | SCAPE2009 | blocked | Entrance+rooms SCAPE2009 §7/7b done; snake charm deferred on that lane |
| 115 | Maple+ bow cut/string | LC | done | Maple/yew/magic cut+string landed, FLETCHING S3 |
| 116 | Mithril+ arrows | LC | done | Mithril/adamant/rune arrowheads landed, FLETCHING S4 |
| 117 | Crossbow stocks / limbs / unfinished bolts | LC | done | **Premise corrected**: not "unexpressible" — every part exists as `xbows_crossbow_{stock,limbs,unstrung}_*`, `xbows_crossbow_string`, `*_unfeathered`. 8 stocks/limbs/strings + 9 metal bolt tiers landed, FLETCHING S5+S6 |
| 118 | Gem bolt tips remainder | LC | done | jade→onyx tip cutting + their tipped bolts landed, FLETCHING S8; enchant combat →#22 still open |
| 119 | Ogre / brutal arrows | LC | done | Composite ogre bow + 6 brutal arrow tiers landed, FLETCHING S7 (no `nails_steel` obj exists, so no Steel tier — documented, not guessed) |
| 120 | Amethyst / dragon ammo + javelins | wiki+cache | done | Amethyst 4-way chisel menu, dragon arrow/dart/javelin, 8 javelin tiers landed, FLETCHING S9 |
| 121 | Broad arrows / bolts | wiki+cache | done | Landed, FLETCHING S10 — level-gated only; no Slayer reward-unlock convention exists in `skill_slayer/` to gate against |
| 122 | Fletching cape / bowstring spool | wiki+cache | done | Worn Boost perk landed, FLETCHING S14 (`skillcape_boost.rs2`); spool is 2025 Vale Totems → KRONOS, search-for-grapple perk not implemented |
| 123 | Konar location-restricted tasks | Kronos | pending | Masters assign live; Konar areas column wire deferred (KRONOS §3) |
| 124 | Monster specials remainder (mirror/gargoyle/banshee/…) | SCAPE2009/Kronos | pending | Rockslug+lizard finishers live (SCAPE2009 §3e); mirror/smash/earmuffs deferred |
| 125 | Superior unique loot / full type map | Kronos | pending | Bigger and Badder spawn+credit stub (KRONOS §40); loot deferred |
| 126 | Task membership category gaps | Kronos | pending | KRONOS §2 deferred cave bug/slime categories; wildy emblem custom drops skipped |
| 127 | Brimstone / Larran chest loot tables | Kronos | pending | Unlock stubs KRONOS §35/§43; loot deferred |
| 128 | Slayer equipment combat effects (helm/mask/boots) | wiki+cache | pending | Rewards IF exists; black mask/slayer helm on-task boosts + shop gear effects incomplete |
| 129 | Slayer cape | wiki+cache | pending | Skillcape perk absent |
| 130 | Mortimer / modern Slayer bosses polish | wiki → Kronos | blocked | Post-2009 master/boss remainder stays KRONOS (Cerberus/Kraken stubs already §24–25) |
| 131 | Higher tree/fruit/crop tiers | SCAPE2009 | pending | Oak/apple/barley/redberry live; willow+ trees, higher fruit/hops/bushes deferred (§1f1 log) |
| 132 | Disease / plant cure / gardener protect | SCAPE2009 | pending | Disease stubbed; check-health always healthy; gardener payments absent |
| 133 | Zeah compost bins + dump/ultracompost | SCAPE2009 | pending | Classic bins 1–4 live; Zeah 5–7 + Dump/potion-convert deferred (§1e) |
| 134 | Spirit / calquat / special patches | SCAPE2009 | pending | Patch registry rows exist; plant/grow scripts not wired like trees |
| 135 | Geomancy / farming tools polish | SCAPE2009 | pending | farming_view live; Geomancy opener deferred; magic secateurs yield bonus absent |
| 136 | Tool leprechaun note exchange | wiki+cache | pending | Wiki note-swap for harvest; store may be partial via tools IF |
| 137 | Farming cape | wiki+cache | pending | Skillcape perk absent |
| 138 | Farming Guild / Tithe / Hespori polish | wiki → Kronos | blocked | Hespori stub KRONOS §32; Tithe Farm + Guild remainder stay KRONOS |
| 139 | Furniture creation menu IF | SCAPE2009 | blocked | → SCAPE2009 §4c / owner elsewhere; garden p_choice MVP live — do not edit `skill_construction/` from this lane |
| 140 | Room editor / additional rooms | SCAPE2009 | blocked | Wiki parlour+rooms; default garden only in collage; room buy/viewer → §4c owner |
| 141 | House state persistence | wiki+cache | pending | poh_build.rs2: built furniture session-only until house state persists |
| 142 | Estate agent move / redecorate / cape | SCAPE2009 | pending | Buy Rimmington live; move/redecorate/cape deferred in poh_estate_agent.rs2 |
| 143 | Servant / house party / visitor mode | wiki+cache | pending | Wiki servants + friend visits absent |
| 144 | Gilded altar / POH prayer furniture | SCAPE2009 | blocked | Already Skills #25 → POH remainder; do not edit live tree from Prayer lane |
| 145 | Construction training furniture (oak larders/mahogany tables/…) | SCAPE2009 | blocked | Beyond garden plants — furniture catalogue → §4c owner |
| 146 | Expanded bird snare / box-trap prey | SCAPE2009+wiki | pending | Five birds + ferret/grey/red chin live but singleton trap ownership is wrong; add jerboa, black chin, Letvek. See [`HUNTER_COMPLETION_PLAN.md`](HUNTER_COMPLETION_PLAN.md) H1/H3/H10 |
| 147 | Butterfly netting / barehand | wiki+cache | pending | Four classic butterflies live with obsolete barehand gates; add sunlight/moonlight moths and current release rules. See Hunter plan §3.3 |
| 148 | Salamander / deadfall / tracking remainder | SCAPE2009+wiki | pending | Four salamanders + four deadfalls + four partial kebbit trails live; add tecu, fox, monkey, razorback, Herbiboar and complete graphs. See Hunter plan §§3.1/3.5/3.7 |
| 149 | Impling jar loot + higher implings | SCAPE2009+wiki | pending | Baby–dragon, loot tables, and Puro live; add surface/Puro XP split, direct loot, barehand, Crystal/Lucky, tracker/spawner audit. See Hunter plan §3.4 |
| 150 | Falconry polish (projectile / zone leave) | SCAPE2009 | pending | Three kebbits, projectile, catch and retrieve live; audit Matthias rental, logout/zone cleanup, ownership races, and rumour hooks. See Hunter plan §3.9 |
| 151 | Hunter cape / clothing / horn | wiki+cache | pending | Cape and fur exchange absent; camouflage is weight-only, while guild outfit affects catches; horn implementation lacks success boost/doubling. See Hunter plan §§5.4/6 |
| 152 | Bird houses / Herbiboar / Hunter Guild / modern prey | wiki+cache | pending | Cache has Fossil/Avium/Guild and 2026 Vampyrium assets; implement per-player sites, Rumours, Savannah roster, Letvek/Stymphike. Wyrmscraig goats need a newer cache. See Hunter plan §§4.1/5/7/8 |
| 153 | Aerial / drift-net / crab trapping | wiki+cache | pending | Cache has aerial spots, drift-net states, and crab guide/assets; implement as shared Hunter/Fishing/Sailing slices with authoritative adjacent gates. See Hunter plan §§4.2–4.4 |
| 154 | Crafting spinning remainder | wiki+cache | done | Spinning wheel extended from 2 to 10 rows (crossbow string ×5 sources, linen/magic string, rope, hemp/cotton yarn), `spinning.dbtable`, bound to all 9 wheel placements via cache category 971. See CRAFTING_COMPLETION_PLAN.md §1.1/S3 |
| 155 | Crafting silver casting remainder | wiki+cache | done | Plain silver Tiara (distinct from the gold tiara in #46) + silver bolts (unf), both previously unreachable. See CRAFTING_COMPLETION_PLAN.md §1.15/S9 |
| 156 | Crafting leather/dragonhide shields + spiky vambraces | wiki+cache | done | Hard leather shield + 4 dragonhide shields (leather-on-wooden-shield + hammer + nails), spiky vambraces (kebbit claws on any vambraces). Depended on Fletching's wooden shields, which had already landed. See CRAFTING_COMPLETION_PLAN.md §1.4–§1.5/S10 |
| 157 | Crafting snakeskin | wiki+cache | done | 7 products (boots/vambraces/bandana/chaps/body/shield/3 broodoo shields) + a 5th tanning option (snakeskin, 15gp) added to the Al Kharid/Canifis tanner menu. See CRAFTING_COMPLETION_PLAN.md §1.6/S11 |
| 158 | Crafting fabric/fur armour families | wiki+cache | done | Xerician robes, Splitbark armour, Mixed hide armour, Hueycoatl hide armour (16 products, one `fabric_armour.dbtable`) + Recipe for Disaster's crab armour (2 products via chisel). See CRAFTING_COMPLETION_PLAN.md §1.9–§1.10/S13 |
| 159 | Crafting dye targets beyond capes | wiki+cache | done | Wizard hat (blue), desert shirt/robe (black, merged into quest_golem's existing `golem_ink` trigger), rose-tinted lens (already existed) + pink cape (merged both directions with quest_handinthesand's `handsand_pink_dye`). See CRAFTING_COMPLETION_PLAN.md §1.19/S14 |
| 160 | Fletching gem bolt tips / amethyst ammo / toxic blowpipe | wiki+cache | done | Landed by a separate concurrent session ahead of this pass (`skill_fletching/scripts/amethyst.rs2`, bolts.rs2's gem-bolt-tip cases). Crafting's chisel switch (`skill_crafting/scripts/gem/uncut_gem.rs2`) already had the coordinating cases; this pass only added the missing onyx/zenyte gem-cutting rows those cases assumed existed |

## Opcode gap log

Record new Server VM opcodes **before** inventing C content hooks.

| Slice | Opcode / surface | Why | Status |
|---|---|---|---|
| 17 | `inv_dropitem_delayed` | Ammo recovery after ranged shot | done (hosted + content wire #17) |
| 2 | secondary player (`.` dialect) + `MOCK230_PLAYER_MAX>1` | PvP melee/ranged/magic need `.stat` / `.queue` / `p_opplayer` against another player | blocked — host; content not ported |

## Log

- queue created: Audit roster seeded (23 skills, Sailing skipped); Finish queue empty pending audits; PORTING_GUIDE §4.7
- audit Attack done: wiki [Attack](https://oldschool.runescape.wiki/w/Attack) + [F2P Melee](https://oldschool.runescape.wiki/w/Free-to-play_Melee_training) + [P2P Melee](https://oldschool.runescape.wiki/w/Pay-to-play_Melee_training) (`/Training` 404). In-tree: `player_melee_swing` + `give_combat_experience` accurate/controlled Attack XP, combat style table + `%com_mode` + `combat_interface` style slots, auto-retaliate, levelrequire, BAS. LC gaps not ported: `player_special_attack`/`specs/`, `pvp_melee`. Harvested: CONTENT 10d potion consume deferred; Kronos §11 WG activities deferred, §68 specs blocked. Emitted finish slices 1–5. Next audit = Strength.
- audit Strength done: wiki [Strength](https://oldschool.runescape.wiki/w/Strength) + F2P/P2P Melee guides. In-tree: aggressive/controlled Strength XP, `combat_maxhit` + strengthbonus + Str prayers, warhammer/halberd Str reqs in `levelrequire.dbrow`, WG Att+Str gate. Specs/PvP/WG activities already queued (#1/#2/#4). Emitted #6–8 (potion consume, cape, non-combat Str training). Next audit = Defence.
- audit Defence done: wiki [Defence](https://oldschool.runescape.wiki/w/Defence) + F2P/P2P Melee. In-tree: defensive/controlled Def XP, armour `levelrequire_defence_*`, protect-from prayers, defence style +8. Gap: magic def not 7:3 (commented defer). WG defenders via Kronos §11/#4. Emitted #9–11. Next audit = Hitpoints.
- audit Hitpoints done: wiki [Hitpoints](https://oldschool.runescape.wiki/w/Hitpoints). In-tree: combat HP XP (1.33×), `food.rs2` category eat (F2P heal table), ring of life, monastery heal, death/respawn heal, Rapid Heal toggle (no regen timer). Gaps: LC `health_regen`, overheal foods, life leech, desert heat, phoenix neck, HP cape. Emitted #12–15. Next audit = Ranged.
- audit Ranged done: wiki [Ranged](https://oldschool.runescape.wiki/w/Ranged). In-tree: F2P bow+arrow `player_ranged.rs2` + ammo table + accurate/rapid/longrange XP (CONTENT 8u); Craw's/Webweaver charge hooks. Harvested 8u deferred list. Emitted #16–22 + opcode gap `inv_dropitem_delayed`. Next audit = Prayer.
- audit Prayer done: wiki [Prayer](https://oldschool.runescape.wiki/w/Prayer). In-tree: full prayer toggle+drain, bury (thin XP table), altar recharge, quickprayer, Redemption/Retribution single-target, monastery +2. Gaps: ectofuntus/chaos/gilded offer, ashes, potions, Smite/Piety combat wiring, bones-to-peaches. Emitted #23–29. Next audit = Magic.
- audit Magic done: wiki [Magic](https://oldschool.runescape.wiki/w/Magic). In-tree: utility suite (tele/alch/enchant/superheat/telegrab/charge/orb/bones) + F2P strike→wave combat (8p/8q); MTA live. Gaps: autocast IF, Ancient/Lunar/Arceuus, god/iban/crumble, enchant5/trollheim, potions, surges/cape. Emitted #30–36. Next audit = Runecraft.
- audit Runecraft done: wiki [Runecraft](https://oldschool.runescape.wiki/w/Runecraft). In-tree: air..death altar craft + essence mine Aubury/Sedridor (CONTENT 8k). Harvested deferred list. Emitted #37–42 (tiara/pure ess, blood/soul/wrath, Ourania/abyss, teleporter remainder, GotR, cape/combos). Next audit = Crafting.
- audit Crafting done: wiki [Crafting](https://oldschool.runescape.wiki/w/Crafting). In-tree: pottery/gems/leather/dhide/jewellery/glass/spinning/studded/battlestaves/snelm/guild/dye. Gaps: weaving, IF/batch polish, glass lens/lantern, modern gems, urns, cape. Emitted #43–48. Next audit = Mining.
- audit Mining done: wiki [Mining](https://oldschool.runescape.wiki/w/Mining). In-tree: clay..runite+blurite, prospect, bronze→rune picks, Mining Guild gate (CONTENT 8x), essence tele (8k), Motherlode (KRONOS §13). Gaps: essence/gem rocks, members ores, higher picks, gear perks, Misc intercept; Blast/Volcanic/Stars → Kronos. Emitted #49–55. Next audit = Smithing.
- audit Smithing done: wiki [Smithing](https://oldschool.runescape.wiki/w/Smithing). In-tree: furnace smelt bronze→runite + glass/jewellery redirects, anvil F2P products bronze→rune, Doric gate, ring of forging (11h), superheat (8p), Blast Furnace (SCAPE2009 §13). Gaps: cannonballs, members anvil products, dragon sq, members bars, IF polish, gear perks. Emitted #56–62. Next audit = Fishing.
- audit Fishing done: wiki [Fishing](https://oldschool.runescape.wiki/w/Fishing). In-tree: salt/fresh/rare F2P spots through swordfish (CONTENT 7d); Fishing Contest/Hemenster live. Gaps: members fish, barb fishing, guild, spot move, Frodi intercept, cape/angler; Trawler remainder→CONTENT; Tempoross/aerial→Kronos. Emitted #63–70. Next audit = Cooking.
- audit Cooking done: wiki [Cooking](https://oldschool.runescape.wiki/w/Cooking). In-tree: F2P meat/fish/bread/pies/pizza/cake, dough+wine, gnome suite (16n–16w), gauntlets+cookomatic burn columns. Gaps: members fish/recipes, guild, cape/Hosidius, wine/dough polish, chompy/brew, restaurant cook XP. Emitted #71–77. Next audit = Firemaking.
- audit Firemaking done: wiki [Firemaking](https://oldschool.runescape.wiki/w/Firemaking). In-tree: tinderbox/Light normal→magic (CONTENT 7c), ashes+push, UPass fire arrows. Gaps: members logs, light sources, barb bow/pyre ships, cape/pyromancer, campfires; pyre→CONTENT blocked; Wintertodt→Kronos. Emitted #78–84. Next audit = Woodcutting.
- audit Woodcutting done: wiki [Woodcutting](https://oldschool.runescape.wiki/w/Woodcutting). In-tree: normal→magic+hollow chop (CONTENT 7a/9r), axe bronze→rune, WC Guild gates/ropes/shrine (KRONOS §83). Gaps: members trees, higher axes, nests, Leif intercept, redwood/+2, cape/lumberjack; Forestry→Kronos. Emitted #85–91. Next audit = Agility.
- audit Agility done: wiki [Agility](https://oldschool.runescape.wiki/w/Agility). In-tree: Gnome course (CONTENT 8s), 8 rooftops + MoG (KRONOS §22), Falador wall/GE/wildy shortcuts. Gaps: barb/wild courses, mid-era courses, Pollnivneach, grapples, cape/graceful, pet; modern courses→Kronos. Emitted #92–99. Next audit = Herblore.
- audit Herblore done: wiki [Herblore](https://oldschool.runescape.wiki/w/Herblore). In-tree: clean/grind/unf+classic finishes through magic (CONTENT 8t), Druidic Ritual (13p). Gaps: decant, sara/stamina/combat/sanfew/coconut+, barb/tar, quest mixes, cape/chemistry, huasca; Drink already #3 family; divine/Mixology→Kronos. Emitted #100–106. Next audit = Thieving.
- audit Thieving done: wiki [Thieving](https://oldschool.runescape.wiki/w/Thieving). In-tree: pickpocket man→hero, Ardougne stalls, trapped chests, locked doors (CONTENT 8n/8o); Pyramid Plunder (SCAPE2009 §7); Rogues' Castle chests (KRONOS §76). Gaps: expanded pickpockets, misc stalls, blackjack, Rogues' Den/outfit, retaliate, cape/gear, coin pouches. Emitted #107–114. Next audit = Fletching.
- audit Fletching done: wiki [Fletching](https://oldschool.runescape.wiki/w/Fletching). In-tree: F2P cut/string/arrows (CONTENT 8r), darts+opal/pearl/barb bolts (13r). Gaps: maple+ bows, mithril+ arrows, crossbows, gem tips remainder, ogre/brutal, amethyst/dragon/javelins, broads, cape/spool. Emitted #115–122. Next audit = Slayer.
- audit Slayer done: wiki [Slayer](https://oldschool.runescape.wiki/w/Slayer). In-tree: masters/assign/kill/points/cancel-block-store, rewards IF, rockslug/lizard specials, superior stub, imbued heart (KRONOS+SCAPE2009). Gaps: Konar areas, specials remainder, superior loot, category gaps, chest loot, helm effects, cape; Mortimer/boss polish→Kronos. Emitted #123–130. Next audit = Farming.
- audit Farming done: wiki [Farming](https://oldschool.runescape.wiki/w/Farming). In-tree: classic herb/allot/flower/compost/tree/fruit/hops/bush + farming_view (SCAPE2009 §1a–1g). Gaps: higher tiers, disease/gardeners, Zeah compost, spirit/calquat, Geomancy/secateurs, leprechaun notes, cape; Guild/Tithe→Kronos. Emitted #131–138. Next audit = Construction.
- audit Construction done: wiki [Construction](https://oldschool.runescape.wiki/w/Construction). In-tree: estate buy, enter/leave instance, garden hotspot Build/Remove (`::poh`/`::pohbuild`) — SCAPE2009 §4a+4b **live, do not park**. Gaps: furniture IF/rooms (§4c), persistence, move/redecorate/cape, servants, gilded altar (#25). Emitted #139–145. Next audit = Hunter.
- audit Hunter done: wiki [Hunter](https://oldschool.runescape.wiki/w/Hunter). In-tree: bird snare, box chin, baby impling, falconry (SCAPE2009 §2a–2d), Puro-Puro enter (§8). Gaps: expanded prey, butterflies, salamander/deadfall/tracking, jar loot, falcon polish, cape; bird houses/Herbiboar/Guild/aerial→Kronos. Emitted #146–153.
- **Audit roster complete** (23/23 OSRS skills, Sailing skipped). Finish queue seeded #1–153. Stop re-arming audit sleeper. Port loop is separate work.
  - **Summoning is a 24th row and is deliberately *not* counted in that 23/23.** It is not an OldSchool skill; it is a flagged port from 2009scape tracked in [`SUMMONING_PORT_QUEUE.md`](SUMMONING_PORT_QUEUE.md). Do not re-open this audit for it.
- **Port loop armed** (skills_port). Selection: lowest pending, deps-first; stop when no actionable pending rows.
- port #1 specials done: LC `skill_combat/scripts/player/{specwep,player_special_attack}.rs2` + `specs/pvm_*`. In-tree: `specwep`/`sa_energy`/`sa_kind` params, energy regen timer, `combat_interface:special_attack` + orb `@specbar_pressed`, combat-start divert, equip clears `%sa_attack`, PvM dds/dlong/dmace/claws + instant dbaxe/Excalibur. Era: drop sound_synth; ranged→#18; spear/halberd deferred. Verified pack 0 err + mock230-scripts. Next = #2 PvP melee.
- port #2 PvP melee → blocked: LC `pvp/pvp_*.rs2` needs secondary-player dialect; `MOCK230_PLAYER_MAX` is 1 (combat_stats.rs2 documents `.` variants not ported). Opcode gap logged. Next = #3 Attack potion.
- port #3 Attack potion done: LC consume_effect_stat shape via name-bound Drink (anti_poison pattern). `attack_potion.rs2` — attack +3/+10%, super +5/+15%, dose ladder. Verified pack 0 + scripts. Next = #4.
- port #5 Attack cape done: wiki +1 Boost; worn op2 host fix in mock230_world.c; `skillcape_attack.rs2`. #4 stays Kronos-blocked.
- port #6 Strength potion done: `strength_potion.rs2` (strength4 ladder). 
- port #9 magic def 7:3 done: LC blend in `player_combat_stat`. Next = #7 Strength cape.
- port #5/#7/#11 skillcape Boosts consolidated in `skillcape_boost.rs2` (Attack/Strength/Defence +1). Next = #8.
- port #10 Defence potion done: `defence_potion.rs2`.
- port #12 HP regen done: LC `health_regen` timer + Rapid Heal interval rearm in `prayer_toggle`. Cape/bracelet → #15. Next = #8.
- port #8 Non-combat Str → blocked SCAPE2009 (barb fishing). Next = #13 food / #17 ammo.
- port #17 ammo recovery done: LC `ranged_dropammo_npc` → `inv_dropitem_delayed`. Sibling fix: ranging_guild_guard `npc_setmode(applayer2)`.
- port #13 food/overheal done: kebab + angler `stat_boost` + Sara brew (`4dosepotionofsaradomin`). Next = #14 leech.
- port #14 leech done: Guthan's set + SGS Healing Blade. Blood → #31. Next = #15 HP cape/desert/phoenix.
- port #15 done: HP cape/bracelet regen + phoenix necklace; desert heat already in-tree. Next = #16 ranged weapons / #18 specs / #21 ranging pot.
- port #16 done: bolt/dart/knife ammo table rows. Chin multi deferred.
- port #21 ranging potion done. Next = #18 ranged specs.
- port #18 MSB/MLB specs done. Thrownaxe/dark bow deferred. Next = #19 cannon / #20 Ava / #22 bolts / #23 bones.
- port #19 multicannon → blocked CONTENT (7g).
- port #20 Ava ammo-save done. #23 bone XP expanded. #26 prayer/super restore done. Next = #22/#24/#27.
- port #27 Piety/Rigour/Augury combat multipliers done. Smite → #2. Next = #22/#24/#28.
- port #22 enchanted bolts done (PvM opal…onyx). MAGIC slices #30–36 → blocked MAGIC plan. Next = #24 ectofuntus / #28 retribution.
- magic lane: plan `MAGIC_CONTENT_PORT_PLAN.md` + loop `AGENT_LOOP_WAKE_magic_port`; M1–M3 done (members combat, magic potion, crumble).
- port #24 ectofuntus/chaos offer done: LC has no worship/grind — 2009scape Phasmatys + wiki Chaos Temple. `ectofuntus.rs2` (worship 4×, hopper/wind/bin, slime→ectoplasm), `chaos_altar_offer.rs2` (350% + 50% save), `bone_xp.rs2` shared with bury. Verified pack 0 + mock230-scripts. Next = #28 Retribution AoE / #29 cape+peaches / #37 RC.
- port #28 Retribution AoE done: LC none; multi uses hosted `map_multiway` + npc/player hunt within 1; single `%aggressive_npc`. Verified pack 0 + scripts. Next = #29 Prayer cape / bones-to-peaches.
- port #29 Prayer cape done: wiki +1 Boost on `skillcape_prayer`/`_trimmed`; peaches already MAGIC M5. Next = #37 RC tiara/pure essence.
- port #43/#45–48/#154–159 (Crafting finish pass) done, plan `CRAFTING_COMPLETION_PLAN.md`: weaving (new script, `[oploc1,_loom]` on cache category 971/2299 shared with `regicide_loom`); spinning 2→10 rows; glass 3→8 blows + oil lantern assembly; onyx/zenyte gem cutting; full onyx/zenyte + gold bracelet + gold tiara + both slayer rings + 12-row opal/jade/topaz silver jewellery; plain silver Tiara + silver bolts (unf); hard leather + 4 dragonhide shields + spiky vambraces; snakeskin (7 products + tanner row); Xerician/Splitbark/Mixed hide/Hueycoatl hide armour + crab armour (16+2 products, `fabric_armour.dbtable`); wizard hat/desert shirt+robe/pink cape dyes; Crafting cape Boost (wired into shared `skillcape_boost.rs2`) + door alternates + Master Crafter cape sale. #44 (skillmulti IF) stays pending — every new menu used the existing `~p_choice*` pagination convention instead. Verified via `make -C src mock230-scripts` (0 errors) at each step; landed alongside a concurrent Magic/Fletching session's commits (`742b4d9b2a`…`95ef473536`) without conflict. `docs/TOOL_TRIGGER_ORGANISATION.md` written from this pass's `[opheldu]` dispatch research. No runtime selftest suite (plan §4) built this pass — sscompile only.
