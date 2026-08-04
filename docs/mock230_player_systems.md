# Player systems on the rev-230 mock

Four things a player does that the mock could not do before: move items around,
run, read their equipment bonuses, and turn a prayer on. They are grouped
because they turned out to share one problem — **at rev 230 the client asks the
server for permission far more often than older revisions did, and a component
the server never armed is inert no matter what the cache says about it.**

A fifth, **XP drops (§5)**, is written up here because it is the same symptom —
a panel that appears to do nothing — with an entirely unrelated cause, and
because the *wrong* diagnosis of it stood for two lanes. That correction is
worth more than the fix was.

Read [`osrs230_mockserver.md`](osrs230_mockserver.md) first for the protocol and
the tick; this is only what sits on top.

```
make -C src mock230-dev              # the server, on 43597
make -C src test-mock230-dev         # game logic, no socket
./run-live.sh manifest_osrs230_dev.ini testc test
```

`mock230-dev` builds `src/build/dev_mock230`, a second binary from the same
sources listening on 43597 instead of 43595, with `manifest_osrs230_dev.ini`
pointed at it. It exists so two sessions can run side by side. The name is
`dev_mock230` and not `mock230_dev` on purpose: the usual way to stop a stray
server is `pkill -f build/mock230`, and that pattern would take a
`build/mock230_dev` down with it.

New `::` commands, all for the headless harness (`TORIRS_NET_CHEAT=`):

| command | effect |
| --- | --- |
| `::run [0\|1]` | run toggle — the client only sets it with ctrl held |
| `::pray <0-28>` | toggle a prayer by its index in the book's button order (18 = Protect from Melee); grants the level it needs. A `[debugproc,pray]` in content, not an engine branch — see §4.1 |
| `::equipstats` | open the equipment-stats screen without walking the sidebar |
| `::xp <stat> <amount>` | grant experience inline. `[debugproc,xp]` — see §5.6 |
| `::xpdrop <amount>` | a repeating three-skill burst every 8 ticks, which is the shape a real hit produces. **Queued, not inline**, because the XP-drop listener snapshots experience when it is armed and a grant that beats login is invisible |
| `::xpqueue <amount>` | push drops through the panel's *other* input, the server-pushed varc queue, granting no experience. The only way to reach that path from this tree |

New env knobs: `TORIRS_OPS_DEBUG=1` (which script wrote which verb onto which
component), `TORIRS_STATIC_SPRITE_DEBUG=1` (which chrome sprite packs bound, and
which did not), `TORIRS_STAT_DEBUG=1` (which stat-transmit hook fired on which
serial — §5.3).

---

## 0. The thing all four have in common: `IF_SETEVENTS`

A rev-230 component's cache record carries *op labels* and a `clickMask`, and
neither of them decides whether a click reaches the server. The events mask the
server sends does. Bit 0 is "accepts a plain click"; bits 1..10 are ops 1..10.

The client half of this already existed for plain clicks and, by the time this
work landed, for numbered ops too (`app_minimenu_run_option` sends
`IF_BUTTON<n>` when `App_IfEventsGet(com) & (1 << n)`). What was missing was any
server that armed anything. The symptom is always the same and always looks
like a client bug: the hovertext reads the right verb, the menu row appears,
clicking it runs the component's local CS2 hook, and nothing happens.

So the login burst now arms what it owns:

```c
mock230_equipment_arm_worn_tab(srv);   /* 387:1 op1 — "View equipment stats" */
```

The prayer half of that arming is `[proc,prayer_login]` in content now: 29
`if_setevents` lines, one per button, beside the handlers they arm.

Everything below assumes that.

---

## 1. Inventories: moving items, equipping, taking things off

### 1.1 What was actually broken

`osrs230_mockserver.md` §5 called this "the single remaining blocker for equip
and drag through the UI", and the diagnosis there was right about the symptom
and wrong about the cause. It said the inventory needed a `UIELEM_RS_INV` grid
node. It does not. It needed the client to understand the *other* shape an item
cell comes in.

Older revisions give an inventory one `TYPE_INV` node holding a cols×rows grid:
the node is the container, the slot is a hit-test inside it, and the items live
in the client's `InvManager`. Rev 230's gameframe has no such node. Its CS2
draws the backpack as 28 `cc_create`d children of a plain layer, and the worn
tab as one child per equipment-slot layer:

```
149:0                       plain RS_LAYER
  └ 28 dynamic RS_GRAPHICs  child index 0..27 == inventory slot, item on the node

387:15 .. 387:25            one static layer per equipment slot
  └ [0] slot background
    [1] the item            <- child index 1, always
    [2] empty silhouette
```

Every one of those children is a bare dynamic graphic. The right-click builder
only classified `UIELEM_RS_INV`, so the backpack fell through to the generic
component path and produced a `PICK_UI` that dispatched a CS2 hook instead of a
packet — the "Read Bronze full helm" row in §5 of the older doc.

### 1.2 The shared resolver

[`src/ui/uitree_obj_cell.c`](../src/ui/uitree_obj_cell.c) answers "which
inventory slot is under this point" for both shapes, and both the menu builder
and the drag machine go through it. Two of them disagreeing about which slot
was pressed is exactly how this drifted before.

```c
struct UITreeObjCell {
    enum UITreeObjCellKind kind;   /* GRID or DYNAMIC          */
    int32_t node_index;            /* the node holding the item */
    int32_t ops_node_index;        /* whose verbs it borrows    */
    int component_id;              /* the uid the wire names    */
    int slot;                      /* the sub id beside it      */
    int inv_source_id;             /* GRID: InvManager source   */
    int obj_id, obj_count;         /* DYNAMIC: on the node      */
    int can_drag, obj_ops, obj_use;
};
```

