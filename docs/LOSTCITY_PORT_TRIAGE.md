# Porting the LostCity content tree onto mock230 — triage

> **Deliverable 1. No code has been written. Nothing in either tree has been
> modified.** Every number below is measured, and §12 says how to re-measure it.
>
> The operating guide for the port — the decision procedures for engine vs
> content, which pack data goes in, how a slice gets ported, and the phase
> plan that wraps this document's §9 — is
> [`PORTING_GUIDE.md`](PORTING_GUIDE.md). Read it first; this document is the
> measurement underneath it.

Source: `/Users/matthewevers/Documents/git_repos/LostCity_Server/content`
(a fork of 2004scape's content at `d386509 prepare for zuk`).
Destination: `OSRS-Content/osrs239-content`, read by `src/net/mock/mock230`
against `cache.osrs239`.

---

## 1. The headline, before the detail

Three things came out of the measurement that change how this should be planned.

**The id problem is one namespace, not all of them.** The brief assumes every
reference has to be re-resolved. Measured, that is true for `npc` and false for
almost everything else:

| namespace | names in both trees | **same id in both** |
|---|---:|---:|
| `obj` | 3,131 | **3,130 (100.0 %)** |
| `spotanim` | 262 | **262 (100 %)** |
| `inv` | 156 | **156 (100 %)** |
| `seq` | 1,090 | **1,088 (99.8 %)** |
| `varp` | 164 | **161 (98.2 %)** |
| `loc` | 3,309 | **3,135 (94.7 %)** |
| `npc` | 1,073 | **0 (0.0 %)** |

LostCity *authors* its client cache, so it allocates npc ids itself, from zero
(`0=hans`, `1=man`, `2=man2`). It did **not** renumber items, and OSRS never did
either — `bronze_sword` is 1277 in both trees, `bucket_milk` 1927, `egg` 1944.
`goblin` is 100 there and 3028 here.

That does not make re-resolution optional. It makes it *cheap and mandatory*: an
id that is right 94.7 % of the time is the worst possible kind, because the 5.3 %
is invisible. The sharpest case found:

```
phoenixgang     rev254 varp 146   osrs239 varp 145
blackarmgang    rev254 varp 145   osrs239 varp 146
```

The two Dragon-Slayer gang varps are **swapped**. Copying ids across compiles,
runs, and silently reverses which gang the player joined.

**The binding constraint is engine opcodes, not ids.** The content calls 268
distinct ServerScript commands. `mock230` implements 123 of them. **145 are
missing, and they are not the tail** — `npc_find` (305 uses, 139 files),
`loc_change` (270), `loc_add` (248), `oc_param` (351), `npc_setmode` (310).
Counted per file: **636 of 1,265 scripts (50.3 %) use only commands the engine
already has.** Every id in the tree could be perfect tomorrow and half the
content still would not run.

**Interfaces are the wall, and they are load-bearing on more of the tree than
expected.** 1,415 interface or component names are referenced from `.rs2`; **35
resolve by name in osrs239 and 11 of the 1,268 component references do.**
**564 of 1,265 scripts (44.6 %) name an interface or a component.** Separately,
`~p_choice*` — the multiple-choice dialogue — is called **879 times across 355
files (28 % of the tree)** and cannot be ported as written (§7.4).

So: **this is a month, not a week**, and the month is mostly spent on the engine
and the interfaces rather than on ids. The id work is a well-defined,
mechanically checkable few days.

---

## 2. What is actually in the tree

The raw counts include two machine-owned directories that are not content:

- `scripts/_unpack/{225,244,245,254}/` — LostCity's cache-bump review queue
  (`docs/CONTENT_ARCHITECTURE.md` §2.3). Machine-generated, never authored.
- `scripts/_test/` — cheat and harness scripts.

Excluding both:

| kind | files | **records/blocks** | note |
|---|---:|---:|---|
| `.rs2` scripts | 1,265 | 4,461 triggers | the work |
| `.obj` | 179 | 1,953 | |
| `.npc` | 125 | 985 | |
| `.loc` | 118 | 1,598 | |
| `.dbrow` | 46 | 1,115 | |
| `.param` | 53 | 214 | |
| `.varp` | 74 | 184 | |
| `.inv` | 43 | 154 | |
| `.struct` | 17 | 149 | |
| `.enum` | 28 | 114 | |
| `.constant` | 109 | 1,562 constants | |
| `.if` | 159 | 1,202 components | IF1 — see §7.3 |
| `.seq` `.spotanim` `.idk` `.flo` `.hunt` `.mesanim` `.varbit` `.dbtable` | 78 | 449 | mostly cache-side |

Triggers used, by family (the dispatch surface a port has to land on):

```
label 2473   proc 1094   oploc1 867   if_button 748   opnpc1 634
command 511  opheld2 321 mapzone 306  zone 262        opheldu 230
oplocu 212   oploc2 193  ai_queue3 184 zoneexit 165   opheld1 159
queue 152    opnpcu 93   ai_timer 87  ai_opplayer2 84 mapzoneexit 73
```

`mock230` dispatches 11 families today: `opnpc1-5`, `oploc1-5`, `opobj1-5`,
`opheld1-5`, `apnpc/aploc/apobj1-5`, `inv_button1-n`, `if_button`, `if_close`,
`ai_queue3`, `login`. It does **not** dispatch `zone`/`mapzone`/`zoneexit`/
`mapzoneexit` (806 uses), the `*u` use-on family (535), `queue`/`timer`/
`ai_timer` (273), `command` (511), `ai_opplayer*`, `advancestat`, or the
`*t` spell-target family.

---

## 3. Assets — the reading is confirmed, with one twist

**Confirmed: do not port any asset.** 3,895 `.ob2`, 458 `.jm2`, 696 `.synth`,
334 `.anim`, 314 `.mid`, 55 `.png`. Positive evidence rather than assumption:

- **Every obj the content references exists in osrs239 by name.** 1,662 distinct
  obj names referenced from `.rs2`, **0 unresolved**. An obj record names its
  own model, so every model behind every referenced item is already in the
  destination cache — in the modern, higher-detail form.
- The same holds for `spotanim` (262/262 ids identical) and `inv` (156/156).
- Maps: shipping a `.jm2`'s **terrain** would overwrite modern Lumbridge with
  2004 Lumbridge and break the collision the client already draws. The scripts
  only *read* geography — 1,582 coordinate literals across 147 map squares, all
  absolute world tiles.

  **But a `.jm2` is not only terrain, and the half that is not does port.**
  265 of LostCity's 458 squares carry `==== NPC ====` / `==== OBJ ====` spawn
  sections: **6,263 npc spawns and 1,082 obj spawns**, which are server content
  and the largest single body of it in the tree after the scripts. An earlier
  draft of this document counted them as part of the asset it says not to port,
  which was wrong. They are inventoried in §4 and their destination is §10.2.

**The twist: 1,696 of the "LostCity" assets came out of osrs239 in the first
place.** This fork has already been fed by `3rd/rscache/tools/port_lostcity` —
`scripts/areas/area_inferno` and `area_tormented_demons` exist, and
`pack/model.pack` and `pack/anim.pack` carry 112 models and 1,584 animations
prefixed `inferno_`, `td_`, `dragon_claws`. Those have a modern equivalent by
construction: they *are* the modern asset, downgraded on the way out. Porting
them back would be a round trip through a lossy transform.

**No asset in the tree lacks a modern equivalent**, with one qualification I
could not close from names alone: the 55 `.png` sprites are IF1 gameframe chrome
(`chatback`, `compass`, `combaticons`, `backbase1`) plus `gnomeball_buttons`.
osrs239 has its own gameframe sprites; it does not have these *as these*, because
the 2004 gameframe does not exist there. They belong with the interfaces (§7.3)
and share their disposition: whatever is decided for an IF1 interface is decided
for its sprites.

The 419 `.synth` and 153 `.mid` names referenced by scripts resolve at **0 %**
against osrs239's sound tables. That is a naming gap, not a content gap — the
sounds exist, spelled differently — and it is why `midi_song`/`midi_jingle`/
`sound_synth` land in "needs a name map", not in "port the asset".

---

## 4. Id resolution, measured

Method: parse every `id=name` table on both sides; walk the authored tree
(`_unpack`/`_test` excluded); a name in a LostCity pack that appears as a token
in a `.rs2` or config counts as referenced. `ref_rs2` restricts to `.rs2`.

| namespace | defined by LC | ref in `.rs2` | **resolves** | **unresolved** | id identical |
|---|---:|---:|---:|---:|---:|
| `obj` | 1,953 | 1,662 | 1,662 | **0** | 3130/3131 |
| `inv` | 154 | 61 | 61 | **0** | 156/156 |
| `varbit` | 6 | 6 | 6 | **0** | 6/6 |
| `npc` | 985 | 899 | 855 | **44** | 0/1073 |
| `loc` | 1,598 | 1,040 | 952 | **88** | 3135/3309 |
| `seq` | 176 | 354 | 310 | **44** | 1088/1090 |
| `spotanim` | 29 | 135 | 105 | **30** | 262/262 |
| `varp` | 184 | 268 | 121 | **147** | 161/164 |
| `param` | 214 | 203 | 23 | **180** | 0/23 |
| `idk` | 82 | 62 | 0 | **62** | 13/13 |
| `flo` | 101 | 15 | 0 | **15** | — |
| `enum` | 114 | 52 | 2 | **50** | 0/4 |
| `struct` | 149 | 51 | 0 | **51** | — |
| `dbrow` | 1,115 | 198 | 0 | **198** | — |
| `category` | — | 122 | 1 | **121** | — |
| `interface` | — | 1,415 | 35 | **1,380** | — |
| `varn` / `vars` / `hunt` / `mesanim` | 31 | 109 | 0 | **109** | — |
| **spawns — npc** | 6,263 lines | — | **6,126 (97.8 %)** | **137 lines / 24 names** | n/a |
| **spawns — obj** | 1,082 lines | — | **1,082 (100 %)** | **0** | n/a |

Read the rows in three groups.

**Group A — the cache fixes the id and it already agrees.** `obj`, `inv`,
`spotanim`, `varbit`. Nothing to do but re-resolve by name so the agreement is
*checked* rather than assumed.

**Group B — the cache fixes the id and it has moved.** `npc` (all of them),
`loc` (174), `seq` (2), `varp` (3). Re-resolution is mandatory and the tail is
hand work:

- **npc, 44 unresolved in scripts.** Sampling shows these are almost all *naming
  drift*, not missing content: `harlow` → osrs239 `dr_harlow`; `king_lathas` →
  `ds2_meeting_king_lathas`; `troll` → 64 candidates none of which is spelled
  `troll`; `leprechaun` → `farming_tools_leprechaun` / `myarm_leprechaun` /
  `zanarisleprechaun`. A human picks one per name. The rest —
  `inferno_zuk`, `td_demon`, `dragonslayer_ghost` — are LostCity's own names for
  ported or 2004-only npcs; for the ported ones the source id is recorded in the
  port manifest, so those are lookups, not decisions.
- **loc, 88 unresolved.** Same shape, plus a genuine 2004-only tail
  (`game_trawler_*`, `gnome_obstacle_*`, `barbarian_rope_swing` — minigame
  scenery).
- **seq/spotanim, 44/30.** Dominated by `inferno_*`, `td_*`, `midget_*`
  (gnomeball) — the ported set and the minigames.

**Group C — the id is not the cache's and the name means nothing there.**
`param`, `struct`, `enum`, `dbrow`, `dbtable`, `category`, `hunt`, `mesanim`,
`varn`, `vars`. These are server-allocated (`content.ini`: `ids = server` or
`names = authored`) and the port *brings its own definitions*. A 0 % resolution
rate here is correct, not a problem — but it means 1,115 dbrows, 214 params, 149
structs and 114 enums have to be **allocated** off layer-0's high-water mark
(`CONTENT_ARCHITECTURE.md` §4.4), and that allocation is the thing that must
never be a guess.

### 4.1 The failure this measurement is designed to catch

Name resolution answers "does something here have this spelling", not "is it the
same thing". Cross-checking the display name of every record present in both
trees:

| type | names in both | same display name | **different** | unnamed |
|---|---:|---:|---:|---:|
| `npc` | 898 | 722 | **75** | 101 |
| `obj` | 1,947 | 1,345 | **499** | 103 |
| `loc` | 1,319 | 893 | **78** | 348 |

Most differences are twenty years of renaming and are harmless
(`Adam full helm (g)` → `Adamant full helm (g)`; `Zamorak potion(1)` →
`Zamorak brew(1)`). Some are not:

```
cave_exit_upass    lc 'Underground Pass Exit'  osrs239 'Cave exit'
cave_railings2     lc 'Cage'                   osrs239 'Gate'
caveorb_vis        lc 'Orb of Light'           osrs239 '<col=ff9040>Orb of light</col>'
```

and the class `CONTENT_ARCHITECTURE.md` §6.2 already caught once —
`goblin_armed` naming npc 2484 in one table and 3045 in another — is exactly
this, one namespace over.

**The bar this sets: the port must emit a diff of display names for every
resolved record, and a human signs it off.** Not a blocker, a review artifact.
It is cheap (the data is in `configs/all.<type>` already) and it is the only
thing that catches a name that resolved to the wrong record.

---

## 5. The engine gap — the real schedule driver

268 distinct commands used, 123 implemented, **145 missing**. By family, ordered
by call sites:

| family | ops | uses | the ones that matter |
|---|---:|---:|---|
| `npc_*` | 26 | 1,360 | `npc_find` 305, `npc_setmode` 310, `npc_add` 175, `npc_walk` 92, `npc_queue` 79 |
| `loc_*` | 8 | 1,033 | `loc_change` 270, `loc_add` 248, `loc_find` 224, `loc_param` 133, `loc_del` 70 |
| `oc_*` | 7 | 417 | `oc_param` 351 — **blocked on a decoder, not effort** (§5.1) |
| `if_*` | 8 | 363 | `if_setcolour`, `if_setobject`, `if_settab`, `if_setmodel` |
| `p_*` | 13 | 268 | `p_finduid` 118, `p_oploc` 41, `p_aprange` 30, `p_walk` 26 |
| `split_*` | 4 | 244 | text paging — **has no job at rev 230** (§7.4) |
| `inv_*` | 10 | 129 | `inv_setslot` 88 |
| `struct_param` | 1 | 115 | same decoder blocker as `oc_param` |
| `map_*` | 4 | 102 | `map_findsquare` 83 |
| `cam_*` | 3 | 88 | cutscenes |
| `stat_add`/`stat_sub` | 2 | 82 | **blocked on a fact, not effort** (§5.1) |
| everything else | 60 | ~500 | `session_log` 85, `finduid` 79, `text_gender` 65, `db_find` 45, `huntall` 45 |

Two of these families are already documented as *deliberately* unimplemented in
`docs/osrs230_mockserver.md` §3.13d, and the reasons still hold.

### 5.1 The three that are blocked on data, not on typing

- **`oc_param` / `nc_param` / `lc_param` / `struct_param` (605 uses).** The
  param's declared type decides whether the result lands on the int or the
  string stack; no decoder here keeps a per-record param type table to answer
  from. This is one decoder change that unblocks 605 call sites and 117 files —
  **the single highest-leverage item in this whole document.**
  **`oc_param` (351 of the 605) and `nc_param` are now done — see §10.3.** The
  claim that the data did not exist turned out to be stale twice over:
  `configs/all.param` has carried the type of every param all along, unpacked by
  cachepack and read by nothing, and the npc records' params were already being
  decoded and discarded inside `mock230_npcinfo.c`.

  `nc_param` is the *smallest* of the four by call sites — this table's own
  numbers leave it 6, against 351 for `oc_param`, 133 for `loc_param` and 115 for
  `struct_param`. It was done next because it was nearly free, not because it
  unblocked much. The two that carry the remaining 248 uses are the expensive
  ones: neither the loc nor the struct config table is decoded at runtime at all.
- **`stat_add` / `stat_sub` (82 uses).** Nothing in this repo pins whether they
  move the base level or the boosted one. That is one fact, from one reference,
  not a week.
- **`oc_cost` / `oc_members` / `oc_tradeable` / `oc_desc` / `nc_desc`.** Not
  decoded. `nc_desc` cannot be: a dat2 npc record has no description at this
  revision (`examine-desc-session`).

The rule §3.13d states — *an opcode that cannot be answered from real data is
better left to the VM's loud stub than implemented from a plausible guess* —
applies unchanged. `oc_desc` was written and removed on exactly this ground.

### 5.2 Where the missing opcodes actually bite

Scripts using only implemented commands, by directory:

```
levelrequire          10/10   100.0%      skill_combat        6/52   11.5%
drop tables           69/71    97.2%      tutorial            3/28   10.7%
levelup               18/19    94.7%      skill_prayer        0/18    0.0%
areas                237/365   64.9%      skill_crafting      0/11    0.0%
quests               204/394   51.8%      skill_magic          0/9    0.0%
general               14/31    45.2%      skill_agility        0/5    0.0%
minigames             19/67    28.4%      skill_thieving       0/5    0.0%
```

The skills are at or near zero because every one of them is a `loc_*` loop:
`loc_find` the tree/rock/altar, `loc_change` it to the depleted form, `loc_add`
the respawn. **`loc_change` + `loc_add` + `loc_del` + `loc_find` + `loc_param`
is the single unlock for the skilling half of the tree**, and it is the same
family the `doors/` directory needs (25 `loc_add`, 22 `loc_param`, 14 `loc_del`).

---

## 6. Does it compile? — the ground-truth pass

`src/serverscript`'s `sscompile` is a real compiler for this grammar, so the
honest way to ask "will these scripts compile against osrs239's symbols" is to
run it. It stops at the first error, so the census is a loop: compile, record the
error, move that file aside, repeat.

```sh
sscompile --src <lostcity>/scripts --out /tmp/out \
    --pack OSRS-Content/osrs239-content/pack \
    --pack OSRS-Content/osrs239-content/configs \
    --pack OSRS-Content/osrs239-content/interfaces
```

**Loading the symbols works: 239,207 symbols, 1,562 constants.** The compiler
reads this tree's tables and LostCity's grammar without modification — the
toolchain is not the problem, which is the good news the brief predicted.

The run was stopped at **784 files** — and the stopping point is the finding, so
it is worth being exact about.

**Past roughly two hundred files the method eats itself.** Quarantining a file
removes the procs it defines, so every caller then fails on *that* rather than on
its own problem. Once `interface_chat/scripts/chat.rs2` went aside, all 355 files
calling `~chatnpc` were doomed; once `player/scripts/equip.rs2` did, so was every
one of `levelrequire/`'s ten — a directory the static census (correctly) scores
as clean on every axis. By the end, **52 % of the failures were
`no proc named` / `no label named`**, which is the loop reporting its own damage.

So the file count is not a usable number and this document will not quote one.
What survives is the classification of the **373 first-order failures** — the
ones that are not a dangling cross-file reference — and the **307 distinct
symbols** inside them, which is the actual work queue:

| class | share of first-order | what it is |
|---|---:|---|
| `'x' is not a command, constant, symbol or script` | 33.5 % | dominated by `questlist:<quest>` — an IF1 component (§7.3) |
| `unknown variable '%x'` | 27.1 % | a varp/varn/vars LostCity invented (§7.5) |
| `unknown type subject 'x' for trigger 'oploc1'/'opnpc1'` | 22.8 % | a loc or npc name that did not resolve (§4, group B) |
| `unknown category subject 'x' for trigger 'y'` | 15.5 % | a `category` name — `_bandit_camp_leader`, `_double_door_open_and_close_left` (§7.6b) |
| arity / case-value mismatches | 1.1 % | four call sites in total |

**Every class maps onto §4 or §7. The compiler found no category of problem the
static census missed**, which is the corroboration this pass was for — and it
puts `category` (15.5 %) far higher up the queue than its 122 references
suggested, because a category is a *trigger subject* and an unresolved one kills
the whole file.

The method's real lesson for the port itself: **symbols must land before the
scripts that name them, or a single missing proc reads as a hundred broken
files.** That is §9's dependency order, arrived at from the other direction.

---

## 7. Feature gaps

### 7.1 The three from the brief, confirmed

| claim | status |
|---|---|
| IF1 vs IF3 is a different layout family | **confirmed and quantified** — §7.3 |
| `chatmulti` / `p_choice` builds options with `cc_create` | **confirmed** — 879 calls, 355 files. §7.4 |
| bare stat names collide and compile to the wrong id | **confirmed against osrs239's own tables** — §7.6 |
| content-first dispatch swallowed an engine verb | **still true structurally** — §7.7 |

### 7.2 Zone triggers (806 uses) — the ZoneMap exists now; the dispatch does not

`zone` 262, `mapzone` 306, `zoneexit` 165, `mapzoneexit` 73. Re-counted
2026-08-01 against `LostCity_Server/content`; the four numbers above still hold.
Every "you have entered the wilderness", every minigame boundary, every area
music trigger is one of these.

**No longer blocked on the engine feature.** The `ZoneMap` landed
(`src/net/mock/mock230_zone.{c,h}`, `osrs230_mockserver.md` §3.17), and with it
the per-zone entity lists that took the flat `npcs[]`/`ground[]` scans out of the
per-client path.

Two things to know before porting any of them, neither of which the count above
tells you:

- **Only 427 of the 806 are zone-keyed.** `mapzone` and `mapzoneexit` key off
  the *map square* (`x >> 6`), not the zone (`x >> 3`) — see
  `NetworkPlayer.updateMap`, which tracks `lastMapZone` and `lastZone` as two
  separate latches. The ZoneMap is irrelevant to 379 of these uses.
- **They are keyed by script *name*, not by a numeric subject.** The reference
  looks up `[zone,<level>_<mx>_<mz>_<lx>_<lz>]` and
  `[mapzone,0_<mx>_<mz>]` through `ScriptProvider.getByName`.
  `mock230_scripts_run_trigger` takes an integer subject and cannot express
  that, so the dispatch half is a name-lookup path in the trigger dispatcher —
  which is the actual remaining work, and it is Phase 2's.

### 7.3 Interfaces — the hard case, sized

| | LostCity (IF1) | osrs239 (IF3) |
|---|---:|---:|
| interfaces | 159 authored `.if` | 969 in-cache |
| components | 8,108 (one flat id space with the interfaces) | 26,478, addressed `(interface << 16) \| child` |
| referenced from `.rs2` | 147 roots, 1,268 components | — |
| **resolve by name** | — | **35 roots, 11 components** |

The addressing is structurally different, not versioned: LostCity's
`interface.pack` puts `inventory` and `player_kit_tailor_legs_man:com_3` in one
flat id space; osrs239 composes `interface << 16 | child` and names components
per-interface in `interfaces/<name>.compack`. The 35 roots that "match" match
only by *spelling* — `inventory` is a different object on each side.

**564 of 1,265 scripts (44.6 %) name an interface or component**, so this is not
a corner of the tree. Per-interface disposition is the right granularity, and
three of them decide most of the tree:

| interface | uses | proposal |
|---|---|---|
| `questlist:<quest>` | the single most common unresolved symbol in the compile pass | **rebuild.** rev 230 has a quest list; this is one component-name map, and it unblocks every `[if_button,questlist:x]` journal script |
| `chatmulti` (`~p_choice*`) | 879 calls, 355 files | **rebuild against 219 `chatmenu`** — §7.4 |
| `levelup` (19 `.if`) | 19 `advancestat` triggers | **rebuild against 233 `levelup_display`**, which osrs239 has |
| everything else (~140) | | **drop or stub per interface**, decided when the script that drives it is ported |

The 55 `.png` sprites belong to this decision, not to the asset decision (§3).

### 7.4 `p_choice` — 28 % of the tree, and it is not hopeless

`OSRS-Content/osrs239-content/server/scripts/interface_chat/scripts/chat.rs2`
already records the finding: rev 230's option dialogue is interface **219
`chatmenu`**, and its component list is exactly

```
0=universe
1=options
```

— the rows are `cc_create`d by a clientscript, so `if_settext` cannot address
them. That is why the existing ported dialogue linearises every choice.

**But the server can already send `RUNCLIENTSCRIPT`** — `mock230_worldmap.c`
does, for `worldmap_transmitdata`. The blocker is narrower than "cannot be
ported": `mock230`'s `RUNCLIENTSCRIPT` sender takes **ints only**
(`mock230_equipment.c:232` records the same limitation blocking
`interface_inv_init`). A string-argument form of one existing encoder is what
stands between here and 879 call sites.

Related: `split_init`/`split_get`/`split_linecount`/`split_pagecount` (244 uses)
are LostCity measuring a string against a font to pick one of four fixed-size
chat interfaces. rev 230 has one multi-line body component that wraps itself.
**These four opcodes have no job here** and should be compiled away, not
implemented — with the one consequence `chat.rs2` already documents: LostCity's
`|` hard break becomes a space, and anything past four lines clips.

### 7.5 varp vs varbit — the shipped-bug class, generalised

Of the **127** LostCity varps that resolve by name in osrs239, **28 name a varp
that osrs239 packs varbits into.** Writing them whole clobbers the neighbours:

```
%dragonquestvar   varp 177  carries 21 varbits (dragonslayer_secret_told, …)
%goblinquest      varp 62   carries  8 varbits (gobdip_main, gobdip_crate1_searched, …)
%prayer0          varp 83   carries 32 varbits (prayer_thickskin, …)
%ibanmulti        varp 162  carries 31 varbits (upass_found_bridge, …)
%emote_access     varp 313  carries 24 varbits
%bankcert         varp 115  carries  5 varbits  ← the one already found
```

This is `CONTENT_ARCHITECTURE.md` §6.1 — *"opening the bank reset the current
tab"* — with 27 more instances waiting. The cause is the same and so is the
detector: `resolve_variable` in `ssc_compile.c` tries VARP before VARBIT, so a
name that is both compiles to the whole-varp write.

The modern cache moved quest state *into* varbits. So the mapping is not
`%goblinquest` → varp 62; it is `%goblinquest` → some **bit range** of varp 62,
and which range is a per-quest decision. **Every one of the 28 needs a human,
and none of them fails loudly if skipped.**

The other **147 unresolved varps** (`com_slashattack`, `damagetype`,
`action_delay`, `eat_delay`, `follower_uid`) are LostCity server bookkeeping —
things it can store in a varp because it authors its own cache. Here they are
`varn`/`vars` or nothing. That is a mechanical reclassification, but it is 147
of them and the compiler reports each as `unknown variable`.

### 7.6 Bare stat names — confirmed against this cache

`pack/stat.pack`'s 23 names, checked against every other osrs239 namespace:

```
attack       also an osrs239 varp
fishing      also an osrs239 loc
hitpoints    also an osrs239 param
```

Content writes `stat_add(attack, …)` and `if(stat(fishing) …)` with a bare name.
Three of 23 collide *in this cache*, and the collision is silent. This wants the
same treatment `content.ini`'s `vardomain` gives varp/varbit: a declared
resolution order, and a load error rather than a shadow.

### 7.6b npc categories do not exist here, and 19 % of the compile failures are that

The compile pass surfaced something neither the brief nor the static census
ranked highly: `unknown category subject 'X' for trigger 'Y'` is **19.3 % of all
failures**, and a category is a *trigger subject*, so an unresolved one kills the
whole file.

The cause is specific. In this tree, `category` means two unrelated things:

- **obj category** — the record's own `category` field, config opcode 94, a
  number the cache states. `pack/category.pack` has **37** names for it as of
  2026-08-01 (this said 6; the weapon categories were added after that count was
  written — re-measure with `grep -c '^[0-9]' pack/category.pack`), and
  `mock230_scripts.c` reads it for `[opheld<n>,_<category>]` and `inv_totalcat`.
- **loc category** — a two-valued door enum (`door_closed` / `door_opened`) in
  `mock230_content.c:1546`, and nothing else. It is **not** a category id and
  must never be passed as one: its two values would alias onto `category.pack`
  ids 0 and 1 and bind unrelated scripts to every door in the world.
  `interaction_category` in `mock230_world.c` says so at the one site where the
  edit is tempting.

**There is no npc category at all.** `SSVM_ProviderGetByTrigger` takes one and
the fallback chain (exact type → category → global) is implemented — but *every
npc call site in `mock230_world.c` passes `-1`*, including `AI_QUEUE3`. There is
nothing to pass. (The obj rung is live and exercised: `[opheld<n>]` fed it
already, and `[opobj<n>]`/`[apobj<n>]` do too since §3.18. npc and loc still
pass -1, deliberately, and that is this item.)

LostCity's content leans on npc categories heavily: `drop tables/` alone binds
**16 of its 94 `[ai_queue3]` triggers to a category** (`_citizen`, `_cow`,
`_guard`, `_bandit_camp_leader`, `_barbarian`, `_black_demon`, …) across 15 of
71 files, and `doors/` does the same with `_double_door_open_and_close_left`.
Categories are *derived* in the reference — crawled out of every `.npc`/`.loc`/
`.obj`'s `category=` key (`CONTENT_ARCHITECTURE.md` §2.1) — so this is not an id
to look up but a field to accept, a crawler to write, and a `-1` to replace at
each call site.

It is a small feature. It is also a **prerequisite of the cheapest slice in
§10**, which is why it is called out here rather than buried in the queue.

### 7.7 Dispatch order

The engine currently resolves an interaction as: `[ap*]` if in range, then
`[op*]`, then "the engine's own verb handling if nothing was bound"
(`osrs230_mockserver.md` §3.13c). "Attack" is deliberately *not* content — the
engine reads the npc's own cache op list.

That worked while the tree bound almost nothing. Importing 634 `[opnpc1]` and
867 `[oploc1]` triggers changes the arithmetic: **wherever content and engine
name the same op, content now wins**, and the goblin's Attack is precisely the
shape of thing that gets swallowed. **It should land before the bulk import, not
after** — after, a swallowed verb is one of a thousand new triggers instead of
one of forty.

**Landed (2026-08-01)**, `osrs230_mockserver.md` §3.18. Content winning is the
*design* now, not a hazard: the reference's own answer to "the goblin's Attack"
is `[opnpc2,_] @player_combat_start` in `skill_combat/`, and the engine's verb
handling is one of seven enumerated rows in `enum Mock230Fallback`, each naming
the blocker keeping it in C, counted at boot and pinned by the selftest. It may
shrink; it must not grow. What that buys for the bulk import: an imported
`[opnpc1,goblin]` that aborts no longer falls through to the engine's greeting
and looks like it worked — an aborted script and an unbound trigger are
different answers now, and only the second is allowed a fallback.

### 7.8 The reverse direction — what osrs239 has that LostCity content cannot say

Worth stating because it bounds what a port can ever achieve:

- **varbits.** LostCity's whole tree declares **6**; osrs239 has **19,008**. The
  content model is "one varp per fact"; the modern model is "bit ranges". §7.5
  is the collision; the deeper point is that ported content will address a
  fraction of the state the client already reads.
- **IF3 / CS2.** Modern interfaces are driven by clientscripts and
  `cc_create`d children. RuneScript at rev 254 has no `cc_*` vocabulary, so
  ported content can *open* and *set* but never *build* an interface.
- **`dbrow`/`dbtable`.** osrs239 ships 16,711 dbrows the client already reads.
  LostCity brings 1,115 of its own, into a namespace whose ids are the cache's.
  The two sets have to coexist; only the server-allocated half is ours.
- **Params on records.** The modern cache carries combat bonuses, respawn and
  wear rules in obj/npc params (`mock230-lumbridge-content`). LostCity states
  them as its own config keys. Where both exist, the cache is the authority and
  the LostCity key is a *claim about the cache* — which is exactly the field
  register `CONTENT_ARCHITECTURE.md` §4.3 specifies and which is not built yet.

---

## 8. Per-category verdict

| category | files / records | verdict | reason |
|---|---|---|---|
| `.obj` configs | 179 / 1,953 | **ports with id remapping — trivial** | 100 % name resolution, 100 % id identity. Overlay the server-only keys; the cache already holds the rest |
| `.inv` | 43 / 154 | **ports as-is** | 156/156 identical |
| `.varbit` | 1 / 6 | **ports as-is** | 6/6 identical |
| `.spotanim` `.seq` | 23 / 205 | **ports with id remapping** | ≥99 % identical; 74 names need hand resolution |
| `.loc` | 118 / 1,598 | **ports with id remapping** | 94.7 % identical — the 174 that moved are the whole risk; 88 names need hand resolution |
| `.npc` | 125 / 985 | **ports with id remapping — mandatory, mechanical** | 0 % identical. Every reference re-resolved by name; 44 need a human |
| `.varp` | 74 / 184 | **needs rework** | 28 clobber varbits (§7.5); 147 referenced names are server bookkeeping needing reclassification |
| `.param` `.struct` `.enum` `.dbrow` `.dbtable` `.category` `.hunt` `.mesanim` | 297 / 2,952 | **ports as-is, but ids must be allocated** | server-allocated namespaces; `max+1` off layer 0, never a hand-picked constant (`CONTENT_ARCHITECTURE.md` §3.3) |
| `.constant` | 109 / 1,562 | **ports as-is** | compiler already loads them |
| `.idk` `.flo` | 5 / 183 | **needs rework** | 0 % name resolution though ids agree — pure naming gap, small |
| `.if` interfaces | 159 / 1,202 | **cannot port; rebuild, drop or stub per interface** | §7.3 |
| `.rs2` scripts | 1,265 | **50 % ports with id remapping; 44 % blocked on interfaces; the rest on opcodes** | §5, §6, §7.3 |
| **spawns inside `.jm2`** | 269 squares / 7,345 lines | **ports with id remapping — into `.spawn` files, never into a `.jm2`** | 97.8 % / 100 % resolve; §10.2 |
| assets (5,810 files) — *terrain half of `.jm2` included* | | **do not port** | §3 |
| `_unpack/` `_test/` | | **do not port** | machine-generated; harness |

---

## 9. Dependency order

Each step is testable and none is a flag day. Steps 0–2 are prerequisites that
are *not* content work.

```
0.  Field register + {client,server} encoders          CONTENT_ARCHITECTURE §4.3, step 5
    ↳ without it there is no defined way for a ported .npc to say
      "hitpoints=7" and have the cache keep its own combat params

1.  Invert the fallback (§7.7)   DONE                  osrs230_mockserver §3.18
    ↳ must precede the bulk trigger import, not follow it — and did

2.  The name-resolution gate itself
    ↳ every unresolved name is a pack-time error; a display-name diff
      is emitted for every resolved record (§4.1); no default, ever

3.  Symbols, in this order — each is named by the next
    3a. constants          (1,562; no dependencies)
    3b. npc categories — the field, the crawler, and the -1 at every npc
        call site (§7.6b). 19.3% of compile failures; blocks drop tables/
    3c. param, struct, enum, dbtable, dbrow  (server-allocated ids)
    3d. varp / varn / vars reclassification (§7.5) — 28 by hand
    3e. npc / loc / seq / spotanim name maps — 206 by hand

4.  Engine opcodes, by leverage
    4a. the param decoder            → oc_/nc_/lc_/struct_param, 605 uses, 117 files
    4b. the loc_* family             → 1,033 uses; unlocks every skill directory
    4c. the npc_* family             → 1,360 uses
    4d. RUNCLIENTSCRIPT with strings → unblocks p_choice, 879 uses
    4e. session_log, finduid, text_gender, stat_add/sub — small, wide

5.  Triggers the engine does not dispatch
    5a. queue / timer / ai_timer   (273 uses)  — cheap
    5b. the *u use-on family       (535 uses)
    5c. zone / mapzone / zoneexit  (806 uses)  — ZoneMap landed; what is
        left is name-keyed trigger dispatch, and 379 of the 806 are keyed
        by map square rather than by zone (§7.2)

6.  Interfaces, per interface, driven by the scripts being ported
    questlist → chatmenu → levelup → the rest on demand

7.  Content, in slices (§10)
```

**Constants and configs before the scripts that name them; interfaces before the
scripts that drive them** — as required. The reordering worth arguing for is that
**4a comes before any bulk content**, because 117 files fail on one decoder.

**Status of step 0 (2026-08-01):** landed on the baker's side —
`CONTENT_PACK_PLAN.md` §5.4 "What has landed" — the field register exists,
`cachepack pack` writes both halves, and `mock230_servercodec.c` can decode
the server band. What has **not** landed is the load path: `mock230_content_load`
still re-parses the `server/scripts` text overlays at boot and nothing reads
`server/pack`. Closing that (and deleting `mock230_pack.c`'s superseded
bakers) is Phase 0 of [`PORTING_GUIDE.md`](PORTING_GUIDE.md) §3.6.

---

## 10. Proposed first slice

**Cook's Assistant, end to end, in Lumbridge.**

`quests/quest_cook/` — 2 scripts, 132 lines, 1 `.loc`, 1 `.varp` — plus
`areas/area_lumbridge/scripts/cook.rs2` which already has a partial port here.

Why this one, out of 58 quests ranked by port difficulty:

- **It is where the server already is.** `MOCK230_HOME` is `3222,3218`, the
  Lumbridge castle courtyard. The cook is upstairs. Nothing has to be teleported
  to or rebuilt to see it.
- **It is the smallest thing that is still a whole feature.** Talk to an npc,
  accept a quest, gather three items, hand them in, quest completes, journal
  updates, reward applies. All four content layers — script, config, varp,
  interface — in 132 lines.
- **It forces the three structural decisions at minimum scale**, which is the
  point of a first slice:

  | it needs | which forces |
  |---|---|
  | `~p_choice4` / `~p_choice2` (3 sites) | §7.4 — `RUNCLIENTSCRIPT` with strings, `chatmenu` 219 |
  | `[if_button,questlist:cook]` | §7.3 — the questlist interface map, the single commonest unresolved symbol in the compile pass |
  | `%cookquest` | §7.5 — varp vs varbit, and `scope=perm` persistence (`osrs230_mockserver.md` §3.15) |
  | `bucket_milk`, `egg`, `pot_flour` | §4 — the resolution gate, on ids that already agree, so a failure is the *gate's* bug and not the data's |
  | `npc_find`, `session_log` | §5 — exactly two missing opcodes |

- **It is verifiable in the real client, headlessly**, with harness that exists:
  `SDL_VIDEODRIVER=dummy`, `TORIRS_SIM_CLICK_AT`, `TORIRS_EXIT_BMP`,
  `MOCK230_VERBOSE=1`.

**Definition of done for the slice:** boot mock230, walk to the cook, complete
the quest, see the journal read "QUEST COMPLETE", log out and back in with the
state persisted — with `mock230_pack` at 0 errors and the existing 38 npc / 776
loc Lumbridge content untouched.

Two smaller slices are available if the interface work should be deferred one
step. Both are strictly easier and neither is a whole feature:

- **`drop tables/`** — 71 scripts, `ai_queue3` already dispatched, **4 missing
  opcodes**, 97.2 % opcode-clean, no interfaces, 1 unresolved npc. Kill a goblin,
  get the right loot. The cheapest *observable* win in the tree — **but it needs
  §7.6b first**: 15 of its 71 files bind their trigger to an npc category, and
  npc categories do not exist here. 56 of 71 land without it.
- **`levelrequire/`** — 10 scripts, 304 `[opheld2]` triggers, **zero missing
  opcodes, zero unresolved names, no category triggers.** Pure equip-requirement
  checks. The only directory in the tree that is clean on every axis measured.

I would do `levelrequire/` first — a half-day proof that the resolution gate
works end to end on real data with nothing else in the way — then `drop tables/`
once §7.6b lands, then Cook's Assistant as the slice that proves the port.

---

## 10.1 What landed

Both ends of that sequence are done. `drop tables/` is not, because §7.6b is not.

### `levelrequire/` — ported as data, not as scripts

**No script from `levelrequire/` was ported, and that is the result rather than a
shortcut.** LostCity states 301 requirements as `[opheld2,<item>]` trigger lines
that claim the Wear verb and hand back to a content `~equip` proc reimplementing
equipping — wearpos conflicts, stackables, two-handed eviction. This server
already equips in C (`equip_from_slot`, reading wearpos_1/2/3 out of the cache)
and reads the requirement as data (`param=levelrequire,<stat>,<level>`), so
porting the scripts would have replaced a working engine path with a content one
needing eight opcodes it does not have — §7.7's hazard, in its purest form.

So the port is a transcription, and the comparison is the deliverable:

| | |
|---|---:|
| requirements LostCity states | 301 |
| names that do not resolve in osrs239 | **0** |
| **this tree already agrees exactly** | **200** |
| **disagreements** | **0** |
| absent here, level 1 (a no-op) | 71 |
| absent here, level above 1 | **30** |

**Zero disagreements across 200 items** is the number worth keeping: the
generated Kronos import and a wholly independent 2004-era source agree on every
value they both state. What the import missed is a *shape*, not a value — the
poisoned variants and the med helms. `adamant_dagger` was here and
`adamant_dagger_p` was not.

Landed as `server/scripts/skill_combat/configs/equipment_lostcity.obj`, a third
file rather than lines in `equipment.obj` (which is generated and says so). 77
records: 28 LostCity states and the base item here corroborates, 2 LostCity alone
(`excalibur`, `ibanstaff`), 47 derived to the further poison charges — and that
derivation is *evidenced*, not assumed: of the 11 base/poisoned pairs this tree
already states both halves of, 11 agree and 0 differ. The same derivation was
tested for helmets and **rejected** (`pvpa_maomas_med_helm` is Defence 1 where
its full helm is Defence 40), so the four med helms are LostCity's stated values
with nothing extrapolated from them.

`mock230_pack`: 1,419 → 1,496 requirement rows, 0 errors.

### Cook's Assistant — ported, and playable

`server/scripts/quests/quest_cook/`, plus the Cook's spawn and his roster block.
Every id re-resolved by name; one of the four moved (`cook`, npc 278 → 4626) and
three did not, which is §1's shape in miniature.

Two things the port needed that were not in the script:

- **The Cook was not in the world.** The imported roster had every other
  Lumbridge Castle npc and not him, so the quest had nobody to start it and
  nothing anywhere said so. Added at 3208,3213 — checked against `m50_50.jl2`
  (west wall at local x=4, the cooking range at local 12,15, the crafting tutor
  already at local 4,13) rather than guessed.
- **`^chat_scared`**, because the reference writes `<p,scared>` and the tree's
  expression table had seven of the eight. `cache.osrs239` has `chatscared1` at
  seq 596, so this is the cache's own animation and not a substitute.

Dropped, each stated at the point it happens: every `~p_choice` (linearised, so
**the player cannot decline the quest** — the reference's "No, I don't feel like
it" branch is gone); the quest journal (`questlist:cook` and `questscroll` are
IF1); `[oplocu,cooksquestrange]` (needs the `oplocu` trigger and `npc_find`);
`session_log`; the clue-scroll branch. `%cookquest` is varp 29 in *both* trees
and — checked — no varbit in `cache.osrs239` is based on it, so writing it whole
clobbers nothing. That check is the §7.5 discipline applied rather than described.

`make -C src test-mock230` grew a `cook's assistant` section that plays it end to
end: accept, talk again mid-quest without restarting, hand in, watch the reward
arrive **one tick late through the queue** (asserted as a negative first, which is
what catches a port that inlined it), and talk again after completion. It also
pins `cook == 4626` and `cookquest == 29` by name, so a cache bump says which
moved.

Two things it found on the way, both recorded in the test rather than papered
over: a resume loop must click **whichever button is registered**, because
`~chatnpc` arms `chat_left:continue` (231:5) and `~chatplayer` arms
`chat_right:continue` (217:5) and a fixed uid silently stalls on alternate pages;
and the combat selftest is **order- and RNG-dependent** — placed above it, this
section made "the goblin should have hit back" fail purely by ticking the world
a few more times first. That is a pre-existing fragility, not this section's bug,
but any future selftest inserted above the combat one can flip it.

### Spawns moved out of the map squares

**Spawns are server-only content and now live in a server-only file.** They used
to sit in the `==== NPC ====` / `==== OBJ ====` sections of
`maps/m<x>_<z>.jm2`, beside the terrain, which is what LostCity does and what
does not transfer: LostCity *authors* its cache from its content tree, so its
`.jm2` is a source file it owns end to end. This tree *receives* a cache and
`maps/*.jm2` is cachepack's output. Three costs came with the mismatch:

1. `cp_decode.c` needed a standing exception — "the codec owns MAP and treats
   every other section as somebody else's" — so that an unpack would not delete
   the spawns. A codec carrying an exception for content it must not touch is
   the problem, not the fix.
2. `maps/` could not be deleted and re-unpacked, which is exactly what
   `CONTENT_ARCHITECTURE.md` §4.1 means by *layer 0 becomes disposable*.
3. **Spawns were the last place in the tree carrying bare ids.** `0 8 13: 4626`
   cannot be checked against anything, cannot be renamed, and after a cache bump
   points at whatever now occupies 4626.

**(3) had already gone wrong, and rewriting the ids as names is what exposed
it.** Six of the 63 carry a prefix naming a content area nowhere near Lumbridge
— `sos_pest_` (Pest Control), `poh_` (player-owned house), `godwars_`,
`dragonslayer_`. The migration was done first and **byte-identically** (63 npc,
12 obj, every id/x/z/level equal to what the `.jm2` files produced — asserted),
because a migration that also changes values cannot be verified. The corrections
came after, as their own reviewable step.

#### How the six were judged

A prefix alone proves nothing — plenty of OldSchool variants are legitimately
used outside the area they are named for. Three independent tests, and only a
spawn failing all three was changed:

1. the name carries a foreign content-area prefix;
2. its combat level is out of line with the plain variant of the same creature
   this roster already spawns nearby;
3. **LostCity spawns the plain variant within a few tiles of it.**

Test 3 is what turns a suspicion into a finding, and it is available for free:
LostCity's own `maps/*.jm2` hold a 2004 roster for the same ground, built by
different people from different data. It is the same move that made §10.1's
equipment comparison worth anything — *a second source you did not write*.

Four failed all three and were corrected:

| was | npc | display | level | LostCity nearby | now |
|---|---:|---|---:|---|---|
| `sos_pest_giantspider1` | 2477 | Giant spider | 50 | `giantspider1` at 2 tiles | `giantspider1` |
| `poh_giantspider` | 134 | **Huge spider** | 81 | `giantspider1` at 3 tiles | `giantspider1` |
| `godwars_goblin4` | 2248 | Goblin | 15 | `goblin` at 1 tile | `goblin` |
| `godwars_goblin2` | 2246 | Goblin | 12 | `goblin` at 1 tile | `goblin` |

The Lumbridge Swamp had a level-81 Huge spider and a level-50 Pest Control
spider standing among the level-2 ones, and the goblin camp east of the river
had a level-15 and a level-12 God Wars goblin in it. Each corrected line carries
what it used to be, so the change is auditable rather than invisible.

**Two were left alone, and that is the more important half.** The
`dragonslayer_giantrat_*` pair (npc 3969/3970, "Zombie rat", level 3) passes
test 1 and arguably test 2, and fails test 3 outright: LostCity spawns *nothing*
within six tiles of either. With no second source there is no basis to say what
they should be, and "it looks wrong" is not one. They are marked `SUSPECT` in
place. Guessing there would be this document's own failure mode, committed by
the person fixing it.

#### The check is now permanent

`mock230_pack` warns when a spawn's name carries a foreign-area prefix **and the
un-prefixed creature is also spawned in this world**. Having both is what says
one of them is probably a mis-import; a prefix on its own is not enough, so once
God Wars is a real area with its own `.spawn` file, `godwars_goblin2` standing
there alone stays silent — which is correct.

```
warn  sos_pest_giantspider1 is spawned here and so is `giantspider1` — a name
      qualified by another content area beside the plain one is usually an
      id-imported roster; check both against a second source
```

A warning, never an error: it is a prompt to go and find a second source, not a
fact. It is silent on the shipped tree and was verified to fire by putting one
of the four back.

#### The format, and the rule for every future port

`server/scripts/**/*.spawn`, walked like `.npc` and `.varp`:

```
==== NPC ====
cook                         3208 3213 0

==== OBJ ====
bones                        3210 3215 0 28
```

The `====` markers are the ones the `.jm2` used, kept so the file reads the same
way to anyone who knows that format. Two things differ, and both are the point:
**names, not ids** (resolved against `configs/all.npc.compack` /
`all.obj.compack`, and an unresolved one is a load error), and **absolute world
tiles**, not `local_x local_z` within a square, because the file is no longer
keyed by a square.

> **Porting rule.** A LostCity `.jm2`'s terrain is an asset and is not ported.
> Its `==== NPC ====` and `==== OBJ ====` sections are content and **are**, into
> a `.spawn` file beside the area's other configs — never back into a `.jm2`.
> Convert `level local_x local_z: id` to `<name> <x> <z> <level>`, with
> `x = map_x * 64 + local_x`, and re-resolve every id to a name through
> *LostCity's* pack and then to osrs239's table. 97.8 % of the npc spawns and
> 100 % of the obj spawns land; the 24 names that do not are the same
> npc-naming-drift tail as §4, so a `.spawn` file is also where that tail
> becomes visible instead of becoming a wrong creature.
>
> **Then read the names you just wrote.** Migrate first and byte-identically, so
> the move is checkable; correct afterwards as a separate step. Any name
> qualified by a content area — `sos_pest_`, `poh_`, `godwars_`, `raids_`,
> `slayer_`, a quest prefix — is a question, not a verdict: change it only when
> a **second roster covering the same ground** agrees, and mark it `SUSPECT` in
> place when none does. `mock230_pack` will prompt you; it cannot decide for you.

**Two second sources exist and both are cheap.** They are the reason §10.1 and
§10.2 each found a real defect instead of restating what was already believed:

| what | second source | what it caught |
|---|---|---|
| equipment requirements | LostCity's `levelrequire/` vs the Kronos import | 30 items the import had no row for — a whole *shape* (poisoned variants, med helms) |
| Lumbridge spawns | LostCity's `maps/*.jm2` roster vs the OpenRune import | 4 npcs from the wrong content area, up to level 81 in a level-2 swamp |

Neither was found by reading the destination harder. **Whenever a port has a
second source for the same fact, spend it** — and where it has none (the two
`dragonslayer_giantrat_*` spawns), say so and stop, rather than substituting
judgement for evidence.

Both failure paths are loud, and deliberately so. A `.jm2` that still has a spawn
section is an error naming the file and the fix rather than a silent skip —
otherwise an unmigrated tree boots with **zero spawns and no message**, which
reads as a scene bug rather than a content one:

```
maps/m50_50.jm2:6227: `0 8 13: 4626` is a spawn, and spawns are server
content — move this square's ==== NPC ==== / ==== OBJ ==== sections into a
.spawn file under server/scripts/
```

`cp_decode.c`'s preservation rule is now vacuous rather than load-bearing.
Removing it is a one-line follow-up for whoever owns cachepack; it is harmless
where it is, and it is the last thing standing between `maps/` and being
genuinely disposable.

### `oc_param` — §9 step 4a, and the blocker that was not one

§5.1 called the param family "blocked on data rather than on effort", quoting
`osrs230_mockserver.md` §3.13d: *no decoder here keeps a general per-record param
table to answer that from*. That was true of the decoders. It was **not true of
the tree** — `configs/all.param` has carried the declared type of all 2,634
params the whole time, unpacked by cachepack and read by nothing, because
`configs/` is write-only (`CONTENT_ARCHITECTURE.md` §3.5).

So the highest-leverage item in this document was gated on a file that already
existed. That is worth stating plainly: **the blocker was a stale belief, and
nothing in the tree would have corrected it** — the data sat one directory away
from the code that said it was missing.

Landed:

- **`mock230_obj_param`** — every param on every obj record, 53,853 rows kept
  from the decode pass as one flat sorted array (1.26 MB, printed at load so it
  is not a claim). Per-obj vectors would be 11,712 allocations for the same
  bytes, and this tree has already been through one pass of shrinking the boot
  heap. Boot RSS is 130 MB, so the table is about 1 %.
- **`mock230_content_param_type`** — the declarations, from `configs/all.param`.
  This is the **first runtime reader of anything in `configs/`**, which makes
  §3.5's "write-only" one third less true.
- **`SS_OP_OC_PARAM`**, typed by the declaration, because that is what the script
  was compiled against: a script that wrote `oc_param($obj, some_string_param)`
  has a string local waiting, and pushing an int leaves the two stacks out of
  step for the rest of the script rather than failing at the call.

Three details worth keeping:

- **1,517 of the 2,634 params declare no type at all** — the config's type
  opcode is optional. For those the value's own stored kind decides, and that is
  not a guess: the record says whether it wrote four bytes or a NUL-terminated
  string. Measured, no param that any obj actually carries is undeclared, so the
  path is defensive rather than load-bearing today.
- **A declaration that disagrees with the stored kind aborts.** Which half to
  believe is not the opcode's call to make.
- **An obj that does not carry the param pushes the declared `default=`**
  (0 when none is declared), matching the reference — `oc_param($obj, specwep)
  = ^true` is asked of every weapon and only special-attack weapons answer it
  (specwep declares no default, so absent reads 0), while 365 params declare
  `default=-1` so that absence spells "no id" rather than obj 0.

#### The bug the test caught, which is the reason the test exists

The first version appended param rows in decode order and claimed the array was
"sorted by construction, because the decode loop walks obj ids in order". Obj
ids yes; a *record's own* params, no — the cache writes them in its own order.
A binary search over an almost-sorted array does not crash, it misses:
`oc_param(magic_longbow, rangeattack)` returned nothing where the cache says 69,
which reads as "this obj has no such param" — a perfectly ordinary state that
content handles by pushing 0. It would have been wrong everywhere and loud
nowhere. There is now an explicit `qsort`.

The string half caught the other one. The test first compared with
`if ($verb ! "Rub")`, which compiles to an int comparison and underflowed the
int stack. The version that survives assigns to a `def_string` local *first* —
that pops the string stack, so a value pushed onto the wrong stack aborts
immediately — and only then compares. **An int-stack assertion on a string param
passes even when the value is on the wrong stack**, which is precisely the
failure `runtime_typed` names, so a test that could not tell them apart would
have been worse than none.

Both are driven by `[proc,selftest_oc_param]` and
`[proc,selftest_oc_param_string]` in `server/scripts/selftest.rs2`, against
`cache.osrs239`'s own values (`magic_longbow` rangeattack 69, attackrate 6;
`amulet_of_glory` param_451 `"Rub"`).

