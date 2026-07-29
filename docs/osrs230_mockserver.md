# The OSRS rev-230 mock server

A standalone server that speaks enough of the rev-230 protocol to drive the
real client: log in, load a scene, walk around, watch npcs roam, switch sidebar
tabs, equip items and rearrange the backpack. It exists so the client's
**server-driven** paths can be exercised without a real server — the paths that
never run offline, because offline the client has nothing to obey.

```
./run-live.sh     manifest_osrs230.ini testc test   # native  — starts the mock
./run-live.sh web manifest_osrs230.ini testc test   # browser — mock + IO server

make -C src test-mock230     # game logic, no socket
make -C src test-rsareabuf   # the wire buffer
make -C src test-ws-frame    # the WebSocket frame codec
```

`run-live.sh` starts the mock itself for any manifest whose `[net:boot]` names
`osrs230` on localhost, and stops it on the way out. `TORIRS_NO_MOCK=1` opts
out, and so does an instance already holding the port — one you started by hand
with `MOCK230_VERBOSE=1` or under a debugger is never fought over. By hand:

```
make -C src mock230 && src/build/mock230 &
src/torirs --manifest manifest_osrs230.ini --user test --pass test
```

| env | effect |
| --- | --- |
| `MOCK230_VERBOSE=1` | log every packet in and out |
| `MOCK230_CACHE=dir` | cache to read metadata and map squares from (default `cache.osrs239`, falling back to `../cache.osrs239`) |
| `MOCK230_CONTENT=dir` | content tree (default `OSRS-Content/osrs239-content`) |
| `MOCK230_SCRIPTS=dir` | compiled script pack (default `<content>/scripts/build`) |
| `MOCK230_HOME=x,z` | tile to log in on (default `3222,3218` — the Lumbridge castle courtyard). The scene's origin zone is derived from it |

`::` commands, for steering a session without a UI: `::talk <slot> [op]`,
`::fight <slot>`, `::style <0-3>`, `::setlevel <stat> <level>`, `::item <id>
[count]`, `::tele <x> <z>`, `::npc <type>`.

Layout:

| file | contents |
| --- | --- |
| `src/net/mock/mock230_main.c` | socket, RSA/ISAAC handshake, the 600 ms tick loop, inbound framing |
| `src/net/mock/mock230_ws.c` | the byte stream: raw TCP or WebSocket, decided per client |
| `src/net/mock/mock230_world.c` | player, npcs, containers, equipment, the tick, the self-test |
| `src/net/mock/mock230_encode.c` | every server→client packet |
| `src/net/mock/mock230_objinfo.c` | obj name / wearpos / ops / combat params, from the cache |
| `src/net/mock/mock230_npcinfo.c` | npc name / level / ops / combat params, from the cache |
| `src/net/mock/mock230_content.c` | the LostCity content tree — see `docs/mock230_content.md` |
| `src/net/mock/mock230_scene.c` | collision and locs, built from the cache's map squares |
| `src/net/mock/mock230_combat.c` | melee on OldSchool's own arithmetic |
| `src/net/mock/mock230_bank.c` | the bank — see [`mock230_bank.md`](mock230_bank.md) |
| `src/net/mock/mock230_pack.c` | content validator + derived-cache exporter |
| `3rd/rsareabuf/` | the packet buffer everything encodes through |

The world is **Lumbridge**, spawned from OpenRune's own spawn list, with
collision read out of the same map squares the client draws. Content — combat
stats, drop tables, doors, dialogue — lives in `OSRS-Content/osrs239-content` and is
documented separately in [`mock230_content.md`](mock230_content.md). The HUD
that fighting drives — hitsplats, the skills tab, npc level suffixes, facing,
the combat tab — is in [`combat_hud.md`](combat_hud.md).

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

### 3.1b Zone packets are half real, half assigned

`UPDATE_ZONE_PARTIAL_FOLLOWS` (106), `_FULL_FOLLOWS` (41) and
`_PARTIAL_ENCLOSED` (38) are real rev-230 opcodes. The sub-packets that follow
them — `OBJ_ADD`, `OBJ_DEL`, `OBJ_COUNT`, `LOC_ADD_CHANGE`, `LOC_DEL` — are
assigned here (70-72, 120-126), the same way the `IF_SET*` family's are (§3.5),
with lc254's payload layouts because those are what `gameproto_parse.c` already
decodes.

They could not simply reuse lc254's numbering: **a zone sub-packet's opcode is
resolved through the same table as a top-level one** (`rev->packetin_code`), and
lc254 puts `OBJ_COUNT` on 98 and `MAP_ANIM` on 114, where rev 230 has
`IF_SETHIDE` and `UPDATE_STAT`.

