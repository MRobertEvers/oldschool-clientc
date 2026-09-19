# quest-driver: file ownership, contracts and build

Read `docs/QUEST_DRIVER_DESIGN.md` (the fixed decisions) and
`docs/QUEST_DRIVER_PLAN.md` (the mechanism, with a file:line for every seam)
first. This file answers one question the other two do not: **who may edit
what**, so seven people can build this at once without a merge ever deciding
anything.

`src/plugin/torirs_plugin_drive.h` is the contract. It is the one file every
builder reads all of.

---

## 0. The rule

**One owner per file. Nobody edits a file another owner holds.** If you need a
change in someone else's file, say so in your report; the Final agent applies
it once. A second owner "just adding one function" to a shared file is how
this lands as a merge conflict that silently drops a stamp.

Shared files are listed in §5 with the reason each is closed.

---

## 1. Ownership table

### core-events

| file | what |
|---|---|
| `src/app/app_plugin_drive_events.c` | the ring. **Already written** (see §6) -- you own the stamps, not a rewrite |
| every `DRIVE_STAMP:` marker in the tree | `grep -rn "DRIVE_STAMP:" src` -- 18 sites, one per row of plan §3, already marked with the payload each must carry |
| `src/app/app_canvas_layout.c` | `App_PluginLayoutTick`: the ring's publication fence and (step 11) `TORIRS_WIDGET_LOADED`/`CLOSED` |
| `src/game/rs_chat.h`, `src/game/rs_chat.c` | the serial field on `struct RS_ChatMessage`, assigned in `RS_Chat_AddMessage` |
| `src/app/app_ui_host.c` | the `container_id` `app_inv_ui_host_change` discards today |
| `src/main.c` | the `t.finish` exit fence, in the same branch as `max_frames` (`:1527, :2016`), through `PluginDrive_Finished` |

### core-scheduler

| file | what |
|---|---|
| `src/plugin/torirs_plugin_drive.c` | the module, the scheduler, `await`, the symbol trampolines, `cheat`/`settled`/`finish`/`ledger`, the chunk composition |
| `src/plugin/torirs_plugin_lua.c` | the three `PluginLua_Thread*` stubs, and nothing else |
| `src/plugin/task_plugin_io.c` | the part concatenation (**already written**, see §6) |
| `src/game/content_test.c` | `TORIRS_QUEST_SCRIPT`: no mailbox, step the virtual clock while an await is armed, hold while a shot is pending. The hook is `PluginDrive_ClockWantsStep` |
| `src/torirsserver/torirs_server_world.c` | `ToriRSServer_RunDebugprocForTest` -- return the verdict `:7787-7792` throws away |
| `script/plugins/quest_driver/core.lua`, `script/plugins/quest_driver.lua` | |

### core-state

| file | what |
|---|---|
| `src/plugin/torirs_plugin_drive_state.c` | `DriveState_*` |
| `script/plugins/quest_driver/state.lua` | `var.*`, `inv.*`, `msg.*`, `skill` |

### core-revconfig

| file | what |
|---|---|
| `src/revconfig/*` | `any(...)` and `derive=` grammar |
| `src/ui/uitree_role.{c,h}`, `src/engine/uitree_role_load.c` | the matcher list and the `fallback` hook |
| `src/app/app_role_derive.c` | `App_RoleDeriveFallback` |
| `tools/revconfig_roles_from_pack.py` | generator + `--check` (stub exists, exits 2) |
| `revconfig/osrs239/osrs239_dat2_roles.gen.ini` (new) and the `[iface:]` sections | generated roles go in a **separate** file chained by the existing loader (`src/engine/uitree_role_load.c:206-221`), never into the hand-edited ini |

The makefile target `check-revconfig-roles` is already added and calls your
script. You do not edit `src/makefile`.

### verbs-chat

| file | what |
|---|---|
| `src/plugin/torirs_plugin_drive_chat.c` | `DriveChat_*` |
| `script/plugins/quest_driver/chat.lua` | `chat.continue_`, `drain`, `close`, `count`, `name_entry`, `kind`, `options`, `options_title`, `choose` |
| `src/game/varc_ids.h` | `meslayer_mode` + its per-revision mapping |

### verbs-read

| file | what |
|---|---|
| `src/plugin/torirs_plugin_drive_read.c` | `DriveRead_WidgetModel` |
| `script/plugins/quest_driver/read.lua` | `chat.text/head/name/item/expect_*`, `scroll.*`, `levelup.*` |
| `src/plugin/torirs_plugin_host.h`, `src/plugin/torirs_plugin_bridge.u.c`, `src/plugin/torirs_plugin_contract.h` | the `PLUGIN_WIDGET_MODEL` request, plus its one `plugin_api.meta.lua` line (§5) |

