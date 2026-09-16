# HOW TO INSPECT A SHOT — not optional, and not per-taste

Twenty plugins were once declared "visually verified on both lanes" and that
went into a pull request. A per-plugin pass then found **42 defects across 17 of
those 20**, including a plugin that drew nothing at all and a settings dropdown
that opened downward off the bottom of the window with 8 of its 9 options
unreachable. Nothing exotic was missed: the screenshots were opened at full size
and scanned whole, and the numbers around them were read as though they were
evidence about the picture.

They are not. `OWNED_WIDGET` counts, `BOUNDS` counts, finding counts and
pixel-diff percentages all answer *"did something change"*. None of them answers
*"is this right"*. And a 765x503 frame viewed whole cannot show a torn edge cut
mid-tear, a progress bar eating an icon's last row, a 1px border overpainted, or
a list running past the window — every one of which was found by cropping.

So:

```
python3 inspect.py plugins/<shot>.png [--plugin-box x,y,w,h]
```

It cuts the same regions every time — chat band, orb column, sidebar, top strip,
left edge, the last rows, plus the plugin's own box — at 3x nearest-neighbour,
and writes a manifest beside them. **Open every file it writes.** The manifest is
the artifact that makes a skipped inspection visible afterwards; there is no
verbal equivalent of having looked.

Three things it cannot do for you, each of which has already cost a defect:

1. **Read the plugin's source first**, config defaults included. Without knowing
   what it is supposed to draw, "drew nothing because a setting said no" and
   "drew nothing because it is broken" are the same picture.
2. **Read `runs/<shot>/log.txt`.** A plugin's own diagnostic line present on one
   lane and absent on the other is a defect even when the picture looks fine.
   That is exactly how the loot-beam lane bug announced itself: CS2 logged
   `1 beam(s) over 1 ground stack(s)`, CS1 logged nothing at all.
3. **Confirm the shot exercised the plugin.** Five shipped shots proved nothing:
   a hover over an item with no stats to show, XP globes photographed after they
   expired, a highlighter tagging CS2 npc ids on a lane with 3-digit LostCity
   ids, a frame provider that was never switched on (`preferred_frame` is the
   switch, not `enabled`), and a stale pre-fix capture.

