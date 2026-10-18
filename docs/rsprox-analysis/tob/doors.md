# Theatre of Blood doors and transitions

Written 2026-10-18. It covers where every Theatre of Blood door puts the
player, and the screen transitions (the Theatre HUD's crimson cover and room
name) and dialogue that come with it.

Read from 10 sessions: 3299, 3285 and 788 (rev 234, named) and 1764, 1779,
1833, 1837, 553 and 582 (rev 227, named through the compacks).

    python3 tools/rsprox/rsprox_tob_doors.py ~/.rsprox/binary/archive/{3299,3285,788}.txt
    python3 tools/rsprox/rsprox_tob_doors.py --tsv FILES   # one row per op

The tool follows the local player for 40 ticks after each op on a `tob_*` loc.
Instance coordinates come out mapped back to the template, so a landing tile
minus the room's template square is directly our `^tob_*_arrive_lx/lz`.

## Where each door lands

"Local" is relative to the room's template square: Maiden m49_69 (with
m50_69 at lx 64+), Bloat m51_69, Nylocas m51_66, Sotetseg m51_67, Xarpus m49_68
(plane 1), Verzik m49_67 and the vault m50_67.

| Door (loc) | Lands on | Local | Count | Ours |
|---|---|---|---|---|
| `tob_surface_raid_entrance` | 3220,4460 | Maiden 84,44 | 40 / 40 | **not changed** (79,31), see below |
| `tob_dungeon_walkway_exit_clickbox` (Maiden) | 3322,4448 | Bloat 58,32 | 31 / 31 | changed to 58,32 |
| walkway exit (Bloat) | 3295,4283 | Nylocas 31,59 | 31 / 31 | changed to 31,59 |
| walkway exit (Nylocas) | 3294,4315, then the next tick 3279,4293 | Sotetseg 30,27, then 15,5 | 29 / 29 | now two teleports a tick apart |
| walkway exit (Sotetseg) | 3170,4375,1 | Xarpus 34,23 | 29 / 29 | changed to 34,23 |
| `tob_dungeon_xarpus_arena_door_exit` | 3168,4299 | Verzik 32,11 | 29 / 29 | **not changed**, see below |
| `tob_dungeon_verzik_throne_door_opened` | 3237,4307 | vault 37,19 | 27 / 27 | already 37,19 |
| `tob_sotetseg_darkrealm_exit` | 3275,4327 | — | 28 / 30 | already the same |
| `tob_treasureroom_teleportout` | 3677,3217 | Ver Sinhaza | 27 / 27 | `^tob_outside` changed from 3677,3219 |

The Nylocas exit teleports twice: first to 30,27, then a tick later to 15,5.
The room name is shown a tick after the second teleport.

**Maiden is not exact.** Landing on 84,44 builds the scene from lx 32, and
Maiden's body is lx 26..31. Nothing rebuilds the scene until a raider goes west
of lx 48, the tile just inside her barrier. The live server has her in view on
lx 48: 3299 adds her the tick the player steps there. Ours clips npc info to
the scene, so from that tile she isn't there. The Maiden solver waits there to
see her, and `tob_maiden` died on lx 48. From 79,31 the scene starts at lx 24.
Moving to the recorded tile needs the npc-info clip settled first.

**Verzik is not exact.** Our m49_67 is blocked on plane 0 south of z14, so a
player put on 32,11 cannot walk. We keep the gate teleport and the walk in.

## The transitions

The two client scripts are 2307 `tob_hud_portal(name)` (blood swoosh, opaque
crimson) and 2306 `tob_hud_fade(trans, jump, name)` (eases the crimson to
`trans` and shows the name). T is the teleport's tick.

| Door | T−1 | T | Later |
|---|---|---|---|
| Entrance | mes "You enter the Theatre of Blood (X Mode)...", synth 1952, 2306(0, 0, "The Theatre awaits...") | teleport | T+1: 2306(255, 0, "The Maiden of Sugadinti") |
| Room exits | 2307(""), synth 3952 | teleport | T+1: 2306(255, 0, name). The names are "The Pestilent Bloat", "The Nylocas", "Sotetseg", "Xarpus" and "The Final Challenge". |
| Nylocas exit | 2307(""), synth 3952 | teleport to 30,27 | T+1: teleport to 15,5. T+2: 2306(255, 0, "Sotetseg"). |
| Vault door | synth 1952, 2306(0, 0, "Verzik Vitur's Vault") | teleport | T+1: 2306(255, 0, "Verzik Vitur's Vault") |
| Treasure-room exit | partystatus 3, jingle 152 (`quest_complete_1`), 2307("You fought well."), synth 3952 | teleport | T+1: partystatus 1, 2306(200, 0, "You fought well."). T+2: 2306(200, 0, ""). |
| Dark-realm exit | synth 3963 | — | 2306(255, 0, ""), then the realm's darkness overlay is closed |

The HUD is opened once and every card plays on it. The recording never sends
`tob_hud` again when a room is entered.

The treasure-room exit also sends song 147 ("The Curtain Closes", already
playing) at T−1. In Ver Sinhaza, song 556 ("Welcome to the Theatre") starts at
T+1 from the area's music.

## Dialogue

**The entrance, the first time** (3285):