### verbs-pointer

| file | what |
|---|---|
| `src/plugin/torirs_plugin_drive_pointer.c` | `DrivePointer_*`, including the projectors and `App_MinimenuRowFind`'s logic -- **put them here, not in `src/app/app_plugin_api.c`**, which two other owners would otherwise also be editing |
| `script/plugins/quest_driver/pointer.lua`, `script/plugins/quest_driver/world.lua` | |
| `src/game/rs_minimenu_world.{c,h}` | export `opnpc/oploc/opobj_action_for_slot` |
| `src/plugin/torirs_plugin_bridge.u.c` (the `app_plugin_world_op` case only) | coordinate with verbs-read, who owns the rest of that file -- see §5 |
| `src/app/app_net.c`, `src/game/rs_if1_buttons.c` | click-outcome counters |

### verbs-ui

| file | what |
|---|---|
| `src/plugin/torirs_plugin_drive_ui.c` | `DriveUi_*` |
| `script/plugins/quest_driver/ui.lua` | `ui.*`, `npc.*`, `t.key/text/shot` |
| `src/app/app_plugin_api.c` | `App_SetCameraPose` (shared with `content_test.c:511-520`), `App_LocalPlayerIdle`, the mount-liveness helper -- **you are the only owner of this file** |

### tests

| file | what |
|---|---|
| `test/quests/*.lua`, `test/quests/fixtures/*` | |
| `tools/quest_gate/run.py`, `tools/quest_gate/gate.py` | stubs exist, both exit 2 |

The `test-quests` target is already in the makefile.

---

## 2. The C contracts

All of them are in `src/plugin/torirs_plugin_drive.h`, grouped by owner with a
banner per group. The shape is uniform and not negotiable:

- every verb returns `enum DriveResult`; `DRIVE_OK` is the only success;
- `struct App* app` is never NULL: **assert it**, never `if(!app) return`;
- an `out_*` pointer is never NULL: assert it;
- `not_found` is for a name the CONTENT does not have, which is a legitimate
  runtime answer a test asserts on. A NULL pointer is not that.

Three project rules bite here (`CLAUDE.md` is authoritative):

1. **One `assert()` per condition.** Never `assert(a && b)` -- the message has
   to name the parameter that was wrong.
2. **An allocation failure is an assert**, not an `if`.
3. **No `switch` between `PT_BEGIN` and `PT_END`.** `task_plugin_io.c`'s boot
   task is a protothread and the composition loop added to it is an `if`/`for`
   for exactly this reason. `make -C src check-pt-switch` must print `total 0`.

### The result set

`ok timeout not_found refused covered no_row not_visible closed unsupported`
-- fixed by the design doc. A test reads these strings; adding one is a design
change, not an implementation detail.

### Naming

**No numeric interface or component ids and no client op strings** in driver C,
driver Lua or tests. Numbers appear only inside a `match=` expression or a
generator's output. **Never key on a lane name** -- not in C, not in Lua, not
in a test. A verb that needs to know which lane it is on has found a missing
engine fact, and the fix is to ask the engine, not to branch.

---

## 3. The Lua surface

`api.drive` is **one flat table of 54 primitives**, assembled from six arrays
in six files. The Lua half composes the readable verbs (`chat.continue_`,
`player.talk_to`, `var.expect`) on top of it.

Each group file defines

```c
static struct LuaFn const LUA_DRIVE_<GROUP>_FNS[] = { {"name", lua_drive_name}, ... };

void PluginDrive<Group>_RegisterLua(struct lua_State* L, void* script)
{ PluginLua_AppendModule(L, script, LUA_DRIVE_<GROUP>_FNS); }
```

and `torirs_plugin_drive.c` calls the six registrars in a fixed order.

- **Names must be unique across all six arrays.** A duplicate lets the later
  registration silently win; `make -C src test-plugin-lua` fails it by name.
- Every name must have a matching `---@field` line in the `torirs.DriveApi`
  class in `script/plugins/plugin_api.meta.lua`, and vice versa: the inventory
  test compares the two sets exactly (54 == 54 today).
- A thunk raises a Lua error on a bad argument (`PluginDrive_ArgInt` and
  friends `luaL_check`), because a test that passes a string where an id
  belongs is a broken test and must stop, not get `not_found` back.
- Every thunk returns `PluginDrive_PushResult(L, result, detail)` -- two
  values -- unless the meta line says it returns a plain value.

### The chunk

