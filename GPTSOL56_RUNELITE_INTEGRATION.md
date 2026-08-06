# RuneLite 239 / mock230 interface integration

This is the durable investigation and verification record for hardening the
compilable RuneLite 1.12.33 / OSRS revision-239 deob against `mock230`.

The authoritative client source is:

`/Users/matthewevers/Documents/git_repos/Deobfuscator/src_osrs239_rl1_12_33`

The runnable instrumented source is:

`/Users/matthewevers/Documents/git_repos/Deobfuscator/instr/src`

The RuneLite checkout is:

`/Users/matthewevers/Documents/git_repos/runelite`

## Verification contract

An isolated C self-test is supporting evidence, not proof of integration.
Interface work is complete only when the rebuilt authoritative client logs in
through RuneLite, receives and decodes the relevant server packets, accepts
controlled input through JCTL, emits the expected client packet or CS2 effect,
the server observes and handles it, and the resulting framebuffer is captured.

Artifacts for the current run live under `build/run239/proof/`.

## Completion status

The requested interface-parity queue is complete. A third clean launch of the
normal, verified deob jar passed the expanded pre-launch `EVENTS` contract. It
proved the full mounted action census, Friends/Ignore, skill-guide lifecycle,
run pose 824, exact inventory contents and menus, model-bearing item sprites,
world-map zoom and close, music play/playlist/unlock behavior, emote Perform and
Loop, and Account Benefits open/close. The final report is
`build/run239/proof/interface_verify.log`; its first line is:

```
RuneLite 239 full interface parity contract: PASS
```

No requested interface-parity item remains open. The broader mock world still
has unrelated content/combat/pathing self-test failures; they are not counted
as interface success and are not hidden by this contract.

## How the authoritative CS2VM works

`Statics.method4464` is the interpreter entry. It resolves ScriptEvent
arguments into three local banks (int, object/string, and long), resets the
three operand-stack pointers, fetches from the current `class43` script, and
runs until top-level opcode 21 (return) or the operation budget is exhausted.
Opcodes below 100 execute in this loop. Opcodes at or above 100 are passed to
`Statics.method6889`, whose revision-239 range ladder dispatches widget, client,
social, database, and world-map opcode families to their host handlers.

The VM is not sufficient by itself for interfaces. The server must also drive
the interface lifecycle and the client must provide the host state on which
the higher opcodes operate.

## Server-side interface component map

The integration requires these cooperating components:

1. Exact revision-239 packet opcodes, lengths, smart-opcode framing, and field
   transforms (`src/net/rev/osrs239`, `mock230_wire`, `mock230_servercodec`).
2. Interface lifecycle packets: `IF_OPENTOP`, `IF_OPENSUB`, `IF_CLOSESUB`, and
   full interface rebuild handling.
3. Mutable interface packets: `IF_SETEVENTS`, text/model/object/animation/head,
   hide, colour, position, and scroll changes.
4. `RUNCLIENTSCRIPT` argument typing/order and the clientscript cache supplied
   over JS5.
5. Client-visible state feeding CS2: varps/varbits, stats, inventories, social
   state, and their transmit rings.
6. Input arming and return traffic: `IF_SETEVENTS` range lookup, menu creation,
   `IF_BUTTONX`/target packets, inbound decoding, and trigger dispatch into the
   server-script VM.
7. Tick ordering: state updates, transmit hooks, script execution, interface
   mutations, entity updates, and `SERVER_TICK_END` must arrive in the order
   the client assumes.
8. A real-client harness: rebuild/API gate, stable launcher, autologin, JCTL
   input, bidirectional packet telemetry, CS2 telemetry, screenshots, and
   artifact assertions.

## Major findings

### The live client path is operational

The rebuilt deob passed `instr/build.sh` and `instr/tools/verify_api.py` (740
classes; API surface complete), then RuneLite reached `LOGGED_IN`. The first
captured frame is `build/run239/proof/01_login.png` (765x503, SHA-256
`e04c0f40e9c683eea77dee4d97964aa5e5e8acd01d9f63b85f5c4837b272f6ef`). It
shows the gameframe, scene, minimap, inventory, tabs, orbs, and chatbox.

