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

The original parity artifacts live under `build/run239/proof/`. The expanded
regression investigation and final combined acceptance artifacts live under
`build/run239-regressions/`, with the accepted five-regression run in
`combined-five-final3/`.

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

The follow-up camera, house, Hans, running, and client-layout regressions are
also complete. Their final combined run used a blocking EVENTS subscriber
attached before launch and passed all five workflows against the real compiled
239 deob. No game-runtime exception, packet decode error, interface writer gap,
or unexpected disconnect occurred before the explicit quit, and the post-quit
telemetry report completed normally. The correctly cache-backed broad mock
world self-test still has 23 unrelated content/combat/pathing failures; they are
not counted as interface success and are not hidden by this contract. A later
Hans-only visual audit corrected the NPC body alignment omitted from that
combined run; its clean proof is
`build/run239-regressions/hans-vertical-final3/proof`.

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

### Camera zoom was blocked by uninitialised server-owned varcs, not input capture

The baseline separated all three input paths before changing the server:

- real AWT wheel input produced `wheel_awt rotation=-1 consumed=false`, the
  client's wheel poll returned `-1`, and CS2 scripts 39 (`camera_zoom`) and 42
  (`camera_do_zoom`) ran;
- the normal Display slider ran scripts 1047/1048 and ultimately script 42;
- the All settings `Camera zoom distance` row ran scripts 3895/3898/3899 and
  ultimately script 42.

All three nevertheless clamped to the same FOV. The authoritative 239 scripts
show why: script 605 writes the four camera-bound varcs 1338..1341, script 626
invokes it with `(128, 896, 128, 896)`, and script 42 clamps against those
varcs. mock230 never sent that initialisation, so Java's zero defaults collapsed
the range. `mock230_world_login_finish` now sends
`RUNCLIENTSCRIPT 605(128,896,128,896)` for revision 239 and later. The final
ordered event record contains the exact invocation at event 45.

The final combined run proves the paths independently. Wheel input changes FOV
512→536, the normal slider changes 536→127, and the All settings slider changes
127→1314. The corresponding traces are
`build/run239-regressions/combined-final/proof/camera_wheel_cs2.json`,
`camera_normal_slider_cs2.json`, and `camera_allsettings_slider_cs2.json`; the
before/after frames are `06_camera_wheel.png`, `07_display.png`,
`08_normal_slider.png`, `09_allsettings_zoom.png`, and
`10_allsettings_slider.png` in the same directory.

The All settings slider is deliberately client-local. Its dynamically-created
`134:19 sub=24` click zone has an on-op CS2 callback but no server
`IF_SETEVENTS`, sends no IF_BUTTON packet, and therefore produces one diagnostic
`arm_missing` event. That is not an interface writer gap; the packet writer is
not involved, and the client trace plus FOV/varc transition is the acceptance
signal.

### House options is a side-only modal and CLOSE_MODAL has no component id

House options is group 370 mounted at `164:71`, and its close component is
`370:24`. The golden client runs script 29 (`if_close`) and sends the zero-byte
`CLOSE_MODAL` packet (revision-239 opcode 95), followed by the ordinary collapsed
IF_BUTTON traffic for the clicked component. The close packet does not identify
which component or interface initiated it.

mock230 tracked group 370 only as `sidemodal_group`; its close routine returned
early whenever `mainmodal_group` was empty. The server therefore retained the
mount after the client had closed its local copy. The close routine now snapshots
both modal groups, runs `[if_close]` for each group that exists, and emits
`IF_CLOSESUB` for a side-only mount as well as for main modal state. A focused
self-test now constructs exactly the `main=0, side=poh_options` case.

The final combined record shows `CLOSE_MODAL (main=0 side=370 chat=0)`, followed
by `IF_CLOSESUB`; `11_house_open.png` contains the mounted panel and
`12_house_closed.png` plus the final mount table prove it was removed.
`house_close_cs2.json` contains script 29.

### Hans exposed a six-byte dynamic choice callback

The deterministic dialogue baseline found a separate callback defect before
the actor-stream crash described below. Selecting a chatmenu row made the golden
client send revision-239 `RESUME_PAUSEBUTTON` opcode 115 as
`g4Alt3(parent uid) + g2(dynamic sub-id)`. For row 1 of `chatmenu:options` the
literal body is `db 00 01 00 00 01`, which normalises to
`00 db 00 01 00 01` (`219:1`, sub 1). The translator discarded the final two
bytes, so `last_slot` stayed zero and the same choice reopened indefinitely.

