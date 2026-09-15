# Finishing Fletching

Plan to take `skill_fletching/` from its F2P vertical slice to the complete
[Fletching](https://oldschool.runescape.wiki/w/Fletching) skill. The wiki is the
authority for content (products, levels, XP, tools, gates); the cache
(`OSRS-Content/osrs239-content/configs/all.obj`, `all.seq`, `pack/category.pack`)
is the authority for names and for what is *expressible* at this revision.

## Implementation status (2026-08-16)

**S0–S14 all landed.** `map_members` is a real world flag
(`src/torirsserver/torirs_server.h`'s `members_world` field, defaulted on via
`ToriRSServer_FlagDefaultOn("TORIRSSERVER_MEMBERS_WORLD")` at every server
construction site); every recipe in §2 down to Broad/S10 is authored as
`fletching_table` / `fletch_bow_table` / `crossbow_limb_table` rows, wired
through `[opheldu,…]` dispatchers, and exercised by a new
`skill_fletching/scripts/fletching_selftest.rs2` (`::fletchingrun`).

Two corrections against this plan's own assumptions, found while building it:

- **§1.3's `~skill_multi` on interface 270 was not built.** Its three C-side
  limits (`TORIRSSERVER_RESUME_BUTTON_MAX`, `TORIRSSERVER_RESUME_SUB_MAX`, the
  `if_addresumebutton` event mask) are real, and interface 270's entry
  clientscript's exact argument order was reverse-engineered from a
  disassembly, never verified live — exactly the risk profile the plan's own
  fallback clause described. S2 ships instead as Make-X on `p_countdialog` +
  `last_int` in `cut_logs.rs2` (the only fletching recipe that re-queues;
  arrows/darts/bolts/javelins already consume the whole available stack in
  one click), the same proven path `summoning_infuse.rs2` already uses.
- **`fletching_table`'s `skill_sound` column (synth type) does not work.**
  `torirs_server_db.c`'s dbtable loader has no `synth` entry in its column-type
  table, and its own comment says why: *"The fix for `synth`/`midi` is to
  name the sound and music namespaces and give this runtime their packs, not
  to widen this list."* That is real engine work, out of scope here. Sounds
  are hardcoded per action instead (`fletch` for cutting/attaching, `chisel`
  for tip cutting, `string_bow` for stringing) — functionally identical,
  since every row of a given action already wanted the same sound.

Two pre-existing bugs fixed as part of S1/S8, beyond what §1.4 already named:
`cut_logs.rs2`'s batch loop had no inventory-full stop (checked only that
logs remained), and `make_arrows` had no inventory-space check at all before
this pass.

Two bugs the selftest itself caught, live, mid-build — not found by inspection:

- **Every `fletching_table:product` read used as a single-variable capture was
  wrong.** `product` is a `namedobj,int` tuple column; `def_namedobj $product =
  db_getfield(...)` compiles and type-checks fine but silently returns the
  wrong object (observed live: it resolved to `mcannontoolkit`, id 1, for
  every bow and crossbow tested) instead of erroring. `string_bow` and
  `string_crossbow` had this bug from the first draft; every other recipe
  already used the correct two-variable `$product, $count = db_getfield(...)`
  form, which is why only bow/crossbow stringing broke. Fixed by matching that
  form everywhere.
- **`[label,...]` cannot be called and returned to.** The five accounting
  checks in the selftest originally called `make_headless_arrows`/
  `make_arrows`/`make_darts`/`make_bolts`/`make_javelins`/`string_bow` via
  `@`, matching how every other trigger handler in this skill invokes them.
  But `@` is a tail jump (`ssvm.c`'s `goto_frame` clears the whole call
  stack) — calling one from inside a test proc doesn't return control to that
  proc, or to the debugproc that called it; the entire script just ends,
  silently, with no OK or FAIL line. Fixed by converting these six recipe
  entry points from `[label,...]` to `[proc,...]` (and their existing
  `[opheldu,...]` call sites from `@` to `~` to match) so they can be gosub'd
  and returned from. Every other fletching recipe stays a label, invoked with
  `@`, exactly as before.


One data gap flagged, not invented: **S10's broad ammo is level-gated only**
— the wiki's 300-point "Broader Fletching" Slayer unlock has no reward-shop
or unlock-varbit convention anywhere in `skill_slayer/` to read from.

Queue items this closes: **#115–#122** in
[`SKILLS_CONTENT_PORT_QUEUE.md`](SKILLS_CONTENT_PORT_QUEUE.md). Scope also
includes mith grapple, ballistae + toxic blowpipe, and wooden shields +
battlestaff — all checked, all expressible (§0.3).

---

## Why this pass is more than eight queue rows

**1. Every fletching recipe is dead at runtime.** All 10 `[label,…]` entry points
open with

```
if (map_members = ^false) {
    mes(^mes_members_fletching);
    return;
}
```