Never write "verified" or "clean" without naming the regions you cropped. "Looks
fine" with no regions named is not a result.

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
| a key held | `TORIRS_SIM_KEYHOLD=<code>` (once, never released; `TORIRS_SIM_KEYHOLD_FRAME=N` to press after the title screen) |
| a right-click on an npc | `TORIRS_SIM_CLICK_NPC=<frame>,<type>,1` — by TYPE, because npcs wander; `-1` is any |
| a plugin's own menu row | `TORIRS_SIM_MENU_ROW=<frame>,<label prefix>` — finds it on the open menu, PRINTS the label it found, clicks its centre |
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
| `lb-coins-{before,after}` | `60,dropobj coins 2000000` (`spin,0` on the before binary only) | **no beam** over two million coins, then an orange one |
| `gi-maxcash-{before,after}` | `60,dropobj coins 2147483647` (`spin,0` on the before binary only), `ground-items,hide_exceptions,Coins` | `Coins (Lots!) (EX: 2B gp)` in the hidden colour, then `(EX: 2147M gp) (HA: 2147M gp)` in the insane colour, under a pink beam |
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
python3 exercised.py --menu-row <label> --log runs/<shot>/log.txt   # did the plugin's row exist
python3 exercised.py --selftest                                     # after touching its log readers
python3 zoom.py <in.png> <out.png> <x> <y> <w> <h> [scale]
python3 detail_lengths.py
python3 shot_rules.py          # what a tracked capture has to still be true of
```

`shot_rules.py` is the one of those that can FAIL. Everything else here makes a
picture; it re-measures the numbers that closed a defect, so a capture that has
gone stale — or a fix that has come undone — stops being something a reader has
to notice and becomes something a gate says.

`jobs/` is the drive for every shot, with the reason for each in place. The
six things that cost a run each to learn, so nobody pays for them twice:

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

**A drive lever's receipt is not narration, and `TORIRS_LOG` is not compiled
into the binary you photograph with.** Every shot here is taken with an `OPT=1`
build, `OPT=1` is `-DNDEBUG`, and `-DNDEBUG` compiles `TORIRS_LOG` away. So
`sim_varbit:` and `sim_oploc:` printed nothing in any capture log while
`sim_cmd:` -- `TORIRS_REPORT`, three lines away in `main.c` -- printed beside
them, and the only way to answer "did the varbit write land, and what did it
read back as" was to trust the jobs file. That is how the cannon builtin's low
notification went a whole port without a capture: setting 249 was never
written, the log could not say so, and the picture of a row that does nothing
is the picture of a row that was never switched on. Both are `TORIRS_REPORT`
now. `strings <binary> | grep sim_varbit` is the check; if it is empty, the
receipts in that binary's logs are too.

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
looked like. `nxthl-tile-cs2` and its control `nxthl-tile-none-cs2` are done
too — see below, because that pair was pre-fix TWICE over and is the reason
`shot_rules.py` exists. These FIVE are still pre-fix and belong to their own
plugins' owners: `itemstats-hover-cs2`, `itemstats-hover-cs1`,
`itemstats-live`, `nxthl-hover-cs2`, `nxthl-hover-cs1`, `nxthl-live`.
Re-taking one is its job row and nothing else.

### `nxthl-tile-{cs2,none-cs2}` — two fixes photographed as live bugs

The pair was taken at 21:31 and 22:02 with the `83c75b410` binary. `190648c80`
(a tile border is the cache's thickness, and 0 is no border) landed at 22:32
and `74fc1f13f` (the parked pointer stopped costing a session) at 22:35, and
neither re-took them. So the headline picture of nxt-highlight showed a hard
opaque rim around a group the cache states at `outline=0`, over a session the
net watch had torn down — both already repaired, both re-reported off the
picture a day later as `NXTHL-TILE-CS2-STALE-PREFIX-548`.

Re-taken here with the same drive on a binary built from this tree — the
`plugins.ini` and `preferences.ini` of the new runs `diff` clean against the
old ones, so the BINARY is the only variable on that axis. The control had no
job row at all and had to be reconstructed from its run directory before it
could be re-taken; it has one in `jobs/reruns.txt` now, beside the shot's.
Measured rather than admired:

| | pre-fix pair | re-taken pair |
|---|---|---|
| unblended `0xBEBA6E` in the frame | **389** | **0** |
| the tile's interior, px (293,200) | `(105,103,78)` | `(105,103,78)` |
| the same px in the CONTROL | `(74,72,67)` | `(74,72,67)` |
| pure white in the viewport band `y150..220` | **649** | **0** |

The first row is the rim; the second says the WASH survived it, which is the
half that separates "the border is gone" from "the plugin stopped drawing";
the third is what makes the control a control; the fourth is the teardown
banner's text. An A/A pair — two runs, the same ini, the same binary — moves
nothing in the tile band and reproduces every other difference between shot and
control, so the entity and minimap deltas are run-to-run and only the tile is
the plugin's.

All four measurements are pinned in `shot_rules.py`, which fails on the pre-fix
pair with three of its five rules and passes on the re-taken one. A measurement
that closed a defect belongs in a file that runs, not in a paragraph.

hover the wrong tile.** FIXED -- both halves were in `frame_loop_teardown`, and
any shot older than that fix which was driven with `TORIRS_SIM_HOVER` shows one
or both. They are worth knowing by sight, because that is how you date a shot:

- The four extra interact frames after the main loop were stamped 20, 40, 60,
  80 ms, a clock that jumps tens of seconds BACKWARDS from the loop's. The net
  watch measures the server's silence in unsigned milliseconds, so the jump read
  as a silence of eighteen quintillion of them and tore the session down before
  the BMP. Every `TORIRS_SIM_HOVER` capture therefore came back with the banner,
  no player model and nothing for a world overlay to draw on -- the tile
  indicator's own headline shot was a blank grey floor, and read as a plugin
  that draws nothing. It is in `pages/BEFORE_itemstats_hover.png` too.
- Those frames also did not RENDER, and the scene pick runs inside the render.
  So the pointer moved and the hover tile did not: the marker stayed on whatever
  tile the main loop's last frame had picked -- nothing at all on CS2, where the
  mouse had never been in the viewport, and the login click's tile on cs1live,
  a hundred pixels from where the drive asked to hover.

Both are gone: the frames continue `app.last_frame_ms` and each one renders. A
hover state that can be reached with `TORIRS_SIM_MOVE_AT` during the run still
should be -- it is one frame cheaper and it is what a real pointer does --
and `*-nohover-*.png` is the same plugin without any hover at all.

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

## ehreveal-* and ehtag-* — the half of a plugin no shot had opened

The entity highlighter draws hulls and it adds `Tag`/`Untag` to the right-click
menu. Five shots of it were filed, one per lane, and every one of them drove
the hulls: no key held, no menu opened. So the menu half — the half the
plugin's own header calls the point of the port, and which it says was
unreachable on **every** lane before the `on_key` forward went in — was
photographed by five drives that could not have seen it fail. Deleting
`on_menu_build` and `on_menu_select` outright would have produced the same
pixels and the same log in all five.

That is the failure this directory is named after, one plugin further in: the
shot was evidence about the drive, and it was read as evidence about the
plugin. Nothing in the harness could say so, because the three signals
`exercised.py` had cannot see a menu row — INK is a few hundred pixels of the
client's own font inside a box that moves with the click, LOG is silent
because this plugin prints nothing, and EMIT needs a scene id a menu row does
not have. It grew a fourth, `--menu-row`, and a `--selftest` for it.

Two drives per lane now, ten in all, and each proves itself in the log:

| row | drive | what it shows |
|---|---|---|
| `ehreveal-<lane>` | tags preloaded, `TORIRS_SIM_KEYHOLD=42` at frame 500, `TORIRS_SIM_CLICK_NPC=620,5037,1` | the menu still open in the last frame with `Untag Romeo` in it, over the hull |
| `ehtag-<lane>` | **no tags**, same key, right-click at 560, `TORIRS_SIM_MENU_ROW=600,Tag` | `sim_menu_row: ... row 'Tag @yel@Romeo'`, `tags=5037` written back, and a hull that exists only because that row was picked |
| `ehtag-nokey-classic548` | the `ehtag` drive **minus the keyhold** | the control: five native rows, no `Tag`, no `sim_menu_row` line, no hull, nothing written |
| `ehtag-prefix-classic548` | the `ehtag` drive against a script tree with the `note_key` forward **deleted** | the negative control, and the one that proves the drive tests the thing |

The frame numbers above are the CS2 ones; `ehreveal-live` and `ehtag-live` do
the same thing at 2100 and 1900 against LostCity npc 547, after the teleport,
and their rows read `Untag Baraek` and `Tag @yel@Baraek` — names out of that
lane's OWN content, which is the other trap this plugin's header is about.

The last two rows are a pair, and the difference between them is the whole
argument:

| menu | rows | `Mark tile` | `Tag Romeo` | `sim_menu_row` | `tags=` |
|---|---|---|---|---|---|
| `ehtag-nokey` (key never held) | 5 | no | no | none | unwritten |
| `ehtag-prefix` (key held, forward deleted) | 6 | **yes** | no | none | unwritten |
| `ehtag-classic548` (as shipped) | 7 | yes | **yes** | `'Tag @yel@Romeo'` | `5037` |

`Mark tile` is the CACHE's own shift-gated client op, so its presence in the
middle row proves the held key reached the client and the plugin did not
forward it. "Key not held" and "key held but not forwarded" are two different
pictures now, and the drive goes red for the second one. Before this, those two
and a working plugin all made the same picture.

There is a scalar too, and it is the one worth quoting because no chrome in
this client is pure magenta: count `#FF00FF` pixels, which is the hull's own
default outline colour. Whole-frame, on the `ehtag` family — where the ONLY
thing that can produce a hull is the menu row having been picked:

```
ehtag-nokey-classic548      0      ehtag-classic548    530
ehtag-prefix-classic548     0      ehtag-classic161    528
ehtag-stone601              0      ehtag-modern164     637
   (declared off)                  ehtag-live          218
```

Four things in those drives are not decoration.

**The npc is named by TYPE, not by pixel.** `TORIRS_SIM_CLICK_NPC=<frame>,<type>,1`
— the trailing 1 is the right button. A fixed coordinate is a coin toss on a
wandering npc, and the type has to be one that is actually **in the tag list**
or the row reads `Tag` where the shot claims `Untag`, which is a different
picture of a different claim.

**The key is pressed once and never released.** `TORIRS_SIM_KEYHOLD` presses on
one frame and that is the only transition there will ever be, so a handler that
misses it never gets a second chance — which is exactly the bug the drive
exists to be able to see. It is deferred to frame 500 because a key pressed on
frame 1 lands on the title screen, before the plugins are started.

**`TORIRS_SIM_MENU_ROW` prints the label it FOUND**, before it clicks it. That
line is the evidence: a row that was never built cannot pass as one that was,
and `exercised.py --menu-row Tag --log runs/<shot>/log.txt` exits non-zero when
it is missing.

**601 has no `Tag` row and that is correct.** It logs in as a phone, so
`Porcelain_KeyEdge` answers ABSENT *at the call* and the plugin turns the rows
off. The tell is the finding's detail: the fence's own absence prints the key
NAME (`shift`, `off`), so `detail=touch lane has no key` at `first_frame=0` can
only have come from the branch inside the call — the capability was already
true when `on_start` asked it. Those job lines carry `NO_KEYBOARD`, and
`--menu-row` answers `DECLARED OFF` rather than `INERT` there, but only for an
absence the plugin **declared**; an undeclared one still fails.

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