A dynamic node is an item cell when `node->dynamic && node->item_id > 0`.
`item_id` is what `CC_SETOBJECT` writes, and it is the only thing separating an
item from the chrome graphics around it.

**The uid is the parent's, not the child's.** rsprot's `If3Button` carries
`combinedId` (a static component) and `sub` (a dynamic child index) — see
OpenRune's `IfButton1Handler`/`IfButtonDHandler`, which read them as exactly
that. So the backpack sends `149:0` + slot 0..27, and a worn slot sends
`387:15..25` + sub 1. Which equipment slot came off is in the **component**;
`sub` is 1 for all eleven of them.

One collateral fix in `uitree_input.c`: `UITree_CollectNodesAt` dropped these
children as pass-through chrome, because they carry no ops and no hook of their
own. A component holding an obj is now a menu target on the strength of the obj
alone. Without it the backpack worked (its children happen to carry a stray op)
and the worn tab did not.

### 1.3 Where the verbs come from

Three containers, three different answers, and the client has to get all three
right at once:

| container | what the paint script writes | what the menu should be |
| --- | --- | --- |
| 149:0 backpack | `op4="Read"` on **every child**, whatever is in it | Wear/Eat/Drop from the ObjType, Use, Examine |
| 387:15 worn head | `op1="Remove"` on the **slot layer** | Remove, Examine |
| 12:x bank | real per-item rows on the **child** (`Withdraw-1/5/10/X/All`) | those, Examine |

(All three verified with `TORIRS_OPS_DEBUG=1`.)

So *which node* carries the strings is not the discriminator — the bank and the
backpack both put them on the child, and only one of them means it. What
separates them is the server: **a CS2 cell's verbs are offered only where
`IF_SETEVENTS` armed that op.** The bank arms its items component; the
gameframe's backpack is never armed, which is exactly why the stray "Read" is
invisible in the real client too. `add_obj_cell_rows` masks the container's ops
against `App_IfEventsGet(cell->component_id)` and falls back to the ObjType's
own verbs when nothing survives.

That also fixed the login burst, which used to arm the backpack and the worn
container with ops 1..9 wholesale "so clicks are not swallowed". They now get
the drag bits only, and the worn tab's real "Remove" is armed on the eleven slot
components where it actually lives.

Two rules that fall out and are worth keeping:

- A live component op **replaces** the ObjType rows rather than joining them —
  a bank item offers Withdraw, not Withdraw *and* Drop.
- The container stops emitting its own generic rows once a cell has claimed
  them, or the worn tab offers "Remove" twice: once as an inventory op and once
  as a plain CS2 button.

`TORIRS_MINIMENU_DEBUG=1` prints the deciding line per cell:

```
objcell: com=149|0 events=0x120000 ops_node=4481   <- drag bits only, ObjType verbs win
objcell: com=12|13 events=0x1203ff ops_node=5120   <- ops armed, the bank's own rows win
```

### 1.4 The wire got wider

`OPHELD*`, `INV_BUTTON*`, `OPHELDT`, `OPHELDU` and `INV_BUTTOND` all wrote the
component as `p2`. At rev 230 a component uid is `(interface << 16) | child`, so
two bytes kept the child and threw the interface away: `149:0` arrived as `0`,
and the mock's "is this the worn tab" test could never fire. They now use
`GameProtoRevTable.component_id_bytes` like every other packet that names a
component, and the rev-230 size table grew to match (`OPHELD*` 6→8,
`INV_BUTTOND` 7→9, …).

### 1.5 Order of operations on a click

An inventory op is the server's to answer, so `app_minimenu_inv_action` now runs
*before* the hook is resolved rather than as the fallback for "this component
has no hook". That held only for the backpack. Rev 230's worn slots do carry an
`onop` hook beside their "Remove" verb, and resolving it first meant clicking
Remove ran a script, sent nothing, and left the helmet on.

### 1.6 Server side

- `mock230_equipment_worn_slot()` maps `387:15..25` to a wear slot, reading the
  `worn_slots` **.enum** (`player/configs/worn.enum`) rather than a C table —
  the component's gameval name *is* the answer, `wornitems:slot7` being wear
  slot 7, which is also why the eleven are not a straight run. It is
  OpenRune's `enums.equipment_tab_to_slots_map` transcribed, cross-checked
  against the cache's own silhouette graphics (387:15 shows sprite 156, a
  helmet; 387:25 shows 166, an arrow).
- `INV_BUTTOND` refuses any component but `149:0`. A real rev-230 `IfButtonD`
  names both ends so an item can be dragged between two interfaces; the mock
  only moves within the backpack, and says so rather than guessing.
- Picking an obj up now stacks it onto a pile of the same obj when the cache
  calls it stackable. Without that a full backpack refuses a single coin while
  holding 15,000 of them two slots over.

### 1.7 Verified

Against the real client under `SDL_VIDEODRIVER=dummy`:

```
right-click backpack slot 0  ->  Wear / Use / Drop / Examine, kind=PICK_INV_SLOT, com=149:0
left-click "Wear"            ->  OPHELD2 obj=1155 slot=0 com=149|0, helm leaves the backpack
drag slot 0 -> slot 20       ->  INV_BUTTOND com=149|0 0 -> 20
right-click worn head slot   ->  Remove / Examine  (one Remove, not two)
left-click "Remove"          ->  OPHELD1 slot=1 com=387|15, helm returns to the backpack
```

---

## 2. Running

### 2.1 The model

Energy is kept in hundredths of a percent (`MOCK230_RUN_ENERGY_MAX = 10000`)
because that is the unit OldSchool's drain formula is written in — one running
step off an unencumbered player costs 67 of them, and a percent is not fine
enough to hold the remainder. The wire and the orb carry the percent.