`nc_param`, `lc_param` and `struct_param` are the same shape over the npc, loc
and struct tables. They are deliberately **not** in this change: the machinery
is what was hard, and doing three more at speed on the back of it is how a
plausible-but-wrong implementation gets in.

### 10.3a `nc_param`, and the third mutation

`nc_param` landed next, over `mock230_npc_param` — 29,869 rows in 700 KB, the
same flat sorted array and the same explicit `qsort`. No new decoding: the npc
records' params were already being walked by `read_combat_params`, which kept
fourteen keys and dropped the rest.

The stack choice is now one shared `push_typed_param` rather than a copy per
table. Two tables with two copies of the declared-vs-stored disagreement is two
places for it to be handled differently, and that disagreement is the only thing
in the family worth being loud about.

Three mutations were run against the new tests, because §10.3's whole lesson is
that a test which cannot fail is worse than none:

| mutation | what it broke |
|---|---|
| drop the `qsort` | 2 assertions — the table check and `[proc,selftest_nc_param]` (stops at case 0) |
| force every result onto the int stack | both string procs, `oc_param`'s *and* `nc_param`'s |
| treat only `type=i` as int-typed, not "anything but `s`" | `[proc,selftest_nc_param]` alone, stopping at case 3 |

The third is the one worth keeping. `param_46` is declared `type=o` — an obj id —
and the VM has one int stack for every non-string type, so it must land there
exactly as a plain `i` does. **No `oc_param` assertion catches that mutation**:
that suite only ever exercises `i` and `s`, so a handler special-casing `i`
passes it completely. The npc suite covers it because `dagcave_ranged_boss`
carries both a `type=o` param and the table's only string param.

