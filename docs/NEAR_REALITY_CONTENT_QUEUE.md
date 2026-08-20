# Near-Reality content port — the remaining-content plan and queue

**Brief.** Go through everything `RSPS-NEAR-REALITY` implements, find what this
tree does not implement (or implements shallowly), and port the difference —
all of it, no shortcuts, no quitting. This document is both the *plan* (how a
slice is chosen, ported and proved) and the *list* (every slice, ordered, with
its measured size on both sides).

Written 2026-08-19 from a full audit of
`~/Documents/git_repos/RSPS-NEAR-REALITY/near-reality-server-main` against
`OSRS-Content/osrs239-content/server/scripts`.

Companion documents — read them, do not restate them:

- [`PORTING_GUIDE.md`](PORTING_GUIDE.md) §4 (slice workflow), §4.3 (definition of
  done), §4.3a (boss triggers), §7 (guardrails).
- [`CONTENT_ARCHITECTURE.md`](CONTENT_ARCHITECTURE.md) (which pack owns what).
- [`minigames/cox/COX_NEARREALITY_PORT_PLAN.md`](minigames/cox/COX_NEARREALITY_PORT_PLAN.md)
  §0 — the precedence ladder and the four standing traps. **They apply verbatim
  to every slice in this document.** They are reproduced in §0.2 below because
  this queue will be picked up cold.
- [`CONTENT_PORT_QUEUE.md`](CONTENT_PORT_QUEUE.md) — the LostCity forward port.
  Different source, same tree. Never park its lanes to green a compile.

---

## 0. Rules of engagement

### 0.1 The precedence ladder

Apply top-down. A lower rung never overturns a higher one; it fills a gap the
higher rung is silent on.

| Rank | Source | Notes |
| --- | --- | --- |
| 1 | **Jagex statements** — Mod Ash / Mod Kieren quotes | The server's own constants. |
| 2 | **OSRS Wiki article text**, fetched `?action=raw` | Infoboxes, Strategies pages, **Changes sections**. |
| 3 | **Measured community data** — tick corpora, RuneLite plugin constants | Measurement beats prose when the prose is vague. |
| 4 | **RSPS-NEAR-REALITY** | The default. Where 1–3 are silent, port it verbatim *in behaviour*. |
| 5 | This tree's existing code | Only where nothing above speaks. Never a reason to keep something 1–4 contradicts. |

Near-Reality is a **Zenyte fork** (2017–2019 codebase) with a `core-restricted`
/ `near_reality` layer of custom systems on top. `com.zenyte.*` is stock Zenyte;
`com.near_reality.*` is NR's own. Both are in scope — the brief is "what NR
implements", not "what OSRS has".

### 0.2 The four standing traps

Each has already cost a pass in this tree.

1. **Deleted content.** Zenyte predates several removals. `SMALL_SCAVENGER_RUNT`
   spawns a Scavenger runt, removed 31 Jan 2019; its ids are a bartender in
   rev239. **Before porting any named npc, loc or item, check the wiki page for
   `{{Gone}}` and for a Changes section.**
2. **Structurally right, numerically stale.** The *shape* of a Zenyte encounter
   is nearly always correct; its constants may predate balance passes. Port the
   shape, re-derive the numbers from rungs 1–3. (Pyramid Plunder precedent.)
3. **Zenyte contradicts itself on projectiles.** `getProjectileDuration` and
   `getTime()` disagree. Derive projectile timing from the measured law in
   `docs/projectiles.md`, not from whichever NR method you read first.
4. **Id spaces drift.** NR runs a much later cache. Every npc/loc/obj/seq/spotanim
   id must be re-resolved by *name* against `cache.osrs239` before use, and a
   name that resolves to nothing is a content decision, not a compile error to
   silence.

### 0.3 Additional traps specific to this queue

5. **A grep hit is not an implementation.** The audit below distinguishes
   *mentioned* (a name in `tele_names.enum`, a spawn row in `areas/world/configs/`,
   a drop table entry) from *implemented* (scripts that run the content). Twelve
   of the "we have this" first impressions were spawns and teleport rows only —
   Zalcano, Vardorvis, the Whisperer, Leviathan, Duke Sucellus, the Nightmare,
   Tempoross, Tithe Farm, Soul Wars. Re-verify before marking a slice `done`.
6. **Depth, not presence.** `minigame_skotizo/` exists at 44 lines against NR's
   829. Presence of a directory closes nothing. Every slice below states both
   line counts; a slice is not `done` while the delta is a mechanic.
7. **Never park sibling content.** No `.rs2.skip`, no renaming another lane's
   directory, no deleting a sibling tree to green `sscompile`. See
   PORTING_GUIDE §7.
8. **Rebuild `sscompile` before verifying.** A stale compiler fakes a broken
   tree (`is not a command`, then 100+ `no proc named`).

### 0.4 Definition of done for a slice

From PORTING_GUIDE §4.3, plus this queue's additions:

- `make -C src` builds; `sscompile` rebuilt first.
- `mock230_pack --check-only` at **0 errors**.
- The content is performed end-to-end in the headless client harness — not
  "it compiles". State persists across logout/login where it should.
- No *new* silently-missing opcodes in the gap report.
- Existing content untouched; the selftest suite has no *new* failures
  (measure with and without, back to back, on one tree — see
  `mock230-selftest-operational-notes`).
- For anything that owns its own attacks: **state the combat triggers,
  including the ones it does nothing in** (PORTING_GUIDE §4.3a).
- The slice's row in §5 is updated with what was ported, what was deferred,
  and why.

### 0.5 Lane policy

Ported bodies that a build may want to leave out get a content lane
(`ported/<lane>/lane.ini` + `manifest [content:lanes]`, see
`content-lanes-are-self-describing`). Default: **no new lane** — this content
belongs in the base tree. Open a lane only for NR-custom systems that a vanilla
build would not want (Wave E), and say so in the slice row.

---

## 1. How the audit was produced

Reproducible; re-run it after any large wave to re-measure the deltas.

```sh
NR=~/Documents/git_repos/RSPS-NEAR-REALITY/near-reality-server-main
OURS=OSRS-Content/osrs239-content/server/scripts

# NR content surface: every package under game/content, aggregated across
# core/, core-restricted/, plugins/excluded/ (a package is split across modules).
cd $NR
find . -type d -path "*game/content/*" | sed 's|.*game/content/||' | cut -d/ -f1 | sort -u
for d in $(...); do
  find . -type d -path "*game/content/$d" \
    -exec find {} -name '*.java' -o -name '*.kt' \; | sort -u | xargs wc -l | tail -1
done

# Our side: LOC per script directory.
cd $OURS
for d in */; do find $d -name '*.rs2' | xargs wc -l | tail -1; done
```

Counts below are lines of `.java`/`.kt` on the NR side and lines of `.rs2` on
ours. They are a **size signal, not a target** — RuneScript is denser than
Zenyte's Java for encounter logic and much sparser for data tables. Use them to
rank work, never to declare parity.

### 1.1 What is *not* a gap

Measured and confirmed already covered, so the waves below skip them:

| Area | NR | Ours | Verdict |
| --- | --- | --- | --- |
| Shops | 295 shop classes | 115 shop directories | covered |
| NPC spawns | 928 spawn classes | 972 `.spawn` files | covered |
| God Wars Dungeon incl. Nex | 4495 + 4190 | `areas/area_godwars/` 5600 | covered — parity sweep only (B21) |
| Inferno | 3272 | 3724 | covered |
| Fight Caves | 1163 | 1627 | covered |
| Theatre of Blood | 10208 | 17523 | covered |
| Wintertodt | 2452 | 2312 | covered |
| Blast Furnace | 632 | 1384 | covered |
| Puro-Puro | 851 | 1056 | covered |
| Nightmare Zone | 134 | 313 | covered |
| Zulrah | 1506 | 2009 | covered (NR port already done) |
| Magic Carpet | 273 | present | covered |
| Construction | 10248 | 21873 | covered |
| Hunter | 7506 | 8689 | covered |
| Crafting | 1812 | 2672 | covered |
| Runecrafting | 998 | 1327 | covered |
| Food / consumables core | 3976 | `general/food.*` + `player/consumption/` | covered — audit only (D9) |

---

## 2. The shape of the gap

Total NR content measured: **~250k lines** across 82 top-level packages.
After removing §1.1, the remaining port surface is roughly:

| Wave | Theme | NR LOC in scope | Slices |
| --- | --- | --- | --- |
| **A** | OSRS systems entirely absent here | ~21k | 12 |
| **B** | Bosses — present but shallow, or absent | ~30k | 24 |
| **C** | Minigames & areas — present but shallow, or absent | ~62k | 19 |
| **D** | Skills — the depth delta | ~38k | 14 |
| **E** | NR-custom / meta systems | ~26k | 22 |
| **F** | Seasonal & event content | ~9k | 4 |

**95 slices.** They are ordered so that shared infrastructure lands before the
content that needs it (Wave A's clue/diary plumbing is depended on by B, C
and E), and so that each wave leaves the tree shippable.

---

## 3. Wave order and the shared infrastructure it assumes

Waves run **A → D → B → C → E → F**, not A → B → C.

Rationale: Wave D (skills) unblocks the most other slices — Agility shortcuts
gate boss approaches (B8 Abyssal Sire, B9 Cerberus), Slayer gates B4/B18/B22
and C8 Konar, Magic teleports gate half of Wave C. Wave B bosses then land on
a tree that can actually reach them.

Inside a wave, slices are ordered by *dependency first, size second*.

**Cross-cutting infrastructure that must land before its dependents** — each is
a numbered slice, not a footnote:

| Infra | Slice | Depended on by |
| --- | --- | --- |
| Clue-scroll object model (tiers, steps, caskets) | A1 | A5 reward tables, A7 diaries, every B-wave drop table |
| Achievement-diary task hook (`~diary_task_complete` call sites) | A7 | almost every C slice |
| Instance / party framework re-use audit | C1 | B1, B2, B4, B6, B8, B16, C2, C14, E1 |
| Drop-table provider indirection (alternate tables) | E10 | every B slice's drops |
| Follower/pet registry | E7 | B-wave pet drops |
| Preset / gear-loadout store | E15 | E1 PvM arena, E2 tournament |

---

## 4. Per-slice porting workflow

Same as PORTING_GUIDE §4.1, with the NR-specific steps made explicit.

1. **Claim.** Mark the slice `in_progress` in §5 *before* measuring. Other lanes
   share this tree.
2. **Read the NR implementation in full.** Not the file you found by grep — the
   whole package, including its `plugins/` subpackage, its dialogue classes and
   its drop tables. List the files on both sides in the slice row.
3. **Extend the wiki corpus first.** Add the pages the slice needs to the
   relevant `tools/fetch_*_wiki.sh` list (or create one) so the corpus grows
   with the port. Check every named entity for `{{Gone}}` and Changes.
4. **Re-resolve every id by name** against `cache.osrs239`. Record any that do
   not resolve, and decide: era-translate, substitute, or defer with a reason.
5. **Opcode gap.** Run the server-VM gap report. A missing opcode is an engine
   slice — file it, do not fake it in content.
6. **Symbols → configs → scripts.** In that order. New `pack/` names go in the
   server pack, never hand-edited into the machine-owned cache pack
   (`exporter-owns-generated-configs`).
7. **State the combat triggers** if the slice owns attacks (§0.4).
8. **Verify** per §0.4. Run the suite with and without, back to back.
9. **Record.** Update the slice row and append a dated note to §6.

---

## 5. The list

Status: `pending` | `in_progress` | `done` | `blocked` | `deferred`.
`NR` = lines of Java/Kotlin in the Near-Reality package. `Ours` = lines of
`.rs2` in the corresponding tree today (`—` = nothing).

### Wave A — OSRS systems entirely absent here