```
drain per running step = 67 + 67 * min(64, weight_kg) / 64
regen per non-running tick = agility / 6 + 8
```

which is xrsps's `MovementService.computeRunEnergyDrainUnits` /
`computeRunEnergyRegenUnits`, i.e. OldSchool's. Unencumbered, that is a hair
over 74 running steps from full; fully laden, about half. Energy is spent per
**step**, not per tick — a running tick covers two tiles and costs twice a
walking one.

Weight comes out of the obj records themselves (config opcode 75, in grams), so
there is no table: a cape weighs what OldSchool says a cape weighs.
`mock230_objinfo` now carries `weight` beside the wearpos fields, from the same
single decode pass.

### 2.2 Toggle, and why it is not a mirror of the packet flag

`running` is derived per tick in `advance_player` from `run_toggle && energy > 0`,
not latched when the move request arrives — that is what makes the energy run
out mid-walk instead of at the start of the next route.

Ctrl held on a move request turns run mode **on** and leaves it on. It is
deliberately not assigned from the flag: the client sends 0 on every ordinary
click, so mirroring it meant the next plain click silently switched running back
off. (This was a real bug during development — the `::run 1` cheat worked and
then the very next walk undid it.)

Hitting zero clears the toggle rather than merely refusing to run, so the orb
goes dark instead of staying lit over a player who is plainly walking. Varp 173
carries the toggle to the client; it is declared `transmit=yes` in
`content/scripts/player/configs/player_controls.varp`, like any other varp the
client's own CS2 reads.

### 2.3 The orb and the Controls-tab run toggle

`UPDATE_RUNENERGY` and `UPDATE_RUNWEIGHT` go out only when the value changed —
at one packet a tick each they would be a third of everything the mock sends —
and are flushed after `PLAYER_INFO` but before the container deltas, so weight
never lags a tick behind the item that changed it.

Two writers share `%option_run`: the minimap orb (`[if_button,orbs:runbutton]`)
and the Controls side panel (`[if_button,settings_side:runmode]` on interface
116). Both client CS2s flip var 173 locally; both need `if_setevents` + a
server `%option_run` write or the icon moves and the player still walks.
`~settings_side_login` arms the Controls button the same way `~orbs_login`
arms the orb.

Two client-side gaps had to be closed for any of it to be visible:

- **`RUNENERGY_VISIBLE` (3321) had no handler.** It was in the opcode metadata
  table, so it fell to the stack-meta stub and pushed 0 — the orb read "0" no
  matter what the packet said. `RUNWEIGHT_VISIBLE` (3322) had a handler that
  hardcoded 0. Both now go through a host request to
  `RS_PlayerStats.run_energy` / `.run_weight`.
- **`UPDATE_RUNWEIGHT` carries kilograms, not grams.** The value is read back by
  `RUNWEIGHT_VISIBLE` and the gameframe prints it with "kg" after it, so sending
  grams put "31892 kg" beside a player carrying 32.

### 2.4 Verified

`make -C src test-mock230-dev` covers the drain arithmetic against the starting
kit's real weight, the regen clamp at full, and that running out clears both the
toggle and varp 173. The same selftest drives `IF_BUTTON1` against both
`orbs:runbutton` and `settings_side:runmode` and asserts `run_toggle` follows.
In the client, `TORIRS_NET_CHEAT="run 1"` plus a world click lights the orb, and
the percent falls and recovers.

---

## 3. The equipment-stats screen

Interface **84**, reached from "View equipment stats" (387:1) on the worn tab.

