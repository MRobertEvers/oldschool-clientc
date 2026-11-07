# ToriRSEd implementation brief (branch v3-maped)

The design is `docs/MAPED_DESIGN.html` in this worktree (open it as text; the
section ids are `#verdict`, `#arch`, `#documents`, `#world`, `#windows`,
`#protocol`, `#chrome`, `#catalog`, `#terrain`, `#placement`, `#locbuilder`,
`#npcbuilder`, `#vars`, `#cs2`, `#save`, `#run`, `#web`, `#plan`, `#risks`).
This file answers what the design does not: **who owns which file in which
phase, the shared conventions every owner codes against, and how a phase is
checked and committed.** Read all of it before the first edit.

## 0. Where you are and what you may touch

- The worktree is `/Users/matthewevers/Documents/git_repos/3draster-maped`,
  branch `v3-maped` off `v3`. Every path below is relative to it. **Never
  touch `/Users/matthewevers/Documents/git_repos/3draster`** (the owner's
  checkout; other sessions build from it).
- `OSRS-Content` and `cache.osrs241` / `cache.osrs239` are symlinks into the
  owner's checkout. **Read only.** Never write a file under `OSRS-Content`,
  never run `make torirsserver-packs`, `torirsserver-scripts`,
  `torirsserver-servpack`, `sscompile` with an `--out` under it, or
  `cachepack pack` with an `--out`/`--server-out` under it. A test that needs
  to write content copies the files it needs into `src/build_opt/tmp/<test>/`
  (or a `mkdtemp` under `src/build_opt/`) and writes there.
- Build with `make -C src <target>`. Objects land in `src/build*/` inside this
  worktree, so builds are private by construction. `make -C src all` links
  `src/torirs` inside the worktree; that is fine here.
- Baseline (already verified in this worktree): `make -C src torirsmaped
  torirsmapedctl test-editor-cmd test-editor-jm2` builds and passes
  (`editor_cmd_test: 33 checks, 0 failed`; `editor_jm2_test: 2949 squares,
  5898 files checked, 0 failed`).
- Keep command output small: pipe builds through `tail -40`, tests through
  `tail -20`. Never `ls -R`, never `cat` a generated file.
- CLAUDE.md applies (asserts for contract violations, allocation failures are
  asserts, no `switch` in a protothread, tree-walk markers, no mutation of
  source to prove a test fails except in a throwaway worktree).
- Do not commit. The phase integrator commits (section 6). Do not run
  `git stash`, `git checkout -- <file>` on a file you do not own, or anything
  that rewrites the index beyond `git add` of your own files.

## 1. One owner per file, per phase

Nobody edits a file another owner holds in the same phase. If you need a
change in someone else's file, write it under `cross_owner_requests` in your
report with the exact diff you want; the integrator applies it. "Just adding
one function" to a shared file is how a parallel phase loses an edit.

Shared files are closed in every parallel phase unless a row below names you:
`src/makefile`, `src/app.h`, `src/app.c`, `src/main.c`,
`src/app/app_frame.c`, `src/ui/uitree.h`, `src/engine/torirs_types.h`,
`src/torirsserver/torirs_server.h`.

### Phase 1 — foundations (one agent: `foundations`)