The inbound translator now preserves the complete six-byte callback. The world
handler accepts both classic four-byte and revision-239 six-byte forms, maps
`ffff` to no subcomponent, and sets `last_slot` before resuming the parked
server script. The literal inbound test and the Hans self-test both use the
six-byte packet rather than substituting IF_BUTTON1.

The final clean session used an actual right-click `Talk-to Hans` menu action,
not a direct server trigger. It walked to NPC slot 26, opened the dialogue,
selected row 1 with real AWT input, logged
`RESUME_PAUSEBUTTON 219:1 sub=1`, advanced to the selected player line, and
completed the conversation. Evidence is `02_hans_menu.png` through
`05_hans_progressed.png`, `hans_talk_cs2.json`, `hans_choice_cs2.json`, the
ordered EVENTS record, and the server log under
`build/run239-regressions/combined-final`.

The reported "Hans crash" was therefore two observable failures: the lost
choice sub-id when dialogue was invoked directly, and the NPC_INFO decoder
failure encountered while physically approaching/interacting with Hans. There
was no separate Hans-script Java exception in the direct-dialogue baseline.

### Hans NPC body alignment is authoritative clientscript 600

The first combined screenshot was functionally healthy but did **not** match the
supplied reference vertically: `chat_left:text` rendered the sentence at the
top of its 67-pixel body box. The raw revision-239 cache explains why. Component
`231:6` is `halign=1, valign=0, lineheight=16`, whereas the corresponding player
component `217:6` is already `halign=1, valign=1, lineheight=16`. The old content
comment that the NPC body "centres by itself" was therefore false.

The golden cache also supplies the missing server-callable behavior. Client
script 600 is exactly `if_settextalign(h, v, lineheight, component)`. There is
no `IF_SETTEXTALIGN` server packet in revision 239. Both NPC-chat entry points
now mount `chat_left` and then send literal
`RUNCLIENTSCRIPT 600(1,1,16,231:6)` before arming Continue. The client applies
that to the mounted widget, producing `textAlign=1,1 lineHeight=16` without
altering the cache or inventing server-side rendering behavior.

The possible decompilation/recompilation defect was tested directly. The
original `gamepack-deob.jar` bytecode and rebuilt `class671.method14513` retain
equivalent vertical text placement, and `class308.method7396` decodes all three
text-alignment fields with the correct multipliers. Thus this defect was a
missing CS2 lifecycle call, not broken font or widget arithmetic. JCTL now
reports text alignment and line height, and its NPC menu control also matches
the menu entry's NPC index so an overlapping Man cannot masquerade as Hans.

The clean post-fix run connected its blocking EVENTS subscriber before launch,
used a real indexed `Talk-to` menu action, and recorded opcode 114 length 25 as
`run_clientscript types=iiii args=[600, 1, 1, 16, 15138822]`. It completed the
dynamic choice, player reply, final Hans reply, and close. The final GPI was
`high=1 low=2046 pending=0 entity=true`; no game exception, decode error, writer
gap, or unexpected disconnect occurred. Evidence is
`hans-vertical-final3/proof/01-hans-centered.png`, the ordered `events.log`,
server/client logs, and retained CS2 trace. The world self-test additionally
parses the literal reverse-argument revision-239 packet and requires script 600
with `(1,1,16,231:6)`.

### The official C client follows the same script-600 path

The C client was rebuilt from the current `v3` integration rather than tested
with the stale binary in the primary dirty checkout. A fresh TCP mock230 session
using the revision-239 login and packet tables then drove `talk hans 1` through
the real client-cheat packet after the login scene barrier. Its trace records
the ordered client-side lifecycle: three `IF_SETTEXT` writes, `IF_OPENSUB` of
group 231 at `162:567`, `RUNCLIENTSCRIPT 600 argc=4`, and the Continue event
mask. At exit, live component `231:6` is `380x67` at `115,377`, with the Hans
sentence retained.

This check found no second C-only arithmetic defect. The C VM already decodes
opcode 2114 in the golden order, the CS2 host applies all three fields to the
mounted `UITree` text node, and both renderers consume the emitted vertical
alignment. The shared content fix therefore fixes the official C client too;
hard-coding Hans in either renderer would be incorrect. A new
`test-cs2-text-align` target pins the literal script-600 arguments
`(1,1,16,231:6)`, while the UITree suite pins setter-to-render-descriptor
propagation. `TORIRS_DUMP_COM` now prints text alignment and line height so the
live client path remains directly auditable. The retained C proof is
`build/run239-regressions/c-client-hans-final/hans.png` plus `client.log` and
`server.log` in the same directory.

