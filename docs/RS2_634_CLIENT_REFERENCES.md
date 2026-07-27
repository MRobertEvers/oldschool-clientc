# RS2 rev 634 — sources, and every place it disagrees with OldSchool

Everything the client does differently for `manifest_void634.ini` traces back to one
of the sources below. This file is the register: what was read, where it lives, and
what it settled. Nothing here is inferred from a rendered frame — a wrong field
width and a right one both produce *an* image, so each entry names the artefact that
decided it.

`cache.void634` is Void's modified **rev 634** cache. The profile calls itself 643
(`[cache:boot] revision=643`) because the two revisions share every layout checked
so far — the interface corpus decodes with exact consumption in both (45,817
components in `cache.rs643`, 41,082 in `cache.void634`). Where this document says
"634" it means the layout family, not the revision number.

---

## 1. Sources

### 1.1 The deobfuscated rev-634 client

`~/Documents/git_repos/634-client/client/src` (identical to
`~/Documents/git_repos/Void_RS2011Client_Deob/client/src`). Obfuscated: classes are
`ClassNNN`, methods `methodNNNN`, fields `aTypeNNNN`. **This is the authority for
anything about field layout or stack arity** — where it and a server-side decoder
disagree, it wins.

| What | Where | Settled |
| --- | --- | --- |
| IF3 widget decode | `Class46.method433(Class348_Sub49, boolean)` | The whole rev-634 IF3 layout (§2) |
| — its call site | `Class320.java:30` — `method433(buffer, true)` | The `bool` parameter is `true`, so all five trigger tables read normally |
| Hook/script-arg reader | `Class46.method432` | `u8 count`, then per entry `u8 type` → 0 = `i32`, 1 = string |
| Trigger int-array reader | `Class46.method441` | `u8 count`, then `count` × `i32` |
| Target-mask gate | `Class348_Sub40_Sub5.method3060(mask, bool)` → `(mask >> 11) & 0x7F` | Which widgets carry the 6-byte target triplet (§2, the bug that started this) |
| Type-5 sprite build | `Class46.method443` | Meaning of outline / shadow / flip / colour, and that flag bit 1 is *not* a draw flag |
| Sprite flip primitives | `Class207.method1514` (swaps rows) / `method1518` (swaps within a row) | First flip byte is **vertical**, second is **horizontal** |
| Sprite pack decode | `Class207.method1517` | Per-pixel alpha is kept verbatim; there is no "non-zero index ⇒ opaque" step (§4) |
| Sprite archive load | `Class207.method1521(archive, id, 0)` | Sprites are one group per id in table 8, as in OldSchool |
| Widget draw loop | `Class348_Sub40_Sub7.method3064` + the type dispatch around line 300 | Clip nesting; tiling is flag bit **0** (`aBoolean697`), which is what the draw path loops on |
| Widget visibility predicate | `client.method111` | `hidden` is the only per-widget gate |
| Op-cursor lookup | `Class100.java:86` | `anIntArray706` is indexed by op slot |
| CS2 command dispatch | `Class66` (`method710`, `method711`, and the range blocks it chains to) | Every opcode arity in §3 |
| — CC/IF aliasing | `Class66.java:2729`, `:3225`, `:3438` — `if (i >= 2000) { i -= 1000; class46 = lookup(pop int) }` | The `IF_` form of a `CC_` op pops one extra int (the component id) **before** the op's own args |
| Struct list archive | `Class65`'s constructor — `aClass45_1141.method407(0, 26)`, over `Class95.aClass45_1541 = method3571(…, 2, …)` | Structs are **table 2, group 26** (OldSchool: group 34) |
| CS2 int/string stacks | `Class66.anIntArray1149` / `anInt1173`, `aStringArray1152` / `anInt1170` | The pop/push idioms the arity extraction counts |

### 1.2 Void's rev-634 server

`~/Documents/git_repos/Void_RS2011Server` (Kotlin). Authority for **what the server
sends**, and a clean cross-read for cache layout. It is not authority for field
semantics — see the `imageRepeat` note in §2.

