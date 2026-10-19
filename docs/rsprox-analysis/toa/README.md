# Tombs of Amascut: recorded behaviour

These files record what Jagex's server sends in each ToA room, read out of
RSProx transcripts. One file per room; each has a `.out` beside it with the
full inventory the tool prints.

## Sources

| Submission | Rev | World | Raids | Party sizes |
|---|---|---|---|---|
| 14 | 223 | normal world | 1, Entry Mode | solo |
| 1852 | 227 | Leagues V | 7, Expert Mode | 2, 2, 2, 3, 6, 6, 6 |

Party size is read off the Crondis palm's base hitpoints (`varb6100_hpbar_hud_basehp`),
which is 200 a raider: 200, 400, 400, 400, 600, 1200, 1200, 1200.

**Caveats.**
- 1852 is Leagues V: relics change damage, accuracy and some timings of the
  *player*. Animations, graphics, projectiles, sounds, locs and interfaces are
  the content's own; hit values are not usable from it.
- 14 is Entry Mode: bosses are scaled down. Its hits are a normal world's but
  at Entry scaling.
- Transcripts show sequences and graphics on the recording player only; other
  raiders' are listed separately ("other players") where they were sent.
- Rev 223/227 transcripts print bare ids. Names come from our osrs239 tables
  (`configs/all.*.compack`, `pack/3_interfaces.pack`, `4_soundeffects.pack`,
  `12_clientscripts.pack`, `6_musictracks.pack`, `11_musicjingles.pack`).
  A bare number in a table means ours has no name for it.
- Heights are in our units (a v2 packet's raw value ÷ 4; v1 is already ours).

## Rooms

The rooms are fixed template regions, so coordinates are real and the same in
every raid:

| Room | Region | File |
|---|---|---|
| Nexus | 55,80 | [nexus.md](nexus.md) |
| Path of Crondis | 61,82 | [crondis.md](crondis.md) |
| Zebak | 61,84 | [zebak.md](zebak.md) |
| Path of Scabaras | 55,82 | [scabaras.md](scabaras.md) |
| Kephri | 55,84 | [kephri.md](kephri.md) |
| Path of Het | 57,82 | [het.md](het.md) |
| Akkha | 57,84 | [akkha.md](akkha.md) |
| Path of Apmeken | 59,82 | [apmeken.md](apmeken.md) |
| Ba-Ba | 59,84 | [baba.md](baba.md) |
| Wardens P1–P2 | 59,80 | [wardens_p1p2.md](wardens_p1p2.md) |
| Wardens P3 | 61,80 | [wardens_p3.md](wardens_p3.md) |
| Loot room | 57,80 | [loot.md](loot.md) |

## Tool

```sh
python3 tools/rsprox_toa.py ~/.rsprox/binary/archive/{14,1852}.txt --out docs/rsprox-analysis/toa
# (14 is transcribed with the existing RSProx checkout; 1852 was already transcribed.)
python3 tools/rsprox_toa.py FILES --room zebak --timeline --compact --visit 1852:2
```

`--out` writes one `<room>.out` per room. `--timeline --compact` prints a visit
tick by tick (relative to entry), one line per kind with its tiles collapsed.
A visit is one unbroken stay of the recording player in the room's region.

## Every room

These are the same in every room and are not repeated in the room files.

**Entering** (E = the teleport into the room):
- E: the player is teleported in; the room's npcs and locs are sent.
- E+1: `fade_overlay` (174) opened in `toplevel_pre_eoc` (164):1, clientscript
  948 `fade_overlay` `(0,255,0,0,50)`; `hpbar_hud` (303):5 hidden;
  `minimap_toggle` state 0; the lobby song `song_730` "Beneath Cursed Sands"
  (fades 0/60/60/0).
- E+2: "Challenge started: <room>."
- E+3: 164:1 closed and `toa_hud` (481) opened in it; the room's song.
- Clientscript 6580 (our `torirs_toa_timer`) runs about every tick with
  `(n, false)`; 6585 (`torirs_varcs_1099_1106`) sets the party names.
- varbits `toa_client_p0..p5` (14346–14351) carry each raider's health;
  `toa_damage_taken`/`_done` (14323/14325) and their `_current`
  (14375/14374) the room's damage; `toa_client_current_path` (14381) the path.

**The room's song is re-sent every 3 ticks** (`midi_song` with the same fades,
and `music` (239):9 re-set to its title) for as long as the room runs.