The zone header is two bytes, not the three RSProt's table states: the payload
is the zone's base as a pair of **classic scene-local tiles**, which the client
converts by adding `scene_off_x` — its own scene base is the 64-aligned map
square corner, the server's origin is `(zone - 6) * 8`. That is the same
conversion every entity coordinate goes through, so getting it wrong shows up as
loot landing a few tiles from the corpse rather than as anything louder. The
shared parser asserts exact consumption, so a third byte aborts the client.

### 3.1c `UPDATE_STAT` carries the boosted level, and that is the field with a consumer

`RS_GameProtoExec` writes the packet's level straight into
`stats->current_level`, having just let `RS_PlayerStats_SetXp` derive
`base_level` from the xp. So the base level on the wire is redundant and the
boosted one is what matters — the health orb reads `current_level[hitpoints]`,
which is the same number as the player's hitpoints. Sending the base there pins
the orb at full health for the whole session, which is exactly what it used to
do.

### 3.2 Field widths in the info streams are revision state

The single sharpest edge here. The classic new-npc record is:

```
slot bits  npc slot          (all-ones = section terminator)
type bits  npc type
   5 bits  dx from the local player
   5 bits  dz from the local player
   1 bit   extended info follows
```

Those two widths are **not constants**. The stream keeps its shape across
revisions while individual fields widen with the game's id space, and a width
that is too narrow does not fail — it truncates. OSRS 230 has ~12,000 npcs;
writing npc 3106 ("Man") into the classic 11-bit type field keeps its low 11
bits and produces 1058, so the client spawns whatever npc 1058 happens to be.

That is how this was found — not from the wrong npcs, which look fine, but from
the two that truncated onto *empty* ids:

```
spawn_npc: npc 1063 unavailable      # 3111 "Woman" truncated to 1063
spawn_npc: npc 1064 unavailable      # 3112 "Woman" truncated to 1064
```

Five of the ten npcs in the first roster spawned as a different npc with no
warning anywhere.

The widths are now stated per revision rather than hard-coded:

```c
/* GameProtoRevTable — 0 means the classic width, so lc254 / lc245_2 need
   no entry and are byte-for-byte unchanged. */
int npc_slot_bits;   /* 0 = classic 14 */
int npc_type_bits;   /* 0 = classic 11 (max id 2047) */
```

`osrs230` declares 14 for both. A reader is armed by
`pkt_npc_info_reader_init(reader, slot_bits, type_bits)` before every decode,
and `pkt_npc_info_reader_read` asserts the widths are in range — a forgotten
init is loud rather than a bit width of zero that invents entities out of
nothing. The terminator is derived from `slot_bits`, so it cannot drift out of
step with the field it terminates. The mock writes the same two widths from
`MOCK230_NPC_SLOT_BITS` / `MOCK230_NPC_TYPE_BITS`.

The 21-bit loop guard deliberately did **not** become a parameter: it is the
reference's own fixed margin and it decides whether the terminator is consumed
before the byte-aligned extended-info section (§3.3), so changing it would shift
that alignment.

`test-entity-decode` covers both widths on the same bytes — 3106 at 14 bits, and
the same stream read at 11 bits producing 388 — because the failure mode is a
decode that still succeeds and simply describes a different npc.

What this does *not* cover is a structural change. Real rev-230 `PLAYER_INFO`
and `NPC_INFO` (§3.1) are a different format, not a rewidened one; that needs a
second decoder, not a parameter.

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

## 3.9 The tick runs LostCity's eleven phases

`mock230_world_tick` is eleven named phase functions in the order
`World.cycle()` runs them, not because symmetry is nice but because the order
*is* the behaviour:

```
1  world      world_delay queue
2  clients_in latched input -> interactions and direct triggers
3  npc_events ai_spawn / ai_despawn
4  npcs       resume, timers, queues, modes, movement
5  players    resume, queues, timers, interaction, movement
6  logouts    [logout]
7  logins     [login]
8  zones      loc/obj respawn + zone event flush
9  info       scene rebuild decision
10 clients_out every encoder
11 cleanup    drop everything that described only this tick
```

That ordering is what makes a script suspended in phase 5 unable to resume in
the same tick, an npc added in phase 4 get its `[ai_spawn]` in the *next* tick's
phase 3, and the rebuild decision (9) land before any encoding (10). Several
phases are still empty; they exist anyway, because an empty named phase says
where work goes whereas an absent one invites putting it wherever is nearest.

## 3.10 Content is RuneScript, not C

