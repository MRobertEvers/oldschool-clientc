# Inferno Scouter Plugin

This plugin reads the monster spawn locations on the first tick they spawn in the Inferno.

It outputs a **9-character code**, which corresponds to the **9 possible spawn locations**. The 9 spawns are listed in **reading order**.

## Spawn code legend

Each character in the spawn code represents what spawned on that tile:

- `o` = no monster spawned on this tile
- `Y` = bat
- `B` = blob
- `R` = ranger
- `X` = melee
- `M` = mager

## Why this exists

Inferno scouting has always been doable, but it used to require recording your screen, watching back the recording, then manually noting down the spawns.

This plugin generates the spawn code instantly, which makes scouting faster and enables easy copy/paste into other Inferno analysis tools.