The client telemetry decoded the server's interface open sequence, including
`IF_OPENTOP` opcode 96, repeated `IF_OPENSUB` opcode 7, and
`IF_SETEVENTS_V2` opcode 108. One measured event range arrived as component
`712:3`, sub-range `0..7`, `events1=0x1e`, `events2=0xf`.

### Static protocol audits are useful but not sufficient

The authoritative deob and the local revision tables agree on all 117
client-to-server protocol rows. The server-to-client audit agrees on every real
opcode/size; the sole extra table row has opcode `-1` (`SCRIPTEDPROJ_CHANGE`),
which is an unavailable sentinel and must never be emitted.

The outbound builder audit currently reports five canonical builders with no
revision-239 mapping: `EVENT_TRACKING`, `IDK_SAVEDESIGN`, `MOVE_OPCLICK`,
`REPORT_ABUSE`, and `TUT_CLICKSIDE`. Each needs classification as obsolete,
collapsed, or incorrectly unmapped; silent drops are not an acceptable final
state.

### Harness stability was not guaranteed

`run-osrs239.sh` originally left its three background children attached to the
invoking pseudo-terminal. Under an agent/CI command session they received
SIGHUP together when the orchestration command returned. The launcher now uses
`nohup` plus `/dev/null` stdin for mock230, jav_config, and RuneLite. A retained
PTY is still used by this environment because its process supervisor removes
the entire command process group even after POSIX detachment.

### Recovered decompiler lambdas were required for a valid RuneLite run

The compilable deob contained three null lambda placeholders:
`class129.field891.execute(null)` and two `class488.submit((Runnable)null)`
calls. The original injected 1.12.33 jar's `invokedynamic` recipes identify
them as preference persistence, platform work, and audio work. The runnable
source now carries equivalent lambdas. Rebuild/API verification passes and the
live `RunnableExceptionLogger` flood is gone.

### EVENTS is the primary run-control channel

JCTL now accepts a dedicated blocking `EVENTS` connection without blocking the
ordinary command socket. It reports game-state transitions, framebuffer
readiness, screenshots, selected packet directions, `IF_SETEVENTS` details,
decode failures, and GPI transitions. Each accepted JCTL connection has its own
daemon worker and each event subscriber has a bounded queue, so an unattended
reader cannot stall or grow the client.

The final 2026-08-06 ordered-login capture observed, without screenshot
polling:

```
event 1 gamestate STARTING
event 3 gamestate LOGGING_IN
event 5 gpi init ... base=0,0 ... entity=false
event 6 gamestate LOADING
event 7 packet_out op=24 len=0
event 8 gamestate LOGGED_IN
event 10 gpi update ... base=3168,3168 ... entity=true
event 11 packet_in op=96 len=2
event 25 packet_in op=108 len=16
event 26 if_setevents 712:3 range=0..7 events1=0x1e events2=0xf
```

This slightly surprising order is authoritative: the client publishes its raw
`LOGGED_IN` state before it drains the queued `PLAYER_INFO`. The server-side
invariant is therefore not “GPI before the state field changes”; it is
`PLAYER_INFO/entity=true` before `IF_OPENTOP`, `UPDATE_STAT`, transmits, and the
interface work which RuneLite subscribers consume.

### GPI coordinates and the login WorldView must agree

The authoritative client has two distinct login transitions. The GPI prefix at
the front of the login rebuild (`client` → `class109.method3795`) seeds the
local index and absolute 30-bit coordinate but deliberately creates no
`Player`. The following real `PLAYER_INFO` calls `class109.method3797`; its
`method3807`/`method3823` path creates the actor only when that coordinate is
strictly inside the selected WorldView bounds. RuneLite's `getLocalPlayer()` is
therefore expected to be null at GPI init. The raw game state can already read
`LOGGED_IN` while the serial packet queue drains, but the actor must exist
before stats, interfaces, social/transmit state, and tick-end work.

