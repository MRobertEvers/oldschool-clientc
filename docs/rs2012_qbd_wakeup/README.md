# QBD wake-up: root-caused

**Correction, superseding everything below the first section:** the original
version of this doc concluded the wake animation itself (`rs2012_seq_16714`)
was rendering broken/corrupted — flickering, disappearing, reverting to a
sleeping-looking silhouette for most of its ~17s. That conclusion was wrong.
The timelapse and screenshots are genuine, unedited captures, but they were all
taken against a **stale compiled server-script cache** that was still running
the pre-fix `^rs2012_qbd_wake_anim_ticks = 42` value from before an earlier
session's `c8b6dacb "fix qbd times"` commit — the source constant had been
edited to `28`, but nothing had recompiled `torirsserver-scripts`/`torirsserver-servpack`
since, so the running server never picked it up. What the timelapse actually
shows is the *old, already-diagnosed* bug: with the switch scheduled far later
than the animation's own ~17s runtime, the client's primary animation track
naturally finishes and falls back to `rs2012_qbd_sleeping`'s own idle
(`rs2012_seq_16716`) for several real seconds before `npc_changetype` finally
catches up — which looks exactly like "flickering / reverting to sleeping."
`rs2012_seq_16714` itself decodes and plays correctly, once, start to finish.

## Root cause

Two independent off-by-N errors, both now fixed in
`rs2012_qbd.constant`:

1. **The compiled cache was stale.** Editing `.rs2`/`.constant` source has no
   effect until `torirsserver-scripts` and `torirsserver-servpack` are rebuilt — this
   project has no watch/auto-recompile step. Confirmed precisely by
   instrumenting the server's queue dispatcher (`TORIRS_ANIM_DEBUG`, see below):
   `rs2012_qbd_begin` (the switch) fired **43** real ticks after
   `rs2012_qbd_wake`, not the `28` the (edited but unbuilt) source specified —
   and `43 = 42 + 1`, an exact match for the *old* constant plus `queue()`'s own
   `+1` (below). Rebuilding the scripts and re-measuring dropped it straight to
   the expected value.
2. **`queue(proc, N, arg)` waits `N + 1` real ticks, not `N`.** `SS_OP_QUEUE`
   stores `delay + 1` and the drain pre-decrements before checking
   (`torirs_server_scripts.c`, "+1 so delay 0 means next tick, not this one"). The
   `28`-tick constant was computed as the animation's raw duration
   (`ceil(836 cycles * 20ms / 600ms) = 28`) and passed straight to `queue()`,
   which actually produced a 29-tick wait — one tick past the animation's
   natural completion. The stored constant is `27` (later tightened to `26`,
   see below) so that `queue()`'s own `+1` lands on the intended real-tick
   count.

Verified with a raw tick trace (`TORIRS_ANIM_DEBUG=1`, server-side, printing
every `queue()` dispatch with its firing tick):

```
stale cache, old 42-tick constant:  wake@11 → begin@54   (43 real ticks — the bug)
rebuilt, constant=28:               wake@11 → begin@40   (29 real ticks)
rebuilt, constant=27:               wake@10 → begin@38   (28 real ticks — exact)
rebuilt, constant=26:               wake@11 → begin@38   (27 real ticks — current)
```

At `constant=26` the switch lands on wake-animation frame ~104 of 108 —
comfortably past the sequence's one real motion burst (frames 80–84, where the
model's bounds radius actually changes) — so `npc_changetype`'s own idle
pre-empts the tail of the wake animation rather than waiting for it to run
out and fall back to sleeping idle first.

## Rebuilding after a `.rs2`/`.constant` edit

```sh
export PATH="$PWD/toolchains/mingw64/bin:$PATH"
mingw32-make -C src torirsserver-scripts torirsserver-servpack
mingw32-make -C src EMBED_SERVER=1 CC=gcc win64
cp src/torirs_win64.exe dist/win64/torirs.exe
```

`torirsserver-scripts`/`torirsserver-servpack` do **not** depend on `torirsserver-cache-rs2012`
(the client-facing asset cache) — they only need `sscompile` and the
`server/pack` band. Rebuilding just those two is enough for a script/constant
change to take effect; you do not need a full cache rebuild for that.

### A pre-existing, unrelated build break this hit along the way

`torirsserver-scripts` fails out of the box on `OSRS-Content/.../ported_scape2009_
summoning/scripts/summoning_spirit_wolf.rs2:22: unknown variable
'%content_restrict_summoning_serverside'`. The varbit is declared
(`ported/scape2009_summoning/configs/summoning.varbit`) but `sscompile`'s
`--pack` list for that lane only includes `pack/`, not `configs/` — and the
symbol lives in the aggregate `configs/all.varbit.compack`, not the standalone
`.varbit` file. Fixed in `src/makefile` by staging just that one file
(`SUMMONING_VARBIT_STAGE`) into an extra `--pack` path, rather than adding the
whole `configs/` directory — that directory holds ~1600 text records for every
summoning cohort creature that are known-stale against the lane's own `pack/`
(see the `torirsserver-cache-summoning` comment in the makefile), and pointing
`sscompile` at all of it reintroduces that drift into every other lane's
symbol table. Confirmed the hard way: doing it the broad way was tried first
and it visibly corrupted QBD's own rendering (see below) — the scoped,
single-file fix does not.