| file | what |
|---|---|
| `src/makefile` | `-include $(wildcard $(SRC_DIR)/maped/*.mk)` right after the `platform_check.mk` include (line ~97); `torirsmaped` and `torirsmapedctl` added to `all`; `test-editor` added to the test aggregate that lists `test-editor-ghost` (line ~1936). Nothing else in this file, ever again: later owners add targets in their own `src/maped/<owner>.mk`. |
| `src/maped/foundations.mk` | your own targets (new tests) |
| `src/torirsmaped/*` | protocol v2 (section 3): `HELLO` v2 with capabilities, `DOC_OPEN`/`DOC_CLOSE`/`FACT_DOC_TEXT`, kind-tagged `CMD`, `SAVE(kind,key)`, `FACT_SAVED` carrying (kind,key) rows, `DOC_CREATE(kind, key, storage, template)` for a NEW square (design §9b: storage is the content tree's `maps/` or a lane directory `ported/<lane>/maps/`; template empty / copy-of / flat fill; the server creates the document in the authority plus its `.filepack` line and broadcasts the filling commands; refused when the key exists in that storage), v1 `SQUARE_OPEN` kept as an alias. Reserve the frame and state-key ranges in section 3 as comments so later owners do not collide. |
| `src/editor/editor_cmd.{c,h}` | `struct Editor_Cmd` gains `doc_kind`, `doc_key[128]`, and the kinds `SPAWN`, `SPAWN_SUPPRESS`, `CFG_FIELD`, `CFG_BLOCK`, `IF_FIELD`, `IF_COMPONENT`, `TEXT_SPLICE` with their before/after unions (section 3 fixes the layouts). Apply dispatches on `doc_kind`; TILE/LOC/SPAWN/SPAWN_SUPPRESS/TEXT_SPLICE apply here; CFG and IF kinds call `Editor_CfgApply` / `Editor_IfApply`, which you declare in `editor_doc.h` and implement as **plain text splices on the document text** for now (phase 2 and 3 owners replace the bodies with real codecs; the signatures are yours and fixed). |
| `src/editor/editor_doc.{c,h}` | the document set becomes kind-keyed: squares (as today), spawn documents, text documents (`kind, key, text, length, dirty`). `Editor_DocOpen(kind,key)`, `Editor_DocFind`, `Editor_DocHasUnsaved`. Keep `Editor_Doc` heap-only. |
| `src/editor/editor_spawn.{c,h}`, `src/editor/test/editor_spawn_test.c` | the `.spawn` codec: `==== NPC ====`/`==== OBJ ====` sections, rows `name x z level [count]`, comment lines preserved verbatim, and the new suppression row `-name x z level`. Round-trip test over every generated file under `OSRS-Content/osrs-content/server/scripts/areas/world/configs/` (972 files) must be byte-exact. |
| `src/editor/editor_host.{h}`, `editor_host_local.c`, `editor_host_remote.c`, `src/editor/editor.{c,h}` | generic blob load/save for documents by (kind,key) → path under the content root; the server resolves paths (`maps/m<x>_<z>.jm2`, `server/scripts/areas/edited/configs/m<x>_<z>.spawn`, `configs/all.<type>`, `interfaces/<name>.if`, `scripts/<name>.cs2`, any `server/scripts/**` path for TEXT). The client never names a path outside the content root. |
| `src/torirsserver/torirs_server_content.c` | `.spawn` loader: a row whose first character is `-` records a suppression keyed on (name, x, z, level); after every file is merged, suppressed spawns are removed. One pass, no behaviour change for files without `-` rows. Add a selftest-free unit check in `src/torirsserver/test/` if one exists for the loader; otherwise a test target in your `.mk` that loads a temp spawn tree through the loader's public entry. |
| `src/app/app_map_editor.c` | only what the renamed/extended editor API forces |

Done when: `make -C src all torirsmaped torirsmapedctl test-editor test-spawn-codec` (your new target) passes; `editor_jm2_test` still reports 0 failed; an embed test opens a square via `DOC_OPEN` and via the v1 alias and gets identical text.

### Phase 2 — four owners in parallel, then `integrate-2`

**`edit-world`**

| file | what |
|---|---|
| `src/engine/world_builder/edit/**` (new) | the EDIT layout: resident build maps, stable placement handles, tile-local invalidation, `flush` |
| `src/engine/world_builder/world_builder.h`, `world_builder.c`, `world_terrain.u.c`, `world_scenery.u.c` | `struct WorldBuilderOps` vtable (design §4) and the STATIC implementation of it, which must be a pure refactor: STATIC behaviour is unchanged byte for byte (the existing world tests prove it) |
| `src/app/app_map_editor.c`, `src/app/app_world_load.c` | `Editor_DrainRebuilds` → `Editor_Flush` path: a `FACT_CMD` TILE calls `ops->tile_set` with one derived tile; LOC calls `loc_place`/`loc_remove`; flush once per frame |
| `src/editor/editor_derive.{c,h}` | a per-tile derive (`Editor_TileDerive`) beside the per-square one |
| the manifest parser for `[editor:boot]` (find it: `grep -rn "content_dir" src/app.c src/app.h`) | a `[world:boot] layout=static|edit` key and `--world-layout` CLI flag; default `static`; the two map-editor manifests set `edit` |
| `src/engine/world_builder/test/edit_world_test.c`, `src/maped/edit-world.mk` | the identity test: a scripted sequence of random tile and loc edits applied through EDIT, then a full STATIC rebuild of the same authored squares, must produce the same element models (vertex-wise) and positions |

**`chrome`**

| file | what |
|---|---|
| `src/ui/uitree_debug_overlay.{c,h}` | capacities 64 panels / 4096 widgets / 16384 prims; new kinds `TREE`, `VLIST`, `TOOLBAR` (`ICONBUTTON`), `SLIDER`, `RADIO`, `MODAL`, `TOOLTIP`, `CODE`, `GRAPH` (a CUSTOM well variant with hit keys), `SPLITTER`/docking on the WINDOW style; truncation logged once per widget serial |
| `src/ui/torirs_chrome_exec*.{c,h}`, `torirs_chrome_surface_sdl.c`, `torirs_chrome_shell.{c,h}` | commands and intents for every new kind (the web and browser executors render from the command stream) |
| `src/plugin_chrome/runtime-source.js`, `runtime.js`, `modern.css`, `src/web/torirs_chrome.js` | DOM rendering of the new kinds |
| `src/engine/torirs_chrome_skin_baked.{c,h}`, `src/ui/torirs_chrome_skin.h`, `res/plugin_chrome/skin/` | new baked slots (design §7 table): probe with `3rd/rscache/tools/spritebake/spritebake --rev osrs241 cache.osrs241 --list` / `--probe`; synthesize with `tools/chrome_button_stamp.py` where no sprite fits; regenerate the PNGs with `tools/spritebake_png.py` |
| `src/maped/chrome.mk` | `chrome-skin` (checks the 2x/3x skin and font tiers are what `tools/chrome_skin_tiers.py` / `tools/chrome_font_tiers.py` bake, no cache needed; docs/CHROME_SCALE.md), `chrome-skin-rebake` (re-runs the recipe from the baked file's header against the cache, then the tiers) and a sync test per new widget kind in `src/ui/test/` |

Rule: a widget never holds a pointer the caller owns beyond the frame (the dropdown options trap, `docs/chrome_executors.md`). Labels 63 bytes, text 191 bytes, unchanged.

**`catalog`**

| file | what |
|---|---|
| `src/torirsmaped/torirs_maped_catalog.{c,h}` (new), `src/torirsmaped/torirs_maped.c` (dispatch only) | the index (design §8): `pack/*.pack`, `configs/all.<type>.compack`, `[name]` blocks in `configs/all.<type>` and `server/scripts/**/configs/*.<type>`, `interfaces/*.compack`, `.cs2` headers, `.rs2` trigger headers, `maps/`; `CATALOG_QUERY` with paging and `key=value` filters; `CATALOG_RECORD` with per-field provenance; mtime refresh. Also the **square atlas** (design §9b): for every `maps/m<x>_<z>` and every lane's `maps/`, its storage, the world-map area that claims it (parse `worldmap/areas/*.wma` and `*.wmc`; "none" for unmapped instance regions), has-locs, spawn count, and a thumbnail colour averaged from the level-0 underlay/overlay ids of its `.jm2` (resolve ids to rgb through `configs/all.underlay` / `all.overlay`); served as `FACT_CATALOG_SQUARES` on `CATALOG_SQUARES`. |
| `src/editor/editor_host_remote.c`, `editor_host.h` (catalog calls only) | `Editor_HostMapEdCatalogQuery/Record` and the fact sink |
| `src/torirsmaped/test/catalog_test.c`, `src/maped/catalog.mk` | every name in every pack is found by exact search; index build time printed |

**`cfg-docs`**

| file | what |
|---|---|
| `src/editor/editor_cfg.{c,h}`, `src/editor/test/editor_cfg_test.c` | block-level codec for `[name]` / `key=value` config text (cache `all.<type>` and server overlays): parse to an ordered key list, emit only the touched block, every other byte preserved; round-trip over every `configs/all.*` and every `server/scripts/**/configs/*.{loc,npc,obj,varp,varbit,enum,struct,param,inv}` must be byte-exact |
| `src/editor/editor_cmd.c` (CFG arms only), `editor_doc.{c,h}` (CFG document type) | replace phase 1's splice bodies of `Editor_CfgApply` with the codec |
| `src/editor/editor_alloc.{c,h}` + test | id allocation mirroring `tools/ss_allocate.py`: next id per namespace from `pack/<ns>.alloc`, varps in the server headroom, membership lines `pack/<ns>.client|server`, compack lines; emitted as commands, never as direct writes |
| `src/editor/editor_vars.{c,h}` + test | varp bit map over its varbits, overlap check, `varp<id>_<name>` / `varb<id>_<name>` naming |
| `src/maped/cfg-docs.mk` | targets |

**`integrate-2`**: builds `all torirsmaped test-editor` plus every phase-2 target, applies cross-owner requests, fixes link and merge faults, runs the world tests the STATIC refactor must keep green (`grep -n "^test-world" src/makefile` for the list), commits (section 6).

### Phase 3 — four owners in parallel, then `integrate-3`

**`views`**

| file | what |
|---|---|
| `src/main.c` (flags only), the manifest parser | `--editor-view`, `--maped tcp://host:port`, `--maped-group N`, `--camera x,z,level,yaw,pitch,zoom`; viewer boot opens no game connection |
| `src/editor/editor_view.{c,h}` (new) | the viewer window's thin toolbar (level cap, layer toggles, follow selection, camera presets, "open panels"), per-view state, `STATE_CAMERA_<conn>` publish and follow |
| `src/platform/launch_sessions.{c,h}` | a `view` verb that spawns the host binary with the editor manifest and the flags above |
| `src/web/torirs_host.js`, `src/web/index.html` | `?editor_view=1&maped=wss://…&group=N` boot; "spawn view" opens such a tab |
| `src/torirsmaped/*` (SPAWN_VIEW only) | `REQ_SPAWN_VIEW` → launch service on the server host |
| `src/maped/views.mk` | a three-connection embed test: one stroke, three identical mirrors, three different cameras |

**`tools-panels`**

| file | what |
|---|---|
| `src/editor/panels/**` (new) | the main window's panel set on the phase-2 chrome: menu bar, tool rail (`TOOLBAR`), terrain brushes with `SLIDER` radius/falloff/step, underlay/overlay palettes with swatches and shape thumbnails from `world_decode_tile`'s tables, flags, level `RADIO`, rectangle select, loc multi-select/nudge/rotate/reshape/duplicate, spawn place/delete/suppress, inspector, output console, `MODAL` save/discard. **The square picker** (design §9b): a `GRAPH` well with one tab per world-map area plus Unmapped, squares drawn as thumbnail cells from the atlas, zoom, hover name, click opens, shift-click adds, right-click Open in new view / Copy to… / Delete, a search field taking `m50_50`, `50 50`, an absolute tile, or an area name; open squares outlined, dirty in accent, every view's window framed from `STATE_VIEW_SQUARE`. **The New button**: a `MODAL` asking the coordinates and the storage (content `maps/` or a lane directory, lanes listed, offer to create one) and the template (empty, copy of selection, flat fill), then `DOC_CREATE`; the storage question alone serves Copy to… and Save as new. Each panel module exports `Editor_<Panel>_Register(struct EditorUi*)`. |
| `src/editor/editor_ui.{c,h}` (new) | the registry of panels; integrate-3 wires every module's register call here |
| `src/editor/editor_panel.{c,h}`, `map_editor_ghost.*`, `loc_editor_selection.*`, `src/app/app_map_editor.c`, `src/app/app_loc_editor.c` | replace `editor_panel.c` with the modules; keep ghost and selection semantics (handles, not element ids) |
| `src/maped/tools-panels.mk` | tests |

**`builders`**

| file | what |
|---|---|
| `src/editor/builder/**` (new) | `.interaction` INI model under `server/scripts/edited/<subject>/`; loc emitter (`oploc1..5`, `oplocu`, `aploc*`), NPC emitter (`opnpc*`, `apnpc*`) and the dialogue graph → nested `if`/`else` + labels (design §11, §12 node table); step templates; multiloc/multinpc shell editor model; `sscompile --dry-run` runner (the binary from `make -C src sscompile`, `--out` under `src/build_opt/tmp/`); panel modules `Editor_LocBuilder_Register`, `Editor_NpcBuilder_Register`, `Editor_ShellEditor_Register` |
| `src/editor/test/builder_test.c`, `src/maped/builders.mk` | a generated Talk-to for a test NPC compiles under `sscompile --dry-run` against a temp copy of the scripts tree; emitter output is stable (golden files under `src/editor/test/golden/`) |

**`iface-cs2`**

| file | what |
|---|---|
| `src/editor/editor_if.{c,h}`, `src/editor/test/editor_if_test.c` | `.if` + `.compack` codec; round-trip over all 982 interfaces byte-exact (this settles `layer=` and the `type=` set mechanically) |
| `src/editor/editor_cmd.c` (IF arms only) | replace phase 1's splice bodies of `Editor_IfApply` |
| `src/editor/iface/**` (new) | component `TREE`, inspector grouped as `ToriRS_Component` groups them, structure ops, hook picker, CS2 runner (`CreateTask_CS2RunMixed`), trace panel (`TORIRS_CS2_TRACE` to a panel), varc/varp/varbit watch, `.cs2` `CODE` editor with compile via `3rd/rscache/tools/cs2 compile` (declines shown, never a silent `.cs2b`); `Editor_Iface_Register`, `Editor_Cs2_Register` |
| `src/app/app_iface_editor.c` (new) | mount as root/sub (`App_OpenRootInterface`, `App_OpenSubInterface`), pick a node in the view, re-fill one node on `IF_FIELD` (`UITree_FillBuildFromToriRS`) |
| `src/maped/iface-cs2.mk` | targets |

**`integrate-3`**: wires every `*_Register` into `editor_ui.c`, builds `all` with the editor manifest, runs every editor test, fixes faults, commits.

### Phase 4 — two owners in parallel, then `integrate-4`

**`live-server`** (the game server and the game client)

| file | what |
|---|---|
| `src/torirsserver/torirs_server_control.{c,h}` (new), `torirs_server_main.c`, `torirs_server_ws.c` (first-byte select only) | the control link (section 4): first byte `0xED` on the game port, gated by the `io_gate` ticket when `TORIRSSERVER_GATE=1`, else loopback only |
| `src/torirsserver/torirs_server_scripts.c` | hot reload per `docs/serverpack.md` §"The hot-reload contract": load the new manifest into a shadow table under the shared pack lock, refcount old script objects for parked/queued instances, swap at the tick boundary; `ToriRSServer_ScriptsLoad`'s once-per-world guard becomes shadow-then-swap |
| `src/torirsserver/torirs_server_servpack.c`, config tables | re-read changed `(index, archive)` groups, replace records at the tick boundary |
| `src/torirsserver/torirs_server_scene.{c,h}`, `torirs_server_world.c` | `ToriRSServer_SceneReloadSquares(list)` from the test hook; `rebuild_pending` for every player whose window covers them; `reload=1` on the region rebuild |
| `src/torirsserver/torirs_server_content.c` | spawn reload for the edited lane only: diff and despawn/spawn |
| `3rd/rsprot` spec INI, `src/net/**`, `src/game/rs_gameproto_exec.c`, cache provider eviction, `src/web/torirs_idb.js` | `CACHE_INVALIDATE(index, count, archives…)` server→client (since 2026-11-07 a control frame behind the control escape, `src/net/torirs_control.h`, not an RSProt prot): evict from sparse dat2 and IndexedDB, re-prime the reference table, evict decoded records, re-open mounted interfaces in the list, re-resolve on-screen records, normal rebuild for maps |
| `src/torirsserver/test/`, `src/maped/live-server.mk` | a player parked in `p_pausebutton` across a reload finishes the old dialogue and gets the new one on the next click; a square reload sends one REBUILD to the covering player |

**`live-editor`** (the editor server and the main window)

| file | what |
|---|---|
| `src/torirsmaped/torirs_maped_build.{c,h}` (new) | `BuildRunner` vtable; native binding spawns `make -C src torirsserver-packs` and `cachepack pack` (outputs under the live cache dir the lane names, never under `OSRS-Content`); `BUILD(what)` → `FACT_BUILD_LINE`/`FACT_BUILD_DONE` |
| `src/torirsmaped/torirs_maped_live.{c,h}` (new), `torirs_maped.c` (dispatch) | `LiveLink` vtable: launch (torirsserver + js5_server + io_server, owned lifetime) or attach by address; speaks the control protocol of section 4; `RUN` = SAVE_ALL → BUILD packs → BUILD client cache → RELOAD → `FACT_LIVE_STATUS` |
| `3rd/rscache/tools/cachepack/cp_incremental.c` and the pack driver | the client pack reuses the server pack's unit model: repack only changed types, write only changed archives, bump the reference table |
| `src/editor/live/**` (new) | Live panel: players, followed player's vars via `SUBSCRIBE`, cheat line, teleport-to-selection, stale list; `Editor_Live_Register`; `STATE_LIVE_FOCUS` |
| `src/maped/live-editor.mk` | a mock control-link peer test for RUN's sequencing and failure stop |

**`integrate-4`**: end-to-end on the launched lane: edit a dialogue line, RUN, a logged-in scriptrun bot sees the new line on the next Talk-to without relog; edit a tile height, RUN, the client window rebuilds. Writes `docs/MAPED_STATUS.md` (what is built, what is not, every open issue from every report), commits.

## 2. Command layouts (fixed in phase 1, used by everyone)

```c
enum Editor_DocKind { EDITOR_DOC_SQUARE = 0, EDITOR_DOC_SPAWN, EDITOR_DOC_CFG, EDITOR_DOC_IFACE, EDITOR_DOC_TEXT };

struct Editor_Cmd
{
    enum Editor_DocKind doc_kind;
    char doc_key[128];            /* "m50_50" | "m50_50" (spawn) | "loc:newbie_door1" | "if:bank" | "rs2:edited/doors/my_door.rs2" */
    enum Editor_CmdKind kind;     /* TILE, LOC (unchanged) ; SPAWN, SPAWN_SUPPRESS, CFG_FIELD, CFG_BLOCK, IF_FIELD, IF_COMPONENT, TEXT_SPLICE */
    int has_before, has_after;
    union
    {
        struct { int level, x, z; struct Editor_Tile before, after; } tile;
        struct { struct Editor_Loc before, after; } loc;
        struct { int is_obj; char name[64]; int abs_x, abs_z, level, count; } spawn;          /* before = has_before, after = has_after */
        struct { char key[64]; char before[192]; char after[192]; } cfg_field;               /* "" = absent */
        struct { char name[64]; char new_name[64]; } cfg_block;                                /* add: has_after; remove: has_before; rename: both */
        struct { int component; char key[64]; char before[192]; char after[192]; } if_field;
        struct { int component, parent_before, parent_after, index_before, index_after; } if_component;
        struct { uint32_t offset; uint32_t before_len, after_len; char text[1024]; } text_splice; /* before text then after text, back to back */
    } u;
};
```

Wire: `REQ_CMD` payload = `u32 doc_kind, u32 key_len, key bytes, u32 cmd_kind, u32 has_before, u32 has_after, u32 payload_len, payload` — every int as u32/i32 little-endian, every string length-prefixed (`u32 len, bytes`), no fixed 144-byte form. A mirror that receives an unknown `cmd_kind` applies nothing and sends `DOC_OPEN` for the key to resync.

## 3. Protocol v2 frame and state-key reservations

`TORIRSMAPED_PROTO_VERSION = 2`. Keep every v1 number. New numbers by owner, so parallel owners never collide:

| owner | requests | facts | state keys |
|---|---|---|---|
| foundations | 17–40 (`DOC_OPEN 17, DOC_CLOSE 18, SAVE 19, SAVE_AS 20, DOC_CREATE 21`) | 139–160 (`FACT_DOC_TEXT 139`) | 6–9 (`STATE_VAR_PREVIEW 6`) |
| catalog | 41–48 (`CATALOG_QUERY 41, CATALOG_RECORD 42, CATALOG_SQUARES 43`) | 161–168 (`FACT_CATALOG_PAGE 161, FACT_CATALOG_RECORD 162, FACT_CATALOG_SQUARES 163`) | — |
| views | 49–52 (`SPAWN_VIEW 49`) | 169–172 | 10–39 (`STATE_CAMERA_BASE 10` + connection index) |
| live-editor | 53–64 (`BUILD 53, RUN 54, LIVE_ATTACH 55, LIVE_DETACH 56, LIVE_CHEAT 57, LIVE_SUBSCRIBE 58`) | 173–190 (`FACT_BUILD_LINE 173, FACT_BUILD_DONE 174, FACT_LIVE_STATUS 175, FACT_LIVE_LINE 176, FACT_LIVE_VARS 177`) | 40–47 (`STATE_LIVE_FOCUS 40`) |

`FACT_HELLO` v2 payload: `u32 version, u32 writable, u32 client_id, u32 capabilities` with bits `1 build, 2 live, 4 catalog, 8 spawn_view`; the server sets the bits for the bindings it has. A v1 client (version 1 in HELLO) is answered with v1 `FACT_HELLO` and the v1 frame set only.

## 4. The game server control link (used by live-server and live-editor)

On the game port, first byte `0xED` selects the control link (`'G'` is HTTP, `15` is JS5, the login opcodes are the game). Frames `[u32 type][u32 length][payload]`, little-endian, same as ToriRSMapEd. Strings are `u32 len, bytes`.

| req | payload | answer |
|---|---|---|
| `CTL_HELLO 1` | `u32 version=1, string ticket` (empty when no gate) | `CTL_FACT_HELLO 129`: `u32 version, u32 ok, string server_name, u32 players` |
| `CTL_STATUS 2` | — | `CTL_FACT_STATUS 130`: `u32 players, u32 tick, string script_pack_stamp, string servpack_stamp, u32 stale_count, stale rows (string path)` |
| `CTL_RELOAD 3` | `string script_manifest_path, u32 n, n × (u32 index, u32 archive), u32 m, m × (i32 map_x, i32 map_z), u32 k, k × string spawn_file` | zero or more `CTL_FACT_LINE 131` (string), then `CTL_FACT_RELOAD_DONE 132`: `u32 status, u32 scripts_swapped, u32 groups_replaced, u32 squares_reloaded, u32 spawns_changed, u32 restart_needed_count, rows (string reason)` |
| `CTL_CHEAT 4` | `string player, string line` | `CTL_FACT_LINE` rows, then `CTL_FACT_STATUS` |
| `CTL_SUBSCRIBE 5` | `string player, u32 n, n × u32 varp_id` (n=0 unsubscribes) | `CTL_FACT_VARS 133` whenever a value changes: `string player, u32 n, n × (u32 varp_id, i32 value)` |
| `CTL_BYE 6` | — | — |

Reload applies at the next tick boundary; the answer is sent after it applied. `restart_needed` lists `fields/`, `content.ini`, `.constant` text and symbol-table changes the server cannot take live.

## 5. Report format (every agent, every phase)

Return structured output with these fields; nothing else is read:

- `summary` — what was built, in a paragraph.
- `files_changed` — list of paths you created or edited.
- `build_targets` — the exact `make -C src …` lines you ran, each with pass/fail.
- `tests` — each test binary run, its one-line result.
- `cross_owner_requests` — list of `{file, owner, diff_or_instruction}` for files you were not allowed to edit.
- `open_issues` — anything unfinished or uncertain, with the file:line.

Never report a target as passing without having run it in this worktree. If a build fails and you cannot fix it inside your ownership, say so under `open_issues`; do not edit around it in someone else's file.

## 6. Integrators

An integrator owns every file for the duration of its step. It: applies the phase's `cross_owner_requests`; builds `make -C src all torirsmaped torirsmapedctl test-editor` and every `src/maped/*.mk` target the phase added; runs the tests; fixes compile, link and semantic conflicts between owners (prefer the change that keeps both owners' tests green); runs `make -C src check-pt-switch` and `check-tree-walks`; then commits **everything** on `v3-maped` with a message of the form

```
maped phase N: <one line>

<what each owner delivered, one bullet each; open issues carried to the next phase>

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
```

and returns the report of section 5 with the commit hash in `summary`. It never pushes.
