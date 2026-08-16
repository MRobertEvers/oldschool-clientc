# Organising shared use-on triggers (`[opheldu]`)

`[opheldu,chisel]` is owned by Crafting, `[opheldu,knife]` by Fletching,
`[opheldu,needle]` by Crafting, `[opheldu,hammer]` by `general_use/`. Each is a
switch that half a dozen *other* lanes need cases in — and each of the ~800
`[opheldu]` triggers in the tree was written on a model of dispatch that is
half wrong. This doc says what the engine actually does, how LostCity handles
the same problem, and the plan.

Written while planning [`CRAFTING_COMPLETION_PLAN.md`](CRAFTING_COMPLETION_PLAN.md)
and re-checked against `skill_fletching/scripts/bows.rs2`; applies to
Fletching, Crafting, Herblore, Cooking, Firemaking alike.

The rules in §4 are what *today's* engine forces. The engine-side fix that
makes most of them unnecessary — a `trigger_decline` fall-through and one
orientation for every rung — is planned in
[`USEON_DISPATCH_ENGINE_PLAN.md`](USEON_DISPATCH_ENGINE_PLAN.md); its §3 says
which rules survive it.

---

## 1. What the engine actually does

`mock230_scripts_run_opheldu` (`src/net/mock/mock230_scripts.c:2519`) is a
line-for-line port of LostCity's `OpHeldUHandler.ts:94-113`. Four rungs, no
wildcard, `getByTriggerSpecific` throughout. `obj` is the item clicked
*second* (the target), `useObj` the one picked up first:

```
1. [opheldu,<obj type>]                              last_item = obj,    last_useitem = useObj
2. [opheldu,<useObj type>]        + SWAP             last_item = useObj, last_useitem = obj
3. [opheldu,_<obj category>]      (state as after 2)  last_item = useObj, last_useitem = obj
4. [opheldu,_<useObj category>]   + SWAP back         last_item = obj,    last_useitem = useObj
   none                                               "Nothing interesting happens."
```

Three consequences, each proved by the engine's own selftest
(`mock230_world.c:30915-31000`, "opheldu, rungs 1 and 2 / rungs 3 and 4"):

**1a. One type binding covers both click orders.** Rung 2 tries the *other*
item's type before either category and swaps, so a script bound to `bow_string`
sees `last_useitem = <the bow>` whether the player used string on bow or bow
on string. Binding the bow as well changes nothing — the bow's own script and
the engine's no-trigger fallback both end in "Nothing interesting happens".

**1b. Category scripts run inverted.** Rung 2's swap sits outside its null
check, so by rung 3 the two are exchanged: in a `[opheldu,_cat]` script,
`last_useitem` names the item the script is *bound to* and `last_item` the
other. Rung 4 swaps back — to the same inversion. This is the reference's
behaviour, asserted in both directions; "fixing" it goes red.

**1c. A category binding is reached only when NEITHER item has a type
binding.** So a category binding on a family whose partner is a type-bound
tool is dead code — and worse, it is a tripwire that catches *other* pairs
touching that family (see §3).

None of this is a compile-time property. Duplicate script names *are* a hard
compile error (`ssc_compile.c:2845`, "declare it once and branch into a
`[label,...]` from the other file" — the 67-duplicate cleanup that un-broke
Sheep Shearer et al.), but a dead binding, an inverted body, or a swallowed
pair all compile clean and pass a selftest that calls the proc directly.

---

## 2. How LostCity does it

Surveyed in `~/Documents/git_repos/LostCity_Server/content/scripts` (rev 254):
298 `[opheldu]` triggers, 25 of them category bindings.

