# The OSRS rev-230 mock server

A standalone TCP server that speaks enough of the rev-230 protocol to drive the
real client: log in, load a scene, walk around, watch npcs roam, switch sidebar
tabs, equip items and rearrange the backpack. It exists so the client's
**server-driven** paths can be exercised without a real server — the paths that
never run offline, because offline the client has nothing to obey.

```
make -C src mock230 && src/build/mock230 &
src/torirs --manifest manifest_osrs230.ini --user test --pass test

make -C src test-mock230     # game logic, no socket
make -C src test-rsareabuf   # the wire buffer
```

| env | effect |
| --- | --- |
| `MOCK230_VERBOSE=1` | log every packet in and out |
| `MOCK230_CACHE=dir` | cache to read obj metadata from (default `cache.osrs230`, falling back to `../cache.osrs230`) |
| `MOCK230_ZONE=x,z` | origin zone to spawn in (default `426,408` — Al Kharid) |

Layout:

| file | contents |
| --- | --- |
| `src/net/mock/mock230_main.c` | socket, RSA/ISAAC handshake, the 600 ms tick loop, inbound framing |
| `src/net/mock/mock230_world.c` | player, npcs, containers, equipment, the tick, the self-test |
| `src/net/mock/mock230_encode.c` | every server→client packet |
| `src/net/mock/mock230_objinfo.c` | obj name / wearpos / ops, decoded from the cache |
| `3rd/rsareabuf/` | the packet buffer everything encodes through |

---

## 1. `3rd/rsareabuf` — the wire buffer

A C port of LostCity's [`rsbuf`](https://github.com/2004scape/rsbuf) (`src/packet.rs`),
the buffer its TypeScript engine encodes packets with. Same surface: `p1`/`p2`/
`p3`/`p4`/`p8`, the four byte-order variants of each, `psmart`/`psmarts`,
terminated strings, a bit-addressed mode for the info streams, and length
back-patching.

Two deliberate differences from the Rust original:

- **Bounds are checked.** `rsbuf` writes through `get_unchecked_mut` and makes
  capacity the caller's problem. Here every accessor clamps and latches a sticky
  `overflow` flag, so a mis-sized packet is a wrong length you can assert on
  rather than memory corruption. `rsab_ok()` gates the send.
- **Allocation is an arena** — the "area" in the name, and the C answer to
  `Packet.alloc()`'s size-class pool. A server builds a burst of packets per tick
  and drops them together; `rsab_arena_reset` is the whole memory story.

The byte-order naming follows the Jagex/RSProt convention, where the "alt"
number is the transform applied to the plain big-endian form:

```
p1        v                          p2        [v>>8, v]
p1_alt1   v+128                      p2_alt1   [v, v>>8]            (LE)
p1_alt2   -v                         p2_alt2   [v>>8, v+128]
p1_alt3   128-v                      p2_alt3   [v+128, v>>8]

p4        [v>>24, v>>16, v>>8, v]    p4_alt2   [v>>8, v, v>>24, v>>16]
p4_alt1   [v, v>>8, v>>16, v>>24]    p4_alt3   [v>>16, v>>24, v, v>>8]
```

`rsareabuf_test.c` asserts both halves of that table: every reader is the exact
inverse of its writer, **and** the bytes land where the protocol expects. Only
the second half catches a transposed alt order — a symmetric value like `0x4242`
round-trips fine through the wrong one.

Length back-patching comes in two forms. `rsab_psize1(buf, size)` is
rsbuf-compatible (the caller already knows the size and the slot is `size + 1`
bytes back). `rsab_psize1_begin`/`_end` is the mark form, where the body length
is discovered rather than tracked:

```c
size_t mark = rsab_psize1_begin(&buf);
put_appearance(&buf, player);
rsab_psize1_end(&buf, mark);
```

---

## 2. What the mock borrows from xrsps

`xrsps-typescript/server/src` is a full TypeScript OSRS server; the mock takes
its shape rather than its wire format (xrsps runs a bespoke WebSocket protocol —
`WIDGET_OPEN`, `INVENTORY_SNAPSHOT` — not RSProt).

- **`game/ticker.ts`** — a fixed-interval tick that anchors to a schedule rather
  than to "now", so a slow tick does not push every later tick out. `serve()`
  does the same with `next_tick += MOCK230_TICK_MS` and a catch-up bail-out.
- **`game/npc.ts`** — `ROAM_DELAY_MIN_TICKS = 15`, `ROAM_DELAY_MAX_TICKS = 30`,
  `DEFAULT_NPC_WANDER_RADIUS = 5`. An idle npc re-rolls a roam on that timer and
  stays inside a radius of its spawn. `advance_npcs` is that, verbatim.