The failure hidden by the old eventual-state test used saved coordinate
`2495,5112` with stale WorldView base `3168,3168`. The first appearance packet
decoded successfully yet could not materialize an actor; a later corrective
rebuild at base `2440,5064` made it appear after RuneLite plugins had already
thrown. `mock230_world_login` now loads the save, recentres/rebuilds the shared
scene, then encodes the GPI prefix and trailing rebuild zone from the same
state. It sends `PLAYER_INFO` before `IF_OPENTOP` and all 23 `UPDATE_STAT`
packets. The client naturally emits `MAP_BUILD_COMPLETE` while its serial
packet queue holds the later packets.

The revision-239 self-test now moves its login fixture outside the existing
scene without pre-recentring, resolves expected opcodes through the selected
wire instead of hard-coded revision-230 ids, and asserts both packet order and
the golden client's strict 104x104 containment rule. The focused login and
empty-social section is green under `MOCK230_REV=osrs239`; the broader suite
still has unrelated failures documented below.

### Coordinate-only UI tests are too fragile

Resizable-Modern has distinct static and movable tab layers whose bounds can
overlap near the minimum 765x503 canvas. A guessed coordinate can hit a valid
but different tab and still produce healthy packet traffic. JCTL now has
`widget`, `arm`, `menu`, `mounts`, and `clickwidget`; reads execute on the actual
client thread and clicks remain real AWT input.

Raw widget bounds are not sufficient either. `skill_guide_v2:close` lays out at
`518,10,26x23`, but its external `IF_OPENSUB` parent (`164:16`) clips at x=531.
The old centre `(531,21)` was outside the input clip and the client correctly
offered `Walk here`. `clickwidget` now walks ordinary widget parents and the
live component/mount table, intersects every clip, and selects `(524,21)` from
the actual visible `518,10,13x23` region. The resulting event chain is:

```
packet_out op=47 len=9
packet_in op=23 len=4
mount 164:16->860 removed
```

### Screenshot completion means a complete presented frame

The first framebuffer hook retained a pointer to the rasterizer's mutable main
surface. A screenshot could race the next paint and capture the world before
the interface pass, producing plausible but incomplete evidence. A shot now
requests an immutable copy from the end of the next raster-provider present;
the command and its `EVENTS` completion are not released until that copy exists.

### Revision-239 social loading required a new payload and ordering

The Friends and Ignore panels initially stayed on `Loading ... Please wait`.
The authoritative handler shows why: revision 239's zero-length
`FRIENDLIST_LOADED` moves the client to social state 1, while decoding
`UPDATE_FRIENDLIST` moves it to state 2. The server sent the marker last and
omitted an update for an empty list. Its 239 wire guard then exposed the second
defect by refusing `UPDATE_FRIENDLIST`: the revision-239 string record had never
been transcribed.

The login burst now sends the marker first and always follows it with a friend
snapshot, including an explicit zero-length snapshot. Non-empty rows use the
deob's exact shape: rename flag, current/previous NUL strings, world, rank,
flags, the online-only extra block, and trailing note. Live `social` telemetry
reports `state=2 friends=0 ignores=0`; the Friends and Ignore screenshots show
their empty-state instructions instead of loading text.

### The real-client interface contract is the primary gate

`tools/verify_runelite239_interfaces.py` starts before a fresh RuneLite launch
and wins the JCTL race with `EVENTS` before autologin. It drives real AWT input
through client-thread widget resolution, then asserts client state, menu rows,
mount lifecycles, packet directions, CS2-visible state, actor poses/animations,
decoded chat, complete frames, and exact item-sprite pixels.