| Question | LostCity's answer |
|---|---|
| Who owns `[opheldu,chisel]`? | The skill that ported it first (`skill_crafting/scripts/gem/uncut_gem.rs2`). Fletching's bolt tips and quest items are `case` lines in that switch that jump to a `[label,…]` **declared in the contributing lane's own tree** (`@make_bolt_tips` lives in `skill_fletching/scripts/bolts.rs2`, `@craft_ivorybeads` in the quest). The trigger is a router; the body lives with its owner. |
| Where do tool triggers that belong to no skill go? | `general_use/scripts/hammer.rs2` — the only one, and it is a pure dispatcher (two `case` lines and a default). |
| Reverse binds? | Habitually **yes** — `[opheldu,_unstrung_bow]`, `[opheldu,_category_22]` (logs), `_category_3` (snelm), `_craft_orb`, `_category_530` (bolt tips), `_category_969` (dart tips) all pair a type-bound tool with a category binding on the target. Every one of them is dead by §1c, and every one is written in the *non*-inverted orientation (`switch_obj(last_useitem) { case chisel : … }`), which would be wrong if it were live. They compile, they never run, nobody noticed. |
| Cross-family recipes (potion × potion)? | `[opheldu,_potion]` with a symmetric body — orientation cannot matter, so it works. |
| Recipe data? | dbtables keyed by the input obj (`fletch_bow_table`, `gem_cutting_table`, `fletching_table`), so a new tier is a dbrow. |
| Categories for families the cache never grouped? | Raw `_category_N` numbers reused where the cache had one, `pack/category.pack` + `.obj` overlay (`unstrung_bow`, `craft_orb`) where it did not. |

So LostCity's real convention, stripped of the dead code: **one type-bound
hub per tool, other lanes contribute a `case → @label` line, recipe data in
dbtables.** The reverse binds are noise it carried without cost because
2004-era content had one bolt, one battlestaff family, one gem tool. Our tree
extended those families and the noise became load-bearing (§3).

---

## 3. What is wrong in our tree today

**Redundant reverse binds — roughly half of the 799.** `bows.rs2` binds
`bow_string` (12-case switch) *and* twelve `[opheldu,unstrung_*]` blocks whose
only content is `if (last_useitem = bow_string)`. By §1a the twelve add
nothing. Fletching alone: 12 unstrung bows, 8 unstrung crossbows (partner
`xbows_crossbow_string` is bound), 8 javelin heads (`javelin_shaft` bound),
one side of stocks × limbs (16 → 8), 5 ballista stages, 3 of `mith_grapple`'s
4 — about **50 of Fletching's 80**. `snelm.rs2` (9), `stringing.rs2` (8),
`dye_cape.rs2` and Herblore's `brew_potion.rs2` (103) are the same shape.

**Dead category bindings, in the wrong orientation.** `cut_logs.rs2
[opheldu,_firemaking_logs]`, `arrows.rs2 [opheldu,_arrowheads]`, `darts.rs2
[opheldu,_dart_tips]`, `bolts.rs2 [opheldu,_bolttips]`, and all five in
`skill_combat/scripts/weapon_poison.rs2` (`_weapon_stab_sword`, `_weapon_spear`,
`_weapon_thrown`, `_bolts`, `_arrows`) — every one pairs with a type-bound tool
(`knife`, `tinderbox`, `headless_arrow`, `feather`, `bolt`, `weapon_poison`,
`tbwt_cleaning_cloth`), so none is reachable for its own purpose, and every
one is written `case <tool>` on `last_useitem`, which under §1b is the bound
item, never the tool.

**One live defect from the tripwire.** Gem-tipped bolts above bronze:
`xbows_crossbow_bolts_runite` (cache category 63 = `bolts`) × `xbows_bolt_tips_onyx`
(530 = `bolttips`). Neither is type-bound (`[opheldu,bolt]` is bronze only), so:

- tips-on-bolts → rung 3 = `_bolts` → `weapon_poison.rs2:46`, `case weapon_poison`
  against `last_useitem` (= the bolts) → default → "Nothing interesting happens".
- bolts-on-tips → rung 3 = `_bolttips` → `if (oc_category(last_useitem) = bolts)`
  where `last_useitem` is the *tip* → false → "Nothing interesting happens".

`fletching_selftest.rs2:265-285` calls `~make_bolts(...)` directly and passes.
Nothing in the tree drives dispatch except the four-item stanza in
`mock230_world.c`. The same shape threatens any future family × family pair
(amethyst arrowtips × headless is safe only because `headless_arrow` is
type-bound).

