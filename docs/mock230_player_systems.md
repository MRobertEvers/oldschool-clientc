# Player systems on the rev-230 mock

Four things a player does that the mock could not do before: move items around,
run, read their equipment bonuses, and turn a prayer on. They are grouped
because they turned out to share one problem — **at rev 230 the client asks the
server for permission far more often than older revisions did, and a component
the server never armed is inert no matter what the cache says about it.**

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
| `::pray <0-28>` | toggle a prayer by its index in interface 541's button order (18 = Protect from Melee); grants the level it needs |
| `::equipstats` | open the equipment-stats screen without walking the sidebar |

New env knobs: `TORIRS_OPS_DEBUG=1` (which script wrote which verb onto which
component), `TORIRS_STATIC_SPRITE_DEBUG=1` (which chrome sprite packs bound, and
which did not).

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
mock230_prayer_arm_buttons(srv);       /* 541:9..541:37 op1 — "Activate"     */
```

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

### 2.3 The orb

`UPDATE_RUNENERGY` and `UPDATE_RUNWEIGHT` go out only when the value changed —
at one packet a tick each they would be a third of everything the mock sends —
and are flushed after `PLAYER_INFO` but before the container deltas, so weight
never lags a tick behind the item that changed it.

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
toggle and varp 173. In the client, `TORIRS_NET_CHEAT="run 1"` plus a world
click lights the orb, and the percent falls and recovers.

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

The table is content, not C: `skill_prayer/configs/prayers.prayer`, one block
per prayer, which is LostCity's `prayers.dbrow`/`prayers.enum` pair flattened
into the `[symbol]` + `key=value` shape the rest of the tree uses. A prayer's
index is its position in that file, which is also its bit in `prayer_active`.

Levels and drain rates are OldSchool's, transcribed from xrsps's
`src/rs/prayer/prayers.ts` (the 1/6/12/24 pattern is not a smooth curve).
Conflicts are a group bitmask rather than a group id, because the combat prayers
(Chivalry, Piety, Rigour, Augury) belong to several groups at once — which in
the config is several `group=` lines on one block.

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
`PLAYER_INFO` they were going to get anyway. `mock230_prayer_toggle` sets
`MOCK230_PMASK_APPEARANCE` and that is the whole of the delivery.

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

### 4.5 Honest gap

**The prayer buttons do not light up.** The lit state is a per-prayer varbit the
client's own CS2 reads, and those ids have not been identified. The prayer is
on, the overhead icon is up, the points drain, the tab looks unchanged. Finding
the varbits is the next piece of work here.

### 4.6 Verified

`make -C src test-mock230-dev` covers the level gate, group replacement, the
drain arithmetic (12 units a tick against 60 = a point every fifth tick), and
that running out clears both the prayers and the icon mask. In the client,
`TORIRS_NET_CHEAT="pray 18"` draws the Protect from Melee icon above the
player's head, and clicking a prayer button in the tab arrives as
`IF_BUTTON1 541:9`.

---

## 5. Where things live

| file | what |
| --- | --- |
| `src/ui/uitree_obj_cell.{c,h}` | item-cell resolution for both tree shapes |
| `src/ui/uitree_input.c` | obj-bearing components are menu targets |
| `src/game/rs_minimenu_build.c` | one row builder for both shapes |
| `src/app.c` | drag machine, click order, headicons fallback |
| `src/net/net_out.c`, `src/net/rev/osrs230/packetout.h` | component uid at the revision's width |
| `src/cs2vm2/cs2vm2.c`, `src/game/rs_cs2_host.c` | `RUNENERGY_VISIBLE` / `RUNWEIGHT_VISIBLE` |
| `src/net/mock/mock230_equipment.{c,h}` | the stats screen |
| `src/net/mock/mock230_prayer.{c,h}` | prayers, drain, the icon mask |
| `src/net/mock/mock230_world.c` | run energy, worn-slot map, the `::` commands |
| `src/net/mock/mock230_objinfo.c` | obj weight from the cache |
