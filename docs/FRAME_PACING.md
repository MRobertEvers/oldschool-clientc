# Frame pacing and the screen cap

There is one clock and one lever.

**The clock** is the pacer's period: 20 ms, the 50 Hz every revision's world
ticks at. `App_RunOnce` runs once per loop iteration at that rate whatever the
screen does, so a plugin's `on_frame`, the pacer trace and anything that
counts loop iterations all read 50 even when the picture is not moving.

**The lever** is the pacer's draw budget (`ToriRS_Pacer_DrawPeriodMs`), the
longest of:

- the period itself;
- the adaptive step-down the pacer applies when frames stop fitting; and
- the configured cap, `ToriRS_Pacer_SetCapFps`, restated by the loop every
  frame from `App_FrameCapFps`.

A frame is drawn only when the budget has elapsed; the rest of the loop's
iterations present the retained picture. Nothing else may gate a draw. Until
2026-09-02 the CS2 frame cap was a second path beside the pacer in
`frame_loop_step`, and a persisted `Limit framerate = 15` (device option 5 in
`preferences.ini`, written by a mobile-lane run on the desktop) gated the
screen at 15 fps while the loop, the pacer trace and the FPS readout all said
50.

## `[frame]` in revconfig

```ini
[frame]
cap_source=cs2   ; revconfig | cs2
cap_fps=0        ; frames drawn per second at most; 0 = the pacer's own rate
```

- `cap_source=revconfig` (the default, and what every lane without the
  section gets): `cap_fps` is the cap, and the only one.
- `cap_source=cs2`: the cache's own All Settings "Limit framerate" row (CS2
  device option 5, persisted with the other device options) is the cap while
  the player has picked one; `cap_fps` applies while it is unset. `osrs239`
  states this, because that revision has the row; the 2004 lanes do not.

`TORIRS_SWAP_DEBUG=1` prints, every 300 iterations, the present cadence and
`draw: N of 300 loop iterations re-rendered (cap F fps, draw period P ms)`,
which is the number to read when the screen looks slower than the readout.

## What a frame-rate readout must count

`ToriRS_PluginEvFrame.drawn_frames` is the client's cumulative rendered-frame
count. The performance display differences it; counting `on_frame` calls
measures the pacer.
