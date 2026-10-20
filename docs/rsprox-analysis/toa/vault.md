# The vault (Osmumten's burial chamber) and leaving

Read from 17 vault visits after 18 Wardens kills: 14 (solo, Entry), 101 (4
solos, 0-700), 553 (3), 1779, 1845 and 1852 (8 party raids). D is the
Wardens' death, V the arrival in the vault, T the "Yes." to leave. 1852 and
1845 are Leagues worlds: their drops are boosted, so no rate is read from
them, only the shape.

**Ours:** `minigame_toa/scripts/toa_rewards.rs2` (the vault section),
`toa_osmumten.rs2` (leaving), `^toa_var_vault_*` / `^toa_vault_*` in
`toa.constant`, varp 3590 declared in `toa.varp`. Tests:
`test/raids/toa_vault_tells.lua` (own chest) and
`toa_vault_purple_tells.lua` (the sarcophagus), both the whole raid from
`::toa 10`; `_toa_wardens_debug.lua` for the cheat.

## At D (18 of 18)

The loot is rolled at the death, not at the chest. Each raider's varbits are
set on the death tick (and re-sent at V):

| Varbit | Value |
|---|---|
| `toa_vault_chest_0..7` (14356-14360, 14370-14372) | 2 on the raider's own slot's chest (`_mine_standard`), 4 on every other (`_empty`); 4 on their own too if the purple is theirs |
| `toa_vault_sarcophagus` (14373) | 3 the purple is theirs, 1 someone else's (both `_closed_rare`, Open), 4 no purple (`_closed`) |
| `toa_should_have_loot` (14319) | 1, to 0 on the take that empties the chest |

The third line of the death tick: "Your completed Tombs of Amascut: Entry
Mode count is: <col=ff0000>N</col>." One unique a raid; its owner's loot is in
the sarcophagus and their side chest is empty.

## Into the vault (17 of 17)

The teleport crystal, each raider on their own: clicked from where they stand
(3 tiles from the crystal, inside the Warden's blocked 5x5), the fade out on
the click, V = click+3 on (31..33, 49..50) with `cam_reset`, the fade in V+1,
the raid HUD V+4. No message. Osmumten (`toa_osmumten_vault`, multi on
`itt_met_osmumten`: "Spirit" the first time) at (32,23).

## A side chest (14 of 14)

Chests `toa_vault_chest_loc0..7` at (38,25) (25,25) (38,22) (25,22) (38,28)
(25,28) (38,19) (25,19), east rot 1, west rot 3; slot n's is locn (the
solos' loc0; 1845/1852's recorder, slot 1, loc1). Open, on arrival A:
`human_pickuptable` d10; A+1 the chest becomes `_open`, `_open02` or
`_open03` (7 / 4 / 3, no rule found: random), `chest_open` d20 and
`found_gem` synths, `toa_chests` (771) over inv 811 (clientscript 149 "Take",
"Take-5", "Take-All", "Take-X"; ops 1-4, 10 on cells 0-5); A+2 the
collection log lines. Take, Take-all (`inventory`), Bank-all (`bank`).

## The purple (3 own: 101 #3-4, 1779; 8 seen in parties)

While closed, `toa_loot_purple_flame_loop_06` range 7 at (31,20) every 2
ticks. Its owner opens `toa_vault_sarcophagus_parent` (31,20) rot 2; O the
tick they reach it:

| Tick | Recorded (every one alike) |
|---|---|
| O | `human_pickuptable` d10; the loc becomes `toa_vault_sarcophagus_open`; `toa_loot_chest_open_01` d20 |
| O+1 | `toa_osmumten_chest_reveal` on the sarcophagus; `_chest_slide_04` d37, `_chest_turn_02` d132 |
| O+8 | `toa_osmumten_chest_open`; the loot orb `toa_vault_loot01_voucher` on (31,21) rot 2; `toa_loot_orb_03` |
| O+15 | "<col=ef1020>Name</col> found something special: <col=ef1020>Item</col>" to everyone in the vault; the orb shows the item: `fx_cycle_ring` (Lightbearer), `_masori_head` (mask), `_sword` (fang), `_ward` (ward); `_masori_top` / `_legs` / `_staff` by name; `toa_loot_inv_01` |
| search | `found_gem`, the interface with the unique and the tertiaries, the collection log |
| unique taken | the orb's tile `invisible_type8_nonblocking` |

All the sounds range 7 at (32,22). The pet, Tumeken's guardian (101 #4, 1845),
comes on the owner's claim with the same "found something special" line.

## Leaving (17 of 17)

Osmumten's Leave (op 3), or Talk-to ending in the question: "Are you ready to
leave the Tombs of Amascut?" Yes. → T: "You leave the Tombs of Amascut." and
the fade out; T+1 the minimap off; T+3 the lobby on (3358..3360,
9113..9114); T+4 the fade in, the minimap back, the lobby song (734); T+6 the
overlay closed.

## Ours

Every recorded step above to the tick: the D varbits and line, the crystal's
+3 / +1 / +4, the chest's anim and A+1 open, the four reveal steps
(O / O+1 / O+8 / O+15, checked in the tick log), the take, the orb's removal,
the leave's T / T+1 / T+3 / T+4 / T+6.

**Not recorded, from the wiki:** the unique's chance (team points), the pet's
(1% per 350,000 - 700 x scaled raid level points), loot left in the chest is
claimed from the lobby's Rewards Niche (`toa_rewards_chest_lobby`, (42,26),
shown open while 14319 is 1). **Ours differs:** the unique's owner is any
raider at random (the wiki weights by personal points; this tree keeps only
the party's); the pet goes into the backpack or the chest (no follower system,
as ToB); the lobby song plays at T+3 with the teleport, not T+4; the
clientscripts the recording sends at D and at the chest (loot tracker 7191-
7194, 4517 / 4518) are not.

## `::toawardens`

`::toawardens <n>` (and `::toa`, third page, "The Wardens..."): 1 phase one
started, 2 phase two (the obelisk broken at once), 3 phase three, 4 the enrage,
5 the vault with your own loot, 6 the vault with the raid's purple yours.
`::toapurple` inside a raid gives you its unique at the Wardens' death.
