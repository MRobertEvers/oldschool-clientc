# The Feud (and Rogue Trader, the Hosidius fruit stall) from RSProx

Recordings (rev 241, 2026-11-01, recorder LoveTori, one account across all four):

| index | session | covers |
|---|---|---|
| 4448 | "30 Thieving and The Feud part 1" | Hosidius fruit stall x120 steals; Varrock tablet, Kharedst's memoirs, GE, bank, glory; Ali Morrisane, the start; Shantay, the jail, the doorway; the carpet; stages 1-6 |
| 4452 | "The Feud Part 2" | the first trial (stages 6-13): pockets, the urchin, lure, knock-out; the disguise; the stake-out (14) |
| 4451 | "The Feud Part 3" | the burglary and the safe (14-16), the traitor and the poison (16-23), both fights (24-26) |
| 4453 | "Rogue Trader Aubury" | the villager and the Mayor (26-27), the completion (28), Rogue Trader to the rune sudoku |

Tooling used (scratchpad, not committed): `rsprox_pages.py` -> a page-to-RuneScript converter
(every page is pasted, not retyped, `<displayname>` substituted); a per-npc conversation
splitter keyed on the clicked npc and Jagex's stage; `revcheck.py`, which fails any
chat/mesbox/mes string in our files that no recording contains (0 on every new file).

## What was there before