The final matrix covers Friends→Ignore replacement; Prayer guide open/close and
mount clipping; a terrain click reaching run pose 824; exact inventory ids,
quantities, names, `Use`/`Drop`/`Examine` menu rows, and five model-bearing
sprites; world-map zoom 4→3→4 and resume-button close; music Play, MIDI, playlist
remove/add, and unlock hint; emote Loop and Perform; Account Benefits
open/close; and a mounted-action audit before and after the modal interface.

The verifier additionally rejects current-run EventBus, overlay,
scheduled-task, local-player, and packet-decode failures; missing revision-239
interface writers; the wrong injected jar; the wrong account; incorrect login
ordering; incomplete PNGs; scale-0/quantity-only item sprites; and dead mounted
actions. The launcher records the injected jar, and the verifier pins the
authoritative normal build at
`Deobfuscator/instr/build/out/injected-client-1.12.33-instr.jar`.

Final proof is in `build/run239/proof/final_parity_driver.log`,
`build/run239/proof/final_parity_events.log`, and the `final_*.png` artifacts.
Every full frame is 765x503 and every direct item sprite is 36x32; the report
records their byte counts and SHA-256 hashes.

The full revision-239 `mock230 --selftest` still reports unrelated pre-existing
content, combat, equipment, and pathing failures. Focused login ordering,
WorldView containment, empty-social, and skill-guide assertions pass; the
real-client contract above is the
integration verdict rather than treating that broader failing suite as green.

### The first real-client contract was still too narrow

The five-path contract above proved those paths, not interface parity. A final
log audit found RuneLite subscriber and overlay exceptions during the same
fresh login even though the eventual `player` probe and screenshots passed.
The contract also did not exercise the world-map close/zoom controls, enumerate
every mounted/openable interface for missing `IF_SETEVENTS`, or cover movement
animation, icon rendering, and inventory menu verbs. It is therefore retained
as useful evidence but is superseded as the completion gate. The verifier now
starts before RuneLite, attaches `EVENTS` before autologin, records the login
transition, and fails on RuneLite subscriber/overlay/decode errors.

### A second gameframe open erased the event contract

The initial login scripts armed the gameframe, then ran the broad
`gameframe_set_mode` path. That path sent a second `IF_OPENTOP`; the golden
client treats it as a new interface tree and discards the `IF_SETEVENTS`
overrides that had just been installed. The final screen looked correct, so
screenshots alone concealed the loss. Login now uses the login-mode script
without reopening the top-level interface, and the clean pre-login `EVENTS`
capture proves there is one `IF_OPENTOP` followed by the retained event ranges.

The final closed-main `ifaceaudit` enumerates 25 mounted groups, 6,127 widgets,
and 4,780 action slots with `dead=0`. With Account Benefits open it enumerates
26 groups, 6,196 widgets, and 4,788 action slots, also with `dead=0`. Every
server-facing action resolves to a live event range; actions proved by the
authoritative widget `onOp` CS2 path are classified as client-only rather than
being falsely required to send a server packet.

### Inventory source targeting needs bits 11 through 16

Inventory slots had only `Drop` and `Examine` although the item-container
script set `targetverb("Use")`. The authoritative menu builder checks
`Statics.method7577(events)`, which extracts the six source-target bits at
positions 11 through 16. The old mask `0x320000` enabled drag/target-receive
semantics but left that source field zero. The server constant is now
`0x33f800`; a pre-login `EVENTS` capture records
`if_setevents 149:0 range=0..27 events1=0x33f800`, and the live menu probe at an
occupied slot reports `Use Chaos rune` as `WIDGET_TARGET`. Evidence:
`build/run239/proof/inventory_menu_fixed.png`.

### World-map close is a resume button, while zoom is local CS2

The golden world-map close widget (`595:38`) sends
`RESUME_PAUSEBUTTON`, not a generic `IF_BUTTONX`. The mock server previously
offered the map through its world-map handler but never routed resume-button
traffic back to that handler. The inbound dispatcher now does so when no
server-script resume trigger claims the button, and sends `IF_CLOSESUB`.