Values are `cache.osrs239`'s own (`goblin` strengthbonus −15 and param_50 2,
`dagcave_ranged_boss` param_46 6729 and param_510 `"Dagannoth Kings (Echo)"`).
The negatives are deliberate — every bonus a goblin carries is −15, so a table
that read the field unsigned would still answer something.

**Fixed since:** both opcodes now answer an absent row with the `default=` the
param's `configs/all.param` block states (469 declare one, 365 of those `-1`),
and the selftest pins the absent-with-default cases through both tables. What
remains — `defaultstr=` unread, overlay `.param` declarations not walked — is
recorded in `docs/osrs230_mockserver.md` §"What oc_param and nc_param still
get wrong".

While regenerating the coverage table for `NC_PARAM`, `gen_opcode_coverage.py`
also added `4209 OC_PARAM`: the checked-in header had been stale since `oc_param`
landed. `make -C src test-mock230-coverage` was the target that would have said
so, and the failure direction is the safe one — it under-reported an implemented
opcode rather than hiding a missing one.

### The loc mutation family — §9 step 4b

The unlock §5.2 named: every `skill_*` directory in the LostCity tree sits at
0 % opcode readiness because every one of them is the same loop — find the
tree/rock/altar, change it to the depleted form for N ticks, let it change back.