Behaviour that a server operator would want to change lives in
`OSRS-Content/osrs239-content/server/scripts/`, compiled by `src/serverscript`'s `sscompile`
and executed by the ServerScript VM. See `docs/serverscript.md` for the
toolchain and [`mock230_content.md`](mock230_content.md) for the tree.

```
make -C src mock230-scripts     # build the pack — needed on a fresh checkout
```

Wired triggers: `[login,_]` from phase 7, `[opnpc1..5,<npc>]` from an OPNPC
packet, and `[ai_queue3,<npc>]` when an npc reaches zero hitpoints — which is
LostCity's death trigger, and where every drop table lives. Resolution follows
the reference: exact npc type, then category, then the global form.

"Attack" is deliberately *not* content. The engine reads the npc's own cache op
list, which is the same five options the client built its right-click menu from,
so anything OldSchool made attackable is attackable here with no per-npc script
line and no second list to keep in step.

**The fallback contract is load-bearing.** No script pack, or no script for a
trigger, means the call site does exactly what it did before scripts existed.
That is what keeps `make -C src test-mock230` green while content is mid-edit,
and what makes a broken toolchain degrade the mock rather than break it.

**Ids are rev-230 ids.** `content/pack/*.pack` maps names to ids valid in
`cache.osrs239`, taken from the cache's own gameval table and re-validated by
`mock230_pack`. LostCity's own packs are 2004-era and would hand the client
something unrelated.

### Suspension

`p_delay`, `npc_delay` and `world_delay` park a script and the matching tick
phase resumes it. Two details are worth stating because both are off-by-one
traps that nothing else would catch:

- **`p_delay(n)` resumes on tick +n+1**, because the reference sets
  `delayedUntil = tick + 1 + n` — a delay costs the rest of the current tick
  plus n more. `queue(script, n, arg)` is the same.
- **A parked script holds its npc by slot, not by pointer.** An npc can despawn
  or have its slot reused while a script waits on it, so `SSVM_State.host_tag`
  carries the slot and a resumed script either finds the same npc or finds none.

There is one parking slot per player, matching the reference. A second script
suspending while one waits is refused rather than queued — two parked scripts
would interleave writes to the same player.

## 3.11 NPC chat dialogue

Content: `scripts/chat.rs2`'s `~chatnpc` opens interface **231** into the chatbox
and blocks on `p_pausebutton`, so a multi-page conversation is just sequential
statements. `hans.rs2`'s `[opnpc3,hans]` is a three-page example.

rev-230 ids, all verified with `tools/dump_interface` rather than assumed:

