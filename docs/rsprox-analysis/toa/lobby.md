# The lobby: the party, invocations, presets and the way in

Read from 17 lobby visits: 14 (1), 101 (4), 553 (3), 1779 (1), 1845 (1) and
1852 (7). The recorder leads a solo party in 14, 101, 553 and 1779, and is a
member of Eo-Lahallia's party in 1845 and 1852. No recording shows a leader's
view of an applicant arriving, a kick, a disband, a preset load or the Nexus
exit.

**Ours:** `minigame_toa/scripts/toa_party.rs2` (the party record),
`toa_lobby.rs2` (the overlay, 772, 774, presets, the entrance),
`toa_presets.rs2`, `toa_nexus.rs2` (`toa_door_exit`), `configs/toa_party.*`.
Tests: `test/raids/toa_lobby_tells.lua` (solo, all of it) and
`_toa_lobby_party.lua` (two seats; `--bots 2`).

## Interfaces

| Interface | Name | What |
|---|---|---|
| 773 | `toa_lobby` | the overlay, `overlay_hud`; `names` (773:5) the members, eight lines, `-` for a free place |
| 772 | `toa_partylist` | the Grouping Obelisk's list |
| 774 | `toa_partydetails` | one party: Members, Applicants, Invocations, Summary |

The overlay opens on arriving in the lobby square and closes with the entry
line. `toa_client_partystatus` (14345): 0 No Party, 1 Party, 2 Step Inside
Now! (the leader is in). `toa_mycontroller` (varp 3603) names the party, -1
none; an applicant keeps -1 until accepted.

### 772

The obelisk (46068): the list, 45 × clientscript 6601 (line, one string), then
`if_setevents` 772:1 0..2 and 772:16 0..44 (pause). A line is eight names (the
viewer's own party's leader in `<col=ffffff>`), members, completions required,
invocations, raid level, mode and age in ticks (14: 2058 → 2147 over t11 →
t100). 772:1 subs (enum_4794): 0 Refresh, 1 Create Party / My Party, 2 the
friends filter (varbit 14318). 772:16 sub n: line n's party, on Members.

### 774

Opened afresh for every change: eight 6722 (viewer's role, `name|combat|att|
str|ranged|magic|def|hp|prayer|entry / normal / expert`), one 6727 per
applicant, 6729 (role, completions required, invocations, raid level, view,
three client masks), then `if_setevents` 774:1 0..97 (pause) and 774:98 0..5
(op1, op2). Roles: 2 leader, 1 member, 3 applicant, 4 may not apply, 0 may.

| 774:1 sub | Button | Recorded |
|---|---|---|
| 0 | Back | |
| 1 | Refresh | |
| 2 / 3 | Unblock / Completions | |
| 4 | Apply / Withdraw / Leave / Disband | "You have applied to join the party of X." then 6729 role 3, view 1; already in a party: "You are already in a party." "Stay in my existing party." / "Quit that one and apply to this one." (quitting: mycontroller -1, status 0, dashes; no line) |
| 5 | Clear All | "Are you sure you want to clear all active Invocations?" Yes. / No. |
| 6 / 7 | Load / Save | Save: varps 3680.. = the masks, "Your preset has been saved.", 14541 → 0, `found_gem` |
| 8..11 | the views | |
| 12+i, 20+i, 28+i | Kick, Move up, Move down | |
| 36+i, 44+i | Accept, Decline | accepted (to the applicant): mycontroller, "Your application to the party of X has been accepted.", the panel closed, `found_gem`, status 1 |
| 52+row | an invocation (`enum_4664` row; `param_1159`) | on `ancient_spell_activate` (6589); off `ancient_spell_deactivate` (6588); on in place of its category's sibling both, the second at delay 15 |

774:98 (the presets): op1 Select (`toa_preset_selected` 14541 = slot + 1), op2
Clear. Create Party opens on Applicants (view 1); My Party and a list line on
Members (view 0). A new party starts with the leader's last selection.

## The way in

`toa_lobby_raid_entry` (46089):

1. Loot left in the vault: the mesbox "A nearby chest contains some
   uncollected rewards..." on every click until claimed.
2. A member before the leader: "Your leader, X, must enter first." (24).
3. Until "Yes, and don't ask again.": the mesbox "You are about to enter the
   Tombs of Amascut which is not a safe area. ...", then "Are you sure you
   wish to enter?" Yes. / Yes, and don't ask again. / No.
4. "You enter the Tombs of Amascut (Entry Mode)..." (Normal has no mode).
   To every member outside: "X has entered the Tombs of Amascut. Step inside
   to join her..." and status 2.

The party outlives the raid (101 came back to it after every raid, aged).
Completions are counted by mode ("Your completed Tombs of Amascut: Expert
Mode count is: 1." after an Entry completion).

## Ours

Every recorded step above, checked on scriptrun: the overlay, the list,
Create Party, an exclusive pair's two sounds and levels (101's +10 then +15),
Save (64 in 3680, the line, the sound), Clear All, Load, the warning and
don't-ask-again, the entry line, the Nexus exit and the party after it; with
two seats: apply, accept, must-enter-first, the leader's line and status 2,
the member into the leader's Nexus.

**Not recorded, ours:** a party is required at the door ("You need to be in
a party to enter the Tombs of Amascut. Form or join one at the Grouping
Obelisk."); the leader's Kick / Quit Party / Disband and Withdraw say
nothing; Load re-applies each invocation through the panel's rules; Clear
empties a preset. The Nexus exit (`toa_door_exit`, Leave) asks the vault's
recorded "Are you ready to leave the Tombs of Amascut?" and leaves as the
vault does. The obelisk's first-read explanation is the Into the Tombs
story's (101 #1, `itt` 4 → 6), which this tree does not model. The 239 panel
has no Promote (the wiki's 2023 change); the arrows reorder.