Eight opcodes landed (189 of 396 now): `loc_find`, `loc_coord`, `loc_type`,
`loc_angle`, `loc_shape`, `loc_change`, `loc_del`, `loc_add`. That is ~612 of
the family's 1,033 uses.

Most of the machinery already existed and was not being reached: `mock230_scene`
had `find_loc` and `replace_loc` for doors, the wire had `LOC_ADD_CHANGE` and
`LOC_DEL`, and the VM had `SSVM_ENT_LOC` with a `SSVM_PTR_ACTIVE_LOC`
requirement the meta table already declared on every loc-scoped opcode. What was
missing was three things:

- **`mock230_scene_add_loc` / `_remove_loc`** — the scene could swap a loc but
  not create or destroy one. `add_loc` reuses a slot a `loc_del` freed before
  growing the array, because slots are never compacted (a script can hold one
  across a suspend) and content that cycles a loc on a timer would otherwise
  leak one per cycle.
- **A revert queue**, drained in tick phase 8 (`zones`), where the reference
  puts loc and obj respawn. This is the half that makes a skilling loop a
  *loop*: without it `loc_change` is a one-way ratchet and the first player to
  mine a rock removes it for the session. Duration 0 means never, matching the
  reference, and the `+1` is the same one `p_delay` has.
- **Holding the active loc by scene slot, not by pointer.** Same reason the
  active npc is held by slot: a script can suspend between `loc_find` and
  `loc_change` and a scene rebuild reallocates the array underneath it. The slot
  rides in the VM's active-entity pointer as `slot + 1`, so non-NULL means "a
  loc is active" and the VM's own requirement check works unmodified.

