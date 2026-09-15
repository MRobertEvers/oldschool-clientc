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

## The three shipped defects the screenshots found (`lb-*`, `gi-maxcash-*`)

Three defects that predate the Porcelain ports, all three photographed the way
this directory says to photograph one: the same drive, the same binary, the
BEFORE tree's `script/` selected with `TORIRS_SHOT_WORKTREE`. Only the Lua
plugins differ between BEFORE and AFTER here, so one binary is honestly both
sides of the pair -- there is no C change for a second build to carry.

The drop is `::dropobj`, which uses the ACTIVE PLAYER'S coord, so the pile
lands under the player on the player's plane; `::tele <coord literal>` moves
the player between storeys for the plane pair.

| image | drive | what it shows |
|---|---|---|
| `lb-coins-{before,after}` | `60,dropobj coins 2000000` + `spin,0` | **no beam** over two million coins, then an orange one |
| `gi-maxcash-{before,after}` | `60,dropobj coins 2147483647` + `spin,0`, `ground-items,hide_exceptions,Coins` | `Coins (Lots!) (EX: 2B gp)` in the hidden colour, then `(EX: 2147M gp) (HA: 2147M gp)` in the insane colour, under a pink beam |
| `lb-upstairs-{before,after}` | `60,tele 1_50_53_10_32;100,dropobj abyssal_tentacle 1;160,tele 0_50_53_10_32` | a beam standing over a pile one storey up, and the log lines below |
| `lb-otherfloor-{before,after}` | `60,dropobj abyssal_tentacle 1;140,tele 1_50_53_10_32` | the same defect from the other side: the player walks UP and leaves the beam behind |

**A coin pile was worth zero.** The alch price is truncated per unit, which is
the reference's own arithmetic, and a coin's cache cost is 1: `floor(1 * 0.6)`
is 0, so five thousand coins alched to nothing and the shipped `alch` default
meant no pile of gold could ever clear a threshold. `gi-maxcash-before` is the
whole defect in one caption -- the HA price is not merely wrong, it is ABSENT,
because `label_for` prints a price only when it is non-zero.

**`2B` is not what the reference prints.** `QuantityFormatter.quantityToStackSize`
gives each suffix ten thousand of its own unit, so B does not begin until 1e10
and a max cash stack is `2147M`. The K rung was already right; only the M one
rolled early.

**The plane pair is a pair of LOGS, not of pixels, and that is the finding.**
`world_cycle.c` gates every plugin object on `obj->level != local_level`, so a
beam raised over another storey never reaches the screen -- which is why
`lb-nodrop-{before,after}` above, taken against the fixture's own two level-1
stacks, show no beams on either side. What the plugin was doing was creating
and COUNTING them:

```
lb-upstairs-before   [loot-beam] 1 beam(s) over 3 ground stack(s)     <- and no line after the player comes back down
lb-upstairs-after    [loot-beam] 1 beam(s) over 3 ground stack(s)     <- while the player is UP there, correctly
                     [loot-beam] 0 beam(s) over 0 ground stack(s)     <- and it comes down with them

lb-otherfloor-before [loot-beam] 1 beam(s) over 3 ground stack(s)     <- three stacks on two planes
lb-otherfloor-after  [loot-beam] 1 beam(s) over 1 ground stack(s)     <- one plane, one stack
                     [loot-beam] 0 beam(s) over 2 ground stack(s)     <- the player goes up; the floor is the two up there
```

The one line this plugin prints is its only diagnostic for "no beams appear",
and it was counting beams nobody could see. The cost that is not cosmetic is
the host's 64-object budget: every off-plane pile held a scene object, so a
player in a multi-storey building could have the beams they CAN see clipped by
beams they cannot.