Nothing about it is client-side *except the figure* — 84:4 is a MODEL widget
with `clientCode = 328`, which names the local player, and the client composites
it from the appearance it already has. See
[`equipment_if3_rendering.md`](equipment_if3_rendering.md#the-equipment-stats-figure-844-clientcode-328);
it used to draw a frozen default avatar.

Everything else on the screen is the server's. OldSchool's client draws eighteen
empty text components and waits, which is why the screen looks broken rather
than empty when a server forgets. The component ids were read out of the cache directly:

```
tools/dump_interface/dump_interface cache.osrs230 --iface 84
```

Each row is the empty string under a heading the cache ships ("Attack bonus" at
84:23, "Defence bonus" at 84:29, …), so the ids are not guessed. They agree with
OpenRune's `enums.equipment_stats_to_slots_map` and xrsps's
`EquipmentStatsUiService`, which is the cross-check that this is the modern
layout and not a coincidence.

| rows | components |
| --- | --- |
| attack: stab, slash, crush / magic, ranged | 24, 25, 26 / 27, 28 |
| defence: stab, slash, crush / magic, ranged | 30, 31, 32 / 33, 34 |
| melee str, ranged str, magic dmg, prayer | 36, 37, 38, 39 |
| undead, slayer | 41, 42 |
| weapon speed base, current | 53, 54 |

Column order is load-bearing: 24/25/26 sit at x=343 and 27/28 at x=424, so the
five attack rows read Stab/Slash/Crush down the left and Magic/Ranged down the
right — not five in a column.

**The screen is content, not C.**
`server/scripts/interface_equipment/scripts/equipment.rs2` owns the mount, the
eighteen component names, the labels, the "+0" convention, the column order and
the tick-to-seconds conversion — every one of those being a wording or a layout
decision. The engine's remaining share is three functions in
`mock230_equipment.c`, none of which names a component: when to repaint, the way
in for the `::equipstats` cheat, and the wearability gate.

That move deleted a second sum over the worn container. `mock230_equipment_bonus`
existed only for this screen, beside the one the fight already rolls against;
`~equipment_refresh` now calls `~equip_get_bonuses` (skill_combat/combat_stats.rs2),
so there is one, and the screen cannot disagree with the fight. The weapon speed
is the weapon's own `attackrate` param, likewise.

The screen refreshes on any tick that dirtied the worn container, not the next
time it is opened — a bonus screen still showing the sword you just took off is
worse than one showing nothing. The gate is `mainmodal_group`, the record the
IF_OPENSUB / IF_CLOSESUB encoders keep of what is mounted, rather than a flag of
the screen's own: the old `equip_stats_open` bool had three places clearing it
and one setting it.

The mount, the eighteen rows, the "+0" wording and the gate are asserted in the
`the equipment screen is content's` section of `--selftest`, off the captured
wire.

### Honest gaps

- **Magic damage, undead and slayer read +0.** Magic damage is cache param 300
  and a percentage of unverified scale; the target multipliers need per-item
  unlock data the mock has none of. OpenRune's own screen prints "TODO" for all
  three.
- **Ranged strength now reads for real.** It is cache param 189, outside the
  contiguous 0..11 bonus block `mock230_objinfo` projects into `bonus[]` — which
  is why it printed +0 while the screen summed that array. `oc_param` reads the
  general per-record param table and has no such limit, and
  `~equip_get_bonuses` was already summing `rangebonus` for the combat block, so
  the number was in hand the whole time.
- **No side panel.** OpenRune also mounts interface 85 in the side slot. 85 is
  one bare 162×248 layer: the backpack appears in it only because the server
  runs the client's `interface_inv_init` script at it, and that is a
  `RUNCLIENTSCRIPT` with five *string* arguments for the op labels. The mock's
  sender takes ints only, so mounting 85 today would replace the sidebar with an
  empty panel. Left out until the string form exists.

---

## 4. Overhead prayers

### 4.1 Prayer state

29 prayers, in the prayer book's own button order — which is by level. Verified:
the gameframe's CS2 sets `op1="Activate"` on exactly those 29 components and no
others. The book has thirty buttons and this revision fills twenty-nine, so each
prayer **names** the component it sits on rather than deriving it from an index.

The table is content, and it is an ordinary dbtable:
`skill_prayer/configs/prayers.dbtable` and `prayers.dbrow`, which is the
reference's own schema (LostCity's `prayers.dbtable` + `prayers.dbrow`). **No C
reads it.** A prayer's index is its `^prayer_*` constant, its position in the
book counting from zero, and it is only a row key — nothing packs these into a
mask any more.

It was a bespoke `.prayer` grammar the engine parsed, which existed only because
nothing could read a `.dbrow` yet (`mock230_db.h`); when that stopped being true
the grammar, the parser and `mock230_prayer.{c,h}` all went. What is left in the
engine is `db_find`/`db_getfield`, which know nothing about prayer.

Levels and drain rates are OldSchool's, transcribed from xrsps's
`src/rs/prayer/prayers.ts` (the 1/6/12/24 pattern is not a smooth curve).
Conflicts are a `group` LIST rather than a group id, because the combat prayers
(Chivalry, Piety, Rigour, Augury) belong to several groups at once — Piety
claims attack, strength and defence, which is what stops it stacking with
Ultimate Strength.

Drain follows xrsps's `PrayerSystem`: rates accumulate per tick against a
resistance of `60 + 2 × prayer bonus`, and each time the accumulator clears the
resistance one prayer point goes. That is the one place the equipment screen's
"Prayer: +N" has a mechanical effect. The level gate reads the **base** level,
not the boosted one — a prayer potion raises the points you have, never the
prayers you may use.

Running out clears every prayer, and so does death; otherwise the overhead icon
follows the corpse back to Lumbridge.

### 4.2 The icon is part of the appearance

There is no overhead-icon packet. It is one byte of the player's appearance
block, so turning a protection prayer on is an appearance change like putting on
a helmet, and everyone who can see the player learns about it through the
`PLAYER_INFO` they were going to get anyway. `headicons_set` — the one opcode
the engine offers here, and it knows nothing about prayer — sets
`MOCK230_PMASK_APPEARANCE`, and that is the whole of the delivery.

**One deviation.** A real rev-230 appearance has two separate one-byte fields
here — a prayer icon index and a PK-skull index, each 255 for "none". This
client reads one byte and treats it as a *bitmask* over the `headicons` sprite
pack (`app_overlay_build_player_headicons` plots every set bit, stacked upward),
which is the older shape. The mask is what goes on the wire because this client
is the only consumer. Bit position is the sprite index: 0 protect-melee,
1 protect-missiles, 2 protect-magic, 3 retribution, 4 smite, 5 redemption —
written down once, in `skill_prayer/configs/prayers.constant`, because the
client reads the index and the server writes the bit and they have to be the
same number. LostCity keeps the same list in `player/configs/headicon.constant`
for its own revision.

### 4.3 The client-side bug this uncovered

The overhead pass asked the scene bridge for `STATIC_SPRITE_HEADICONS`, whose
dat2 archive name is `headicons`. **Rev 230 has no such archive.** OldSchool
split the pack into `headicons_prayer`, `headicons_pk` and `headicons_hint`, all
three of which bind fine and none of which anything asked for. The slot stayed
-1, the guard `headicons_scene <= 0` returned early, and the whole feature was
silently dead — mask arriving, nothing drawn.

`app_build_entity_overlays` now falls back to `STATIC_SPRITE_HEADICONS_PRAYER`.
The prayer icons keep their indices across the split, so it is a drop-in.

`TORIRS_STATIC_SPRITE_DEBUG=1` prints the binding table, which is how this was
found and is the fastest way to find the next one:

```
static_sprite: 'headicons' unresolved
static_sprite: 'headicons_prayer' sprite=440 scene=5
```

### 4.4 Effect in combat