### The running crash was a revision-239 NPC_INFO boundary violation

The independent movement reproduction retained the last completed framebuffer,
ordered EVENTS tail, packet/CS2 traces, and both logs under
`build/run239-regressions/diagnosis`. The fatal client condition was:

```
java.lang.RuntimeException: 66,9
    at Statics.method13029(Statics.java:55602)
    at client.method2413(client.java:3363)
    at Statics.method1844(...)
    at client.method2036(...)
    at client.method1743(...)
```

The final NPC_INFO payload was the literal nine bytes
`0d 3d e9 4a 81 c7 ff f8 00`. Golden-client bit traversal consumed 13 tracked
entries and ended at bit 45 with one queued extended update. Its low-resolution
loop only reads another NPC index when at least `16 + 12 = 28` bits remain.
The old packet left 27 bits including its sentinel, so the client could not
enter the loop to consume `ffff`; it byte-aligned, treated `ff` as the extended
mask, and overran at offset 66 of a nine-byte packet.

Two encoder errors combined at that boundary. A face-only NPC was marked as
having extended information even though the v5 encoder intentionally has no
FACE_ENTITY/FACE_COORD block, and sentinel emission used `queued_count > 0`
instead of the golden 28-bit guard. The v5 pending-mask predicate now includes
only blocks the v5 writer actually serialises, encodes the byte tail separately,
and emits `ffff` exactly when padding plus the extended tail is at least 28
bits. `mock239_npcinfo_tail_needs_sentinel` has focused bit-45 and byte-aligned
threshold tests derived from the captured packet.

The final combined session then used the real Hans approach plus 24 independent
terrain clicks. Its NPC log repeatedly crosses the original tracked-count 13
condition with legal packet lengths and `extended=0` for face-only changes;
GPI remains `high=1 low=2046 pending=0 entity=true`. `13_movement_final.png`
and `14_final_frame.png` are complete presented frames after the stress pass.

### Final four-regression acceptance run

`build/run239-regressions/combined-final/proof/events.log` is the blocking EVENTS
subscriber attached before RuneLite launch. It retains 1,545 ordered lines from
STARTING through all interactions. The final stack used the rebuilt authoritative
239 deob, mock230 on 43630, jav_config on 8097, and JCTL on 43635.

The run proves actual Hans NPC input, wheel zoom, normal slider zoom, All settings
slider zoom, side-only house close, and independent movement in one login. The
pre-quit health scan found no game/client runtime fatal, packet decode error,
unknown inbound opcode, interface writer gap, or unexpected disconnect. The
last framebuffer and GPI state were captured before the explicit JCTL quit.

The first combined attempt revealed a telemetry-only shutdown race:
`JProf.percentiles` read `ringUsed` repeatedly while the last game frame could
increment it, selecting index N from an N-element snapshot. JProf now snapshots
the length once. The repeated final run exits normally, writes the full profiler
report, and contains no uncaught shutdown exception. RuneLite still logs its
pre-existing, caught INFO-level `setupCompilerControl` missing-resource warning;
it occurs before login and is not a client/game-thread failure.

### Client layout selection was a callback and widget-lifecycle defect

The authoritative revision-239 client does not encode Fixed, Resizable Classic,
and Resizable Modern as `WINDOW_STATUS` values 0/1/2. Its outbound writer sends
one byte with window mode 1 for Fixed or 2 for either resizable layout, followed
by the big-endian canvas width and height. The distinction between the two
resizable roots exists in varbit 4607 (`resizable_stone_arrangement`) and in the
selected dynamic dropdown row.

The Display dropdown is created by script 4568. Its rows are dynamic children
`116:40` subids 1..3, and selecting a row runs script 4569. The golden script
updates the row label/timer and emits the normal component operation, but does
not call script 3998 for this row type. The exact revision-239 outbound callback
is opcode 47 (`IF_BUTTON1` after normalisation) with the `116:40` uid and the
selected subid. mock230 had not armed those dynamic children, so the rows looked
interactive but had no server callback.

Content now arms subids 1..3 and maps them to Fixed, Classic, and Modern. The
server latches the precise selection before content resumes, because the later
`WINDOW_STATUS` packet cannot recover that information. A queued apply runs
client script 3998, opens root 548/161/164, synchronises varbit 4607, and then
runs script 7990 to recreate the Display panel's dynamic children. That last
step is essential: `IF_OPENTOP` replaces the root while preserving the mounted
settings group id, so its onLoad does not otherwise recreate the dropdown.