The map's zoom controls are client-only `onOp` CS2 hooks. JCTL world-map
telemetry observed zoom `4 -> 3 -> 4` after controlled clicks on `595:27` and
`595:28`, with no server round trip. Closing then produced outbound opcode 115,
inbound opcode 23, and removal of mount `164:18->595`. Evidence:
`worldmap_zoom_initial.png`, `worldmap_zoom_out.png`,
`worldmap_zoom_in.png`, `worldmap_closed.png`, and
`worldmap_fixed_events.log` under `build/run239/proof/`.

### Revision-239 run movement has two independent wire fields

A dedicated RUN position opcode was necessary but not sufficient. Live actor
telemetry showed the player advancing two tiles per update while remaining on
pose 819 (walk), disproving the first focused codec test. In the golden client,
`class109` first stages the opcode-2 coordinate and separately decodes extended
flag `0x1000` into the traversal type used by `Statics.method3189`. Value 2 is
the run traversal; the default value 1 remains walk and value 0 halves speed.
The server writer now adds the raw one-tick `TEMP_MOVE_SPEED=2` block after the
sequence block and before appearance whenever it emits RUN. The independent
decoder test asserts both opcode/direction and the full `0x1008` continuation
flag plus value 2. A real terrain click in the final RuneLite run advanced the
actor through walk pose 819 into run pose 824; repeated actor EVENTS and
`build/run239/proof/final_run_pose824.png` make that the integration proof. The
earlier two-tile/pose-819 capture is retained as negative evidence.

### Dynamic action parity required server scripts, transmit state, and audit fixes

The final `dead=0` result was not obtained by weakening the audit. The audit
was corrected to use the golden client's actual event-table key: a root widget
has dynamic child index `-1`, while generated rows use their real sub-id. This
removed instrumentation false positives without arming any action. Each
remaining real row was then traced through its widget actions, `onOp` CS2, and
the revision-239 inbound packet selected by the client.

The music player (`239:11`) is generated by clientscript 9290 from a DB query,
so its row sub-id is a result index rather than a DB row id. Server content now
repeats the same query, resolves the selected track, and arms ops 1 through 5
for Play, Unlock hint, and the three playlists. The mock DB runtime now retains
column defaults and returns them for rows without overrides, matching the CS2
DB op semantics needed by the music table. `MIDI_SONG` uses the revision-239
10-byte payload. All 150 playlist carrier varps are permanent and
`transmit=yes`, so a toggle rebuilds the client menu instead of changing only
server memory.

The live proof selected `Crest of a Wave` at sub 115, decoded
`Now playing: Crest of a Wave`, observed `MIDI_SONG` opcode 35 length 10,
changed `Remove from playlist 1` to `Add to playlist 1`, restored it, and
decoded the locked `Adventure` hint `This track unlocks in Varrock.`. JCTL's
`chat` command reads the client's decoded message nodes, so these assertions do
not infer success from an outbound click.

The emote grid (`216:2`) now arms both Perform and Loop for every generated
row and maps the authoritative loop sequence table, including the two rows
whose verb order is reversed. Real input proved Yes/Loop as animation 189 and
Dance/Perform as animation 866. Account `View Benefits` (`109:41`) is armed at
login; real input mounts the Membership Benefits interface, its own audit stays
at `dead=0`, and its close packet removes the mount.

### Item icon and model corruption was a decompiler evaluation-order defect

The supplied rune/feather screenshot was a real renderer regression, but the
prior z-buffer work was not its cause. Normal-z-buffer A/B runs and increasingly
narrow hybrid jars isolated the first bad class to `class144`, then the exact
method to `class144.method4947`, the model depth-bucket fill.

The original revision-239 bytecode performs:

```
field2108[depth][field2107[depth]++] = face;
```

The decompiler had split that into “increment count, then index with the new
count.” Bucket zero remained stale and one real face was omitted from every
non-empty depth bucket. That distorted item silhouettes and could drop faces
from world models even though item ids, quantities, textures, and z-buffer
state were all correct. Both the authoritative compilable source and the
instrumented source now preserve the bytecode order explicitly:

```
int index = field2107[depth]++;
field2108[depth][index] = face;
```

The normal jar rebuilds and passes API verification without `-Xverify:none`.
The final live inventory frame visibly restores the chaos, water, blood, death,
and feather models. JCTL also clears the client sprite cache and regenerates
each sprite through the real `Client.createItemSprite` path at scale 512. The
verifier pins exact 36x32 pixel hashes for all five.

### The final verifier catches persisted state and visual false positives

Two apparently reasonable final runs were rejected before the clean pass. The
first found that world-map zoom persists across launches, so assuming an
initial zoom of 4 made the test state-dependent. The verifier now reads the
actual value, normalizes it, proves both 4→3 and 3→4 transitions, and then
proves close/unmount.

The second completed every workflow but failed item-sprite hashes. Investigation
showed that the old expected images were generated at scale 0 and contained
only quantity text; they could pass while the 3D item was missing or corrupt.
JCTL now defaults `itemsprite` to scale 512 and the contract hashes the genuine
model-bearing pixels. The third clean launch passed unchanged from login
through all 17 full screenshots, five direct sprites, both interface audits,
server trigger checks, EVENTS ordering, and runtime-log health.

Every final launch had a blocking `EVENTS` subscriber connected before
RuneLite. A second standalone `EVENTS` reader remained attached throughout the
final run, writing `build/run239/proof/final_parity_events.log`; no screenshot
polling or idle-client assumption is part of the verdict.

## Step log

- 2026-08-06: Audited dirty state in all three repositories and preserved
  pre-existing user work, including CS2/JCTL/packet instrumentation.
- 2026-08-06: Read the authoritative CS2 interpreter entry, dispatch ladder,
  interface packet decoder, and existing deob defect/run instructions.
- 2026-08-06: Ran deob build and API verification successfully.
- 2026-08-06: Ran authoritative protocol/table audits; recorded the sentinel
  server row and five outbound canonical coverage gaps.
- 2026-08-06: Reproduced launcher child teardown and hardened detachment.
- 2026-08-06: Launched mock230 + jav_config + RuneLite with autologin, inbound
  and outbound packet telemetry, IF_SETEVENTS lookup telemetry, and CS2 stack
  telemetry.
- 2026-08-06: Verified `LOGGED_IN` over JCTL and captured the first real-client
  screenshot and packet logs.
- 2026-08-06: Added the concurrent `EVENTS` subscription and event sources for
  game state, framebuffer, screenshots, packets, event arming, and decode
  failures; verified an asynchronous screenshot completion event.
- 2026-08-06: Recovered three missing decompiler lambdas from the authoritative
  injected jar; rebuilt and removed the live null-Runnable failure.
- 2026-08-06: Added authoritative GPI telemetry (`player` plus transition-only
  events) and proved the player stream was structurally correct but arrived
  after plugin-visible stat events.
- 2026-08-06: Reordered the mock230 login burst to materialize the local actor
  before interfaces/stats; the focused login-burst self-test passes and the
  rebuilt RuneLite run has no subscriber exception.
- 2026-08-06: Captured `04_ordered_login.png` and a live interface-tab frame;
  confirmed the revision-239 client emits the collapsed `IF_BUTTONX` opcode 47
  and mock230 decodes it to the named `IF_BUTTON1` target.
- 2026-08-06: Began replacing guessed UI coordinates with client-thread widget
  resolution and centre-click commands after reproducing an overlapping-layer
  false selection in Resizable-Modern mode.
- 2026-08-06: Added `arm`, `menu`, `mounts`, and `social` probes and moved all
  injected-API reads onto the real client thread.
- 2026-08-06: Opened Prayer Overview through `IF_BUTTON2` → `IF_OPENSUB` →
  `RUNCLIENTSCRIPT`, then proved its close lifecycle with `IF_BUTTON1` →
  `IF_CLOSESUB` and before/after screenshots.