- **`game/equipment.ts`** — `deriveAdditionalEquipSlotsFromParams` reads the
  *cache* for the extra slots an item claims rather than hard-coding a
  two-handed list. `equip_from_slot` does the same through `wearpos_2`/`_3`.
- **Actors hold a queue of pending tiles**, not a position delta. A click
  produces a queue; the queue produces one step per tick.

---

## 3. Protocol notes

### 3.1 The entity streams are a deliberate deviation

Real rev-230 `PLAYER_INFO` (op 23) and `NPC_INFO_SMALL` (op 104) carry RSProt's
v5 high/low-resolution streams. This client has no decoder for those — it decodes
the classic (lc254 / Kronos) bitstreams. So `src/net/rev/osrs230/packetin.h`
binds the rev-230 opcodes to the classic layouts and the mock encodes to match.

The pairing is self-consistent and exercises every downstream system — spawn,
appearance, movement interpolation, minimap dots, the pick set. **It is not wire
compatible with a real OldSchool 230 server.** Everything else (`IF_OPENSUB`,
`IF_SETEVENTS`, `UPDATE_INV_FULL`/`_PARTIAL`, `REBUILD_NORMAL`) uses the real
RSProt layouts, alt byte orders and all.

### 3.2 NPC type is an 11-bit field — ids above 2047 corrupt silently

The single sharpest edge here. The classic new-npc record is:

```
14 bits  npc slot          (terminator 16383)
11 bits  npc type          <-- 2047 maximum
 5 bits  dx from the local player
 5 bits  dz from the local player
 1 bit   extended info follows
```

OSRS 230 has ~12,000 npcs. Spawning npc 3106 ("Man") writes `3106 & 0x7FF` =
1058, and the client spawns whatever npc 1058 happens to be — or nothing at all,
which is how this was found:

```
spawn_npc: npc 1063 unavailable      # 3111 "Woman" truncated to 1063
spawn_npc: npc 1064 unavailable      # 3112 "Woman" truncated to 1064
```

Five of the ten npcs in the first roster truncated to ids that *did* exist and
spawned the wrong npc with no warning at all. `npc_spawn` now refuses anything
over 2047, and the roster is drawn from low ids: 385 Man, 397/398 Guard, 305
Jennifer, 542 Monk, 655 Goblin, 687 Bartender, 731 Sheep, 766 Banker, 1020 Rat.

Widening the field means widening `pkt_npc_info.c`, which is shared with the
lc254 and lc245_2 revisions that really do use 11 bits — so it needs a
per-revision seam (a `npc_type_bits` on `GameProtoRevTable`, threaded down to
`pkt_npc_info_reader_read`) rather than an edit.

### 3.3 The bit-section terminator is not optional, and the two streams disagree

Both info streams end their bit section with a terminator whose only job is to
stop the client's decode loop before it walks into the byte-aligned
extended-info section. Their loop guards differ, and the difference matters:

```c
/* pkt_player_info.c */  while( bitpos + 11 <= length * 8 )  { pid = gbits(11); if (pid == 2047) break; ... }
/* pkt_npc_info.c   */   while( bitpos + 21 <  length * 8 )  { slot = gbits(14); if (slot == 16383) break; ... }
```

- **Player stream**: the guard always admits the terminator (padding is at most
  7 bits), so it is always consumed and the byte alignment always agrees.
- **NPC stream**: with no extended blocks the guard *rejects* the terminator —
  `8 + 21 < 24` is false for a 3-byte packet. The client byte-aligns before the
  terminator and reads a couple of bytes fewer than were sent. Harmless, because
  there is nothing after it. As soon as an extended block exists the guard
  passes (`ext_bits + pad > 7` always holds) and the terminator is read. Writing
  it unconditionally is correct for both cases.

Omitting it is the classic way to make a stream decode as garbage entities: the
loop keeps pulling 11- or 14-bit ids out of the appearance blob that follows.

### 3.4 The client sends turning points, not tiles

`net_out_move_gameclick` writes the client's own route: `p1 ctrlHeld`, `p2`
absolute start x, `p2` absolute start z, then up to 24 signed byte pairs
relative to that start, ordered from the tile nearest the player to the
destination.

Those are the **turning points** of `collision_map_try_route`, not every tile. A
six-tile walk arrived as two waypoints:

```
mock230: <- MOVE ctrl=0 start=3409,3268 waypoints=1 steps=6 dest=3406,3271
```

So the server has to interpolate between consecutive waypoints the way an actor
walks — diagonally while both axes differ, then straight (`steps_walk_to`).
Treating each waypoint as one tile would make the player teleport in jumps.

`MOVE_MINIMAPCLICK` appends a 14-byte anti-cheat trailer. Counting waypoints as
`(len - 5) / 2` without subtracting it appends seven junk tiles to every minimap
walk.

### 3.5 Client→server opcodes are assigned, not transcribed

