# Forgettable Tale's tunnel tracks

Regenerates `OSRS-Content/osrs-content/server/scripts/quests/quest_forgettabletale/scripts/forget_tracks.rs2`
from RSProx index 4646 (docs/rsprox-analysis/quests/forgettable_tale.md, "The rides").

    # 1. the tunnel window of the transcript (track locs, moves, ops)
    A=~/.rsprox-index-cache/home-default/.rsprox/binary/archive
    awk '/^\[[0-9]+\]/{match($0,/[0-9]+/); t=substr($0,RSTART,RLENGTH)+0} t>=2790 && t<=4900 && /loc_add_change|loc_del|forget_quest|forget_region|forget_if[0-9]|oploc|if_buttonx\] com=forget_puzzle|teleport\] player|exactmove|synth_sound|message_game/ {print t"\t"$0}' $A/idx4646.txt | sed 's/  */ /g' > tun.txt
    # 2. validate the model against every recorded ride (must print "rides matched 20 of 20")
    python3 gen.py
    # 3. validate the step timing and turn sounds against every recorded ride
    #    (must print "timing matched 20 of 20" and "turn sounds matched 20 of 20")
    python3 delays.py
    # 4. validate the copy-local paths against the --raw transcript (must print
    #    "copy paths matched 20 of 20"); needs the --raw transcript and the all-filters
    #    one (home-wire, for rebuild_region), see copymap.py
    python3 copymap.py
    # 5. write forget_tracks.rs2
    python3 emit.py

`walk.py` reads the track pieces; `gen.py` builds the trees, the switch numbering and the
mouths, and replays every recorded layout; `delays.py` replays the same rides for each
step's delay and the ticks before the next one; `emit.py` writes the build and ride procs.

Step timing (`emit.py` `finalize`, the rules `delays.py` checks): a three-tile step that runs
into a turn, or into a mouth of delay 90 or more, goes at delay 90 and the next step waits a
tick more; the start is 135 then 90 (60 when its second step is two tiles); a stop (blocked,
buffer) is 80 over one tile and 130 over more; the recorded mouth tails keep their delays.
`gen.py` alone compares landing tiles only: its 20 of 20 said nothing about timing.

The coordinates `emit.py` writes are COPY-local. Jagex's copy of the tunnels is composed: the
template's planes 0 and 1 with the platforms' zones (template planes 2 and 3) copied over them
past each mouth (`copymap.SUBS`, from 4646's rebuild_region; built in the quest by
`~forget_tunnels_copy`). The default transcript translates those zones back to their template
tiles, which is what walk.py and gen.py read; `copymap.copy_tile` maps them into the copy again,
so no ride changes plane or jumps.
A platform's own pieces (its box, its cart, its track) are laid only where the `--raw`
transcript adds them (`copymap.raw_pieces`): group one copies one platform zone to two places,
and laying every piece in both put platform two's box on the tile platform one's rider steps
onto.
