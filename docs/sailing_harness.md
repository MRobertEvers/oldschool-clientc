# Sailing acceptance harness: design

Keep one native client and its embedded server alive. Load the revision-239
cache once, then inspect and manipulate the same session through a local file
mailbox. `tools/sailing_harness.py` is the controller; `ContentTest_Begin/End`
owns the application boundary. No rebuild or login belongs in a warm command.

## Time and transaction boundary

The client and server share a virtual millisecond clock. A `step N` command
advances N client cycles of 20 ms, including one server tick per 600 ms. Pausing
freezes that clock while cache IO, packet application, interface scripts and
rendering can finish. `TORIRS_MAX_FRAMES` must be unset: its legacy mode forces
a cycle every frame even when the virtual clock is frozen.

**`step N` counts client FRAMES, not server ticks.** One server tick is about
**30** of them. `step 5` settles a menu or a repaint and reaches no tick at all;
anything whose answer comes from the server — a packet's reply, a script's
`mes`, a varbit write, a walk step, a grant — needs `step 30` per tick it has to
cross, and `step 90` is the usual "one tick to send, one to run, one to
publish". A check that reads its state back after `step 5` is reading the frame
before the server has spoken, and it will fail intermittently rather than
consistently, which is the expensive way to find this out.

A command completes only after its effects settle. Publish JSON by renaming
`response.tmp` to `response`; the controller publishes a complete `request` by
the same method. Serialize controllers with an advisory lock. Timeouts retain
the unanswered request state, so a late answer cannot be mistaken for the next
command's answer. Report the process exit and log tail on failure.

## Reviewable state and checkpoint scope

`state` reports online/readiness, virtual time, player position, vessel position,
heading, sails, speed, helm ownership and camera. `save NAME` and `restore NAME`
operate in the warm session and report their checkpoint scope explicitly.
Sailing acceptance needs player and vessel state, facilities/cargo, relevant
varps, camera and interpolation state to agree after restore. A player save
file alone is insufficient; shallow copying pointer-owning App/world structures
is invalid. Checkpoints are local to the running process, not persistent saves.

`capture` must use the selected renderer's completed framebuffer and report its
path. A GPU acceptance capture must not silently call the software renderer.
The controller checks that a nonempty image exists before reporting success.

## Controller interface

All commands print one JSON result, including elapsed command milliseconds.
The default session directory is `/tmp/3draster-sailing-<uid>`. `--session`
selects another directory; each session keeps its PID, launch metadata, log,
isolated player saves, mailbox and captures together.

```sh
python3 tools/sailing_harness.py start --headless
python3 tools/sailing_harness.py status
python3 tools/sailing_harness.py pause
python3 tools/sailing_harness.py save ocean
python3 tools/sailing_harness.py cheat 'sails'
python3 tools/sailing_harness.py step 30
python3 tools/sailing_harness.py capture /tmp/sailing-moving.png
python3 tools/sailing_harness.py restore ocean
python3 tools/sailing_harness.py click 340 230
python3 tools/sailing_harness.py camera 256 256 100
python3 tools/sailing_harness.py bench --iterations 20
python3 tools/sailing_harness.py stop
```

Additional commands expose `button NAME SUB OP`, `widget NAME SUB`,
`varbit NAME`, `hover X Y`, `close`, `reload`, and a raw mailbox command for
focused investigation. Useful raw queries include `collision 20` (independent
boat and player maps), `inventory 963` (server, local and captain containers),
`activity SLOT` (facility registers, target health and experience), and
`scenery X Z` (the actual rendered model and animation on land or a boat deck).
`start` launches the existing binary with the sailing manifest and dedicated
test credentials; prepare/build content separately before starting. An existing
healthy session is reused. No fixed multi-second sleeps are used; startup waits
on readiness and mailbox progress with short polling intervals. Readiness means
both client and server are aboard the same vessel view with the helm held;
boot advances complete server ticks until the initial boat/player packets agree.