Live proof in `build/run239-regressions/layout_final2/proof` first established
the fix independently. The final combined session repeats all three choices:
`14-layout-modern.png` has root 164, varbit 4607=1, and label `Resizable -
Modern layout`; `15-layout-fixed.png` has root 548, varbit 0, label `Fixed -
Classic layout`, and server `WINDOW_STATUS window=1`; `16-layout-classic.png`
returns to root 161, varbit 0, label `Resizable - Classic layout`, and server
`WINDOW_STATUS window=2`. Each recreated dropdown retained effective op-1 masks
on subids 1..3.

### The revision-239 widget layout arithmetic is intact

The reported interface geometry was audited against the compilable 239 deob,
not inferred from content. `Statics.method3791` implements width/height modes
0 (absolute), 1 (parent minus inset), 2 (14-bit proportional), and 4 (aspect
ratio). `Statics.method6166` implements the corresponding absolute, centred,
right/bottom, proportional, proportional-centred, and proportional-right/bottom
position modes. The rebuilt instrumented jar's bytecode preserves those
operations; differences in the Java source are equivalent decompiler branch
forms, not altered arithmetic.

JCTL now reports original/relative geometry, all four alignment modes, parent
identity and bounds, and on-op listeners. Live probes such as `116:27`, its
dynamic label `116:27:4`, and layout row `116:40:3` match the authoritative
formulas in Fixed, Classic, and Modern roots. The broad geometry arithmetic is
intact, but the Hans body text exposed a separate lifecycle omission: raw
`231:6` is intentionally top-aligned and must be changed after mount by
authoritative clientscript 600. The earlier
`combined-five-final3/proof/02-hans-dialogue.png` is therefore retained as the
before frame, not accepted as a reference match. The corrected frame is
`hans-vertical-final3/proof/01-hans-centered.png`. In short, no remaining core
decompilation math defect was found; the layout regression and Hans alignment
had distinct callback/lifecycle causes.

### Final five-regression acceptance run

`build/run239-regressions/combined-five-final3/proof/events.log` is the blocking
EVENTS subscriber connected before RuneLite launched. The run used only real
AWT input through JCTL and retained 22 complete framebuffers, focused CS2 traces,
widget/GPI/camera state, complete packet logging, and server logging.

In one clean login it completed the Hans conversation from a live NPC hull,
made 24 independent terrain clicks with running enabled, changed camera FOV by
the normal slider (497→1345), changed it again through the All settings slider
(1345→1399), changed it by wheel input (1399→1198), selected Modern, Fixed, and
Classic layouts, and closed the side-only house interface. Final GPI was
`high=1 low=2046 pending=0 entity=true`; group 370 was absent from the final
mount table; the final layout was root 161 with varbit 4607=0.

The ordered record includes both `wheel_awt` and `wheel_poll`, exact layout
IF_BUTTON packets and roots, CLOSE_MODAL opcode 95 followed by IF_CLOSESUB, and
all NPC_INFO traffic. The pre-quit scan found no game runtime exception, packet
decode error, unknown inbound opcode, interface writer gap, unexpected
disconnect, or profiler shutdown failure. The only exception text is RuneLite's
known caught, pre-login `setupCompilerControl` warning. Explicit JCTL quit wrote
the complete profiler report, and only this run's recorded server/jav_config
PIDs were stopped afterward.

### `::zuk` exposed a narrowed varbit-mask local in the compilable deob

The reported `::zuk` failure was reproduced from a clean `origin/v3` worktree
with a blocking EVENTS subscriber connected before RuneLite. The command
successfully rebuilt the Inferno instance and mounted group 596 at
`161:8->596`, but its onload chain failed twice:

- `739 -> 737 -> 735 -> 4018` (`ArithmeticException: / by zero`);
- `739 -> 738 -> 4018` on the following var transmit.

Script 739 is the Inferno HP updater. It reads cache varbits 5653 and 5654,
then executes SCALE (4018) as `current * width / base`. Server-side tracing
proved this was not a guessed packet or ordering defect. The authoritative
cache places both 11-bit values in varp 1575, current at bits 0..10 and base at
bits 11..21. mock230 wrote base first and current second, before IF_OPENSUB,
and emitted the literal revision-239 VARP_LARGE bodies:

- base only, 2457600: `06 a7 00 80 25 00`;
- current plus base, 2458800: `06 a7 b0 84 25 00`.