`loc_param` is deliberately absent — it is `runtime_typed` like `oc_param` and
needs a runtime loc-config decode that does not exist (only `mock230_pack.c`
decodes loc configs today, for validation). That is 60k+ records at boot and a
real memory question, not a copy of what landed here. `loc_findallzone` /
`loc_findnext` need the ZoneMap from §7.2.

#### A pre-existing oddity the test walked into

`mock230_scene_find_loc` ends with

```c
return loc_id >= 0 ? fallback : fallback;
```

Both branches are the same, so **the `loc_id` argument does not filter the
result** — the function returns the first loc on the tile whatever id you ask
for. That is deliberate for its original caller (an OPLOC carrying a stale id
must still resolve to the door somebody else already opened, which the comment
above it explains) and useless for asking "is *this* loc still here". The first
version of the `loc_add` test used it and reported the loc present after it had
expired, because 3220,3220 carries other locs. The test now asserts on the slot
`loc_add` returned instead.

The expression is left alone: the behaviour is intended and relied on, and only
the ternary is misleading. It is worth knowing about before anything else reaches
for `find_loc` as an existence check.

### The npc family and the find-all iterators — §9 step 4c

Fifteen more opcodes, in two groups.

**Addressing, lifecycle and reads** (189 → 198): `npc_find`, `npc_findexact`,
`npc_finduid`, `npc_add`, `npc_del`, `npc_tele`, `npc_range`, `npc_stat`,
`npc_basestat`. `npc_find` alone was in front of 139 files — it is how content
addresses an npc that is *not* the one who triggered the script, and everything
else in the family rides on it. The active npc is held by slot in `host_tag`,
the same way the active loc is and for the same reason.

**The find-all iterators** (198 → 205): `npc_findall`, `npc_findallany`,
`npc_findnext`, `loc_findallzone`, `loc_findnext`, `huntall`, `huntnext`.

These closed `test-mock230`'s **"the content tree should not use unimplemented
opcodes"** failure outright — down from 8 missing opcodes to 0. That failure was
never about the port: `[proc,npc_findcount]`, `[proc,loc_within_distance]` and
`[proc,sound_area]` are already-committed content that could not run.

So the test for them is that content. `[proc,selftest_iterators]` calls
`~npc_findcount` and the C side counts the same goblins with its own walk; the
two have to agree. Nothing is hardcoded, so changing the Lumbridge roster cannot
fail it for the wrong reason.

Three things worth stating:

- **`*_findnext` sets the active entity**, it does not merely return an id. The
  loop body is `while (npc_findnext = true) { if (npc_type = $npc) ... }`, so a
  version that returned the id would run the right number of times over the
  wrong npc.
- **One iterator, not one per script.** The reference has the same single global
  cursor, for the same reason there is one script-parking slot per player. A
  script that suspends mid-loop and resumes after another has iterated sees the
  other's list. Stated rather than fixed, because fixing it means a per-state
  cursor the VM has no room for yet.
- **The list is re-checked as it is walked**, because the body can kill what it
  is iterating over — a `loc_del` inside a `loc_findnext` loop is ordinary
  content.

> **Superseded, 2026-07-31.** `npc_setmode` **is implemented**
> (`src/net/mock/mock230_scripts.c:1258`) and the `mock230 selftest: npc modes`
> case passes. The npc mode machine this paragraph called for was built. Do not
> quote the claim below as a live blocker — it was verified stale by re-reading
> the source, which is the check this file's own §12 asks for.

~~`npc_setmode` is deliberately absent and is now the single biggest blocker left
(122 files). It needs an npc mode machine — `playerfaceclose`, `wander`,
`playerescape` are standing states an npc holds across ticks — which this server
does not have; `interface_chat/scripts/chat.rs2` already notes the gap where it
works around it. That is a feature, not a copy of anything landed here.~~

### The cheap wide ones, and npc deferred work — §9 steps 4e and 5a

Seven more opcodes (205 → 212), none of which needed a new subsystem and which
together bought more files than anything before them.

**`finduid` / `p_finduid` / `session_log` / `gender` / `text_gender`** — 198
files. `p_finduid`'s negative case is the whole point of it: content asks *"is
the player I remembered still here"*, so a uid naming nobody must answer false
rather than abort. A version that aborted would pass every positive test and
break every caller.

`text_gender` needed a fact the server did not have. The appearance encoder had
`rsab_p1(buf, 0); /* gender: male */` — a constant standing in for state, and an
opcode cannot be implemented against a comment. `Mock230Player.gender` now backs
both, so the answer comes from one place. Nothing sets it (there is no
character-design flow here) and every player is still male; what changed is that
this is now a *field with no writer* rather than two literals that could drift.

**`npc_queue` / `npc_settimer`** — 67 files, and half the state was already
there: every npc carried `timer_script`, `timer_interval` and `timer_clock`, and
nothing ever set or read them. Phase 4 now runs queues then timers, in the
reference's order, dispatching by npc *type* so an npc that changed type between
queueing and firing runs the new type's script.

The test asserts the negative for both: that `npc_settimer(0)` **stops** the
timer, which is the only way to tell "stopped" from "not due yet", and that a
`[ai_queue1]` does not fire early.

### npc modes — the last large item

`npc_setmode` was in front of 122 files, twice anything else. The opcode is one
line; what it implies is a **standing state the npc holds across ticks**, so the
work was giving phase 4 somewhere to run one.

Measured first, which is what made it a day instead of a week: the tree's 253
`npc_setmode` calls name eleven modes, and **162 of them are `none`/`null`** —
"stop what you were doing". The distribution:

```
none 86   null 76   opplayer2 73   applayer2 42   playerface 16
playerescape 14   playerfaceclose 8   playerfollow 7   patrol 4   wander 2
```

Implemented: `none`/`null`, `wander`, `playerescape`, `playerfollow`,
`playerface`, `playerfaceclose`, and the `opplayer1..5` / `applayer1..5`
families. That is 249 of the 253. `patrol` and the loc/obj/npc-targeted modes
are not: they carry a *target* the mode field has nowhere to put. An unhandled
mode warns once per npc type and falls back to `none`, because an npc quietly
standing still is the failure that reads as "the script did not run".

Two decisions worth recording:

- **Wander is the default, not the absence of a mode.** An npc with a radius
  starts on `wander`; one without starts on `none`. If roaming were what
  happens when no mode is set, `npc_setmode(none)` would be
  indistinguishable from "never had one" and every roster npc would carry on
  roaming through it. The selftest asserts exactly that — park a follower, set
  `none`, tick ten times, assert it has not moved.
- **`opplayer<n>` is an errand, not a state.** It walks the npc to the player,
  fires `[ai_opplayer<n>]` *once*, and drops back to `none`. An npc left in the
  mode would re-fire its trigger every tick from then on, which is why the test
  ticks past the arrival and asserts the count is still 1.

`playerfollow` is what the test leans on, because it has a stopping condition: a
mover that merely walks toward the player looks right for several ticks and then
stands on top of them.

### Where the number stands

§1's headline metric, re-measured after 4a and 4b:

```
scripts using only implemented commands   636/1,265 (50.3%)  ->  873/1,265 (69.0%)
opcodes implemented                       179/396            ->  214/397
mock230 selftest failures                 2                  ->  1
```

The remaining blockers, re-measured:

| opcode | files still blocked | note |
|---|---:|---|
| `map_findsquare` | 54 | find a free tile near a coord |
| `loc_param` | 52 | needs the runtime loc-config decode |
| `spotanim_map` | 48 | the wire exists (`MAP_ANIM`); this is a host command over it |
| `npc_walk` | 44 | needs npc step queues |
| `inv_setslot` | 33 | trivial |
| `db_find` | 23 | the db layer exists (`mock230_db.c`) |

**Nothing large is left.** Every remaining item is a day or less, and the
biggest single one is now a fifth the size of what `npc_setmode` was. The list
has also stopped being dominated by one family — which is the point at which
the useful question changes from "what is blocking scripts" to "which *content*
do we want", i.e. §9 step 7.

### Still red, and not from this work

`make -C src test-mock230` has two failures that predate all of it and belong to
the field-register work in flight in the content submodule:

```
FAIL and the cache's own name for it should still resolve too, got -1
     mock230_content_symbol(MOCK230_PACK_VARP, "randomhitsound") == 843
FAIL the content tree should not use unimplemented opcodes
     HUNTALL, HUNTNEXT, NPC_FINDALLANY, NPC_FINDNEXT,
     LOC_COORD, LOC_FINDALLZONE, LOC_FINDNEXT, LOC_TYPE
```

The second is §9 step 4b/4c arriving from another direction — `[proc,sound_area]`,
`[proc,npc_findcount]` and `[proc,loc_within_distance]` are already in the tree
and want the `loc_*`/`npc_*` families this document ranks as the skilling unlock.
`make -C src test-content` and `mock230_pack` are both green (0 errors).

### The interface half — §9 step 6, and four bugs under it

The interface work this document ranks as "the wall" (§7.3) turned out to have
four defects sitting *underneath* it, none of them interface work and all of
them presenting as one. They are worth recording as a class, because each was
invisible in exactly the same way: the thing that failed reported nothing, and
what the player saw was an unrelated panel being empty.

**1. `%varbit` was broken in every script in the tree.** `SS_OP_PUSH_VARBIT` and
`SS_OP_POP_VARBIT` took the varbit id off the *int stack*; the compiler puts it
in the *instruction operand*, exactly as it does for varp. So `PUSH_VARBIT`
underflowed and `POP_VARBIT` read the value as the id and then underflowed
looking for a value. No content had used a varbit yet, so nothing had noticed —
and the first line that did took `[login,_]` down with it, silently, which meant
the opening state (`com_mode`, `option_nodef`, `sa_energy`) also stopped being
sent. One wrong operand source, four symptoms, none of them named a varbit.

**2. The client decoded obj configs with no revision flags.** Every other config
type in the client already used the profile-aware entry point; obj used
`RSCache_Dat2ConfigObjNewDecode`, which passes flags 0. The obj decoder's
`default:` case is `return false` — *stop rather than misalign* — so a rev-239
record carrying opcode 160 or 200-202 stopped there, and `params` (opcode 249)
is written last, so it was always past the stop. **Every objtype in the client
reached the CS2 VM with `param_count == 0`.** The prayer book is built from
`oc_param(<prayer obj>, param_1751)`, so it rendered as an empty panel; whatever
else reads an obj param was wrong too and had not been looked at.

**3. Nothing armed anything.** §7.3 sizes the interface gap as ids and layout.
The larger half is permission: at rev 230 a component is inert until the server
sends IF_SETEVENTS for it, and content had no way to say so — there is no
reference `[command,if_setevents]` (only a commented-out nine-argument form in
`engine.rs2:1042`, from a later dialect, that does not match the packet). Two
commands were added in a reserved band past the reference's highest opcode:

```
11000  if_setevents(component, from, to, events)
11001  if_opensub(component, interface, type)
```

`docs/UI_ERA_PORTING_GUIDE.md` is the companion to this: what changes across
LostCity → Kronos → OpenRune, which reference answers which question, and the
procedure for converting one interface script.

**4. A trigger subject wider than 21 bits.** `[if_button,orbs:runbutton]` is
`160 << 16`, and the compiled lookup key has 21 bits for its subject. The format
is LostCity's and `test-ss-roundtrip` proves this compiler reproduces it byte for
byte, so it was not widened: those scripts compile **name-addressed** and the
engine resolves them by name through the same component pack the compiler read.
`bankmain` is interface 12 and fits, which is why the bank's buttons had always
worked and hid the limit.

Landed as content, not C: `interface_orbs/` (Toggle Run and the special-attack
bar, both armed and both answered), `interface_journal/` (the side journal's four
tabs), and `varbit=` on all 29 prayers so the book draws the lit border from the
cache's own `prayer_<name>` varbits rather than from a mask only the server could
see.

**Still open, and each is a client bug rather than a content one:**

| symptom | what is actually wrong |
|---|---|
| the side journal (629) draws nothing | it mounts, its onload runs, `TORIRS_DUMP_BOUNDS=629` shows sane boxes — but `TORIRS_DUMP_TREE_EXIT` shows its children laid out at `abs=1100,185` with heights like `18x-2`. A layout defect, and the only sidebar tab whose body is a second-level mount |
| chat lines never appear | `MESSAGE_GAME` arrives and `RS_Chat_AddMessage` stores it; the rev-230 chatbox has no `UITREE_SLOT_CHAT` node, so `app_chat_region` returns 0 and the built-in renderer draws nothing. IF3 chat lines are cc_created by clientscript |
| `runclientscript` takes ints only | blocks `~p_choice*` (§7.4) *and* every modern panel populated by a script call rather than a var |

---

## 11. What I propose not to port