`src/net/rev/osrs230/packetout.h` now carries the full outbound table plus the
payload size each opcode frames to. `NO_TIMEOUT` (0), `MAP_BUILD_COMPLETE` (54),
`MOVE_MINIMAPCLICK` (55) and `MOVE_GAMECLICK` (86) are real RSProt values; the
rest are **assigned here** because RSProt's client-prot table is not vendored in
this repo and the mock is the only thing that reads them.

Payload layouts are the lc254 ones — `net_out.c` has one set of builders shared
by every revision and only the opcode number is revision-specific. A packet
built here would not parse on a real OldSchool server.

The inbound framer never guesses: an unknown opcode frames as zero-length and
resynchronises on the next byte, rather than consuming a made-up payload and
desyncing the ISAAC stream permanently.

### 3.6 Packets the client had no decoder for

Newly mapped in `packetin.h`, with `osrs230_parse` overrides where rev 230's
layout differs from lc254's:

| packet | op | note |
| --- | --- | --- |
| `NPC_INFO` | 104 | classic GNI (§3.1) |
| `MESSAGE_GAME` | 90 | `p1 type` + NUL string. The lc254 parser reads a newline-terminated string over the *whole* payload and swallows the type byte into the text. |
| `UPDATE_STAT` | 114 | 7 bytes: `p1 stat, p1 level, p4 xp, p1 boosted`. lc254 is 6 bytes in another order — and its parser **asserts** the frame is fully consumed, so the wrong size is a crash, not a misread. |
| `UPDATE_RUNENERGY` | 77 | `p2` hundredths of a percent; lc254 sends one byte. |
| `VARP_SMALL`/`_LARGE`, `UPDATE_RUNWEIGHT`, `VARP_SYNC` | 35/82/27/88 | identical to lc254, mapped as-is. |
| `SET_MAP_FLAG` | 2 | mapped onto `UNSET_MAP_FLAG`; the mock only ever sends the 255,255 "clear" form and the canonical handler ignores the payload. |

`IF_SETEVENTS` (op 47) is still dropped: the client has no concept of a
server-driven events mask (there is no `PKT_NAME_IF_SETEVENTS`). The mock sends
it anyway so the burst matches a real login, but nothing acts on it.

### 3.7 Equipment comes out of the cache, not a table

`wearpos_1` is the slot an item goes in. `wearpos_2` and `wearpos_3` are the
extra slots it *claims* — which is how the cache encodes both "this bow is
two-handed" and "this full helm covers your hair and jaw":

```
obj   841  Shortbow           wearpos=3/5/-1     weapon, claims shield
obj  1117  Bronze platebody   wearpos=4/6/-1     body,   claims arms
obj  1155  Bronze full helm   wearpos=0/8/11     head,   claims hair + jaw
```

One numbering serves three things — the cache's wearpos fields, the worn
container's 14 slots, and the 12-entry appearance blob:

```
0 head   1 cape   2 amulet  3 weapon  4 body   5 shield  6 arms
7 legs   8 hair   9 hands  10 feet   11 jaw   12 ring   13 ammo
```

Appearance slots 6, 8 and 11 hold body kits; an item claiming them through
`wearpos_2`/`_3` blanks the kit underneath. In the blob a slot is `0` when
empty, `0x100 + idkId` for a kit, `0x200 + objId` for a worn item — the same
split `PlayerModel_CollectAppearanceModelIds` keys on.

Deriving all of this from the cache (`mock230_objinfo.c` decodes the whole obj
table in ~0.1 s at startup) means every wearable item in the game works and the
appearance hiding rules come out right for free.

### 3.8 Scene rebuild

The client holds a 104×104 scene based at `(zone - 6) * 8`. Entity coordinates
in the info streams are relative to that origin; the client adds its own
`scene_off` (the offset from the map-square corner it actually loaded).

Once the player is within 16 tiles of a scene edge the mock re-centres:
`REBUILD_NORMAL` with the new origin zone, then an absolute placement (move op 3)
on the next `PLAYER_INFO`.

Tracked npcs deliberately **survive** a rebuild. The client shifts every kept
entity by the base-tile delta (`App_WorldRebuildShift`), so their slots stay
valid; dropping and re-adding them would re-spawn into slots the client still
holds.

---

## 4. Client fix this work required: the inv half of the transmit loop

The mock delivered container 93 correctly from the first run —

```
inv-full: container=93 (com 0x00950000) size=28
  slot  0 obj=1155 x1
  slot  1 obj=1117 x1
  ...
```

— and the inventory panel stayed blank.