and `SS_OP_MAP_MEMBERS` is hardcoded to push `0`
(`src/torirsserver/torirs_server_scripts.c:8058` — *"The mock is a free world, so this is a
constant"*). Fletching is a members skill, so **nothing fletches today**; every
click prints "You need to be on a members' world to gain experience in
Fletching." No other skill in this tree is gated this completely.

**2. Queue #117's premise is wrong.** "Crossbow stocks / limbs / unfinished bolts
— not expressible" is false. Every part exists in `cache.osrs239` under the
`xbows_` prefix: limbs bronze→dragon (`9420`–`9431`, `21918`), stocks wood→magic
(`9440`–`9452`, `21952`), `xbows_crossbow_string` (`9438`), unstrung crossbows,
`*_unfeathered` bolts, gem tips jade→onyx (`9187`–`9194`), and the grapple chain.
Even the per-tier fletching anims are there (`4436=xbows_fletching_wood_bronze` …
`4441=xbows_fletching_mahogany_adamantite`). The blocker was a name search, not
the data.

**3. The real make-menu is alive client-side and unused server-side.** Interface
**270 `skillmulti`** is unpacked and named (`interfaces/skillmulti.if`, 35
components: `1/5/10/other/x/all` + 18 item slots `a`–`r`), with 18 clientscripts
(2046–2063) and both varcs (`200=skillmulti_quantity`,
`201=skillmulti_suggestedquantity`). The entry point is clientscript **2046**,
signature **21 ints + 1 string** (mode, max, 18 obj ids with `-1` = empty, initial
quantity, title). `grep skillmulti` across `src/` and the whole content tree
returns **zero** hits — every skill inlines `~p_choice3("Make 1","Make 5","Make
10")` instead, and says so in a comment
(`cut_logs.rs2:3`, `skill_crafting/scripts/{glass,pottery,spinning,leather}`,
`areas/wizard_tower/scripts/armourmaking_wizard.rs2:7`).

---

## 0. Where Fletching stands today — measured

### 0.1 What is wired

`OSRS-Content/osrs239-content/server/scripts/skill_fletching/` is **775 lines**
across 7 scripts plus 5 dbrow files and a 16-line dbtable.

| file | recipes | notes |
|---|---|---|
| `scripts/cut_logs.rs2` (135 L) | shafts (normal logs only), 6 unstrung bows | also the shared `[opheldu,knife]` dispatcher for **9 non-fletching consumers** (Wintertodt bruma root, fruit slicing, achey logs, Great Brain Robbery oak planks, Elid shoes, sacred eel, barbarian fish, arctic pine) plus a `bucket_water` selftest fixture that must stay first |
| `scripts/bows.rs2` (74 L) | 6 stringings | 6 hand-written `[opheldu,unstrung_*]` blocks — the scaling wall, see §1.2 |
| `scripts/arrows.rs2` (93 L) | headless + bronze/iron/steel arrows | owns `[opheldu,feather]`, cap 15 |
| `scripts/darts.rs2` (45 L) | bronze→rune darts | cap 10; complete against the wiki |
| `scripts/bolts.rs2` (87 L) | opal/pearl tip cutting + opal/pearl/barb bolts | `[label,make_bolt_tips]`, called from crafting's `[opheldu,chisel]` (`skill_crafting/scripts/gem/uncut_gem.rs2:13`) |
| `scripts/ogre_arrows.rs2` (140 L) | full Big Chompy chain | achey shafts, wolfbone tips, ogre headless, ogre arrows |
| `scripts/fletching.rs2` (6 L) | `~get_fletching_data` | `db_find(fletching_table:item, …)` |
| `configs/fletching.dbtable` | `fletch_bow_table`, `fletching_table` | 2 schemas, 24 rows total |

XP is stored in **tenths** and the existing rows are **wiki-exact** — spot-checked
shortbow `50` = 5 XP, oak longbow `250` = 25, willow shortbow `332` = 33.2, bronze
arrow `13` = 1.3, barbed bolts `95` = 9.5. That convention is correct; keep it.

### 0.2 Stat wiring is complete — no work required

| layer | location |
|---|---|
| Stat id | `pack/stat.pack:12` → `9=fletching` (protocol-fixed by `UPDATE_STAT`) |
| Display name | `general/configs/stat.enum:30` |
| XP curve | generic `g_xp_table` in `src/torirsserver/torirs_server_combat.c:527` |
| Level-up trigger | `levelup/scripts/levelup.rs2:17` → `[advancestat,fletching] @levelup` |
| Skill guide | `interface_skill_guide/configs/skill_guide.constant:94` = 19 |
| XP drops | `interface_chrome/configs/xpdrops.varp:117,237` |
| Client UI | `interfaces/stats.if:209`, `levelup_display.if`, `xpreward.if` |
| Cape item + shop | `skill_combat/configs/equipment.obj:1855`, `shop/catherby/hicktons_archery_emporium__2.inv` |

The **level-up dialogue** is a one-line `mes("You feel yourself getting
stronger.")` for all 23 skills — interface 233 is never opened. That is a
tree-wide gap with its own spec in
[`questlist_chatmenu_levelup.md`](questlist_chatmenu_levelup.md) §3, out of this
lane, noted here so it isn't mistaken for a fletching bug.

### 0.3 Every item this plan needs is already named in the cache

Checked against `configs/all.obj` / `all.obj.compack`. **No new obj is required
anywhere in this plan.** The names that are not guessable:

| thing | cache name |
|---|---|
| crossbow limbs | `xbows_crossbow_limbs_{bronze,blurite,iron,steel,mithril,adamantite,runite,dragon}` |
| crossbow stocks | `xbows_crossbow_stock_{wood,oak,willow,teak,maple,mahogany,yew,magic}` |
| unstrung crossbows | `xbows_crossbow_unstrung_{bronze…dragon}` |
| crossbow string | `xbows_crossbow_string` |
| unfinished bolts | `xbows_crossbow_bolts_{bronze,blurite,iron,steel,mithril,adamantite,runite,silver}_unfeathered` |
| gem bolt tips | `xbows_bolt_tips_{jade,redtopaz,sapphire,emerald,ruby,diamond,dragonstone,onyx,amethyst}` |
| existing tips | `opal_bolttips`, `pearl_bolttips`, `barbed_bolttips` |
| broad | `slayer_broad_arrowhead`, `slayer_broad_arrows`, `slayer_broad_bolt`, `slayer_broad_bolt_unfinished`, `slayer_broad_bolt_amethyst` |
| amethyst | `amethyst`, `amethyst_arrowheads`, `amethyst_dart_tip`, `amethyst_javelin_head`, `xbows_bolt_tips_amethyst`, `amethyst_arrow`, `amethyst_javelin` |
| dragon ammo | `dragon_arrowheads`, `dragon_dart_tip`, `dragon_javelin_head`, `dragon_bolts`, `dragon_arrow` |
| javelins | `javelin_shaft` (**19584**) + `{bronze…dragon}_javelin` / `_javelin_head` |
| mith grapple | `xbows_grapple_tip_mithril` → `xbows_grapple_tip_bolt_mithril` → `xbows_grapple_tip_bolt_mithril_rope` |
| brutal arrows + nails | `zogre_brutal_{bronze,iron,steel,black,mithril,adamant,rune}`, `nails_{bronze,iron,black,mithril,adamant,rune}` |
| comp ogre bow | `unstrung_zogre_bow` / `zogre_bow` |
| ballista | `ballista_frame_{light,heavy}`, `ballista_limbs`, `ballista_incomplete_{light,heavy}`, `ballista_spring`, `ballista_unstrung_{light,heavy}`, `ballista_rope`, `light_ballista`, `heavy_ballista` |
| toxic blowpipe | `blowpipe_fang` (tanzanite fang) → `toxic_blowpipe` |
| wooden shields | `{oak,willow,maple,yew,magic,redwood}_shield` |
| battlestaff | `celastrus_wood` (= celastrus bark) → `battlestaff` |
| maple+ bows | `unstrung_{maple,yew,magic}_{short,long}bow` → `{maple,yew,magic}_{short,long}bow` |
| anims | `1248=human_fletching`, `4433=human_ogre_fletching`, `4436`–`4441` per-crossbow-tier |
| sounds | `2604=fletch_once`, `2605=fletch`, `2606=string_bow` |
| categories | `968=arrowheads`, `530=bolttips`, `969=dart_tips`, `22=firemaking_logs` |

**One category trap for #120**: dragon arrows are cache category **701
`arrows_dragon`**, not 62 `arrows`. `pack/category.pack` documents this at length
and content must ask `~ammo_is_arrow`
(`skill_combat/scripts/player/player_ranged.rs2`).

**Two names to confirm before writing rows**: `ballista_rope` is *probably* the
[monkey tail](https://oldschool.runescape.wiki/w/Monkey_tail), and `blowpipe_fang`
is *probably* the [tanzanite fang](https://oldschool.runescape.wiki/w/Tanzanite_fang).
Check `oc_name` before committing either — do not assume.

### 0.4 What has no coverage at all

- **No fletching selftest.** `grep -rn fletching server/scripts/selftest*.rs2`
  returns nothing.
- **`skillmulti` is server-side dead** (see above).
- **`p_countdialog` + `last_int` already works** (bank, farming tools;
  `ported_scape2009_summoning/scripts/summoning_infuse.rs2:31-52` is a proper
  1/5/10/X/max shape) — numeric entry is reachable today without new engine work.
- **`weakqueue` is implemented** (`torirs_server_scripts.c:3035`, `:7739`) and
  [`SERVER_QUEUES.md`](SERVER_QUEUES.md) §1 names "Make-X, fletching, smithing
  loops" as its intended user — but `SS_TRIGGER_QUEUE` is **not dispatched
  anywhere** in `src/net` or `src/game`, and content calls `weakqueue` only in
  `selftest_triggers.rs2`.

---

## 1. Blockers — fix these before adding a single recipe

### 1.1 S0 · `map_members` → a real world flag  *(blocks everything)*

Replace the hardcoded `SSVM_PushInt(state, 0)` at
`src/torirsserver/torirs_server_scripts.c:8058` with a world field: members on by default,
env override to force a free world.

**Before changing it**, run `grep -rn 'map_members'
OSRS-Content/osrs239-content/server/scripts` and enumerate every other behaviour
the flag flips — report that list rather than discovering it from a test failure.
Then run the full suite and confirm the failure count is unchanged from its
pre-fletching baseline.

This is the single highest-value line in the plan: without it, no slice below is
testable end to end.

### 1.2 S1 · One table, not forty `[opheldu]` name bindings

`bows.rs2` needs a new 5-line `[opheldu,unstrung_*]` block *per bow*, because
unstrung bows have no cache category. Adding maple/yew/magic that way is 3 more
blocks; adding 8 stocks + 8 limbs + 8 unstrung crossbows + 8 unfinished bolts +
9 gem tips + 8 javelin heads that way is ~40 more.

Collapse to a single recipe table keyed on the **pair**:

```
[fletching_recipe]
column=primary,obj,INDEXED,REQUIRED    // the item clicked / cut
column=secondary,obj                   // knife, chisel, hammer, feather, bow_string, limbs…
column=product,namedobj,int            // product + count per action
column=level,int
column=experience,int                  // tenths, per action
column=batch_cap,int                   // 15 arrows / 10 bolts+darts / 1 bow
column=ticks,int                       // 2 shafts+arrows, 7 shields
column=skill_anim,seq
column=skill_sound,synth               // restores the dropped column, §1.4
column=message,string
column=unlock_varbit,int               // Broader Fletching; 0 = always
```

Dispatch from one generic `[opheldu]` per tool/secondary — `knife`, `chisel`,
`hammer`, `feather`, `bow_string`, `xbows_crossbow_string` — plus category
bindings for `arrowheads` / `bolttips` / `dart_tips`, all resolving through one
`~fletch_recipe(primary, secondary)` lookup. New content becomes a dbrow, not a
script. This is the move [`FISHING_COMPLETION_PLAN.md`](FISHING_COMPLETION_PLAN.md)
§1 made with `fishing.dbtable`, and what `cooking_generic.dbrow` already does.

`fletch_bow_table` (log → shafts / shortbow / longbow, chosen by dialogue) stays
— it is a genuinely different shape.

**Four engine/compiler facts the design must respect:**

- **`[opheldu,_category]` inverts `last_item` / `last_useitem`.** The dispatcher
  is a 4-rung ladder (`torirs_server_scripts.c:2519-2580`): rung 1 the clicked item's
  *type*, rung 2 the dragged item's type **with a swap that sits outside the null
  check**, rung 3 the clicked item's *category*, rung 4 the dragged item's
  category with a swap back. So under a category subject, `last_item` names the
  *other* item. `arrows.rs2` already depends on this — `@make_arrows(last_item)`
  under `[opheldu,_arrowheads]` versus `(last_useitem)` under
  `[opheldu,headless_arrow]` — and `[opheldu,_bones]` in `selftest_useon.rs2`
  pins it. **Normalise the pair once, in the dispatcher.**
- **A duplicate `[opheldu,X]` header replaces rather than extends.** One binding
  per subject, tree-wide. That is why `cut_logs.rs2` owns `[opheldu,knife]` for
  nine unrelated consumers, and why `leather.rs2`'s header records that leather
  crafting and the bongos could not both work. Check any new tool binding against
  the whole tree first; [`SCRIPT_NAME_COLLISIONS.md`](SCRIPT_NAME_COLLISIONS.md)
  line 28 flags exactly this hazard. There is no `[opheldu,_]` wildcard,
  deliberately.
- **New param/table names need allocation.** A `.param` file beside the configs,
  then `python3 tools/ss_allocate.py --tree OSRS-Content/osrs239-content` (which
  `make -C src torirsserver-scripts` runs for you). Server dbtable ids ≥ 259, dbrow
  ids ≥ 16940 ([`DBTABLES.md`](DBTABLES.md)). `.obj` overlays have **no** C
  whitelist — only `.npc` params go through `apply_param()`
  (`torirs_server_content.c:1189`) and `.loc` params need `fields/loc.ini`. The fishing
  pass's whitelist trap does not apply to this lane.
- **A stat name in an identifier position is a trap.** `fletching` happens to be
  safe, but `attack` is varp 259, `fishing` is loc 20926, `hitpoints` is param
  2100 — only the `STAT*`/`NPC_STAT*` family gets the kind hint. Keep stat
  literals in config keys.

### 1.3 S2 · `~skill_multi` on interface 270

The client half is complete; only the server driver is missing.

```
[proc,skill_multi](string $title, int $mode, int $max, obj $a … obj $r)(namedobj, int)
if_openchat(skillmulti)
runclientscriptvararg(2046, $mode, $max, $a…$r, <initial qty>, $title)
… arm the 18 item rows …
p_pausebutton
return(<chosen product>, <chosen quantity>)
```

`runclientscript*` (opcode 11003) carries up to `TORIRSSERVER_RUNCLIENTSCRIPT_ARG_MAX
= 28` mixed int/string args — enough for 2046's 21i+1s. Precedent:
`shop/scripts/shop.rs2:108`, `minigames/minigame_puropuro/scripts/puro_scroll.rs2:27`.

**Three C-side limits this hits** — `src/torirsserver/torirs_server.h:577-587` and
`torirs_server_scripts.c:7040-7058`:

| limit | current | needed |
|---|---:|---:|
| `TORIRSSERVER_RESUME_BUTTON_MAX` | 8 | **18** (item rows `a`–`r`) |
| `TORIRSSERVER_RESUME_SUB_MAX` | 15 | **28** — `script_2052.cs2` resumes with `cc_find($component1, %varcint200)`, i.e. **the resume sub-id *is* the quantity** |
| `if_addresumebutton` event mask | arms `TORIRSSERVER_EVENT_CLICK` (0x1) only | the rows are driven by `if_setonop`/`if_setop(1,…)`, needing `^if_event_op1` (2) — so an `if_setevents(skillmulti:a, 0, 28, ^if_event_op1)` must land **after** the resume registration, which overwrites the mask |

`RESUME_PAUSEBUTTON` already carries the sub-id into `last_slot` unclamped
(`torirs_server_world.c:6549-6566`), so raising the two constants is mechanical.
`if_setobject` is implemented but used **once** tree-wide
(`quests/scripts/questscroll.rs2:74`, with the note that its third argument is
*wire zoom, not a count*) — expect to debug it.

Fletching is the first consumer. Smithing, Crafting (glass/pottery/spinning/
leather) and `armourmaking_wizard.rs2` all carry `Era: skill_multi → ~p_choice*`
comments and become follow-on adopters — **do not convert them in this pass**.

If any of the three limits proves deeper than this section expects, fall back to
`~p_choice3` for the content slices, say so explicitly, and re-file S2 — do
**not** stall S3–S14 behind it.

### 1.4 Four data bugs found while measuring

| where | current | correct | ref |
|---|---|---|---|
| `cut_logs.rs2:97,109` | shafts XP = `multiply($shaft_count, 5)` → 15 shafts = **7.5 XP** | **5 XP** flat per log | [Arrow shaft](https://oldschool.runescape.wiki/w/Arrow_shaft) |
| `cut_logs.dbrow` | only `fletching_normal` carries a `shafts` value, so oak/willow can't be cut into shafts at all | every log tier gives shafts, at its own level | same |
| `cut_logs.rs2:119` `@process_fletch_logs` | **no inventory-full stop** — it only checks that logs remain | a full inventory must halt the batch | — |
| `fletching.dbtable:2` | *"Dropped skill_sound (synth)"* → `bolts.rs2:51` hardcodes `sound_synth(chisel)` for every tip type; arrows/darts/stringing are **silent** | `fletch` / `fletch_once` / `string_bow` (2604–2606) are packed and mostly unused | [`SKILLING_SOUNDS.md`](SKILLING_SOUNDS.md) §4.7, row 9 |

Arrow-shaft levels/XP/counts per log ([Arrow shaft](https://oldschool.runescape.wiki/w/Arrow_shaft)):

| log | level | XP | shafts |
|---|---:|---:|---:|
| Logs | 1 | 5 | 15 |
| Oak | 15 | 10 | 30 |
| Willow | 30 | 15 | 45 |
| Maple | 45 | 20 | 60 |
| Yew | 60 | 25 | 75 |
| Magic | 75 | 30 | 90 |
| Redwood | 90 | 35 | 105 |

**Sound caveat when restoring the column**: `sound_synth`'s first argument is
*not* type-hinted — it resolves across all namespaces with no collision note,
which is why `smithing.rs2` says `anvil_4` (a bare `anvil` is an obj category).
Check any new synth name against every `pack/*` first, and check the seq's
embedded frame sounds — `woodcut.rs2` deliberately plays no chop sound because
this cache's seqs already carry one.

---

## 2. The complete wiki inventory

Source of truth: [Fletching](https://oldschool.runescape.wiki/w/Fletching).
✅ = in-tree today · ➕ = this plan · ⛔ = out of lane (§5).

### 2.1 Bows — knife on logs, then bow string

| lvl | unstrung | logs | cut XP | string XP | state |
|---:|---|---|---:|---:|---|
| 5 | Shortbow (u) | Logs | 5 | 5 | ✅ |
| 10 | Longbow (u) | Logs | 10 | 10 | ✅ |
| 20 | Oak shortbow (u) | Oak | 16.5 | 16.5 | ✅ |
| 25 | Oak longbow (u) | Oak | 25 | 25 | ✅ |
| 30 | Unstrung comp bow | Achey + Wolf bones | 45 | 45 | ➕ S7 |
| 35 | Willow shortbow (u) | Willow | 33.3 | 33.2 | ✅ |
| 40 | Willow longbow (u) | Willow | 41.5 | 41.5 | ✅ |
| 50 | Maple shortbow (u) | Maple | 50 | 50 | ➕ S3 |
| 55 | Maple longbow (u) | Maple | 58.3 | 58.2 | ➕ S3 |
| 65 | Yew shortbow (u) | Yew | 67.5 | 67.5 | ➕ S3 |
| 70 | Yew longbow (u) | Yew | 75 | 75 | ➕ S3 |
| 80 | Magic shortbow (u) | Magic | 83.3 | 83.2 | ➕ S3 |
| 85 | Magic longbow (u) | Magic | 91.5 | 91.5 | ➕ S3 |

The cut/string asymmetry (33.3 vs 33.2, 58.3 vs 58.2, 83.3 vs 83.2) is the wiki,
not a typo — the existing `willow_shortbow` rows already encode it (`333` cut,
`332` string).

### 2.2 Arrows — [Arrow](https://oldschool.runescape.wiki/w/Arrow)

| lvl | arrow | inputs | XP | state |
|---:|---|---|---:|---|
| 1 | Headless | Arrow shaft + Feather | 1 | ✅ |
| 1 | Bronze | Headless + Bronze arrowtips | 1.3 | ✅ |
| 15 | Iron | + Iron arrowtips | 2.5 | ✅ |
| 30 | Steel | + Steel arrowtips | 5 | ✅ |
| 45 | Mithril | + Mithril arrowtips | 7.5 | ➕ S4 |
| 52 | Broad | + Broad arrowheads *(Slayer unlock)* | 10 | ➕ S10 |
| 60 | Adamant | + Adamant arrowtips | 10 | ➕ S4 |
| 75 | Rune | + Rune arrowtips | 12.5 | ➕ S4 |
| 82 | Amethyst | + Amethyst arrowtips | 13.5 | ➕ S9 |
| 90 | Dragon | + Dragon arrowtips | 15 | ➕ S9 |

### 2.3 Crossbows — **all ➕ S5**

Stock (knife on logs) → + limbs (hammer) → + `xbows_crossbow_string`.

| lvl | stock | logs | stock XP | limbs | attach XP | string XP |
|---:|---|---|---:|---|---:|---:|
| 9 | Wooden | Logs | 6 | Bronze | 12 | 6 |
| 24 | Oak | Oak | 16 | Blurite | 32 | 16 |
| 39 | Willow | Willow | 22 | Iron | 44 | 22 |
| 46 | Teak | Teak | 27 | Steel | 54 | 27 |
| 54 | Maple | Maple | 32 | Mithril | 64 | 32 |
| 61 | Mahogany | Mahogany | 41 | Adamantite | 82 | 41 |
| 69 | Yew | Yew | 50 | Runite | 100 | 50 |
| 78 | Magic | Magic | 70 | Dragon | 135 | 70 |

Use the per-tier anims (`xbows_fletching_*`, 4436–4441) rather than
`human_fletching` where a tier match exists.

### 2.4 Bolts — feather on unfinished bolts — **all ➕ S6**

| lvl | bolt | XP | | lvl | bolt | XP |
|---:|---|---:|---|---:|---|---:|
| 9 | Bronze | 0.5 | | 54 | Mithril | 5 |
| 24 | Blurite | 1 | | 55 | Broad *(unlock)* | 3 |
| 39 | Iron | 1.5 | | 61 | Adamant | 7 |
| 43 | Silver | 2.5 | | 69 | Runite | 10 |
| 46 | Steel | 3.5 | | 84 | Dragon | 12 |

Unfinished bolts come from Smithing (10 per bar). Check whether `skill_smithing`
emits `xbows_crossbow_bolts_*_unfeathered`; if it does not, **flag the coupling —
do not fix Smithing here**.

### 2.5 Bolt tips — chisel on gems

| lvl | tips | gem | qty | XP | state |
|---:|---|---|---:|---:|---|
| 11 | Opal | Opal | 12 | 1.5 | ✅ |
| 26 | Jade | Jade | 12 | 2 | ➕ S8 |
| 41 | Pearl | Oyster pearl / pearls | 6 / 24 | 3.2 | ✅ |
| 48 | Topaz | Red topaz | 12 | 3.9 | ➕ S8 |
| 56 | Sapphire | Sapphire | 12 | 4 | ➕ S8 |
| 58 | Emerald | Emerald | 12 | 5.5 | ➕ S8 |
| 63 | Ruby | Ruby | 12 | 6.3 | ➕ S8 |
| 65 | Diamond | Diamond | 12 | 7 | ➕ S8 |
| 71 | Dragonstone | Dragonstone | 12 | 8.2 | ➕ S8 |
| 73 | Onyx | Onyx | 24 | 9.4 | ➕ S8 |
| 83 | Amethyst | Amethyst | 15 | 60 | ➕ S9 |

### 2.6 Tipped bolts — tips on finished bolts

| lvl | bolt | XP | state | | lvl | bolt | XP | state |
|---:|---|---:|---|---|---:|---|---:|---|
| 11 | Opal-bronze | 1.6 | ✅ | | 58 | Emerald-mithril | 5.5 | ➕ S8 |
| 26 | Jade-blurite | 2.4 | ➕ S8 | | 63 | Ruby-adamant | 6.3 | ➕ S8 |
| 41 | Pearl-iron | 3.2 | ✅ | | 65 | Diamond-adamant | 7 | ➕ S8 |
| 48 | Topaz-steel | 3.9 | ➕ S8 | | 71 | Dragonstone-runite | 8.2 | ➕ S8 |
| 51 | Barb-bronze | 9.5 | ✅ | | 73 | Onyx-runite | 9.4 | ➕ S8 |
| 56 | Sapphire-mithril | 4.7 | ➕ S8 | | 76 | Amethyst-broad | 10.6 | ➕ S10 |

`barbed_bolttips` has a consumer row but no producer, and that is **correct** —
they come from the
[Ranging Guild Ticket Exchange](https://oldschool.runescape.wiki/w/Barb_bolttips)
(140 archery tickets per 30). Check whether that shop is ported; if not, note it
rather than inventing a fletching recipe.

### 2.7 Darts — feather on dart tips

| lvl | dart | XP | state | | lvl | dart | XP | state |
|---:|---|---:|---|---|---:|---|---:|---|
| 10 | Bronze | 1.8 | ✅ | | 67 | Adamant | 15 | ✅ |
| 22 | Iron | 3.8 | ✅ | | 81 | Rune | 18.8 | ✅ |
| 37 | Steel | 7.5 | ✅ | | 90 | Amethyst | 21 | ➕ S9 |
| 52 | Mithril | 11.2 | ✅ | | 95 | Dragon | 25 | ➕ S9 |

### 2.8 Javelins — heads on `javelin_shaft` — **all ➕ S9**

[Javelin](https://oldschool.runescape.wiki/w/Javelin)

| lvl | javelin | XP | | lvl | javelin | XP |
|---:|---|---:|---|---:|---|---:|
| 3 | Bronze | 1 | | 62 | Adamant | 10 |
| 17 | Iron | 2 | | 77 | Rune | 12.4 |
| 32 | Steel | 5 | | 84 | Amethyst | 13.5 |
| 47 | Mithril | 8 | | 92 | Dragon | 15 |

### 2.9 Amethyst — chisel on amethyst — **➕ S9**

[Amethyst](https://oldschool.runescape.wiki/w/Amethyst): 83 bolt tips ×15 · 85
arrowtips ×15 · 87 javelin heads ×5 · 89 dart tips ×8 — **60 XP each**.

### 2.10 Ogre / brutal arrows — **➕ S7**

[Ogre arrow](https://oldschool.runescape.wiki/w/Ogre_arrow) ·
[Brutal arrow](https://oldschool.runescape.wiki/w/Brutal_arrow) ·
[Ogre bow](https://oldschool.runescape.wiki/w/Ogre_bow)

| lvl | item | inputs | XP | state |
|---:|---|---|---:|---|
| 5 | Ogre arrow shafts | Achey tree logs | 1.6 | ✅ |
| 5 | Ogre arrows | Flighted ogre arrow + Wolfbone arrowtips | 1 | ✅ |
| 7 | Bronze brutal | Flighted ogre arrow + Bronze nails | 1.4 | ➕ |
| 18 | Iron brutal | + Iron nails | 2.6 | ➕ |
| 33 | Steel brutal | + Steel nails | 5.1 | ➕ |
| 38 | Black brutal | + Black nails | 6.5 | ➕ |
| 49 | Mithril brutal | + Mithril nails | 7.5 | ➕ |
| 62 | Adamant brutal | + Adamantite nails | 10.2 | ➕ |
| 77 | Rune brutal | + Rune nails | 12.5 | ➕ |

Gate: [Zogre Flesh Eaters](https://oldschool.runescape.wiki/w/Zogre_Flesh_Eaters)
— `quests/quest_zogreflesheaters/` exists; read its varp, don't invent one.

### 2.11 Broad ammo — Slayer unlock — **➕ S10**

[Broader Fletching](https://oldschool.runescape.wiki/w/Broader_Fletching), 300
Slayer reward points: broad arrows (52, 10 XP), broad bolts (55, 3 XP), amethyst
broad bolts (76, 10.6 XP). The Slayer rewards interface is already live (KRONOS)
— read the unlock varbit from `skill_slayer/`.

### 2.12 Mith grapple — **➕ S11**

[Mith grapple](https://oldschool.runescape.wiki/w/Mith_grapple), 59 Fletching:
grapple tip + mithril bolt → mith grapple (unf), **11 XP**; then + rope → mith
grapple, **0 XP**. Unlocks the Agility grapple shortcuts.

### 2.13 Ballistae + toxic blowpipe — **➕ S12**

[Light ballista](https://oldschool.runescape.wiki/w/Light_ballista) ·
[Heavy ballista](https://oldschool.runescape.wiki/w/Heavy_ballista) ·
[Toxic blowpipe](https://oldschool.runescape.wiki/w/Toxic_blowpipe)

| lvl | step | inputs | XP |
|---:|---|---|---:|
| 47 | Incomplete light | Light frame + Ballista limbs | 15 |
| 47 | Unstrung light | + Ballista spring | 15 |
| 47 | **Light ballista** | + Monkey tail | 300 *(330 total)* |
| 72 | Incomplete heavy | Heavy frame + Ballista limbs | 30 |
| 72 | Unstrung heavy | + Ballista spring | 30 |
| 72 | **Heavy ballista** | + Monkey tail | 600 *(660 total)* |
| 78 | Toxic blowpipe | Chisel on tanzanite fang | 120 |

### 2.14 Wooden shields + battlestaff — **➕ S13**

Knife on **2 logs**, **7 game ticks** per shield — which is why the recipe table
needs the `ticks` column ([Oak shield](https://oldschool.runescape.wiki/w/Oak_shield)):

| lvl | shield | XP | | lvl | shield | XP |
|---:|---|---:|---|---:|---|---:|
| 27 | Oak | 50 | | 72 | Yew | 150 |
| 42 | Willow | 83 | | 87 | Magic | 183 |
| 57 | Maple | 116.5 | | 92 | Redwood | 216 |

[Battlestaff](https://oldschool.runescape.wiki/w/Battlestaff): **40 Fletching,
80 XP**, from `celastrus_wood`. The obj exists; whether Farming produces
celastrus bark in-tree is a separate check — land the recipe anyway and note the
sourcing gap rather than dropping it silently.

### 2.15 Fletching cape, and the shops/NPCs around the skill — **➕ S14**

- **Cape perk**: +1 boost, and *search the cape for a mith grapple and bronze
  crossbow, 3×/day* ([Fletching cape](https://oldschool.runescape.wiki/w/Fletching_cape),
  99,000 gp from Hickton in Catherby, 99 Fletching). Add `skillcape_fletching` /
  `skillcape_fletching_trimmed` to `~skillcape_boost`
  (`skill_combat/scripts/player/skillcape_boost.rs2`) — one `if` block matching
  the capes already there. Item and shop inv already exist.
- **Sources to audit while here** (all *inputs*, not fletching actions):
  [Lowe's Archery Emporium](https://oldschool.runescape.wiki/w/Lowe%27s_Archery_Emporium)
  (Varrock, Lowe) ·
  [Hickton's Archery Emporium](https://oldschool.runescape.wiki/w/Hickton%27s_Archery_Emporium)
  (Catherby, Hickton — the cape vendor) ·
  [Ranging Guild Ticket Exchange](https://oldschool.runescape.wiki/w/Ranging_Guild_Ticket_Exchange)
  (barb bolttips) · Slayer masters' reward shop (Broader Fletching, broad
  arrowheads) · Rantz, Feldip Hills (ogre bow).

### 2.16 There are no fletching locs

Worth stating explicitly, because every other skill plan in `docs/` is loc- or
npc-driven: **Fletching is entirely item-on-item.** No `[oploc*]`, `[opnpc*]` or
`[opobj*]` work appears anywhere in this plan. The only loc-shaped dependency is
the spinning wheel for bow string, which is Crafting and already ported
(`skill_crafting/scripts/spinning`).

---

## 3. The slices

Each is one commit's worth.

| # | slice | closes | depends on |
|---|---|---|---|
| **S0** | `map_members` world flag (§1.1) — the skill is inert without it | — | — |
| **S1** | `fletching_recipe` table refactor (§1.2) + the four §1.4 bugs + restore `skill_sound` | — | S0 |
| **S2** | `~skill_multi` on interface 270 + the three C limits (§1.3) | — | S1 |
| **S3** | Maple / yew / magic bows, cut + string; shafts from every log tier | #115 | S1 |
| **S4** | Mithril / adamant / rune arrows | #116 | S1 |
| **S5** | Crossbows: 8 stocks, 8 limb attachments, 8 stringings | #117 | S1 |
| **S6** | Metal bolts: feather on `*_unfeathered`, 10 tiers | #117 | S1, S5 |
| **S7** | Comp ogre bow + 7 brutal arrow tiers | #119 | S1 |
| **S8** | Gem bolt tips jade→onyx + their tipped bolts | #118 | S1 |
| **S9** | Amethyst (4 chisel products) + amethyst/dragon ammo + javelins | #120 | S1, S8 |
| **S10** | Broad arrows / bolts / amethyst broad bolts + the Slayer unlock read | #121 | S1, S9 |
| **S11** | Mith grapple | — | S1 |
| **S12** | Ballistae ×2 + toxic blowpipe | — | S1 |
| **S13** | Wooden shields ×6 + battlestaff | — | S1 |
| **S14** | Fletching cape perk + skillcape boost | #122 | S1 |

**Ordering note**: S2 sits early on purpose — every slice after it authors its
product list once, on the real menu, instead of writing a `~p_choice3` ladder
that S2 then rewrites. See §1.3 for the fallback if S2 stalls.

```
S0 (map_members)
 └─ S1 (recipe table + 4 bugs)
     ├─ S2 skill_multi ─── enables the real menu for everything below
     ├─ S3 maple+ bows ─┐
     ├─ S4 mith+ arrows │
     ├─ S5 crossbows ───┼─ each independent, any order
     │    └─ S6 bolts   │
     ├─ S7 ogre/brutal  │
     ├─ S8 gem tips ────┤
     │    └─ S9 amethyst/dragon/javelins
     │         └─ S10 broad ammo
     ├─ S11 mith grapple│
     ├─ S12 ballista/blowpipe
     ├─ S13 shields/battlestaff
     └─ S14 cape ───────┘
```

---

## 4. Selftests — land with S1, extend per slice

There is no fletching selftest today. Add
`skill_fletching/scripts/fletching_selftest.rs2` as `[debugproc,fletchingrun]`,
modelled on the 209-line `skill_fishing/scripts/fishing_selftest.rs2`: print
exactly one `fletchingrun OK - N checks passed` / `… FAIL - …` line, and restore
everything it touches.

Wire it into CI by copying the ~50-line capture block beside `::fishingrun` at
`src/torirsserver/torirs_server_world.c:35400`. **`::miningrun` and `::runecraftrun` were
never wired in** — skipping that block means the test only runs when typed
in-client.

| test | asserts |
|---|---|
| `fletchingrun_table` | every `fletching_recipe` row resolves both objs; no duplicate `(primary, secondary)` pair |
| `fletchingrun_xp` | every row's XP matches a checked-in wiki fixture (tenths) — the guard against a 7.5-vs-5 recurrence |
| `fletchingrun_level_gate` | at level N, only rows with `level <= N` succeed |
| `fletchingrun_triggers` | **every** `primary` obj resolves through the dispatcher — the dead-click guard, and what would have caught S0 |
| `fletchingrun_accounting` | `stat_boost(fletching, 99, 0)`, run N actions, assert *inputs spent == products gained × count*, deleting products each iteration so a full inventory can't be mistaken for a consumption bug; then `stat_drain` and re-assert the level |
| `fletchingrun_members` | with S0's flag off fletching refuses; with it on it works |

**Headless trap**, recorded in `fishing_selftest.rs2`'s own comment: every
level-fail branch here ends in `~mesbox`, which opens a dialogue and suspends on
`p_pausebutton` waiting for a click a headless run never sends — **parking the
debugproc forever**. Test *pass* paths through the label; test fail paths by
asserting the table, not by calling the label.

---

## 5. Explicitly out of this lane

| what | why | where it goes |
|---|---|---|
| Level-up dialogue (interface 233, per-skill text, jingles) | tree-wide gap across all 23 skills | [`questlist_chatmenu_levelup.md`](questlist_chatmenu_levelup.md) §3 |
| Converting Smithing / Crafting / `armourmaking_wizard` to `~skill_multi` | S2 makes them possible; converting them is its own lane | their own slices |
| Enchanting tipped bolts (combat effects) | queue #22 | Magic lane |
| Smithing's `*_unfeathered` bolt output | a coupling this plan flags, not fixes (§2.4) | `skill_smithing/` |
| Bow string spool | [Vale Totems](https://oldschool.runescape.wiki/w/Bow_string_spool) minigame, 2025 | KRONOS |
| Camphor / Ironwood / Rosewood blowpipes, [Atlatl darts](https://oldschool.runescape.wiki/w/Atlatl_dart), Sunlight & Moonlight antler bolts, Hunters' sunlight crossbow, Redwood hiking staff | 2024–25 Varlamore/Sailing content | [`KRONOS_CONTENT_PORT_QUEUE.md`](KRONOS_CONTENT_PORT_QUEUE.md) |
| [Kebbit bolts](https://oldschool.runescape.wiki/w/Kebbit_bolts), [Hunter's spear](https://oldschool.runescape.wiki/w/Hunter%27s_spear) | Hunter-sourced inputs | `skill_hunter/` |

---

## 6. Verification

- `make -C src torirsserver-scripts` after **every** config change — it runs
  `tools/ss_allocate.py`, then `sscompile`, which name-checks every obj / seq /
  synth. Then `./src/build/ToriRSServer_Pack --check-only` (expect 0 errors), then
  `make -C src test-ToriRSServer` — which deliberately re-depends on
  `torirsserver-scripts`, so a fresh binary can never be tested against a stale pack.
- Use a **scratch `TORIRSSERVER_SAVES`** — headless runs are not independent.
- **S0**: `grep -rn 'map_members' OSRS-Content/osrs239-content/server/scripts`
  before and after; enumerate everything else the flag flips, and confirm the
  suite's failure count is unchanged from its pre-fletching baseline.
- **S1**: A/B — run the existing 24 recipes before and after the refactor and
  byte-compare the message and XP output.
- **S2**: client-visible and invisible to every selftest — drive it live with
  `./run-osrs239.sh`; confirm all 18 slots populate, that quantities **16–28**
  come back correctly (the `RESUME_SUB_MAX` fix), and that the "X" keyboard
  prompt (`meslayer_skillmulti`, meslayer mode 16) resolves.
- **S5/S6**: crossbow parts have never been touched by content — spawn one of
  each `xbows_` obj with a cheat before writing rows, to confirm they are
  reachable.
- **Per slice**: fletch one of each new product at its exact gate level and one
  level below; confirm the level message, input deletion, XP in tenths, batch
  cap, tick cadence, and that a full inventory halts the batch (§1.4 row 3)
  rather than eating inputs.
- **Live client** for at least one recipe per slice — the anim
  (`human_fletching` / `xbows_fletching_*`) and the restored `fletch` /
  `string_bow` sounds are client-visible and invisible to every selftest.