| | why |
|---|---|
| **All 5,810 assets** — 3,895 `.ob2`, 458 `.jm2`, 696 `.synth`, 334 `.anim`, 314 `.mid`, 55 `.png` | §3. Every referenced obj already exists in osrs239 with a better model. 1,696 of them came *from* osrs239 via `port_lostcity` and porting them back is a lossy round trip. Maps would overwrite the geometry the client draws |
| **`scripts/_unpack/` (4 revisions)** | LostCity's own cache-bump review queue. Machine-generated. This tree has its own (`configs/_unpack/`) |
| **`scripts/_test/`** | cheat/harness scripts. 20 of the first 160 compile failures. `::` commands cover the same ground |
| **The 4 `split_*` opcodes (244 uses)** | §7.4 — they measure a string to pick one of four fixed chat interfaces; rev 230 has one that wraps itself. Compile them away rather than implement them |
| **~140 of the 159 `.if` interfaces** | §7.3. Rebuild `questlist`, `chatmenu`, `levelup`. Decide the rest when a ported script needs one; most never will |
| **Minigames (67 scripts, 28.4 % opcode-clean) and the deep quests** (`quest_legends` 37 missing ops, `quest_grandtree` 23, `quest_upass` 25) | not "never" — "not until §9 step 4 and 5 are done". Porting them now means porting them twice |
| **`nc_desc` and anything else derived from a field the cache does not carry** | `examine-desc-session`; `osrs230_mockserver.md` §3.13d. A loud stub beats a plausible guess |

---

## 12. How to re-measure

Everything above is reproducible. Working files are under
`…/scratchpad/lcport/`:

| file | what it produces |
|---|---|
| `census2.py` | the §4 resolution table, the §4.1 display-name diff, per-namespace unresolved lists (`census2.json`) |
| `loop.sh` | the §6 compile census — compile, record first error, quarantine, repeat (`errors.txt`). Note its limit: past ~200 files the quarantine cascade dominates, so filter out `no proc/label named` before reading it |
| `levelreq_diff.py` | the §10.1 equipment-requirement comparison. **Re-run it after any `kronos_item_import.py` run** — `equipment_lostcity.obj` is disjoint from `equipment.obj` by construction and nothing else checks that |
| `opcodes.json` | the §5 missing-opcode table with per-op use and file counts |
| `perfile.json` | the §5.2 per-script opcode readiness split |

The two source-of-truth tables the census reads are
`OSRS-Content/osrs239-content/configs/all.<type>.compack` and
`.../pack/*.pack` on this side, and `<lostcity>/content/pack/*.pack` on the
other. The engine's opcode coverage is `src/net/mock/mock230_opcode_coverage.gen.h`,
which is generated, so the §5 numbers move when the engine does.

Baseline on the destination tree, unchanged, today:

```
mock230_pack: 0 error(s), 1 warning(s)
content loaded (213430 symbols, 136 constants, 38 npc defs, 776 loc defs,
                16 varp defs, 29 prayers, 1419 equip reqs, 62 npc spawns)
```

---

## 13. The bars, restated as they apply here

From the brief, with what each one costs given the measurements:

1. **Every unresolved name is an error at pack time, never a default.** §9 step 2.
   206 names will fail on the first run (44 npc, 88 loc, 44 seq, 30 spotanim)
   plus 147 varps and 1,380 interface references. That is the *point* — the list
   is the work queue.
2. **`make -C src test-content` passes; `mock230_pack` reports 0 errors.** It does
   today; nothing has been changed.
3. **`mock230` boots with no "did not resolve".** Requires 1.
4. **The existing 38 npc / 776 loc Lumbridge content keeps working.** This is an
   addition — but the first slice lands **on top of an existing port of the same
   npcs**. `server/scripts/areas/lumbridge/scripts/` already holds `bob.rs2`,
   `hans.rs2`, `father_aereck.rs2`, `citizens.rs2` and `tutors.rs2`, binding
   `[opnpc1,bob]`, `[opnpc3,bob]`, `[opnpc1,hans]`, `[opnpc3,hans]`,
   `[opnpc1,father_aereck]` among others. LostCity's `areas/area_lumbridge/`
   has 12 scripts, **three of which bind the same triggers on the same npcs**
   (`bob`, `hans`, `father_aereck`). Those three are a hand merge, not a copy,
   and that merge is a prerequisite of the first slice rather than a detail —
   a second `[opnpc1,bob]` is a duplicate-trigger conflict, not an override.

I would add two:

5. **No content file carries a bare id.** Spawns were the last exception and are
   no longer one (§10.2). The rule is what makes bars 1 and 3 mean anything: an
   id has nothing to resolve, so it cannot fail to resolve, so a gate that only
   checks names cannot see it. `sos_pest_giantspider1` is what that costs.

6. **A display-name diff is reviewed for every resolved record before it lands.**
   §4.1. Name resolution proves a spelling exists; only this proves it means the
   same thing, and `goblin_armed`/`goblin_cook` are the precedent for what it
   costs when nobody looks.

---

## 14. §9 step 2, as landed — the name-resolution gate

Written 2026-08-01. Bars 1, 5 and 6 are enforced now; this records what each one
turned out to be, what it cost, and the four things the pass found that no
section above predicted.

**Baseline, re-measured before anything changed** (§12's block is stale on all
seven numbers, so use this one):

```
mock230_pack: 0 error(s), 13 warning(s)
content loaded (213530 symbols, 284 constants, 39 npc defs, 776 loc defs,
                39 varp defs, 1496 equip reqs (675 from the cache),
                63 npc spawns, 12 obj spawns)
server band: 2973 archive(s) verified against the text parse
```

The 12 extra warnings are all `has a combat block but the cache gives it no
Attack op`; none is a regression.

### 14.1 What was actually unguarded

The gate was not one hole. It was four, in four different layers, and each one
answered a wrong name with a *plausible* value rather than with silence — which
is why none of them had ever produced a failing check.

| layer | what it did with a name nothing knows | now |
|---|---|---|
| `mock230_content.c` `apply_param` | `mock230_content_symbol` maps the literal `null` to -1 **by design**, and maps a miss to -1 too. So `param=death_drop,bones_TYPO` loaded at **0 errors** and the npc dropped nothing — indistinguishable from `param=death_drop,null` | `mock230_content_symbol_checked` separates the two; the four symbolic npc params go through it |
| `mock230_content.c` `load_param_types` | a `[block]` in `configs/all.param` naming a param its own `.compack` does not list was skipped in silence, leaving `type` 0 and `default` 0 — so `push_typed_param` answered every absent row with 0 instead of the declared default | a content error naming file and line |
| `cachepack` `pack_server_type` | a field value that named nothing was counted, the field was **skipped**, and the band read back as *absent* — which the param-defaults path answers with the declared default. A second silent default one layer below the first. `cp_pack_server_run` returned 1 regardless | the value is an error and reaches the exit status |
| `sscompile` | a constant declared twice was appended twice and an **unstable** `qsort` picked the winner, while `mock230_content.c` errors on the second declaration — the compiler was *looser than the server it feeds* | `SSC_SymbolsValidate`, fatal before the first line is compiled |

The `cachepack` row had a second defect underneath it. `tally[f].unresolved`
counted two different facts under one message, and the message described only
one of them — *"the register declares no `ref` namespace for it"*. A field that
**did** declare a `ref` and named something the pack has never heard of printed a
line blaming the register. They are separate counters now: `no_ref` is the
declared gap (`huntmode = aggressive`, 10 values, no cache namespace) and
`unresolved` is an error.

Turning the exit status on needed one more thing first. `[default]` — the npc
defaults block, this tree's own invention, documented in
`general/configs/npc_default.npc` — has no id by construction, and was being
counted as an unresolved name. **One legitimate no-id record sat permanently in
the bucket, which is the whole reason the bucket could never be made fatal.**
`server/pack` is byte-identical across the change (2973 archives, same
`shasum`).

### 14.2 The bars, and where each one lives

- **Bar 1, unresolved names.** Three exact checkers, one per layer: `sscompile`
  (scripts — it already refused), `mock230_pack` via the content loader
  (configs), `cachepack` (the server band). Plus `tools/ss_unresolved.py --check`
  over the authored **config** half, which had no per-field checker at all.
- **Bar 5, no bare ids.** `ss_unresolved.py --check` reads `fields/<type>.ini`'s
  `ref =` rows — the register is the authority, so there is no second list — and
  refuses a numeric section header, a numeric value for a `ref` field
  (`param=death_drop,526`), and a numeric first column in a `.spawn`. `-1` is
  allowed: it is `null` spelled as a number and names no record.
- **Bar 6, the display-name diff.** `tools/port_name_diff.py`, signed into
  `<tree>/port/name_diff.signed`.

Both fold into a new `make -C src test-port`, which `test-content` depends on.
No new top-level target, for the reason the makefile already gives about
aggregate bars: a check in a command nobody types is not covered.

### 14.3 The name diff, measured

4,270 names are present in both trees and stated by a record on both sides:

| namespace | rows | identical | formatting-only | plausible-sibling | different-thing | unnamed | shape-differs |
|---|---:|---:|---:|---:|---:|---:|---:|
| obj | 1,949 | 1,347 | 30 | 404 | 41 | 127 | — |
| loc | 1,319 | 881 | 20 | 59 | 11 | 348 | — |
| npc | 898 | 679 | 53 | 32 | 33 | 101 | — |
| seq | 99 | 88 | — | — | — | — | 11 |
| spotanim | 5 | 1 | — | — | — | — | 4 |

§4.1's three counts reproduce exactly — npc 898, obj 1,947 (1,949 after the
reference gained two objs mid-run, see §14.5), loc 1,319 — and so do two of its
three `unnamed` columns to the record, npc 101 and loc 348. Its "same display
name" column is *looser* than `identical` here (722 against 679) because this
separates `formatting-only`; 679 + 53 = 732 is the comparable figure.

`seq` and `spotanim` state no display name, so their verdict is structural: a
seq's frame count and a spotanim's model. Both trees spell the same fact two
ways — this tree repeats `frame=` where the reference numbers `frame1..frameN`,
and the reference names an asset `model_2393_obj` where this tree writes `2393`
— so the tool reduces both before comparing. **88 of 99 seq frame counts agree**,
which is the cross-check §9 step 3e wants. The four `spotanim` rows that differ
are not a finding: a spotanim's `model` is an id in each tree's *own* model
namespace (`3094` there, `16940` here), so it is not comparable across eras and
the port manifest is the only thing that can answer it.

**The worked example the gate exists for is in the corpus and signed:**

```
obj rock_sample1   id 671 in BOTH trees — the name resolves, the ids agree
    LostCity: 'Rock sample 1'  model_2393_obj
    osrs239:  'Animal skull'   model 17290
```

Every check in this repo passed on that row before today and passes on it now;
what changed is that it carries a `wrong-record` signature. A `wrong-record` row
is fatal only once *this* tree's authored content names it — otherwise recording
the finding would break the build, and a bar that punishes honesty gets signed
`ok` instead.

The 4,270-row baseline is signed `unreviewed` wholesale, deliberately: the bar
arms today and what it catches from today is a row **changing class** or a new
row appearing unsigned, which is the event a review would have been looking for.

### 14.4 What copying an id would land on — §7 item 1, re-measured

`port_name_diff.py --collisions` over 14 namespaces both trees number:

```
1,329 names would land on a different record if the id were copied
    0 would land on nothing            ← there is no fails-loudly case, anywhere
   10 would land on a lexically similar name
```

Per namespace: npc 1,073 · loc 174 · dbrow 28 · param 23 · varp 21 · enum 4 ·
dbtable 3 · seq 2 · obj 1.

The ten that survive review are the class worth naming:

```
seq  human_knife_slash  LC 911  → this tree's 911 is human_knife_chop (real id 3747)
loc  elemental_workshop_valve_1  LC 3404 → ..._valve_2   (real id 3403)
loc  elemental_workshop_valve_2  LC 3405 → ..._wheel
loc  palacedoor_l  LC 1575 → towered_gateway_l
loc  statue_king_waterfall_quest LC 2005 → stonepillar_small_waterfall_quest
npc  ogre_guard3   LC 860  → zogre_ogre_shaman
varp com_slashattack LC 46 → com_stance      varp com_stabattack LC 44 → com_ammo
varp prayer_drain_counter LC 98 → prayer15
enum displaymessage_enum LC 53 → enum_53
```

The valve pair is a **shifted chain, not a swap**, which is why a 2-cycle sweep
misses it. The one true 2-cycle in the corpus is the documented one and the tool
finds it: `blackarmgang` LC 145 → this tree's 145 is `phoenixgang`, and the
reverse.

### 14.5 Four findings this pass produced that §1–§13 do not contain

1. **The reference checkout is not frozen and is being written to.**
   `LostCity_Server/content` was dirty mid-run — `scripts/skill_combat/configs/ghrazi.obj`
   and `vitur.obj` were added between the baseline and the first `--check`, and
   the gate correctly reported two new unsigned rows. `test-ss-roundtrip` also
   failed once against a `script.dat` that was being rewritten as it was read,
   and passed on re-run. **Any bar computed against the reference is only as
   reproducible as that checkout**, and a stage that re-signs the baseline is
   partly recording the reference's state, not this tree's.
2. **The script half of `ss_unresolved.py` cannot be a bar and does not need to
   be.** It flags 74 names in this tree's `.rs2` — all provably false positives,
   because `sscompile` refuses an unresolved name and compiles all 319 scripts.
   The heuristic cannot see argument position. The **config** half can be exact,
   because the types are declared: `.enum` states `inputtype`/`outputtype`, and a
   `.dbrow`'s `data=` lines are typed by its `.dbtable`'s `column=` rows. Reading
   those declarations is what took the config check from 35 false positives to 0
   without a whitelist.
3. **`%name` ambiguity costs nothing to forbid today, and only today.**
   `content.ini`'s `vardomain` column declares varp/varbit/varn/vars to share one
   name domain; `mock230_content.c` enforced it and `sscompile` never read the
   column, so `resolve_variable`'s VARP-first precedence was live. Measured
   overlap right now: **zero names**. §7.5's whole clobber class walks through
   that door, so it is shut before §9 step 3d opens it.