- 2026-08-06: Fixed `clickwidget` to honor internal and external mount clips;
  the skill-guide close regression now selects x=524 instead of world x=531.
- 2026-08-06: Made screenshots wait for an immutable end-of-present copy,
  eliminating partial world-only frames from the evidence channel.
- 2026-08-06: Reproduced Friends/Ignore stuck loading, read the authoritative
  social handlers, added the empty friend snapshot, corrected marker ordering,
  and transcribed revision 239's `UPDATE_FRIENDLIST` row payload.
- 2026-08-06: Verified Friends→Ignore replacement and the floating world-map
  overlay through live mount tables, packet events, and screenshots.
- 2026-08-06: Added and passed `tools/verify_runelite239_interfaces.py`; its
  report records commands, mount assertions, EVENTS traffic, PNG sizes, and
  SHA-256 hashes.
- 2026-08-06: Classified the revision-239 applet-focus packet as lifecycle
  telemetry, rebuilt mock230, restarted the entire stack, attached `EVENTS`
  before interaction, and passed the full interface contract again against
  the exact final binary. Updated the evidence hashes above from that run.
- 2026-08-06: Re-audited the supposedly final client log and found local-player
  null subscriber/overlay exceptions which the five-path contract had missed.
  Hardened the verifier to attach before launch and treat those log failures as
  fatal; expanded the acceptance matrix to all interfaces plus world-map
  close/zoom, run animation, icon rendering, and inventory menu verbs.
- 2026-08-06: Removed the second login `IF_OPENTOP` that discarded the first
  event table and reran the verifier with `EVENTS` attached before autologin;
  the clean contract passed with no duplicate gameframe open.
- 2026-08-06: Added a complete mounted-widget/action audit. Its first live pass
  measured 26 groups, 6,877 widgets, 4,973 actions, and identified the dynamic
  music rows, emote loop operations, and Account Benefits action as the
  remaining server-event work.
- 2026-08-06: Corrected inventory event masks from `0x320000` to `0x33f800` by
  following the golden client's source-target bit extractor. Recompiled 12,514
  content scripts and proved the live `Use` target menu entry.
- 2026-08-06: Routed unclaimed `RESUME_PAUSEBUTTON` traffic to the world-map
  lifecycle handler. Proved local zoom 4→3→4 and server close through packet,
  mount, world-map-state, event-log, and screenshot evidence.
- 2026-08-06: Replaced teleport-only local movement with the revision-239
  WALK/RUN direction tables. Live telemetry then exposed the missing second
  half: two-tile movement still used walk pose 819.
- 2026-08-06: Read the golden traversal decoder and added one-tick
  `TEMP_MOVE_SPEED=2` extended info for RUN, in authoritative field order. The
  focused test now asserts opcode 2, direction, flag `0x1008`, and traversal
  byte 2; live RuneLite pose verification is next.
- 2026-08-06: A stricter client-log audit invalidated an apparent clean pass:
  the saved GPI coordinate was outside the initial WorldView, so RuneLite
  subscribers observed a null local player until a corrective rebuild.
- 2026-08-06: Traced the golden `method3795`/`method3797` actor lifecycle,
  rebuilt mock230 with post-save scene alignment, and observed `entity=true`
  before `IF_OPENTOP` and stats. The final trace clarified that the golden
  client's raw `LOGGED_IN` transition precedes queued `PLAYER_INFO` processing.
- 2026-08-06: Hardened the revision-239 login self-test with an off-scene saved
  coordinate, strict WorldView containment, and revision-resolved packet ids;
  the focused login/social section is green even though the broad suite still
  reports unrelated failures.
- 2026-08-06: Removed the misleading `trackerActive` GPI label, rebuilt the
  instrumented deob (740 classes), and passed API-surface verification (10,730
  rebuilt members).
