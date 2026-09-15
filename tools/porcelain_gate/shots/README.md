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

### `chatpane-*` and `chatfurniture-*` — the desktop provider's chat pane

Two defects in the same region, photographed the same way: one binary per side,
built from this tree with only `gameframe.c` differing, the same drive, the
same window.

`chatpane-birdnest-{before,after}.png` is the proof for the pane's HEIGHT, and
it is a notification rather than a line count on purpose. Classic Fixed asked
the lane how big its chatbox was and used only the width, giving interface 162
the 2004 builtin's 96 rows; the pack lays itself out to its box, so it showed
three lines where the lane's own frame shows eight. A plugin notification is an
ordinary game chat line, so `nxt-bird-nest`'s notice -- which the log says fired
on both sides -- scrolled out of the pane before it could be read. In the BEFORE
the chat ends at "Guards on the Al Kharid road"; in the AFTER "A bird's nest
falls out of the tree." is on screen. `chatpane-birdnest-native.png` is the same
drop with no provider, which is the reference the AFTER has to match, and its
chat region is byte-identical between the two runs.

`chatfurniture-cs1-{before,after}.png` is Classic Fixed forced over the 2004
frame -- a state no lane preset reaches and the client settings page offers.
The provider blitted its own parchment as ordinary chrome, which put it OVER
the chat builtin that draws its scrollbar, its rule and its `Press Enter to
chat...` line inside that same rectangle; the BEFORE is a bare sheet. The
parchment is the chat's BACKING and now says so. `chatfurniture-native-cs1.png`
is the lane's own frame, and after the fix the provider's whole chat region is
pixel-identical to it.

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
## plugins/ — every plugin, one at a time, on both lanes

`frames/` photographs the two frame providers. `plugins/` photographs all
twenty plugins, one per picture, on the CS2 lane and on the 2004 CS1 lane.
Thirteen of them had no named picture at all before this, and CS1 had four.

```
zsh plugin_ini.sh <out.ini> <plugin-id> ["key=value" ...]   # one on, 19 off
zsh pshot.sh <name> <plugin-id> <lane> [ENV=v ...] [-- cfg=v ...]
PAR_JOBS=1 zsh par.sh jobs/<file>.txt
python3 vs_control.py plugins/control-<lane>.png plugins/<shot>.png
python3 zoom.py <in.png> <out.png> <x> <y> <w> <h> [scale]
python3 detail_lengths.py
```

`jobs/` is the drive for every shot, with the reason for each in place. The
five things that cost a run each to learn, so nobody pays for them twice:

**`enabled` defaults to ON.** The host only writes the flag when it differs
from the plugin's own default, so an ini naming one plugin `enabled=1` leaves
the other nineteen running. `plugin_ini.sh` lists every id and switches each
one off by name.

**`preferred_frame` is the master switch for a frame provider, not `enabled`.**
`[plugin:gameframe-layout] enabled=1` with `preferred_frame=auto` leaves the
provider off, and `preferred_frame=<provider>/<offer>` turns it on however the
ini reads. `TORIRS_SHOT_FRAME` overrides the lane's choice, which is the only
way to ask for the desktop provider on CS1 or the touch provider on 548.

**`auto` is not `native` on the CS2 lane.** It resolves to `gameframe-layout`
there, so `control-cs2.png` has the provider in it and a diff against that
control cannot see anything the provider did. The native CS2 frame needs
`TORIRS_PLUGIN_ONLY=<id>`, which registers one plugin and leaves the frame to
the client; `control-noplugins-cs2.png` is that frame with nothing at all on.

**`TORIRS_SHOT_FRAMES` is read by shot.sh, not by the client.** Passed in the
trailing `ENV=value` list it reaches the client, where nothing reads it, and
the run is 700 frames after all — which is how a cannon drive scheduled at
frame 1400 came back with no cannon fired and no error. Pass
`TORIRS_MAX_FRAMES` in that list instead; `env` takes it last-wins.

**`TORIRS_SIM_HOVER` used to put "Connection lost" across the viewport, and to
mark the wrong tile.** FIXED — both halves were one block in `main.c`, and both
are worth knowing because the shots taken before the fix are still in git
history and in `pages/BEFORE_itemstats_hover.png`.

