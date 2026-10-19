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
