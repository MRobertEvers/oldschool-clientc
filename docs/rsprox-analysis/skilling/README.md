# Make-X batch timing (2026-11-16)

Every production loop behind interface 270 `skillmulti`, checked against RSProx
after cooking was found running a batch too fast (OSRS-Content 3b01bbb1f1).

## Method

- Sessions: every session that opened `skillmulti` (137), the newest 15
  normal-world sessions per production seq, and sessions found by each skill's
  private game message. Leagues / Grid Master sessions are kept apart: their
  relics boost smelting, smithing, fletching and leather to 1-2 ticks.
- The index stores every player's seqs, so a seq hit is usually someone else at
  the GE. Only the recorder's own `update_stat_v2` and messages count.
- Per batch: the `resume_pausebutton com=skillmulti` tick (the pick), then the
  ticks of that skill's xp drops. Click ticks jitter by one.
- No recording: the wiki recipe infobox `ticks =` field (`?action=raw`),
  labelled unverified.
- `p_delay(n)` costs n+1 ticks.

## What the recordings show

| action | session | pick -> first | gap | n |
|---|---|---|---|---|
| cooking (fire/range) | 4503, 4079, 4461, 4500 | 2-3 (from panel open) | 3 then 4 | 250+ gaps |
| smithing (anvil) | 3449 | 3 | 5 | 9 |
| weaving, bolt of linen | 3449 | 3 (6 of 7; one 4) | 3 | 43/43 |
| spinning, linen yarn | 3449 (rev 235, Seers' wheel) | 2-3 | 2, seq `human_spinningwheel_60` | 93/93 |
| gem cutting | 4494 (+16 unboosted Leagues) | 1 | 2 | 16 of 20 first at 1 |
| herblore, finished | 4076, 4187, 4392, 4079 | 1 | 2 | 97 |
| herblore, unfinished | 3123 | 0-1 | 1 | 45 |
| ogre arrow shafts | 4503 | 3 | 3 | 1 |
| ogre headless / wolfbone tips | 4503 | 1-2 | 2 | 4 |
| construction (sailing parts) | 3443, 3449 | 3-4 | 4 | 43 |
| bow stringing (magic shortbow) | 4567 | 1 | 2 | 26/26 |
| glassblowing (orbs) | 4567 | 2 | 3 | 52/52 |
| battlestaves (fire, water) | 4567 | 2 | 2 | 39/39 |
| gold jewellery (bracelet, sapphire ring) | 4567 | 2 (click on 446) | 3 | 49/49 |
| pottery wheel (bowls) | 4567 | 2 | 3 | 12/12 |
| pottery oven (bowls, cups) | 4567 | 6 | 7 | 15/16 |
| darts | 4567 | 0 (on the use) | one use = 10 | 61 uses |
| amethyst dart tips | 4568 | 1 | 2 | 6/6 |
| bolt tipping (rune + dragonstone/onyx) | 4568 | 2 (seq at 1) | 2 | 25 sets |
| bolt feathering (adamant) | 4568 | 0 (on the use) | one use = 10 | 23 uses |
| Enchant Crossbow Bolt | 4568 | 0 (on the pick) | 2, then 3 | 25 sets |
| jewellery enchant, clicked | 4568 | 0 (on the click) | at most every 3 | 5 |
| jewellery enchant, carrying on by itself | 4568 | 6 after the last cast | 7 | 13 |
| smelting (steel, runite) | 4569 | 4 | 5 | 6 |
| bow string | 4569 | 3 (from the server's first answer) | 3 | 16/16 |
| green d'hide body | 4569 | 2 | 3 | 4/4 |
| bread dough | 4570 | 0 (on the pick) | 1, then 2 | 6 |
| bread on a range | 4570 | 2 | 3 then 4 | 6 |
| gem bolt tips (diamond, ruby) | 4571 | 1 | 5 | 18/19 |
| dragon bolt tipping | 4571 | 2 | 2 | 24 sets |
| Enchant Crossbow Bolt (dragon ruby/diamond) | 4571 | 0 | 2, then 3 | 23 sets |

Weaving, spinning and construction time the first item from the pick, a full
cycle later. Cooking times it from the panel opening. Gems and herblore resolve
the first one tick after the pick.

The 3449 spin panel reads "Linen yarn|Bow string" [6099], and the wheel plays
`loc_anim spinningwheel (466)` with synth 2590 on each item.

Index 4567 ("Skilling X make", 2026-11-16, rev 242, normal world) shows the
3-tick actions' first item at +2, not +3. Glass, battlestaves, jewellery and
the pottery wheel play the seq (and synth) on the pick and again on each item
tick but the last: the next item's start. Stringing plays the seq only with
each bow. Lines: "You add a string to the bow.", "You make an orb.", "You make
the clay into a bowl." / "a set of cups.", "You put a bowl in the oven." / "You
remove the bowl from the oven." (a cup is "an item" / "the item"), "You finish
making 10 darts.", all spam. Battlestaves and gold jewellery print nothing.
Battlestaves add the element's `battlestaff_<x>_crafting_spotanim` and
`attach_orb`; darts play `human_fletching_add_dart_feathers_<tier>`, no sound;
the oven plays `potteryoven_quick`. Panels: battlestaves "How many do you wish
to make?" mode 0; the oven "What would you like to fire in the oven?" mode 9;
the wheel lists Pot, Pie dish, Bowl, Plant pot, Set of cups, Pot lid.

## Fixed (2026-11-16)

| file | was | now | evidence |
|---|---|---|---|
| weaving.rs2 | item on the pick tick, then 3 | first at +3, then 3 | 3449 |
| spinning.rs2 | item on the pick tick, then 3, seq _90 | first at +3 / 3 (_90); linen +2 / 2 (_60) | 3449 (linen); wiki 3 for wool/bow string, unverified |
| spinning.rs2 | Bow string / Linen yarn | Linen yarn / Bow string | 3449 [6099] |
| uncut_gem.rs2 | item on the pick tick, then 2 | first at +1, then 2 | 4494 |
| ogre_arrows.rs2 (shafts) | 2 ticks | 3 ticks | 4503 + wiki |
| wine.rs2 | 2 ticks | 3 ticks | wiki only, unverified |
| bows.rs2, bows.dbrow | first on the pick, then 3; no seq; `string_bow` synth; make title | +1, then 2; `stringing_<bow>` per bow; no sound; "How many would you like to string?" | 4567 |
| glass.rs2 | first on the pick, then 3 | +2, then 3; seq + synth (delay 10) on the next item's start | 4567 |
| battlestaves.rs2 | one per use, instant, no seq | make-X panel; +2, then 2; seq, spotanim, `attach_orb` | 4567 |
| jewellery.rs2 (gold) | first on the click, then 3; "You make a ..." | +2, then 3; no line; stops quietly when the bars run out | 4567 |
| jewellery.rs2 (tiara, slayer ring) | first on the click | +2, then 3 | by analogy with gold, unverified |
| pottery.rs2 (wheel) | first on the pick, then 3; no Use op; cups and plant pot swapped | +2, then 3; Use opens the panel; recorded order and lines | 4567 |
| pottery.rs2 (oven) | 6-tick cycle, `human_furnace`, invented "hardens" line | 7-tick cycle, `potteryoven_quick`, recorded put/remove lines; oven title and mode 9 | 4567 |
| darts.rs2, darts.dbrow | no seq, `fletch` synth, plain message | tier seq, no sound, spam line | 4567 |

Index 4568 (2026-11-16): bolt tipping is a batch ("How many sets of 10 would
you like to tip?", mode 3), feathering one set per use; both say "You fletch 10
bolts." with `fletch` and the metal's `human_fletching_add_bolt_{tips,feathers}_*`.
Tips on an already tipped bolt: "Nothing interesting happens." [759]. Enchant
Crossbow Bolt opens "How many sets of bolts to enchant?" (mode 3) and casts each
set with `human_xbow_enchant_arrowtip`, `xbows_enchanting_tips_spot`,
`enchanted_tipping` and "The magic of the runes coaxes out the true nature of the
gem tips.". Jewellery enchant sits in a sub-list (`spellbook_sublist` 9730: the
Enchant Jewellery button sets it, the back button clears it); a sapphire ring
casts with `enchant_sapphire_ring` and no line; after the last click the spell
goes on through the rest of that item, 6 ticks after the cast then every 7.
Amethyst: mode 0, `human_amethystcutting`, `chisel`, "You craft some Amethyst
dart tips.". The Enchant Crossbow Bolt panel opens on the click's tick in 4 of 6 casts
([834], [848], [860], [888]); [827] and [925] are the proxy filing the click a
tick early, so ours (same tick) is right.

Index 4569: smelting shows only the bars the pack can make ("What would you like
to smelt?", or "How many would you like to smelt?" for one), mode 13; each bar is
"You smelt the iron in the furnace along with two heaps of coal." + seq + synth,
then 4 ticks later the bar, "You retrieve a bar of steel." and the seq again, the
next smelt the tick after. The flax panel is "What would you like to spin?"; bow
string prints nothing and the wheel plays `loc_anim spinningwheel` each cycle.
Leather: no sound; the batch stops without a line when the leather runs out.

Index 4570: the dough panel is unchanged; "You mix the water and flour to make
some bread dough." as spam; bread bakes on the cooking beat with "You
successfully bake some bread.".

## Fixed from 4568-4570 (2026-11-16)

| file | was | now |
|---|---|---|
| amethyst.rs2 | mode 12, first on the pick, then 3, no seq, "You cut the amethyst into ..." | mode 0, +1 then 2, seq + chisel, "You craft some Amethyst <kind>." (dart tips recorded) |
| bolts.rs2, bolts.dbrow | tipping one set per use, no seq; feathering no seq, "You attach feathers to ..."; a mesbox for the wrong bolt | tipping a mode-3 batch (seq +1, sets +2 then 2); feathering seq + "You fletch N bolts."; wrong bolt "Nothing interesting happens." |
| enchant_bolts.rs2 (new), magic_spells.dbrow, magic.constant | Enchant Crossbow Bolt did nothing | the spell: panel of held kinds (22: crossbow, dragon, ascension), one row per gem from the wiki (onyx/dragonstone runes match 4568) |
| enchant.rs2 | 2-tick cooldown, "You successfully enchant ...", no sound, no sub-list | 3-tick cooldown, no line, gem ring/amulet synth, the sub-list buttons, the carry-on (weak queue: scriptrun casts 26, 32, 39, 46; a new click cancels it) |
| smelting.rs2, smelting.dbrow | 4-tick cycle; all nine bars; "You place the ... into the furnace." | 5-tick cycle; only makeable bars and the recorded titles; the recorded lines (mithril/adamantite by pattern) |
| spinning.rs2 | a line per spin; no wheel anim; make title | no line; `loc_anim spinningwheel`; "What would you like to spin?" |
| leather.rs2 | first on the pick, then 3; stitching synth; a refusal when the leather runs out | +2 then 3; no sound; stops quietly |
| dough.rs2 | on the pick, then every 3 | on the pick, +1, then every 2 |
| bolts.rs2, skill_multi.rs2 | tipping cell "Onyx bolts" | "10 x Onyx bolts" (`~skill_multi1_makeable_named`) |

## Fixed from 4571 and by likeness (2026-11-16)

Index 4571: gem bolt tips are a batch, "How many sets of 12 will you make?"
(mode 2, the yield in the title), the gem's cutting seq + chisel on the pick,
the first tips the tick after, then a gem every 5 ticks with the seq again on
all but the last, each "You use your chisel to fletch small bolt tips.". Dragon
bolt tipping and enchanting match what 4568 set up ("10 x Ruby dragon bolts",
"Ruby dragon bolts (e)|Diamond dragon bolts (e)").

| file | was | now | evidence |
|---|---|---|---|
| bolts.rs2, bolts.dbrow | gem tips one gem per use | the 4571 batch; every gem row has its cutting seq | 4571 |
| snakeskin.rs2, leather.rs2 | first on the pick, then 3; stitching synth; no thread used; the two shields on the needle panel, thread required, steel nails only | leather's beat and silence, a reel every five (shared `~leather_thread_use`); snakeskin shield = snakeskin on a willow shield (hammer, 15 iron nails); broodoo = hammer on the mask, 2 snakeskins, 8 nails of any kind | wiki (Snakeskin_body .. boots: 3 ticks, needle + thread; Snakeskin_shield: hammer; Broodoo_shield) + leather (4569) |
| hammer.rs2 | | the three masks dispatch the broodoo shield | wiki |
| fabric_armour.rs2 | first on the pick, then 3; synth; refusals every pass; no thread used | leather's beat, silence and thread | likeness to leather (4569), unverified |
| jewellery.rs2 (silver) | first on the click, then 3; a line each | gold's beat and silence | likeness to gold (4567), unverified |
| cooking_generic.dbrow | bread got the generic line | "You successfully bake some bread." |

## Open

| action | ours | evidence | note |
|---|---|---|---|
| silver jewellery quantity | each interface-6 click makes one | (gold's 446: one click made 27) | same as gold |
| broodoo nail order | cheapest kind held | none | which kind Jagex takes first |
| wooden shields | p_delay(7) = 8 | wiki 7 | the comment says 7 |
| gold jewellery quantity | each 446 click makes one | 4567: one click made 27 (`makexcrafting` 2224 = 27) | port the 446 quantity |
| furnace Smelt (op2) with gold bars and moulds | the bar panel | 4567 [594]: opened crafting_gold | |
| pottery panel cell names | "Unfired pot" ... | 4567: "Pot" ... "Set of cups"; the oven says "Cup" | needs named cells |

These are already right: cooking, anvil smithing, herblore, log cutting (3),
headless arrows and arrowtips (2), amulet stringing (2, index 4185).