| what | uid |
|---|---|
| npcchat group | `231` (root 506x129) |
| chathead model | `231:2` |
| speaker name | `231:4` |
| body text | `231:6` (67px, about four lines) |
| "Click here to continue" | `231:5` (`clickMask=0x1`) |
| chat container | `162:559` — **ships `hidden=1`** |
| mount slot | `162:561` (506x129, exactly 231's root size) |

**A genuine port decision.** LostCity picks between four groups
(`npcchat1..npcchat4`) by line count, because each of its chat interfaces has a
fixed number of text components. rev 230 has one group with a single multi-line
body, so the page/line machinery collapses to "open 231, set the body".
Anything here that reads like a missing feature is that difference.

**Opening the dialogue is two packets, not one.** `162:559` is hidden, so
mounting into `162:561` alone builds a dialogue that is never drawn — which
looks exactly like the mount having failed.

### The client bug this exposed: a nested mount hid the gameframe

Opening the dialogue made the client render a blank frame. Not a mock bug —
`hide_unmounted_spillover` in `task_interface_open.c` was hiding root group
**161**, the entire gameframe.

That function hides interface packs the CS2 runtime baked ahead of their mount,
so they do not draw as stray roots. Its guard against hiding the live tree was:

```c
int host_group = (self->target_uid >> 16) & 0xffff;
if( group == host_group )
    continue;
```

which protects only the mount target's *immediate* group. Every login-burst
mount targets `161:xx`, so `host_group` is 161 and the gameframe is safe. The
chat dialogue is the first mount into a **nested** sub-interface — `162:561`,
where 162 is itself mounted under `161:96` — so `host_group` came out 162 and
161 fell through and was hidden.

The failure had no error anywhere: the packets are correct, the mount succeeds,
and the frame is simply blank.

The fix generalises the guard. A root group that *hosts* a mounted
sub-interface is part of the live tree by definition, whatever the depth:

```c
if( group_hosts_a_mount(self->tree, group) )
    continue;
```

`TORIRS_SPILLOVER_DEBUG=1` prints every root the tree hides and which mount
caused it — that print is what found this, and "which root did the tree just
hide" is invisible from anywhere else, so it stayed.

### Making the continue button live: IF_SETEVENTS

At rev 230 nothing is clickable by default. A component the server never
enabled produces no menu row however clickable it looks, so the first version of
this rendered a perfect dialogue that swallowed every click:

```
click: miss=0 (0,0) com=0xe70005 gate=-1 drawable=1 picks=0
```

`0xe70005` is 231:5 — the hit was right, `picks=0` was the problem.
`RS_Minimenu_IfButtonActionForType` derives a row from `button_type`, an IF1
concept; 231:5 is IF3 with `button=0` and `clickMask=0x1`.

IF_SETEVENTS (op 47) is now wired end to end:

- parsed in `osrs230_parse.c`. It packs four different byte orders into twelve
  bytes — p4Alt3 uid, p2Alt2 start, p4Alt1 events, p2 end — which is why it
  cannot go through the shared parser;
- persisted by `App_IfEventsSet`, like IF_SETHIDE, because the server enables a
  component before the interface holding it has mounted;
- consulted by `add_component_rows` through a callback on
  `RS_MinimenuBuildCtx`, so the minimenu still knows nothing about the App. A
  component with the click bit set gets a row labelled with its own text, which
  is the string the player is already reading.

Server side, `if_addresumebutton` now sends IF_SETEVENTS as well as recording
the uid — registering the button without enabling it is exactly the
looks-right-does-nothing case above. Inbound, IF_BUTTON is tried as a resume
first: rev 230 has no separate resume opcode in practice, and a click matching
no registered button falls through, which keeps sidebar tabs (switched
client-side on a varc) a no-op.

The whole loop then works: `~chatnpc` opens the dialogue and suspends, the
player clicks, IF_BUTTON arrives, `mock230_scripts_resume_button` matches it by
full uid, and the script continues to the next page.

### NPC speech has no overhead text in this client

`npc_say` originally set the NPC_INFO SAY mask. That decodes correctly and
reaches `World_NpcSetChat` — but **nothing reads `entity->chat` back**;
`world.c` is the only file that touches it. Overhead bubbles are not
implemented, so npc speech rendered nowhere while looking correct at every other
level: the mask was set, the payload grew, the client parsed it.

The mock now sends npc speech to the chatbox as `"<name>: <text>"`. That is
visible, and it keeps `npc_say`'s fire-and-forget semantics — routing it through
a modal dialogue would have made a non-blocking command blocking. Content is
unchanged; scripts still write `npc_say`.

The speaker's name comes from `mock230_npcinfo.c`, which decodes the npc config
table the same way `mock230_objinfo.c` decodes objs (14,205 records). It also
backs `npc_name`.

Facing the player is kept on the SAY path, because FACE_ENTITY does render.

## 3.12 Baseline melee combat

`mock230_combat.c`. The split follows the rest of the mock: the engine owns what
has to stay consistent — hitpoints, hitsplats, death, respawn, swing timing —
and content decides who is attackable.

```
[opnpc2,goblin]      // action 31 is "Attack" in cache.osrs239
p_opnpc(2);
```

That is the whole of it, because everything visible was already a wire feature.

**The DAMAGE mask carries the hitsplat *and* the health bar** (damage, type,
health, total_health), so a hit is one mask write rather than a packet of its
own. **An npc dying is the ordinary NPC_INFO remove path**, and respawning
clears `tracked` so the next NPC_INFO adds it as a new entity — which is what a
respawn is from the client's side.

Loop: engage → walk beside → swing every `MOCK230_ATTACK_SPEED` ticks → the npc
retaliates the first time it is hit → death animation holds the corpse for
`MOCK230_DEATH_TICKS` → despawn → respawn at the spawn tile at full health.

Npc hitpoints scale off the cache's combat level (`level * 2`). That formula is
the mock's own: rev-230 npc configs carry no server-side hitpoints field, unlike
LostCity's `npc.dat`.

Three deliberate omissions:

- **No attack animations from the engine.** The sequence ids are not derivable
  from this cache without decoding the weapon's bas type, and a wrong seq id at
  rev 230 is a silent no-op at best. Content can add one with `anim` /
  `npc_anim`; the hitsplat and health bar are what carry the fight visually.
- **A zero-damage hit is a *block* splat, not nothing.** Otherwise a miss is
  indistinguishable from the server having dropped the swing.
- **Player death heals to full** rather than teleporting and dropping items. The
  mock has no respawn point, no item loss and no death interface, and inventing
  them is a lot of behaviour nothing asked for.

Commands: `p_opnpc`, `npc_damage`, `damage`, `healenergy`, `uid`,
`npc_findhero`, `npc_attackrange`. `::fight <slot>` engages an npc without a
right-click, for headless sessions.

One ordering trap worth stating: `advance_npcs` used to clear `step_dir` at the
top of its loop, which also wiped the step the combat mover had just produced.
Phase 11 does that clear, once, at the right time — roaming now skips any npc
that is fighting or dead.

## 3.13 One port, two transports

A browser tab has no TCP. The web build's sockets are emscripten's, which
implement `connect()` as a WebSocket to `ws://host:port` — so the same client,
built for the browser, arrives here as an HTTP/1.1 upgrade followed by RFC 6455
frames, and every byte of the 230 stream is inside one.

The mock takes both on the same port, and decides by looking rather than by
configuration: a 230 client opens with `INIT_GAME_CONNECTION` (opcode 14) and an
upgrade opens with `G`. One byte tells them apart, and one byte is all that can
be peeked — a raw client sends its opcode alone and then waits, so peeking
further would deadlock. Nothing above `mock230_ws.c` knows which happened;
`mock230_conn_recv`/`_send` carry application bytes either way.

Two things about the handshake are not optional:

- **Echo the subprotocol.** Emscripten asks for `binary` by default, and a
  browser fails a connection outright when the server does not confirm a
  requested subprotocol. Ignoring the header looks like "the page cannot reach
  the server" with nothing in either log.
- **Server frames are unmasked** (RFC 6455 §5.1). `ws_frame_encode` takes
  `mask == NULL` for that direction; `ws_frame_encode_header` emits just the
  header so a large packet goes out straight from the caller's buffer.

Frame boundaries carry no meaning in either direction — the payloads are
concatenated into one stream, because the 230 protocol frames itself and a
WebSocket message is not a packet. The receive path must therefore treat "the
socket was readable but produced no application bytes" as normal (a partial
frame) rather than as a closed connection.