Those bytes match the golden handler (`g2_alt2` id followed by `g4_alt1`
value). The broken compilable deob nevertheless reduced varp 1575 to raw
`1200`, leaving varbit 5654 at zero. The cause was in both revision-239 mask
tables, `class313.field4227` and `class419.field5295`: the decompiler had
narrowed the original bytecode's integer accumulator to `byte`. It overflowed
while constructing masks wider than six bits. The 11-bit POP_VARBIT setter
therefore treated its legal maximum as `-1`, rejected base HP 1200, wrote zero,
and handed SCALE a zero divisor. Restoring an `int` accumulator in the
authoritative source and instrumented mirror fixes the client without changing
server behavior.

The deob build now runs `VarbitMaskRegression` against the recompiled classes
and checks masks 1, 127, 2047, 4194303, and -1 in both tables. The root world
self-test independently packs the two cache varbits, requires carrier value
2458800, and compares both six-byte packet bodies above. The correctly
cache-backed broad self-test reaches this section without a Zuk assertion; its
unrelated pre-existing content/combat/pathing failures remain recorded in
`build/run239-zuk/mock230-selftest.log`.

Fresh live acceptance is under `build/run239-zuk/fixed`. Real AWT input typed
and submitted `::zuk`; JCTL then reported varp 1575 = 2458800, varbits
5653/5654 = 1200/1200, mounted group 596, and component `596:9` text
`1200 / 1200`. The interface audit reports `dead=0`, GPI remained valid in the
Inferno instance, and the presented frames, ordered EVENTS, packet trace,
client log, and server log contain no Zuk CS2 exception, packet decode error,
writer gap, or unexpected disconnect. The only exception text is RuneLite's
known caught pre-login `setupCompilerControl` warning.

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
- 2026-08-06: Merged current `v3` (`2c8b891b`) and its
  `lane-servsplit` content (`9d9f34f1c7`) in isolated worktrees, preserving
  the unrelated dirty files in the primary root and content checkouts.
- 2026-08-06: Resolved the content varp conflict in favor of `v3`'s split
  allocation ledger: cache-owned music carriers remain in
  `configs/all.varp.compack`, while server allocations remain in
  `pack/varp.alloc`. The latest allocator produced no drift and the merged tree
  compiled all 12,534 scripts.
- 2026-08-06: Resolved the launcher conflict by retaining both `v3`'s single
  world/JS5 cache and detached `nohup` child lifecycle. Retained both
  `test-server-clean` and every revision-239 interface fixture in the merged
  Makefile.
- 2026-08-06: Re-ran the inbound, `RUNCLIENTSCRIPT`, interface-setter,
  interface-state, player-info/run-traversal, and DB suites after the merge;
  all passed. Python control/verifier compilation and whitespace checks also
  passed.
- 2026-08-06: Opened the authoritative Deob telemetry/renderer PR at
  `https://github.com/MRobertEvers/Deob/pull/1`; GitHub reports it cleanly
  mergeable into `perf-instrumentation`.
- 2026-08-06: Read this integration record in full, fetched the root/content/deob
  PR branches, inspected both PR diffs, and created clean isolated root,
  OSRS-Content, Deob, and RuneLite worktrees. Left every pre-existing dirty
  primary tree and lagging local branch ref untouched.
- 2026-08-06: Started a blocking EVENTS subscriber before the baseline client
  and reproduced dead wheel zoom with both `wheel_awt` and `wheel_poll`
  present. Reproduced the normal Display slider and All settings slider with
  their CS2 callbacks running but FOV unchanged.
- 2026-08-06: Read golden scripts 39, 42, 605, 626, 833, 1043, 1047, 1048,
  3895, 3898, and 3899. Identified zero varcs 1338..1341 as the common clamp and
  added revision-239 login `RUNCLIENTSCRIPT 605(128,896,128,896)`.
- 2026-08-06: Reproduced house options at mount `164:71->370`, component
  `370:24`, with CS2 script 29 and outbound CLOSE_MODAL opcode 95. Proved the
  server returned on `main=0, side=370`, then fixed side-only `[if_close]` and
  IF_CLOSESUB lifecycle handling.
- 2026-08-06: Reproduced Hans's chatmenu loop and retained its screenshots,
  ordered events, packet and CS2 traces. Read the golden outbound decoder and
  corrected opcode 115 translation to preserve its trailing dynamic sub-id.
- 2026-08-06: Reproduced the independent running failure and retained the
  exception stack, final EVENTS records, server/client tails, fatal nine-byte
  NPC_INFO payload, and last completed framebuffer under
  `build/run239-regressions/diagnosis`.