Protection is applied to the **damage**, after the accuracy roll — OldSchool's
order, and why a protected hit still splashes a blue 0 rather than not landing.
Against monsters the reduction is total; the 40% PvP figure has no meaning here
because nothing but npcs attacks. Every npc in the mock is a melee attacker, so
protect-from-melee is the only one with an effect today; the lookup is by damage
type so that stops being true the moment a ranged npc exists.

### 4.5 Prayerbook button bindings

**Fixed 2026-08-03.** Twenty-four of the twenty-nine `[if_button,prayerbook:prayerN]`
handlers in `prayer.rs2` used to toggle a different prayer than the button the
player clicked. The cause is that `prayerbook.compack`'s `prayer1..prayer29` are
numbered by **component id**, and the book's on-screen (level) order that
`^prayer_*` uses is not. The cache states which component belongs to which
prayer, on the obj, as **`param_1751`** — and it is not `8 + book_index`:

| the player clicks | component | compack calls it | handler toggles (now) |
|---|---:|---|---|
| Thick Skin | 541:9 | `prayer1` | Thick Skin ✓ |
| Rock Skin | 541:12 | `prayer4` | Rock Skin ✓ (was Sharp Eye) |
| Sharp Eye | 541:27 | `prayer19` | Sharp Eye ✓ (was Protect from Melee) |
| Preserve | 541:37 | `prayer29` | Preserve ✓ (was Augury) |

Five already agreed by coincidence: `prayer1`, `prayer2`, `prayer3`,
`prayer26` (Chivalry) and `prayer27` (Piety). Measured three ways that agree
exactly: `param_1751`/`param_1752` on the prayer objs in `configs/all.obj`; the
`prayer_*` `startbit`s on `basevar=prayer0` in `configs/all.varbit`; and the
client itself — lighting bit 3 lights the **sixth** slot in the book, which is
Rock Skin's position, not the fourth.

**Choice taken: re-point the 24 headers**, leave `^prayer_*` as book/level order.
Renumbering the constants to cache/component order instead would also have
worked, but would have forced `::pray N` and every dbrow identity through the
same shift, and (because a few component indices still disagree with varbit
bits for the ranged/magic cluster — e.g. `prayer20` is Hawk Eye bit **20**, not
19) would not have deleted §4.7's `bit` column cleanly. Doing both translations
independently produces a silently wrong prayer on every activate. Quick prayers
(§4.7) does **not** go through these bindings and was already correct via `bit`.

LostCity puts the same handlers in content (`skill_prayer/scripts/prayer.rs2`)
but names each interface component after the prayer (`prayer_thickskin`, …), so
this mismatch cannot arise there — rev 254 has 15 prayers and no interleaved
modern book.

The selftest pins both ends: `prayerbook:prayer4 == 541:12`, and `IF_BUTTON1` on
that component lights `%prayer_rockskin` rather than `%prayer_sharpeye`.

### 4.6 Verified

`make -C src test-mock230-dev` covers the level gate, group replacement, the
drain arithmetic (12 units a tick against 60 = a point every fifth tick), and
that running out clears both the prayers and the icon mask. In the client,
`TORIRS_NET_CHEAT="pray 18"` draws the Protect from Melee icon above the
player's head. The binding table is pinned past the third: `prayerbook:prayer4`
is `541:12`, and clicking it lights Rock Skin (bit 3), not Sharp Eye.

### 4.7 Quick prayers

The orb (`orbs:prayerbutton`, 160:20) and its setup panel, interface **77**
`quickprayer`. `skill_prayer/scripts/quickprayer.rs2`, one new engine opcode,
two varp declarations and one column.

**No LostCity reference exists.** `grep -rli quickpray` over the whole of the
reference — engine, content, data, both bundled clients — returns nothing, and
could not return anything: rev 254 has no minimap orbs. This is
`PORTING_GUIDE.md` §5 in its pure form.

**The client half was already complete.** Clientscript 466 (the panel's
`onload`) reads enum **4956**, `cc_create`s one clickable slot per prayer at
sub-id = the prayer's cache index, gives each `cc_setop(1, "Toggle")`, and hangs
`quickprayer_button_op` (469) on it. 469 flips bit `oc_param($obj, param_630)`
of varbit **4102** locally; 7556 flips varbit **4103** for the orb. Both writes
are optimistic — the server's is the true one, which is why both carrier varps
are `transmit=yes`.

Five things the server had to supply:

1. **`if_setevents` for three components.** Nothing at rev 230 is clickable
   until the server says so. `orbs:prayerbutton` needs **two** ops armed (op 1
   `*` activate, op 2 `Setup`), which is `^quickprayer_orb_events`;
   `quickprayer:buttons` is armed as a **range 0..28**, because its slots are
   `cc_create`d children and a dynamic child's events come from its parent's
   `(from..to)`.
2. **The mount, and the slot is not the obvious one.** Interface 77 carries no
   `xmode`/`ymode`, unlike every panel this tree puts in `mainmodal`, so it is
   anchored to its slot's **top-left**. In `mainmodal` it draws in the corner of
   the world view — measured, screenshotted, and how this was settled. It is
   190x261, which is the **sidebar's** exact size and the prayer book's, so it
   goes into `toplevel_osrs_stretch:sidemodal` with type 3.
3. **`if_closesub(component)`** — a new opcode, **11005**. The panel's "Done"
   button has no client close at all (clientscript 472 swaps a graphic and sets
   a timer), and this server's `SS_OP_IF_CLOSE` is specialised to the chatbox
   modal, which is what every `[if_close]` caller in the tree means by it. The
   modal-slot bookkeeping happens inside the IF_OPENSUB/IF_CLOSESUB encoder, so
   the X and Escape kept working for free.
