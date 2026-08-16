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

`mock230` dispatches these families today: `opnpc1-5`, `oploc1-5`, `opobj1-5`,
`opheld1-5`, `apnpc/aploc/apobj1-5`, `inv_button1-n`, `if_button`, `if_close`,
`ai_queue1-20`, `ai_spawn`, `login`; since §9 step 5a `queue`, `timer`,
`softtimer` and `ai_timer` (275 uses; see osrs230_mockserver.md §3.19, and note
the 273 this file used to print omitted `softtimer`); and since §9 step 5b the
`*u` use-on family — `opheldu`, `oplocu`+`aplocu`, `opnpcu`+`apnpcu`,
`opobju`+`apobju`, **541 of its 546 uses** (osrs230_mockserver.md §3.20). It does
**not** dispatch `zone`/`mapzone`/`zoneexit`/`mapzoneexit` (806 uses — the
name-keyed *dispatch function* exists now, the two coordinate latches that would
call it do not), `opplayeru`/`applayeru` (5 — rev 230 assigns them no wire
opcode, so there is no packet to route), `command` (511), `ai_opplayer*`,
`walktrigger`, `advancestat`, or the `*t` spell-target family.

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

**Re-measured 2026-08-01, after §14–§19 landed** — method in §12.2, commits in
§12.3. The `(was)` values are what this table said before, and they are shown
only where the number moved:

| namespace | defined by LC | ref in `.rs2` | **resolves** (was) | **unresolved** (was) | id identical (was) |
|---|---:|---:|---:|---:|---:|
| `obj` | 1,955 *(1,953)* | 1,664 *(1,662)* | 1,664 | **0** | 3130/3133 *(3130/3131)* |
| `inv` | 154 | 61 | 61 | **0** | 156/156 |
| `varbit` | 6 | 6 | 6 | **0** | 6/6 |
| `npc` | 985 | 899 | 855 | **44** | 0/1073 |
| `loc` | 1,598 | 1,040 | 952 | **88** | 3135/3309 |
| `seq` | 181 *(176)* | 354 | 310 | **44** | 1088/1095 *(1088/1090)* |
| `spotanim` | 33 *(29)* | 140 *(135)* | 109 *(105)* | **31** *(30)* | 262/266 *(262/262)* |
| `varp` | 184 | 268 | 142 *(121)* | **126** *(147)* | 164/185 *(161/164)* |
| `param` | 214 | 203 | 49 *(23)* | **154** *(180)* | 0/50 *(0/23)* |
| `idk` | 82 | 62 | 0 | **62** | 13/13 |
| `flo` | 101 | 15 | 0 | **15** | — |
| `enum` | 114 | 52 | 22 *(2)* | **30** *(50)* | 0/24 *(0/4)* |
| `struct` | 149 | 51 | 0 | **51** | — |
| `dbrow` | 1,115 | 198 | 36 *(0)* | **162** *(198)* | 0/49 |
| `dbtable` | 27 | 8 | 5 | **3** | 0/10 |
| `category` | 0 | 122 | 27 *(1)* | **95** *(121)* | 0/31 |
| `interface` | 0 | 1,415 | 35 | **1,380** | 0/37 |
| `varn` / `vars` / `hunt` / `mesanim` | 100 *(31)* | 109 | 0 | **109** | — |
| **spawns — npc** | 6,263 lines | — | **6,126 (97.8 %)** | **137 lines / 24 names** | n/a |
| **spawns — obj** | 1,082 lines | — | **1,082 (100 %)** | **0** | n/a |

Four things about that re-measurement are worth saying out loud, because they
are the difference between a number that was checked and a number that was
plausible.

**Fourteen of the eighteen rows this table originally carried reproduced
exactly** on the first re-run of the census, *before* any of §14–§19 landed —
including both spawn rows to the line. Four had moved (`varp`, `dbrow`,
`category`, and the `varn` group's "defined"), and `dbtable` had no row. So the
method is stable and the table was honest.

**Six rows moved because this tree grew, not because the reference did**, and
they moved at two different times. `varp` 121 → **142** was already true before
any of §14–§19 ran: §8.5's `%com_*` allocator work put server varps 5705–5722
in place and `desert` moved 196 → 599. The other five moved *during* this work
— `param` 23 → **49**, `enum` 2 → **22**, `dbrow` 0 → **36**, `category` 1 →
**27**, and `dbtable` gaining a row at all — because §16 minted 18 category
names and §17 landed 27 params, 20 enums, 7 dbtables and 21 dbrows. The
resolution rate in this table is now partly a measure of how much of the port
has been done, which is the point of measuring it here rather than once.

**One row was simply wrong.** `varn`/`vars`/`hunt`/`mesanim` "31 defined" is
100 (`varn` 33, `vars` 36, `hunt` 13, `mesanim` 18); its reference and
unresolved counts were right. `category` and `interface` were `—` where the
answer is 0, and `dbtable` had no row at all.

**Three rows moved because the reference checkout moved under the measurement**
— `obj` +2, `seq` +5, `spotanim` +4 records appeared in `LostCity_Server`
between runs on three separate occasions during this work (§14.5 finding 1,
§15.9, §16.10). Any number in this section is only as reproducible as that
checkout, and §12 now records the commit it was taken at.

Read the rows in three groups.

**Group A — the cache fixes the id and it already agrees.** `obj`, `inv`,
`spotanim`, `varbit`. Nothing to do but re-resolve by name so the agreement is
*checked* rather than assumed. **Re-measured, "already agrees" is not "agrees
entirely"**: `obj` is 3130/3133 and `spotanim` 262/266 today, where both read
clean when this table was first taken — 3 obj and 4 spotanim mismatches. Six of
the seven arrived with the records the reference added mid-run (`ghrazi_rapier`,
`scythe_of_vitur`, and all four `dragon_halberd_special_*_red`); the seventh,
`dragon_claws` at 3142 here and 13652 there, was always there. `inv` (156/156)
and `varbit` (6/6) are still exact.
That is the argument for checking rather than assuming, made by the row that was
used to justify assuming.

**Group B — the cache fixes the id and it has moved.** Re-measured: `npc` (all
1,073 of them), `loc` (174), `varp` (21 — *not* 3; the `%com_*` allocations are
new ids on both sides), `seq` (7), `spotanim` (4), `obj` (3). Re-resolution is
mandatory and the tail is hand work:

- **npc, 44 unresolved in scripts.** Sampling shows these are almost all *naming
  drift*, not missing content. The rest — `inferno_zuk`, `td_demon`,
  `dragonslayer_ghost` — are LostCity's own names for ported or 2004-only npcs;
  for the ported ones the source id is recorded in the port manifest, so those
  are lookups, not decisions. **113 of the 524 whole-tree names turned out to be
  exactly that kind of lookup** (§19.3).
- **loc, 88 unresolved.** Same shape.
- **seq/spotanim, 44/31.** Dominated by `inferno_*`, `td_*`, `midget_*`
  (gnomeball) — the ported set and the minigames. `midget_*` → `gnome_*` is one
  rename covering 28 of them (§19.3).

**Every worked example this section originally gave was wrong, and §19.4 says
how.** They are left out of the bullets above rather than corrected in place,
because the mistake is the point: `harlow` → `dr_harlow` names a **nameless
multinpc wrapper** (the record is `dr_harlow_vis`); `king_lathas` →
`ds2_meeting_king_lathas` names the **Desert Treasure II cutscene copy** (the
Ardougne record is `kinglathas_vis`); all three proposed `leprechaun` targets
are level-0 shop npcs against a level-12 attackable monster, and **no osrs239
npc displays 'Leprechaun' at all**; and the "genuine 2004-only tail" is wrong
for all three families named — this tree carries 28 `trawler*` locs, both
agility courses and four ropeswings. The *numbers* in this section held up under
re-measurement. The *examples* were four plausible records nobody had read, which
is precisely the failure §4.1 exists to catch, arriving inside §4 itself.

**And the corpus is 524, not 206** (§19.2). 206 counts names referenced from a
`.rs2`; a `.npc` naming a `seq` is a reference the port has to resolve exactly as
much, and 293 of the 524 are reached only that way. `npc` reproduces at 44/44;
`loc` reproduces under no variant tried (131 / 220 / 266 depending on scope).

**Group C — the id is not the cache's and the name means nothing there.**
`param`, `struct`, `enum`, `dbrow`, `dbtable`, `category`, `hunt`, `mesanim`,
`varn`, `vars`. These are server-allocated (`content.ini`: `ids = server` or
`names = authored`) and the port *brings its own definitions*. A 0 % resolution
rate here is correct, not a problem — but it means 1,115 dbrows, 214 params, 149
structs and 114 enums have to be **allocated** off layer-0's high-water mark
(`CONTENT_ARCHITECTURE.md` §4.4), and that allocation is the thing that must
never be a guess.

**§17 landed the allocator and 75 of those records**, and found that the
high-water mark was doing all the work: every `server_base` in
`content_register.c` was advisory, because `ss_allocate.py` read only
`content.ini` and `content.ini` declares no `base =` key at all. The `varp`
floor was 8000 against a 6217-entry array — the next server varp would have
been dropped by a bounds check, silently. The remaining 1,544 are classified
row-by-row in `port/configs.map`, and 149 of them (every `struct`) are blocked
on an engine loader that does not exist rather than on an id.

### 4.1 The failure this measurement is designed to catch

Name resolution answers "does something here have this spelling", not "is it the
same thing". Cross-checking the display name of every record present in both
trees:

| type | names in both | same display name | **different** | unnamed |
|---|---:|---:|---:|---:|
| `npc` | 898 | 722 | **75** | 101 |
| `obj` | 1,947 | 1,345 | **499** | 103 |
| `loc` | 1,319 | 893 | **78** | 348 |

**Re-measured 2026-08-01 under this section's own comparison (`a.lower() ==
b.lower()`), it reproduces exactly**: npc 898 / 722 / 75 / 101, obj 1,347 / 499
/ 103 (of 1,949 — the reference gained two obj records mid-run), loc 1,319 / 893
/ 78 / 348. Strip `<col>` tags and punctuation and it becomes 728/69,
1,350/496, 894/77 — so **only 9 of the 652 differences are pure formatting.**
The rest are real.

That comparison is now an artifact rather than a paragraph. `port/name_diff.signed`
(§14) carries **4,279 signed rows** across five namespaces and splits "different"
into classes that mean different things:

| ns | rows | identical | formatting-only | plausible-sibling | different-thing | unnamed | shape-differs |
|---|---:|---:|---:|---:|---:|---:|---:|
| `obj` | 1,949 | 1,347 | 30 | 404 | **41** | 127 | — |
| `loc` | 1,319 | 881 | 20 | 59 | **11** | 348 | — |
| `npc` | 898 | 679 | 53 | 32 | **33** | 101 | — |
| `seq` | 104 | 93 | — | — | — | — | 11 |
| `spotanim` | 9 | 5 | — | — | — | — | 4 |

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

**The worst row in the corpus is `obj rock_sample1`, and it is worth stating
precisely: obj 671 in both trees — the name resolves *and* the id is
identical.** LostCity's record is `model=model_2393_obj / name=Rock sample 1`;
osrs239's is `model=17290 / name=Animal skull`. Every check that existed before
§14 passed on it. `rock_sample2` and `rock_sample3` are 'Special cup' and
'Teddy'; `antidragonbreathshield` is 'Dragonfire shield' here and 'Anti-dragon
shield' there — two different items in the modern game, referenced 16 times.
There is also a 404-record **plausible-sibling** class in `obj` where the noun
matches and a tier or colour word does not (`nails` → 'Steel nails',
`mcannonball` → 'Steel cannonball'), which is exactly what a human eyeball waves
through.

**The bar this sets: the port must emit a diff of display names for every
resolved record, and a human signs it off.** Not a blocker, a review artifact.
It is cheap (the data is in `configs/all.<type>` already) and it is the only
thing that catches a name that resolved to the wrong record. **Landed 2026-08-01
as `tools/port_name_diff.py --check`, inside `make -C src test-port`** (§14.3):
an unsigned row, a changed verdict or a deleted row is a build failure.

The companion measurement is what would happen if an id were copied instead of
resolved (`--collisions`, 14 namespaces): **1,329 name pairs would land on a
real but different record and 0 would land on nothing** — npc 1,073, loc 174,
dbrow 28, param 23, varp 21, enum 4, dbtable 3, seq 2, obj 1. There is no
fails-loudly case anywhere. Ten of them land on a *lexically similar* name and
would survive review, including the `elemental_workshop_valve_1/2` **shifted
chain** — which a swap sweep misses because it is not a 2-cycle. A full sweep of
every namespace found exactly **one** true 2-cycle in the whole corpus, and it
is the documented one: `blackarmgang` 145/146 and `phoenixgang` 146/145. No
longer cycles, no display-name transpositions, and no duplicate names inside
either tree's own tables.

---

## 5. The engine gap — the real schedule driver

268 distinct commands used, 123 implemented, **145 missing**. By family, ordered
by call sites:

| family | ops | uses | the ones that matter |
|---|---:|---:|---|
| `npc_*` | 26 | 1,360 | `npc_find` 305, `npc_setmode` 310, `npc_add` 175, `npc_walk` 92, `npc_queue` 79. ~~`npc_param`~~ **was implemented and *wrong* — done, and really 291 uses / 137 files** (§10.3d). Still open: `npc_walk` **really 108/45**, `npc_changetype_keepall` 36/17 — both blocked on missing per-npc engine state, not on opcodes |
| `loc_*` | 8 | 1,033 | `loc_change` 270, `loc_add` 248, `loc_find` 224, ~~`loc_param` 133~~ **done, and really 140** — §10.3c, `loc_del` 70. Still open: `loc_anim` 56, `loc_category` 38 |
| `oc_*` | 7 | 417 | `oc_param` 351 — **blocked on a decoder, not effort** (§5.1) |
| `if_*` | 8 | 363 | `if_setcolour`, `if_setobject`, `if_settab`, `if_setmodel` |
| `p_*` | 13 | 268 | `p_finduid` 118, `p_oploc` 41, `p_aprange` 30, `p_walk` 26 |
| `split_*` | 4 | 244 | text paging — **has no job at rev 230** (§7.4) |
| `inv_*` | 10 | 129 | `inv_setslot` 88 |
| ~~`struct_param`~~ | 1 | 115 | **done** — §10.3b |
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
  unblocked much.

  **`lc_param` and `struct_param` are now done too — see §10.3b.** The claim
  above that they were "the expensive ones" was half wrong: both config groups do
  have to be decoded at boot, but the *retained* cost is 41 KB for loc and 877 KB
  for struct. Reading loc's 62,194-record count as a memory figure is what made
  it look expensive; 61,124 of those records carry no params at all. `loc_param`
  (3011) is a different opcode from `lc_param` (4106) — it pops *one* int and
  reads the *active* loc — and **it is done too, see §10.3c.** All five of the
  family's 605-plus call sites are answerable now; only `obj_param`, which has
  no callers and no active-obj entity to read, is left.

  **The sixth member of the family was never on this list, because it was
  already counted as done.** `npc_param` (2529, **291 uses / 137 files** —
  larger than any other `*_param`) had a `case` label in
  `mock230_scripts.c` that answered one hardcoded param and pushed 0 for the
  rest, always on the int stack. Coverage is derived from `case` labels, so
  wrong and right were indistinguishable to every check here. **Done properly in
  §10.3d.** The lesson generalises past this opcode: a coverage number counts
  *dispatch*, not *correctness*, and there is no automated way in this tree to
  tell the two apart.
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

**Every cell here is a rate, not a fact about a directory, and one of them has
been re-measured.** `drop tables` reads **70/71 (98.6 %)** as of 2026-08-01 —
the one file that is not clean is `grip.rs2`, and the one command is
`obj_addall`. Nothing in that directory changed; the engine did. Assume the same
of every other cell before quoting it, and note what this row is *not*: it
counts **scripts**, so 97.2 % here and the 99.97 % of **call sites** quoted in
§10.1 are two different measurements of the same directory that happen to look
comparable. (`grip.rs2` was not ported — §10.1.)

The skills are at or near zero because every one of them is a `loc_*` loop:
`loc_find` the tree/rock/altar, `loc_change` it to the depleted form, `loc_add`
the respawn. **`loc_change` + `loc_add` + `loc_del` + `loc_find` + `loc_param`
is the single unlock for the skilling half of the tree**, and it is the same
family the `doors/` directory needs (25 `loc_add`, 22 `loc_param`, 14 `loc_del`).

**`doors/` was ported on 2026-08-02 and it found two defects in that family**,
which is worth knowing before the skilling half is written on top of it. Both are
transposed argument pairs, both were invisible, and both were invisible for the
same reason: every existing caller passed the two swapped arguments as the *same
number*.

- **`loc_add` popped `shape` where the reference pops `angle`.** The signature is
  `(coord, loc, angle, locshape, duration)` — `engine.rs2:657`, `LocOps.ts:19` —
  and `ss_meta.gen.h` carries arity and stack class, never order, so nothing
  could catch it at the call. The two callers in the tree wrote `..., 10, 0, 3)`
  and `..., 0, 0, 200)`.
- **`movecoord` added `$z` to the plane and `$y` to the north axis.**
  `ServerOps.ts:107` is `packCoord(level + y, x + x, z + z)`. All twelve callers
  write `movecoord($c, $dx, 0, $dz)`, so `~move_north($c, 3)` went up three
  floors and did not move north. A skilling script that walks to a respawn tile
  is the next thing that would have hit this.

Two things follow for the skilling port. The `loc_*` family is now checked with
*different* numbers in each argument (`selftest.rs2`'s `[proc,selftest_loc_add]`
reads `loc_angle` and `loc_shape` back), and the compiler learned to pass a
multi-return proc's result straight into another proc's argument list —
`~movecoord_loc_return(~door_open(loc_angle, loc_shape))` is the reference's
ordinary idiom and used to be rejected as "takes 2 int, called with 1".

**And the `loc_*` family's unlock finished the `oploc` eviction on the same
day.** With doors, ladders and — the last piece — the 78 bank booths bound,
`interaction_engine_loc` and `climb` are deleted and `enum Mock230Fallback` is
**5**. The part to carry into the skilling port is which of the three was the
blocker: not the `loc_*` opcodes, which had landed, but a *list*. 78 loc records
in this cache say "Bank" and content bound one, so the C's `strcmp` was reaching
77 booths that no script named. The skilling loops have the same shape — every
tree, rock and altar the cache states a Chop/Mine/Pray verb on is a record some
script has to name, by name or by category — and `tools/bank_import.py` and
`tools/ladder_import.py` are the two worked examples of generating that list
from the cache with a `--check` instead of keeping it by hand.

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

### 7.2 Zone triggers (806 uses) — DONE, and the ZoneMap was never the point

`zone` 262, `mapzone` 306, `zoneexit` 165, `mapzoneexit` 73. Re-counted
2026-08-01 against `LostCity_Server/content`; the four numbers above still hold,
and were re-derived a third time in step 5c. Every "you have entered the
wilderness", every minigame boundary, every area music trigger is one of these.

**Dispatched as of §9 step 5c** — `osrs230_mockserver.md` §3.21. Two things to
know before porting any of them, neither of which the count above tells you, and
both of which survived the port unchanged:

- **Only 427 of the 806 are zone-keyed.** `mapzone` and `mapzoneexit` key off
  the *map square* (`x >> 6`), not the zone (`x >> 3`) — see
  `NetworkPlayer.updateMap`, which tracks `lastMapZone` and `lastZone` as two
  separate latches. All 427 have five-part subjects (levels 0 ×392, 1 ×2, 3 ×33)
  and all 379 have three-part ones beginning `0_`.
- **They are keyed by script *name*, not by a numeric subject.** The reference
  looks up `[zone,<level>_<mx>_<mz>_<lx>_<lz>]` and `[mapzone,0_<mx>_<mz>]`
  through `ScriptProvider.getByName`. `mock230_scripts_run_trigger` takes an
  integer subject and cannot express that, so the dispatch is
  `mock230_scripts_run_trigger_at` / `_queue_trigger_at`, name-addressed with no
  keyed rung at all.

**The `ZoneMap` was not the blocker and is not in the path.** It landed first
(`src/net/mock/mock230_zone.{c,h}`, §3.17) and the two are unrelated: it is keyed
`(zx, zz, level)`, which is the wrong granularity for 379 of these uses and the
wrong *kind* of address for the other 427. Sizing this work off "the ZoneMap
exists now" would have been sizing off the wrong structure — what it needed was
two integer latches on the player and a name lookup.

What is left is the 806 scripts themselves, which are content and Phase 4.

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
does, for `worldmap_transmitdata`.

**The paragraph that used to follow was wrong twice over**, and the correction
is worth more than the claim was. It read: "`mock230`'s `RUNCLIENTSCRIPT` sender
takes **ints only** … a string-argument form of one existing encoder is what
stands between here and 879 call sites."

The sender never took ints only. `mock230_send_run_clientscript_mixed` has
carried a per-argument type string since it was written; what was fixed at one
int and two strings was the *opcode*, `runclientscript_ss` (11002). The general
form — `runclientscript*` / `SS_OP_RUNCLIENTSCRIPTVARARG` (11003), any mix of
ints and strings with the arity decided at the call site — has since landed and
is exercised by content. See [`runclientscript.md`](runclientscript.md).

And `~p_choice*` was never what it blocked. `~p_choice_open` ships today
(`interface_chat/scripts/chat.rs2`) on `runclientscript_ss`, because its target
`chatbox_multi_init` takes exactly the one-int-two-string shape. What bounds the
879 call sites is three *caps*, none of them this opcode: `script_58` parses at
most five options, the `|`-joined option list rides one 512-byte string
(`PKT_RUNCLIENTSCRIPT_STR_LEN`), and `MOCK230_RESUME_SUB_MAX` is 15.

Related: most `split_init`/`split_get`/`split_linecount`/`split_pagecount` uses
(244 in the original corpus) are LostCity measuring dialogue against one of
four fixed-size chat interfaces. rev 239's chat body wraps itself, so those
dialogue uses still compile away as `chat.rs2` documents. **The quest journal
is the measured exception**: interface 119 exposes 210 separate 415x20
single-line `p12_full` rows, and the official client renderer will not wrap a
row that short. The split primitives are therefore implemented with
cache-backed font metrics for journal/book-style server-painted rows; do not
generalise the chat translation rule to fixed single-line components.

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

### 7.6b npc categories — 19 % of the compile failures, and the premise below was false

> **Corrected 2026-08-01.** The heading used to read "npc categories do not
> exist here". They do — 9,149 of the cache's 16,292 npc records state one, and
> nothing had read it (§16.1). The 19 % figure stands; the diagnosis under it
> did not. Landed in §16, §16.11 and §10.1. Read the strikethrough below as the
> record of what was believed, not as the state of the tree.

The compile pass surfaced something neither the brief nor the static census
ranked highly: `unknown category subject 'X' for trigger 'Y'` is **19.3 % of all
failures**, and a category is a *trigger subject*, so an unresolved one kills the
whole file.

The cause is specific. In this tree, `category` means two unrelated things:

- **obj category** — the record's own `category` field, config opcode 94, a
  number the cache states. `mock230_scripts.c` reads it for
  `[opheld<n>,_<category>]` and `inv_totalcat`. The count in this paragraph has
  now been wrong twice, so it is stated as a series rather than a fact: this said
  **6**, then **37** (the weapon categories), and `pack/category.pack` holds
  **55** as of 2026-08-01 and **68** as of 2026-08-02 — 37 obj names, the **18**
  npc names §16 minted, and the **13** loc names below. One id space, three
  domains (§16.5). Re-measure with `grep -c '^[0-9]' pack/category.pack`, and
  split it with `tools/port_category_crawl.py --check -v`, which prints how many
  of the rows are `minted` per domain (`grep -c minted port/categories.map` says
  21 and is wrong — the file's own header uses the word three times).
- **loc category** — ~~a two-valued door enum (`door_closed` / `door_opened`) in
  `mock230_content.c`, and nothing else~~. **Corrected 2026-08-02, and the
  correction is that the two things were never unrelated.** A loc states a
  category at config opcode 61, in the *same* id space as obj 94 and npc 18, and
  the linked decoder had been reading the opcode and throwing the value away
  (`g2(buffer); // Skip unsigned short`). 8,407 of cache.osrs239's 62,194 loc
  records carry one, 712 distinct ids, max 2474; 684 is 63 records of which 43
  display 'Bank booth', 907 is 360 bookshelves. The paragraph's *warning* was
  right and its reason was the accident: the private enum's values were 1 and 2
  and would have aliased onto real names — which is exactly why
  `interaction_category` answered -1 for locs, and exactly why
  `[oploc1,_door_closed]` could not bind. Both are gone. `Mock230LocDef.category`
  is a `pack/category.pack` id, `mock230_loc_category` merges the overlay over
  the cache, and the rung answers.

  The half of this that is *not* the cache's, and it is the part that forced the
  namespace to change: **none of the 776 records in `doors.loc` carries a cache
  category at all**, so the reference's own door binding had no id to read. The
  crawl says so — all 9 door categories come back `orphan`, with
  `SUSPECT 167(71/82),168(54/66)`, two ids that share the display names Door and
  Gate and are a different set. `content.ini`'s `category` namespace has an
  allocation base now (8192, `content/content_register.c`), `door_closed` and
  `door_opened` are the first two ids minted from it, and the map records them
  with a disposition of their own (`allocated`) so that "the id came from us" can
  never be confused with "the id was read off a record".

