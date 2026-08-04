# The OSRS rev-230 server

> **This is not a mock, whatever the filenames say.** It began as one and the
> `mock230_` prefix stuck; it is now the server this project runs, and it is
> being grown into a full one modelled on LostCity — content in content files,
> engine holding no ids. The prefix is a misnomer twice over, since it reads a
> rev-**239** cache while speaking the rev-**230** wire. Renaming is on the
> roadmap (§6.1) and has not happened.
>
> Read §3.13b–d and §6.1 before changing anything structural. The short version:
> the transport is a seam (a socket *or* an in-process queue pair), the login
> handshake is a non-blocking state machine, packets and script opcodes dispatch
> through tables, and the world holds a *pool* of players whose entity streams
> are per-player — two clients in one process see each other move (§6.1 step 1),
> though the socket server still accepts one connection at a time.

A server that speaks enough of the rev-230 protocol to drive the real client:
log in, load a scene, walk around, watch npcs roam, switch sidebar tabs, equip
items, rearrange the backpack, bank, fight, and persist across sessions. It
exercises the client's **server-driven** paths — the ones that never run
offline, because offline the client has nothing to obey.

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
| `MOCK230_CACHE=dir` | cache to read metadata and map squares from (default `cache.osrs239.baked`, falling back to `../cache.osrs239.baked`) |
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
  two-handed list. This does the same, and since 2026-08-02 it does it in
  **content**: `~wearpos_conflicts` (`player/scripts/equip.rs2`) over
  `oc_wearpos` / `oc_wearpos2` / `oc_wearpos3`, which read the obj record's
  config opcodes 13/14/27. The C that used to do it (`equip_from_slot`) is gone
  with `MOCK230_FALLBACK_OPHELD` (§3.18). `wearpos_2`/`_3` are still read from C
  in one place — `put_appearance`, to blank the body kit underneath (§3.7).
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

**One thing about the shape of these two streams outlives the deviation, and it
is the source of a whole class of bug.** A *tracked set* is per-observer — which
npcs and players this client has been told about, in what order — and that half
lives on `struct Mock230Player`. The *extended-info mask block* is not: it is
computed once per entity per tick and the same bytes go to everybody, exactly as
the reference does it (`World.cycle` → `rsbuf.computeNpc`, one call per npc).
So **any value inside a mask block that means something different depending on
who is reading it is a bug by construction**, and it will be a silent one,
because the one observer the value was written for sees it work. That is what
§3.11j is about — a `face_entity` that spelled "the player" as an alias for the
reader — and it is the test to apply to every future mask field: an id in here
must be absolute.

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
is the zone's base as a pair of **classic scene-local tiles**. The client's
scene base is now also `(zone - 6) * 8`, so those tiles need no further
offset. That is the same coordinate space every entity coordinate uses, so
getting it wrong shows up as loot landing a few tiles from the corpse rather
than as anything louder. The shared parser asserts exact consumption, so a
third byte aborts the client.

All three are in use now (§3.17). `_PARTIAL_ENCLOSED` carries its sub-packets
inside its own payload, opcodes and all, and those inner opcodes are **plain
wire bytes** — no ISAAC — resolved through the same `rev->packetin_code` table.
That is the second reason the sub-opcodes had to be assigned clear of the
top-level ones, and it is why `mock230_encode_zone_sub` writes the opcode itself
rather than leaving it to `mock230_send`.

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

### 3.4 The client sends turning points; the server re-routes

`net_out_move_gameclick` writes the client's own route: `p1 ctrlHeld`, `p2`
absolute start x, `p2` absolute start z, then up to 24 signed byte pairs
relative to that start, ordered from the tile nearest the player to the
destination.

Those are the **turning points** of `collision_map_try_route`, not every tile.
The server does **not** trust that list as a walk queue: `handle_move` takes the
**last** waypoint as the destination (LostCity `MoveClickHandler`), validates
`distanceToSW <= 104`, and re-routes through `mock230_scene_route` /
`collision_map_route_tiles` — the same flood the client uses. Op clicks
(`OPLOC`/`OPNPC`/`OPOBJ`) build a `CollisionApproach` (footprint, angle, shape,
rotated `forceapproach`) and route with `mock230_scene_route_op` rather than a
four-neighbour guess of the SW tile.

Player movement stores at most 25 dest-first waypoints and advances with a
greedy `takeStep` that re-validates `mock230_scene_can_step` each tick and
stalls instead of clearing. Reach uses `collision_map_reached` against the same
approach used to route; an unreachable interaction terminates with content's
`[proc,cannot_reach_message]`. See [`PATHING_INTERACTION_PARITY.md`](PATHING_INTERACTION_PARITY.md) §7.

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

The **wield requirement** was "the engine's to enforce and content's to
describe" until 2026-08-02, and it is content's on both halves now.
`mock230_equipment_may_wear` and the `equip_level_message` hook are deleted;
`~levelrequire_check` (`skill_combat/scripts/levelrequire.rs2`) decides and
speaks, from `[opheld2,_]`, and the sanctioned-hook list went **11 → 10** with
it — the first time it has gone down (§3.18).