`RS_CS2_PumpTransmits` re-dispatched inventory-transmit hooks **only** on
`widgets_loaded_dirty` (a widget being unhidden). `inv_change_serial` was
initialised to 1 and never bumped, and no `UPDATE_INV_*` handler notified
anything. So the CS2 script that paints an inventory ran exactly once, at
interface-build time, against an empty container — and nothing ever asked it to
run again. For a server-driven inventory the container *always* arrives after
the interface is built, so the panel could never be anything but blank.

This is the same reactive loop `RS_CS2Host_NotifyVarChanged` closes for varps and
varcs; only the container half was missing. Added:

- `RS_CS2Host_NotifyInvChanged(host, container_id)` — bumps `inv_change_serial`,
  sets `inv_transmit_dirty`, records the changed container id (mirroring
  `var_changed_ids`, so a change to one container does not re-run every hook).
- `RS_CS2_PumpTransmits` dispatches inv hooks on `inv_transmit_dirty` as well as
  on unhide, filtered by container.
- `exec_update_inv_full` / `exec_update_inv_partial` call it, and so does the
  local drag path in `app.c` (the swap is applied locally so the drag feels
  instant, but the paint still has to be asked for).

All 14 starting items render immediately after.

---

## 5. Verified, and the one thing that is not

Against the real client (`SDL_VIDEODRIVER=dummy`, `TORIRS_MAX_FRAMES`,
`TORIRS_EXIT_BMP`, `TORIRS_SIM_CLICK_AT`):

- **Walking** — a world click produces `MOVE_GAMECLICK`; the mock routes,
  interpolates and steps one tile per tick (two running), and the player and
  camera follow.
- **NPCs** — all ten spawn, render, appear as minimap dots and roam.
- **Tab switching** — clicking a sidebar tab swaps the mounted panel
  (inventory → worn equipment, hovertext and tab highlight both follow).
- **Inventory rendering** — all 14 items, including the ≥255 count escape
  (15,000 coins).
- **Scene, gameframe, chat, stats, run energy** — the full login burst applies.

**Not reachable from the client yet: inventory item ops.** Both the menu builder
and the drag machine require a `UIELEM_RS_INV` grid node:

- `rs_minimenu_build.c: add_inv_slot_rows` — needs `node->u.rs_inv` for the slot
  hit-test, and only then emits `pick_inv_slot`, which is what
  `app_minimenu_inv_action` turns into `OPHELD`/`INV_BUTTON`.
- `app.c: app_inv_node_at` — `if (node->type != UIELEM_RS_INV) continue;`.

At rev 230 the inventory has no such node. Interface 149 mounts a single static
`RS_LAYER`; every item cell is a CS2-created dynamic component with no recorded
container or slot. A right-click on an item therefore falls through to the
generic component-ops path and produces a `UI` pick, which dispatches a CS2 hook
instead of a packet:

```
minimenu: open at 541,229 in_world=0 picks=0
  row[0] action=1106 op=-1 kind=0 id=0        "Cancel"
  row[1] action=331  op=3  kind=1 id=9801690  "Read <col=ff9040>Bronze full helm</col>"
                           ^ kind=1 is PICK_UI, not PICK_INV_SLOT
```

(The op label is wrong too — "Read" is the component's static op, not the
objtype's "Wear" — and component `9801690` = `149<<16|39386` does not appear in
the mounted tree at all, which points back at the interface-pack double-bake.)

Closing this needs a slot/container association on CS2-created item components
and an inventory-slot pick built from it. That is a rev-230 UI change, not a
server one, and it is the single remaining blocker for equip and drag through
the UI.

**The server half of both is implemented and tested** — `make -C src
test-mock230` drives the game logic with no socket attached:

```
movement                          walk 1 tile/tick, run 2, waypoint interpolation
rebuild on scene edge             re-centre + absolute placement, player inside the new scene
equip / unequip                   worn slot, backpack vacated, appearance + partial-update dirty bits
two-handed weapon evicts shield   shortbow (wearpos 3/5) displaces the kiteshield back to the backpack
inventory drag                    INV_BUTTOND swaps both slots and marks both dirty
npcs roam inside their radius     200 ticks, radius respected, a zero-radius npc never moves
```

The mock also accepts `::` commands over `CLIENT_CHEAT` (`item <id> [count]`,
`tele <x> <z>`, `npc <id>`) for steering a session once chat input is reachable.

---

## 6. Follow-ups, in the order they unblock things

1. **Inventory-slot association for CS2-created components** (§5) — unblocks
   equip and drag through the UI. Everything on both sides of it already works.
2. **Per-revision `npc_type_bits`** (§3.2) — lets the roster use the whole npc
   space instead of ids under 2048.
3. **`IF_SETEVENTS`** (§3.6) — the client has no server-driven events mask, so
   op availability is currently whatever the interface definition says.
4. **A real v5 `PLAYER_INFO`/`NPC_INFO` decoder** (§3.1) — the prerequisite for
   ever pointing this client at a real OldSchool 230 server.