4. **Two varp declarations.** `prayer1` (varp 84, sole tenant
   `quickprayer_selected` bits 0..28) `scope=perm`; `armourhitsound` (varp 375,
   bit 0 `quickprayer_active`, **ten other tenants**) `scope=temp`. Both names
   are the cache's verbatim — an authored name replaces the cache's rather than
   adding to it, the lesson `combat_tab.varp` carries at its own head.
5. **A `bit` column on `prayer_table`,** because the client speaks cache prayer
   indices and this tree speaks `^prayer_*`, and **they agree for only five of
   the twenty-nine: 0, 1, 2, 25 and 26.** The cache orders the 29 prayers by the date they entered the game
   (the Ranged and Magic prayers and the four combination prayers appended, not
   interleaved); `^prayer_*` orders them the way the book is laid out. Feeding a
   mask bit straight into `~prayer_toggle` toggles the wrong prayer for **24 of
   29**, plausibly. This is the same disagreement §4.5 is about, seen from the
   other side.

Everything else is `~prayer_toggle`'s: the level gate, the points gate, the
exclusion groups, the drain accumulator and the overhead icon. `~quickprayer_activate`
deactivates everything and then calls it once per selected bit.

**Verified in the client** (headless, SDL dummy, against a live mock230):

| what | how | result |
|---|---|---|
| orb armed, both ops | right-click 160:20 | menu reads *Activate / Setup / Cancel* |
| panel opens where it belongs | click Setup | 29 slots + Done, filling the sidebar |
| panel opens under Fixed (548) | Display → Fixed, then Setup | mounts into live `toplevel:sidemodal` (engine rewrites the stretch role alias); no `if-opensub: target 0x00a1004a … skip` |
| a slot is armed and addressed | click | `IF_BUTTON1 77:4 sub=3` |
| **the bit→prayer map is the cache's** | prayer level **9**, click sub 3 | *"You need a Prayer level of 10 to use Rock Skin."* — the naive `^prayer_*` map makes bit 3 Sharp Eye, level 8, and accepts silently |
| the same, other direction | level 9, click sub 18 and 19 | accepted (Sharp Eye 8, Mystic Will 9); the naive map makes 18 Protect from Melee, level 43, and refuses |
| Done closes | click | `-> op=36` (IF_CLOSESUB) |
| activate | left-click the orb | both selected prayers glow in the book; the orb's star turns **gold** |
| deactivate | left-click again | book empty; star **white** |
| the selection survives a relog | reconnect, reopen the panel | red ticks on exactly the two chosen slots |
| reopen after close | close → reopen | renders again, no slot poison |

The level-9 row is the one that matters: it is the mutation that makes the
assertion capable of failing, and it fails loudly under the wrong mapping in
both directions.

**What it cost, beyond the plan.** One runtime bug the compiler cannot see:
`mes("… <db_getfield($data, prayer_table:name, 0)>")` written inline aborts with
*"int stack underflow"*, because `db_getfield` returns `any` — a string column
read inline pushes the string stack while `TOSTRING` pops the int one. It only
executes on the refusal branch, so it survived compilation and the happy path.
Typed locals first, exactly as `~prayer_checks` already did it.

