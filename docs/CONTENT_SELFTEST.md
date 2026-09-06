# Fast content acceptance tests

The canoe pilot runs inside a **persistent client with an embedded server**.
Both use one controlled clock. Thirty client frames advance one 600 ms server
tick without sleeping for 600 ms. Cache IO and CS2 work settle before the clock
continues. Inputs travel through the normal client encoder and server handlers;
client state is inspected after decoding the responses.

The pilot renders selected checkpoints with `App_RequestScreenshot`, checks the
pixels against reviewed fixtures, and checks server and client state separately.
It does not treat a server varbit or the existence of a PNG as rendering proof.

## Run

From the repository root, with `cache.osrs239` and a current base script pack
(`make -C src torirsserver-scripts` refreshes a missing/stale base pack):

```sh
make -C src content-test-client
make -C src OPT=1 sscompile
.venv/bin/python tools/content_selftest.py start
.venv/bin/python tools/content_selftest.py canoes
```

The Python environment needs Pillow for image comparison. Use the repository's
`.venv`, or install Pillow in your development environment. This driver currently
targets the native macOS/Linux SDL client. It creates an isolated account, save
directory, preferences and manifest. It skips Tutorial Island by setting the
new-account home to the canoe station. It does not use a normal player's save.

Repeat `canoes` to run again **without restarting either process**. Results,
full PNGs and any difference images are in `build/content-test`. The process
stays paused when the controller finishes. Use `--session DIRECTORY` **before**
the subcommand for a separate session. One controller owns each session.

```sh
.venv/bin/python tools/content_selftest.py stop
```

The focused server gate is independent of the client:

```sh
make -C src test-canoes-server
# Subsequent runs need neither a rebuild nor the rest of the server suite:
TORIRSSERVER_SELFTEST_CANOES_ONLY=1 TORIRSSERVER_CACHE=cache.osrs239 \
  src/build_opt/torirsserver --selftest
```

`TORIRSSERVER_SCRIPTS=/absolute/pack/directory` selects a different pack for the
focused gate. Missing or stale scripts fail; they are not a skipped success.

## Edit without a restart

Start the compiler once alongside the client:

```sh
.venv/bin/python tools/content_selftest.py compiler
```

It performs one full compilation, retains the declarations/symbols in memory,
and writes an isolated pack under the session directory. After a body edit:

```sh
.venv/bin/python tools/content_selftest.py recompile \
  OSRS-Content/osrs239-content/server/scripts/canoes/scripts/canoe_cutscene.rs2
.venv/bin/python tools/content_selftest.py canoes
```

`SSC_RecompileFile` compiles into a separate owner and replaces the old bodies
only on success. It preserves script IDs and checks declarations. Syntax errors,
added/deleted scripts, and incompatible argument/return signatures fail without
replacing the last good bytecode. Tests execute a caller after each rejected edit
to prove that rollback works. Configuration/symbol changes require restarting the
compiler for a full build (`compiler --restart`); the controller checks their
fingerprint before reuse.
The low-level `sscompile --serve DIR` interface is intended to be driven through
this controller, which owns that dependency check.

Reload clears suspended VM states. Finish or close the current interaction and
rerun the scenario from its fixture; this is not arbitrary mid-quest rollback.
Client C changes and cache asset changes still need their normal rebuild/load
workflow. The warm path is for existing `.rs2` script bodies.

## Coverage and limits

The server pilot checks all **440** combinations of ten departure stations,
four canoe types and eleven candidate destinations against an independent
minimum-boat table. This includes cross-river rejection, same-station rejection,
Ferox asymmetry and the Wilderness restriction. It also drives Lumbridge's
chop → shape → float → board → refused destination → valid ride chain through
packets, including XP, state transitions and arrival.

The client pilot checks:

- The real shaping widget created by CS2 and the destination-map widget.
- Decoded station/type varbits against server values.
- The rowing sequence and advancing animation frames.
- Moving scenery received as NPCs in the client world.
- Camera lock, cutscene coordinates, arrival, camera reset and chat-input release.
- All four canoe models in the cave scene, with old scenery allowed to expire
  between rides so prior fixtures cannot contaminate the next image. These debug
  rides are rendering fixtures; the route matrix separately enforces waka-only
  access to the Wilderness.
- Eight pixel regions covering menus, river motion and the four cave hulls.

The screenshots exposed a real defect: the cave eye was too close and the waka
bow disappeared behind the sidebar. The canoe camera constants now move that eye
back and aim farther along the channel. The checked-in cave regions record the
corrected framing.

The current visual oracle is **Soft3D at 807×503 with the revision-239 cache**.
It compares lightly blurred, resized regions with a mean RGB error limit of 3/255.
FPS/chat/history/sidebar noise is outside the regions; avatar pose regions are
masked because animation is checked semantically and varies with entry timing.
This catches substantial missing/incorrect graphics and framing regressions; it
is not a pixel-perfect oracle for every animation frame or every UI element.
GPU renderers, other viewport sizes, all assistants/axe-storage branches, and
full construction at the other nine stations are not certified by this pilot.

To change visual fixtures deliberately, inspect the full checkpoint PNGs first:

```sh
.venv/bin/python tools/content_selftest.py visuals --record
.venv/bin/python tools/content_selftest.py visuals
```

Ordinary `canoes` runs never update baselines. They fail and write a difference
image when a region exceeds its tolerance. The fixtures are small region images
in `tools/testdata/canoes`; full captures remain in the session directory.

## Extending to quests

Add a scenario function that seeds an isolated account, sends normal interactions,
waits on bounded state predicates, and asserts both decoded state and visual
checkpoints. Name widgets/varbits/locs through the content symbol table instead of
copying numeric IDs into the controller. Use checkpoints around dialogue pages,
multiloc changes, camera transitions, moving NPCs, rewards and cleanup.

The shared mailbox also supports `state`, `step N`, `cheat TEXT`,
`loc OP X Z NAME`, `button COMPONENT SUB OP`, `resume COMPONENT`, `close`,
`varbit NAME`, `npc NAME`, `widget COMPONENT SUB`, and `shot /absolute/path.png`.
`step` counts 20 ms client frames; `resume COMPONENT` resumes dialogue, whereas
bare `resume` enables real-time running in the interactive harness extension.
The canoe launcher enables checkpoint rendering and normal tick-only publication;
interactive sessions can retain continuous drawing and publish paused fixture
changes. See the separate sailing harness for its movement checkpoint commands.

No quest is marked validated simply because the canoe pilot passes.

## Measured validation on the development Mac

- Focused server: 464 checks, zero failures, 3.17 seconds including boot.
- Warm client pilot: 4.45–4.74 seconds including captures and visual comparison.
- Existing cutscene body: 23–33 ms compile, 56–72 ms compile plus VM reload;
  about 0.62 seconds including the controller's dependency fingerprint check.
- One-time full compiler startup: about 19–30 seconds.
- Removing the real cutscene camera call failed the camera checkpoint; restoring
  and reloading it made the pilot pass in the same client process.
- A black cave capture failed at mean error 53.0; substituting the log image for
  the waka failed at 8.5, both against a limit of 3.0.
- `make -C src OPT=1 test-ssc` passes, including incremental rollback tests.

These are measured pilot timings, not budgets guaranteed on other machines.
