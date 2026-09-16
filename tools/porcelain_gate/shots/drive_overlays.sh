#!/bin/zsh
# Every shot of the four overlay plugins, AFTER and BEFORE, and the controls
# that make them evidence.
#
#   drive_overlays.sh <after-binary> <preorbs-tree> <prehl-tree>
#
# <preorbs-tree> is a checkout of b8d48bfd8 -- pre-port for ground-items,
# loot-beam and xp-drop-orbs -- with a built binary at src/torirs_gfmatrix.
# <prehl-tree> is a checkout of 455308be3 with src/torirs_sh: entity-highlighter
# was ported at 23a32b616, BELOW b8d48bfd8, so preorbs is NOT its before.
#
# WHY THERE ARE CONTROL SHOTS. Three of these overlays animate, and a capture is
# one frame of an animation sampled at whatever wall-clock moment frame 700
# happened to be. Comparing two differently-built binaries then shows a
# difference that is nothing but speed. Each animated element gets a control
# that removes the clock before any difference is called a defect:
#   xp-drop-orbs  the "+N" label climbs over drop_duration ms -> xpslow-* pins
#                 it by slowing the climb 6.7x, and xporbs-cs2-repeat measures
#                 the same-binary noise floor.
#   loot-beam     the column USED to spin at `spin` deg/sec -> lb-nospin-* set
#                 spin=0 to hold it. The spin is gone (loot_beam.lua's header
#                 says why), so a beam is already still on the after binary and
#                 only the `old` shots below still have a key to pin.
#   highlighter   Romeo wanders and animates -> highlighter-cs2-repeat measures
#                 the floor, which turns out to equal the before/after "signal".
set -u

here=${0:A:h}
bin=${1:?after binary}; pre=${2:?preorbs tree}; prehl=${3:?prehl tree}
shot() { TORIRS_SHOT_BIN=$bin zsh $here/shot.sh "$@" }
old()  { TORIRS_SHOT_BIN=$pre/src/torirs_gfmatrix TORIRS_SHOT_WORKTREE=$pre zsh $here/shot.sh "$@" }
oldhl(){ TORIRS_SHOT_BIN=$prehl/src/torirs_sh   TORIRS_SHOT_WORKTREE=$prehl zsh $here/shot.sh "$@" }

XP='660,setlevel 10 45'
# dropobj is a content debugproc; it uses the ACTIVE PLAYER'S coord, so the pile
# lands under the player on the player's plane -- which is the whole reason the
# fixture's own plane-1 stacks never produced a label.
GI='60,dropobj abyssal_tentacle 1;70,dropobj ags 1;80,dropobj abyssal_whip 1'
TENT='60,dropobj abyssal_tentacle 1'
HL='60,entity-highlighter,tags,5037,6708,2880,2899,3106,3108'   # 5037 = Romeo, 1 tile away
T=(TORIRS_TRACE_NATIVE_UI=1 TORIRS_TRACE_PLUGIN_WORLD=1 TORIRS_GROUND_ITEMS_DEBUG=1)

# ---------------------------------------------------------------- xp-drop-orbs
shot xporbs-cs2        cs2 TORIRS_SIM_CMD=$XP $T
shot xporbs-cs2-repeat cs2 TORIRS_SIM_CMD=$XP $T          # noise floor
shot xporbs-cs1        cs1 TORIRS_SIM_CMD=$XP $T          # lane has no server
old  xporbs-cs2-before cs2 TORIRS_SIM_CMD=$XP $T
# the climb, with the clock taken out
shot xpslow-after  cs2 TORIRS_SIM_CMD=$XP TORIRS_SIM_PLUGIN_CONFIG='100,xp-drop-orbs,drop_duration,8000'
old  xpslow-before cs2 TORIRS_SIM_CMD=$XP TORIRS_SIM_PLUGIN_CONFIG='100,xp-drop-orbs,drop_duration,8000'
# setlevel lands EXACTLY on a level threshold, so the progress arc is zero-length
# in every capture ever taken of this plugin. This one gives it real progress.
shot xporbs-arc-cs2 cs2 TORIRS_SIM_CMD='600,setlevel 10 45;660,xp fishing 300000'

# ---------------------------------------------------------------- ground-items
shot grounditems-cs2        cs2 TORIRS_SIM_CMD=$GI $T
shot grounditems-cs1        cs1 TORIRS_SIM_CMD=$GI $T
old  grounditems-cs2-before cs2 TORIRS_SIM_CMD=$GI $T
shot gi-tile-outline-cs2    cs2 TORIRS_SIM_CMD=$TENT \
     TORIRS_SIM_PLUGIN_CONFIG='100,ground-items,highlight_tiles,1;110,ground-items,text_outline,1'

# ------------------------------------------------------------------- loot-beam
shot lootbeam-cs2        cs2 TORIRS_SIM_CMD=$TENT $T      # defaults: insane, pink
shot lootbeam-cs1        cs1 TORIRS_SIM_CMD=$TENT $T
old  lootbeam-cs2-before cs2 TORIRS_SIM_CMD=$TENT $T
shot lb-nospin-after  cs2 TORIRS_SIM_CMD=$TENT
shot lb-nospin-after2 cs2 TORIRS_SIM_CMD=$TENT
old  lb-nospin-before cs2 TORIRS_SIM_CMD=$TENT TORIRS_SIM_PLUGIN_CONFIG='100,loot-beam,spin,0'
shot lb-light-cs2        cs2 TORIRS_SIM_CMD=$TENT TORIRS_SIM_PLUGIN_CONFIG='100,loot-beam,style,light'
old  lb-light-cs2-before cs2 TORIRS_SIM_CMD=$TENT TORIRS_SIM_PLUGIN_CONFIG='100,loot-beam,style,light;110,loot-beam,spin,0'
# The port changed two tier rules. No drop: the fixture's own Spade and Knife are
# the only stacks, and one of them is worth EXACTLY the threshold, so >= and >
# disagree -- 2 beams before, 1 after.
shot lb-nodrop-after  cs2 TORIRS_SIM_PLUGIN_CONFIG='100,loot-beam,tier,low;110,loot-beam,low_value,1' $T
old  lb-nodrop-before cs2 TORIRS_SIM_PLUGIN_CONFIG='100,loot-beam,tier,low;110,loot-beam,low_value,1' $T
# The same change as a colour: only the low tier left enabled. A 0 threshold now
# DISABLES its tier; it used to match everything, so insane won first.
TIER='100,loot-beam,tier,low;102,loot-beam,low_value,1;104,loot-beam,medium_value,0;106,loot-beam,high_value,0;108,loot-beam,insane_value,0'
shot lootbeam-tierrule-cs2        cs2 TORIRS_SIM_CMD=$TENT TORIRS_SIM_PLUGIN_CONFIG=$TIER
old  lootbeam-tierrule-cs2-before cs2 TORIRS_SIM_CMD=$TENT TORIRS_SIM_PLUGIN_CONFIG="$TIER;110,loot-beam,spin,0"

# ----------------------------------------------------------- entity-highlighter
shot  highlighter-cs2        cs2 TORIRS_SIM_PLUGIN_CONFIG=$HL $T
shot  highlighter-cs2-repeat cs2 TORIRS_SIM_PLUGIN_CONFIG=$HL $T   # noise floor
shot  highlighter-cs1        cs1 TORIRS_SIM_PLUGIN_CONFIG=$HL $T
oldhl highlighter-cs2-before cs2 TORIRS_SIM_PLUGIN_CONFIG=$HL $T
