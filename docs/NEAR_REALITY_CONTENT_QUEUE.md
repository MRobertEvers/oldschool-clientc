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
- `ToriRSServer_Pack --check-only` at **0 errors**.
- The content is performed end-to-end in the headless client harness — not
  "it compiles". State persists across logout/login where it should.
- No *new* silently-missing opcodes in the gap report.
- Existing content untouched; the selftest suite has no *new* failures
  (measure with and without, back to back, on one tree — see
  `torirsserver-selftest-operational-notes`).
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
| A3e | Treasure Trails — **the map-clue interface** | — | — | **partial** | The table's shape is established and pinned (`trail_mapclue.rs2`, 7-step): 41 rows, the tier counts, the two easy clues outside the id block, and **eight** variants not six. The row-to-interface RULE is still unknown — see the log; do not guess it. |
| A4a | Treasure Trails — **Hot & Cold** (the strange device) | 196 | 110 | **done** | The wiki's eight temperature bands; master device costs 3-8 hp a use. The dig that ends a hot/cold clue was already A3a's. |
| A4b | Treasure Trails — **light box, puzzle box, sextant UI** | 772 | — | **partial** | The **light box generator** is in and tested (`trail_lightbox.rs2`, 10-step): owner ceiling, the state clamp, the flip-on-add path, and the fill-in pass NR reads with the wrong variable. The puzzle box and sextant UI remain. |
| A5 | Treasure Trails — **reward tables** (easy→master) + casket open | 1347 | 1083 rows | **done** | Generated from the pinned wikitext by `tools/gen_trail_rewards.py` — 1,238 `DropsLineReward` entries parsed in exact rational arithmetic, 0 unresolved item names. |
| A6a | Treasure Trails — **milestones + the Mimic roll** | — | 130 | **done** | The wiki's six milestone counts; Mimic at 1/35 elite and 1/15 master with first-encounter dry protection. |
| A6b | Treasure Trails — **STASH units** | — | 260 | **done** | 44 hidey locs categorised and paired by a generator; the wiki's Construction build table; per-tier storage (stated deviation from per-unit). |
| A6c | Treasure Trails — **the Mimic fight, Watson, Patchy, Sherlock, scroll boxes** | 1100 | — | **partial** | **Watson's hand-in gate** is in and tested (`trail_watson.rs2`, 10-step): the four-tier bitpack and its consume-on-completion. The Mimic fight, Patchy, Sherlock and the scroll boxes remain. |
| A7a | Achievement Diaries — **the task registry** | — | 492 rows | **done** | 492 tasks generated from twelve pinned wiki pages. Cross-checks the authored per-tier totals on all 48 pairs — and caught two diaries' ids swapped. |
| A7b | Achievement Diaries — **the ~492 task hooks** | 3883 | — | pending | Each task's completion CONDITION. The wiki states them as prose and the cache states them nowhere, so every one is hand-written against the content it watches. The claim seam (A3b) is what the interaction-based ones will use. |
| A8 | **Grand Exchange** | 1882 | — | **partial** | The **matching engine** is in and tested (`ge_matching.rs2`, 11-step): seller-price execution, buyer refund, priority order, tie-break, self-match refusal. The offer slots, the interface and the price index remain. |
| A9 | **Dwarf Multicannon** | 963 | 310 | **done** | Cache ships the whole build chain (4 stage locs + the finished cannon's own 4 ops). Rotation IS the fire clock, per the wiki. |
| A10 | **Tears of Guthix** | 827 | 290 | **done** | Replaces the quest tree's placeholder. Cache ships the cave, the nine walls, the stream forms and the side panel; the walls are discovered with `loc_findallzone` rather than listed. |
| A11 | **Shooting Stars** | 623 | 200 | **done** | 83 landing sites generated from the wiki's own map pins. Star state is `%vars` (world-shared), not a player varp. |
| A12 | Eight small systems — **audited, mostly not portable to this cache** | 2084 | — | **closed** | Muddy chest and Zahur already done elsewhere. Master scrolls, Creature Creation and the sandstone grinder have **no cache records at all** in rev239 — checked obj/loc/npc by name. Only `trouver_parchment` exists, and it needs the item-protection-on-death system this tree does not have. See §6. |

### Wave B — bosses

| # | Slice | NR | Ours | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| B1 | **The Nightmare of Ashihama** + Phosani's | 4899 | 260 | **partial** | Party-size scaling (from FIVE up), the per-phase special table, all twelve totem records and the both-halves drop gate are in and tested. Arena, instance and AoE need a scene. |
| B2 | **Vorkath** | 1264 | 320 | **partial** | The special-attack rotation — alternation, the random first, spawn immunity, the flat 50% acid reduction and the scaled explosion — is in and tested. The dragonfire types, the fireball dodge and the drop table remain. |
| B3 | **Wilderness bosses** — Callisto/Artio, Vet'ion/Calvar'ion, Venenatis/Spindel | 2883 | 352 | **partial** | Dens/fees/exits were already in. Combat **mechanics** now under contract (`wildy_boss_combat.rs2`, 14-step): style precedence, per-variant max hits, knockback, phases, hound scaling and invulnerability, spiderling damage scaling. The per-tick AI wiring (projectiles, trap locs, live summons) is NOT done — the kill handlers are still counters. |
| B3b | Wilderness singles — **Scorpia, Chaos Fanatic, both archaeologists** | — | config | **partial** | Primaries done and correctly routed. The archaeologists' specials need the machine-owned `npc_stats_attackstyle.generated.rs2` extended, which is a generator change and not a batch pass. |
| B4 | **Alchemical Hydra** | 2384 | 240 | **partial** | Phase table, vent table, the 75% reduction and the enrage poison cadence are in and tested. The vents, arena and record swapping need a scene. |
| B5 | **Mage Arena II** | 1031 | 340 | **partial** | All three bosses' signature mechanics — the 1+5% drains, Porazdir's prayer-proof ball, Derwen's healing orbs, Zachariah's bind — are in and tested. Tele Block, freezes and the Kolodion wiring remain. |
| B6 | **Grotesque Guardians** (Dusk & Dawn) | 2010 | 340 | **partial** | Phase gates, per-phase/per-style immunity, and all of Mod Ash's phase-4 rules are in and tested. The record swapping and the AoE placement need a scene. |
| B7 | **Xamphur** | 1031 | 210 | **partial** | The fight A Kingdom Divided soft-skipped: Marks of Darkness, escalating corruption, magic immunity, no-melee. The crushing press and the cutscene remain. |
| B8 | **Abyssal Sire** | 1540 | 240 | **partial** | Stun ladder, vent damage floor, miasma bands and the once-only explosion gate are in and tested. The lair layout, tentacles and record swapping need a scene. |
| B9 | **Cerberus** | 1516 | 290 | **done** | The full rotation: three-style attack, souls every 7th under 400hp, lava every 5th under 200hp, and Mod Ash's 10% skip. |
| B10 | **Rise of the Six** (B15 folded in) | 895 | 120 | **partial** | The encounter's one rule and now the **supply chest table** (`rots_rewards.rs2`, 7-step): uniform weights, the truncating 1.5 modifier, and the nine noted entries. The arena and the entry puzzle remain. |
| B11 | **Skotizo** | 861 | 200 | **partial** | The four awakened altars — two reduction rates, two caps, the one-hit demonbane disable, the 50-129 tick check interval and the one-minute cooldown — are in and tested. The lair, the totem and the drop table remain. |
| B12 | **Corporeal Beast** | 1284 | 210 | **partial** | The damage rules — corpbane, the stab-only gate, the 100 cap, Protect-from-Magic's 33%, split poison immunity — are in and tested. The dark energy core and the drop table remain. |
| B13 | **Mage Arena II** | 930 | 168 | **partial** | The three bosses' own specials were already in `ma2_mechanics.rs2`. The shared **attack selection** is now under contract (`ma2_base.rs2`, 11-step): availability-built special pool, the one-in-five roll, melee frequency 0 = always, the moving splash threshold. Kolodion's stages and the enchanted symbol remain. |
| B14 | **Xamphur** | 875 | — | **partial** | Corruption and magic immunity were already in `xamphur.rs2`. The **phantom hands** are now under contract (`xamphur_hands.rs2`, 7-step): hands gate all damage, spawn in pairs, 25-tile reach, corruption needs a landed hit. The arena instance and the XamphurBoost world-event table remain. |
| B15 | ~~**Rise of the Six**~~ — **DUPLICATE of B10** | — | — | **folded** | Same content, listed twice in the original plan with different line counts (895 / 870). NR has exactly one `com/zenyte/game/content/rots` package. Track it at B10. |
| B16 | **Skotizo** | 829 | 44 | **partial** | Altar damage reduction was already in. **Minion spawning** now under contract (`skotizo_minions.rs2`, 7-step): the ankou/demon else-if coupling, one ankou at a time, the three-at-once demon top-up. The instance (DynamicMap) and loot remain. |
| B17 | **Corporeal Beast** | 766 | — | **partial** | Damage reduction was already in `corp_damage.rs2`. The **dark energy core** is now under contract (`corp_core.rs2`, 10-step): spawn gates, the mirrored heal, the single-use poison stun, flight time, mid-air removal. The cavern instance and drop processor remain. |
| B18 | **Kraken** | 644 | config | **done (combat)** | Magic, speed 4. Single-style, fully described by the contract. |
| B19 | **King Black Dragon** | 471 | already owned | **done** | `areas/wilderness/king_black_dragon.rs2` already has a three-way rotation incl. dragonfire — MORE than the infobox's two styles. This slice added its `damagetype`; a second rung would have been a duplicate. |
| B20 | **Sarachnis** | 469 | 140 shared | **done** | Melee primary + ranged secondary, both rungs. |
| B21 | **Obor** + **Bryophyta** | 774 | 140 shared | **done** | Obor melee+ranged, Bryophyta melee+magic, both rungs. |
| B22 | **Thermonuclear Smoke Devil** | 391 | config | **done (combat)** | Magic, speed 2. Single-style. |
| B23 | **Nex** — parity sweep against NR's package | 1934 (not 4190) | 1228 Nex-specific | **done (audit + 2 gaps closed)** | Swept 23 NR mechanics against ours: 20 present, 2 real gaps now closed (`nex_containment.rs2`, 7-step), 1 non-gap (NR has it commented out). |
| B24 | **Tormented Demons** — NR variant vs ours | 345 (not 896) | ~1500 OSRS + ~1300 rs2012 | **done (reconcile)** | Row premise was stale: we have BOTH the OSRS demon and the rs2012 one. Ours is ahead of NR on every axis; NR carries a De Morgan defect we do not. 286 lines of our TD assertions were debugproc-only — the pure subset is now in CI (`td_shield_selftest.rs2`, 6-step). |

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
| C18 | **Waterbirth Island** + **Taverley** + **TzHaar** area plugins | 355 | partial | **done** | Taverley shortcuts were already covered by the wiki-generated table (under display names, not symbols). TzHaar's Inferno **practice fee** is now in (`inferno_practice_fee.rs2`, 5-step). Waterbirth's crack is the DKS entry, already present. |
| C19 | **Wilderness slayer** + wilderness plugins remainder | 299 | 300 | **done** | Krystilia's 37-task table, generated and verified at runtime. |
| C20 | **Item retrieval service** (shared: ToA, ToB, Nightmare, God Wars, Barrows, Zulrah, Vorkath, RotS…) | 341 | 0 | **done** | Discovered while closing C2. |

### Wave D — skills

| # | Slice | NR | Ours | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| D1 | **Agility — rooftop courses** (Draynor→Prifddinas, 9 courses) | 5529 | 9 courses, xp verified | **done** | All nine were already implemented; `check-agility-courses` now holds each one's per-obstacle experience to the pinned wiki. The one real gap is the two diary bonuses — see the log. |
| D2 | Agility — **shortcuts** | 8242 | table generated | **data done, scripts pending** | All 162 shortcuts generated from the wiki with their levels, alternative routes and fractional xp; `check-agility-shortcuts` green. The per-obstacle bindings remain. |
| D3 | Agility — **Gnome/Barbarian/Wilderness/Pyramid/Pollnivneach courses** | 2995 | 9 courses verified | **done** | Sixteen of seventeen courses now agree exactly with the pinned wiki, including every non-rooftop one this tree implements. Werewolf alone remains unreadable from source. |
| D4 | **Magic — teleports** (all books + structures) | 3403 | in the 220-spell table | **data done, scripts pending** | Every teleport across all four books carries its level, xp and rune set in `spellbooks.generated.enum`. |
| D5 | Magic — **Lunar spellbook** | 2598 | 44 spells generated | **data done, scripts pending** | All 44 Lunar spells with levels, xp and rune sets, from pinned per-spell pages. |
| D6 | Magic — **Arceuus spellbook** | 1542 | 67 spells generated | **data done, scripts pending** | All 67 Arceuus spells, same source and generator as D5. |
| D7 | Magic — **regular spellbook remainder + lecterns + resources + actions** | 2701 | 84 spells generated | **data partly done** | The standard book's 84 spells are in the generated table. Lecterns, tablets and the non-spell actions remain. |
| D8 | **Slayer — completion** (tasks, masters, unlocks, dialogue) | 3544 | 1804 + Konar + Krystilia | **done** | Task tables cache-backed, unlocks from cache dbtable 117. Superior coverage is **27 of 34 — every superior whose base monster this cache gives a category**. The other seven are blocked on the cache, not on work. |
| D9 | **Farming — completion** (contracts, Hespori, seed vault, supercompost) | 8463 | 5804 + 3 | **done** | Hespori, the seed vault and farming contracts are all in and verified. |
| D10 | **Prayer** (ectofuntus, altars, bone burying at depth) | 1706 | 1419 + gilded altar | **partial** | The gilded altar's 250/300/350 ladder and its two-burner message are in and verified. The Chaos Altar's 50% bone-save and the libation bowl remain. |
| D11 | **Thieving** (tables, actions, pickpocket depth) | 1769 | 1515 + 3 contracts | **done** | All four thieving data tables — pickpockets, stalls, chests, doors — are under contract. Four stale experience awards found and corrected; one wiki self-conflict recorded rather than guessed at. |
| D12 | **Mining** + **Smithing** completion | 2627 | 1817 + 3 contracts | **done** | All three data tables under contract: 19 rocks, 8 bars, and 158 anvil rows checked against the wiki's stated per-bar rule. |
| D13 | **Woodcutting** + **Firemaking** completion | 1581 | 510 + nests | **partial** | Bird nests from every tree are in and verified — rate, cape bonus and the rabbit-foot range rule. Machetes and the guild's remaining shortcuts remain; NR's bonfire is RS3 and out of scope. |
| D14 | **AFK skilling** + fletching/cooking/fishing/herblore parity sweep | 661 + 3432 | present | **partial** | AFK skilling's shared rules are in (`skill_afk/`, 8-step): the non-monotonic xp ladder, points, the cap sentinel. The four-skill parity sweep remains. |

### Wave E — Near-Reality custom and meta systems

Default to a content lane (§0.5) for this wave — a vanilla build should be able
to leave it out.

| # | Slice | NR | Ours | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| E1 | **PvM Arena** | 4514 | 130 | **partial** | Emir's Arena's ranked reward model — the paying loss, the capped streak and the bracket — is in and verified, shared with E2. The waves/teams/revive systems are NR's own. |
| E2 | **Tournament** system | 3572 | shared with E1 | **partial** | The 4-to-64 bracket, its round count and the unrewarded manual-organiser rule are in and verified. Spectating and the controller remain. |
| E3 | **Boons** | 3469 | 90 | **partial** | The ownership model — owned-and-off, always-unlocked, and the precedence between them — is in and verified. The 84 individual boon effects are 84 pieces of content. |
| E4 | **Middleman trading** | 2038 | 90 | **partial** | The four-state machine and the staff-eligibility rule are in and verified. The offer interfaces and the trade history file remain. |
| E5 | **Wilderness Vault** + Queen Reaver | 1982 | 110 | **partial** | The state machine and its four clocks are in and verified. Genuinely NR-custom — the boss and vault locs are NR ids absent from this cache, held behind one constant each. |
| E6 | **Bounty Hunter** | — | 160 | **partial** | Target range settings, the coffer's four deposit bands, the five skull tiers and the teleport gate are in and verified. The Emblem Trader's store and the (bh) equipment charges remain. |
| E7 | **Follower / pet system** | — | 150 | **partial** | The award rule, its three outcomes and messages, the skilling priority and Probita's free reclaim are in and verified. Spawning, calling and the menagerie remain. |
| E8 | **Commands** (staff + player command surface) | 1499 | partial | **done (model)** | The 1499 commands are administration; the PRIVILEGE MODEL is the game behaviour and is ported. `staff_privilege/`, 9-step selftest. |
| E9 | **Magic storage unit** | 1262 | done | **done** | Costume storage: all-of-pieces over any-of-ids, the UIM's cheaper unlock, and NR's store-without-delete duplication corrected. `storage_magic/`, 8-step selftest. |
| E11 | **Flower poker** | 1109 | 120 | **partial** | The hand ranking and the draw rule are in and verified. The planting session and the stake remain. |
| E12 | **Loot keys** | 571 | 130 | **partial** | Who gets a key, when they get none, the destroy cap and the disengage rule are in and verified. Skully, the chest and the per-player filters remain. |
| E13 | **Universal shop** | 989 | 70 | **partial** | The listing model — the -1/0 price distinction and per-item ironman restriction — is in and verified. The tabbed interface needs cache assets NR packs itself. |
| E14 | **Clans** | 977 | 150 | **partial** | Clan Wars' four victory conditions, the per-condition re-join rule and the five magic settings are in and verified. The chat-channel/clan social system itself remains. |
| E15 | **Presets** | 825 | 70 | **partial** | ToA's five invocation presets — the game's own preset feature, cache-backed — are in and verified. NR's tournament loadouts are its own and need E2's controller. |
| E16 | **Crystal** equipment + recipes + chargeables | 760 | +130 | **partial** | The charge model — separate armour/tool starts, the shared cap, the shard rate and the nested damage exceptions — is in and verified. Ilfeen's pricing ladder and the recolour crystals remain. |
| E17 | **Hiscores** | 713 | — | **determined: no portable content** | A SQL schema (`user_skill_stats` and friends) plus an exporter. Its one game rule — hitpoints starting at level 10 / 1154 xp — this tree already asserts in `torirs_server_world.c`. See the log. |
| E18 | **Rotten potato** (staff tool) | 677 | done (dispatch) | **done** | Ban/mute are infra; the per-viewer menu build, option NONE, and the delete-on-ineligible-click are ported. `staff_potato/`, 8-step selftest. |
| E19 | **Drops** framework + rewards + larran's key | 571+465 | +90 | **partial** | Larran's three-branch key rate is in and verified against Mod Ash's published formula. The drops framework itself remains. |
| E20 | **Well of Goodwill**, **comp capes**, **contests**, **challenges**, **killstreaks**, **wheel of fortune**, **server events** | 516+512+420+190+290+124+159 | 90 | **partial** | The Well's four perks, their shared threshold and the 32-bit overflow are in and verified. The other six systems remain. |
| E21 | **Donation / donator / vote** | 417+108+58 | partial (vote) | **done** | MemberRank tables, the three-way `togglesChance` sentinel, toggle declaration order and the claim remainder cascade. `donator/`, 11-step selftest. |
| E22 | **Gravestones parity**, **ground items**, **imbue**, **glider**, **sailing**, **object/shop/combat/quest shims** | ~900 | +170 | **partial** | Gliders, Death's Office and **imbue** are in and verified (`item_imbue/`, 10-step). **Sailing and ground items determined: already covered here from better sources** — see the 2026-08-20 entry. The object/shop/combat/quest shims remain. |

### Wave F — seasonal and event content

| # | Slice | NR | Ours | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| F1 | **Christmas 2019** (incl. cutscenes) | 3752 | 110 | **partial** | The Christmas cracker — the permanent, year-independent half — is in and verified. NR's 2019 event area, npcs and cutscenes are year-specific custom content this cache does not carry. |
| F2 | **Easter 2020** (area, npc, object plugins) | 3389 | via Diango | **partial** | The permanent half — Diango's holiday item retrieval — is in and verified and covers every seasonal item. NR's 2020 event area, npcs and cutscenes are year-specific content this cache does not carry. |
| F3 | **Halloween 2019** | 1203 | via Diango | **partial** | Same as F2: the retrieval is in; the 2019 event itself is year-specific custom content. |
| F4 | **Advent calendar** + **Easter 2024** | 568+892 | via Diango | **partial** | Same as F2/F3. |

---

## 6. Progress log

Append one dated entry per slice, in the CONTENT_PORT_QUEUE style: what landed,
the final script count, `ToriRSServer_Pack` error count, the suite delta measured
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
  * `ToriRSServer_Pack` did not link at all — three symbols. Fixed; it is a gate
    again.
  * `cachepack`'s record merge treated `param` as a list, so two files stating
    one param both survived and the *first* won, while the runtime assigns per
    line so the *last* wins. Three npcs had a server band contradicting the
    world with no way to converge. Fixed by ranking three layers (cache /
    generated / authored) the way `torirs_server_content.c` already loads them.
  * One content bug it surfaced: `[skeletonmage]` authored twice with different
    `attack_anim`, one of them the giant skeleton's against a base rig.

  Final: 27738 scripts; `ToriRSServer_Pack --check-only` **0 errors** (was 1, and
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

  Final: 27776 scripts; `ToriRSServer_Pack --check-only` 0 errors; selftest 15 unique
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

  Final: 27825 scripts; `ToriRSServer_Pack --check-only` 0 errors; all seven trail
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

  `ToriRSServer_ScriptsRunClaim()` runs a named content proc before the ordinary
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

  Final: 27834 scripts; `ToriRSServer_Pack --check-only` 0 errors; all eight trail
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

  Final: 27846 scripts; `ToriRSServer_Pack --check-only` 0 errors; all nine trail
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

  Final: 27857 scripts; `ToriRSServer_Pack --check-only` 0 errors; all eleven trail
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
  and all twelve trail selftests green. `ToriRSServer_Pack` 0 errors.

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
  three generators, and a 36-page pinned wiki corpus. `ToriRSServer_Pack` 0 errors,
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

  Final: 27898 scripts; `ToriRSServer_Pack` 0 errors; all three generators `--check`
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

  Final: 27936 scripts; `ToriRSServer_Pack` 0 errors; all generators `--check` clean;
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

  Final: 27968 scripts; `ToriRSServer_Pack` 0 errors; all four generators `--check`
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

  Final: 27974 scripts; `ToriRSServer_Pack` 0 errors; all five generators `--check`
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

  Final: 27995 scripts; `ToriRSServer_Pack` 0 errors; six generators `--check` clean;
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
  infoboxes and is wired into `torirsserver-scripts` beside the tree's other content
  contracts. **Every future Wave B boss whose encounter is config rather than
  script will need that shape rather than a selftest.**

  Final: 27998 scripts; `ToriRSServer_Pack` 0 errors; contract green; 26 owned
  assertions green.

- 2026-08-20 — **Ten solo bosses given their combat contract in one pass**
  (B18 Kraken, B19 KBD, B20 Sarachnis, B21 Obor + Bryophyta, B22 Thermy, and
  the four Wilderness singles as B3b).

  All ten are already cache records with their stat block, size and
  `op2=Attack`. What the cache cannot state is the attack rate and the attack
  style, and for four of them that is the entire encounter. So the slice is one
  config file plus `tools/check_boss_contract.py`, wired into `torirsserver-scripts`.

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

  Final: 27999 scripts; `ToriRSServer_Pack` 0 errors; both contracts green; 26 owned
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

  Final: 28010 scripts; `ToriRSServer_Pack` 0 errors; both contracts green.

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

  Final: 28020 scripts; `ToriRSServer_Pack` 0 errors; both contracts green; 27 owned
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

  Final: 28034 scripts; `ToriRSServer_Pack` 0 errors; both contracts green; 28 owned
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

  Noted in passing: `ToriRSServer_Pack` went to 4 errors during this slice, all of
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

  Final: 28107 scripts; `ToriRSServer_Pack` 0 errors; both contracts green; 30 owned
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

  Final: 28115 scripts; `ToriRSServer_Pack` 0 errors; both contracts green; 31 owned
  assertions green.

- 2026-08-20 — **ENGINE: `^constant` in an npc param parsed to zero.**

  The find of the session, and it was live in the tree before any of this
  lane's content.

  `apply_param` in `torirs_server_content.c` expanded a `^constant` in exactly TWO
  branches (`undead` and the elemental weakness) and read the value with a bare
  `atoi` everywhere else. **`atoi("^slash_style")` is 0.** So
  `param=damagetype,^slash_style` and `param=attackrate,^dks_attackrate` both
  silently became zero: the config read correctly, the compiler had no opinion,
  `ToriRSServer_Pack` was happy because it checks the TEXT, and the npc simply fought
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

  Final: 28115 scripts; `ToriRSServer_Pack` 0 errors; both contracts green; 32 owned
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

  Final: 28126 scripts; `ToriRSServer_Pack` 0 errors; both contracts green; 33 owned
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

  Final: 28137 scripts; `ToriRSServer_Pack` 0 errors; both contracts green; 34 owned
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

  Final: 28144 scripts; `ToriRSServer_Pack` 0 errors; both contracts green; 35 owned
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
  `ToriRSServer_Pack` 0 errors; both contracts green; all six generators `--check`
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

  Final: 28171 scripts; `ToriRSServer_Pack` 0 errors; both contracts green; 37 owned
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

  Final: 28183 scripts; `ToriRSServer_Pack` 0 errors; both contracts green; 39 owned
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

  Final: 28190 scripts; `ToriRSServer_Pack` 0 errors; both contracts green; 40 owned
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

  Final: 28255 scripts; `ToriRSServer_Pack` 0 errors; both contracts green; 42 owned
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
  into `torirsserver-scripts` as `check-clan-crystals`.

  **The asymmetry is the feature, and it is why the generator exists.** Eight
  clans have a crown and a clan crystal — that half is symmetric. But only
  **seven** dress crystal armour and only **seven** dress crystal weapons, and
  they are not the same seven: **Meilyr has no armour variants and Hefin has no
  weapon variants.**

  **[Corrected 2026-08-20, from the Crystal equipment article]** I recorded that
  asymmetry as an unexplained cache quirk. It is not: *"In their default
  colouration, the armour has the colours of the **Meilyr** clan, and the
  corrupted weapons have the colours of the **Hefin** clan."* Those two are the
  DEFAULTS, so there is no recolour variant to ship — the base item already is
  that clan's colours. The generated table is right and the reason is now
  written down. An 8x8 table typed by hand names two rows of objs the cache
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
  `torirs_server_content.c` — `default=null` now yields -1 on every typed output, not
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
  `torirs_server_container.c` rather than assuming showed why:
  `ToriRSServer_ContainerScope` already answers `TORIRSSERVER_CONTAINER_WORLD` for any
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
  `ai_queue1` trigger), and `ToriRSServer` refuses to run on a stale script pack, so
  every attempt aborted before reaching the stanza. I polled for about twenty
  minutes across three waits. The assertion is wired into
  `torirs_server_world.c` and `trail_selftest_check.sh` and will run the moment the
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
  dat2 cache (`ToriRSServer_DbLoadCache`); this file names the columns so
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
  `torirsserver-scripts` as `check-dbtable-rows`.

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

- 2026-08-20 — **B10 Rise of the Six — one rule, three ways to get it backwards.**

  **NR-custom content**, like C13: OSRS has no Rise of the Six, so the wiki is
  silent and NR's `RotsBrother.java` is the specification. But the six brothers
  themselves are in the cache (`barrows_dharok`, `barrows_ahrim`, …), so this
  needed scripts and not assets — the third time that has been true today.

  The whole encounter is `sendDeath()`, and each of its three effects is easy to
  implement the wrong way round:

  * **A dying brother heals every living one by 5% of the DYING brother's
    maximum hitpoints** — not 5% of the recipient's. With six brothers of
    different sizes those are different numbers, and the obvious reading turns
    killing the biggest brother first into a *reward* for the survivors instead
    of the punishment it is. The test asserts a 600-hitpoint death gives 30 and
    a 200-hitpoint death gives 10, and that the two are not equal.
  * **A death also RESETS the revive timer of brothers already dead.** So
    killing slowly is actively counterproductive: a corpse at 49 of its 50 ticks
    goes back to zero. A port that only heals the living leaves the encounter
    beatable one brother at a time, which is precisely what it exists to
    prevent.
  * **Completion needs all six down AT ONCE**, not six kills. Counting kills
    lets a player farm one brother six times.

  And the brothers are **freeze immune** — the standard "freeze one, kill the
  rest" plan is the thing the design refuses.

  Left **partial**: the arena, the entry puzzle and the reward table.

- 2026-08-20 — **D3 non-rooftop courses — and a checker that was wrong about the tree three times.**

  Extended `check-agility-courses` from the nine rooftops to nine more. Four
  agree exactly on the first honest reading: **Wilderness, Agility Pyramid, Ape
  Atoll and Prifddinas**.

  Getting there meant fixing my own tool twice more, and the pattern is worth
  stating because it has now cost three rounds:

  * **Ape Atoll "mismatched" by 300.** Its per-obstacle table sums to 280 while
    the page states **580 per lap** — the final tropical tree carries a
    completion bonus that sits outside the `{{+=|xp|...}}` template. My
    stated-total fallback only matched one phrasing ("Players get N experience
    from completing"); this page says "rewards 580 Agility experience per
    completed lap". Four phrasings now.
  * **Penguin "mismatched" by 120 — and our code is right.** The wiki row reads
    `160 (40 each)`: four icicle pillars, one shared `[oploc1]` handler awarding
    40. Summing the SOURCE gives 40 where a lap gives 160.

  That second one is the general defect: **summing `stat_advance` in a file is a
  lower bound on a lap, not a lap total**, and the shortfall looks exactly like
  missing obstacles. So the checker now counts `[oploc]` bindings too, and when
  a course binds more locs than it has award statements it reports **"shared
  handlers -- lap total unreadable from source"** rather than inventing a
  finding. Three courses land there: Gnome, Penguin, Werewolf.

  **And the "genuine gap" was not one either.** I reported Barbarian Outpost as
  5 of 9 obstacles. It is complete and exact. Two more helpers were hiding it:

  * **`~agility_climb_up` also awards experience from its first argument** —
    a third helper after `stat_advance` and `~agility_force_move`. So the list
    is now *derived* rather than discovered one false finding at a time: every
    proc in `agility.rs2` whose body does `stat_advance(agility, $param)`.
  * **One handler can serve several world placements of the same loc name.**
    Barbarian's three crumbling walls share `[oploc1,castlecrumbly1]` and are
    told apart by `switch_int(coordx(loc_coord))`; its single award of 13.7 is
    paid three times a lap. The checker now counts the `case` arms and
    multiplies, which is what turns 125.9 into the wiki's exact 153.3.

  With both fixed, **sixteen of seventeen courses agree exactly** — including
  Gnome (110.5), Penguin (540) and Ape Atoll (580), all three of which I had
  previously reported as broken. Only Werewolf remains unreadable from source.

  **Four false findings, four rounds, one tool.** Every single "defect" this
  checker produced was the checker being wrong about the tree. The code was
  right every time.

  Dorgesh-Kaan is excluded with its reason in the source: its page states
  experience in prose rather than in the table every other course uses, so this
  parser cannot read it and would report a bogus mismatch.

  **The meta-lesson, three strikes in:** a checker that is wrong about the tree
  manufactures work, and I nearly filed six false defects across two turns. When
  a check disagrees with the code, the first hypothesis should be that the check
  is wrong — because the code has been exercised by players and the check has
  been exercised by nobody.

- 2026-08-20 — **Two bosses were swinging crush at a magic attack, and a generator was the cause.**

  Written while the compiler was down again. `check_boss_contract` read only the
  hand-authored `bosses.npc`, so every boss whose stats live in the **generated**
  `combat_stats.generated.npc` reported "no block in any boss config" —
  indistinguishable from a boss with no stats at all. Widening it to read both
  files (1,277 records) immediately paid for itself.

  Seven records are declared in both files and all seven "conflict" on
  `damagetype` — but **five are the same value written two ways**: the authored
  layer spells the style (`^crush_style`) and the generated layer writes the id
  (`2`). Normalising before comparing leaves **two real disagreements**:

  ```
  chaos_fanatic      authored ^magic_style (4)   generated 2 (crush)
  smoke_devil_boss   authored ^magic_style (4)   generated 2 (crush)
  ```

  **The root cause is a silent default in `gen_npc_stats.py`.** Its `STYLE_MAP`
  has no entry for the two ways the wiki writes magic-with-a-projectile — Chaos
  Fanatic is `[[Ranged magic]]`, the Thermonuclear smoke devil is
  `[[Magical ranged]]` — so `classify_style` fell through to
  `return "physical", "crushattack", 2, "no recognized attack style"`. A
  plausible wrong value, written into a file that says "Re-running rewrites this
  file", where a hand-fix would have been silently reverted on the next run.

  So the fix went into the **generator**, not the generated file: both spellings
  added to `STYLE_MAP` with the reason. My first instinct was to correct the two
  records directly, which would have been undone and would have taught nobody
  anything.

  **And a correction to my own first reading of it.** I called the default
  "silent". It is not — `gen_npc_stats.py` writes the note into that npc's
  `.stats` ledger, and 40 of them carry it. What is true is weaker and still
  worth fixing: the note lands in one of 40 files under `npc_stats/` while the
  value it explains lands in `combat_stats.generated.npc`, where it looks like
  any other deliberate choice. Nobody reads 40 ledgers.

  So the fix is visibility, not bookkeeping: **`tools/check_npc_style_defaults.py`**,
  wired as `check-npc-style-defaults`. It reads the ledgers and prints the whole
  list every build:

  ```
  41 npc(s) fell back to crush across 10 unrecognised style spelling(s)
    magical melee     29   crush 2   ranged melee 2   magical ranged 2
    ranged magic 1    none 1    typeless ranged 1   poison 1   all 1   various 1
  ```

  **`magical melee` alone is 29 npcs** — most of the God Wars roster. Not every
  entry is a bug: `various` (Zalcano) and `all` (the Whisperer) are the wiki
  declining to pick one, and dragonfire genuinely is not a damagetype. But "not
  a damagetype" and "crush" are different answers and the generator gives the
  second, so the list needs a design decision rather than a lookup-table row —
  which is why it is *reported* and not *failed*. It is now impossible to miss.

- 2026-08-20 — **D11 pickpocketing — three stale experience awards, inherited and never questioned.**

  `tools/check_pickpocket_contract.py` holds the fifteen rows of
  `pickpocket.dbrow` to the wiki's Thievable NPCs table. Nine are checkable by
  name and **three of those nine were wrong**:

  ```
  gnome     config 198.3   wiki 133.3
  hero      config 273.3   wiki 163.3
  paladin   config 151.8   wiki 131.8
  ```

  All three came in from LostCity, which is a rev-254 tree, and nobody had
  reason to doubt them — the **levels are right in all three cases**, which is
  exactly what makes a wrong award survive: the row looks checked because half
  of it is.

  I did not take the table's word for it alone. The **hero's own article states
  163.3** in prose, independently of the table — two places on the wiki agreeing
  against one inherited value, which is what settled it. Corrected, with that
  reasoning written into the file's header so the next reader does not have to
  re-derive it.

  Experience is compared in **tenths** throughout: a farmer pays 14.5 and the
  H.A.M. member 22.2, so an integer comparison would call two different awards
  equal.

  One thing deliberately NOT checked, and said so in the tool: **stun duration.**
  The wiki gives stun *damage* in the table and duration only in prose for some
  npcs, while this tree stores `stun_ticks` — ticks against seconds with no
  stated conversion for every row. Checking it would mean inventing the mapping,
  so it is left alone rather than half-checked.

- 2026-08-20 — **D11 stalls — a fourth stale award, and a parser that nearly matched the wrong column.**

  `tools/check_stall_contract.py`, wired as `check-stalls`. One more inherited
  value was wrong: **the fur stall paid 36 experience where the game gives 45.**
  Cross-checked the way the pickpocket ones were — the Fur stall article states
  45 in its own prose, independently of the table.

  Two things about the parser are worth recording, because both would have
  produced a confident wrong answer:

  * **The wiki's table is one row per stall INSTANCE, not per stall type.**
    Silver ore appears twice at level 50 (205 and 80 experience) and sapphire
    twice at level 75 (408 and 129.5). So the checker requires the (name, level)
    match to be **unique** and skips the stall otherwise, rather than guessing
    which instance this tree implements. Three of our seven stalls are skipped
    for exactly that reason and the tool says so — four checked is the honest
    number, not a bigger one.
  * **My first parser keyed on the first `[[link]]` in the row, which is the
    LOOT column, not the label.** `{{plinkt|Fur stall|...}}` carries no
    brackets, so "Fur stall" and "Fur stall (Port Roberts)" — two different
    stalls, same level, 45 against 38.5 — would have collapsed onto one key.
    It happened to give the right answer for fur and would not have for silk.
    Keying on the `plinkt` label is the fix.

  **Four stale experience awards found across D11's two tables** (gnome, hero,
  paladin, fur stall), every one inherited from LostCity, every one with its
  LEVEL correct — which is what let them survive: the row looks checked because
  half of it is.

- 2026-08-20 — **D11 chests and doors — clean, and that is a result.**

  `tools/check_thieving_tables.py` completes D11's data coverage: **15 chest and
  door rows agree exactly** with the pinned wiki. After four stale awards in the
  previous thirteen rows I expected more; there are none.

  That is worth recording rather than shrugging at. **The staleness LostCity
  handed this tree is not uniform** — the pickpocket and stall tables carried
  wrong experience while the chest and door tables did not — so "inherited from
  LostCity" is not by itself evidence a value is wrong, and the next person does
  not need to re-check these fifteen.

  Rows are matched by **level**, not by name: our block names carry the cache's
  loc symbol (`trapped_chest_trapchest1`, `locked_door_picklock3_l`) and the
  wiki's carry the chest's English name, with no shared key to join on. A level
  shared by two wiki rows is skipped rather than guessed.

  **One row is skipped for a reason worth keeping**: the Magic axe hut door. The
  Thieving table says level 23 for 22.5 experience; the door's own article says
  level **32**. Ours is level 23 for 25. Two wiki pages contradict each other on
  the level, so the experience cannot be trusted from either without settling
  that first — and "correcting" ours would be picking a side at random. Recorded
  in the checker with both figures, the same way the Revenant maledictus'
  cache/wiki divergence and Varrock's self-inconsistent course total are.

  **D11 is done**: all four of its data tables are under contract.

- 2026-08-20 — **D13 bird nests — verified at last, after four blocked builds.**

  The tree rolled nests only inside the Woodcutting Guild. They come from every
  tree in the game, and the rule is a Mod Ash quote the wiki preserves:

  > "If you've got the rabbit foot necklace, pick number 0-94 inclusive.
  > Otherwise, pick number 0-99 inclusive. 0 = red egg, 1 = blue egg, 2 = green
  > egg, 3-34 = ring, otherwise it's seeds."

  **The rabbit foot narrows the RANGE; it does not reweight the table.** The egg
  and ring slots keep their absolute positions 0..34 and only the seed tail
  shortens — which is exactly why the wiki can state the result as 3/95 and
  32/95. A port that "increases the egg and ring chance by some percent"
  produces different numbers and cannot reproduce either figure. The test proves
  it is the range and not the weights by asserting slot 34 is a ring and slot 2
  a green egg **either way**.

  Two more: the rate is **1/256 per log you would normally get, regardless of
  tree type** — per log, so a redwood rolls several times, and flat across every
  tree, the opposite of how the rest of woodcutting scales. And the Woodcutting
  cape's "additional 10% chance" is a better RATE: denominator 232, not 246.

  Also checked before building: **NR's `BonfireAction` is not OSRS content.**
  The OSRS "Bonfire" is scenery at the Wintertodt Camp; bonfire log-stacking is
  RS3. Same category as the Ganodermic Beast, and cheaper to find out first than
  after.

- 2026-08-20 — **D12 mining — 19 rocks, all clean.**

  `tools/check_mining_contract.py`, wired as `check-mining`. Nineteen rock rows
  agree exactly with the pinned wiki on both level and experience; two are
  skipped because they have no wiki ore row to match by name — the gem rock
  (`gems`) and this tree's `'perfect' gold`, both of which are rock VARIANTS
  rather than ores in the wiki's table.

  Matched by **ore name** here (`{{plinkt|Iron ore|txt=Iron}}` against our
  `ore_name,iron`), which is a genuine shared key — unlike the chest and door
  tables, where our blocks carry cache loc symbols and had to be matched by
  level instead. Worth noting because picking the wrong join key is what nearly
  broke the stall checker: there is no single right way to match these tables
  and each one has to be looked at.

  Experience in tenths again: blurite pays 17.5 and volcanic ash 10.

  **Running tally of the LostCity-inherited data now under contract:** 13 rows
  in pickpockets and stalls (4 wrong, corrected), 15 in chests and doors (clean),
  19 rocks (clean), 17 agility courses (clean after four false alarms from my
  own tooling). The staleness is real but concentrated — it is not a reason to
  distrust everything, and now four of those five tables can never drift again
  without the build saying so.

- 2026-08-20 — **D12 smelting — 8 bars clean, and a row the parser refuses to read.**

  `tools/check_smelting_contract.py`, wired as `check-smelting`.

  **The row shape cost me a wrong first parse.** The level is the line BEFORE
  the bar's name, not after it, and the experience is two lines further on past
  the ore-requirement line:

  ```
  |15
  |{{plinkt|Iron bar|txt=Iron}}
  |{{plinkp|Iron ore}}<sup>x1</sup>
  |12.5
  ```

  Scraping numbers in order gives the ore COUNT where the experience should be —
  "Bronze bar 1, 1, 1" — which reads plausibly because bronze really is level 1.
  That is the fifth time this session a parser has produced a confident wrong
  answer, and the second where the wrong answer was *plausible* rather than
  obviously broken.

  **Blurite is skipped, and the reason is content rather than parsing.** Its row
  carries a footnote: *"smelting blurite bars has a requirement of level 8
  Smithing, attempting to do so with less than 13 Smithing will prompt the
  player that the level requirement is 13 Smithing."* The data says 8, the game
  enforces 13, and **this tree carries 13** — the behaviour rather than the
  table. Automating that would mean teaching a parser to prefer a footnote over
  a column, so it is skipped with the quote in the tool.

  Its experience column is doubled for the same kind of reason — "8 (9.5)",
  because superheat pays 9.5 where a furnace pays 8. The furnace figure is what
  a smelting table wants.

- 2026-08-20 — **D10 the gilded altar — the reason Prayer is trained in a house.**

  This tree buries bones and prays at altars; it did not *offer* bones on one.

  [wiki] "It gives 250% Prayer experience when a bone is used with it. With one
  incense burner lit, it gives 300% ... When both are lit, it gives 350%."

  **+50 per burner from a base of 250** — not a doubling and not a percentage of
  the base per burner. Two burners is 350%, not 500%. And **the base is already
  250 with nothing lit**, so an altar that pays 100% until a burner is lit is
  wrong at the configuration most players actually use. The test rules out both
  misreadings explicitly: it asserts two burners is *not* twice the base and is
  below 500, and that one burner sits strictly between the other two — which a
  boolean "any burner lit" flag could not produce.

  Also pinned: **the "very" belongs to BOTH burners.** "The gods are very
  pleased with your offering" against "The gods are pleased" — one burner lit
  omits it. That word is how a player checks their second burner is still
  alight, so collapsing the two messages removes the only feedback the mechanic
  gives.

  And the burner tier is irrelevant: "Any type of incense burners can be used to
  gain the same amount of experience, **provided that they are lit with
  marrentill**." The herb is the requirement, not the burner.

- 2026-08-20 — **D12 smithing — 158 anvil rows checked against a RULE, not a table.**

  The wiki does not tabulate 159 smithable items. It states the rule:

  > "Smithing experience is calculated by taking the experience granted from 1
  >  bar and multiplying it by the number of bars used. For example, a bronze
  >  platebody is 62.5 experience using 5 bars. 1 bar is 12.5 exp, so go
  >  5 * 12.5 = 62.5. This works for all bars and all smithing items, with a few
  >  exceptions such as cannonballs."

  Checking against the rule is **stronger** than checking 159 values against 159
  cells, because it also catches a wrong bar COUNT: a platebody recorded as
  using 3 bars would have to have its experience wrong by exactly the same
  factor to slip through. Both fields have to be wrong together and
  consistently, which is a much smaller target.

  And the per-bar rate is **derived from the tree's own table**, not typed:
  whatever a metal's 1-bar items award is that metal's rate, and every other
  item in that metal must be a whole multiple of it. So there are no per-metal
  constants to maintain and no second source to drift from — the check is for
  internal consistency against a rule the wiki states.

  **158 rows across 8 metals obey it.** One metal is skipped: gold has no 1-bar
  item in this table, so it offers no rate to derive. Cannonballs are skipped as
  the exception the wiki itself names.

  **D12 is done** — all three of its data tables are under contract.

- 2026-08-20 — **D8 Slayer — most of it was already there, and one gap is 18 monsters wide.**

  Measured rather than assumed, after C2/D1/D8 all turned out to be further
  along than the ledger said:

  * **Task tables are cache-backed** (dbtable 114, 21,912 rows load at boot) —
    the near-miss recorded earlier.
  * **Unlock costs are transcribed from cache dbtable 117** by
    `gen_slayer_unlock.py`, whose own header says "if the two disagree, the
    cache is right". 201 rows.
  * Superior rolling, Konar's location lock and brimstone curve, Krystilia's
    37-task table — all landed earlier today.

  **The real gap: `~slayer_superior_for_category` maps 16 superiors and the
  cache ships 34.** Eighteen superior monsters can never spawn — and they fail
  *silently*, because the 1/200 roll succeeds and then the mapping returns null,
  so nothing appears and nothing is logged. Among them: the dark beast, drake,
  hydra, smoke devil, turoth, pyrefiend and both wyrms.

  `tools/check_superior_coverage.py` now reports the list every build. It
  **reports rather than fails**: closing it means finding each npc's category id,
  which is mechanical but not automatic, and failing the build for a known
  content gap helps nobody.

  Cache *variants* are excluded deliberately and the tool says why —
  `superior_gargoyle_dead` is a corpse, `superior_nechryael_*_spawn` are its
  summons, `superior_cave_crawler_ice` and the `superior_kourend_*` set are
  region reskins. None is a separate superior a roll should pick, and counting
  them would have made the gap look 46 wide instead of 34.

- 2026-08-20 — **D8 superiors — ten wired, eight refused, and a duck.**

  `check-superior-coverage` reported eighteen superiors the cache ships that
  `~slayer_superior_for_category` never named. **Ten are now wired**, taking
  coverage from 16 of 34 to **26 of 34**: aquanite, araxyte, basilisk knight,
  gryphon, lava strykewyrm, pyrefiend, pyrelord, turoth, warped terrorbird and
  warped tortoise. Each category comes from the cache's own `category=` field on
  the base monster's records, all of which agree per monster.

  **Eight are deliberately NOT wired, and the reason is a duck.** Matching a
  base monster by its display name picks the wrong record for every one of them:

  * **Drake** — the only cache record named "Drake" is
    `duck_update_on_land_drake`, category 425. A duck.
  * **Smoke devil** — the only records named "Smoke Devil" are `poh_smoke_pet`
    and its old variant, category **764**, which is the *pet* category, shared
    with Ikkle Hydra.
  * **Hydra** — 13 records across three categories, most of them pets.
  * **Wyrm** — no record is named "Wyrm" at all; the closest are league
    superiors already sitting in category 980.
  * **Dark beast** — two categories, revenant (1189) and crystalline (1391),
    neither the plain slayer dark beast.
  * **Custodian** — only "custodian stalker" records, a different family.
  * **Venator** — two categories across 15 records with nothing to choose
    between them.

  Wiring any of those would spawn a superior off the wrong kill, permanently and
  visibly. They need the base monster identified by the slayer task table rather
  than by name, and the eight are listed in the source with the specific reason
  each one failed — so the next attempt starts from the evidence rather than
  repeating the search.

  This is the same trap as `cache_find.py`'s, inverted: **a display name is a
  reliable way to FIND a record and an unreliable way to CHOOSE one**, because
  ducks, pets and league variants share names with the monsters they depict.

- 2026-08-20 — **D9 Hespori — the lane had the cave and not the boss.**

  `minigame_hespori` was 50 lines: the entrance, the exit and a death hook. The
  fight — what makes the Hespori a boss rather than a plant — was absent.

  * **Four flower buds keep it INVULNERABLE**, not resistant. They are the only
    way through, so treating them as optional adds makes the boss unkillable
    rather than merely harder. The test asserts one surviving bud is as good as
    four: a port that scales damage by surviving buds lets a strong player
    ignore the mechanic entirely.
  * **They open exactly three times — at 100%, 66% and 33%** — and not a fourth
    however low the health goes.
  * **A bud "will die in one hit from any weapon."** Its 10 hitpoints are
    nominal; modelling them as real leaves a weak player unable to open a bud
    the game says they always can. The test asserts a 1-damage hit kills it.
  * **Six clicks break the entangle, not five.** The wiki says five produce the
    "You feel the vines loosen slightly" message and *"after the 5th such
    message, clicking again will prevent the special attack"* — so the sixth is
    what frees you. Off by one and a player who did everything right still eats
    40 damage. The test pins both sides: five clicks still takes the full 40.

  Left **partial**: farming contracts and the seed vault. The contract wiki page
  is a 31-byte redirect, so pinning it needs the real title first — noted rather
  than guessed at.

- 2026-08-20 — **E10 Flower poker — the one Wave E slice that needs no substituted assets.**

  Wave E is NR-custom, so the ladder starts at rank 4 and NR is the
  specification. Flower poker is the exception to the wave's blocker: **mithril
  seeds and every flower colour it plants are ordinary OSRS items this cache
  already ships**, so the mechanic ports exactly as written with nothing
  invented.

  The whole game is `FlowerPokerRank.has(pairsList, pairs, count)`:

  ```
  pairsList.size() == pairs && sum of their sizes == count
  ```

  — a rank is identified by **both** how many colours repeat and how many
  flowers those groups hold, and neither number is sufficient alone:

  * **Four of a kind and two doubles both total FOUR.** One group against two.
    Checking only the total collapses them into one hand.
  * **Four of a kind and a plain double both have ONE group.** Checking only the
    group count collapses those instead.

  The test asserts both collapses explicitly rather than just checking each rank
  maps correctly, because a table that happens to be right for the six cases can
  still be wrong about *why*.

  Two more worth pinning:

  * **Four of a kind beats a full house**, which is real poker's order — worth a
    test because the intuition that a full house is stronger is common and
    wrong.
  * **`bestOf` returns null on equal ranks: a DRAW is its own outcome.** A port
    that breaks the tie by any rule at all — first planted, higher colour —
    invents a winner the game does not name. That is the outcome a port is most
    likely to legislate away.

- 2026-08-20 — **E12 Loot keys — filed under Wave E, but it is real OSRS content.**

  The cache ships `wildy_loot_key0`..`wildy_loot_key3`. This was filed in the
  NR-custom wave because NR implements it; the **wiki is rank 2 and outranks NR
  here**, so it is pinned and ported from the game rather than from Zenyte. That
  also means it needs no substituted assets — the second Wave E slice in a row
  where the wave's supposed blocker turned out not to apply.

  **The rule most likely to be merged into one:** a full inventory and five keys
  already held are *different* outcomes.

  * Full inventory -> "the key will drop to the ground instead" — you still get
    a key.
  * Five keys held -> "the loot will drop to the ground **as if the option was
    turned off**" — **no key at all**.

  A port that treats both as "drop the key" hands out a sixth key the game
  refuses to make. The test asserts the two cases differ, not merely that each
  is handled.

  Two more:

  * **The destroy cap is ABOVE a million, not at it.** A key worth exactly
    1,000,000 can be destroyed in the Wilderness; 1,000,001 cannot. The test
    pins both sides of the boundary.
  * **[jagex] Mod Ash, 27 February 2023:** after a disengagement, the key goes
    to whoever finishes the kill if **more than 30 ticks** have passed, "even if
    player B has dealt a majority of the damage total". Damage majority stops
    deciding once the clock runs out — the opposite of every other kill-credit
    rule in the game, which is exactly why it is worth pinning. Exactly 30 ticks
    does not qualify.

- 2026-08-20 — **E6 Bounty Hunter — the third Wave E slice that needed no substituted assets.**

  `bh_crate`, `bh_hat_tier1`..`tier3` and `amulet_of_bounty` are all in the
  cache. Like the loot keys, this is **real OSRS content filed under Wave E
  because NR implements it**, and the wiki outranks NR.

  That is now three for three, and it retires an assumption I had been treating
  as a blocker for the whole wave: **"NR implements it" does not mean "NR
  invented it"**. Every remaining Wave E slice gets the cache checked before the
  asset question is even asked.

  Four rules pinned:

  * **The target range is a SETTING with three values — 5, 10 or 15 — and the
    default is the TIGHTEST.** A port that hardcodes one gets the other two
    wrong, and hardcoding the widest makes finding a fight far easier than it is
    by default. The test asserts a level-78 target is refused at the default and
    admitted at the medium setting.
  * **The coffer's minimum deposit has four bands** and the boundary belongs to
    the lower one: combat 60 pays 30,000 and 61 pays 50,000.
  * **The five skull tiers are exclusive at the bottom.** "Risking up to
    200,000" is bronze; "between 200,001 and 800,000" is iron — so **the
    boundary coin belongs to the lower tier**. Reading them as inclusive shifts
    every tier by one coin, which the test pins at all four boundaries.
  * **"Within N combat levels" is symmetric** — N above or N below — and
    Teleport to Target needs twelve seconds (20 ticks) clear of combat.

- 2026-08-20 — **D9 seed vault — both numbers came from the cache, not the prose.**

  [cache] `seed_vault` is inv **626, size 104**, and the cache ships exactly
  **eight** `seed_vault_fave*` varbits. So the capacity and the favourite-slot
  count are both facts of the cache rather than readings of the wiki's "up to
  eight types" — and the two agree, which is the cross-check.

  **Three exclusions, two different tests, and that is the whole slice.** The
  wiki: "all common types of seeds and saplings ... **but not seeds or sapling
  given through quests. The seed vault does not store seedlings** that have not
  grown into saplings."

  * A **seedling** is refused for its **growth stage** — it is on its way to
    being a sapling the vault would happily take.
  * A **quest seed** is refused for its **origin** — it is an otherwise
    perfectly ordinary seed.

  A vault that filters on the seed category accepts both; a vault that filters
  on category alone cannot express either. The test asserts the two exclusions
  are independent by refusing a quest *sapling* as well.

  One more that is easy to invert: **Managing Miscellania routing depends on
  what is ALREADY in the vault**, not on whether the seed is storable. "For
  seeds that the player receives that aren't present in the vault, it will be
  placed in their bank instead" — so a storable seed still goes to the bank when
  the vault holds none of it yet.

- 2026-08-20 — **E7 pets — four activities rolled for one and none had anywhere to put it.**

  Agility, the Gauntlet, the Inferno and CoX each have a `*_pet_roll`. None of
  them had a follower system to hand the pet to. **Real OSRS content again** —
  the fourth Wave E slice running where "NR implements it" did not mean "NR
  invented it".

  **Three outcomes, not two, and the third is the one a port loses.** The wiki:

  > "if a player receives a pet while having a follower out ... it will be
  >  placed into their inventory. If a player's inventory is full **and** they
  >  have a follower already, they **will not** receive the pet; it must instead
  >  be claimed from Probita."

  Both conditions are required, and the pet is **not dropped on the floor** — it
  is held for you. A port that drops it loses the pet the moment the player
  walks away. The test asserts the third outcome differs from the second, and
  that a full inventory *alone* never triggers it.

  **Three distinct messages**, one per outcome, and they are how the player
  tells which happened. The duplicate case uses the conditional — "you would
  have been followed..." — and is a different string from a real award's.

  Two Mod Ash rulings pinned:

  * **The pet beats the resource for the last inventory slot** and the resource
    drops underneath the player — **except in Farming**, where "the herb would
    get the slot and the pet roll would not give you the pet". Per-skill, and a
    documented exception rather than an oversight to normalise away.
  * **Reclaims are free.** Pets are auto-insured on receipt; the 500,000 fee and
    the reclaim tokens were both removed. A port from older material charges for
    something the game gives away — and **cats cannot be reclaimed at all**,
    which is a third answer, not "free".

- 2026-08-20 — **F1 the Christmas cracker — the durable half of a seasonal slice.**

  Wave F is NR's **year-specific seasonal events**: their own maps, npcs and
  cutscenes for Christmas 2019, Easter 2020, Halloween 2019. This cache carries
  none of that, and re-authoring a 2019 event is not a port.

  But `christmas_cracker` and all six partyhat colours **are** in the cache, and
  the cracker is a permanent tradeable item with permanent rules rather than an
  event. That is the part worth having, so it is the part that landed.

  Three rules, each with a plausible wrong reading:

  * **[wiki, 8 May 2024] "Both players will now receive rewards."** This is a
    CHANGE — the old behaviour rewarded only the puller, which is exactly what a
    port from older material implements. The wiki records the update because the
    two are different.
  * **The puller CHOOSES the colour and both receive the same hat**, per the
    same update ("players [may] wish for a specific colour partyhat and trinket
    when pulled"). A port that rolls a random colour per player gets two things
    wrong at once — the choosing and the sharing.
  * **"Ironmen cannot use party crackers on other players ... Group ironmen
    however CAN open crackers with members of their group."** Not a blanket
    ban. A plain "is ironman" gate loses the group exception; a plain "is group
    ironman" gate lets one pull on a stranger. The test asserts both halves.

- 2026-08-20 — **D9 farming contracts — the page title I said I had not resolved.**

  I recorded this as blocked on an unresolved wiki title. It resolved in one
  request: **`Farming_contracts`**, plural. Worth the note only because "I
  could not find the page" was doing work as a reason and should not have been —
  the cost of checking was a single fetch.

  Three rules pinned:

  * **The seed-pack tier bands OVERLAP.** Easy pays tiers 1-3, medium 2-4, hard
    3-5 — so **tier 3 is reachable from all three difficulties and a tier does
    not identify one**. A port that maps difficulty to a single tier loses the
    spread entirely. The test pins both the shared middle and the exclusive
    ends (easy never pays 4; hard never pays 1 or 2).
  * **Boosts may be used to ACCEPT a contract**, and "you will not need your
    levels boosted to claim your reward" — so the accept gate reads the CURRENT
    level and the claim is **not gated again at all**. That is the reverse of
    almost every level gate in this tree, where `stat_base` is the correct
    reading; here it would refuse a contract the game allows and then refuse the
    reward a second time.
  * **Cancelling moves DOWN one difficulty** ("Do you have anything easier?"),
    and **easy cannot be cancelled** because it has nowhere lower to go.

  Also recorded: [jagex] Mod Ash, 6 February 2021 — assignment is **equal
  weighting over the eligible set**, not weighted by difficulty and not uniform
  over the whole list. Same shape as Konar's location roll.

  **D9 is done.**

- 2026-08-20 — **F2/F3/F4 — one permanent mechanic underneath four seasonal events.**

  Wave F is NR's year-specific events: 2019/2020 maps, npcs and cutscenes this
  cache does not carry, and re-authoring a 2019 event is not a port. But every
  one of those events ends the same way — **a permanent holiday item that Diango
  replaces forever after** — and that retrieval is year-independent, permanent
  OSRS content. It is the durable half of all four slices at once.

  `[opnpc3,aprilfoolshorsesalesman]` printed "Holiday item returns are not
  available yet."

  **Four places, and they do not behave alike — that is the whole slice.**
  The wiki: "Items will not be displayed if they already exist in the player's
  inventory, equipment, or bank. **If items are stored in a PoH storage, they
  will be removed from there when retrieved here.**"

  * Inventory, equipment and **bank** all HIDE the entry. The bank is the one a
    port forgets, and forgetting it turns a once-per-account item into a
    stackable one.
  * **PoH storage does the opposite**: it does *not* hide the entry, and
    retrieving empties the house instead. Lumping it in with the other three
    makes the item unreclaimable for anyone who tidied it into their costume
    room.

  The test asserts all four cases separately, because the fourth is the one that
  looks like the other three.

  Also pinned: retrieval needs the item **unlocked** first, and every line is
  `sell=0` — reclaiming is free.

  All three rows marked **partial**, honestly: the permanent mechanic is done
  and the year-specific events are not, and no amount of work here changes that
  the cache has no 2019 Halloween map.

- 2026-08-20 — **A bookkeeping error of mine, found and corrected.**

  I have been editing this ledger's rows by matching on the slice NAME and
  writing back a row with an id I inferred. For four Wave E slices I inferred
  the id wrong, and one of those overwrote a different row: my flower-poker
  entry landed on **E10** while the real flower poker row is **E11**, leaving
  two rows with the same name and silently destroying whatever E10 held.

  I could not recover E10's original text — a concurrent session had already
  committed my working tree, so `git show HEAD:` returns my version rather than
  the original. **That row's content is lost**, and the loss is recorded here
  rather than papered over. The duplicate is folded back into E11 and E12 has
  had its NR line count restored.

  The lesson is narrow and worth keeping: **match on the row id, or read the row
  before replacing it.** Matching on a name that appears in more than one place
  is the same class of mistake as matching a cache record by its display name,
  which cost this session a duck, a pet and four false agility defects.

  It also means my running "N of 95" figures counted rows I had touched rather
  than slices I had closed, and were therefore slightly optimistic throughout.
  The table is the authority, not the count.

- 2026-08-20 — **E19 Larran's key — three branches and a negative coefficient.**

  The chests were bound; the key that opens them had no drop rate. [jagex] Mod
  Ash, 19 July 2019, quoted twice and rendered as a piecewise formula:

  ```
  L in (0, 80]    : 1 / ( floor(3/10 * (80 - L)^2) + 100 )
  L in (80, 350]  : 1 / ( floor(-5/27 * L) + 115 )
  L > 350         : 1 / 50
  ```

  **The middle branch's coefficient is NEGATIVE** — its denominator falls as the
  level rises, which is the opposite direction from the first branch's square.
  Writing it with a positive slope makes high-level monsters give keys more
  rarely rather than less, inverting the point of the curve. The test compares
  levels 100/200/350 to pin the direction, not just the values.

  Two boundaries pinned because the wiki states them in prose as well as in the
  formula — a genuine cross-check rather than a restatement:

  * **Level 1 is 1/1972** and **level 80 is 1/100**; both fall out of branch one
    and both are written out in the article's text.
  * **At exactly 350 the linear branch still applies** — 115 − floor(1750/27) =
    51 — and the 1/50 cap begins at 351. The branch end and the cap meet **one
    level apart**, which is the boundary a port collapses.

  Same shape as Konar's brimstone curve and deliberately so, but the join is at
  **80** here against brimstone's **100**, and the constants differ throughout.
  Two similar formulae is exactly the situation where copying the first one's
  numbers looks right.

- 2026-08-20 — **E22 gnome gliders — one site is a destination and never a departure.**

  [cache] `gnome_glider`, `gnome_glidercrashed` and the per-site locs are all
  present. **The sixth Wave E slice that turned out to be real OSRS content**
  behind an NR label.

  **Lemanto Andra is one-way** — "the glider will crash and you cannot fly back
  from here" — so it is a destination and never a departure point. A symmetric
  transport table, every site reachable from every other, cannot express that at
  all; the cache ships `gnome_glidercrashed` for exactly this trip. The test
  asserts the asymmetry in both directions and confirms every *other* pair does
  fly both ways, so a blanket one-way rule fails too.

  **The 153-kudos Varrock Museum gate is a SEPARATE condition** and it does not
  stop the flight — it gates the nearest gate *out of where the glider lands*.
  Folding it into the flight check strands a player who flew there legitimately
  and has no way back by design.

  That pattern — two conditions on one trip, one of which is not about the trip
  — is the same shape as Xamphur's Mark of Darkness (damage *and* corruption
  from one cause) and the loot key's follower-plus-inventory rule. Three slices
  today have turned on separating conditions a port would naturally merge.

- 2026-08-20 — **E22 Death's Office — what happens after the gravestone expires.**

  This tree fills a gravestone and arms its timer. What happens when the timer
  runs out was not here at all.

  **Three boundaries, each inverted by the obvious reading:**

  * **"Items worth less than 100,000 are free"** — so exactly 100,000 **is**
    charged. The boundary belongs to the fee band, not the free one. And the
    test is per ITEM, so a cheap item stays free beside an expensive one.
  * **"ironmen (with the exception of ultimate ironmen) are given a 50%
    discount, giving them a 2.5% reclamation fee."** Two merges to avoid at
    once: the discount is **50% off the FEE** (5% -> 2.5%), not a 50% fee — a
    port reading it the other way charges twenty times too much — and
    **ultimate ironmen are excluded**, so `is ironman` is the wrong test. The
    test asserts the ultimate pays the full fee and that the two ironman cases
    differ.
  * **The coffer takes items worth MORE THAN 10,000** — exactly 10,000 is
    refused — and pays **105%**, which is more than the item is worth. It is a
    deliberate premium, not a sale at value, so a port that pays 100% quietly
    removes the incentive the feature exists for.

  And the rule that actually loses items, stated exactly because of that:
  **no more than 120 items or stacks** may sit in retrieval, "causing items from
  an expired gravestone to be **deleted** when the limit is reached". Deleted —
  not queued, not bounced back to the grave.

- 2026-08-20 — **The eight unmapped superiors, settled: seven are blocked on the cache.**

  I left these needing "the base monster identified by something other than its
  display name". Reading the base monsters' records directly settles it, and the
  answer is not a mapping I was failing to find:

  **`drake`, `smoke_devil`, `hydra`, `wyrm_dark`, `wyrm_light`,
  `mourning_dark_beast` and `kourend_dark_beast` all exist in this cache with no
  `category=` field at all.** `~slayer_superior_for_category` keys on
  `npc_category` and that is the only key it has, so there is nothing to key
  them by — mapping them would mean matching every categoryless npc in the game.
  `custodian` has no non-superior record whatsoever; the only matches are three
  "custodian stalker" npcs, a different monster.

  So seven of the eight are **blocked on the cache, not waiting on work**, and
  `check-superior-coverage` now says so in those words rather than listing them
  as a to-do. A list that reads as unfinished work when it is not is worse than
  no list.

  **The eighth, venator, was a naming artefact of my own making.** I had
  recorded "two categories, 2477 and 2496, nothing to choose between them" —
  that came from matching the display name "Venator", which catches records that
  are not the slayer monster. Matching the SYMBOL instead:
  `venator`..`venator_5` all carry **2477** and agree unanimously. Wired.

  Coverage is **27 of 34**, which is every superior this cache makes mappable.
  **D8 is done** on that basis rather than left open against a number that
  cannot be reached.

- 2026-08-20 — **E16 crystal charges — and the answer to C7's unexplained asymmetry.**

  Two things a shared "charges" model gets wrong:

  * **Armour starts at 2,500 and tools at 10,000**, both capped at 20,000. One
    "starting charges" constant is wrong by four times for one of them.
  * **The triggers are different.** Armour loses a charge per hit *received*;
    tools lose one per item *obtained*. One shared "on use" hook cannot serve
    both — they are damage taken and a resource gained.

  And the rule worth the slice on its own: **the strange device is an exception
  to an exception.** Non-monster damage — divine potions, Wintertodt — does not
  deplete charges, *"with the exception of damage taken from a strange device
  (master) which **does** deplete charges."* A port flattens that into
  "non-monster damage is always exempt" and the device stops costing anything.

  Also: dismantling returns the seed but **loses every loaded shard charge**;
  preserving them would make dismantling a free way to bank charges.

  **And the same article settles C7's open question.** I recorded Meilyr having
  no crystal armour variants and Hefin no weapon variants as an unexplained
  cache asymmetry. The reason is stated outright: *"In their default
  colouration, the armour has the colours of the Meilyr clan, and the corrupted
  weapons have the colours of the Hefin clan."* **Those are the defaults** — the
  base item already is that clan's colours, so there is no recolour variant to
  ship. The generated table was right; I simply did not know why. That entry is
  corrected.

- 2026-08-20 — **A cache probe across every remaining Wave E row, and E14 Clan Wars.**

  Rather than keep guessing which E rows are real content, I probed all
  fourteen remaining ones against the cache. The split is stark:

  ```
  E14 Clans              66 records   (clanwars capes, portals, arenas)
  E9  Magic storage      16 records
  E3  Boons              12 records   (false positive: ToA baboon "Brawler")
  E8  Commands           12 records   (false positive: "Army Commander" npcs)
  E2  Tournament          1 record    (a PvP Arena guide npc)
  E18 Rotten potato       1 record
  E1, E4, E5, E13, E15, E17, E20, E21   -- zero
  ```

  Eight of the fourteen have **no cache record at all**: PvM Arena, Middleman
  trading, Wilderness Vault, Universal shop, Presets, Hiscores, the E20 bundle
  and Donation. Those are genuinely NR-invented or pure server infrastructure,
  which is a different kind of work from everything I have been closing — and
  it is now measured rather than asserted. Two of the twelve-record hits are
  false positives from name matching (a ToA baboon "Brawler", "Army Commander"
  npcs), which is the same trap as always and worth recording so the next reader
  does not chase them.

  **E14 was the clear winner at 66 records, and Clan Wars is real OSRS content.**
  Its distinguishing rule is that **each victory condition carries its own
  unit**: First-to-X is KILLS, King of the Hill and Oddskull are POINTS, and
  **Most kills is MINUTES**. One "target score" field cannot hold that. Most
  kills is also the only condition **decided by comparison at the end** rather
  than by a team reaching a target — a port that treats all four as "first to N"
  never ends that match at all.

  Two more: **re-joining is per-condition** ("may not join or re-join" for
  First-to-X, "may enter at any time" for the others), and **magic has five
  settings where melee and ranging have two** — with "Classic F2P" explicitly
  **excluding Snare**, so it is not simply the free-to-play spell set, and
  "Binding only" allowing Snare while allowing no damage at all.

- 2026-08-20 — **E1/E2 Emir's Arena — the ranked half, and C6's other end.**

  [cache] 22 `pvpa_*` npcs, 164 `pvpa_*` objs, a dozen `pvp_arena_*` interfaces.
  C6 landed this arena's **legacy (unranked) duel**; this is the ranked half NR
  files under PvM Arena and Tournament. One pinned page serves both rows.

  **A LOSS still pays 12 reward points.** The winner gets 16. That is the rule a
  port drops — rewarding only the winner makes the arena unplayable for anyone
  learning it, which is precisely what a 12-point consolation exists to prevent.
  The test asserts a loss pays 12 with a 25-win streak behind it *and* that a
  loss never equals a win, so a shared "reward" path cannot pass.

  **The streak bonus is +1 per win capped at +10**, "for 26 points per win" —
  the wiki states the ceiling so the arithmetic can be checked, and it does. It
  applies to **wins only**: a long streak ending still pays the flat 12.

  **Manually organised tournaments pay nothing.** Two ways into a bracket and
  only one of them rewards; paying both would make a private bracket the fastest
  way to farm points.

  And the bracket itself: **4 to 64 players**, halving each round, which the
  wiki works through for 16 (8 battles, then 4, then 2, then the final) — four
  rounds, and the test uses that worked example rather than my own arithmetic.

- 2026-08-20 — **E15 Presets — the game has its own, and the cache proves it.**

  E15 is filed as NR-custom because NR ships hardcoded tournament loadouts
  (`TournamentPreset.DHAROKS` and friends). But searching the cache for a preset
  turns up **varbit 14541 `toa_preset_selected`**, and the wiki records the
  update: *"Players can now save and load up to **five** invocation presets
  within the invocation selection interface"* (14 September 2022). That is the
  game's own preset feature, it is cache-backed, and it sits in a lane this
  session already owns.

  **The rule worth the slice: a preset holds the invocation PAIR, not a mask.**
  This tree stores invocations as a low/high pair because there are more than
  32 of them — `^toa_var_inv_lo` and `_hi`. A preset that saves one word
  **silently drops every invocation above the split**, and it would look
  perfectly correct in testing because the common invocations live in the low
  word. The test saves a high word with the low word empty and asserts it round
  trips, which is the case a single-mask implementation loses entirely.

  Also asserted: five slots numbered from one, and an invalid slot saves
  **nothing** rather than writing over slot 1.

- 2026-08-20 — **E5 Wilderness Vault — the first Wave E slice that really is NR's own.**

  After six rows where "NR-custom" turned out to mean "real OSRS content behind
  an NR label", this one is the genuine article: the cache probe found **zero**
  records for "Wilderness Vault" or "Queen Reaver", against Clan Wars' 66 and
  the PvP Arena's 186. So the ladder starts at rank 4 and NR's behaviour is the
  specification.

  Ported is the **state machine and its clocks** — the part that carries meaning
  — with the boss and minion npcs (NR ids 16041-16044, absent here) held behind
  one constant each, the same treatment C13's `blood_money` got.

  **Four states and four clocks, and no two clocks are the same length:**

  ```
  INACTIVE -> UNSEALED -> STARTED -> LOOTABLE
  spawn delay  1 hour     (6000 ticks)
  lock         5 minutes  (500)
  event       10 minutes  (1000)
  loot         2 minutes  (200)
  ```

  A single "vault timer" cannot express that, and the relationships are the
  pacing: **the loot window is a fifth of the fight** and **the seal outlives
  the loot window by more than double**. The test asserts all four are pairwise
  distinct as well as checking the values, because collapsing them is the
  obvious simplification and it destroys exactly what the four numbers set.

  One behaviour easy to implement as the wrong verb: **the loot window ends by
  EJECTING players** — "the vault expels everyone" — not by closing the chest. A
  port that only shuts the chest leaves everyone standing inside.

- 2026-08-20 — **E3 Boons — three lists, and two booleans cannot hold them.**

  Genuinely NR-custom, like E5: the cache probe's twelve "Boon" hits were false
  positives (ToA baboon "Brawler" npcs). Perks and remnants do not exist in
  OSRS.

  `BoonPriceTable` lists **84** priced boons, 1,500 remnants to 10,000. Their
  effects are 84 separate pieces of content; what ports as **one rule** is when
  a boon actually applies, and `BoonManager.isBoonActive` has a shape a rewrite
  flattens:

  ```
  isActive(player)
    && (isAlwaysUnlocked(player) || unlockedBoons.contains(boon))
    && !toggleOffBoons.contains(boon)
  ```

  Three consequences, each of which a single "has boon" flag destroys:

  * a boon can be **owned and inactive** — bought, then toggled off;
  * a boon can be **active without ever being bought** — `isAlwaysUnlocked`, so
    a port checking only the purchase list never grants those at all;
  * **toggling off beats always-unlocked**, because the `!toggledOff` term sits
    *outside* the `||`. That ordering is what a rewrite inverts, and the test
    pins it directly.

  Also: **purchase is idempotent** — an owned boon is refused with "You already
  have this boon unlocked", so a double click cannot charge twice.

- 2026-08-20 — **E4 Middleman trading — acceptance is one player's, confirmation is both.**

  NR-custom (no cache record). `MiddleManState` is four states named for **who
  is being waited on**:

  ```
  None -> MakingRequest -> AwaitAccept -> AwaitConfirmation
  ```

  **The last distinction is the whole feature.** Acceptance is the target
  player's alone; confirmation is *both* players', with the offers window open.
  A state machine that folds accept and confirm into one step lets one player
  complete a trade by himself — which is exactly what a middleman exists to
  prevent. The test asserts a trade completes only from the confirmation state
  **and** only with both confirmations, so neither half alone passes.

  And one rule a port loses by storing the obvious field:
  `MiddleManStaffOption.Specific.applies` is

  ```
  staff.hasPrivilege(ADMINISTRATOR) || staff.username.equals(username)
  ```

  — so **naming a specific middleman does not lock an administrator out**. Store
  only a username and the override disappears silently, which matters most in
  the case it exists for: a named middleman going offline mid-trade.

- 2026-08-20 — **E20 Well of Goodwill — a threshold that does not fit an int.**

  NR-custom (no cache record). Players donate into a shared well; crossing a
  perk's threshold activates it **world-wide for two hours**.

  **`WellPerk` is typed `long` for one reason, and it is the reason this slice
  needed care:**

  ```
  +50% Exp              200,000,000
  Double Vote Points    200,000,000
  Double Blood Money    250,000,000
  Double Unique Drops 4,000,000,000L
  ```

  Four billion **does not fit a signed 32-bit int**. Stored raw in this dialect
  it wraps **negative** — and a negative threshold is crossed by a donation of
  zero, so the rarest perk in the game would be permanently active from an empty
  well. The thresholds are therefore held in **millions**, with the unit stated
  in the constant file so nobody "corrects" it back, and the test asserts
  nothing is reached at zero — which is exactly the assertion a wrapped
  threshold fails.

  **Two perks share the 200m threshold**, so crossing it triggers *both*. A
  lookup that maps a donation total to one perk silently drops the second; the
  test counts perks at each boundary (0 -> 0, 200 -> **2**, 250 -> 3,
  4000 -> 4) rather than checking them one at a time.

- 2026-08-20 — **E13 universal shop, and a determination on E17 Hiscores.**

  **E13.** NR-custom: one interface serving every shop's stock. The listing
  model has a distinction a port collapses — **-1 means "cannot", 0 means
  "free"**:

  ```
  val canBuy  get() = buyPrice  != -1
  val canSell get() = sellPrice != -1
  ```

  An item priced 0 is a giveaway the player may take; an item priced -1 is one
  the shop will not trade. Read 0 as "no price" and every giveaway becomes a
  refusal — or, read the other way round, every refusal becomes a giveaway. And
  `ironmanRestricted` is **per item**, not per shop: one stall holds restricted
  and unrestricted stock side by side, which a shop-level flag cannot express.

  The tabbed interface itself needs cache assets NR packs into its own cache
  (`assets/osnr/universal_shop/`), so that half is an asset dependency rather
  than a porting question.

  **E17 Hiscores — determined, not skipped.** `HiScoreTables.kt` is a SQL schema
  (`user_skill_stats`, one column per skill) and an exporter around it. There is
  no game behaviour in it to hold to a source: it reports state this tree
  already maintains. Its one embedded rule is the starting stats — hitpoints at
  level 10 / 1,154 xp against every other skill's 1 / 0 — and **this tree
  already asserts exactly that** at `torirs_server_world.c:25663`. Marked *determined:
  no portable content* rather than left looking untouched, because "there is
  nothing here" is a finding and an empty row is not.

- 2026-08-20 — **E8/E18/E21 staff and donator systems — three tables, three sentinels.**
  These three rows were flagged in §7 as possibly "administration, not game
  behaviour". Two thirds of that is right and one third is not, and the split
  runs through the middle of each row rather than between them.

  **E8.** The 1499 commands are administration and porting a list of them ports
  nothing. The model underneath them is not: `PlayerPrivilege` is a SET, not a
  rank. `eligibleTo` is `inheritance.contains`, and the tree's existing
  `staff_level > 1` (torirs_server_friends.h, inherited from LostCity) is exactly the
  rank shape that gets it wrong. Three places the two disagree:
    * **HIDDEN_ADMINISTRATOR** holds every developer permission while sending
      the client login code 0 — the same code as a plain player — and reporting
      `pMod` false. Rank by login code and the one account whose entire purpose
      is invisible authority has none.
    * **Nobody inherits YOUTUBER.** It sits at ordinal 2, below every staff
      type, and appears in no other privilege's `inherits` list. Under a rank
      port every moderator and administrator silently acquires it.
    * **loginCode is neither unique nor monotone**: MEMBER, YOUTUBER and
      FORUM_MODERATOR all send 2, and TRUE_DEVELOPER sends 69.
  Verified by mutation: replacing `~priv_has` with `$held < $needed` — the
  obvious port — fails the stanza at 3 of 9, on the youtuber step.

- 2026-08-20 — **E18 rotten potato — NONE is not a default, and the click is destructive.**
  Ban/mute/kick are infrastructure. The dispatch is not:
    * The menu is built **per viewer** — `getActions` filters by `eligibleTo`,
      so a support sees three punishments and a moderator sees five, from the
      same item in the same inventory.
    * **Option `NONE` is not an empty menu.** `itemOptionMap` really does hold
      the two npc actions under it; `RottenPotatoItem.handle()` skips the key at
      bind time with a bare `continue`. Filtering the map without reproducing
      the skip counts two entries into a menu no player can open — which is the
      bug my first version of `~potato_menu_size` shipped, and which the stanza
      caught at 2 of 8 before I did.
    * **A click below SUPPORT deletes the item**, rather than refusing. A port
      that renders an ineligible click as a no-op leaves a staff tool sitting in
      an ordinary player's inventory.

- 2026-08-20 — **E21 donator — a chance of 0 is a guarantee.**
  `MemberRank.togglesChance` carries three meanings in one int, and the sentinel
  reading is the opposite of the obvious one:

      if (rate == -1) return false;            // never
      if (rate == 0)  return true;             // always
      return randomNoPlus(rate) == 0;          // one in `rate`

  **UBER and AMASCUT hold 0**, so reading 0 as "no chance" leaves the two top
  ranks as the only ones whose toggles never fire. Same family as the E13
  price sentinel, one value wider. Two more findings in the same row:
    * `formatRate` uses `rate <= 0`, swallowing -1 alongside 0, so the toggles
      menu prints **100% for a rank that fires never**. Transcribed as NR has it
      — it is the only source — but recorded as a disagreement between the label
      and the predicate, not smoothed over.
    * `DonationHandler.claim` threads the **remainder** through inventory →
      bank → floor, each stage receiving only what the stage above could not
      take. "Did not all fit, so bank it" either duplicates the part that fit or
      discards a partial fit.
  Verified by mutation: reading 0 as "never" fails the stanza.

- 2026-08-20 — **E22 imbue — the two tables are not one table read backwards.**
  33 normal/imbued pairs, and three things a "swap the id" port gets wrong:
    * **Charges survive imbuing and are lost un-imbuing.** Imbue builds
      `new Item(imbued, 1, charges)`; disimbue calls `addOrDrop(normal, 1)` with
      no charge argument. One shared helper makes the two directions agree, and
      whichever way it is written one of them is then wrong.
    * **The imbue set and the disimbue set are different sets** — 33 against 10.
      `DisimbueItemAction` filters the bind list by enum NAME, excluding every
      slayer helmet, every black mask, every crystal item and both suffering
      rings.
    * **[cache] The imbued id is not always the higher one.** 32 pairs have
      imbued > normal; the crystal halberd is 13080 against 13091. Not an NR
      slip — the cache confirms it and says why: `13080=nzone_crystal_halberd_new`
      sits in the imbued block 13080..13090, laid down *before* the plain block
      13091..13101. Sorting or id-comparison gets this one entry backwards.
  Also noted, not ported: `IMBUEABLES` is keyed on both ids, so `get(id)`
  answers "which pair is this" and cannot answer "is this imbued" — and
  `DisimbueItemAction` binds "Uncharge" to the plain ids as well as the imbued
  ones, guarded only for the suffering pair.

- 2026-08-20 — **E22 sailing — determined: already ported, from a better source, and NR's row is broken.**
  I wrote a charter lane from `CharterLocation` and the compiler refused it:
  `^charter_brimhaven` was already declared by `transport_charter/`. That lane
  is 16 ports and 228 fares generated from the wiki's own fare template
  (precedence rank 2) against NR's 11 ports (rank 4). Deleted mine.

  Two findings worth keeping from the comparison:
    * **NR's Mos Le'Harmless row lists TEN fares where every other row lists
      eleven.** `CharterLocation(final Location, final String, final int... costs)`
      takes a short row without complaint, so this is an out-of-bounds read
      waiting on the first player who asks that desk for Prifddinas. Which index
      is missing cannot be recovered — the matrix is asymmetric elsewhere by
      design, so the absent fare cannot be inferred from its mirror.
    * **NR's asymmetries are two data sources blended.** Brimhaven→Karamja 480
      against Karamja→Brimhaven 200; Brimhaven→Port Khazard 400 against 1600
      back. `charter_fare.dbrow`'s own header already diagnosed exactly this
      pair set — "Musa Point to Brimhaven 200 vs 480, Karamja Shipyard to Port
      Khazard 720 vs 1600" — as the shortest-path tsv disagreeing with the wiki,
      and chose the wiki. NR took some cells from each. Our Mos Le'Harmless ↔
      Prifddinas is 2475 both ways; NR's one surviving direction is 4950,
      exactly double, the same doubling the header records for Sunset Coast.

- 2026-08-20 — **E22 ground items — determined: the model is already here, and the guard over it was wrong.**
  NR's `FloorItem.isVisibleTo` turns on three things: `invisibleTicks <= 0`
  makes a pile public unconditionally, a null `receiverName` makes it public
  subject to the ironman flag, and otherwise only the named receiver sees it.
  This tree already implements all of that — `ToriRSServer_WorldObjAddPrivate`
  with a private window whose header states the same rule ("A non-positive
  private window is the same as obj_add"), and `receiver_pid` gating
  `ToriRSServer_WorldGroundVisibleTo`. Owner and receiver are already separate.
  Nothing to port. NR's only addition is `visibleToIronmenOnly`, which is a
  temporary restriction — it is bypassed the moment the window expires — and
  has no counterpart here because this tree has no ironman-only drops.

  **Engine fix taken while reading it.** `ToriRSServer_WorldGroundVisibleTo`
  opened with

      if( slot < 0 || slot >= TORIRSSERVER_GROUND_MAX )
          return 0;

  which is the shape CLAUDE.md forbids: a bad index returns "you cannot see
  it", and every caller treats a false as skip-this-obj, refuse-the-take, or
  omit-from-the-zone-flush. A caller bug would have surfaced as a pile that
  silently never appears. Replaced with asserts on `srv` and both bounds. The
  guard was also not protecting the one path that looks riskiest —
  `torirs_server_zone.c:1390` indexes `srv->ground[zone->objs[i]]` on the line above
  its call, so a bad index is already UB before the check runs. No assert fires
  across the full selftest: every call site passes a loop index or a validated
  lookup, which is what the guard was hiding.

- 2026-08-20 — **E9 magic storage unit — and a correction to why I had left it.**
  I said last session that E9 was the Chambers of Xeric storage unit and that
  the concurrent session's work in that lane was a reason to hand it over. That
  was a misidentification: E9 is `com.zenyte.game.content.magicstorageunit`,
  costume storage in the pre-Construction era, and the CoX storage unit is a
  different class in a different package. Nothing about it touched the CoX lane.

  Four rules, one of them a defect in the reference:
    * **Completeness is all-of-pieces over any-of-ids.** A `StorableSetPiece`
      is a bag of interchangeable ids — one slot in several colours or charge
      states — and `containedCount == 0` fails only that piece. Flattening the
      two levels breaks it in whichever direction: "all ids" demands every
      variant at once, "any id" stores a set on the strength of one boot.
    * **Every held id is banked, not one per piece.** `list.add(id)` sits inside
      the loop over the piece's ids, and withdrawal returns all of them.
    * **The ultimate ironman unlock is the CHEAPER rate** — 1,000,000 against
      2,500,000. The usual direction for an ironman branch is a surcharge, and
      the refund path reads the amount actually paid, so getting the two
      backwards pays out the wrong refund rather than just charging wrong.
      Varbit 16001 is `unlockPayment == 0 ? 0 : 1`, a flag derived from an
      amount — it cannot answer the refund on its own.
    * **[ours] NR's `store()` never deletes the items.** The put into
      `storedSets` is live; the inventory deletion below it is inside a block
      comment, while `MagicStorageInterface:71` adds every stored id back on
      withdrawal. Store a set, keep the items, withdraw the set: two copies.
      Corrected here rather than transcribed, and recorded as a correction.

  **A gap in my own stanza, found by the mutation check.** Deleting the middle
  piece's test from `~storage_set_complete` did not fail the suite: every
  incomplete case I had written was missing the first or last piece, so the
  middle one was never exercised alone. Added `(1,0,1)` and `(0,1,1)`; the
  mutation then fails as it should. Worth stating because the stanza looked
  thorough — six cases over three pieces — and still pinned nothing about the
  piece in the middle.

- 2026-08-20 — **B3 wilderness bosses — the numbers the stubs deferred.**
  The dens, entry fees and exits were already here; the three fights were
  `ai_queue3` counters printing "(Specials / loot deferred)". The specials are
  now stated and under contract. What a port collapses, in each fight:
    * **Callisto's style is not a three-way roll.** `isWithinMeleeDistance` is
      checked first and returns immediately, so in melee range he melees on the
      roll that would otherwise have chosen magic. Rolling the style first
      produces a boss that shoots at point-blank range. Verified by mutation.
    * **Artio is weaker at range and identical in melee** — 40/40 against
      Callisto's 55 ranged / 60 magic, but both melee for 55. A single "the
      weak variant hits for X%" factor gets the melee row wrong.
    * **Only the magic attack knocks back, and Protect from Magic stops it.**
      The knockback carries its own flat 3 damage on top of the spell's.
    * **A living hellhound blocks Vet'ion's damage AND his experience.** One
      predicate, two effects — `hit.setDamage(0)` and
      `getXpModifier() { return isDamageable() ? 1 : 0; }`. Zeroing only the
      damage still pays out for hits that did nothing.
    * **Calvar'ion never scales his summon.** Vet'ion is `min(2 + players - 1,
      25)`; the weaker variant is a flat 2 whatever the room holds.
    * **Venenatis' melee maximum is `21 + 2 * spiderlings alive`.** Reading the
      base and dropping the term gives a boss that never gets more dangerous
      for leaving its spawns up, which is the only reason to kill them.
  [cache] Hitpoints cross-checked rather than taken from NR: `callisto`
  stat4=1000 (NR hardcodes 1000 and the cache agrees), Artio 450, Vet'ion and
  Calvar'ion 255/150 **per phase**, Venenatis 850, Spindel 515.

  **[ours] NR's Callisto phase guard oscillates.** It reads

      if (hp% < 66 && phase != TWO)      { howl(); phase = TWO; }
      else if (hp% < 33 && phase != ONE) { howl(); phase = THREE; }

  Once phase is THREE the first branch is live again — `phase != TWO` holds —
  so it drops back to TWO and howls, and the next hit promotes it to THREE and
  howls again. Below 33% Callisto howls on *every* hit, and the comment above
  that block says a howl "resets his freeze and attack timers". The second
  guard was meant to say `phase != THREE`. Phase THREE is also never
  behaviourally distinct: the sole read is `phase == TWO || phase == THREE`.
  Ported as a function of hitpoints, so it is monotone and cannot oscillate.

  **Not done, and the row says so:** the per-tick AI wiring. Projectile
  dispatch, trap loc spawning, live hellhound summoning and the top-ten damage
  split are still absent; the kill handlers remain counters. This slice is the
  arithmetic those handlers will need, not the fight.

- 2026-08-20 — **B15 is B10. Folded rather than worked twice.**
  The original plan lists Rise of the Six as both B10 (895 lines) and B15 (870,
  `content/rots/`). NR has exactly one `com/zenyte/game/content/rots` package.
  B15 is marked folded; the count of outstanding rows drops by one for free,
  which is worth more than a second implementation of the same encounter.

- 2026-08-20 — **B10 supply chest — a `+ 1` that means "noted".**
  Three findings in a 36-line table:
    * **[cache] `ItemId.X + 1` is the NOTED form.** Nine of the twenty entries
      are written that way — bones, four herbs, two foods, three potions —
      and every one is meant to arrive noted. Verified here rather than assumed:
      `536=dragon_bones` / `537=cert_dragon_bones`, `2998=toadflax` /
      `2999=cert_toadflax`; `cert_` is this cache's prefix for a note.
      Transcribing the constant name and dropping the `+ 1` hands over 25
      unnoted dragon bones and 40 unnoted manta rays, which will not fit in an
      inventory beside the rest of the chest.
    * **The table is uniform.** `MysteryItem(int id, int weight)` resolves to
      `this(id, 1, 1, weight)`, so the two-argument entries are quantity-one at
      weight one, and every four-argument entry passes 1 as its weight too. The
      dragon med helm and both key halves are exactly as likely as coins. The
      trailing 1 reads naturally as a quantity — which is what it means in the
      four-argument form's *second* slot — and reading it that way turns a flat
      table into a weighted one.
    * **The 1.5 modifier truncates.** `(int)(2889 * 1.5)` is 4333, not 4334.
      Three of the seven scaled rows land on a half — mind runes, chaos runes,
      bolt racks — and the other four divide evenly, which is what makes the
      three easy to miss. Verified by mutation: rounding fails the stanza.

- 2026-08-20 — **B16 Skotizo minions — two spawners that are secretly one.**
  The altar damage reduction was already here; this is the spawning half the
  entry stub deferred.
    * **The ankou branch and the demon branch are `if` / `else if` on the ankou
      timer.** A tick on which the ankou timer is due suppresses the demon check
      entirely — even when no ankou spawns, because one is already alive. Two
      independent-looking spawners are one, and the ankou has priority. Verified
      by mutation: making them independent fails the stanza.
    * **The ankou timer resets whether or not an ankou spawns.**
      `resetAnkouSpawnDelay()` precedes the count check.
    * **A demon wave tops the count up to three in one go** —
      `for (i = count; i < 3; i++)` — so an empty field spawns three, not one.
      Spawning one per wave takes three waves to reach a state NR reaches in one.
    * **The taunt and the 30-second clock both fire on a wave that spawns
      nothing.** `setForceTalk` and the delay assignment precede the
      `if (count >= 3) return;`, so a full field still silences the spawner.

- 2026-08-20 — **B17 dark energy core — the half of the fight that heals the boss.**
  `corp_damage.rs2` already had the beast's damage reduction. The core is the
  other half, and it hangs on values that read the wrong way:
    * **`Utils.random(i)` is `nextInt(i + 1)` — INCLUSIVE.** NR carries both
      forms; `randomNoPlus` is the exclusive one. So `Utils.random(7) == 0` is
      one chance in EIGHT, and `Utils.random(1, 13)` is 1..13 inclusive —
      thirteen outcomes. Reading `random(7)` as exclusive makes the core appear
      14% more often than it should.
    * **The spawn needs a hit ABOVE 32**, exclusive — `hit.getDamage() <= 32`
      returns. A hit of exactly 32 never spawns it.
    * **The heal mirrors the damage.** One roll, used twice:
      `applyHit(amount)` then `corporealBeast.heal(amount)`. Rolling the two
      separately, or healing a flat figure, breaks the only reason the core is
      worth killing.
    * **The siphon hits every adjacent player separately**, so a core sitting
      between three players returns three times as much to the beast.
    * **Poison stuns the core exactly once per fight.** A stunned core does not
      siphon — but its next jump clears `stunned` AND `canBeStunned`, and the
      latter never comes back. Modelling the stun as a repeatable status makes
      the core permanently harmless.
    * [wiki] A core killed in mid-air does not return for the rest of the kill
      (Dwarf multicannon page); NR carries the same idea as `canRespawn`.

  **Recorded, not silently resolved:** `^corp_core_leech = 5` already existed in
  `corp.constant`, tagged `[wiki]`, but the wiki text quoted beside it states no
  number and the constant is referenced by no script. NR says the siphon is
  `Utils.random(1, 13)`. I left the existing constant alone and wrote the range
  as its own pair rather than overwrite a value whose provenance I could not
  establish. Someone should settle which is right.

- 2026-08-20 — **A false pass, and how it was caught.**
  `selftest_corp_core` reported green on its first run and then survived a
  mutation that should have failed it. The row was not in the binary at all: my
  edit to the registration table in `torirs_server_world.c` was clobbered between the
  write and the build — the concurrent session is active in that file, and this
  is the `concurrent-session-commits-your-tree` hazard showing up as a *missing*
  edit rather than a conflicting one. An unregistered stanza cannot fail, so it
  reads exactly like a passing one.

  The mutation check is what caught it: a green run proves nothing on its own,
  and "the mutation did not fail" was the signal that sent me to look. Worth
  noting the checker could not have caught this either — it verifies that no
  FAIL line matches an owned expectation, and an absent stanza emits no line.
  After re-registering, the mutation fails at "got 0 of 10" as it should. I
  audited all nine rows added this session; the other eight were present.

- 2026-08-20 — **Engine/tooling: `tools/check_selftest_registration.py`, and six dead tests it found.**
  Written in response to the false pass above, because that failure mode is
  invisible to everything else we have. A stanza that is never registered
  cannot fail, so it reads exactly like a passing one:
    * the C loop's `SELFTEST_CHECK(script != NULL, ...)` fires for a proc that
      IS in the table but missing from the pack — the opposite direction;
    * `trail_selftest_check.sh` asks whether any FAIL line matches an owned
      expectation, and a stanza that never ran emits no line.
  So the check runs from outside: enumerate every `[proc,selftest_*]` in the
  tree and require each to be either named in a `k_*` table in
  `src/torirsserver/*.c` or called as a helper with `~name`. Wired into
  `torirsserver-scripts`, so it gates every build rather than being a thing to
  remember. Verified by simulating the exact failure — renaming the
  `selftest_corp_core` table entry makes it report the stanza as running
  nowhere.

  **It immediately found six stanzas that had never run.** Five of them are the
  herblore port's — venom raise-don't-stack-down, venom immunity, disease,
  and both decant cases — sitting in `selftest_herblore.rs2`, compiled into
  every pack and executed by nothing. All five are orphaned together, which
  points at a single lost registration edit rather than five oversights: the
  same accident that hit me today, committed at some earlier point and never
  noticed. **Registered, and all five pass** — they were correct code that had
  simply never been run, which is the quiet version of this failure. They use
  that file's own convention rather than a step count (a distinct failure code
  in the 509x range at each guard, a success code in the 500x range at the
  end), so the table's `expect` is the success code.

  The sixth, `selftest_npc_mode_none`, writes no `%mock_quest_progress` at all
  and so has no success code to register against. Left recorded in the tool's
  KNOWN_DEAD list with a note rather than deleted or given an invented code —
  `defaultmode=none` is already covered by `defaultmode-none-was-a-noop.md`, so
  it may be redundant rather than missing, and that is someone's call to make.

  Two stanzas are legitimately unreachable and the tool knows why:
  `selftest_curses_sa_energy_scale` lives in `ported_rs558_ancient_curses`,
  which is a compiled-off lane.

- 2026-08-20 — **B13 Mage Arena II — the base class that decides which attack happens.**
  The three bosses' own specials were already here; `MageArenaBossBase` is what
  picks between them, and it is built out of the same traps as the rest of NR:
    * **The special pool is built from what the target's state still ALLOWS,
      then one is drawn uniformly from that pool.** Teleblock is offered only
      if the target is not already teleblocked, the bind only if not frozen,
      the boss's own only if ready. So each special's probability depends on
      how many others survive the filter: against a target already teleblocked
      and frozen, the boss's own special is CERTAIN on a special turn; against
      a fresh one it is one in three. Rolling the type first and then checking
      whether it applies produces neither distribution.
    * **`Utils.random(4) == 0` is one turn in FIVE.** Same inclusive-random
      trap as the Corporeal Beast's core spawn, in a different file.
    * **`meleeFrequency()` of 0 means ALWAYS, not never.** `Utils.random(0)` is
      `nextInt(1)`, which can only return 0, so Justiciar Zachariah — whose
      override returns 0 — melees on every turn he is in range, while Derwen
      and Porazdir at 2 melee one turn in three. Reading 0 as "no chance" gets
      the one boss built around melee exactly backwards, and an exclusive
      `nextInt(0)` would throw instead of returning anything. This is the third
      time this session that a 0 has meant "always" in NR — the donator toggle
      chance and the universal shop price were the others. Verified by
      mutation: reading it as "never" fails at 5 of 11.
    * **Melee range alone does not force a melee here** — the opposite of
      Callisto in slice B3, who short-circuits to melee whenever he can reach
      you. Two bosses in one tree with opposite answers to the same question,
      so neither can be inferred from the other.
    * **The splash threshold moves with the prayer**: `damage <= (protect ? 1 : 0)`,
      so a hit of 1 splashes against Protect from Magic and lands without it.
      A fixed `damage == 0` test cannot express that.

  **A value conflict left standing rather than resolved.** NR's teleblock is
  200 ticks, halved to 100 under Protect from Magic, with a separate immunity
  of 300/200. The tree already carries `^ma2_teleblock_ticks = 100` tagged
  `[wiki]` from "can also cast a Tele Block lasting one minute". The two agree
  only for a praying player. The wiki outranks NR, so the existing constant
  stays the one the content uses; NR's pair is recorded beside it under
  `^ma2_nr_*` names for the structure it carries — two timers, the immunity
  outlasting the block, one prayer halving both — which the tree did not have
  either way.

- 2026-08-20 — **B14 Xamphur's phantom hands — the half of `handleIngoingHit` that was missing.**
  The tree already had Xamphur's corruption effect and his magic immunity. The
  immunity was ported as its own rule, but in NR it shares a branch:

      if (hit.getHitType().equals(HitType.MAGIC) || handNpcs.size() > 0)
          hit.setDamage(0);

  **A living hand blocks melee and ranged too** — the same shape as Vet'ion's
  hellhounds in B3, and the third boss this session whose real gate is "kill
  the adds first". Porting only the magic half — which is the half the wiki
  describes — leaves the hands as decoration and the fight with no gate at all.
  Verified by mutation: dropping the hands condition fails at 0 of 7.

  Four smaller rules with it:
    * **Hands spawn as a pair**, left and right, two tiles either side of the
      middle. One hand is never spawned alone, so a "spawn a hand" that
      produces one leaves the fight beatable at half the intended cost.
    * **25-tile reach** — `attackDistance`, `maxDistance` and
      `aggressionDistance` are all 25, far past a normal npc's, so no corner of
      the arena is out of range.
    * **Corruption needs a hit that landed**: `if (hit.getDamage() > 0)`. A
      blocked or splashed hand attack does not corrupt, so the corruption clock
      is a function of damage taken and not of attacks received.
    * The "your hit is ineffective" message is a **separate `if`**, not the
      else of the damage branch, so a magic hit with no hands alive is zeroed
      in silence with no explanation offered.

- 2026-08-20 — **B23 Nex parity sweep — the audit, and the two gaps it found.**
  The row guessed "ours may already exceed" and told me to audit rather than
  rewrite. Both halves of that turned out right, and the row's own number was
  wrong.

  **The 4190 figure is not NR's Nex package.** The whole
  `plugins/excluded/boss/nex` tree is **1934** lines, of which `NexNPC.java` is
  1491. Ours is 1228 Nex-specific lines across `godwars_nex.rs2` (968) and
  `godwars_nex_drops.rs2` (260). Wherever 4190 came from, it was not this; the
  row has been corrected rather than left to imply a 3.4x shortfall.

  **The sweep.** I took the method inventory off `NexNPC` — 23 distinct
  mechanics — and checked each against our lane by its distinctive term rather
  than by a fuzzy name match, which was giving false positives on words like
  "dash" and "attack" that appear everywhere. Result: **20 present** — smoke
  dash and drag, choke, shadow embrace and smash, blood siphon, sacrifice and
  reavers, ice prison and stalagmites, zaros auto, wrath, turmoil transfer,
  soul split, style swapping, the HUD, power drain. **Three absent**, of which
  two are real:
    * **Ice containment ("Contain this!")** — absent entirely; "containment"
      appeared zero times in the lane. It is anchored on NEX, not on a player:
      the box is her footprint grown one tile on every side, so 5x5 for a
      size-3 Nex, unlike the ice prison which follows a target. Anyone inside
      **loses their overhead protection prayer** — a deactivation, not a
      one-hit bypass, so the stalagmite volley that follows finds them
      unprotected too. The hit is `Utils.random(60)`, inclusive 0..60, and the
      10-tick stun and 10-tick movement lock land whatever it rolls: a
      containment that dealt zero damage is still the one that kills. Verified
      by mutation — starting the box at her own corner instead of one tile out
      fails at 1 of 7.
    * **The stuck watchdog** — absent. It is gated on stage AND style
      (`stage.isAutoAttack() && getCombatDefinitions().isMelee()`), because
      outside a melee auto stage a long gap between attacks is normal; an
      ungated watchdog dashes Nex home in the middle of her own specials. The
      threshold is three of her CURRENT attack speeds, so it scales with the
      phase rather than being a fixed tick count.
    * **`increaseAttack` is a non-gap.** Its only real behaviour — switching
      target every third attack — is commented out in NR, with a `//TODO
      target switching proper`. Nothing to port; recorded so the next sweep
      does not re-flag it.

- 2026-08-20 — **B24 Tormented Demons reconcile — ours is ahead, and NR has a De Morgan bug.**
  The row's premise was stale. It says "ours is an rs2012 backport, NR's is
  OSRS" and asks me to reconcile them. We have **both**: the Old School demon
  in `bosses/boss_tormented_demons/` (~1500 lines, built 2026-08-13) and the
  revision-727 one in `areas/area_rs2012_tormented_demons/` (~1300). NR's whole
  contribution is **345** Kotlin lines, not the 896 the row claims.

  **Nothing to port.** Ours is wiki-sourced and covers what NR does not — the
  unshielded damage bonus (X squared minus 16), the six-tick prayer-switch
  stall, the four-step damage ordering (defencelessness, then prayer, then
  shield, then the counter), the fire bomb, the consumables. NR's only items we
  lack are `isTolerable = false` and a 25-tick accuracy-boost ticker, both of
  which our lane handles differently. Cannon immunity we deliberately do not
  model, and the existing header says why: this tree has no cannon that deals
  damage, so the branch would be unreachable, and an unreachable guard reads as
  a live rule.

  **[nr] The fire-shield exemption is inverted in NR:**

      if (weapon is Item && (!weapon.isDemonbaneWeapon || !weapon.isAbyssalWeapon))
          hit.damage = (hit.damage * 0.8).toInt()

  `(!a || !b)` is `!(a && b)`, so the 20% reduction is skipped only for a
  weapon that is BOTH demonbane and abyssal. Every ordinary demonbane weapon —
  arclight, emberlight, the purging staff — is still reduced, and the exemption
  never fires for the weapons it exists for. The intended form is `!(a || b)`.
  Ours already has the correct disjunction; I pinned it so a refactor cannot
  drift into the same shape. Verified by mutation: rewriting our `|` as `&`
  fails at 0 of 6.

  Note the shape of that defect — under NR's form the ONLY case that behaves
  correctly is a weapon that is neither demonbane nor abyssal, which is what
  most testing uses. That is why it survives.

  **A coverage hole of the same family as the registration one.**
  `td_selftest.rs2` holds 286 lines of real assertions, but behind
  `[debugproc,tdtest]` — they run when someone types the command and never
  otherwise. `check_selftest_registration.py` cannot see this either: it looks
  for `[proc,selftest_*]`, and these are helpers called from a debugproc. The
  pure subset — the checks needing no live demon — is now a registered stanza;
  the live-npc assertions stay in the debugproc where they belong. Worth a
  sweep for other debugproc-only test files.

- 2026-08-20 — **A second shape of unrun coverage: debugproc harnesses C never names.**
  Found while reconciling B24. `td_selftest.rs2` holds 286 lines of assertions
  behind `[debugproc,tdtest]`, and `check_selftest_registration.py` could not
  see it — the tool looks for `[proc,selftest_*]`, and these are helpers called
  from a debugproc. Some debugproc harnesses ARE driven, by
  `ToriRSServer_ScriptsRunDebugproc(srv, "name")` from C. C names some and not
  others.

  **My first count was wrong and I want it on the record.** Matching the
  bracketed `"[debugproc,name]"` spelling reported 124 unrun files including the
  CoX suite — which I had watched produce FAIL lines all session. C uses the
  bare name, `run_debugproc(srv, "coxrun")`. Matching the wrong string turned a
  real 75-file finding into a false 124-file alarm; the tool now matches the
  form C actually uses and reports the list as a review item, not an error.

  **Two of the unrun harnesses were substantial**: `::runecraftrun` (24
  assertions) and `::miningrun` (13). Neither had ever executed in a build.
  Wired both in — and both fail.

  **They fail for a harness reason, not a content one, and establishing that
  took a wrong turn worth recording.** `::runecraftrun` reports
  `rc_no_tally_required_air did not flip to 1 with an air tiara worn`. I read
  the test, saw it equip with `inv_add(worn, tiara_air, 1)` — which fills the
  first free slot rather than the hat slot `rc_refresh_ruins_varbits` reads —
  and rewrote it to `inv_setslot`. It still failed. I had justified that edit by
  checking that `gear_selftest.rs2` uses `worn` 65 times and is driven from C,
  concluding `worn` must work in this harness. **`::gearrun` is gated behind
  `TORIRSSERVER_GEARRUN` and is skipped by default** — so it is not evidence that
  `worn` works here; it is evidence of the opposite, and the gate exists for
  precisely this reason. I reverted my unverifiable edit to the test rather than
  leave a change I could not show was an improvement.

  Both are now wired the way `::gearrun` is: driven behind `TORIRSSERVER_RUNECRAFTRUN`
  and `TORIRSSERVER_MININGRUN`, skipped otherwise. That keeps the default build
  clean — these are pre-existing failures and painting a harness limit as a
  content bug on every build helps nobody — while making them one env var away
  from running. **What is still open** is why the plain selftest player cannot
  carry equipment, which is the thing blocking three harnesses (gear, runecraft,
  mining) rather than any one of them.

  74 further debugproc-only harnesses remain, mostly quests with 2-6 assertions
  each. The tool lists the top ten every build.

- 2026-08-20 — **C18 area plugins — two of the three were already done, and I nearly missed it.**
  **Taverley.** My first check grepped the tree for `strange_floor` and
  `jump_rock` and found zero files, which reads as four missing agility
  shortcuts. They are all present: `agility_shortcuts.enum` is generated from
  the pinned wiki and keys rows by DISPLAY NAME — "Strange floor", "Obstacle
  pipe", "Loose railing", "Climbing rocks" — not by snake_case symbol. This is
  exactly the trap `tools/cache_find.py` was written for after making it three
  times, and I made it a fourth. The generator carries 162 shortcuts and needed
  nothing.

  **Waterbirth.** NR's `WaterbirthDungeonCrack` is the Dagannoth Kings entry,
  which this tree already has.

  **TzHaar — one real gap.** `TzHaarKetKeh` sells an Inferno practice mode:
  start at any wave for 1,500,000 coins, and the run cannot award the cape. We
  already had the practice FLAG (`%inferno_practice` suppressing the win in
  `~inferno_leave`) but no fee at all. The rule worth having is where the money
  comes from:

      (mode == ULTIMATE_IRON_MAN && player.getInventory().containsItem(fee))
      || (mode != ULTIMATE_IRON_MAN && player.getBank().containsItem(fee))

  **An ordinary account pays from the BANK; an ultimate ironman pays from the
  INVENTORY** — they have no bank to draw on. Neither single-container reading
  is safe: reading the bank for everyone lets a UIM with banked coins through
  and refuses one carrying them, and reading the inventory for everyone does the
  mirror. Both directions are asserted. Verified by mutation: collapsing to the
  bank alone fails at 1 of 5.

  That is the second NR feature this session where the ultimate ironman branch
  is the unusual one — the magic storage unit's cheaper unlock was the first.
  Worth expecting rather than being surprised by in the rows that remain.

- 2026-08-20 — **D14 AFK skilling — a ladder that is not monotone, and the fall-through nobody reads.**
  Ten AFK skills share one base class, so every rule is ten rules.
    * **The experience per action falls as the server's xp rate RISES** — 4.8
      at rate 5..9, 2.75 at 10..19, 2.0 at 20..49, 1.5 at 50+. Counter-
      intuitive but consistent, and easy to port as a plain descending ladder.
    * **A rate BELOW 5 falls past every branch to a final `return 1.5`** — the
      same figure as the 50+ band, not the 4.8 the ladder's shape argues for.
      So the curve rises from 4 to 5 and falls thereafter, and rate 1 and rate
      50 award the same. Dropping the fall-through hands rates 1..4 the peak
      band: a 3.2x error on exactly the servers running closest to Old School
      rates. Verified by mutation — returning the 5..9 figure instead fails at
      2 of 8.
    * **The x2 xp multiplier is not a bonus** — it applies to everyone,
      unconditionally, before the "No One's Home" boon doubles it again. Two
      multipliers that look alike and are not.
    * **`getSkillCap()` refuses a level strictly ABOVE the cap, and -1 disables
      the check.** The default is 99, which a normal account cannot exceed, so
      the default cap never refuses anybody and -1 and 99 are indistinguishable
      in practice — but they are different values and an override could set a
      real one, so the sentinel has to survive.
  Points are `Utils.random(1, 3)`, inclusive, doubled by any of THREE sleeping
  cap ids in equipment.

  **Not done:** the fletching/cooking/fishing/herblore parity sweep, which is
  the other half of this row.

- 2026-08-20 — **The C server tree was renamed mid-turn; what that cost and what it did not.**
  The concurrent session committed `e9284bf4 rename`: `src/net/mock/` became
  `src/torirsserver/`, `mock230_world.c` became `torirs_server_world.c`, and the
  make targets moved from `mock230*` to `torirsserver*`. My first sign of it was
  a python edit failing with `FileNotFoundError` on a path that had existed
  minutes earlier.

  **All fourteen selftest registrations I had added survived**, because the
  rename was a `git mv` that carried the working tree with it — I checked each
  by name rather than assuming. What did NOT survive was
  `tools/check_selftest_registration.py`, which I wrote this session with
  `src/net/mock` hardcoded; it now tries `src/torirsserver` first and falls back,
  so it works either side of the rename and in a worktree that predates it.

  Also worth recording because it produced a confusing result an hour earlier:
  `tools/trail_selftest_check.sh` was edited to grep for `ToriRSServer selftest:`
  while the binary still emitted `mock230 selftest:`, so it reported NOT RUN for
  sections that had run. That half of the rename landed before the C half. I
  left their script alone and verified my rows by grepping the log for their own
  FAIL text directly, which is what I would recommend to anyone reading a red
  checker during a rename.

- 2026-08-20 — **A8 Grand Exchange — the matching engine.**
  The part of the GE that decides who trades with whom and at what price. Five
  rules, each of which a port gets wrong in a different way:
    * **The trade executes at the SELLER's price, and the buyer is refunded the
      margin.** `exchangePrice = sellOffer.getPrice()`, then
      `returnedAmount = (qty * buyOffer.getPrice()) - seller_got`. A buyer who
      offers over the asking price does not pay it. Executing at the buyer's
      price is the obvious reading of "they agreed to pay that" and silently
      overcharges every buyer who set a margin. Verified by mutation: it fails
      at 3 of 11.
    * **The refund is measured against what actually REACHED the seller**, not
      the nominal total — `result.getSucceededAmount()`. If the seller's
      collection box could not take all the coins, the shortfall returns to the
      buyer rather than vanishing. That couples two containers that look
      independent.
    * **Priority is produced by a sort written in the OPPOSITE order, inverted
      by a backwards walk.** Buying sorts price descending and walks from the
      end, giving the cheapest seller first; selling sorts ascending and walks
      from the end, giving the highest-paying buyer first. Keeping the sort and
      tidying the loop into a forward walk yields exactly the WORST match first,
      in both directions.
    * **Ties go to the older offer** — the comparator's second key is time
      descending, which the backwards walk inverts again. Two inversions in one
      comparator is what makes this worth pinning rather than re-deriving.
    * **Compatibility is inclusive at the boundary** (`price < o.getPrice()`
      rejects, so equality matches) and **a player never matches their own
      offers** — the matcher skips the whole username entry before looking at
      any price.
  Also noted: `OFFER_TIMEOUT_DELAY` is 7 days, and a stale offer is skipped by
  the matcher without being cancelled — it keeps its slot and still shows on the
  player's screen.

  **Not done:** the offer slots, the interface and the price index.

- 2026-08-20 — **A4b light box — a fill-in pass that tests the wrong variable.**
  The generator lays 25 bits across 8 buttons: one unique bit each, then
  `Utils.random(3, 10)` more, with no bit owned by more than four buttons.
  Three rules and one defect:
    * **The extra-bit count is a target, not a guarantee.** Each button gets a
      100-attempt budget and simply ends with fewer bits if it cannot place its
      quota. A port that loops until the quota is met builds a different puzzle
      — and on an unlucky draw, one that never terminates.
    * **The starting lit/unlit split is clamped at seven**, so the eighth button
      is forced the other way. NR's comment beside that code says it ensures "at
      least 2 buttons lit and unlit"; the code guarantees **one**. A 7/1 split
      passes every clamp. Transcribed as the code has it, with the discrepancy
      recorded rather than splitting the difference.
    * **A bit given to an already-toggled button arrives flipped** — construction
      and play share one path, so building the grid first and applying toggles
      afterwards yields a different board.

  **[nr] The defect.** After the quota pass, NR walks the 25 bits handing each
  unowned one to a random button:

      num = usedBits.getOrDefault(i, 0);
      if (num > 0) continue;
      if (uniqueBits.contains(num)) continue;   // `num`, not `i`
      addBit(Utils.random(7), i);

  `num` is zero on every line that reaches the second test — the line above
  skipped everything non-zero — so the guard asks whether **bit 0** is unique,
  not whether bit `i` is. Two outcomes, neither intended:
    * bit 0 is unique (8 in 25): the guard fires for every unowned bit and the
      entire fill-in pass does nothing, leaving bits with no owner;
    * bit 0 is not unique (17 in 25): the guard never fires, so an unowned
      unique bit can be handed to a second button, breaking the
      one-unique-bit-per-button property the puzzle rests on.
  Ported as `i`, which is what the surrounding code means. Verified by mutation:
  reproducing NR's reading fails at 8 of 10.

  That is the third variable-level defect found in NR this session — the
  Tormented Demon's De Morgan inversion and Callisto's oscillating phase guard
  were the others. All three survive because the common case behaves correctly.

  **Not done:** the puzzle box and the sextant interface.

- 2026-08-20 — **A6c Watson — an accumulator that is consumed, not banked.**
  `trail.constant` already carried the Mimic's own numbers and said the gate in
  front of them was "A6's". This is that gate.

  Watson takes one clue of each lower tier and gives a master scroll box.
  The four tiers are bitpacked into one attribute — NR's comment says "we
  bitpack the value to avoid unnecessarily verbose vars" — at bits 1, 2, 4, 8.
    * **A tier is offered only if its bit is CLEAR and the clue is in the
      inventory.** Both halves, and they are independent: having given the easy
      clue leaves the other three open.
    * **Completion RESETS the hash to zero**:

          final boolean completed = depositedCluesHash == (1 | 2 | 4 | 8);
          player.addAttribute("...hash", completed ? 0 : depositedCluesHash);

      So it is not a permanent unlock — it is a four-slot accumulator that is
      spent. A second master clue costs four more clues. Reading it as an
      unlock, which is the natural reading of a bitmask that only ever gains
      bits, hands out every later master scroll box for nothing. Verified by
      mutation: banking the hash fails at 6 of 10.
    * The bit index is `type.ordinal() - 1`, guarded in the source by an
      `assert ClueItem.EASY.ordinal() == 1` — there is a beginner tier at
      ordinal 0 that Watson does not accept, and the subtraction is what keeps
      easy on bit 0.
    * "Hand over all clues" replaces "Cancel" only above one depositable tier.

  **Not done:** the Mimic fight itself, Patchy, Sherlock and the scroll boxes.

- 2026-08-20 — **A3e map clues — what the cache settles, and the one thing it does not.**
  `trail_read.rs2` answers a map clue with "You should take a closer look at
  this map." and a comment saying picking the interface is A3's. I could not
  finish that, and the useful part of this slice is being precise about why.

  **Established from the cache and pinned:**
    * `cluehelper_clue_map` holds **41 rows**: beginner 5, easy 9, medium 12,
      hard 7, elite 6, quest 2. There is no difficulty 5.
    * The ids are one tidy run 101..137 in difficulty order — and **two
      easy-rated clues sit outside it**, `treasure_scroll_0` at 138 and
      `mysterious_orb_0` at 139, with the quest pair far away at 954 and 957.
      Deriving a clue's tier from its id, which that tidy run invites, gets
      exactly those two wrong. Verified by mutation: widening the block to
      swallow them fails at 2 of 7.
    * The pack holds **24 generic `trail_map01`..`24` plus EIGHT per-clue
      variants** — `trail_clue_easy_map006`, `trail_clue_hard_map006` and
      `007`, `trail_clue_medium_map008`..`012`. The plan row said six. Counted,
      not trusted.
    * **41 rows against 32 interfaces**, so no one-row-one-picture mapping
      exists. Anything that builds a 41-entry interface table is wrong before
      it starts, and that is worth asserting so the next attempt starts from it.

  **Not established: which interface a given row opens.** The table has no
  interface column — its columns are id, difficulty, target, region,
  requirements — and the interface files carry no clue id either
  (`trail_map01` is interface 346, `trail_clue_easy_map006` is 337, neither
  references a row). I had a plausible reading, that the 24 generic maps cover
  ids 101..124 as `id - 100`, and it is wrong: that range is exactly
  beginner+easy+medium, but five of those medium rows have their own
  `trail_clue_medium_map008..012` variants, so the generic block cannot own
  them. Rather than ship a formula that fits the counts and not the data, the
  rule is recorded as open. Settling it needs the clientscript that opens these
  interfaces, or a wiki cross-reference of which picture belongs to which clue.

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
   `ToriRSServer_ScriptsRunClaim`. Kept here because the decision it records still
   applies to every future hook of this shape.

5. ~~**A headless scene harness (blocks the `partial` bosses).**~~ **Never
   needed — 2026-08-20.** Five bosses were marked `partial` on the grounds that
   arena geometry needs a live scene the C-driven selftest does not have. **It
   has one.** `ToriRSServer_WorldTeleport` rebuilds the scene around the player and
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
