# Plugin callback and redraw measurements

Set `TORIRS_PLUGIN_TELEMETRY=1` for an opt-in host measurement window. Set
`TORIRS_PLUGIN_TELEMETRY_START_FRAME=200` to begin at scenario frame 200, after
startup and initial asset loads. With `TORIRS_SIM_AFTER_READY=1`, the measurement
also waits for the gameplay-ready boundary. No timing clock reads or telemetry
allocation occur in plugin dispatch when the measurement is disabled.

At exit, before diagnostic BMP rendering, the client writes:

- `PLUGIN_CALLBACK`: one row per plugin and callback, including zero-call slots.
  `subscribed` is the current callback-table/listener census; `running` is the
  live lifecycle state. `calls` is actual invocations in the measurement window.
  `elapsed_ns` and `max_ns` measure monotonic wall time around the callback.
  `self_ns` removes time spent in nested plugin callbacks. These are callback
  times, not an attribution of the client's entire frame cost to that plugin.
  The shell clock has microsecond resolution, expressed in nanoseconds.
- `PLUGIN_MUTATION`: validated retained requests, actual retained changes and
  redraw requests, separately. Widget changes include latent owner intent;
  redraws require a paint, topology or layout publication. Repeating a winning
  value is an attempt with no change or redraw. A later owner can still claim a
  property, and release restores the most recent remaining owner/native value.
  Instance counters cover position and active setters, including unchanged
  early-outs. Panel counters currently cover structured select option updates.
  They are not a census of every retained API method.
- `PLUGIN_LOOP_TELEMETRY`: actual loop iterations, logic ticks, emitted redraws,
  fresh presentations, retained presentations, draw-cap skips and the frame
  counter delivered to plugins. `draw_work_us` and `idle_work_us` are work before
  pacing sleep, split by whether the iteration presented a fresh frame.
  `render_present_us` is only the fresh render/present interval. An emitted
  redraw and a presentation are different events; do not infer either from
  setter attempts or the overall frame duration.

`TORIRS_DRAW_CAP_FPS=15` is an independent diagnostic gate on fresh draws. It
changes no preference, logic clock or loop pacing deadline. Use the same scene
with `TORIRS_DRAW_CAP_FPS=50` (or omit the diagnostic cap) as the paired control.
Keep normal frame caps at 50 or higher and `TORIRS_PACER_ADAPT=0` when isolating
50 Hz callbacks from 15 Hz draws. `TORIRS_FRAME_MS=20` controls the normal loop
budget; setting it to 66 is not the 15-draw/50-loop experiment.

The gate uses rational deadlines and never repays late frames with a burst.
Async loading can increase loop frequency; inspect the measured counters rather
than assuming the loop achieved 50 Hz. Do not run competing clients/scorers
while comparing callback times.

`FPS` uses fresh presentations from all renderers. The counter increments once
at `App_DrawComplete`; extra BMPs and screenshot fallback renders do not count.
Explicit headless simulation frames count as their own presented frames.
`Frame`/`Effective FPS` sample the preceding iteration's work only when that
iteration presented a fresh frame, so skipped draws do not dilute render cost.
The sample still includes that iteration's logic and plugin work.

For idle-beam evidence, hold a qualifying ground stack fixed and compare spin 0
with spin 90. For mobile dressing, use a root with the relevant visible roles
and compare idle measurements with a real resize or configuration change. For
sparse Lua callbacks, compare the complete subscription census with the shipped
script's declared handlers and trigger at least one supported callback; an
untriggered zero alone does not prove a missing subscription.

`TORIRS_SIM_ITEM_INFO_DELAY=995,750` withholds one item's definition from plugin
`game.item_info` and ground snapshots for 750 logic ticks after its first plugin
observation. The trace records the exact start and release cycles. This is an
API-response-delay fixture: native cached definitions, models, network delivery
and the rest of the world remain unchanged. It does not simulate network
latency. Use a qualifying stack, preserve it beyond the release cycle without
sending another item/config event, then compare with the same no-delay control.

Ground snapshot cost zero alone cannot distinguish pending metadata from a
resident item worth zero. `game.item_info` supplies the missing distinction.
Loot Beams now polls only pending definition IDs every 25 logic ticks, with no
arbitrary expiry; a zero-valued resident definition and a departed stack stop
polling. The API has no definition-ready notification, so a bounded readiness
poll remains necessary for a script that must react to late metadata.