Startup places the player aboard a skiff at ocean tile `(3072, 3160)` with the
helm held and sails down. This is natural cache water near Port Sarim. The
controller opens Sailing Options and sets a wide side view so the whole boat
and its facilities are immediately visible. The
default software renderer can run visibly or with `start --headless`; use
`start --renderer gl3-zbuffer` for a visible GPU verification session. A renderer
change requires stopping and starting the session. Captures are PNG files from
the selected lane. `recover` collects a response after a controller timeout.

Checkpoints restore movement, helm ownership, player stats/experience, varps,
backpack, worn equipment, all five cargo holds, retained net catches, facility
options and operating registers, camera, and the exact hull interpolation
phase. Scalar player queues and timers are restored with relative timing.
They retain the same vessel identities and deck geometry; creating, deleting
or resizing a vessel requires a new fixture. Changes to installed facilities
are restored through the same placement code used by customisation.

Stop active facility operations before saving or restoring. Checkpoints reject
suspended scripts and active encounters because they do not rewind other
actors, damaged targets, or depleted world resources. They also do not restore
general interface state; reopen the desired panel for comparison. `resume`
lets the shared virtual clock follow wall time until `pause` freezes it again.
Reloading scripts clears checkpoints because their queued script IDs belong to
the previous pack. Live player queues and timers are rebound by script name
and argument count; removed or incompatible callbacks are cancelled. The
loaded pack hash is updated even when a timed-out reload is collected later.

`raw 'text Cargo'` types through the normal keyboard event path into the focused
native input. `raw 'key enter'` submits it; escape, backspace and tab are also
available. Text commands accept up to 63 printable ASCII characters, enough
for native player-name, quantity and settings-search inputs. `activity SLOT
BOAT` can inspect the captain's boat while the player is ashore, for example
when testing the extractor's grace period.

## The second player

`peer` drives a real second `ToriRSServerPlayer` that has no transport of its
own: everything it does reaches the captain's client through ordinary
PLAYER_INFO. `peer` alone reports its state; the rest are subcommands.

```sh
python3 tools/sailing_harness.py peer create Deckhand
python3 tools/sailing_harness.py peer place 0 3069 2987
python3 tools/sailing_harness.py peer walk 3072 3162 1
python3 tools/sailing_harness.py peer varbit sailing_sidepanel_crew_slot_1
python3 tools/sailing_harness.py peer proc sailing_cargo_open
python3 tools/sailing_harness.py peer button sailing_boat_cargohold:items 0 3
python3 tools/sailing_harness.py peer oploc 3 3070 2987 sailing_gangplank_the_pandemonium
python3 tools/sailing_harness.py peer namedialog socialdock
python3 tools/sailing_harness.py peer target sailing_sidepanel:crew_content_clicklayer
python3 tools/sailing_harness.py proc sailing_cargo_deposit 1 14 25 10
```

The verbs a guest needs that a keyboard and mouse would otherwise supply:

- `peer oploc OP X Z LOC` sends OPLOC&lt;op&gt; — `p2 x, p2 z, p2 locId`, the same
  bytes a second client would put on the wire — so the server walks the guest
  to the loc and runs the bound `[oploc<n>]`. `LOC` is a content symbol.
- `peer namedialog TEXT` answers a `p_namedialog` the guest's script is parked
  on with RESUME_P_NAMEDIALOG. Without it every `p_namedialog` in a guest path
  (`~sailing_board_friend` is the one that matters) is unreachable. 1..63
  printable ASCII.
- `peer button NAME SLOT OP` is the numeric `peer button GROUP COM SLOT OP` with
  the component named the way the script names it.
- `peer target COMPONENT` is the CAPTAIN's half of a player-targeted interface
  op: the OPPLAYERT an already-armed native click sends, aimed at the guest. It
  separates arming (the mouse's job) from the grant (the server's) — it is not a
  substitute for the click, and a run that uses it has to say so.

`proc NAME [int args]` runs a named content proc in the PRIMARY player's
context, the mirror of `peer proc`. Up to four integer arguments.

`peer` reports the guest's server position and role, the client actor it became,
and two readings the social proofs are about:

- `screen` — the guest's projected on-screen point plus `picked`, the client's
  OWN world-pick verdict for that actor. Aim `hover`/`click` at `screen.x/y`
  and read `picked` back: a projection that disagrees with the client fails the
  proof instead of quietly clicking empty water.