| What | Where | Settled |
| --- | --- | --- |
| Login interface burst | `game/src/main/kotlin/content/entity/player/modal/GameFrame.kts` — `list` + `openGamframe`, fired from `interfaceOpen("toplevel*")` | Which sub-interfaces open at login, and in what order |
| What opens the root | `engine/…/data/AccountManager.kt` `spawn()` → `player.open(interfaces.gameFrame)` → `"toplevel"` | Interface **548** is the fixed-mode root |
| Interface name → id | `data/entity/player/modal/**/*.ifaces.toml` | The ids in `[ui:gameframe]` |
| Interface type → slot | `data/entity/player/modal/interface_types.toml` (`fixedIndex`) | Which component of 548 each one mounts into |
| How a slot resolves | `engine/…/data/definition/InterfaceDefinitions.kt` — `pack(parent, index)`, parent defaulting to `toplevel` | Bare entries target the root; `parent = "chat_box"` is why chat_background is qualified |
| Cache index numbers | `cache/src/main/kotlin/world/gregs/voidps/cache/Index.kt` | Table ids for the promoted config types |
| Shard widths | `cache/…/definition/decoder/*Decoder.kt` — `getArchive`/`getFile` | Files per group per type (§5) |
| IF3 decode (cross-read) | `cache/…/definition/decoder/InterfaceDecoderFull.kt` | Confirms §2 field-for-field, apart from the type-5 flag-bit labels |
| Selected-tab variable | `data/entity/player/modal/tab/tab.varcs.toml` (varc 168, list) + `content/…/modal/Tab.kt` | The tab list and its `Inventory` default |

### 1.3 Reproducing the derivations

Byte-exact consumption over a whole corpus is the check that a layout is right; a
rendered frame is not. The scratch tools used here decoded every interface in a
cache and compared consumed-vs-size per component. Equivalent in-tree harnesses:

```
make -C 3rd/rscache test          # includes the interface round-trip suite
make -C src test-cs2-dialect      # opcode translation
```

Opcode arities were extracted mechanically from `Class66` by counting the stack
idioms, then hand-checked against the source for every id that ended up in the
overlay — the mechanical count over-reports by one on the very common
`if (c) push; else { push; return; }` shape, so it is a candidate generator, not an
answer.

---

## 2. IF3 widget layout: 634 vs OldSchool

Implemented in `decode_if3_rs2()`, `3rd/rscache/src/datatypes/dat2_component.c`,
selected by `RSCache_Dat2ComponentDecodeRevFromProfile` (RS2-on-dat2 ⇒ the 634
family). The OldSchool path is untouched.

**The symptom.** Before this, 106 of interface 548's 225 components decoded short —
type-0 widgets by exactly 6 bytes — and the whole gameframe was built from
mis-assigned geometry, zero hooks, and zero var triggers. `pack_components=225
onloads=0 inv_hooks=0 var_hooks=0`.

| Field | rev 634 | OldSchool | Note |
| --- | --- | --- | --- |
| leading byte | **version**; `255` ⇒ `-1` | 255 marker, discarded | A non-negative version adds fields in six places. Every file in a 634-era cache reads `-1`; decoded anyway so a cache that uses it cannot silently desync. |
| `type` | `u8`; **bit 7 flags a trailing string** | `u8` | `type &= 0x7f` then read the string |
| width/height | `u16` for every type | `i16` for type 9, `u16` otherwise | |
| hidden | bit 0 of a flag byte (bit 1 = no-click-through when version ≥ 0) | `byte == 1` | |
| type 0 | scrollW `u16`, scrollH `u16`, noClickThrough `u8` | same | |
| type 3 | colour `i32`, filled `u8`, alpha `u8` | same | |
| type 4 | font, text, lineHeight, hAlign, vAlign, shadow, colour, **+ alpha `u8`** | no alpha byte | |
| type 5 | graphic `i32`, angle `u16`, flags `u8`, alpha `u8`, outline `u8`, shadow `i32`, **vflip `u8`, hflip `u8`**, **colour `i32`** | flips ordered **hflip, vflip**; no trailing colour | Flip order settled by `Class207.method1514/1518`. The previous 643 branch had them swapped. |
| type 5 flags | bit 0 = **tiling**, bit 1 = an unused draw-cache key | bit 0 = tiling | Void's decoder labels bit 1 `imageRepeat`; the client's draw path tiles on bit **0** (`aBoolean697`), so the client wins |
| type 6 | modelId `u16`, **flag byte** selecting one of two viewport blocks (or neither), seq `u16`, `[wm≠0]` `u16`, `[hm≠0]` `u16` | fixed offsets/angles/zoom/seq/ortho/shorts | Completely different shape |
| type 9 | lineWidth `u8`, colour `i32`, direction `u8` | same | |
| **key bindings** | a chain of `(index nibble, 12-bit modifier, repeat, code)` records after the click mask, terminated by a zero lead byte | absent | |
| ops count byte | low nibble = op count, **high nibble = cursor-override count** | whole byte = op count | |
| cursor overrides | up to two `(slot u8, cursor u16)` pairs | absent | |
| **option override** | a string between the ops and the drag fields | absent | Stored in `opBase` |
| **target triplet** | when `(clickMask >> 11) & 0x7F ≠ 0`: three `u16` | absent | **The missing 6 bytes.** `Class348_Sub40_Sub5.method3060` |
| hooks | **20** | 18 | The extra two are varc and varcstr, appended at the end |
| trigger tables | **5** (varp, inv, stat, varc, varcstr) | 3 | |