`quest_thefeud/` was a wiki-and-guesswork port: invented dialogue ("the gangs were fighting over
nothing but a stolen camel"), an invented stage machine 1..28 whose numbers matched Jagex's only
by accident, the quest npcs spawned as multinpc RUNGS (`feud_egyptian_minder`, `feud_hag`, ...)
instead of the shells Jagex adds, so ops dispatched on the wrong names and no post-quest swap
could happen. Every file is replaced.

## Stages (feud_var, varbit 334)

| value | written by | session [tick] |
|---|---|---|
| 1 | Ali Morrisane, "Yes." | 4448 [1076] |
| 2 | Drunken Ali, third beer | 4448 [2774] |
| 3 | the second gang asked | 4448 [2953] |
| 4 | 1,000 coins to the Camel Man | 4448 [3037] |
| 5 | the second receipt | 4448 [3249] |
| 6 | joined (Ali the Operator) | 4448 [3377] |
| 7 | first pocket | 4452 [45] |
| 8 | the urchin's distraction | 4452 [236] |
| 9 | second pocket (the mark) | 4452 [246] |
| 10 | Lure | 4452 [358] |
| 11 | Knock-Out | 4452 [515] |
| 12 | third pocket (knocked out) | 4452 [520] |
| 13 | the burglary set, the keys | 4452 [587] |
| 14 | the stake-out | 4452 [1005] |
| 15 | the jewels out of the safe | 4451 [365] |
| 16 | the jewels to the Operator | 4451 [586] |
| 17 | Traitorous Ali named | 4451 [752] |
| 18 | the snake to the Hag | 4451 [1456] |
| 22 | the poison (19-21 never written) | 4451 [2517] |
| 23 | the beer poisoned | 4451 [2579] |
| 24 | the Operator sends you to the boss | 4451 [2811] |
| 25 | the Tough Guy dead | 4451 [3084] |
| 26 | the Bandit champion dead | 4451 [3347] |
| 27 | Ali the Mayor | 4453 [189] |
| 28 | Ali Morrisane's last page | 4453 [373] |

Sub-varbits written on their recorded pages: feud_var_drink 318, talk_gangs 315 / comp_gangs 316
(Menaphite 2 + bandit 1), npc_multi 340 (0 -> 1 -> 2 at the first two pockets), closest_villager
329 (the urchin's mark, the villager type), urchin_again 332, var_urchin 319, sapp_bit 327 (the
knock-out), mayorsdoor 320, safe_digit 325 / safe_correct 326 (an ERROR flag, 1 from the first wrong
digit), given_jewels 339, found_trait 342, drink_found 321, hag_list 328, used_sauce 337, boss_vis
335/336, menaboss 322, banditboss 323, talk_villager 343, mayor_invis 344, mayor_multivar 14604,
metwitch 345, witchbothered 324, ali_money 331.

**Transmit.** `varp436_feud_var_multi` (most of the above) and Rogue Trader's `varp540`,
`varp541`, `varp2980..2990` were `temp, transmit=no` in the generated block: never sent, lost at
logout (varp_transmit_audit.py: 74 recorded writes in 4 sessions for 436 alone). Now stated in
`quest_thefeud/configs/thefeud.varp` and `roguetrader.varp` (perm/temp, transmit=yes); the generated
blocks are deleted.

## Action inventory (rsprox_actions.py), verdicts

### The Feud's own (all built from the recording)

| action | verdict |
|---|---|
| feud_ali_m op1/op3 | feud_alimorrisane.rs2; Trade says "Despite your best efforts..." from stage 1 (silent at 0, [1014]) |
| Drunken Ali op1, beer (and 8 other items) | feud_town.rs2: three pages by beers drunk; beer gone + viking_tankard_drink delay 5 on the op tick; any other item "Eh? What's this?..." |
| Barman op1/op3, bandits, Menaphite thugs (op1, op2), coward bandit | feud_town.rs2 / feud_gangs.rs2; thugs can't be attacked while you're one of them ("You can't do that right now.", [2410]) |
| Camel Man op1/op3/item, the two camels op3 | full menu tree incl. the haggle (1,000 taken; free/200 refused; 500 [unverified]); camels' three refusals by roll |
| Ali the Operator | every stage 1..27 recorded branch |
| villagers op1 (nine greetings by roll), op3/op4/op5 | feud_lessons.rs2: the quest's lesson, NOT blackjacking (owner, 2026-11-02); blackjack.rs2 no longer binds villagers |
| street urchin | info menu, the hire with both overheads ("Hey you, camel features! Come here." / "Excuse me?"), paid/refused/double branches |
| kebab seller, sauce, kebabs | 5/3-option menus (stage >= 17 the short one, boundary [unverified]); sauce free, refused while held |
| Hag, snake charmer, money pot, desert snake, flute | Hag per stage; pot (ratcatchers.rs2, its feud branch rewritten: a coin each time, kit / lost basket / lost flute / "I'm not stopping you!"); charm shape C, C+10, C+13 |
| troughs, dung (41 clicks), bucket | sauce -> camel eats -> dung 22 ticks later on the camel's tile; five refusal lines by roll; the scoop shape |
| beer table | "...don't want their germs!"; the poison sets boss_vis and the stage (vial kept, as recorded) |
| cactus, mayor's doors (+keys), desk, bed, notes, picture, safe | feud_heist.rs2: forced walk + overhead + feud_crawling_ready; walk-through double door (inviswall + open leaves 2 ticks, door_open); notes on the box's continue; safe on soft timers (a queue or normal timer waits for the open modal) |
| Menaphite Leader, Tough Guy, Bandit Leader, champion | feud_confrontation.rs2: spawns on the last page's click with the hint arrow, drops (willow blackjack, adamant scimitar) private 200 ticks on the tick after the kill, the leaders' after-pages |
| Mayor, dye lady, market seller | feud_town.rs2 |
| disguise (beard on turban) | feud_heist.rs2 |
| journal (26 recorded states) | feud_journal.rs2, generated from the recorded rows; stages 8, 10, 11, 19-22, 26, 28 [unverified] |
| quest list overview at stage 0 | quest_overview_start.rs2: "talking to Ali Morrisane at his stall in Al Kharid." |

### Generic content the sessions exercise

| action | verdict |
|---|---|
| Hosidius fruit stall (401 clicks, 120 steals) | ported by an agent: docs/rsprox-analysis/thieving_fruit_stall.md, test/thieving/fruit_stall.lua (2/2) |
| Rug Merchant talk + travel (4448 [1224..1504]) | magic_carpet_talk.rs2: recorded greeting ("traveler"), the shrinking second menu ("I want to travel by magic carpet." ... "Thanks, I'm done here."), the Feud-time Ali branch, neutral moods; Shantay station's one-page offer, "either", the fare's two pages. Other stations' answers stay wiki [unverified] |
| Shantay (talk, Trade, Buy-pass, jail) | shantay.rs2: Irena's page (gated on Tourist Trap, [unverified] gate), "Shantay pass" capital, Trade opens the recorded stock (new shantay_pass_shop.inv), **[opnpc4,shantay] did not exist** (now: box + 5 coins + pass on the op tick); the members gates removed (map_members is 0 on every world: the shop and the pass never worked) |
| Shantay doorway into the desert | shantay_pass.rs2: **members gate removed (refused everyone)**; the 2004 poster/dialogue/disclaimer replaced by cws_warning_10 (565) and "The guard takes your Shantay Pass as you go through the gate." + two walked steps |
| shop Value lines | shop.rs2: "...: currently costs 1 coin." / "N coins." with commas (was "Ngp."), same for "shop will buy for" (100+ recorded opens) |
| banknote merchant, Aristarchus | new areas/area_desert/scripts/pollnivneach.rs2 (no script existed) |
| Ali's Discount Wares, Asp & Snake bar | the generated shop bindings moved to the shells/quest (gen_shop_scripts.py would undo: re-apply) |
| Varrock tablet, Kharedst's memoirs, glory, GE, bank, bank doors, oak tree, Hosidius door, papaya/apple basket, eating fruit/kebab, vodka, stamina, desert clothes, run, prayer flicks | not diffed this session (Open) |
| crate ("I can't reach that!"), broken ladder ("The ladder is broken, I can't climb it."), cupboards, bath/cage examine | engine/generic, lines present |

### Teleports

| tick | cause | verdict |
|---|---|---|
| 4448 [52], [785]; 4453 [549] | Varrock tablet | generic (teleport_tablet.rs2), not re-diffed |
| 4448 [149] | Kharedst's memoirs submenu | generic, not re-diffed |
| 4448 [976]; 4453 [264], [635], [726] | glory (worn) to Al Kharid | generic, not re-diffed |
| 4448 [1454..1504]; 4453 [883..933] | magic carpet | existing ride; landing pad matched by the test |
| 4451 [57]..[518] | mayor's stairs | maplink.dbrow rows land on the recorded tiles |

## Spawns

Pollnivneach's quest npcs spawn as the shells the wire adds (`id=` on every recorded add):
m52_46.spawn rewritten for 10 npcs, and the same corrections added to tools/gen_spawns.py's
NPC_SPAWN_ID_CORRECTIONS so the next generation keeps them. Which wandering camel is
`feud_camel2_multi` is not decided by any recording.

## Wire audit (spot-checked, scriptrun tick log)

| section | recorded | ours | verdict |
|---|---|---|---|
| knock-out | human_blunt_pound, human_unconscious d30, cudgel; "Zzzzzz" +1; wake +12 human_ready "Ouch, my head." (4452 [514..526]) | identical, 355/356/367 | matched |
| urchin hire | both overheads on the stage tick | 297 | matched |
| pocket | human_pickpocket + pick at C+3 | 271, 303 | matched (C offset not re-measured) |
| knocked-out pocket | human_pickupfloor + pick C+1 | 365 | matched |
| completion | page, then stage + line + scroll, xp after the scroll (4453 [372..373]) | same order | matched |

## Test

test/quests/thefeud.lua follows the recorded order with Jagex's stage names. scriptrun
(private packs, `src/build_fruit_opt/torirsserver`, `TORIRSSERVER_DEV=1`): 266 of 267 rows pass;
the one failure is `quest.journal`, which fails on every quest under scriptrun (no clientscripts).

## Open

- The safe once clanked on a seventh press, not the sixth (4451 [415]); ours decides at six.
- Villager stun damage: the dbrow says 1, the recording hit 2 (4452 [55]).
- A 2-damage hit on the player 2 ticks after the test's knock-out (tick log 357), cause not traced.
- Our generic pickpocket (`~pick_pocket`) is the 2004 shape: "You attempt to pick..." and success
  at C+1; the recording has no attempt line and succeeds at C+3. Only the villagers (quest) match.
- Flute play outside Rat Catchers: "I just can't hit that high note." (4448 [1984]) not built.
- "I don't really need another one of these." on picking up a held flute/sauce/vial not built.
- Eating strange fruit ("You eat the fruit." / "It tastes great, some of your energy is
  restored!"), the extra-hot kebab, drinking vodka: not checked.
- Unrecorded branches written as nothing (no invented lines): the 500-coin camel offer, "Perhaps I
  could help.", "Ok I'll see what I can do.", the paid urchin, the Thieving <30 start, the drink
  option at the bar (opens the shop), stage gaps listed as [unverified] in the scripts.
- The fights: npc levels/combat blocks are the existing thefeud.npc (wiki, matched to the cache);
  no per-attack damage fit or hit-cause audit was run (regular quest monsters, not bosses).
- Rogue Trader (roguetrader.rs2, aubury.rs2, by an agent): the sudoku board packing reproduces all
  11 initial and 53 click varp writes of 4453 offline; not run on scriptrun. Its open list:
  "Buy all the runes for N" does nothing, the 4x4 board is never rolled, a wrong grid does nothing,
  runes value 3 inferred, casket shop prices unrecorded (see roguetrader.rs2's [unverified]).
- Journal: stage 28's completed form and the stages listed above are unverified; not added to
  test/quests/_journals.lua.
- Generic teleports and the bank/GE in 4448 not re-diffed this session.