## Open issue: cache-rebuild also broke QBD's render — separate from the above, unresolved

Getting `torirsserver-scripts` to rebuild at all first required running
`torirsserver-cache-rs2012`, which deletes and fully repacks `cache.osrs239.rs2012`
from scratch. That repack's own `cachepack verify` step failed on real,
pre-existing fidelity checks (sprite payloads changing length; `script 0`
failing to decode with `trailer=modern`) — on the very first attempt, before
any change in this investigation. Separately, and more visibly: **after the
repack, QBD's post-`npc_changetype` idle pose renders as a white/jagged mess**
instead of the correct red-eyed dragon head, reproducibly, across multiple
fresh runs and independent of the scripts fix above (confirmed with the
properly-scoped summoning fix in place, so it is not the same regression).

This was not present before the from-scratch cache rebuild — a capture taken
earlier in this investigation, against the *original* `cache.osrs239.rs2012`
(committed timestamp before any of this session's cache work), shows the
correct pose at the same point in the encounter. The regression tracks with
*rebuilding the cache*, not with any source edit in this thread, and most
likely shares a cause with the `cachepack verify` failures above (something in
the RS2012 asset re-port that the previously-shipped, un-rebuilt cache was
masking). **Not investigated further** — flagged here for whoever picks up
the cache/asset side. `cache.osrs239.rs2012` is gitignored, so this only
affects local rebuilds; nothing in git is broken by it.

## Diagnostic tooling added this session

All gated behind `TORIRS_ANIM_DEBUG` (a pre-existing flag in this codebase;
extended here to new call sites, not introduced) and safe to leave in place
uncommitted-behavior-wise — every print is a no-op unless the env var is set:

- `3rd/toridraw/toridraw_scene.c` (`ToriDraw_SceneElementApplyAnimation`):
  `anim: element=N primary=B seq=S frame=F verts=V radius=R min_y=Y max_y=Y` —
  per-render pose/bounds for one scene element.
- `src/world/world_cycle.c`: `loopback: seq=S frame=F count=C frame_step=FS
  max_loops=ML loop=L stepped=ST -> STOP|LOOP` — fires once per animation
  loop-back/stop decision.
- `src/torirsserver/torirs_server_scripts.c` (`drain_queue`): `queue: script=S FIRE
  tick=T` when a queued proc actually runs, and `queue: script=S BLOCKED
  tick=T delayed_until=D mainmodal=M chatmodal=C` when a due entry is held by
  `player_can_access`.
- `src/torirsserver/torirs_server_combat.c` (`ToriRSServer_AnimPlayNpc`): `srv: npc_anim
  npc=P seq=S delay=D (was playing W)` — every server-side `npc_anim` call.
- `src/main.c` (`TORIRS_BMP_SERIES` writer): `bmp_series: frame_count=N` —
  correlates a screenshot's app-level frame number with the above traces from
  the same run, since run-to-run login/load jitter otherwise makes frame
  numbers useless across separate processes.

`src/engine/proctex/test/rs2012_wake_dump.c` (`make -C src rs2012-wake-dump`)
dumps decoded per-bone transform values and framemap bone-group structure for
every frame of a given sequence, straight out of a cache directory, no
rendering — used to rule out (not confirm) a pose-decode bug in
`rs2012_seq_16714` before the real cause was found. Kept for the next time
someone needs to diff decoded keyframe data directly.

## Reproducing the tick measurement

```sh
SDL_VIDEODRIVER=dummy TORIRS_MAX_FRAMES=1400 TORIRS_ANIM_DEBUG=1 \
  ./src/torirs_win64.exe --manifest manifests/manifest_osrs239_rs2012.ini \
  --user <name> --pass test --soft3d 2>&1 \
  | grep -E "^queue: script.*FIRE|message_game: The Queen"
```

`manifests/manifest_osrs239_rs2012.ini` carries `[net:boot] cheat=rs2012qbdmanifest`,
which starts the encounter automatically on login. Login/asset-load latency
jitters by hundreds of frames run to run, so don't rely on absolute frame
numbers across separate processes — correlate within one run (see the
`bmp_series` marker above) or key off tick numbers from the queue trace, which
are stable relative to each other within a run.

## The original (superseded) timelapse

`timelapse.png` and `frames/*.png` are kept as-is: a real capture, just of the
*old* bug (stale-cache, 43-tick gap) rather than of animation-data corruption.
They still accurately show what the falls-back-to-sleeping-idle gap looks
like, which is useful context even though the diagnosis text above them no
longer applies.