What survives from that paragraph is the half that was already right: the words
are content's. `[proc,equip_level_message]` takes the `stat` id rather than its
name, and the 23 skill names it used to be handed were a C table in
`mock230_equipment.c` — `general/configs/stat.enum` and `[proc,stat_name]` hold
them now, so renaming or translating a skill is a content edit. The selftest
still asserts the sentence off the wire ("the wield refusal is content's, words
and all"), but it now measures something different: it used to prove the wording
had moved while the engine still decided, and it is now the leg proving the
*decision* moved with its wording intact. It is also what catches the RuneScript
trap here: `<...>` interpolates a *variable*, so a `<~proc(...)>` in a string
reaches the player as those literal characters unless the compiler is taught the
`~` sigil — which it now is (`ssc_compile.c`).

`mock230_equipment.c` is what is left of equipment in C: **102 lines** (it was
134 before the level gate went), and none of it is the equip *rule*. It is the
component-to-worn-slot map plus the requirement table `mock230_obj_require`,
which stays because `mock230_pack` validates those `.obj` lines, the dbrow
generator reads them, and the selftest cross-checks content's answer against
them (§3.18).

### 3.8 Scene rebuild

The client holds a 104×104 scene based at `(zone - 6) * 8`
(`WorldBuilder_RebuildCenterzone` via `Task_WorldLoad`'s zone-centre path).
Entity coordinates in the info streams are relative to that origin — no
separate `scene_off` window.

Once the player is within 16 tiles of a scene edge the mock re-centres:
`REBUILD_NORMAL` with the new origin zone, then an absolute placement (move op 3)
on the next `PLAYER_INFO`. Same-zone duplicates early-out without an ack
(Client-TS / deob `method3310`).

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

**All three rungs are live and content uses two of them.** `drop_tables/` binds
136 `[ai_queue3]` triggers, six of them to an npc *category* (`_chicken`,
`_cow`, `_bear`, `_ice_warrior`, `_unicorn`, `_werewolf`) and the rest by npc
type. The ordering is load-bearing rather than academic: an exact binding
shadows a category one, so `[ai_queue3,chicken]` beside `[ai_queue3,_chicken]`
would leave the chicken on the old table and give the new one only to its
siblings. `tools/port_droptables_check.py` fails on that shape, on a category
this cache does not state, and on a duplicate binding — which the compiler
compiles twice and `find_by_key` resolves by `bsearch` (triage §16.11).

**A trigger is how the engine should reach content, and a script name in C is
not.** The ten call sites that used to spell one — `"[queue,player_death]"`,
`"[proc,npc_meleeattack]"` and eight more — go through `srv->hooks` now, a table
`mock230_scripts_resolve_hooks` fills from the pack when it loads. An unresolved
hook is reported at boot instead of doing nothing forever, which is what an
unknown name used to do in every `run_proc` helper: renaming a script deleted a
feature without failing a build, a test or a log line outside `--verbose`. The
by-name helpers remain for tests. `docs/CONTENT_ARCHITECTURE.md` §8.6 has the
rule and what is left (`mock230_say`'s message procs).

"Attack" is engine *for now*. It reads the npc's own cache op list — the same
five options the client built its right-click menu from, so anything OldSchool
made attackable is attackable here with no per-npc script line. The reference
says it in `[opnpc2,_] @player_combat_start`, and this is
`MOCK230_FALLBACK_OPNPC`: one of **four** enumerated rows waiting on the opcode
surface, not a design position (§3.18). It is also the one row whose blocker was
never an opcode — its `blocked_ops` is empty and always has been.
`MOCK230_FALLBACK_OPNPC`: one of five enumerated rows waiting on the opcode
surface, not a design position (§3.18).

**The fallback contract is inverted, and §3.18 is the whole of it.** It used to
read: no script pack, or no script for a trigger, means the call site does
exactly what it did before scripts existed. It now reads: **a trigger with no
script does nothing**, the `_` wildcard is the only fallback, and the C that
still stands in is enumerated, counted at boot, and refused outright when no
pack is loaded.

**Ids are rev-230 ids.** `content/pack/*.pack` maps names to ids valid in
`cache.osrs239`, taken from the cache's own gameval table and re-validated by
`mock230_pack`. LostCity's own packs are 2004-era and would hand the client
something unrelated.

## 3.10b The npc/loc server fields boot from the band, not the text

PORTING_GUIDE §3.6 item 1 — "the server band is written but never read" — is
closed. `cachepack pack` writes `<content>/server/pack` (one archive per record
at *(config kind, id)*, under the opcodes `fields/<type>.ini` declares), and
boot now reads it: after the text pass, `mock230_content_load_server_band`
opens the pack, verifies it, and decodes every archive over the live defs.
The boot log says which path won, every boot:

```
mock230: server band loaded: 2973 archive(s) verified identical to the text
parse and applied (815 overlay authored defs, 21 field value(s) text-only, ...)
```

```
make -C src mock230-servpack    # rebuild the band — cachepack pack --server-only
```

`--server-only` exists because a full `pack` copies the base cache and emits
116k archives; the band half needs no cache at all, so the build step is as
cheap as `mock230-scripts`. `test-mock230` and `test-content` run it first.

**During migration the text parse is both the fallback and the proof.** Both
readers consume the same tree, so each is a full check on the other. Every
band archive is held to the text-loaded records three ways, per registered
field, against the *seed* (engine defaults + `[default]` + cache params):

- a value the band states must equal the value the text pass loaded;
- a field the band lacks must be one the text left at its seed, **or** one the
  band has no wire for — today `npc.huntmode` (10 records) and `npc.nomove`
  (11): their values are enum names (`aggressive`, `moverestrict=nomove`) and
  the band carries integers, the gap `fields/npc.ini` documents. Those stay
  text-loaded and the boot says so per field;
- an archive over a record with no authored block must decode to exactly its
  seed — which held for ~2,100 records whose `attackrate` rides in from
  `configs/all.npc`, and is skipped for the 113 nameless multinpc instances
  the runtime never loads (`mock230_npcinfo_known`: the npcinfo accessor hides
  nameless records behind its placeholder, so there is nothing to compare).

Only when every archive passes is anything applied; one failure keeps the text
values and says so. Staleness is loud twice over: each archive carries the
`'S' 'P' version kind crc32` header (`cp_fields.h` writes it,
`mock230_servpack.c` refuses on it — a single flipped payload byte was
verified to fail), and a pack older than the tree shows up as a value
mismatch naming the record and field. `mock230_pack` runs the same
verification and makes a stale band a validator failure, so the check is
permanent; an *absent* pack is not an error, exactly like an absent script
pack.

What stays text at boot: everything else — constants, enums, varps, spawns,
`[default]`, patrol routes, `loc.category`, and the two no-wire fields above.
Removing the text parse for the band-carried fields is the "remove the
fallback once green" step of §3.6, and it is now unblocked.

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
| mount slot | `chatbox:chatmodal` — `162:567`, **ships `hidden=1`** |

**A genuine port decision.** LostCity picks between four groups
(`npcchat1..npcchat4`) by line count, because each of its chat interfaces has a
fixed number of text components. rev 230 has one group with a single multi-line
body, so the page/line machinery collapses to "open 231, set the body".
Anything here that reads like a missing feature is that difference.

**Opening the dialogue is one packet, and the slot is the whole message.** The
modal ships hidden, but the server never unhides it and never hides the chat
behind it — the *cache's own clientscripts* do both, keyed off this exact
component. The gameframe's `on_sub_change` hook (`161:0` → script903) runs
script908:

```
if (if_hassub(chatbox:chatmodal) = true) {
    if_sethide(false, chatbox:chatmodal);
    if_sethide(true,  chatbox:chatoverlay);
    if_sethide(true,  chatbox:chatcrm);
    if_sethide(true,  chatbox:chatdisplay);   // the scrollback and input line
} else { ...the mirror image: chatdisplay and chatcrm back... }
```

so `IF_OPENSUB(162:567, 231)` reveals the dialogue *and* hides the chat, and
`IF_CLOSESUB(162:567)` brings the chat back. Decompile before picking a
component: this was `162:561` with an `IF_SETHIDE(162:559, 0)` in front of it
for a while, ids chosen by matching 231's 506x129 root against a layer of the
same size. Both were wrong — `559` is `chatscrollbar` and `560`/`561` are
`chatcrm`/`crmspace`, the Jagex announcement popup that script908 shows in the
*no-dialogue* state — and the symptom was not a missing dialogue but a correct
one with the chat scrollback and `name: *` input line drawn straight through
it, because nothing had told the chatbox to stand down.

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
chat dialogue is the first mount into a **nested** sub-interface — `162:567`,
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

## 3.11b Animation priority, and the two bugs that hid behind each other

Two separate faults produced one report — "npcs only ever play their block
animation, and they do not face what they are fighting" — and each of them is
the kind that has no error anywhere.

**Facing was written into the wrong half of an id space.** `FACE_ENTITY` reads
below 32768 as an *npc slot* and at or above it as `32768 + player index`
(`world_cycle.c`, `WORLD_FACING_*`). At the time, `UPDATE_PID` told every client
it was index 2047 — the same number that terminates the player stream — so "face
the player" on the wire was **34815**. The mock wrote the bare 2047,
which the client resolved as npc slot 2047. That slot never exists, the lookup
returned NULL, the branch fell through, and every npc in a fight kept whatever
yaw it had been walking with. The selftest asserts the id space rather than "not
-1", because the wrong value is a *plausible* one.

The constant that fixed it — `MOCK230_FACE_LOCAL_PLAYER`, `32768 + 2047` — was
itself the next bug, and §3.11j is what became of it. The number above is
correct for exactly one reader.

**Animations had no priority gate.** An npc swings in phase 4 and is hit in
phase 5, so every exchange wrote the attack animation and then overwrote it with
the block — one field, last write wins. The reference's rule
(`PathingEntity.playAnimation`) is that a new sequence replaces the queued one
only when `priority(new) >= priority(incumbent)`, and the data says a swing
outranks a flinch: `goblin_attack_unarmed` declares `forcedpriority=6`,
`goblin_block` declares none and defaults to 5.

Three things that fell out of implementing it:

- **Priority is cache opcode 5**, `forcedpriority` in the unpacked configs — not
  the record's `priority` (opcode 10) or `precedence` (9), both of which are
  client-side rendering concerns. `mock230_seq_priority` indexes it by sequence
  id.
- **The decoder zero-initialises**, so a record omitting opcode 5 arrives as 0
  rather than as the reference's default of 5. Taking that literally would make
  every un-prioritised animation lose to every other one, turning the gate into
  "the first animation of the tick wins".
- **`anim_id` is cleared in phase 11, with the masks.** It is the incumbent the
  gate compares against, and one that outlives its tick keeps refusing lower
  priorities forever — a goblin that lands one attack would never flinch again.

`anim` and `npc_anim` route through the same gate, as they do in the reference.

### The engine stopped naming animations

There was a third fault under those two, and it is the one worth keeping.
`mock230_seq_for_npc` built a sequence *name* out of an npc's display name —
`"Goblin"` → `goblin_attack` — and asked the cache for it. It is deleted.

It guessed right for the monsters anyone would spot-check and answered -1 for
most of the roster: the cache has no `man_attack`, `woman_attack`,
`guard_attack` or `duck_attack`. `play_npc_seq` returns on -1 without sending
anything, so those npcs swung with no animation, no wire evidence and no log
line. It was also the engine naming content, which is the arrangement this
server is organised against.

What an undescribed npc gets instead is the content tree's own `[default]` block
(`general/configs/npc_default.npc`) — which is where the four ids that used to be
C string literals in `init_defaults` now live. `.npc` files are read in two
passes for it, because `[default]` has to be applied before any block that
inherits from it and `areas/` sorts before `general/`.

The selftest walks every *attackable* npc in the roster and fails if any of them
resolves no attack, block or death animation. Per npc, because the failure is
per npc and silent.

## 3.11d A varp has two writers, and only one of them had side effects

"Running works but the run toggle does not" — `::run` made the player run, and
clicking the run orb lit the orb, transmitted the varp, and left the player
walking.

Every visible part of the click was correct, and each was checked in turn: the
component is armed (`if_setevents(orbs:runbutton, …)`), the hit test finds
160:28, the left click builds the same menu the right click shows and picks
"Toggle Run" as its default, the events mask gates the send and passes,
`IF_BUTTON1 160:28` arrives, and the server resolves it by *name* — 160:28 is
`160 << 16`, too wide for a compiled trigger key's 21 bits, so it goes through
`mock230_scripts_run_if_button_named` — and runs `[if_button,orbs:runbutton]`,
which writes `%option_run`.

The gap is one level below all of that. **`SS_OP_POP_VARP` wrote
`player->varps[]` directly**, while the engine's own `mock230_world_set_varp`
carried the `option_run` → `run_toggle` mirror. So anything hanging off a varp
worked when the *engine* set it and silently did nothing when *content* did —
which is the wrong way round, because content is where a varp is supposed to be
written.

`varp_side_effects` is now the shared seam and both writers call it. The opcode
cannot simply call the setter: assignment marks a varp for transmission even
when the value is unchanged (the reference's semantics, and what makes
LostCity's `%option_nodef = %option_nodef; // resync varp` mean anything) while
the setter early-returns on an equal write. So the transmission half stays in
the opcode and the side-effect half comes through `mock230_world_varp_written`.

`TORIRS_CLICK_DEBUG=1` on the client prints the rows a left click built, which
one is the default, and the events mask on the component it points at. That is
the four-step chain above, and the right-click menu looking correct only rules
out the first step.

## 3.11e Patrol, and why Hans was a wanderer

`defaultmode=patrol` plus `patrol1..patrolN` are content now, ported from
LostCity's own `[hans]` block. A waypoint is
`<level>_<mapx>_<mapz>_<localx>_<localz>,<pause>` — a map square is 64 tiles, so
`0_50_50_7_33` is 3207,3233, the same absolute tile in both eras because
Lumbridge has not moved.

Hans was `wanderrange=5`, with a comment saying the patrol was "expressed as a
wander radius instead" because the mode was unimplemented. That is not a smaller
version of a patrol: the route is his whole character. He is the greeter outside
the castle, so he has to be *findable*, and a wanderer of the same range is
somewhere random in a hundred tiles.

Three things about the implementation:

- **The route is a ring.** Reaching the last waypoint goes back to the first, so
  he keeps circling rather than stopping at the end of his round.
- **The pause is charged on arrival, before the index advances**, or it would be
  charged to the waypoint he is walking away from.
- **Routing matches LostCity `Npc.patrolMode`:** queue the absolute patrol
  tile as a waypoint and `takeStep` toward it. Re-running `naive_path` every
  tick is wrong here — that finder can return the source tile on a pure
  cardinal approach, which left Hans one tile short of every waypoint until the
  stuck-teleport fired (looking like ordinary wander near the castle).
  `maxrange=50` is the reference's leash and is what lets a route this wide
  exist at all; the default 7 would drop it the moment he left his spawn tile's
  neighbourhood.

`moverestrict=outdoors` is deliberately not ported: this engine implements
`nomove` of that family and nothing else, so declaring it would be a claim the
collision map does not enforce. The route stays outside on its own.

### The teleport nobody was told about

Patrol carries the reference's stuck-teleport: 32 ticks without reaching the
waypoint (or a waypoint on another level) and the npc is put on it. The move
happened on the server and **was never encoded**, so Hans was the npc this cost
the most — his route rounds the castle wall, so he is the one who stalls.

NPC_INFO's tracked section has four movement ops: nothing, one step, two steps,
and remove. None of them says "is now over there". The player section already
knew this — `place_dirty` makes a teleporting player a *remove* in every
observer's stream and the entering-view loop re-adds them at their new tile, in
the same packet — and the npc section had no equivalent. So every client that
already held the npc kept drawing it where it used to be, indefinitely: nothing
in the stream ever contradicted the stale tile.

That is what "Hans is not patrolling correctly" actually was, and why it
presented as an *identity* bug. The click path resolves a scene element to
`server_slot` and sends OPNPC with it, so the slot was always right; the server
then routed the player to the tile Hans was really standing on. Clicking the man
standing by the courtyard door walked you across the grounds — he answered from
somewhere other than where he was drawn.

The fix is `Mock230Npc.tele`, the npc half of `place_dirty`, set by the one
chokepoint every discontinuous move now goes through:

```c
mock230_world_npc_teleport(npc, x, z, level);   /* PathingEntity.teleport() */
```

It does the four things the open-coded sites kept getting partly right — moves
the collision stamp with the npc, abandons the route, clears `step_dir` (a
teleport is not a step; leaving it set makes the client glide the npc across the
map), and raises `tele`. Three callers: patrol's stuck-teleport, wander's
500-tick walk home, and the `npc_tele` opcode — which previously moved the npc
without moving its occupancy at all, so the imp left a blocked tile behind it on
every hop.

`tele` is cleared in phase 11 beside `masks`, and for the same reason: every
observer's NPC_INFO has to have been written before it is dropped, or whoever is
encoded first consumes it and everyone else sees an npc that did not move. Zone
membership is refiled in phase 8, before anything is encoded, so the re-add finds
the npc at its new tile.

The selftest (`a teleported npc is re-added, not left behind`) checks the
client's list order rather than the flag: a kept npc holds its index and a
re-added one is appended after every kept npc, so an npc that was first and is no
longer first was removed and re-added. Reverting the encoder's one-term change
turns it red.

## 3.11c `[ai_spawn]`, and the imp

Phase 3 was an empty named phase. It now dispatches `[ai_spawn,<npc>]` for every
npc spawned since the last tick, which is where the reference's behaviours set
their first `npc_settimer` — and without it `[ai_timer]` could never fire for an
npc no script had touched, which was every npc in the world.

**A respawn re-runs it.** A timer set once at login stops the first time the npc
dies, so the behaviour decays out of the world one death at a time and the npc
that stops behaving is never the one being watched.

`[ai_despawn]` is deliberately not dispatched: death goes through `[ai_queue3]`,
where the drop tables already are, and a trigger that fires from nowhere is worse
than one that does not fire.

The first behaviour on top of it is the imp
(`areas/lumbridge/scripts/imp.rs2`), which is the reference's: a 50 % roll on a
50-200 tick timer teleports it up to twenty tiles away. Two new opcodes were
needed and both are small:

| opcode | note |
| --- | --- |
| `map_findsquare(coord, min, max, mode)` | rejection sampling over the box, because enumerating every legal tile is 1,681 collision reads at radius 20 and this runs per npc per timer. Modes match LostCity `MapFindSquareType`: `0` lineofwalk, `1` lineofsight, `2` none — the first two also require a clear walk/sight path from the candidate back to the origin. Failure returns the **source** coord, not -1: callers assign it straight into `npc_tele`. |
| `npc_getmode` | the setter had been here since npc modes landed and the getter had not, so content branching on it (`npc_getmode = opplayer2`) was always false — invisible for a sound, a behaviour that silently never happens for a guard. |

Two things the reference does that this does not, both stated in the script's own
header: the smoke puff (`spotanim_map` wants a MAP_ANIM zone sub-packet the
server does not send — the client already decodes one) and the 1-in-10 panic
teleport on damage (there is no damage hook for it to hang on).

## 3.11f The multiple-choice dialogue, and three caps on one string

`~p_choice2..5` works. It was the single biggest gap in the dialogue content —
LostCity calls it 879 times across 355 files — and closing it needed one new
command and three buffer sizes raised in step.

**The interface builds itself.** LostCity has four interfaces (`multi2..multi5`),
one per option count, each shipping fixed text components, so its `~p_choice3`
is six `if_settext` calls onto components it can name. rev 230 has ONE interface
with two components — a root and an empty layer — and its rows are `cc_create`d
by the clientscript `chatbox_multi_init(title, options)`, where `options` is the
rows joined with `|`. There is nothing for `if_settext` to address.

So the server has to *run a clientscript with string arguments*, and the 2004
protocol has no RUNCLIENTSCRIPT at all — the reference has nothing to port.
`runclientscript_ss(clientscript, string, string)` is the third command in the
`EXTRA_OPCODES` band (11002), and the clientscript id is an **argument**: it is a
cache id, and content names it (`^clientscript_chatbox_multi_init` in
`interface_chat/configs/chat.constant`, read out of the decompiled script 58).

**The answer is `last_slot`, not `last_com`.** Every row is a child of the same
component, so the reference's `switch_component (last_com)` cannot work here; the
row is the sub-id. That is the one thing a caller ported from LostCity must
change, and `if_addresumebutton` had to widen its arming range to match —
`MOCK230_RESUME_SUB_MAX` rather than slot 0, because arming slot 0 arms the empty
container and none of the rows.

### The three caps

A RUNCLIENTSCRIPT string argument is not a label, it is a *payload*, and three
places assumed otherwise. Hans's three-way choice is a 132-character list:

| cap | was | now |
| --- | --- | --- |
| `PKT_RUNCLIENTSCRIPT_STR_LEN` (packet parser) | 128 | 512 |
| `TASK_CS2_RUN_STR_ARG_LEN` (client CS2 task) | 80 | 512 |
| the encoder's packet size | 1024 | 4096 |

Each hid the next: raising the first left the second trimming, and the wire trace
by then showed the full string.

**Truncation does not fail.** The clientscript splits on `|`, counts what
survived, and sizes and positions that many rows — so a three-option question
renders as a tidy, correct-looking two-option one with the second cut off
mid-word, and nothing reports anything at any layer. Two things guard it now: a
`_Static_assert` tying the task buffer to the packet buffer (they can only be
raised together), and a selftest that searches the captured payload for the *last*
option's tail. A length threshold is the obvious check and a bad one — truncating
at 128 still yields a 152-byte packet, so any threshold loose enough to be safe
passes the bug.

Two harness additions came with it: `::talk <name> [op]` accepts an npc name (the
roster is built from the map squares in walk order, so a slot number is a
different npc between runs) and `::fight` with no argument takes the nearest
attackable npc.

## 3.11g The emotes tab, and how a dynamic child is addressed

The emotes tab is entirely client-built. Interface 216's onload (clientscript
699, `emote_init`) walks emote indices 0..55 and `cc_create`s a cell for each one
the cache gives an icon to, **using the index as the sub-id**. Labels come from
`enum_1000`, icons from `enum_1001`, and the "Perform"/"Loop" verb from
`enum_4998`/`enum_4999`. None of that needs a server.

What no cache config states is which *animation* an emote plays — that is
server-side in every RuneScape server — so the content here is one trigger and an
index-to-seq table (`interface_emote/`). The indices are named out of the cache's
own `enum_1000`, so the list and the labels the player reads cannot drift.

LostCity binds twelve `[if_button,controls:com_N]` triggers because its emotes
are twelve fixed components. Here there is **one** trigger on the container and
`last_slot` says which cell — the same shape as the choice dialogue (§3.11f), and
for the same reason: at this revision an interface that lists things builds the
list itself.

### Two client bugs it exposed, both about the same thing

`IF_SETEVENTS` carries a sub-id **range**, and nothing was reading it.

- **The arming gate matched component ids exactly.** A `cc_create`d cell has a
  runtime component id the server has never heard of, so `App_IfEventsGet`
  found nothing and every emote click was dropped before it was sent. The
  right-click menu still offered "Perform Bow" — the row comes from the cache's
  own op list — so the tab hovered, highlighted and named the verb, and clicking
  did nothing at all.
- **The outbound packet carried the child's own id.** RSProt's `If3Button` is
  `combinedId` + `sub` precisely so a dynamic child is addressed as *(container,
  index within it)*; sending `216:37984` instead of `216:2 sub=2` gave the server
  a component no name resolves and no script binds.

`app_if_button_target` is the one answer both now use. It also fixes the same
latent problem for the choice dialogue's rows, which are dynamic children too.

## 3.11h XP drops, and the two things that had to exist first

The XP-drop panel is interface 122, and it is entirely client-driven: its onload
registers a **stat-transmit listener** naming all twenty-four skills as triggers,
plus a per-tick timer, and the script diffs the experience it is handed against
what it kept from last time.

Two things were missing, at opposite ends of the same path.

**`IF_SETONSTATTRANSMIT` was in the VM's discard group** — parsed for its
operands (which it must be, or the stack unwinds wrong) and then thrown away, so
the listener was never registered. Half the reactive loop already existed:
`RS_CS2Host_NotifyStatChanged`, `stat_change_serial` and the
`stat_transmit_hooks` registry were all there, and `UPDATE_STAT` had been calling
the notifier the whole time. Registration and dispatch are now written, mirroring
the var-transmit pair exactly — same one-hook-per-component rule, same
`last_seen_serial` gate, same hidden/reclaimed handling.

**`CC_DELETE` was unimplemented**, and an unimplemented opcode aborts here rather
than no-oping (deliberately — see `StackMetaStub`). So the first time a drop
expired, the whole client went down on a two-line script:

```
[clientscript,script1006](component $c, int $sub)
if (cc_find($c, $sub) = ^true) { cc_delete; }
```

`cc_deleteall` takes a parent and clears its children; `cc_delete` takes no
operand and removes the *active* component, whatever the preceding `cc_find`
selected. `UITree_CcDelete` is `CcDeleteAll`'s per-child body applied to one
node: siblings keep their sub-ids, which is what a list removing one row expects.
Static children are refused — a script deleting a cache-built widget would leave
a hole nothing rebuilds.

With both in, the XP counter tracks (1,154 → 1,159 over a fight where it used to
sit still) and nothing aborts.

Two traps this left behind, both recorded because they cost real time:

- **The dispatch's hook bound must be snapshotted.** A dispatched hook re-arms
  its own listener, so reading `stat_transmit_hook_count` fresh each iteration
  lets a hook extend the loop it is being run from.
- **An early exit reads as a hang.** The abort is a `SIGABRT` partway through a
  headless run, which under a frame cap and a `pkill` looks exactly like a
  client that stopped producing frames. Check the exit code before diagnosing a
  loop; `exit=134` is an assert, not a spin.

## 3.11i The drag ghost, and why the CS2 inventory had none

"Drag and drop just snaps to the destination and doesn't render the dimmed
object." The *machine* was working the whole time — the deadzone, the five-cycle
dead time, the swap and the outbound packet are all correct — and only its
feedback was missing.

`emit_rs_inv_slots` has drawn the armed slot at `(dx, dy)` with `trans = 128`
since the TYPE_INV grid was written. rev 230's backpack is not a TYPE_INV grid:
it is `cc_create`d cells, so that path never runs and nothing else applied the
offset or the fade.

The generic drawable emit now does, and two things about the predicate are worth
recording because each one was a wrong first attempt:

- **Not the draw kind.** A rev-230 item cell emits as an ordinary
  `UITREE_EMIT_SPRITE` — the obj icon is baked into a scene atlas — not as
  `UITREE_EMIT_CC_OBJ`. Gating on `CC_OBJ` reads correctly and matches nothing.
  What makes a node the dragged one is that the drag machine armed it, which is
  a question about identity.
- **Not the node's own component id either.** `app_obj_cell_at` resolves a
  backpack cell to `149:0` plus a *slot*, not to the cell's runtime component id
  — the same (container, sub) addressing `IF_BUTTON` uses (§3.11g). So the match
  accepts both forms: a cell that is its own component, and a dynamic child
  whose index is the armed slot inside the armed container.

`UITREE_HOST_GET_INV_DRAG` reports the armed component id alongside the source/
slot pair for this, since the two shapes are named differently and only one of
the two names exists for either. `TORIRS_DRAG_DEBUG=1` prints when a drag arms
and every offset it takes — which is what showed that the machine was fine and
the renderer was not.

## 3.11j Who an npc is facing, when more than one client is watching

The last shared-state remainder of the multiplayer change (§6.1 step 1), and the
one worth writing down because **the plan for it was wrong and the code is
smaller than the plan**.

The symptom: NPC_INFO is encoded once per npc per tick and sent to every client,
so an npc's `FACE_ENTITY` is one number that all observers read. It was
`MOCK230_FACE_LOCAL_PLAYER` — `32768 + 2047`, "the player reading this" — which
is right for exactly the client the retaliation is being encoded to and names
somebody else on every other stream. Two clients watching one goblin: the goblin
faces whoever is looking at it.

The stated fix was "make the npc masks per-observer, the same shape of change as
the tracked sets". **It is not.** Two things had to be separated:

- *Which fields are dirty* is a fact about the **npc**. The reference agrees and
  is explicit about it: `World.cycle` calls `rsbuf.computeNpc` once per npc per
  tick, and only the *stream* is per-observer (`NetworkPlayer.npcInfo`). Nothing
  in `struct Mock230Npc`'s mask block is observer-dependent — the audit went
  field by field and found exactly one candidate.
- That one field's **value** was observer-dependent, and only because the id was
  a self-alias. `PathingEntity.setFaceEntity()` stores `this.target.slot + 32768`
  — the world-global pool slot. An absolute number means the same player on every
  stream, which is precisely what lets one encode serve everybody.

So the change is: `mock230_npc_face_player(npc, pid)`, one seam that all five
facing sites go through, writing `MOCK230_FACE_PLAYER_BASE + pid`; and
`UPDATE_PID` carrying `player->pid` instead of the 2047 sentinel, which is what
the reference sends too (`Player.onLogin` → `new UpdatePid(this.slot, …)`).
`MOCK230_FACE_LOCAL_PLAYER` is **deleted**, which is what makes the bug
unrepeatable: no site can reach for "the local player" because there is no such
constant.

**The two halves cannot be split**, and the headless client says so in three
builds:

```
SDL_VIDEODRIVER=dummy TORIRS_NET_DEBUG=1 TORIRS_MAX_FRAMES=900 \
  TORIRS_NET_CHEAT="fight" ./torirs --manifest manifest_osrs230_embed.ini \
  --user testc --pass test
```

`TORIRS_NET_DEBUG` is what turns the `entity_sync:` lines on. Without it the run
is silent and looks exactly like a run that proved something.

| built with | the client logs | what the npc faces |
|---|---|---|
| **both halves** (shipped) | `player 0 spawned` · `npc 25 faces entity 32768` | the player — `32768 + 0` |
| honest face id, `UPDATE_PID` back to 2047 | `player 2047 spawned` · `npc 25 faces entity 32768` | **nobody** — the client filed itself under 2047, so `World_PlayerGetByServerPid(world, 0)` returns NULL |
| honest `UPDATE_PID`, facing back to the self-alias | `player 0 spawned` · `npc 25 faces entity 34815` | **nobody** — 2047 stopped being anybody's pid |

Both failures are the *quiet* one: no error, no warning, an npc that keeps
whatever yaw it was walking with — the same symptom §3.11b's original facing bug
had, which is why these assertions name a number rather than "not -1".

The third row also says which call site does the work. It was produced by
mutating the **mode-machine** facing (`npc_run_mode`) and leaving the combat one
honest, and the run never logs `32768` at all: in the `fight` scenario the latch
is set by the mode machine, not by the retaliate path. That is the mechanism
behind "mutating retaliate alone leaves every test green" below.

**Cost, measured before choosing.** Per-observer state is priced per
(player × npc), and `MOCK230_NPC_MAX` is 2048:

| representation | B per pair | total | Δ world struct |
|---|---:|---:|---|
| **absolute pid + honest UPDATE_PID** (chosen) | 0 | **0** | 0% |
| per-pair `int16` face override | 2 | 32 KB | +3.5% |
| full per-observer mask block (132 B) | 132 | 2.06 MB | **+228%** |

`sizeof(struct Mock230Npc)` is 328 B before and after; the world struct stays at
946,112 B. The naive per-pair array is *affordable* — 32 KB is nothing, and that
is not why it loses. It loses because it creates a second source of truth for a
value the reference keeps single, it scales with `MOCK230_PLAYER_MAX`, and it
leaves a pid field that means "whoever is reading this" in place for every future
observer-relative id to inherit.

**What it is asserted against.** `embed_test.c` had zero npc coverage; it has an
`absorb_npc_info` beside `absorb_player_info` now, driving
`pkt_npc_info_reader_read` — the client's own reader — with widths off the
revision table. Two logged-in clients, one npc, alice hits it, and then, out of
the two *decoded* streams: each client was told its own real pid; the two pids
differ; both are tracking the npc; the face id is in the player half; **both
streams name the same absolute player**; that player is alice; and it is not
whoever happens to be watching.

Note the last two together. The naive expectation is that the two streams should
*disagree* — that is what per-observer masks would produce. Under an absolute id
they must **agree**, and the observer-relativity is resolved by UPDATE_PID
instead. Agreement is the reference's invariant.

**Proven able to fail.** Restoring the self-alias inside
`mock230_npc_face_player` turns "which is alice" red in the embed test and
"names the pid it is fighting" red in the selftest; restoring the 2047
UPDATE_PID turns "each client was told its own real pid" and "different pids"
red. Mutating only the *retaliate* call site does **not** go red — the npc's
per-tick combat facing re-writes the latch on the next tick — which is why the
mutation that counts is the shared helper.

### Adjacent, found, deliberately not fixed

- ~~**`world->local_pid` is never written.**~~ **It is**, and the sentence that
  said otherwise is kept here because of *how* it was wrong. It came out of
  `grep local_pid | grep -v esync` — and the one line that assigns it is
  `world->local_pid = app->esync.local_pid;` in `app_world_frame`, mirrored
  every frame just before `World_Cycle`. The filter that was there to drop the
  client-side field ate the assignment *to* the world-side one, and a grep whose
  exclusion matches the right-hand side of the only write reports "no writers"
  with total confidence. Both consumers are live and were live before this
  change — the npc right-click `(level-N)` suffix (`rs_minimenu_world.c`, via
  `World_PlayerGetByServerPid(world, world->local_pid)`, which is the fix
  `combat_hud.md` §3 landed) and the local-player-first tile claim in
  `world_cycle.c`. Neither changes behaviour under the honest pid: the local
  player is filed under whatever `UPDATE_PID` said — 2047 before, its real pid
  now — and the lookup asks with the same number either way.
- **`MOCK230_NMASK_DAMAGE2` has no writer**, so two players hitting one npc in a
  tick lose the first splat. Same family — a shared slot — and the reference's
  answer is a second hitmark pair on the npc, not per-observer state.
- **`npc_say` is delivered to `srv->active_player` only.** Wrong recipient, not
  wrong value; a broadcast question.
- **`npc_run_mode` reads `srv->active_player`** in a phase where it is nobody's
  turn, unlike `maybe_aggress`, which picks the nearest eligible victim. This
  change makes its face id honest about which player it named; it does not fix
  which player it picks.

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

Loop: engage → walk beside → swing every `attackrate` ticks → the npc retaliates
the first time it is hit → the death animation holds the corpse for the npc's
`death_delay` → despawn → respawn `respawnrate` ticks later at the spawn tile at
full health.

What it leaves behind expires on `^lootdrop_duration`
(`drop_tables/configs/lootdrop.constant`), which the engine now reads through
`mock230_ids()` instead of keeping its own 200 in `MOCK230_LOOT_TICKS`.

**Every tick count in that sentence is the npc's record, not a constant**
(2026-08-01). `death_delay` and `respawnrate` are fields on the `.npc` block,
defaulting through `general/configs/npc_default.npc`, and `attackrate` is its
`param=`. `MOCK230_DEATH_TICKS`, `MOCK230_RESPAWN_TICKS` and
`MOCK230_ATTACK_RANGE` are gone from `mock230.h`; `MOCK230_ATTACK_SPEED` remains
for the *unarmed* interval only, which is a player-side default. `death_delay` is
the tree's newest server-band field — `[npc.death_delay]` in `fields/npc.ini`,
opcode 155, `client = drop` because nothing on the client asks how long a corpse
lingers.

> ⚠️ **Three claims in this section were stale and are corrected below
> (2026-07-31). They were verified against the shipped code, not re-reasoned.**
> Do not build anything this section says is missing without grepping for it
> first — all three describe work that had already landed.

Npc hitpoints come from the **content** `.npc` block's `hitpoints = N`
(`mock230_content.c:861-863`, default 10 at :2201). *This section previously
said they scale off the cache's combat level as `level * 2`; they do not, and
the only `level * 2` in the combat code is the aggression cutoff at
`mock230_combat.c:772`.*

Two deliberate omissions, and one that no longer applies:

- ~~**No attack animations from the engine.**~~ **The engine does play them.**
  `player_attack_seq` resolves the swing and `mock230_combat.c:660` plays it
  every attack. Content can still override with `anim` / `npc_anim`.
- **A zero-damage hit is a *block* splat, not nothing.** Otherwise a miss is
  indistinguishable from the server having dropped the swing.
- ~~**Player death heals to full** rather than teleporting.~~ **Death
  teleports** — and as of 2026-08-01 none of it is C. `[queue,player_death]`
  (`player/death.rs2`) plays the animation, delays `^death_delay`, teleports to
  `^respawn_coord`, heals, restores energy and clears the prayers. The engine
  contributes two things: it stops the fight, and it holds a `dying` flag that
  gates a corpse acting. **That flag is cleared by the script**, in
  `mock230_combat_player_tick`, on the tick hitpoints come back above zero — so
  the length of a death is content's too. There is still no item loss and no
  death interface, which *is* deliberate.

Commands: `p_opnpc`, `npc_damage`, `damage`, `healenergy`, `uid`,
`npc_findhero`, `npc_attackrange`. `::fight <slot>` engages an npc without a
right-click, for headless sessions.

Two of those answered the wrong thing until 2026-08-01 and neither said so:
`healenergy` restored **hitpoints** rather than run energy (invisible, because
`[queue,player_death]` is its only caller and heals hitpoints on the line above),
and `npc_attackrange` pushed a C constant rather than the active npc's
`attackrange`, so a ranged npc's script was told "one tile" whatever its config
said.

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

Authored `.dbtable`/`.dbrow` under `server/scripts` load first
(`mock230_db_load`). Cache kinds 38/39 then fill via `mock230_db_load_cache`
(`mock230_dbinfo.c`) so ServerScript can `db_find(quest:id, …)` the same
tables CS2 already reads — the machine export `configs/all.dbtable` /
`all.dbrow` uses `columndef=`/`values=` and is skipped by the text reader.
Authored schemas keep priority (e.g. `interface_questjournal/configs/quest.dbtable`
names columns 0..19); boot reports `db tables loaded (N tables, M rows …)`.
`MOCK230_DB_COLUMN_MAX` is 64 so sparse cache columns (quest's 48) fit.

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
| adjacent (or *on* the tile, for a ground obj) | `[opnpc<n>]` / `[oploc<n>]` / `[opobj<n>]`, then the engine's own verb handling **only if nothing was bound** and only for the kinds that still have any — `[opobj<n>]` has none, so a miss there is `Player.defaultOp`, the message and nothing else. A script that aborted stops here either way, and with no script pack there is no fallback at all (§3.18) |
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

**Facing a loc or obj** is engine behaviour, not content. LostCity's
`PathingEntity.setInteraction` records a fine face point
(`CoordGrid.fine(x, width)` / `fine(z, length)` = `pos*2+size`) for non-pathing
targets; `clearInteraction` leaves that stash alone. After movement in the
player phase, `reorient()` ships `FACE_COORD` when `stepsTaken === 0` and
consumes the stash. Content (`pickup.rs2`, `tables.rs2`) never calls
`facesquare` for ordinary take/put — the engine turns the player. Here that is
`face_target_x/z` on the player, set by `mock230_world_interaction_set` for
`LOC`/`OBJ`, and `player_reorient` after `advance_player` +
`mock230_world_process_interaction`. Scripted `facesquare` / `npc_facesquare`
emit the same absolute half-tiles (`(tile<<1)+1`); yaw is client `atan2`.

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

### 3.13e Two commands the reference does not have

The opcode table is generated from LostCity's `ScriptOpcode.ts`, so an opcode
this engine needs and the reference lacks has nowhere to come from. Two do, and
they are both about the same thing — the rev-230 rule that **a component is inert
until the server arms it**, which the 2004 protocol has no concept of at all.

They live in `EXTRA_OPCODES` in `src/serverscript/gen_opcode_meta.py`, in a band
one past the reference's highest id (10003) so a future LostCity opcode can never
collide:

```
11000  if_setevents(component, from, to, events)   arm ops on a component
11001  if_opensub(component, interface, type)      mount a panel into a slot
11002  runclientscript_ss(clientscript, str, str)  see docs/runclientscript.md
11003  runclientscript*(clientscript)(args...)     the vararg form of the above
11004  p_countdialog_noprompt                      wait for a number, no prompt
```

`p_countdialog_noprompt` is the wait half of `p_countdialog` (2071) without its
packet. At 2004 the two are inseparable, because the chatbox prompt is the only
thing that can produce a number; at rev 230 `resume_countdialog` is an ordinary
CS2 opcode, so an interface already on screen can answer a parked script itself.
The bank PIN keypad is the case that forced the split — `p_countdialog` there
would open a prompt that echoes each digit as it is typed, over a screen whose
whole purpose is that the digits are never typed. See
[`bank_pin_server_reqs.md`](bank_pin_server_reqs.md) §4.

`if_setevents` is the one that matters. Without it the cache still says a widget
*has* an op — the client hovers it, highlights it, puts the verb in the menu —
and the click then runs the component's own clientscript and sends nothing. That
was the whole of "the minimap orbs aren't clickable": `orbs:runbutton` carries
`op1=Toggle Run`, clientscript 7557 flipped the client's own copy of
`option_run`, and no packet ever left the client.

`if_opensub` generalises the reference's `if_openmain` / `if_openside` /
`if_openoverlay`, which bake one fixed slot into each command name. Three names
is the whole 2004 vocabulary; the rev-230 gameframe has 24 slots and panels nest,
so the target has to be an argument.

The event mask is **v1**, and that is a trap worth stating: rev 230 sends
`IfSetEvents` (i32, ops at bits 1-10) while rev 239 sends `IfSetEventsV2` (i64,
ops moved to bits 32+). This server speaks the 230 wire against a 239 cache, so
the v1 layout is correct even though the cache is newer, and a v2 mask over a v1
packet arms nothing and reports no error. `^if_event_op1` and friends are in
`server/scripts/engine_osrs230.constant`, kept out of `engine.constant` because
that file is a byte-for-byte port of the reference's.

One consequence for content authors: a component uid is `(interface << 16) |
child`, and a compiled trigger key has 21 bits for its subject. `bankmain` is
interface 12 and fits; `orbs:runbutton` is `160 << 16` and does not. Those
scripts compile **name-addressed** (`ssc_compile.c`) and the engine resolves them
through `mock230_scripts_run_if_button_named`, which asks the component pack for
the uid's name. The on-disk format is not widened — it is LostCity's, and
`test-ss-roundtrip` proves this compiler reproduces it byte for byte.

See `docs/UI_ERA_PORTING_GUIDE.md` for how the three reference servers each
answer a different part of this.

### What is deliberately unimplemented

166 of 396 as of this writing. The gap is not uniform, and four groups are
blocked on data rather than on effort — worth knowing before picking one up:

| family | why not |
|---|---|
| ~~`oc_param`~~, ~~`nc_param`~~, ~~`lc_param`~~, ~~`struct_param`~~ | **All four are done.** See §3.13e. |
| `oc_cost`, `oc_members`, `oc_tradeable`, `oc_desc`, `nc_desc` | not decoded. A dat2 npc record has no description at all — it is server-driven at this revision. |
| `oc_op` | `known = 0` in the meta table: no signature exists, so the VM correctly refuses to execute it rather than guessing an arity. |
| `stat_add`, `stat_sub` | same arity as `stat_boost`, but nothing in this repo pins whether they move the *base* level or the *boosted* one. |

The rule the last batch followed, and which the next one should: **an opcode that
cannot be answered from real data is better left to the VM's loud stub than
implemented from a plausible guess.** The stub says it is missing; a wrong
implementation is silent. `oc_desc` was written and then removed on exactly this
ground when `Mock230ObjInfo` turned out to carry no description field.

There is a second rule the `nc_param` change added, and it points the other way:
**check whether the blocker is still true before quoting it.** The row above had
called all four blocked on data. For the npc half the data was never missing —
`read_combat_params` in `mock230_npcinfo.c` had been walking those exact param
rows since it was written and discarding all but fourteen keys, one line from
where the table now goes. The change was ~120 lines and no new decoding at all.

## 3.13e The `*_param` family

All four config-param opcodes are landed: `oc_param` and `nc_param` in the big
switch, `lc_param` and `struct_param` in `src/net/mock/mock230_ops_param.c` —
the second per-domain opcode file after `mock230_ops_db.c`.

**The stack routing has exactly one implementation.** `mock230_push_typed_param`
lives in `mock230_ops_param.c` and is declared in `mock230.h`; it used to be a
`static push_typed_param` in `mock230_scripts.c`, which made it unreachable from
a domain file. It was *moved*, not copied, because the whole point of it is that
the declared-vs-stored disagreement is handled in one place. The reference does
the same thing in the same place — `StructOps.ts` and `LocConfigOps.ts` both
branch on `paramType.isString()`, never on what the record stores.

**One table type, four users.** `mock230_paramtable.{c,h}` is the flat
`(owner, key) -> row` store: one `add`, one `read`, one `compare`, one `qsort`,
one binary search. `mock230_locinfo.c` and `mock230_structinfo.c` build theirs
on it. (`mock230_objinfo.c` and `mock230_npcinfo.c` still carry their own
copies, which is the obvious next cleanup — behaviour-identical, and their
selftests already cover it.)

**The sort is explicit and it is load-bearing.** Measured on `cache.osrs239`:

| table | records | rows | string rows | records with ≥2 params | of those, out of key order |
|---|---:|---:|---:|---:|---:|
| loc | 62,194 | 1,709 | 0 | 599 | **525 (87.6%)** |
| struct | 3,988 | 20,751 | 6,115 | 2,833 | **1,847 (65.2%)** |
| obj | 33,747 | 53,853 | 2,722 | 7,485 | 5,097 (68.1%) |
| npc | 16,292 | 29,869 | 204 | 4,616 | 3,690 (79.9%) |

A record's params arrive in the order the cache wrote them, which is not
ascending by key. A binary search over an almost-sorted array does not crash —
it misses, the miss reads as "this record has no such param", and content
answers with the declared default. Wrong everywhere, loud nowhere. No
`RSCACHE_PARAM_LONG` rows exist in any of the four groups, so nothing is being
silently dropped.

**Retained cost.** loc 41 KB, struct 877 KB, against a 73 MB boot RSS. The
triage's "60k+ records at boot and a real memory question" was true only of
loc's *record count*; loc's retained answer is 41 KB. Decoding is eager because
lazy is not available as a design — the records are files inside one dat2 group
and the group is the unit of compression, so reaching one means decompressing
all of them. Boot cost is ~130 ms for the two together.

**Not read through the scene, deliberately.** `mock230_scene.c` already decodes
every loc record, but that table has the *scene's* lifetime and is rebuilt at
every rebuild boundary; params would be unavailable before first login and would
flicker. (Separately: `g_loc_configs` is scene-lifetime but scene-*independent*,
so the scene re-pays ~94 ms and ~45 MB on every rebuild for no reason. Worth
hoisting; out of scope where it was found.)

**The check is `make -C src test-mock230-param`.** Two halves.
`src/net/mock/test/param_test.c` re-decodes both groups with the rscache decoder
directly and asks the table for *every* row it saw — exhaustive rather than spot
checked, because a spot check would have to be lucky. It also counts the
out-of-order records and fails if that count is ever zero, since that would mean
the assertion had quietly stopped testing anything. The second half compiles a
RuneScript that assigns the result to a typed local (`def_string $s =
struct_param(...)`), because a value pushed to the wrong stack does not fail at
the call — it underflows later. No ids are written in the test file; every
subject is located in the decoded data at run time.

Five mutations were shown to fail it before it landed: dropping the `qsort`
(6,201 struct + 526 loc misses), forcing every result onto the int stack (string
stack underflow), treating only `type=i` as int-typed (the `'1'`-typed subject
routes to the string stack), answering an absent row with 0 instead of the
declared `default=`, and reversing `lc_param`'s pop order.

**Still not landed in this family:** `obj_param` (3509), which pops *one* int and
reads the *active obj* rather than an explicit id. Zero callers in the reference
tree, and nothing in `src/net/mock/` ever sets an active obj — so the VM's
pointer requirement would abort before a handler could run. `loc_param` (3011)
is landed; it lives with the rest of the loc reads, in §3.13f.

### What `oc_param` and `nc_param` still get wrong

**~~Param defaults are not read~~ — int defaults are now.** `configs/all.param`
states a `default=` on 469 params, 365 of them `-1`; `load_param_types` reads
them and `push_typed_param` answers an absent row with the declaration instead
of 0. A param that declares none reads as 0, which is the cache's own value —
`RSCache_Dat2ConfigParamInit` leaves `default_int` on the zeroed record, and
LostCity's handlers push `paramType.defaultInt` the same way. There is no
"undeclared" sentinel in the table: with -1 the single most-declared default,
any in-band marker would collide with real data, so undeclared is simply
stored as 0. The selftest pins all three shapes through both opcodes — absent
with no declaration (`rangeattack` → 0), `default=-1` (`param_87`),
`default=4` (`attackrate`), `default=526` via the npc table (`param_46`) —
and each assertion was shown to fail under a mutated loader and a mutated
push before landing.

Two halves remain, both silent in the same way the int half was:

- **`defaultstr=` (311 declared) is read by nothing.** An absent string param
  reports `""` where the reference reports the declared string.
- **The server-overlay `.param` declarations are not walked at runtime.**
  `load_param_types` reads only `configs/all.param`, so an overlay-declared
  param (`death_anim`, `death_drop`, `next_loc_stage`, …) has no type and no
  default in the VM's table — its type falls back to the stored value's own
  kind, and its symbolic default (`default=human_death`) is invisible. The
  overlay grammar also spells types as words (`type=seq`) where the dump file
  uses the cache's single chars, so a future overlay walk must not reuse the
  dump parser's `value[0]`: six of the word types collide on `s` alone.

  **Half of the second one is closed as of §3.13g.** An overlay param's *value*
  is now reachable by id — `apply_param` files each authored row under its param
  id and `npc_param` reads it ahead of the cache record — because `death_drop`
  is exactly one of these and a cache-only `npc_param` would answer 0 for it.
  What is still missing is the overlay's *declared type and default*, which is
  the part above about `load_param_types`. The two are independent: authored
  values are ints, so the stored-kind fallback is right for all 205 of them
  today.

## 3.13f The loc config reads

`src/net/mock/mock230_ops_loc.c` — the third per-domain opcode file. Five
opcodes, all pure reads of a loc's own config record:

| op | id | signature (`engine.rs2`) | reads |
|---|---:|---|---|
| `loc_param` | 3011 | `(param $param)(any)` | the **active loc**'s param map |
| `loc_name` | 3010 | `()(string)` | the **active loc**'s name |
| `lc_name` | 4104 | `(loc $loc)(string)` | a named loc's name |
| `lc_width` | 4107 | `(loc $loc)(int)` | `size_x` |
| `lc_length` | 4103 | `(loc $loc)(int)` | `size_z` |

The loc family's *mutating* half — `loc_add`, `loc_change`, `loc_del`,
`loc_find`, `loc_coord`, `loc_type`, `loc_angle`, `loc_shape`, the two find-all
iterators — stays in `mock230_scripts.c`'s switch with the scene and the revert
queue it needs. Only the config reads moved out, which is why they could.

**`loc_param` pops one int; `lc_param` pops two.** Same word, same result,
different stack discipline — `loc_param` is told nothing and reads the active
loc, `lc_param` is handed an id. Reading one as the other leaves every later
value on the wrong rung of the int stack and nothing fails at the call.
`lc_param` is in `mock230_ops_param.c` with the rest of its family (§3.13e);
`loc_param` is here with the rest of the active-loc reads. They are one word
apart and two files apart on purpose.

**The active loc is a slot, not a pointer**, exactly as it is in the big switch:
the VM's active-entity pointer holds `slot + 1` so that non-NULL means "a loc is
active" and the VM's own pointer-requirement check (`require = 0x040`) works
unmodified. The requirement guarantees the pointer is *present*, not that the
slot is still live — a script can suspend between `loc_find` and here and a
scene rebuild reallocates the array — so the handler re-resolves through
`mock230_scene_loc` and aborts on a dead slot.

**What `mock230_scene_find_loc` must not be used for.** It ends in
`return loc_id >= 0 ? fallback : fallback;` — both branches the same — so its
`loc_id` argument does not filter. It returns the first loc on the tile whatever
id it is asked for, which is right for its own caller (a stale OPLOC id must
still resolve to the door somebody else already opened) and useless as an
existence check. `mock230_loc_known` is the existence check.

**What `mock230_locinfo.c` now retains**, measured on `cache.osrs239`, all of it
built in the one decode pass that was already happening for params:

| table | rows | bytes | note |
|---|---:|---:|---|
| params | 1,709 | 41,016 | of 62,194 records, only 1,070 carry any |
| names | 30,033 | 565,681 | one concatenated blob + a sorted (id, offset) index |
| footprints | 17,309 | 138,472 | only the records that are not 1x1 |
| categories | 8,407 | 67,256 | config opcode 61, added 2026-08-02 with the loc category rung (§3.18) |
| known-id bitmap | — | ~7.8 KB | one bit per id; the reference's `LocTypeValid` |

The category rows are `int32_t`, not sized to the 2,474 this cache tops out at:
the namespace has a `server_base` of 8192 and an authored `.loc` block can carry
one of those, so sizing to the cache would make the *authored* half the case that
overflows.

Names are ~560 KB, not the ~700 KB the previous revision of that file estimated
and declined to pay: the estimate was `strdup` per record — 30,033 allocations
with headers, plus a pointer array over all 62,194 ids. A blob and an index over
only the named records is two allocations. Footprints store one byte each
(measured maxima 17 and 33; both are `g1` on the wire) and an id that is not in
the table answers 1x1, which is the decoder's own default rather than a guess.

**One loc opcode is deliberately NOT landed, and it is not blocked on effort:**

- **`loc_anim` (3002) — 56 uses / 23 files, the largest single loc gap. Blocked
  on a missing wire packet.** The reference is `LocOps.ts:50` →
  `World.animLoc(...)`, i.e. a **zone event**. `zone_sub_opcode` in
  `mock230_encode.c` enumerates every zone sub-packet this server has —
  `LOC_ADD_CHANGE`, `LOC_DEL`, `OBJ_ADD`, `OBJ_DEL`, `OBJ_COUNT` — and there is
  no loc-anim among them. Landing it means a new `MOCK230_ZONE_EV_*` kind, a new
  encoder arm and headless-client verification that the loc actually animates.
  That is engine work outside the ops-file seam; file it as its own item.

One was measured and cut on data, not on scope:

- **`lc_desc` (4102) — 0 of the 62,194 loc records carry a `desc`.** The field is
  gone from OSRS loc configs; a handler could only ever push the reference's
  `'null'` fallback. There are also zero callers in the reference tree.

**Three that this section refused and that landed on 2026-08-02**, kept here with
the refusal beside the answer, because the refusals were the reason the `oploc`
fallback row stood and both were plausible when written:

- **`loc_category` (3003, 38 uses / 16 files) and `lc_category` (4100, 3 / 1).**
  The refusal was *"the linked decoder throws the field away"* and it was true —
  `dat2_config_loc.c` case 61 was `g2(buffer); // Skip unsigned short` where
  `dat2_config_npc.c:668` decodes `category` explicitly. It asked for two things
  and got both. (i) Opcode 61 was confirmed as `category` **against the data**
  rather than against a client, since neither client in this tree decodes it: the
  ids group semantically (684 is 63 records, 43 of them named *Bank booth*; 907 is
  360 Bookshelves) and share their space with npc opcode 18 and obj opcode 94, on
  9 and 3 ids respectively, each the same concept on both sides. `LocType.ts:179`
  agrees. (ii) The `EXCEPTIONS.md` edit was made and the fidelity suite earned its
  keep on the first run — see §3.18, finding 2. Both push **−1** for "states
  none", not the reference's raw 0, because `pack/category.pack` reserves 0.
- **`lc_debugname` (4101, 1 / 1).** The refusal was *"`debugname` is a LostCity
  build-time symbol; the dat2 record has no such field"* — correct about the
  cache and a category error about the opcode. `debugname` is not supposed to be
  a cache field: it is the `[block]` header of a config file, and this tree has
  that table, `pack/loc.pack`. `mock230_content_symbol_name(MOCK230_PACK_LOC, id)`
  with the reference's own `'null'` fallback. It landed for its single caller,
  `stairs.rs2:431`'s `mes("Unhandled stairs: <lc_debugname(loc_type)> at")` — the
  line that tells a port which locs it missed, which is worth more during a port
  than after one.

**The check is `make -C src test-mock230-loc`**, source
`src/net/mock/test/loc_test.c`. Half 1 re-decodes the whole loc group with the
rscache decoder directly and asks the tables about *every* record — both
directions, because a sparse binary search that is off by one returns a
*neighbour's* string, and 32,161 records being nameless is what makes "reads
back nothing" the sharper of the two assertions. It also asserts the 1x1
majority, since the footprint table stores only overrides and a version
answering 0x0 for a miss would pass every check that looked only at the 17,309
records in the table.

Half 2 builds a real scene at the selftest's own zone, adds real locs to it and
sets the active-loc pointer the way the server does. `def_string $s = loc_name;`
is the string-stack assertion — a value pushed to the int stack underflows and
aborts inside the VM, which an int comparison could not see.

Mutations shown to fail it: transposing `lc_width`/`lc_length`; pushing
`loc_name` to the int stack; giving `loc_param` `lc_param`'s two-pop arity;
making `check_loc_id` always pass; answering a footprint miss with 0x0;
returning the nearest row instead of NULL on a name miss; dropping the loc param
`qsort`; and reading the active-loc pointer as a raw slot instead of `slot + 1`.

One mutation that **does not** fail it, stated rather than hidden: removing the
`qsort` over the *name* and *footprint* tables. `archive->file_ids` happens to
be ascending on this cache, so those two sorts are no-ops today. They are kept
because nothing promises that ordering — but this test does not prove them, and
a reader should not believe it does. (The *param* sort is a different matter and
is emphatically load-bearing: 525 of the 599 loc records carrying two or more
params write them out of key order.)

## 3.13g The npc reads and the hunt searches

`src/net/mock/mock230_ops_npc.c` — the fourth per-domain opcode file. Seven
opcodes: four reads of an npc's own config record, three searches over the
roster.

| op | id | signature (`engine.rs2`) | reads |
|---|---:|---|---|
| `npc_param` | 2529 | `:563 (param $param)(any)` | the **active npc**'s type's param map |
| `npc_category` | 2505 | `:521 ()(category)` | the **active npc**'s type's category |
| `nc_category` | 4000 | `:741 (npc $npc)(category)` | a named type's category |
| `npc_hasop` | 2523 | `:649 (int $op)(boolean)` | the **active npc**'s type's menu ops |
| `npc_huntall` | 2526 | `:25 (coord, int $distance, int $checkvis)` | fills the shared iterator |
| `npc_hunt` | 2525 | `:21 (coord, int, int)(boolean)` | the nearest of the same set |
| `npc_findcat` | 2517 | `:545 (coord, category, int, int)(boolean)` | the nearest of a category |

The npc family's *addressing, lifecycle and mode* half — `npc_add`, `npc_del`,
`npc_find`, `npc_tele`, `npc_setmode`, `npc_queue`, `npc_settimer`,
`npc_changetype`, the find-all iterators — stays in `mock230_scripts.c`'s switch
with the world state it mutates. Only the record reads and the pure searches
moved out, which is why they could.

### `npc_param` was implemented and wrong

This is the reason the file exists. `mock230_scripts.c` has carried a
`case SS_OP_NPC_PARAM:` since before the param family existed, and what it did
was compare the popped param id against one
`mock230_content_symbol(MOCK230_PACK_PARAM, "death_drop")` — a game-facing name
spelled in C — push that one field, and push **0 for every other param**. It
never consulted `mock230_npc_param`, which is loaded and public and would have
answered. It never reached `mock230_push_typed_param`, so every result landed on
the int stack whatever `configs/all.param` declared. It ignored `default=`.

Nothing in this repo could see it. `gen_opcode_coverage.py` keys off the
presence of a `case SS_OP_*:` label, so 2529 was counted as covered; the
load-time gap report was silent; `--selftest` was green. Meanwhile it is **291
uses across 137 files** of the reference tree — the largest npc figure by a
factor of three — and the already-committed combat slice reads through it:
`skill_combat/combat_stats.rs2:383` (`npc_param(stabdefence)`), `:459`
(`npc_param(strengthbonus)`), `:489` (`npc_param(damagetype)`). Every npc
defence roll and every npc max hit in that slice was computed from bonus 0.

**The old case is now unreachable dead code**, because the per-domain hooks run
before the switch. It should be deleted; deleting it is a second edit to
`mock230_scripts.c` and the lane that landed this file was allowed exactly one.
Recorded here so a reader does not change it and expect anything to happen.

### Fixing it needed the overlay params to become visible first

The obvious fix — read `mock230_npc_param`, which is the cache record's param
table — is a **regression** on its own, and the hardcoded case was hiding why.
`death_drop` is a *server-band* param: it is authored in a rank-1 `.npc` overlay
(`param=death_drop,bones`, 17 blocks plus the `[default]`) and is nowhere in the
cache record. A cache-only handler answers the declared default, and the drop
tables' `obj_add(npc_coord, npc_param(death_drop), 1, …)` — 7 call sites — adds
obj 0.

So `apply_param` in `mock230_content.c` now files each authored row **under its
param id** as well as into the named C field it already wrote, and
`mock230_content_npc_param()` reads it back. `npc_param` consults that first and
the cache record second: rank 1 over rank 0, which is the merge
`CONTENT_ARCHITECTURE.md` §3.1 already states and the precedence
`npc_def_seed_from_cache` already gives the combat bonuses.

Measured: **39 npc defs carry 205 authored rows** between them (the `[default]`
block's five are inherited by every def, which is the same inheritance
`def->death_drop` has always had). A name the param pack does not know is filed
under no id and still writes its field — several of the twenty names
`apply_param` accepts are engine spellings older than the pack, and refusing
them here would break loading rather than teach anyone anything.

This is a partial close of the gap §3.13e records as "overlay `.param`
declarations are invisible". The *values* are now visible to `npc_param`; the
*types* still are not, because `load_param_types` reads only
`configs/all.param`. Authored values are always ints, so the typed push sees
`declared == 0` and falls back to the stored kind — correct today, and a param
this tree later declares `type=string` in an overlay would abort loudly at the
push rather than desynchronise the stacks, which is the right failure.

### Reading a field means reading the *ungated* record

`mock230_npcinfo()` reports a "Someone" placeholder for any record with no name.
That is right for text, which is what it was written for, and wrong for a field:
of cache.osrs239's 16,292 npc records, **9,149 carry a category and 1,585 of
those have no name**; 10,505 declare a menu op and 177 of those have no name.
Read through the gated accessor, `npc_category` would report "no category" for
every multinpc instance in the game. `mock230_npcinfo_record()` is the ungated
row and returns NULL rather than a placeholder that looks like a record.

`category` is config opcode 60 and the linked rscache decoder
(`dat2_config_npc.c:668`) has always decoded it; `mock230_npcinfo.c` was
throwing it away. Retaining it costs one `int` per record — 65 KB — and no new
decode pass. The value is a raw cache id compared against `pack/category.pack`
names on the content side, so no category name is spelled in C. **Note that
`category.pack` today names only *obj* categories**; the 982 distinct npc
category ids in this cache have no names yet, which is a content-side gap
(triage §7.6b) rather than an engine one.

### The active npc is a slot, and there is no secondary

`SSVM_SetActive(..., SSVM_ENT_NPC, ...)` is paired with
`state->host_tag = slot + 1` at every call site in the server, and the slot is
what survives: a parked script outlives its tick, and an npc can despawn or have
its slot reused while the script waits. Nothing here ever sets the *secondary*
npc, so a `.npc_category` / `.npc_hasop` dot form is refused by the VM's own
pointer table (`require2 = 0x020`, never added) before reaching a handler. That
is a pre-existing tree-wide simplification, shared with `npc_type` and
`npc_coord`, not a decision of this file.

### `npc_huntall` is not `npc_findallany`

The two look interchangeable and are not. `NpcHuntAllCommandIterator`
(`ScriptIterators.ts:274-280`) skips any npc whose **type declares no `op[1]`**
— the second menu op, conventionally Attack. So "huntall" means every npc nearby
that something can be done to, and an implementation without the filter hands
content the scenery. `npc_hasop`'s own `npcType.op[op - 1]` indexing
(`NpcOps.ts:538`) is what fixes which element that is.

Two more details are the reference's and are not the obvious choice: `npc_hunt`
and `npc_findcat` **filter** by the Chebyshev range but **rank** by
euclidean-squared (`NpcOps.ts:305`, `:385`), and the tie-break is `<=`, which
keeps the **last** candidate at an equal distance where `<` would keep the
first. The two measures disagree on diagonals, so ranking by the filter's
measure picks a different npc.

`checkvis` is LostCity HuntVis (`0` off, `1` lineofsight, `2` lineofwalk), and
filters candidates with a scene ray cast — the same gate `npc_find` and
`npc_findall` honour.

### Eight families deliberately NOT landed

Every one is `known = 1`, so the VM's stub pops what the signature declares,
pushes zeros and says so once per env; the stacks stay consistent and the gap
stays visible. Counts re-measured over `LostCity_Server/content/scripts` with
comments stripped and `engine.rs2` excluded.

- **`npc_walk` (108 uses / 45 files) and `npc_walktrigger` (9/2) — the largest
  npc gap in the tree, and it is engine state rather than an opcode.**
  `NpcOps.ts:451` is `activeNpc.queueWaypoint(x, z)`: a queue the tick drains.
  `struct Mock230Npc` has no destination and no waypoint queue, and the only
  mover — `mock230_world_npc_walk_to` — takes one step toward a target its
  caller supplies, from inside phase 4 and the combat tick. Landing it honestly
  means a waypoint queue on the npc plus a drain in `advance_npcs`, ahead of the
  mode machine and behind combat. That is engine work in `mock230_world.c`, not
  an ops file. Faking it as a teleport would make 45 files *look* ported while
  every npc arrived instantly — worse than the stub, because it is silent.
- **`npc_statheal` (16/10), `npc_statsub` (9/6), `npc_statadd` (1/1)** — all
  three write `npc.levels[stat]` clamped against `baseLevels[stat]`. There is no
  per-npc mutable stat store here: `SS_OP_NPC_STAT` reads
  `npc->def->attack/strength/defence/…` straight off the content block, and
  hitpoints is the only number that moves.
- **`npc_changetype_keepall` (36/17)** — verified at `Npc.ts:426-449`: the
  *only* difference from `npc_changetype` is whether `levels[]`/`baseLevels[]`
  are re-derived, i.e. the store the row above says does not exist. The two are
  the same operation in this engine, so the cheapest correct fix is one
  fallthrough line in `mock230_scripts.c`. Not done from a domain file, where
  the alternatives are copying that case's body or shadowing a working opcode.
- **`npc_heropoints` (13/13)** — needs per-npc damage attribution keyed by
  player, and the drop-table consumer that reads it. Neither exists;
  `SS_OP_NPC_FINDHERO` pushes the constant 1 because there is one player.
- **`npc_sethuntmode` (14/7), `npc_sethunt` (1/1)** — a `hunt` config namespace,
  per-npc huntMode/huntrange, a HuntVis test and a per-tick hunt pass. One
  feature wearing several opcodes. `npc_hunt`/`npc_huntall` above are the two
  that are pure searches and need none of it.
- **`npc_arrivedelay` (2/2)** — needs `lastMovement` against the current tick
  (`NpcOps.ts:542`), a field written by the mover.
- **`npc_inrange` (2/1)** — `targetWithinMaxRange()` (`Npc.ts:635`) reads the
  npc's `target`, `targetOp`, spawn tile and `type.maxrange`. This server has a
  `combat_target` and nothing else that answers "what is this npc interacting
  with".
- **`npc_findallzone` (0 uses) and `nc_desc` (0 uses)** — no caller anywhere in
  the reference tree, and a dat2 npc record has no description at this revision.

**The check is `make -C src test-mock230-npc`**, source
`src/net/mock/test/npc_test.c`. Half 1 re-decodes the whole npc group with the
rscache decoder and asserts `category` and `ops[]` on *every* record, plus the
nameless case in both directions — the ungated row answers, the gated one would
have answered 0. Half 2 compiles a RuneScript, binds `mock230_ops_npc` over a
zeroed heap `struct Mock230Server` (no world, no socket, no pack) and drives it.
`def_string $s = npc_param(...)` is the string-stack assertion.

The hunt layout is designed so one assertion has three distinguishable answers:
two npcs joint-nearest at euclidean distance 1 (slots 3 and 5) and a third at
Chebyshev 1 but euclidean 2 (slot 11). The correct answer is slot 5; ranking by
Chebyshev gives 11 and a `<` tie-break gives 3.

Mutations shown to fail it: indexing `npc_hasop` 0-based; pushing every
`npc_param` result to the int stack; never consulting the param table (the
shipped bug); keying `npc_param` on the slot instead of the type; **skipping the
authored-overlay lookup** (the `death_drop` regression); reading `category`
through the name-gated accessor; never copying `category` out of the decoded
record; dropping the `op[1]` huntable filter; ranking by the Chebyshev range the
search filters with; keeping the first at an equal distance; and popping
`npc_findcat`'s or `npc_huntall`'s arguments in the wrong order.

Two of those needed the subject search tightened before they could fail, and
both are the `serverscript-guard-testing-confounds` shape. The authored-param
subject must be one whose value differs from the param's declared `default=` —
the first one found was `attackrate = 4` against a default of 4, so "read the
overlay" and "fall through to the default" produced the same number. And the
`npc_hunt` layout had to put a candidate at Chebyshev 1 but euclidean 2, because
three npcs in a row on one axis rank identically under both measures.

One thing worth knowing about that last one: **membership assertions cannot
catch a swapped coord/distance**, because a coord literal packs to a number in
the millions and a search with a radius of millions still finds everything. The
assertion that catches it is `npc_huntall` at distance 0 collecting *nothing*.

## 3.15 Player persistence is an ini per player

`src/net/mock/mock230_save.c`. One file per player under `saves/`
(`MOCK230_SAVES=dir` overrides).

> **It is not wired.** `mock230_save_player` and `mock230_load_player` have no
> callers — `grep -rn 'mock230_save_player\|mock230_load_player' src/` finds
> the definitions and the two prototypes and nothing else — and they have had
> none since the commit that added the file. Everything below describes a
> format that round-trips when called directly and is called by nothing, so
> every session still starts from the defaults. Written as intent and read as
> fact once already, which is why this paragraph is here.
>
> What that costs is a *case*: "a returning player" cannot be tested. It is why
> `[proc,newplayer_setup]` (player/newplayer.rs2) gates on a `scope=perm` varp
> even though nothing yet makes a perm varp outlive anything — an ungated seed
> is correct exactly until the day this file is connected.

The intended order is: read at login, over the defaults `mock230_world_init`
and `[login,_]` have already put down.

```ini
[player]
version = 1
name = embed
x = 3222
[stats]
; <stat> = <level> <boosted> <xp_tenths>
3 = 10 10 11540
[inv]
; <slot> = <obj> <count>
4 = 1321 1
```

Ini rather than a binary blob because a save is content a human has to be able
to read and fix: a corrupt binary save is a bug report with no evidence in it. It
also means a save survives a struct change — an unknown key is skipped and a
missing one keeps its default, so adding a field does not invalidate every
existing save. `version` is bumped only when a key changes *meaning*, which is
the one case a reader cannot detect for itself.

Three things about it are load-bearing:

- **What persists is content's decision.** A varp is written only when its
  `.varp` config says `scope=perm` — LostCity's rule, and the field
  `Mock230VarpDef.scope_perm` had been parsed off those configs and read by
  nothing since that reader was written. An *undeclared* varp is server
  bookkeeping and is deliberately not saved, matching the transmit gate's
  default.
- **Write-then-rename.** A crash mid-write leaves the previous save intact
  rather than a truncated one, which for a save file is the difference between
  losing a session and losing a character.
- **The name is sanitised to `[a-z0-9_]`.** It comes off the login screen, so it
  is attacker-controlled; `../../etc/passwd` is a valid login name and must not
  become a path. Characters outside the set are *dropped* rather than mapped, so
  `..` cannot survive as `__`.

The save order matters at teardown: it runs before `mock230_bank_shutdown`,
which frees the container the save is about to read.

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

**Which component the orb is** was wrong for a long time, and the way it was
wrong is worth keeping. It was 160:53, argued from OpenRune's rev-235 gameval
table (which names 160:55 `orbs:worldmap` and 160:53 `wiki_icon`): components
must have been inserted between the revisions, so the rev-230 orb must be the
lower id. The cache this server reads says the opposite twice —
`interfaces/orbs.compack` names 53 `wiki_icon_graphic` and 55 `worldmap`, and
`orb_worldmap`'s onload (script 1492 → proc 1700) installs "Floating World Map"
and "Fullscreen World Map" on the argument it passes as 10485815 = 160:55. The
server armed the wiki button, the orb stayed inert, and the map never opened.
A symbol table from a neighbouring revision is a hypothesis; the compack and the
onload are the revision you are running.

### 3.14b CLOSE_MODAL is not the bank's

`handle_close_modal` tested `bank.open` and returned. The bank was therefore the
only interface in the game that could be closed: the equipment screen, and
anything a content script opened with `if_openmain`, stayed on screen for the
rest of the session however many times the X was clicked.

The server is what unmounts, so it has to know what it mounted. `IF_OPENSUB` and
`IF_CLOSESUB` now record the group sitting in each of the gameframe's two modal
slots (`mock230_note_modal_mount`, called from the encoders rather than from each
opener, so no new opener can forget). CLOSE_MODAL offers the open interface's
`[if_close]` script first — the same order the bank already had — and otherwise
drops the mount. The bank and the equipment screen keep their own closers
because they own state beyond the mount; everything else is just a mount, and
dropping it is the whole of closing it.

## 3.16 The chatbox is widgets at this revision, and nothing was writing to them

The chatbox rendered its background, its seven filter tabs, its Report button
and its scrollbar perfectly, and never showed a message. Every `MESSAGE_GAME`
arrived and was parsed; `RS_Chat_AddMessage` stored all of them.

Two eras answer "where does a chat line come from" completely differently.

At 254 the chatbox is a **surface**: the gameframe reserves a box (a revconfig
node tagged `slot=chat`) and the client paints message text into it with its own
font calls. `RS_Chat_BuildView` and `emit_chat` are that path.

At 230 the chatbox is **widgets**. Interface 162 ships 500 empty text components
— `chatbox:line0`..`line499`, inside `chatbox:scrollarea` — and the client's
whole job is to fill in their text and move them. There is no chat *region* in a
dat2 tree at all, so `app->slots.chat_index` stayed -1 and the surface path
correctly drew nothing.

`src/game/rs_chat_widgets.c` is the second answer. It writes the visible
messages into the line components oldest-first, hides the rest, and sets the
scroll layer's content height and offset. Everything else — fonts, colours
through `<col=…>` markup, the background, the tabs, the per-line right-click op
— is already in the cache and already drawn by the ordinary widget pipeline.

`clientCode 1336` is *not* this. It is documented as `CONTENT_CHAT` and it is
`161:19`, a 120×100 layer at 430,0 inside the viewport — the split private-chat
overlay, not the chatbox.

Four things about it are load-bearing, and three of them produced a wrong
picture before they were right:

- **The ids are declared, in the manifest's `[ui:chatbox]`.** They are revision
  facts, like the gameframe mount table beside them, and the reference client's
  own equivalent is a generated `ComponentID` constant. The alternative was
  recognising the chatbox by shape — "a scrolling layer with a few hundred
  identical text children" — which is a heuristic between the player and every
  message the server sends. `chatbox_interface=0` (every dat1 manifest) disables
  the whole path.
- **Read the scroll viewport before moving anything.** `UITree_ApplyPosition`
  clears `layout_resolved`, and `UITree_LayoutGetBounds` silently falls back to
  the *declared* fields when a node is unresolved. `chatbox:scrollarea` is
  `heightmode=1`, so the record says 452 (against a 503-tall root) where the
  gameframe's chat slot actually gives it ~114.
- **Resolve the layout on the way out.** The app's own `UITree_LayoutResolve`
  runs earlier in the frame and only when a CS2 script ran. An unresolved text
  node emits at its declared position with a size of *zero* — which drew every
  chat line as a 0×0 box at the top-left corner, correct text and all.
- **Hide unused lines, do not blank them.** Each carries `op8='*'` (the
  reference's per-line Report abuse). Five hundred invisible, clickable, empty
  lines stacked over the chatbox swallow every click that misses a message.

A short chatbox fills from the *bottom*, against the input line; once the column
overflows the viewport that term goes to zero and the scroll offset takes over.
`make -C src test-chat-widgets` pins the order, the anchor and the overflow,
because all three are "text in the right font in the right box" and only the y
coordinates tell them apart.

## 3.17 Zones: the ZoneMap, and what a broadcast could never do

`src/net/mock/mock230_zone.{c,h}`, ported from the reference's
`engine/src/engine/zone/` (`Zone`, `ZoneMap`, `ZoneEvent`). The header carries
the design; this is what it changed and what it cost.

### The bug it exists for

A loc change used to be `mock230_world_broadcast_loc`: walk the player pool,
send `LOC_ADD_CHANGE` to everyone on that level whose scene contained the tile.
That is correct for everyone standing there when the door opens and silently
wrong for everyone else **forever** — a broadcast has no memory, and
`REBUILD_NORMAL` hands a new client a scene straight out of the cache, with
every door shut and the server still believing they are open.

Ground objs had the mirror-image problem solved a different way: a per-player
`ground_sent[MOCK230_GROUND_MAX]` bitmap, rescanned flat over all 256 slots for
every player every tick. That one was *correct*; it was just the wrong question.
"Has this client been told about that obj" does not generalise to locs and does
not survive a rebuild. "Does this client hold that zone" does both.

### The shape

The key is the reference's, bit for bit —
`(x >> 3) | (z >> 3) << 11 | level << 22` — because the wire's is: every
`UPDATE_ZONE_*` names an 8x8 zone and every sub-packet after it carries only a
`pos` inside that zone.

Each zone holds **state** and **this tick's events**:

| a zone holds | what it is | consumer |
|---|---|---|
| `locs[]` | `struct Mock230ZoneLoc` — every loc that differs from the map square | the replay, and the rebuild's re-apply |
| `objs[]` | `Mock230Server.ground` slots standing here | the replay |
| `npcs[]` | `Mock230Server.npcs` slots standing here | NPC_INFO's entering-view scan |
| `events[]` | this tick's `LOC_ADD_CHANGE` / `LOC_DEL` / `OBJ_ADD` / `OBJ_DEL` / `OBJ_COUNT` | the per-client flush |

and the per-client half is two sets on `Mock230Player`: `active_zones`, the 7x7
window around the player clipped to the 13x13 build area (the reference's
`BuildArea.rebuildZones`, recomputed only when the player changes zone), and
`loaded_zones`, the subset this client has been sent state for.

In the tick: **phase 8** reconciles membership, **phase 10** flushes per client
(right after NPC_INFO, where the reference puts `updateZones()`), **phase 11**
drops the event buffers and leaves the state alone.

### A zone the client does not hold gets state; one it holds gets events

That sentence is the whole mechanism, and the *and* in it is a bug. The first
version sent both — `FULL_FOLLOWS`, the zone's whole state, *and* the tick's
events — with a guard skipping any entity whose change tick was the current one.
Ground objs arrived twice and the client drew two stacks on one tile. The guard
is subtly the wrong test: the world's own spawns are placed before any tick has
run, so they carry tick 0, and by the time the first client is flushed on tick 1
their stamps no longer match. What is correct is unconditional and needs no
stamps at all — the loc records are written at the moment of the mutation and
obj membership is reconciled in phase 8, so **the state already includes
everything the events would say**. A newly-loaded zone therefore gets state and
nothing else, and both tick-stamp fields are gone.

The one thing that gives up is a *receiver-scoped* event landing on the same
tick a client loads the zone. Nothing produces one yet; when something does (the
reference's per-killer loot), the state write has to learn about receivers too.

### The ZoneMap owns loc mutations. The scene does not.

`mock230_scene_build` calls `mock230_scene_free` first, so the scene is re-read
from the cache whenever the origin moves. It therefore cannot be the authority
on anything that happened at runtime — and it was not, despite the comment in
`maybe_rebuild` claiming "mock230_scene_build keeps the changed list". It never
did: the `changed` flags lived on the array the rebuild had just freed, so
phase 10's re-send loop walked an empty list and **the server forgot its own
doors on every rebuild**. Two comments, in two files, describing a mechanism that
could not work.

So: `struct Mock230ZoneLoc` is the durable record, keyed by
`(x, z, level, shape)` — the wire's own key, since `LOC_ADD_CHANGE` and
`LOC_DEL` identify a loc by its tile and its shape and nothing else — and
`mock230_world_locs_reapply` puts every record back onto the freshly built scene
so collision and the pick lookup agree with what the clients were told. Three
things follow:

- **One door.** `mock230_world_loc_set` is the only runtime loc mutation, and it
  does all three halves together: move the scene's collision, record the change,
  queue the event. The scripts' `loc_change`/`loc_del`/`loc_add` and the engine's
  door swap all go through it.
- **The record is the diff.** It carries `base_loc_id`/`base_angle` — what the
  map square has, captured on creation — and is retired the moment the loc
  matches it again. A tree felled and regrown every thirty seconds does not
  accumulate an entry per cycle.
- **The revert table is keyed by coordinate**, not by a scene slot. A slot does
  not survive a rebuild; before this, a revert armed before one fired against
  whatever loc had inherited its index.

A `loc_del` on a map-square loc leaves a **tombstone** in the scene array —
inactive, `is_static` set, its slot never handed to a `loc_add` — because the
square still has a loc on that tile and "it has been removed" has to stay
addressable.

### The npc cap was a wire field standing in for a world capacity

`MOCK230_NPC_MAX` was 256, annotated "the tracked count is an 8-bit field on the
wire, so this must stay under 256". True about the *tracked* count and silent
about the world: the stream's slot field is 14 bits, so the wire's own ceiling
on how many npcs may exist is 16383, and the reference runs at exactly that
(`NODE_MAX_NPCS`, default 16383). What made the world cap load-bearing was
NPC_INFO scanning every slot in the world for every client every tick.

It asks `mock230_zone_npcs_near` now — the npcs standing in the zones the
15-tile add radius touches, at most 5x5 of them — so the two numbers are free to
differ: `MOCK230_NPC_MAX` is 2048 (a memory decision: 336 bytes an npc,
statically allocated; the world struct is 940 KB) and
`MOCK230_TRACKED_NPC_MAX` is 255 (the wire's). Lumbridge itself is 63 npcs, and
the per-tick phases walk `srv->npc_slot_max` rather than the pool so raising the
cap does not make the tick read as though it costs more.

One correctness fix fell out: the entering-view scan ignored level, which was
invisible while it was flat and ignored level too. The ZoneMap is keyed by level,
so an npc left tracked across a climb could never be re-added, only re-encoded
forever. `in_range` tests level now, the same way `player_in_view` always did.

### What is *not* fixed

- **One scene origin for the whole world** (§6.1 step 1) is untouched, and it is
  now the visible limit: a loc revert aimed at a tile the moved scene no longer
  covers cannot apply, so the loc stays as it is. The ZoneMap record stands,
  which is the safe direction — the clients were told it changed — and the
  refusal is reported under `MOCK230_VERBOSE` rather than swallowed.
- **`ground[256]` is still walked once a tick** by the membership reconcile.
  What went away is the *per-player* pass: the cost was O(players × objs) and is
  O(objs). Objs are refiled at each of their four mutation sites anyway; the
  per-tick pass is the reconcile that catches a call site nobody has written yet.
- ~~**Zone triggers are still undispatched.**~~ **Closed — §3.21.** All four
  dispatch now, and the number worth carrying forward is not the 806: **427 are
  zone-keyed and 379 are map-square-keyed**, and the ZoneMap is irrelevant to the
  379. Nothing in the zone-trigger path consults this structure — `mapzone` keys
  off `x >> 6` and even `[zone]` reaches its script by *name*, not through any
  index this file owns. Read §3.21 before assuming a zone trigger and a zone are
  the same thing.

### Verified

- `net/mock/test/embed_test.c` — three clients in one embedded world. Alice
  walks to Lumbridge castle's courtyard door (3226,3223, `1535` → `1536`) and
  opens it with a real `net_out_oploc` through her own ISAAC stream; alice's and
  bob's clients decode the change out of an `UPDATE_ZONE_PARTIAL_ENCLOSED` with
  the client's own `gameproto_parse`, on the door's own tile. Then **carol
  connects, and is told the same thing without anybody touching the door
  again.** Both halves were proved failable: skipping the replay fails only
  carol's three checks, skipping the enclosed blob fails only alice's and bob's.
- `make -C src test-mock230` — a drop reaches a client already in the zone as an
  enclosed `OBJ_ADD`; a client holding no zones is caught up with `FULL_FOLLOWS`
  plus the objs already on the floor, with nothing having changed; a door open
  writes a zone record naming what the map square has under it.
- Headless client, `manifest_osrs230_embed.ini`, `SDL_VIDEODRIVER=dummy`: all
  seven Lumbridge obj spawns arrive **exactly once** each, on the right tiles
  (they arrived twice before the double-send above was found), and a scripted
  `loc_change` + its revert arrive through the enclosed stream and apply to the
  right tile (`714` then `114` at scene-local 76,79 — the cooking range at
  3212,3215).

---

## 3.18 The inverted fallback: a trigger with no script does nothing

`mock230_scripts.c` used to open with a promise:

> when no script pack is loaded, or when a trigger has no script, every call
> site does exactly what it did before scripts existed.

That was right while scripts were an experiment. For a server it means **every
behaviour has two implementations**, and nine of the nineteen
`mock230_scripts_run_trigger` call sites branched on the return value to pick
between them. The lookup order was already the reference's; what was inverted
was what happened after a miss.

### Three things that were indistinguishable, and now are not

**1. A content bug and a content gap.** `run_trigger` answered `0` both for "no
script is bound" and for "a script was bound and aborted". A `[opnpc1,cook]`
that blew up on line 3 handed the click to `interaction_engine_npc`, which
greeted the player — so the *observable* result of a broken quest script was a
plausible, wrong, entirely silent conversation. The return is
`enum Mock230TriggerResult` now: `NONE`, `RAN`, `FAILED`. `0` and `1` keep their
old meanings so the existing shape of every call site still reads correctly, and
`FAILED` is the third answer nothing could previously express.

**2. An engine fallback and "what happens otherwise".** The C that answers an
unbound trigger is enumerated — `enum Mock230Fallback`, **four** rows — and
unbound trigger is enumerated — `enum Mock230Fallback`, **five** rows — and
reached only through `mock230_scripts_fallback(srv, which, result)`. Each row
carries what it is blocked on, and the boot prints the count (the roll under
`MOCK230_VERBOSE`, abbreviated here — the live text is longer and is the
authority):

```
mock230: 4 engine fallback(s) still answer triggers content does not bind
  opnpc        blocked on: combat, 1,061 lines of mock230_combat.c … the blocker is the player_combat closure, not the dispatch
  oploc        blocked on: NOT the loc_* family — [oploc<n>] binds no active loc, no loc category rung, P_OPLOC unimplemented
mock230: 5 engine fallback(s) still answer triggers content does not bind
  opnpc        blocked on: combat, 1,061 lines of mock230_combat.c … the blocker is the player_combat closure, not the dispatch
  opobj        blocked on: an entity kind, not an opcode pair — SSVM_ENT_OBJ has no writers; OBJ_* 3502-3511 all unimplemented
  opheld       blocked on: NOT 'equipment is C' — eight declared-unimplemented opcodes (OC_WEARPOS/2/3, INV_SETSLOT, …)
  inv_button   blocked on: ADDRESSING — the grid's quantity row moves, so [inv_button1..5] cannot name Withdraw-All
  if_button    blocked on: the same last_verb reader, plus bank.rs2's bindings and bank_set_events naming different components
```

Seven rows, then six, five, four. `ai_queue3` went first (below), `opobj`
second — `[opobj3,_]` in `player/scripts/pickup.rs2`, the whole sequence
recorded below as the worked example — and `opheld` third, one day later,
`[opheld2,_]` and `[opheld5,_]` together.

### The list's job is to be audited, and the audit found the list

The point of enumerating the fallbacks was never the count. It was that each row
carries a **reason**, so "why is this behaviour still C?" has an answer at the
place someone asks it. A 2026-08-01 pass re-measured all seven reasons against
the tree, and the result is the thing worth recording: **four of the seven were
false or misdirected on the day they were read, and not one had been edited to
become so.** Three had been overtaken — the thing they were waiting for landed
in some other lane and nobody came back — and the fourth was never right.

| row | what it said | what was true | stale since |
|---|---|---|---|
| `ai_queue3` | "drop tables need npc categories (§7.6b, §9 step 3b)" | categories, the category rung and 69 drop-table files had all landed; **nothing** blocked it | `ff2d172f` → `44d42dff`, two lane merges |
| `oploc` | "doors, bank booths and stairs need `loc_*` and a per-loc destination" | `loc_find/change/add/del/coord/type/angle/shape` and `p_teleport` all landed; the real gate is that `[oploc<n>]` binds no active loc | `ff2d172f` (the `loc_*` family) |
| `opobj` | "no `obj_take` / `inv_add`-from-ground opcode pair yet" | not a pair and not opcodes first — `SSVM_ENT_OBJ` has zero writers, so no obj opcode would have a subject | never true as stated |
| `opheld` | "equipment is C" | `mock230_equipment.c` is 134 lines of component→worn-slot map and the equipment *screen* is content (`interface_equipment/scripts/equipment.rs2`) | since the screen moved |

The three that were *directionally* right carried stale numbers — combat 858 →
**1,061**, bank 1,370 → **1,395** — and `inv_button`/`if_button` both blamed
those line counts when the actual obstacle is addressing: the bank's rules are
already sayable, its **op indices** are not.

None of that was visible by reading. A blocker that has expired and a blocker
that is live are the same sentence, which is the failure mode of any audit list
whose entries are prose. Two things came out of that and both are below: every
surviving string is now written to be **checkable in one command**, and the
opcode-shaped half of every string is **machine-checked**.

### The blockers are machine-checked, because prose cannot go stale loudly

The count above catches a row being *added*. It cannot catch the way this list
actually failed, which is a row that stops being right without changing: the
thing it is waiting for arrives, nobody comes back to the row, and it keeps
printing a reason that is no longer a reason. `ai_queue3` did this for two
stages (below). A stale blocker and a live one are the same text.

So each row's blocker names its opcodes in a form the machine can resolve —
`k_engine_fallbacks[].blocked_ops`, `struct FallbackBlockingOp`, the symbol
exactly as `ss_opcode.h` spells it — and `mock230_scripts_stale_blockers()`
resolves each against `opcode_implemented()`. A hit prints at boot,
unconditionally (not behind `MOCK230_VERBOSE`: the reader who needs it is
whoever just implemented the opcode, and they have no reason to be running
verbose):

```
mock230: STALE BLOCKER — the `opobj` engine fallback says it is waiting on
SS_OP_OBJ_DEL (3504), and SS_OP_OBJ_DEL is implemented now. Either the row can
go or its reason has to be rewritten to what is still true.
```

Loud at boot, **fatal in the selftest** — the opcode landing is progress, and a
server that refused to start because somebody implemented `OBJ_DEL` would teach
the wrong lesson. Two mutations were run and both turn the suite red: adding
`SS_OP_OBJ_ADD` (implemented) to `k_blocked_opobj`, and declaring
`SS_OP_LAST_VERB` as an implemented value, which lights `inv_button` *and*
`if_button` because both cite it. The `LAST_VERB` case is the interesting one:
nothing declares that opcode at all, so it is carried as `-1` behind an
`#ifdef`, and declaring it is enough to put it under the check.

**That example is not hypothetical any more — it is the transcript.** The
`opobj` row really did print exactly that line the day `OBJ_DEL` landed, its
reason really was rewritten rather than left, and then the row went. The
illustration is kept in its original wording for that reason; `k_blocked_opobj`
no longer exists, and the comment where it stood says why.

The check is deliberately one-directional and deliberately incomplete. It
catches "a row cites something that is now present", which is the failure that
hides. It cannot cover a blocker that is not an opcode — `opnpc` waits on 1,061
lines of `mock230_combat.c`, `oploc`'s first gate is an entity binding nobody
writes, `if_button`'s second defect is two component lists that disagree — and
asserting those would mean asserting a defect *stays* present, which produces
false alarms when somebody fixes the defect without clearing the row. Those cite
the one command that settles them instead (`grep -rn SSVM_ENT_OBJ src`,
`wc -l net/mock/mock230_bank.c`, a `file:line`). Writing a blocker that cannot
be checked in one command is the thing to avoid.

**One check was costed and deliberately not built**, and the reason generalises.
`if_button`'s second defect is that the content meant to replace it is compiled
and *inert*: `interface_bank/scripts/` binds eleven `[if_button,bankmain:*]`
components — `swap_insert_graphic`, `note_graphic`, `quantity1_text`,
`quantity5_text`, `quantity10_text`, `quantityx_text`, `quantityall_text`,
`deposit_line`, `depositcontainers_graphic`, `potionstore_container`,
`banktags_header_separator` — while `bank_set_events` (`mock230_bank.c:609`)
arms nine — `swap_insert`, `note`, `quantity1`, `quantity5`, `quantity10`,
`quantityx`, `quantityall`, `depositinv`, `depositworn`. Checked against
`interfaces/bankmain.compack`, those are **adjacent, different components** (23
vs 24, 25 vs 26, 29 vs 30), the two sets are disjoint, and nothing is clickable
until `IF_SETEVENTS` — so every one of those eleven scripts can never fire.
Asserting the disjointness would be ~30 lines. It was rejected on correctness,
not cost: it would assert that a **defect stays present**, and fixing the arm
list without moving the bank to content is a legitimate change that leaves the
row valid, so the check would fire falsely and train people to ignore the STALE
lines it sits beside. The right shape is a separate boot report — *content bound
to a component nothing arms*, the dual of `mock230_scripts_report_shadowed_ops`
— because that is a general defect class and not a fact about the bank.
Proposed, not built.

### `oploc` was the second row, and it took three stages

**Gone 2026-08-02.** `interaction_engine_loc` (84 lines) and `climb` (34) are
deleted, the call site in `mock230_world_process_interaction` is a bare
`mock230_scripts_run_trigger_on_loc`, and the count is **5**.

It is worth reading as three stages rather than one deletion, because each
stage's end state was a legitimate place to stop and none of them was "the row
goes":

1. **the surface** — the `SSVM_ENT_LOC` binding, the loc category rung, and four
   opcodes. End state: every `blocked_ops` entry discharged, **row retained**.
   That is a state this list was built to be able to express, and it is the
   first time it did: a row whose blocker is gone is not thereby a row to
   delete, because the C was still the thing answering the trigger.
2. **the content** — doors and ladders. End state: both behaviours bound and
   both C paths unreachable, **row still retained**, because the third
   behaviour — the bank booths — was not covered.
3. **the eviction** — the booths, then the delete.

What the row's original blockers said and what happened to each part:

| the gate | how it went |
|---|---|
| `[oploc<n>]` binds no active loc — `SSVM_ENT_LOC` had three writers, all inside the VM | `mock230_scripts_run_trigger_on_loc` binds the scene slot, so `loc_coord`/`loc_param`/`loc_change` in a door script no longer abort. `handle_oploc` had computed that slot and discarded it since the day it was written |
| no loc category rung — `interaction_category` returned a hardcoded -1 for locs | `dat2_config_loc.c` keeps config opcode 61 (it was `g2(buffer); // Skip unsigned short`), `mock230_loc_category` merges the cache's 8,407 categorised records with the authored overlay, and `content.ini`'s allocation base minted `door_closed`/`door_opened` |
| `LOC_CATEGORY` / `LC_CATEGORY` unimplemented | `mock230_ops_loc.c`, with `LC_DEBUGNAME` beside them because `stairs.rs2` needs it |
| `P_OPLOC` unimplemented while the shadowed-verb report told content to call it | `mock230_ops_player.c`, as a **re-issue** — `setInteraction`, not a call into `interaction_engine_loc` |

**Three findings from doing it, in descending order of how much they changed the plan.**

**1. The doors this row is tested on carry no cache category, so keeping opcode
61 does not land them.** `poordoor` 1535 and `poordooropen` 1536 both state
nothing, and so do the other 774 records in `doors.loc`. The crawl agrees from
the other end: all 9 door categories the reference binds come back `orphan`, with
`SUSPECT 167(71/82),168(54/66)` — two cache categories that share the *display
names* Door and Gate and are a different set. So the door route needed the
`category` namespace to be allowed to grow (`content.ini`, base 8192), which is
`PORTING_GUIDE` §2.4 item 4 verbatim; the rscache change buys the other half of
the row, the 78 Bank records and the 8,407 categorised locs behind them.

**2. Storing a field the text form cannot express is a regression in the
packer.** Making the decoder keep opcode 61 took `cachepack`'s loc `lost-here`
column from 0 to 205 — records the library round-trips byte-exactly and the text
layer no longer does — until `cp_loc.c` gained the `category=` key. The fidelity
suite caught it on the first run. Byte-exact loc records went 581 → 786.

**3. `MOCK230_VERB_USE_QUICKLY` matches zero records in this cache.** Rescanned
across every string-bearing loc opcode; the string appears nowhere in the 62,194
records. It is a dead branch in `interaction_engine_loc` *and* it inflates
`mock230_world_engine_claimed_verb`'s shadow surface with a verb that can never
match. It can go on its own, with no content at all.

### The loc category rung, priced — so nobody re-derives it

Two questions cost a day between them and both have short answers now. Written
down here because the next reader will ask them in this order.

**Does the cache carry a loc category? Yes.** Config opcode 61, the same field
npc carries at 18 and obj at 94, in the **same shared id space**. The decoder had
it and threw it away — `dat2_config_loc.c` case 61 was
`g2(buffer); // Skip unsigned short` — so the field was never missing, only
discarded, and `LocType.ts:179` in the reference names it in the same wire slot.
Re-measured on `configs/all.loc` (`cache.osrs239`):

| | |
|---|---:|
| loc records | 62,194 |
| **state a category** | **8,407** (13.5 %) |
| distinct ids | 712 |
| highest id | 2,474 |

with npc at 9,149 / 16,292 (982 ids, max 2,504) and obj at 11,680 / 33,747 (575
ids, max 2,506). Largest loc groups: **206** ×1,749 (POH furniture), **907** ×360
(every Bookshelf), **207** ×251, **154** ×234 (Table), **167** ×82 / **168** ×66
(the door pair), **684** ×63 (Bank booth). The shared id space is coherent where
it overlaps — loc∩npc is 9 ids (202, 206, 955, 956, 1135, 1226, 1301, 1503,
1930), loc∩obj is 3 (206, 1338, 1807), and each is the same concept on both
sides — so one flat `pack/category.pack` still works, but 206 is a genuine
three-way name waiting to be argued over.

**What the rung cost: nine changes across five trees**, and the vendored-cache
half was the smaller one.

| tree | change |
|---|---|
| `3rd/rscache` | decode opcode 61 into `RSCache_Dat2ConfigLoc.category`, **plus an encoder arm** — `EXCEPTIONS.md`-governed, byte-exact round-trip is the bar |
| `cachepack` | `cp_loc.c` gains a `category=` key and `cp_resolve_category` (number first, then `pack/category.pack`), or the packer regresses — see finding 2 above |
| content | `fields/loc.ini` `[loc.category]` `client = drop` → `scope = client, client = native`; `configs/all.loc` regenerates **+8,407 lines** |
| content | `content.ini` gives the `category` namespace a `server_base` (8192), because `ids = cache` alone means an authored name has nowhere to get an id |
| tools | `port_category_crawl.py` grows `--domain loc` and a second map, `port/categories_loc.map`; `--check` runs both, since the id space is one |
| `mock230` | a sparse (id, category) table in `mock230_locinfo.c` — 8,407 rows, 67 KB, against 249 KB flat — and `mock230_loc_category`, overlay first, cache second, **never 0** |
| `mock230` | `Mock230LocDef.category` stops being a private two-value enum and becomes a `MOCK230_PACK_CATEGORY` id; `interaction_category`'s hardcoded −1 for locs goes |
| `mock230_pack` | `validate_categories` gains a loc arm, a third carried-by class, and an at-or-above-base rule |
| opcodes | `LOC_CATEGORY` 3003, `LC_CATEGORY` 4100 (`mock230_ops_loc.c`) |

**What it bought, and what it did not.** Of the **91** loc categories the
reference's own `.loc` files declare, the crawl mints **11** from ids this cache
states, holds **2** back as `broader`, splits **3**, allocates **2**, and returns
**73 orphan** — a concept this cache names no id for. `unusable_table`→154,
`rc_altar`→2156, `red_vine`→216, `well`→245 and `digsite_soil`→537 are the shape
that works. **The doors are orphans**, which is finding 1 and is the thing that
would have been assumed the other way: the rung is what makes
`[oploc1,_door_closed]` *bindable*, and the allocation base is what gives it an
id to bind to. They are two separate changes and only the second one lands the
doors.

### Stage 2: the content landed and the row survived it

2026-08-02, the same day the gates cleared. Both of the behaviours anyone would
have named if asked what this row was were bound by content and neither C path
was reachable — and the row still could not go:

| behaviour | where it lives | proof |
|---|---|---|
| the door swap | `doors/scripts/doors.rs2` + `door_procs.rs2`, the reference verbatim — `[oploc1,_door_closed]`, `[oploc1,_door_opened]`, `[oploc2,_door_opened]` | `SELFTEST_CHECK(0)` at the top of `interaction_engine_loc`'s `next_loc_stage` branch, whole selftest + a client session: never fired |
| the climb | `ladders_stairs/scripts/ladders.rs2` + generated `climb_shared.rs2`, keyed on four allocated categories | the same probe inside `climb()`: never fired. And a real client session — right-click "Climb-up Ladder" at Lumbridge castle, left-click, player is on the first floor |

**The doors now SWING**, which is a behaviour change and the point of porting
verbatim: the reference is `loc_del(500)` then `loc_add` on the *adjacent* tile
with the angle turned one quarter, where the C swapped in place. The selftest
section asserts both tiles, both ZoneMap records and both wire opcodes
(`LOC_DEL` 71 *and* `LOC_ADD_CHANGE` 70) instead of one of each.

### Stage 3: the booths, and what the eviction actually cost

**What kept the row was a list nobody had written.** Not an opcode, not an
entity binding, not a volume of C — 78 loc records in this cache say "Bank" on a
menu op and content bound exactly one of them (`bankbooth`, by hand). The C
reached all 78 for free with a `strcmp`, so deleting it without that list would
have silently lost 77 booths, and **nothing in the suite would have gone red**:
there was no bank-booth coverage anywhere. A row can be one unglamorous list away
from going, and the list is the part that has no engineering in it.

`tools/bank_import.py` writes it — `interface_bank/scripts/bank_booths.rs2`, 78
`[oploc<n>,<name>] ~openbank;` on the slot the cache states the verb on (op1 ×18,
op2 ×60) — with `--check` in `test-port` so the file and the cache cannot drift.
`~openbank` is unchanged and `mock230_bank_open` stays engine; what moved is
*which locs reach it*.

**Names, not a category, and that was measured rather than assumed.** The obvious
route — the one the previous stage's notes proposed — was three cache-category
bindings (`684`, `237`, `1929`) plus 8 names. It over-reaches badly. Category 684
has **63** members and 58 say "Bank"; one of the other five is
`exchange_bank_wall_exchange`, whose op1 is "Exchange" (the Grand Exchange).
Category 237 has **44** members and 11 say "Bank"; of the other 33, twenty-nine
say "Use" on op1 — `chest_alchemist01_closed01` among them — and four state no
op1 at all. Binding those three ids would open the bank on
38 records that never offered it. (The stage-2 note's "`237` 11" was counting the
records that say "Bank", not the category's membership — the same mistake in
miniature.) An *authored* `category=bank_booth` on exactly the 78 has no
over-reach but is the same 78 generated blocks moved from a `.rs2` to a `.loc`,
plus a namespace row, plus overwriting the cache's own category on 70 records.
And the reference settles it anyway: LostCity binds every booth by **name**
(`interface_bank/scripts/bank_booth.rs2`, `tut_bank_booth.rs2`,
`shantay_chest.rs2`, `misc_locs.rs2`, `gundai.rs2`) and has no bank category.

**What the eviction gave up, measured, and it is not the doors.** The C's door
branch never read an op number — it swapped for whatever op the client sent — so
it also answered `Pick-lock` on a locked house door, `Repair` on a damaged Pest
Control gate, `Force`, `Remove`, `Attack`, `Search` and `Quick-open` by opening
the thing. Across the whole cache that is **54 (record, op) pairs on 32 records**
— re-counted as "every op the C answered that no content binding names", which is
the only counting of it that means anything: Pick-lock 12, Repair 12, Force 11,
Remove 11, Attack 4, Quick-open 2, Search 1, and `Pick-Lock` 1, which is the
cache's own second spelling on `osf_trapdoor_closed` op5 and is exactly the kind
of thing a `strcmp` ladder in C gets wrong in the other direction. Eleven of the
32 are POH dungeon doors carrying all three of Pick-lock/Force/Remove; twelve are
Pest Control gates carrying Repair. Content binds the op the pairing is about and nothing else, so those
54 now get `Player.defaultOp` — "Nothing interesting happens." — which is what
the reference gives them, since it binds none of those verbs either. That is a
wrong answer removed, not a route lost, and it is the *only* behaviour difference
the deletion makes. Quick-open (2 records) is the one arguable case and was
deliberately **not** invented: it is a double-door verb, `doubledoors.rs2` is not
ported, and reproducing a single-leaf swap because the C happened to do one would
be writing content to match a bug.

Everything else the row's stage-2 text named as blocking turned out not to be,
and the reason is one sentence: **a category binding is keyed on what the record
*is*, so it does not care what the menu says.** The "26 doors whose op1 is
neither Open nor Close" — 15 Climb-down, 6 Open, 3 Pass-through, 1 Go-down, 1
Peek — are every one of them reached by `[oploc1,_door_opened]`, and the 15
Climb-down trapdoors are additionally overridden by name in `climb_shared.rs2`.
The "144 with no Close op" splits: **131 state no menu op at all** and cannot be
clicked, and the other 13 put one of those non-Close verbs on op1, where the
same category binding catches them. Neither number was ever a gate.
`MOCK230_VERB_USE_QUICKLY` went with the rest, still matching zero records.

**What replaced the fallback call.** Not nothing: the arm is

```c
if( mock230_scripts_run_trigger_on_loc(...) == MOCK230_TRIGGER_NONE )
    mock230_say(srv, "nothing_interesting_message", NULL);
```

which is `Player.defaultOp` (`Player.ts:1143`) and is the same shape the use-on
arm beside it already had. `FAILED` deliberately says nothing — a script that
aborted has had its turn.

**The permanent checks, and the mutation that proves each can fail.**

| check | mutation | result |
|---|---|---|
| new selftest section **"a bank booth is content's"** — `bankbooth` op2 and `duel_chestopen` op1 both resolve to a script and open the bank; op3 of the same chest resolves to nothing | delete `[oploc1,duel_chestopen]` from the generated file and recompile | **2 FAIL** (the trigger answers NONE, the bank stays shut) |
| `mock230_world_engine_claimed_verb(OPLOC2, bankbooth) == NULL` | put a five-line `k_engine_loc_verbs`-style arm back in C | **FAIL** |
| the shadowed-op pin, now **0** | the same mutation | **FAIL** — the report goes to 78 |
| `tools/bank_import.py --check` in `test-port` | hand-edit one binding | exit 1 |
| `MOCK230_FALLBACK_COUNT == 5` | any row added | FAIL by count |

**Three engine defects the move exposed, all of the same class**: an argument
pair that two callers happened to pass as equal numbers.

| opcode | what it did | why nothing saw it |
|---|---|---|
| `LOC_ADD` (3000) | popped `shape` where the reference pops `angle` — `LocOps.ts:19` is `[coord, type, angle, shape, duration]` | both selftest callers passed `..., 10, 0, …` and `..., 0, 0, …`; `ss_meta.gen.h` carries arity and stack class, never order |
| `MOVECOORD` (16) | added `$z` to the level and `$y` to the north axis; `ServerOps.ts:107` is `level + y, x + x, z + z` | all twelve callers in the tree write `movecoord($c, $dx, 0, $dz)`, so the two wrong terms were `level + $dz` and `z + 0` — `~move_north($c, 3)` went up three floors |
| `P_TELEPORT` (399) | moved x/z/level and set `place_dirty`, nothing else | a plane change does not move the scene *window*, so `maybe_rebuild` never fires and the client keeps a whole floor's npcs, players and zones. `climb()` did this bookkeeping inline; `mock230_world_player_level_changed` is the shared seam now |

And one content-surface defect: a `.loc` overlay's `param=` landed in a private
field of `struct Mock230LocDef` and **nowhere a script could read it**, so
`loc_param(next_loc_stage)` — the reference's own line — answered the declared
default and the door opened into loc 0. `mock230_paramtable_set_int` publishes it
to the loc param table now. An overlay param no script can read is not a param.

### `ai_queue3` was the seventh, and it went first

The list lost its first row on 2026-08-01. `ai_queue3` was "the npc's
`death_drop` param" — what an npc with no bound drop table left behind — and it
is `[ai_queue3,_]` in `skill_combat/npc_combat.rs2` now, five lines of
`npc_param` / `obj_add`, which is where the reference states it
(`[ai_queue3,_] gosub(npc_default_death)`).

Two things about it are worth more than the deletion.

**The row's `blocked_on` string was false at every boot for two stages.** It
said "drop tables need npc categories"; npc categories had landed at `ff2d172f`
(19 of them — triage §16), the death dispatch had adopted the category rung at
`44d42dff`, and `drop_tables/` had landed on top — **69 files, 136 bindings, 6
of them on the category rung**, all measured. So the row was printing, at every
boot, that it was waiting for three things it was already standing on. Nothing
was wrong with the code; the *reason* had expired, and an expired reason reads
exactly like a live one. That is the failure mode this text exists to prevent —
and it is not one row's accident: the table above shows three more, and the
category-rung bullet below shows this section doing it to itself. Which is why
the six survivors were rewritten in the same pass against what is true today
rather than left as they were found.

**The precondition everyone expected was not there.** The reference guards
`if ($drop ! null …)`, and the obvious worry is that `npc_param(death_drop)`
answers 0 for the npcs no `.npc` block describes, which would drop obj 0 under
every unbound death. It does not: `general/configs/npc_default.npc`'s
`[default]` block authors `param=death_drop,bones` and `npc_def_seed_from_cache`
copies the default record — params included — into every block, so the value is
`bones` for an npc nothing describes. Measured, 526 for the default def and for
all 40 authored blocks. The guard is kept because `param=death_drop,null`
(resolved to -1 and filed under the param id) is the only way to say "leaves
nothing"; it is *not* what stops obj 0, and it cannot be shown to be by any
test here, because `mock230_world_obj_add` refuses `obj_id < 0` on its own.
Deleting the guard leaves the suite green — stated in the test's own comment,
because a test that cannot fail proves nothing.

What proves the behaviour moved is the selftest section **"the death drop is
content's"**: a chicken still runs its own table on the *category* rung (one
raw chicken, one bones — the `_` rung must not also fire); a duck, which nothing
binds, still leaves its `death_drop`; and an npc whose `death_drop` is `null`
leaves nothing. Four mutations were run and three turn it red — unbinding
`[ai_queue3,_]`, inverting the null guard, and rebinding
`[ai_queue3,_chicken]` to another category. The fourth (deleting the guard) does
not, which is the limit stated above.

The count is the point, but only half of it. It is asserted in the selftest, it
may shrink, and it must not grow — the same discipline as the named hooks
(`PORTING_GUIDE` §2.4 item 5; **10** now, `mock230_scripts.c:203-212` — 9 until
2026-08-01, 11 after `equipment_open`, and back to 10 on 2026-08-02 when
`equip_level_message` went with `mock230_equipment_may_wear`). *This sentence
said 11 for a day after that*, which is the section's own failure mode landing
on the section, again, and in the same window as the category-rung bullet below.
The other half is that a count cannot rot and a reason
can, which is what the table and the staleness check above are for. Each row's
blocker is §6.1 step 5's order: widen the opcode surface until a script can say
it, move it, delete the row. The `ai_queue3` deletion is the first time that
order ran to completion — and the honest summary of the pass that produced it is
**one row deleted, four blockers corrected**, with the corrections worth more,
because a wrong row is visible the moment someone tries to act on it and an
expired reason is not visible at all. `opobj` is the second, one day later, and
it is written up below in full because it is the first time the *order itself*
was measured rather than assumed.

**3. A server with no content and a server with content that binds nothing.**
This is the one worth the whole change. A fresh checkout has no script pack (the
compiler's output is gitignored), and `srv->scripts_ok` turned every fallback on
at once — so the default state of the repository was a **second, complete,
silently different implementation of the game**, discoverable only by finding a
behaviour where the two disagreed. `mock230_scripts_fallback` returns 0 when
there is no pack, and the load prints a banner:

```
mock230: ============================================================
mock230: NO SCRIPT PACK at OSRS-Content/osrs239-content/server/scripts/build
mock230:   cannot read .../script.dat
mock230: The game's behaviour is content, not C. Without the pack the
mock230: engine's fallbacks stay OFF and almost nothing will work —
mock230: no interactions, no buttons, no dialogue, no drops.
mock230: Build it:  make -C src mock230-scripts
mock230: ============================================================
```

Verified in the real client (`SDL_VIDEODRIVER=dummy`, `TORIRS_EXIT_BMP`): the
same login, side by side. With the pack, Lumbridge, fourteen items and the
`[login,_]` messages. Without it, the world and the gameframe — which are engine
— and an empty inventory, an empty chatbox, and clicks that do nothing. That is
what "fails visibly" looks like, and it is strictly better than a game that
plays.

### The lookup, and the one fallback

`SSVM_ProviderGetByTrigger` is `ScriptProvider.getByTrigger`: exact **type**,
then **category**, then the bare **`_` wildcard**, and nothing after that. The
wildcard is the only fallback the design has — the reference writes `[opnpc2,_]`,
`[opheld2,_]` and `[opheld5,_]` for exactly the behaviours listed above.

`make -C src test-ss-provider` pins the order with synthetic scripts, because
**every way of getting it wrong still finds a script**: swap the first two rungs
and the category's script runs for an item that had its own — the right kind of
thing happens to the right item and the wrong script did it. Drop the third rung
and every `_` in the tree stops firing, which reads as content nobody wrote.
Neither is a crash, an abort, or a suspicious log line. Both mutations were run;
both turn the test red.

Two call sites use `getByTriggerSpecific` (one rung, no chain) because the
reference does: `[login]` has no subject, and an `[if_button,_]` that swallowed
every click on every interface is not a fallback anyone wants
(`IfButtonHandler`).

A miss reports itself under `MOCK230_VERBOSE`, in the reference's words:

```
mock230: no trigger for [aploc1,tree2]
```

Only for **player-initiated** triggers, which is also the reference's division
(`Player.defaultOp` and the OpHeld/InvButton/IfButton handlers speak;
`World.spawnNpc` does not). An `[ai_spawn]` miss is 2,197 npcs at world init and
would be a wall of text rather than a diagnostic. The name comes from the same
`pack/` file the compiler resolved the script's subject through, so a name that
prints is a name that could have been bound.

### The category rung — obj and npc answer it, loc does not

The reference passes `type.category` for npc, loc and obj alike. Re-measured
2026-08-01 off `mock230_pack --check-only`, which prints the split:
**55 category names — 36 obj-only, 18 npc-only, 1 carried by both.**

- **obj** — config opcode 94, a number the cache states and `pack/category.pack`
  names. **37 names** (36 + the shared one), not the 6 the triage records —
  the weapon categories were added after that count was written. This is the
  rung `[opheld1,_bones]` already binds through, and it now also feeds
  `[opobj<n>]` and `[apobj<n>]`, which passed -1.
- **npc** — **19 names**, and this bullet is the second thing this section had
  wrong. It used to read *"there is no npc category in an osrs239 record at all.
  Not unread: absent"*, which was true when written and stopped being true at
  `ff2d172f` — `mock230_npcinfo.c` went from zero mentions of `category` to a
  populated field and `mock230_npc_category()`, and `44d42dff` then passed it at
  the `[ai_queue3]` site. The bullet survived both, plus two lane merges,
  including the commit whose own subject line is *"the AI_QUEUE3 category
  rung"*. Six `[ai_queue3,_<category>]` bindings in `drop_tables/` ride it
  today. Worth stating plainly: the section that exists to explain why the
  fallback list goes stale went stale in exactly that way, in the same window,
  and the count at the top of §3.18 could not see it because the count is not
  what rots.
- **loc** — `Mock230LocDef.category` exists and is **not this**: a private
  two-valued door enum with no entry in `pack/category.pack`. Passing it would
  alias every door onto category ids 0 and 1 and bind unrelated scripts to them.
  It stays -1, and `interaction_category` (`mock230_world.c:687`) says why,
  because the tempting edit is a one-liner. The real field needs
  `dat2_config_loc.c` to stop discarding config opcode 61 — an rscache
  write-path change, which is why this is the `oploc` row's expensive half and
  not an opcode.

### `[if_close]` was a fallback and should never have been one — and never ran

Two findings, stacked, both from reading `Player.closeModal` beside the code.

It runs the close trigger **and then** unmounts, unconditionally. This server had
`if( ran ) return;`, which made a bound `[if_close]` suppress the unmount it had
no opinion about — both of the tree's `[if_close]` scripts are one
`inv_stoptransmit`. So it is not in `enum Mock230Fallback`; the trigger is a
notification and the close is engine.

And under that: the dispatch asked with `MOCK230_COM(main_group, 0)` while the
compiler keys `[if_close,bankmain]` on the bare interface id **12**
(`[if_button,bankmain:note_graphic]` is the one that keys on a packed uid,
because *that* subject names a child). The two never met, so **no `[if_close]` in
this tree had ever run** — invisible because the only cost so far was the server
transmitting to a screen the player had closed.

The selftest pins the unconditional unmount (mutating it back turns the stanza
red) and, separately, the subject convention. It does not pin the call site's
expression, and that is a real limit rather than an oversight: `@closebank`
sends no packet, so a close that ran the script and one that did not are
identical from outside one process.

### One test that had stopped testing anything

`held-item content`'s "eating at full health should not overheal" passed because
the script was **dropped**, not because `stat_heal` clamped: `~eat_food` ends in
`p_delay`, and the next `OPHELD1` in the same tick hit the one-parked-script-
per-player rule. Nothing could have turned it red. It runs the delay out first
now and asserts the food was consumed, which is the evidence the heal happened
at all. The drop is what surfaced it — `run_or_park`'s message names both
scripts now, having named neither.

### `opobj` is gone — the order run to completion, in two stages

2026-08-02. The second row the list has lost, and the first whose whole sequence
was recorded as it ran, so it is written here as the worked example of §2.4
item 7: **widen the surface, land the content, verify it through the content,
and only then delete.** Stage one did the first three and deliberately left the
row standing. Stage two deleted it. **The count is 5.**

The two-stage split was not caution. It is the only order in which the evidence
exists at all — see *the mutation that means nothing until the C is gone*, at
the end of this section.

What landed in stage one:

- **The entity kind.** `SSVM_ENT_OBJ` had zero writers tree-wide. It has one
  now: `mock230_world_process_interaction`'s `MOCK230_INTERACT_OBJ` arm resolves
  the clicked pile to a ground slot and parks it on `srv->pending_active_obj`,
  which `run_trigger_script` consumes and clears. A one-shot latch rather than a
  sixth parameter on `mock230_scripts_run_trigger`, because `[opobj<n>]` is the
  only trigger family with an obj subject and the other eighteen call sites
  would have had nothing to pass.
- **Five opcodes**, in a fifth per-domain file `net/mock/mock230_ops_obj.c`:
  `obj_type` (3511), `obj_count` (3503), `obj_coord` (3502), `obj_takeitem`
  (3510), `obj_del` (3504). Coverage 250 → **255** of 401.
- **The content.** `player/scripts/pickup.rs2`, ported from the reference file
  of the same name, binding `[opobj3,_]` — op **3** because a rev-230 obj record
  names no Take verb and the client synthesises the row at menu index 2
  (`rs_minimenu_world.c:472`, `REVCONFIG_MINIMENU_OPOBJ3`).

**The active obj is a handle, not a slot, and that is the one design decision
worth carrying forward.** `srv->ground[256]` is a free list — `mock230_world_obj_add`
hands a freed index straight to the next drop — so the npc/loc `slot + 1`
convention is not safe here: a script parked between `obj_find` and
`obj_takeitem` would resume onto whatever landed in the slot meanwhile and take
it, silently. `mock230_world_obj_handle` packs the slot in nine bits with a
per-slot `generation` above it, and `mock230_world_ground_slot` refuses a handle
whose generation has moved. The reference does not need this because it holds a
real `Obj` reference; this is the same guarantee written over an index.

**`obj_del`'s duration is the one thing that does not port literally, and the
substitution predates this work.** The reference is
`World.removeObj(activeObj, ObjType.respawnrate)`; there is no obj `respawnrate`
in this tree — no decoder, no `fields/obj.ini` row, no value — so
`mock230_world_ground_take` substitutes content's `^lootdrop_duration`, which is
what the engine's own take has always done. `removeObj` ignores its duration
entirely for a non-spawn obj, so the two agree exactly on every drop; they
differ only in that the reference returns each map spawn at its own rate and
this returns all of them at one. A data gap, and the pre-existing one.

**A container fix came with it, and its refusal is the interesting half.**
`obj_takeitem` needs a stacking add; `SS_OP_INV_ADD` wrote the first free slot
and never merged, so taking coins twice opened two coin slots (reachable today
from `~pickpocket`). Both now go through `mock230_container_add`, a port of
`Inventory.add`. What is *not* ported is that method's other half — one slot per
unit for an unstackable obj — and the measurement is why: writing it turns four
bank assertions red at once, because `[proc,newplayer_bank]` says
`inv_add(bank, logs, 100)` and a bank stacks everything. The missing input is
`InvType.stackType`, which needs `fields/inv.ini` — the same one gap
`mock230_container_scope` is blocked on. The merge needs no per-inv field and
landed; the spread waits.

**Verified**, and the negative controls matter more than the positives here:

- `mock230 --selftest`, new stanza *"taking an obj is content's"* — the take,
  the enclosed `OBJ_DEL`, the ZoneMap replay count before/after, the handle's
  generation across a slot recycle, and the stack merge.
- Five mutations, all run: unbinding `[opobj3,_]`; making the handle drop its
  generation; stubbing the zone replay's obj loop; making `container_add` never
  merge; making the dispatch bind no active obj (7 checks red — the script
  aborts and, correctly, the fallback does **not** stand in for a script that
  failed).
- The real client, `manifest_osrs230_embed.ini` headless: right-click the tile
  reads *Take Bones*, clicking it prints *You pick up the Bones.* and the bones
  are in the backpack. With `[opobj3,_]` unbound the same run logs
  `no trigger for [opobj3,bones]` and the C picks it up instead — which is the
  check that the client run is measuring content and not the fallback.

#### Stage two: the deletion, and the mutation that means nothing until the C is gone

Deleted: `interaction_engine_obj` (~39 lines), its forward declaration, the
`mock230_scripts_fallback` call in the `MOCK230_INTERACT_OBJ` dispatch arm,
`MOCK230_FALLBACK_OPOBJ` and its comment, the `k_engine_fallbacks` row,
`k_blocked_opobj`, and `inv_stack_slot`, whose last caller it was. The count
assertion went **6 → 5**. `mock230_world_ground_take` stays — it is the removal
primitive `obj_del` and `obj_takeitem` both call, and its `ground_clear`-first
ordering is what makes a taken pile leave the *zone's state* rather than merely
produce a removal event.

**The measurement that decided the order, and it is the whole reason stage one
did not delete.** The two selftest legs that assert the take (*"taking an obj
puts it in the backpack"*, *"and the obj is taken on arrival"*) were re-pointed
from `OPOBJ1` to `OPOBJ3` **while `interaction_engine_obj` was still present**,
and then `[opobj3,_]` was unbound. They stayed **green** — the fallback answered
them, and only stage one's one discriminating leg went red (3 red in total).
After the deletion the same mutation turns **11** checks red, the two moved legs
among them. A leg that survives the mutation is measuring the C; the legs could
only become evidence of anything once the C was gone. That asymmetry — 3 red
before, 11 after, same mutation, same test file — is the argument for the order
in one number.

**What deleting it changes, measured rather than asserted.**
`interaction_engine_obj` took **no op number**, so it picked the pile up on ops
1, 2, 4 and 5 as readily as on 3. Across `configs/all.obj` the cache really does
state ground ops at those indices — **75 lines**: 30 `op4=Light` (logs), 18
`op5=Remove`, 9 `op1=Study`, three `op4=Lay` on hunter traps, one
`op4=Activate`. Every one of those was being answered by *taking the item*.
The reference answers them in **content** — `[opobj4,_category_22]` is
firemaking's Light (`skill_firemaking/scripts/firemaking.rs2:2`),
`[opobj1,yommiseeds]` is Legends' Quest — and its engine answers an unbound one
with `Player.defaultOp`: the message, and the walk that already happened. So the
dispatch arm now says `[proc,nothing_interesting_message]` on
`MOCK230_TRIGGER_NONE` and nothing on `FAILED`, which is `defaultOp` exactly.

Two objs in the whole 30k-record table lose a *working* Take by this:
`giant_bones` (`op4=Take`, because its op3 is Bury) and
`brain_deck_gun_powder_barrel` (`op1=Take`). Neither exists in this world, both
are one `[opobj<n>,<obj>]` line away, and both were being answered by accident
rather than by address. That is the behaviour decision, stated so it is a
decision and not a discovery.

**The permanent guard for the deletion itself** is a new pair of legs in the
same stanza: an `OPOBJ1` — which nothing binds — leaves the pile on the floor
and answers *"Nothing interesting happens."* Mutation: put any take back in the
dispatch arm and both go red; that was run. The *other* new leg is the one in
"interactions walk before they act" that was deliberately **left** on `OPOBJ1`,
because the latch is set by the packet handler and the trigger lookup happens on
arrival — an op no script answers still walks you there, and nothing else pins
that.

**Verified in the real client**, headless, `manifest_osrs230_embed.ini`,
`SDL_VIDEODRIVER=dummy`, three matched runs from a deleted save:

| run | ground | backpack | chat |
|---|---|---|---|
| A control — drop, walk away, never click | bones on the tile | slots 0-13 | — |
| B — drop, left-click *Take Bones*, walk away | **empty** | `14 = 526 1` | *You pick up the Bones.* |
| C — same as B with `[opobj3,_]` unbound | bones still there | slots 0-13 | ***Nothing interesting happens.*** |

The right-click menu reads *Take Bones / Walk here / Examine Bones / Cancel*,
and C also logs `no trigger for [opobj3,bones]`. C is the client-level proof
that the deletion took effect: before it, the same run picked the bones up.

**A harness gotcha found doing this and worth the line:** player saves are live
(`saves/<user>.ini`, written on logout), so a headless run inherits the previous
run's backpack and login tile. Two of these screenshots were confounded by
bones carried over from an earlier take before the save was noticed. Delete the
save between runs or the control is not a control.

### `opheld` is gone — two stages, and the blocker was never an opcode

2026-08-02, the same day as `opobj`. **The count is 4.** Two stages again, and
the split was the same discipline for the same reason — the first widened the
surface and deliberately left the row standing with the two bindings commented
out, the second wrote them and deleted the C.

Stage one is below as it was written, because it is the clearest example in
this document of §2.4 item 7's *first* step landing on its own: the opcodes in,
the content written, compiled and exercised, and nothing in the game changed.
Stage two is at the end.

**Five opcodes landed.** `oc_wearpos` (4213), `oc_wearpos2` (4214),
`oc_wearpos3` (4215) into `mock230_ops_obj.c` — which now holds the obj
domain's config half as well as its active-obj half, the way `mock230_ops_loc.c`
holds `loc_*` and `lc_*` — and `inv_movefromslot` (4318) and `inv_dropslot`
(4312) into a new sixth per-domain file, `mock230_ops_inv.c`. Coverage 250 →
**260** of 401.

**The `oc_wearpos*` three are the dangerous kind of missing, and this is the
general lesson rather than a fact about equipment.** All three have `known = 1`
in `ss_meta.gen.h`, so a script calling one **compiled and ran** — into
`unimplemented_stub`, which pushes **0**. And `0` is `^wearpos_hat`, a legal
slot. So a content `~wearpos_conflicts` written against the loud stub would have
found every worn item conflicting with every other and unequipped the lot,
plausibly, with nothing in the log. A declared-but-unimplemented opcode whose
stub value is *in range* is worse than one that aborts, and the coverage header
is the only thing that can tell them apart.

**`inv_moveitem` moved with them, and its gap is the one nothing could see.**
The opcode had three arms — bank→inv, inv→bank, worn→bank — and then

```
mock230: inv_moveitem %d -> %d is not modelled
```

a printf and a silent no-op, which is what every reference equip *and* unequip
path walks into (`inv_moveitem(inv, worn, …)`, `inv_moveitem(worn, inv, …)`).
`gen_opcode_coverage.py` reported the opcode **covered** the whole time, and
correctly by its own definition: it reads `case` labels, and there was one.
Coverage is a question about labels, not about arms. The three bank arms are
preserved byte for byte in the new file and the generic
container-to-container arm is appended *after* them, so no bank behaviour can
change by the move.

**Two cited blockers came off without being implemented, and both corrections
are worth more than the implementations would have been.**

- **`BUILDAPPEARANCE` (2004) — §3.13d, and the sharpest example of it so far.**
  Its job in the reference is `this.appearanceInv = inv; masks |= APPEARANCE`:
  it **selects which container the appearance encoder reads**
  (`Player.ts:1366`, `getInventory(this.appearanceInv)`). `put_appearance`
  (`mock230_encode.c:915`) reads `player->worn` unconditionally. So the obvious
  implementation — accept the argument, raise the mask — would make
  `buildappearance(<anything else>)` silently paint the worn set: plausible,
  wrong, quiet. The mask half meanwhile is *already* unforgettable here, and
  more strongly than in the reference: the worn container is adopted with
  `appearance = 1`, so every ServerScript write to it raises
  `MOCK230_PMASK_APPEARANCE` through `mock230_container_mark`, whereas
  reference content that forgets the call gets a stale appearance. Left to the
  loud stub. It becomes implementable the day the encoder gains a selectable
  source, and not before.
- **`P_CLEARPENDINGACTION` (2070) — misfiled.**
  `mock230_world_clear_pending_action` is called from `handle_move`, `opnpc`,
  `opobj`, `oploc`, `opheldu` and `useon_interact` and **never** from
  `handle_opheld` (`grep -n mock230_world_clear_pending_action
  net/mock/mock230_world.c`). It was cited because the reference opens its
  *unequip* binding `[inv_button1,wornitems:wear]` with it — and unequip is not
  this row.

`k_blocked_opheld` is therefore empty, and the row's text is rewritten to the
one thing that is actually left.

#### The one blocker as stage one measured it, and why the bindings waited

**The level requirement has no script-readable form.** `mock230_equipment_may_wear`
refuses the wear and says the two sentences, and it only ever runs from the
fallback — dispatch is content-first, so the moment `[opheld2,_]` exists the
fallback stops running and the gate goes with it. Binding it today equips a
rune platebody at level 1 and nothing says anything.

The data, re-measured: **857 objs, 1,254 (stat, level) pairs** across
`skill_combat/configs/*.obj` — 613 objs with one pair, 187 with two, 33 with
three, 24 with seven, 23 distinct stats.

    grep -rh param=levelrequire OSRS-Content/osrs239-content/server --include=*.obj | wc -l

`oc_param` cannot answer it, and `fields/obj.ini`'s `[obj.levelrequire]` already
says why *in the file*: a param maps one id to one scalar and this is a
repeating pair. There is no `oc_levelrequire` in the reference's `engine.rs2`,
so inventing one is barred — that is the `oc_desc` mistake.

Triage §10.1's **conclusion survives and its inference does not**, and the
inference was doing the work. It decided two things welded together: *the
requirement values are data, not script* (still right, and 1,254 pairs makes it
more right — nobody should hand-maintain 857 bindings) and *therefore the gate
stays in C* (does not survive: the stated reason was "eight opcodes it does not
have", and there are now none). A refusal-with-a-message is a **rule**, and
rules are content's. "The data is data" never implied "the gate is C"; the two
were joined only because nothing else could read the data.

The shape that fits is a `levelrequire` **dbtable** with two parallel `LIST`
columns, read with `db_find` / `db_findnext` / `db_getfield` — all three
implemented (`mock230_ops_db.c`) and already used over a `LIST` column by
`combat.dbtable`. One `[opheld2,_]` binding rather than 857, still generated
from the same `param=levelrequire` lines `mock230_pack` validates, so §10.1's
single source of truth holds literally. That is a data-relocation job and it is
the next stage's, **measured before it is written**.

#### What is written, and how it is proven without a binding

`player/scripts/equip.rs2` (`~equip`, `~unequip`, `~try_equip`,
`~unequip_conflicts_space`, `~unequip_conflicts`, `~wearpos_conflicts`) and
`player/scripts/drop.rs2` (`~dropslot`), ported from the reference files of the
same name minus what this tree has no model for — the duel arena, gnomeball,
tutorial island, `elemental_shield`, `ibanstaff`, `%sa_attack`, and
`~update_all`, each named in the file rather than dropped quietly.

They are reached by `[debugproc,equip]` / `[debugproc,dropslot]` in
`general/scripts/misc/cheat_equip.rs2`. **That makes the evidence unambiguous by
construction rather than by a discriminating leg**: with `[opheld2,_]` unbound,
`::equip` can only reach content, and `handle_opheld`'s verb ladder is not in
the path at all. The selftest stanza *"equipping is content's rule"* drives it,
and the mutation is deleting a debugproc and rebuilding the pack.

The `[debugproc,equip]` **shadows an engine `::equip <slot>` cheat** that calls
`equip_from_slot`, because the cheat handler offers every line to a debugproc
first. That is the reference's posture and the right way round — a cheat written
in C exercises the C rather than the shipped path — but it is a silent
replacement, and `mock230_scripts_report_shadowed_ops` cannot see it: that
report walks *trigger*-addressed scripts and a debugproc is name-addressed. It
is written down in the content file, which is the whole of the record.

#### The behaviour this changes, which is why "nothing changed" would be the wrong result

`~wearpos_conflicts` compares all 3×3 wearpos pairs **in both directions**.
`equip_from_slot` only ever collects the *incoming* item's claims, so equipping
a shield while a two-hander is worn looks at `worn[SHIELD]`, finds it empty, and
leaves both on. Wearing a shortbow and a kiteshield at once is a live defect and
the port fixes it. The existing engine test only runs shield-then-bow, which is
the direction that already works.

Demonstrated in the real client, headless, `manifest_osrs230_embed.ini`,
`SDL_VIDEODRIVER=dummy`, matched runs from the same checked-out save, one line
of content apart:

| run | cheats | `saves/embed.ini` `[worn]` | chat |
|---|---|---|---|
| B — `[debugproc,equip]` bound (content) | `equip 5;equip 3` | **`5 = 1189`** only | *You equip the Shortbow. / You equip the Bronze kiteshield.* |
| D — same run, that one proc commented out (engine) | `equip 5;equip 3` | **`3 = 841`** *and* `5 = 1189` | *Equipping Bronze kiteshield (category 18).* first |
| C — drop | `dropslot 0` | — | *You drop the Bronze full helm.* |

The engine's answer is **not asserted in the selftest**, deliberately: asserting
that a defect is still present is the check this section already costed and
rejected, because fixing `equip_from_slot` without moving the row is a
legitimate change that would fire it falsely. Content's answer is asserted; the
engine's is recorded here so that whoever deletes the C knows what changes.

Mutations run, all seven, each turning the suite red exactly where intended:
unbinding `[debugproc,equip]` (9 checks); `oc_wearpos3` returning `wearpos_2`
(13 of 64 probed objs); `oc_wearpos2` returning `wearpos_3` (the two-hander
eviction and the `worn→inv` move); `oc_wearpos` pushing **0**, which is what the
loud stub did (57 of 64, plus four behaviour legs); the generic `inv_moveitem`
arm made a no-op; `inv_dropslot` ignoring its duration argument;
`inv_movefromslot` made a no-op.

The `oc_wearpos*` leg is **exhaustive against `Mock230ObjInfo`** rather than
behavioural, and the reason is worth stating: nothing observable in this tree
depends on `wearpos_3`. It is only ever 8 or 11 — hair and jaw, body-kit
positions no item is ever *worn* in — so `~wearpos_conflicts` gives the same
answer whatever `oc_wearpos3` returns. The appearance encoder is what reads
those two, and it reads them from C.

#### What is NOT done, stated so the row is not read as smaller than it is

- **The worn tab's unequip.** `unequip_slot` sits *before* the content-first
  gate in `handle_opheld` (the worn-component branch `return`s), so no script
  can pre-empt it and deleting the row would leave it running unconditionally.
  `[proc,unequip]` is ported and has **zero callers** for that reason; moving it
  needs `[inv_button1,wornitems:wear]`, `if_close`, `p_finduid` and
  `P_CLEARPENDINGACTION`. Evicting the row while claiming unequip moved would be
  dishonest.
- **`[opheld5,_]` alone.** The drop half needs nothing missing and its content is
  written — but `handle_opheld` answers Wear, Wield and Drop from one verb
  ladder, so binding drop alone would leave the row standing with a reason that
  no longer describes what is under it. Both bindings go in the commit that
  deletes the ladder, the row, and `k_engine_held_verbs`' Wear/Wield/Drop
  entries — the last mandatory in the same commit, or a later
  `[opheld2,<obj>]` is reported as shadowing a verb nothing answers.
- **`Inventory.add`'s one-slot-per-unit spread** for unstackables, still, and
  still for `fields/inv.ini`'s `stackType`. `inv_movefromslot` and the generic
  `inv_moveitem` arm inherit that limit.

#### Stage two: the level requirement is a MERGE, and every account of it named one half

The go/no-go stage one set was "can the `levelrequire` dbtable be authored and
read in this lane?" It can, and it is
`skill_combat/configs/levelrequire.dbtable` — but building it turned up
something worth more than the table.

**The requirement comes from two sources and the `.obj` overlay is 59% of it.**
Every prose statement of this — triage §10.1, `equipment_lostcity.obj`'s own
header, the fallback row, stage one above — says *857 objs, 1,254 pairs* and
names `param=levelrequire` in `skill_combat/configs/*.obj`. Measured off the
running server (`mock230: … 1496 equip reqs (675 from the cache)`):

| source | objs | pairs |
|---|---:|---:|
| the `.obj` overlay, `param=levelrequire` | 857 | 1,254 |
| the cache's own `skillrequire`/`levelrequire` params, on objs no `.obj` names | 639 | ~868 |
| **effective** | **1,496** | **2,122** |

The cache states its own in params 434/436 and 435/437 —
`configs/all.param.compack` names them — and `read_requirements`
(`mock230_objinfo.c`) reads them for every wearable, filtered by
`gates_wearing()`. The `.obj` overlay then **replaces** an obj's set outright
rather than adding to it, which is what lets `equipment_disputed.obj` *correct*
the cache for the eighteen items where the two disagree.

A content gate written against the dbtable alone would have passed a structural
check against the `.obj` files and silently stopped gating 639 items — a rune
scimitar among them, whose Attack 40 is the cache's. That is the failure this
list exists to prevent, arriving through the replacement rather than through the
thing being replaced.

So nothing was relocated. `~levelrequire_check`
(`skill_combat/scripts/levelrequire.rs2`) reads **both halves, each in its own
natural form**: the overlay from the dbtable, the cache's two pairs through
`oc_param`, which is already the opcode for reading a cache param.

**The table is the transpose of the obvious one**, and that is the only design
decision in it. A row is one *requirement* — `stat` and `level` scalars, `obj`
a LIST of everything that needs them — so it is **125 rows, not 857**, because
the requirements repeat hard (111 objs want Defence 40). And `db_find` on a
LIST column means *contains*, so "which requirements does this obj have" is
literally "which rows contain it" and `db_findnext` walks them: no arithmetic,
no parallel-column indexing. Rows are emitted sorted by (stat id, level), which
reproduces the `.obj` files' own line order for 855 of the 857 — load order
decides which skill the player is told about, because only the first unmet
requirement is named.

`tools/gen_levelrequire_dbrow.py` generates it and `--check` gates it, but the
check that matters is not textual: the selftest walks **every** gated obj at
both sides of its own boundary and compares content's answer to the C table
`mock230_obj_require` still holds. A regeneration would make a diff go away
without anyone learning the gate had been wrong in between.

#### The trap it found: a bare stat name in a comparison compiles to the wrong number

`~levelrequire_gates_wearing` was first written the obvious way:

```
if ($stat = attack | $stat = defence | ...) { return(true); }
```

It compiles, it runs, and it is false for **every Attack requirement in the
game** — 357 of the 1,496 gated objs, silently wearable. A bare name resolves
through `SSC_SymbolsFind(.., SSC_SYM_UNKNOWN)`, which answers with the
lowest-numbered *kind* holding that name, and cache.osrs239 spends three of the
23 skill names on something that sorts earlier: **`attack` is also varp 259**,
`hitpoints` param 2100, `fishing` loc 20926.

That trap is already on record — the `stat_heal(hitpoints, 3, 0)` that healed
nothing — and its fix, `compiler->arg_kind_hint`, only reaches *arguments of the
`stat_*` command family*. A bare comparison has no command to take a hint from,
so **a comparison against a stat literal has no safe spelling in this compiler
at all.** The seven skills live in `skill_combat/configs/levelrequire.enum`
instead, keyed `inputtype=stat`, because a config's keys are resolved by the
loader against the declared type and never by the symbol table.

Worth stating as a general rule rather than a fact about equipment: *the safe
way to name a stat in a script is a config key, not an identifier.*

A second, smaller one came with it. `gates_wearing` is a filter on the **cache's**
data only — it exists because a fire battlestaff's Crafting 62 is what it takes
to *make* one. Applying it to the overlay too makes `slayer_earmuffs` and
`slayer_nosepeg` wearable at level 1, because an authored `slayer 15` means
exactly what it says. Twenty-six objs; the exhaustive leg found every one.

#### Deleted, and the mutation that means nothing until it is

`equip_from_slot` (83 lines with its comment), the `handle_opheld` verb ladder,
`MOCK230_VERB_WEAR` / `WIELD` / `DROP` **and `k_engine_held_verbs`**, the
`[opheld<n>]` arm of `mock230_world_engine_claimed_verb`,
`mock230_equipment_may_wear` (45 lines) with the `equip_level_message` hook —
the sanctioned-hook list is **10** now, and this is the first time it has ever
gone down — the `::equip` C cheat branch, `MOCK230_FALLBACK_OPHELD`, its
`k_engine_fallbacks` row and `k_blocked_opheld`. The count assertion went
**5 → 4**.

`unequip_slot` **stays**, and the row said it would: it sits *before* the
content-first gate in `handle_opheld`, so no script can pre-empt it.
`[proc,unequip]` is ported and still has zero callers. Evicting the row is not a
claim that unequip moved, and the selftest now asserts the C answer explicitly
so that nobody reads it as one.

`mock230_obj_require` and the `.obj` loader stay too: `mock230_pack` validates
those lines, the generator reads them, and the selftest cross-checks both forms.

**The measurement, repeated from `opobj` and coming out the same way.** Same
mutation, same test file, before and after the deletion:

| state | mutation: unbind `[opheld2,_]` | checks red |
|---|---|---|
| bound, `equip_from_slot` still present | the ladder answers | **0** |
| after deletion | nothing answers | **18** |

Zero, not three. The whole suite was green with the content unbound, because
the C and the content agreed on every case the tests covered — which is exactly
what "verify, then delete" cannot be shortened to "verify". Unbinding
`[opheld5,_]` after the deletion turns 4 red.

Seven more mutations, all run, all red where intended: dropping the `oc_param`
half of `~levelrequire_check` (7); dropping the dbtable half (7); `stat()` for
`stat_base()` (8); removing one `data=obj,` line from `levelrequire.dbrow` (1);
applying `gates_wearing` to the overlay path too (1); spelling
`~levelrequire_gates_wearing` with bare stat names (7, and the leg reports *122
did not* — Attack requirements only).

#### What the selftest looks like now, and one assertion that had to move rather than go

Three stanzas — `equip / unequip`, `equipment level requirements`, `two-handed
weapon evicts the shield` — drove `equip_from_slot` and
`mock230_equipment_may_wear` as C symbols and could not survive the deletion.
Every claim they pinned is kept, in `equipping is content's rule`, driven by a
real OPHELD2 packet: the helm reaching the head slot and the cell it left, the
appearance mask, the `worn_dirty` bit, the merge at **both** ends, base-not-
boosted, and the two-hander/shield eviction now in **both directions** — only
one of them ever worked and the port fixed the other. They were not left in
place with the calls swapped because they ran with no pack loaded and a
packet-driven equip needs one.

`the wield refusal is content's, words and all` changed what it measures. It
asserted the *words* while the engine made the decision; the engine no longer
decides, so it is the leg that proves the gate moved with its wording intact.

**The pack/no-pack pair in `the inverted fallback` stayed on OPHELD2 and got
stronger.** It used to say: with a pack the *fallback* wields because nothing
claims `bronze_sword`; without one it must not. There is no such C, so with a
pack it is **content** that wields and without one nothing at all does — which
is the inversion in its purest form, not "the fallback declined to run" but
"there was never anything else to run". Before, that leg was satisfied by
either implementation and stayed green under the unbind mutation; now it goes
red. The three-input gate check beside it moved to `MOCK230_FALLBACK_OPNPC`,
because the gate is a property of `mock230_scripts_fallback` and not of any one
row — OPNPC rather than OPLOC deliberately, `oploc` being the next expected to
go.

#### In the real client

`manifest_osrs230_embed.ini`, `SDL_VIDEODRIVER=dummy`, `MOCK230_VERBOSE=1`,
right-click the inventory cell and click the row — the click a player makes,
not a cheat:

| run | menu row | server log | chat |
|---|---|---|---|
| bronze full helm | *Wear* | `<- OPHELD2 obj=1155 (Bronze full helm) slot=0 verb=Wear` | *You equip the Bronze full helm.* — and the helm leaves the cell and appears on the head |
| abyssal whip, Attack 1 | *Wield* | `<- OPHELD2 obj=4151 (Abyssal whip) slot=11 verb=Wield` | ***You are not a high enough level to use this item. / You need to have an Attack level of 70.*** — both lines, in order, and the whip stays in the cell |
| abyssal whip | *Drop* | `<- OPHELD5 obj=4151 (Abyssal whip) slot=11 **verb=-**` | *You drop the Abyssal whip.* — and it is on the tile |

The drop row's `verb=-` is the evidence that binding on the op **index** rather
than on the word "Drop" is right: the record states no fifth verb at all, the
client synthesises the row, and the old ladder reached it through a
`!verb && op_num == 5` special case.

**Two harness findings, both costing real time and neither caused by this
change.** *Left-clicking an inventory item aborts the client* —
`CS2VM2: unimplemented opcode 1928 (CC_TRIGGEROP)`, then the `StackMetaStub`
assert, exit 134, before any packet is sent. Going through the right-click menu
avoids it. And *the headless client is not frame-deterministic*: the same
`TORIRS_SIM_CLICK_AT` frame lands at a different point in the async load run to
run, so an inventory cell that held the helm in one run is empty in the next.
Reproduced with the binding present **and** absent, so it is the harness. The
negative control is therefore the selftest mutation above rather than a fourth
screenshot, and that one is the rigorous version anyway.

### Where the list stands, re-measured 2026-08-02

Four rows. The numbers below are re-run rather than quoted, because everything
this section is about is what happens when they are not.

```
mock230: 4 engine fallback(s) still answer triggers content does not bind
mock230_pack --check-only  0 error(s), 15 warning(s)
mock230 --selftest         all checks passed
coverage                   260 / 401 declared opcodes
sanctioned hooks           10  (mock230_scripts.c:203-212)
report_shadowed_ops        1   ([oploc2,bankbooth])
```

| row | what still stands in C | measured |
|---|---|---:|
| `opnpc` | `interaction_engine_npc` — a `strcmp` against the cache's Attack verb, the `FACE_ENTITY` latch, then `[proc,npc_default_chat]` | 35 lines, `mock230_world.c:2326-2360` |
| `oploc` | `interaction_engine_loc` | 85 lines, `mock230_world.c:2525-2609` |
| `inv_button` | `mock230_bank_quantity_for_op` | 108 lines, `mock230_bank.c:1060-1167` |
| `if_button` | `mock230_bank_handle_button` | 65 lines, `mock230_bank.c:1250-1314` |

**293 lines**, down from ~370 across six rows. Two of the four `blocked_ops`
arrays are now empty — `opnpc`'s always was, and it is the row that says so in
its own first clause. `oploc` cites `P_OPLOC`, `LOC_CATEGORY` and `LC_CATEGORY`;
`inv_button` and `if_button` share the one undeclared `SS_OP_LAST_VERB` carried
as `-1` behind an `#ifdef`.

**One correction that this pass found and did not apply**, because it is a C
edit in a file another lane is holding: `opnpc`'s `blocked_on` cites
`mock230_world.c:2333-2364` and the function is at **2326-2360**. Six lines of
drift, caused by the `opobj` and `opheld` deletions rather than by anyone
touching the row. It is the least harmful kind of stale — a reader who runs the
command lands six lines away and finds the function — but it is the one form of
staleness a `file:line` blocker is structurally exposed to, and it argues for
citing a **symbol** wherever a symbol will do. `interaction_engine_npc` is
`grep`-able and cannot drift; `mock230_world.c:2333` cannot help drifting.

### The opposite failure, which inverting the fallback could not catch

Everything above makes a **missing** script loud. Triage §7.7 names the mirror
image, and none of it addressed that: a script that is present, runs, succeeds —
and quietly takes a verb the engine was going to answer. The engine's verb
handling only runs when nothing was bound, so binding `[opnpc2,goblin]` does not
add to Attack, it *replaces* it. Nothing fails. The goblin says its line and
stands there.

That is not hypothetical, and the scar is in the content rather than here:
`skill_combat/combat.rs2` records that a goblin's Attack is op 2, that
`[opnpc2,goblin]` replaced it, and that the fix was to add `p_opnpc(2)` back. It
then states the rule for everyone else — *"any other script that binds an op the
cache gives a verb to has the same obligation"* — and until now the only thing
enforcing that rule was whoever remembered reading it.

`mock230_scripts_report_shadowed_ops` enforces it at load, beside the gap report
and for the same reason: a script behind a quest step may never be triggered by
anyone, and a verb nobody clicks this session is still swallowed. It walks every
trigger-addressed script, and where the binding names an exact type whose cache
op list carries a verb the engine implements, it asks whether the script
re-issues the op. **Today the whole tree yields no lines at all**, and the way it
got there is the finding rather than the number.

It read one — `[oploc2,bankbooth]` taking "Bank" without re-issuing it — and that
one was *correct*, because `~openbank` does what the engine's Bank branch did.
Which is the shape of the check rather than a flaw in it: the second legitimate
way to discharge the obligation is to do the engine's job yourself, and nothing
static separates that from doing something else. So it prints a list and never
fails a load — the same posture as `mock230_pack`'s foreign-area spawn warning
(triage §10.2): a prompt to go and look, not a verdict.

Then it read **20** when the ladders landed, and **97** when the 78 bank booths
did. Every new entry was the same correct-but-shadowing shape, and 97 review
items nobody can act on is a list that has stopped being read. The response was
not to suppress them: `interaction_engine_loc` was what they were all shadowing,
and deleting it (§3.18, the `oploc` row) took the report to **0** in one hunk.
What remains claimable is "Attack" on an npc and "Wear"/"Wield"/"Drop" on a held
obj, and nothing in the tree binds over those. `[opnpc2,goblin]` was and is
*absent* from the list because it calls `p_opnpc(2)`; a version of this that
could not tell the goblin from the booth would be measuring nothing, and the
selftest still asserts the goblin half directly for exactly that reason.

Two things worth being exact about, because both bound the claim:

- **Only exact-type bindings are visible.** A category binding
  (`[opheld1,_vegetable]`) or a bare `_` wildcard names no record, so there is no
  op list to read. Those are invisible here and the check is not total.
- **The verbs are one list now, not seven** — and four of them have since left
  C entirely. The engine's claims were nine `strcmp` literals scattered over
  three functions — `Attack`, `Wear`/`Wield`/`Drop`, and
  `Bank`/`Use-quickly`/`Climb-up`/`Climb-down`/`Climb`. The five loc verbs went
  with `interaction_engine_loc` on 2026-08-02; the four that remain are the npc
  and held ones. A report keeping
  its own copy would eventually say the opposite of what the runtime does, so
  `mock230_world_engine_claimed_verb` and the runtime branches read the same
  constants. That they are string literals in C is the standing PORTING_GUIDE
  §2.4 item 2 violation and is *not* fixed here: the verb is the cache's own word
  and comparing against it is how the engine reads the menu. What changed is one
  occurrence each instead of seven.

The argument for doing this before the bulk import rather than after is §7.7's
own: the reference binds 634 `[opnpc1]` and 867 `[oploc1]` triggers, and
`levelrequire/` alone binds 304 `[opheld2]` — which is exactly the verb the
engine equips on. After the import a swallowed verb is one of a thousand new
triggers; before it, it is one of forty-five.

Three mutations were run against the assertions, because the lesson this file
keeps relearning is that a test which cannot fail is worse than none:

| mutation | what went red |
|---|---|
| the claim matches any verb, not just the engine's | the booth's op 3 (`Collect`) assertion **and** the count — 34 shadows instead of 1 |
| the re-issue test always passes | the count alone, 0 instead of 1 |
| the npc claim reads op 1 instead of the trigger's op | the goblin's op-2 assertion |

The third is the one worth keeping. A goblin's Attack is op 2 and a guard's is
op 1, so an implementation that assumed the first slot passes every test that
only ever asks about the first slot.

---

## 3.19 Queues and timers, and the name-keyed dispatch path

Triage §9 step 5a. `[queue]` (153 uses in the reference), `[timer]` (34),
`[softtimer]` (1) and `[ai_timer]` (87) — 275, re-measured, where the triage says
273 because it omits `softtimer` and counts `queue` at 152.

**Roughly two thirds of it already existed and was wrong in ways nothing could
see.** All four triggers reached a script before this; what was missing was the
*semantics*, and every gap was invisible for the same structural reason — the
engine had no notion of a player being unable to act, so a queue was an
`n`-tick-delayed immediate call rather than a queue.

### 1. `canAccess()`, which is what makes a queue a queue

`Player.canAccess()` is `!protect && !busy()`, and `busy()` is
`delayed || containsModalInterface()` — MAIN or CHAT, deliberately not the
sidebar. `mock230_scripts.c:player_can_access` is that, minus `protect`: the
reference uses that flag to stop two protected scripts overlapping inside one
tick, and this engine gets the same property from the one-parked-script-per-player
rule in `run_or_park`. That divergence is in the permissive direction and is
stated rather than hidden.

The drain decrements **unconditionally** and gates only the *run*, which is the
reference's shape and not a rearrangement of it: an entry that comes due while a
dialogue is open goes to zero and stays there, so it fires on the tick the
dialogue closes rather than restarting its wait.

Three things fell out of adding the gate, and all three are the interesting part
of this stage:

- **A finished dialogue takes its own chatbox down, and this engine never did
  it.** `Player.executeScript`, on FINISHED or ABORTED, and only for the script
  that was parked: `if ((modalState & MAIN) === NONE) this.closeModal(false)` —
  "close chat dialogues automatically and leave main modals alone", in its own
  comment. Nothing depended on it here until `canAccess()` existed, and then
  everything did: `[label,cooks_assistant_completion]` ends `~mesbox(...)` then
  `queue(cooks_quest_complete, 0, 0)`, verbatim from the reference, and without
  the auto-close the chatbox stayed mounted forever, the player was permanently
  busy, and the quest reward never ran. This is the one place the reference calls
  `closeModal(false)` — the `false` means *do not discard the weak queue* — so
  `mock230_world_close_modal_ex(srv, clear_weak_queue)` exists for exactly one
  caller.
- **The player's queue has a cap and the reference's does not.** 16 slots was 16
  entries in flight while everything drained on its next tick; an entry that can
  wait makes it 16 entries *outstanding*, and a fight with a level-up message box
  on screen adds one `[queue,playerhit_n_retaliate]` per hit. `queue_hook` says so
  now instead of returning 0 in silence — a dropped `[queue,player_death]` looks
  exactly like a death that never ends.
- **The selftest was carrying dialogue state between sections.** Every stanza that
  fights or talks used to leave a chatbox mounted; nothing noticed, because a busy
  player was not a concept. `selftest_park_player` closes the modal and clears the
  queue now, for the same reason it already cleared the pending interaction.

### 2. Timer type, and an absolute clock

`SS_OP_SETTIMER` and `SS_OP_SOFTTIMER` shared one `case`, and `struct
Mock230Timer` had nowhere to record which one had been used. Both halves were
wrong, in opposite directions: `Player.processTimers` runs SOFT **while busy** and
**without** protected access, and NORMAL only with access and **with** it. So
`run_script_id` takes the protect flag rather than always adding
`SSVM_PTR_PROTECTED_PLAYER`, and the drain is two passes, normal then soft, as
`World.processPlayers` runs them.

`clock` is now the absolute world tick of the last arm-or-fire, testing
`tick >= clock + interval`, matching `setTimer`'s `clock = World.currentTick`.
That is not cosmetic: `gettimer` returns `timer.clock`, so a countdown makes the
opcode unimplementable, and a relative counter also fires once per *call* rather
than once per *tick* — the prayer-drain stanza was calling the drain five times
inside one tick and getting five drains.

`cleartimer` and `clearsofttimer` are the same operation in the reference — both
are `clearTimer(id)`, which deletes by script id with no regard for type. Two
names, one behaviour, not an oversight to fix.

**There is no lower bound on the interval, and that is deliberate.** An earlier
draft skipped `interval <= 0`, which reads as a sanity check and is a behaviour
change: `Player.processTimers` tests `World.currentTick >= timer.clock +
timer.interval` and nothing else, so an interval of **0 is a timer that fires on
every tick**, not a stopped one — only `cleartimer` stops a timer. The guard left
such a timer *set*: it held its slot, `gettimer` still answered for it, and it
never fired again. It is reachable from real reference content rather than
theoretical — `settimer(agilityarena_pillar, sub(%agilityarena_next_pillar_time,
map_clock))` is 0 or negative the moment that deadline has passed — and it was
found by mutation: re-introducing the guard is now red on
`[proc,selftest_timer_zero_arm]`'s two checks.

### 3. Queue kinds

`enum Mock230QueueKind`: NORMAL, LONG, WEAK, STRONG. One array where the
reference keeps two lists; the kind is what the drain splits on. The only
observable difference between the kinds is *when they are cleared*:

- **STRONG** closes whatever modal is up **before** the drain, so its own entry
  passes the access check on the tick it is due (`Player.processQueues`).
- **WEAK** is discarded whenever a modal closes (`Player.closeModal`'s first
  statement). Implementing `weakqueue` without that would have been a synonym for
  `queue` that content could not tell apart.
- **LONG** carries a logout action. It is stored and not yet read, because
  `phase_logouts` is empty.

ENGINE and SOFT are deliberately **absent**. ENGINE's only producer is the zone
family (step 5c) and SOFT is declared in the reference and never used — a kind
nothing can put in the queue is a branch no test can reach.

Seven opcodes landed with it, all of which had a real signature in
`ss_meta.gen.h` and no implementation, and the VM **aborts** on an unimplemented
opcode rather than no-op'ing, so each was a hard stop for any content using it.
Reference call sites, measured: `settimer` 75, `cleartimer` 70, `clearqueue` 39,
`longqueue` 15, `getqueue` 14, `gettimer` 8, `clearsofttimer` 2, `strongqueue` 1,
`weakqueue` 0.

`strongqueue` pops three ints here where the reference's handler also pops a type
string. That is not a divergence in either engine's favour: this compiler emits
the arity `engine.rs2` declares and refuses the vararg forms outright, so both
halves of *this* tree agree; the reference's bytecode for the same source line
differs and `test-ss-corpus` only decodes it.

### 4. Two npc-side corrections, re-derived rather than inherited

- **`npc_queue(q, arg, 1)` fires on tick +1, not +2.** The reference compares the
  value the counter had *before* its decrement for a player (`const delay =
  request.delay--`) and the value it has *after* for an npc (`request.delay--; if
  (request.delay <= 0)`), so an npc's delay 0 and delay 1 both land on the next
  npc phase where a player's do not. This engine stored `delay + 1` on both — right
  for the player, one tick late for the npc — and **the selftest asserted the wrong
  convention**, which is why it had to be re-derived from `Npc.ts` and not
  preserved. The drain is also gated on the npc not being delayed, with the
  decrement inside the gate: "purposely only decrements the delay when the npc is
  not delayed", which is the opposite of the player's queue.
- **Phase 4 runs timers before queues**, which is `Npc.processNpc`'s order
  (`processTimers()` then `processQueue()`). It ran the other way round under a
  comment claiming it matched.

And one deletion: `mock230_scripts_process_npc_timer` gated on
`Mock230Npc.timer_script >= 0`, and `timer_script` was written in exactly one
place, only ever to `-1`. The function could not run. It is gone, with the field
and the write; `timer_interval`/`timer_clock` stay, because the live drain in
`advance_npcs` is the one that reads them.

### 5. The name-keyed dispatch path

`mock230_scripts_run_trigger_at(srv, trigger, level, x, z)`, beside
`mock230_scripts_run_if_button` and sharing `run_trigger_script` with it for the
same reason: the keyed and name-addressed forms must not come to disagree about
what a trigger's execution context is.

The zone family is the only one whose subject is a *place*, and a place is not a
type id. It takes coordinate components rather than a packed coord because the
two granularities disagree about level:

```
zone/zoneexit  : "[%s,%d_%d_%d_%d_%d]"  level, x>>6, z>>6, ((x&0x3f)>>3)<<3, ((z&0x3f)>>3)<<3
mapzone/…exit  : "[%s,0_%d_%d]"         x>>6, z>>6        (level literal 0, argument ignored)
```

`lx`/`lz` are tile offsets 0, 8, … 56 — not zone indices 0..7, and not
zero-padded. `mapzone`'s level is a literal 0 because the reference builds that
latch with `CoordGrid.packCoord(0, x, z)`, so a climb inside one map square does
not re-enter it. That is the 427-vs-379 split, in two format strings.

**It takes no keyed rung, and that is a correctness requirement.** `ssc_lex.c`
packs a 5-part coord into 28 bits and the compiled `lookup_key` gives its subject
21, so a zone's key either goes negative — and `ssvm_provider.c` deliberately
keeps negatives out of the index — or wraps onto a subject no runtime lookup
reproduces. Name is the only address these four have. (The compiler-side tidy-up,
setting `*out_lookup_key = -1` for coord subjects so this is true by construction
rather than by accident, belongs with step 5c.)

A miss is **silent**: every tile in the world misses at least three of the four,
which is §3.18's `[ai_spawn]`-across-2,197-npcs argument, and these are
engine-initiated rather than player-initiated.

Nothing in the tick calls it yet. It landed here, one stage early, because it is
the riskiest structural piece in the lane and its failure mode is *silence* — if
the format string disagrees with the compiler's spelling by one character, or if
the lexer ever stops preserving a coord subject's raw text, every zone script in
the tree simply never runs and nothing anywhere says so. The selftest calls it
directly against an authored `[zone,0_50_50_16_16]` and `[mapzone,0_50_50]`,
which also separates "the name resolves" from "the latch fires it" — two
independent failures that would otherwise present identically.

### Verified

`make -C src test-mock230`, stanza `player queues, timers, name-keyed dispatch`,
plus the rewritten npc-queue and prayer-drain assertions. Thirteen mutations were
run and each turned exactly the assertions it should red:

| mutation | what went red |
|---|---|
| `process_timer_pass` ignores `type` | the busy-player timer split, 202 instead of 200 |
| `clearqueue` is a no-op | "clearqueue cancels it rather than delaying it" |
| zone `lx`/`lz` not truncated to the zone | the whole name-path block |
| `mapzone`'s name carries the real level | "the level is a literal 0 in its name" |
| the queue decrement is gated on access | "runs it on the very next tick, not one delay later" |
| `strongqueue` does not close the modal | both strong assertions |
| npc queue back to `delay + 1` | "[ai_queue1,chicken] should fire on tick +1" |
| the timer clock is relative again | the busy-player split, 400 instead of 200 |
| the timer clock is zeroed at arm | `gettimer` returns 0 instead of the arm tick |
| `gettimer` pushes 0 for an unset timer | "-1 for a timer that is not set" |
| `getqueue` counts at most one | "getqueue counts every copy" |
| the weak queue survives a modal close | "closing a modal discards the weak queue" |
| no auto chat close when a script finishes | **the whole Cook's Assistant stanza** |

The last one is the load-bearing one: it is the mutation that shows the auto-close
is not a tidy-up but the thing that lets reference content run at all here.

Headless client, `manifest_osrs230_embed.ini`, `SDL_VIDEODRIVER=dummy`,
`TORIRS_NET_CHEAT="setlevel prayer 40;pray 18"`: `[debugproc,pray]` arms
`[timer,prayer_drain]` through content and the drain reaches the real client as a
run of `UPDATE_STAT` packets spaced over ticks — the NORMAL timer path end to end,
with the absolute clock and the access gate in it. What that run does **not**
cover is the auto chat-close from a real click, which is asserted in the selftest
through the real `RESUME_PAUSEBUTTON` handler instead.

### Still open after this

`[logout]` does not clear queues or timers (`Player.cleanup()` does) — noted and
not landed, because `phase_logouts` is empty and `mock230_save.c` still has no
callers, so "a returning player" is not a case anything can be tested against.
The npc queue stores its `arg` and the drain does not pass it: `[ai_queue<n>]`
gets no `last_int`, where the reference sets `state.lastInt = request.lastInt`.
And `MOCK230_QUEUE_MAX` is a cap the reference does not have.

---

## 3.20 Use-on: the `*u` family

Triage §9 step 5b. `[opheldu]` 230 uses in the reference, `[oplocu]` 212,
`[opnpcu]` 93, `[opobju]` 3, `[opplayeru]` 3, `[aplocu]` 2, `[applayeru]` 2,
`[apnpcu]` 1, `[apobju]` 0 — **546**, re-measured, where the triage's 535 is the
three commonest forms and silently drops the other five. **541 of the 546 land
here**; the two player forms do not, and §"What is not here" says why.

Where LostCity puts it: **engine**, five packet handlers
(`OpHeldUHandler`/`OpLocU`/`OpNpcU`/`OpObjU`/`OpPlayerU`) that validate the
click, latch `lastUseItem`/`lastUseSlot`, and set an `AP*U` interaction that
`Player.getApTrigger`/`getOpTrigger` resolve. No use-on *effect* is engine
anywhere in the reference.

### 1. There was no wire — this was packet handlers first, dispatch second

The client has emitted all four for as long as the minimenu has had an objsel
(`net_out_opheldu`/`oplocu`/`opnpcu`/`opobju`, opcodes 64/26/18/37 in
`src/net/rev/osrs230/packetout.h`), and the mock's inbound routing table had **no
row for any of them**. Every one was dropped as an unrouted packet, so every `*u`
script in the tree was unreachable regardless of what the dispatch did.

One asymmetry the decode has to respect, because it is the client's own: the
component field is **4 bytes in OPHELDU** (`net_out_opheldu` writes it through
`out_p_com`, which is `rev->component_id_bytes`) and **2 bytes in the other
three** (`net_out_oplocu` and friends write `p2`). A shared decode is wrong for
one of them, and the wrongness presents as a use-on that silently does nothing.
`useon_tail` takes the width as an argument for that reason and the embed test
pins it.

### 2. THE TRAP: "use A on B" has two ids and only one is the subject

The subject is **B, the thing clicked on** — the loc, the npc, the ground obj.
`Player.getOpTrigger`/`getApTrigger` read `type.id` and `type.category` off
`this.target`, which is the target entity; the used obj appears nowhere in the
lookup. It reaches content only as `last_useitem` / `last_useslot`.

Getting this backwards compiles, runs, and works for every item that only ever
has one target — which is most of them in a small content tree. It is the same
shape as the `POP_ARRAY_INT` transposition (docs/…/cs2-pop-array-int-transposed):
invisible on the symmetric case. So the selftest's two ids are deliberately
unrelated (`cooksquestrange` loc 114, `bucket_water` obj 1929) and it asserts
**both** halves: the same loc with a *different* item runs the same script, and
the same item on a *different* loc runs nothing.

The op form is the ap form **+7**, exactly — `ss_trigger.h` lays every family out
as `ap1..ap5, apU, apT, op1..op5, opU, opT`, so `APLOCU 64 → OPLOCU 71`,
`APNPCU 8 → OPNPCU 15`, `APOBJU 36 → OPOBJU 43`. That is stated once, in
`interaction_ap_trigger`.

A use-on is an interaction like any other: it latches, it walks, and it acts on
arrival. `Mock230Interaction.use_on` selects the trigger; it does not change what
the walk does, and `op` is meaningless while it is set (the handlers pass 1 as a
placeholder). `opheldu` alone is answered in the handler, because there is
nothing to walk to — the reference does the same.

### 3. `opheldu` is a four-rung chain that mutates the player as it searches

`OpHeldUHandler.ts:94-124`, in order, and **no `_` wildcard** — verified: zero
bare `_` bindings across the whole `*u` family in the reference tree, against 53
category bindings.

```
1. (OPHELDU, b.id, -1)          the item that was CLICKED
2. (OPHELDU, a.id, -1)          the item that was DRAGGED    → then SWAP
3. (OPHELDU, -1, b.category)
4. (OPHELDU, -1, a.category)                                 → then SWAP
```

**The swap is the contract.** It exchanges `last_item`↔`last_useitem` and
`last_slot`↔`last_useslot`, so a script bound to one of the two items always
finds *itself* in `last_item` and the other item in `last_useitem`, whichever
order the player clicked them in. Content depends on it:
`skill_crafting/scripts/jewellery/stringing.rs2` binds
`[opheldu,ball_of_wool]` and `[opheldu,unstrung_sapphire_amulet]` and reads
`last_useitem` in both, and only one of them can be the clicked item on any given
click.

This cannot be expressed as a call to `mock230_scripts_run_trigger`, which
resolves one `(type, category)` pair — hence `mock230_scripts_run_opheldu`. It
ends at the shared `run_trigger_script` like everything else, so the three
dispatch entry points cannot come to disagree about a trigger's execution
context.

**Rung 2's swap sits outside its null check, and that is ported deliberately.**
After a failed rung 2 the state is left swapped, so a rung-3 hit — the *clicked*
item's category — runs with `last_item` naming the **other** item. It is an
inversion relative to the two id rungs, it is the reference's behaviour, 53
category bindings observe it, and content is written against what it observes.
The selftest asserts the inversion in both directions so that "fixing" it goes
red.

### 4. The miss adds **no** row to `enum Mock230Fallback`

The reference's answer to an unbound `*u` is a message and nothing else
(`OpHeldUHandler`'s `messageGame('Nothing interesting happens.')`,
`Player.defaultOp` for the other four). That string already lives here as
`[proc,nothing_interesting_message]` behind `mock230_say`.

**A `*u` miss must not be routed into `MOCK230_FALLBACK_OPLOC`/`OPNPC`/`OPOBJ`.**
There is no engine use-on behaviour for it to fall back *to*, and doing it would
hand "use a bucket on the castle door" to the door handler — the door would open.
That is why the use-on arm in `mock230_world_process_interaction` returns before
the `switch( kind )` that holds three of the fallback call sites, rather
than growing a fourth case inside it: Phase 3 has to be able to delete those
lines out of arms this stage never rewrote.

**That prediction was collected on 2026-08-02.** `MOCK230_FALLBACK_OPLOC` is a
deleted symbol now — the loc arm of that `switch` was removed whole, with no edit
to the use-on arm above it — and the mutation table below still names it because
that is what was run. The rule outlives the symbol: a `*u` miss goes to
`mock230_say`, and it goes there for the same reason the loc arm's own miss now
does, which is `Player.defaultOp` and not a fallback.

### 5. `last_useitem` / `last_useslot`

New on `Mock230Player`, initialised to **-1** (0 is a real obj id and a real
backpack slot), and read by the two new opcodes `LAST_USEITEM` 2058 and
`LAST_USESLOT` 2059. Nothing clears them after a use-on: the reference does not
either (`clearInteraction` leaves them alone), so they mean "the last use-on",
not "the current one".

Noted rather than fixed: `last_item` and `last_slot` are still left at the
memset's 0 by `mock230_world_player_init`, where `Player.ts:371-374` declares all
four -1. That is a pre-existing divergence this stage found; changing it is a
behaviour change to `opheld`/`inv_button` and does not belong in a use-on stage.

### Verified

`make -C src PLATFORM_OBJ_BASE=<objdir> test-mock230`, stanza `use-on, the *u
family`. Ten mutations were run and each turned exactly the assertions it should
red:

| mutation | what went red |
|---|---|
| key the `*u` trigger on the used obj | every `[oplocu]`/`[opnpcu]`/`[opobju]` assertion |
| rung 2's swap moved inside its null check | both category-rung inversions |
| rung 2 dropped | the B→A direction, plus both category rungs |
| op form is `ap + 1` | every op-rung assertion |
| a `*u` miss falls through to `MOCK230_FALLBACK_OPLOC` | **"the door stays SHUT"** |
| `interaction_ap_trigger` ignores `use_on` | the loc op rung *and* the ap rung |
| the used-item tail decodes obj/slot backwards | the whole stanza |
| the swap moves items but not slots | only the two slot assertions |
| the interaction handlers do not latch the used item | "a different item on the same loc" |
| the interaction is built without `use_on` | five assertions across all three forms |

The load-bearing pair: the first (a transposition that compiles and runs) and the
fifth (a use-on that opens a door).

**The wire is verified against the client's own encoders**, in
`make -C src test-mock230-embed`: a real embedded client calls the same
`net_out_opheldu` / `net_out_oplocu` that `app.c` calls, through the real login
handshake, the real ISAAC stream and the real session framing, and the content
script runs. Asserting on a hand-built payload would prove the dispatch and
nothing about whether the two halves of the wire agree — mutating OPLOCU's
component width from 2 to 4 leaves the selftest green and turns the embed check
red, which is the whole reason it is there.

What is **not** covered end to end: the client's *mouse* path into those encoders
— minimenu → objsel → `net_out_*` in `app.c`. `TORIRS_SIM_SETTAB=3:149` does not
mount the inventory in a headless session (the rev-230 sidebar is server-driven,
§3.16), so the two-step "Use → target" click was not simulated. The client boots
and plays against this server with the four new routes in place; the code between
the click and the encoder is pre-existing and untouched by this stage.

### What is not here

- **`opplayeru` / `applayeru`, 5 of the 546.** rev-230 assigns no `OPPLAYERU`
  wire opcode — `src/net/rev/osrs230/packetout.h` has rows for OPHELDU, OPNPCU,
  OPLOCU and OPOBJU and none for the player form, so `net_out_opplayeru` cannot
  encode anything this revision carries. There is no packet to route. It also
  means the one place the reference overrides the trigger's subject with the
  *used* obj rather than the target (`setInteraction(..., APPLAYERU, useObj)`
  feeding `targetSubject.com`, which is why `[applayeru,rotten_tomato]` names the
  thrown item) has no call site here — and that asymmetry must not be generalised
  to the other four.
- **`apobju`** — the trigger constant exists; the reference tree binds it zero
  times.
- **Component validation.** The reference checks `com.usable` and
  `isComponentVisible`, and resolves the used inventory through the player's own
  listener list. This server has no listener model, so the check is "the backpack
  slot holds that obj", which is the load-bearing half and the same one
  `handle_opheld` already makes.
- **The `*t` spell-target family** (89 uses) — not part of step 5.

---

## 3.21 The zone family: two latches, not one

Triage §9 step 5c. `[zone]`, `[zoneexit]`, `[mapzone]`, `[mapzoneexit]` — 806
uses in the reference, and the number is the least useful thing about them.

### The split, re-measured

Corpus: 1,267 `.rs2` under `LostCity_Server/content`, pruning any path component
`_unpack`/`_test`, 9,606 headers.

| trigger | uses | subject shape | keyed off |
|---|---:|---|---|
| `zone` | 262 | five-part, `<level>_<mx>_<mz>_<lx>_<lz>` | the 8-tile zone, **level included** |
| `zoneexit` | 165 | five-part | same |
| `mapzone` | 306 | three-part, `0_<mx>_<mz>` | the 64-tile map square, **level forced to 0** |
| `mapzoneexit` | 73 | three-part | same |

**427 zone-keyed, 379 square-keyed.** Every one of the 427 has a five-part
subject (levels 0 ×392, 1 ×2, 3 ×33) and every one of the 379 has a three-part
subject beginning `0_`. All 806 subjects are distinct within their trigger.

So these are not one latch at two scales, and no single latch can express them:
climbing a ladder without moving re-enters a *zone* and does not re-enter a *map
square*. `NetworkPlayer.updateMap` holds `lastZone` and `lastMapZone` as two
fields for exactly that reason, and `Player.ts` initialises both to -1.

**The ZoneMap (§3.17) is irrelevant to all 806.** It is keyed `(zx, zz, level)`
and the 379 do not use that granularity; the 427 do, but they are addressed by
*name*, not by any index. Nothing in this section consults `mock230_zone.c`.

### What landed

**Engine, all of it.** `Player.ts` holds the latches, `World.processClientsOut`
calls `updateMap`, `World.processPlayers` drains the engine queue. What each zone
*does* is content, and this stage ported none of the reference's 806.

- **`mock230_world_update_map`** (mock230_world.c), called first in
  `phase_client_out` — "`// - map update`", which is where
  `World.processClientsOut` puts it. Two comparisons, four `snprintf`s' worth of
  dispatch, ~40 lines.
- **Five new fields on `Mock230Player`**: `last_zone_level/x/z` and
  `last_map_x/z`, all -1. Stored as components rather than a packed coord so no
  unpack is needed to name the zone being *left*.
- **`mock230_scripts_queue_trigger_at`** — the same name lookup as
  `run_trigger_at` (§3.19), enqueued instead of run.
- **`MOCK230_QUEUE_ENGINE` and `Mock230Player.engine_queue`**, drained by
  `mock230_scripts_process_engine_queue` after the timers, which is where
  `World.processPlayers` calls `processEngineQueue()`.
- **`ssc_compile.c` writes -1 for a coord subject.** See below.

`enum Mock230Fallback` did not grow: a zone with no script does nothing, and
the reference has no engine behaviour behind a missing `[zone]` either.

### `zone_index` is the trap, and it is invisible

`Mock230Player.zone_index` is the `>> 3` key including the level. It looks
exactly like `lastZone` and it is not: `mock230_zone_player_reset` sets it to -1
on every `REBUILD_NORMAL` and every climb, from four call sites. A latch hung off
it re-fires `[zone,…]` whenever the world's origin moves under a standing player
and swallows the `[zoneexit]` that should have paired with it — a bug that
presents as "this area's script sometimes runs twice" and never as an error. The
reference gets away with sharing the comparison only because it never resets
`lastZone` on a rebuild.

The selftest forces a `REBUILD_NORMAL` and asserts nothing fires. Replacing the
new fields with `zone_index` turns exactly that check red.

### Detection is phase 10; execution is phase 5 of the next tick

All four of the reference's `triggerZone` family end in
`enqueueScript(trigger, PlayerQueueType.ENGINE)`, which forces `delay = 0` and
appends to a list drained from `World.processPlayers` — *earlier* in the tick
than `processClientsOut`. **A zone script therefore runs on the tick after the
crossing.** That is not a detail to preserve for fidelity's sake; a direct call
from phase 10 gets two things wrong:

- a zone script that teleports would move a player whose PLAYER_INFO has already
  been encoded for the tile they are leaving;
- `run_or_park` allows one parked script per player, so a boundary crossed
  mid-dialogue would have its script **refused** with a message rather than held.
  `processEngineQueue` gates the run on `canAccess()` and decrements regardless,
  so the entry fires on the first tick access returns.

The engine queue is a **separate array**, not a fifth kind in `queue[]`, because
the reference keeps it as a separate list and the separation is load-bearing:
`unlinkQueuedScript`'s default branch walks `queue` and `weakQueue` and never
`engineQueue`, so `clearqueue` cannot cancel a zone trigger.

### Order, and the two things the order hides

`mapzoneexit(old)` → `mapzone(new)` → `zoneexit(old)` → `zone(new)`, matching
`updateMap` line for line. `-1` suppresses the exit on the first transition, so
**login fires enter with no exit**. A teleport across both boundaries fires all
four in one tick, with no special case.

The selftest's content uses an accumulator (`log * 10 + n`) rather than counters
precisely because order is the half a port gets wrong: 23 and 32 are different
numbers where "an exit happened and an enter happened" is the same fact.

### The compiler change that goes with it

`ssc_compile.c`'s `subject_is_coord` branch used to write
`SSVM_LookupKey(trigger, TYPE, subject_value)`. Measured rather than argued:

- **five-part subjects** pack into 28 bits (`ssc_lex.c`) and the compiled
  `lookup_key` is an i32 with the subject at bit 10, so the top 21 bits are lost.
  Of the reference's 427: **78 truncate to a negative key**, which
  `ssvm_provider.c` deliberately keeps out of `by_key`, and **349 land on a
  subject field that is not the coord**, 10 of them colliding — which is the
  `dup zone x10` line `test-ss-corpus` prints. The sign turns on bit 1 of `mx`.
- **three-part subjects** lex to their first component, which for all 379 is 0.
  One key, 379 scripts. That is the reference's own `parseInt("0_49_46")`
  collapse, faithfully reproduced.

Neither shape is addressable by key, so the key was a number nothing could look
up. It is -1 now, which makes "these four are name-addressed" true by
construction. `test-ss-corpus` is unaffected — it loads the reference's own
compiled bytes, so its duplicate count describes the reference's compiler.

`SSVM_ProviderGetByName` works on the raw header text (`ssc_compile.c` stores
`"[%s,%s]"` with the lexer's untouched source span), and that is a real silent
dependency: if the lexer ever normalised a coord token, or the compiler
zero-padded a component, every `[zone]` in the tree would stop running and
nothing would say so. `test-ssc`'s `coord subjects are name-addressed` pins both
halves — the name *and* the -1.

### The miss path is the hot path, and it was measured

There are tens of thousands of nameable zones and a handful of bound ones, so
almost every lookup misses. Measured, not asserted: 2M iterations at `-O2` of
exactly what `zone_trigger_script` does — `snprintf` the name, then
`SSVM_ProviderGetByName` — with the coordinate walking 1,024 zones east so the
branch predictor and the cache see a moving player rather than one repeated
string. Two real packs, this tree's and the reference's compiled corpus
(`LostCity_Server/engine/data/pack/server`), because the cost grows with the name
count and this tree's pack is the small case:

| pack | names | format only | format + miss | format + hit |
|---|---:|---:|---:|---:|
| this tree | 353 | 110 ns | **137 ns** | 112 ns |
| the reference's | 9,389 | 97 ns | **165 ns** | 128 ns |

Two things fall out, and both argue against building an index:

1. **The `snprintf` is the cost, not the lookup.** ~100 ns of it is formatting;
   the miss adds 27 ns on a 353-name pack and 68 ns on a 9,389-name one. A
   "bound zones" index would save at most the second number and would still need
   the string to key on.
2. **The latch bounds the call rate, not the lookup.** This runs once per zone
   *crossing* — at most one per four ticks at running pace — never once per tick.
   Four lookups per crossing is ~660 ns; 2,000 players each crossing every four
   ticks is ~330 µs of a 600 ms tick, 0.05 %.

A miss is also **silent**: `trigger_is_player_initiated` excludes this family and
`trigger_subject_kind` has no row for it, so it does not borrow §3.18's report.
Every tile in the world misses at least three of the four; a report here would be
the `[ai_spawn]`-across-2,197-npcs argument again.

### What this stage did not do

- **`SetMultiway`** — the other half of `updateMap`'s zone branch, driven by
  `World.gameMap.isMulti(zone)`. This tree has no multi-way map data, and a wire
  packet asserting something nobody computed is worse than no packet.
- **npc zone triggers.** Only `NetworkPlayer` has `updateMap`; npcs never fire
  them, in either tree.
- **Persisting the latches.** Per-session, matching a fresh `Player` per login.
- **Porting any of the 806.** Dispatch is engine; every zone's behaviour is
  content and is Phase 4.

### Verified

`make -C src test-mock230`, stanza `the zone family, and its two latches`, plus
`test-ssc`'s new `coord subjects are name-addressed`. Nine mutations, each red on
the assertions it should be:

| mutation | what went red |
|---|---|
| the zone latch is `player->zone_index` | "a REBUILD_NORMAL fires nothing", 121 instead of 1 |
| enter dispatched before exit | "moving zone fires [zoneexit] then [zone]", 32 instead of 23 |
| the map-square latch carries the real level | the climb check, plus both later square checks |
| `queue_trigger_at` → `run_trigger_at` (inline from phase 10) | "phase 10 detects and dispatches nothing", the `map_clock` check, and the busy-player check |
| the engine drain drops its `canAccess()` gate | "a busy player does not run a queued zone script" |
| the latches start at a memset's 0 | six checks, all reading 91 — the 9 is `[zoneexit,0_0_0_0_0]` firing at login |
| the zone latch uses `>> 6` | "moving zone fires [zoneexit] then [zone]", and the square-crossing check |
| an engine entry is queued with `delay = 1` | eight checks; nothing fires on the tick it should |
| `ssc_compile.c` writes the old coord key | `test-ssc`, three checks — `lookup_key` −1,875,754,333 instead of -1 |

The sentinel mutation is worth calling out: **-1 versus 0 in the latches is
otherwise untestable**, because no real content binds anything at the map's
origin. `selftest_zone.rs2` binds `[zoneexit,0_0_0_0_0]` and `[mapzoneexit,0_0_0]`
for no other purpose than to make that mistake print a digit.

**Headless client**, `manifest_osrs230_embed.ini`, `SDL_VIDEODRIVER=dummy`,
`MOCK230_VERBOSE=1`, `TORIRS_NET_CHEAT="tele 3238 3218"` — a real client, real
login, real ISAAC, real wire:

```
tick 1   MESSAGE_GAME payload=26    "Teleported to 3238,3218."     <- the crossing
tick 2   MESSAGE_GAME payload=43    "You step into the zone east of Lumbridge."
```

and the same run without the cheat produces neither. That is the whole path —
client packet → server teleport → phase 10 latch → engine queue → phase 5 of the
*next* tick → content `mes` → MESSAGE_GAME → client — and the one-tick gap is
visible in the transcript rather than asserted only in C.

### Still open

`[logout]` still clears neither queues nor timers nor the engine queue
(`Player.cleanup()` clears all three) — `phase_logouts` is empty. The engine
queue has a cap (`MOCK230_ENGINE_QUEUE_MAX`, 8) where the reference's list has
none; an overflow is reported, for the same reason `queue_hook`'s is. And
`updateMovement`'s "players cannot walk with a modal open *and* something
queued" rule reads `engineQueue` in the reference and is not ported here.

---

## 3.22 The dispatch surface after step 5, and the shape of a trigger's address

§3.19, §3.20 and §3.21 are one stage each. This is the view none of them gives:
what the engine can now reach a script through, what it still cannot, and — the
part that turned out to be the actual work — the fact that a trigger's *address*
is not one kind of thing.

The reason to write it down is that triage §9 sized step 5 off a use count, and
every use count in it moved when it was measured. The number that did not move is
the one nobody was counting: how many trigger families have a dispatch path at
all.

### 1. What dispatches now, against what dispatched before

Counted in `mock230_world.c` below `mock230_world_selftest`, so selftest call
sites are excluded. A *family* here is one trigger stem as content writes it —
`opnpc1`..`opnpc5` is one family reached from one site, not five.

| | before step 5 (HEAD) | after |
|---|---:|---:|
| production dispatch sites | 18 | **24** |
| trigger families reachable | 17 | **28** |
| script-id-addressed drains (`queue`, `timer`, `softtimer`, engine queue) | 3 | **4** |
| rows in `enum Mock230Fallback` | 7 | **7** (4 today — `ai_queue3`, `opobj` and `opheld` all moved to content, §3.18) |
| rows in `enum Mock230Fallback` | 7 | **7** (5 today — `ai_queue3` and `oploc` both moved, §3.18) |

Triage §2 keeps the canonical list, and it has never been complete. Even after 5a
and 5b amended it, it omits `debugproc`, `ai_opplayer<n>` and `ai_applayer<n>` —
and it names `ai_opplayer*` among the families the engine does **not** dispatch,
where `mock230_world.c:1642` has dispatched it for as long as npc modes have
existed (`ai_opplayer2` alone is 84 uses in the reference). It also counts
`command` (511) as a trigger family: `[command,...]` is the *declaration* syntax
of `engine.rs2` and all 511 are in that one file, so it is the engine's opcode
surface and not a trigger anything could dispatch. A specified correction is in
the lane report; this file does not edit that one.

The six new sites and the eleven new families do not correspond one to one, and
that is worth seeing:

| new site | families |
|---|---|
| `mock230_world.c` op-`*u` arm, above the `switch( kind )` | `opnpcu`, `oplocu`, `opobju` |
| `mock230_scripts_run_opheldu` from the OPHELDU handler | `opheldu` |
| four `mock230_scripts_queue_trigger_at` calls in `mock230_world_update_map` | `mapzone`, `mapzoneexit`, `zone`, `zoneexit` |
| *(no new site)* — the at-range attempt, through `interaction_ap_trigger`'s `use_on` arm | `apnpcu`, `aplocu`, `apobju` |

The last row is the one to notice. The three ap-side use-on forms cost zero new
dispatch sites: an interaction that is walking already asks `interaction_ap_trigger`
what to try at range, and teaching that one function about `use_on` was the whole
change. Three families for one `?:`. It is the clearest evidence available that
the interaction machinery (docs/…/interaction-model-walk-before-act) was the right
shape — a use-on is not a new kind of thing, it is an interaction with a second
id attached.

And the four script-id-addressed drains, which are dispatch by another name and
never touch the trigger index because their scripts are compiled name-addressed:
`queue`, `timer`, `softtimer` (§3.19) and now the engine queue (§3.21).

### 2. A trigger's address is one of five things, and all five end in one place

This is the structural finding of the lane, and it is not visible from any single
stage.

```
                                            resolves through          entry point
  keyed, chained  type → category → `_`      by_key, 3 rungs          mock230_scripts_run_trigger
  keyed, one rung type | category | global   by_key, 1 rung           mock230_scripts_run_trigger_specific
  keyed then named                           by_key, then by_name     mock230_scripts_run_if_button
  keyed, four rungs, mutating                by_key, 4 rungs          mock230_scripts_run_opheldu
  named only                                 by_name                  mock230_scripts_{run,queue}_trigger_at
                                                                              │
                                                                              ▼
                                                                      run_trigger_script
```

Every one of them ends at `run_trigger_script`, which is the only place that
decides what a trigger's execution context is: zero arguments, active player =
`srv->active_player`, `SSVM_PTR_PROTECTED_PLAYER`, and the active npc set **by
slot** through `host_tag` rather than by pointer, so a script that parks and
resumes finds the same npc or none. That function was not modified by any of the
three stages. It is the one thing in the seam that must not fork, because the two
forms did once disagree about the npc and `[if_button,…]` had no npc context at
all.

**Why the keyed path could not express the zone family** is the interesting half.
It is not that a coordinate is too large to be a subject. It is that a coordinate
*fits*, compiles without complaint, and lands on the wrong script.

A compiled `lookup_key` is an `int32_t`: trigger in bits 0-7, kind in bits 8-9,
subject from bit 10. The arithmetic is exactly saturating —

```
  (1 << 21) - 1  = 2,097,151         the largest representable subject
  2,097,151 << 10 = 2,147,482,624
  INT32_MAX - 2,147,482,624 = 1,023  = (3 << 8) | 255, precisely the room kind|trigger needs
```

— so one subject past 2^21 turns the field negative, and a negative `lookup_key`
is the reader's sentinel for "name-addressed". `ssc_compile.c` guards ordinary
symbol subjects against that bound. Its **coord** branch was a separate path that
never reached the guard, and `ssc_lex.c` packs a five-part coord into 28 bits.

Measured over the reference's 427 five-part `[zone]`/`[zoneexit]` headers, with
this tree's own lexer arithmetic:

| | count |
|---|---:|
| truncate to a **negative** key — silently dropped from `by_key` | **78** |
| land on a **non-negative** key whose subject is not the coord | **349** |
| …of which are *extra* entries on an already-taken key | **10**, all in `zone` (20 headers in a collision group) |
| `zoneexit` collisions | **0** |

§3.21 records "10 of them colliding", which is the duplicate-key count and is what
`test-ss-corpus` prints as `dup zone x10`; the number of *headers* involved is 20,
and the split by trigger is 57/205 negative/positive for `zone` and 21/144 for
`zoneexit`. The sign turns on bit 1 of `mx`, so which failure a zone gets is a
property of where it is on the map.

The three-part `[mapzone]`/`[mapzoneexit]` subjects fail the other way and more
completely: they lex to their first component, which for all 379 is `0`, so 306
`mapzone` headers share key **673** and 73 `mapzoneexit` headers share key
**674**. Two keys, 379 scripts. That one is the reference's own
`parseInt("0_49_46")` collapse reproduced faithfully — and it is why `getByName`
exists in the reference at all, with `// todo: getByTrigger needs more bits to
lookup by coord` sitting above the first caller.

So `mock230_scripts_run_trigger_at` takes **no keyed rung**, and that is a
correctness requirement rather than an optimisation. `run_if_button` takes one —
there the keyed lookup is a correct fast path and only the components above
interface 31 need the name. The zone family has no correct fast path to take.
`ssc_compile.c` now writes -1 for any coord subject, which makes "these four are
name-addressed" true by construction instead of true by whichever half of the
coordinate space a zone happens to sit in.

The composition rule that falls out, for whoever adds the next family: **ask by
key when the subject is a config id, ask by name when the subject is a place or a
component uid, and never ask by both unless the keyed rung is exactly right.** A
keyed rung that is *usually* right is worse than none, because its failure is a
script that runs.

There is a sixth address that is not a dispatch at all and bit this lane anyway:
the **bare name in an operand**. `queue(my_handler, 3, 0)` names
`[queue,my_handler]` and `gosub(helper)` names `[proc,helper]`, and rather than
thread the parameter's declared type down to the resolver,
`ssc_compile.c:script_id_for_bare_name` tries the name-addressed triggers **in a
fixed order** — `proc`, `label`, `queue`, `softtimer`, `timer`, `walktrigger` —
on the stated grounds that "the namespaces do not overlap in practice". They do.
A `[proc,X]` and a `[queue,X]` sharing a bare name compiles silently to the
**proc**, so `queue(X, 0, 0)` inside `[proc,X]` is an immediate recursive call
rather than a queued one. 5a wrote exactly that by accident and it presented as a
hang, not as an error. The resolution is not trigger-scoped and nothing warns;
until it is, a queue and a proc must not share a name.

### 3. The map-square half landed, and it is not a scale of the zone half

Naming it explicitly, because a reader who arrives here through §3.17 will have
just been told the ZoneMap exists and will size this work off it.

| | uses | subject | latch | level |
|---|---:|---|---|---|
| `zone` + `zoneexit` | **427** | five-part, every one | `x >> 3`, `z >> 3` | carried: 392 at level 0, 2 at 1, 33 at 3 |
| `mapzone` + `mapzoneexit` | **379** | three-part, every one begins `0_` | `x >> 6`, `z >> 6` | **literal 0**, always |

Both landed, as two independent latches on `Mock230Player`. The 379 are not a
coarser version of the 427: a player who climbs a ladder without moving re-enters
a *zone* and does not re-enter a *map square*, because the map-square latch packs
level as a literal 0. One latch cannot produce both behaviours, and a port that
tried would look right everywhere except stairs.

The ZoneMap (§3.17) is consulted by neither. It is keyed `(zx, zz, level)`, which
is the wrong granularity for 379 of the 806 and the wrong *kind* of address for
the other 427.

### 4. Every count in step 5, measured this run

Corpus: `LostCity_Server/content`, pruning any path component `_unpack` or
`_test` — **1,267 `.rs2` files, 9,606 trigger headers**. Headers matched by
prefix (`^\[name,subject\]`) and not anchored to end-of-line, because a header
with its body on the same line is common and anchoring loses 191 `oploc1` and 726
`proc` alone.

| | triage §9 | measured | why they differ |
|---|---:|---:|---|
| **5a** queue/timer/ai_timer | 273 | **275** | `queue` is 153 not 152, and `softtimer` (1) is unnamed by the triage though it is the same machinery |
| **5b** the `*u` family | 535 | **546** | 535 is `opheldu`+`oplocu`+`opnpcu` only; it drops `opobju` 3, `opplayeru` 3, `aplocu` 2, `applayeru` 2, `apnpcu` 1 — five more dispatch shapes, two of them ap-side |
| **5c** the zone family | 806 | **806** ✔ | holds exactly; 262/165/306/73 |

Per-trigger, for the record: `queue` 153, `timer` 34, `softtimer` 1, `ai_timer`
87; `opheldu` 230, `oplocu` 212, `opnpcu` 93, `opobju` 3, `opplayeru` 3, `aplocu`
2, `applayeru` 2, `apnpcu` 1, `apobju` 0; `zone` 262, `zoneexit` 165, `mapzone`
306, `mapzoneexit` 73. All 806 zone subjects are distinct within their trigger,
and 14 files hold all 806 — `duel_arena.rs2` alone is 144 `zone` + 144
`zoneexit`, and `music/scripts/move.rs2` is 303 `mapzone` + 53 `mapzoneexit`.

Bound in this tree, before and after the three stages:

| | before | after |
|---|---:|---:|
| `.rs2` files / headers | 47 / 319 | **50 / 353** |
| `queue`, `timer`, `softtimer`, `ai_timer` | 4, 1, 0, 2 | **8, 2, 1, 2** |
| the eight `*u` forms | 0 | **6** |
| the four zone triggers | 0 | **9** |
| implemented opcodes | 237 of 399 | **246** of 399 |

Nine opcodes, and the VM **aborts** on an unimplemented one rather than no-op'ing
(docs/…/cs2-unimplemented-opcode-assert), so each was a hard stop for any content
that used it: `CLEARQUEUE` (39 reference call sites), `LONGQUEUE` (15), `GETQUEUE`
(14), `GETTIMER` (8), `CLEARSOFTTIMER` (2), `STRONGQUEUE` (1), `WEAKQUEUE` (0),
`LAST_USEITEM`, `LAST_USESLOT`.

### 5. What still has no path to a script

Measured the same way, so nobody reads step 5 as having closed the trigger
surface. It closed eleven families of it.

| family | reference uses | why not |
|---|---:|---|
| the `*t` spell-target family | **89** (`apnpct` 31, `applayert` 31, `opheldt` 8, `opnpct` 7, `opplayert` 6, `oploct` 4, `apobjt` 1, `opobjt` 1) | not part of step 5; needs the spell-selection wire first |
| `advancestat` / `changestat` | **19** / **1** | no dispatch from the stat writer |
| `walktrigger` | **7** | `ai_walktrigger` binds 0 times |
| `inv_buttond` | **4** | the packet is decoded and goes straight to the bank |
| `opplayer<n>` / `applayer<n>` | **4** | player-target ops |
| `opplayeru` / `applayeru` | **5** | rev 230 assigns no `OPPLAYERU` wire opcode — there is no packet to route, so this is a revision limit and not an omission |
| `logout` | **1** | `phase_logouts` is empty |
| `tutorial` | **1** | |
| `ai_despawn`, `apobju`, `aploct` | **0** each | the constants exist; the reference binds them zero times |

Two of those rows are load-bearing rather than trivia. `[logout]` is the one that
already has consequences: `Player.cleanup()` clears the queue, the weak queue, the
engine queue and the timers, and nothing here clears any of them — which is
harmless only for as long as `mock230_save.c` has no callers and a returning
player is a fresh `Mock230Player`. And `advancestat`'s 19 uses are all in
`levelup/scripts/levelup.rs2` — one file, and it is the level-up handler, so what
an undispatched `advancestat` looks like from outside is an XP bug.

### 6. What deliberately did not change

- **`enum Mock230Fallback` did not grow.** Eleven families gained a
  dispatch path and none of them gained an engine fallback, because in the
  reference none of them *has* one: a zone with no script does nothing, a queue
  with no script cannot exist, and an unbound `*u` is `messageGame('Nothing
  interesting happens.')`, which lives here as
  `[proc,nothing_interesting_message]`. §3.20 records the one place this nearly
  went the other way — routing a `*u` miss into `MOCK230_FALLBACK_OPLOC` hands
  "use a bucket on the castle door" to the door handler, and the door opens. The
  table may shrink; it did not grow.
- **`run_trigger_script` is byte-identical to HEAD** across all three stages —
  checked, not assumed — for the reason in §2 above.
- **`mock230_scripts_run_trigger`'s signature is unchanged.** Every existing call
  site still passes `(trigger, type, category, npc_slot)` and still reads the
  tri-state of §3.18. The three new addressing modes are new functions beside it,
  not parameters on it — a `run_trigger` that took a coordinate *or* a type would
  be a function whose subject means two things depending on an argument it also
  takes, which is the shape of the bug this whole section is about. **A fourth
  followed the same rule on 2026-08-02**: `mock230_scripts_run_trigger_on_loc`,
  which binds the scene slot as the active loc, is a sibling and not a sixth
  parameter. Only the two call sites that are actually about a loc changed; every
  other one still reads exactly as it did.

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
movement                          walk 1 tile/tick, run 2; 25 dest-first waypoints + greedy takeStep (collision re-validated); approach-aware route_op for loc/npc/obj; collision_map_reached for arrival; cannot_reach_message on dead ends
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

Server pathing parity with the client (shared `collision_map_route_tiles`,
approach construction, reach predicate) is recorded in
[`PATHING_INTERACTION_PARITY.md`](PATHING_INTERACTION_PARITY.md) §7.

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

The transport seam (§3.13b), the interaction model (§3.13c) and the dispatch
tables (§3.13d) are done. The operating guide that wraps this list together
with the content-port plan into one phased sequence is
[`PORTING_GUIDE.md`](PORTING_GUIDE.md). What is left, in the order each
unblocks the next:

1. ~~**Finish multiplayer.**~~ — **done.** Two clients in one world see each
   other move, and `net/mock/test/embed_test.c` is where that is asserted: it
   drives two `ToriRS_Network` instances through two real handshakes into one
   embedded world, then decodes each one's PLAYER_INFO with the *client's own*
   reader (`pkt_player_info_reader_read`) and checks that alice's steps arrive
   in bob's stream, attributed to alice's pid, with alice's appearance and
   alice's name on them. Asserting on server state instead would have proved
   nothing: every way this could be wrong is a bitstream that only a reader
   sees.

   What the change actually was — and what it was *not* — is worth stating,
   because "raise `MOCK230_PLAYER_MAX`" is the tempting summary and is wrong.
   The pool already existed at 1. What was single-player was the set of things
   the *world* held that are really facts about a *client*:

   | was on the world | is on the player | because |
   |---|---|---|
   | `tracked` / `tracked_count` | same, per player | NPC_INFO's deltas are relative to whoever is being written to |
   | `Mock230Npc.tracked` | `npc_tracked[slot]` | "the client knows about this npc" is two answers with two clients |
   | `Mock230GroundObj.sent` | `ground_sent[slot]` | likewise |
   | `rebuild_pending` | same, per player | one client walking off the scene is not the other's rebuild… |
   | `login_pending` | same, per player | …and a second login must not re-run the first player's `[login]` |
   | `clear_map_flag` | same, per player | one walk ending is not the other's map flag |
   | — | `tracked_players` / `player_tracked` | new: who this client can see |

   Three seams carry the rest:

   - **`srv->active_player` is "whose turn it is"**, not "the player". It was
     called `player`, and every read of it looked correct while there was one.
     The per-player phases set it as they iterate
     (`mock230_world_set_active`), the session sets it before dispatching a
     packet (`mock230_world_handle` takes a `Mock230Player*` now — the encoder
     pass's inbound half), and the tick borrows and returns it. Every subsystem
     that still reaches through the world — the scripts, the bank, combat, the
     world map — is asking "who am I doing this for", which is the right
     question; each is moved to an explicit parameter as it is touched.
   - **A world build is not a login.** `mock230_world_init` is the scene, the
     npc roster and the map squares' objs, and is idempotent: a second login
     that re-ran it would respawn the roster on top of itself and return every
     taken spawn. `mock230_world_player_init` is the character.
     `mock230_scripts_load` is idempotent for a harder reason — reloading frees
     the env out from under the first player's parked script.
   - **World events broadcast.** `mock230_world_broadcast_loc` was what made a
     door one player opens open for the other; ground objs withdrew from
     everyone who had been told about them. It was an approximation of the zone
     the reference addresses, and step 3 below has replaced it — both functions
     are gone, along with `Mock230Player.ground_sent[]`. The half a broadcast
     could not do is *replay* to whoever arrives later, which is §3.17.

   PLAYER_INFO itself is worth reading (`mock230_encode.c`). Three traps, all
   of which corrupt the stream rather than dropping a field:

   - Extended blocks go in **the order the bit section queued them**, which is
     why the encoder keeps a `queued[]` rather than walking the tracked list
     twice. Reversing that order does not lose a block, it applies one player's
     appearance to another — caught here only because the appearance now
     carries the player's name.
   - A player **removed** in the tracked section must not be counted into the
     order the extended blocks index against, which is why the kept list is
     rebuilt as it is written.
   - The tracked section has **no placement op** — its four are nothing, one
     step, two steps, and remove — so a teleport is spelled as
     remove-and-re-add in the same packet. `place_dirty` therefore cannot be
     cleared inside the encoder (whoever is encoded first would consume it);
     phase 11 clears it, beside `masks`, for the same reason `masks` is there.

   Two changes fell out that are not bookkeeping. `Mock230Npc.combat_target` is
   a **pid** now rather than the flag "is fighting the player" — the same
   number and a different meaning — and an aggressive npc picks the nearest
   eligible player rather than reading `active_player`, which in phase 4 is
   nobody's turn at all. And the appearance blob carries the player's **name**;
   it was a literal 0 labelled "name37: empty", which with one player was
   invisible and with two makes everybody an anonymous body.

   What is still single-player, deliberately and separately (the scene-origin
   entry is the one step 3 did *not* close, and is now the sharpest of them):

   - **The socket server accepts one connection at a time.** `serve()` runs one
     session to completion; a real one wants the non-blocking accept with
     per-connection output buffering noted at the end of this section. The
     embedded host (`mock230_embed_connect`) holds several, which is what the
     test drives.
   - **One scene origin for the whole world.** `mock230_scene_build` is a
     singleton, so `maybe_rebuild` moves the origin for everybody and marks
     every player as owing a REBUILD_NORMAL. Two players more than ~70 tiles
     apart pull it back and forth. Step 3 is the fix.
   - ~~**NPC_INFO's extended masks are shared across observers.**~~ — **done**,
     and the premise above was wrong; §3.11j has the whole of it. The masks are
     *still* shared, because "which fields are dirty" is a fact about the npc
     and not about the reader — the reference computes them once per npc per
     tick too. What was observer-dependent was one **value**: a `face_entity`
     that spelled "the player" as the 2047 self-alias. It carries the target's
     absolute pid now, and `UPDATE_PID` carries each client's real pool slot
     instead of 2047, which is what makes an absolute pid resolvable at all.
     Cost: **0 bytes** — `struct Mock230Npc` is unchanged at 328 B. Asserted in
     `embed_test.c` from two clients' decoded NPC_INFO streams.
   - **`srv->iterator` is one cursor**, as it is in the reference, and for the
     same reason there is one script-parking slot per player (§3.10).
2. ~~**Dispatch tables, twice.**~~ — **done**, see §3.13d. Inbound packets are a
   45-entry table; the opcode gap report is generated from the `case` labels and
   runs at load. What is *not* done is splitting the 1,550-line host `switch`
   into per-domain files — the introspection that motivated it came from the
   generator instead, so the split is now a readability change rather than a
   blocking one.
3. ~~**Zones with buffered events.**~~ — **done**, see §3.17.
   `mock230_zone.{c,h}`: a `ZoneMap` keyed `(zx, zz, level)` with per-zone loc,
   obj and npc lists and a per-tick event buffer, and the loc/obj packets moved
   onto it. A door one client opens is open for a client that connects
   afterwards, asserted in `embed_test.c` against the client's own decoder.

   Three things it turned out to be, none of which is "add a hash map":
   **the ZoneMap owns loc mutations** (the scene is re-read from the cache on
   every rebuild, so the server was forgetting its own doors — two comments in
   two files described a mechanism that could not work); **a newly-loaded zone
   gets state and *not* the tick's events**, because the state already includes
   them and sending both put every ground obj on the floor twice; and **the npc
   cap and the wire's tracked count are two numbers** — 2048 and 255 — now that
   NPC_INFO asks the zones who is nearby instead of scanning the world per
   client.

   Still open and now the visible limit: **one scene origin for the whole
   world** (step 1's remainder). A loc revert aimed at a tile the moved scene no
   longer covers cannot apply, and says so under `MOCK230_VERBOSE`. Zone
   *triggers* remain undispatched — Phase 2 work, and they need name-keyed
   dispatch (`[zone,<level>_<mx>_<mz>_<lx>_<lz>]`), not a numeric subject.
4. **Fill in the host opcodes.** **246 of 399** are implemented (63 VM core,
   160 host, 9 db, 5 loc, 7 npc, 2 param — the count is generated into
   `src/net/mock/mock230_opcode_coverage.gen.h`, so read it there rather than
   trusting a number typed into prose. This line has now been wrong twice: it
   said "~100 of 396" long after that stopped being true, then "215 of 398"
   until 2026-08-01). Driven by the gap
   report from step 2, plus `.dbtable`/`.dbrow` support in the content reader,
   which landed: prayers were flattened into a bespoke `.prayer` grammar to
   avoid writing one and are an ordinary dbtable now (`db_find` was the last
   piece), and drop tables and shops want the same.
5. **Move the C content into content.** Re-measured 2026-08-02: the bank is
   **1,418** (not 1,370 and no longer 1,395), combat **1,061** (not 858),
   `mock230_equipment.c` **102** (it was 134 until the level gate moved), the
   world map **199** — plus doors and the login burst (prayer was one of these
   and is done — see mock230_player_systems.md §4.1). But the ~3,200-line total
   was measuring the wrong thing: the C that actually stands between content and
   these behaviours is the `enum Mock230Fallback` rows, now **four** and
   **293 lines of dispatch** (`interaction_engine_npc` 35,
   `interaction_engine_loc` 85, `mock230_bank_quantity_for_op` 108,
   `mock230_bank_handle_button` 65). It was ~370 across six rows before
   2026-08-02; `interaction_engine_obj` and the OPHELD verb ladder are gone with
   their rows (§3.18). The rest is what those reach, and most of it — combat,
   the bank's arithmetic — is engine in the reference too. The reason is real
   and documented in `bank.rs2`'s own header — the rev-230 bank builds its op
   ladder conditionally on varbits, so the index alone does not say what was
   clicked — which is exactly why step 4 comes first. Widen the opcode surface
   until a script *can* say it, then move it. §3.18 has the per-row blockers,
   all of which were re-checked in the same pass and four of which were wrong.

   One of those four line counts is itself a warning about this list. The
   `opnpc` row's own `blocked_on` string cites `interaction_engine_npc` at
   `mock230_world.c:2333-2364`; the function is at **2326-2360** today. Nothing
   edited it — the deletions above moved it. A `file:line` in a blocker is
   checkable in one command, which is why it is the right form, but it is also
   the only part of a blocker that goes stale by someone else's commit.
5. **Move the C content into content.** Re-measured 2026-08-01: the bank is
   **1,395** (not 1,370), combat **1,061** (not 858), `mock230_equipment.c`
   **134**, the world map **199** — plus doors and the login burst (prayer was
   one of these and is done — see mock230_player_systems.md §4.1). But the
   ~3,200-line total was measuring the wrong thing: the C that actually stands
   between content and these behaviours is the `enum Mock230Fallback` rows —
   ≈370 lines of dispatch when this was measured (`interaction_engine_npc` 31,
   `interaction_engine_obj` 37, `interaction_engine_loc` 84,
   `mock230_bank_quantity_for_op` 107, `mock230_bank_handle_button` 64, the
   OPHELD arm ~50), and **≈252 today**: `interaction_engine_loc` and `climb`
   went with the `oploc` row on 2026-08-02. The rest is what those reach, and most of it — combat, the
   bank's arithmetic — is engine in the reference too. The reason is real and
   documented in `bank.rs2`'s own header — the rev-230 bank builds its op ladder
   conditionally on varbits, so the index alone does not say what was clicked —
   which is exactly why step 4 comes first. Widen the opcode surface until a
   script *can* say it, then move it. §3.18 has the per-row blockers, all of
   which were re-checked in the same pass and four of which were wrong.
6. ~~**Invert the fallback.**~~ — **done**, see §3.18. The lookup is the
   reference's (type → category → the bare `_`, and nothing after that), the
   `_` wildcard is the only fallback the design has, and a trigger with no
   script does nothing — loudly under `MOCK230_VERBOSE`, in the reference's own
   words.

   What survives of the C is four enumerated rows (`enum Mock230Fallback`) —
   seven when the change landed, and `ai_queue3`, `opobj` and `opheld` have gone
   since — each naming what it is blocked on, counted at boot and pinned by the
   What survives of the C is five enumerated rows (`enum Mock230Fallback`),
   each naming what it is blocked on, counted at boot and pinned by the
   selftest: it may shrink, it must not grow. Nothing was moved by the change —
   step 5's order is the point. Three things that used to be indistinguishable
   now are not: a script that *aborted* versus one that was never bound
   (`enum Mock230TriggerResult`); an engine fallback versus "what happens
   otherwise"; and — the one worth the change — a server with **no pack at
   all**, which used to turn every fallback on at once and run a second,
   silently different implementation of the whole game by default in a fresh
   checkout. Two findings fell out of reading `Player.closeModal`: `[if_close]`
   is a notification and not a handler, and its dispatch had been asking with a
   packed com uid against a compiler that keys it on the interface id, so no
   `[if_close]` in this tree had ever run.
7. **Rename.** `mock230_*` is a double misnomer: it is not a mock, and it reads
   a 239 cache while speaking the 230 wire. Mechanical, and cheapest while
   there is still one consumer.

An item that slots between steps 4 and 5: **read the server band at boot.**
`cachepack pack` writes `<content>/server/pack` — the split
`CONTENT_PACK_PLAN.md` §5.4 landed — but `mock230_content_load` still
re-parses the `server/scripts` text overlays at every boot, which is exactly
the condition `mock230_servercodec.h` says the band exists to remove; the
decode half has no caller outside its own test. Wiring it into
`mock230_boot.c`, and then deleting `bake_npc_params` / `bake_loc_params`
from `mock230_pack.c` (retired on paper by §5.4, still present in the file),
is Phase 0 of `PORTING_GUIDE.md` — it should precede the bulk content port so
there is one load path to debug rather than two.

Two smaller things that belong with step 1: the accepted socket is *blocking*,
so the `select()` in `mock230_main.c` is load-bearing (see §3.13b) and a real
server wants non-blocking accept with per-connection output buffering; and
`MOCK230_VARP_COUNT` is a flat `int32_t[5000]` per player — 20 KB each — which
wants to be sparse or sized from the cache.

The player stream's 11-bit pid field is the same class of thing as §3.2 and has
not been parameterised — it is 11 bits in every revision in the tree, so there
is nothing yet to vary. If a revision widens it, it wants a `player_pid_bits`
beside the two npc widths rather than an edit.