Validation: **41,082 / 41,082** components in `cache.void634` and **45,817 /
45,817** in `cache.rs643` consume exactly. `cache.osrs230` (25,310),
`cache.osrs239` (26,478) and `cache.kronos` (25,393) are unchanged.

---

## 3. CS2: every 634-vs-OldSchool discrepancy

Three separate mechanisms, because the divergences are of three different kinds.

### 3.1 Renumbered — translated at script-copy time

`CS2_OpcodeTranslate`, `src/engine/cs2_opcode_dialect.c`. Same command, different
wire number; the canonical handler is correct once the id lines up.

| 634 wire | Canonical | Command |
| --- | --- | --- |
| 51 | 60 | `SWITCH` (OldSchool reused 51 for `GET_VARC_LONG`) |
| 4500 | 6516 | `STRUCT_PARAM` — same stack shape, pops `(struct, param)`, pushes int or string by the ParamType |

### 3.2 Same id, different command — diverted to a stubbed signature

`g_cs2vm2_opcode_stack_rs2` in `src/cs2vm2/cs2vm2.c`. An id listed here skips the
canonical handler entirely (`CS2VM2_RunOp` checks the overlay before its switch):
running the wrong command is worse than running none, so these become no-ops with
the right pop/push counts and the stack stays balanced. Signatures are
`(int_in, str_in, int_out, str_out)`.

| Op | 634 | Canonical | What 634 means |
| --- | --- | --- | --- |
| 202 | 1,0,0,0 | `CC_FINDROOT` 0,0,1,0 | Remove a widget from its group array |
| 1122 | 1,0,0,0 | — | Set the type-5 flag bit 1 |
| 2122 | 2,0,0,0 | — | `IF_` form of 1122 |
| 1311 | 1,0,0,0 | `CC_SETOPSUBMENU` 2,1,0,0 | One-int widget setter (`anInt713`) |
| 2314 | 2,0,0,0 | `IF_SETTARGETPRIORITY` | One-int widget setter (`anInt719`), `IF_` form |
| 2703 | 1,0,1,0 | — | Count of a widget's dynamic children |
| 3316 | 0,0,1,0 | `STAFFMODLEVEL` | same |
| 3323 | 0,0,1,0 | `PLAYERMOD` | same |
| 3329 / 3335 / 3340 | 0,0,1,0 | — | client-state getters |
| 3351 | 0,0,3,0 | — | three mouse-button booleans in one call |
| 3609 | 0,1,1,0 | `FRIEND_TEST` 1,0,1,0 | takes a **name string**, not an int |
| 3619 | 0,1,0,0 | `CLAN_JOINCHAT` 1,0,1,0 | takes a name string, pushes nothing |
| 4124 | 2,0,0,1 | — | `(value, comma-group)` → formatted number. OldSchool's 4124 takes **no** arguments (script 5031 in `cache.osrs239` calls it on an empty stack) — the clearest proof the two numberings diverge above 4000 |
| 4125 | 1,1,1,0 | — | `(font, text)` → rendered width |
| 5003 / 5004 / 5010 / 5011 / 5012 / 5019 / 5024 | see table in source | `CHAT_GETHISTORY_*` etc. | At 634 this whole range is a **friend/ignore-record accessor family** — every one takes a list index and reads a field off the record (`s.method3985(index)`). Overridden even where the counts happen to agree, because the command is different. |
| 5056 | 1,0,1,0 | — | array length or 0 |
| 5102 | 0,0,1,0 | — | key-down test |
| 5420 | 0,0,1,0 | — | |
| 5424 | 11,0,0,0 | — | Chat-scrollbar skin: four geometry values, five sprite ids the client preloads, two more |
| 5428 | 2,0,1,0 | — | |
| 5504 / 5505 / 5506 | 2,0,0,0 / 0,0,1,0 / 0,0,1,0 | `CAM_FORCEANGLE`, `CAM_GETANGLE_XA/YA` | same commands, previously unsignatured |
| 5507–5510 | 0,0,0,0 | — | no-arg actions |
| 5547 | 0,0,1,0 | — | |
| 6506 | 1,0,**4,3** | `WORLDLIST_SPECIFIC` | 634 pushes 4 ints + 3 strings; `cache.osrs239` script 6918 stores 4 ints + **2** strings. Different tuple. |
| 6510 / 6900 | 0,0,1,0 | — | |

### 3.3 Same id, different command — needs real behaviour

| Op | Handling |
| --- | --- |
| **100** `CC_CREATE` | 634 pushes `(parent, type, index)`; OldSchool added a fourth `isnested` flag. Gated inside `CS2VM2_Op_CC_Create` — popping the flag under RS2 steals the parent id and every dynamic child fails to create. This is what left interface 548 at 225 components instead of 511. |
| **1613** `CC_GETPARAM` | Reads a param off the active widget, and the ParamType decides int-vs-string, so a fixed stack signature cannot express it. A version −1 widget carries no param table (the two id-keyed side tables in `Class46.method433` are `version >= 0` only), so the answer is always the ParamType default — which is exactly `STRUCT_PARAM` with struct `-1`. Forwarded there. Claimed the vendor placeholder `_1613`; nothing in the OldSchool numbering uses it. |