The frame codec is the client's, shared: `src/platform/net_transport_ws_frame.c`,
unit-tested by `make -C src test-ws-frame`.

### 3.13b Three transports, and why the handshake had to stop blocking

Raw TCP and WebSocket are both *sockets*. A third has no socket at all: the
server hosted inside the client's own process, exchanging bytes through a pair
of queues. `src/net/mock/mock230_transport.h` is the seam — `recv` / `send` /
`pollfd` / `close` over a `void*` — and `mock230_embed.h` is the API a host
drives:

```
client PopOut   --->  mock230_embed_write     (client -> server)
                      mock230_embed_pump
client NET_RECV <---  mock230_embed_read      (server -> client)
```

The client half needed nothing. `struct ToriRS_Network` never touched a
descriptor: bytes arrive through `HandleCmd(NET_RECV)` and leave through
`PopOut`, and the platform layer is what bridges that to a socket. So an
in-process game is those two streams crossed with the server's.

**The server half needed one real change, and it is the whole story of this
section.** The login handshake was a straight-line function that blocked on
`recv_full(1)`, `recv_full(3)`, `recv_full(len)`. That is fine when a thread is
free to wait. In-process, client and server share a thread — so a server
waiting for bytes is a server the client can never reach, and a blocking read
stops being a stall and becomes a deadlock.

So `mock230_session.c` is a state machine (`INIT` → `LOGIN` → `ONLINE`), and
every wait is "have enough bytes arrived yet?" answered against a buffer and
re-entered on the next pump. `mock230_conn_recv_full` has been **deleted**
rather than left unused: it is the API that made blocking possible, and leaving
it invites its return.

Two things fell out of that rewrite that were latent bugs on the socket path
too:

- **Torn packets now survive.** The old reader had two `fprintf(stderr, "split
  var-u8 header")` bail-outs admitting it could not cope with a packet split
  across two reads; it stayed correct only because the client happens to write
  each packet with a single `write()`. The awkward part is that descrambling an
  opcode *spends a byte of ISAAC keystream* and a stream cipher cannot be
  rewound — so the opcode is consumed the instant it is read and parked in
  `Mock230Session.pending_opcode` until the body catches up. The length prefix
  is not scrambled, so re-reading that costs nothing.
- **One `recv` per pump, never a loop.** An accepted socket is blocking, so the
  `select()` in `mock230_main.c` is the only thing guaranteeing the *first* read
  returns; a second has no such guarantee. For the same reason a `select()`
  timeout must not fall through into a read, or a session whose player is
  standing still hangs the server.

`make -C src test-mock230-embed` runs a real client subsystem against a real
server over the queue pair, feeding the stream in 7-byte chunks so the torn-read
path is exercised on every run. If it ever hangs, the handshake has gone back to
waiting for bytes.

**The real client can do it too.** `src/platform/net_transport_embed.c` is a
third `NetTransport` beside TCP and WebSocket — same vtable, no wire under it —
selected by `[net:boot] transport=embed`:

```sh
make -C src torirs EMBED_SERVER=1
src/torirs --manifest manifest_osrs230_embed.ini --user testc --pass test
```

That boots the scene, the player and the npc roster with **no server process and
no socket**. The manifest differs from `manifest_osrs230.ini` by one line.

Three notes on it:

- **`EMBED_SERVER=1` is opt-in**, because linking it puts the server's tick, its
  script VM and its content loaders inside the *client* binary (3.0 MB → 3.5 MB)
  and makes the client's build depend on a content tree it otherwise never
  reads. Without the flag, selecting `transport=embed` fails loudly rather than
  silently falling back to TCP.
- **`[net:boot] transport=` was dead config until now.** It was parsed into the
  manifest and read by nothing but its own unit test; the revision table's
  `transport_kind` decided everything. It is honoured in `main.c` now, with the
  revision as the default — which is right, since a rev table describes a
  protocol and `embed` is a *deployment*.
- **The server ticks on its own clock**, not the frame clock: a client rendering
  at 144 Hz must not run the world 144 times a second. `embed_poll` anchors to a
  600 ms schedule the same way the socket server's loop does.

A makefile trap worth recording, because it cost a build cycle and the symptom
points nowhere near the cause: `$(TARGET): $(OBJS)` expands its prerequisites
when the makefile is *read*, while its recipe expands them when it *runs*. So
`SRCS += $(MOCK230_CORE_SRCS)` placed after that rule produced a link line
listing objects that were never prerequisites — `no such file or directory:
build/ssvm.o` for a file the build had simply never been asked to compile. The
source lists are defined above the rule for that reason.