1. Mesbox: the warning ("<col=7f0000>Warning:</col> The Theatre of Blood is
   <col=7f0000>dangerous</col>. Once you enter, you are at …").
2. Mesbox: "You will not see the warning again should you accept."
3. "Accept the warning and proceed?" with "Yes - proceed." / "No - stay out."
4. The Mysterious Stranger (`tob_stranger`), three lines:
   - `chatneu3`: "Verzik's magic prevents most forms of teleportation. However,
     there is a special crystal with the power to overcome this restriction."
     `tobquest_stranger_op` goes to 1 on this line's continue.
   - `chatshifty3`: "I have... acquired some shards of the crystal. …"
   - no animation: "I can sell you some of these shards if you wish."
5. "Select an option" with "Go and buy teleport crystals." / "Enter the Theatre
   without any teleport crystals."
6. The leader's ready check (below).

**The entrance, later entries** (3299). There is no warning. The crystal question
is asked on every entry until its third answer is picked, and never again
after that:

- Header: "Only Verzik's crystals can teleport out of the Theatre."
- Options: "Go and buy teleport crystals." / "Enter the Theatre without any
  teleport crystals." / "Enter the Theatre, and don't ask this again."

No cache varbit carries that choice, so ours keeps it in the server-only perm
varp `varp7486_tob_crystal_dont_ask`.

**The ready check.** "Is your party ready? Members: N. Mode: X." with "Yes,
let's go!" / "Cancel." It is asked of a party of one too (3285, "Members: 1").

**A member before the leader** (3299, 7 times). The mesbox is "Your leader,
<name>, must go first." The rev-227 sessions 1845 and 1852 also record
"must enter first."

**A room's barrier.** Any raider clicking an unstarted barrier is asked "Is
your party ready to fight?" with "Yes, let's begin." / "No, don't start yet."
(788 and 3299, 9 times). In 3299 the recording player is a member
(Settings0082 leads) and starts Maiden, Bloat, Sotetseg and Xarpus. It starts
Verzik too, by talking to her: "Back again for more? …", then the same
question. On "Yes", in the same tick:
- the menu closes;
- the bar opens (6447 = 1, 6448/6449 = 1000);
- the player is put two tiles across, a short teleport the player info sends
  as a walk.

A started or cleared barrier steps the player across the same way with no
question. Every recorded menu that closes without an answer was closed by a
prayer click; no recording answers "No".

Ours matched this on 2026-10-18. It dropped the leader-only rule of 2026-10-06
from the barrier and from Verzik, and added the `if_close` on the answer.

**The barrier from inside a running fight** (788, 5 times). The game message
"You must stay and fight!" and the Vyre Orator (`tob_male_orator`, `chatang1`)
saying the same line. The barrier does not open.

**The treasure-room exit** (3299). The header is "Make sure you've collected
all your stuff!" with "Leave the Theatre." / "Stay in the Theatre." The
transition above follows on the answer's tick.

## Ours (2026-10-18)

- `tob.constant`: the arrival tiles above and `^tob_outside`.
- `tob_raid.rs2`: the Sotetseg two-step, and `~tob_leave_hud(false)` for the
  treasure-room exit.
- Every step a tick after a teleport now runs on `[timer,tob_hud_step]`
  (`varp7488_tob_hud_step`): the room cards, Sotetseg's second teleport, the
  vault card and "You fought well." A `queue(x, 0)` is next tick only when it
  is asked for after that tick's queue pass. From `[queue,tob_transition_build]`
  or a script resumed out of `p_delay(0)` it ran the same tick: Sotetseg's two
  teleports were measured landing together. A timer fires on
  `tick >= armed + 1` from any phase.
- `tob_title.rs2`: the cover and reveal model:
  - `~tob_title_cover_fade` is 2306 to 0 with the stairs synth;
  - `~tob_title_portal` is 2307 with synth 3952;
  - `~tob_title_reveal` is 2306 to a transparency.
- `tob_vault.rs2`: the vault door's cover, teleport and reveal; the exit's
  dialogue and "You fought well."
- `tob_party.rs2`: the entrance's warning, Stranger, crystal question and
  leader mesbox; the barrier's wording, any raider starting a room, and "You
  must stay and fight!". `tob_verzik.rs2`: any raider starts Verzik.
- `tob_hud.rs2`: the HUD is opened once (`varp7487_tob_hud_mounted`).

Checked on scriptrun (seed `sa`, trio): `tob_maiden`, `tob_bloat`, `tob_nylocas`,
`tob_sotetseg`, `tob_xarpus`, `tob_verzik` (through the vault and the exit) and
the full `tob_relay` all pass. Nylocas→Sotetseg lands on 30,27 at T and 15,5 at
T+1. The vault door plays its synth at T−1 and teleports at T. The exit's answer
plays jingle 152 and synth 3952, and the teleport to 3677,3217 is the next tick.

Not checked: the lobby door's warning, crystal question and leader mesbox, and
the barrier's "You must stay and fight!". The door runs only on the live client
(`tob_lobby.lua`), and no test clicks a barrier from inside a running fight.

Not done:
- Maiden's and Verzik's arrival tiles (above).
- The Mysterious Stranger's crystal shop behind "Go and buy teleport crystals."
  That answer just leaves the player outside.
