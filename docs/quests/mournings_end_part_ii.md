# Mourning's End Part II -- pinned wiki brief

Content-parity source for `quest_mourningsendpartii`. Pinned 2026-09-23 for the
parity1b pass (mourningsendpartii's Temple of Light leg). LostCity has no
implementation of this quest (2005-era content, past its Sept 2004 cutoff --
confirmed by grep of both `LostCity_Content2` and `LostCity_Server`), so this
quest is source=wiki, built from this brief plus Quest Helper's
`MourningsEndPartII.java` (1,100 lines, `~/Documents/git_repos/quest-helper`).

## References

| Reference | Pinned | Use |
| --- | --- | --- |
| [Mourning's End Part II](https://oldschool.runescape.wiki/w/Mourning%27s_End_Part_II) | revid 15351344, 2026-09-19 (`action=query&prop=revisions` on 2026-09-23) | Walkthrough, rewards |
| [Mourning's End Part II/Quick guide](https://oldschool.runescape.wiki/w/Mourning%27s_End_Part_II/Quick_guide) | fetched 2026-09-23, no oldid resolved this pass (quick guide page id not queried; content below is stable long-form guide text, low volatility) | Exact per-puzzle mirror/crystal steps |
| `MourningsEndPartII.java` | quest-helper HEAD in this checkout | Varbit/object identifiers (`VarbitID.MOURNING_LIGHT_TEMPLE_*`, `ObjectID.MOURNING_TEMPLE_PILLAR_*`), step ordering, `WorldPoint`s for Puzzle 1's five pillars |

## Getting to the Temple of Light (getCrystal leg -- already real content)

"To reach the Temple of Light, you must first equip full mourner gear and
travel to West Ardougne. Head to the Mourner HQ basement and speak with
Essyllt, who gives you a new key to access the tunnels. Run west through the
tunnel (praying Protect from Melee to avoid level 182 Dark Beasts) until you
reach the temple entrance. Search the corpse of the guard slumped against the
northern wall to get Edern's Journal." Inside, "navigate to the top floor via
stairs and ladders, then head east to the blackened crystal. Use your chisel
on the crystal to break off a piece of blackened crystal." Return to Arianwyn,
who has Eluned enchant it into a newly made crystal.

This leg landed in the parity1 pass: `mend2_temple.rs2`'s
`[oplocu,mourning_temple_obsidian_crystal_dead]` (chisel) and
`[oploc1,mourning_dead_guard4]` (journal search), gated in
`mend2_shared.rs2`. See `build/parity_state/parity1/mourningsendpartii.parity.progress.md`.

## The Light Puzzles -- Chest #1 (implemented this pass, real content)

Quest-helper's own five `WorldPoint`s, all Temple of Light floor 1 (z=1),
region m29_72 (matches `mend2_temple.rs2`'s own region note for the crystal
one floor above):

| Step | Object | Tile | Action |
| --- | --- | --- | --- |
| Pillar 1 | `MOURNING_TEMPLE_PILLAR_2_9` | 1909,4639,1 | "Put a mirror in the pillar next to the dispenser and have the light point north." |
| Pillar 2 | `MOURNING_TEMPLE_PILLAR_2_7` | 1909,4650,1 | "Put a mirror in the pillar to the north and point it west." |
| Pillar 3 | `MOURNING_TEMPLE_PILLAR_2_6` | 1898,4650,1 | "Put a mirror in the pillar to the west and point it south." |
| Pillar 4 | `MOURNING_TEMPLE_PILLAR_2_11` | 1898,4628,1 | "Put a yellow crystal in the pillar to the south." |
| Pillar 5 | `MOURNING_TEMPLE_PILLAR_2_15` | 1898,4613,1 | "Put a mirror in the pillar to the south and point it east." |

Wiki walkthrough, quoted: "Pull on the crystal dispenser and reset the
puzzle. Click on the crystal dispenser again to obtain four mirrors and a
yellow crystal." Then: "Place Mirror #1 to shine the light north" / "Place
Mirror #2 to shine the light west" / "Place Mirror #3 to shine the light
south" / "Place the Yellow crystal at the next junction. The light will turn
yellow" / "Place Mirror #4 to shine the light east over the gap." Then: "Cross
the gap to the other side using the wall supports on the south side."
`mourning_temple_wall_support`/`mourning_temple_agility_hanging` (op1=Climb,
`configs/all.loc:111093/111101`) is that crossing -- already real, generic
geography, same as the getCrystal leg's caves. Then: "open the chest to
obtain 2 mirrors and a cyan crystal" -- `mourning_temple_light_parts_2_closed`
(op1=Open) / `_open` (op1=Search), `configs/all.loc:107730-107744`, which
starts Puzzle 2.

Cache-verified objects for this pass: `mourning_temple_light_wall_lever`
("Crystal dispenser", op1=Collect, `configs/all.loc:107689`) is the dispenser;
`mourning_temple_pillar_2_6/2_7/2_9/2_11/2_15` (`configs/all.loc:110408-110485`)
are the five pillars, each a `multivarbit`-driven light-glow prop with no
native `op` (the interactive "place/rotate a mirror" trigger does not exist
anywhere in this cache dump and is authored fresh this pass as `[oplocu,...]`
on each pillar, cycling a facing value on `last_useitem == mourning_mirror`
and requiring one specific facing on `last_useitem == mourning_crystal_yellow`
for pillar 4); `mourning_temple_mirrors_reset_tray` (`configs/all.varbit:5533`)
is quest-helper's own `dispenserEmpty` -- the real varbit gating "already
pulled, do not hand out a second set." The per-edge light-color varbits
(`mourning_light_temple_2_15_east`, `_2_7_9`, `_2_6_7`, `_2_6_11`, `_2_5_6`,
etc., `configs/all.varbit:5973-6143`) are the same names
`VarbitID.MOURNING_LIGHT_TEMPLE_2_*` resolves to and are written to their real
solved values (their real in-game color constant) by each pillar's correct
placement, in propagation order, so a player (or the driver) reading them back
sees the authentic "solved" state at every step -- not a general beam-tracing
engine (no rotate-and-see-it-fail-elsewhere feedback for a wrong facing), but
the real objects, the real items, the real varbits, at their real final
values, gated on five distinct real player actions in the wiki's documented
order. See `legs_left` in the parity JSON for why the general n-directional
simulation itself (every pillar, every possible facing, all six puzzles) is
out of scope for one pass.

## Chest #2 through Death Altar (NOT implemented this pass -- legs_left)

Quick guide, condensed (full per-puzzle detail is in the quick guide page
itself if a future pass needs the pillar-by-pillar WorldPoints -- quest-helper
only declares `WorldPoint`s for Puzzle 1's five pillars; puzzles 2-6 would
need the same extraction done against quest-helper's own later `ObjectStep`
declarations, which name the pillar object ids and, for most, an explicit
`WorldPoint` too):

- **Chest #2**: reset+collect; Mirror #1 north, Mirror #2 west, place Cyan
  crystal, Mirror #3 north, Mirror #4 east, place Yellow crystal; open chest
  for 2 mirrors (no reset needed for #3).
- **Chest #3**: remove Yellow crystal, Mirror #5 up, climb north ladder,
  Mirror #6 west, Mirror #7 down (toward middle floor), reach the eastern
  yellow barrier, Mirror #8 south; open chest for 2 mirrors + a Fractured
  crystal; rotate a mirror east (no reset for #4).
- **Chest #4**: swap Cyan crystal for Yellow crystal, Mirror #7 south,
  Mirror #8 down; open chest for a Blue crystal.
- **Chest #5** (two parts, the largest): reset; Mirrors 1-3 north/west/south,
  Yellow crystal, Mirror #4 east, cross the gap, place Blue crystal, remove
  Mirror #4, rotate Mirror #3 up, go to the top floor; Mirror #4 south, place
  Fractured crystal, Mirrors 5-7 positioned, reach the bottom floor, Mirrors
  8-10 positioned; open chest for 3 mirrors + a second Fractured crystal.
- **Death Altar puzzle**: reset; Mirror #1 north, Mirror #2 down, Mirror #3
  west, place a vertical Fractured crystal, Mirror #4 north, place a
  horizontal Fractured crystal, Mirrors 5-6 up, place the Yellow crystal,
  Mirrors 7-13 positioned with a Blue crystal placement, rotate Mirror #14 west
  to enter the Death Altar -- **and turn it back toward the entrance door
  before leaving**, or the return route re-locks.

None of `mourning_light_temple_1_*`/`mourning_light_temple_3_*` (floors 0 and
2) or `mourning_pillar_light_cross_*`/`mourning_door_2_*` are touched by this
pass -- they remain exactly as the previous pass's audit found them, all zero.

## Rewards

"2 Quest points", "60,000 Agility experience", "A crystal trinket, which
allows you to enter the Temple of Light again after the quest", access to the
Death Altar, access to Dark Beasts, Dark Beasts teleport in the Slayer rings
menu. Matches `configs/mend2.constant`'s existing dbrow reading exactly
(`mourning_crystal_trinket`, `death_talisman`, 600000 tenths Agility XP); no
change needed there.