**Coupling.** A lane cannot add a recipe without editing another skill's file:
Fletching's amethyst edits `skill_crafting/scripts/gem/uncut_gem.rs2`; nine
non-fletching consumers (Wintertodt bruma, sacred eel, Elid shoes, Brain
Robbery planks, Fremennik arctic pine, barbarian fish, fruit, achey logs, the
selftest probe) live inside Fletching's `[opheldu,knife]`. Parallel lanes
collide on the same handful of files (`concurrent-session-commits-your-tree`).

---

## 4. The rules

**R1 — one binding per pair, on the hub.** A *hub* is the obj that meets many
families: `knife`, `chisel`, `needle`, `hammer`, `tinderbox`, `feather`,
`bow_string`, `xbows_crossbow_string`, `javelin_shaft`, `vial_water`,
`pestle_and_mortar`, `rope`. Bind the hub by type, once. The target side gets
**no binding**; rung 2 handles the other click order and hands the script the
same state.

**R2 — the target side gets a category, not a binding.** Categories exist so
the hub can `switch_category(oc_category(last_useitem))` (the form
`interface_combat/scripts/weapon_type.rs2:28` uses) instead of listing twelve
bows, and so a new product is a dbrow + a three-line `category=` overlay with
no script edit at all. Content-allocatable: append to `pack/category.pack`
(content owns `8216=talismans`, `8217=rc_pouch`, `8218=farming_crop`), rank-1
`.obj` overlay stating only `category=` (`skill_runecraft/configs/runecraft_pouch.obj`).
Check the obj does not already carry a meaningful one first
(`grep -a -A40 '^\[<name>\]$' configs/all.obj | grep -a '^category='`).

**R3 — family × family gets ONE category binding, written inverted.** When
neither side is a hub (bolts × bolt tips, potion × potion), bind one family's
category and write the body knowing `last_useitem` is the bound item:

```
[opheldu,_bolttips]
if (oc_category(last_item) = bolts) {          // last_item is the OTHER item
    ~make_bolts(last_useitem, last_item);        // (tip, bolt)
}
~displaymessage(^dm_default);
```

Never bind both families — the second one is dead and a tripwire.

**R4 — no defensive bindings.** Do not add a reverse type binding "to be
safe" and do not add a category binding for a family whose partner is a hub.
Both are unreachable for their own purpose (§1a, §1c) and the category form
silently swallows other lanes' pairs (§3). If a category binding exists on a
family, it *is* that family's only use-on handler — it must route every pair
that family participates in, or not exist.

**R5 — hub × hub is the only order-dependent case.** `rope × mith grapple
tip`, `knife × chisel`, `hammer × …`: both type-bound, so rung 1 picks
whichever the player clicked *second*. Both hubs must route the pair. Keep the
hub set small; if an obj is only ever a target, it is not a hub.

**R6 — the trigger is a router; the skill owns the label.** The hub script
carries no levels, XP, strings or inventory ops. It `switch_category`s, falls
to `switch_obj` for genuine one-offs, and jumps to a `@label` (or `~proc`)
declared in the contributing lane's own tree. That is LostCity's rule; the only
thing we change is where the router file lives (R7).

**R7 — routers live in `general_use/scripts/tools/`.** LostCity leaves each
router in the skill that ported it, which is fine for one team on one branch.
With parallel lanes it is the file everyone edits, so it moves to the
directory that already holds `hammer.rs2` (the one router LostCity itself put
there):

```
general_use/scripts/tools/
    chisel.rs2      [opheldu,chisel]
    knife.rs2       [opheldu,knife]
    needle.rs2      [opheldu,needle]
    hammer.rs2      (moved from general_use/scripts/)
    tinderbox.rs2   [opheldu,tinderbox]
    feather.rs2     [opheldu,feather]
```

Header of every file: *"`general_use/scripts/tools/` owns the trigger; the
skill owns the label. Add a `case` line here, the body in your own tree — or,
if your objs carry a category this file already routes, add nothing here."*