### 3.4 Surveying

`TORIRS_CS2_SURVEY=1` downgrades the unimplemented-opcode abort to one report per
opcode, so a bring-up produces the whole missing list in one run instead of one
entry per rebuild. The run afterwards is **not** trustworthy — a no-op with the
wrong arity corrupts the stack — it is a survey tool only.

Note that a survey only finds ids with *no* signature. It cannot see an id that has
a canonical signature meaning a different command; those need the arity diff in
§3.2, which is why that was done from the client rather than from a run.

---

## 4. Sprites: the alpha channel

`RSCACHE_SPRITELOAD_FLAG_OPAQUE_INDEX`, `3rd/rscache/src/datatypes/dat2_sprites.c`.

OldSchool 232+ forces every pixel with a non-zero palette index to fully opaque
*after* reading any `FLAG_ALPHA` payload — RuneLite dropped the `else` that used to
make the two exclusive. Older clients keep the channel: `Class207.method1517` reads
the alpha plane and discards it only when every byte is `0xFF`.

Applying the newer rule to a 634 cache turns every soft-edged asset into a hard
block. Interface 548's tab-flash overlays (sprites 1840/1841) and the summoning-orb
glow (1796) all ship an alpha ramp, and drawing them opaque painted **solid yellow
over both tab strips** — the most visible symptom of the whole exercise, and one
that looked like a decoder fault while the decoder was fine.

The flag is now era-selected (`RSCache_Dat2SpriteFlags`) and threaded through
`ToriRS_SpriteFromDat2Archive` from the cache profile.

---

## 5. Where config records live

`RSCache_RecordAddressFor`, `3rd/rscache/src/rscache_profile.c`. RS2 promotes
several config types out of table 2 into their own sharded tables; ids come from
Void's `Index.kt`, shard widths from its decoders' `getArchive`/`getFile`.

| Type | OldSchool | RS2 | Files per group |
| --- | --- | --- | --- |
| loc | table 2 group 6 | table **16** | 256 |
| enum | table 2 group 8 | table **17** | 256 |
| npc | table 2 group 9 | table **18** | 128 |
| obj | table 2 group 10 | table **19** | 256 |
| seq | table 2 group 12 | table **20** | 128 |
| spotanim | table 2 group 13 | table **21** | 256 |
| varbit | table 2 group 14 | table **22** | **1024** |
| struct | table 2 group **34** | table 2 group **26** | — (still a config group) |

loc/npc/obj/seq/spotanim were already mapped. This pass added **enum**, **varbit**
and **struct**:

- Enums silently failed 100× per boot (`Failed to decode dat2 enum config group`),
  which is why several gameframe scripts produced nothing.
- Varbits reported `config group absent; varbits will read 0` — every varbit read 0
  and every script branching on one took the zero path. The sharded form needs
  *every* group, not one, so the load now walks the table's reference table:
  **8,736** types.
- Structs looked healthy (group 34 exists in `cache.void634`) but holds only 100
  files, so any id past that fell back to a param default. The task-complete
  overlay rendering with a blank body instead of "The Essence of Magic" was the
  tell.

---

## 6. The client is not the whole frame

A gameframe root is a set of empty slots. Every panel in it — chat box, orbs, the
sidebar tabs, the inventory — arrives from the server as a separate `IF_OPENSUB`
right after the player spawns, together with the var writes the frame's scripts
branch on. An offline client therefore renders correct chrome around nothing, and
that is not a bug in the client.

Two manifest sections state that burst instead of inventing a client-side default,
so the values stay traceable to the server that actually sends them
(`manifest_void634.ini`; both are skipped when networked, since a real server sends
the real thing):

- `[ui:gameframe]` — `component = interface`, or `parent_interface:component =
  interface` for a slot on an already-mounted interface. In file order.
- `[ui:varc]` — `varc = value`.

**Known gap.** `chat_background` (137, `752:9`) is the one entry from Void's list
that is not mounted: it blanks the entire frame — 137's root resolves to a
full-canvas opaque layer over everything. The tree still builds (511 components, no
failed scripts), so this is a layout/size-mode problem in the nested mount, not a
decode one.

**Recorded, not acted on.** Void writes varc **168** (the selected sidebar tab,
default `Inventory` = 4) at login, and the manifest seeds it — but nothing in
`cache.void634`'s interfaces triggers on 168, so it currently changes no pixels.
The tab strip's own triggers are varcs 232–245 and 822–824. Kept because it is
correct state and records where the tab variable lives.