The sandbox has no `require`, so the driver is **one chunk** built by the C
loader from, in order:

```
quest_driver/core.lua  state.lua  chat.lua  read.lua  pointer.lua  world.lua  ui.lua
quest_driver.lua                                    <- the manifest's source=, LAST
```

Two rules follow and both are silent when broken:

- **No part but `quest_driver.lua` may have a top-level `return`.** A top-level
  return ends the chunk and the files after it never run.
- **Only `core.lua` declares chunk-scope locals.** Everything else adds to
  `QD`. Chunk-scope locals are registers in one Lua function and there are 200
  of them for all eight files.

`quest_driver.lua`'s `on_start` counts the namespaces and logs
`quest-driver: 12 namespaces present`. If it ever logs `EMPTY namespaces:`,
a part failed to read and the rest of the run is a lie.

### The sandbox

No `coroutine`, no `pcall`, no `load`, no `require`, no `setmetatable`. The
budget hook must be re-armed on the **coroutine's own** `lua_State` at every
resume: `lua_newthread` copies the parent's hook only at creation
(`3rd/lua/lstate.c:280-284`). A busy-loop test chunk with no await must still
be killed -- that is the gate that proves the re-arm is on the right state.

---

## 4. Decisions I made that the plan does not state

These came out of the tree, not out of preference. Each is a thing a builder
would otherwise have to decide alone, differently.

**A1. The pump runs from `on_frame_start`, not from the content-test pump, and
there is one pump per frame, not two.**
D8 wants two pumps because a `FRAME_START` pump cannot see events stamped
later in the same frame. But the ring is a **cursor** read, not a drain: a
pump at `app_frame.c:569` reads everything appended since its own last read,
including everything stamped after it last frame. Nothing is lost; an await
resolves at most one frame (20 virtual ms) later than it could. Against a
6-server-tick deadline -- 180 frames -- that is nothing, and it buys:
no second callback fence, no `PluginHost_DriveEvents` entry in the plugin ABI,
and a coroutine that resumes inside a live api scope so every existing
`api.*` verb works inside a quest test. Revisit only if a gate shows the frame
matters; if it does, the fix is a second pump call at the layout fence, which
is core-events' file anyway.

**A2. There is no `PluginHost_DriveEvents`.**
The host has no App and getting one would mean a test-only entry in
`struct ToriRS_PluginEngine`, which every plugin-host unit test then carries.
The cursor read is `App_DriveEventsRead(app, after_serial, out, cap, &count,
&serial)` and the driver holds the App from `PluginDrive_Init`.

**A3. The driver's six `.c` files include BOTH `app.h` and `lua.h`.**
This is the deliberate exception to "the one TU that includes lua.h". The
alternative -- a vtable between the Lua thunks and the engine -- splits every
verb across two files and two owners for no gain, since the driver is
client-only and test-only and never enters a host unit-test link. The makefile
gives them the `-I$(LUA_DIR)` rule (`DRIVE_LUA_SRCS`).

**A4. `struct LuaFn` moved to `torirs_plugin_lua.h`**, spelled with
`int (*)(struct lua_State*)` so that header still needs no `lua.h`
(`app_internal.h` includes it). The runtime reaches the driver through one
function pointer, `PluginLua_SetTestModules`, so the host's eight test
binaries link with no driver symbol in sight.

**A5. `TORIRS_PLUGIN_MANIFEST` is relative to `script/`.**
It is `plugins/quest_driver.ini`, **not** `script/plugins/quest_driver.ini`
(`task_plugin_io.c:57-64`, `PLUGIN_MANIFEST_DEFAULT_PATH`). The plan and the
task text both spell it the long way; that path does not load and the client
starts with no plugins at all, which looks exactly like a driver that failed
to register. `tools/quest_gate/run.py` must use the short form.

**A6. The ring is in every build, unconditionally.**
A stamp guarded by an env var is a stamp that rots. It is 16 KB on `struct App`
and a struct store per event.

**A7. Drive events carry four ints and no text.** The per-kind payload table is
in the header. `chat_message` carries `(type, serial)` only -- the text is read
from `app->chat.messages` by the state group, which needs the whole ring
anyway. If a kind needs a fifth number, split it into two events; do not widen
the record.

**A8. The quest test chunk is loaded by C**, not by the driver's Lua: `load`
is not in the sandbox. C reads `TORIRS_QUEST_SCRIPT`, `luaL_loadbuffer`s it
onto a new thread and resumes it. It is a separate chunk from the driver's, so
it cannot see `QD` as an upvalue -- the driver passes the verb table in as
`run(t)`, which is the shape the design doc already specifies.