- 2026-08-06: Walked the golden NPC traversal at the bit level: 13 tracked
  entries ended at bit 45 and the 27-bit suffix could not satisfy the client's
  28-bit add-record guard. Split the v5 tail, mirrored the exact sentinel
  threshold, and stopped queueing unsupported face-only v5 masks.
- 2026-08-06: Added literal revision-239 tests for opcode-115 uid/sub
  normalisation and the NPC_INFO bit-45/byte-aligned sentinel thresholds.
  Added a world self-test for the side-only poh_options close and changed the
  Hans self-test to exercise the actual six-byte resume packet.
- 2026-08-06: Ran `test-mock239-playerinfo` and `test-mock239-inbound` (48
  inbound checks) plus a full mock230 build successfully. The broad cached
  mock230 self-test reached the new side-only section without a new failure but
  retains 25 pre-existing content-gap/shadow failures; the complete output is
  `build/run239-regressions/selftest.log`.
- 2026-08-06: Extended JCTL with real AWT wheel input and authoritative camera,
  var, and NPC probes; instrumented both AWT wheel receipt and client polling.
  Rebuilt the instrumented client and passed API-surface verification.
- 2026-08-06: Proved the camera wheel and both sliders separately against live
  RuneLite, with FOV/varc changes and per-path CS2 traces. Proved house options
  opens and closes, including script 29, CLOSE_MODAL, and IF_CLOSESUB.
- 2026-08-06: Ran a clean Hans-only live session. The golden client emitted
  opcode 115 length 6 with sub 1, dialogue advanced to the selected player
  line, completed, and left GPI/client state healthy.
- 2026-08-06: Ran the first all-four session with EVENTS attached before launch;
  actual Talk-to Hans, all camera paths, house close, and movement passed. Its
  explicit quit exposed an instrumentation-only JProf percentile race, so the
  session was rejected as final evidence.
- 2026-08-06: Fixed JProf shutdown reporting by snapshotting `ringUsed` once,
  rebuilt the deob, and passed API verification again.
- 2026-08-06: Ran the final clean combined session with a pre-launch blocking
  EVENTS subscriber. Captured 14 presented-frame screenshots, six focused CS2
  traces, packet/GPI/NPC logs, and the final framebuffer. No game runtime fatal,
  decode error, writer gap, unexpected disconnect, or shutdown exception was
  present; the profiler report completed after explicit quit.
- 2026-08-06: Re-ran the literal camera RUNCLIENTSCRIPT fixture, 48-check
  revision-239 inbound suite, NPC_INFO sentinel/player traversal suite, full
  mock230 build, deob build/API verification, and whitespace checks. Created
  root regression commit `16b94123` and deob instrumentation commit `845c2c7`.
- 2026-08-06: Pushed `codex/runelite239-regressions` to both
  `MRobertEvers/oldschool-clientc` and `MRobertEvers/Deob`. No OSRS-Content
  change was needed: the defects were root protocol/lifecycle behavior and deob
  telemetry. Fast-forwarded the existing Deob PR #1 head
  `codex/runelite239-deob-telemetry` from `95a5db1` to `845c2c7`. GitHub CLI had
  no authenticated session and no signed-in browser surface was available, so
  the new root dependency PR creation URL was retained for handoff instead of
  fabricating a PR number.
- 2026-08-06: Found and stopped a retained task-specific movement client,
  mock230, jav_config, and duplicate EVENTS subscribers using only the PIDs in
  `build/run239-regressions/movement_fixed`; no client from this regression run
  was left idle.
- 2026-08-06: Added the reported client-layout regression and supplied Hans
  reference frame to the acceptance scope. Reproduced the dead Display dropdown
  in an isolated worktree and identified its CC-created rows as `116:40`
  subids 1..3 with no effective server op masks.
- 2026-08-06: Decompiled and traced authoritative scripts 3962, 3998, 4568,
  4569, and 7990. Established that `WINDOW_STATUS` is window mode 1/2 plus
  dimensions, the dynamic IF_BUTTON callback carries the three-way choice,
  script 4569 does not invoke 3998 for this row, and root replacement discards
  the dropdown's CC-created children.
- 2026-08-06: Armed the three layout rows, added exact selection validation and
  varbit synchronisation, latched Classic versus Modern before `WINDOW_STATUS`,
  queued script 3998/root replacement after the onOp, and replayed script 7990
  after `IF_OPENTOP`. Added a literal six-byte revision-239 IF_BUTTON1 layout
  persistence/root-remount self-test.
