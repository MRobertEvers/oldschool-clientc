# Finishing Hunter

Plan to take `OSRS-Content/osrs239-content/server/scripts/skill_hunter/`
from its current broad-but-incomplete port to the complete current
[Hunter](https://oldschool.runescape.wiki/w/Hunter) skill.

The OSRS Wiki snapshot checked on **2026-08-16** is the gameplay authority for
levels, XP, rewards, traps, locations, success modifiers, and quest gates. The
checked-in cache is the naming and interaction authority:

- `configs/all.npc` for NPC names and ops;
- `configs/all.loc` for scenery names, ops, and multilocs;
- `configs/all.obj` for tools and rewards;
- `configs/all.dbrow` for the in-game Hunter skill guide;
- `server/scripts/areas/world/configs/*.spawn` for authored NPC placement.

Do not infer the feature set from `meta.ini` saying revision 239. This cache
already contains the 2024 Hunter Guild, 2025 crab trapping, and the June 2026
Letvek/Stymphike records. Use the records that are actually present.

Primary references:

- [Hunter](https://oldschool.runescape.wiki/w/Hunter)
- [Hunter creatures](https://oldschool.runescape.wiki/w/Hunter_creatures)
- [Hunter level-up table](https://oldschool.runescape.wiki/w/Hunter/Level_up_table)
- [Hunter experience table](https://oldschool.runescape.wiki/w/Hunter/Experience_table)
- [Hunter training](https://oldschool.runescape.wiki/w/Hunter_training)
- [Hunters' Rumours](https://oldschool.runescape.wiki/w/Hunters%27_Rumours)

### Implementation progress (2026-08-17, final audit)

The detailed tables below preserve the original implementation plan and Wiki
fact inventory. Their old `wired`/`partial`/`missing` labels describe the
starting point; this section is the authoritative current status.

| slice | current result |
|---|---|
| Owned traps and shared rewards | Complete: six player-owned slots, mixed-method cap, Wilderness allowance, one-deadfall cap, expiry/recovery, bait/smoke, outfit, horn, rumours, and pouch routing. |
| Classic methods | Complete gameplay loops for bird snares, box/net/magic/rabbit traps, deadfalls, pitfalls, falconry, butterflies/moths, implings, and all five kebbit-tracking families. Uzer and Feldip now use their full cache-authored ten-node/fifteen-link graphs, reveal each traversed footprint segment, never reuse a link, and extend a three-link route to four only when needed to reach one of the six valid catch endpoints. |
| Modern creatures | Complete: jerboa, black chinchompa, pyre fox, tecu salamander, sunlight/moonlight moths and antelopes, Razor-backed kebbit, Herbiboar, maniacal monkey, Letvek, and Stymphike. The 26 Letvek spawns use the current Wiki coordinates. |
| Passive/hybrid methods | Complete gameplay loops for all nine bird houses, aerial fishing, drift-net fishing, all three crab tiers, sandworm castings, and moss lizards. Drift nets include passive Fishing-only catches and fossil/clue-fallback pre-rolls. |
| Hunter Guild | Complete Guild NPC placement, all six retained Rumour assignments/task blocking, Gilman reset, pity/parts, sacks, milestones/lore, outfit, huntsman's kit, meat/fur pouches, whistles, 14-stop quetzal transport, shops, fur clothing, gloves of silence, crossbows/bolts, Hunter Kit, cape, and horn of plenty. |
| Adjacent transport/raid hooks | Giant-eagle routes already belong to Eagles' Peak. All seven Chambers bat NPCs now have exact level/XP/reward catch handlers; their normal placement remains owned by the broader unfinished Chambers room generator. |

Current explicit blockers, which must not be papered over with guessed IDs:

1. **Wyrmscraig goat hunting:** this cache has only generic/desert goat items;
   it lacks the Wyrmscraig goat, Geoff, Mr McGroot, goat-pit locs, spikes, fur,
   hoof, and horn records. Goat Rumours therefore remain disabled.
2. **Lucky impling loot:** catching and storing lucky implings works, but this
   repository has no Treasure Trails state or reward tables. Opening preserves
   the jar and reports that dependency instead of silently consuming it or
   awarding an invented table. Crystal implings have their full current
   eighteen-entry main table and the representable elven-signet tertiary; the
   unusable elite-clue tertiary is suppressed for the same reason.
3. **Adjacent world systems:** full Chambers room generation and Fossil Island
   underwater oxygen/weight rules remain with their owning systems. Hunter's
   NPC/loc interactions are ready to bind when those systems land.

Verification at this checkpoint:

- isolated clean-overlay RuneScript compile: **20,523 scripts**;
- spawn/config loader: clean after adding the required `==== NPC ====` headers
  to Letvek and Hunter Guild spawn files;
- `mock230 --selftest` now invokes `::hunterrun`, which checks trap registry
  contracts, modern creature rows, crystal loot cardinality/ranges, current butterfly rules, bird houses,
  antelope pits, sandworms, and both complete Uzer/Feldip tracking graphs;
- broad `mock230 --selftest` reaches completion with the same two unrelated,
  pre-existing movement failures (Hans walking and stopped-player chase).

Remaining delivery plan is dependency-driven: import a post-2026-07-29 cache
and implement H10b from the already-recorded Wiki rules; land Treasure Trails
and route lucky/crystal/clue-bottle rolls into its single-roll API; then bind
the Chambers bat handlers from the raid room generator and run the client
interaction matrix in section 11.

Where Wiki summary tables disagree with a dedicated current page, record the
conflict and use the dedicated creature/method page. This matters today for
black chinchompa XP (315 on its page and the box-trap page) and impling XP
(the Impling page's explicit Puro/surface columns). Never silently choose the
value that happens to match an old local row.

---

## 0. Measured repository state

Hunter is not a stub. It is about **5,600 lines** and already has these
end-to-end slices:

| method | current coverage | main files |
|---|---|---|
| Bird snare | five classic birds | `bird_snare.rs2`, `bird_snare.dbrow` |
| Box trap | ferret, chinchompa, red chinchompa | `box_trap.rs2`, `box_trap.dbrow` |
| Butterflies | four classic butterflies, including an obsolete barehand model | `butterfly.rs2`, `butterflies.dbrow` |
| Tracking | polar/common/feldip/desert trails; two trails initially lacked complete coordinate graphs | `polar_trail.rs2`, `common_trail.rs2`, `desert_trail.rs2`, `feldip_trail.rs2` |
| Deadfall | four classic kebbits | `deadfall.rs2`, `deadfall.dbrow` |
| Falconry | spotted, dark, and dashing kebbits | `falconry.rs2`, `falconry.dbrow` |
| Implings | baby through dragon, net + jar only | `impling.rs2`, `implings.dbrow`, `impling_loot.dbrow` |
| Puro-Puro | entry, wheat, Elnock storage/tracker, imp defender | `minigames/minigame_puropuro/` |
| Net trap | swamp, orange, red, and black salamanders | `net_trap.rs2`, `net_trap.dbrow` |
| Pitfall | larupia, graahk, and kyatt | `pitfall.rs2`, `pitfall.dbrow` |
| Rabbit | rabbit snare and ferret flushing | `rabbit_snare.rs2`, `rabbit_hole.rs2` |
| Magic box | imp-in-a-box | `magic_box.rs2`, `imp_box.rs2` |

Adjacent content already exists for Hunter potion, Fancy Clothes Store's shop,
Leon’s Prototype Crossbow shop, and horn-of-plenty charging. Those are not all
OSRS-correct yet.

### 0.1 Blocking correctness defect: traps are not really plural

`~hunter_trap_max` correctly returns 1/2/3/4/5 traps at levels
1/20/40/60/80, but each method stores only one player coordinate and state:

- `%hunter_snare_coord`
- `%hunter_box_coord`
- `%hunter_deadfall_coord`
- `%hunter_net_coord`
- `%hunter_pitfall_coord`

The active count therefore counts *trap methods*, not individual traps. A
player can lay only one box trap even when their level permits five, while a
snare plus box plus net incorrectly looks like three independent traps. World
loc mutations also carry no owner, so another player can interact with or
replace a trap with no authoritative ownership check.

**No new placeable-trap creature should land before H1 fixes this.**

### 0.2 Known data drift

The current tables mix 2006/2009 values with modern OSRS rules. Examples:

- rabbit snaring awards 36 XP in `rabbit_snare.dbrow`; current OSRS awards
  144 XP;
- ferrets award 100 XP locally; current OSRS awards 115 XP;
- butterfly barehand requirements are 80/85/90/95 Hunter plus Agility locally;
  current OSRS uses ten Hunter levels over the net requirement, no Agility
  requirement, and the normal catch XP;
- impling XP uses one value for surface and Puro-Puro, although current OSRS
  has different XP values and surface catches can loot immediately without a
  jar;
- there is no Wilderness `+1` trap allowance;
- current scripts do not consistently model preferred bait `+3%`, smoke
  `+2%`, standing on a trap, or the OSRS attempt radius/cadence.

Every existing row must be treated as **implemented but unaudited**, not done.

### 0.3 Tests are missing

Hunter has debug procedures in individual scripts, but no comprehensive
`hunter_selftest.rs2` and no `::hunterrun`. There is currently nothing that
proves the trap cap, ownership, XP, item conservation, timers, or cross-system
hooks.

---

## 1. H1 — owned runtime trap foundation

This is the first implementation slice.

### 1.1 Host representation

Extend dynamic loc state in `src/net/mock/mock230_zone.h`,
`mock230_world.c`, and the RuneScript command bridge in
`mock230_scripts.c`. Each runtime trap loc needs:

```text
runtime_uid
owner_pid + owner_login_generation
trap_kind
state             // laid, baited, smoking, failed, caught, dismantling
prey_npc_uid
created_tick
expires_tick
flags             // baited, smoked, wilderness allowance, protected site
small method data // product/row/site slot as needed
```

Expose the smallest general script API needed to:

- add an owned dynamic loc;
- read its stable uid and owner;
- find/count all owned locs by category or trap kind;
- update state without losing owner metadata;
- enumerate and clean the owner's traps on logout or expiry.

Do not use `npc_findowned` as a workaround: it is singular and already serves
familiar/thrall ownership, so controller NPCs would collide with unrelated
systems.

### 1.2 Shared trap service

Replace the per-method singleton coordinates with a shared service in
`skill_hunter/scripts/hunter_traps.rs2`:

- `~hunter_trap_limit(method, coord)` — base 1/2/3/4/5; add one in the
  Wilderness for eligible traps;
- `~hunter_trap_count(owner)` — count every active placeable trap, regardless
  of method;
- `~hunter_trap_place`, `~hunter_trap_transition`, `~hunter_trap_remove`;
- `~hunter_trap_can_interact` — only the owner may check/reset/dismantle;
- `~hunter_trap_return_items` — inventory first, private ground item fallback;
- `~hunter_trap_reserve_prey` — prevent two traps from catching one NPC;
- cleanup on logout, expiry, NPC despawn, zone rebuild, and interrupted action.

Use a stable loc uid, not coordinate alone. Two sequential traps can occupy the
same coordinate over time, and a coordinate-only delayed queue can mutate the
new trap when an old timer fires.

### 1.3 Common state rules

All placeable methods must share these invariants:

1. Validate membership, quest/area gate, Hunter level, item, tile, trap cap,
   collision, and prohibited area before consuming anything.
2. Consume items only when the loc is successfully created.
3. A trap checks only compatible, unreserved prey in its real method radius.
4. A player standing on a snare/box/net trap prevents a catch where the Wiki
   specifies it.
5. Bait and smoke flags survive reset only when OSRS says they do.
6. Check/reset/dismantle is atomic; repeated clicks cannot duplicate XP, prey,
   rewards, or tools.
7. Failure, expiry, logout, and forced cleanup return exactly the right items.
8. A caught prey NPC disappears once and returns through normal spawn timing.

---

## 2. H2 — one authoritative catch pipeline

Create a central `~hunter_complete_catch` hook. Every method calls it after its
own animation/state machine has succeeded.

Inputs should include method, creature row, location/site, amount, and flags
(surface/Puro, barehanded, rumour-eligible, doubled). It owns:

- Hunter XP and secondary XP;
- inventory/private-ground reward delivery;
- Hunter Guild rare-part roll and dry protection;
- guild outfit success/rare-part modifiers;
- horn-of-plenty boost, charge use, and optional double catch;
- catch tracker/counters and future collection-log hooks;
- shared sound/message behavior.

Keep trap-specific state in method tables rather than forcing every method into
one giant schema. Add a small common creature table for facts used across
systems: `method`, `level`, `xp`, `rumour_part`, `rumour_group`, and
`horn_double_policy`.

### 2.1 Catch chance audit

Reconstruct success formulas method by method from Wiki mechanics and verified
client behavior. Do not silently preserve the old generic random formula.

- Apply preferred bait as an additive `+3%` catch chance.
- Apply smoke, bruma torch, or anti-odour salt as `+2%` where eligible.
- Apply the guild hunter outfit to success and rare-part rates.
- Apply the horn's invisible boost to success calculations, never to level
  eligibility.
- Preserve guaranteed methods such as crab trapping.
- Add boundary tests at requirement level, 99, boosted levels, and guaranteed
  catch thresholds where known.

---

## 3. Complete skill-guide roster

Status meanings:

- **wired** — a handler and data row exist, but H2 still audits correctness;
- **partial** — some interaction exists but the method is incomplete;
- **missing** — no usable Hunter implementation exists;
- **adjacent** — belongs partly to another skill, quest, minigame, or world
  system but must still integrate with Hunter.

### 3.1 Tracking with a noose wand

Reference: [Tracking](https://oldschool.runescape.wiki/w/Tracking)

| level | creature | XP | status | implementation work |
|---:|---|---:|---|---|
| 1 | [Polar kebbit](https://oldschool.runescape.wiki/w/Polar_kebbit) | 30 | partial | verify every burrow, snow drift, tunnel, and final plant in the Rellekka area; complete reset/branch graph |
| 3 | [Common kebbit](https://oldschool.runescape.wiki/w/Common_kebbit) | 36 | partial | verify woodland burrow-to-bush graph and ring-of-pursuit shortcut |
| 7 | [Feldip weasel](https://oldschool.runescape.wiki/w/Feldip_weasel) | 48 | partial | replace the current soft trail with an authored coordinate graph and final-catch loc |
| 13 | [Desert devil](https://oldschool.runescape.wiki/w/Desert_devil) | 66 | partial | replace the current soft trail with the Uzer-area graph and final-catch loc |
| 49 | [Razor-backed kebbit](https://oldschool.runescape.wiki/w/Razor-backed_kebbit) | 348.5 | implemented | verified Piscatoris graph, noose catch, spikes/bones, full-trail ring-of-pursuit reveal, and self-test |
| 80 | [Herbiboar](https://oldschool.runescape.wiki/w/Herbiboar) | 1,950–2,461 | implemented | five starts, nine route/end variants, intermediate fossils/numulite, Herblore harvest, magic-secateurs bonus, pet roll, and self-test |

Implementation notes:

- Treat tracks as per-player loc state. One player's trail must not reveal or
  reset another's.
- Store the selected path and step, not one boolean per visual object.
- Validate the cache loc placement before authoring a graph. Never invent a
  coordinate because an object name exists.
- Ring of pursuit can reveal the full kebbit trail; it does not apply to
  Herbiboar.

### 3.2 Bird snares

Reference: [Bird snare](https://oldschool.runescape.wiki/w/Bird_snare)

| level | creature | XP | status | NPC / result |
|---:|---|---:|---|---|
| 1 | [Crimson swift](https://oldschool.runescape.wiki/w/Crimson_swift) | 34 | wired | `hunting_bird_jungle`; feathers, raw bird meat, bones |
| 5 | [Golden warbler](https://oldschool.runescape.wiki/w/Golden_warbler) | 47 | wired, XP audit | `hunting_bird_desert`; current row says 48 |
| 9 | [Copper longtail](https://oldschool.runescape.wiki/w/Copper_longtail) | 61.2 | wired, XP audit | `hunting_bird_woodland`; current row says 61 |
| 11 | [Cerulean twitch](https://oldschool.runescape.wiki/w/Cerulean_twitch) | 64.5 | wired, XP audit | `hunting_bird_polar`; current row says 64.7 |
| 19 | [Tropical wagtail](https://oldschool.runescape.wiki/w/Tropical_wagtail) | 95.2 | wired | `multicoloured_bird` |

Convert every snare to the owned-trap service. Verify the three visible states,
attempt timing, player-on-trap exclusion, smoke, failure reset, expiry, tool
return, and all spawn regions. Add the Wilderness allowance only where an
eligible trapping method is actually in the Wilderness.

The level-up guide also lists **Stymphike at 82** under birds, but it is not a
bird-snare creature; implement it separately in H10.

### 3.3 Butterflies and moths

References: [Butterfly net](https://oldschool.runescape.wiki/w/Butterfly_net)
and [Barehanded butterfly catching](https://oldschool.runescape.wiki/w/Barehanded_butterfly_catching)

| net level | barehand level | creature | XP | status |
|---:|---:|---|---:|---|
| 15 | 25 | [Ruby harvest](https://oldschool.runescape.wiki/w/Ruby_harvest) | 24 | wired; replace obsolete 80 Hunter/75 Agility barehand gate |
| 25 | 35 | [Sapphire glacialis](https://oldschool.runescape.wiki/w/Sapphire_glacialis) | 34 | wired; replace obsolete 85/80 gate |
| 35 | 45 | [Snowy knight](https://oldschool.runescape.wiki/w/Snowy_knight) | 44 | wired; replace obsolete 90/85 gate |
| 45 | 55 | [Black warlock](https://oldschool.runescape.wiki/w/Black_warlock) | 54 | wired; replace obsolete 95/90 gate |
| 65 | 75 | [Sunlight moth](https://oldschool.runescape.wiki/w/Sunlight_moth) | 74 | missing; add `moth_sunlight` cache NPC, jar/barehand rewards, Avium Savannah spawns, rumour part |
| 75 | 85 | [Moonlight moth](https://oldschool.runescape.wiki/w/Moonlight_moth) | 84 | missing; add `moth_moonlight`/`_low_wander` cache NPCs, jar/barehand rewards, Savannah and cavern spawns, rumour part |

Net catches require a wielded net; barehanded catches require neither the net
nor Agility. A jar is required only to retain the butterfly/moth. Without one,
the catch is immediately released and its effect is applied. Remove the old
large barehand Hunter/Agility XP awards, use the normal catch XP, and audit the
release/use effects of filled jars as a separate item-interaction test.

### 3.4 Implings and Puro-Puro

References: [Impling](https://oldschool.runescape.wiki/w/Impling) and
[Puro-Puro](https://oldschool.runescape.wiki/w/Puro-Puro)

| level | barehand | impling | Puro XP | surface XP | status |
|---:|---:|---|---:|---:|---|
| 17 | 27 | [Baby](https://oldschool.runescape.wiki/w/Baby_impling) | 18 | 20 | wired, wrong context model |
| 22 | 32 | [Young](https://oldschool.runescape.wiki/w/Young_impling) | 20 | 22 | wired, wrong context model |
| 28 | 38 | [Gourmet](https://oldschool.runescape.wiki/w/Gourmet_impling) | 22 | 24 | wired, wrong context model |
| 36 | 46 | [Earth](https://oldschool.runescape.wiki/w/Earth_impling) | 25 | 27 | wired, wrong context model |
| 42 | 52 | [Essence](https://oldschool.runescape.wiki/w/Essence_impling) | 27 | 29 | wired, wrong context model |
| 50 | 60 | [Eclectic](https://oldschool.runescape.wiki/w/Eclectic_impling) | 30 | 32 | wired, wrong context model |
| 58 | 68 | [Nature](https://oldschool.runescape.wiki/w/Nature_impling) | 34 | 36 | wired, wrong context model |
| 65 | 75 | [Magpie](https://oldschool.runescape.wiki/w/Magpie_impling) | 44 | 216 | wired, wrong context model |
| 74 | 84 | [Ninja](https://oldschool.runescape.wiki/w/Ninja_impling) | 50 | 240 | wired, wrong context model |
| 80 | 90 | [Crystal](https://oldschool.runescape.wiki/w/Crystal_impling) | — | 280 | missing; Prifddinas-only `ii_impling_type_12_*` NPC family |
| 83 | 93 | [Dragon](https://oldschool.runescape.wiki/w/Dragon_impling) | 65 | 300 | wired, wrong context model |
| 89 | 99 | [Lucky](https://oldschool.runescape.wiki/w/Lucky_impling) | 80 | 380 | missing; surface `ii_impling_type_11` only, despite an unused `_maze` cache variant |

Required behavior:

- normal net, magic net, and barehand catch routes;
- barehand/magic-net `+8` percentage-point success modifier;
- a jar is mandatory in Puro-Puro;
- on the surface, no jar means loot the impling immediately; a present jar can
  store it;
- bind/snare/entangle grace ownership and Dark Lure movement;
- invisible tiered world spawner, reveal timing, 100-tile roam boundary,
  despawn/respawn, water/lava travel, and stuck teleport;
- separate Puro and surface counters exposed by the net tracker interface;
- all twelve current loot tables and jar-break/return behavior;
- Elnock Inquisitor jar exchange, jar generator, storage, imp defender bonus,
  crop-circle entry/exit, magical wheat push, and temporary crop circles.

Do not duplicate loot tables between direct-loot and jar-opening paths. Both
must call the same weighted table with the appropriate jar-return rule.

### 3.5 Deadfalls

Reference: [Deadfall](https://oldschool.runescape.wiki/w/Deadfall)

| level | creature | XP | preferred bait / material | status |
|---:|---|---:|---|---|
| 23 | [Wild kebbit](https://oldschool.runescape.wiki/w/Wild_kebbit) | 128 | raw meat | wired |
| 33 | [Barb-tailed kebbit](https://oldschool.runescape.wiki/w/Barb-tailed_kebbit) | 168 | raw rainbow fish | wired |
| 37 | [Prickly kebbit](https://oldschool.runescape.wiki/w/Prickly_kebbit) | 204 | barley | wired |
| 51 | [Sabre-toothed kebbit](https://oldschool.runescape.wiki/w/Sabre-toothed_kebbit) | 200 | raw meat | wired |
| 57 | [Pyre fox](https://oldschool.runescape.wiki/w/Pyre_fox) | 222 | embertailed jerboa tail | missing; NPC `varlamore_hunterfox01` |
| 60 | [Maniacal monkey](https://oldschool.runescape.wiki/w/Maniacal_monkey_(Hunter)) | 1,000 | banana, not logs | implemented; native boulders/spawns, gorilla mount, quest/greegree gates, tail regrowth and rare intact tail |

Deadfalls are fixed boulder sites and have a method cap of one, but still need
owner-aware state. Add the pyre fox NPC/reward/rumour part and Savannah sites.
For maniacal monkeys, bind the existing Kruk's Dungeon NPC spawns, require the
Monkey Madness II access state, use banana bait, model tail-hanging/catch
animation, and award the tail without pretending that ordinary log deadfall
rules apply.

### 3.6 Box traps

Reference: [Box trap](https://oldschool.runescape.wiki/w/Box_trap)

| level | creature | XP | location / bait | status |
|---:|---|---:|---|---|
| 27 | [Ferret](https://oldschool.runescape.wiki/w/Ferret_(Hunter)) | 115 | Piscatoris | wired; XP is currently 100 |
| 39 | [Embertailed jerboa](https://oldschool.runescape.wiki/w/Embertailed_jerboa) | 137 | Locus Oasis / west of Hunter Guild | missing; NPC `varlamore_hunterjerboa01` |
| 53 | [Chinchompa](https://oldschool.runescape.wiki/w/Chinchompa_(Hunter)) | 198.4 | woodland; spicy tomato | wired |
| 63 | [Carnivorous chinchompa](https://oldschool.runescape.wiki/w/Carnivorous_chinchompa) | 265 | jungle; spicy minced meat | wired |
| 73 | [Black chinchompa](https://oldschool.runescape.wiki/w/Black_chinchompa_(Hunter)) | 315 | Wilderness; spicy minced meat | missing handler for `hunting_chinchompa_black`; 12 NPC spawns already authored |
| 76 | [Letvek](https://oldschool.runescape.wiki/w/Letvek_(Hunter)) | 208.5 | Apsul and Virer hunting grounds | missing; H10 |

Partial completion of Eagles' Peak gates ordinary box-trap use. A laid box
attempts to lure compatible prey in a two-tile radius every three ticks, and a
player standing on the trap prevents capture. Black chinchompas receive the
Wilderness extra-trap allowance and must integrate with death/escape behavior.

### 3.7 Net traps

Reference: [Net trap](https://oldschool.runescape.wiki/w/Net_trap)

| level | creature | XP | preferred bait | status |
|---:|---|---:|---|---|
| 29 | [Swamp lizard](https://oldschool.runescape.wiki/w/Swamp_lizard_(Hunter)) | 152 | guam tar | wired |
| 47 | [Orange salamander](https://oldschool.runescape.wiki/w/Orange_salamander_(Hunter)) | 224 | marrentill tar | wired |
| 59 | [Red salamander](https://oldschool.runescape.wiki/w/Red_salamander_(Hunter)) | 272 | tarromin tar | wired |
| 67 | [Black salamander](https://oldschool.runescape.wiki/w/Black_salamander_(Hunter)) | 304 | harralander tar | wired |
| 79 | [Tecu salamander](https://oldschool.runescape.wiki/w/Tecu_salamander_(Hunter)) | 344 | irit tar | missing; NPC `salamander_mountain` |

Use the real young-tree state locs for set/catching/full/failed. On failure,
return the rope and small fishing net as private ground items with correct
duration. Add the Wilderness extra trap for black salamanders. Tecu needs its
cache NPC/loc states, five verified trees southeast of Ralos' Rise, reward,
immature variant if represented, and rumour part.

### 3.8 Pitfalls

Reference: [Pitfall](https://oldschool.runescape.wiki/w/Pitfall)

| level | creature | XP | status |
|---:|---|---:|---|
| 31 | [Spined larupia](https://oldschool.runescape.wiki/w/Spined_larupia) | 180 | wired |
| 41 | [Horned graahk](https://oldschool.runescape.wiki/w/Horned_graahk) | 240 | wired |
| 55 | [Sabre-toothed kyatt](https://oldschool.runescape.wiki/w/Sabre-toothed_kyatt) | 300 | wired |
| 72 | [Sunlight antelope](https://oldschool.runescape.wiki/w/Sunlight_antelope) | 380 | implemented; five Savannah pits, nine spawns, guaranteed catch and full reward set |
| 91 | [Moonlight antelope](https://oldschool.runescape.wiki/w/Moonlight_antelope) | 450 | implemented; four Hunter Guild cavern pits/spawns, guaranteed catch and full reward set |

For each site, verify knife + log construction, tease with teasing stick or
hunter's spear, NPC targeting/chase, player jump, prey jump/fail/catch,
retaliation damage, reset, hide/meat/antler rewards, and rumour part. Add the
Savannah antelope pit coordinates from cache/map data; do not reuse the classic
16-slot pitfall varbits if the new multilocs define their own slots.

### 3.9 Falconry, rabbits, and magic boxes

| level | method / creature | XP | status | remaining work |
|---:|---|---:|---|---|
| 27 | Rabbit snare / [white rabbit](https://oldschool.runescape.wiki/w/White_rabbit) | 144 | wired, wrong XP | audit ferret release, hole flushing, rabbit foot roll, tool ownership, Eagles' Peak gate |
| 43 | Falconry / [spotted kebbit](https://oldschool.runescape.wiki/w/Spotted_kebbit) | 104 | wired | audit Matthias rental/return, hands/weapon restrictions, projectile and retrieve race |
| 57 | Falconry / [dark kebbit](https://oldschool.runescape.wiki/w/Dark_kebbit) | 132 | wired | add rumour part and common catch hook |
| 69 | Falconry / [dashing kebbit](https://oldschool.runescape.wiki/w/Dashing_kebbit) | 156 | wired | add rumour part and common catch hook |
| 71 | Magic box / [imp](https://oldschool.runescape.wiki/w/Imp) | 450 | wired | move to owned traps; verify two-imp banking charges and exclusions |

The falcon is rented from Matthias and occupies the hand-equipment contract;
logout, leaving the area, death, and interrupted retrieval must return it
without duplicating the bird or fur. Falconry drops cannot be doubled by a
horn because both need the off-hand slot.

---

## 4. Passive and hybrid methods absent from `skill_hunter/`

### 4.1 H6 — bird house trapping

Reference: [Bird house trapping](https://oldschool.runescape.wiki/w/Bird_house_trapping)

The cache already has `birdhouse_not_built`, tier-specific empty/full/bird
states, and four wrapper locs `birdhouse_1..4` driven by transmit varbits. Use
four per-player site records, not shared world loc mutation.

| Hunter | bird house | Hunter XP | Crafting to make |
|---:|---|---:|---:|
| 5 | [Regular](https://oldschool.runescape.wiki/w/Bird_house) | 280 | 5 |
| 14 | [Oak](https://oldschool.runescape.wiki/w/Oak_bird_house) | 420 | 15 |
| 24 | [Willow](https://oldschool.runescape.wiki/w/Willow_bird_house) | 560 | 25 |
| 34 | [Teak](https://oldschool.runescape.wiki/w/Teak_bird_house) | 700 | 35 |
| 44 | [Maple](https://oldschool.runescape.wiki/w/Maple_bird_house) | 820 | 45 |
| 49 | [Mahogany](https://oldschool.runescape.wiki/w/Mahogany_bird_house) | 960 | 50 |
| 59 | [Yew](https://oldschool.runescape.wiki/w/Yew_bird_house) | 1,020 | 60 |
| 74 | [Magic](https://oldschool.runescape.wiki/w/Magic_bird_house) | 1,140 | 75 |
| 89 | [Redwood](https://oldschool.runescape.wiki/w/Redwood_bird_house) | 1,200 | 90 |

Implement:

- Crafting recipe: logs + clockwork, hammer, and chisel;
- build at the four Fossil Island spaces and load ten eligible seeds;
- persist tier, seed type/count, and absolute completion epoch so logout does
  not pause the roughly 50-minute timer;
- check/interact/seed/dismantle/empty/reset ops for every multistate loc;
- full collection: Hunter XP, clockwork, feathers, raw bird meat, up to one
  seed-nest roll plus ten ordinary nest rolls;
- strung rabbit foot's nest-table effect;
- early-empty warning: return clockwork, destroy bird house, seeds, and accrued
  loot;
- Bone Voyage/access gate and all four verified sites.

### 4.2 H6 — aerial fishing

Reference: [Aerial fishing](https://oldschool.runescape.wiki/w/Aerial_fishing)

| Hunter | Fishing | catch | Hunter XP | status |
|---:|---:|---|---:|---|
| 35 | 43 | [Bluegill](https://oldschool.runescape.wiki/w/Bluegill) | 16.5 | implemented |
| 51 | 56 | [Common tench](https://oldschool.runescape.wiki/w/Common_tench) | 45 | implemented |
| 68 | 73 | [Mottled eel](https://oldschool.runescape.wiki/w/Mottled_eel) | 90 | implemented |
| 87 | 91 | [Greater siren](https://oldschool.runescape.wiki/w/Greater_siren) | 130 | implemented |

Implemented in `skill_fishing/scripts/fishing_spots/aerial_fishing.rs2`, with
the shared catch data in `skill_fishing/configs/fishing.constant`. It binds
`fishing_spot_aerial`, Alry the Angler, cormorant glove state, native bird
flight/projectile graphics, current regular/fine fish offcuts and king worms,
the Wiki's visible-level catch and Molch-pearl formulas, both skill XP awards,
inventory-full behavior, filleting, golden tench exchange, and pearl shop.

### 4.3 H6 — drift-net fishing

Reference: [Drift net fishing](https://oldschool.runescape.wiki/w/Drift_net_fishing)

This shared Hunter/Fishing activity requires non-boostable 44 Hunter and 47
Fishing plus Bone Voyage. The cache already has `fossil_fish_shoal`,
`fossil_mermaid_driftnets`, two `fossil_drift_net*_multi` site wrappers,
none/setup/some-fish/full loc states, and `fossil_drift_net` objects.

Implement:

- Ceto's 200-numulite daily or 20,000 permanent access state;
- underwater equipment/oxygen/weight/flippers movement rules owned by the
  Fossil Island underwater system;
- Annette storage for up to 2,000 noted or unnoted nets;
- two per-player anchor states, ten shoals per net, and destruction on harvest;
- passive shoal capture, which awards Fishing XP only;
- active chase/scare behavior and pathing toward the nearest non-full net;
- active Hunter XP plus Fishing XP, scaling by the real level formula through
  level 70 (44/47 gives 52.3/46.2 per chased shoal; 70/70 caps at 101.5/77);
- scare modifiers for merfolk/trident variants and dragon harpoon;
- collect, discard, or bank all for five numulites;
- the six Fishing-level loot bands, fossil 1/25 pre-roll, clue/pufferfish
  1/600 rule, and ten rolls per full net.

This activity does not use the placeable-trap cap and must not count toward H1.
It does use H2 for Hunter XP/counters, with a method flag that prevents outfit,
horn, and rumour modifiers unless the Wiki explicitly supports them.

### 4.4 H6 — crab trapping

Reference: [Crab trapping](https://oldschool.runescape.wiki/w/Crab_trapping)

| Hunter | crab | XP | access / bait |
|---:|---|---:|---|
| 21 | [Red crab](https://oldschool.runescape.wiki/w/Red_crab) | 64 | The Pandemonium; fish offcuts |
| 48 | [Blue crab](https://oldschool.runescape.wiki/w/Blue_crab) | 136 | The Great Conch / Troubled Tortugans; fish offcuts |
| 77 | [Rainbow crab](https://oldschool.runescape.wiki/w/Rainbow_crab) | 216 | Crown Jewel and 64 Sailing; fine fish offcuts |

Each location has five permanent build sites. Building one needs 10
Construction, saw, hammer, bucket, plank, and two nails. Construction is
one-time and permanent per player. Active baited/full traps are limited to two
at Hunter 21, then three/four/five at 40/60/80. Red/blue attract after 15
ticks; rainbow after 25; catches never fail. Empty and optionally re-bait,
then support knife-to-raw-meat and pestle-to-paste processing. Quest/Sailing
access belongs to those systems; Hunter must consume their authoritative gate.

---

## 5. H7 — Varlamore creatures and Hunter Guild

Land these together because Rumours depend on their rare-part hooks.

### 5.1 Remaining Savannah roster

The creature rows above already identify embertailed jerboa, sunlight and
moonlight moths, pyre fox, tecu salamander, and both antelopes. For each:

1. bind the cache NPC/category and every interaction loc state;
2. derive spawn rows from authoritative map/cache or Wiki advanced-location
   coordinates;
3. add product, meat/hide/antler, bait, XP, animation, and sound;
4. call the shared catch and rumour hook;
5. verify at least one live hunting circuit in the client.

The relevant modern NPCs currently have no authored `.spawn` rows. Asset
presence is not placement; do not claim these methods complete until the
spawns and locs are visibly reachable.

### 5.2 Hunter Guild NPC interactions

Reference: [Hunter Guild](https://oldschool.runescape.wiki/w/Hunter_Guild)

Cache NPCs include:

| NPC | cache name | required interaction |
|---|---|---|
| Guildmaster Apatura | `hg_apatura` | guild introduction, milestones, lore/key |
| Huntmaster Gilman | `hg_gilman` | novice rumours, assignment reset/all-tiers fallback |
| Guild Hunter Ornus | `hg_ornus` | adept assignment list A |
| Guild Hunter Cervus | `hg_cervus` | adept assignment list B |
| Guild Hunter Aco | `hg_aco` | expert assignment list A |
| Guild Hunter Teco | `hg_teco` | expert assignment list B |
| Guild Hunter Wolf | `hg_wolf` | master assignment list |
| Guild Scribe Verity | `hg_verity` | unlock at 46, status, back-to-back toggle |
| Soar Leader Pitri | `hunter_guild_pitri` | whistle blueprints/milestones |
| Pellem | `hg_fur_trader` | trade and `Fur-clothing` exchange |
| Imia | `hg_mixedhide_seller` | Hunter supplies, mixed-hide stock/interactions |
| Quetzal | `hg_quetzal` | transport integration |

Keep Leon's existing prototype-crossbow shop. Add Imia's Hunter supplies if its
shop NPC/cache op is present, and audit bank/banker ops rather than inventing
new identifiers.

### 5.3 Hunters' Rumours

| tier | level | NPCs | extra gate |
|---|---:|---|---|
| Novice | 46 | Gilman | none |
| Adept | 57 | Cervus / Ornus | none |
| Expert | 72 | Aco / Teco | none |
| Master | 91 | Wolf | At First Light |

Implement persistent assignment state **per guild hunter**, because assignments
can be held to block creatures for other hunters. Store task creature, rare
part, progress/dry count, completion-ready state, back-to-back preference, and
completed-rumour total.

Use the current assignment sets, not the release-day lists:

| NPC | current assignable creatures |
|---|---|
| Gilman | every creature in the rows below; he is the novice/all-task fallback |
| Cervus | swamp lizard, horned graahk, spotted kebbit, black warlock, orange salamander, razor-backed kebbit, sabre-toothed kebbit, grey chinchompa, dark kebbit, pyre fox, Wyrmscraig goat, red chinchompa, sunlight moth |
| Ornus | spined larupia, snowy knight, embertailed jerboa, spotted kebbit, orange salamander, sabre-toothed kebbit, sabre-toothed kyatt, pyre fox, red salamander, red chinchompa |
| Aco | orange salamander, sabre-toothed kebbit, grey chinchompa, sabre-toothed kyatt, dark kebbit, red salamander, red chinchompa, dashing kebbit, sunlight antelope, tecu salamander, moonlight moth |
| Teco | sabre-toothed kebbit, grey chinchompa, sabre-toothed kyatt, dark kebbit, red salamander, Wyrmscraig goat, red chinchompa, dashing kebbit, sunlight antelope, sunlight moth, herbiboar |
| Wolf | red salamander, red chinchompa, dashing kebbit, sunlight antelope, tecu salamander, herbiboar, moonlight moth, moonlight antelope |

Gilman's “every” set is the union of the explicit task rows: tropical wagtail,
wild kebbit, sapphire glacialis, swamp lizard, spined larupia, barb-tailed
kebbit, snowy knight, prickly kebbit, embertailed jerboa, horned graahk,
spotted kebbit, black warlock, orange salamander, razor-backed kebbit,
sabre-toothed kebbit, grey chinchompa, sabre-toothed kyatt, dark kebbit, pyre
fox, red salamander, Wyrmscraig goat, red chinchompa, dashing kebbit, sunlight
antelope, sunlight moth, tecu salamander, herbiboar, moonlight moth, and
moonlight antelope. Filter assignments whose quest/access requirements the
player cannot meet, and disable the goat row until H10b's cache assets land.

The rare-part roll occurs only on a legitimate catch of the assigned creature.
Use these current base rates and dry protection, modified by guild outfit
pieces:

| method | base rate | pity without outfit | pity with outfit |
|---|---:|---:|---:|
| Bird snare | 1/20 | 40 | 38 |
| Box trap | 1/50 | 100 | 94 |
| Butterfly net | 1/75 | 150 | 142 |
| Deadfall | 1/15 | 30 | 28 |
| Falconry | 1/10 | 20 | 18 |
| Net trap | 1/25 | 50 | 46 |
| Spiked pit | 1/15 | 30 | 28 |
| Goat pit | 1/48 | 96 | re-read when goat assets land |
| Kebbit tracking | 1/15 | 30 | 28 |
| Herbiboar | 1/7 | 14 | 12 |

A pity part with no inventory space is destroyed, while a normal rare roll can
drop the part on the ground. Possessing any rare part prevents another part
from being awarded, though successful catches still advance pity progress.
Doubled horn loot neither grants a second part nor advances dry protection. On
turn-in:

```text
XP = (current Hunter level + 5) * modifier
modifier = 50 novice/adept, 55 expert, 60 master
```

Award the correct `hg_lootsack_t0..t3`, implement each sack table once, and
land milestone unlocks (10/25/50/100/150/250) with quetzal whistle blueprints,
meat cooking flags, lore, and key. Then add guild hunter outfit, huntsman's kit,
meat/fur pouches, whistles, quetzal feed, and pet/unique rolls using existing
cache objects.

### 5.4 Guild hunter outfit

Each piece affects Hunter success and rumour rare-part chance; the full set is
not just cosmetic:

| piece | catch rate | rare-part chance |
|---|---:|---:|
| Headwear | +0.2% | +0.4% |
| Top | +0.8% | +1.6% |
| Legs | +0.6% | +1.2% |
| Boots | +0.4% | +0.8% |
| Full-set bonus | +0.5% | +1.0% |

Implement piece helpers in the shared catch service and test every combination,
not just counts 0–4. Pity rounding has special behavior when any piece is worn
and when the full set is owned; encode it from the Wiki rather than deriving it
from the percentage sum. Do not apply success modifiers to guaranteed crab
catches.

Classic camouflage outfits do **not** improve catch chance in current OSRS;
their useful behavior is weight reduction. Do not copy the stale queue note
that treats camouflage as a Hunter accuracy bonus.

---

## 6. H8/H9 — other skill-guide unlocks and adjacent systems

| level | unlock | status | implementation boundary |
|---:|---|---|---|
| 1–66 | polar/wood/jungle/desert camo, larupia/graahk/kyatt gear, spotted/spottier capes, gloves of silence | partial | Fancy Dress fur exchange and Crafting recipes; weight/Thieving effects belong to equipment/Thieving |
| 15 | [sandworm castings](https://oldschool.runescape.wiki/w/Sandworm_castings) | implemented | `piscarilius_grub_castings`; spade + bucket, non-boostable gate, 10 XP on sandworms, sand fallback, 59%→70% scaled chance, and bucket harvesting |
| 20 | [moss lizard trapping](https://oldschool.runescape.wiki/w/Moss_Lizard) | soft quest stub only | rope on `pmoon_lizard_rock`, rustle `_bush`, run `moss_lizard` into `_trap_set`/`_activated`; XP is 90% of Hunter level capped at 90; raw lizard always, tail during Perilous Moons |
| 24/37 Crafting | [lucky rabbit foot](https://oldschool.runescape.wiki/w/Rabbits_foot) / strung rabbit foot | partial | audit rabbit-foot drop; ball-of-wool stringing; Woodcutting/bird-house nest modifier |
| 27 | [giant eagle transport](https://oldschool.runescape.wiki/w/Eagle_(giant)) | missing/adjacent | Eagles' Peak quest state, eagle NPC/loc travel routes, rope state |
| 43/57/69 | falconry | wired | Matthias dialog/rental lifecycle and rumours |
| 50 Ranged | [hunter's crossbow](https://oldschool.runescape.wiki/w/Hunters%27_crossbow) | partial/adjacent | Leon prototype dialog/shop, kebbit bolts; advanced sunlight crossbow is a Fletching/Ranged integration |
| 50 | [horn of plenty](https://oldschool.runescape.wiki/w/Horn_of_plenty) | partial and incorrect | see below |
| 54 | [gloves of silence](https://oldschool.runescape.wiki/w/Gloves_of_silence) | missing/adjacent | Fancy Dress exchange; Thieving failure modifier and degradation |
| 71 Magic | [Hunter Kit spell](https://oldschool.runescape.wiki/w/Hunter_Kit_(spell)) | missing/adjacent | Lunar spell gate, Dream Mentor, inventory-space behavior, kit contents |
| 99 | [Hunter cape](https://oldschool.runescape.wiki/w/Hunter_cape) | implemented | +1 visible boost; shared five daily red/black chin teleports; unlimited guild teleport after Apatura unlock |

The current Perilous Moons script directly handles `pmoon_lizard_bush` and
`pmoon_lizard_rock` as a soft tail grant. Remove or delegate that handler when
real moss-lizard hunting lands, or both scripts will compete for the same ops.

### 6.1 Horn of plenty repair

Reference: [Horn of plenty](https://oldschool.runescape.wiki/w/Horn_of_plenty)

The existing script drains a charge every 25 catches but supplies neither
success boost nor double catch. Replace it with the current rules:

- empty horn: invisible `+2` Hunter success boost;
- charged horn: invisible `+4`, consuming a charge every 25 catches;
- optional charged toggle: `1/10` double reward, no extra XP, consuming a
  charge when it triggers;
- no double implings, crabs, Molch pearls, or falconry drops;
- no second rumour dry-protection increment;
- charge/check/toggle/uncharge/worn-blow ops, up to 20,000 gryphon feathers;
- Stymphike doubling follows the post-2026-07-15 behavior.

`gryphon_feather` and its charge variants now exist in the cache, so remove any
stale comment claiming the item cannot be represented.

### 6.2 Chambers of Xeric bats

The in-game Hunter guide lists Guanic (1), Prael (15), Giral (30), Phluxia
(45), Kryket (60), Murng (75), and Psykk (90), with barehanded catching at 99.
Keep these in the Chambers of Xeric content lane, but route their Hunter XP
through the common catch hook. They are not world-spawn Hunter creatures and
must not be spawned globally to make the guide row appear implemented.

---

## 7. H10 — June 2026 Vampyrium methods

These records are already in the cache and belong in scope.

### 7.1 Letvek

Reference: [Letvek (Hunter)](https://oldschool.runescape.wiki/w/Letvek_(Hunter))

- level 76 Hunter, 208.5 XP;
- box trap at the Apsul and Virer hunting grounds in Vampyrium;
- 26 Wiki-listed spawn coordinates, to be validated against the map before
  authoring `.spawn` rows;
- cache NPC `hunting_letvek`, product `letvek`;
- gate access on The Blood Moon Rises;
- normal owned box-trap state, but no chinchompa-specific reward assumptions;
- Letvek is consumed as Stymphike bait.

### 7.2 Stymphike

Reference: [Stymphike](https://oldschool.runescape.wiki/w/Stymphike)

- level 82 Hunter, 1,350 XP on a clean kill;
- require makeshift spear or hunter's spear and at least one Letvek;
- bait `hunting_stymphike_tree_no_letvek` into `_letvek`, consuming 1–3 bait;
- hide using `hunting_stymphike_bush*` loc states;
- make three lure calls worth 125 XP each when timing allows;
- spawn `hunting_stymphike` at roughly 65 ticks, then support manual or
  automatic spear;
- alerted/failure shot deals 6/10 and awards 891 XP instead of full XP;
- always award bones, 15 Stymphike feathers, and one carcass on success;
- integrate horn doubling without extra XP;
- gate the whole hunting ground on The Blood Moon Rises.

Use the cache's actual tree/bush multilocs, spear projectile/spot animation,
NPC, feathers, and carcass. Do not approximate this as a bird snare.

---

## 8. H10b — Wyrmscraig goat hunting (current Wiki delta)

References: [Goat hunting](https://oldschool.runescape.wiki/w/Goat_hunting) and
[Wyrmscraig Goat](https://oldschool.runescape.wiki/w/Wyrmscraig_Goat)

Goat hunting released on 2026-07-29, after the cache represented in this tree.
The generic `cattleprod` object exists, but the Wyrmscraig goat NPCs, Geoff,
spike supply, goat-pit loc states, goat horn/fur/hoof, and Mr McGroot do not.
This slice is **blocked on importing a cache that contains IDs/models/ops**.
Do not assign guessed ids or reuse unrelated goat records.

Once those records exist, implement:

- level 60 Hunter and Sheep Herder completion gate;
- Geoff dialog plus unlimited cattleprod and spike supplies;
- spike the goat pit, then prod a goat toward it or pull it with Telekinetic
  Grab/Dark Lure from the opposite side;
- direction/distance/path validation and support multiple goats in flight;
- pit capacity 16/18/20/22/24 at Hunter 60/69/77/85/93;
- 20 XP for cattleprod movement or 10 XP for spell movement;
- collection XP starting at 100 at level 60, +3/level through 80, then
  +1/level to 179 at 99;
- 3/4 goat horn and 1/4 Wyrmscraig goat fur rewards;
- 1/48 assigned-rumour hoof, pity 96 until an outfit rule is published;
- Mr McGroot roll (`1 / (40,000 - 25 * Hunter level)`, with the documented
  200m-XP multiplier);
- Gilman, Cervus, and Teco assignment lists; Wolf must not assign goats after
  the 2026-08-05 change;
- the optional goat-to-ship/Port Sarim Sailing interaction in the Sailing lane,
  not inside Hunter's catch script.

Keep goat-pit capacity separate from the normal placeable-trap limit. It is a
fixed communal-looking activity with per-player pit contents and its own cap.

---

## 9. Spawn, loc, and interaction policy

For every new creature or activity:

1. Confirm its symbolic name and ops in `all.npc`/`all.loc`/`all.obj`.
2. Count existing `.spawn` rows and map loc placements.
3. If missing, source coordinates from Wiki advanced data, cache map data, or
   another authoritative current-data source and record the provenance in a
   comment/generator.
4. Keep moving NPCs inside an explicit hunting-area boundary; never allow a
   prey NPC to wander out of reach or across unrelated regions.
5. Give catchable NPCs a reservation/ownership state during attraction,
   falcon flight, teasing, or luring.
6. Ensure capture uses the normal NPC respawn lifecycle rather than immediately
   cloning the NPC.
7. Wire all displayed ops: catch, spear, tease, bait, smoke, set, check, reset,
   dismantle, investigate, track, empty, seeds, release, trade, talk, and shop.

Modern Hunter assets with no current spawn rows include Letvek, Stymphike,
crabs, moths, antelopes, pyre fox, embertailed jerboa, and tecu salamanders.
Black chinchompas and maniacal monkeys already have authored NPC spawns, so
those can bind existing world data first.

---

## 10. Delivery order

| slice | deliverable | dependency | exit condition |
|---|---|---|---|
| H0 | freeze Wiki facts and cache-name audit in DB rows/tests | — | every guide row mapped to cache name or explicit blocker |
| H1 | owned dynamic loc/trap registry | H0 | five simultaneous same-method traps work; cross-player ops rejected |
| H2 | common catch/chance/reward hook | H1 | XP, bait, smoke, outfit, horn, rumours have one integration point |
| H3 | repair all currently wired classic methods | H2 | exact levels/XP/items; no singleton state remains |
| H4 | razor kebbit, black chin, jerboa, pyre fox, maniacal monkey | H3 | each works at a real reachable site |
| H5 | modern implings + barehand/surface/Puro rules | H2 | 12 types, two XP contexts, direct loot, tracker, full loot audit |
| H6 | bird houses, aerial fishing, drift-net fishing, crab trapping | H2 + adjacent skill gates | offline timers/hybrid XP/permanent sites verified |
| H7 | Savannah roster | H3 | moths, tecu, antelopes and their rewards live |
| H8 | Herbiboar and complete tracking | H2 | every trail graph and herb/reward roll verified |
| H9 | Hunter Guild, Rumours, outfit, sacks, whistles | H2 + H4/H7/H8 | all tiers and assignment lists completable |
| H10 | Letvek and Stymphike | H1/H2 | Blood Moon gate and full bait/hide/spear loop live |
| H10b | Wyrmscraig goat hunting | newer cache import + H2/H9 | pit, rewards, pet, and current rumour lists work |
| H11 | cape, horn, shops/fur exchange, sandworms, moss lizards, eagles, crossbows, CoX hooks | relevant owners | every skill-guide unlock has a live or explicitly external owner |
| H12 | full QA and queue closeout | all | `::hunterrun`, compile, selftest, and client matrix pass |

H1–H3 should be one reviewable foundation series. Thereafter H4, H5, H6, H7,
and H10 can be implemented independently against the common hook. H9 lands
after all of the creatures it can assign, except H10b remains disabled until
the newer cache assets exist.

---

## 11. Test plan

Add `skill_hunter/scripts/hunter_selftest.rs2` and `::hunterrun`.

### 11.1 Table and roster checks

- every Hunter creature row has one method, exact level, exact tenths XP, and
  a valid NPC/loc/product;
- every in-game guide row is classified wired, external, or blocked;
- every rumour task has a rare-part object and a reachable creature;
- every filled trap loc maps back to exactly one source trap and catch row;
- every item/npc/loc identifier resolves at content-load time.

### 11.2 Trap lifecycle checks

- trap limits at 1, 19, 20, 39, 40, 59, 60, 79, 80, and 99;
- Wilderness `+1` and no bonus outside it;
- five simultaneous box traps, plus mixed-method total cap;
- two players' traps on adjacent tiles cannot steal or mutate each other;
- place/reset/check/dismantle/expire/logout each conserves tools exactly once;
- a stale delayed queue cannot mutate a newer trap at the same coordinate;
- one prey cannot be reserved or rewarded twice;
- full inventory sends private recoverable items to the right tile.

### 11.3 Catch modifier checks

- base, baited, smoked, anti-odour, outfit piece counts, empty horn, charged
  horn, and capped success chance;
- horn charge every 25 catches and double-catch charge independently;
- double reward never doubles XP or rumour progress;
- guaranteed crab catches ignore success modifiers;
- surface/Puro/net/magic-net/barehand impling paths use correct XP and jar rule.

### 11.4 Persistent activity checks

- bird house completion while logged out and all early-empty/full states;
- four bird houses with different tiers/timers at once;
- permanent per-player crab construction;
- simultaneous guild-hunter assignments, task blocking, resets, dry protection,
  turn-in XP formula, sacks, and milestone unlocks;
- tracking path isolation between players and after relog.

### 11.5 Verification commands

```sh
make -C src mock230-scripts
make -C src mock230
./src/build_opt/mock230 --selftest
```

Then run `::hunterrun` and a live-client smoke matrix covering at least one
success, failure, reset, bait, smoke, expiry, logout recovery, and second-player
interaction for each trap family. Bird houses, Puro-Puro, Herbiboar, Rumours,
crabs, and Stymphike each need their own end-to-end client pass.

---

## 12. Definition of done

Hunter is complete only when:

- every row in §§3–8 is functional or explicitly owned by a completed adjacent
  system;
- the in-game skill guide opens to interactions that really exist;
- levels, XP, products, bait, modifiers, and gates match the cited Wiki pages;
- multiple simultaneous traps and cross-player ownership are correct;
- every NPC and loc is placed, reachable, bounded, and fully op-wired;
- no quest soft-skip (especially the Perilous Moons moss-lizard handler)
  competes with the real mechanic;
- every reward can be consumed, processed, stored, or exchanged by its owning
  system;
- Hunter compilation, the global selftest suite, `::hunterrun`, and the live
  smoke matrix pass without adding failures.
