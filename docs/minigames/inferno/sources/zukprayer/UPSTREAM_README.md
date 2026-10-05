# ZukPrayer (RuneLite plugin)

A **display-only** RuneLite plugin that shows a [ZukSharp](https://github.com/propagating/zuksharp) plan in-game:

- the since-login tick counter (the wave spawns on tick 15 — same counter the AUTOZUK video
  uses via the report-button timer),
- which protection prayer to **click right now** ("CLICK NOW" flashes when the rotation
  switches, "Keep" otherwise),
- the repeating rotation (4 slots from ZukSharp, but any length parses) with the current
  click slot bracketed and a cycle position such as `3/6`,
- an optional highlight of the plan's starting tile.

It reads NPC spawn/despawn state (to know which of the big-3 are still alive, so the
rotation can advance when something dies — and step back when the mager revives it) and
automates nothing — it renders a plan you computed ahead of time, in the same
informational-overlay category as existing Plugin Hub Inferno helpers.
Automating prayers or input is against Jagex's third-party client rules and is deliberately
out of scope.

## Usage

1. In ZukSharp, run the solver, then click **EXPORT RUNELITE PLAN**. You get a string like:

   ```
   ZS1|tile=26,6|prayers=RANGE,MAGE,MELEE,MAGE
   ```

2. In RuneLite, open the ZukPrayer config and paste it into **Plan string**.
3. Scout the wave, log out, run ZukSharp, paste the plan, log back in — the overlay counts
   ticks from login and tells you the first click lands on tick 15.

The cycle is anchored to the wave spawn: slot 0 is clicked on tick 15 and active on tick 16,
then the rotation repeats at its own length. The plugin shows the prayer to *click* this tick,
which is what your hands care about. For 4-slot plans this is identical to the older
login-aligned `(t + 1) % 4`.

To try the overlay outside the Inferno, turn off "Only show in the Inferno" and paste a
hand-written cycle such as `ZS1|tile=26,6|prayers=MELEE,MELEE,MAGE,RANGE,MELEE,MAGE`.

With a ZS2 plan (solver exported with phase rotations enabled), the overlay switches to a
simpler rotation as big-3 mobs die; OFF means you can flick prayers off on that slot's
ticks. If the fight reaches a kill order the solver never saw, the overlay shows an
orange "off-plan" line and displays the closest safe (superset) rotation.

## Building

Requires JDK 11+:

```bash
./gradlew build       # produces build/libs/zukprayer-1.0.0.jar
./gradlew test        # or `./gradlew run` for a dev client with the plugin loaded
```

To use it before any Plugin Hub submission, sideload the jar from
`~/.runelite/sideloaded-plugins/` (create the folder and drop the jar in) or run the dev
client via `./gradlew run`. This repository follows the official
`runelite/example-plugin` template layout for a
[Plugin Hub](https://github.com/runelite/plugin-hub) submission.

## Status

Compiles against the current RuneLite client API. The overlay logic is covered by the tick
mapping tests in the main ZukSharp test suite (the plan-string format is shared); in-game
behavior has not been play-tested — treat the first run as a beta and verify the tick
counter against the report-button timer.

## History

This plugin was developed in the [zuksharp](https://github.com/propagating/zuksharp)
repository (under `runelite-plugin/`) and moved here so plugin development has its own
repository, as the Plugin Hub requires.