**Compared with ours:** Zebak and the Path of Crondis (each room file's "Ours
vs recorded"). The other rooms name our script but have not been diffed yet.

**Leaving** (F = the "Challenge complete" tick):
- F: "Challenge complete: <room>. Duration: <col=ef1020>M:SS.T</col>. Total:
  <col=ef1020>M:SS.T</col>", `midi_song` 147 (0/0/0/0) and `midi_jingle` 295
  `test_of_nobility_tombs_of_amascut`.
- F+1..: the lobby song 730 again.
- F+9..11: `hpbar_hud`:5 hidden, `fade_overlay` reopened in 164:1.
- F+11/12: "<name> has proceeded to the next challenge. Join her..." when
  another raider leaves first; `minimap_toggle` state 2.

## Teleport crystals (the boss rooms' way in)

Zebak's, Kephri's, Akkha's, Ba-Ba's and the Wardens' rooms have no barrier:
every recorded barrier click is in one of the four challenge squares. Each is
entered by a `Teleport crystal` placed by the map square, Use (op1) and
Quick-Use (op2) except the Wardens' (Use only). Read from 74 uses in 14 and
1852 (`oploc` on 45505, 45506, 45579, 45754, 45866):

| Room | Crystal (loc, tile, square-local) | Puts the raider on | Uses |
|---|---|---|---|
| Zebak | `zebak_teleport` 45506, (3954,5408) = (50,32) | (3941,5408) = (37,32) | 19 of 19 |
| Kephri | `kephri_teleport` 45505, (3538,5408) = (18,32) | (3545,5408) = (25,32) | 14 of 14 |
| Akkha | `akkha_teleport` 45866 (2x2), (3694,5407,1) = (46,31) | (3689,5408,1) = (41,32) | 12 of 12 |
| Ba-Ba | `baba_teleport` 45754, (3794,5408) = (18,32) | (3800,5408) = (24,32) | 19 of 19 |
| Wardens | `wardens_teleport` 45579 (2 wide), (3808,5172,1) = (32,52) | (3808,5166,1) = (32,46) | 8 of 8 |

- **The teleport**: spotanim `abyssal_demon_teleport` (409, no height or
  delay) on the raider and synth `quick_teleport` (198), on the tick of the
  move. The first raider through starts the room: "Challenge started: <boss>."
  and the boss overlay on that tick in the solo raid.
- **When**: a raider standing beside the crystal who has not moved for a tick
  goes on the click's tick (6 uses); one who stopped the tick before goes the
  tick after (face the crystal, then go); one who walks to it goes 2 ticks
  after arriving, facing it in between (7). That is LostCity's
  `p_arrivedelay` (wait while the player stepped this tick or the last) on an
  op that fires the tick after arrival.
- **A raider missing**: Quick-Use does nothing but the mesbox (interface 229)
  "Some of your party don't seem to have arrived yet.<br>If you proceed, they
  will be abandoned.", on every click until they arrive (34 uses had at least
  one). Use was never clicked with a raider missing.

**Ours** (2026-10-18, `toa_barrier.rs2`): the five crystals' ops, the tiles
above, the spotanim and synth, the room started on the first teleport, the
Quick-Use refusal with the recorded text. The rooms no longer spawn a barrier,
and Kephri's, Akkha's, Ba-Ba's and the Wardens' proximity start (12 from the
fight tile, which reached the lobby side) stops short of the crystal (4-5).
The engine's `p_arrivedelay` waited only for a step on the same tick; it now
waits as LostCity's does, which puts a walked-to crystal's teleport on
arrival + 2 (`test/raids/toa_crystals.lua`: steps on 9 and 10, the op on 11,
the teleport on 12). **Open:** Use with a raider missing is not recorded;
ours shows the same warning and teleports when it is clicked through, which
is what "If you proceed" offers. The Wardens P2/P3 room still spawns its
barrier; nothing was recorded there.