## `xtbargeom-*` — one number, five lanes

Ten full frames and six crops for XPTRACKER-BAR-EATS-ICON-LAST-ROW and
XPTRACKER-BAR-OVERPAINTS-BOX-BORDER, which are one defect. One binary per
side, built from the same tree with only `xp_tracker.c` differing, the
`jobs/cs2_toplevels.txt` and `jobs/cs1live.txt` xptracker drives: all four CS2
toplevels and the live CS1 lane, because the constant is lane-independent and
a fix that only looked right on 548 would be the same mistake again.

The BEFORE side is not the original port. It is the state after the defect had
been repaired ONCE, as two separate one-pixel nudges -- the bar down a row to
stop it eating the icon's last row, and in a column to stop it erasing the
box's outline. Both stop the symptom and neither is the cache's number:

|  | before (two nudges) | after (the layer's carry) | cache |
|---|---|---|---|
| bar, box rows | 28..42 | 30..44 | 30..44 |
| rows between icon and bar | 0 | 2 | 2 |
| rows under the bar | 4 | 3 | 3 |
| bar margin, left and right | 1, 1 | 3, 3 | 3, 3 |
| goal label, in from the box's right | 4 | 6 | 6 |

Interface 729 keeps the box wash, its outline, the icon and the stat grid in
layers 4, 5, 6 and 12..16 -- all `503,2 244x499` -- and the bar's five
components in 7..11, which are `506,5 238x493`. Every measurement in
script5365 is therefore in a frame three pixels inside the one script5363 and
script5366 measure in, and carrying it across is the whole fix. Read that way
the box accounts for its own forty-eight rows, `3 + 25 + 2 + 15 + 3 = 48`, and
the bar's left edge (absolute 506) lands on the icon's (absolute 503 + 3) --
which is what `zoom/xtbargeom-live-attack-after.png` shows and what the two
nudges cannot produce at any pair of values.

`zoom/xtbargeom-live-attack-{before,after}.png` is the Attack sword at 12x on
the live lane, the icon whose pommel this defect was first filed about: before,
the bar is flush under the pommel's outline; after, two rows of the box's own
wash separate them. `zoom/xtbargeom-548-goal-{before,after}.png` is the other
end of the same bar, where the goal label and the track's edge both move two
columns in.
## `nxt5fresh{A,B}` and `nxt5cannonwarm` — the live lane's chat pane

Three captures that settle `CANNON-BIRDNEST-LIVE-CHAT-COVERED`, and the reason
they are three rather than a before/after pair: the control has to differ in the
INTENDED VARIABLE ONLY, and the intended variable here is not the code.

The defect report said the tutorial parchment owning the chat pane in
`cannon-live`, `birdnest-live` and `control-live` was "a per-run race", because
`nxthl-live` -- same job file, same `~skiptutorial` drive -- shows it skipped. It
is not a race. It is whether the shot's account already existed:

| capture | account | login | `CHAT_REGION` | row 434 chat rule |
|---|---|---|---|---|
| `nxt5freshA` | `szn5fresh01`, created by this run | first | `iface=6179 tut_com=6179 log_visible=0` | 10/470 |
| `nxt5freshB` | `szn5fresh01`, **the same one** | second | `iface=-1 log_visible=1` | 470/470 |
| `nxt5cannonwarm` | `scannon_liv`, `cannon-live`'s own | second | `iface=-1 log_visible=1` | 470/470 |

`freshA` and `freshB` are one drive, one binary, one plugin and one account
name, run twice. The only thing that differs is that the second one is not the
account's first login -- and `login.rs2` runs `@start_tutorial` only while
`%tutorial < ^tutorial_complete` AND the player stands on tutorial island, both
of which are true exactly once. `nxt5cannonwarm` is the same statement made
about the defect's own shot: `cannon-live`'s row, `cannon-live`'s account, one
login later, with a chat pane in it.

The shipped four, measured the same way: `cannon-live`, `birdnest-live` and
`control-live` score 10/470 on that rule and their `.sav` files were born inside
their own captures (23:37:24, 23:36:30, 23:31:49); `nxthl-live` scores 470/470
and its account was born at 21:07:20, two and a half hours before the sweep.

Why `~skiptutorial` cannot help: `tut_open` puts the box in the engine's TUT
modal slot and `if_close` runs `Player.closeModal`, which clears modalMain,
modalChat and modalSide and never modalTutorial -- only `tut_close` writes
`TutOpen(-1)`. @see the note in `lostcity.sh` for the one-line server-side fix,
which lives in the LostCity checkout and cannot be committed here.

**What the harness could not see, and now can.** The tab strip is 46 colours in
ALL THREE of these and in all four shipped shots -- a covered run and a clear one
have the same sidebar, so `live_check.py`'s old "~12 colours means the tutorial
never got skipped" could never separate them, and 37 of the 47 live captures
taken on this lane's own 2004 frame shipped covered. The client now writes

    CHAT_REGION iface=<id> chat_com=<id> tut_com=<id> log_visible=<0|1>

beside every BMP -- the chat builtin draws neither its log nor its input line
while an interface is mounted in that region, so this is the difference between
"the plugin said nothing" and "the plugin's line had nowhere to go".
`live_check.py` reads it and fails; a log without the line is `UNKNOWN-CHAT`,
which is not a pass. `live_check_test.py` pins all of it, and `test-ui-slots`
pins the client half.

**Neither notification plugin could have drawn here anyway**, and that is the
other half of the refutation: `revconfig/rs289lc/rs289lc_dat1_cache.ini`
declares no `varbit:bird_nest` and no `varp:cannon_coord`, so both builtins'
`Porcelain_Require` refuses at start and registers no sampler -- each run log
says so twice as a `PORCELAIN_FINDING` on frame 0 -- and neither live drive
produces a nest or a cannon to begin with.
## lostcity/ — the drives the live lane needs, carried where the pictures are

`jobs/cs1live.txt` names `~skiptutorial`, `~torirsdrop`, `~torirscoins` and
`~torirskit` as though they were part of this repository. They are not: they
were written straight into a LostCity checkout's `content/scripts/_test/
scripts/cheats/`, and nothing here says so or ships them. A fresh clone can
read every one of those job rows and reproduce none of those shots.

`lostcity/` is where that stops. A drive a picture depends on is part of the
picture, so a new one goes here first and is copied into the server checkout
from here:

```
cp tools/porcelain_gate/shots/lostcity/*.rs2 \
   <LostCity_Server>/content/scripts/_test/scripts/cheats/
```

The dev thread's file watcher rebuilds the script pack on its own — no restart
— and a debugproc changes nothing the client downloads, because script.pack is
not one of the nine jag archives.

### `cheat_torirsbeam.rs2` — `~torirsbeamdrop`, and why it is not `~torirsdrop`

`lootbeam-live.png` is a beamless picture and it is CORRECT: the ~torirsdrop
pile alches to 15,360 / 60,000 / 20,000 / 180 against a shipped `high_value` of
1,000,000, and nothing on that floor can raise a beam. A correct blank and a
broken plugin are the same photograph, which is the failure this whole
directory exists to prevent, and it is how `LB-LIVE-NEVER-LIT-CS1` was filed.

The live lane did have one beam already — `lb-live-beam.png`, a blue low-tier
column over that same pile, taken with `tier` forced to `low`. Measured inside
the 512x334 viewport it is 193 low-hue pixels against the control's 9. What it
proves is the MODEL, the scene object and the plane filter on a dat1 world.
What it cannot prove is the thing loot_beam 2.0 was rewritten for: an override
matched BY NAME on a cache that is not the one the table was written against.
A beam raised out of `OC_COST` says nothing about the table.

So `~torirsbeamdrop` drops two objs chosen so the cache alone cannot light
either of them, on two tiles because one beam per tile takes the best thing on
it:

| obj | cache value | alched | prices.txt row | tier |
|---|---|---|---|---|
| `red_partyhat` | 1 (no `cost=`, so ObjType's default) | **0** | `Red partyhat = 1500000000` | insane, pink |
| `dragon_chainbody` | 250,000 | 150,000 — MEDIUM, under the floor | `Dragon chainbody = 1800000` | high, orange |

`lb-priced-live.png` against `lb-ctrl-torirsdrop.png` is the pair, and the only
difference between the two job rows is which proc runs. Viewport hue counts:
insane 0 -> 155, high 437 -> 1206. The report's own pink mask (r>120, b>90,
r>g+40, b>g+20) goes 0 -> 250 over the whole frame.
## `cannon-*` — a drive that could not produce the event it was named after

Four numbers said this plugin was fine and one `grep` said it had never spoken.
`nxt-cannon-ammo` ran on every lane, reported `enabled=1 running=1 error=0` in
every capture, and raised no finding anywhere — and `grep -rh PLUGIN_NOTIFY
runs/` across all 307 runs returned not one line from it.

The reading that first fit was that the lane pre-empts it: `cannon.rs2`'s tick
timer says "Your cannon is out of ammunition!" the tick after the count reaches
zero, the builtin holds its own empty notice for two server ticks, and
`nxt_cannon_chat` drops the duplicate when the lane speaks. All of that is true
and **none of it is the defect** — it is the repair for a photographed
two-lines-for-one-event bug, and on an OSRS239 lane the correct output at empty
is silence. Believing it was the defect is how you end up "fixing" a plugin
that is already right.

The defect was in the **drive**, and it was two independent things at once:

- **The threshold row was never written.** Setting 249 (`varbit 14176`,
  `cannon_low_amount`) has no default on purpose — the row exists so the number
  is the user's — so `threshold > 0` is false until something writes it. The
  job rows wrote 14175 and 14177 and stopped.
  `docs/plugin-engine/PLUGIN_PORTS.md` had been saying the drive sets
  `14175=1, 14176=10, 14177=1` the whole time; the shipped rows had two of the
  three. A drive and the prose about it disagreeing is worth a grep.
- **A drop is not a crossing.** The only ammo change was loc op 3 "Empty",
  which returns all fifteen balls in one tick. The builtin's low notice fires on
  `previous > threshold && ammo <= threshold`, and the empty branch wins at
  `ammo == 0`, so a one-tick 15 → 0 cannot be a crossing whatever the threshold
  is. The count has to WALK.

`::spawn goblin 20` makes it walk: twenty targets inside the cannon's eight-tile
range, one ball a tick through `~cannon_fire_once`, 15 → 0 over fifteen server
ticks. The chat pane now carries both halves at once —

```
Your cannon is running low on cannonballs: 10 left.   <- the plugin, at the crossing
Your cannon is out of ammunition!                     <- the lane, at zero
```

— with nothing from the plugin after the second line, on all four CS2
toplevels. `cannon-classic548-before.png` is the shipped drive taken with the
same binary, the same lane, the same manifests and the same worktree, so the
drive string is the only variable between the pair.

The inventory is the other half of the measurement and is easy to miss: the
BEFORE shot has a 15-stack of cannonballs in slot 4, because the unload put
them back, and the AFTER has three items and no cannonballs, because they were
fired.

`cannon-thresholdonly-control.png` is the third leg, and it is what says the
two causes above are INDEPENDENT rather than one cause said twice: the old
`::cannon` + op-3 drive with `14176=10` added and nothing else. Its chat is the
BEFORE's, to the line -- "You unload 15 cannonballs." then "Your cannon is out
of ammunition!" -- and `grep -c PLUGIN_NOTIFY` on its log is 0. Writing the
threshold is necessary and not sufficient; a drive that only ever drops to zero
stays silent however the rows are set.

**The general shape, which is not about cannons.** An edge-triggered plugin
needs a drive that produces the EDGE, and "the state it reports is reached" is
not the same claim. Emptiness was reached in every one of those 307 captures.
The transition the plugin watches for never happened in any of them — and a
capture of a state a plugin does not fire on looks exactly like a capture of a
plugin that does not work.
## xporbs-collide-{live,548}-{before,after} — XP drop labels that ate each other

XPORBS-DROPLABEL-COLLIDE, and it is TWO pictures because the defect has two
axes and only one of them was reported.

A drop label is as wide as its digits; the globe column's pitch is fixed at
`orb_size + ORB_STEP` = 50. `orb_plan` centred each label on its own globe and
nothing else looked at the row, so the moment a number was wider than the
pitch it reached into its neighbour — and the neighbour, being a later item in
the same description, painted OVER it.

| image | drive | what it shows |
|---|---|---|
| `xporbs-collide-live-{before,after}` | the `xporbs-live` row of `jobs/cs1live.txt`, plus `drop_duration=10000` | five globes, five `+13,034,431` |
| `xporbs-collide-548-{before,after}` | the `xporbs-classic548` row of `jobs/cs2_toplevels.txt`, plus `drop_duration=10000` | ONE globe, two gains 1.2s apart |

`drop_duration=10000` is not decoration and it is not the fix under test. The
climb and the fade are functions of wall-clock milliseconds since the gain, so
two runs of the same drive stop at different points of the same 1200ms curve
and the labels come back at different heights and different opacities — which
is not a difference anybody can read a BEFORE/AFTER pair through. At 10,000ms
both sides are caught at the same height, fully opaque, and the only thing
left different between them is the binary.

**The live pair is the reported half: five wide labels on five globes.**
`"+13,034,431"` measures 59px in the plugin's own p11 atlas against a 50px
pitch, so each label lost nine pixels to the next one. Measured as the x-extent
of each label's own SKILL COLOUR in the band (rows 68..75):

```
before   132..188   182..238   234..288   (...)   332..388     <- runs OVERLAP
after    114..170   173..229   232..288   (...)   350..406     <- runs are disjoint
```

Before, run 2 begins at 182 while run 1 still has ink at 188. After, the middle
label has not moved (231), the two beside it moved 9px out and the two outside
those 18px out, and every run is a full 122 ink pixels instead of 113 or 119.
The picture says the same thing in one glance: four of the five numbers used to
end in a digit painted in the NEXT skill's colour, so they did not read as
truncated, they read as a different number attributed to the wrong skill.

**The 548 pair is the half nobody reported, and it is the same rule fixing it.**
Two gains in ONE skill want the same x exactly and are separated only by the
12px climb, so `+15,983` and `+800` 1.2 seconds apart were printed on top of
each other and read as `+15003`. There is no lane in this difference: the
labels are laid out against each other once, by shared rows and then by x, and
a column laid out VERTICALLY is untouched because consecutive globes there are
a pitch apart in y and their labels share no row at all.

Neither pair is visible to the port gate. No gate lane gains xp — `grep -c
XP_ORBS_GAIN` over all ten is zero — so all ten lanes are identical before and
after, which is exactly the hole this directory exists to cover.