**~~There is no npc category at all.~~ — that was wrong, and §16.1 is the
correction.** It was written from `pack/category.pack` and from the call sites,
never from the cache: `cache.osrs239` states a category on **9,149 of its 16,292
npc records**, and the decoder had always read it. What *was* true is the rest of
the sentence. `SSVM_ProviderGetByTrigger` takes a category and the chain (exact
type → category → global) is implemented, but every npc call site in
`mock230_world.c` passed `-1`. Two of them do not any more:

| site | passes | since |
|---|---|---|
| the `[opnpc<n>]`/`[apnpc<n>]` interaction path (`mock230_world.c:885`, `:927`) | `interaction_category()` | §16.6 |
| `mock230_world_npc_died` → `AI_QUEUE3` (`mock230_world.c:3942`) | `mock230_npc_category(npc->type)` | §16.11 |

Everything else still passes `-1`: `AI_QUEUE1..n`, `AI_TIMER`, `AI_SPAWN`,
`AI_OPPLAYER`/`AI_APPLAYER`, the `--cheat` `[opnpc<n>]` path at `:2720`, and every
loc site. (The obj rung is live and exercised: `[opheld<n>]` fed it already, and
`[opobj<n>]`/`[apobj<n>]` do too since §3.18.)

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

**Landed, and the slice it gated landed on top of it the same day.** §16 is the
crawl and the 18 minted names; §16.11 is the four collisions and the `AI_QUEUE3`
call site; §10.1's `drop tables/` entry is the content. The measured outcome for
this paragraph's own example: of those 16 category subjects, **six** bind as
categories (`_bear`, `_chicken`, `_cow`, `_ice_warrior`, `_unicorn`,
`_werewolf`) and are the only content in the tree that exercises the category
rung; the other **ten** are bound to the reference's own member lists instead,
because a reference category *is* a list of npc names and those names resolve
here. Nothing had to be minted for the port, and no `split` was resolved by
inventing a name.

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
handling is one of the enumerated rows in `enum Mock230Fallback` (seven when
this was written; **four** since `ai_queue3`, `opobj` and `opheld` moved to
content),
each naming the
this was written; **five** since `ai_queue3` and then the whole `oploc` row —
doors, ladders and bank booths — moved to content), each naming the
blocker keeping it in C, counted at boot and pinned by the selftest. It may
shrink; it must not grow. Each row's blocker also names its missing opcodes in a
form `mock230_scripts_stale_blockers` can resolve, so a *reason* that expires
turns the selftest red instead of continuing to print —
`ai_queue3`'s did exactly that for two stages before the check existed.
What that buys for the bulk import: an imported
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

2.  The name-resolution gate itself   DONE (§14)
    ↳ every unresolved name is a pack-time error; a display-name diff
      is emitted for every resolved record (§4.1); no default, ever
    ↳ landed as five rules + `port/name_diff.signed` (4,279 rows).
      Two "no default, ever" holes were real and are shut: four
      symbolic npc params resolved a typo to -1 silently, and
      cachepack counted an unresolved `ref =` as a warning and
      skipped the field. Still open: full `cachepack pack` exit status

3.  Symbols, in this order — each is named by the next
    3a. constants          (1,562; no dependencies)          DONE (§15)
        ↳ 22 landed, all 1,562 classified in port/constants.map.
          111 names exist in both trees and 14 DISAGREE on the value —
          a namespace with no gate at all until this step. 0 of 1,562
          name an id, so "resolve through the pack" was vacuous here
    3b. npc categories — the field, the crawler, and the -1 at every npc
        call site (§7.6b)                                    DONE (§16)
        ↳ the premise was wrong: this cache states a category on 9,149
          of 16,292 npc records. Unread, not absent. 18 names minted,
          20 orphans blocked on `category` being allowed to grow.
          NOT 19.3% of compile failures — measured 15.7% (§6's own
          15.5% reproduces; 19.3% reproduces under no framing)
    3c. param, struct, enum, dbtable, dbrow  (server-allocated ids)
                                                             DONE (§17)
        ↳ the allocator was the stage: every `server_base` was advisory
          and `varp`'s was out of bounds. 75 records landed of 1,619;
          149 structs blocked on a loader, 287 rows on a type
    3d. varp / varn / vars reclassification (§7.5) — 28 by hand
                                                             DONE (§18)
        ↳ 27 carriers, not 28, and the hand queue is 60 not 28 once the
          33 LostCity bitfields are counted. varn/vars is 0 + 0, not
          "the other 147": the real destination is a server varp with
          transmit=no. 357 rows in port/vars.map, 117 undecided
    3e. npc / loc / seq / spotanim name maps — 206 by hand    DONE (§19)
        ↳ the whole-tree corpus is 524, not 206; 180 rows carry a
          target and 113 of those are port-manifest lookups. 344 rows
          are the work queue (§20)

4.  Engine opcodes, by leverage
    4a. the param decoder            → oc_/nc_/lc_/struct_param, 605 uses, 117 files
    4b. the loc_* family             → 1,033 uses; unlocks every skill directory
    4c. the npc_* family             → 1,360 uses
    4d. RUNCLIENTSCRIPT with strings → unblocks p_choice, 879 uses
    4e. session_log, finduid, text_gender, stat_add/sub — small, wide

5.  Triggers the engine does not dispatch
    5a. queue / timer / softtimer / ai_timer  (275 uses)  — DONE, see
        osrs230_mockserver.md §3.19. 275 and not 273: this line omitted
        `softtimer` and counted `queue` at 152. All four already reached a
        script; what was missing was `canAccess()`, the timer type, an
        absolute timer clock, the four queue kinds and seven opcodes.
    5b. the *u use-on family       (546 uses)  — DONE, see
        osrs230_mockserver.md §3.20. 541 of 546; `opplayeru`/`applayeru` are
        the other 5 and rev 230 assigns them no wire opcode. 535 was
        opheldu+oplocu+opnpcu only; opobju 3, opplayeru 3, aplocu 2,
        applayeru 2, apnpcu 1 are the rest. The mock's inbound routing table
        had no row for any of the four wire packets, so 5b was packet
        handlers first and dispatch second. The trap it turns on: "use A on
        B" carries two obj ids and the subject is B, the target — the item
        reaches content only as `last_useitem`/`last_useslot`. `opheldu` is
        the exception, a four-rung chain that *swaps* the two as it searches.
    5c. zone / mapzone / zoneexit  (806 uses)  — DONE, see
        osrs230_mockserver.md §3.21. All four, both granularities: 427 are
        keyed off the 8-tile zone *including the level* and 379 off the
        64-tile map square with the level forced to 0, so they are two
        latches on the player and not one at two scales (§7.2). The producer
        is `mock230_world_update_map` in phase 10; execution is the ENGINE
        queue drained in phase 5 of the *next* tick, which is what lets a
        boundary crossed mid-dialogue hold its script instead of losing it.
        The `ZoneMap` is not in the path — that was the wrong structure to
        size this off. Three findings: `Mock230Player.zone_index` looks like
        `lastZone` and is reset on every rebuild, so a latch hung off it
        double-fires silently; the compiler was writing coord `lookup_key`s
        that no lookup could reproduce (78 of 427 negative, 349 wrapped,
        10 colliding) and now writes -1; and the miss path is cheap because
        the *latch* bounds the call rate — measured, the `snprintf` costs
        more than the failed lookup.

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

**Status of steps 2 and 3 (2026-08-01): all six landed**, and each one is
written up in its own section — §14 (step 2), §15 (3a), §16 (3b), §17 (3c),
§18 (3d), §19 (3e). §10.1's entry is the summary; §20 is what they left. Six
`port/*` artifacts and seven gate rules exist now that did not before, all of
them inside `make -C src test-port`. **None of the six steps ported a script**,
which is the shape of the phase: symbols are statements about what a name
becomes, and the slices in step 7 are what consume them.

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

  **Landed 2026-08-01 (§10.1). Every readiness number in the paragraph above
  was stale by the time it did**, and the two that were stale in the *helpful*
  direction are the ones worth keeping visible:

  | claimed here | measured 2026-08-01 | how |
  |---|---|---|
  | 4 missing opcodes | **1** — `obj_addall` (op 3501), **one** call site, `grip.rs2:10` | §12.2's scratch path, below |
  | 97.2 % opcode-clean (= 69/71 *scripts*, §5.2) | **98.6 % — 70/71** scripts; separately, **99.97 % of call sites** (29 command spellings over 3,129 sites, 1 unimplemented) | ditto |
  | 71 scripts | 71 files / 3,673 lines / 94 `[ai_queue3]` blocks, of which **69 files** were ported | `wc -l`, `grep -c '^\[ai_queue3'` |
  | 15 files bind a category | 15 files, **16** subjects (`man.rs2` carries two) | `grep -l '^\[ai_queue3,_'` |
  | 1 unresolved npc | **1** — `dwarf_mountain2`, confirmed | §14's gate |
  | — | **282** obj names referenced by the port, **0** unresolved | §10.1 |
  | 56 of 71 land without §7.6b | never tested — §7.6b landed first, and **69 of 71** landed with it | — |

  The count of missing opcodes is a *rate*, not a fact about the directory: it
  fell from 4 to 1 because the engine moved between §5 and here, not because
  anything was measured wrong the first time. Any similar number in §5, §8 or
  §11 should be assumed to have moved the same way and re-measured, not quoted.

  Of the 16 category subjects, six bind as categories and ten still cannot; they
  are bound to the reference's own member lists instead, which needed no id
  minted.
- **`levelrequire/`** — 10 scripts, 304 `[opheld2]` triggers, **zero missing
  opcodes, zero unresolved names, no category triggers.** Pure equip-requirement
  checks. The only directory in the tree that is clean on every axis measured.

I would do `levelrequire/` first — a half-day proof that the resolution gate
works end to end on real data with nothing else in the way — then `drop tables/`
once §7.6b lands, then Cook's Assistant as the slice that proves the port.

---

## 10.1 What landed

All three are done, in the order proposed but not on the schedule proposed:
`levelrequire/` and Cook's Assistant first, then §16's npc categories, then
`drop tables/` on top of them — 2026-08-01, and the last one is the first slice
in this document that had to land an engine prerequisite before it could bind.

### `levelrequire/` — ported as data, not as scripts

**No script from `levelrequire/` was ported, and that is the result rather than a
shortcut.** LostCity states 301 requirements as `[opheld2,<item>]` trigger lines
that claim the Wear verb and hand back to a content `~equip` proc reimplementing
equipping — wearpos conflicts, stackables, two-handed eviction. This server
already equips in C (`equip_from_slot`, reading wearpos_1/2/3 out of the cache)
and reads the requirement as data (`param=levelrequire,<stat>,<level>`), so
porting the scripts would have replaced a working engine path with a content one
needing eight opcodes it does not have — §7.7's hazard, in its purest form.

> **2026-08-02 — the conclusion above survives; the inference in its last
> sentence does not, and the inference was doing the work.**
>
> Two things were welded together here. *The requirement values are data, not
> script* — still right, and more so: the effective table is **1,496 objs and
> 2,122 (stat, level) pairs**, up to seven per obj, and nobody should
> hand-maintain 857 bindings for it. And *therefore the gate stays in C* — which
> does not survive. "Eight opcodes it does not have" became zero: five landed and
> two were misfiled (`osrs230_mockserver.md` §3.18). A refusal-with-a-message is
> a **rule**, and rules are content's. The two were joined only because nothing
> else could read the data.
>
> `~equip`, `~try_equip`, `~unequip_conflicts`, `~wearpos_conflicts` and
> `~dropslot` are ported now — `player/scripts/{equip,drop}.rs2` — `[opheld2,_]`
> and `[opheld5,_]` are bound, and `MOCK230_FALLBACK_OPHELD`,
> `equip_from_slot` and `mock230_equipment_may_wear` are deleted. Still no
> per-item `levelrequire/` script: the gate is **one** `~levelrequire_check`
> over `skill_combat/configs/levelrequire.dbtable`, which is this section's own
> "as data" conclusion given a script-readable home rather than reversed.
>
> **And the measurement below is half of the table.** The 301/200/0 comparison
> is about the `.obj` overlay — 857 objs, 1,254 pairs. The cache states its own
> requirement in params 434/436 and 435/437 for **639 further objs that no `.obj`
> file mentions**, a rune scimitar's Attack 40 among them. A content gate reading
> only the overlay passes every check in this section and silently stops gating
> 43% of the population. `~levelrequire_check` reads both halves; the overlay
> *replaces* the cache's for an obj it names, which is what
> `equipment_disputed.obj` relies on.

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

#### Amendment, 2026-08-02: the conclusion survives, the inference does not

The paragraph above welded two decisions together and only one of them holds.

1. *The requirement values are data, not script.* — **Still right, and more so.**
   Re-measured across `skill_combat/configs/*.obj`: **857 objs, 1,254 (stat,
   level) pairs** — 613 objs with one, 187 with two, 33 with three, 24 with
   seven, 23 distinct stats
   (`grep -rh param=levelrequire <tree>/server --include=*.obj | wc -l`).
   LostCity's 304 trigger lines are that same table wearing script syntax.
   Nobody should hand-maintain 857 bindings, and `mock230_pack` checks the data
   form at build time.