- 2026-08-06: Added fatal verifier gates for runtime RuneLite exceptions,
  packet decode errors, interface writer gaps, login packet order, exact
  injected-client jar, and controlled account identity. A concurrent hybrid
  client on the shared ports was detected and rejected rather than accepted as
  authoritative evidence.
- 2026-08-06: Corrected the mounted-action audit's root-widget event key from a
  guessed dynamic child 0 to the golden client's `-1`; retained strict
  server/client classification and drove the real remaining dead rows to zero.
- 2026-08-06: Implemented revision-239 music row resolution from the same DB
  query as clientscript 9290, fixed mock DB column-default semantics, added the
  10-byte `MIDI_SONG` writer, and armed all five dynamic jukebox operations.
- 2026-08-06: Added 150 permanent/transmitting playlist carrier varps and proved
  live remove/add menu transitions. Added decoded `chat` telemetry and asserted
  both the now-playing message and a locked-track location hint.
- 2026-08-06: Armed both emote operations and mapped the cache's loop sequence
  table; real AWT input produced Yes/Loop animation 189 and Dance/Perform
  animation 866. Added and proved Account Benefits open, audit, and close.
- 2026-08-06: Proved the run traversal fix end-to-end: a real terrain click
  produced repeated actor EVENTS at pose 824 and a complete presented-frame
  screenshot. The focused player-info and DB test targets both pass.
- 2026-08-06: Ruled out the prior z-buffer path with normal-renderer A/B runs,
  bisected hybrid jars to `class144.method4947`, and compared the source with
  original revision-239 bytecode.
- 2026-08-06: Fixed the lost post-increment evaluation order in both compilable
  and instrumented `class144` sources. Rebuilt the normal deob (740 classes),
  passed API verification (10,735 rebuilt members), and restored item/world
  model faces without `-Xverify:none`.
- 2026-08-06: Added cache-clearing real item-sprite generation to JCTL. Rejected
  an icon check whose scale-0 expected files contained quantity text but no 3D
  model, switched the default and contract to scale 512, and pinned five exact
  model-bearing sprite hashes.
- 2026-08-06: Rejected a nominal final run because persisted map zoom began at
  3 instead of 4; changed the verifier to normalize observed state and prove
  both directions rather than assume process-global client preferences reset.
- 2026-08-06: Launched the final normal stack with `EVENTS` connected before
  autologin and a second standalone `EVENTS` reader. The expanded contract
  passed all workflows, 17 full frames, five sprites, server-trigger checks,
  runtime-log health, and both `dead=0` action censuses.
- 2026-08-06: Visually inspected the final inventory/menu, run, world-map,
  music/chat, emote, Account Benefits, and direct item-sprite artifacts. Updated
  this record with the final findings and proof paths.
- 2026-08-06: Re-ran `make -C src test-mock239-playerinfo test-db`; the run
  traversal codec and DB default/fallback suites passed. Python compilation of
  both control/verifier tools and `git diff --check` also passed. The final
  deob API gate reports 723/10,469 original classes/members versus 740/10,735
  rebuilt, with the API surface complete.
- 2026-08-06: Confirmed the retained proof stack is listening on mock230 43594,
  jav_config 8080, and JCTL 43601, with the standalone `EVENTS` reader still
  connected after the verifier completed.
- 2026-08-06: Scoped the deliverable onto `codex/` branches while preserving
  unrelated dirty work. Created content commit `d9cf5e1d1c` and deob telemetry /
  renderer commit `95a5db1`; an isolated checkout of the content commit compiled
  all 12,534 scripts without relying on excluded local config changes.
- 2026-08-06: Re-ran the five focused revision-239 interface/player-info C test
  targets, the DB test, Python compilation, deob build/API verification, and
  cached-diff whitespace checks before creating the root integration commit.
- 2026-08-06: Created root integration commit `018fb17b`, pushed all three
  `codex/` branches, and opened
  `https://github.com/MRobertEvers/oldschool-clientc/pull/7` against `v3`.
