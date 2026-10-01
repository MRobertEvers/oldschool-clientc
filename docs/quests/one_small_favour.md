# One Small Favour (onesmallfavour) -- content brief (wiki-sourced)

Status: pinned 2026-09-28 (seam pass seam27, `onesmallfavour_five_legs`).
The quest dbrow gives the release date as **28 February 2005**, so it is not
in LostCity. The port is built from the OSRS wiki and Quest Helper's
`onesmallfavour/OneSmallFavour.java`, plus the cache's own dbrow, loc and
varbit schema (see the header of `quest_onesmallfavour/configs/onesmallfavour.constant`).

## 1. Pinned references

| Reference | Revision | Used for |
| --- | --- | --- |
| [One Small Favour](https://oldschool.runescape.wiki/w/One_Small_Favour?oldid=15332553) | 15332553, 2026-09-06 | Walkthrough order, items, lamps, vane, Tassie's clay, Horvik |
| [Transcript:One Small Favour](https://oldschool.runescape.wiki/w/Transcript:One_Small_Favour?oldid=15270998) | 15270998, 2026-07-21 | Dialogue and box text for Brian, Aggie, Horvik, Tassie, Gnormadium, the landing lights, the weather vane; varp 416 changes |
| Quest Helper `onesmallfavour/OneSmallFavour.java` | quest-helper `5ea99d5e` (2026-07-25) | `steps.put` ladder, item requirements, lamp WorldPoints |

Cache: `configs/all.dbrow [quest_onesmallfavour]` (endstate 285, 2 QP).
Progress is `%onesmallfavour` (varp 416). The lamp and vane sub-fields are
varbits on `onesmallfavourmulti`.

## 2. Facts the seam27 content restored, with their source

- **Aggie comes after Brian.** Brian's "Ok, ok, I'll do it! I'll go and see
  Aggie." is varp 416 15 -> 20 (Transcript). Quest Helper has
  `steps.put(20, talkToAggie)` and `steps.put(25, goTalkToJohanhus)`. Aggie
  sends the player to look for Jimmy the Chisel. Port: Brian sets 20
  (`^osf_aggie_agreed`), Aggie sets 22 (`^osf_aggie_told`), and Johanhus
  reads 22. The port's 25 is Johanhus's own hand-off.
- **Horvik takes three steel bars.** Seth owes him the bars, and Horvik takes
  them before he asks for medicine (Transcript "Asking Horvik about chicken
  cages"). Quest Helper's `talkToHorvik` requires `steelBars3`, and its
  `steelBars4` requirement is 3 bars for Horvik plus 1 for the vane.
- **Horvik has three visits.** First the steel bars. Second the medicine:
  "Wonderful! That's just great! I just need the pigeon cages now." Third,
  "I have the five pigeon cages you asked for!": the pigeon cages are
  replaced with chicken cages. Quest Helper: `steps.put(240,
  returnToHorvik)`, `steps.put(245, talkToHorvikFinal)`. Port: 235 -> 238
  (`^osf_horvik_medicine`) -> 240.
- **Tassie gives soft clay, not a pot.** Walkthrough: "talk to Tassie, who
  will give you some soft clay and teach you how to make pot lids ... Pick up
  the pot in the Village helmet shop if you don't yet have one." Quest
  Helper lists `pot` in `getItemRequirements` and has a `pickUpPot` step. The
  helmet shop pot is `m48_53.spawn` pot_empty at 3074,3431.
- **The eight landing lights are fixed at the lights.** Gnormadium's "Yes,
  I'll take a look at them." is 115 -> 120. Searching a light gives its uncut
  gem ("You find an uncut sapphire in the landing light.") and sets that
  light's bit in `checklandinglights`. Placing the cut gem ("You place the
  sapphire in the landing light. It seems to look right.") sets its bit in
  `fixedlandinglights`, followed by "You've fixed N landing lights so
  far..." or "You've fixed all the landing lights!". In the live game the
  eighth light is 120 -> 125; the port uses 122, because the port's 125 is
  Rantz's gate. Gnormadium's "I've fixed all the lights!" sets
  `all_lights_fixed` and moves the live game 125 -> 130 (the port: 122 ->
  125).
- **Light positions** (`maps/m39_46.jl2`, which match Quest Helper's
  take1..take8): jade 2554, red topaz 2551, opal 2548, sapphire 2545. Each
  gem has one light at z 2974 (row 1, `*light1_*`) and one at z 2969 (row 2).
- **Cutting gems.** The wiki item list asks for two each of cut jade, opal
  and red topaz, "or a chisel to cut the uncut gems received during the
  quest (high chance to crush them, up to 2 replacements available for 500
  coins each during the quest)". Sapphires cannot be crushed. Gnormadium
  sells a chisel for 10 coins (Transcript "Obtaining a chisel"). He sells a
  replacement uncut gem for 500 coins, at most two of each, against a
  crushed gem (Transcript "Obtaining more uncut gems"). The purchases are
  counted in the cache's `osf_bought*` varbits.
- **The weathervane repair is three actions.** Search it: "You search the
  weather vane... it might take quite a powerful force to loosen them."
  Use a hammer on it: "You give the structure a good solid whack...". Search
  it again: "You find a broken ornament...", "broken directionals...", "and a
  broken rotating pillar." This needs three free slots. Look shows "It looks
  pretty broken!" (Transcript "Weather vane"). Quest Helper: `steps.put`
  175 `searchVane`, 176 `useHammerOnVane`, 177 `searchVaneAgain`. Port:
  175 -> 176 -> 177 -> 178 (parts in hand).

## 3. Not ported

- Horvik's replacement for lost chicken cages (a pigeon cage and 100 coins
  each).
- Phantuwti's paid replacement vane parts and the other lost-item recovery
  paths.
- The cutscene when Gnormadium turns the lights on.
