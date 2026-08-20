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
| A1 | **Treasure Trails — core** (`TreasureTrail`, tiers, casket chain, step model) | 760 | — | in_progress | `content/treasuretrails/` |
| A2 | Treasure Trails — **Emote clues** | 1699 | — | pending | `clues/EmoteClue.java` — largest single file in NR content |
| A3 | Treasure Trails — **Cryptic + Anagram + Map + Coordinate clues** | 1140 | — | pending | `clues/{CrypticClue,Anagram,MapClue,CoordinateClue}` |
| A4 | Treasure Trails — **puzzles**: Light box, puzzle box, sextant, Hot/Cold | 968 | — | pending | `clues/{LightBox,PuzzleBox,HotColdClue}`, `coordinateutils/SextantInterface` |
| A5 | Treasure Trails — **reward tables** (easy→master) + `ClueRewardTable` | 1347 | — | pending | `rewards/` |
| A6 | Treasure Trails — **NPCs & STASH**: Mimic, Watson, Patchy, Sherlock, Uri, stash units | 1100 | — | pending | `npcs/`, `stash/`, `plugins/` |
| A7 | **Achievement Diaries — 12 area task sets** (UI already exists) | 3883 | 402 (UI) | pending | `achievementdiary/diaries/*.java`; our `interface_diaries/` provides `~diary_task_complete` |
| A8 | **Grand Exchange** | 1882 | — | pending | `content/grandexchange/` |
| A9 | **Dwarf Multicannon** (place/load/fire/decay/retrieve, all variants) | 963 | quest only | pending | `content/multicannon/` |
| A10 | **Tears of Guthix** (Juna, cave, XP award) | 827 | — | pending | `content/tog/` |
| A11 | **Shooting Stars** | 623 | — | pending | `content/stars/` |
| A12 | **Master scrolls / item transportation**, **Creature Creation**, **Sandstone grinder**, **Zahur**, **Trouver parchment**, **supply caches**, **muddy chest**, **books** | 611+427+396+111+243+96+62+138 | — | pending | one slice, eight small self-contained systems |

### Wave B — bosses

| # | Slice | NR | Ours | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| B1 | **The Nightmare of Ashihama** + Phosani's | 4899 | — (spawns only) | pending | `boss/nightmare/` — largest boss package; needs the instance framework |
| B2 | **DT2 bosses** — Duke Sucellus, the Leviathan, the Whisperer, Vardorvis | 5076 | — (quest refs only) | pending | `content/dt2/` — quest is ported, the four bosses are not |
| B3 | **Wilderness bosses** — Callisto/Artio, Vet'ion/Calvar'ion, Venenatis/Spindel | 2883 | 352 | pending | `boss/wildernessbosses/` |
| B4 | **Alchemical Hydra** | 2384 | 79 | pending | `kebos/alchemicalhydra/` — phases, instance, model |
| B5 | **Araxxor + Araxytes** | 2396 | — | pending | `content/araxxor/`, `content/araxyte/` — cave hunt, attacks, venom, rewards |
| B6 | **Grotesque Guardians** (Dusk & Dawn) | 2010 | 135 | pending | `boss/grotesqueguardians/` |
| B7 | **Zalcano** | 2501 | — | pending | `area/prifddinas/zalcano/` — combat, formation, symbols. This is the whole of `area/prifddinas`. |
| B8 | **Abyssal Sire** | 1540 | 64 | pending | tentacles, respiratory system, lairs, poison fumes, spawns |
| B9 | **Cerberus** | 1516 | 104 | pending | `boss/cerberus/` + area |
| B10 | **Phantom Muspah** | 1289 | — | pending | `boss/phantommuspah/` |
| B11 | **Vorkath** | 1118 | 158 | pending | `boss/vorkath/` + plugins |
| B12 | **Dagannoth Kings** | 948 | drops only | pending | `boss/dagannothkings/` |
| B13 | **Mage Arena II** | 930 | 168 | pending | `boss/magearenaii/` |
| B14 | **Xamphur** | 875 | — | pending | `content/xamphur/` — phantom hand, area |
| B15 | **Rise of the Six** | 870 | — | pending | `content/rots/` |
| B16 | **Skotizo** | 829 | 44 | pending | instance, npc, plugins |
| B17 | **Corporeal Beast** | 766 | — | pending | `boss/corporealbeast/` (+ dark core) |
| B18 | **Kraken** | 644 | 74 | pending | `boss/kraken/` |
| B19 | **King Black Dragon** | 471 | present, thin | pending | `wilderness/king_black_dragon/` |
| B20 | **Sarachnis** | 469 | 58 | pending | |
| B21 | **Obor** + **Bryophyta** | 774 | 111 | pending | both `plugins/` sets |
| B22 | **Thermonuclear Smoke Devil** | 391 | 93 | pending | `boss/smokedevil/` |
| B23 | **Nex** — parity sweep against NR's 4190-line package | 4190 | 5600 (GWD total) | pending | ours may already exceed; **audit, do not rewrite** |
| B24 | **Tormented Demons** — NR variant vs our rs2012 port | 896 | 47 files | pending | reconcile; ours is an rs2012 backport, NR's is OSRS. See `tormented-demons-osrs` |