4. **`mock230_scripts.c` still spells two content names in C** and neither is
   this change's to move: `mock230_content_symbol(MOCK230_PACK_PARAM,
   "death_drop")` at the `npc_param` opcode (a param name in C — §2.4 item 3),
   and the `::` cheat's symbolic argument, which resolves a mistyped name to -1
   in silence (`mock230_scripts.c:1447`). The second is operator input rather
   than a content file, so a pack-time gate cannot see it and the fix is an
   operator-facing message. `mock230_seq_by_name` stays as it is: it is a lookup
   whose only callers assert on both the hit and the miss, not a loader
   assigning a default.

### 14.6 What step 2 did **not** land

- **The full `cachepack pack` exit status.** `cp_pack_run` still returns
  `failed == 0`. It counts asset-table and config-parse unresolved names too, a
  full pack copies the base cache and emits 116,450 archives, and it was not run
  here — so tightening it is a change that cannot be verified in the same pass
  that makes it. The server-band path, which is what `test-content` and the boot
  depend on, is fatal now.
- **The 452-reference whole-tree count** §13.1 is sized against. It is not
  reproduced here and should not be quoted: `ss_unresolved.py` answers "what
  does this tree reference that nothing provides", and against the *reference*
  tree that is 1,288 script-only and 6,213 whole-tree — dominated by LostCity's
  own asset names (`model` 1,271, `anim` 2,183). The four-record-namespace
  figure §13.1 quotes needs the census script (§12), not this tool.

---

## 15. §9 step 3a, as landed — constants

Written 2026-08-01, on top of §14. **22 constants landed, 1,562 classified.** The
classification is the deliverable; the copy is the small half, and the reason
those two numbers are so far apart is the finding.

### 15.1 The corpus, re-measured

§9 step 3a's "1,562; no dependencies" reproduces **exactly**: 1,562 `^name =`
lines in 109 `.constant` files under `content/scripts` (`_unpack` and `_test`
excluded — neither contains one). This tree held **284** in 23 files before this
pass and holds **306** after; both loaders agree on the number, which is the
check that matters, because `sscompile` and `mock230_content.c` walk the same
files through different parsers and a divergence means one of them skipped a
file.

Value shapes, which nothing above records and which decide what a copy costs:

```
int 1,388   coord 132   hex 17   int-with-a-trailing-semicolon 13
string 10   empty 1     another constant 1   (^inferno_loc_duration = ^max_32bit_int)
```

The 13 semicolons are `skill_runecraft/configs/runecraft.constant`'s
`^air = 1;`. Both loaders keep the value as verbatim text, so `1;` is stored and
would reach a use site as `1;` — harmless in the lexer, not a number. It is the
kind of thing that only shows up when somebody copies the file.

### 15.2 What the two trees already share

**111 of the 1,562 are already spelled in this tree, and 14 of those disagree on
the value.** That is the whole reason this stage has a gate rather than a
checklist:

```
prayer_thickskin           LostCity  1     here 0    (12 prayers)
prayer_protectfrommelee    LostCity 15     here 18
headicon_prayer_protectfrommelee  LostCity 3   here 0   (2 headicons)
```

This cache's prayer book has 29 entries where the reference's has 15, and rev 230
gave the overhead prayer icons an archive of their own that starts at 0. Every
one of those 14 is a name that **resolves**, in a file that **compiles**, meaning
something else — `obj rock_sample1` (§14.3) one layer over, except that a
constant has no namespace at all, so §14's gate is structurally blind to it.

Three more of the reference's 15 prayers survive as a *concept* under a different
word — `prayer_strengthburst` → `prayer_burstofstrength`, `prayer_clarity` →
`prayer_clarityofthought`, `prayer_protectitems` → `prayer_protectitem`. They are
recorded as `renamed` and deliberately given **no alias**: two names for one
number is how the two drift.

### 15.3 The test that decided what landed

> **A constant lands when its value cannot be wrong** — a string, a duration, a
> divisor, a direction, a side, a wire field with two states — **and when the
> reference's directory for it already exists in this tree.**

Everything else waits for the thing that knows the answer. The classification,
all 1,562 rows, is `OSRS-Content/osrs239-content/port/constants.map`:

| disposition | rows | what it means |
|---|---:|---|
| `defer-slice` | **1,112** | portable in kind; the value is an encoding chosen by content that has not been ported, and arrives with it |
| `defer-varbit` | **168** | the value is a *bit position* in a reference varp. §7.5's clobber class; step 3d owns it |
| `defer-table` | **115** | indexes a table this tree numbers differently and nobody has measured |
| `present` | **97** | already here, same value, landed by an earlier port |
| `never` | **31** | a 2004 client surface rev 230 replaced |
| `landed` | **22** | this pass |
| `rederived` | **14** | here with a *different* value, on purpose |
| `renamed` | **3** | the concept survived, the word did not |

**This is a deliberate departure from "land the class-(a) 1,138 mechanically",
and the reason is written in this tree already.**
`quests/quest_cook/configs/quest_cook.constant` carries Cook's Assistant's two
lines and says why the other 114 of the reference's `general/configs/quest.constant`
are not beside them: *"a constant for a quest nothing can start is a name waiting
to be resolved against the wrong thing."* That decision is followed here rather
than reversed. Landing `quests/quest_legends/configs/quest_legends.constant`
means creating a slice directory for a quest §11 says not to port, and 79 numbers
nothing in this tree can check — and 22 of those 79 are bit positions step 3d has
not decided yet.

The directory half of the test is what keeps the number at 22 rather than at
1,112: `general/configs/`, `player/configs/`, `doors/configs/`,
`skill_combat/configs/`, `interface_chat/configs/` and `skill_prayer/configs/`
exist here; `quests/quest_*`, `areas/area_*`, `minigames/`, `tutorial/`,
`macro events/`, `interface_boat/`, `interface_trade/`, `general_use/`,
`skill_runecraft/` and `skill_cooking/` do not.

What landed:

```
general/configs/free_to_play.constant      10  members-only refusals — strings
player/configs/gender.constant              2  the appearance block's gender byte
interface_chat/configs/book.constant        2  page turn, +1 / -1
doors/configs/doubledoors.constant          2  which half of a pair
skill_combat/configs/combat.constant       +3  spec regen 100 per 50 ticks, dropammo 1-in-5
skill_combat/configs/pvp.constant           1  skull duration, 2000 ticks
player/configs/player_controls.constant    +2  the run toggle's two values
```

### 15.4 The 168 bit positions, and the 22 varps under them — for step 3d

Measured directly off the call sites (`testbit|setbit|clearbit`, second
argument): **168 distinct constants, 624 uses, over 22 reference varps.** §9 step
3a's plan sized this at 202/880; the difference is that the larger figure counts
`and()`/`or()` mask arithmetic too, which is not the same class. Use 168.

None of the 168 is in the varp position — the reference never writes
`testbit(^const, …)` — so every one of them is unambiguously a bit index.

```
legends_bits 22   ibanmulti 17   itwatchtower_bits 15   dueloptions 14
barcrawl 11       elemental_workshop_bits 11             desertrescue_map_mechanisms 10
zq_map_mechanisms 10   bioerrand 9   thieving_stall_timer 7
crest_spells_levers_gauntlets 6   excalibur_components_progress 5
death_bits 5   cog_bits 5   agilityarena_varbit 4   chompybird_kills 4
emote_access 4   druidspirit_bits 2   hunt_store_employed 2   ikov_dungeon 2
murder_evidence 2   cogquest 1
```

`ibanmulti` and `emote_access` are two of §7.5's own six examples, arrived at
from the other direction, which is the cross-check. **This list is step 3d's work
queue for the bit-packed bucket**: each carrier varp becomes N osrs239 varbits,
and the constant naming bit *k* of it becomes a varbit name, not a number.

Two of them are not quests and are easy to miss:
`skill_thieving/configs/stalls/stealing.constant`'s seven `^*_stall_index` are
bits of `%thieving_stall_timer`, not stall ids; and all four of
`minigames/game_agilityarena/configs/agilityarena.constant` are bits.

### 15.5 The four `defer-table` destinations

115 constants whose number indexes something this tree renumbers. Each row in
the map names where the number has to be read from instead:

| n | destination |
|---:|---|
| 53 | varbit `autocast_spell` — the rev-230 spellbook's numbering, unmeasured. The reference's `^wind_strike = 51` is its own id space |
| 23 | rune-altar index (`skill_runecraft`), LostCity-local; 13 of the 23 are unreferenced even there |
| 20 | the rev-230 xp-lamp interface's index. The reference's values are its *stat* numbering offset by one, which this tree does not share |
| 19 | `pack/stat.pack`. The reference numbers `attack = 1`; here `attack` is **0**, and `stat` is a first-class RuneScript type, so the right answer is not a re-derived constant but no constant at all |

### 15.6 Three findings

1. **A class-(c) file is already in the tree, and it is fine — because the tree
   re-homed the table.** `general/configs/displaymessage.constant` is a verbatim
   copy of the reference's, RS3 enum-33 indices and the reference's own "all
   below are guessed" comment included. It is not a hazard here, and the reason
   is worth stating: `general/configs/displaymessage.enum` is keyed **by those
   constants**, so the numbers index a table this tree authors. A 2004 index
   stops being a 2004 index the moment the table it indexes moves into content.
   That is the shape every other `defer-table` row wants and none of them has.
2. **`.constant` is invisible to every bar that existed.** `ss_unresolved.py`'s
   `CONFIG_SUFFIXES` does not include it (correctly — a constant references
   nothing), `port_name_diff.py` compares records, and `mock230_pack` validates
   ids. The only check a constant had before today was §14.1's duplicate rule,
   which catches a collision and cannot catch a wrong number. The 14 disagreeing
   values sat in exactly that gap.
3. **63 of the reference's 1,562 constants are referenced nowhere outside their
   own file**, including 13 of the 23 in `runecraft.constant` and 9 of the 40 in
   `macro_events.constant`. A bulk copy would have imported them as-is; the map
   records them with the rest, and a slice port can drop them.

### 15.7 The permanent check

`tools/port_constant_diff.py --check`, added to `test-port` (which
`test-content` already depends on — no new top-level target, same reasoning as
§14.2). It holds the tree to `port/constants.map`:

- a reference constant with no row → classify it before it is copied
- `present`/`landed` whose value no longer matches the reference
- `rederived`/`renamed` whose stated destination value drifted
- **a `defer-*` or `never` name that appears in this tree** — the failure the map
  exists for
- a tree constant in neither the rows nor the `tree-only` list (173 constants are
  this tree's own and are listed, so the file describes the whole namespace)

The reference half degrades the same way the name diff does: no LostCity
checkout, no reference bar, and it says so.

Four mutations, each reverted: copying `^doric_complete` in (`defer-slice` →
error), setting `^prayer_thickskin` back to the reference's 1 (`rederived`
drift → error), editing a landed value away from the reference (→ error), and
adding an undeclared constant (→ error).

### 15.8 What this stage did **not** do

- **The other 1,112.** They are classified, not landed, per §15.3. If that call
  is overruled, the map is the work order and `--report` prints it as TSV.
- **`general/configs/quest.constant`** (116 rows, 770 uses in the reference) —
  the single biggest cross-cutting file, deferred on the tree's own recorded
  reasoning. The `^*_questpoints` half of it is era-independent in kind and is
  the strongest candidate for a re-argument.
- **Re-derived class-(b) values.** The plan expected this stage to land the
  non-varp class-(b) buckets with re-derived numbers. Measured, there was nothing
  to re-derive: every bucket this tree *could* answer had already been answered
  by an earlier port (`equip` 14/14, `combat_damagestyles` 9/9,
  `combat_damagetypes` 6/6, `hunt` 3/3, `map_findsquare` 3/3, prayers as the 14
  `rederived` rows), and the four that remain need a rev-230 table measured
  first — which is a measurement, not a decision, and does not belong to a
  symbols stage.
- **Client verification is vacuous** and is said rather than skipped: no client
  code, no packet and no interface is touched. The closest real check is
  `test-mock230`, which boots the server, compiles and loads 319 scripts and
  builds the scene — green.

### 15.9 §14.5 finding 1 recurred, in the same shift

The reference checkout moved again while this stage ran: three seq records
(`ghrazi_rapier_attack`, `scythe_of_vitur_attack`, `scythe_of_vitur_ready`)
appeared in `LostCity_Server/content` between the first green `test-content` of
this pass and the last, and `port_name_diff --check` correctly went red on three
unsigned rows that had nothing to do with constants. Re-baselined with
`--write-signed`; the diff is **exactly +3 rows**, all `identical/unreviewed`,
no existing row rewritten.

Worth recording twice, because it is now a property of the setup rather than an
incident: **a bar computed against the reference goes red when the reference
moves, and the stage that notices is whichever one happens to be running.**
`constants.map` has the same exposure by construction — a new `.constant` line in
the reference is an unclassified row and therefore an error. That is the intended
behaviour (it is a review queue), but it means "test-content was green" is a
statement about two trees, and the second one is not frozen.

---

## 16. §9 step 3b, as landed — npc categories

Written 2026-08-01. §7.6b's three parts — **the field, the crawler, and the -1
at every npc call site** — with one of the three landed only in part and the
remainder documented as a seam rather than skipped. What follows is what was
measured, what was minted, and the two decisions the crawl is not allowed to
make.

### 16.1 The premise was wrong, and the comment was the bug

`mock230_world.c`'s `interaction_category()` carried this, and every npc trigger
in the server dispatched `-1` on the strength of it:

> **npc** — an osrs239 npc record carries no category at all. Not "unread":
> absent, which is why `struct Mock230NpcInfo` has no field for it.

Measured: **cache.osrs239 states a non-zero `category` on 9,149 of its 16,292 npc
records.** `dat2_config_npc.c:666` has decoded config opcode 18 into
`RSCache_Dat2ConfigNpc.category` the whole time, `:158` re-encodes it, cachepack
round-trips it, and `configs/all.npc` carries all 9,149 lines. It was unread, not
absent — and the distance between those two words is the entire middle rung of
the trigger lookup for the npc domain.

That is worth stating as a rule, because the comment was careful, specific, and
load-bearing: **a negative claim about a data source is a measurement, and it
goes stale in exactly the direction that stops anyone re-checking it.** §7.6b
repeated it ("There is no npc category at all"), and so did the port plan.

### 16.2 What "derived" means here, and why it is not the reference's crawl

A category is the one config type LostCity never authors. `PackFile.ts` builds
`CategoryPack` with no file extension and `validateCategoryPack`; `CategoryType.ts`
says the table is regenerated by crawling the `category=` key out of every
`.npc`/`.loc`/`.obj` (CONTENT_ARCHITECTURE.md §2.1, "fully regenerated … Never
authored"). So a port cannot look a category up: there is nothing to look up.

But the reference's crawl **mints** the id (`pack.max++` over its own records),
and `content.ini` here says `[namespace:category] ids = cache`. The cache already
states the number. So the crawl this tree runs attaches the reference's *name* to
the id **this tree's own records carry**, resolved member by member:

```
LostCity `.npc` blocks saying `category=bank_teller`
  -> 6 record names
  -> configs/all.npc.compack   (name -> osrs239 npc id)
  -> those records' own `category=` in configs/all.npc
  -> all six say 249
  -> `249=bank_teller` is READ, not chosen.