**A9. Anything a verb needs from `struct App` that does not exist yet is a
prototype in `torirs_plugin_drive.h` under your own banner and a definition in
your own `.c`.** Do not add prototypes to `src/app.h`. I added exactly one
field there (the ring) and closed the file.

---

## 5. Shared files, and why each is closed

**`src/app.h`** -- closed. It now carries `struct App_DriveRing drive_events;`
and includes `plugin/torirs_plugin_drive.h`. Almost every TU depends on it, so
one edit rebuilds the tree for everybody; and two builders appending
prototypes to it is the conflict this whole document exists to prevent. Need a
field? Report it.

**`src/makefile`** -- closed. Every driver file is already in `SRCS`, the six
Lua-including TUs are in `DRIVE_LUA_SRCS` with their own rule, and
`check-revconfig-roles` and `test-quests` exist. A new source file means a new
owner, which means a new architect pass.

**`src/plugin/torirs_plugin_drive.h`** -- shared, fenced. Edit **only inside
your own owner's banner**, and only to change a signature you own. Changing a
shared type (`enum DriveResult`, `struct App_DriveEvent`, `enum DrivePickKind`)
is a report line, not an edit.

**`script/plugins/plugin_api.meta.lua`** -- shared, fenced. The
`torirs.DriveApi` class is laid out in six labelled blocks, one per owner. Edit
only the lines under your own label, and only together with the matching
`LUA_DRIVE_*_FNS` row -- the inventory test compares both sets exactly, so a
one-sided edit fails `make -C src test-plugin-lua` immediately. verbs-read also
owns the one `PLUGIN_WIDGET_MODEL` line elsewhere in that file.

**`src/plugin/torirs_plugin_bridge.u.c`** -- two owners by exception:
verbs-read owns the `PLUGIN_WIDGET_MODEL` case, verbs-pointer owns
`app_plugin_world_op`. They are in different parts of a 6000-line file and
neither touches the other. If you find yourself editing outside your own
addition, stop and report.

**`src/game/content_test.c`** -- core-scheduler only. Its one new call is
`PluginDrive_ClockWantsStep`.

**`src/app/app_plugin_api.c`** -- verbs-ui only (A9 moved everything else out).

---

## 6. What is already written (do not rewrite it)

| file | state |
|---|---|
| `src/app/app_plugin_drive_events.c` | the ring, `DriveResultName`, `DriveEventKindName/FromName` -- **complete**. core-events owns the stamps and the layout fence, not this |
| `src/plugin/task_plugin_io.c` | the part concatenation -- **complete and proved** (12 namespaces present at boot) |
| `src/plugin/torirs_plugin_lua.{c,h}` | `struct LuaFn` moved out, `PluginLua_SetTestModules`, `PushModule`/`AppendModule` -- complete. The three `PluginLua_Thread*` functions are `assert(0)` stubs and are core-scheduler's |
| `src/plugin/torirs_plugin_drive.c` | registration, composition, arg/result helpers, `finish` -- complete. `await`, `pump`, `events`, `symbol*`, `cheat`, `ledger`, `settled`, `ClockWantsStep` are stubs |
| the other five `torirs_plugin_drive_*.c` | every seam and every thunk stubbed, returning `DRIVE_UNSUPPORTED`; arrays and registrars complete |
| `script/plugins/quest_driver.ini`, `quest_driver.lua`, `quest_driver/*.lua` | the chunk, the namespaces and the boot-time namespace count |
| `script/plugins/plugin_api.meta.lua` | all 54 verbs documented |
| `src/plugin/test/plugin_lua_api_inventory_test.py` | extended: `@testonly` modules, the `LUA_DRIVE_*_FNS` union, the duplicate-name check, and `quest_driver/*.lua` in the script scan |
| `src/app/app_role_derive.c` | stub |
| `tools/quest_gate/{run,gate}.py`, `tools/revconfig_roles_from_pack.py` | stubs that **exit 2**. A gate that prints nothing and returns 0 is worse than a red one |

Stubs return `DRIVE_UNSUPPORTED` rather than asserting, so the client boots
with the driver loaded while the work is in flight. `PluginLua_Thread*` do
assert, because nothing may reach them until they are real.

---

## 7. Build and test

Always a **private objdir and target**, never the shared ones:

```sh
W=/Users/matthewevers/Documents/git_repos/3draster-quest-driver
L=<yourlabel>                      # core-events, verbs-chat, ...

mkdir -p $W/src/build_qd_${L}_opt_es
make -C $W/src OPT=1 EMBED_SERVER=1 \
     PLATFORM_OBJ_BASE=build_qd_$L PLATFORM_TARGET=torirs_qd_$L torirs_qd_$L
```