- 2026-08-06: Audited golden `Statics.method3791` and `method6166` against the
  rebuilt instrumented bytecode. Added live widget alignment, parent-bounds,
  listener, NPC hull, and deterministic NPC menu telemetry to JCTL. Found no
  remaining decompilation arithmetic defect; live geometry matched the golden
  width, height, x, and y modes.
- 2026-08-06: Proved layout switching independently under
  `build/run239-regressions/layout_final2`: Modern root 164/varbit 1, Fixed root
  548/window 1, and Classic root 161/window 2, with the dynamic dropdown
  recreated and correctly labelled after every root replacement.
- 2026-08-06: Recompiled 12,536 content scripts, rebuilt mock230, passed the
  five focused revision-239 suites (including all 48 inbound checks), rebuilt
  the instrumented deob and passed API verification. The correctly cache-backed
  broad self-test retained 23 pre-existing failures and introduced no layout
  failure; its log is `build/run239-regressions/broad-selftest-cache-final.log`.
- 2026-08-06: Ran `combined-five-final3` with a blocking EVENTS subscriber
  connected before launch. Used real AWT input to complete Hans, execute 24 run
  clicks, operate wheel and both sliders independently, select all three layouts,
  and open/close house options. Captured 22 screenshots and focused CS2 traces;
  final GPI, mounts, camera, and profiler state were healthy with no game fatal,
  decode failure, writer gap, or unexpected disconnect.
- 2026-08-06: Committed and pushed the OSRS-Content layout implementation as
  `92bca783ab` on `codex/runelite239-regressions`. Rebuilt and API-verified the
  deob after its final JCTL changes, then committed and pushed them as `45aa607`
  on the matching Deob branch. The original dirty primary worktrees remained
  untouched.
- 2026-08-06: Merged the latest root `v3` into the regression branch and the
  merged Deob `perf-instrumentation` base into its dependency branch. Deob
  rebuilt and passed API verification at merge head `3a4e2c9`. A root
  post-merge make invocation was blocked before compilation by conflict markers
  already committed in `origin/v3`'s `src/makefile`; the focused suites had all
  passed immediately before that base merge, and no unrelated base repair was
  folded into this PR.
- 2026-08-06: Opened root PR #10, OSRS-Content dependency PR #2, and Deob
  dependency PR #2 from their pushed `codex/runelite239-regressions` branches.
- 2026-08-06: Rejected the prior Hans screenshot as visual acceptance after
  comparing it directly with the supplied reference. Dumped raw cache group 231
  and found `chat_left:text` is `halign=1, valign=0, lineheight=16`; group 217's
  player text is already vertically centred.
- 2026-08-06: Read authoritative clientscript 600 and established that
  `if_settextalign(1,1,16,chat_left:text)` is the missing post-mount behavior.
  Added it to both NPC-chat entry points and recompiled all 12,536 scripts.
- 2026-08-06: Audited the original jar and rebuilt deob bytecode for widget text
  decoding and vertical font placement. Found equivalent arithmetic, so this
  defect is not a remaining decompilation/recompilation math error. Extended
  JCTL widget telemetry with text alignment/line height and made `clicknpc`
  select the menu entry with the requested NPC index when actors overlap.
- 2026-08-06: Added a world regression assertion that parses the literal
  revision-239 reverse-argument RUNCLIENTSCRIPT packet and requires script 600
  with `(1,1,16,231:6)`. The focused Hans assertion passes; the broad selftest
  retains the same 23 unrelated baseline content/combat/pathing failures.
- 2026-08-06: Ran clean `hans-vertical-final3` with a blocking EVENTS subscriber
  connected before launch and real AWT input. Captured the corrected reference
  line, completed choice/player/NPC/close lifecycle, retained packet/CS2 logs,
  and ended with healthy GPI and no runtime fatal, decode error, writer gap, or
  unexpected disconnect before explicit quit.
- 2026-08-06: Fetched the now-merged current root `v3`, merged it into the
  isolated regression branch, and rebuilt the official C client successfully.
  This used `v3`'s resolved integration files and preserved every unrelated
  dirty/generated file in both the primary checkout and isolated worktree.
- 2026-08-06: Rejected an initial C-client connection made against the default
  revision-230 mock wire (`rsa decrypt failed`), restarted the mock explicitly
  as `--rev osrs239`, and rejected the first successful-login frame because its
  one-shot cheat arrived behind the login scene barrier. Repeated the command
  only after that barrier for the actual Hans proof.