### Wave C — minigames and areas

| # | Slice | NR | Ours | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| C1 | **Chambers of Xeric — completion** | 24346 | 6498 | in progress elsewhere | already has [`COX_NEARREALITY_PORT_PLAN.md`](minigames/cox/COX_NEARREALITY_PORT_PLAN.md); this queue defers to it and tracks it here |
| C2 | **Tombs of Amascut — completion** | 18484 | 6912 | pending | `content/tombsofamascut/` — encounters, lobby, raid, invocations |
| C3 | **Gauntlet — parity sweep** | 5976 | 4340 | pending | `content/gauntlet/` |
| C4 | **Pest Control — completion** | 2956 | 1464 | pending | portals, spinners, brawlers, void knight, reward shop |
| C5 | **Warriors' Guild** | 2681 | 223 | pending | catapult, shotput, keg balance, dummy room, magical animator |
| C6 | **Duel Arena** | 2430 | 330 | pending | `minigame/duelarena/` incl. interfaces + area |
| C7 | **Prifddinas / elven city** (excl. Zalcano → B7) | 2688 | shops+quest only | pending | `content/elven/` — area, npc, dialogue, obj, item |
| C8 | **Konar quo Maten** (slayer master, Mount Karuulm) | 2196 | — | pending | `kebos/konar/` — depends on D6 |
| C9 | **Revenant Caves — completion** | 2610 | 1475 | pending | `wilderness/revenant/` |
| C10 | **Barrows — completion** | 2118 | 659 | pending | `minigame/barrows/` + wights |
| C11 | **Pyramid Plunder — parity sweep** | 2134 | 1731 | pending | ours was rebuilt from 2009scape; re-check against NR + wiki Changes |
| C12 | **Partyroom** | 1760 | 230 | pending | `content/partyroom/` |
| C13 | **Wilderness events** — hot zone, chest, Ganodermic Beast | 1558 | — | pending | `wilderness/event/` |
| C14 | **Castle Wars** | 1284 | — | pending | `minigame/castlewars/` |
| C15 | **Stronghold of Security** | 1121 | — | pending | `area/strongholdofsecurity/` |
| C16 | **Chompy bird hunting** (the activity, not the quest) | 794 | quest only | pending | `content/chompy/` + plugins |
| C17 | **Motherlode Mine — completion** | 572 | 220 | pending | |
| C18 | **Waterbirth Island** + **Taverley** + **TzHaar** area plugins | 355 | partial | pending | `content/area/` remainder |
| C19 | **Wilderness slayer** + wilderness plugins remainder | 299 | 34 | pending | |

### Wave D — skills

| # | Slice | NR | Ours | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| D1 | **Agility — rooftop courses** (Draynor→Prifddinas, 9 courses) | 5529 | part of 3929 | pending | `skills/agility/*rooftop/` |
| D2 | Agility — **shortcuts** | 8242 | — | pending | `skills/agility/shortcut/` — the single largest skill file set; gates B8, B9 |
| D3 | Agility — **Gnome/Barbarian/Wilderness/Pyramid/Pollnivneach courses** | 2995 | part of 3929 | pending | |
| D4 | **Magic — teleports** (all books + structures) | 3403 | part of 2340 | pending | `skills/magic/spells/teleports/` |
| D5 | Magic — **Lunar spellbook** | 2598 | part of 2340 | pending | `skills/magic/spells/lunar/` |
| D6 | Magic — **Arceuus spellbook** | 1542 | part of 2340 | pending | `skills/magic/spells/arceuus/` |
| D7 | Magic — **regular spellbook remainder + lecterns + resources + actions** | 2701 | part of 2340 | pending | |
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

---

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