- `permissions` — `navigate` and `cargo`, straight from
  `ToriRSServer_VesselCanNavigate` and `ToriRSServer_VesselCargoAllowed` on the
  captain's hull.

`state`'s `sailing.vessel` also carries `owner_uid`, `cargo_slot` and
`navigator_mask` (vessel_stat 11), so a permission change is read as a number
rather than inferred from a message.

`start --fixture-tile X Z` spawns and boards the fixture hull on another tile;
the default is the surveyed ocean at 3072,3160. The Pandemonium berth is
`--fixture-tile 3073 2987`, which is where the dock-only content lives
(Board-friend exists at a mooring and nowhere else).

For a real persistence check, `raw logout` closes the production network
transport and lets the server perform its ordinary disconnect/save path.
After the save file appears, stop the client and use `start --resume-save`.
That mode suppresses both environment and manifest fixture cheats and waits
for the loaded character's actual shore/deck state; it does not spawn a fresh
boat, move the character, or take the helm. A missing save is an error.
Ordinary fixture startup archives any previous character save under
`saves/history/` before seeding a fresh fixture, preventing a restored old boat
from surviving beside the new test boat. The archive path is recorded in the
session metadata. Starting an already-live session still reuses it unchanged.
`start --scripts /path/to/compiled-pack` selects an isolated compiled content
pack and records its hash, which permits independent content validation during
concurrent development.

**`osrs239 login: rejected reply=<n>` at the tail of a logged-out session's
log belongs to that session, not to the next one.** `raw logout` drops the
transport; the client then tries its own automatic reconnect, and over the
embed transport that starts a brand-new embedded server which answers
GAMERECONNECT. `<n>` is not a login response code — the whole set is in
`src/net/rev/osrs239/loginblock.h` and 195/209/239/16 are none of them. It is a
byte of ISAAC keystream, which is why the same failure reports a different
number every time and reads as flaky. Diagnosis and fix:
`docs/sailing_validation/lifecycle-reconnect-results.json`. **The defect
(LIFE-1) is fixed as of the 09:35 shared pair `5e34b81f` / `6a0e7f04`** — five
consecutive aboard logout cycles reconnect cleanly with no rejection line
(`/tmp/sailing-fin3-life/client.log`). A rejection line in a log from an earlier
binary is that binary's, not a live defect. Two consequences
for anyone reading a log: a `--resume-save` process always logs in cleanly
(a fresh GAMELOGIN answers before any game packet), and a rejection line only
appears when the character's save reconstructs a boat — an ashore logout
reconnects silently.

**`::vesselspawnat` does not pick a legal berth, so `--fixture-tile` cannot
prove turning at a dock.** `ToriRSServer_VesselRecover` requires the hull's
footprint to be clear at **all 16 headings** before it will place a boat;
`::vesselspawnat` — which is what the harness's `--fixture-tile` uses — applies
no such check and will happily drop a hull into a berth it cannot rotate in.
Measured on 2026-09-07 at `--fixture-tile 3073 2987`: native SET_HEADING turned
0 → 3 cleanly, then 3 → 8 printed "The boat runs aground", HP fell 80 → 78 and
the angle stalled at 256 (labelled control
`sailing_validation/lifecycle-cheatberth-aground.png`). Materialise the hull
through the **content selector** instead — gangplank `oploc1` →
`~sailing_select_owned(3, dock, coord)` → native interface 934 "Board Boat" →
Board. That lands it on the recovery berth (3073,2984 at the Pandemonium dock,
reproduced identically by two independent processes), where four headings
covering a full 360° turn in place with the hull's fine position unchanged and
no grounding line. **Anything proving turning at a dock must use the selector,
not the cheat.** The hull does not need `Junior Jim` or the shipwright first: it
is freed at logout, so `sailing_new_hull`'s `vessel_recover` picks the berth.

**Do not send `raw 'key escape'` to dismiss a minimenu.** Measured on
2026-09-07 (session `/tmp/sailing-fin-lifecycle-sail`, pid 42522): the command
never answered the mailbox, three `recover` calls timed out, and the client sat
at 100% CPU repeating `scene built at zone …` until it was stopped. Dismiss a
menu with a left click on `Cancel`, or skip the menu entirely — a plain `click`
executes the first option without opening one.