**R8 — never `[opheldu,_]`.** `mock230_scripts.c:2532`: it "would swallow
every 'use A on B' in the game the moment somebody wrote one".

**R9 — a name binding beats a category silently.** Category-ifying an obj that
still carries a working name binding leaves the category case looking dead
(`name-binding-silently-kills-category`; the `farming_crop` header). Delete
the name binding in the same commit, or do not add the category.

---

## 5. Sequencing

Each slice is independently landable and one tick. Selftest green before and
after is only meaningful for S0 onward, because today's selftests bypass
dispatch.

| # | Slice | Effect | Proof |
|---|---|---|---|
| **S0** | A dispatch driver: `::useon <obj> <useobj>` debug cheat (or a second stanza next to `mock230_world.c:30915`) that builds the OPHELDU payload and calls `mock230_world_handle`, plus a `[debugproc,useonrun]` that fires the pairs each skill cares about and asserts the product landed. | Makes every claim below falsifiable | Before S1: runite bolts × onyx tips both orders → 0 tipped bolts (the defect, red). Delete `[opheldu,unstrung_shortbow]`, string a shortbow both orders → still strung (R1, green). |
| **S1** | Fix the bolts defect: delete `weapon_poison.rs2`'s five category bindings (its two hubs already cover them), rewrite `[opheldu,_bolttips]` inverted per R3, delete `[opheldu,bolt]`. Same treatment for `_arrowheads`, `_dart_tips`, `_firemaking_logs` (delete — their hubs route them). | −9 dead/wrong bindings, one real bug fixed | S0's bolt probe goes green; poison-on-bolts still works. |
| **S2** | Fletching reverse-bind cleanup: 12 unstrung bows, 8 unstrung crossbows, 8 javelin heads, 8 of stocks/limbs, 5 ballista, 3 grapple. `bows.rs2` becomes `[opheldu,bow_string]` + procs, exactly LostCity's shape minus its dead `_unstrung_bow`. | −44 triggers, ~−250 lines, zero behaviour change | S0 both-order probes for one member of each family. |
| **S3** | `general_use/scripts/tools/`: create, move `hammer.rs2`, move `[opheldu,knife]` out of `cut_logs.rs2` and `[opheldu,chisel]` out of `uncut_gem.rs2` as routers; the fletching/crafting bodies stay where they are as labels. Fold the selftest `bucket_water` probe into a `case`. | Unblocks amethyst and the nine knife consumers from editing another lane | Compile clean, S0 probes, `--selftest` unchanged. |
| **S4** | Categories for hub targets: `unstrung_bow`, `unstrung_crossbow`, `crossbow_stock`, `crossbow_limbs`, `javelin_head`, `uncut_gem`, `snelm_shell`, `unstrung_amulet`, `craft_orb`, `craft_leather`, `craft_dye`. Routers switch to `switch_category`; per-tier `case` lists go away. | Every new tier is a dbrow + overlay; ~50 of Crafting's 53 collapse to ~8 | S0 probes per family; `awk`/`grep` that no member has a live name binding (R9). |
| **S5** | Herblore (136, `brew_potion.rs2` 103) and Cooking (115): same audit — hubs `vial_water`, `pestle_and_mortar`, `knife`; unf-potion × secondary is family × family → one inverted category binding (R3). | Largest single reduction | S0 probes. |

Order matters only in that S0 precedes everything and S1 precedes S4 (an
inverted `_bolttips` is the template for R3). S2–S3 can run in parallel with
the crafting plan's slices; S4 should not, since it touches the same overlays.

---

## 6. Checklist for adding a new use-on interaction

1. Is one side a hub (R1 list, or a new tool that meets ≥3 families)? Add a
   `case` (or rely on the category) in `general_use/scripts/tools/<hub>.rs2`,
   body as `[label,…]` in your lane. Do **not** bind the target.
2. Neither side a hub? Does one family already carry a category binding? Add
   your case there, inverted (R3). If not, bind *one* family's category, inverted.
3. Both sides hubs? Add the case to both routers; note in each which order
   reaches it.
4. Run the S0 probe in both click orders. A selftest that calls the proc is
   not evidence the interaction works.