The loader order also moved out of `main()` into `mock230_boot.c`, because two
callers now need it and it is order-dependent in three places that all fail
*silently* when reversed (see that file's header).

## 3.13c Interactions walk before they act

Every op handler used to walk *and* act in the same call. `handle_opobj` queued
a route to the tile and emptied the ground pile in the same breath, under a
comment admitting "the mock has no interaction model, so the player arrives
instantly in game terms". The player could take an obj from across Lumbridge,
through a wall.

There is now a real interaction (`struct Mock230Interaction`, latched on the
player), resolved once per tick in phase 5 after movement:

| distance | what runs |
|---|---|
| within ap range (10) | `[apnpc<n>]` / `[aploc<n>]` / `[apobj<n>]` — if content bound one, the interaction is done and the player never closes the distance |
| adjacent (or *on* the tile, for a ground obj) | `[opnpc<n>]` / `[oploc<n>]` / `[opobj<n>]`, then the engine's own verb handling if nothing was bound |
| further | keep walking, try again next tick |

The packet handler also attempts resolution immediately, so clicking something
you are already standing next to acts on the tick the click arrives rather than
the one after. That is what the reference does, and it is why most of the
existing selftests needed no change.

This is the prerequisite for the whole `ap`/`op` half of the LostCity content
convention: "at range" was previously not a state the server could be in, so no
`[ap*]` trigger could ever have fired.

Three things worth knowing:

- **An npc target is re-read every tick**, because it moves. Re-routing as it
  goes is what makes following work; without it the player walks to a memory.
  Slot reuse is checked too — same index, different npc, and acting on it would
  attack whatever respawned there.
- **A loc is re-found by id first, tile second.** A tile routinely carries more
  than one loc: 3226,3223 in Lumbridge holds the castle door *and* a wall
  decoration, and "whatever is at this tile" resolves to the decoration. Only
  when the id is gone does the tile-only form apply — which is the case that
  matters for a door somebody else opened mid-walk.
- **Anything meaning "the player changed their mind" must clear it** — a ground
  click, a teleport, `p_stopaction`. A pending op that survives is one that
  fires whenever the player next wanders into range.

One deliberate behaviour change came with it: the door swap used to run *before*
the script trigger, making a door the one thing content could not override. It
is now behind the trigger like everything else. Nothing in the tree binds a door
script today, so this changes no behaviour — it removes an exception that would
otherwise have been found the hard way.

`make -C src test-mock230` covers it, including the negative: that a click from
eight tiles away latches an interaction, starts a walk, and does **not** act.

## 3.13d Two dispatch tables, and the opcode gap report

**Inbound packets** are a table (`k_packet_routes` in `mock230_world.c`), 45
entries mapping `PKTOUT_NAME_*` to a handler. It was a 240-line `switch` with
half its bodies written inline, which made `mock230_world_handle` the function
every new packet had to be threaded through. Adding one is now a line plus a
handler. Handlers take `name` even when they ignore it, because the numbered
families (`OPHELD1..5`, `OPNPC1..5`, `IF_BUTTON1..10`) derive their op index
from it — one table entry per opcode, one handler per family.

**Host opcodes** get the more interesting half. The server implements 155 of the
396 declared ServerScript opcodes (63 in the VM core, 92 in the host seam), and
the question that matters is not "how many" but "which ones does *this content
tree* need that we lack".

`mock230_scripts_report_gaps` answers it at **load** time by walking every
loaded script's `opcodes[]` array:

```
mock230: 3 opcode(s) this content uses are not implemented:
  MES                          first wanted by [opnpc3,bob]
  NPC_SAY                      first wanted by [opnpc2,goblin]
  INV_ADD                      first wanted by [label,citizen_chat]
```

Load time, not call time, is the whole value. The VM already complains when an
unimplemented opcode is *reached* — but only if a player triggers that script,
which for content behind a quest step or a rare drop may be never. This turns
the number into a work queue: an opcode nothing asks for is not worth
implementing, and one three scripts ask for is blocking three scripts.

The coverage set is **generated** from the `case SS_OP_*:` labels in the VM core
and the host seam (`gen_opcode_coverage.py`), because the answer already exists
twice in the source and a third hand-kept copy goes stale the first time someone
adds an opcode without remembering it. A wrong coverage list is worse than none:
it would either hide a missing opcode or cry wolf about an implemented one.
`make -C src test-mock230-coverage` fails when the generated header is stale,
and the failure direction is safe either way — a stale table over-reports.

Two traps worth recording:

- **Opcode values are sparse, not dense.** 396 declared opcodes span values up
  to 10003 (host commands start at 4000). An array sized by the *count* silently
  treats every real opcode as out of range, which is exactly how the first
  version of this reported no gaps at all while three were present.
- The `first_user` array is `static` because 10,004 pointers is 80 KB, most of a
  default thread stack.

`make -C src test-mock230` asserts the shipped tree has zero gaps, so content
written against an opcode the engine lacks fails the build rather than a player.

## 3.14 The world map is the server's, start to finish

`src/net/mock/mock230_worldmap.c`. The orb on the minimap has no client-side
behaviour beyond a click sound, so opening the map is four server steps:
`IF_SETEVENTS` to arm the orb's ops at login, `IF_BUTTON<op>` in, a
`RUNCLIENTSCRIPT` of `worldmap_transmitdata` carrying the player's coord, then
`IF_OPENSUB` of interface 595 into the toplevel's floater slot (161:18).
Clicking the map comes back as `CLICK_WORLD_MAP`.

Three of those were new wire: `IF_BUTTON1..10` (the *numbered* op on a plain
IF3 widget — `IF_BUTTON` alone is the op-less click), `RUNCLIENTSCRIPT`
inbound, and `CLICK_WORLD_MAP` outbound. Full write-up, including how each id
was established from the decompiled clientscripts, in
[`worldmap_and_gameframe_fixes.md`](worldmap_and_gameframe_fixes.md).

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
- **NPCs** — all ten spawn, render, appear as minimap dots and roam, including
  the OSRS-era ids (3105 Hans, 3106-3108 Man, 3111/3112 Woman, 3114 Farmer)
  that the 11-bit type field used to silently redirect.
- **Tab switching** — clicking a sidebar tab swaps the mounted panel
  (inventory → worn equipment, hovertext and tab highlight both follow).
- **Inventory rendering** — all 14 items, including the ≥255 count escape
  (15,000 coins).
- **Scene, gameframe, chat, stats, run energy** — the full login burst applies.

**Inventory item ops now reach the client too.** They did not when this section
was first written, and the diagnosis here was right about the symptom and wrong
about the cause: the claim was that the menu builder and the drag machine needed
a `UIELEM_RS_INV` grid node, which rev 230's inventory does not have. What they
actually needed was to understand the *other* shape an item cell comes in — a
CS2 `cc_create`d child whose index within its static parent IS the slot, which
is also what rsprot's `If3Button.combinedId` / `.sub` mean.

`UITree_ObjCellForNode` resolves both shapes for both callers. Right-click,
left-click and drag all work on the backpack and on the worn tab, and the wire's
component field was widened from 2 bytes to the revision's 4 so `149:0` stops
arriving as `0`. See [`mock230_player_systems.md`](mock230_player_systems.md) §1
for the whole of it, including where a cell's verbs come from and why the worn
tab needed a different answer from the backpack.

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
`tele <x> <z>`, `npc <id>`, `run`, `pray`, `equipstats`) for steering a session
once chat input is reachable.

Run energy, the equipment-stats screen and overhead prayers are documented
separately in [`mock230_player_systems.md`](mock230_player_systems.md), along
with the second binary (`make -C src mock230-dev`, port 43597) they were built
against.

