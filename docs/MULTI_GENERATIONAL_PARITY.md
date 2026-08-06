# Multi-Generational Parity

How the v3 C client (`src/`) supports multiple generations of RuneScape from one
core game, and the plan for connecting to the modern-generation **xrsps** server
(`~/Documents/git_repos/xrsps-typescript`).

Reference implementations:
- **Old generation**: `Client-TS/` (LostCity, rev ~225–254, dat1 cache, IF1 UI, CS1).
- **Modern generation**: `xrsps-typescript` (OSRS rev 233, dat2 cache, IF3 UI, CS2).
- **Prior art**: `v1/` + `v0/osrs/` — the earlier iteration that rendered worlds,
  NPCs, players and projectiles for both generations. See §3 for what to adopt
  and what to avoid.

---

## 1. The axes of "generation"

A generation is not one switch. v3 already separates two axes; xrsps adds a third:

| Axis | Enum / seam | Values today |
|---|---|---|
| **Cache format** | `enum AppCacheKind` (`src/app.h`) → `CacheProvider` vtable | `APP_CACHE_DAT1` (jagfile-era), `APP_CACHE_DAT2` (js5-era, OSRS) |
| **Protocol revision** | `enum GameProtoRevision` → `struct GameProtoRevTable` (`src/net/rev/gameproto_revisions.h`) | `LC254`, `LC245_2` |
| **Transport** *(new)* | none yet — `src/net/net.c` assumes raw TCP + ISAAC framing | TCP+ISAAC (old gen) vs **WebSocket, plaintext opcodes** (xrsps) |

Derived-but-independent sub-axes that follow from these:
- **UI logic VM**: IF1 components + CS1 (`src/cs1vm`) vs IF3 components + CS2
  (`src/cs2vm2`). Currently keyed implicitly off `cache_kind`
  (`src/app.c` passes the CS2 host only for dat2). Should become an explicit
  `ui_logic` field of the generation profile.
- **UI chrome sourcing**: RevConfig INI gameframe (dat1 has no gameframe
  interface in cache) vs cache-driven root interface (dat2/xrsps —
  `WIDGET_SET_ROOT` names a cache interface group; no RevConfig needed).

A **generation profile** is a tuple: `(cache_kind, proto_rev, transport,
ui_logic, chrome_source)`. Old gen = `(dat1, lc254, tcp+isaac, cs1, revconfig)`.
xrsps = `(dat2, xrsps233, websocket, cs2, cache-root)`.

---

## 2. Current v3 seams — what already works

### 2.1 Cache — clean, generation-agnostic (keep as-is)

- `struct CacheProvider` + `CacheProviderVTable` (`src/engine/cache_provider.h`):
  ~30 async `Task_*Load` slots (models, obj/npc/loc/seq/spotanim types,
  components, clientscripts, sprites, fonts, enums/structs/params, maps,
  worldmap). The core game only calls the null-tolerant `CreateTask_*` inline
  wrappers — an unset slot means "this generation doesn't have that asset", and
  callers already treat NULL as absent.
- Two concrete providers: `dat1_vtable` (`src/engine/dat1/dat1_buildcache.c`)
  and `dat2_vtable` (`src/engine/dat2/dat2_buildcache.c`). dat2 adds
  enum/struct/param/worldmap/mapelement; dat1 adds jagfile-sourced
  sprite/font-by-name loads.
- All decode converges on neutral `struct ToriRS_*` types
  (`src/engine/torirs_types.h`, `torirs_*_from_rscache.c`). World, UI, and
  render layers never see dat1/dat2 structs.

**Consequence for xrsps**: the cache side is essentially done. xrsps serves a
standard OSRS **rev 233 dat2** cache (`main_file_cache.dat2` + `.idx0..14`)
over plain HTTP (`/caches/` — see `xrsps/src/client/Caches.ts`); a C client
just points the existing dat2 provider at a local copy of the same files.
Remaining cache work is decoder currency (rev 233 fields) and runtime XTEA
(§5.4), not architecture.

### 2.2 Network revision — clean seam, needs three extensions

- `struct GameProtoRevTable` (`src/net/rev/gameproto_revisions.h`): revision id,
  name, `client_version`, `jag_checksum[9]`, and four fn pointers —
  `packetin_size`, `packetin_code` (wire→canonical), `packetin_wire`,
  `packetout_code`.
- Canonical vocabulary: `enum GameProtoPktName` / `GameProtoPktOutName`
  (`src/net/rev/pktnames.h`). Rev modules that lack a packet simply omit the
  table row; unknown wire opcodes are dropped with a log line.
- Shared parser `gameproto_parse` fills the tagged union `struct RevPacket`
  (`src/net/rev/revpacket.h`); the game consumes canonical packets via the
  command bus → serial `exec_runner` → `RS_GameProto_Exec` switch
  (`src/game/rs_gameproto_exec.c`).

This survives contact with xrsps *if* extended with (a) a transport/framing
seam, (b) a per-rev login driver, (c) per-rev payload parsers where field
shapes differ (§5).

### 2.3 What is still hard-wired to the old generation

Identified gaps (all outside the rev table today):