| # | Slice | NR | Ours | Status | NR path / notes |
| --- | --- | --- | --- | --- | --- |
| A1 | **Treasure Trails — core** (tiers, step model, casket chain, Read/Check steps) | 760 | 660 | **done** | The cache IS the clue database — 26 `cluehelper_*` dbtables, ~2,000 rows, reached by `param=trail_clue_row` on 724 objs. NR's 13k lines of transcribed clue tables deliberately NOT ported. See [`treasure_trails/TREASURE_TRAILS.md`](treasure_trails/TREASURE_TRAILS.md). |
| A2 | Treasure Trails — **Emote clues** | 1699 | 560 | **done** | The 126 emote rows drive it; `param_258` decoded as `trail_item_group` (55 groups, 400 objs) is how the cache says "any rune heraldic shield". Uri + double agents wired. |
| A3a | Treasure Trails — **the shared target layer + digging** | — | 300 | **done** | Five target kinds (coord 535 / npc 222 / loc 105 / kill 14 / key 12) behind one dispatch; the dig path solves all 535 coord-targeted clues. Hooked into `general_use/spade.rs2`'s chain. |
| A3b | Treasure Trails — **talk and search targets** (327 clues) | 1140 | 180 | **done** | Rides a new engine seam: `[proc,interact_npc_claim]` / `[proc,interact_loc_claim]` are asked before any `[opnpc/oploc]` dispatch. Unblocks Slayer and diary task hooks too. |
| A3c | Treasure Trails — **challenges, kill targets, and the clue DROPS** | — | 200 | **done** | Challenge questions answered via `p_countdialog`; 14 kill targets; and 807 (npc, tier) clue drops generated from this repo's own pinned wiki corpus — the 21 sites `shared_droptables.rs2` §2 deleted, restored from a better source. |
| A3d | Treasure Trails — **key targets** | — | 110 | **done** | The 12 two-action clues: kill for a key, then unlock a loc with it. |
| A3e | Treasure Trails — **the map-clue interface** | — | — | pending | `trail_map01`..`24` plus six per-clue variants; a map clue currently reads as a message. |
| A4a | Treasure Trails — **Hot & Cold** (the strange device) | 196 | 110 | **done** | The wiki's eight temperature bands; master device costs 3-8 hp a use. The dig that ends a hot/cold clue was already A3a's. |
| A4b | Treasure Trails — **light box, puzzle box, sextant UI** | 772 | — | pending | `clues/{LightBox,PuzzleBox}`, `coordinateutils/SextantInterface`; cache ships `light_puzzle`, `trail_slidepuzzle`, `trail_sextant`. |
| A5 | Treasure Trails — **reward tables** (easy→master) + casket open | 1347 | 1083 rows | **done** | Generated from the pinned wikitext by `tools/gen_trail_rewards.py` — 1,238 `DropsLineReward` entries parsed in exact rational arithmetic, 0 unresolved item names. |
| A6a | Treasure Trails — **milestones + the Mimic roll** | — | 130 | **done** | The wiki's six milestone counts; Mimic at 1/35 elite and 1/15 master with first-encounter dry protection. |
| A6b | Treasure Trails — **STASH units** | — | 260 | **done** | 44 hidey locs categorised and paired by a generator; the wiki's Construction build table; per-tier storage (stated deviation from per-unit). |
| A6c | Treasure Trails — **the Mimic fight, Watson, Patchy, Sherlock, scroll boxes** | 1100 | — | pending | Uri is done (A2); the Mimic ROLL and item are done (A6a). |
| A7a | Achievement Diaries — **the task registry** | — | 492 rows | **done** | 492 tasks generated from twelve pinned wiki pages. Cross-checks the authored per-tier totals on all 48 pairs — and caught two diaries' ids swapped. |
| A7b | Achievement Diaries — **the ~492 task hooks** | 3883 | — | pending | Each task's completion CONDITION. The wiki states them as prose and the cache states them nowhere, so every one is hand-written against the content it watches. The claim seam (A3b) is what the interaction-based ones will use. |
| A8 | **Grand Exchange** | 1882 | — | pending | `content/grandexchange/` |
| A9 | **Dwarf Multicannon** | 963 | 310 | **done** | Cache ships the whole build chain (4 stage locs + the finished cannon's own 4 ops). Rotation IS the fire clock, per the wiki. |
| A10 | **Tears of Guthix** | 827 | 290 | **done** | Replaces the quest tree's placeholder. Cache ships the cave, the nine walls, the stream forms and the side panel; the walls are discovered with `loc_findallzone` rather than listed. |
| A11 | **Shooting Stars** | 623 | 200 | **done** | 83 landing sites generated from the wiki's own map pins. Star state is `%vars` (world-shared), not a player varp. |
| A12 | Eight small systems — **audited, mostly not portable to this cache** | 2084 | — | **closed** | Muddy chest and Zahur already done elsewhere. Master scrolls, Creature Creation and the sandstone grinder have **no cache records at all** in rev239 — checked obj/loc/npc by name. Only `trouver_parchment` exists, and it needs the item-protection-on-death system this tree does not have. See §6. |

### Wave B — bosses

| # | Slice | NR | Ours | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| B1 | **The Nightmare of Ashihama** + Phosani's | 4899 | 260 | **partial** | Party-size scaling (from FIVE up), the per-phase special table, all twelve totem records and the both-halves drop gate are in and tested. Arena, instance and AoE need a scene. |
| B2 | **Vorkath** | 1264 | 320 | **partial** | The special-attack rotation — alternation, the random first, spawn immunity, the flat 50% acid reduction and the scaled explosion — is in and tested. The dragonfire types, the fireball dodge and the drop table remain. |
| B3 | **Wilderness bosses** — Callisto/Artio, Vet'ion/Calvar'ion, Venenatis/Spindel | 2883 | 352 | pending | `boss/wildernessbosses/` |
| B3b | Wilderness singles — **Scorpia, Chaos Fanatic, both archaeologists** | — | config | **partial** | Primaries done and correctly routed. The archaeologists' specials need the machine-owned `npc_stats_attackstyle.generated.rs2` extended, which is a generator change and not a batch pass. |
| B4 | **Alchemical Hydra** | 2384 | 240 | **partial** | Phase table, vent table, the 75% reduction and the enrage poison cadence are in and tested. The vents, arena and record swapping need a scene. |
| B5 | **Mage Arena II** | 1031 | 340 | **partial** | All three bosses' signature mechanics — the 1+5% drains, Porazdir's prayer-proof ball, Derwen's healing orbs, Zachariah's bind — are in and tested. Tele Block, freezes and the Kolodion wiring remain. |
| B6 | **Grotesque Guardians** (Dusk & Dawn) | 2010 | 340 | **partial** | Phase gates, per-phase/per-style immunity, and all of Mod Ash's phase-4 rules are in and tested. The record swapping and the AoE placement need a scene. |
| B7 | **Xamphur** | 1031 | 210 | **partial** | The fight A Kingdom Divided soft-skipped: Marks of Darkness, escalating corruption, magic immunity, no-melee. The crushing press and the cutscene remain. |
| B8 | **Abyssal Sire** | 1540 | 240 | **partial** | Stun ladder, vent damage floor, miasma bands and the once-only explosion gate are in and tested. The lair layout, tentacles and record swapping need a scene. |
| B9 | **Cerberus** | 1516 | 290 | **done** | The full rotation: three-style attack, souls every 7th under 400hp, lava every 5th under 200hp, and Mod Ash's 10% skip. |
| B10 | **Phantom Muspah** | 1289 | 290 | **partial** | Form swaps (damage-keyed, not health), the four-hit rule, special ordering, corruption and spike heal are in and tested. Arena, spikes and record swapping need a scene. |
| B11 | **Skotizo** | 861 | 200 | **partial** | The four awakened altars — two reduction rates, two caps, the one-hit demonbane disable, the 50-129 tick check interval and the one-minute cooldown — are in and tested. The lair, the totem and the drop table remain. |
| B12 | **Corporeal Beast** | 1284 | 210 | **partial** | The damage rules — corpbane, the stab-only gate, the 100 cap, Protect-from-Magic's 33%, split poison immunity — are in and tested. The dark energy core and the drop table remain. |
| B13 | **Mage Arena II** | 930 | 168 | pending | `boss/magearenaii/` |
| B14 | **Xamphur** | 875 | — | pending | `content/xamphur/` — phantom hand, area |
| B15 | **Rise of the Six** | 870 | — | pending | `content/rots/` |
| B16 | **Skotizo** | 829 | 44 | pending | instance, npc, plugins |
| B17 | **Corporeal Beast** | 766 | — | pending | `boss/corporealbeast/` (+ dark core) |
| B18 | **Kraken** | 644 | config | **done (combat)** | Magic, speed 4. Single-style, fully described by the contract. |
| B19 | **King Black Dragon** | 471 | already owned | **done** | `areas/wilderness/king_black_dragon.rs2` already has a three-way rotation incl. dragonfire — MORE than the infobox's two styles. This slice added its `damagetype`; a second rung would have been a duplicate. |
| B20 | **Sarachnis** | 469 | 140 shared | **done** | Melee primary + ranged secondary, both rungs. |
| B21 | **Obor** + **Bryophyta** | 774 | 140 shared | **done** | Obor melee+ranged, Bryophyta melee+magic, both rungs. |
| B22 | **Thermonuclear Smoke Devil** | 391 | config | **done (combat)** | Magic, speed 2. Single-style. |
| B23 | **Nex** — parity sweep against NR's 4190-line package | 4190 | 5600 (GWD total) | pending | ours may already exceed; **audit, do not rewrite** |
| B24 | **Tormented Demons** — NR variant vs our rs2012 port | 896 | 47 files | pending | reconcile; ours is an rs2012 backport, NR's is OSRS. See `tormented-demons-osrs` |

### Wave C — minigames and areas

| # | Slice | NR | Ours | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| C1 | **Chambers of Xeric — completion** | 24346 | 6498 | in progress elsewhere | already has [`COX_NEARREALITY_PORT_PLAN.md`](minigames/cox/COX_NEARREALITY_PORT_PLAN.md); this queue defers to it and tracks it here |
| C2 | **Tombs of Amascut — completion** | 18484 | 8846 | **done** | All eight rooms, the Wardens, invocations, paths, points, rewards and CAs are in, with 54 runtime checks. The one NR feature it lacked — the retrieval chest — turned out to be shared with a dozen other bosses and became C20. |
| C3 | **Gauntlet — parity sweep** | 5976 | 4413 | **done** | Swept NR's 68 classes against our triggers: every resource node, recipe, potion, demi-boss and Hunllef mechanic was already bound. The one genuine gap was `GauntletStatistics` — the scoreboard's world-shared half — now in `%vars`. |
| C4 | **Pest Control — completion** | 2956 | 1786 | **done** | The activity bar replaces the pre-2007 damage threshold; barricade/gate repair, the wiki reward table, the 4000 cap and elite void all landed. |
| C5 | **Warriors' Guild** | 2681 | 380 | **partial** | The token economy and the defender ladder — including the re-entry rule — are in and tested. Catapult, shotput, keg balance and the dummy room remain. |
| C6 | **Duel Arena** | 2430 | 543 | **done** | Ported as the **legacy duel** — the Duel Arena does not exist at this revision. The twelve rules, the shared rule/worn varp layout and the presets are in and tested; staking is deliberately absent because the game removed it. |
| C7 | **Prifddinas / elven city** (excl. Zalcano → B7) | 2688 | +clan crystals | **partial** | Clan-crystal livery is in, generated from the cache, and it turned up an engine defect. Crystal equipment and the singing bowl were already done under the Gauntlet. NPC dialogue and the crystal chest remain. |
| C8 | **Konar quo Maten** (slayer master, Mount Karuulm) | 2196 | 300 | **partial** | The two things that make her different — location-locked tasks and the brimstone key curve — are in and tested, the location table generated from the wiki. Binding location names to area bounds remains. |
| C9 | **Revenant Caves — completion** | 2610 | 1750 | **partial** | The four stacking drop-rate modifiers and the amulet of avarice are in and tested. The drop TABLE itself and the Revenant maledictus remain. |
| C10 | **Barrows — completion** | 2118 | 900 | **partial** | Reward potential, the roll count and the equipment-pool gate are in and tested against the wiki's own worked examples. The full reward table and the wight npcs remain. |
| C11 | **Pyramid Plunder — parity sweep** | 2134 | 2129 | **done** | Swept every entry in the wiki's Changes section against our implementation. All present, including the two post-2021 sceptre updates. Six selftest procs already wired and green. |
| C12 | **Partyroom** | 1760 | 500 | **partial** | The chest is a real world-shared container; the lever's two prices and the announcement ladder are in and tested. Only the deposit INTERFACE remains. |
| C13 | **Wilderness events** — hot zone, chest, Ganodermic Beast | 1558 | 250 | **partial** | Hot zone bands, the three leaderboards and the chest announcement are in and tested. The Ganodermic Beast needs a cache asset this snapshot does not contain — see the log. |
| C14 | **Castle Wars** | 1284 | 190 | **partial** | Clock, teams, flag rules and the four-outcome ticket table are in and tested. Catapults, barricades, doors and the arena need a scene. |
| C15 | **Stronghold of Security** | 1121 | 150 | **partial** | Per-floor reward economy and the combat-level portal skip are in and tested. The security questions and the maze doors need the dialogue/door pass. |
| C16 | **Chompy bird hunting** (the activity, not the quest) | 794 | 260 | **done** | The hat ladder — the reason anyone hunts past the quest — is in, generated from the wiki and cross-checked against the cache's eighteen objs. |
| C17 | **Motherlode Mine — completion** | 572 | 400 | **done** | Nugget economy, sack thresholds and both upper-level unlocks, verified. |
| C18 | **Waterbirth Island** + **Taverley** + **TzHaar** area plugins | 355 | partial | pending | `content/area/` remainder |
| C19 | **Wilderness slayer** + wilderness plugins remainder | 299 | 300 | **done** | Krystilia's 37-task table, generated and verified at runtime. |
| C20 | **Item retrieval service** (shared: ToA, ToB, Nightmare, God Wars, Barrows, Zulrah, Vorkath, RotS…) | 341 | 0 | **done** | Discovered while closing C2. |

### Wave D — skills

| # | Slice | NR | Ours | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| D1 | **Agility — rooftop courses** (Draynor→Prifddinas, 9 courses) | 5529 | 9 courses, xp verified | **done** | All nine were already implemented; `check-agility-courses` now holds each one's per-obstacle experience to the pinned wiki. The one real gap is the two diary bonuses — see the log. |
| D2 | Agility — **shortcuts** | 8242 | table generated | **data done, scripts pending** | All 162 shortcuts generated from the wiki with their levels, alternative routes and fractional xp; `check-agility-shortcuts` green. The per-obstacle bindings remain. |
| D3 | Agility — **Gnome/Barbarian/Wilderness/Pyramid/Pollnivneach courses** | 2995 | part of 3929 | pending | |
| D4 | **Magic — teleports** (all books + structures) | 3403 | in the 220-spell table | **data done, scripts pending** | Every teleport across all four books carries its level, xp and rune set in `spellbooks.generated.enum`. |
| D5 | Magic — **Lunar spellbook** | 2598 | 44 spells generated | **data done, scripts pending** | All 44 Lunar spells with levels, xp and rune sets, from pinned per-spell pages. |
| D6 | Magic — **Arceuus spellbook** | 1542 | 67 spells generated | **data done, scripts pending** | All 67 Arceuus spells, same source and generator as D5. |
| D7 | Magic — **regular spellbook remainder + lecterns + resources + actions** | 2701 | 84 spells generated | **data partly done** | The standard book's 84 spells are in the generated table. Lecterns, tablets and the non-spell actions remain. |
| D8 | **Slayer — completion** (tasks, masters, unlocks, dialogue) | 3544 | 1804 | pending | gates C8, B4, B18, B22 |
| D9 | **Farming — completion** (contracts, Hespori, seed vault, supercompost) | 8463 | 5804 | pending | |
| D10 | **Prayer** (ectofuntus, altars, bone burying at depth) | 1706 | 1419 | pending | |
| D11 | **Thieving** (tables, actions, pickpocket depth) | 1769 | 1515 | pending | |
| D12 | **Mining** + **Smithing** completion | 2627 | 1817 | pending | |
| D13 | **Woodcutting** + **Firemaking** completion | 1581 | 510 | pending | largest proportional skill gap |
| D14 | **AFK skilling** + fletching/cooking/fishing/herblore parity sweep | 661 + 3432 | present | pending | audit-and-fill, not rewrite |

### Wave E — Near-Reality custom and meta systems

Default to a content lane (§0.5) for this wave — a vanilla build should be able
to leave it out.

| # | Slice | NR | Ours | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| E1 | **PvM Arena** | 4514 | — | pending | waves, teams, revive, widget; depends on E14 |
| E2 | **Tournament** system | 3572 | — | pending | presets, spectating, area, controller |
| E3 | **Boons** | 3469 | — | pending | `content/boons/` + impl |
| E4 | **Middleman trading** | 2038 | — | pending | staff-mediated trade, history, offers |
| E5 | **Wilderness Vault** + Queen Reaver | 1982 | — | pending | |
| E6 | **Bounty Hunter** | 1773 | — | pending | tasks, teleport, targets |
| E7 | **Follower / pet system** | 1531 | partial (summoning familiars) | pending | registry gates B-wave pet drops |
| E8 | **Commands** (staff + player command surface) | 1499 | partial | pending | audit which are engine vs content |
| E9 | **Magic storage unit** | 1262 | — | pending | |
| E10 | **Alternate drop tables** | 1176 | — | pending | provider indirection; gates every B slice's drops |
| E11 | **Flower poker** | 1109 | — | pending | incl. gamble ban |
| E12 | **Loot keys** | 1104 | — | pending | + Skully NPC |
| E13 | **Universal shop** | 989 | — | pending | |
| E14 | **Clans** | 977 | — | pending | |
| E15 | **Presets** | 825 | — | pending | gates E1, E2 |
| E16 | **Crystal** equipment + recipes + chargeables | 760 | partial | pending | `content/crystal/` |
| E17 | **Hiscores** | 713 | — | pending | |
| E18 | **Rotten potato** (staff tool) | 677 | — | pending | |
| E19 | **Drops** framework + rewards + larran's key | 571+465 | partial | pending | |
| E20 | **Well of Goodwill**, **comp capes**, **contests**, **challenges**, **killstreaks**, **wheel of fortune**, **server events** | 516+512+420+190+290+124+159 | — | pending | one slice, seven small systems |
| E21 | **Donation / donator / vote** | 417+108+58 | partial (vote) | pending | |
| E22 | **Gravestones parity**, **ground items**, **imbue**, **glider**, **sailing**, **object/shop/combat/quest shims** | ~900 | partial | pending | audit-and-fill sweep, closes the tail |

### Wave F — seasonal and event content

| # | Slice | NR | Ours | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| F1 | **Christmas 2019** (incl. cutscenes) | 3752 | — | pending | `event/christmas2019/` |
| F2 | **Easter 2020** (area, npc, object plugins) | 3389 | — | pending | `event/easter2020/` |
| F3 | **Halloween 2019** | 1203 | — | pending | `event/halloween2019/` |
| F4 | **Advent calendar** + **Easter 2024** | 568+892 | — | pending | `content/advent/`, `seasonal/easter_2024/` |

---

## 6. Progress log

Append one dated entry per slice, in the CONTENT_PORT_QUEUE style: what landed,
the final script count, `mock230_pack` error count, the suite delta measured
with and without, and what was explicitly deferred with its reason.

- 2026-08-19 — queue created. Audit measured 82 NR content packages
  (~250k LOC), 95 slices after removing the 16 already-covered areas in §1.1.
  No slice started.

- 2026-08-19 — **A1 done.** `server/scripts/trail/` (660 lines of `.rs2` +
  configs), `tools/gen_trail_steps.py`, `docs/treasure_trails/` with a 35-page
  pinned wiki corpus and `sources/fetch.sh`.

  The finding that shaped it: **the clue database is already in the cache.**
  26 `cluehelper_*` dbtables (ids 3–28, ~2,000 rows) hold every emote, cryptic,
  anagram, cipher, coordinate, map, music, fairy-ring, hot/cold and skill
  challenge with its target, outfit, hidey-hole and text; 724 `trail_*` objs
  point into it via `param=trail_clue_row`; six more params state the tier.
  Near-Reality carries the same data as ~13,000 lines of hand-transcribed Java.
  None of it was ported — what was ported is the policy around it, and where NR
  and the wiki disagree the wiki won (NR's trail lengths are stale on every one
  of the six tiers).

  Landed: tier/row/type/text model, `Read` + `Check steps` bound by category
  across all six tiers, step advance, casket hand-over, completion counters,
  a generated per-tier step pool (658 objs), five selftest procs driven by a new
  C stanza.

  Engine, all required by the slice's own verification gate:
  * `mock230_pack` did not link at all — three symbols. Fixed; it is a gate
    again.
  * `cachepack`'s record merge treated `param` as a list, so two files stating
    one param both survived and the *first* won, while the runtime assigns per
    line so the *last* wins. Three npcs had a server band contradicting the
    world with no way to converge. Fixed by ranking three layers (cache /
    generated / authored) the way `mock230_content.c` already loads them.
  * One content bug it surfaced: `[skeletonmage]` authored twice with different
    `attack_anim`, one of them the giant skeleton's against a base rig.

  Final: 27738 scripts; `mock230_pack --check-only` **0 errors** (was 1, and
  that 1 was unfixable before the merge fix); selftest 15 unique failures with
  and without, identical sets, all pre-existing.

  Left open, filed not fixed: 8 npcs whose generated `attackrate` is stated
  twice with different values, and 56 where `npc_anims.generated.npc` overrides
  a hand-authored God Wars/Theatre anim on directory order alone. Both readers
  now agree about all 64, which is what made them visible; choosing the right
  value is a content pass. Also: `sscompile` accepts `if ($string = "")` and
  compiles it as an int comparison, which underflows the int stack at run time
  — it should be a compile error, and is not.

---

- 2026-08-19 — **A2 done.** `trail/scripts/trail_emote.rs2` (+ constants, varps,
  two selftests), one hook line in `interface_emote/scripts/emote.rs2`.

  Near-Reality's `EmoteClue.java` is 1,699 lines — the largest single file in
  its content tree — and all of it is the clue table: 126 clues × location
  polygon × emote list × item requirements, typed out. None was ported. The
  cache's `cluehelper_clue_emote` is that table, and the emote column turned out
  to be the client's own emote-tab index: `cluehelper_emote_beginner_0` is
  "Blow a raspberry at Aris" with emote 19, and `^emote_raspberry` is 19. No
  translation table was needed and the selftest pins that.

  Decoded on the way: **`param_258` is the treasure-trail item group** — 400
  objs across 55 groups, and the cache names the groups itself
  (`cluehelper_requirement_obj_param_trail_item_god_book` is group 7, and the
  twelve objs carrying group 7 are the twelve god books). That is how
  `cluehelper_outfit` says "any ring of dueling" in one row instead of eight,
  and it is now named `trail_item_group` in `configs/all.param.compack`.

  Landed: the emote hook (called for every emote, silent unless it completes a
  step), position check, the eleven-slot outfit check with both the obj lists
  and the item groups, two-emote sequencing, Uri summon per tier, double agents
  on the 42 clues the cache marks with a `combat_encounter`, the agent death
  rung calling the default drop before spawning Uri, and Uri's talk gate
  re-checking the outfit exactly as the wiki says it must.

  Two traps worth the record. The cache's `cluehelper_outfit` columns skip the
  three *body* wear positions (arms, head, jaw), so its column 8 is
  `wearpos_hands` = wear position **9** — passing the column as the position
  checks the wrong slot for four of the eleven and passes by luck for the rest.
  And `worn` is addressed by wear position, so `inv_add(worn, gold_ring, 1)`
  puts the ring on the player's *head*; it needs `inv_setslot`.

  Final: 27776 scripts; `mock230_pack --check-only` 0 errors; selftest 15 unique
  failures with and without, identical sets.

  Not in A2, by design: hidey-holes/STASH (119 emote rows carry one) are A6, and
  the two emote rows with a `requirements` column are A3's requirement model.

- 2026-08-19 — **A3a done.** `trail/scripts/trail_target.rs2` +
  `trail_dig.rs2`, two more selftests, one link added to
  `general_use/scripts/spade.rs2`'s `~*_try_dig` chain.

  The finding that set the shape: organising by *clue type* is the wrong axis.
  Every one of the 967 clue rows ends in a `target` pointing at one of five
  tables — coord 535, npc 222, loc 105, kill 14, key 12 — so what matters is
  which of five things the player must DO, and that is one dispatch shared by
  all eleven clue types. Near-Reality's one-class-per-type layout is what makes
  its clue code eleven files.

  The dig path covers the largest group outright: every coordinate clue, every
  map clue, and the 53 cryptic clues whose target is a coord. Double agents come
  from the row's own `combat_encounter` (162 of 169 coordinate rows carry one),
  not from the tier, and an ambush does NOT advance the clue — the player digs
  again once the agent is dead, which is what the wiki describes.

  Final: 27825 scripts; `mock230_pack --check-only` 0 errors; all seven trail
  selftests green; 4 unique suite failures, none in the trail stanza and none
  of them this lane's.

  **A note on measuring against this tree.** A second session is editing it
  continuously, and a back-to-back A/B is not reliable while that is true: a
  `::hauntedrun` failure appeared, an A/B with the spade hook removed said it
  was mine, and re-running with the hook restored showed it gone — the tree had
  moved between the two runs, not the hook. The suite went 15 → 17 → 4 unique
  failures across three runs of this slice with no change of mine explaining
  any of it. `tools/content_build_blame.sh` was written for the same reason:
  it answers "is this compile error mine" without parking anybody's file.

- 2026-08-19 — **A3b done, and the engine seam §7.4 asked for is built.**

  `mock230_scripts_run_claim()` runs a named content proc before the ordinary
  interaction dispatch, with the clicked npc or loc bound as the active entity,
  and treats a `true` return as having consumed the interaction. Two call sites,
  both in `interaction_dispatch`: `[proc,interact_npc_claim]` and
  `[proc,interact_loc_claim]`.

  It is optional by construction — a tree defining neither proc pays one failed
  name lookup per interaction and behaves exactly as before — which is what
  makes it safe on a path every click goes through. A proc that PARKS is treated
  as no claim, because the engine has to dispatch now and a script that has not
  decided cannot be allowed to eat the interaction on credit.

  This is the only way to express what the content needs. A trigger binds one
  subject: `[opnpc1,<npc>]` is exclusive, a category binding loses to a name
  binding, and `[opnpc1,_]` shadows every specific handler in the game. 139 of
  the 218 npcs a clue can point at already own an `[opnpc1,…]`. The same seam is
  what a Slayer-task hook and an achievement-diary task hook (A7) will want.

  Content: `trail/scripts/trail_solve.rs2` — 222 npc targets and 105 loc
  targets, with `fallback_npc`/`fallback_loc` honoured, and the anagram/cipher
  challenge posed (83 + 17 rows carry one). Claims only op 1, so a clue in the
  backpack cannot change what Attack or Pickpocket do on the same npc.

  Verification of the seam is mostly negative and that is deliberate: the
  selftest asserts the claim says **no** with no clue, no to the wrong npc, and
  no to a non-Talk op. A claim that always said yes would eat every "Talk-to" in
  the world, and no clue-shaped test would notice. The suite's existing
  npc-interaction stanzas — cook's assistant, the inverted fallback, npc
  addressing, name-keyed dispatch — all still pass.

  Final: 27834 scripts; `mock230_pack --check-only` 0 errors; all eight trail
  selftests green; 9 unique suite failures, every one of them CoX/ToB/pathing
  from the lane running alongside this one.

- 2026-08-19 — **A5 done.** `tools/gen_trail_rewards.py` +
  `trail_reward_aliases.tsv` + `trail/configs/trail_rewards.dbrow` (1,083
  entries over six tiers) + `trail/scripts/trail_casket.rs2`.

  Near-Reality's five reward tables are 1,347 lines of hand-typed Java. The
  wiki carries the same tables as 1,238 structured `{{DropsLineReward}}`
  entries, and **1,235 of the 1,238 item names resolve straight to a cache obj
  by display name** — so they are generated, not typed. The three that do not
  are in a hand-owned alias file: one is a disambiguated wiki title, two are not
  items at all (the Uri transform emote, a music track).

  Rarities are parsed in exact rational arithmetic, not floating point: several
  are of the form `1/( 1/13 * 1/125 )`, and rounding those through a float loses
  the distinction between neighbouring entries. All 1,238 parse.

  **The one approximation, stated in the generated file's own header.** The
  wiki's reward pages are nested sub-tables whose rarities are conditional on
  reaching the sub-table, and the per-tier rarities sum to 1.00 / 1.55 / 1.53 /
  1.07 / 2.20 / 1.96 rather than to 1. The weights are therefore treated as
  RELATIVE and normalised, and the generator prints the measured sum every run
  so the size of the gap stays visible. Which items exist, their quantities and
  which is rarer than which are all faithful; how often a casket yields a
  particular one is scaled.

  Two real bugs found by the tests rather than by reading:
  * **The beginner reward casket shares cache category 1314 with the beginner
    clue SCROLL.** So A1's Read binding also claimed the casket's Open —
    opening one ran the clue reader, rolled a trail length and said "nothing
    interesting happens". All six caskets now carry an authored category.
  * **Reward caskets carry no tier param.** The six `trail_is_*` markers sit on
    clue steps, and a casket is not a step; asking the usual way returned
    `none` and the casket silently refused to open.

  And one bad assertion of my own, worth recording because the shape recurs: the
  A3b selftest asserted that `trail_clue_easy_simple001`'s target is an npc
  because cryptic clues are mostly npcs. It is a **loc** — "Search the chest in
  the Duke of Lumbridge's bedroom". Cryptic targets are npc 115 / loc 99 /
  coord 53 / kill 14 / key 12, and assuming the majority case is how a test ends
  up asserting something the cache never said. It had been failing since A3b and
  my verification grep was too narrow to show it — the grep now covers the whole
  log, not a stanza window.

  Final: 27846 scripts; `mock230_pack --check-only` 0 errors; all nine trail
  selftests green.

- 2026-08-19 — **A3c done, and Treasure Trails is playable end to end.**

  Three things landed. Challenge questions: `cluehelper_challenge_question`'s
  column is a TUPLE (`string,int`) — the question *and* its answer — so it must
  be received into two locals; taking only the string leaks the int and makes
  every challenge accept nothing. Answered through `p_countdialog`/`last_int`,
  and a wrong answer is not punished, because a clue destroyable by a typo is a
  worse bug than one that can be retried.

  Kill targets: 14 clue rows name npcs as a LIST (one clue covers every record
  the wiki's phrase does), hooked from `~npc_default_death` rather than bound
  per npc, and not returning — a clue kill still drops what the npc drops.

  **And clue scrolls now drop.** `shared_droptables.rs2` §2 recorded that
  `~trail_*cluedrop` was deleted from all 21 LostCity sites on the grounds that
  "a stub that drops a clue scroll nothing can read is worse than no clue at
  all", and that the sites were recoverable. They are recovered — from a better
  source than those 21. This repo's `wiki/monsters/` corpus (978 pages) states
  clue drops with a dedicated `{{DropsLineClue|type=<tier>|rarity=A/B}}`
  template, 346 of them across 260 monsters, and the title→gameval join already
  exists in `npc_stats/`. `tools/gen_trail_clue_drops.py` reads both and emits
  **807 (npc, tier) rows over 656 npcs at the wiki's own rates.**

  So the whole system now closes: a clue drops from a monster at its real rate,
  reads, is solved by digging, talking, searching, killing or emoting, advances
  through the wiki's number of steps, and pays out of a table generated from the
  wiki's own reward pages. 2,300 lines of RuneScript, three generators, and not
  one clue, rate or reward typed by hand.

  Final: 27857 scripts; `mock230_pack --check-only` 0 errors; all eleven trail
  selftests green; all three generators `--check` clean; 9 unique suite
  failures, all CoX/ToB/pathing from the neighbouring lane.

- 2026-08-19 — **A4a done.** `trail/scripts/trail_hotcold.rs2`.

  All 142 `cluehelper_clue_hotcold` rows carry a coord target, so the dig that
  *ends* a hot/cold clue was already the ordinary dig path from A3a. What was
  missing was the only way to find out where — the strange device, which the
  cache ships as two records (`beginner_device`, `master_device`) sharing
  category 1650 and `ifop1=Feel`.

  The eight temperature bands are the wiki's own table, and the selftest walks
  their BOUNDARIES rather than their middles: an off-by-one in a band table
  reads perfectly at 50 tiles and is wrong at exactly 69. The master device
  costs 3-8 hitpoints per use and the beginner one is harmless, which is the
  only difference between two otherwise identical records — and the damage is
  taken *after* the inactive check, because the wiki is explicit that an
  inactive device does not hurt you.

  Suite: **0 unique failures** — the neighbouring lane's CoX/ToB reds cleared —
  and all twelve trail selftests green. `mock230_pack` 0 errors.

- 2026-08-19 — **A6a done.** `trail/scripts/trail_milestone.rs2`.

  Milestone rewards fire from `~trail_reward`, on the tick a casket is counted —
  the wiki's own trigger, and what `%trail_done_<tier>` has been counting since
  A1. Six counts (beginner 600, easy 500, medium 400, hard 300, elite 200,
  master 100); four are items and two are emotes. The two emotes are announced
  and not granted, because this world has no emote-unlock model at all
  (`interface_emote`'s own "Not ported" note: every gate would be permanently
  open or permanently closed). Saying nothing would be worse — a player who has
  done 300 hard clues would see the game do nothing.

  The Mimic rolls before the casket's rewards, because it REPLACES them: 1/35
  elite, 1/15 master, both quoted from Mod Wolf on the wiki, with dry protection
  guaranteeing the first encounter at 25 elite / 10 master caskets. It is keyed
  on having met none rather than on a rolling counter, which is what "your
  first Mimic encounter" says. The fight is A6b; the roll, the rates and the
  item are here so the encounter has something to hang off.

  Also caught: `return($roll = 0)` does not compile — this dialect takes no
  comparison inside a `return()` or as a call argument
  (`runescript-expression-limits`).

  Treasure Trails now stands at **2,515 lines** of RuneScript over 13 selftests,
  three generators, and a 36-page pinned wiki corpus. `mock230_pack` 0 errors,
  all generators `--check` clean, no trail failures in the suite.

- 2026-08-19 — **A9 done.** `server/scripts/cannon/`, a pinned 10-page wiki
  corpus in `docs/cannon/sources/`, and two selftests.

  The cache ships the entire build chain and needed no authoring:
  `multicannon_base` (7), `multicannon_stand` (8), `multicannon_barrels` (9),
  `dwarf_multicannon1` (6) carrying its own four ops (`Fire`, `Pick-up`,
  `Empty`, `Load X`), and `broken_multicannon` (5) for the decayed form.

  The one structural decision: **the rotation IS the fire clock.** The wiki says
  the barrel turns 45 degrees every tick and fires at most one ball per facing,
  so one per-tick timer does both and there is no separate attack speed anywhere
  in the file. Modelling them apart is how a cannon ends up firing eight balls
  into one direction.

  A binding collision worth recording: the Dwarf Cannon quest already owns
  `[oploc1,broken_multicannon]` for the broken cannon in the dwarf mines, and
  the player's own decayed cannon is the same loc. Rather than a second binding
  (a hard duplicate-script error), the quest's handler branches to
  `~cannon_repair` when `%cannon_stage >= 4` — so the quest keeps every case
  where the player has no cannon of their own, which is every case it cares
  about.

  **And a real miss, caught late.** The strange-device selftest had been
  reporting 3 of 9 for a whole slice, and my ad-hoc verification grep did not
  show it: the grep looked for "device" in the FAIL text and that assertion's
  wording does not contain the word. The cause was the same dialect limit
  already recorded in A1 — there is no string-comparison opcode, so
  `if ($s ! "x")` compiles as an int comparison and is not refused.
  `~trail_device_reading` is now split into a band id and a sentence, the test
  asserts the id, and `tools/trail_selftest_check.sh` exists so that "did any of
  MY assertions fail" is answered from a stated list rather than by grepping for
  words I happen to remember. `SELFTEST_CHECK` prints only on failure, so the
  checker verifies the stanza HEADERS ran as well — otherwise "absent" is
  ambiguous between passed and never-ran.

  Final: 27898 scripts; `mock230_pack` 0 errors; all three generators `--check`
  clean; `tools/trail_selftest_check.sh` green on all 17 owned assertions.

- 2026-08-19 — **A10 done.** `minigames/game_tearsofguthix/`, a pinned wiki
  corpus, three selftests. Replaces the placeholder
  `~tog_soft_collect_tears` in the quest tree, which awarded experience to a
  hand-picked low skill and said so.

  [cache] The cave was already built: nine `tog_weepingwall` locs, the stream
  forms `tog_weeping_wall_{good,bad,off,back}_{l,r}`, Juna, and a side panel
  with ten drip components, a counter and a progress bar. The nine walls are
  **discovered** with `loc_findallzone` rather than listed — an authored list of
  nine coords is nine chances to be wrong about a map this tree does not own.

  Both entry gates are implemented, which is the part a cooldown-only version
  misses: seven days AND 100,000 experience or one quest point *earned since*,
  so the baseline has to be stored rather than recomputed. Streams are drawn
  without replacement — the first draft drew each independently and could put a
  blue and a green on one wall, which is not a state the cave has, and the
  selftest asserts the masks never overlap across 50 rolls.

  Three dialect traps in one slice, all of which type-checked and none of which
  worked:
  * `$a, $b = ~proc()` puts both values on the same int stack, so a `(stat,int)`
    pair is only correct if the order is and nothing checks that. The
    lowest-skill accumulator returned Hitpoints on an account whose every other
    skill was level 1. Rewritten as one `if` per skill — no pair to get wrong.
  * `stat_base($variable)` does not read a chosen skill's level: a `stat` is a
    typed symbol the opcode takes literally, not an index a variable carries.
    The *test* was asking a question the dialect does not answer, and reported
    0 of 3 while the implementation was correct.
  * The skill is `runecraft`, not `runecrafting` — the latter resolves into a
    different namespace and aborted with `stat_base 790 is not a skill`. My
    first fix for it did nothing, because **BSD `sed` does not support `\b`**
    and the substitution silently matched nothing. `perl -pe` did it.

  `tools/trail_selftest_check.sh` caught all three within one run each, which is
  what it was written for.

  Final: 27936 scripts; `mock230_pack` 0 errors; all generators `--check` clean;
  20 owned assertions green.

- 2026-08-20 — **A11 done.** `minigames/game_shootingstars/`,
  `tools/gen_star_sites.py`, a pinned wiki corpus, three selftests.

  [cache] The nine size locs, the landing form, the noticeboard, the sprite npc
  and the stardust graphics were all already there. The **83 landing sites are
  generated from the wiki's own map pins** — `{{Map|mtype=pin|X,Y|…|desc=…}}` is
  a table, not prose, so it is parsed rather than typed. Near-Reality carries
  the same list as Java `ShootingStarLocation` entries.

  The star lives in `%vars`, the world-shared namespace, not in a player varp:
  one star crashes per world and everybody mines the same one. A player-scoped
  star would give each player a private one, which is the opposite of the
  activity.

  Two details the wiki pins down that a reading of the size table alone would
  get wrong: a star can only LAND at sizes 6-9 (the table gives 9-6 a crash
  chance and leaves 5-1 as `{{NA}}` — those are sizes a star decays into), and
  the discovery bonus is claimed **before** the level gate, because the page
  says "You do not need to meet the skill requirements for the layer to receive
  the bonus."

  The nine size locs are named rather than computed from an id base, and the
  selftest asserts all nine are distinct: the records are **not contiguous**
  (41019-41021, then 41223-41229), so arithmetic on the id would land in
  unrelated content and still look plausible.

  One self-inflicted trap, caught by the compiler: the world state was declared
  in a `.varn` file for one build. `varn` is NPC-scoped; a star belongs to the
  world. `varn` and `vars` share one RuneScript name domain, so the dead
  allocations left `%star_next_crash` ambiguous and stopped the compile — the
  check doing exactly its job. Removed with a note in `pack/varn.alloc`
  explaining why removing rather than keeping them is safe: no build ever
  shipped a record under those ids.

  Final: 27968 scripts; `mock230_pack` 0 errors; all four generators `--check`
  clean; 23 owned assertions green.

- 2026-08-20 — **A7a done.** `tools/gen_diary_tasks.py`,
  `interface_diaries/configs/diary_task{,s}.dbrow|dbtable`, a twelve-page pinned
  corpus, one selftest.

  Established first, by checking rather than assuming: **the diary task list is
  not in cache.osrs239 in any form** — not an enum, not a dbtable, not a struct,
  and not a clientscript string. So unlike Treasure Trails, this one genuinely
  has to be authored. The wiki carries it as numbered rows inside
  `data-diary-name`/`data-diary-tier` tables, which is a table rather than
  prose, so the registry is generated: **492 tasks across the twelve diaries.**

  What is generated is each task's text and its (area, tier, number). What is
  not — and cannot be — is its completion condition: the wiki states those as
  prose ("Partial completion of Dragon Slayer I", "30 coins or an activated ring
  of Charos"). That split is what makes the registry derivable at all, and it is
  A7b.

  **The registry deliberately does not replace `~diary_tier_total`.** Those
  authored constants were already right, and two independent derivations of the
  same 48 numbers are worth more as a cross-check than one number with two
  spellings. `~diary_registry_agrees` is that check — and it earned its place on
  its first run: the generator had **Wilderness and Western Provinces the wrong
  way round**, eight disagreeing pairs. Nothing else in the tree would have
  noticed, because the two diaries would simply have counted each other's tasks.
  `enum_595` and the tree's own constants agree on 9=Wilderness, 10=Western;
  alphabetical order does not.

  What the registry adds that this tree had no way to do: name a task. The panel
  could count them and could not say what any of them was.

  Final: 27974 scripts; `mock230_pack` 0 errors; all five generators `--check`
  clean; 24 owned assertions green.

- 2026-08-20 — **A3d and A6b done.**

  **Key targets.** The 12 `cluehelper_target_key` rows are the only clue shape
  that is TWO actions — kill for a key, then unlock a loc with it — and from the
  death hook they looked exactly like the 14 kill-targets, which is why they had
  been missed. Now separated explicitly rather than by whichever matched first.
  The cache states all four parts (which npcs drop it, which key, which loc,
  where), `npcs` is a LIST because "any Guard located around East Ardougne" is
  four records, and the key is required at the loc — accepting it without the
  key would collapse a two-action step back into one, which is the whole reason
  the table exists. The selftest asserts exactly that refusal.

  **STASH units.** The cache ships 44 hidey locs in built/unbuilt pairs, each
  already carrying `op1=Build` / `op2=Search`, and 119 of the 126 emote rows
  already state their `hidey_hole_loc` and `hidey_hole_coord`. Missing was the
  Construction content and the storage. `tools/gen_stash_locs.py` categorises
  the 44 by built state and tables the two facts their naming carries — which
  unbuilt form becomes which built one, and the tier suffix — because a script
  cannot read a loc's name. Build costs are the wiki's table (level 12-88,
  150-1500 xp, two planks of a rising grade and ten nails, plus a gold leaf for
  master), and "any hammer and saw" defers to the four saws
  `skill_construction` already recognises rather than inventing a fifth opinion.

  One deviation, stated in the code rather than hidden: storage is **per tier**,
  six containers, not per unit. A player who builds two stashes of the same tier
  finds they share contents. The faithful version needs 44 private containers
  per player, which is a save-format decision and not this slice's to make.

  Final: 27995 scripts; `mock230_pack` 0 errors; six generators `--check` clean;
  26 owned assertions green.

- 2026-08-20 — **A12 closed, not done, and the difference matters.**

  Audited all eight systems against the cache by name across obj, loc and npc:

  * **muddy chest** — already implemented (`areas/wilderness/lava_maze.rs2`).
  * **Zahur** — already implemented (`areas/area_desert/zahur_services.rs2`);
    the npc is `elid_herbalist`, which is why a name search for "zahur" missed
    it.
  * **master scrolls / item transportation**, **Creature Creation**, **the
    sandstone grinder** — **no records in cache.osrs239 at all.** No scroll
    objs, no homunculus npc, no grinder for sandstone (`grinder_machine` is
    unrelated). These postdate or sit outside this cache's content, and porting
    them would mean authoring models, locs and npcs, which is a cache-absorb
    job and not a content slice.
  * **Trouver parchment** — the obj exists (`ifop4=Read`, "Take it to Perdu to
    help you avoid losing certain items") and nothing else does. It needs
    item-protection-on-death, which this tree has no model for; the parchment is
    the UI on a system that would have to be built first.
  * **supply caches**, **books** — the NR versions are wrappers over content
    this tree already has other routes to.

  Recorded as **closed** rather than done or deferred: a slice that cannot be
  built from this cache is not waiting on effort, and leaving it `pending`
  would have it re-picked forever.

- 2026-08-20 — **B12 done — the first Wave B slice.**

  All three kings are already cache records (`dagcave_melee_boss`,
  `dagcave_magic_boss`, `dagcave_ranged_boss`), size 3, `op2=Attack`, 255
  hitpoints. They are identical in every stat the cache states and differ in the
  one it cannot: attack style. That difference IS the encounter — there are no
  phases and no specials — so the slice is four authored params and a stated
  choice about combat triggers.

  Per PORTING_GUIDE §4.3a the trigger choice is written down rather than left
  implicit: all three take the engine's default swing deliberately, and only
  Prime and Supreme override retaliation to `applayer2` so a ranged or magic
  king attacks from where it stands. Rex is deliberately absent from that
  override — melee retaliation is `opplayer2`, and overriding it would have him
  throwing punches across the room.

  **Two traps, and a finding about how this tree can be tested.**

  `damagetype` is not a melee/ranged/magic triple. Its values are
  `combat_damagetypes.constant`'s: 0/1/2 are melee SUB-styles (stab, slash,
  crush), ranged is 3, magic 4. Reading it the obvious way gave Rex crush (right
  by coincidence), Prime slash and Supreme stab — three melee bosses, and
  nothing in game would say so except Prime quietly not being weak to arrows.
  Rex's own infobox says slash.

  And a `^constant` expands inside a `param=` row but **not** in a bare npc
  field. `respawnrate=^dks_respawn_ticks` left the band holding 150 and the
  runtime holding 0 — three mismatched archives, the same split class the
  cachepack merge fix was about, caught by the same band check.

  **The finding: authored npc params are not script-testable here.**
  `nc_param(<type>, …)` reads the CACHE's param table; an authored `param=` row
  lands in the server's own def, which only `npc_param` sees, and that needs a
  live npc — which the C-driven selftest has no scene to provide. Every param
  read back 0 whether the config was right or wrong, and a test that answers 0
  for both is worse than none. So the contract moved to
  `tools/check_dks_contract.py`, which holds `dks.npc` to the pinned wiki
  infoboxes and is wired into `mock230-scripts` beside the tree's other content
  contracts. **Every future Wave B boss whose encounter is config rather than
  script will need that shape rather than a selftest.**

  Final: 27998 scripts; `mock230_pack` 0 errors; contract green; 26 owned
  assertions green.

- 2026-08-20 — **Ten solo bosses given their combat contract in one pass**
  (B18 Kraken, B19 KBD, B20 Sarachnis, B21 Obor + Bryophyta, B22 Thermy, and
  the four Wilderness singles as B3b).

  All ten are already cache records with their stat block, size and
  `op2=Attack`. What the cache cannot state is the attack rate and the attack
  style, and for four of them that is the entire encounter. So the slice is one
  config file plus `tools/check_boss_contract.py`, wired into `mock230-scripts`.

  Four are **complete** (Kraken, Thermy, Scorpia, Chaos Fanatic — single-style
  bosses). Six are **partial and marked so in the config itself**: Sarachnis,
  Obor, Bryophyta, the KBD and both archaeologists each have a second attack the
  infobox lists and `damagetype` cannot hold, because it is one value. Those
  need their own rung, and saying which are half-described is the difference
  between a slice that is finished and one that merely compiles.

  **The checker was mutation-tested before being trusted** — a wrong style and a
  wrong speed each turn it red, and restoring turns it green. It also does one
  thing the config cannot: it compares the wiki's hitpoints against the CACHE
  record's `stat4`. That is a check on record IDENTITY, which is the likeliest
  mistake when binding ten bosses at once and the hardest to notice afterwards —
  `hillgiant_boss` really is Obor and `gb_mossgiant` really is Bryophyta, and
  now something says so.

  Final: 27999 scripts; `mock230_pack` 0 errors; both contracts green; 26 owned
  assertions green.

- 2026-08-20 — **Second attack rungs: B20 and B21 complete, and three bosses
  turned out to be somebody else's already.**

  Six of the ten batched bosses list two attack styles and `damagetype` holds
  one, so the second style is a rung. Writing them found that half of the six
  were already handled:

  * **The King Black Dragon** — `areas/wilderness/king_black_dragon.rs2` owns
    `[ai_opplayer2,king_dragon]` with a three-way rotation including dragonfire
    and the special breaths, which is *more* than the infobox's two styles. My
    rung would have been a duplicate-script error, and the near miss is worse:
    whichever bound first would win and the other would silently never run. B19
    is done, and what this slice actually contributed to it was the
    `damagetype` its melee bite rolls against.
  * **Both archaeologists** — `npc/scripts/npc_stats_attackstyle.generated.rs2`
    already routes them to `~npc_rangeattack`. That file is machine-owned and
    exists for a reason its header states: a Ranged- or Magic-style npc left on
    the melee default rolls accuracy off Attack and damage off Strength whatever
    `damagetype` says. Their primaries are correct; only the specials are
    missing, and adding those means extending the generator, which is not a
    batch pass. They stay `partial` and the queue says why.

  So the rungs written are Sarachnis (melee + ranged), Obor (melee + ranged) and
  Bryophyta (melee + magic). Each **owns the whole swing**, primary included —
  a rung that handled only the special and fell through would swing twice.

  The four single-style bosses are deliberately absent from the file, and it
  says so: PORTING_GUIDE §4.3a's point is that naming no rung is a *choice*, and
  for them the engine's default is exactly right.

  One number is ours and flagged as such: `^boss_second_style_chance = 3`. The
  infoboxes state that both attacks exist, not how often each is used. It is one
  constant rather than six so a measured rotation replaces one thing.

  Final: 28010 scripts; `mock230_pack` 0 errors; both contracts green.

- 2026-08-20 — **B9 Cerberus done — the first deep encounter of Wave B.**

  The lair plumbing already existed (`cerberus.rs2`: the crawl, the winch, the
  lobby). What was missing was the fight, and it is a rotation rather than a
  phase machine — one form, three specials gated on remaining health:

  * six-tick cycle;
  * ordinary attack cycles magic → ranged → melee, and **the melee reaches the
    player at any distance**, which is why it does not call `~npc_meleeattack`;
  * souls every 7th attack, only under 400 hitpoints;
  * lava every 5th attack, only under 200 hitpoints;
  * and a 10% chance she does a normal attack anyway when a special is due —
    Mod Ash, quoted on the Strategies page.

  **The counter is the state, not the clock.** "Every seventh ATTACK" is what
  the page says; a tick-keyed version drifts the moment she is stunned, frozen
  or out of range. And the counter resets on death through the existing
  `~cerberus_killed`, because carrying it across a kill would make the next
  Cerberus open mid-rotation.

  Both specials are due on attack 35 (a multiple of 5 and 7) and the wiki gives
  no precedence; souls are taken first because their gate opens earlier. The
  selftest asserts **both are genuinely due at 35**, so that precedence is a
  real choice rather than an accident of one never firing — and it asserts the
  two gates are strict (`< 400`, not `<= 400`) and are not swapped, which is the
  mistake that would put lava at 400 hitpoints and look like a working boss
  until somebody counted.

  A duplicate-binding trap, the third this session: `cerberus.rs2` already owns
  all three records' `[ai_queue3]`. A second one would be a hard error, and the
  near miss is worse — one wins, the other silently never runs.

  Final: 28020 scripts; `mock230_pack` 0 errors; both contracts green; 27 owned
  assertions green.

- 2026-08-20 — **B6 Grotesque Guardians — the numbers and the rules, tested.**

  A genuine phase machine, unlike Cerberus: four phases, and in each one a
  different member of the pair is attackable while the other is immune. The
  cache already draws every phase as its own npc record
  (`gargboss_dawn_phase1`, `gargboss_dusk_phase2_attacking`,
  `gargboss_dawn_phase3`, `gargboss_dusk_phase4` at size 6 where the rest are
  4), so a transition is a record SWAP — which is also why the phase must be
  stored rather than derived: the swap, the animation and the invulnerability
  change have to happen exactly once, and a derived phase re-fires them every
  tick.

  What is implemented and tested is everything that is arithmetic: the phase
  gates, the per-phase-per-style immunity table, and all four of the Mod Ash
  rules the Strategies page quotes.

  Three details worth having pinned, each of which would be invisible if wrong:

  * **Phase 3's gate is not a percentage.** Phases 1 and 2 end at 55% of 450;
    phase 3 ends when Dawn is "less than 10 health". Working down the page it is
    natural to write a third percentage, and nothing in play would look wrong.
  * **The first phase-4 orb does not roll damage.** Mod Ash: "treats the NPC's
    max hit as 60% of its usual value" — it is exactly 60%, halved if praying.
    An implementation that rolled it would have about the right average and be
    wrong on every single hit.
  * **Praying doubles the style-switch chance** (1/3 rather than 1/6). Asserted
    as a comparison between a prayed and an unprayed sample rather than as two
    rates, because that is the claim the quote makes and it survives sampling
    noise.

  Marked **partial**, not done: the record swapping, the rockfall and lightning
  AoE placement, and the energy spheres all need a live scene, which the
  C-driven selftest has none of. The numbers they will use are in the config and
  cited.

  Final: 28034 scripts; `mock230_pack` 0 errors; both contracts green; 28 owned
  assertions green.

- 2026-08-20 — **B8 Abyssal Sire — the arithmetic half.**

  Phase 1 is not a damage race: the Sire is invulnerable behind four
  respiratory systems, those are protected by six tentacles, and the tentacles
  only open while the Sire is STUNNED by a shadow spell. It is a puzzle with a
  timer, and the timer is the mechanic. Every form is already a cache record
  (seven Sire states, the lung, the Scion), so a phase change is a record swap —
  the same shape as the Grotesque Guardians.

  Four things pinned, each of which is a specific way to be wrong:

  * **The stun ladder is a ladder** — Rush 25, Burst 50, Blitz 75, Barrage 100 —
    and the top rung being exactly 100% is what makes the published opener a
    plan rather than a gamble. The selftest draws 200 barrages and requires all
    200 to land.
  * **The wake delay applies only to a sleeping Sire.** Ten ticks, "due to the
    Sire having to perform its waking up animation" — one boolean apart from the
    fast opener, and the exact trap the page warns about.
  * **The vent damage rule is a FLOOR, not a bonus.** A roll of 5 against a max
    of 50 becomes 25; a roll of 40 stays 40. Adding 50% instead would make a
    max-50 weapon hit for 75 and turn the wiki's two-hit vent into a one-hit
    vent.
  * **The explosion gate is strict and fires once.** Re-checking every tick
    would teleport the player back into the blast on the tick after they escaped
    it — a hard mechanic becoming an unsurvivable one.

  Marked **partial**: the lair layout, the six tentacles, the record swapping
  and the spawns need a live scene. Their numbers are in the config and cited.

  Noted in passing: `mock230_pack` went to 4 errors during this slice, all of
  them `minigame_cox/configs/cox.varp` from the lane running alongside — a
  varbit name not in `all.varp.compack` and a malformed line. Not this lane's,
  and recorded so the next reader of that count knows whose it was.

  Final: 28041 scripts; both contracts green; 29 owned assertions green.

- 2026-08-20 — **B10 Phantom Muspah — the rule every implementation gets wrong.**

  **Its form changes are keyed on DAMAGE TAKEN, not on health.** 100 damage in
  ranged form, 80 in melee. So the state is an accumulator that resets on each
  swap, and a health percentage is used only for the two specials. Writing it
  as health thresholds — the obvious reading, and what every other boss in this
  wave does — produces a boss that swaps on a schedule instead of in response
  to the player.

  Four more things pinned:

  * **Two conditions after the first swap, not one.** "After switching forms for
    the first time, it must also take damage at least four times before it can
    switch again." Tracking only damage lets one large hit chain two swaps;
    tracking only hits lets four glancing blows swap a boss that has taken
    almost nothing. The selftest asserts both directions and that the FIRST swap
    is damage-only.
  * **The thresholds differ per form** (100 vs 80). One number for both is the
    obvious simplification and makes melee phases 25% too long.
  * **Which special fires first depends on the spawn form.** Ranged spawns get
    lightning clouds at 75%, melee spawns get homing spikes; the unused one
    always follows at 50%. A fixed order is right half the time and looks right
    the other half.
  * **Corruption escalates 3/6/9 and sums to 18.** The sum is the check that
    catches a flat drain of 6 three times — same total, wrong shape.

  Marked **partial**: the arena, the spike field and the record swapping need a
  live scene.

  Final: 28107 scripts; `mock230_pack` 0 errors; both contracts green; 30 owned
  assertions green.

- 2026-08-20 — **B4 Alchemical Hydra — the arithmetic half.**

  Four phases at 25% of 1100, each with its own chemical vent, and in the first
  three the Hydra takes **75% less damage until lured onto the right vent**. So
  the fight is a positioning puzzle wrapped around a damage race, and what is
  worth pinning is the cadences.

  Three specific ways to be wrong, all now asserted:

  * **The enrage phase has no damage reduction even though it has no vent.**
    "During the first THREE phases" is the wiki's wording. A version that kept
    reducing "until vented" would make the last quarter of the fight take four
    times as long, and would look like a tuning problem rather than a bug.
  * **Enrage poison is "the fourth attack and every ninth attack afterwards"** —
    4, 13, 22, 31 — not a period of four. Reading it as every fourth is the
    obvious misreading and makes the enrage roughly twice as poisonous.
  * **Three attacks is both the special cadence and the style-switch cadence**
    in phases 1-3, and the enrage switches style every attack instead. One
    number serving two purposes looks like a coincidence in the source and is
    not.

  Marked **partial**: the four vents, the arena and the record swapping need a
  live scene.

  Final: 28115 scripts; `mock230_pack` 0 errors; both contracts green; 31 owned
  assertions green.

- 2026-08-20 — **ENGINE: `^constant` in an npc param parsed to zero.**

  The find of the session, and it was live in the tree before any of this
  lane's content.

  `apply_param` in `mock230_content.c` expanded a `^constant` in exactly TWO
  branches (`undead` and the elemental weakness) and read the value with a bare
  `atoi` everywhere else. **`atoi("^slash_style")` is 0.** So
  `param=damagetype,^slash_style` and `param=attackrate,^dks_attackrate` both
  silently became zero: the config read correctly, the compiler had no opinion,
  `mock230_pack` was happy because it checks the TEXT, and the npc simply fought
  with damage type 0 and no attack rate.

  Fixed at the single point every branch reads the value — one expansion, before
  the dispatch — so a param cannot be `^`-aware in some branches and not others
  again. An unresolvable `^name` now takes the same error path a misspelled
  constant already did instead of reading as 0.

  **How it was found, and why nothing else could have.** The value is only
  observable through `npc_param` on a LIVE npc. That needs a scene, and the
  earlier B12 note concluded the C-driven selftest has none — which was wrong,
  and usefully so: the selftest player stands in Lumbridge with a built scene,
  and `npc_add(coord, …)` at the player's own tile spawns into it. Spawning at a
  hardcoded square was what had failed, not spawning as such.

  So the B12 conclusion is superseded: **authored npc params ARE testable at
  runtime**, by spawning at `coord`. The Dagannoth Kings' contract is now two
  layers — `check_dks_contract.py` on the config text, and a selftest on the
  parsed values — and only the second could see this bug. It was
  mutation-tested: changing one king's style turns it red at exactly that
  king's assertion, and restoring turns it green.

  This also means the five `partial` bosses can have their config halves
  verified the same way, and that the "needs a live scene harness" blocker
  recorded against them is smaller than it looked: what actually needs a scene
  is arena geometry and AoE placement, not param reads.

  Final: 28115 scripts; `mock230_pack` 0 errors; both contracts green; 32 owned
  assertions green.

- 2026-08-20 — **B7 Zalcano — the odd one out of Wave B.**

  Not a combat boss at all: [wiki] "fought using skilling rather than
  conventional combat ... players must use their Mining, Smithing, and
  Runecraft skills to create imbued tephra." Two damage tracks — the armour,
  hit with thrown tephra, and her hitpoints, mined with a pickaxe — and the
  drop weighting counts them **differently**. That asymmetry is the encounter,
  and it is pure arithmetic, so all of it is tested.

  Four things pinned, three of them Mod Ash or Mod Lenny quotes:

  * **The tephra formula excludes Mining.** `5 + (Smithing + Runecraft)/14` —
    "the average of your Smithing (not Mining) and Runecrafting levels". Mining
    is the skill the boss is named for and IS used on the other track, so a
    formula averaging Mining and Smithing would look right and be wrong by a
    plausible amount at every level.
  * **The blue sigil is +50%**, and the test uses Mod Ash's own worked pair:
    16 becomes 24 ("hence you calculating 16 but observing 24").
  * **Armour damage counts double toward points**, and the caps agree with the
    stated maximum: 400 + 2×300 = 1000. Get either wrong and the maximum stops
    being reachable — the kind of error nobody notices because nobody hits a
    ceiling they cannot reach. Mod Lenny's worked example (150 health + 200
    armour = 550) is asserted directly.
  * **Two drop gates on two different quantities** — 5 shield damage for the
    main table, 31 COMBINED for uniques. One number for both would lock a
    player who mined but never threw out of the main table entirely, and the
    test asserts a pure miner is refused.

  Marked **partial**: the demonic symbols, the golems and the arena need a
  scene.

  Final: 28126 scripts; `mock230_pack` 0 errors; both contracts green; 33 owned
  assertions green. The 21 suite failures in this run are the neighbouring
  lane's (blackarmgang, sheeprun, smithingrun, TD).

- 2026-08-20 — **B5 Araxxor — two counts that share a number.**

  A cadence boss like Cerberus, and the thing worth getting right is that the
  eggs and the specials are **two different counts that happen to share the
  interval 6**:

  * [wiki] "The first egg hatches after three standard attacks ... Each
    subsequent egg hatches after every six standard attacks" — so 3, 9, 15, 21.
  * [wiki] "Every six standard attacks, Araxxor will use one of three special
    attacks" — so 6, 12, 18, 24.

  They never coincide, and the selftest asserts they DISAGREE at both 6 and 9,
  in opposite directions. Merging them into one counter — the obvious
  simplification, since both say "six" — would put every egg on a special and
  halve the number of minions. They are kept as two constants for the same
  reason.

  Three more:

  * **The first egg interval differs from the rest** (3, then 6), the same shape
    as the Hydra's enrage poison. Reading it as "every three" doubles the
    minions.
  * **The special is READ off the south-easternmost egg's colour, not rolled.**
    It is the only tell the fight gives; rolling it removes the mechanic while
    looking identical in a log.
  * **The Mirrorback composes three effects from one hit** — 20% diverted, the
    minion takes exactly that, and half of THAT reaches the player. On a
    100-damage hit: 80 to Araxxor, 20 to the minion, 10 to the player.
    Reflecting 50% of the whole hit is two and a half times too harsh and reads
    as a tuning complaint rather than a bug. The test also asserts the absorbed
    and delivered halves always sum to the original, so the minion can neither
    create nor destroy damage.

  Marked **partial**: the arena, the acid pools and the minion AI need a scene.

  Final: 28137 scripts; `mock230_pack` 0 errors; both contracts green; 34 owned
  assertions green.

- 2026-08-20 — **B2 The Forgotten Four — DT2's bosses.**

  Four bosses in one file because they share exactly one thing and differ in
  everything else. The shared thing: [cache] each is drawn in a QUEST form and a
  POST-QUEST form with different hitpoints — Duke 360/485, Whisperer 660/900,
  Leviathan 720/900, Vardorvis 500/700. That split is the cache's own and is
  **not** a scaling rule, so nothing computes one from the other; the selftest
  asserts each pair independently and that the quest form is always weaker.

  Four mechanics pinned:

  * **Vardorvis gets STRONGER as he weakens.** "his Defence level lowers while
    his Strength level rises" — two scalings in opposite directions off one
    quantity, which is the whole character of the fight: easier to hit and
    harder to survive at once. Scaling both the same way is the natural mistake
    (both are "scale with remaining health") and produces a boss that is simply
    easier at the end. The test asserts the direction at both ends and
    monotonicity between.
  * **His lifesteal is 50% rounded DOWN**, which the page says outright — so a
    1-damage hit heals him nothing. That is the difference between chip damage
    being free and being counterproductive.
  * **Prayer halves the axes' initial hit and does nothing to the bleed.**
    Applying it to both would halve the whole attack, and the bleed is a large
    part of it (35 initial against 15 over five ticks).
  * **The Whisperer's melee is "42 (x2)"** — a ceiling of 84, not 42. The
    "(x2)" reads like a typo and is the difference between a survivable hit and
    a lethal one.

  One thing deliberately NOT written: the Leviathan has no `attackrate`
  constant, because its infobox says "attack speed = Varies". Inventing a number
  would be stating something the source does not.

  Marked **partial**: the four arenas and the special-attack AI need a scene.

  Final: 28144 scripts; `mock230_pack` 0 errors; both contracts green; 35 owned
  assertions green.

- 2026-08-20 — **B1 The Nightmare — the largest boss in the wave.**

  [cache] The whole encounter was already drawn and needed no authoring: three
  phase records and three "weak" records per variant, the blast/initial/dying
  forms, **four totems in three states each**, parasites (80 and 40 hp) and both
  husks. Phosani's is a separate record set, not a flag.

  Three things pinned:

  * **The shield scales from FIVE players up, not from one.** "The minimum
    durability is 2,000 and caps at 19,600", and the 2020 change adds "200 and
    30 health ... per player" **"for groups of five or more"**. A version adding
    200 from the first player gives a solo Nightmare a 200-point shield and hits
    the documented floor at five purely by coincidence — which is exactly the
    kind of agreement that makes a wrong formula look verified. The test asserts
    the floor holds at 1 AND at 5, and that 6 is one increment above.
  * **Each special is locked to its phase, and the refusals are the assertion.**
    Claws in all three; husks and corpse flowers phase 1; curse and parasites
    phase 2; surge phase 3. A roll from one shared list allows everything
    everywhere and looks completely normal in a log — so the test checks that
    husks are REFUSED in phases 2 and 3, not just permitted in 1.
  * **Drop eligibility needs damage to BOTH the shield and the totems**, not
    either. That is what stops a pure-DPS player skipping the mechanic the whole
    fight is built around.

  Marked **partial**: the arena, the instance framework and the AoE placement
  need a scene.

  With this, **every boss in Wave B has been touched.** Final: 28158 scripts;
  `mock230_pack` 0 errors; both contracts green; all six generators `--check`
  clean; 36 owned assertions green.

- 2026-08-20 — **C14 Castle Wars — the first Wave C slice.**

  [cache] The whole minigame was already drawn: both standards, both catapults
  and their broken forms, the team portals/exits/quit portals, banners, cloaks,
  bandages, barricades, toolkits, explosive potions, climbing ropes, catapult
  rocks, Lanthus and the four barricade npcs. Nothing here authors a loc, obj or
  npc. What was missing is the GAME.

  State lives in `%vars`, the world-shared namespace — a Castle Wars game
  belongs to the world, both teams sharing one clock and one pair of scores. A
  player varp would give every player a private match. Same choice as Shooting
  Stars, same reason.

  Two rules pinned:

  * **The ticket table has FOUR outcomes, not three.** A shut-out win pays 4
    where an ordinary win pays 3, and a scoring draw pays 3 where a scoreless
    one pays 2. A win/lose/draw table collapses both distinctions and is wrong
    for exactly the games players care about. The test asserts the two
    inequalities directly — shut-out strictly beats a regular win, scoring draw
    strictly beats scoreless — because those are the comparisons a collapsed
    table fails.
  * **A capture needs the carrier's OWN standard at home.** "Return it TO the
    standard in your own castle" — so a carrier whose flag has been stolen
    cannot score. That is what makes a simultaneous steal a stalemate rather
    than a race, and it is the rule an implementation checking only "am I
    carrying" would lose.

  Marked **partial**: the catapults, barricades, breakable doors and the arena
  itself need a scene.

  Final: 28171 scripts; `mock230_pack` 0 errors; both contracts green; 37 owned
  assertions green.

- 2026-08-20 — **C15 Stronghold of Security.**

  [cache] The dungeon is already drawn — `stronghold_ent`, every floor's gates
  and portals in closed and open forms, and both reward boots (`sos_boots`,
  `sos_boots2`). What was missing is the reward economy and the skip rule.

  Two things pinned:

  * **The portal skip is on COMBAT LEVEL and is explicitly independent of the
    reward.** The wiki says it outright: "can be used at a combat level of 26 or
    above, **even if the reward hasn't been claimed before**". Implementing it as
    "you may skip a floor you have already cleared" is the natural reading of a
    dungeon shortcut and is the opposite of what the page says — it would lock a
    high-level player out of the shortcut that exists precisely for them. The
    test asserts both directions: unclaimed-but-high-level may skip, and
    claimed-but-low-level may not.
  * **The fourth floor pays boots, not coins.** 2,000 / 3,000 / 5,000 and then a
    choice of boots, so the reward is a table rather than a scaling formula, and
    floor 4 answering 0 coins is asserted rather than left to fall through.

  Marked **partial**: the security questions (a dialogue pass) and the maze
  doors remain.

  Final: 28183 scripts; `mock230_pack` 0 errors; both contracts green; 39 owned
  assertions green.

- 2026-08-20 — **C5 Warriors' Guild — the economy and the defender ladder.**

  [cache] The guild was already drawn: Lorelai, the six high-level cyclopes, the
  catapult and an animated-armour npc per metal. `warriorsguild_cyclops.rs2` and
  its siblings already owned the doors and npcs. What was missing is what the
  activities PAY and how the ladder advances.

  Three things pinned:

  * **The rule of this minigame, and the one easiest to miss:** "After obtaining
    the first defender of each metal, the player must leave the room and
    re-enter before the next tier defender can drop." Without that gate a lucky
    player walks the entire ladder in a single trip, which is materially
    different content. It needs two pieces of state — how far up the ladder, and
    whether they have left since — and the test asserts a fresh player needs no
    re-entry while one holding a bronze defender does.
  * **The token table is per METAL, not per hitpoint.** Bronze is 10 HP for 5
    tokens, steel 40 for 15 — the wiki prints a tokens-per-HP column precisely
    because it falls from 0.500 to 0.375. A rate over-pays steel by a third, and
    the test asserts steel pays strictly LESS than four bronzes.
  * **The Combat Achievement multiplier applies "throughout the guild"**, so it
    is one proc every activity calls rather than a rule repeated per activity.

  Marked **partial**: the catapult, shotput, keg balance and the dummy room
  remain.

  Final: 28190 scripts; `mock230_pack` 0 errors; both contracts green; 40 owned
  assertions green.

- 2026-08-20 — **C2 Tombs of Amascut — closed by reading, and it spawned C20.**

  The row said `pending` on a line-count comparison. It was wrong. Ours is 8846
  lines with a 2105-line selftest running **54 runtime checks**, and it already
  covers all eight rooms (Akkha, Baba, Kephri, Zebak, Het, Crondis, Scabaras,
  Apmeken), the three Warden phases, the invocation board and its 46
  invocations, path levels, raid level scaling, points, deaths, rewards and the
  Combat Achievement ladder. Walking NR's 130 ToA classes against our trigger
  list found exactly one feature absent: `TOARetrievalChestAction`.

  **And that turned out not to be ToA's at all.** `ContainerType
  .ITEM_RETRIEVAL_SERVICE` is **525** — the same cache inv this tree already
  calls `gravestone`. The retrieval service and the gravestone are two
  presentations of one store, which is why a player can never have both. It is
  shared by eighteen services across ToB, the Nightmare, God Wars, Barrows,
  Zulrah, Vorkath, RotS, the Mimic and more. Filed as **C20** and implemented.

- 2026-08-20 — **C20 Item retrieval service — the shared death store.**

  Three rules, each with an assertion naming the misreading it rules out:

  * **The container holds ONE death.** `DeathMechanics.service()` calls
    `container.clear()` before it adds anything, so dying a second time destroys
    what was waiting. It does not accumulate and it does not refuse the second
    death. A port that appends quietly doubles a player's insurance — the test
    stores coins, stores a bar on top, and asserts the coins are **gone**. I
    mutation-tested this one by deleting the clear: it fails.
  * **Locked is index equality, not price.** `isFree()` is
    `lockedEnumIndex == unlockedEnumIndex`. God Wars has a pair (100, 101) that
    is unequal but no entry in the price enum — so it is locked, needs the
    unlock click, and costs nothing. Reading "free" off the price collapses
    that. The Tombs is the pair that runs backwards, (39, **38**): `locked + 1`
    would send its panel the wrong state and nothing else's.
  * **The fee is paid from the inventory first and the bank for the
    remainder**, and affordability sums both. Charging only the inventory makes
    the service unusable in exactly the case it exists for — a player who died
    carrying nothing. The test pays 100000 against 40000 carried and 70000
    banked and asserts the bank is down 60000.

  **[cache] Not one price is typed into this tree.** Enum **1757** is the price
  list and `~retrieval_cost` reads it at runtime; the selftest asserts three of
  its values so that a cache which stops shipping it says so out loud instead of
  charging a wrong price silently.

  Wired through a `%retrieval_area` seam — the equivalent of NR reaching
  `player.getArea()` — so `[proc,player_death]` branches to the service or the
  gravestone, and ToA sets it on both entry paths and gives it back on exit. A
  stale claim would send a death in the open world to a boss's chest, where the
  player would never look, so the test asserts the give-back too.

  Final: 28255 scripts; `mock230_pack` 0 errors; both contracts green; 42 owned
  assertions green.

- 2026-08-20 — **C3 Gauntlet — swept, and it needed one thing.**

  NR ships 68 Gauntlet classes. Walking them against our trigger list, every
  resource node was already bound — the crystal deposit, phren roots, linum
  tirinum, grym leaf, the fishing spot, the tool storage, the cooking range,
  the water pump and the singing bowl — along with the recipes, the egniol
  potion chain, the demi-bosses, the Hunllef's tile patterns and both reward
  tables. My first pass thought half of them were missing; it was grepping for
  NR's Java names (`GauntletWaterPump`) rather than our loc names
  (`gauntlet_sink`). Worth remembering: a name-based gap audit across two trees
  finds absences that are not there.

  **The one real gap was `GauntletStatistics`** — the scoreboard printed
  "Not tracked" for all six global figures. They are server-owned, not
  per-player, so they belong in `%vars`: a varp would give every player a
  private "global" count and the board's two columns would always read the same.

  The rule the test pins: **a best time is a MINIMUM, and the unset value must
  stay out of the comparison.** NR's sentinel is -1 and this tree's is 0, but
  the rule is identical either way — feed the sentinel into the `<` and no real
  run ever beats it, so the board freezes. The test records 900, then 1500, and
  asserts the best is still 900. It also asserts the two Gauntlets keep separate
  records: one shared set would let a Corrupted run set the regular board's
  time, and they are very different activities.

  One trap re-encountered: **`!` on two strings compiles as an int comparison
  and underflows at runtime.** `compare($a, $b) ! 0` is the form that works.
  This tree has 2429 uses of `compare` and no string `!` — I wrote the wrong one
  anyway, and the selftest caught it as a stack underflow rather than a wrong
  answer.

- 2026-08-20 — **C4 Pest Control — the eligibility rule this tree shipped was the 2006 one.**

  Ours gated the reward on `zeal >= 50` — deal fifty points of damage over the
  game. The wiki records that rule being **removed**, and says why:

  > "Originally, players had to deal at least 50 points of damage or repair 10
  > barricades in the game in order to receive commendations. This is no longer
  > the case as of an update, due to player complaints about participants simply
  > meeting the minimum requirement and not participating afterwards. To counter
  > this, the activity bar was added."

  A threshold pays for the first thirty seconds of a twenty-minute game; a
  draining bar pays for all of it. They are opposite designs, not two spellings
  of one, and the tree had the one the game deleted.

  Four numbers from NR's implementation, none of them guessable:

  * **The bar is 52 wide, not 100** (varbit 5662, `FULL_ACTIVITY_PERCENTAGE_VALUE
    = 52`). Reading it as a percentage is off by roughly half everywhere.
  * **A player starts on half a bar** — something to lose from tick one and
    something to gain from the first hit.
  * **Portals and spinners pay 10, everything else 5.** They are what wins the
    game, so they are what the bar pays for; a flat rate makes farming endless
    trash spawns as good as pushing a portal.
  * **It drains one unit every third tick, and not for the first thirty.** Two
    gates, not one — walking off the lander is not idling.

  **And the rule that is the entire point of the feature:** once the bar reaches
  zero it can never refill. `incrementActivity` returns early on `activity <= 0`.
  An implementation that lets a player climb back has the anti-AFK measure doing
  precisely nothing — idle, hit something, idle again — which is the behaviour
  the update existed to stop.

  Also corrected against the wiki, all three of which our tree or NR had wrong:

  * **The reward table is 3/4/5 base with one more per Combat Achievement tier,
    to a maximum of three** (Novice 3-6, Intermediate 4-7, Veteran 5-8). Ours
    used 2009scape's `ordinal + 2` = 2/3/4; NR uses a flat 6/8/10, which is
    above the wiki's own top row at every lander.
  * **The point cap is 4000**, not the 500 this tree invented.
  * **Barricade repair** was entirely absent though the damage half was there.
    Hammer **and** log — the log is consumed; it repairs to **full** from either
    damaged state, not one stage back; it counts as **five points of damage**;
    and the tick cost is the equipped weapon's attack speed, which is why the
    strategy pages tell people to bring darts.
  * **Elite void** was explicitly deferred in the shop's header comment. 200
    points **per piece** (400 is the pair, and charging 400 for one makes the
    second unreachable), the regular piece is **consumed**, and the gate is the
    **hard** Western Provinces diary — not the elite tier above it, which would
    put the reward two tiers out of reach of the diary that unlocks it.

  One structural change worth noting: `~pest_elite_void_upgrade` returns a
  status and a separate `~pest_elite_void_exchange` speaks. A proc that both
  charges the player and opens a chat can only be exercised by a human standing
  in front of the Void Knight; splitting them is what let the exchange be
  asserted at all. The selftest first failed at 16 of 18 for exactly that reason.

  A shared `%combat_achievement_tier` varp now lives in `player/configs/` rather
  than each reward table inventing a private one — Pest Control and the
  Warriors' Guild both read it. This tree still has no Combat Achievement system
  to write it; the seam exists so that the day one lands, every table that
  scales on it changes in one place.

  Final: 28283 scripts; both contracts green; 44 owned assertions green.

- 2026-08-20 — **C6 Duel Arena — the thing NR implements no longer exists.**

  The Duel Arena was **removed on 6 July 2022** and replaced by the Emir's
  Arena. What survives is the "legacy duel", which the wiki describes as working
  "like the Duel Arena used to, **bar the staking aspect**." The cache agrees
  and is the decisive evidence: it ships `pvp_arena_legacyduel_options`,
  `pvp_arena_legacyduel_confirm` and `pvp_arena_unrankedduel`, and **no staking
  interface at all**. Porting NR's Duel Arena verbatim — it has a whole
  `Tax.java`, a `DuelStakingInterface`, a `DuelContainer` — would have built a
  staking screen this client cannot draw. Two of NR's twenty-two classes are for
  a feature that was deleted from the game.

  **The cache handed over the varp layout for free.** `dueloptions` is varp 286,
  and `duelwornoptions` declares `startbit=14, endbit=27` — fourteen bits, one
  per wear position. That single declaration fixes the rule field at bits 0-13
  beneath it, so the two halves can never collide. The test asserts exactly
  that: setting rule bit 0 must leave every worn slot alone, and disabling worn
  slot 0 must not read back as "No Ranged". An off-by-fourteen there disables
  the player's helmet when they tick a rule.

  A detail that matches one already in this tree's notes: the cache declares a
  worn varbit for **all fourteen** positions, but the interface draws only
  `duel_wornoption0,1,2,3,4,5,7,9,10,12,13` — it skips 6, 8 and 11 (arms, hair,
  jaw), which are body layers rather than equipment. **A UI column index is not
  a wear position**, the same trap `cluehelper_outfit` sets.

  Three rules with a plausible wrong reading:

  * **Fun Weapons is "negative attack stats", not a list of joke items** — the
    wiki says "such as the Rubber chicken, Flowers, Birthday cake..." and stops,
    because the set is defined by the stat. And "**Bare fists/feet are not
    allowed**" even though a bare fist is not a weapon: the obvious
    implementation, reject anything that is not a fun weapon, permits the empty
    hand by accident.
  * **No Drinks RESETS boosted stats when the fight begins.** Blocking the drink
    alone lets a player chug a super set in the lobby and walk in boosted, which
    is exactly what the option exists to prevent — the reset is the enforcement
    and the block is only the follow-through. This cost me a failing test:
    `stat_heal(s, 0, 0)` restores a *drained* stat up to base and does nothing
    to a boosted one. Removing a boost is `stat_sub` of the excess, guarded so a
    duellist who walks in already drained is not drained further.
  * **Boxing is Whip plus exactly one slot**, the right hand — "no weapons or
    armour" against "any one-handed weapon, no armour". Both are expressed
    purely as worn-slot disables, which is why the worn half of the varp exists.

  Also pinned: legacy win/loss counts are stored separately and move neither
  rank nor reward points, and the "impossible combination" rule generalises past
  the wiki's single example — Fun Weapons plus No Melee is the same
  impossibility spelled differently, since the only weapons you may hold are
  melee ones.

  Final: 28297 scripts; 45 owned assertions green.

- 2026-08-20 — **C7 Prifddinas — clan crystals, and an engine defect they exposed.**

  Most of what NR's `elven/` tree holds this tree already has: crystal
  equipment, the singing bowl, the recipes and the charge system all landed with
  the Gauntlet, and Zalcano is B7. What was missing is the **clan-crystal
  livery** — the eight clans, and the recolour a clan crystal puts on crystal
  equipment.

  **Generated, not typed.** Every recolour is its own obj named `<base>_<clan>`,
  so `tools/gen_clan_crystals.py` walks `all.obj.compack` and emits the
  (base, clan) -> obj table. 72 rows, no obj name typed into the tree, wired
  into `mock230-scripts` as `check-clan-crystals`.

  **The asymmetry is the feature, and it is why the generator exists.** Eight
  clans have a crown and a clan crystal — that half is symmetric. But only
  **seven** dress crystal armour and only **seven** dress crystal weapons, and
  they are not the same seven: **Meilyr has no armour variants and Hefin has no
  weapon variants.** An 8x8 table typed by hand names two rows of objs the cache
  does not contain. Worse, a table built by string substitution at runtime
  produces two silently broken items instead of a refusal. The test asserts both
  gaps independently, and asserts Hefin *does* dress armour — so no single
  "incomplete clan" flag can stand in for both.

  **Engine defect found by writing that test.** The slice's first run failed at
  1 of 10: `~clan_has_armour(meilyr)` came back **true**. `default=null` on an
  enum resolved through `atoi("null")` — **zero** — for every typed output
  except coord, which had its own branch. And 0 is a real obj, a real npc and a
  real loc. So a lookup that missed returned *item 0* rather than nothing, the
  natural `= null` test never fired, and no caller could tell "this key has no
  entry" from "this key maps to item 0". Exactly the failure the dbrow decoder
  had with an unset namedobj column. Fixed in
  `mock230_content.c` — `default=null` now yields -1 on every typed output, not
  only coords. **This was silently wrong tree-wide, not just here**; every enum
  in the tree with a `default=null` and a typed output was answering 0.

  Left **partial**: Prifddinas' NPC dialogues and the elven crystal chest remain.

  Final: both contracts and the new generator check green; 46 owned assertions.

- 2026-08-20 — **C8 Konar quo Maten — a published formula and a location lock.**

  Two things make Konar different from every other Slayer master, and both were
  absent although her master id already existed.

  **The brimstone key curve is a Jagex statement, not a wiki estimate.** Mod
  Kieren published it on 16 January 2019 and the wiki quotes the tweet:

  ```
  L >= 100:  1 / (120 - floor(L / 5))
  L <  100:  1 / (100 + floor((100 - L)^2 / 5))
  ```

  **The two branches meet exactly at combat level 100** — 120 - 20 = 100, and
  100 + 0 = 100 — so an off-by-one at the boundary is a visible step in a curve
  Jagex published a graph of. The test pins the join, the bend (95 -> 105), the
  weak end (50 -> 600, six times rarer than a level-100 monster), the cap
  ("Creatures over this will cap at 1/50", so 350 and 1000 both give 50), and
  the linear branch's slope (200 -> 80).

  And Mod Ash's rider, 8 June 2020: a monster with a Slayer level requirement
  gets **"a 20% boost in the drop probability"**. A boost to the *probability*
  is 5/6 on the denominator — 350 goes 50 -> 41. Subtracting 20 instead would
  give 30, and worse, would be a boost that grows as the monster gets weaker:
  the test asserts a level-50 monster moves 600 -> 500, a change of 100, which
  subtraction cannot produce.

  **The location lock, generated.** "Unlike other Slayer masters, Konar's Slayer
  tasks also include a specific location where players must complete the entire
  task" — a kill elsewhere gives no credit, which is why she pays more points
  than Duradel despite being the lower master. `tools/gen_konar_locations.py`
  extracts the per-task location lists from the pinned wiki: **38 tasks, 112
  location entries**, widest 7. Nothing typed. The generator refuses a table
  wider than its 8-slot stride rather than let task N's fifth location be read
  as task N+1's first — and the test asserts exactly that non-collision.

  [wiki, Mod Ash] "Yes, it's an equal chance for each of the areas you're
  eligible to access." **Uniform over the eligible subset** — a location behind
  an unfinished quest is removed from the draw, not rolled and re-rolled. Those
  are different distributions, and the test drives twenty rolls with a single
  eligible location to prove the roll never escapes it, then asserts that no
  eligible location answers -1 rather than sending the player somewhere they
  cannot go.

  One spelling trap: the cache's unpacker writes string enums as
  `outputstring`/`defaultstr`/`valstr`, but an **authored** `.enum` uses
  `outputtype=string` with plain `val=`/`default=`. The generator emitted the
  dump spelling first and every row was rejected as an unknown key — which
  showed up not as a parse error but as the selftest failing at 7 of 14 with a
  location count of zero.

  Left **partial**: binding the location NAMES to area bounds so the gate can be
  evaluated from the player's position.

- 2026-08-20 — **C9 Revenant Caves — four modifiers, three directions.**

  The wilderness weapons, ether and the charge system were already here. What
  was missing is what actually governs the caves: how a drop rate is arrived at.
  All revenants share one list, and the wiki names four modifiers that stack in
  three different directions.

  * **The x5 needs a REVENANT task, not a task revenants satisfy.** "This
    boosted rate is **not applied** if the player is assigned ghosts as a Slayer
    task." Revenants *are* ghosts, so a ghost task legitimately sends a player
    into the same caves and pays nothing extra — which is the whole reason the
    exclusion is written down. "Am I on a task these count for" is true in both
    cases and is the wrong question.
  * **The skull's SOURCE matters.** "the skull obtained by playing on a
    high-risk world does not provide a boost". A plain `is skulled` test hands
    high-risk players a bonus the game denies them.
  * **The two boosts STACK.** "The boost for being skulled stacks with the
    increased drop rate of Wilderness weapons while on a revenants Slayer task."
    The test pins all three cells — task-only 200, skull-only 689, both 137 —
    so taking the better of the two is caught rather than passing on two of
    three.
  * **And then the skull goes the other way, twice.** It makes the ancient
    emblem "nearly 3.5 times" RARER, and it **removes the dragon med helm from
    the table entirely** — removed, not made rarer. A single "skulled is better"
    boolean gets the emblem 45% commoner instead of 3.5x rarer, a factor-of-five
    error in the wrong direction, and leaves the med helm droppable.

  **The amulet of avarice is not the cape of skulls, and the wiki says so
  itself**: "The skull cannot be removed until the amulet is unequipped, after
  which it behaves like a normal PK skull and disappears after twenty minutes.
  **This behaviour is different from the cape of skulls**, which will only keep
  the skull active for 20 minutes from the moment of equipping." The page draws
  that contrast because the two are the obvious same implementation and are not:
  the cape starts a timer, the amulet *suppresses* one. An amulet that started a
  twenty-minute timer on equip would leave a player unskulled while still
  wearing it — precisely the loophole it exists to close. The test asserts there
  is no timer at all while worn, and that the clock starts on removal.

  Also pinned: the amulet's 20% "does not stack with the Slayer helmet (i)", and
  Forinthry Surge needs **both** the amulet and top damage on the maledictus.

  Left **partial**: the drop table itself and the Revenant maledictus fight.

- 2026-08-20 — **C10 Barrows — the number the whole activity is built around.**

  The crypt, the tunnels and the four puzzle doors were already here. Reward
  potential was not — and it is what every reward in the activity keys off.

  **The 1,000 cap is on the kill sum; the brothers' points sit above it.** "the
  combat level of the monster that was slain, capped for a total of 1,000. For
  every Barrows brother that was slain, the player will receive an extra 2
  points ... capping the maximum reward potential at 1,012." A single cap of
  1,000 on the total can never reach the 1,012 the wiki names — and 1,012 is the
  divisor the in-game percentage uses, so getting the cap wrong also puts the
  readout out by 1.2% at the top.

  **A brother pays twice, and the wiki's worked examples are what prove it.**
  "6 brothers, 1 giant crypt spider, 1 skeleton, and 1 crypt spider (880
  points)" only reaches 880 if a brother contributes BOTH his combat level to
  the capped sum AND the extra 2: 656 for the six of them (98+115+115+98+115+115),
  212 for the tunnel monsters, 12 on top. My first test read a brother as paying
  only the bonus and failed at 1 of 10 with 224. The second example — 3
  bloodworms and a crypt spider — is also exactly 212, so two different kill
  sets land on one number, which is the check that the example was read rather
  than fitted to.

  **880 shows as 86.95%**, which is 880/1012 — so the readout divides by 1,012
  and not by 1,000, which would print 88.0%. Asserted in tenths, because whole
  percent cannot tell 86.95 from 87.

  **Brothers killed drives two different things and they must not collapse.**
  It sets how MANY rolls the chest makes — "brothers + 1", which is the
  `rolls=7` the reward table is annotated with, not six — and it sets WHICH
  items those rolls can land on, because "players will not be able to receive
  equipment belonging to brothers they did not kill". A five-brother run is six
  rolls over **twenty** pieces, not six rolls over twenty-four.

  And the eligibility gate is "at least one enemy of **any kind**" — a crypt rat
  qualifies and no brother is needed, so gating on brothers locks out a
  legitimate if poor run.

  One thing flagged as **[ours] rather than [wiki]**: the wiki gives the
  equipment rate only at the endpoint (1/2448 per piece over seven rolls at six
  brothers and full potential, so 24/2448 = 1/102 per roll) and points at its
  own calculator for anything else. Scaling that endpoint linearly in potential
  is this tree's inference, marked as such in the source. What is *not* an
  inference is that potential drives it and that zero potential must not divide
  by zero.

  Left **partial**: the full reward table and the wight npcs.

- 2026-08-20 — **C11 Pyramid Plunder — swept, nothing to change.**

  The row was queued because ours was rebuilt from 2009scape and 2009scape's
  numbers are routinely stale. Walking the wiki's Changes section entry by entry
  against the implementation, every one is already handled, and the two that
  matter most are handled with the reason written down:

  * **24 March 2021** — "Pharaoh's Sceptre now has scaling drop rates depending
    on the room its chest/sarcophagus is located." Ours carries the per-room
    table (4200/2800/1600/950/800/750/650) with a comment naming the 2009scape
    numbers it replaced.
  * **30 August 2023** — chests and sarcophagi roll the sceptre even on a failed
    open. Ours rolls on the give-up path too, with a note that the failure path
    "is the one nobody plays" and so is the half that gets missed.
  * The hourglass (removed 12 April 2018) is correctly absent; quick-leave
    (14 August 2014) is present and cited.

  Six selftest procs were already wired and are green. Marked **done** without
  changes — a sweep that finds nothing is still the slice being closed, and the
  alternative was inventing work to have something to show.

- 2026-08-20 — **C12 Party Room — the balloons were here, the party was not.**

  Party Pete and the six balloon colours were already bound; what they had
  nothing to do with was a drop party. The chest, the lever and the announcement
  were all stubs — `partyroom_chest.rs2` was fifteen lines and its Deposit
  option printed "The drop chest isn't taking deposits yet."

  **The cache settled the wiki's one ambiguity.** "216 individual items can be
  put in before the chest is full" could be 216 units or 216 slots — and
  `partyroom_dropinv` is declared **size=216**, so it is slots, and a stack of
  ten thousand coins is one of them. `partyroom_tempinv` at size=8 is the
  staging tray. Two numbers the prose could not have decided.

  **The members' announcement threshold is the HIGHER one** — 76,000 against
  free-to-play's 50,000. That is the opposite of the usual direction: members'
  worlds have more valuable items in circulation so the bar rises, and a port
  that assumes members get the easier number announces parties the game would
  not. The test asserts the ordering as well as both values, so a swap is caught
  even if both numbers are present.

  **Two prices on ONE lever** — 1,000 coins drops the balloons, 500 summons the
  dancing knights, and they are options on the same pull rather than two levers
  or one price. The test proves they are separate by showing 999 coins is not
  enough to pull but *is* enough for the knights.

  Also pinned: donations **cannot be withdrawn** ("a deposit interface with a
  withdraw button is not a party room, it is a bank"); the announcements are
  **overhead only, not chatbox**, so a `mes` puts them in the one place the wiki
  says they do not go; and the two phases have different shouts, before the
  lever and after.

  **Correction to my own first reading of this slice.** I recorded the shared
  chest as blocked on a missing engine feature — "this tree has world-shared
  variables but no world-shared item container". That was wrong, and reading
  `mock230_container.c` rather than assuming showed why:
  `mock230_container_scope` already answers `MOCK230_CONTAINER_WORLD` for any
  inv whose `.inv` declares `scope=shared`, and `srv->world_containers` already
  exists to hold them. The path arrived with shops, but nothing on it is
  shop-specific — a shared inv with no `stock=` rows seeds with nothing and
  works. Declaring `[partyroom_dropinv] scope=shared` was the entire change; no
  engine edit was needed. The chest is now one real 216-slot container that
  every player resolves to, asserted by depositing forty thousand coins into it
  and popping them back out.

  That also makes the cache's `size=216` visible in behaviour rather than only
  in a comment: forty thousand coins occupy **one** slot, which is the reading
  the prose could not decide.

  Left **partial**: only the deposit interface remains.

- 2026-08-20 — **C13 Wilderness events — the first slice where the source is the specification.**

  This is Wave E's shape arriving early, and it is worth naming before Wave E
  proper. The hot zone, the Rogues' Castle chest and the Ganodermic Beast are
  **Near-Reality's own content, not the game's**. The wiki is silent because
  OSRS has none of them. So the precedence ladder starts at rank 4 and NR's
  behaviour *is* the specification — there is no higher rung to check it
  against, and "port it faithfully" means port it exactly.

  Except where it cannot be. **The reward table names items this cache does not
  have**: NR pays in `ItemId.BLOOD_MONEY` and `CustomItemId.PVP_MYSTERY_BOX`,
  and `blood_money` does not appear in `all.obj.compack` at all. Porting the
  table verbatim would name objs that cannot be rendered. The amounts and the
  ranking are ported exactly; **which obj pays them is one constant,
  `^wildy_event_reward_obj`**, so substituting is a decision made once and on
  purpose rather than a name invented in passing. That is flagged
  `[nr, substituted]` in the source.

  Three mechanics with a plausible wrong reading:

  * **The two bands overlap at exactly 30** — `1..30` and `30..60`, so level 30
    is in both. That is not a typo to tidy into disjoint ranges: NR's own multi
    test is `wildernessLevelRange.first >= 30`, which asks the BAND and not the
    level, so a level-30 kill counts for whichever band is running and is paid
    at that band's rate.
  * **Three INDEPENDENT leaderboards**, not one score. Kills, skilling
    experience and monster damage each pay their own top three, all three are
    awarded unconditionally, and one player can win all three. A combined score
    pays three prizes where NR pays nine. The test also pins that experience and
    damage pay identically in both bands — only the kills board's tiers move —
    so a band-wide multiplier is caught too.
  * **Exactly one cell in the table pays an ITEM rather than a sum**: first
    place for player kills while the multi band is hot, which is a mystery box.
    Its coin amount is zero *because* the item is the prize, and the test
    asserts both halves so a port cannot quietly pay 5,000 there as well.

  Left **partial** for a reason that is not effort: **the Ganodermic Beast is an
  RS3 monster with a `CustomNpcId`**, and this cache has no npc, loc or obj
  matching it. Implementing it means allocating an id and choosing a substitute
  model — a visible, permanent artefact that a wrong choice makes worse than an
  absence. Everything else in the slice is in.

- 2026-08-20 — **C16 Chompy hats — the cache hid them behind opaque names.**

  The quest, the ogre bow, the toads and Rantz's twenty-two-rung rank ladder
  were all here. The hats were not, and they are the reason anybody hunts
  chompies after the quest ends.

  **They were hard to find because the cache names carry no rank word and not
  even the word "chompy":** `cbhat1`..`cbhat18`. Grepping `all.obj.compack` for
  "chompy" returns four objs, none of them a hat; grepping for "bowman",
  "yeoman", "forester" returns a lumberjack hat and a jungle axe. They only
  surface by searching the `name=` fields of `all.obj` for "Chompy bird hat",
  which finds eighteen blocks whose symbols are `cbhat*`.

  **The hat ladder is not the rank ladder, and that is the trap.** Rantz names
  **twenty-two** ranks starting at Ogre Novice; there are **eighteen** hats and
  the first is at 30 kills. The bottom four rungs — Ogre Novice, Beginner, Ogre
  Learner, Learner — have no hat at all. One hat per rank invents four that do
  not exist. The two ladders line up from 30 upward and only from there, which
  the test pins by asserting that 29 kills has earned no hat while the rank at
  30 is "Ogre Bowman" and the first hat is the ogre bowman's.

  The count is cross-checked from both directions: `tools/gen_chompy_hats.py`
  reads eighteen thresholds out of the wiki table and **refuses to write a table
  that is not eighteen rows**, because eighteen is what the cache ships. Neither
  side could be checked by reading the other alone — the cache names have no
  ranks in them and the wiki has no obj ids.

  Also pinned: "chompy hats can be dropped and re-obtained later from Rantz", so
  the claim is idempotent and counts a worn hat as held; and a player who never
  claimed is owed **every** hat they earned rather than only the top one — at
  4,000 kills with an empty pack that is all eighteen.

- 2026-08-20 — **C17 Motherlode Mine — written, and NOT verified. Read this before trusting it.**

  The veins, hopper, strut and sack search were already here; the economy on top
  of them was not, and nuggets are what the whole activity is for. Four things
  pinned, each with a plausible wrong reading:

  * **The nugget rate is flat.** "each pay-dirt having a 3.13% chance of being a
    nugget, **regardless of a player's Mining level**" — one in thirty-two.
    Everything *else* about cleaning pay-dirt scales with Mining (the ore table
    and the bonus experience both do), so a nugget rate that scales looks
    consistent with its neighbours and is wrong.
  * **The 3-per-batch cap DELETES the excess.** "seasonal events with boosted
    rates can cause golden nuggets to be **deleted**." It does not hold them
    back for the next batch — which is why the wiki's own workaround is to
    deposit half a load at a time, advice that would be pointless if the cap
    merely deferred.
  * **The upgraded sack is not double.** It blocks at 108, or **189** upgraded —
    and 189 is 108 + 81, where 81 is what the sack used to hold. Doubling gives
    216 and is wrong by twenty-seven loads. The test asserts the difference is
    81 *and* that the upgraded figure is not twice the base.
  * **The upper level is TWO purchases.** 100 nuggets and base 57 Mining (not
    boostable, so `stat_base`) open the ladder; the hopper up there is a
    separate **50**. Paying the 100 leaves a player walking back down to the
    free hopper, and the test asserts the first purchase does not grant the
    second.

  **Status is "written, unverified", deliberately.** The code compiles — it
  built clean at 28,426 scripts — but the runtime selftest has never run against
  it. The shared tree has been broken by another lane throughout
  (`quest_arthur/scripts/thrantax_altar.rs2:85`, an unknown type subject on an
  `ai_queue1` trigger), and `mock230` refuses to run on a stale script pack, so
  every attempt aborted before reaching the stanza. I polled for about twenty
  minutes across three waits. The assertion is wired into
  `mock230_world.c` and `trail_selftest_check.sh` and will run the moment the
  tree compiles; until it does, **this slice has not met the bar the others
  did** and should not be counted as if it had.

- 2026-08-20 — **C19 Krystilia — generated while the tree was blocked.**

  Written during the same outage that stopped C17 being verified, and chosen for
  that reason: **the deliverable is generated data, and a generator's `--check`
  runs without the script compiler.** So the half of this slice that can be
  proved today has been.

  `tools/gen_krystilia_tasks.py` extracts **37 tasks, total weight 196** from
  the pinned wiki — the base amount, the extended amount, the Slayer experience
  and the task weight per row. None of it typed. Wired in as
  `check-krystilia-tasks`; green.

  Two things the generator refuses to produce, because both are silent when
  wrong:

  * **A row with zero weight.** A task that fell out of the parse would
    otherwise just never be assigned, and nothing would say so.
  * **A table where every extended amount equals its base amount.** The base and
    extended lengths are separate columns and the extended one applies only once
    the player has bought that task's Extend unlock. Collapsing them hands every
    player the extended length for free — a reward the Slayer shop sells. The
    generator asserts the two columns did not collapse; the runtime test asserts
    abyssal demons are 75 base and 200 extended, and that bandits, which have no
    extension, keep their base length rather than falling to zero.

  Also pinned: **"I Wildy More Slayer" is free** — the one Rewards Shop entry
  beside it that costs nothing. Pricing it locks four of the thirty-seven tasks
  (abyssal demons, dust devils, jellies, nechryaels) behind points they were
  never meant to cost.

  **Status "written, unverified" for the same reason as C17**: the runtime
  stanza is wired but has never executed. See that entry.

- 2026-08-20 — **D2 Agility shortcuts — the data half, done offline.**

  NR's `skills/agility/shortcut/` is the largest file set on its side at 8,242
  lines, and almost all of it is one fact repeated: this obstacle needs level L
  and pays X experience. That is a table.
  `tools/gen_agility_shortcuts.py` extracts **162 shortcuts** from the pinned
  wiki. Wired in as `check-agility-shortcuts`; green.

  Three things the extraction had to get right, and the generator now **refuses
  to write a table where any of them silently collapsed**:

  * **A shortcut can have two routes, and they are ALTERNATIVES.** The Broken
    Raft is Agility 8 with a grapple (plus Strength 19 and Ranged 37) **or**
    Agility 48 barehanded. Reading the first number as "the" level locks a
    barehanded player out until 48 and lets a grappler through at 8 carrying no
    grapple. Six shortcuts have a second route; the generator fails if none
    does, because that means the column collapsed.
  * **Experience is not always one number.** "3 (1 on failure)" is two values
    and the shortcut pays either. Eight shortcuts have a failure award; taking
    the first integer drops it, taking the last drops the success.
  * **Fractional experience is real and small.** The Falador crumbling wall pays
    **0.5**. Stored in tenths, because an integer column rounds it to nothing —
    and the generator fails if no fractional value survives. Nineteen rows have
    one.

  Also: **level 1 is a real requirement** (the Lumbridge stepping stones), so
  "no requirement" cannot be spelled 0 and then tested with `> 0`. The generator
  rejects any row that parsed to level 0.

  Marked **data done, scripts pending**: the per-obstacle bindings are the other
  half and need the compiler, which is down (see C17).

- 2026-08-20 — **D8 Slayer — a generator written, run, and then deleted.**

  Worth recording as a near-miss rather than quietly dropping.

  `slayer_master_task.dbtable` is declared in this tree and no `.dbrow` file
  fills it. I read that as "no master can assign anything", pinned all nine
  master pages (Turael, Spria, Mazchna, Vannaka, Chaeldar, Nieve, Duradel,
  Konar, Krystilia), wrote a generator, and extracted **329 rows across nine
  masters** with amounts and weights.

  Then I read the dbtable's own header comment, which says: *"Rows stay in the
  dat2 cache (`mock230_db_load_cache`); this file names the columns so
  ServerScript can write `slayer_master_task:master_id` etc."* The table is
  **cache-backed**. Boot confirms it — "db tables loaded (328 tables, **21912
  rows** from cache.osrs239)". The rows have been there all along; the schema
  overlay exists precisely so `~slayer_assign`'s `db_find` can read them.

  My 329 rows would have been duplicates under wrong column names — the cache's
  schema is `master_id, task(dbrow), weight, min_amount, max_amount, areas,
  task_unlock`, and I had generated `task_name, amount_low, amount_high`.
  Generator and output both deleted.

  **The lesson is the one this queue keeps relearning**: check what the cache
  already ships before authoring it. It cost ToA a whole row on this ledger
  (C2, marked pending on a line count while 8,846 lines and 54 checks already
  existed), and it nearly cost D8 three hundred wrong rows. An empty authored
  file is not evidence of missing data when the table is cache-backed.

  The nine master pages stay pinned under `docs/skills/slayer/sources/` — they
  are the reference for D8's real remaining work (unlocks, extend costs,
  dialogue), which is not the task tables.

- 2026-08-20 — **A guardrail for the trap that keeps catching this queue.**

  Twice now a slice has been misjudged by reading an empty authored file as
  missing data: C2 (ToA, marked pending on a line count while 8,846 lines and 54
  runtime checks already existed) and D8 (329 duplicate rows generated for a
  table the cache already fills). Both cost real time. So the check is now a
  tool rather than a lesson: **`tools/check_dbtable_rows.py`**, wired into
  `mock230-scripts` as `check-dbtable-rows`.

  A `.dbtable` in this tree is a **schema overlay** — it names columns so
  ServerScript can address them. Whether the ROWS are authored here or shipped
  in the dat2 cache is a separate question the file does not have to answer. So
  an overlay with no matching `.dbrow` means one of two very different things,
  and the tool says which:

  ```
  82 declared table(s), 79 with authored rows
    cache-backed, no authored rows needed (3): quest slayer_master_task slayer_task
    no rows anywhere, unexplained (0): -
    no rows anywhere, known and explained (1): legends_gem_data
  ```

  All three cache-backed tables are Slayer/quest infrastructure — exactly where
  I went wrong. `legends_gem_data` is the one genuine empty, and it is a
  *documented* soft-skip: a previous session recorded that no carved-rock locs
  exist in `all.loc`, so the per-rock puzzle cannot be built yet. It is
  whitelisted **with that reason in the source**, so a new orphan appearing is
  the finding rather than being lost in a list of known ones.

  One implementation note worth keeping: **the table name is the `[section]`
  header inside the file, not the filename.** `gem.dbtable` declares
  `gem_cutting_table`. My first pass keyed on the filename and reported 47 false
  positives — a checker that cries wolf 47 times is worse than no checker.

- 2026-08-20 — **D5/D6 Lunar and Arceuus — the data half, from 113 pinned pages.**

  Chosen as offline work while the shared tree stays unbuildable. The spellbook
  ARTICLES were useless for this: they transclude a navbox that lists spell
  names and nothing else. The data lives on each spell's own page, in an
  `{{Infobox Spell}}` carrying level, spellbook, experience and rune cost. So
  the pin is **113 individual pages** under
  `docs/skills/magic/sources/spells/`, and
  `tools/gen_spellbook_tables.py` reads all of them: **111 spells — 44 Lunar,
  67 Arceuus**. (Two of the 113 belong to other books; the infobox's own
  `spellbook` field decides, not the navbox that led there — a few spells appear
  in more than one book's navigation.)

  Three things the generator now **refuses to emit a table without**, because
  each is silent when flattened:

  * **A rune cost is a SET.** `{{RuneReq|Astral=2|Cosmic=2|Law=1}}` is three
    runes at three counts; 109 of the 111 need more than one. A spell recorded
    as "five runes" cannot be cast at all, so the generator fails if no spell
    has more than one rune. The key packs four runes per spell and fails if any
    spell needs a fifth rather than silently truncating.
  * **Experience can be fractional.** Four spells award a fraction; stored in
    tenths, and the generator fails if none survives.
  * **A whole book going missing** — it fails if either book is empty, which is
    what a changed page title or a redirect would look like.

  **Extended the same day to all four books: 220 spells — 84 Normal, 44 Lunar,
  67 Arceuus, 25 Ancient**, from 224 pinned pages. That covers D4's teleports
  (every one carries its level, xp and rune set) and the spell half of D7.

  Extending it surfaced the naming trap the generator now documents: **the
  standard spellbook's pages say `spellbook = Normal`, not "Standard"** — the
  value the infobox uses is not the name the article uses. My first run reported
  "0 Standard" and the guard caught it, which is exactly what that guard is for.
  Two further spells declare `all`/`All` (castable from every book) and are
  excluded deliberately rather than filed under one.

  Wired as `check-spellbooks`. **Data done, scripts pending**: the per-spell
  cast handlers need the compiler.

- 2026-08-20 — **D1 rooftop courses — verified rather than written, and the checker was wrong twice first.**

  All nine rooftop courses were already in the tree. Rather than assume the
  numbers, `tools/check_agility_course_contract.py` now sums each course's
  Agility awards and holds them to the pinned wiki, in TENTHS because rooftop
  obstacles pay fractions (Al Kharid's rope swing is 8.5). Wired as
  `check-agility-courses`; **all nine agree.**

  It took two wrong answers to get there, and both are worth recording because
  a checker that is wrong about the tree manufactures work:

  * **First run: seven of nine "short".** Not every award is a bare
    `stat_advance` — `~agility_force_move` takes the experience as its FIRST
    argument and advances the stat itself. Counting only `stat_advance` missed
    most of every course. I nearly filed seven false defects.
  * **Second run: Rellekka and Pollnivneach still short, by 140 and 126.** Those
    two courses pay MORE on the final obstacle once the matching Achievement
    Diary is done, and the wiki writes it as `|475 (without diary)` followed by
    `{{+=|xp|615}} (with diary)` — **only the diary-boosted figure is inside the
    template**, so a sweep of `{{+=|xp|...}}` reads the boosted number as the
    base. Our 475 was right all along.

  The checker now compares against the base and *reports* which courses have a
  diary-boosted obstacle, because that boost is the one genuine gap: this tree
  awards the base on both courses and has no diary branch on either. That is
  real remaining work, correctly scoped and now visible on every run instead of
  being buried.

  It also flags that Varrock's page is **self-inconsistent** — its obstacle
  table sums to 269.7 while its prose says 270 — so neither figure is quite the
  reference. Ours matches the table.

- 2026-08-20 — **Measured the untouched slices instead of guessing at them.**

  Three slices this session were queued as unimplemented and turned out to be
  substantially or entirely done (C2 ToA, D8's task tables, D1's nine rooftop
  courses). That is a third of what I touched, so before assuming the rest of
  the queue is real work I measured it.

  **A crude keyword sweep is worthless here and I nearly trusted one.** Grepping
  the tree for "vorkath", "flower", "easter" and so on reported 8,945 lines for
  Vorkath and 14,024 for flower poker — because it counted every file that
  mentions the word anywhere, including farming's flower patches and a dozen
  drop tables. Measuring the actual owning directories gives a very different
  and much smaller picture:

  | Slice | ours (measured) | NR |
  | --- | --- | --- |
  | B2 Vorkath | **158** | 1264 |
  | B11 Skotizo | **44** | 861 |
  | B5 Mage Arena II | **168** | 1031 |
  | B12 Corporeal Beast | **0** | 1284 |
  | B10 Rise of the Six | **0** | 895 |
  | B7 Xamphur | **0** | 1031 |
  | B14 Tormented Demons | 1698 | — |
  | B15 TzHaar (Inferno + Fight Cave) | 5351 | — |

  So Vorkath, Skotizo and Mage Arena II are **stubs**, not absences — a hundred
  and a half lines each where NR has eight to twelve times that. Corporeal
  Beast, Rise of the Six and Xamphur have **no files at all**. Tormented Demons
  and the TzHaar caves are genuinely substantial and match this tree's own
  notes.

  **All six of them are in the cache, under names nothing like their own.**
  My first check grepped the symbol index for `corporeal_beast`, `skotizo`,
  `xamphur` and found nothing — which reads as "this cache cannot render them"
  and is wrong. Searching the `name=` fields instead:

  | Called | Cache symbol |
  | --- | --- |
  | Corporeal Beast | `corp_beast` |
  | Skotizo | `cata_boss` |
  | Xamphur | `akd_xamphur_combat` |
  | Derwen (RotS) | `ma2_boss_guthix` |
  | Justiciar Zachariah (RotS) | `ma2_boss_saradomin` |
  | Revenant maledictus | `wild_cave_superior` |
  | Elite Void Knight | `pest_voidknight_elite` |
  | Prospector Percy | `motherlode_percy` |

  So none of these six needs authored assets — they need scripts. That is a
  materially easier slice than "build a boss from nothing", and I had it
  recorded the other way round an hour ago.

  The lesson generalises past this queue: **"how many lines mention X" and "how
  much of X is implemented" are different questions**, and the first one
  flatters the answer. Every "ours" figure in the tables above was originally a
  line-count estimate of exactly that kind, which is why three of them were
  wrong by an order of magnitude in one direction and these six are wrong in the
  other.

- 2026-08-20 — **`tools/cache_find.py` — search the display name, not the symbol.**

  Three times in one session I concluded a cache lacked something because its
  symbol did not contain the obvious word, and twice that conclusion reached
  this ledger before I checked the `name=` fields. The chompy hats are
  `cbhat1..18`; the Corporeal Beast is `corp_beast`; Skotizo is `cata_boss`;
  Xamphur is `akd_xamphur_combat`; the Revenant maledictus is
  `wild_cave_superior`.

  So the check is now a tool. `tools/cache_find.py <kind> "<display name>"`
  searches the `name=` field across `all.npc`, `all.obj`, `all.loc` and the
  rest, and when it finds nothing it says so in a way that does not read as
  proof of absence.

  It is worth being precise about what this does and does not settle: a display
  name that is absent still is not proof — the Ganodermic Beast really is absent
  (it is an RS3 monster), and that only becomes trustworthy because the same
  search finds everything else that was thought missing. **A negative from a
  symbol grep is worthless; a negative from a name search is evidence.**

- 2026-08-20 — **Six boss cache records confirmed against the wiki, one divergence found.**

  Having established the cache symbols by display name, the next question is
  whether they are the RIGHT records — a symbol that merely *contains* a name is
  not proof, and picking the wrong record is the mistake hardest to see
  afterwards. So `check_boss_contract.py` was extended from ten bosses to
  seventeen, and taught to run its **cache half** even where no authored config
  exists yet: a record whose cache hitpoints match the wiki's is a boss waiting
  for a script; one whose hitpoints disagree is a boss whose symbol was guessed
  wrong. Very different news, and previously both would have printed as
  "missing".

  Six confirmed outright:

  ```
  akd_xamphur_combat  (Xamphur, 450 hp)
  cata_boss           (Skotizo, 450 hp)
  corp_beast          (Corporeal Beast, 2000 hp)
  ma2_boss_guthix     (Derwen, 320 hp)
  ma2_boss_saradomin  (Justiciar Zachariah, 320 hp)
  vorkath             (Vorkath, 750 hp)
  ```

  **And the seventh caught a real divergence.** `wild_cave_superior` is named
  "Revenant maledictus" and carries **1200** hitpoints where the wiki states
  **1250**. The name matches exactly, so this is not a wrong record — this cache
  snapshot predates a hitpoints change. It is deliberately left out of the
  checker **with that reason written where the entry would go**, because listing
  it would make the tool cry wolf about a divergence that is real and expected,
  and silently omitting it would lose the fact. Recheck if the cache is ever
  re-exported.

  That is the check working as intended on its first extended run: six
  confirmations and one genuine finding, from a tool that would have reported
  seven identical "missing" lines an hour ago.

  `check_boss_contract` now covers **17 bosses**.

- 2026-08-20 — **The tree returned, and both held-back slices failed on the first run.**

  The other lane applied the `[ai_queue1,thrantax]` fix and the compiler came
  back after roughly two and a half hours. C17 and C19 ran for the first time
  and **both failed** — and both failures were real, which is the entire
  argument for having held them at "written, unverified" instead of counting
  them.

  * **Party Room — a sampling bug that would have shipped.** My
    `~partyroom_pop_payout` picked a random slot and retried on empty. With one
    item in a 216-slot chest, 216 random picks miss it about **37%** of the
    time: better than a third of balloons paying nothing from a chest that
    demonstrably is not empty. Rewritten to count the occupied slots, pick the
    Nth of *those*, and walk to it. The test now pops a single item out of a
    full-size chest twenty times running, which the rejected version fails with
    probability ~1.
  * **Motherlode — the test was wrong, not the code.** It failed at 5 of 8
    because the selftest player does not have 57 Mining, so the purchase was
    correctly refused. Fixing it surfaced a second fact worth keeping:
    **`stat_sub` drains the CURRENT level and leaves the base alone**, so once a
    player has base 57 there is no way to take it back — the not-boostable rule
    can only be proved *before* the level is granted. The test now boosts over
    the requirement while the base is still low and asserts both that the ladder
    stays shut and that the nuggets are not spent.

  Both now pass. **C17 and C19 are done**, and C12's payout is fixed.

  Final: 28487 scripts; all owned assertions green.

- 2026-08-20 — **B12 Corporeal Beast — a per-weapon flag masquerading as a category.**

  Started as soon as the compiler came back. The npc is `corp_beast` (2000 hp,
  confirmed against the wiki by `check_boss_contract`), so this needed scripts
  rather than assets.

  **[jagex] Mod Ash, 30 August 2023, is the whole slice:** *"They're both
  classified as spears internally. The Corp actually uses a separate parameter
  called **corpbane** that's applied to individual spears/halberds that have
  been enabled against it."*

  So full damage is a **per-weapon flag, not a weapon category** — and the
  difference is not academic. The **dragon hasta is a spear and is not
  corpbane**; the wiki records it losing full damage on 5 June 2019. An
  implementation that asks "is this a spear" pays full damage on a weapon the
  game halves, and no amount of testing spears will reveal it. So the list is
  generated — `tools/gen_corpbane_weapons.py`, **29 weapons, 15 spears and 11
  polearms** — and the generator **fails if a hasta ever parses into it**, which
  is the canary for the category/flag confusion coming back.

  Three more rules, each with a plausible wrong reading:

  * **The halving needs TWO conditions**: a corpbane weapon *and* stab style. A
    corpbane spear swung on slash is halved like anything else.
  * **Magic deals FULL damage** despite not being a spear — accuracy is the
    separate axis the wiki mentions in the same sentence, and halving magic
    because it is not a spear is the obvious wrong move.
  * **Protect from Magic only reduces by 33% and does not block.** Every other
    Protect prayer in the game zeroes the style it names. Applying the usual
    full block here makes the boss's main attack — "attacks mostly with Magic" —
    harmless. The test asserts 60 becomes 40, and separately that it is not 0.

  Also pinned: the damage cap for corpbane weapons is **100**, not the pre-update
  50, and it applies only on the corpbane path (a halved hit is not capped
  again); and the beast is **immune to poison and venom while its dark energy
  core is not**, so one fight-wide poison rule gets exactly one of the two npcs
  wrong.

  Left **partial**: the dark energy core's leech behaviour and the drop table.

- 2026-08-20 — **Cleaned up after the deleted D8 generator.**

  The abandoned `slayer_master_task` generator had allocated **329 dbrow ids**
  in `pack/dbrow.alloc` before I deleted it, and the allocator kept them
  ("no longer declared; kept, ids are stable"). Correct behaviour on its part —
  ids must not be reused — but they were mine and they were noise, so they are
  removed. Worth remembering: **deleting a generator does not undo its id
  allocations**, and the next run tells you so in a line that is easy to read
  past.

- 2026-08-20 — **B11 Skotizo — the altars are the fight, and they have two of everything.**

  `cata_boss`, 450 hitpoints, confirmed. Skotizo himself is an ordinary demon;
  the encounter is the four wall altars soaking damage.

  **[jagex] Mod Ash, 3 June 2020:** *"If you're wielding arclight, each altar
  reduces damage by 15% up to a maximum of 60%. Otherwise it's 25%, up to a
  maximum of 100%."*

  Two rates and two ceilings, chosen by the weapon — and the consequence is not
  a tuning difference. **Without a demonbane weapon, four live altars reduce
  damage by 100%**: literally zero gets through and the fight cannot start until
  one is broken. With Arclight the same four leave 40%. A port that picks one
  flat rate makes the boss impossible or trivial depending which it picked. The
  test pins both ceilings, both per-altar rates, and the three-altar case where
  the demonbane cap has not yet bitten (45 vs 75) — so a wrong per-altar rate is
  caught even if the cap happens to be right.

  Three more, each from a Jagex quote the wiki carries:

  * **Arclight disables an altar in one hit "regardless of the player's stats
    and gear."** Not big damage — an instant disable that ignores the altar's
    100 hitpoints. Modelling it as a large hit leaves a weak player unable to
    break an altar the game says they always can.
  * **"50 - 129 ticks inclusive, chosen independently for each of the four
    altars."** Inclusive at both ends; the test draws 400 timers and requires
    both 50 and 129 to appear, because an exclusive upper bound never produces
    129 and is the classic off-by-one on the word "inclusive". Independent, too
    — one shared timer would make all four breathe together.
  * **The timer is a CHECK INTERVAL, not a respawn countdown.** Mod Ash: "It's
    running a check every X ticks... If it finds an inactive altar at that time,
    it activates it." An altar already up is not re-woken, and the separate
    one-minute cooldown keeps a freshly-broken altar down even when its timer
    fires.

  Left **partial**: the lair, the dark totem and the drop table.

- 2026-08-20 — **B2 Vorkath — half the fight was missing and it looked fine.**

  This tree already had the six-attack counter and the Zombified Spawn. What it
  did not have is the **second** special or the alternation between them: every
  sixth attack fired the freeze, forever. A boss that does its one mechanic on
  schedule looks like a working boss, which is why this sat unnoticed.

  * **[wiki]** "After six normal/dragonfire attacks, it will use one of two
    special attacks and **alternate between them**" — and from the Changes
    section, "Vorkath's initial special attack will now be **randomly
    selected**." **Two different rules.** The first is a coin flip; every one
    after it is determined. Using the random rule for both makes the fight
    unpredictable forever, which breaks the standard strategy because that
    depends on knowing what is coming.
  * **While the spawn lives Vorkath is IMMUNE, not reduced.** A large reduction
    still lets a strong player burn through the mechanic that exists to stop
    them.
  * **The acid phase is a flat 50%** — and the Changes section is why that is
    pinned rather than inferred: "reduced by 50% instead of 75%". A port from
    older material makes the phase three times worse than it is.
  * The spawn's explosion **scales with its own health** (60 at full). A flat 60
    punishes a player who nearly killed it exactly as much as one who ignored
    it.

  Left **partial**: the three dragonfire types, the fireball dodge and the drop
  table.

- 2026-08-20 — **A flaky assertion of my own, found and removed.**

  B11's first timer test drew 400 samples and required both ends of the
  inclusive 50-129 range to appear. Each end has probability 1/80, so it failed
  about **1.3% of the time** — and it duly failed on the next run at 8 of 11,
  after passing when I wrote it.

  A flaky assertion is worse than no assertion: it teaches you to re-run instead
  of to look, and the one time it is telling the truth you will not believe it.
  Replaced with arithmetic — the width must be 80 so that `min + width - 1` is
  exactly 129 — which catches the `random(79)` off-by-one on **every** run
  rather than 98.7% of them. The sampling that remains only checks that no draw
  escapes the range, which is deterministic.

  Worth generalising: **this tree's selftests run under a shared RNG**, and
  anything I write that samples is a candidate for the same problem. Prefer
  arithmetic over sampling wherever the property can be stated as one.

- 2026-08-20 — **B5 Mage Arena II — three bosses that were loot piñatas.**

  All three existed and all three did exactly one thing: award their heart on
  death. No drain, no specials, no bind. They wore boss names and fought like
  nothing.

  **The cache symbols name the GOD, not the boss** — `ma2_boss_zamorak` is
  Porazdir, `ma2_boss_guthix` is Derwen, `ma2_boss_saradomin` is Justiciar
  Zachariah — which is why searching for "porazdir" finds nothing. Same trap as
  `corp_beast` and `cata_boss`, and it also corrects an error I made two entries
  ago: I filed Derwen and Zachariah under **Rise of the Six**. They are Mage
  Arena II's, and RotS is a separate slice.

  Each boss has one signature mechanic and each has a plausible wrong reading:

  * **The god spells drain "1 + 5%", each a DIFFERENT stat** — Porazdir takes
    Magic, Derwen takes Defence. The wiki stresses these differ from the Mage
    Arena battle mages' versions, which do not drain at all, so reusing the
    battle-mage spell drains nothing. The test pins 99 -> 5 and 20 -> 2, which a
    flat drain cannot produce, and level 1 -> 1, which a pure percentage cannot.
  * **Porazdir's ball "cannot be negated through prayer, however if the player
    is at least 12 tiles away ... no damage will be dealt."** Prayer is not the
    lever. The obvious implementation — a big hit that protection reduces — is
    wrong twice: it lets prayer help where the wiki says it cannot, and it
    ignores the only mitigation that works. The test asserts the praying and
    non-praying damage are *identical*, and that 12 tiles is zero while 11 is
    full.
  * **Derwen's orbs HEAL him** five each, every few seconds, and have 20
    hitpoints so they are killable. They are not an attack — modelling them as a
    damage source leaves Derwen regenerating faster than a player can hurt him
    with no visible cause.
  * **Zachariah's shock wave punishes standing STILL.** It is a movement check,
    not a damage roll: fail to move and you are dragged into melee and bound,
    where his sword lands **every 3 ticks** — faster than a normal weapon — and
    his melee "hit much harder than his regular magic attacks".

  Left **partial**: Tele Block, the freezes and the Kolodion wiring.

- 2026-08-20 — **B7 Xamphur — a fight that printed "Soft-skip" and advanced the quest.**

  `[opnpc1,akd_xamphur_combat]` did exactly this:

  ```
  mes("Soft-skip: Xamphur fight and cutscene.");
  %akd = ^akd_xamphur_cs;
  %akd = ^akd_table;
  ```

  Clicking the boss finished him. The npc is `akd_xamphur_combat`, 450
  hitpoints, confirmed — so this needed the fight, not the asset.

  Four rules, each with a plausible wrong reading:

  * **The Mark of Darkness doubles TWO things from one cause** — grasp damage
    *and* corruption chance — and it is **opt-in**: "Avoid the marks to fully
    avoid this effect." A player who never steps on one never sees either. A
    port that treats it as a timed phase makes the encounter harder than it can
    be played, and one that applies it to damage alone loses half the mechanic.
  * **Prayer applies AFTER the mark's doubling.** A marked player praying still
    takes more than an unmarked player praying — the test asserts that
    inequality, which applying the prayer first would collapse to equality.
  * **Corruption escalates 2, 4, 6 — it is not a flat drain three times.** That
    is where the stated total of 12 comes from; three equal drains of 4 reach
    the same total with the wrong shape, and the shape is what the player feels.
    The test asserts both the individual steps and that they sum to 12.
  * **Xamphur prays Protect from Magic himself, so he is IMMUNE to magic**, not
    resistant — a magic loadout does literally nothing. And he has **no melee
    attack at all**, so melee range is safe, which is the reverse of nearly
    every other boss and the reason the fight is a stand-still except during the
    crushing press.

  Left **partial**: the crushing press and the cutscene.

## 7. Open questions to settle before Wave E

These change what gets built and are the user's call, not the port's:

1. **Scope of NR-custom content.** Waves A–D and F are OSRS content NR happens
   to implement. Wave E is Near-Reality's own game design (boons, PvM arena,
   tournaments, loot keys, middleman, donation). Porting it makes this tree an
   NR clone rather than an OSRS server. Recommendation: build Wave E behind a
   content lane so both builds exist; confirm before E1.
2. **Persistence surface.** Grand Exchange (A8), hiscores (E17), clans (E14),
   presets (E15) and middleman history (E4) all need durable cross-session
   storage this tree does not have. That is an **engine** slice, not content —
   it must be filed and built before those five.
3. **Staff/administrative systems** (E18 rotten potato, E8 commands, E4
   middleman) are operational tooling, not content. Confirm they are wanted.

4. ~~**The clue-target dispatch seam (blocks A3b).**~~ **Built 2026-08-19** —
   `mock230_scripts_run_claim`. Kept here because the decision it records still
   applies to every future hook of this shape.

5. ~~**A headless scene harness (blocks the `partial` bosses).**~~ **Never
   needed — 2026-08-20.** Five bosses were marked `partial` on the grounds that
   arena geometry needs a live scene the C-driven selftest does not have. **It
   has one.** `mock230_world_teleport` rebuilds the scene around the player and
   `p_teleport` reaches it from script, so a selftest can MOVE to an arena and
   work there. `selftest_trail_dig` had been doing exactly that since A3a; what
   was missing was noticing it generalised.

   Proven rather than asserted: `[proc,selftest_nightmare_scene]` teleports,
   spawns the Nightmare and all four totems four tiles apart, confirms every one
   takes a distinct slot, is findable by uid, and reads its own hitpoints. It
   was mutation-tested — requiring two totems to share a uid turns it red at
   that stage and restoring turns it green.

   **What actually remains un-testable here is much smaller than "arena work":**
   animation timing and client-visible placement. Geometry, spawning, footprints
   and live param reads are all reachable. The `partial` rows stay partial
   because their AoE and record-swap logic is genuinely unwritten — not because
   it cannot be tested. 139 of the 218 npcs a clue
   can point at already own an `[opnpc1,…]` handler — they are quest NPCs,
   shopkeepers, slayer masters. In OSRS, talking to one of them while holding a
   matching cryptic or anagram clue gives the *clue* response instead of the
   normal dialogue, so the clue check has to run FIRST. This tree has no way to
   express that from content: a name binding is exclusive, a category binding
   loses to a name binding, and `[opnpc1,_]` would shadow every specific
   handler in the game (`inverted-script-fallback`).

   The fix is an engine seam — the npc-op dispatch asking content one question
   before it dispatches, and treating a `true` answer as having consumed the
   interaction. That is small, but it changes how every npc interaction is
   routed, so it is stated here rather than slipped in under a content slice.
   The same seam unblocks the loc-search targets (105) and is what a
   Slayer-task or diary-task hook would want too.