---

## 6. Follow-ups, in the order they unblock things

1. ~~**Inventory-slot association for CS2-created components** (§5)~~ — **done**.
   `UITree_ObjCellForNode` resolves a CS2 item cell, and a numbered op on one
   goes to the server as `IF_BUTTON<n>` carrying the container uid and the sub
   id. The last piece was whose verbs the cell offers — the container's or the
   child's — which the bank is what found; see
   [`mock230_bank.md`](mock230_bank.md) §6.
2. ~~**`IF_SETEVENTS`** (§3.6)~~ — **done**. The mask is persisted by
   `App_IfEventsSet` and gates both the minimenu row and the outbound
   `IF_BUTTON<n>`. Note the convention: the bit for op N is `1 << N` with N
   one-based.
3. **A real v5 `PLAYER_INFO`/`NPC_INFO` decoder** (§3.1) — the prerequisite for
   ever pointing this client at a real OldSchool 230 server. The rev table has
   three unused function-pointer slots (`player_info_read`, `npc_info_read`,
   `appearance_decode`) reserved for exactly this; nothing assigns or calls them
   yet.

### 6.1 Becoming a full server, in dependency order

The transport seam (§3.13b) and the interaction model (§3.13c) are the first two
steps of this and are done. What is left, in the order each unblocks the next:

1. **Finish the session/world/player split.** Net state is out of
   `struct Mock230Server` and lives on `Mock230Session`; what remains is the
   player. `srv->player` is still a single embedded struct, so a second player
   is not a change but a rewrite of every signature that takes `srv`. The shape
   wanted is `M2World { players[], npcs, zones, ... }` with the player passed
   down rather than reached through the world. Do it before the content grows —
   it gets more expensive every week, and *what is saveable is exactly
   `M2Player`*, so persistence wants the same struct.
2. ~~**Dispatch tables, twice.**~~ — **done**, see §3.13d. Inbound packets are a
   45-entry table; the opcode gap report is generated from the `case` labels and
   runs at load. What is *not* done is splitting the 1,550-line host `switch`
   into per-domain files — the introspection that motivated it came from the
   generator instead, so the split is now a readability change rather than a
   blocking one.
3. **Zones with buffered events.** `npcs[256]` / `ground[256]` are scanned flat
   every tick, and the npc cap is justified by a *wire* field width, which is a
   protocol constant standing in for a world capacity. A `ZoneMap` keyed
   `(zx, zz, level)` holding per-zone entity lists and an event buffer is what
   makes a loc change replayable to whoever walks in later — which is the thing
   multiplayer actually needs.
4. **Fill in the host opcodes.** ~100 of 396 are implemented. Driven by the gap
   report from step 2, plus `.dbtable`/`.dbrow` support in the content reader —
   `mock230_content.h` admits prayers were flattened into a bespoke `.prayer`
   grammar to avoid writing one, and drop tables and shops want it too.
5. **Move the C content into content.** ~3,200 lines that are content by
   LostCity's definition still live in C: the bank (1,370), combat (858),
   equipment, prayer, the world map, doors, and the login burst. The reason is
   real and documented in `bank.rs2`'s own header — the rev-230 bank builds its
   op ladder conditionally on varbits, so the index alone does not say what was
   clicked — which is exactly why step 4 comes first. Widen the opcode surface
   until a script *can* say it, then move it.
6. **Invert the fallback.** `mock230_scripts.c` promises that a missing script
   leaves every call site doing "exactly what it did before scripts existed".
   That was right while scripts were an experiment; for a real server it means
   every behaviour has two implementations that can disagree, and a content bug
   (script aborted, returned 0) is indistinguishable from an engine path. Keep
   one fallback — the reference's `_` wildcard script — and let a trigger with
   no script do nothing, loudly under `MOCK230_VERBOSE`.
7. **Rename.** `mock230_*` is a double misnomer: it is not a mock, and it reads
   a 239 cache while speaking the 230 wire. Mechanical, and cheapest while
   there is still one consumer.

Two smaller things that belong with step 1: the accepted socket is *blocking*,
so the `select()` in `mock230_main.c` is load-bearing (see §3.13b) and a real
server wants non-blocking accept with per-connection output buffering; and
`MOCK230_VARP_COUNT` is a flat `int32_t[5000]` per player — 20 KB each — which
wants to be sparse or sized from the cache.

The player stream's 11-bit pid field is the same class of thing as §3.2 and has
not been parameterised — it is 11 bits in every revision in the tree, so there
is nothing yet to vary. If a revision widens it, it wants a `player_pid_bits`
beside the two npc widths rather than an edit.