2. *Therefore the gate stays in C.* — **Does not survive.** The stated reason
   was "porting the scripts would have replaced a working engine path with a
   content one needing eight opcodes it does not have". As of 2026-08-02 the
   count is **zero**: `oc_wearpos`/`2`/`3`, `inv_movefromslot`, `inv_dropslot`
   and `inv_moveitem`'s generic arm all landed, `BUILDAPPEARANCE` and
   `P_CLEARPENDINGACTION` came off the list as miscited, and
   `player/scripts/equip.rs2` — `~try_equip`, `~unequip_conflicts_space`,
   `~unequip_conflicts`, `~wearpos_conflicts` — is written and exercised. §2.4
   item 7 exists precisely because a blocker's reason expires without the
   sentence changing; this is that.

A refusal-with-a-message is a **rule**, and rules are content's. "The data is
data" never implied "the gate is C" — the two were joined only because nothing
else could read the data.

**The resolution that keeps both halves** — **landed 2026-08-02**, and the shape
is one turn different from what this paragraph proposed — is to give the
requirement a script-readable home: a `levelrequire` **dbtable**. Not keyed by
obj with two parallel `LIST` columns; keyed by the **requirement**, with `stat`
and `level` as scalars and `obj` the LIST of everything that needs them. That is
125 rows rather than 857, because the requirements repeat hard, and `db_find` on
a LIST column means *contains*, so "which requirements does this obj have" is one
query with no arithmetic. It is the reference's own mechanism
for repeating tabular content data (`combat.dbtable`'s
`column=damagestyle,int,LIST` is the identical shape and already ships here), it
needs **zero new opcodes** (`db_find` / `db_findnext` / `db_getfield` are all
implemented, `mock230_ops_db.c`), it is **one** `[opheld2,_]` binding rather
than 857 — so the shadowed-ops report does not move — and it is still data,
generated from the same `param=levelrequire` lines, so this section's
single-source-of-truth claim holds literally. It also answers the thing
`fields/obj.ini` says it cannot do: a repeating (stat, level) pair "is neither a
native obj field nor anything the record's param table can hold". Correct — and
a dbtable is the third option that comment does not consider.

