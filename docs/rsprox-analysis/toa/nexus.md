# The Nexus

Read from every Nexus stay in 14 and 1852. Full inventory: [nexus.out](nexus.out).

**Ours:** `minigame_toa/scripts/toa_nexus.rs2` (not yet compared).

## Doors

Each path's door is a loc in three or four states, re-sent on every arrival:

| Path | Open / selected | Unselected | Closed (done) |
|---|---|---|---|
| Scabaras | `toa_nexus_scabaras_door` (46155) rot 0 | `_unselected` (46156) | `_closed` (46157) |
| Apmeken | `toa_nexus_apmeken_door` (46158) rot 1 | `_unselected` (46159) | `_closed` (46160) |
| Crondis | `toa_nexus_crondis_door` (46161) rot 3 | | `_closed` (46163) |
| Het | `toa_nexus_het_door` (46164) rot 3 | `_unselected` (46165) | `_closed` (46166) |
| Wardens | `toa_nexus_wardens_door` (46167) rot 2 | | `_open` (46168) once four paths are done |

The `_unselected` states are sent on arrival +11..17, when the leader picks
the next path. The completed bosses stay as their dead npcs
(`toa_zebak_dead`, `toa_zebak_tail_dead`, `toa_kephri_boss_dead`, Akkha's
`akkha_headbar_npc`s), and `toa_pottery_bankdeposit` (49583) rot 2 is placed.

## Supplies

`toa_midraidloot_trader` (11694) is here. Arriving opens `toa_midraid_loot`
(777) in `toplevel_pre_eoc` (164):16 on arrival +6/7 with setevents on
components 4, 7, 10 (2..8) and 6, 9, 12 (0..9); it closes 2 ticks later.

## Messages and interfaces

- Song `song_730` "Beneath Cursed Sands", re-sent every 3 ticks.
- A raider who is not the leader trying a door: `messagebox` (229) in
  `chatbox` (162):565, "Your leader, <name>, must go first.", with
  setevents PAUSEBUTTON on 229:2 and "Click here to continue".
- The leader leaving early: "Some of your party don't seem to have arrived
  yet.<br>If you proceed, they will be abandoned."
- `pvp_icons` (90):45 shown on arrival.
