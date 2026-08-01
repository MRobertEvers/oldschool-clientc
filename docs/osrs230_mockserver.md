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

The **wield requirement** is the engine's to enforce and content's to describe.
`mock230_equipment_may_wear` reads the obj's skill/level pairs and compares base
levels; what the player is told is `[proc,equip_level_message]`, which since
2026-08-01 takes the `stat` id rather than its name. The 23 skill names it used
to be handed were a C table in `mock230_equipment.c` — `general/configs/stat.enum`
and `[proc,stat_name]` hold the words now, so renaming or translating a skill is
a content edit. The selftest asserts the sentence off the wire ("the wield
refusal is content's, words and all"), which is also what catches the RuneScript
trap here: `<...>` interpolates a *variable*, so a `<~proc(...)>` in a string
reaches the player as those literal characters unless the compiler is taught the
`~` sigil — which it now is (`ssc_compile.c`).

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

**A trigger is how the engine should reach content, and a script name in C is
not.** The ten call sites that used to spell one — `"[queue,player_death]"`,
`"[proc,npc_meleeattack]"` and eight more — go through `srv->hooks` now, a table
`mock230_scripts_resolve_hooks` fills from the pack when it loads. An unresolved
hook is reported at boot instead of doing nothing forever, which is what an
unknown name used to do in every `run_proc` helper: renaming a script deleted a
feature without failing a build, a test or a log line outside `--verbose`. The
by-name helpers remain for tests. `docs/CONTENT_ARCHITECTURE.md` §8.6 has the
rule and what is left (`mock230_say`'s message procs).

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
(`world_cycle.c`, `WORLD_FACING_*`). The local player's index is 2047, the same
number that terminates the player stream — which is why `UPDATE_PID` carries it
— so "face the player" on the wire is **34815**. The mock wrote the bare 2047,
which the client resolved as npc slot 2047. That slot never exists, the lookup
returned NULL, the branch fell through, and every npc in a fight kept whatever
yaw it had been walking with. `MOCK230_FACE_LOCAL_PLAYER` is the constant now,
and the selftest asserts the exact number rather than "not -1", because the
wrong value is a *plausible* one.

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
- **Routing is `mock230_world_npc_walk_to`** — the same flood the player's click
  uses — so a leg that has to go round the castle wall does. `maxrange=50` is
  the reference's leash and is what lets a route this wide exist at all; the
  default 7 would drop it the moment he left his spawn tile's neighbourhood.

`moverestrict=outdoors` is deliberately not ported: this engine implements
`nomove` of that family and nothing else, so declaring it would be a claim the
collision map does not enforce. The route stays outside on its own.

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
| `map_findsquare(coord, min, max, mode)` | rejection sampling over the box, because enumerating every legal tile is 1,681 collision reads at radius 20 and this runs per npc per timer. Only `^map_findsquare_none` is honoured — the line-of-walk/sight modes need a reachability test this server has no cheap form of, and an unsupported mode says so under `MOCK230_VERBOSE` rather than silently teleporting a monster through a wall. Failure returns the **source** coord, not -1: callers assign it straight into `npc_tele`. |
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
```

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

**What `mock230_locinfo.c` now retains**, measured on `cache.osrs239`, all three
built in the one decode pass that was already happening for params:

| table | rows | bytes | note |
|---|---:|---:|---|
| params | 1,709 | 41,016 | of 62,194 records, only 1,070 carry any |
| names | 30,033 | 565,681 | one concatenated blob + a sorted (id, offset) index |
| footprints | 17,309 | 138,472 | only the records that are not 1x1 |
| known-id bitmap | — | ~7.8 KB | one bit per id; the reference's `LocTypeValid` |

Names are ~560 KB, not the ~700 KB the previous revision of that file estimated
and declined to pay: the estimate was `strdup` per record — 30,033 allocations
with headers, plus a pointer array over all 62,194 ids. A blob and an index over
only the named records is two allocations. Footprints store one byte each
(measured maxima 17 and 33; both are `g1` on the wire) and an id that is not in
the table answers 1x1, which is the decoder's own default rather than a guess.

**Two loc opcodes are deliberately NOT landed, and neither is blocked on
effort:**

- **`loc_anim` (3002) — 56 uses / 23 files, the largest single loc gap. Blocked
  on a missing wire packet.** The reference is `LocOps.ts:50` →
  `World.animLoc(...)`, i.e. a **zone event**. `zone_sub_opcode` in
  `mock230_encode.c` enumerates every zone sub-packet this server has —
  `LOC_ADD_CHANGE`, `LOC_DEL`, `OBJ_ADD`, `OBJ_DEL`, `OBJ_COUNT` — and there is
  no loc-anim among them. Landing it means a new `MOCK230_ZONE_EV_*` kind, a new
  encoder arm and headless-client verification that the loc actually animates.
  That is engine work outside the ops-file seam; file it as its own item.
- **`loc_category` (3003, 38/16) and `lc_category` (4100, 3/1) — the linked
  decoder throws the field away.** `3rd/rscache/src/datatypes/dat2_config_loc.c`
  case 61 is `g2(buffer); // Skip unsigned short`, where
  `dat2_config_npc.c:668` decodes `category` explicitly. The bytes are in the
  cache; nothing in this tree can see them. Landing it needs (i) confirming
  opcode 61 really is `category` against the OSRS `LocType` reference — a strong
  candidate given the npc/obj precedent, but one unsigned short and a guess here
  is exactly the class of error this project keeps paying for — and (ii) an edit
  to the vendored `3rd/rscache/` tree, which is `EXCEPTIONS.md`-governed and
  validated by byte-exact round-trip. Both are out of scope for an ops file.

Two more were measured and cut on data, not on scope:

- **`lc_desc` (4102) — 0 of the 62,194 loc records carry a `desc`.** The field is
  gone from OSRS loc configs; a handler could only ever push the reference's
  `'null'` fallback. There are also zero callers in the reference tree.
- **`lc_debugname` (4101) — `debugname` is a LostCity build-time symbol.** The
  dat2 loc record has no such field and the decoder has no place to put one.

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

`checkvis` is accepted and ignored, exactly as `npc_find` and `npc_findall`
already accept and ignore it: there is no line of sight in this server. The
effect is a search that occasionally reaches through a wall — a wrong answer in
the direction of doing something rather than nothing.

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
- **Zone triggers are still undispatched.** Re-measured against the reference
  rather than taken from the triage: `[zone]` 262, `[zoneexit]` 165,
  `[mapzone]` 306, `[mapzoneexit]` 73 — 806, which is the number
  `LOSTCITY_PORT_TRIAGE.md` §7.2 states and it is right. Worth knowing before
  sizing anything by it: only **427** of those are zone-keyed. `mapzone` and
  `mapzoneexit` key off the *map square* (`>> 6`), not the zone (`>> 3`), so
  they never touch this structure. Dispatch is Phase 2 work; the reference fires
  all four from `NetworkPlayer.updateMap`, keyed by script *name*
  (`[zone,<level>_<mx>_<mz>_<lx>_<lz>]`), which the numeric-subject
  `mock230_scripts_run_trigger` cannot express yet.

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
   - **World events broadcast.** `mock230_world_broadcast_loc` is what makes a
     door one player opens open for the other; ground objs withdraw from
     everyone who was told about them. This is an approximation of the zone the
     reference would address, and step 3 below is what replaces it — the half a
     broadcast cannot do is *replay* to whoever arrives later.

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

   What is still single-player, deliberately and separately:

   - **The socket server accepts one connection at a time.** `serve()` runs one
     session to completion; a real one wants the non-blocking accept with
     per-connection output buffering noted at the end of this section. The
     embedded host (`mock230_embed_connect`) holds several, which is what the
     test drives.
   - **One scene origin for the whole world.** `mock230_scene_build` is a
     singleton, so `maybe_rebuild` moves the origin for everybody and marks
     every player as owing a REBUILD_NORMAL. Two players more than ~70 tiles
     apart pull it back and forth. Step 3 is the fix.
   - **NPC_INFO's extended masks are shared across observers.** An npc's
     `face_entity` names the *local* player, which is right for the client the
     retaliation is being encoded to and wrong for every other one. Making it
     per-observer needs the npc masks to be per-observer.
   - **`srv->iterator` is one cursor**, as it is in the reference, and for the
     same reason there is one script-parking slot per player (§3.10).
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
4. **Fill in the host opcodes.** 215 of 398 are implemented (63 VM core, 145
   host, 7 host-db — the count is generated into
   `src/net/mock/mock230_opcode_coverage.gen.h`, so read it there rather than
   trusting a number typed into prose; this one said "~100 of 396" long after
   it stopped being true). Driven by the gap
   report from step 2, plus `.dbtable`/`.dbrow` support in the content reader,
   which landed: prayers were flattened into a bespoke `.prayer` grammar to
   avoid writing one and are an ordinary dbtable now (`db_find` was the last
   piece), and drop tables and shops want the same.
5. **Move the C content into content.** ~3,200 lines that are content by
   LostCity's definition still live in C: the bank (1,370), combat (858),
   equipment, the world map, doors, and the login burst (prayer was one of
   these and is done — see mock230_player_systems.md §4.1). The reason is
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