| Hard-wired piece | Where | Old-gen assumption |
|---|---|---|
| Login handshake shape | `src/net/loginproto.c` | opcode 14/16, RSA block, ISAAC±50 seeding, 3-byte success tail, uid=1337, 9 jag CRCs |
| Transport & framing | `src/net/net.c`, `packetbuffer.c` | raw TCP, ISAAC-encrypted opcode byte |
| Appearance decode | `src/net/rev/packets/pkt_player_appearance.c` | Colocated 2026-08-04: one canonical 12-slot vocabulary + every wire spelling of it (classic and osrs239's two-array/`+0x800` shape) + an encoding-independent op-stream reader, mirroring pkt_npc_info.c's op list. `GameProtoRevTable.appearance_decode` selects the reader; NULL = classic. |
| PLAYER_INFO / NPC_INFO bit layout | `pkt_player_info.c`, `pkt_npc_info.c` | classic bitcodec, NPC 14-bit slots |
| Scene-origin math | `task_gameproto_exec.c` / `World_ResetScene` | `(zone-6)*8` local-coord base (unified 2026-08-03; `scene_off` deleted) |
| ClientCode constants | `src/game/rs_clientcode.h` | 254-era baked component behaviors (friends rows, bankmode, designer…) |
| Minimenu action / button-type codes | `src/revconfig/revconfig.h` | 254-era action numbers (WALK=718, OPLOC1=625, …) |
| CS1-vs-CS2 selection | `src/app.c` | keyed on `cache_kind` instead of explicit profile field |

---

## 3. Lessons from v1 (the prior dual-generation client)

v1's generation support lived in `v0/osrs/` (shared RS lib) + `v1/` (engine).
**Correction to folklore**: v1's *old-gen network* was never finished
(`packetin_size_lc254` is stubbed; only `REBUILD_NORMAL` handled) — old gen ran
from cache. Its genuinely dual-generation strengths were the cache/world side.
v3 is already ahead of v1 on the network seam.

**Adopt:**
- **Normalized-core-type convergence** — `ToriAuxLibCore_{Model,Npctype,Objtype,
  Sequence}` with `NewFromDat1…`/`NewFromDat2…` converters; render/world 100%
  generation-agnostic. v3 already does this (`ToriRS_*` types).
- **Decode-to-op-list for entity info** (`v0/osrs/packets/pkt_player_info.c`):
  the bitstream reader emits a flat `PktPlayerInfoOp[]` array (ADD_PLAYER,
  WALKDIR, APPEARANCE, SPOTANIM, EXACT_MOVE, …) with zero game coupling; the
  game applies ops. This is the right shape for supporting classic *and* modern
  GPI: two readers, one op vocabulary, one application path. v3's
  `task_exec_entity_info.c` already applies decoded streams to `World`; the
  work is formalizing the op vocabulary so a second reader can plug in.
- Both-generation fields coexisting per RevConfig item (dat1 jagfile fields vs
  dat2 archive-id fields in one struct) — v3 already inherited this.

**Avoid:**
- **Wholesale task duplication** — v1 duplicated every task
  (`tasks/dat1/task_dat1_*` vs `tasks/dat2/task_dat2_*`) and every fetch
  wrapper, dispatched by a global `cache_mode` enum, maintained in lockstep by
  hand. v3's vtable provider already collapses this; keep it that way for the
  network side too (fn-pointer slots in the rev table, not parallel file trees
  dispatched by `if (rev == …)` in core code).
- **Instance-wide mode enum with both backends allocated** — v1 carried both
  `dat1_bc` and `dat2_bc` with one always idle. v3's "exactly one disk is live"
  approach is better.
- Don't assume v1 has a modern-OSRS GPI reader to copy — it has only the
  classic bitcodec. The modern 4-pass reader must be written fresh (§5.3),
  with `xrsps/server/src/network/encoding/PlayerPacketEncoder.ts` as ground
  truth.

---

## 4. xrsps target profile (ground truth summary)

The single most important finding: **xrsps is not a classic RS TCP server.** It
is a Node `ws` WebSocket server speaking a custom length-prefixed binary opcode
protocol. There is **no RSA, no ISAAC, no JS5, no opcode encryption**.

| Property | Value | Ground truth |
|---|---|---|
| Cache | OSRS rev 233 dat2 (`osrs-233_2025-10-01`), local/HTTP, XTEA map keys | `target.txt`, `caches/caches.json`, `src/client/Caches.ts` |
| Transport | WebSocket (RFC 6455), binary frames, server offers permessage-deflate (client may decline) | `server/src/network/wsServer.ts` |
| Framing | `[opcode u8][len u8 or u16 BE if variable][payload]`, multiple packets batched per WS message | `ServerBinaryEncoder.toPacket`, `decodeBatchedServerPackets` |
| Opcode tables | S→C: `src/shared/packets/ServerPacketId.ts` (+ lengths). C→S: `src/shared/packets/ClientPacketId.ts` (opcodes 180–255 — the **active** set; ignore the alternate 1–105 OSRS-style set in `src/shared/network/`) | |
| Payload shapes | typed union in `server/src/network/messages.ts` | |
| Login | app-level, plaintext: S→C `welcome(0)`; C→S `hello(200)`, `login(204)` = user + pass + revision int (rejected with code 6 on mismatch); S→C `login_response(3)`; C→S `handshake(202)` (name, appearance, clientType); S→C `handshake(2)` ack with local index + world-entry burst | `server/src/network/LoginHandshakeService.ts` |
| Tick | 600 ms server tick (`TICK=1` packet), client 20 ms cycles (30 cycles/tick) | `server/src/config/index.ts` |
| Player sync | `PLAYER_SYNC(20)`: header `baseX u16, baseY u16, localIndex u16, loopCycle int, len u16` + modern OSRS 4-pass GPI bitstream (2-bit skip-run selector +0/5/8/11 bits; movement 2-bit type, 12/30-bit teleport forms; 13-bit external coords; 1–3-byte mask with 0x80/0x4000 extension) | `encoding/PlayerPacketEncoder.ts`, `BitWriter.ts` |
| Player masks | `constants.ts` `PLAYER_MASKS`: FORCED_CHAT 0x01, FACE_DIR 0x02, APPEARANCE 0x04, ANIMATION 0x08, PUBLIC_CHAT 0x10 (Huffman CP1252), HIT 0x20, FACE_ENTITY 0x40, COLOR_OVERRIDE 0x200, FORCE_MOVEMENT 0x400, ACTIONS 0x800, …, SPOT_ANIM 0x10000 | |
| NPC sync | `NPC_INFO(21)`: `loopCycle int, large bool, len u16` + GNI bitstream (8-bit count, 2-bit move selectors, masks incl. SEQUENCE 0x10, SPOT_ANIM 0x20000) | `encoding/NpcPacketEncoder.ts` |
| Appearance | gender, skull icon, prayer icon, 12 equip + 12 secondary-override slots, 5 colors, 7 anim shorts, CP1252 name, combat lvl, skill total, hidden, override flags, 3 action strings, custom ammo | `encoding/AppearanceEncoder.ts` |
| Scene | 104×104 build area; base `(tile-48) & ~7`, rebased when local offset exits `[16,88)`; base arrives in every PLAYER_SYNC header. `REBUILD_NORMAL(141)` = regionX/Y u16 + per-region 4×int XTEA keys; `REBUILD_REGION(140)` = instanced 4×13×13 bit-packed template chunks | `ServerBinaryEncoder.encodeRebuildNormal/Region` |
| Zone/effects | `LOC_ADD_CHANGE(134)/LOC_DEL(135)/LOC_ANIM(136)`, `SPOT_ANIM(83)`, `PROJECTILES(84)`, `HITSPLAT(82)`, `GROUND_ITEMS(54)/…_DELTA(55)` | |
| Vars/skills/inv | `VARP_SMALL/LARGE/VARBIT/VARP_BATCH(40–43)`, `SKILLS_SNAPSHOT/DELTA(70/71)`, `INVENTORY_SNAPSHOT/SLOT(50/51)` (qty u8 with 255→int sentinel; itemId+1, 0=empty) | |
| UI | `WIDGET_SET_ROOT(102)` (display-mode root group, e.g. 161/164), `WIDGET_OPEN_SUB(103)` addressed by packed `targetUid = parentGroup<<16 \| child`, `WIDGET_SET_TEXT/HIDDEN/ITEM/NPC_HEAD/PLAYER_HEAD/ANIM/FLAGS[_RANGE]`, `RUN_CLIENT_SCRIPT(170)` + inline `WIDGET_RUN_SCRIPT(110)`. Behavior = real cache CS2 clientscripts; server sends scriptIds + varps. **No ClientCode-style baked behavior.** | `server/src/widgets/WidgetManager.ts` |
| Interactions out | `widget_action(251)` = widgetId int + group + child + opId + buttonNum + slot + itemId; `if_buttond(253)` drag; `resume_*` dialogs; movement/entity/loc/ground ops with `opNum` 1–5 + modifierFlags | `ClientBinaryDecoder.ts` |
| Chat | Huffman table from cache idx10; public chat payload Huffman-compressed CP1252 | `wsServer.ts` `tryLoadOsrsHuffman` |

---

## 5. Interface inventory — what each abstraction layer must provide

The contract between the **core game** and a **generation module**. ✅ = exists
and sufficient, 🔶 = exists but needs extension, ❌ = missing.

### 5.1 `CacheProvider` (✅, minor additions)

Already generation-agnostic. Additions for xrsps:
- **Runtime XTEA key injection**: map keys arrive in `REBUILD_NORMAL`, not from
  a local `keys.json`. Add `CacheProvider_SetMapKeys(region_id, int[4])` (or a
  keys table on the dat2 provider) consulted by `Task_MapTerrain/SceneryLoad`.
- **Decoder currency**: verify dat2 decoders against rev 233 (we already decode
  OSRS-era caches; spot-check obj/npc/loc/seq/spotanim/component opcode ranges
  against `xrsps/src/rs/config/*`). The padded exact-consumption scan technique
  (see dat2 if3 decoder notes) is the validation tool.
- **Huffman table load** (idx10) for chat — a new provider slot or a one-shot
  load owned by the net layer.

### 5.2 `GameProtoRevTable` (🔶 → extend to a full generation module)

Keep the canonical-name architecture. Extend the table with:

```c
struct GameProtoRevTable {
    /* existing */
    int revision; const char* name; int client_version; int jag_checksum[9];
    int (*packetin_size)(int wire);
    int (*packetin_code)(int wire);
    int (*packetin_wire)(int name);
    int (*packetout_code)(int name);
    /* new */
    const struct NetTransportVTable* transport;   /* §5.5: tcp+isaac | websocket */
    int (*login_drive)(struct LoginState*, ...);  /* §5.6: per-rev handshake */
    int (*parse)(int pkt_name, const uint8_t*, int, struct RevPacket*);
        /* NULL → shared gameproto_parse; xrsps overrides shapes that differ */
    int (*player_info_read)(struct EntityInfoReader*, ... /* → op-list */);
    int (*npc_info_read)(struct EntityInfoReader*, ...);
    int (*appearance_decode)(const uint8_t*, int, struct PktPlayerAppearance*);
    void (*scene_base)(const struct RevPacket*, int* base_x, int* base_z);
        /* lc254: (zone-6)*8 from REBUILD_NORMAL; xrsps: baseX/Y from PLAYER_SYNC header */
    int (*serialize_out)(int pkt_out_name, const void* args, uint8_t* buf);
        /* outbound payload shapes differ (widget_action vs IF_BUTTON, etc.) */
};
```

Rationale: this is the v1 lesson applied — fn-pointer slots on one table, not
parallel task trees dispatched by `if (rev == …)` in core code.

### 5.3 Canonical packet vocabulary (🔶)

Most xrsps packets map onto **existing** canonical names — the exec layer
(`RS_GameProto_Exec`, `App_World*` appliers, `VarPManager`, `InvManager`,
`RS_UISlots`) is reused unchanged:

| xrsps | canonical name (existing) |
|---|---|
| VARP_SMALL/LARGE 40/41 | VARP_SMALL / VARP_LARGE |
| REBUILD_NORMAL 141 | REBUILD_NORMAL |
| LOC_ADD_CHANGE/LOC_DEL/LOC_ANIM | LOC_ADD_CHANGE / LOC_DEL / LOC_ANIM |
| SPOT_ANIM 83 / PROJECTILES 84 | MAP_ANIM / MAP_PROJANIM |
| HITSPLAT 82 | (folds into entity-info HIT op) |
| GROUND_ITEMS/…_DELTA | OBJ_ADD / OBJ_DEL family |
| INVENTORY_SNAPSHOT/SLOT | UPDATE_INV_FULL / UPDATE_INV_PARTIAL |
| SKILLS_SNAPSHOT/DELTA | UPDATE_STAT |
| WIDGET_SET_TEXT/HIDDEN/ANIM/NPC_HEAD/PLAYER_HEAD | IF_SETTEXT / IF_SETHIDE / IF_SETANIM / IF_SETNPCHEAD / IF_SETPLAYERHEAD |
| CHAT_MESSAGE 120 | MESSAGE_GAME |
| PLAYER_SYNC 20 / NPC_INFO 21 | PLAYER_INFO / NPC_INFO |
| RUN_ENERGY 81 | UPDATE_RUNENERGY |

**New canonical names needed** (old-gen table simply omits them, per the
established omitted-row convention): `WELCOME`, `TICK`, `HANDSHAKE_ACK`,
`WIDGET_SET_ROOT`, `WIDGET_OPEN_SUB` / `WIDGET_CLOSE_SUB` (targetUid-addressed),
`WIDGET_SET_ITEM`, `WIDGET_SET_FLAGS[_RANGE]` (IF_SETEVENTS), `RUN_CLIENT_SCRIPT`,
`VARBIT`, `VARP_BATCH`, `REBUILD_REGION`, `DESTINATION`, `COMBAT_STATE`,
`SHOP_*`/`TRADE_*`, `NOTIFICATION`. Outbound: `HELLO`, `LOGIN`, `HANDSHAKE`,
`WIDGET_ACTION`, `IF_BUTTOND`, `RESUME_*`.

Payload shapes for same-named packets **differ** between generations (e.g.
REBUILD_NORMAL carries XTEA keys in xrsps) — hence the per-rev `parse` override
slot; `RevPacket` payload structs become the superset (optional fields zeroed
for the generation that lacks them).

### 5.4 Entity-info op-list codec (❌ — the biggest new build)

Adopt v1's decode-to-op-list design as the seam:
- Define `struct EntityInfoOp` (superset vocabulary: ADD, REMOVE, WALK, RUN,
  TELEPORT, SET_BASE, APPEARANCE, SEQ, FACE_ENTITY, FACE_COORD, SAY, HIT,
  SPOTANIM, EXACT_MOVE, EXT-masks…).
- Reader A (exists, refactor): classic lc254 bitcodec from
  `pkt_player_info.c`/`pkt_npc_info.c`.
- Reader B (new): modern 4-pass GPI + GNI mirroring
  `PlayerPacketEncoder.ts`/`NpcPacketEncoder.ts` — skip-runs, 12/30-bit
  teleports, 13-bit external coords, extensible mask bytes, ushort-smart hits,
  Huffman chat, secondary-equipment appearance fields.
- One shared applier (`task_exec_entity_info.c` reworked to consume ops) —
  routeMove/entityFace/hitmark/spotanim application in `World` is already
  generation-agnostic.

Superset entity state needed in `World`: skull+prayer icon split, secondary
equipment overrides, action-string ops, force-movement, color overrides,
per-tick server `loopCycle` sync.

### 5.5 Transport (❌ — new seam)

```c
struct NetTransportVTable {
    int  (*connect)(struct NetTransport*, const char* host, int port);
    int  (*poll)(struct NetTransport*);            /* pump handshake/frames */
    int  (*read)(struct NetTransport*, uint8_t*, int);   /* de-framed bytes */
    int  (*write)(struct NetTransport*, const uint8_t*, int);
    void (*close)(struct NetTransport*);
};
```
- `transport_tcp`: current behavior (raw stream; ISAAC opcode decode stays in
  `packetbuffer` as today, gated per-rev).
- `transport_ws`: RFC 6455 client — HTTP Upgrade handshake, Sec-WebSocket-Key,
  frame mask (client→server frames MUST be masked), binary frames, ping/pong,
  close. **Do not offer permessage-deflate** — the server only compresses when
  negotiated, so declining keeps the C side zlib-free initially.
- `packetbuffer` gains a per-rev "opcode cipher" flag: ISAAC-subtract (lc254)
  vs plaintext (xrsps). Batched WS messages are just a byte stream to the
  framer — existing size-table walking already handles concatenation.
- The command bus (`TORIRS_CMD_NET_RECV`) sits above the transport, so
  record/replay keeps working for both transports.

### 5.6 Login (🔶 → per-rev driver)

`loginproto.c` becomes the **lc254 login driver** behind the
`login_drive` slot. New `loginproto_xrsps.c`:
1. Await `welcome(0)` (tickMs, serverTime).
2. Send `hello(200)` (client string, version).
3. Send `login(204)` (username, password, **revision int = 233** — server
   rejects mismatches with error 6).
4. Await `login_response(3)` (success, errorCode, error, displayName).
5. Send `handshake(202)` (name, hasAppearance [+ appearance block], clientType 0).
6. Await `handshake(2)` ack → **local player index** + world-entry burst
   (interfaces, inv, skills, varps, appearance + NPC snapshots).
No RSA/ISAAC state; `jag_checksum`, `uid`, seeds unused (NULL/zero in table).

### 5.7 UI: ClientCode, RevConfig, and the VM split (🔶)

- **RevConfig becomes old-gen-only chrome.** xrsps needs none: root group +
  sub-mounts arrive over the wire and everything else is cache IF3 + CS2. Keep
  RevConfig for dat1; the profile's `chrome_source` decides.
- **ClientCode (`rs_clientcode.c`) is old-gen-only.** Modern behaviors are CS2
  scripts + enums/structs/params. Gate `RS_ClientCode_Tick/Button` on the
  profile, not unconditionally.
- **Mount addressing**: xrsps mounts sub-interfaces by
  `targetUid = parentGroup<<16 | child`. `RS_UISlots` + the existing
  persist-by-com_id/re-apply-on-generation pattern (if_texts / if_heads) map
  directly; add the packed-uid addressing mode alongside old-gen slot ids.
- **`RUN_CLIENT_SCRIPT`** → existing CS2 host (`rs_cs2_host.c` /
  `task_cs2_run.c`) with mixed int/string args — same invocation path as hook
  scripts today. `WIDGET_SET_FLAGS[_RANGE]` maps to IF_SETEVENTS semantics on
  the UITree (new).
- **VM selection** becomes `profile->ui_logic` (CS1|CS2) instead of
  `cache_kind == DAT1` in `src/app.c`.
- Minimenu action codes (`RevConfigMiniMenuAction`) are old-gen wire numbers;
  xrsps interactions are `opNum` 1–5 + packet kind. The minimenu layer should
  emit **canonical interaction intents** (OPLOC{1-5}, OPNPC{1-5}, OPPLAYER,
  OPHELD, OPOBJ, WIDGET_ACTION, MOVE) and let `serialize_out` map them per rev.

### 5.8 Timing (🔶)

Old gen: no explicit tick packet (client infers from packet cadence). xrsps:
`TICK(1)` + `welcome.tickMs` (600 ms) with 30 client cycles/tick, and
tick-aligned input draining server-side. The client loop already runs 20 ms
cycles; add a `server_tick_ms` profile field and use server `loopCycle` values
(PLAYER_SYNC/NPC_INFO headers) to sync animation/effect clocks.

---

## 6. Connection plan — xrsps in phases

Mirrors the proven v3 sequence for LostCity (net stack → entity sync → zones →
UI), reusing the exec/appliers unchanged.

**Phase 0 — cache bring-up (no network).**
Obtain the `osrs-233_2025-10-01` cache (copy from `xrsps/caches/` or OpenRS2).
Boot `--dat2` against it offline: world render via `TORIRS_WORLD_MAP`, open the
root interface groups (161/164) by id, run CS2 smoke tests. Validates decoder
currency before any protocol work. XTEA keys from the cache's `keys.json` can
seed the provider for offline map loads.

**Phase 1 — transport + login.**
`NetTransportVTable` + `transport_ws` (no deflate); rev module `xrsps233`
(opcode/length tables transcribed from `ServerPacketId.ts`/`ClientPacketId.ts`);
`loginproto_xrsps` driver; handle `WELCOME`/`TICK`/`LOGIN_RESPONSE`/
`HANDSHAKE_ACK`. Exit criterion: logged in, tick packets flowing, clean logout.
(Test hook: xrsps server runs locally via its `server/` npm scripts;
`--connect ws://localhost:<port>` + `--rev xrsps233`.)

**Phase 2 — world entry.**
`REBUILD_NORMAL` (region coords + runtime XTEA keys → `CacheProvider_SetMapKeys`
→ existing `Task_WorldLoad` path), scene base from PLAYER_SYNC header via
`scene_base` hook. Exit criterion: standing in the world, terrain + locs
rendered, camera on local player.

**Phase 3 — entity sync.**
Modern GPI/GNI readers → op-list → shared applier; modern appearance decode
(worn-equipment model composition already exists from the dat2 UI work; the
appearance block itself is done — `pkt_player_appearance.c`'s `APPEARANCE_ENC_V5`
reader, wired via `osrs239`'s `appearance_decode` hook);
movement/teleport/skip-runs; Huffman chat decode. Exit criterion: local player
+ other players + NPCs moving/animating. This is the phase with real new code;
everything downstream of the op-list already exists.

**Phase 4 — zones & effects.**
Map LOC_*/SPOT_ANIM/PROJECTILES/HITSPLAT/GROUND_ITEMS onto the existing
`App_World*` appliers (projectile/spotanim/hitsplat plumbing all exists from
the LostCity work). Exit criterion: combat scenes look right.

**Phase 5 — vars, UI, interactions.**
VARP/VARBIT/VARP_BATCH → `VarPManager` (add varbit masking); WIDGET_* → uitree
mounts by targetUid; RUN_CLIENT_SCRIPT → CS2 host; SKILLS/INVENTORY → existing
managers; outbound `widget_action`/movement/entity ops via `serialize_out`.
Exit criterion: tabs, inventory, clicking, walking, talking.

**Phase 6 — polish.**
Shops/trade, dialogs (`resume_*`), quest list, notifications, jingles/songs,
REBUILD_REGION (instances), WORLDENTITY_INFO, permessage-deflate if bandwidth
ever matters.

---

## 7. Risks / open questions

- **Modern GPI reader correctness** — the densest new code. Mitigation: the
  server encoder is readable TypeScript we control; build a golden-stream test
  (record WS bytes from the reference client, replay through the C reader,
  diff op-lists). The existing mock-server harness (`src/net/mock/`) extends to
  WS by feeding recorded frames through the command bus.
- **Rev-233 dat2 decoder drift** — our dat2 decoders were validated on other
  OSRS-era caches; rev 233 may add config opcodes. The misalignment symptom
  pattern (corrupt hook-script ids, garbage IF3 fields) is known; use the
  exact-consumption scan to validate early in Phase 0.
- **CS2 opcode coverage** — newer caches exercise opcodes our table lacks;
  unimplemented ops are silent no-ops that visibly break layouts (seen before
  with CC_COPY 105). Budget for opcode gap-filling during Phase 5. Note xrsps
  also ships custom client-side CS2 (`src/shared/ui/widgets/custom/*.cs2.ts`)
  — those are client-authored scripts, not cache scripts; treat any scriptId
  the cache can't resolve as a candidate xrsps-custom script and decide then
  whether to port or stub.
- **WebSocket in C** — small but fiddly (masking, fragmentation, ping/pong).
  Keep it minimal and behind the transport vtable; no TLS needed for local dev
  (`ws://`), revisit `wss://` only for remote play.
- **Two client→server opcode sets in xrsps** — implement only the active
  180–255 set (`src/shared/packets/ClientPacketId.ts`); the 1–105 OSRS-style
  set in `src/shared/network/` is an alternate path the reference client does
  not use for emission.
- **Sub-tile precision** — xrsps uses `<<7` (128 units/tile) sub-tile coords in
  some snapshots; old gen also uses 128-unit fine coords internally, so this
  should align, but verify exact-move/force-movement interpolation.

---

## 8. Status log

- **2026-07-23** — Document created. Research pass over v3 seams, v1/v0 prior
  art, and the xrsps server. Conclusions: cache seam is done (rev-233 dat2 is
  standard); network rev-table survives but needs transport/login/parse/
  entity-codec slots; xrsps is WebSocket + plaintext framing (no RSA/ISAAC/JS5);
  the modern 4-pass GPI reader is the largest net-new component and v1 has no
  copyable implementation of it. No code changes yet.

- **2026-07-23 — Phase 0 landed (boot-manifest system + offline rev-233 boot).**
  - Boot-manifest INI: `manifest_rs254.ini` / `manifest_xrsps.ini` at repo root;
    loader `src/bootmanifest/bootmanifest.{h,c}` (`struct BootManifest`,
    `BootManifest_LoadFile` / `_ApplyToConfig`) reuses the `3rd/ini` tokenizer.
    Sections `[cache:boot]` / `[net:boot]` / `[ui:boot]`; relative path values
    join against the manifest's directory, absolutes pass through.
  - `struct AppConfig` (src/app.h) gained `connect_port`, `rsa_exp`, `rsa_mod`,
    `jag_crc[9]` + `jag_crc_set`, `client_version`, `ui_logic` (+ new
    `enum AppUiLogic`). `main.c` pre-scans `--manifest`, applies before the flag
    loop (precedence CLI > manifest), and adds `--port` and `--offline`; the
    hard-coded 43594 now falls back from `cfg.connect_port`.
  - Login-param flow (src/app.c net init): RSA precedence env > manifest >
    built-in; jag CRCs / client version pushed into the rev table via new
    `GameProtoRev_SetJagChecksums` / `_SetClientVersion`
    (`src/net/rev/gameproto_rev_params.c`) only when the matching env is absent.
    The lazy `TORIRS_JAG_CRC` getenv in the lc254 table is untouched (env wins).
  - UI-logic seam: `App_UiLogic(app)` resolves CS1/CS2 from the manifest or
    derives it from `cache_kind` (bit-identical legacy default). The CS2-host
    wiring and the old-gen ClientCode `Tick`/`Button` calls now key off it
    instead of `cache_kind == DAT1`.
  - Verified: `make` links clean; `test-bootmanifest` (new) + `test-revconfig`,
    `test-net-login`, `test-net-loopback`, `test-net-exec`, `test-entity-decode`
    all green. `--manifest manifest_rs254.ini --offline` boots the dat1 world +
    revconfig UI as before; `--manifest manifest_xrsps.ini --offline` boots the
    rev-233 dat2 cache and opens interface 161 (1116 components, 6 CS2 onloads)
    — confirming the dat2 config/component/clientscript decoders handle rev 233.
  - Known follow-up (Phase 2): the map XTEA loader (app.c:1352) reads
    `<cache_dir>/xteas.json`; the xrsps cache ships `keys.json`, so offline maps
    load 0 locs until the keys.json seed + runtime injection land.

- **2026-07-23 — Phase 1 landed (transport + xrsps login), verified live.**
  - Rev-table seam (`src/net/rev/gameproto_revisions.h`): `struct
    GameProtoRevTable` gained `transport_kind`, `opcode_plaintext`,
    `server_tick_ms`, `login` (NetLoginVTable), and NULL-defaulted `parse` /
    `player_info_read` / `npc_info_read` / `appearance_decode` / `scene_base` /
    `out_vt` slots for later phases. lc254/lc245_2 leave them zero → unchanged.
  - Opcode cipher flag (`packetbuffer.c`): the ISAAC opcode-subtract is now
    gated on `!rev->opcode_plaintext`; xrsps sends plaintext opcodes.
  - Login-driver seam (`src/net/login_vtable.h`, `net.c`): the 5 login touch
    points dispatch through `net->rev->login` when set, else the untouched
    classic `loginproto.c`. New handle field `login_generic`.
  - Transport seam (`src/platform/net_transport.h` + `_tcp.c` / `_ws.c` /
    `_ws_frame.c`): `NetTransport` vtable selected by `rev->transport_kind`.
    TCP is a thin adapter over the unchanged `PlatformSocket`; WS is a minimal
    RFC 6455 client (HTTP upgrade, masked binary frames out, de-framed in,
    ping/pong, close) that never offers permessage-deflate. `main.c` now builds
    the transport from `app.net->rev->transport_kind`. The pure frame codec is
    unit-tested (`test-ws-frame`).
  - xrsps233 rev module (`src/net/rev/xrsps233/`): full server-packet SIZE
    table (from ServerPacketId.ts) so framing never desyncs; Phase-1
    `packetin_code` returns PKT_NAME_NONE (packets frame then drop). Login
    driver `src/net/loginproto_xrsps.c`: welcome→hello→login→login_response→
    handshake, no RSA/ISAAC. Registered as `"xrsps233"` in GameProtoRev_ByName.
  - **KEY PROTOCOL FINDING**: the xrsps server decodes exactly ONE packet per
    WebSocket message (`decodeClientPacket` does not loop). So every client
    packet must be its own WS frame. Two fixes were required: the login driver
    emits one frame per `SendRaw` (frame-boundary tracking in the handle), and
    the WS transport preserves per-message boundaries in its pre-OPEN queue
    (`app_msg_len[]`) instead of coalescing them into one frame on OPEN.
  - **Verified against the live xrsps server** (`server/src/index.ts`, port
    43594): WS upgrade accepted, server logged "Hello from osrs-typescript",
    full handshake completed (client received welcome(0) + login_response(3),
    sent handshake(202), got the ack(2) + world-entry burst). Client reached
    GAME state; TICK(1) framed 29× over ~18 s (600 ms cadence), PLAYER_SYNC(20)
    / NPC_INFO(21) once per tick, VARBIT(42) 325×, all framing to clean sizes
    with zero desync — validating the whole size table. No crashes. lc254
    offline boot + all net/ui regressions still green.
  - Makefile: added `bootmanifest`, `gameproto_rev_params`, the three transport
    files, `loginproto_xrsps`, and `gameproto_rev_xrsps233` to SRCS +
    NET_CORE_OBJS; new `test-ws-frame` target; per-file `-Inet/rev/xrsps233`
    rule.

- **2026-07-23 — Phase 2 investigation: the plan's world-entry model was wrong
  for xrsps; Phase 2 folds into Phase 3.** Captured a full live login stream and
  found:
  - **REBUILD_NORMAL(141) is never sent on a normal login.** The server only
    emits it (and REBUILD_REGION 140) from `WorldEntityService`/`MovementService`
    for world-entities/instances. The login burst goes straight to
    handshake-ack(2) → inventory(50) → skills(70) → run_energy(81) →
    varps(40/41) → varbits(42) → PLAYER_SYNC(20)/NPC_INFO(21) every tick. So
    the planned "REBUILD_NORMAL → scene_base hook → Task_WorldLoad" path does
    not fire; **the scene base for xrsps must come from the PLAYER_SYNC(20) GPI
    header (baseX/baseY/localIndex)** — which is the Phase 3 GPI reader. Scene
    entry and the GPI header are therefore one unit of work, not two phases.
  - **Map XTEA keys are static, not runtime-delivered.** With no REBUILD_NORMAL,
    keys come from the cache's `keys.json`. But the existing loader
    (`3rd/rscache/src/xtea_config.c`, `RSCache_XteaConfigLoadKeys`) expects the
    OSRS array form `[{archive,group,mapsquare,key[4]},…]` and
    `RSCache_XteaConfigFindKey(table_id, archive_id)` matches on
    `archive==table_id && group==mapGroupId`. The xrsps `keys.json` is instead an
    **object keyed by region id** `{"<(rx<<8)|ry>":[k0..k3],…}`. Bridging it
    needs (a) an object-form parser and (b) mapping each region id to its map
    group id (resolve `l{rx}_{ry}` / `m{rx}_{ry}` via the dat2 reference table)
    so keys land under the `(5, groupId)` the loader looks up — or a
    region-keyed `FindKey` variant. This also fixes the offline "0 locs" gap
    (app.c used to require `xteas.json`, absent from the xrsps cache). **Bound:**
    OldSchool still encrypts map locs at revision 233; the plain-storage gate is
    ≥ 237 (`RSCache_MapLocsEncrypted`). The region-keyed loader work item is
    unaffected — rev 233 still needs those keys.
  - **Revised plan**: merge Phase 2 into Phase 3 as "world entry + entity sync":
    write the modern GPI/GNI readers (`xrsps_player_info.c`/`xrsps_npc_info.c`),
    derive the scene base from the GPI header (the `scene_base` slot is already
    in the rev table, unused), add the region-keyed XTEA loader, and wire
    `rev->parse` + real `packetin_code` mappings for the reused canonical names.
    The rev-table slots (`parse`, `player_info_read`, `npc_info_read`,
    `appearance_decode`, `scene_base`) are all in place for this; no further
    seam work is needed, only the decoders + the XTEA loader.
  - No Phase 2 code landed (the original design was invalidated before
    implementation); the finding above is the deliverable.

- **2026-07-23 — Pivot to real OSRS rev 230 + mock server; world builds.**
  The xrsps custom WebSocket protocol was dropped (drifted too far from real
  revisions). New target: authentic **OSRS revision 230** (raw TCP + ISAAC +
  RSA), driven by RSProt (the 230 protocol lib) for opcodes/login and Kronos
  (rev-184 server) for on-login packet order. All Phase-0/1 seam work carried
  over unchanged.
  - **osrs230 rev module** (`src/net/rev/osrs230/`): full server-packet size
    table from RSProt `GameServerProt` (framing never desyncs); `packetin_code`
    maps REBUILD_NORMAL (opcode 68) now, rest frame-and-drop. `parse` override
    (`osrs230_parse.c`) decodes the 230 REBUILD_NORMAL (worldArea, zoneX via
    p2Alt2, zoneZ, keyCount, per-square XTEA ints). `transport_kind=TCP`,
    `opcode_plaintext=0` — reuses the classic ISAAC packetbuffer + login vtable.
    `PktMapRebuild` gained heap key fields (freed in gameproto_free);
    `net.c` now dispatches `rev->parse` before the shared parser.
  - **osrs230 login driver** (`src/net/loginproto_osrs230.c`, NetLoginVTable):
    op 14 → `[status][sessionId:8]` → op 16 GAMELOGIN with header + RSA block
    (encCheck=1, 4 seeds, sessionId echo, authType, user/pass) → response 2 →
    GAME. ISAAC out=seed, in=seed+50. Simplified vs full RSProt (no XTEA/OTP/
    CRC/host-stats block) since the client and mock agree on the block.
  - **Mock server** (`src/net/mock/mock230_main.c`, `make -C src mock230`): a
    standalone TCP listener that RSA-decrypts the login block with a fixed
    private key (client uses the matching public key via
    `manifest_osrs230.ini` rsa_exp/rsa_mod), arms ISAAC, and sends the on-login
    burst — REBUILD_NORMAL (Lumbridge zone 402,402) + VARP_SMALL + run
    energy/weight + a welcome MESSAGE_GAME — then idles.
  - **Map XTEA**: the client loads keys from the cache's own `xteas.json`
    (array form `{archive:5, group, key[4]}`), which `RSCache_XteaConfigLoadKeys`
    already reads and `FindKey(5, archiveId)` already matches — **and only when
    `RSCache_MapLocsEncrypted` says keys are required** (OldSchool below 237;
    RS2 dat2 from 414). The correct cache is `cache.osrs230/` (has xteas.json), NOT the
    xrsps keys.json cache — so REBUILD_NORMAL keys are only needed for instanced
    regions. `manifest_osrs230.ini` points at `cache.osrs230`. At OldSchool ≥ 237
    (`manifest_osrs239.ini` / `cache.osrs239`) archives are plain and no key file
    is shipped or applied.
  - **Verified end-to-end**: `mock230` + `torirs --manifest manifest_osrs230.ini`
    → 230 login handshake (RSA/ISAAC) completes, client reaches GAME, parses
    REBUILD_NORMAL (zoneX=402 zoneZ=402), triggers the existing
    `CreateTask_WorldLoad`, and builds the Lumbridge scene — `world_load: 430
    locs, 399 models, 17 seqs` (was 0/0 before the right cache). Headless BMP
    shows Lumbridge castle, terrain, water, minimap. All net/ui regressions +
    lc254 offline boot still green. The on-login burst's VARP/energy/message
    packets frame cleanly and drop (decoders not yet wired — not needed for the
    world build); GPI/NPC decode is the natural next step.

- **2026-07-23 — Expanded mock burst + RSProt-style parser folders.**
  - The mock (`mock230_main.c`) now sends the full Kronos-order on-login burst
    with real rev-230 opcodes: REBUILD_NORMAL → IF_OPENTOP(60) → IF_OPENSUB(6)×5
    → IF_SETEVENTS(47) → VARP_SMALL(35)×2 → run energy(77)/weight(27) →
    UPDATE_INV_FULL(10)×2 → UPDATE_STAT_V2(114)×23 → MESSAGE_GAME(90) →
    PLAYER_INFO(23)/NPC_INFO(104) placeholders → SERVER_TICK_END(108). Every
    outbound packet is logged (`mock230: -> NAME op=N fixed/var payload/framed +
    hex`). All 41 frame cleanly on the client (fixed sizes match the table); the
    world still builds.
  - **Packet parsers reorganized like RSProt** under `src/net/rev/packets/` (the
    "codec" library): the entity parsers moved there (`pkt_player_info.c`,
    `pkt_npc_info.c`, `pkt_player_appearance.c` = the classic/lc versions), and
    the osrs230 REBUILD_NORMAL decode became `packets/pkt_rebuild_normal.c`. A
    revision's `parse` slot now dispatches by canonical name into the versioned
    parser: `osrs230_parse.c` calls `pkt_rebuild_normal_read`, and future
    modern packets slot in as `pkt_player_info_v5.c` / `pkt_npc_info_v5.c` that
    the osrs230 `player_info_read` / `npc_info_read` slots call — matching
    RSProt's revision → versioned-codec structure.

- **2026-07-24 — Local player renders with appearance; rev->parse packet_type
  bug fixed.**
  - The mock now sends a real PLAYER_INFO (opcode 23 → PKT_NAME_PLAYER_INFO):
    a classic/Kronos-style GPI bitstream placing the local player (teleport to a
    scene-local tile) plus a byte-aligned extended-info APPEARANCE block (lc254
    layout: gender, 12 slots with naked-male body idks, 5 colours, 7 anim
    shorts, name, combat). The client reuses the existing
    `pkt_player_info_reader_read` + `PktPlayerAppearance_Decode` + model builder
    unchanged — so the local player spawns and the orbit camera follows it.
  - **Root-cause fix**: a `rev->parse` override (e.g. `osrs230_parse`) fills the
    RevPacket payload but the shared code, not the override, set
    `packet_type`. So osrs230 REBUILD_NORMAL arrived with `packet_type=0`, was
    treated as NONE, and its world-load branch never ran (the world that loaded
    was the offline-boot default). `net.c` now sets `packet.packet_type = name`
    before dispatching to `rev->parse`, so override-parsed packets route
    correctly. With that, REBUILD drives a full 9-chunk scene load.
  - **Serialization confirmed**: the exec FIFO already guarantees one packet
    handler completes before the next starts. Once packet_type was fixed, the
    trace shows REBUILD → 9-chunk world load → world_active=1 → REBUILD done →
    PLAYER_INFO applied (world_active=1) → player spawned — strictly ordered, no
    delay hack needed. (A prior mock `usleep` workaround was removed.)
  - Verified end-to-end: headless screenshot shows the player model (green
    top/olive legs) on the path with the camera orbiting it and the minimap
    tracking it. All net/UI regressions + lc254 offline boot green.

- **2026-08-04 — rev-239 (live OldSchool) protocol tables, login, and JS5;
  RuneLite reached this server.** Full write-up in
  [`RSPROT_OSRS239_PORT.md`](RSPROT_OSRS239_PORT.md); the parts that belong to
  this document's seams:
  - `GameProtoRevTable` gained **`opcode_smart2`**. Revision 239's server
    opcodes reach 148, and RSProt writes anything >= 0x80 as two stream-cipher
    bytes (`pSmart1Or2Enc`). `packetbuffer.c` grew `PKTBUF_READ_OPCODE_LOW` to
    park between them, because the first byte's ISAAC step cannot be replayed.
    A revision that needs this and leaves it 0 does not drop the high packets —
    it reads each one's second byte as the next opcode and never resyncs.
  - `src/net/rev/osrs239/` is **generated** (`tools/rsprot_gen_rev.py`), 158
    server prots and 101 client prots, and is the first table in the tree whose
    every number is real rather than partly assigned. `osrs230` stays as it is;
    it is what the regression suite runs on.
  - The 5.4 "entity-info op-list codec" item is still the largest gap and is now
    the *only* thing between this and a vanilla client in the world: PLAYER_INFO
    and NPC_INFO v5 are unwritten, so `osrs239_parse.c` refuses them (along with
    REBUILD_NORMAL/REGION V2, IF_SETEVENTS V2, IF_SETMODEL V2 and CAM_MOVETO/
    LOOKAT V2) rather than decoding them with the 230 layout.
  - **JS5 landed** as `src/net/mock/mock_js5.c` + `make -C src mock-js5`. It is
    a login-prot branch, not a service — one socket, opcode 14 game vs 15 JS5 —
    so it must move into `mock230_session`'s handshake; it is standalone today
    because that is the half testable against a real client now.
  - **Measured, not assumed**: the JS5 master index's layout was read off a live
    239 server with `tools/js5_probe.py` (25 archives of `p4 crc, p4 version`,
    no prefix), and three of our computed CRCs match live exactly.
  - **RuneLite was launched against it** and its vanilla client pulled 2,733
    groups / 7.9 MB before crashing in its own decode. The blocker is external:
    RuneLite ships a revision **240** client, while RSProt's newest module and
    every archived cache are 239.

- **2026-08-04 (later) — the server-side wire adapter; JS5 folded into the
  session.** `src/net/mock/mock230_wire.h` is the fourth vtable seam, shaped
  like `Mock230Transport` / `NetLoginVTable` / `GameProtoRevTable`: struct of
  function pointers, one instance per revision, a `_by_name` resolver, NULL
  slots meaning classic. Selected by `--rev osrs239` or `MOCK230_REV`;
  default osrs230, so anything that says nothing is unchanged.
  - `mock230_encode.c`'s 50-opcode enum became **canonical-name aliases**, so
    all 140 call sites are untouched and `mock230_send` resolves per revision.
    The packet capture records the RESOLVED opcode, which is what keeps the
    selftest's wire-number assertions meaningful — measured: the same 13
    pre-existing failures before and after, none new.
  - `payload` is a WHOLE writer set, not sparse overrides: a packet a
    revision's set does not name is refused rather than written with the other
    revision's layout. Necessary because payloads move even when sizes do not —
    IF_OPENTOP is 2 bytes at both revisions and `p2Alt1` vs `p2Alt2`; IF_OPENSUB
    is 7 bytes at both with its fields in the opposite order.
  - `zone_sub_code` is a separate slot from `opcode`: inside
    UPDATE_ZONE_PARTIAL_ENCLOSED, 230 uses the top-level opcode and 239 uses the
    ordinal of RSProt's IndexedZoneProtEncoder — a third numbering. Missing it
    is invisible at the frame level.
  - **Transcribe each writer from its own encoder.** Checking against RSProt
    found three of the first six wrong (VARP_SMALL's id order, VARP_LARGE's
    field order, UPDATE_STAT_V2 reordered entirely) — none of which has any
    downstream symptom.
  - JS5 moved into `mock230_session`'s handshake (opcode 15 beside 14), which
    is where it belongs since one socket carries both. That immediately exposed
    a real latent bug: mock230 did not ignore SIGPIPE, and a cache download is
    megabytes in a tight loop, so a client closing its update connection killed
    the process mid-write (exit 141, indistinguishable from a crash).
  - RuneLite re-run against the integrated server reached `JS5 session opened at
    revision 240` and then crashed in its own decode. Blocker unchanged and
    external: RuneLite ships a rev-240 client; the newest RSProt module and
    every archived cache are 239.

<<<<<<< HEAD
- **2026-08-06 — Manifests gained a lower-priority command-line layer.**
  - `[client:args]` carries repeated `arg=` entries, one exact argv token per
    line in file order. It deliberately reuses the browser query string's
    repeated-`arg` model instead of inventing a POSIX- or Windows-specific shell
    tokenizer: spaces, quotes, backslashes and punctuation remain literal.
  - Boot precedence is now typed manifest fields → manifest arguments → process
    argv. Each layer gets fresh positional slots, so an explicit cache directory
    or interface id replaces the manifest's positional value. Connectivity is
    also resolved per layer: an explicit `--offline` clears a manifest
    `--connect`, while an explicit `--connect` replaces manifest `--offline`.
  - Argument backing is fixed storage inside the process-lifetime
    `BootManifest`, because CLI values such as `--user`, `--connect`, and
    `--revconfig` can become `AppConfig` pointers that must outlive parsing.
    Empty entries are preserved like real argv values. Overflow beyond 64
    tokens is a load error, while a `--manifest` token is rejected contextually
    only when it occupies option position (so it remains legal as a password or
    another option's literal value).
  - The web boot preloader also inspects manifest arguments for `--revconfig`
    and `--revconfig-cache`, ensuring those CLI-named files exist in MEMFS before
    `main()`. Typed RevConfig keys remain manifest-relative; argument paths keep
    ordinary CLI/working-directory semantics.
=======
- **2026-08-04 (later still) — the 239 handshake and PLAYER_INFO v5.**
  - `mock230_session` reads the real 239 login block: `serverVersion`, the OTP
    discriminator, the XTEA body (username), and the RSA encryption-check byte.
    Verified against this repo's own 239 client — `login user='testc'` proves
    the RSA envelope, the four seeds and the XTEA decrypt all agree between two
    halves written from one spec.
  - Twelve packets now flow at real 239 opcodes and sizes (IF_OPENTOP 96,
    IF_SETEVENTS 108/16, VARP_SMALL 97, REBUILD_NORMAL 49, PLAYER_INFO 28, …).
  - **`serve()` memsets the server struct**, which erased the wire set at the
    call site — every login block was read as revision 230 and reported
    `rsa decrypt failed`, pointing at the key rather than at four bytes of
    `serverVersion`.
  - `mock239_playerinfo.c` writes the v5 stream (local player only), with a
    round-trip test against a decoder transcribed from RSProt's reference
    client. Mutation-tested. Correction it produced: an empty bit section emits
    zero bytes, so the four sections are not four markers on the wire.
  - **One fact, three consumers**: the local player's wire index is used by the
    init block, PLAYER_INFO, and the login response. The pool is 0-based and the
    client's table is 1..2047; `mock230_wire_local_index()` is the single
    definition. Found by arithmetic — REBUILD_NORMAL was 4616 bytes where 4614
    was predicted, and the gap is exactly one 18-bit entry.
  - **A bare `0x02` login response is a desync at 239**, not a short response:
    `LoginResponse.Ok` is 34 bytes behind a length, so the client swallowed the
    first 35 bytes of the login burst. Silent — the packets simply never arrive.
  - Process note: this selftest is **noisy** (11–14 failures per run) while the
    content submodule is being edited concurrently. A 3-run union is not enough
    to separate a regression from a flake; a back-to-back 5-run union against
    the same HEAD is. Measured that way, the whole change set is 13 vs 13.

- **2026-08-04 — which RuneLite is the rev-239 client, and NPC_INFO v5's shape.**
  - **1.12.33.** Both 1.12.34 and 1.12.34.1 ship a rev-**240** injected client,
    including the 1.12.34 tag whose own commits say `rev239` — those are content
    data updates, not the client revision. RuneLite republishes
    `injected-client` at the same coordinates when the game updates, so a source
    build at an old tag still pulls the current client. The only reliable way to
    learn a client's revision is to ask one; our JS5 server logs it.
  - 1.12.33 passes the revision gate (`JS5 session opened at revision 239`) and
    then crashes in its own init before any request (`cq.gh is null`). Ruled
    out: plugin-hub plugins, and a cache written by the 240 client.
  - `mock239_npcinfo_write_empty` — NPC_INFO v5 is ONE bit section, not four:
    8-bit high-resolution count, then 16-bit npc indices terminated by 0xFFFF.
    The index widened from 14 to 16 bits at this revision. The terminator is not
    optional when extended info follows: the client's loop consumes indices
    while the bit reader has bits, and that reader spans the rest of the packet.

- **2026-08-04 — appearance colocated: one vocabulary, every wire spelling,
  one encoding-independent decoder.**
  - The 12-int appearance buffer had drifted into three tellings of the same
    fact: `pkt_player_appearance.c` decoded only the classic block,
    `mock230_encode.c` had a second, hand-duplicated classic writer plus a
    separate 239 writer (`put_appearance_v5`), and every renderer
    (`entity_model_build.c`, the chathead build, the design preview, the
    uitree scene bridge, held-item swap) tested `>= 0x100` / `>= 0x200`
    against the wire's own numbers. Nothing was wrong yet — 230 and 239 never
    ran in the same process — but a third encoding, or a 239-tagged value
    reaching a classic-shaped test, would have decoded into a different, valid
    body part rather than failed.
  - All three now live in one file, `src/net/rev/packets/pkt_player_appearance.h/.c`:
    a canonical slot vocabulary (`Appearance_PackKit`/`PackObj`,
    `Appearance_SlotKind`/`SlotKit`/`SlotObj`), every wire tag next to the
    others (`APPEARANCE_WIRE_KIT_TAG` 0x100, `_OBJ_TAG_CLASSIC` 0x200,
    `_OBJ_TAG_V5` 0x800) behind `Appearance_WirePack`/`WireUnpack`, and an
    op-stream reader (`pkt_appearance_read`) in the same shape as
    `pkt_npc_info.c`'s `PktNpcInfoOp` — a revision's shape becomes a new
    reader function, not a branch in every consumer. `PktPlayerAppearance`
    is the folded form the client still applies; `PktPlayerAppearance_Decode`
    is now `DecodeAs(APPEARANCE_ENC_CLASSIC, ...)`, kept for every rev table
    that leaves `appearance_decode` NULL.
  - The canonical range moved off the wire's own numbers (`APPEARANCE_PACK_KIT`
    0x10000, not 0x100): the osrs239 cache ships 307 identity kits, and
    `0x100 + kit` collides with the classic obj range at kit 256 — not
    reachable today, but a canonical packing that equals one encoding's wire
    tag is exactly the bug this change exists to close off.
  - `mock230_encode.c`'s two writers now build from one `appearance_slots()`
    (the worn/covered/kit-fallback rule, stated once) and one
    `put_appearance_slots()` (the zero-byte-vs-two-byte framing, stated once),
    differing only in which `AppearanceEncoding` they pack against.
  - The two cache-sourced held-item override sites (`task_exec_entity_info.c`'s
    SEQ handling, `app.c`'s per-frame held-item swap) both read
    `SeqType.replaceheldleft/right`, which are classic-tagged appearance
    values from the cache regardless of which net revision is connected —
    routed through `Appearance_FromCacheValue` rather than compared to a
    literal at each site.
  - Verified: `make -C src test-entity-decode` (new cases: a same-content
    classic/239 pair decodes to an identical `PktPlayerAppearance`; the
    classic obj tag read under the 239 encoding lands as a *kit*, not an
    error — the silent-failure case this module exists to prevent; malformed
    customisation flags and truncated blocks are refused; 239 transmog ends
    the equipment array early), `test-mock239-playerinfo`,
    `test-mock230-embed`, `mock230_pack --check-only` (0 errors), and a
    headless `torirs --manifest manifest_osrs230_embed.ini` run
    (`SDL_VIDEODRIVER=dummy`) with `TORIRS_NET_CHEAT="equip 0;equip 1;..."`
    confirming the local player still renders equipped gear end to end.
>>>>>>> 5cc78a2898eaf81842f0042a51fce58c1e512f0c