Two alternatives were considered and rejected. An `oc_levelrequire` opcode is
**barred**: no such command exists in the reference's `engine.rs2` and inventing
one is the `oc_desc` mistake. 857 generated `[opheld2,<obj>]` bindings (the
reference's literal shape) would work but cost ~3,500 generated lines, a second
generator, and would take `report_shadowed_ops` from 1 to 858 unless
`k_engine_held_verbs` loses Wear/Wield/Drop in the same commit.

`[opheld2,_]` and `[opheld5,_]` are bound now and `MOCK230_FALLBACK_OPHELD` is
deleted (count 5 → 4). The warning this paragraph carried was right and is worth
keeping as the reason the two stages were separate: dispatch is content-first, so
a content `~equip` with no gate *replaces* the gate and equips a rune platebody
at level 1 in silence.

**One thing this whole section had wrong, corrected where it is measured.** The
"857 objs / 1,254 pairs" the paragraphs above rest on is the `.obj` overlay
only. The cache states its own requirement in params 434/436 and 435/437 for
**639 further objs no `.obj` file mentions** — 1,496 and 2,122 effective — so a
gate reading the dbtable alone would have passed every structural check here and
stopped gating 43% of the table. `~levelrequire_check` reads both, the overlay
through the dbtable and the cache through `oc_param`.
`osrs230_mockserver.md` §3.18 has the full account, including the compiler trap
that made the first version of it wrong for every Attack requirement in the
game.

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

### 10.3b `lc_param` and `struct_param`, and the first shared param table

The other two landed in `src/net/mock/mock230_ops_param.c` — the second
per-domain opcode file after `mock230_ops_db.c`, registered by one line in
`mock230_script_command`. Coverage 224 → 226 of 399.

Two new tables, both built at boot from the cache group the rscache decoder
already handles (`RSCache_Dat2ConfigLocNewDecodeProfile`,
`RSCache_Dat2ConfigStructDecodeInplace` — the latter had been linked with no
caller anywhere in `src/`). Measured on `cache.osrs239`: loc 62,194 records
carrying 1,709 param rows retained in **41 KB**, struct 3,988 records carrying
20,751 rows (6,115 of them strings) retained in **877 KB**. No
`RSCACHE_PARAM_LONG` rows in either. Boot cost ~130 ms for the pair.

§5.1's "the two that carry the remaining 248 uses are the expensive ones"
was wrong about the loc half specifically: **loc's retained answer is 41 KB.**
The 60k figure is a *record* count, and 61,124 of those records carry no params
at all. What made it look expensive was reading the record count instead of the
row count.

Three things changed shape rather than just growing:

- **`push_typed_param` is now `mock230_push_typed_param` in
  `mock230_ops_param.c`**, declared in `mock230.h`. It was `static` in
  `mock230_scripts.c` and therefore unreachable from a domain file — a
  structural blocker on the whole per-domain split, not a style question. It was
  *moved*, not copied: `mock230_scripts.c` lost 75 lines and gained the one hook
  line plus two identifier renames, which is the cheapest merge shape available.
- **`mock230_paramtable.{c,h}`** is one flat `(owner, key) -> row` store with one
  `qsort` and one binary search. loc and struct use it. `mock230_objinfo.c` and
  `mock230_npcinfo.c` still carry their own copies — four copies of §10.3's bug
  would have been the wrong number, three still is; retrofitting those two is
  behaviour-neutral and their selftests already cover it.
- **The check is C, not content** (`make -C src test-mock230-param`), because the
  lane that landed this could not write to `OSRS-Content` or rebuild the pack.
  `mock230_ops_param` never touches `struct Mock230Server`, so it binds as a host
  callback with a NULL user — no world, no socket, no pack. The content-side
  `[proc,selftest_lc_param]` / `[proc,selftest_struct_param]` are still owed.

`src/net/mock/test/param_test.c` re-decodes both groups with the rscache decoder
and asks the table for **every** row it saw, rather than spot-checking: the
family's failure mode is a *miss*, and a spot check would have to be lucky. It
also counts the out-of-order records and fails if that count reaches zero, so the
assertion cannot quietly stop testing anything. No ids are written in the file —
every subject is located in the decoded data at run time, which is also what
keeps it honest when the cache is replaced.

Loc is the worst-ordered of the four tables: **525 of the 599** loc records with
two or more params are out of key order (87.6%), against struct's 1,847 of 2,833
(65.2%), obj's 68.1% and npc's 79.9%.

Five mutations were run:

| mutation | what it broke |
|---|---|
| drop the `qsort` | 6,201 struct + 526 loc rows became unfindable |
| force every result onto the int stack | `def_string $s = struct_param(...)` — string stack underflow |
| treat only `type=i` as int-typed | the `'1'`-typed subject routed to the string stack, and the abort fired |
| absent row answers 0, not the declared `default=` | 0 where 526 was declared |
| reverse `lc_param`'s pop order | 0 instead of the stored 2 |

The third mutation is §10.3a's lesson again and the test now selects for it
deliberately: it insists its int subject is declared something *other* than `i`
(`'1'`, `'o'`, `'S'`, …), because a subject typed `i` makes that mutation
invisible. The first version of this test passed it.

**Still open in the family:** `obj_param` (3509) pops *one* int and reads the
*active obj*. It has zero callers in the reference tree and nothing in
`src/net/mock/` ever sets an active obj, so the VM's pointer requirement would
abort before a handler could run. (`loc_param` (3011), the same shape over the
active *loc*, landed with the rest of the loc reads — §10.3c.)

### 10.3c The loc config reads — `loc_param`, `loc_name`, `lc_name`, `lc_width`, `lc_length`

`src/net/mock/mock230_ops_loc.c`, the third per-domain opcode file, registered
by one line in `mock230_script_command`. Coverage 226 → **231 of 399**.

The mutating half of the loc family stays in `mock230_scripts.c`'s switch with
the scene and the revert queue it needs. Only the config reads moved out, which
is the whole reason they *could* move: they touch the loc table and (for the two
active-loc ops) `mock230_scene_loc`, and nothing else.

**`loc_param` pops one int; `lc_param` pops two.** `loc_param` reads the active
loc; `lc_param` is handed an id. They are one word apart and now two files apart,
deliberately — reading one as the other leaves every later value on the wrong
rung of the int stack and nothing fails at the call.

**The active loc is held by slot** (`slot + 1` in the VM's active-entity
pointer), the same convention the big switch uses and for the same reason: a
script can suspend between `loc_find` and here and a scene rebuild reallocates
the array. The VM's `require = 0x040` guarantees the pointer is *present*, not
that the slot is live, so the handler re-resolves and aborts on a dead slot.

`mock230_scene_find_loc` is **not** used for this. Its `loc_id` argument does not
filter (`return loc_id >= 0 ? fallback : fallback;`), so it returns the first loc
on the tile whatever id it is asked for. `mock230_loc_known` — a one-bit-per-id
bitmap, ~7.8 KB — is the existence check, matching the reference's
`check(id, LocTypeValid)`.

**What `mock230_locinfo.c` retains now**, all built in the decode pass that was
already happening for params:

| table | rows | bytes |
|---|---:|---:|
| params | 1,709 | 41,016 |
| names | 30,033 | 565,681 |
| footprints (non-1x1 only) | 17,309 | 138,472 |

The ~700 KB that §10.3b's file header quoted for names, and declined to pay, was
`strdup` per record. One concatenated blob plus a sorted (id, offset) index over
only the 30,033 named records is ~560 KB in two allocations. Re-measured, not
inherited.

**Four opcodes were measured and cut, each for its own reason:**

| op | uses/files | why not |
|---|---:|---|
| `loc_anim` 3002 | 56 / 23 | **The largest single loc gap, and it is a missing wire packet.** `LocOps.ts:50` → `World.animLoc(...)`, a **zone event**; `zone_sub_opcode` in `mock230_encode.c` has `LOC_ADD_CHANGE`, `LOC_DEL`, `OBJ_ADD`, `OBJ_DEL`, `OBJ_COUNT` and no loc-anim. Needs a new `MOCK230_ZONE_EV_*` kind, an encoder arm and headless-client verification. Its own item. |
| ~~`loc_category` 3003 / `lc_category` 4100~~ | 38 / 16, 3 / 1 | **Landed 2026-08-02**, `mock230_ops_loc.c`. The blocker was right — case 61 was `g2(buffer); // Skip unsigned short` — and so was the price: it is an `EXCEPTIONS.md`-governed edit and the fidelity suite failed on the first run, because a decoder that keeps a field the *text* form cannot express takes `cachepack`'s loc `lost-here` from 0 to 205. `cp_loc.c` gained a `category=` key and it went back to 0; byte-exact loc records went 581 → 786. Opcode 61 was confirmed against the data rather than against a reference client (neither `Client-TS` here decodes it): the ids group semantically — 684 is 63 records, 43 of them 'Bank booth'; 907 is 360 bookshelves — and share a space with npc 18 and obj 94. Both opcodes push -1 for "none", not the reference's raw 0, because `pack/category.pack` reserves 0. |
| `lc_desc` 4102 | 0 / 0 | **0 of the 62,194 records carry a `desc`** — the field is gone from OSRS loc configs, so a handler could only push the reference's `'null'`. Zero callers besides. |
| ~~`lc_debugname` 4101~~ | 1 / 1 | **Landed 2026-08-02**, and the stated reason was a category error rather than a fact: `debugname` is indeed not a dat2 field, but it is not supposed to be one — it is the `[block]` header of a config file, and this tree has that table, `pack/loc.pack`. `mock230_content_symbol_name(MOCK230_PACK_LOC, id)`, with the reference's `'null'` fallback. Landed for its one caller, `stairs.rs2:431`'s `mes("Unhandled stairs: <lc_debugname(loc_type)> at")` — the line that tells a port which locs it missed. |

**A census correction worth carrying forward.** The `loc_*` row in §5's table
reads `loc_param 133`. Re-measured over the reference's `content/scripts/`
excluding `engine.rs2`: **`loc_param` 140 / 56 files, `loc_anim` 56 / 23,
`loc_category` 38 / 16, `loc_name` 4 / 3.** The earlier figures were low because
the census pattern required a `(` — and `loc_name`, `loc_category`, `loc_coord`,
`loc_type` and every other zero-arg command is written bare. This is the same
blindness §7's note records for `loc_coord` (3 recorded, 1,002 real) and it is
systematic: **any ranking built on a `name(` pattern under-counts every zero-arg
opcode.** `last_useitem` — 800 uses across 213 files, three times `loc_param` —
is invisible to every table in this document for exactly this reason.

**The check is `make -C src test-mock230-loc`** (`src/net/mock/test/loc_test.c`),
C rather than content for the same reason §10.3b's was: this lane could not write
to `OSRS-Content` or rebuild the pack. Half 1 re-decodes the whole loc group and
asks the tables about every record in **both** directions — 32,161 nameless
records make "reads back nothing" the sharper assertion, because an off-by-one
binary search returns a *neighbour's* string. Half 2 builds a real scene at the
selftest's own zone, adds real locs and sets the active-loc pointer the way the
server does; `def_string $s = loc_name;` is the string-stack assertion.

Eight mutations were shown to fail it: transposing `lc_width`/`lc_length`;
pushing `loc_name` to the int stack; giving `loc_param` `lc_param`'s two-pop
arity; making the id check always pass; answering a footprint miss with 0x0;
returning the nearest row instead of NULL on a name miss; dropping the loc param
`qsort`; and reading the active-loc pointer as a raw slot instead of `slot + 1`.

**One mutation does *not* fail it, and that is stated rather than hidden:**
removing the `qsort` over the name and footprint tables. `archive->file_ids` is
ascending on this cache, so both sorts are no-ops today. They are kept because
nothing promises that ordering, but this test does not prove them. (§10.3's
param sort is a different matter and remains emphatically load-bearing.)

### 10.3d The npc reads and the hunt searches — and `npc_param` was wrong

`src/net/mock/mock230_ops_npc.c`, the fourth per-domain opcode file, registered
by one line in `mock230_script_command`. Coverage 231 → **237 of 399**. Landed:
`npc_param`, `npc_category`, `nc_category`, `npc_hasop`, `npc_huntall`,
`npc_hunt`, `npc_findcat`. Full detail in `osrs230_mockserver.md` §3.13g.

**The coverage number is not the headline.** `npc_param` (2529) was *already*
implemented, in `mock230_scripts.c`'s switch, and it was wrong: it compared the
popped param against a single `mock230_content_symbol(MOCK230_PACK_PARAM,
"death_drop")` — a game-facing name in C — and pushed **0 for every other
param**, on the int stack, ignoring `default=`. It never consulted
`mock230_npc_param`, which has been loaded and public all along.

Nothing here could see it, and the reason generalises. `gen_opcode_coverage.py`
derives coverage from the presence of a `case SS_OP_*:` label, so **an opcode
that is implemented badly is indistinguishable from one implemented well**. 2529
has been counted as covered, the load-time gap report has been silent, and
`--selftest` has been green, for as long as the case has existed. Re-measured,
`npc_param` is **291 uses across 137 files** — three times the next npc figure —
and the committed combat slice reads through it (`skill_combat/combat_stats.rs2`
:383 `stabdefence`, :459 `strengthbonus`, :489 `damagetype`). Every npc defence
roll and max hit in that slice was computed from bonus 0.

The old case is now unreachable dead code, because the domain hooks run before
the switch. **Deleting it needs the same one-time `mock230_scripts.c` exception
§10.3b took**; this lane was allowed one hook line and nothing else.

**The obvious fix was a regression, and the hardcode was hiding why.** Reading
`mock230_npc_param` — the *cache record's* param table — answers 0 for
`death_drop`, because `death_drop` is a **server-band param authored in a rank-1
`.npc` overlay** and is nowhere in the cache. The drop tables would have added
obj 0 at 7 call sites. So `apply_param` in `mock230_content.c` now files every
authored row under its param *id* as well as into the named C field, and
`npc_param` reads the overlay first and the cache second — rank 1 over rank 0,
`CONTENT_ARCHITECTURE.md` §3.1's own merge. Measured: 39 npc defs, 205 authored
rows. This is a partial close of §7 item 1: the overlay params' **values** are
now script-visible; their **types** still are not, because `load_param_types`
reads only `configs/all.param`.

Three smaller findings, all measurements rather than opinions:

- **A name gate was hiding a field.** `mock230_npcinfo()` reports a placeholder
  for a record with no name. Of cache.osrs239's 16,292 npc records, 9,149 carry
  a category and **1,585 of those are nameless**; 10,505 declare a menu op and
  177 of those are nameless. `mock230_npcinfo_record()` is the new ungated
  accessor. `category` (config opcode 60) was already being decoded by the
  linked rscache decoder and thrown away in `mock230_npcinfo.c`.
- **`npc_huntall` is not `npc_findallany`.** The reference's hunt iterator
  (`ScriptIterators.ts:274-280`) skips any npc whose type declares no `op[1]`.
  Without that filter, "every npc nearby" hands content the scenery. `npc_hunt`
  and `npc_findcat` additionally *filter* by Chebyshev range and *rank* by
  euclidean-squared, with a `<=` tie-break that keeps the last candidate — three
  details that each pick a different npc when got wrong.

`make -C src test-mock230-npc`. Ten mutations shown to fail it, listed in
§3.13g. One worth repeating here because it is a trap for any future search
opcode: **a membership assertion cannot catch a swapped coord/distance**, since
a coord literal packs to a number in the millions and a radius of millions finds
everything. Asking for an empty result at distance 0 is what catches it.

**Eight npc families were deliberately not landed** (see §3.13g for each), and
the shape of the list is the finding: seven of the eight are blocked on per-npc
*engine state* that does not exist here, not on opcode work. The largest —
`npc_walk`, re-measured at **108 uses / 45 files** — needs a waypoint queue on
`struct Mock230Npc` and a drain in `advance_npcs`, because `NpcOps.ts:451` is
`queueWaypoint` and this server's only mover takes one step toward a target its
caller supplies. `npc_statheal`/`statsub`/`statadd` and
`npc_changetype_keepall` (36/17) all want the same missing thing: a per-npc
mutable `levels[]`. **That is the next npc item, and it is engine work in
`mock230_world.c`, not another ops file.**

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

~~`loc_param` is deliberately absent — it is `runtime_typed` like `oc_param` and
needs a runtime loc-config decode that does not exist (only `mock230_pack.c`
decodes loc configs today, for validation). That is 60k+ records at boot and a
real memory question, not a copy of what landed here.~~ `loc_findallzone` /
`loc_findnext` need the ZoneMap from §7.2.

> **Superseded, 2026-08-01.** `loc_param` is implemented (§10.3c) and the memory
> claim was wrong: it read a *record* count as a memory figure. The whole loc
> table — params, names and footprints — retains **745 KB** against a 73 MB boot
> RSS, and 61,124 of the 62,194 records carry no params at all. This is §12's
> rule again: re-check a documented blocker before quoting it.

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
nothing ever set or read them. Phase 4 now runs timers then queues, dispatching
by npc *type* so an npc that changed type between queueing and firing runs the
new type's script.

The test asserts the negative for both: that `npc_settimer(0)` **stops** the
timer, which is the only way to tell "stopped" from "not due yet", and that a
`[ai_queue1]` does not fire early.

**Two corrections to the paragraph above, from step 5a** (osrs230_mockserver.md
§3.19), both of which the test had been pinning the wrong way round:

- The order is **timers then queues** — `Npc.processNpc` calls `processTimers()`
  then `processQueue()`. This ran queues first, under a comment claiming it
  matched the reference.
- `npc_queue(q, arg, 1)` fires on tick **+1**, not +2. The reference compares the
  counter's value *before* the decrement for a player and *after* it for an npc,
  so an npc's delay 0 and delay 1 both land on the next npc phase. This engine
  stored `delay + 1` for both, and the selftest asserted that — a test can be
  green and still encode the wrong convention.

Also from 5a: `Mock230Npc.timer_script` and the second drain that read it are
gone. The field was written in exactly one place and only ever to `-1`, so
`mock230_scripts_process_npc_timer` could never run. The npc queue's `arg` is
still stored and never passed — `[ai_queue<n>]` gets no `last_int`, where the
reference sets `state.lastInt = request.lastInt`.

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
| `npc_walk` | 44 | needs npc step queues — **re-measured 45 files / 108 uses; still open after §10.3d, and it is engine state, not an opcode** |
| `inv_setslot` | 33 | trivial |
| `db_find` | 23 | the db layer exists (`mock230_db.c`) |

`loc_param` is done (§10.3c). `npc_walk` is now the largest single item in this
table and the one that has not moved: §10.3d refused it deliberately rather than
faking it, because `NpcOps.ts:451` queues a *waypoint* and this server has no
queue to put one in. See §10.3d's closing paragraph for what it actually costs.

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

The band has since taken three more: `runclientscript_ss` (11002) and
`runclientscript*` (11003), both in [`runclientscript.md`](runclientscript.md),
and `p_countdialog_noprompt` (11004) — the wait half of `p_countdialog` without
its chatbox prompt, for interfaces that produce a number themselves
([`bank_pin_server_reqs.md`](bank_pin_server_reqs.md) §4).

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
| ~~`runclientscript` takes ints only~~ | **false, and false in both directions** — the encoder always took a type string, and `~p_choice*` never needed one. `runclientscript*` (11003) now sends any mix; see §7.4's correction and [`runclientscript.md`](runclientscript.md) |

### The symbols phase — §9 steps 2 and 3a–3e, all six

Written 2026-08-01. Six steps, one after another, each with its own section
(§14–§19) because each found something the plan did not contain. This is the
summary; §20 is the queue they left.

**What it was.** The whole of §9's prerequisite block: the name-resolution gate,
then constants, npc categories, the five server-allocated config namespaces, the
varp/varbit reclassification, and the npc/loc/seq/spotanim name maps. **Not one
of the six ported a script**, and that is the shape of the phase rather than a
shortfall — a symbol step produces a *statement about what a name becomes*, and
the thing that consumes it is a slice.

**What it cost, measured on both loaders:**

```
                       before        after
mock230_pack symbols   213,430  ->   213,623      (§12's old baseline was stale by 100
constants                  136  ->       306       before any of this: 213,530 / 284)
npc defs                    38  ->        39
varp defs                   16  ->        39
equip reqs               1,419  ->     1,496
spawns                      62  ->     63 + 12
sscompile symbols      212,829  ->   212,922      +75, the same 75 mock230_pack sees
db columns                   9  ->        52
carrier varps                —  ->     2,872      (new; both readers derive it, agreeing)
errors / warnings          0/1  ->      0/13      the 12 are `combat block, no Attack op`
```

Records landed: 22 constants, 18 category names, 27 params, 20 enums, 7
dbtables, 21 dbrows. **Records classified: 4,279 signed name rows, 1,735
constants, 53 categories, 1,650 config blocks, 357 variables, 524 name maps** —
six `port/*` artifacts, every one of them re-derived from both trees by a rule
inside `make -C src test-port`. The ratio is the finding: the classification is
two orders of magnitude larger than the copy, and it is the part that could not
have been done by a bulk import.

**What it found that nobody expected.** Each step's real result was a defect in
something already believed to work:

| step | the premise | what was actually true |
|---|---|---|
| 2 (§14) | "no default, ever" is a policy | four npc params resolved a typo to **-1 silently** — and `mock230_content_symbol` maps the literal `null` to -1 too, so a typo and a deliberate nothing were the same value. cachepack counted an unresolved `ref =` as a *warning*, skipped the field, and exited 0 |
| 3a (§15) | constants have no dependencies | **111 names exist in both trees and 14 disagree on the value** — 12 prayers and 2 headicons. A namespace with no gate at all: `ss_unresolved` skips it, `port_name_diff` compares records, `mock230_pack` validates ids. Also: **0 of 1,562 constants name an id**, so the pack-resolution rule the plan wrote for them was vacuous |
| 3b (§16) | "an osrs239 npc record carries no category at all. Not 'unread': absent" | it is on **9,149 of 16,292 records**, 982 distinct ids. The decoder has read opcode 18 all along; `mock230_npcinfo.c` simply never copied it. One wrong word in one comment held the middle rung of the trigger lookup shut for the entire npc domain |
| 3c (§17) | ids are allocated off `max+1` with a floor | **every `server_base` in `content_register.c` was advisory.** `ss_allocate.py` read `content.ini`, which declares no `base =` key. Making the floor live printed `varp base_was=8000` against a 6,217-entry array — the next server varp would have been dropped by a bounds check. Also: `mock230_pack` never loaded the `.dbtable`/`.dbrow` half of the tree, and an unrecognised column type was read as a literal, turning every `synth` *name* into 0 |
| 3d (§18) | 28 varps clobber varbits; the other 147 are `varn`/`vars` or nothing | 27 clobber; **varn is 0 and vars is 0.** The reference already used `varn` where it meant npc state. And six names *resolve to the wrong concept*: `%prayer9`–`%prayer12` are the Clan Wars block here, 35 whole-varp writes' worth, while `%prayer2..8` fail to compile — one family, silent corruption for five members and a build error for nine |
| 3e (§19) | 206 names, mostly naming drift | 524, and **all four of §4's worked examples were wrong records** (§4 above). The row that matters is `macro_bigfish`, which satisfies *every* automatic rule — unique display, matching vislevel, injective, target not itself a reference name — and is a Shadow-of-the-Storm troll |

The through-line is one shape, and it is the reason §13 bar 6 exists: **six times
in six steps, the thing that was wrong resolved cleanly.** `rock_sample1` is obj
671 in both trees with the id identical and the name resolving, and it is an
Animal skull. `_giantrat` reads as the rat category. `%prayer9` reads as a varp.
`macro_bigfish` reads as a troll. Nothing failed; something was just quietly
different.

**Mutations, because a gate nobody has broken on purpose is a comment.** Every
rule landed in this phase was proved by editing the tree to violate it,
confirming the failure, and reverting: 4 in §14 (typo'd `death_drop` in both
loaders, duplicate constant, `%name` ambiguity, edited signed row), 4 in §15
(deferred constant copied in, a `rederived` value set back to the reference's,
an edited landed value, an undeclared constant), 5 in §16 (`4999=ghost_category`,
`0=unstated_category`, `275=black_demon` — which `mock230_pack` accepts and the
python check refuses, *which is the argument for two layers* — a dropped name, a
deleted row), 5 in §17 (`table=prayer_tabel`, `column=sound,synth`, a
`defer-slice` block landed, `[music]` authored, a deleted file), 5 in §18
(`%prayer0 = 0`, a carrier read, `wholewrite=allow` on a non-carrier,
`wholewrite=maybe`, and the runtime backstop with its exclusion removed), 8 in
§19 (deleted row, swapped manifest target, edited `display=`/`frames=`, a
`family` row given a target, two rows on one target, evidence stripped, and a
probe `.npc` spelling `macro_bigfish`). The content tree was verified clean after
each.

**What was deliberately NOT done, and the reasoning in one line each:**

- **1,112 of 1,562 constants were not copied**, though the plan sized 3a as
  "1,138 mechanically". The tree's own precedent says why: `quest_cook.constant`
  carries Cook's two lines and states that "a constant for a quest nothing can
  start is a name waiting to be resolved against the wrong thing." §11 says the
  same thing about the quests themselves. `port/constants.map` is the work order
  if that is overruled.
- **No `.struct` landed and none can** — no loader anywhere in `src/`, and
  `SS_OP_STRUCT_PARAM` has meta and no `case`. An engine prerequisite, not a
  follow-up.
- **No `.varp` or `.varbit` file was written**, for 3a's reason: 3d ports no
  scripts. All 19 evidenced `varbit` rows need no new content anyway.
- **Seven npc dispatch sites still pass a literal `-1`** (§16.6). They are listed
  beside `mock230_npc_category()` so the lane that owns those lines adopts them
  one token each; a category of -1 and a category nothing binds are
  indistinguishable, so it is additive. `AI_QUEUE3` is the one that matters —
  `drop tables/` binds 16 of its 94 there.
- **Nothing was committed.** `port/` is untracked in the content submodule and
  holds all six baselines; without them `test-port` fails on a fresh checkout
  with "no baseline".
- **Client verification is vacuous for all six steps and is stated rather than
  skipped.** No client code, packet or interface was touched. The closest real
  check is `test-mock230` — boots the server, compiles and loads 319 scripts,
  reads every table and row, builds the scene — green throughout, and it now
  also asserts that no whole-varp write to a carrier happened during the run.

**One process finding worth more than any of the numbers.** The reference
checkout is live and was written to *during* three of the six steps
(§14.5 finding 1, §15.9, §16.10): `+2` obj, `+5` seq, `+4` spotanim records
appeared between two green runs and turned `port_name_diff --check` red on rows
unrelated to the work in hand. Every bar computed against `LostCity_Server` is
only as reproducible as that checkout, and the correct response is a recorded
commit (§12) rather than a re-baseline nobody reads.

### `drop tables/` — ported as scripts, 69 of 71 files

Written 2026-08-01, on top of §16.11's category work. **The decision the slice
was posed as — scripts or data — went to scripts, and the reference decided it,
not taste.** LostCity has a `drop_table.dbtable` and a `~roll_on_drop_table`
proc that walks it, and this tree already had both. They are a red herring for
this directory: `~roll_on_drop_table`'s only caller anywhere in the reference is
`skill_mining/scripts/mining.rs2:226` for `gem_rock_table`, and there is **not
one `.dbrow` in `drop tables/`**. All 71 files are threshold walks over
`random(128)`, and what they walk cannot be a row: `map_members` gates 28 rungs,
six read a quest varp, one reads `inv_total(worn, ring_of_wealth)`, one branches
on `coordz(coord) > 6400`, several call a sub-table proc from inside a rung and
one writes `session_log` as a side effect. The `levelrequire/` precedent points
the other way for the same reason it applied there: porting *those* scripts
would have replaced a working engine path with a content one, and porting
*these* as data would replace a working content path with a schema that cannot
express half of it. **What the data choice would have cost, stated so the call
is checkable:** a `.dbrow` per table (69 names to mint in a namespace whose ids
are ours), a `param` binding npc -> dbrow, and either a `condition` column the
reference's schema does not have or the conditional rungs left out.

**Two slices have now answered the same question in opposite directions, which
makes it a rule rather than a preference.** `levelrequire/` went to data and
`drop tables/` went to scripts, and the test that separates them is not "is it
tabular" — both look tabular — but **which side of the seam already owns the
behaviour here**:

| | `levelrequire/` | `drop tables/` |
|---|---|---|
| what LostCity's scripts do | reimplement equipping in content | roll a table in content |
| what this server already does | equips in C, reads the requirement as `param=levelrequire` | nothing; `death_drop` is a single fallback item |
| porting the scripts would | replace a working **engine** path with a content one (§7.7) | be the only implementation |
| verdict | **data** — transcribe the values, drop the scripts | **scripts** — port them |

So: *port the layer that is missing here, not the layer the reference happens to
write it in.* The tell in both cases was found by asking what already answers
the behaviour on this side, and the failure mode is symmetric — porting
`levelrequire/` as scripts would have duplicated an engine path, and porting
`drop tables/` as data would have thrown away every conditional rung to fit a
schema nothing was asking for. Neither call is about which representation is
tidier.

**Landed:** `server/scripts/drop_tables/scripts/`, one file per reference file
so the two trees diff, 69 of 71, **4,030 lines**. **137 npc-family bindings** —
136 `[ai_queue3]` and one `[opnpc1,jonny_the_beard]` — plus 22 `[label]`s and 6
`[proc]`s. (`port_droptables_check --report` prints **175**; that is the whole
content tree, of which this directory is 137 and the other 38 are Lumbridge,
thieving, the bank and the selftest. The two numbers get confused easily and the
one that belongs to this slice is 137.) The four-file partial port
(`goblin.rs2`, `humans.rs2`, `livestock.rs2`, `imp.rs2`) is merged into it, not
appended: `humans.rs2` and `livestock.rs2` are gone, their content is in the
reference's `man.rs2`, `guard.rs2`, `chicken.rs2`, `cow.rs2`, `rat.rs2` and
`giant_rat.rs2`, and every binding this world had still has one.

**Measured against what §10 and §5.2 claimed. Every row moved except the last
two, and none of them moved because the directory changed:**

| | claimed | measured 2026-08-01 |
|---|---|---|
| missing opcodes | 4 | **1** — `obj_addall` (3501), one call site, `grip.rs2:10` |
| scripts using only implemented commands (§5.2's metric) | 69/71 = 97.2 % | **70/71 = 98.6 %** |
| the same thing counted by call site | — | **99.97 %** — 29 command spellings over **3,129** sites, one unimplemented |
| category-keyed files | 15 of 71 | 16 subjects across **15 files** (`man.rs2` carries two) |
| unresolved npc | 1 | **1** — `dwarf_mountain2`, confirmed |
| obj names | — | **282 referenced by the 69 ported files, 0 unresolved** (287 across the reference's 71) |

The command census is a token scan of the reference's 71 files against
`g_ss_opcode_names` in `src/serverscript/ss_meta.gen.h` and the implemented set
in `src/net/mock/mock230_opcode_coverage.gen.h`, counting a bare command
(`npc_coord`, `npc_findhero`, `map_members`) as a site — which is why it is
3,129 and not the ~1,700 a `name(` scan finds. `npc_coord` 1,334 and `obj_add`
1,312 are 84 % of it. **`mock230_scripts_report_gaps` cannot answer this
question before the slice lands**, because it reports on loaded content only;
that is why it is a scratch measurement (§12.2) and not a bar.

**The obj number is the finding that changed the port.** The existing four-file
port trimmed the goblin's, the men's and the livestock's tables and said why:
"the runes it drops replaced by ones this content tree has symbols for",
"reduced to the coins-and-bones core ... none of which this tree models". That
premise was wrong. `earthrune`, `bodyrune`, `waterrune`, `brass_necklace`,
`air_talisman`, `beer`, `bronze_sq_shield`, `bronze_spear`, `chefs_hat`, every
`unidentified_*` herb and every `uncut_*` gem resolve in `all.obj.compack`. The
tables are back at full width.

The display-name review of those 282 is the signed one (§14), not a hand pass,
so it re-runs: `port_name_diff --report`, filtered to the names the ported files
mention.

| verdict | n | what they are |
|---|---:|---|
| identical | 232 | |
| different-thing | 16 | 14 of them the herbs — LostCity calls every unidentified herb 'Herb', this cache names each one ('Grimy guam leaf'). Plus `bolt` 'Bolt'/'Bronze bolts' and `intelligence_report` 'Scroll'/'Intel report'. |
| plausible-sibling | 12 | the same disambiguation on capes, hides, wizard robes, and three spelling fixes (`Adamnt`→`Adamant`, `Cadaver`→`Cadava`, `Wizards`→`Wizard`) |
| formatting-only | 1 | `cow_hide` 'Cow hide'/'Cowhide' |
| **no row at all** | **21** | |

**The 21 is the row that matters and it is not a rounding error.**
`port_name_diff` only emits a row for a name LostCity's *authored* tree states;
a name that exists only in its `pack/` (reserved for content living in the cache
it was built from) is skipped, because there is nothing to compare and so
nothing to sign. `coins`, `knife`, `rope`, `fur`, `keyhalf1`, `keyhalf2`,
`muddy_key`, `sinister_key`, the six `cert_*` and seven more are in that state:
**7.4 % of the obj names this slice drops are unreviewed, and no bar in the tree
can see it.** They resolve, they compile, and nobody has asserted they are the
same item. This is the same shape §14 found for npc (below), one namespace over.

**The npc side is worse, and it produced this slice's one real defect.** The 69
files bind **130 distinct npc subjects**. Run the same filter over the npc rows:

| verdict | n |
|---|---:|
| **no row at all** | **75** |
| identical | 39 |
| formatting-only | 14 (`Mountain Troll`/`Mountain troll` ×8, `Troll General`/`Troll general` ×3, `Al-Kharid warrior`, `Entrana Fire Bird`, `Black Demon`) |
| unnamed | 1 — `jonny_the_beard`, a multinpc base with no `name=` here |
| **different-thing** | **1** |

**58 % of the subjects are unreviewed** because the reference states them only
under `scripts/_unpack/`, which `port_name_diff` does not walk. And the one row
that *is* signed `different-thing` is the `rock_sample1` shape (§4.1) arriving
for real, in a port, for the first time:

```
npc  death_man_indoors1   LostCity: 'Unferth' (vislevel 6, category=citizen_burthorpe)
                          osrs239:  'Man'     (vislevel 2, NO category)
```

Its six siblings in that block — `death_man_indoors2` 'Penda',
`death_man_outdoors1` 'Breoca', `death_woman_indoors1` 'Hild' and the rest — all
keep their names *and* carry category 564/565. This one name did not survive:
osrs239's Unferth is `twocats_unferth_*` (category 341), and `death_man_indoors1`
here is a generic Burthorpe man. The missing `category=` is the tell, and it is
visible in `port_droptables_check --report`'s category column as a bare `-`
beside six rows reading 564.

**The port binds it anyway, deliberately.** The reference's
`citizen_burthorpe` table is a citizen table, this record *is* a Burthorpe
citizen, and giving it that table is right for the record that is actually
there — but it is right by accident, not because the name meant the same thing.
The generalisation is the one worth keeping: **a name-resolution gate cannot see
this, and neither can a compile; only the signed diff can, and it is blind on
75 of the 130 subjects here.** Anything that expands a reference category to its
member list is doing 130 of these lookups at once.

**Categories: six bound as categories, ten expanded to their members.**
`_bear` (438), `_chicken` (444), `_cow` (365), `_ice_warrior` (466),
`_unicorn` (2263) and `_werewolf` (454) are bound as the reference writes them,
and they are the **first and only content in this tree that exercises the
category rung of `SSVM_ProviderGetByTrigger`** — §16.11's item 1, now held. For
the other ten the reference's category is still a *list of npc names*, and those
names resolve here, so each is bound member by member — **52 bindings** where
the reference writes ten trigger lines:

| file | reference | here |
|---|---|---:|
| `man.rs2` | `_citizen` | 10 |
| `man.rs2` | `_citizen_burthorpe` | 7 |
| `mountain_troll.rs2` | `_mountain_troll` | 12 |
| `troll_commander.rs2` | `_troll_general` | 7 |
| `giant_rat.rs2` | `_giantrat` | 4 |
| `pirate.rs2` | `_pirate` | 4 |
| `bandit_camp_leaders.rs2` | `_bandit_camp_leader` | 3 |
| `black_demon.rs2` | `_black_demon` | 2 |
| `guard.rs2` | `_guard` | 2 |
| `barbarian.rs2` | `_barbarian` | 1 |

Only three members have no
spelling in this cache — `barbarian_woman`, `guard2`, `dragonslayer_giantrat` —
and each is named in the file that lost it. **Nothing was minted and no split
was resolved**: minting `citizen` would have to invent two names (266 men, 492
women) the reference does not have, which is the objection §16.11 raised, and
the port does not get to overrule it by being the one that wants it. What the
expansion costs is one sentence in `shared_droptables.rs2`: an osrs239 npc
carrying category 266 that is not one of the reference's `citizen` members gets
no table. Three of this world's men are exactly that and are bound by hand.

**`[ai_queue3,chicken]` and `[ai_queue3,cow]` are gone.** They were exact
bindings that would have shadowed the category ones (§16.11 item 2), and
removing them is what makes the chicken's loot a live test of the category rung
rather than a decoration.

**The double drop is resolved by a rule, not by inspection.** Binding
`[ai_queue3,<npc>]` at all wins the lookup ahead of anything more general, so a
bound table that does not itself state
`obj_add(npc_coord, npc_param(death_drop), …)` silently takes the bones off an
npc that had them. (Until 2026-08-01 the more general thing was
`MOCK230_FALLBACK_AI_QUEUE3`, C. It is `[ai_queue3,_]` in
`skill_combat/npc_combat.rs2` now and the fallback row is deleted —
`osrs230_mockserver.md` §3.18 — which changes nothing about this rule: a bound
table still shadows it, per binding.) In the reference this costs
nothing — its engine has no such fallback, `death_drop` is only a param content
reads — so six reference tables legitimately state no death drop
(`earth_warrior`, `jonny_the_beard` which names `bones` directly, and the four
kalphite files). Those six carry a `// no-death-drop: <reason>` waiver and the
other 130 bindings reach one — 71 `obj_add(npc_coord, npc_param(death_drop), …)`
statements in 64 files, because a `@label` serves many bindings. The rule is
enforced per *binding*, not per file, and not remembered — see the check below.

**`rat.rs2` is the one rung in the slice that is not the reference's.** The
reference's rat drops nothing but a Witch's Potion tail, because its own `[rat]`
record carries `param=death_drop,null`. This tree's does not, an OldSchool rat
drops bones and raw rat meat, and `livestock.rs2` already dropped both. Kept and
marked as the modernisation it is (§7.8), rather than regressed to 2004 in the
name of fidelity.

**Not ported, each stated where it happens:**

- `grip.rs2` — Heroes' Quest progression, `%npc_aggressive_player` (a **varn**,
  and `content.ini` has no `[namespace:varn]` at all), and the slice's one
  unimplemented opcode. It is the whole of the `obj_addall` gap.
- `drop_table.rs2` — `~roll_on_drop_table`, no caller here or in the slice.
- `gosub(npc_death)`, all 76 sites — death bookkeeping, engine here (§2.3).
- `~trail_*cluedrop`, all 21 sites — Treasure Trails, which this tree does not
  model. Dropped, not stubbed.
- Six quest gates, dropped **whole** rather than un-gated: Observatory Quest,
  Heroes' Quest, Legends' Quest, Troll Romance (twice), Witch's Potion. An
  un-gated rung is the opposite of what its gate says — un-gate
  `mountain_troll` and every kill of Twig hands out a quest key.
- `[ai_queue2,salarin_the_twisted]` — combat, and it needs
  `~npc_default_damage` from `skill_combat/`.

**`if (npc_findhero = ^false) { return; }` is KEPT at every site that gates a
drop table**, which is the one place this port is less aggressive than the
four-file one it replaces. `SS_OP_NPC_FINDHERO` is a stub pushing 1, so the
guard is dead today; the day it is not, it is the difference between "the killer
gets the loot" and "anything that dies drops a table". Measured, because the
count moved and the reason is informative: the reference states it **79** times
and the port states it **76**. Four went with things that were not ported — the
`if (npc_findhero = ^true) { ~trail_checkmediumdrop; }` head of `guard.rs2` and
`guard_dog.rs2`, and the Troll Romance key gates at the head of
`mountain_troll.rs2` and `troll_commander.rs2` — and `shared_droptables.rs2`
adds one back. **Not one of the 76 that guards a table was dropped.**

**Verified in the real client, headlessly** — `SDL_VIDEODRIVER=dummy`,
`manifest_osrs230_embed.ini`, `TORIRS_NET_CHEAT="…;tele …;fight"`,
`TORIRS_SIM_CLICK_AT` right-click on the corpse tile, `TORIRS_EXIT_BMP`:

- **a chicken** (teleport to 3231,3296) — the right-click menu on the loot tile
  reads *Take Raw chicken* / *Take Bones*. The chicken has no binding of its own
  any more, so this loot exists only through category 444. That is the AI_QUEUE3
  category rung, proven from the client's own decode of the OBJ_ADD zone
  packets, not from a server log line.
- **a goblin** (3221,3269) — *Take Bones*, on the per-npc binding, with the
  `random(128)` roll landing in the empty 63..127 range.

`mock230_pack --check-only`: **0 errors, 13 warnings** — unchanged.
`mock230 --selftest`: all checks passed, **462 scripts** (319 before), and the
gap report stays silent, which is the selftest asserting no new missing opcode.
`make -C src test-content`: green.

**Re-measuring this entry** (§12's discipline — every number above came from one
of these, and the two that did not reproduce on the docs pass were corrected in
place: the binding count, which was the tree-wide 175 rather than the slice's
137, and the obj-name count, which was 293 rather than 282):

```sh
D=OSRS-Content/osrs239-content/server/scripts/drop_tables/scripts
ls $D | wc -l; cat $D/*.rs2 | wc -l              # 69 files, 4,030 lines
grep -ho '^\[[a-z_0-9]*' $D/*.rs2 | sort | uniq -c  # 136 ai_queue3, 22 label, 6 proc, 1 opnpc1
grep -c '^\[ai_queue3,_' $D/*.rs2 | grep -v ':0'    # the six category subjects
python3 tools/port_droptables_check.py --report      # 175 tree-wide; filter col 6 on drop_tables
python3 tools/port_name_diff.py --report             # the verdict column, joined on the names above
./src/build_dtb/mock230 --selftest                   # 462 scripts, gap report silent
./src/build_dtb/mock230_pack --check-only            # 0 errors, 13 warnings
```

**The permanent check is `tools/port_droptables_check.py`, in `test-port`, and
every one of its six bars is mutation-proved.** It exists because nothing else
in the tree holds any of them:

| bar | mutation | result |
|---|---|---|
| no duplicate `[trigger,subject]` tree-wide | a second `[ai_queue3,goblin]` in another file | names both sites, exit 1 |
| a category subject must be in `pack/category.pack` | `_chicken` -> `_giantrat` | exit 1 |
| …and `minted` in `port/categories.map` | `chicken minted 444` -> `broader` | exit 1 |
| no exact binding shadowing a category binding | add `[ai_queue3,chicken]` beside `_chicken` | names both, exit 1 |
| every bound `[ai_queue3]` states or waives its death drop | delete the line from `cow.rs2` | exit 1 |
| every subject resolves | `goblin_guard` -> `goblin_guardx` | exit 1 |

The duplicate bar is §13 bar 4 finally enforced. §16.11 measured that the
compiler does **not** collapse two identical triggers and that
`provider->duplicate_keys` is written and never read; this reads the tree
instead, which needs no engine change and catches it before the pack is built.

**The bug the check caught was in the check.** `strip_comment()` split a
multi-line trigger body on the first `//` anywhere in it, so the death-drop bar
read every one of the 136 bindings as failing. It was found by the mutation run
— the "before" state was already red, which is the only reason anyone looked.
A bar that fails everything and a bar that passes everything are the same bug
wearing different signs, and only mutating a *passing* tree distinguishes them.
The comment explaining it is in the file.

**Three findings for whoever is next.**

1. **`MOCK230_FALLBACK_AI_QUEUE3`'s blocker string is now doubly stale.**
   `src/net/mock/mock230_scripts.c:1273-1275` still says *"drop tables need npc
   categories (triage §7.6b, §9 step 3b)"*; categories landed in §16 and the
   drop tables landed here. The row prints its count at every boot and its
   reason under `MOCK230_VERBOSE`, so this is a line somebody reads exactly when
   they are trying to find out what is blocked. The fallback itself is still
   real and still wanted — an npc with no bound table drops its `death_drop` —
   so the row stays; only its reason is false. It was left unedited because this
   stage wrote no C, and it is §16.1's own failure mode sitting in the banner
   whose whole job is to be true. `docs/osrs230_mockserver.md`'s copy of that
   block is fixed.
2. **118 of the slice's 136 `[ai_queue3]` bindings can never fire in this
   world**, and only 8 of the 68 files carrying one can. `lumbridge.spawn` is
   the only spawn file in the tree; it stands 34 distinct npc names up. Sixteen
   of them are subjects here (`goblin`, `goblin_armed`, `goblin_armed_melee_4`,
   `goblin_unarmed_melee_in_3`, `rat`, `dragonslayer_giantrat_1_key`,
   `dragonslayer_giantrat_2`, `imp`, `man`, `man3`, `man4`, `varrock_man1`,
   `falador_man1`, `woman`, `woman2`, `guard1`), and two more arrive through a
   category (`chicken` → 444, `cow` → 365). That is 18 live bindings in
   `chicken.rs2`, `cow.rs2`, `giant_rat.rs2`, `goblin.rs2`, `guard.rs2`,
   `imp.rs2`, `man.rs2` and `rat.rs2`. **The other 60 files compile, load,
   resolve and wait**, and that is invisible to every bar: `mock230_pack`'s
   `validate_categories()` checks membership in the *cache*, never in the
   *world*, so "this table is unreachable" is not a thing anything can currently
   say. It is the argument for porting a second **area** before porting more
   tables — the slice's observable surface is 13 % of what it states, and the
   limit is the world, not the content.
3. **`make -C src test-db` is red, and it has nothing to do with any of this.**
   Reproduced in a clean objdir: `src/game/test/db_cache_test.c` hardcodes
   `CACHE_DIR` as an absolute path into the *main* worktree
   (`…/3draster/cache.osrs230`) and asserts `RSCache_ProfileIsIdentified`, so it
   fails from any other checkout. Nothing in it reads the content tree.

---

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

**This section used to point at `…/scratchpad/lcport/`. Those files are gone,
and their loss is the reason this rewrite exists.** A scratch directory is
per-session and per-agent; the next run gets a new one, so a doc that names a
scratch path has documented nothing. The measurements in §14–§19 were re-derived
from scratch for exactly that reason, which cost a day and is not a cost anyone
should pay twice. **The rule now: a number that a committed tool cannot
re-derive does not belong in this document without the sentence that says so.**

### 12.1 The committed path — anything a bar depends on

All six of these live in `tools/` and every one runs inside `make -C src
test-port`, which is a dependency of `make -C src test-content`. They read both
trees on every invocation, so they re-measure rather than replay:

| tool | `--report` / `--summary` produces | `--check` fails on |
|---|---|---|
| `tools/port_name_diff.py` | §4.1's display-name diff, 4,279 rows over npc/obj/loc/seq/spotanim; `--collisions` is §4.1's "where a copied id would land", 14 namespaces | an unsigned row, a changed verdict, a deleted row |
| `tools/port_constant_diff.py` | §15's 1,735-row constant classification | a reference constant with no row; a `present`/`landed` value that drifted; a deferred constant copied into this tree |
| `tools/port_category_crawl.py` | §16's 53 npc categories, `--groups` for the member evidence | a minted name missing from `pack/category.pack`; a row whose group changed |
| `tools/port_config_diff.py` | §17's 1,650 param/struct/enum/dbtable/dbrow blocks, `--blockers` for why each is held | a `defer-*` block authored here; `[music]` declared; a `landed` block deleted |
| `tools/port_vars_diff.py` | §18's 357 `%name` rows with the carrier/false-friend split | an unclassified name; a row whose resolution fact changed |
| `tools/port_names_diff.py` | §19's 524 name maps, `--summary` for the disposition table | a reference name with no row; a stale row; evidence that no longer re-derives |

Plus `tools/ss_unresolved.py --check` (bars 1 and 5 over this tree's own configs
and scripts) and `tools/ss_allocate.py --check` (what the next id would be, per
namespace, without writing). All of them **degrade to "skipped" and exit 0 with
no LostCity checkout**, because a bar that cannot run has not failed.

The engine-side numbers:

```
# the §12 baseline, both loaders
make -C src PLATFORM_OBJ_BASE=<lane> mock230-scripts   # sscompile's symbol line
./src/<objdir>/mock230_pack --check-only               # the content-load line
make -C src PLATFORM_OBJ_BASE=<lane> mock230-servpack  # needed before the band check
```

`src/net/mock/mock230_opcode_coverage.gen.h` is generated, so §5's numbers move
when the engine does.

### 12.2 The scratch path — for §4, §5, §6, which no committed tool covers

These were written fresh for this run and are **expected to be gone**; what
survives is the method, stated so the next run can rewrite them in an hour
rather than a day.

| what | method |
|---|---|
| §4's resolution table | parse `id=name` on both sides — this tree's `configs/all.<t>.compack` + `pack/*.pack` + `interfaces/*.compack`, the reference's `content/pack/*.pack`; walk the reference's authored tree with `_unpack/` and `_test/` pruned; a name is *referenced* when it appears as a token in a `.rs2` or config; `ref_rs2` restricts to `.rs2`. Corpus this run: 1,265 `.rs2`, 109 `.constant`, 159 `.if`, 21 config extensions |
| §4.1's diff | now committed — `port_name_diff.py`. The scratch version additionally reported the **strict** (`a.lower()==b.lower()`) and **normalised** (strip `<col>` tags and punctuation) counts side by side, which is how "only 9 of 652 differences are pure formatting" was measured |
| §4's swap sweep | for every namespace with a name table on both sides, look for `lc[A]==mod[B] && lc[B]==mod[A]`; then for cycles >2 (a permutation over one id set); then for display-name transpositions; then for duplicate names inside each side's own tables. Result this run: one 2-cycle, nothing else |
| §5's opcode table | the engine's generated coverage header against a token scan of the reference's `.rs2` |
| §6's compile census | compile, record the **first** error per file, quarantine, repeat. Its limit: past ~200 files the quarantine cascade dominates, so filter out `no proc/label named` before reading it. **The static form is better and is what §16.3 used** — extract every `[<trigger>,<subject>]` header and classify the subject exactly as `ssc_compile.c:parse_header` does |
| §10.1's equipment-requirement comparison | **re-run after any `kronos_item_import.py` run** — `equipment_lostcity.obj` is disjoint from `equipment.obj` by construction and nothing else checks that |

### 12.3 The commits every number above was taken at

Reference drift is not hypothetical here — the reference checkout was written to
during three of the six steps in §10.1's symbols entry, adding records mid-run
(§14.5 finding 1, §15.9, §16.10). Record the commit or the number is not
reproducible:

```
reference   ~/Documents/git_repos/LostCity_Server   d386509  branch 254_zuk
              content submodule                     593008c87  (2 modified, 1 untracked)
this tree   OSRS-Content/osrs239-content            531d8f687a
```

`drop tables/` (§10.1) and §16.11 were measured later the same day, at the same
reference commit and one destination commit on:

```
reference   ~/Documents/git_repos/LostCity_Server   d386509  content 593008c87
this tree   OSRS-Content/osrs239-content            82c8b8773e
```

### 12.4 Baseline on the destination tree, 2026-08-01

```
mock230_pack: 0 error(s), 13 warning(s)
content loaded (213623 symbols, 306 constants, 39 npc defs, 776 loc defs,
                39 varp defs, 1496 equip reqs (675 from the cache),
                63 npc spawns, 12 obj spawns)
sscompile: 212922 from packs, 26478 components, 306 constants, 52 db columns,
           2872 carrier varp(s), 0 whole-write exemption(s)
server band: 2973 archive(s) verified against the text parse
             (needs `make -C src mock230-servpack` first — `server/pack` is a
              build product and the check reports "skipped" without it)
```

**The block this replaced was stale in every field** and had been for some time
— it read `0 errors / 1 warning`, `213430 symbols, 136 constants, 38 npc defs,
16 varp defs, 1419 equip reqs, 62 npc spawns`, and named "29 prayers", a field
the line no longer prints. Six of its seven numbers had moved before this run
started (the first re-measurement, before any of §14–§19 landed, read `213530
symbols, 284 constants, 39 npc, 39 varp, 1496 equip reqs, 63+12 spawns, 0/13`).
The 12 extra warnings are all `combat block but no Attack op` and are not a
regression. A baseline nobody re-runs is a baseline that is wrong.

---

## 13. The bars, restated as they apply here

From the brief, with what each one costs given the measurements:

1. **Every unresolved name is an error at pack time, never a default.** §9 step 2.
   206 names will fail on the first run (44 npc, 88 loc, 44 seq, 30 spotanim)
   plus 147 varps and 1,380 interface references. That is the *point* — the list
   is the work queue.
   **Landed 2026-08-01 (§14), and the numbers are now measured rather than
   projected: 524 names, not 206** (§19.2), **126 varps, not 147** (§4), and the
   1,380 interface references stand. **The list is written down** — six `port/*`
   artifacts, consolidated as §20. Two "never a default" holes were found and
   shut, both of which had been silently answering with -1 or a skipped field.
2. **`make -C src test-content` passes; `mock230_pack` reports 0 errors.** It does
   today; nothing has been changed. **Still true after §14–§19**, with `test-port`
   added to `test-content` and seven new gate rules under it.
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
   **Enforced 2026-08-01** by `tools/port_droptables_check.py` (§10.1), tree-wide
   and on every npc-family trigger, not just this slice's. It had to be a
   content-tree check because neither layer below it says no: `sscompile`
   compiles the two to **two** scripts, and `SSVM_ProviderLoad` counts them into
   `provider->duplicate_keys` which nothing reads (§16.11 item 3). This bar was
   written down here and unenforced for the whole of §14–§19.

I would add two:

5. **No content file carries a bare id.** Spawns were the last exception and are
   no longer one (§10.2). The rule is what makes bars 1 and 3 mean anything: an
   id has nothing to resolve, so it cannot fail to resolve, so a gate that only
   checks names cannot see it. `sos_pest_giantspider1` is what that costs.

6. **A display-name diff is reviewed for every resolved record before it lands.**
   §4.1. Name resolution proves a spelling exists; only this proves it means the
   same thing, and `goblin_armed`/`goblin_cook` are the precedent for what it
   costs when nobody looks.
   **Landed 2026-08-01 as `port/name_diff.signed` + `--check` (§14.3), and it
   earned its keep six times over** — `rock_sample1`, `_giantrat` reading as the
   rat category, `%prayer9` reading as a varp, `[music]` reading as the client's
   own 15-column table, `macro_bigfish` reading as a troll, and
   `antidragonbreathshield` reading as a Dragonfire shield. Every one of those
   resolved cleanly. **The bar generalises past display names**: §17's collision
   is a dbtable *schema*, §18's is a varbit *bit range*, §16's is a category
   *group*. What is being signed off is that the thing means the same, and the
   display name is only where that is cheapest to see.

   **What it cannot see, measured on the first slice that leaned on it
   (§10.1).** `port_name_diff` emits a row only for a name LostCity's *authored*
   tree states; a name it holds only in `pack/` — reserved for content living in
   the cache it was built from, or stated only under `scripts/_unpack/` — is
   skipped, because there is nothing to compare. On `drop tables/` that is **21
   of 282 obj names (7.4 %) and 75 of 130 npc subjects (58 %)** with no row at
   all. They resolve, they compile, and nothing has asserted they mean the same
   thing. **A signed corpus with no row is not a pass**, and the report does not
   currently distinguish the two. That is the next thing this bar needs.
   (It also caught its first live one under a port on the same slice:
   `death_man_indoors1` is 'Unferth' in the reference and a generic 'Man' here
   — §10.1.)

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

> **AI_QUEUE3 has since been adopted** and the list above is six, not seven —
> `mock230_world_npc_died` passes `mock230_npc_category(npc->type)`. See §16.11
> for what still holds it (nothing does yet) and what will.

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
  **Update (§16.11): the second trap is gone** — `pack_path()` honours
  `NON_CONFIG_NAMESPACES` now and would write `pack/category.pack`. The first
  stands, and it is the *only* remaining category blocker. The count is **21**
  orphans, not 20: `battle_mage` joined them. Every one was re-checked for an
  authorable id and none has one.
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

### 16.11 The collisions, settled — and the two tools that disagreed

Written 2026-08-01, as the prerequisite stage of the `drop tables/` slice (§10).
§16.4's four collisions and 20 orphans were the thing standing between the slice
and binding. What follows is what was settled, what was not, and the bug that
made the question look different from what it was.

**`--report` and `--write-map` disagreed, and the louder one was wrong.**
`report()` re-derived every row from scratch and never read `port/categories.map`,
so it printed `black_demon minted 275` and `giantrat minted 262` — the two rows
§16.4 exists to hold back. `write_map()` preserved the demotions; `--report`
undid them on screen. Anyone opening the slice was handed the crawl's *unreviewed*
answer as though it were the reviewed one. `--report` now prints the map's
disposition, adds an eighth `derived` column with its own, and names every
divergence on stderr; `--groups` does the same, because printing `minted` beside
the histogram that disproves it is the worst possible caption. The map, and only
the map, is the authority.

**A collision is never resolved by picking a winner.** That is a result, not a
policy: if two reference names crawl to one osrs239 id, the id holds both
concepts, so it is *wider than either* — which is `broader`, and it is the same
call §16.4 already made for `black_demon` and `giantrat`. Measured:

| id | holds | the names claiming it |
|---|---|---|
| **354** | 37 records — Gnome x5, Gnome guard x3, Gnome child x3, Mounted terrorbird gnome x2, **Gnome troop x2**, Gnome woman x2, **Battle mage x1**, Meegle, Sarble, Burkor. It is "gnome". | `gnome_troop`, `battle_mage` |
| **309** | 67 records — Mountain troll x8, (unnamed) x7, **Troll spectator x7**, My Arm x3, **Troll general x3**, Cook x3, Ug x2, Arrg x2. It is "troll". | `troll_general`, `troll_spectator`, and one half of `mountain_troll`'s split |

`gnome_troop`, `troll_general` and `troll_spectator` are **`broader`**. All their
members resolve, uniquely, to an id that is not the concept.

**`battle_mage` is worse than a collision and is now `orphan`.** Its three
reference members are `zamorak_mage`, `saradomin_mage`, `guthix_mage`
(`area_mage_arena/configs/mage_arena.npc`); all three exist here, all three
display "Battle mage", and exactly one carries a category. That one is
`guthix_mage`, and it carries 354 because **it is a gnome** —
`readyanim=gnome_ready`, `walkanim=gnome_walk`, against its two siblings'
`human_ready`/`human_walk_f`, which carry nothing. So the single resolution is a
false friend: the record is in the group for a reason unrelated to the name.
1/3 resolving read as "nearly there"; it is "not at all". Nothing in this cache
states `battle_mage`.

Settling 354 buys the slice nothing — neither name appears in `drop tables/`.
`troll_general` does, and stays unresolvable either way.

**The demotions could not survive a regenerate, and that was the sharper bug.**
`write_map()` preserved a hand `broader` **only over a derived `minted`**. That
was the whole set when it was written and is not now: the four collision rows
demote to `broader`/`orphan` *over a derived `collision`*, and one
`--write-map` would have silently put all four back — re-arming the exact "one
name wins and nothing says so" failure the demotion was written to disarm, and
deleting four paragraphs of evidence with it. Preservation is now `HELD_BACK`
(`broader`, `orphan`) over `OVERRIDABLE` (`minted`, `collision`), verbatim
including the id.

**Orphans carry their near-misses now.** An orphan is a name whose own members
carry nothing, and the next reader's instinct is to find "the obvious id" by
display name — which usually succeeds, at a different concept wearing the same
word. The crawl now derives that search and records the answer as SUSPECT, with
the `<matching>/<group>` ratio a `broader` call is made from:

```
witches_experiment  SUSPECT 725(4/67)    725 is the NIGHTMARE ZONE BOSS category:
                                         67 nzone_* records, 4 of which are the
                                         shapeshifter's four forms
pirate              SUSPECT 2363(8/9),2358(5/5),1181(2/2)
                                         Trouble Brewing's, the pickpocketable
                                         set, the Warrens' — three sets, none the
                                         Port Sarim four
barbarian           SUSPECT 369(15/16),493(7/7)
                                         both are Barbarian Assault / fishing
                                         rosters (fai_*/akd_*). The Barbarian
                                         Village barbarian is in neither
guardian_of_armadyl SUSPECT 2046(4/5),2042(2/2),1715(1/5)   all three are WGS
monk_of_zamorak     SUSPECT 1321(3/3),2491(3/3)
```

The rest — `bandit_camp_leader`, `witch`, `sailor`, `shipyardworker`,
`tower_advisor`, `diseased_sheep`, `fisherman_platform`, `legends_guard`, the 7
`macro_event_*` — say *"no categorised record anywhere in this cache shares
these display names"*, which is a stronger statement than "we found nothing".

`bandit_camp_leader` is the clean worked example the brief asked about: its
three members are here, as npc 301/302/303, "Black Heather" / "Donny the lad" /
"Speedy Keith", and **all three carry no `category=`**. Nothing is missing and
the crawl is right; the cache is simply silent. That is a statement about the
cache, and it is left unported.

**No orphan was authorable, and that was checked rather than assumed.** The
reference derives a category from each record's own `category=`, so the fix for
a missing one is normally to author it here — but authoring needs an id, and for
all 20 orphans this cache states no id for the concept (that is what the SUSPECT
scan above is: the search for one, run and failed). `content.ini` still gives
`category` `ids = cache` with no `server_base`, so one cannot be allocated
either. **This is the single remaining blocker and it is register work, not a
content judgement** — §9 step 3c-0. The second trap §16.7 named alongside it is
already gone: `ss_allocate.py`'s `pack_path()` now honours
`NON_CONFIG_NAMESPACES` and would write `pack/category.pack`.

**The eight splits were left alone, deliberately.** Minting a per-group name
(`citizen` is {266 = Man x17 + Drunken man/Norman/Cuffs/Narf/Rusty/Jeff/Hengel,
492 = Woman x12 + Anja} — osrs239 split the reference's one category by sex)
would not copy an id, but it *invents a name the reference does not have* and
forces the ported script to bind two triggers instead of one. That is a content
decision belonging to the port, not to its prerequisite.

**Two stale negative claims about the cache, both fixed in place.** §16.1's rule
had propagated into content:

- `drop_tables/scripts/livestock.rs2` — "there is no such category in this
  cache" for `_giantrat`. There is: `rat`, `dragonslayer_giantrat_1_key` and
  `dragonslayer_giantrat_2` **all carry 262**, which is why the per-npc binding
  is right and why `giantrat` is `broader`. Same conclusion, opposite reason,
  and the wrong reason is the one that gets "fixed" later.
- `areas/lumbridge/scripts/citizens.rs2` — "An OldSchool cache has no such
  category". It has it twice: the five men bound there carry 266, the two women
  carry 492.

**The permanent checks, and the mutation that proves each.** Both live in
`port_category_crawl --check`, inside `test-port`, inside `test-content`.

- **A `collision` left standing is a failure.** It is a derived disposition and
  never a resting one; resting is exactly "two names claim one id and one wins
  silently". Mutation: `battle_mage orphan 354` → `collision` → 1 problem,
  exit 1. Reverted.
- **A preserved demotion must still refuse the id it names.** `write_map` keeps
  a hand row's id verbatim instead of re-deriving it, so it is the one number
  here nothing else would notice going stale. Mutation: `black_demon broader
  275` → `276` → "*its members now carry [275] — a preserved demotion states the
  id it refuses*", exit 1. Reverted.
- **The preservation fix itself was mutation-tested**, because a checker that
  cannot fail proves nothing: reverting `write_map` to the pre-fix rule
  (`broader` over `minted` only) and running `--write-map` puts all four
  collision rows back and deletes their evidence — and `--check` then reports 4
  problems. Both halves earn their lines.

Green after: `port_category_crawl --check` → *53 row(s), 18 minted, 6 demoted by
hand over the crawl's own answer, all agree with pack/category.pack*;
`--write-map` idempotent; `test-port` green; `test-content` green with
`mock230_pack: 0 error(s), 13 warning(s)`; `mock230 --selftest` all checks
passed at 319 scripts. **No C was written in this stage.** The map's state:

```
minted 18   broader 5   orphan 21   split 8   placeholder 1   collision 0
```

(`orphan` is 21, not 20, because `battle_mage` moved into it; `broader` is 5, not
2, and `collision` is 0.)

**What the port stage inherits.** (It ran the same day — see §10.1. All three
items below held: `_chicken` and `_cow` are bound as categories and their exact
bindings were removed, and `tools/port_droptables_check.py` now enforces all
three plus the death-drop rule.) Of the slice's 16 category subjects, six
resolve — `bear` 438, `chicken` 444, `cow` 365, `ice_warrior` 466, `unicorn`
2263, `werewolf` 454 — and ten do not: `black_demon`, `giantrat`,
`troll_general` (`broader`), `citizen`, `citizen_burthorpe`, `guard`,
`mountain_troll` (`split`), `bandit_camp_leader`, `barbarian`, `pirate`
(`orphan`). Three things it should not rediscover:

1. **The AI_QUEUE3 rung has no permanent check yet.** `mock230_world_npc_died`
   passes `mock230_npc_category()` now instead of `-1`, proved live and the
   probe removed — but nothing in the tree binds a category-keyed npc trigger,
   so nothing holds it. Converting `livestock.rs2`'s `[ai_queue3,chicken]` to
   the reference's `[ai_queue3,_chicken]` is both the first port move and that
   check.
2. **An exact binding shadows a category binding** (`SSVM_ProviderGetByTrigger`:
   type → category → global). `chicken`, `cow`, `rat` and `duck` are all
   exact-bound today, so a half-converted file silently keeps the old table for
   the named npc and gives the category table only to its siblings.
3. **A duplicate trigger is silent, but not where it was reported to be.**
   Measured, because the inherited claim was that `sscompile` compiles the two
   to one: it does not. A second file declaring `[ai_queue3,chicken]` compiles
   to **320 scripts, not 319** — both survive. The collision is at *load*:
   `SSVM_ProviderLoad` sorts `by_key`, counts equal adjacent keys into
   `provider->duplicate_keys`, and **nothing in mock230 ever reads that field**
   (only `ss_corpus_test` and `ss_provider_test` do). `find_by_key` is a
   `bsearch` over the sorted array, so which of the two duplicates answers is
   whichever the search lands on. §13 bar 4 calls a second binding a conflict;
   neither the compiler nor the server enforces it, and the counter that could
   is already there unread.

---

## 17. §9 step 3c, as landed — param / struct / enum / dbtable / dbrow

The five namespaces whose ids are *ours*. §4 group C is right that a 0 %
name-resolution rate against the cache is the correct answer here — nothing in
cache.osrs239 names a param, a struct or an enum the server defines — and that is
exactly why the name-resolution gate (§14) has never been able to see this
family. This step is the allocation mechanism, the gate that replaces §14 inside
it, and 75 records.

### 17.1 The mechanism came first, and it was not a formality

**Every `server_base` in `src/content/content_register.c` was advisory.**
`tools/ss_allocate.py`'s `declared_base()` read `content.ini` and nothing else,
and `content.ini` declares **no `base =` key at all** — measured, zero, in the
whole file. So the floor was always 0, `max(floor, mark + 1)` was always
`mark + 1`, and the C table described an id space nothing consulted. Its own
docstring said the numbers lived in the register and were "not duplicated into
this file", which was true and had the opposite effect from the one intended.

`declared_base()` reads the register now, `content.ini` still overlays it, and
the first run after that change printed the bug:

```
varp      declared=39 allocated=5724 base_was=8000 floor=8000
```

The next server varp would have been **8000**, and `MOCK230_VARP_COUNT` is
`MOCK230_VARP_CACHE_MAX + 512` = **6217** — past the end of
`Mock230Player.varps`, where `mock230_world_set_varp` bounds-checks and returns.
The write would have looked like it worked and transmitted nothing:
`CONTENT_ARCHITECTURE.md` §8.3's named failure mode, re-armed, and invisible for
as long as the floor was inert.

So the register's `varp` row is **5705**, not 8000 — the third of the
"already allocated" exceptions the `server_base` docstring describes beside
`param`'s 2634. Nineteen server varps already sit at 5705..5723 and §8.5 records
that as the correct result. **`struct` stays at 8000**: the stated policy is a
round number above the cache's maximum, `struct` has nothing allocated yet, its
cache maximum is 6499, and nothing bounds a struct id — there was no evidence
against the policy, so the policy stands. The measured floors now:

| namespace | cache max | floor | first server id in use |
|---|---:|---:|---:|
| `param` | 2633 | 2634 | 2634 |
| `enum` | 5994 | 5995 | 5995 |
| `dbtable` | 258 | 259 | 259 |
| `dbrow` | 16939 | none | 16940 |
| `struct` | 6499 | 8000 | — none yet |
| `varp` | 5704 | **5705** (was 8000) | 5705 |

**Three more things that had to change for "allocated, never hand-picked" to be
a checkable claim rather than a convention.**

1. **`validate_id_bases` read the wrong register.** It called
   `ContentRegister_Defaults` and never opened the tree's `content.ini`, so the
   four namespaces the file *promotes* to `ids = server` — `varp`, `param`,
   `dbtable`, `dbrow`, each with a paragraph in that file saying why — were
   checked as though they were the cache's. A validator reading a different
   register than the runtime is checking a tree that does not exist. It calls
   `ContentRegister_Load` now.
2. **The reverse direction is an error, not a hidden `report_info`.** An id
   above the cache's maximum but below the declared base means the register is
   describing an id space that is not in use, and the *next* allocation jumps the
   gap. For `ids = cache` it stays information — `obj` has twelve ids between the
   cache's 33834 and its base of 40000 and all twelve are `obj_<id>` layer-0
   filler, not allocations.
3. **`ss_allocate.py`'s `pack_path()` was unconditionally
   `configs/all.<ns>.compack`**, while `mock230_content.c`'s `pack_kind_is_config()`
   puts `category`, `stat`, `component` and `3_interfaces` in `pack/<ns>.pack`.
   Latent, because none of the four is `ids = server` today — and armed, because
   §16.7 wants `category` promoted so the 20 orphan npc categories can get an id.
   The moment that landed, the allocator would have written
   `configs/all.category.compack`, which nothing reads, while `pack/category.pack`
   went untouched. `CONTENT_ARCHITECTURE.md` §8.2(c), a fourth time. Fixed here so
   3b's blocker can be cleared without shipping with it.

### 17.2 The gate: two holes, both in this family only

**`mock230_pack` never loaded the `.dbtable`/`.dbrow` half of the tree.**
`mock230_db_load` had exactly one caller, `mock230_boot.c`. So for the one
namespace family whose ids are allocated rather than the cache's, "the table has
no such column", "this row names an unknown table" and "this value resolves to
nothing" were **boot-time discoveries in a tree `mock230_pack --check-only`
called clean**. It loads it now; the errors already went through
`mock230_content_report_error`, so they reach the exit status with no other
plumbing. Proved by mutation: `table=prayer_tabel` → 7 errors, exit 1; was 0.

**An unrecognised column type was read as a literal.** `db_kind_for_type`
returned `MOCK230_PACK_COUNT` — the same answer it gives `int` — for any word it
did not know, and `row_value` then fell through to `atoi()`. A column declared
`synth` turned every sound *name* in it into **0**, silently. That is bar 1
exactly, one namespace over from where §14 shut it. `db_type_is_literal` now
states the four words that carry no symbol (`int`, `string`, `boolean`, `coord`)
and anything else must resolve through a pack; a type nothing resolves is a load
error naming the type. Proved by mutation: `column=sound,synth` → error, exit 1;
was silent.

### 17.3 What landed: 75 records, and the rule that chose them

**A file lands whole or not at all**, and only when *every* block in it is clean,
no block name is already spelled here, its subject directory already exists in
this tree, and — for a `.dbrow` — every table it names lands in the same pass or
is already here. Partial files were not an option worth taking: the reference's
prose is colocated with its blocks, and splitting a file to rescue three rows
loses the paragraph explaining them.

```
param    27   2651..2677   bas / consume / death / weapon_poison / bones / 6 quests
enum     20   6004..6023   cocktail_guide, cook_book, mage_arena, priestperil, …
dbtable   7    262..268    drop_table, sheep_table, legends_gem_data, 4 thieving
dbrow    21  16984..17004  zone tables (coord_pair_table) + the sheepherder sheep
```

Both symbol loaders rose by **exactly 75, and by the same number** — sscompile
212,847 → 212,922, `mock230_pack` 213,548 → 213,623 — which is this step's
equivalent of §15's constant-count check: a divergence would mean one loader
skipped a file. `git diff` on the four `.compack` files is **75 added lines and
no line rewritten**, all below the allocator's marker. `mock230_pack` 0 errors,
server band still 2,973 archives verified, `test-content` green.

`drop_table` is the one worth naming: it is the schema `drop tables/` needs, the
slice PORTING_GUIDE §6 phase 4 puts first and §16 unblocked half of.

**Verified at runtime, not just at pack time.** A selftest stanza beside the
category one asserts the whole allocation chain on `sheep_table`: the id is
server-allocated (≥259), the table loaded with its four rows, and the row's
`npc` and `namedobj` columns hold **3986** and **280** — the ids this tree's packs
give `herder_plaguesheep_1` and `sheepbonesa`, where the reference's own numbers
are 1379 and 1929. Reverting `db_kind_for_type` to the old silent fallback makes
both read 0 and both checks fail, which is what makes the stanza worth its lines.

### 17.4 The other 1,544, classified — `port/configs.map`

1,619 reference blocks, plus 31 this tree declares and the reference does not.
`tools/port_config_diff.py --check` holds the tree to it inside `test-port`.

| disposition | rows | what it is |
|---|---:|---|
| `defer-slice` | **985** | clean; the subject has not been ported |
| `blocked-type` | **287** | needs a type nothing here can resolve |
| `blocked-loader` | **149** | every `struct` |
| `blocked-name` | **63** | a symbol inside it has no spelling here |
| `present` | 56 | already here, authored |
| `landed` | 75 | this pass |
| `duplicate-concept` | 2 | this tree carries it under another name |
| `cache-record` | 1 | the cache states it; resolve, never re-declare |
| `collision` | **1** | the name here means something else |
| `tree-only` | 31 | declared here, not in the reference |

Primary blockers, by row:

```
195 dbrow   component + midi     the music slice
 68 dbrow   synth                consumption, magic, fletching
 34 dbrow   loc                  thieving's locked doors and chests
 32 dbrow   constant             §15 deferred the constant it indexes
 19 dbrow   component+interface  levelup
  7 enum    npc / constant       the macro-event level tables
  6 param   synth / loc          five sound defaults and one loc default
```

**The blocker nobody had counted: this tree names no sounds and no music.**
`pack/4_soundeffects.pack` is 12,010 lines of `synth_<id>` and
`pack/6_musictracks.pack` is 881 lines of `song_<id>` — not one real name in
either. **282 reference dbrows and 5 params** name one — every
`blocked-type` row in the table above. That is a 3e-shaped naming
job in two asset namespaces nobody has scoped, and until this step it was
invisible because the reader answered every one of those names with 0.

### 17.5 `music` — the one collision, and why a name check cannot see it

LostCity's `music.dbtable` has **4** columns beginning `name`; cache.osrs239's
`music` is **dbtable 44** with **15** beginning `sortname`, and it is what the
client's music player reads. **195 LostCity dbrows say `table=music`.** Resolved
by name — which is the rule everywhere else and the right rule — all 195 attach
to the client's table, and `data=name,Adventure` names a column that table does
not have.

Every check that exists passes on it: the name resolves, the id is not copied,
the display-name diff is a different namespace, and `ss_allocate` would decline
to allocate because the name already has an id. It is `obj rock_sample1` (§14.3)
one namespace over. `port/configs.map` carries it as `collision`, and the
enforcement is the opposite of the usual one: **no config file under
`server/scripts` may declare a block by that name.** Proved by mutation —
authoring `[music]` fails the check, exit 1. `musicregion` (302 rows) does not
collide, so the fix when the music slice is wanted is a rename of one table, not
of the slice.

Two `duplicate-concept` rows are the softer version of the same thing and both
were already ported here under different names: LostCity `prayers` →
`prayer_table` (which states the exclusion the other way round, deliberately),
and `stat_names` → `stat_name_enum`. Porting either gives two tables saying one
thing.

### 17.6 What this step did **not** do, and why

- **No `struct` landed, and none can.** There is no `.struct` loader anywhere in
  `src/` and `SS_OP_STRUCT_PARAM` has meta but no server `case` — grepped, zero
  hits each. A struct here would be 733 `param=` references nothing reads. Both
  are engine work and a prerequisite, not a follow-up.
- **No param *values* are readable, and that is unchanged.** `load_param_types`
  opens `configs/all.param` only and `walk_configs` has no `.param` pass, so the
  27 params landed here — and the 17 this tree already had — keep `type` 0 and
  `default` 0 at runtime. Nothing references them yet, which is why landing the
  declarations is safe today and why the overlay walk has to land before anything
  does: 27 of the reference's 214 params are `type=string`, and
  `push_typed_param` answers an absent row on an undeclared param by pushing an
  **int**, which is a permanent stack desync in the VM.
- **`--server-only` band unchanged** at 2,973 archives verified. The new params
  are declarations; no record carries one.
- **Client verification is vacuous** and is stated rather than skipped: no client
  code, packet or interface was touched. The closest real check is
  `test-mock230`, which boots the server, loads 319 scripts, reads every table
  and row in the tree and builds the scene — green, with the new stanza in it.

### 17.7 Corrections to the numbers in §4 and §9

| claim | measured |
|---|---|
| §4 "214 params, 149 structs, 114 enums, 1,115 dbrows" | **exact**, all four |
| §4 (not stated) | dbtable **27** |
| §4 `param` "resolves 23 / unresolved 180" | 23 reproduces: 13 are real cache params, 10 are prior ports |
| §9-3c ordering | `param → struct/enum` is **not** a cycle: all 18 `type=struct` and 3 `type=enum` params declare `default=null` |
| §12 baseline | `213,623 symbols, 306 constants, 39 npc, 776 loc, 39 varp, 1,496 equip reqs, 63+12 spawns, 0 errors 13 warnings` |

---

## 18. §9 step 3d, as landed — varp / varn / vars reclassification

The step with the worst failure mode in the whole plan, and the reason is one
sentence: **in this namespace, resolving proves nothing.** A `%name` that
resolves compiles, runs, transmits, and can still be writing somebody else's
state — and unlike every other namespace there is no display name to diff, so
§4.1's artifact has nothing to compare.

Two halves landed, and they are different kinds of thing. The **mechanism** makes
the wrong class impossible or loud, and it needed no table. The **data** is one
human decision per name, and most of them are not made.

### 18.1 The mechanism — three layers, each with a mutation that proves it

| where | rule | proved by |
|---|---|---|
| `sscompile` (`ssc_compile.c` `check_carrier_write`) | `%name = value` on a varp that has varbits based on it is a **compile error**, naming the varbits and bit ranges it would destroy | `%prayer0 = 0` → *"is varp 83, which 32 varbit(s) are packed into … (prayer_allactive (0..28), prayer_thickskin (0..0), …)"*, exit 1 |
| `sscompile` (`warn_carrier_read`) | reading one is a **warning** — the packed word is not a value, but a read is recoverable | `return(%prayer0)` → warning, still compiles |
| `sscompile` (`SSC_SymbolsValidate` rule 0) | `wholewrite=allow` on a varp nothing is based on is an **error** | on `cookquest` → *"no varbit is based on it — there is nothing to exempt"*, exit 1 |
| `mock230_content.c` | `wholewrite` takes `allow` and nothing else | `wholewrite=maybe` → 1 content error, exit 1 |
| `mock230_world.c` (`check_carrier_write`) | every whole-varp write at **runtime** is counted, and the selftest asserts the count is zero | removing the varbit-patch exclusion → `FAIL 21 whole-varp write(s) landed on a carrier varp (last: 1105)` |

All five reverted; the content tree was verified clean after each.

**The carrier set is read, never derived.** `configs/all.varbit`'s `basevar=` key
is the only statement anywhere of which varp a varbit lives inside, and both
readers use it: sscompile parses the record file (2,872 carriers), the engine
builds the same reverse index out of config group 14 at boot (`mock230: 2872
varp(s) carry varbits`). Two independent paths, one authority, and the numbers
agree to the record.

**The gate went on green, which is the only time it is free.** Measured before
landing it: this tree's own scripts make **97 whole-varp writes over 32 names,
and 0 of them touch a carrier**; **0 carrier reads**; and 3 of the tree's 39
`.varp` declarations name a carrier (`prayer0`, `bankcert`, `bankinsert`) — all
three correctly, because a varbit's *container* is what has to be transmitted.
Nothing needed an exemption, and none is declared.

**Why the exemption exists at all, given nothing uses it.** A rule content cannot
opt out of is a rule content will route around, and `CONTENT_ARCHITECTURE.md`
§8.2(c) is explicit that the thing with nowhere to put a decision is the bug. So
the hatch is a fourth key on the `.varp` block — beside `transmit`, `scope` and
`protect`, declared in `fields/varp.ini`, read by **both** the compiler and the
engine — rather than a compiler flag. And it is itself checked: an exemption that
excuses nothing is an error, so it cannot be left behind after the write it
excused is gone.

**The runtime layer is not redundant with the compiler.** sscompile covers
content. It cannot cover the other three writers — a `::` cheat, C, a packet
handler — and §8.2(d) is about exactly that: *moving a rule to content leaves the
engine holding the other end of it.* The selftest's assertion spans the whole
run, so it fails on a write from any of them. Its negative control is real: the
one path that legitimately writes a carrier is `mock230_varbit_set`, and deleting
its exclusion turns 21 correct varbit patches into 21 counted violations.

### 18.2 What was measured, against what §7.5 and §9 claim

| claim | measured | verdict |
|---|---|---|
| §7.5 "**28** varps by hand" | **27** | one off; the list below is the whole set |
| §7.5's six worked examples | `%dragonquestvar` 177/21 bits · `%goblinquest` 62/8 · `%prayer0` 83/32 · `%ibanmulti` 162/31 · `%emote_access` 313/24 · `%bankcert` 115/5 | **all six exact** — varp id and varbit count |
| §4 "127 resolve / 147 unresolved" | **143 resolve / 128 unresolved** (of 278 non-varn/vars references) | the tree grew |
| §7.5 "the other 147 are `varn`/`vars` or nothing" | of the 128 unresolved, **varn 0, vars 0** — the reference already used the right namespace where it meant npc or world state | **wrong**, and the correction matters: the real destination is a server-allocated player varp, `transmit=no`, which this tree has already done 18 times (`com_*`, 5705..5722) |
| §9 step 3d "28 by hand" as the size of the step | **357** `%name` references, of which **117 are undecided** (98 unresolved + 19 carrier) | the clobber set is the dangerous part, not the big part |
| §15.4's 168 bit-position constants over 22 reference varps | reached from the other side: **39** referenced names carry `testbit`/`setbit`/`clearbit` | the two lists cross-check on `ibanmulti` and `emote_access` |
| whole-container writes in the reference | **2,108**, of which **187 over 26 names** land on a carrier | — |

Baseline after, unchanged in every field: `0 error(s), 13 warning(s)` ·
`213623 symbols, 306 constants, 39 npc defs, 776 loc defs, 39 varp defs,
1496 equip reqs (675 from the cache), 63 npc spawns, 12 obj spawns` ·
`2973 archive(s) verified` · 319 scripts · `port_name_diff` 4,279 rows, no
re-signing needed this session.

### 18.3 The false friends — six names that resolve, five of them to the wrong thing

This is the finding the step exists for, and it is provable from both sides.

The reference's `%prayerN` is the Nth prayer toggle: `skill_prayer/scripts/
prayers/<name>.rs2` owns exactly one N each, **15 of 15**. osrs239 packs exactly
those fifteen as **bits 0..14 of varp 83**, in the same order — `thickskin`,
`burstofstrength`, `clarityofthought`, `rockskin`, `superhumanstrength`,
`improvedreflexes`, `rapidrestore`, `rapidheal`, `protectitem`, `steelskin`,
`ultimatestrength`, `incrediblereflexes`, `protectfrommagic`,
`protectfrommissiles`, `protectfrommelee`. Independent cross-check:
`king_black_dragon.rs2:163` gates dragonfire on `%prayer12`, and bit 12 is
`prayer_protectfrommagic`.

But osrs239's gameval table kept the legacy `prayerN` labels on **reused ids**:

```
%prayer0  -> varp 83   the prayer block           right container, wrong granularity
%prayer1  -> varp 84   quickprayer_selected       WRONG — a different concept
%prayer9  -> varp 92   the Clan Wars block        WRONG
%prayer10 -> varp 93   Clan Wars                  WRONG
%prayer11 -> varp 94   Clan Wars                  WRONG
%prayer12 -> varp 95   Clan Wars                  WRONG
```

Five names, **35 whole-varp writes**, every one of which resolves, compiles, and
lands on the Clan Wars state. The other nine (`prayer2..8`, `13`, `14`) do not
resolve at all and would be counted among the "unresolved" — so the *same*
mechanical rule produces a compile error for nine of a family and silent
corruption for five, which is why the deliverable here is a signed map and not a
rename. A name check cannot tell `goblinquest` → `gobdip_*` (same concept, needs
a bit range) from `prayer9` → `clanwars_*` (different concept, needs a different
name) apart. **Both "resolve".**

The carrier gate catches all six as writes. It does **not** catch a read, and it
cannot catch the *choice* — that is what `port/vars.map` is.

### 18.4 The data — `port/vars.map`, 357 rows

`tools/port_vars_diff.py` (`--report` / `--write-map` / `--check`). The
mechanical columns are re-measured on every check; `disposition`, `target` and
`evidence` are human and never regenerated.

| disposition | rows | what it means |
|---|---:|---|
| `unresolved` | **98** | no defensible target. Left unported, and listed |
| `clean-varp` | **92** | resolves to a non-carrier varp of the same concept — safe when its slice lands |
| `varn` | 43 | the reference already scopes it to an npc |
| `vars` | 36 | the reference already scopes it to the world |
| `present` | 24 | already declared in this tree, correctly (18 of them server-allocated) |
| `bitfield` | **21** | bit-packed in the reference, unresolved here → becomes N varbits. **Blocked**, §18.6 |
| `varbit` | **19** | decided, with evidence: the reference's varp is a varbit here |
| `carrier` | **19** | resolves to a shared container of the *right* subsystem; which bit range is a per-quest human decision |
| `false-friend` | **5** | resolves, to a different concept. Never auto-map |

The 19 `varbit` rows are the ones the measurement resolved with evidence, and all
19 need **no new content**, which is the honest result: 10 prayers + 2 bank
settings are already varbits in this tree and its scripts already use them, the 6
`troll_*` are LostCity's own varbits resolving at identical ids, and
`autocast_spell` is a varp there and varbit 276 here. The step's product is the
*statement*, not a file.

`--check` fires on: a reference `%name` with no row · a row whose resolution
changed under a decision already made · a `false-friend` this tree's own scripts
have started to spell · a `carrier` row that acquired a target · a target that is
not a varbit here · a decided row with no evidence. Four mutations proved, each
reverted.

### 18.5 The 27 carriers, and the one distinction worth carrying forward

```
prayer0 83/32   ibanmulti 162/31   emote_access 313/24   dragonquestvar 177/21
prayer9 92/18   dueloptions 286/15 itkeepgatelock 113/14 elemental_workshop_bits 299/12
flour 203/12    mcannonmulti 1/12  demonstart 222/10     smithbars 210/10
agilityarena_varbit 309/9          desert 599/9          goblinquest 62/8
dragonresist 277/6                 ernestlever 33/6      bankcert 115/5
prayer10 93/5   duel2accept 284/4  sheepherdervar 61/4   trawler 109/3
bankinsert 304/2 prayer11 94/2     prayer12 95/2         score_gnomeball_game 143/2
prayer1 84/1
```

**Bit-packed in the reference is not the same question as shared here.** Of the
39 names the reference addresses with `testbit`/`setbit`/`clearbit`, **10 resolve
to varps osrs239 packs nothing into** — `barcrawl` (varp 77, 46 bit ops),
`musicmulti_1..7`, `cogquest`, `trail_status`. Those can keep the reference's own
bit layout verbatim, because nothing else lives in the container. The 8 that are
*both* bit-packed there and shared here (`ibanmulti`, `dueloptions`,
`elemental_workshop_bits`, `emote_access`, `ernestlever`, `agilityarena_varbit`,
`mcannonmulti`, `sheepherdervar`) are the hardest rows in the file: every
individual bit is a separate decision against an osrs239 varbit that already
exists.

### 18.6 What did **not** land, and why

- **No `.varp` or `.varbit` file was written.** Following §15.3's test: a
  declaration whose script is not here is a name waiting to be resolved against
  the wrong thing, and 3d ports no scripts. The 19 evidenced rows need no file
  because their destinations are already in the tree.
- **The `bitfield` bucket (21 names) is blocked, not deferred.** Creating a varbit
  means writing config group 14, and three things are missing: there is no
  `fields/varbit.ini`, `content.ini` says `[namespace:varbit] ids = cache` so
  `ss_allocate` will not allocate one, and — the real blocker — **the server reads
  varbits from `cache.osrs239` directly** (`mock230_boot.c:93`, the pristine frozen
  cache), not from cachepack's output, so an authored varbit would be invisible to
  it. `cachepack` itself is ready (`CP_TYPE_VARBIT`, kind 4, not lossy, and the
  walk already picks up any extension under `server/scripts`). This is an rscache
  and boot-path change; read `3rd/rscache/EXCEPTIONS.md` first.
- **The 98 `unresolved` were not reclassified as `server-varp` in bulk.** The
  class argument is sound — 18 already exist here at 5705..5722 with
  `transmit=no`, and 3c raised the floor so the next lands at 5724 — but applying
  it per name needs a check that no osrs239 varbit already covers the concept
  under a different spelling, and that is the exact trap this step is about. An
  unported varp is a gap; a wrongly-classified one is a corruption.
- **`clean-varp` (92) is the bucket with no check.** The name resolves to a
  non-carrier varp, so a whole write is safe *mechanically* — but nothing proves
  the varp still means what it meant, and a varp has no display name for §4.1's
  diff to compare. `phoenixgang`/`blackarmgang` sit in this bucket: **145/146 in
  this tree, 146/145 in the reference**, re-verified this session. Resolving by
  name is correct for them and copying the id is corruption, which is the
  standing rule; what has no artifact is concept drift.
- **`mock230_bank.c:314-319` writes `player->varps[basevar]` directly**, bypassing
  `mock230_world_set_varp` and therefore the runtime backstop. It is a correct
  varbit patch through a private copy of the arithmetic, so it is not a bug today
  — but it is the one writer the counter cannot see, and it is the third copy of
  "patch a bit range into a varp" in the file. Not this step's to move.

### 18.7 Corrections

- §7.5's "28 by hand" is **27**, and its closing paragraph — "here they are
  `varn`/`vars` or nothing" — is wrong: **varn 0, vars 0**. The reference already
  used `varn` where it meant npc state (`npc_lastcombat` exists as a varn *beside*
  the player varp `lastcombat`), so the unresolved bucket is not a namespace
  mistake to fix, it is 98 destinations to choose.
- §7.5's six example rows are **exact**, varp id and varbit count both — the only
  thing worth adding is that `prayer0`'s 32 includes `prayer_allactive`, a 0..28
  *alias over the whole block*, so varp 83 has two overlapping readings and the
  reference's fifteen toggles are only bits 0..14 of it.
- §4's varp row (`ref in .rs2` 268) counts differently from this step's 357. The
  difference is that 357 counts every `%name` token in the authored `.rs2` tree
  including the ones the reference declares in `scripts/_unpack/{225,244,245,254}/
  all.varp` (91 of them) and the 9 it declares nowhere at all. Both are correct
  answers to different questions; the map uses 357 because a name with no
  declaration is still a name a ported script will spell.

---

## 19. §9 step 3e, as landed — npc / loc / seq / spotanim name maps

Written 2026-08-01, after §14–§18. The deliverable is
`OSRS-Content/osrs239-content/port/names.map` (524 rows) and
`tools/port_names_diff.py --check`, which runs inside `make -C src test-port`.
**No content file was written**, for the same reason §18 wrote no `.varp`: this
step ports no scripts, and a map is a statement about what a name becomes, not
a record.

### 19.1 The thing this step is actually guarding

An unresolved name is the **safe** case. Bar 1 makes it a pack-time error, the
compiler refuses it, and the list is the work queue — §4's 206 exist so that
somebody has to look at them. Nothing in this step tries to make them go away.

The unsafe case is the opposite one, and it has no failure mode at all:

```
npc macro_bigfish   LostCity  'Big fish', no ops, readyanim fish_ready, resize 64
                    osrs239   the ONLY record displaying 'Big Fish' is
                              sotn_hazeel_troll_vis — a Shadow-of-the-Storm troll
                              on models 35833/35836, the same models as
                              my2arm_flashback_troll_*, with op1=Talk-to, size 2
```

That row satisfies **every** automatic rule this step has: the display name is
unique, the vislevel agrees, the mapping is injective, the target is not itself
a reference name. It is `obj rock_sample1` (§14.3) one namespace over, and it is
why `--propose` and `--check` are separate verbs — proposing is mechanical,
landing is not. It is carried as `deferred` with the rejection written down.

### 19.2 The corpus, re-measured

Method, stated because every previous count of this used a different one: a name
is *referenced* when it appears as a token anywhere in the reference's authored
tree (`_unpack`/`_test` excluded) **other than** as the `[name]` header of a
`.<ns>` file declaring it — otherwise every authored record reads as a reference
to itself. A name is *unresolved* when this tree's `configs/all.<ns>.compack`
and its own authored overlays have no such spelling.

| namespace | referenced | **unresolved** | §4 claims (`.rs2` only) |
|---|---:|---:|---:|
| `npc` | 984 | **92** | 44 |
| `loc` | 1,408 | **266** | 88 |
| `seq` | 974 | **125** | 44 |
| `spotanim` | 295 | **41** | 30 |
| | | **524** | 206 |

§4's 206 is reproducible **only for `npc`** (44/44) and near for `seq` (45) and
`spotanim` (32) when the walk is restricted to `.rs2` and to names the reference
*authors*. `loc` does not reproduce under any variant I tried: 131 authored-only
`.rs2`-only, 220 whole-pack `.rs2`-only, 266 whole-tree. **Do not quote 206 as
the size of this step.** The gap is not drift — it is that a `.npc` naming a
`seq` is a reference the port has to resolve exactly as much as an `.rs2` naming
an `npc`, and 293 of the 524 are reached only that way.

The plan's whole-tree figure of 452 (npc 93, loc 201, seq 118, spotanim 40) is
close on three namespaces and 65 low on `loc`; the numbers above are what
`tools/port_names_diff.py --summary` prints, so they are re-derivable.

### 19.3 What landed: 180 rows with a target, three kinds of evidence

| disposition | rows | what it means |
|---|---:|---|
| `manifest` | **113** | a `port_lostcity` export. The source id is stated in the manifest or minted into the name, so the target is a **lookup** |
| `renamed` | **28** | a wholesale rename, verified on every member of the set |
| `resolved` | **39** | display name + a structural field, uniquely and injectively |
| `proposed` | 2 | one target, one stated disagreement — a human signs it |
| `collision` | 6 | two reference names, one osrs239 record |
| `family` | 125 | the family is certain, the member is not |
| `scenery` | 130 | a generic loc: 328 records here display 'Gate', 488 'Ladder', 191 'Rocks' |
| `absent` | 22 | no osrs239 record carries that display name at all |
| `deferred` | 59 | nothing defensible, or a proposal rejected |

**The 113 manifest rows are the free half and they cross-check clean.** The
exporter writes the source id into the name it mints (`inferno_loc_30344`,
`td_seq_10640`, `inferno_model_33006` inside a spotanim's `model=`), so 113
targets are read out of this tree rather than chosen — and every one of them
also agrees on a structural field: **seq 65/65 frame counts, spotanim 24/24
model ids, npc 8/8 display + vislevel + model, loc 16/16 footprint, zero
mismatches.** That matters most for the 30 `td_*` rows, which were exported from
**`cache.osrs230`, not this tree's cache**: the assumption that the id still
means the same record held, and every one of those rows says so.

**The 28 `renamed` rows are one fact, not 28 guesses.** The reference's
`midget_*` seqs are this tree's `gnome_*`: **30 of 30** members have a
`gnome_`-prefixed counterpart with an identical frame count, and there is no
counterexample anywhere in the namespace. A frame count on its own proves
nothing (345 seqs here have 13 frames); the rule plus the count is what carries
it, and `tools/port_names_diff.py` refuses to apply a rename rule that has a
counterexample.

**The 39 `resolved` rows are one signature each.** Four narrowing rules, every
one of which exists because a looser version produced a wrong answer *on this
corpus*:

- **injective** — `gnome_tree_branch_1/2/3` all want `climbing_branch`; only
  `_1` (op `Climb`) is unique once ops are read, and `_2`/`_3` (`Climb-down`)
  stay `family`.
- **the target must not itself be a reference name** — `observatory_professor2`
  → `observatory_professor`, which the reference *also* defines and which
  resolves here already. That is a 2:1 collapse, not a rename. Four rows are
  `collision` for exactly this, and one (`dragonslayer_ned` → `ned`) would
  otherwise have produced a duplicate `[opnpc1,ned]`.
- **no suffix heuristic** — longest-common-suffix maps `king_lathas` to
  `ds2_meeting_king_lathas` and `crafting_guild_door` to `ranging_guild_door`.
  Both are wrong; see 19.4.
- **ops are a subset, not an equality** — a modern record gains verbs.
  Requiring equality lost `loc_2114` → `coal_truck` (which gained
  `Investigate`) and kept generic coffins that equality happened to single out.

### 19.4 Corrections to §4's own worked examples

§4's *numbers* for `npc` are exact. Its *examples* are not, and all four are the
same mistake — a name that resolves to a plausible record nobody read.

- **`harlow` → `dr_harlow`** is wrong. `dr_harlow` (npc 3480) is a **nameless
  multinpc wrapper** whose `multinpc1` is the record that is actually Dr Harlow:
  `dr_harlow_vis` (npc 16277). Landed as `resolved` against the concrete record.
- **`king_lathas` → `ds2_meeting_king_lathas`** is wrong. That record (npc 8046)
  is the Desert Treasure II cutscene copy — identical fields plus
  `category=1207`. The Ardougne record is `kinglathas_vis` (npc 9005). Both
  display 'King Lathas' and both carry `op1=Talk-to`, so no rule here separates
  them; the row is `family` and the open question is wrapper-vs-concrete, which
  is a decision rather than a measurement.
- **`leprechaun` → `farming_tools_leprechaun` / `myarm_leprechaun` /
  `zanarisleprechaun`** — all three wrong. They display 'Tool Leprechaun',
  'Tool Leprechaun' and 'Shamus' at vislevel 0, against the reference's
  **level-12 attackable** Leprechaun (`op2=Attack`). **No osrs239 npc displays
  'Leprechaun' at all**, so the row is `absent`. Three plausible spellings,
  three wrong records.
- **the "genuine 2004-only tail" (`game_trawler_*`, `gnome_obstacle_*`,
  `barbarian_rope_swing`)** is wrong for all three families. This tree carries
  28 `trawler*` locs, the gnome and barbarian agility courses, and four
  ropeswing records. Four `game_trawler_*` rows landed as `resolved`; the rest
  are naming drift waiting on a member choice, not missing content.

One more the plan did not name: **`dragonslayer_giantrat`** is 'Giant rat' at
vislevel 6, and this tree has **three** records there (`giantrat1`,
`giantrat1_2`, `giantrat1_3` — one per body model). The reference's single
record maps to a *set*. Auto-mapping on the display name is worse than it looks,
because the same display at vislevel 3 is `giantrat`/`giantrat_grey`/
`newbiegiantrat` and at 26 it is the Sins-of-the-Father familiars.

And one found by reading a use site rather than a record: **`loc_3192`** matches
`pvpa_scoreboard` on display, footprint and op, and is not the same thing — the
reference uses it from `minigames/game_duelarena/`, and `pvpa_scoreboard` is the
**PvP Arena's**, the Duel Arena's replacement. osrs239 states no Duel Arena
scoreboard. Carried as `proposed`: a successor is a decision, not a rename.

### 19.5 What could not be resolved, and why that is the honest answer

- **`seq` and `spotanim` state no display name**, so outside the 113 manifest
  rows and the 28 renames there is nothing to compare. The frame count is the
  only signal and it is worthless alone: `chompy_landing` has 13 frames, so does
  `chompy_update_attack`, and the semantic match — `chompy_update_fly_down`, a
  landing — has **35**. All 49 remaining `seq`/`spotanim` rows are `deferred`
  and each one states how many records here share its structure (345 for 13
  frames), which is the measurement that says why.
- **130 `scenery` rows** are locs whose display name is generic past the point
  where any amount of name evidence exists — 328 'Gate', 488 'Ladder', 226
  'Barrel', 191 'Rocks'. A loc's identity is fixed by **where it stands**; the
  `.jm2` ↔ osrs239 map-square cross-reference is the tool that resolves them and
  this step neither builds it nor hand-picks 130 gates instead.
- **22 `absent` rows** are the recorded 2004-only decision: nothing in this tree
  carries the display name. The clearest block is the anti-macro Ent event
  (`macro_ent_dead_tree1/2`, `macro_ent_oak/maple/yew/magic` — 'Dead tree',
  'Oak', …, as *npcs*), plus `jeremy_servil`, `zambo`, `willowthewisp`,
  `gnomepilot`, `Artist1/2` ('DeVinci') and `leprechaun`.
- **`troll` is the largest single unresolved name in the corpus** — 47
  references — and it does not resolve: the reference's 'Troll' is vislevel 69,
  and this tree's five 'Troll' records are `poh_troll` (91) and four
  `my2arm_flashback_troll_*` (0).

### 19.6 The permanent check

`tools/port_names_diff.py --check`, a rule inside the existing `test-port`
target (no new top-level target). It re-derives every stated fact from both
trees on every run — a row's evidence is `display=`/`vislevel=`/`size=`/`ops=`/
`frames=`/`model=`/`src=`, not prose — and fails on:

1. a reference name with no row (an unclassified name),
2. a row whose name this tree now provides directly (the row is stale),
3. a `manifest` row whose source id stopped holding its stated target,
4. a `resolved`/`renamed` row whose display, size, ops, frames or model no
   longer agree on either side,
5. a target that is not a record of that namespace here,
6. two rows claiming one target without `collision`,
7. a `family`/`absent`/`scenery`/`deferred` row carrying a target, or a
   `resolved` row carrying no re-checkable evidence.

Eight mutations, each proved and reverted: a deleted row; a manifest target
swapped to another manifest row's; an edited `display=`; a `family` row given a
target; two rows on one target; a `resolved` row stripped of its evidence; an
edited `frames=`; and a probe `.npc` in this tree spelling `macro_bigfish`,
which fires rule 2 — the case where landing the row would have been *worse*
than leaving it unresolved.

Like the other five port checks it degrades: with no LostCity checkout it prints
"skipped" and returns 0, because a bar that cannot run has not failed.

### 19.7 Judgement calls, stated

1. **`_unpack` is read for EVIDENCE, never to port.** §11 is right that it is
   the reference's own decompiled review queue — but **163 of the 524 names its
   authored scripts reference are stated nowhere else**, and a row with no
   record on the reference side has nothing to check. `lc_layer` records which
   half every row's facts came from. `tools/port_category_crawl.py` (§16.2) made
   the same call for the same reason.
2. **The Inferno manifest is parsed out of the area README's shell block**,
   because that is where it is (`area_inferno/README.md`). `lc_manifest.h` says
   shell history is not a repeatable port and it is right; that is a filed gap,
   not this step's, and the 71 Inferno rows are read rather than trusted to
   re-run.
3. **`port/names.map` is TSV, machine-checked**, like every other artifact in
   `port/`. A build gate that reads prose is fragile.
4. **The map lands no aliases.** A `resolved` row does not add a second name to
   `configs/all.<ns>.compack`; `sscompile` consuming `port/names.map` belongs to
   the lane that ports scripts, and the id space is 3c's file.

### 19.8 Two findings for whoever is next

1. **`port/name_diff.signed` and `port/names.map` are disjoint by
   construction**, so this step appended nothing to the signed file. §14's diff
   covers names present in **both** trees; every row here is a name present in
   the reference and **not** in this tree. The plan's expectation that 3e would
   append sign-off rows was a misreading of what the two artifacts contain.
2. **`tools/port_name_diff.py`'s `loc` shape column has never measured
   anything.** It reads `model1`, and `model1` appears **0 times** in this
   tree's `configs/all.loc` (which spells it `models=`) and **0 times** in the
   reference's authored `.loc` files (which spell it `model=`). All 1,319 signed
   `loc` rows therefore carry an empty model on both sides. No verdict is wrong
   — `loc` has a display name, so the verdict never depended on the shape — but
   the structural column a reviewer might lean on is not there. Fixing it is
   §14's file and would re-write 1,319 signed rows, so it is reported rather
   than done. `tools/port_names_diff.py` reads `model`/`models` and is where the
   33 `model=` facts in this step's map come from.

---

## 20. The work queue the symbols phase left

Written 2026-08-01, from the six `port/*` artifacts. **This is bar 1's whole
point**: an unresolved name is an error, the error list is the queue, and a
queue that lives only in a build failure is a queue nobody can plan against.
Every row below is machine-checked — `make -C src test-port` fails if any of
these dispositions is quietly changed, so this section cannot rot without the
build going red.

### 20.1 By artifact

| artifact | rows | landed / decided | **queued** | blocked on something else first |
|---|---:|---:|---:|---|
| `port/constants.map` | 1,735 | 167 (22 landed, 97 present, 14 rederived, 3 renamed, 31 never) + 173 tree-only | **1,395** | 168 wait on 3d's varbits, 115 on a destination table, 1,112 on their slice |
| `port/categories.map` | 53 | 18 minted | **35** | 20 orphans need `category` allowed to grow; 8 splits and 4 collisions need a human; 2 `broader`, 1 placeholder |
| `port/configs.map` | 1,650 | 134 (75 landed, 56 present, 1 cache-record, 2 duplicate-concept) + 31 tree-only | **1,485** | 149 structs on a loader, 287 on a column type, 63 on a name, 985 on their slice, 1 on `music` |
| `port/vars.map` | 357 | 43 (24 present, 19 evidenced varbits) | **314** | 98 unresolved, 92 clean-varp unproven, 79 varn/vars, 21 bitfields blocked on authoring a varbit at all, 19 carriers, 5 false friends |
| `port/names.map` | 524 | 180 carry a target | **344** | 130 scenery, 125 family, 59 deferred, 22 absent, 6 collisions, 2 proposed |
| `port/name_diff.signed` | 4,279 | all signed | — | 85 `different-thing` and 495 `plausible-sibling` rows are signed *as such* — the verdict is recorded, not resolved |

### 20.2 The five things that are blocked on engine or register work, not on judgement

Nothing in this list is a decision anybody can make today. Each is a
prerequisite with a named owner elsewhere in the tree.

1. **`.struct` has no loader and `SS_OP_STRUCT_PARAM` has no server case.**
   149 blocks and 733 `param=` references wait on it. `grep -rn '"\.struct"'
   src/` is 0 hits.
2. **Server-overlay `.param` declarations are not walked**, so no landed param
   has a readable type or default. Safe only while nothing names one — and **27
   of the reference's 214 params are `type=string`**, where `push_typed_param`
   answers an absent row by pushing an int, which is a permanent VM stack
   desync rather than a wrong value.
3. **A varbit cannot be authored.** No `fields/varbit.ini`; `content.ini` says
   `ids = cache`; and the server reads varbits from the frozen `cache.osrs239`
   rather than from cachepack's output, so an authored varbit would be invisible
   to it. 21 `bitfield` rows and every future bit-range decision sit behind this.
4. **`category` cannot grow.** `ids = cache`, no `server_base`, so
   `ss_allocate.py` never sweeps it — 20 npc categories have nowhere to get an
   id. §17 removed the trap that would have made promoting it write a file
   nothing reads, so it is now a one-line change plus a base decision.
5. **loc categories do not exist at all.** `dat2_config_loc.c:1009` is
   `case 61: g2(buffer); // Skip`, so `configs/all.loc` carries **0**
   `category=` lines against npc's 9,149, and all 89 loc category names have
   nothing to resolve against. An rscache write-path change (read
   `3rd/rscache/EXCEPTIONS.md` first; the bar is the byte-exact round trip),
   *and* the two-valued door enum currently squatting on the `category=` key has
   to move first.

Two smaller ones, same shape: **this tree names no sounds and no music** —
`pack/4_soundeffects.pack` is 12,010 `synth_<id>` placeholders and
`pack/6_musictracks.pack` 881 `song_<id>`, with 0 real names in either, blocking
282 dbrows and 5 params; and **seven npc dispatch sites still pass `-1`**
(§16.6), of which `AI_QUEUE3` is what `drop tables/` needs.

### 20.3 The rows that need a human, ranked by what a wrong answer costs

| # | what | why it cannot be mechanical |
|---:|---|---|
| 5 | `port/vars.map`'s **false friends** — `%prayer1/9/10/11/12` | they resolve, to Clan Wars and quick-prayer state, across 35 whole-varp writes. The nine siblings that fail to compile are the *safe* half of the same family |
| 1 | **`[music]`** — LostCity's 4-column table against the client's 15-column table 44 | 195 dbrows attach to it by name and every `data=name,…` lands on `sortname`. Fixed by renaming one table, but which name is a decision |
| 8 | **category splits** — `citizen`, `guard`, `petcat`, `shop_keeper`, `troll_thrower`, `mountain_troll`, `undead_one`, `citizen_burthorpe` | this cache split one reference category by sex, area or tier. Members resolve to 2–6 ids each |
| 4 | **category collisions** — `troll_general`/`troll_spectator` both read 309, `battle_mage`/`gnome_troop` both read 354 | 309 is just "troll". Two names, one group |
| 2 | **`broader` categories** — `black_demon` → 275 (the demon category, 37 of 77 not black), `giantrat` → 262 (the rat category) | both pass every automatic rule: unique, resolving, only candidate. Only reading the members says no |
| 125 | `port/names.map` **`family`** rows | the family is certain and the member is not — `dragonslayer_ghost` → five records all at level 19 |
| 130 | `port/names.map` **`scenery`** rows | 328 records here display 'Gate', 488 'Ladder', 191 'Rocks'. **The right tool is not more name-guessing**: a loc's identity is *where it stands*, and the `.jm2` ↔ map-square cross-reference would collapse most of these to one candidate each. It does not exist |
| 98 | `port/vars.map` **`unresolved`** | the class argument (a server varp, `transmit=no`) is sound and applying it per name still needs a check that no varbit already covers the concept under another spelling — which is the exact trap the step is about |
| 92 | `port/vars.map` **`clean-varp`** | mechanically safe, and nothing proves the varp still *means* what it meant. A varp has no display name, so §4.1's artifact has nothing to compare. `phoenixgang`/`blackarmgang` live here |

### 20.4 The one policy question handed back

**1,112 constants were deferred rather than copied** (§15.3). The plan sized 3a
as "1,138 mechanically"; the test applied instead was that a constant lands when
its value cannot be wrong *and* the reference's directory for it already exists
here. `general/configs/quest.constant` — 116 rows, 770 reference uses — is the
strongest candidate for re-argument, because its `^*_questpoints` half is
era-independent in kind. If that is overruled, `port/constants.map` is the work
order and `tools/port_constant_diff.py --report` prints it as TSV.