- 2026-08-06: Drove Hans through the rebuilt official C client and retained the
  real packet/CS2 trace and Soft3D framebuffer. It consumed
  `RUNCLIENTSCRIPT 600` after mounting group 231 and rendered the sentence at
  the reference vertical centre; no C-only decompilation or renderer-math fix
  was warranted.
- 2026-08-06: Added `test-cs2-text-align` for the literal golden script-600
  stack order, extended UITree mutation/emission coverage for alignment
  `(1,1,16)`, and added alignment fields to `TORIRS_DUMP_COM` telemetry.
- 2026-08-06: Fetched the newly advanced `origin/v3` head `6a69be24` and
  merged it cleanly into the isolated regression branch as `3cc412c9`. The
  debug-overlay changes auto-merged with the Hans telemetry in `main.c` and the
  new test target in `src/makefile`; no conflict resolution or unrelated-file
  mutation was required.
- 2026-08-06: Rebuilt the full official C client after that latest-v3 merge and
  re-ran `test-cs2-text-align` plus the expanded UITree suite; all passed. A
  fresh final TCP session used exactly one post-barrier Hans command, logged one
  dialogue `RUNCLIENTSCRIPT 600`, and exited with component `231:6` reporting
  `textalign=1,1 lineheight=16`. Its final `hans.png` visually matches the
  supplied vertical alignment and neither client nor server log contains a CS2
  failure or unexpected disconnect.
- 2026-08-06: Committed the official-C regression and telemetry as `046d3798`,
  committed the post-merge verification record as `2ad0e8fd`, and pushed the
  merged branch to `origin/codex/runelite239-regressions`. GitHub shows prior
  root PR #10 as merged, so this post-merge delta requires a follow-up PR; the
  CLI and the only available browser session were both signed out, and the
  prepared `v3...codex/runelite239-regressions` comparison was retained rather
  than claiming an uncreated PR.
- 2026-08-06: Fetched current `origin/v3` and created the clean isolated root
  worktree `/Users/matthewevers/Documents/git_repos/3draster-runelite239-zuk`
  on `codex/runelite239-zuk`, leaving the dirty primary repositories untouched.
- 2026-08-06: Connected a blocking EVENTS subscriber before launching the
  baseline client, used real AWT input for `::zuk`, and retained the two script
  739 SCALE exceptions, ordered packet/CS2 records, server log, interface audit,
  and last complete frame under `build/run239-zuk/baseline`.
- 2026-08-06: Proved cache varbits 5653/5654 share carrier 1575 at bits 0..10
  and 11..21. Captured mock230's two pre-mount VARP_LARGE bodies and matched
  their transforms and handler order against the authoritative golden client.
- 2026-08-06: Located the actual failure in the compilable deob's `class313`
  and `class419` mask-table initializers: a decompiler-narrowed `byte` local
  overflowed before the 11-bit mask. Restored the original integer semantics in
  the authoritative source and instrumented mirror.
- 2026-08-06: Added and ran `VarbitMaskRegression` as part of the deob build,
  rebuilt the injected client, and passed the 723-class/10,469-member API
  surface check with 740 rebuilt classes and 10,742 members.
- 2026-08-06: Added a root world regression for the packed value 2458800 and
  the literal six-byte base-only/combined revision-239 packet bodies. The
  correctly cache-backed broad self-test reached the new section without a Zuk
  failure; its unrelated pre-existing failures remain in the retained log.
- 2026-08-06: Ran fresh live acceptance with EVENTS connected before launch.
  `::zuk` produced carrier 2458800, varbits 1200/1200, mounted group 596, and
  widget text `1200 / 1200`; the client remained logged in with `dead=0` and no
  Zuk CS2 error, decode error, writer gap, or unexpected disconnect.
- 2026-08-06: Re-ran the deob build with the runtime mask-table gate, committed
  the authoritative and instrumented fixes as `e52e7148bf`, and pushed
  `codex/runelite239-regressions` to the MRobertEvers Deob remote. The isolated
  root branch remained based directly on fetched `origin/v3` at `2557dcec`.
- 2026-08-06: Committed and pushed the root Zuk carrier regression and evidence
  record as `48068a328b`. Merged the already-landed Deob PR #2 base back into
  its clean dependency branch, rebuilt successfully, and pushed merge head
  `142593f`. Opened Deob follow-up PR #3 and root follow-up PR #14 against
  `perf-instrumentation` and `v3`, respectively.