**An isolated pack directory must be NESTED one level below a directory of its
own** — `/tmp/sailing-<name>-pack/build`, never `/tmp/sailing-<name>-pack`:

```sh
mkdir -p /tmp/sailing-<name>-pack/build
src/build_opt/sscompile --src OSRS-Content/osrs239-content/server/scripts \
    --out /tmp/sailing-<name>-pack/build \
    --content-root OSRS-Content/osrs239-content
python3 tools/sailing_harness.py start --scripts /tmp/sailing-<name>-pack/build ...
```

Two different staleness gates read that path and only one of them is this
tool's. `tools/server_scripts_stale.py` (the controller's preflight) takes the
pack directory as `--out` and compares it against `--tree`, which defaults to
the content tree, so a flat directory passes it — which is exactly why the
failure arrives later, from somewhere else. The **server's own** gate is the
one that cares: `scripts_newer_than_pack`
(`src/torirsserver/torirs_server_scripts.c`) derives the sources as the
**parent of the pack directory** — `dir` is assumed to be `<tree>/build` — and
walks it depth-first for any `.rs2`/`.constant`/`.dbrow`/`.dbtable`/`.varp`
newer than `script.dat`. Point it at `/tmp/sailing-<name>-pack` and the "source
tree" it walks is the whole of `/tmp`. Measured on this fixture:

- a single unrelated `/tmp/*.rs2` newer than the pack — another worker's staged
  content, a scratch copy — makes the embedded server print the STALE SCRIPT
  PACK banner and `exit(1)`, and the client dies at boot naming a file that has
  nothing to do with it;
- even when nothing trips it, the walk costs real time: **20.8 s** of startup
  against **3.9 s** for the nested pack, every start;
- and the ledger half of the check is silently lost, because it looks for the
  `.alloc` files at `<pack dir>/../../pack`, which from `/tmp` is `/pack`.

The nested directory makes that walk see only the pack's own `build/`, which
the scan skips by name. `TORIRSSERVER_ALLOW_STALE_SCRIPTS=1` silences the
server gate, but it silences the real staleness check with it; nest the
directory instead.

## Performance acceptance

Measure cold launch separately from warm state queries, stepping and capture.
The target is under 100 ms for a warm query/capture, measured rather than
assumed. `bench` reports median, p95 and maximum per operation. Step benchmarks
advance real game simulation and therefore use a saved checkpoint when the
runtime supports it. Every visual assertion must inspect the captured image;
JSON state alone cannot establish that the hull, deck facilities or Sailing
Options interface look correct.

**Measured on the 08:08 pair** (`src/torirs 4854e303…`,
`src/build_opt/torirsserver 214b43d7…`, pack `902a6b8d…`) on 2026-09-07 08:53,
and **not** re-measured on the 09:35 final pair (`5e34b81f…` / `6a0e7f04…`,
same pack). The final pair's only render-path difference removes two per-frame
allocations from the sailing paint-order pass, so these numbers stay relevant as
measured; no new number was taken.
on a machine checked to be idle first, 20 samples per operation. Full data,
including the process list and a retained unfavourable set, is in
`sailing_validation/harness-performance.json`.

| Operation | Median | p95 |
|---|---:|---:|
| `state` | 2.795 ms | 2.865 ms |
| `pause` | 2.825 ms | 5.308 ms |
| `save` | 2.780 ms | 2.849 ms |
| `restore` | 7.799 ms | 7.865 ms |
| `step 30` (one server tick) | 15.424 ms | 17.324 ms |
| `capture` (real software renderer PNG) | 42.295 ms | 42.977 ms |

Cold start is **not** part of the 100 ms warm target and is reported on its own:
2.479 s and 2.473 s from empty session directories on the idle machine, 2.698 s
for a run's first launch with a cold page cache.

**A timing taken while other clients or builds are running is not a
measurement.** The final acceptance phase ran several clients concurrently; every
`elapsed_ms` recorded in those result files is scheduling noise and is labelled
as such. Check the process list before benchmarking, and if a foreign build
starts mid-run, keep the contended numbers and say so rather than quietly
re-rolling for a better set.
