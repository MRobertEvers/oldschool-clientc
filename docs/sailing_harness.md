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
`varbit NAME`, `reload`, and a raw mailbox command for focused investigation.
`start` launches the existing binary with the sailing manifest and dedicated
test credentials; prepare/build content separately before starting. An existing
healthy session is reused. No fixed multi-second sleeps are used; startup waits
on readiness and mailbox progress with short polling intervals. Readiness means
both client and server are aboard the same vessel view with the helm held;
boot advances complete server ticks until the initial boat/player packets agree.

Startup places the player aboard a skiff at ocean tile `(3072, 3160)` with the
helm held and sails down. This is natural cache water near Port Sarim. The
default software renderer can run visibly or with `start --headless`; use
`start --renderer gl3-zbuffer` for a visible GPU verification session. A renderer
change requires stopping and starting the session. Captures are PNG files from
the selected lane. `recover` collects a response after a controller timeout.

Checkpoints restore sailing state within the same deck/facility topology; a
topology change is rejected explicitly rather than partially restored. They do
not claim to snapshot the entire world, other actors, arbitrary script queues,
or general interface state. Reopen the required sailing panel when comparing a
checkpoint. `resume` lets the shared virtual clock follow wall time until
`pause` freezes it again.

## Performance acceptance

Measure cold launch separately from warm state queries, stepping and capture.
The target is under 100 ms for a warm query/capture, measured rather than
assumed. `bench` reports median, p95 and maximum per operation. Step benchmarks
advance real game simulation and therefore use a saved checkpoint when the
runtime supports it. Every visual assertion must inspect the captured image;
JSON state alone cannot establish that the hull, deck facilities or Sailing
Options interface look correct.