**Still open:** the orb un-lights when a prayer is switched off by hand
(`~prayer_toggle`'s off branch clears `%quickprayer_active`), which is what OSRS
does, but nothing re-lights it if the player turns the same set back on
manually. And a *conflicting* selection — Thick Skin and Rock Skin, both
`^prayer_group_defence` — activates as OSRS does, later wins, which reads as
"one of my quick prayers didn't come on" and is correct.

---

## 5. XP drops, and a diagnosis that stood for two lanes

The XP-drop panel is **interface 122**, mounted into the gameframe's clientCode
**1354** (`CONTENT_XP_DROPS`) slot. It is not a builtin: 1354 is an empty layer
in gameframes 80/161/164/548/601, and 122 is an ordinary cache interface that
mounts there. Nothing about it is server-drawn.

**What it cost: two lanes, both of which worked on the wrong thing**, because
the blocker had been written down in `PORTING_GUIDE.md` §5.2 as a finding when
it was only ever a hypothesis. The fix, once the measurement was taken, is four
characters.

### 5.1 The panel's two inputs

Both live in the cache, and only one of them is on the path a mock xp gain takes.

| input | armed by | entered as | who writes it |
| --- | --- | --- | --- |
| **stat transmit** | `script1003`'s `if_setonstattransmit` on `122:2` | `script1004(0, …)` | the client, by diffing `stat_xp(stat_N)` against baselines it was handed |
| **varc queue** | `script1003`'s `if_setontimer` | `script1004(1, …)` | `script2091`, a `runclientscript` target — the **server-pushed** path, which nothing in this tree sends |

`script993` is the onload; it initialises varcints 953..966 to `-1`. 953..959
are a seven-deep queue of stat ids, 960..966 the paired xp amounts.

Structure, from `TORIRS_DUMP_TREE_EXIT=1`: `122:3..10` is the XP-counter plaque
(graphics 297 + 222, text `122:10`); `122:17` → `122:18..24` are **seven drop
rows**, each given one `cc_create`d text child plus up to five skill-icon
graphics by the onload.

### 5.2 What §5.2 of the porting guide claimed, and why all four clauses are false

> "XP drops — blocked on one client bug… the remaining work is the
> non-terminating varc queue-shift loop in script 1004 (`xpdrops_stattransmit`,
> varcs 953..966) — plus re-arming the listener… **Fix the VM loop, not the
> server.**"

Decompiled (`3rd/rscache/tools/cs2/cs2 decompile --cache cache.osrs239 --rev
osrs239 1004`) and traced, none of it holds:

- **The loop terminates, and is bounded at 7 by construction.** Its induction
  variable is `%varcint953`, not the output counter `$int36` — `$int36` counts
  *accepted* drops and is incremented inside a filter, which is what makes it
  look unbounded to a reader. Each pass shifts 953..959 and 960..966 one slot
  left with a seven-wide parallel assignment and feeds `-1` into the tail, so
  953 reaches `-1` in at most seven passes whatever the payload is.
- **A stat transmit never enters it.** The loop is under `if ($int0 = 1)` —
  the *timer* entry. A stat transmit is `$int0 = 0` and takes the `else`.
- **Nothing populates that queue anyway.** Its only writer is `script2091`, and
  nothing in the tree sent it until `::xpqueue` was written to (§5.6).
- **`TORIRS_XP_DROPS` never existed.** `grep -rn TORIRS_XP_DROPS src/` is empty
  and always was; the gate named in §5.2 was never written. Registration of
  `IF_SETONSTATTRANSMIT` is unconditional.
- **Re-arming was already done.** The long comment at `task_cs2_run.c`'s
  `self->hook_count = self->host->stat_transmit_hook_count;` describes the
  snapshot that *fixed* a dispatch-loop hang, not outstanding work. Its phrase
  *"the client hangs: no crash, no error, just a frame that never completes"* is
  the likely origin of the whole story — a **dispatch**-loop hang, long since
  fixed, re-attributed in prose to a **script** loop.

Measured against the VM rather than argued: over one traced run, script 1004 was
entered 1026 times and returned 1026 times, and **the loop body was entered
zero times** (the `$int0 = 1 & %varcint953 = -1` guard returns after seven
opcodes). Driving the queue with `::xpqueue`: 554 invocations, 554 returns, two
loop tests, one body pass, exit 0. Across 1004/1003/1005/1006 the trace contains
**zero `_unknown` opcodes and zero `result=error`**.

The four traps this project has recorded in exactly this area were each checked
and each is absent:

| trap | status here |
| --- | --- |
| an unimplemented opcode is a silent no-op, so a counter never reaches its bound | not present — zero `_unknown`, zero `result=error`; `CC_DELETE` (the one recorded as missing) is implemented and running in `script1006` |
| rev-239 arrays are handles held in string locals | handled — `DEFINE_ARRAY` operands `83` and `65641`; `'S'`(83) ≠ `'s'`(115) at the `cs2vm2.c` test, so the stat array is correctly an int array, two distinct handles |
| `POP_ARRAY_INT` popped index and value backwards, invisible on symmetric fills | already fixed, and 1004's writes are maximally asymmetric — it would be loud here, not invisible |
| `CS2VM_MAX_FRAMES` had to be 50 | now 128 (raised for the rev-239 spellbook sort's depth-70 recursion); 1004's deepest chain is two frames |

Also checked, because it is the trap that killed the quest tab: the hook carries
**35 int arguments** against `CS2VM_SETON_INT_ARG_MAX = 64`. No truncation.

### 5.3 The actual defect: the pump's guard

`RS_CS2_PumpTransmits` (`src/game/rs_cs2_dispatch.c`) had two enumerations of
the same set of dirty flags — an early-return guard at the top and a clear-down
at the bottom — and they had drifted:

```c
/* before */
if( !host->widgets_loaded_dirty && !host->var_transmit_dirty &&
    !host->inv_transmit_dirty && !host->misc_transmit_dirty &&
    !host->friend_transmit_dirty )
    return;                          /* stat_transmit_dirty is missing */
```

`stat_transmit_dirty` **was** in the clear-down and was never a guard key. The
stat branch landed after the guard was written; `misc` and `friend` were each
appended later, each appending only itself. So a tick carrying only
`UPDATE_STAT` — no varp, no container, no run-energy change, which is exactly
what an xp gain is — took the early return, **and kept the flag**, because the
clear-down is past the return. It sat set until something unrelated opened the
guard, then fired once for everything behind it.

Measured with temporary instrumentation at the guard (since reverted), typing
`::setlevel 0 50` at frame 400: **447 consecutive ticks** held `stat_transmit_dirty`
at the guard on serial 25, and the dispatch that eventually ran came on serial
26 — two stat changes delivered as one hook call. With the fix, serial 25 gets
its own dispatch on the tick it arrives, and the run's total stat dispatches
went 14 → 13. No storm.

The var dispatch survives the same guard only by accident: it is created
*unconditionally* below it, so any flag opening the guard runs it. The stat
dispatch sits inside its own `if( host->stat_transmit_dirty )`.

### 5.4 It was never "draws nothing" — it was *merged*

This is the part that made the diagnosis go wrong, and it is the transferable
lesson. Same content, same cheat (`::xpdrop 12000` = 1,200 per skill per burst,
one burst per 8 ticks), reading the **exit BMP** — not a tree dump — at three
frames:

| frame | old guard | fixed guard |
| --- | --- | --- |
| 830 | empty | `1,200` ×3 |
| 845 | empty | `1,200` ×3 |
| 860 | **`3,600` ×3** — three bursts collapsed into one row | `1,200` ×3 |

Script-1004 dispatch serials for the same runs (`TORIRS_STAT_DEBUG=1`, hook 5,
`com=0x7a0002` = interface 122 child 2):

```
old guard:  15, 28, 31,     37
fixed:      15, 28, 31, 34, 37      <- serial 34 was swallowed into 37
```

The panel is empty most of the time and occasionally shows a lump of the
accumulated total. **Sampling one frame — which is what a headless harness does
— lands in a starved gap and reads as "the panel draws nothing."** An earlier
lane's frame-845 observation of "all seven rows `hidden=1`, `text=""`" was real;
it was a gap, not a dead panel. Meanwhile the *counter* kept updating in both
builds, so the panel looked half-alive and the drops looked like a render bug.

Why merged rather than lost: `script1003`'s `if_setonstattransmit` carries **no
`{...}` trigger list**, so `trigger_count == 0` and it matches any stat change;
the twenty-four `stat_xp(stat_N)` terms in the hook string are *arguments* — the
baselines captured at arm time — not triggers. `script1004` re-arms itself with
fresh baselines every run, so a delayed dispatch cannot drop experience, only
sum it into the next row at the wrong moment. (The dispatch comment that called
them triggers was wrong and is corrected in place.)

### 5.5 The fix, and the gate

The guard is no longer hand-written. One table, `rs_cs2_dirty_flags()`, feeds
both the guard (`RS_CS2_TransmitsPending`) and — via
`assert(!RS_CS2_TransmitsPending(host))` at function exit — the clear-down. A
flag added to the table becomes a guard key automatically; a flag that gains a
guard key but no clear-down now aborts instead of re-dispatching forever, which
is the dual of the original bug.

```c
/* after */
if( !RS_CS2_TransmitsPending(host) )
    return;
```

**`make -C src test-cs2-transmit-pump`** (`src/game/test/rs_cs2_transmit_pump_test.c`,
six files, no cache and no server). It sets each dirty flag **alone** — the only
shape in which this bug is visible, since any second flag opens the guard and
everything downstream looks correct — and asserts that it opens the guard, is
consumed, and queues a dispatch. The stat case is driven through the real
`RS_CS2Host_NotifyStatChanged`. The flag count is pinned, so a seventh flag
fails the test until a case is added for it.

Proven to fail by mutation, three ways — a test that cannot fail is not a gate:

| mutation | result |
| --- | --- |
| restore the literal pre-fix guard | 5 failures, all on the stat path |
| drop `stat_transmit_dirty` from the table | 7 failures, including the pinned count |
| drop `stat_transmit_dirty` from the clear-down | abort: `Assertion failed: (!RS_CS2_TransmitsPending(host))` |

### 5.6 The cheats, and why two of them queue

`server/scripts/general/scripts/misc/cheat_xp.rs2` — three `[debugproc]`s, the
same shape as `::pray` (§4.1), content and not engine.

`::xp <stat> <amount>` grants inline. `::xpdrop` and `::xpqueue` both **queue**
with an 8-tick delay instead, for a reason specific to this panel: the listener
snapshots each skill's experience *when it is armed*, and the onload zeroes the
varc queue, so anything that lands before login settles is either invisible or
wiped. `::xpdrop` hits three combat skills at once because the panel merges
simultaneous drops into one row and a single skill would not exercise that.

`::xpqueue` exists only to reach the timer path — it sends
`runclientscript(2091, <stat>, <xp>)` three times, granting nothing. The
clientscript id lives in content (`configs/cheat_xp.constant`,
`^clientscript_xpdrop_enqueue = 2091`), not in C. **This is what made §5.2's
loop testable at all**, and it is how the loop was shown to terminate rather
than argued to.

### 5.7 Deliberately not done

- **No server packet and no ServerScript opcode was added.** The instruction was
  right on this one point even though its diagnosis was not: this was a client
  bug, and the server's `RS_CS2Host_NotifyStatChanged` on `UPDATE_STAT` was
  correct throughout.
- **The var and inv dispatches still re-read their hook bound inside the loop
  condition** (`Task_CS2VarTransmitDispatch_Run`, `task_cs2_run.c`). Only the
  stat dispatch snapshots it. That is the hang the stat path already had; it is
  latent for the other two and was left alone rather than changed unmeasured.
- **`CC_SETONSTATTRANSMIT` (1415) is still parse-and-discard** in `cs2vm2.c`,
  alongside `CC_SETONRELEASE` — the exact shape of the bug that was fixed for
  the `IF_` form. Three cache scripts use it (`5724`, `6902`, `9680`); none is
  on the XP-drop path, which uses `if_setonstattransmit`.
- **The blank-panel ladder was not run.** The panel was never blank; see
  `REV230_UI_BLANK_PANELS.md` §1's step 0, added because of this.

---

## 6. Where things live

| file | what |
| --- | --- |
| `src/ui/uitree_obj_cell.{c,h}` | item-cell resolution for both tree shapes |
| `src/ui/uitree_input.c` | obj-bearing components are menu targets |
| `src/game/rs_minimenu_build.c` | one row builder for both shapes |
| `src/app.c` | drag machine, click order, headicons fallback |
| `src/net/net_out.c`, `src/net/rev/osrs230/packetout.h` | component uid at the revision's width |
| `src/cs2vm2/cs2vm2.c`, `src/game/rs_cs2_host.c` | `RUNENERGY_VISIBLE` / `RUNWEIGHT_VISIBLE` |
| `src/net/mock/mock230_equipment.{c,h}` | the stats screen |
| `skill_prayer/` (content) | prayers: the dbtable, the toggle, the drain, `::pray` |
| `src/net/mock/mock230_world.c` | run energy, worn-slot map, the `::` commands |
| `src/net/mock/mock230_objinfo.c` | obj weight from the cache |
| `src/game/rs_cs2_dispatch.{c,h}` | the dirty-flag table, `RS_CS2_TransmitsPending`, the pump — §5.3 |
| `src/game/test/rs_cs2_transmit_pump_test.c` | `test-cs2-transmit-pump`, one flag at a time |
| `src/game/rs_cs2_host.c` | stat-transmit hook registry, `RS_CS2Host_NotifyStatChanged` |
| `src/game/task_cs2_run.c` | the transmit dispatch tasks, and the snapshotted hook bound |
| `general/scripts/misc/cheat_xp.rs2` (content) | `::xp`, `::xpdrop`, `::xpqueue` — §5.6 |
