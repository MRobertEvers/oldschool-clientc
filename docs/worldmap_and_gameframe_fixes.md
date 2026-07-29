# World map, camera angles, and the inventory's third slot

Three rev-230 gameframe items, done together because they share one seam — the
one where the *server* is the thing that decides what a click means.

- [1. Opening the world map, and clicking on it](#1-opening-the-world-map-and-clicking-on-it)
- [2. The inventory's third slot only drew while hovered](#2-the-inventorys-third-slot-only-drew-while-hovered)
- [3. CS2 5504/5505/5506 — the orbit camera's angles](#3-cs2-550455055506--the-orbit-cameras-angles)
- [Running two servers at once](#running-two-servers-at-once)

References used throughout: `~/Documents/git_repos/OpenRune-Server` for what a
server does with each message, `~/Documents/git_repos/XRSPS-Typescript` for what
the client does with it, and the cache's own clientscripts decompiled with
`3rd/rscache/tools/cs2/cs2` — the last of these settles arguments the other two
cannot, because it is the actual content.

Decompiling the corpus is worth doing once at the start of work like this:

```sh
NAMES=~/Documents/git_repos/cs2/target/classes/org/runestar/cs2
3rd/rscache/tools/cs2/cs2 decompile --cache cache.osrs230 --rev osrs230 \
    --names "$NAMES" --out /tmp/cs2
# 7657 of 7884 scripts, named — [clientscript,worldmap_init].cs2 &c.
```

Without `--names` about a third of the corpus fails to print (`no source
spelling for 1 as boolean-boolean`) and what does print says `script1050`
instead of `orbs_worldmap_setup`.

---

## 1. Opening the world map, and clicking on it

### What was already there

The map itself was done in an earlier session (`src/game/rs_worldmap.c`,
`rs_worldmap_render.c`, the `WORLDMAP_*`/`MEC_*` opcode family, the `dat2`
table-19 decode). `./run-live.sh manifest_osrs230_worldmap.ini asdf a --offline`
boots straight into interface 595 and draws Gielinor, its icons, its key panel
and its zoom buttons. Drag-to-pan was there too
(`app_worldmap_drag_tick` in `src/app.c`).

What was missing was every part that involves the server: the map could not be
*opened* from a live session, and a click on it did nothing.

### Why the client cannot open it by itself

The orb on the minimap is component **160:53**. Decompiling its setup script
settles what it does:

```
[proc,orbs_worldmap_setup](component $c0, component $c1, component $c2)
    ...
    if_setop(2, "Floating <col=ff9040>World Map</col>", $c2);
    if_setop(3, "Fullscreen <col=ff9040>World Map</col>", $c2);
    if_setonop("opsound(event_opindex, -1)", $c2);
```

The whole client-side behaviour of the orb is **a click sound**. Everything
else is the server's, exactly as in
`OpenRune-Server/content/.../interfaces/WorldMapEvents.kt`:

```kotlin
on<ButtonClickEvent> {
    where { component.combinedId == "components.orbs:worldmap".asRSCM() }
    then {
        if (!player.ui.containsOverlay(fromInterface("interfaces.worldmap"))) {
            player.sendWorldMapTile()          // RUNCLIENTSCRIPT worldmap_transmitdata
            when (option) {
                2 -> player.ifOpenOverlay("interfaces.worldmap")
                ...
```

So four things had to exist that did not:

| Piece | Direction | Why |
|---|---|---|
| `IF_BUTTON1..10` | client → server | the numbered verb the player picked |
| `RUNCLIENTSCRIPT` | server → client | pushes the player's coord into the map's varcs |
| `IF_OPENSUB` of 595 | server → client | already existed; just needed calling |
| `CLICK_WORLD_MAP` | client → server | a click on the map surface |

### `IF_BUTTON1..10` — the op that was never sent

`PKTOUT_NAME_IF_BUTTON` already existed but carries only a component id: it is
the *op-less* plain click ("Click here to continue"). RSProt models a numbered
op as ten separate opcodes, `If3Button` = `combinedId (p4) + sub (p2)`, and
that is what `PKTOUT_NAME_IF_BUTTON1..IF_BUTTON10` are now
(`src/net/rev/pktnames.h`, wire opcodes 90..99 in
`src/net/rev/osrs230/packetout.h`, builder `net_out_if_button_op`). `sub` is
the dynamic-child index for a grid cell, `-1` for a plain widget.

The send site is the `UI_MINIMENU_PICK_UI` arm of `app_minimenu_run_option`
(`src/app.c`). It sends **and** runs the local `onop` hook — both, not either.
The orb is the clearest case for why: its hook is only `opsound`, so an
either/or would mean the map never opens, while suppressing the hook would mean
the click is silent.

The gate is the component's `IF_SETEVENTS` mask, bit *n* arming op *n* (bit 0
is the plain click). rev 230 has no clickable-by-default, so a component the
server never armed stays purely client-side — which is why the mock now sends
`IF_SETEVENTS` for the orb at login. This is the same convention the inventory
grid already used (`0x3fe` = ops 1..9).

### `RUNCLIENTSCRIPT` — the coord push

Wire opcode 84 was in `packetin.h` already, mapped to `PKT_NAME_NONE` (parsed
and dropped). It now has a canonical name, a parser, and a run path.

The reference layout, which the parser in `osrs230_parse.c` follows:

```
<type chars, one per argument, newline-terminated>   e.g. "iiis\n"
<arguments, in REVERSE order>                        's' -> string, else p4
<script id>                                          p4
```

The reverse order is not a quirk of this port — the reference writer pushes the
CS2 operand stack, which unwinds last-argument-first.

`App_RunClientScript` (`src/app.c`) hands it to `RS_CS2_RunScript`
(`src/game/rs_cs2_dispatch.c`), a sibling of `RS_CS2_DispatchHook` that passes
component id `-1`: nothing in the tree triggered this script, so `.cc_*`
opcodes inside it have nothing to resolve against, which is also true in the
reference. It enqueues onto the same serial task FIFO as every other packet, so
"sent first" is "applied first" — which is what lets the mock send the coord
and the `IF_OPENSUB` back to back and rely on the coord landing first.

The script the mock names is **1749**, `worldmap_transmitdata`:

```
[clientscript,worldmap_transmitdata](int $int0, int $int1, int $int2)
    %varcint188, %varcint1078, %varcint401 = $int0, $int1, $int2;
```

`varcint188` is the packed player coord; `worldmap_init` reads it
(`~worldmap_findcoordinmap($maparea45, %varcint188)`) to pick which map area to
open on, and the "You are here" marker is drawn from it.

### Where the map mounts

`161:18`. That is not a guess: `orbs_worldmap_setup` tests

```
if (if_hassub(enum(component, component, $enum3, interface_161:18)) = true) {
```

to decide whether the orb should offer "Close Floating panel" instead of the
two open verbs — so `161:18` is by definition the floater slot, and `$enum3`
(from `~toplevel_getcomponents`) is the identity map when the toplevel *is*
161. It matches OpenRune's `ifOpenOverlay(interf) = ifOpenOverlay(interf,
"components.toplevel_osrs_stretch:floater")`.

The close button is **595:4**, from the last line of `worldmap_init`:

```
if_setopkey(1, ^key_escape, 0, interface_595:4);
```

— escape is bound to op 1 of 595:4, i.e. that component is the close button.
OpenRune agrees (`components.worldmap:close`).

### `CLICK_WORLD_MAP`

`PKTOUT_NAME_CLICK_WORLD_MAP` (wire 100), payload one packed
`level << 28 | x << 14 | z` — the same 30-bit coord every world map CS2 op
speaks.

`app_worldmap_click` in `src/app.c` does the screen → tile conversion, which is
the inverse of the icon placement a few lines above it: the box centre shows
the view's display position, each map tile is `RS_WorldMap_ZoomScale` pixels
wide, and screen y grows opposite map y. That yields a *display* coord (a
position on the flattened map surface, which is not the world); the area's
sections turn it back into a world coord via `RS_WorldMap_DisplayToSource`.

Click and drag share one press. `worldmap_drag_moved` records whether the view
ever actually moved; a release that never panned is a click, a release that did
is not also a click.

**"Inside the surface box" is not "on the map."** The map's own chrome is drawn
*inside* that box — the close X, the key panel, the search field, the zoom
buttons — so the first version of this teleported the player every time they
closed the map, to whatever tile the X happened to be drawn over:

```
mock230: <- IF_BUTTON1 595:38     # close
mock230: world map closed
mock230: <- CLICK_WORLD_MAP 0,3271,3261   # ...and a teleport nobody asked for
```

The press is gated on `app->hover_com_id < 0` instead. That is precisely the
distinction wanted, and it is cheap to confirm: hovering bare map reports -1,
while the X, the key panel and the bottom bar all report real component ids.

```
[400,90]   hover_com_id=-1          <- bare map
[470,22]   hover_com_id=38993958    <- 595:38, the close X
[80,60]    hover_com_id=39031453    <- key panel
[236,314]  hover_com_id=39032023    <- bottom bar
```

The gate covers the drag start as well as the click, which also stops the map
panning behind the key panel's scrollbar while it is being dragged.

### Mock server side

`src/net/mock/mock230_worldmap.c`, four entry points:

| | |
|---|---|
| `mock230_worldmap_login` | `IF_SETEVENTS` arming orb ops 1..3 and both close buttons |
| `mock230_worldmap_handle_button` | claims `IF_BUTTON<op>` for 160:53, 595:4 and 595:38 |
| `mock230_worldmap_click` | `CLICK_WORLD_MAP` → `mock230_world_teleport` |
| `mock230_worldmap_tick` | re-sends the coord while the map is open and the player moves |

Opening is toggling: clicking the orb with the map already up closes it, which
is what the reference does and what the orb's own "Close Floating panel" op
expects. Ops 2 ("floating") and 3 ("fullscreen") both open it — the mock has
one presentation.

The click teleports unconditionally. The reference gates it on an admin
privilege; the mock has one player and no privilege system, and the point of
the packet here is that the round trip is observable at all.

`mock230_world_teleport` was factored out of the `tele` cheat, because a
world-map click routinely lands past the rebuild margin — the scene has to
follow, or collision and ground-obj visibility still describe wherever the last
section left off.

### Verified end to end

Against `manifest_osrs230_alt.ini`, headless:

```sh
# open, then click the map: teleports and the marker follows
MOCK230_VERBOSE=1 SDL_VIDEODRIVER=dummy TORIRS_MOCK_BIN=src/build/alt_mock230 \
TORIRS_SIM_CLICK_AT="150,704,140;260,400,90" TORIRS_MAX_FRAMES=400 \
TORIRS_EXIT_BMP=/tmp/wm.bmp ./run-live.sh manifest_osrs230_alt.ini
# mock230: <- IF_BUTTON2 160:53 sub=-1
# mock230: world map opened
# worldmap_click: screen=400,90 display=3248,3238 -> 0,3248,3238
# mock230: <- CLICK_WORLD_MAP 0,3248,3238
# mock230: rebuild zone=408,407 origin=3216,3208 squares=4
```

The three closing paths were checked separately: the orb again (toggles), the
red X (595:38), and — with the gate above — neither of them teleporting.

### Known gap

Interface 595 measures itself through `IF_GETWIDTH(0x02530009)`, which returns
0 where the cache says 573×403, so script 1750 takes its legal "surface not
sized" branch. That is a layout-side defect in 595 that predates this work and
is unrelated to the packets above.

---

## 2. The inventory's third slot only drew while hovered

### Symptom

Backpack slot index 2 (the third square) was blank. Moving the pointer over it
made the item appear; moving away made it vanish again. Every other slot was
fine.

### It was not a load failure

`SETOBJECT` for that slot succeeded, with a real icon:

```
INVDBG setobject com=0x958fdc obj=1075 count=1 scene=474
```

but the emit walk skipped the component entirely — the draw list jumped from
slot 1 straight to slot 3. The gate it hit is in `emit_walk_node`
(`src/ui/uitree_emit.c`):

```c
/* Hide-gated layers stay invisible unless their component_id is hovered. */
if( c->behavior.hide && !UITree_ComponentVisibleById(c, hovered_component_id) )
    return;
```

So the question was not "why does hover reveal it" but **"who set `hide` on
it"**. Tracing every `UITree_ApplyHide` gave the answer immediately:

```
setobject com=0x958fdc obj=1075 ...      <- slot 2 gets its item
applyhide  com=0x958fdc hide=0
setobject com=0x958fdb obj=1117 ...      <- slot 1 gets its item
applyhide  com=0x958fdc hide=1           <- ...and slot 2 is hidden
applyhide  com=0x958fdb hide=0
```

An extra hide landed on slot 2 while slot **1** was being filled — every time,
deterministically.

### Root cause

`UITree_ApplyObject` (`src/ui/uitree.c`) carries an equipment-slot heuristic:
an equipment slot container has three `cc_create`'d children — d0 border, d1
item overlay, d2 empty silhouette — and setting an item on d1 hides d2 so the
silhouette does not show through the item.

The guard was:

```c
int const is_equipment_overlay = c->dynamic && c->dynamic_child_index == 1;
```

The backpack is not a slot container, it is a **grid**: interface 149:0 holds
one cell per sub id, d0..d27. Its d1 *is* a real slot, and its "d2 sibling" is
another real slot. So filling slot 1 hid slot 2. The same collision applies to
bank rows; the comment above the line already warned about it, and the previous
narrowing (`+ d2 must be an `RS_GRAPHIC``) did not help, because a grid cell is
an `RS_GRAPHIC` too.

### Fix

Discriminate on the *shape* of the parent, which is the thing that actually
differs: a slot container's cells stop at d2, a grid's do not.

```c
static bool
uitree_parent_is_equipment_slot(struct UITree const* tree,
                                struct UITreeComponent const* child)
{
    /* ... returns false if any dynamic sibling has dynamic_child_index > 2 */
}
```

Verified both ways: all 14 backpack items now draw without hover, and the
equipment tab still shows silhouettes in empty slots and replaces the
silhouette with the item when something is equipped.

### Related finding, deliberately not changed

The hover exemption in `emit_walk_node` applies to every component. In the
reference it does not: `widgets-gl.ts` skips an IF3 widget on `hidden`
unconditionally and grants the `mousedOverIf1WidgetUid` exemption only to IF1
type-0 and type-11 containers. So our gate is what turned "one slot is hidden"
into the more confusing "one slot flickers on hover". Narrowing it to `!if3`
would match the reference, but nothing currently depends on it being wrong and
it is a broad change with no driving symptom — noting it here instead.

---

## 3. CS2 5504/5505/5506 — the orbit camera's angles

### Symptom

```
CS2VM2: unimplemented opcode 5505 (CAM_GETANGLE_XA) — no stack signature
  in script 1050 ...
Assertion failed: (0 && "unimplemented CS2 opcode reached StackMetaStub")
```

### What script 1050 is

The compass. Decompiled:

```
// 1050
[clientscript,script1050](int $int0)
    ...
    sound_synth(synth_2266, 1, 0);
    def_int $int1 = 0;
    if (%varbit15323 = 1) {
        $int1 = calc(225 + randominc(5));
        if ($int1 = cam_getangle_xa) { $int1 = calc($int1 + 1); }
    } else {
        $int1 = cam_getangle_xa;
    }
    switch_int ($int0) {
        case 1 : cam_forceangle($int1, 0);      // Look North
        case 2 : cam_forceangle($int1, 1536);   // Look East
        case 3 : cam_forceangle($int1, 1024);   // Look South
        case 4 : cam_forceangle($int1, 512);    // Look West
    }
```

It is installed by `script7045` as the compass's `onop`, behind the four
"Look North/East/South/West" verbs. It reads the current pitch and hands it
straight back to `cam_forceangle`, so the two opcodes have to agree on units.

### Units

**Pitch is 128..383 and yaw is 0..2047** — the reference's `orbitCameraPitch` /
`orbitCameraYaw`, which is exactly what `app->orbit_pitch` / `app->orbit_yaw`
already hold. Two independent confirmations:

- The script's random-pitch branch picks `225 + rand(5)`, squarely inside
  128..383 and nowhere near a 0..512 or 0..2047 range.
- XRSPS converts in both directions between its own 0..512 free-cam pitch and
  the script value: `camAngleX = 128 + pitch * 255 / 512`, and clamps the
  inbound `camAngleX` to `[128, 383]`.

(The 634-era client returns `(int) aFloat1287 >> 3` for the same opcode, i.e. a
2048-unit angle. Different era, different internal representation — the RS2
dialect overlay in `cs2vm2.c` already had these ids as stubs for that reason,
and real dispatch now shadows them with the same 0-in/1-int-out signature.)

### Implementation

| Layer | Change |
|---|---|
| `gen_opcode_stack.py` | `MANUAL_STACK` entries for 5504 `(2,0,0,0)`, 5505/5506 `(0,0,1,0)`; regenerate, never hand-edit the `.gen.h` |
| `cs2vm2.c` | dedicated dispatch → three new host request kinds. `CAM_FORCEANGLE` pops `(x, y)` in push order |
| `rs_cs2_host.c/h` | `cam_angle_x` / `cam_angle_y` / `cam_angle_forced`; `RS_CS2Host_SetCameraAngles` (mirror in) and `RS_CS2Host_TakeCameraForce` (snap out) |
| `app.c` | mirror `orbit_pitch`/`orbit_yaw` into the host each logic tick, then consume any pending force |

`RS_CS2Host` has no pointer to the render-side camera — the live camera is
reachable only through the separate UITree host bus — so the link is a
per-tick mirror rather than a callback. Order matters: mirror first, then apply
a force, so a snap issued this tick is not read back as "the camera moved there
on its own". `SetCameraAngles` skips the mirror while a force is pending, for
the same reason.

`CAM_FORCEANGLE` clamps pitch to 128..383 in the host, so a script that reads,
adjusts and writes back cannot walk the camera out of bounds one call at a
time.

### Verified

Clicking the compass (component `161:32769`, at 544,3 36×36) now runs the
whole round trip with no assert:

```
getangle_xa -> 128        # the live orbit pitch, not a stub
forceangle x=128 y=0      # Look North
apply pitch=128 yaw=0     # consumed by app.c, written back to the orbit cam
```

---

## Running two servers at once

Two agents (or two people) holding live sessions fight over port 43595 and over
`src/build/mock230`. There are now three builds of the same sources:

| target | binary | port | manifest |
|---|---|---|---|
| `mock230` | `src/build/mock230` | 43595 | `manifest_osrs230.ini` |
| `mock230-dev` | `src/build/dev_mock230` | 43597 | `manifest_osrs230_dev.ini` |
| `mock230-alt` | `src/build/alt_mock230` | 43599 | `manifest_osrs230_alt.ini` |

`run-live.sh` picks the binary up from `TORIRS_MOCK_BIN`:

```sh
TORIRS_MOCK_BIN=src/build/alt_mock230 ./run-live.sh manifest_osrs230_alt.ini
```

The port still comes from the manifest, so the two only have to agree there.