**Warming your objdir from another one: use `cp -Rp`, never `cp -R`.**
`cp -R` stamps every object with the time of the copy, so make sees objects
newer than sources it has never compiled and builds nothing. I lost a build to
exactly this: the binary linked, the driver loaded, and `api.drive` was nil
because `app.o` predated the change that installs it. If a build ever
disagrees with its source, delete the object -- or the objdir -- and rebuild.
Do not trust make to notice (GNU Make 3.81 here compares whole seconds).

Gates:

```sh
make -C $W/src check-pt-switch
make -C $W/src test-plugin-lua PLATFORM_OBJ_BASE=build_qd_$L
make -C $W/src torirsserver-scripts          # before ANY embedded run; a stale
                                             # pack is a refusal, never a pass
make -C $W/src check-revconfig-roles         # core-revconfig makes this green
make -C $W/src test-quests                   # tests makes this green
```

Headless run (one private session directory, never the repo's own `saves/`,
`preferences.ini` or `plugin_prefs.ini`):

```
TORIRS_CONTENT_TEST=<session>        TORIRS_QUEST_SCRIPT=<test.lua>
TORIRS_PLUGINS=1                     TORIRS_PLUGIN_MANIFEST=plugins/quest_driver.ini   # A5
TORIRS_PLUGIN_LOG=1                  TORIRS_PLUGIN_PREFS=<session>/plugin_prefs.ini
TORIRS_PREFS=<session>/preferences.ini
TORIRSSERVER_SAVES=<session>/saves   TORIRSSERVER_STAFF_LEVEL=2
TORIRSSERVER_HOME=3222,3218          TORIRSSERVER_TUTORIAL_HOME=3222,3218   # skip Tutorial Island
TORIRSSERVER_CONTENT=<root>/OSRS-Content/osrs239-content
TORIRSSERVER_SCRIPTS=<root>/OSRS-Content/osrs239-content/server/scripts/build
TORIRSSERVER_CACHE=<root>/cache.osrs239
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy TORIRS_STDERR_UNBUFFERED=1
argv: --manifest <rewritten manifest_osrs239.ini> --user <name> --pass test --soft3d --window 765x503
```

The manifest is `manifests/manifest_osrs239.ini` with `[cache:boot] dir=` and
`[net:boot] transport=embed` rewritten, exactly as `tools/content_selftest.py:62-94`
does it. **The embedded world does not tick until login completes**, and with
`TORIRS_CONTENT_TEST` set and no mailbox command the virtual clock does not
advance at all -- which is what `PluginDrive_ClockWantsStep` exists to fix.

**Mutation checks go in a throwaway worktree**, never here:

```sh
git worktree add --detach "$SCRATCH/mut" HEAD
cp <the uncommitted files the check needs> "$SCRATCH/mut"/...
( cd "$SCRATCH/mut" && <mutate> && make -C src PLATFORM_OBJ_BASE=build_mut <gate> )
git worktree remove --force "$SCRATCH/mut"
```

Several sessions build from this checkout; a mutation here can be compiled by
another build in the same second as the restore and never look stale again.

---

## 8. Things the plan does not say that you need

- `App_PluginLayoutTick` is called **twice** per frame (`app_frame.c:940` and
  `:2166`). Anything core-events puts at that fence runs twice; make it
  idempotent or gate it.
- `on_logic_tick` fires **before** the packet pump (`app_tick.c:68`), not
  after. It is not a post-layout fence and cannot serve as the second pump.
- The plugin host is built in `App_New` (`app.c:506-525`) and `PluginDrive_Init`
  is called there, after `PluginRegistry_RegisterAll` and before any script
  compiles. `TORIRS_PLUGINS` outranks the manifest in both directions.
- Scripts arrive as **bytes through the IO queue**, never as paths
  (`task_plugin_io.c`). Anything that wants a file at boot goes through
  `ToriRS_IO_QueueScript`; `TORIRS_QUEST_SCRIPT` is the one deliberate
  exception (A8) and is read once, at thread creation, from the driver.
- `plugin_lua_test.c` compiles a hard-coded list of 17 bundled scripts and
  asserts the count. `quest_driver.lua` is deliberately **not** in it -- it is
  not a standalone chunk. Do not add it.
- The `assert()`s in this tree vanish under `NDEBUG`, which `OPT=1` sets. A
  parameter named only by an assert is unused in the shipping build; every
  stub here carries the `(void)` casts already. Keep them when you fill a stub
  in only if the parameter is still unused.