```

No id crosses between the trees, which is bar §7 item 1. `tools/port_category_crawl.py`
is that crawl; `port/categories.map` is its output, one row per reference
category, and it is the artifact `test-port` holds the tree to.

**`_unpack/` is crawled, and that is deliberate.** Every other tool in this repo
correctly excludes `content/scripts/_unpack/` as machine output. The reference's
own build does not — §2.1 again: "the queue is inside `scripts/`, so its `[name]`
headers are crawled into the packs immediately" — and **eleven of the categories
content binds a trigger to (`cow`, `chicken`, `bear`, `duck`, `pirate`, `witch`,
`barbarian`, `ice_warrior`, `unicorn`, `duckling`, `monk_of_zamorak`) are
declared only there.** Excluding it turns eleven mechanical answers into eleven
orphans. The map's `provenance` column records which half each row came from.

### 16.3 The corpus, measured

| | measured | the plan's figure |
|---|---:|---:|
| distinct npc categories in the reference | **53** | — |
| …bound by a trigger (`[…,_name]`, npc-family) | **49** | 49 |
| loc categories bound by a trigger | **89** | 89 |
| obj categories bound by a trigger | **48** | — |
| npc records this cache categorises | **9,149 / 16,292** | — |
| …of which are **nameless** | **1,585** | — |
| distinct npc category ids in this cache | **982** (max 2504) | — |
| distinct obj category ids | **575** (max 2506) | — |
| ids carried by **both** npc and obj records | **21** | 21 |
| names in `pack/category.pack` before | **37**, all obj | 37 |

Restricted to the 49 referenced, the plan's split reproduces exactly:
**23 mechanical · 8 split · 18 orphan.** Over all 53 it is 25/8/20, the four
extras being categories no trigger names.

§7.6b's "19.3 % of compile failures" is **not** re-derived here and should not be
quoted; §15's predecessor measured 15.7 % of first-order failures and §6's own
15.5 % reproduces to 0.2 points.

### 16.4 What was minted: 18 names, and the two that were not

18 of the 53 are in `pack/category.pack` now, each with the group histogram that
justified it recorded in the file's own header (the same evidence format the 37
obj entries above them use):

```
249 bank_teller   280 freshfish   281 rarefish    282 memberfish  283 saltfish
287 kolodion      365 cow         425 duck        426 duckling    438 bear
444 chicken       454 werewolf    455 canafis_citizen             466 ice_warrior
503 healer        552 death_archer               559 death_guard  2263 unicorn
```

**The rule applied: mint when the osrs239 group is what the reference name says.**
That is a stronger test than "the members agree on one id", and the difference is
the finding. Two names pass the weaker test and fail this one:

```
black_demon  -> 275, and 275 is the DEMON category:
                18 Lesser demon, 15 Greater demon, 4 Scarred lesser demon
                beside the 17 Black demon. 37 of 77 are not black demons.
giantrat     -> 262, and 262 is the RAT category:
                8 Rat, 6 Dungeon rat, 3 Zombie rat, 1 Brine rat.
                20 of 39 are not giant rats.
```

Both resolve. Both are unique. Both are the *only* candidate. Minting either
gives two thirds of a group the wrong `[ai_queue3]` drop table, and nothing
anywhere reports it — this is `obj rock_sample1` (§14.3) one namespace over, and
the same shape as §4.1's display-name class. They are `broader` in the map, and
`port_category_crawl --check` fails if either is minted later without the row
changing.

`giantrat` has a live witness in this tree already: Lumbridge spawns
`dragonslayer_giantrat_1_key`, `dragonslayer_giantrat_2` **and plain `rat`**, all
three carrying 262. A `[ai_queue3,_giantrat]` bound to 262 puts a giant rat's
drops on the swamp rat.

Held back for the same class of reason:

- **4 collisions.** Two reference names crawling to one id, which a `id=name`
  pack cannot express: `troll_general` and `troll_spectator` both read **309**
  (which also holds `mountain_troll`'s eight — 309 is simply "troll"), and
  `battle_mage` and `gnome_troop` both read **354** (which is "gnome": 5 Gnome,
  3 Gnome guard, 3 Gnome child, 2 Mounted terrorbird).
- **8 splits**, unchanged from the plan: `citizen`{266,492},
  `citizen_burthorpe`{564,565}, `guard`{470,1732}, `mountain_troll`{309,566},
  `petcat`{29,30,31}, `shop_keeper`{271,922,1372,1373,1374,1375},
  `troll_thrower`{322,569}, `undead_one`{257,274}. `citizen` has a witness too:
  this tree spawns `man`/`man3`/`man4`/`falador_man1`/`varrock_man1` at 266 and
  `woman`/`woman2` at 492 — osrs239 split the reference's one citizen category
  by sex.
- **1 placeholder.** `category_453` is a LostCity *id* wearing a name. This
  cache's 453 being a plausible fishing-spot group is a coincidence, and
  accepting it would be copying an id with extra steps.
- **20 orphans**, blocked rather than deferred — see 16.7.

### 16.5 One id space, two domains

npc and obj categories are **the same id space**. 21 ids in this cache are
carried by records of both kinds, and the interesting part is that 20 of the 21
are *pet pairs* — the follower npc and its item form share an id and mean the
same thing (`overgrowncat`/`overgrowncatobject` at 29, `skillpetwc` at 1783,
`red_crab` at 2275, …). **The single genuine cross-domain reuse is 36**,
`weapon_spear` for 102 spear objs and also the category of npc
`twocats_robert_cutscene`.

Nothing mis-dispatches — the provider keys the lookup on the trigger as well as
the subject — but a name minted in this file is visible to every domain, so it
has to be chosen as though it were. `pack/category.pack`'s header says so.

Two more facts the crawl produced, worth keeping: `_bones` proves name→id is not
1:1 the other way (the pack says `6=bones`; the reference's `_bones` members land
on osrs239 **6 and 117**), and `petcat` is 3 ids here where the reference had one
— osrs239 actually has five (29 overgrown, 30 grown, 31 kitten, 32 lazy, 33
wiley) and the reference's 18 members only reach three of them.

### 16.6 The three parts, and which one is only half landed

**The field — landed.** `[npc.category]` is now stated in
`fields/npc.ini` with the evidence for PORTING_GUIDE §3.2 case 1 (`client =
native`: the client's own decoder has an opcode for it). It restates the built-in
default in `content_fields.c:95` and is therefore inert — proved by A/B:
`cachepack pack --server-only` writes a **byte-identical** `server/pack` with and
without the block. It is written down because the *absence* of a statement was
read as a statement (16.1). `struct Mock230NpcInfo` carries `category`, populated
in `mock230_npcinfo.c` from the decoded record.

**The crawler — landed.** `tools/port_category_crawl.py`
(`--report`/`--groups`/`--check`/`--write-map`), `port/categories.map` (53 rows),
18 names in `pack/category.pack`.

**The call sites — half landed, and this is the part to read.** The constraint on
this stage was that another change owns the nine `mock230_scripts_run_trigger`
call sites in `mock230_world.c`, so the fix went **behind a function the dispatch
already calls**: `interaction_category()` answers for `MOCK230_INTERACT_NPC` now,
which is the whole `[opnpc1..5]`/`[apnpc1..5]` path — four call sites reached
without one of them being edited.

**Seven npc dispatch sites still pass a literal -1**, listed in `mock230.h` beside
`mock230_npc_category()` so the lane holding those lines can adopt them one token
at a time:

```
SS_TRIGGER_AI_OPPLAYER1 + op   SS_TRIGGER_AI_APPLAYER1 + op
SS_TRIGGER_AI_QUEUE1 + n       SS_TRIGGER_AI_TIMER
SS_TRIGGER_AI_QUEUE3           SS_TRIGGER_AI_SPAWN
SS_TRIGGER_OPNPC1 + n          (the `::talk` cheat)
```

**AI_QUEUE3 is the one that matters**: it is where `drop tables/` binds 16 of its
94 triggers to a category, which is the slice §10 says this whole item gates.
None of the seven needs a guard — a category of -1 and a category nothing binds
behave identically, so adopting them is additive and cannot change an existing
dispatch.

`mock230_npc_category()` deliberately does **not** read through
`mock230_npcinfo()`. That accessor hides a nameless record's whole row, by
documented design, and **1,585 of this cache's 9,149 categorised npc records have
no name** — the multinpc instances are all of them. Reading the category through
it would answer "no category" for every one, silently, which is the failure the
rung exists to prevent.

### 16.7 Blocked, not deferred

- **20 orphans** — the 7 `macro_event_*` plus `pirate`, `witch`, `sailor`,
  `barbarian`, `bandit_camp_leader`, `diseased_sheep`, `fisherman_platform`,
  `guardian_of_armadyl`, `legends_guard`, `shipyardworker`, `tower_advisor`,
  `monk_of_zamorak`, `witches_experiment`. Their members either are not in this
  cache or carry no category, so there is no id to read and one has to be
  *allocated* — and `content.ini` gives `category` `ids = cache` with no
  `server_base`, so `tools/ss_allocate.py` never sweeps it. This is
  CONTENT_ARCHITECTURE §8.2(c) verbatim: **a namespace that cannot grow is a
  bug.** Promoting it is §9 step 3c-0, which had not landed when this stage ran.
  A second trap comes with it, and it is not this stage's to fix:
  `ss_allocate.py`'s `pack_path()` returns `configs/all.<ns>.compack`
  unconditionally, while `mock230_content.c`'s `pack_kind_is_config()` puts
  `category` in `pack/<ns>.pack` — so the moment `category` is promoted the
  allocator will write a file nothing reads.
- **89 loc categories.** `dat2_config_loc.c:1009` throws opcode 61 away
  (`case 61: g2(buffer); // Skip unsigned short`), so `configs/all.loc` carries
  **0** `category=` lines and the crawl has nothing to resolve against. Fixing it
  is an rscache write-path change (`EXCEPTIONS.md` first, byte-exact round-trip
  is the bar) *and* moving `mock230_content.c`'s two-valued door enum off the
  `category=` key it currently occupies — which is itself two game-facing strings
  in C (§2.4 items 3 and 4).

### 16.8 The permanent checks, and the mutation that proves each one

Two layers, because neither can see what the other does.

**In `mock230_pack` — `validate_categories()`.** Every name in
`pack/category.pack` must be carried by at least one obj *or* npc record, and no
name may be id 0 (`content.ini` reserves it: `zero = reserved`). The failure it
is for is invisible everywhere else — a category is a trigger *subject*, so
`[ai_queue3,_bank_teller]` naming an id no record holds compiles, loads, resolves,
reports nothing at any verbosity, and simply never fires. There is no wrong
behaviour to notice; a whole drop table just does not exist. A *misspelled* name
fails loudly at compile time, so the typo is the safe case and the
plausible-but-empty id is not. It reports `55 category name(s): 36 obj-only, 18
npc-only, 1 carried by both` — the 1 is `weapon_spear`.

- mutation `4999=ghost_category` → `1 error(s)`, exit 1. Reverted.
- mutation `0=unstated_category` → `ERROR … 0 is the decoder's "unstated"`. Reverted.

**In `test-port` — `port_category_crawl --check`.** Holds the tree to the map:
a reference category with no row, a `minted` row whose members no longer agree on
the id it states, a minted name missing from the pack, and — the one that matters
— **a name held back for a human that got minted anyway**.

- mutation `275=black_demon` → `mock230_pack` reports **0 errors** (275 has 77
  npc records, so the C rule is satisfied) and `port_category_crawl --check`
  fails naming the reason. That is the whole argument for two layers. Reverted.
- mutation: drop `444=chicken` from the pack → fails. Reverted.
- mutation: delete the `cow` row from the map → fails, "a new reference category
  is a decision, not a default". Reverted.

**In the mock230 selftest**, beside the existing `[opheld1,_bones]` obj-rung
section: the chicken npc's decoded category equals the id the crawl minted
`chicken` at, and at least one *nameless* record still answers a category. Proved
to bite by mutating `mock230_npcinfo.c` to store 0 — both checks fail, both
messages name the number. Reverted.

### 16.9 Verified end to end, then removed

The three checks above are static. The dispatch was proved live, and the probe is
recorded here rather than kept because keeping it would mean shipping an
`[opnpc1,_chicken]` script this tree has no other reason to have.

Temporarily: a `[opnpc1,_chicken]` script bumping `%mock_greeting_count` by 50,
plus a selftest stanza that puts the player beside Lumbridge's chicken (slot 30,
3229,3298), sets an npc interaction and calls `mock230_world_process_interaction`.

```
PROBE chicken slot 30 cat 444: varp 0 -> 50 (expect +50)     ← as landed
PROBE chicken slot 30 cat 444: varp 0 -> 0  (expect +50)     ← interaction_category
                                                                forced back to -1
```

A category-bound npc script fires on a real interaction, and did not before. Both
the script and the stanza were removed; `mock230-scripts` is back at 319.

**Client verification is vacuous for this stage and that is stated rather than
skipped**: no client code, packet, interface or asset was touched, and nothing
here is visible in the client. The closest real check is `test-mock230`, which
boots the server, compiles and loads the script pack and builds the scene —
green, plus the live probe above.

### 16.10 Two observations for whoever is next

**`mock230_world_npc_spawn` cannot spawn anything in the selftest world.** The
probe's first form asked for a chicken beside the player and got
`mock230: no free npc slot for type 1173`, 49 times over. Counted at that point:
`npc_slot_max=63` and **2,048 of 2,048 slots report `active`** — the whole array,
though only 63 npcs exist and `srv` is `memset` to zero at the top of the
selftest. Something marks every slot active without going through `npc_spawn()`.
Not chased: it is not this stage's code and the probe was reworked to use an npc
the world had already placed. But `npc_add` is content's way of creating an npc,
and on this evidence it returns -1 for every call in a world that has been
through `mock230_world_init`.

**§14.5 finding 1 / §15.9 recurred a third time, mid-stage.** Six records
(`sp_d_halberd_glow`, `warguild_parry_defend` and four
`dragon_halberd_special_*_red`) appeared in `LostCity_Server/content` between two
`test-content` runs of this pass, turning `port_name_diff --check` red on rows
unrelated to categories. Re-baselined with `--write-signed`; the diff is
**exactly +6 rows**, all `identical/unreviewed`, none rewritten. The reference
content submodule was dirty with 15 modified and 5 untracked files while this ran.
It is now a property of the setup, not an incident.
