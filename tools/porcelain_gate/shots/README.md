# Looking at the plugins

The port gate answers *did anything move*. It compares component boxes, roles
and owned controls, and it is good at that. It cannot answer *does it look
right*, and for about half the ported plugins nobody had asked, because their
page, tooltip, label or beam is never on screen in a gate lane:

- the loot tracker's page and the XP tracker's page — no lane opens the rail
- the client settings page — same
- item stats' tooltip — no lane hovers anything
- ground items' labels — the fixture's drops are on a different plane from the
  player, so the caption hook never fires
- loot beam's beams — the fixture floor is empty
- the entity highlighter's outlines — the fixture tags nothing

The first time anyone looked, the loot tracker's page turned out to have been
drawing an EMPTY well. Both the overview line and the words "No loot to
display." were in the pre-port source and neither reached the screen. Every
unit test passed, the gate passed on seven lanes, and the page was blank. That
is the whole argument for this directory.

## Use

```
zsh tools/porcelain_gate/shots/shot_manifests.sh <worktree> <manifest dir>
zsh tools/porcelain_gate/shots/shot.sh <name> <lane> [ENV=value ...]
```

`shot_manifests.sh` has to run first and once per tree you photograph: the
manifests it writes pin the caches and content to the data checkout and the
revconfig to the tree under test, which is capture_set.sh's split and is there
for the same reason. A BEFORE shot needs its OWN set, pointed at the BEFORE
worktree and selected with `TORIRS_SHOT_MANIFESTS`, or it is taken with the
AFTER tree's revconfig.

The lane is a preset, and it is how a toplevel is chosen, because a toplevel is
not a knob the client takes — it is a consequence of the save's
`client_layout_mode` and of whether the lane logs in as a phone, exactly as
`lane.sh` picks one for the gate:

| lane | mode | client type | toplevel | frame under test |
|---|---|---|---|---|
| `classic548` (alias `cs2`) | 0 | desktop | 548 | `gameframe-layout/classic-fixed` |
| `classic161` | 1 | desktop | 161 | `gameframe-layout/classic-fixed` |
| `modern164` | 2 | desktop | 164 | `gameframe-layout/modern-resizable` |
| `stone601` | 0 | `TORIRS_CLIENTTYPE=7` | 601 | `mobile-gameframe/stone-drawer` |
| `native548` | 0 | desktop | 548 | none — the lane's own frame |
| `dat1_254` (alias `cs1`) | 0 | desktop | — | none, offline |

It writes `<name>.png` beside the script and the run under `runs/<name>/`.
Point it at a different tree with `TORIRS_SHOT_WORKTREE` and a different binary
with `TORIRS_SHOT_BIN`, which is how you take the BEFORE shot: build the commit
the port branched from and capture the same state.

Openers worth knowing, all of them drives the client already had:

| want | env |
|---|---|
| a plugin's page or settings | `TORIRS_SIM_PLUGIN_PANEL=600,<plugin>,<page\|settings>` |
| a plugin config value | `TORIRS_SIM_PLUGIN_CONFIG='60,<plugin>,<key>,<value>'` |
| the pointer somewhere | `TORIRS_SIM_HOVER=x,y` |
| a click | `TORIRS_SIM_CLICK_AT=x,y,<tick>` |
| a key held | `TORIRS_SIM_KEYHOLD=<code>` |
| a server command | `TORIRS_SIM_CMD='<tick>,<command>'` |

## The rule that matters

A screenshot on its own proves nothing: you have to know what it looked like
before. Take the BEFORE shot from the commit the port branched from, with the
same drive, and compare them. Three of the four pages first captured this way
were identical before and after, which is what a faithful port looks like. The
fourth was the bug.

## frames/ — the two frame providers

`frames/` holds the fourteen states each frame provider was photographed in,
before and after: the desktop provider (`gameframe-layout`) on toplevels 548,
161 and 164, the touch provider (`mobile-gameframe`) on 601, the same four
again after a layout switch at frame 500, and the six minimap states on 548.
BEFORE is `c5d2a9343`, the commit the desktop port branched from, built in its
own worktree with its own manifests.

Two things about the set are worth knowing before reading it.

**Only one of the two providers is ported.** `mobile_gameframe.c` is
byte-identical between `c5d2a9343` and this commit and contains no reference to
Porcelain at all. Its BEFORE is its only state; the `before-601*` shots are
there as the control that says so, and they differ from the `after-601*` ones
only in the performance readout and one frame of water animation.

**The housing-depth fix is hidden by another plugin.** The ledger defect the
desktop port exists to fix -- the map housing anchored over a compass that does
not paint, so the plate keeps its own native draw index and covers the orb
column -- is almost invisible with `minimap-orbs` on, because that plugin's four
covers are described `AT_CANVAS` with no anchor and cannot be got above, and
everything else the 172x156 plate would cover lies inside the plate's own
146x151 map hole. What is left over, and what the shots show, is interface 160's
world-map button and WIKI scroll: 198 pixels of them are painted over in the
BEFORE on minimap states 3, 4 and 5 and only on those three, which is the same
1:1 correlation with the suppressed compass that the rule reported.
`frames/*-noorbs-mm*.png` is the same pair with the orb plugins off, where the
plate has the whole native orb column to cover instead.

### The two defects the photographs found, and their own pairs

Two rows are named after what was wrong rather than after a lane, because the
lane they were found on is not the only one they were on.

**`*-chatstrip-548.png` — two chat bars on an OldSchool toplevel.** The 2004
frame's filter strip stood below the pack's own filter row, showing its
parchment lip and then a band of dark sockets. The sockets were not the
strip's four hollows: they were `classic_base_flat`, a picture composed to
COVER those hollows, which tiled twenty-nine columns of the strip and so
repeated the first hollow's cast shadow eighteen and a half times. There is no
run of plain rock in that strip to tile instead. The AFTER seats the pack on
the strip — 357 + 96 + 50 = 503, which also puts its bar on row 480, where
interface 548 puts that same bar on its own frame — and the composed band is
retired, because nothing is left for it to cover.

**`chatstrip-548-control.png` — who draws what, in one picture.** The same
hundred rows on four lanes: `native548` with no provider at all, `classic548`
before and after, and the CS1 lane as the reference. It is here because the
first reading of the BEFORE shot was that the provider had put PLATES where
the frame wants hollows. The control says the opposite. With no provider the
eight filters are the OldSchool pack's own raised plates on a dark bar, and
there is no second band at all; the provider is what turns them into 2004
hollows. Only the band below them was the provider's doing. The two lanes are
not pixel-identical in that region either: mean absolute difference 70.85
before the fix, 36.20 after.

**`*-remount-548-from-164.png` — the eighth gate lane's own end state.** These
two are IDENTICAL, and that is the point worth recording: the defect the lane
exists for lives in ONE fence, the one where the frame root becomes 548 while
the roles still answer the dying tree's nodes, and the frame converges either
way within a few frames. It cannot be photographed by stopping the client at a
chosen frame, because the switch is a server answer and does not land on a
fixed frame number — two runs of the same binary put it four frames apart, and
the client's own reload screen sits in the middle of the window. What DOES see
it is the findings channel the gate already compares: twenty-five
STALE_REFERENCE refusals in that fence before, none after.