The banner: the four parked-pointer frames after the main loop were stamped
20/40/60/80 ms — absolute, counted from zero. After a run of any length that is
the clock jumping BACKWARDS by the whole session, so `now_ms - last_recv_ms`
wrapped unsigned inside `NetLinkWatch_Step` and the watch read the wrap as
fifteen silent seconds. It tore the session down in the last four frames of
every hover shot: banner, no local player, no npcs, no minimap dots. That is
what left `tileind-c-cs2` — the headline picture of the plugin the ledger calls
the yardstick — with none of its three markers in it. The frames now continue
the loop's clock.

The wrong tile: those frames ran `App_RunOnce` and nothing else, and the world
PICK is armed inside `App_Render`. So the pointer moved and nothing re-picked,
and every hover-tile consumer went on answering the tile the last main-loop
event had left — on `tileind-c-live` that was the login click, a tile the
`~varrock` teleport had since put under a roof, 110 px from the pointer the
shot declared. They now render, like every other headless sim frame does
(`sim_render_frame`, and see the comment on it).

`TORIRS_SIM_MOVE_AT` during the run is still the better instrument where a
state needs many frames of hover to build, and `*-nohover-*.png` is the same
plugin with no parked pointer at all.

Every hover shot in `plugins/` was taken against the broken teardown, so every
one of them has to be re-taken before it can be read. The tile indicator's
eleven are done — `tileind-{c,lua}-{cs2,cs1,live}`, `tile-{c,lua}-all-{cs2,cs1}`,
`pair-tiles`, `toggle-tile-off`, and `tileind-c-cs2-before.png` is the same
drive on a binary built before the fix, kept as the picture of what the bug
looked like. These SEVEN are still pre-fix and belong to their own plugins'
owners: `itemstats-hover-cs2`, `itemstats-hover-cs1`, `itemstats-live`,
`nxthl-hover-cs2`, `nxthl-hover-cs1`, `nxthl-tile-cs2`, `nxthl-live`. Re-taking
one is its job row and nothing else.

One correction to the openers table above: `TORIRS_SIM_CLICK_AT` is
`frame,x,y[,right]`, frame FIRST, not `x,y,<tick>`.

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

## lt-blank-{before,after} and lt-cells-{before,after} — the Loot Tracker's two

Two binaries, one drive each, under `plugins/`; the cells pair is cropped into
`zoom/loot-cells-{before,after}.png`. Both defects are in the C plugin, so a
BEFORE shot needs a binary built from the tree before the fix and
`TORIRS_SHOT_BIN` selects it — pointing `TORIRS_SHOT_WORKTREE` at the old tree,
which is how the Lua pairs were taken, would change nothing here. The drives
are in `jobs/loottracker.txt`.

**`lt-blank-*` is the coin toss, made into a picture.** `lootsweep-*` and
`lootrepeat-*` above could not reproduce the blank well on demand — four of
eleven at panel tick 620, six deliberate repeats that all painted — because the
thing being raced is not the tick. The well was blank for a FIXED 3,520 ms
after every open, measured the same to within forty milliseconds on every run,
and a 900-frame capture either ran past that window or ended inside it.
`TORIRS_MAX_FRAMES=621` stops one RENDERED frame after the page opens, which is
inside it every time and is also what a person opening the rail actually sees.
Before: an empty box. After: the totals band and "No loot to display."
`well_ink.py` scores them 0 and 1018.

**`lt-cells-*` is what a recorded drop looks like.** Two goblin kills, each five
thousand coins and a bone. Before: two empty plates and "Total value: 100M gp" —
the plates are the cell art, so the cells were LAID OUT correctly and had
nothing in them. After: the coin pile at its ten-thousand stack variant under a
yellow `10000`, the bones under a `2`, and "10K gp", which is what ten thousand
coins at one gp each and two bones are worth.

The 100M is not a rounding difference. The store held `cost * qty` in a field
the plugin API documents as the price of ONE, so the quantity went in twice:
10,000 * (1 * 10,000) = 100,000,000. The zoomed pair shows both halves at once.
