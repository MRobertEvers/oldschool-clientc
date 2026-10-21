# The Nexus

Read from every Nexus stay in 14 and 1852. Full inventory: [nexus.out](nexus.out).

**Ours:** `minigame_toa/scripts/toa_nexus.rs2` (doors compared 2026-10-19).

## Doors

Each path's door is a loc in three or four states, re-sent on every arrival:

| Path | Open / selected | Unselected | Closed (done) |
|---|---|---|---|
| Scabaras | `toa_nexus_scabaras_door` (46155) rot 0 | `_unselected` (46156) | `_closed` (46157) |
| Apmeken | `toa_nexus_apmeken_door` (46158) rot 1 | `_unselected` (46159) | `_closed` (46160) |
| Crondis | `toa_nexus_crondis_door` (46161) rot 3 | | `_closed` (46163) |
| Het | `toa_nexus_het_door` (46164) rot 3 | `_unselected` (46165) | `_closed` (46166) |
| Wardens | `toa_nexus_wardens_door` (46167) rot 2 | | `_open` (46168) once four paths are done |

**Where a door lands you** (every door teleport in the archive, 54-57 a door,
room-local): Scabaras x 2-3 z 31-33 (one tick on (32,32) first), Crondis and
Het x 49-50 z 31-33, Apmeken x 16-17 z 31-33, the Wardens x 31-33 z 56-57
plane 1 (one tick on (32,34) first): a random tile of the patch per raider, at
the path's NEAR end. The path starts on the crossing (Scabaras' barrier,
Crondis past x 47), not on the door. The paths' far doors land the same way:
Kephri x 14-15 z 31-33, Zebak x 53-55 z 31-33 (one tick on (29,32) first),
Akkha x 48-50 z 30-32 plane 1, Ba-Ba x 15-17 z 31-33, the vault x 31-33 z 49-50.
**Ours (2026-10-19):** `~toa_room_entry_land` (toa_raid.rs2) draws the tile;
the door had put the party on the room's fight tile, the far end of the path,
and started it. The one-tick centre tiles are not modelled.

The `_unselected` states are sent on arrival +11..17, when the leader picks
the next path. The completed bosses stay as their dead npcs
(`toa_zebak_dead`, `toa_zebak_tail_dead`, `toa_kephri_boss_dead`, Akkha's
`akkha_headbar_npc`s), and `toa_pottery_bankdeposit` (49583) rot 2 is placed.

## Supplies

`toa_midraidloot_trader` (11694) is here. Arriving opens `toa_midraid_loot`
(777) in `toplevel_pre_eoc` (164):16 on arrival +6/7 with setevents on
components 4, 7, 10 (2..8) and 6, 9, 12 (0..9); it closes 2 ticks later.
He stands on his tile facing south until removed (4292, both visits).

### What he offers (2026-10-21)

99 rolls in the 20 sessions with a Nexus, each raid's invocations read from its
6729 masks (bit = `toa_invocation` index; Leagues invocations are absent from
that table, which is why a Leagues raid's level is above the sum). Five tiers:

| tier | rolls | restore (nectar+tears pair, then ambrosia/silk/scarab) | balance | boost (salts, adrenaline) |
|---|---|---|---|---|
| Entry Mode, no spirit invocation | 6 | 17 + 5 | 13 | 3, 2 |
| Normal / Expert, none | 39 | 11 + 4 | 9 | 2, 1 |
| Need Some Help? | 28 | 8 + 3 (4 4 1 1 1, 28 of 28) | 7 | 2 or 1 (6 of 28), 1 |
| Need Less Help? | 2 | 4 + 3 | 4 | 1, 1 |
| No Help Needed | 4 | 2 + 3 | 2 | 1, 1 |

On a Diet takes the silk: Normal 5 5 2 2 (4292, 2 of 2), No Help Needed
1 1 1 1 (553, 2 of 2). Dehydration and Overly Draining change nothing. The
wiki's 66 / 33 / 10 % does not describe these numbers. Ours:
`toa_supply_bag.rs2` `~toa_spirit_tier` and its tables; a scratch test rolled
every tier 6 times, 42 of 42 in the recorded shapes.

## Messages and interfaces

- Song `song_730` "Beneath Cursed Sands", re-sent every 3 ticks.
- A raider who is not the leader trying a door: `messagebox` (229) in
  `chatbox` (162):565, "Your leader, <name>, must go first.", with
  setevents PAUSEBUTTON on 229:2 and "Click here to continue".
- The leader leaving early: "Some of your party don't seem to have arrived
  yet.<br>If you proceed, they will be abandoned."
- `pvp_icons` (90):45 shown on arrival.

## The exits: abandoning the raid

Source: local index **4288** [693..703] (rev 241, solo), on `toa_zebak_exit`. It is the only exit taken in any recording.

The flow:

| tick | what is sent |
|---|---|
| L+1 | face to the exit (`sizez=3`) |
| L+2 | mesbox "You are about to <col=ad2800>abandon the raid</col>. If you do this, you <col=ad2800>will not</col> be able to return to your current run." |
| on continue | chat menu "Abandon the raid?" with "Yes, abandon the raid." / "No, I want to stay." |
| on Yes | "You abandon the raid and leave the Tombs of Amascut." and the fade |
| +1 | minimap off |
| +3 | the lobby (3359,9113), raid varbits cleared, `toa_mycontroller` -1 |
| +4 | fade back |

From the Yes on, this is the vault leave's own shape.

Every exit is "Exit" / op1 Leave with loc category 1657:

| loc | room(s) and local tile |
|---|---|
| `toa_door_exit` | the Nexus (29,43), Scabaras (1,30), Het (51,30), Apmeken (15,30) |
| `toa_crondis_exit` | Crondis (51,30) |
| `toa_zebak_exit` | Zebak (57,31, plane 1) |
| `toa_door_exit_wardens` | the Wardens' first room (30,58, plane 1) |
| `toa_door_exit_small` | placed in no shipped square |

Kephri, Akkha, Ba-Ba and the Wardens' second room have none.

**Ours:**
- **Exits:** all five are bound to `~toa_abandon_exit` (`toa_nexus.rs2`). The unrecorded exits share the recorded answer: same category, name and op.
- **Before this:** the Nexus door used the vault's "Are you ready to leave?", and the room exits did nothing.
- **Shared leave:** the leave is `~toa_leave_tombs(line)`, shared with Osmumten's vault leave.
- **Test:** `test/raids/toa_exits.lua` checks every exit's box and menu, "No" keeping the raid, and Zebak's "Yes" ending in the lobby (29/29).
