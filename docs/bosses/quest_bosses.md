# Quest combat and quest-boss implementation plan

Last audited: **2026-08-17** against the live Old School RuneScape Wiki and
this repository's revision-239 cache/content tree.

Status: **plan / acceptance contract**. An unchecked row is not implemented
merely because an NPC spawns, the generic melee handler can damage it, or a
debug command can advance the quest.

## 1. Scope, definitions, and sources

This file deliberately uses a broader scope than the Wiki's `Boss` article.
The requested first inventory is every quest or independently tracked quest
subquest whose Wiki `Enemies` field contains a combat encounter. It therefore
includes required bosses, required ordinary kills, conditional branch fights,
optional fights, companion/pet fights, and fights which may be bypassed by
bringing an item. Each distinction is retained below. It does **not** turn an
avoidable aggressive monster on a travel route into a quest encounter unless
the Wiki lists it in that quest's `Enemies` field.

The implementation queue then treats each distinct staged fight as an
encounter. "Encounter-specific items" means items consumed, required, checked,
granted, dropped, reclaimed, or restricted by the fight. It does not mean that
every possible food, potion, weapon, or armour choice becomes quest content.
"Loot" includes ordinary drops, guaranteed quest drops, quest-state credit,
post-fight searchable/choppable objects, item-retrieval/death-bank contents,
and the explicit absence of loot for summoned or instanced actors.

Authoritative catalogue sources, pinned for reproducibility:

| Source | Pinned revision | What it establishes |
| --- | ---: | --- |
| [Quests/Requirements by quest](https://oldschool.runescape.wiki/w/Quests/Requirements_by_quest?oldid=15281241) | 15281241, 2026-07-29 | The full quest roster and each quest's rendered `Enemies` field; source of section 2 |
| [Boss](https://oldschool.runescape.wiki/w/Boss?oldid=15301140#Quest_bosses) | 15301140, 2026-08-14 | The Wiki's narrower quest-boss and multi-boss lists |
| [Quests](https://oldschool.runescape.wiki/w/Quests?oldid=15290013) | 15290013, 2026-08-07 | Current quest catalogue and quest/subquest identity |
| [Monster](https://oldschool.runescape.wiki/w/Monster?oldid=15264774) | 15264774, 2026-07-16 | General monster, combat, death, and drop terminology |
| [Item Retrieval Service](https://oldschool.runescape.wiki/w/Item_Retrieval_Service?oldid=15124878) | 15124878, 2026-02-10 | Unsafe-instance death-bank behavior where an encounter uses one |
| [Nightmare Zone](https://oldschool.runescape.wiki/w/Nightmare_Zone?oldid=15271187) | 15271187, 2026-07-21 | Replay eligibility and altered Nightmare Zone variants; not a substitute for the quest fight |

Every encounter implementation must additionally pin and cite the current
revision of its quest article, quick guide, transcript, every NPC page, and
every encounter-specific item page. The article/guide defines route and
mechanics, the transcript defines dialogue/re-talk/reclaim behavior, the NPC
page defines stats/attacks/immunities/drops, and the revision-239 cache defines
symbolic NPC/loc/obj/sequence/spotanim/projectile/sound names. Quest Helper may
be used for state transitions and tests, but does not override the Wiki or
cache.

### Version boundary

The live Wiki catalogue is ahead of the repository's `osrs239` cache. The
Sailing-era rows in section 2 remain in scope, but must be marked
`blocked-cache-version` until the exact NPCs, items, maps, interfaces,
animations, graphics, sounds, varbits, and quest dbrows are present in an
accepted cache. Do not approximate those encounters with unrelated revision-239
assets. In particular this applies to **Death on the Isle, The Blood Moon
Rises, The Final Dawn, Prying Times, Troubled Tortugans, The Red Reef, Shadows
of Custodia, Scrambled!, Learning the Ropes, The Ides of Milk, Fallen From
Grace**, and any prerequisite content introduced after the target cache.

## 2. Combat-bearing quest inventory

This is the requested first list. Text in the encounter column is normalized
from the pinned Wiki page's rendered `Enemies` field. `Optional`, `conditional`,
`avoidable`, branch, companion, and item-bypass language is part of the
contract, not editorial advice. Recipe for Disaster appears once as the
aggregate quest and again for each combat-bearing independently completed
subquest; implementation and tests use the subquest rows and then an aggregate
finale test.

| Quest or subquest | Wiki-listed combat encounter(s) |
| --- | --- |
| [Demon Slayer](https://oldschool.runescape.wiki/w/Demon_Slayer) | Delrith (27) |
| [Shield of Arrav](https://oldschool.runescape.wiki/w/Shield_of_Arrav) | Weaponsmaster (23) on the Black Arm route; Jonny the Beard (2) on the Phoenix route |
| [The Restless Ghost](https://oldschool.runescape.wiki/w/The_Restless_Ghost) | Skeleton (13), avoidable |
| [Vampyre Slayer](https://oldschool.runescape.wiki/w/Vampyre_Slayer) | Count Draynor (34) |
| [Imp Catcher](https://oldschool.runescape.wiki/w/Imp_Catcher) | Imps (2), optional except for self-sufficient accounts |
| [Prince Ali Rescue](https://oldschool.runescape.wiki/w/Prince_Ali_Rescue) | Jail guard (26), optional |
| [Witch's Potion](https://oldschool.runescape.wiki/w/Witch%27s_Potion) | Rat (1) |
| [Pirate's Treasure](https://oldschool.runescape.wiki/w/Pirate%27s_Treasure) | Gardener (4), optional |
| [Dragon Slayer I](https://oldschool.runescape.wiki/w/Dragon_Slayer_I) | Key-dropping zombie rat (3), ghost (19), skeleton (22), zombie (24); Melzar the Mad (43); lesser demon (82); optional Wormbrain (2); Elvarg (83) |
| [Druidic Ritual](https://oldschool.runescape.wiki/w/Druidic_Ritual) | Suit of armour (19), avoidable |
| [Lost City](https://oldschool.runescape.wiki/w/Lost_City) | Tree spirit (101); Entrana zombies (25) when obtaining a bronze axe |
| [Witch's House](https://oldschool.runescape.wiki/w/Witch%27s_House) | Witch's experiment forms (19, 30, 42, 53) |
| [Merlin's Crystal](https://oldschool.runescape.wiki/w/Merlin%27s_Crystal) | Sir Mordred (39); giant bat (27), optional for bat bones |
| [Heroes' Quest](https://oldschool.runescape.wiki/w/Heroes%27_Quest) | Ice Queen (111) unless ice gloves are already owned; Entrana firebird (2); optional jailer (47); Grip (22), Phoenix route only |
| [Scorpion Catcher](https://oldschool.runescape.wiki/w/Scorpion_Catcher) | Jailer (47), conditional on obtaining rather than buying a dusty key |
| [Family Crest](https://oldschool.runescape.wiki/w/Family_Crest) | Chronozon (170) |
| [Temple of Ikov](https://oldschool.runescape.wiki/w/Temple_of_Ikov) | Fire Warrior of Lesarkus (84); Guardians of Armadyl (43) when siding with Lucien; Lucien (14) when siding with Armadyl |
| [Holy Grail](https://oldschool.runescape.wiki/w/Holy_Grail) | Black Knight Titan (120) |
| [Tree Gnome Village](https://oldschool.runescape.wiki/w/Tree_Gnome_Village) | Khazard warlord (112) |
| [Fight Arena](https://oldschool.runescape.wiki/w/Fight_Arena) | Khazard scorpion (44), Khazard ogre (63), Bouncer (137), optional General Khazard (142) |
| [Hazeel Cult](https://oldschool.runescape.wiki/w/Hazeel_Cult) | Alomone (13), Ceril route only |
| [The Grand Tree](https://oldschool.runescape.wiki/w/The_Grand_Tree) | Black demon (172) |
| [Underground Pass](https://oldschool.runescape.wiki/w/Underground_Pass) | Doomion, Othainian, Holthion (91); three paladins (62); Kalrag (89); Disciple of Iban (13) |
| [Observatory Quest](https://oldschool.runescape.wiki/w/Observatory_Quest) | Goblin guard (42), optional/avoidable |
| [The Tourist Trap](https://oldschool.runescape.wiki/w/The_Tourist_Trap) | Mercenary Captain (47) |
| [Watchtower](https://oldschool.runescape.wiki/w/Watchtower) | Gorad (68); giant bat (27) only when bat bones are needed |
| [Legends' Quest](https://oldschool.runescape.wiki/w/Legends%27_Quest) | Ranalph Devere (92) twice, Irvig Senay (100) twice, San Tojalon (106) twice, Nezikchened (187) three times |
| [Big Chompy Bird Hunting](https://oldschool.runescape.wiki/w/Big_Chompy_Bird_Hunting) | Chompy bird (6); optional wolves (64) |
| [Elemental Workshop I](https://oldschool.runescape.wiki/w/Elemental_Workshop_I) | Earth elemental (35) |
| [Nature Spirit](https://oldschool.runescape.wiki/w/Nature_Spirit) | Three ghasts (30) |
| [Priest in Peril](https://oldschool.runescape.wiki/w/Priest_in_Peril) | Temple guardian dog (30, Magic-immune); Monk of Zamorak (30) |
| [Regicide](https://oldschool.runescape.wiki/w/Regicide) | Tyras guard (110) |
| [Tai Bwo Wannai Trio](https://oldschool.runescape.wiki/w/Tai_Bwo_Wannai_Trio) | Monkey (3) |
| [Troll Stronghold](https://oldschool.runescape.wiki/w/Troll_Stronghold) | Dad (101), Troll General (113), conditional Berry and Twig (71) |
| [Shades of Mort'ton](https://oldschool.runescape.wiki/w/Shades_of_Mort%27ton) | Five Loar shades (40) |
| [The Fremennik Trials](https://oldschool.runescape.wiki/w/The_Fremennik_Trials) | First three Koschei forms; Draugen (69); optional repeated Lanzig, Borrokar, Lensa, or Freidir (48) |
| [Horror from the Deep](https://oldschool.runescape.wiki/w/Horror_from_the_Deep) | Dagannoth (100), then Dagannoth Mother (100) |
| [Monkey Madness I](https://oldschool.runescape.wiki/w/Monkey_Madness_I) | Jungle Demon (195) |
| [Haunted Mine](https://oldschool.runescape.wiki/w/Haunted_Mine) | Treus Dayth (95) |
| [Troll Romance](https://oldschool.runescape.wiki/w/Troll_Romance) | Arrg (113); unsafe death |
| [In Search of the Myreque](https://oldschool.runescape.wiki/w/In_Search_of_the_Myreque) | Skeleton Hellhound (97) |
| [Creature of Fenkenstrain](https://oldschool.runescape.wiki/w/Creature_of_Fenkenstrain) | Experiment (51) |
| [Roving Elves](https://oldschool.runescape.wiki/w/Roving_Elves) | Moss Guardian (84), fought with equipment/Prayer restrictions |
| [Ghosts Ahoy](https://oldschool.runescape.wiki/w/Ghosts_Ahoy) | Giant lobster (32) |
| [One Small Favour](https://oldschool.runescape.wiki/w/One_Small_Favour) | Slagilith (92); dwarf gang members (44/48/49) in multicombat |
| [Mountain Daughter](https://oldschool.runescape.wiki/w/Mountain_Daughter) | The Kendal (70) |
| [Between a Rock...](https://oldschool.runescape.wiki/w/Between_a_Rock...) | Scorpions (14); Arzinian Being of Bordanzan/Avatar (75–125) |
| [The Feud](https://oldschool.runescape.wiki/w/The_Feud) | Bandit champion (70), Tough Guy (75) |
| [Desert Treasure I](https://oldschool.runescape.wiki/w/Desert_Treasure_I) | Dessous (139); five ice trolls (120–124); Kamil (154); Fareed (167); Damis forms (103/174); hostile route populations |
| [Icthlarin's Little Helper](https://oldschool.runescape.wiki/w/Icthlarin%27s_Little_Helper) | Possessed Priest (91); one stat/jar-selected apparition: Apmeken (75), Crondis (75), Scabaras (75), or Het (81) |
| [Recruitment Drive](https://oldschool.runescape.wiki/w/Recruitment_Drive) | Sir Leye (20) |
| [Mourning's End Part I](https://oldschool.runescape.wiki/w/Mourning%27s_End_Part_I) | Mourner (11) with combat-stat reduction to 20, including Hitpoints |
| [Wanted!](https://oldschool.runescape.wiki/w/Wanted%21) | Black Knight (32); scripted Solus Dellagar encounter |
| [Rum Deal](https://oldschool.runescape.wiki/w/Rum_Deal) | Evil spirit (150); fever spider (49) |
| [Shadow of the Storm](https://oldschool.runescape.wiki/w/Shadow_of_the_Storm) | Agrith-Naar (100) |
| [Ratcatchers](https://oldschool.runescape.wiki/w/Ratcatchers) | Player's cat versus King rat |
| [Spirits of the Elid](https://oldschool.runescape.wiki/w/Spirits_of_the_Elid) | Black, grey, and white golems (75 each) |
| [Devious Minds](https://oldschool.runescape.wiki/w/Devious_Minds) | Abyssal creatures (41+), conditional if a large pouch is obtained elsewhere |
| [Cabin Fever](https://oldschool.runescape.wiki/w/Cabin_Fever) | Pirates (57) during ship combat |
| [Fairytale I - Growing Pains](https://oldschool.runescape.wiki/w/Fairytale_I_-_Growing_Pains) | Tanglefoot (111) |
| [Recipe for Disaster](https://oldschool.runescape.wiki/w/Recipe_for_Disaster) | Aggregate of the five combat-bearing freeing subquests and six-boss Culinaromancer finale listed separately below |
| [In Aid of the Myreque](https://oldschool.runescape.wiki/w/In_Aid_of_the_Myreque) | Gadderanks (35), two Vampyre Juvinates (54), then two level-75 or four level-50 Juvinates by route |
| [A Soul's Bane](https://oldschool.runescape.wiki/w/A_Soul%27s_Bane) | 7–8 angry animals; five fear reapers (42); six unkillable confusion beasts (43); five hopeless creatures (40); three Tolna parts (46) |
| [Rag and Bone Man I](https://oldschool.runescape.wiki/w/Rag_and_Bone_Man_I) | Eight species-specific bone sources listed by the quest guide |
| [Swan Song](https://oldschool.runescape.wiki/w/Swan_Song) | Eleven Sea trolls (65/79/87/101); Sea Troll Queen (170) |
| [Royal Trouble](https://oldschool.runescape.wiki/w/Royal_Trouble) | Giant Sea Snake (149) |
| [Death to the Dorgeshuun](https://oldschool.runescape.wiki/w/Death_to_the_Dorgeshuun) | Sigmund (50); three optional H.A.M. guards (22) |
| [Fairytale II - Cure a Queen](https://oldschool.runescape.wiki/w/Fairytale_II_-_Cure_a_Queen) | Goraks (145); protection prayers ineffective |
| [Lunar Diplomacy](https://oldschool.runescape.wiki/w/Lunar_Diplomacy) | Suqah (111); Me (79) |
| [The Eyes of Glouphrie](https://oldschool.runescape.wiki/w/The_Eyes_of_Glouphrie) | Six one-Hitpoint Evil Creatures |
| [The Slug Menace](https://oldschool.runescape.wiki/w/The_Slug_Menace) | Slug Prince (62) |
| [Elemental Workshop II](https://oldschool.runescape.wiki/w/Elemental_Workshop_II) | Two earth elementals (35), conditional if bars are not brought |
| [My Arm's Big Adventure](https://oldschool.runescape.wiki/w/My_Arm%27s_Big_Adventure) | Baby Roc (75), Giant Roc (172) |
| [Eagles' Peak](https://oldschool.runescape.wiki/w/Eagles%27_Peak) | Kebbit (13), optional |
| [Contact!](https://oldschool.runescape.wiki/w/Contact%21) | Giant Scarab (191); summoned Locust Rider (68) and Scarab Mages (66/119) |
| [Cold War](https://oldschool.runescape.wiki/w/Cold_War) | One to three Icelords (51) |
| [The Fremennik Isles](https://oldschool.runescape.wiki/w/The_Fremennik_Isles) | Ten ice trolls (74–82); Ice Troll King (122) |
| [The Great Brain Robbery](https://oldschool.runescape.wiki/w/The_Great_Brain_Robbery) | Barrelchest (190); four Sorebones (57) |
| [What Lies Below](https://oldschool.runescape.wiki/w/What_Lies_Below) | Five outlaws (32); scripted King Roald fight (47) |
| [Olaf's Quest](https://oldschool.runescape.wiki/w/Olaf%27s_Quest) | Skeleton Fremennik (40); Ulfric (100) |
| [Another Slice of H.A.M.](https://oldschool.runescape.wiki/w/Another_Slice_of_H.A.M.) | H.A.M. archer (30), H.A.M. mage (30), Sigmund (64) |
| [Dream Mentor](https://oldschool.runescape.wiki/w/Dream_Mentor) | The Inadequacy (343), Everlasting (223), Untouchable (274), Illusive (108) |
| [Grim Tales](https://oldschool.runescape.wiki/w/Grim_Tales) | Glod (138) |
| [Shilo Village](https://oldschool.runescape.wiki/w/Shilo_Village) | Nazastarool forms (91/68/93) |
| [Biohazard](https://oldschool.runescape.wiki/w/Biohazard) | Mourner (13) |
| [Rag and Bone Man II](https://oldschool.runescape.wiki/w/Rag_and_Bone_Man_II) | Twenty-seven species-specific bone sources listed by the quest guide |
| [Land of the Goblins](https://oldschool.runescape.wiki/w/Land_of_the_Goblins) | Snothead (32), Snailfeet (56), Mosschin (88), Redeyes (121), Strongbones (184) |
| [While Guthix Sleeps](https://oldschool.runescape.wiki/w/While_Guthix_Sleeps) | Two assassins (167), two mercenary axemen (131), mercenary mage (112), three elite Black Knights (138), Surok Magis (265), Balance Elemental (524), two Tormented Demons (450) |
| [Zogre Flesh Eaters](https://oldschool.runescape.wiki/w/Zogre_Flesh_Eaters) | Slash Bash (111); zombie (39) |
| [Monkey Madness II](https://oldschool.runescape.wiki/w/Monkey_Madness_II) | Kruk (207), Keef (178), Kob (185), tortured gorillas (141/142), demonic gorillas (275), Glough (431) |
| [Dragon Slayer II](https://oldschool.runescape.wiki/w/Dragon_Slayer_II) | Vorkath (392), Spawn (100), Robert the Strong (224), dragon gauntlet from green through rune (79–380), Galvek (608) |
| [The Depths of Despair](https://oldschool.runescape.wiki/w/The_Depths_of_Despair) | Sand Snake (36) |
| [RFD: Freeing the Mountain Dwarf](https://oldschool.runescape.wiki/w/Recipe_for_Disaster/Freeing_the_Mountain_Dwarf) | Icefiend (13), conditional if ice gloves are absent |
| [RFD: Freeing Pirate Pete](https://oldschool.runescape.wiki/w/Recipe_for_Disaster/Freeing_Pirate_Pete) | Five Mudskippers (30/31); crab (21/23) |
| [RFD: Freeing Sir Amik Varze](https://oldschool.runescape.wiki/w/Recipe_for_Disaster/Freeing_Sir_Amik_Varze) | Stat-scaled Evil Chicken; black dragon (227) |
| [RFD: Freeing King Awowogei](https://oldschool.runescape.wiki/w/Recipe_for_Disaster/Freeing_King_Awowogei) | Big Snake (84); conditional zombie monkey (82/129), monkey guard (167), monkey archer/ninja (86) for greegrees |
| [RFD: Freeing Skrach Uglogwee](https://oldschool.runescape.wiki/w/Recipe_for_Disaster/Freeing_Skrach_Uglogwee) | Jubbly bird (11) |
| [The Corsair Curse](https://oldschool.runescape.wiki/w/The_Corsair_Curse) | Ithoi the Navigator (35) |
| [A Taste of Hope](https://oldschool.runescape.wiki/w/A_Taste_of_Hope) | Abomination (149), four Vyrewatch (87), Ranis Drakan (233; melee/flail restriction) |
| [Tale of the Righteous](https://oldschool.runescape.wiki/w/Tale_of_the_Righteous) | Corrupt Lizardman (46) |
| [Making Friends with My Arm](https://oldschool.runescape.wiki/w/Making_Friends_with_My_Arm) | Don't Know What (163), Mother (198) |
| [RFD: Defeating the Culinaromancer](https://oldschool.runescape.wiki/w/Recipe_for_Disaster/Defeating_the_Culinaromancer) | Agrith-Na-Na (146), Flambeed (149), Karamel (136), Dessourt (121), Gelatinnoth Mother (130), Culinaromancer (75) |
| [Song of the Elves](https://oldschool.runescape.wiki/w/Song_of_the_Elves) | Twenty-one mourners (24/51/106), paladins (49/118), Knights of Ardougne (27/53), Iorwerth archers/warriors (90/108), Arianwyn (212), Essyllt (236), Fragment of Seren (494) |
| [The Ascent of Arceuus](https://oldschool.runescape.wiki/w/The_Ascent_of_Arceuus) | Five Tormented Souls (16); Trapped Soul (30) |
| [The Fremennik Exiles](https://oldschool.runescape.wiki/w/The_Fremennik_Exiles) | Basilisk youngling (53), basilisk (61), monstrous basilisk (135), Typhor (218), Jormungand (363) |
| [The Curse of Arrav](https://oldschool.runescape.wiki/w/The_Curse_of_Arrav) | Golem guard (141), Arrav (339) |
| [Defender of Varrock](https://oldschool.runescape.wiki/w/Defender_of_Varrock) | At least six armoured zombies (85); chaos golems (70) |
| [Sins of the Father](https://oldschool.runescape.wiki/w/Sins_of_the_Father) | Kroy (133), nail beasts (67/143), Vampyre Juvinates (119), mutated bloodveld (123), Damien Leucurte (204), Vampyre Juvenile (122), Vanstrom Klause (459) |
| [A Kingdom Divided](https://oldschool.runescape.wiki/w/A_Kingdom_Divided) | Judge of Yama (168), two assassins (132), Lizardman brute (75), Xamphur (239), Barbarian Warlord (91) |
| [A Porcine of Interest](https://oldschool.runescape.wiki/w/A_Porcine_of_Interest) | Sourhog (37) |
| [Getting Ahead](https://oldschool.runescape.wiki/w/Getting_Ahead) | Headless Beast (82) |
| [Below Ice Mountain](https://oldschool.runescape.wiki/w/Below_Ice_Mountain) | Ancient Guardian (25), optional with a pickaxe and boostable 10 Mining |
| [A Night at the Theatre](https://oldschool.runescape.wiki/w/A_Night_at_the_Theatre) | Vyrewatch (105); avoidable araxytes (96/146); quest Hespori (302); conditional full Theatre roster: Maiden/Matomenos, Bloat, Nylocas waves/Vasilias, Sotetseg, Xarpus, Verzik/Athanatos/Matomenos |
| [Beneath Cursed Sands](https://oldschool.runescape.wiki/w/Beneath_Cursed_Sands) | Head Menaphite Guard (174; no protection prayers), two Scarab Mages (119), Champion of Scabaras (379), Menaphite Akh (351) |
| [The Path of Glouphrie](https://oldschool.runescape.wiki/w/The_Path_of_Glouphrie) | Three Warped Terrorbirds (138); Evil Creature (1) |
| [Desert Treasure II - The Fallen Empire](https://oldschool.runescape.wiki/w/Desert_Treasure_II_-_The_Fallen_Empire) | Ancient Guardian (153), Vardorvis (572), Kasonde (193), demon/abyssal route enemies, Leviathan (593), Jhallan (491), Duke Sucellus (538), Whisperer (587), Mysterious Figure (271), Forsaken Assassin (252), Ketla (236), Kasonde the Craven (221), Persten (264) |
| [Secrets of the North](https://oldschool.runescape.wiki/w/Secrets_of_the_North) | Evelot (148), Assassin (262), Strange Creature (368) |
| [Perilous Moons](https://oldschool.runescape.wiki/w/Perilous_Moons) | Sulphur Nagua (98), Blue Moon (329), Blood Moon (329), Eclipse Moon (329) |
| [The Ribbiting Tale of a Lily Pad Labour Dispute](https://oldschool.runescape.wiki/w/The_Ribbiting_Tale_of_a_Lily_Pad_Labour_Dispute) | Cuthbert, Lord of Dread (1) |
| [Twilight's Promise](https://oldschool.runescape.wiki/w/Twilight%27s_Promise) | Knight of Varlamore (81); eight cultists (34) |
| [The Heart of Darkness](https://oldschool.runescape.wiki/w/The_Heart_of_Darkness) | Emissary Brawlers (74/86), Emissary Conjurers (71), Prince Itzla Arkan (167), Amoxliatl (263) |
| [Death on the Isle](https://oldschool.runescape.wiki/w/Death_on_the_Isle) | Two low-level enemies, fought unequipped with non-lethal failure behavior |
| [Meat and Greet](https://oldschool.runescape.wiki/w/Meat_and_Greet) | Dire Wolf Alpha (113); Minotaur (193) |
| [The Blood Moon Rises](https://oldschool.runescape.wiki/w/The_Blood_Moon_Rises) | Vyrewatch/sentinels, Monks of Zamorak (36), Sanguidae (106), webbed-winged crows (98), Venator (200), four ancient feral vyres, Nylocas waves, Maiden, Nylocas Vasilias, Wyrd (564), four Lowerniel Drakan fights (1063) |
| [The Final Dawn](https://oldschool.runescape.wiki/w/The_Final_Dawn) | Emissary Enforcer (196), Chimalli and Lucius (160), cultist waves (70–90), Ennius Tullus (306), Augur Metzli (396) |
| [Prying Times](https://oldschool.runescape.wiki/w/Prying_Times) | Drink troll (14), skippable |
| [Troubled Tortugans](https://oldschool.runescape.wiki/w/Troubled_Tortugans) | Gryphon (95), Shellbane gryphon (235) |
| [The Red Reef](https://oldschool.runescape.wiki/w/The_Red_Reef) | Two pirate ships or crews; Oaky Doak, Boatswain Bill Teak, Mister U., Old Jack, Mute Jack, Bloody Jack (77); Captains Ruban Acer/Jack (95); six pirates (52); Black Eye Bethel (191); giant lobster (112) |
| [Shadows of Custodia](https://oldschool.runescape.wiki/w/Shadows_of_Custodia) | Three Strange Creatures (93) |
| [Scrambled!](https://oldschool.runescape.wiki/w/Scrambled%21) | Large chicken (16), black jaguar (88), red dragon (106) |
| [Learning the Ropes](https://oldschool.runescape.wiki/w/Learning_the_Ropes) | Two giant rats (3) |
| [The Ides of Milk](https://oldschool.runescape.wiki/w/The_Ides_of_Milk) | Quest bull/Brutus encounter |
| [Fallen From Grace](https://oldschool.runescape.wiki/w/Fallen_From_Grace) | Three mountain trolls (69); Mad Angel (270) |

### Miniquests with combat encounters

Miniquests are not counted as quests in the table above, but their quest-boss
content must not be lost from an "all quest bosses" implementation:

| Miniquest | Encounter(s) and reference |
| --- | --- |
| [Curse of the Empty Lord](https://oldschool.runescape.wiki/w/Curse_of_the_Empty_Lord) | Conditional Troll General (113) |
| [The Enchanted Key](https://oldschool.runescape.wiki/w/The_Enchanted_Key) | Temple guardian (30) |
| [Family Pest](https://oldschool.runescape.wiki/w/Family_Pest) | Chronozon (170) |
| [The General's Shadow](https://oldschool.runescape.wiki/w/The_General%27s_Shadow) | Ghost Bouncer (160) |
| [Hopespear's Will](https://oldschool.runescape.wiki/w/Hopespear%27s_Will) | Strongbones (184) |
| [In Search of Knowledge](https://oldschool.runescape.wiki/w/In_Search_of_Knowledge) | Undead Druid (105) |
| [Lair of Tarn Razorlor](https://oldschool.runescape.wiki/w/Lair_of_Tarn_Razorlor) | Tarn Razorlor, with Treus Dayth (95) as the Wiki's toughest-enemy listing |
| [Mage Arena I](https://oldschool.runescape.wiki/w/Mage_Arena_I) | Kolodion's five forms (112 final) |
| [Mage Arena II](https://oldschool.runescape.wiki/w/Mage_Arena_II) | Porazdir, Justiciar Zachariah, and Derwen (348 toughest), then Kolodion hand-in |
| [His Faithful Servants](https://oldschool.runescape.wiki/w/His_Faithful_Servants) | All six Barrows brothers (98/115) |
| [Into the Tombs](https://oldschool.runescape.wiki/w/Into_the_Tombs) | Complete Tombs of Amascut through Tumeken's/Elidinis' Wardens (489+) |
| [The Frozen Door](https://oldschool.runescape.wiki/w/The_Frozen_Door) | Graardor, Kree'arra, Zilyana, K'ril and key-piece lifecycle; use [God Wars plan](../GOD_WARS_PLAN.md) rather than duplicate those bosses here |

## 3. Per-encounter definition of done

Every checkbox in sections 4 and 5 expands to every item below. A quest boss
is not complete if only the NPC definition or generic combat loop exists.

### 3.1 Source and cache manifest

- [ ] Pin the quest article, quick guide, transcript, NPC page(s), item page(s),
  strategy/mechanics page where one exists, and drop-table revision IDs and
  retrieval dates in the quest audit.
- [ ] Resolve every quest-stage NPC version and transform, helper/add, loc,
  object, item, varp/varbit, map square, interface, sequence, spot animation,
  projectile, sound, jingle, and music cue to a symbolic cache name. Record
  absent assets as a cache-version blocker; do not use numeric IDs or visual
  substitutes.
- [ ] Record combat level, size, Hitpoints, five combat stats, offensive and
  defensive bonuses, elemental weakness, immunities, attributes (demon,
  undead, vampyre, dragon, etc.), attackable options, aggression, hunt range,
  movement, respawn, XP multiplier, and Slayer interactions for every version.

### 3.2 Entry, ownership, and state

- [ ] Implement the real initiating interaction: dialogue choice, item-on-NPC,
  item-on-loc, search/chop/mine action, gate/door/ladder, cutscene, wave trigger,
  companion action, or attack. Enforce the exact quest state, branch,
  prerequisites, equipment/inventory restrictions, and safe/unsafe warning.
- [ ] Use a private or party-owned instance where OSRS does. Otherwise prove
  the public spawn cannot be stolen, duplicated, despawned for another player,
  or credited to the wrong player. Persist only durable milestones; temporary
  HP, phase, add, hazard, and cooldown state belongs to the encounter owner.
- [ ] Define leave, escape, death, logout, reconnect, world hop, region change,
  simultaneous kill/death, and server-restart behavior. Restore/retain the
  right quest items and place gravestones or death-bank items at the exact
  service. Clean every NPC, loc, projectile, hazard, timer, overlay, and
  temporary variable exactly once.

### 3.3 Combat and presentation

- [ ] Implement every attack style, cadence, range, targeting rule, accuracy
  roll, max hit, prayer interaction, damage cap/floor, status effect, stat or
  prayer drain, heal, reflect, summon, transform, phase threshold, enrage,
  forced movement, arena hazard, safe tile, and companion/NPC-vs-NPC action.
- [ ] Bind attack/defend/death/transform/movement animations and all launch,
  travel, impact, ground, overhead, hitsplat, sound, dialogue, camera, and
  cutscene cues on the correct game tick. A generic animation or message is
  not an accepted stand-in.
- [ ] Enforce unusual win conditions exactly: special weapon or spell, final
  blow item, style cycle, equipment ban, no-Prayer rule, companion damage,
  survive-N-attacks, mine/chop instead of kill, non-lethal knockout, gauntlet,
  or an NPC which must not die.

### 3.4 Items, loot, and progression

- [ ] Implement every encounter-specific item acquisition, charge/state,
  equip/use check, consumption, transform, ground fallback, duplicate rule,
  bank-aware possession check, loss/destruction text, and reclaim path. Make
  state writes and item removal/grant atomic and idempotent.
- [ ] Implement exact guaranteed quest drops and ordinary/unique/tertiary
  tables only where the quest version actually has them. Preserve quantity,
  condition, ownership, contribution, noted state, independent versus exclusive
  rolls, and the explicit no-loot behavior of summoned/quest-only actors.
- [ ] Advance the quest only from authoritative owner kill/survival/action
  credit. Run the correct NPC/loc transform, cutscene, journal update,
  replacement source, reward/replay unlock, and post-quest world change. A boss
  death must never directly grant the whole quest reward unless OSRS does.

### 3.5 Verification

- [ ] Add deterministic tests for entry eligibility, each attack/phase/add,
  restrictions and immunities, kill credit, item/full-inventory/reclaim paths,
  no-loot/loot behavior, failure/reset, death, relog, reconnect, and repeated
  completion. Add statistical tests for every random drop table.
- [ ] Compile/pack through `make -C src mock230-scripts` and the cache check,
  then run a real-client smoke from the canonical pre-fight state through the
  next stable quest state. Multiplayer/party encounters require attribution,
  wipe/retry, leave, reconnect, and cleanup soaks.
- [ ] Mark the row complete only after its owning quest passes the four gates
  in [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md); Nightmare Zone
  replay and post-quest repeatable variants are separate acceptance rows when
  their behavior or drops differ.

## 4. Quest-specific encounter TODOs

The roster in section 2 is authoritative for **all** combatants. The entries
below identify the encounter-specific item, interaction, and loot/progression
contract which cannot be inferred safely from generic NPC combat. Each entry
also requires the full checklist in section 3 and the linked quest's Article,
Quick guide, Transcript, and NPC/item pages.

### 4.1 Early and classic quests

- [ ] **[Demon Slayer](https://oldschool.runescape.wiki/w/Demon_Slayer/Quick_guide) — [Delrith](https://oldschool.runescape.wiki/w/Delrith).** Spawn the five dark wizards and Delrith from the stone-circle ritual; require [Silverlight](https://oldschool.runescape.wiki/w/Silverlight) to initiate the fight and be equipped for the lethal hit (which may come from recoil), double damage while it is equipped, require the incantation learned from Gypsy Aris, handle a wrong incantation without credit, and advance on banishment. Delrith has no ordinary loot.
  **Implementation status — in progress (2026-08-17):** the state/item gates,
  doubled damage, exact-UID weakened/Banish flow, wrong-chant heal, quest credit,
  explicit no-drop config, source ledger and structural regression contract are
  implemented, including the persistent per-player incantation permutation and
  Aris reminder. Full ritual choreography, shared actor ownership, and runtime
  concurrency/relog tests remain open; the
  manifest deliberately does not mark this encounter verified.
- [ ] **[Shield of Arrav](https://oldschool.runescape.wiki/w/Shield_of_Arrav/Quick_guide) — gang-route kills.** Bind Weaponsmaster/Jonny to the selected gang route, make the weapons-store/cupboard and Phoenix crossbow interactions reachable, and drop/grant the required shield half only through the correct gang sequence. Preserve partner exchange and make the non-selected fight neither required nor a source of duplicate halves.
- [ ] **[The Restless Ghost](https://oldschool.runescape.wiki/w/The_Restless_Ghost/Quick_guide) — skull skeleton.** Searching the coffin/grave path spawns the avoidable skeleton without making its death a quest gate; the [ghost's skull](https://oldschool.runescape.wiki/w/Ghost%27s_skull) is obtained/replaced from the altar independently and returned to the coffin atomically.
- [ ] **[Vampyre Slayer](https://oldschool.runescape.wiki/w/Vampyre_Slayer/Quick_guide) — [Count Draynor](https://oldschool.runescape.wiki/w/Count_Draynor).** Implement coffin opening, garlic's weakening effect, the exact [stake](https://oldschool.runescape.wiki/w/Stake) plus [hammer](https://oldschool.runescape.wiki/w/Hammer) finishing condition, failure when either item is absent, and the scripted staking/death sequence. Do not give normal vampyre loot; grant quest credit only after staking.
- [ ] **[Imp Catcher](https://oldschool.runescape.wiki/w/Imp_Catcher/Quick_guide) — optional imps.** Keep normal imp combat/drop behavior and exact black, red, white, and yellow [bead](https://oldschool.runescape.wiki/w/Bead) rolls; the quest accepts acquired or traded beads, so no kill may be hard-required for accounts able to source them elsewhere.
- [ ] **[Prince Ali Rescue](https://oldschool.runescape.wiki/w/Prince_Ali_Rescue/Quick_guide) / [Pirate's Treasure](https://oldschool.runescape.wiki/w/Pirate%27s_Treasure/Quick_guide) — optional guard/gardener.** Preserve ordinary attackability and drops but ensure combat is not the only route past the jail guard or gardener. Neither kill writes quest state or grants a special quest drop.
- [ ] **[Witch's Potion](https://oldschool.runescape.wiki/w/Witch%27s_Potion/Quick_guide) — rat.** The rat's tail is a quest-state-gated, owner-visible guaranteed drop from a rat killed after starting the quest, with inventory-full ground behavior and no duplicate after possession/hand-in. Implement onion, burnt meat, eye of newt, cauldron hand-in, and potion drink outside the combat handler.
- [ ] **[Dragon Slayer I](https://oldschool.runescape.wiki/w/Dragon_Slayer_I/Quick_guide) — Melzar's Maze and [Elvarg](https://oldschool.runescape.wiki/w/Elvarg).** Implement the visually distinct key-bearing zombie rat, ghost, skeleton, zombie, [Melzar](https://oldschool.runescape.wiki/w/Melzar_the_Mad), and lesser demon, each with its exact coloured key drop and door consumption/replacement rules; keep Wormbrain's map-piece kill optional to telekinetic grab/payment. Gate Crandor travel through the repaired Lady Lumbridge and three map pieces. Require/strongly enforce the [anti-dragon shield](https://oldschool.runescape.wiki/w/Anti-dragon_shield) contract, correct dragonfire, lair shortcut, owner credit, Elvarg death cutscene, and [Elvarg's head](https://oldschool.runescape.wiki/w/Elvarg%27s_head) acquisition/turn-in. See the existing `quest_dragon` scripts, but replace generic/default gaps rather than accepting them.
- [ ] **[Druidic Ritual](https://oldschool.runescape.wiki/w/Druidic_Ritual/Quick_guide) — suit of armour.** Animate the cauldron-room suit only when searched, let the player avoid it, and keep raw rat/bear/beef/chicken enchanting and hand-in independent of its death; no quest loot comes from the armour.
- [ ] **[Lost City](https://oldschool.runescape.wiki/w/Lost_City/Quick_guide) — [Dramen tree spirit](https://oldschool.runescape.wiki/w/Tree_spirit_(Lost_City)).** Enforce Entrana's equipment ban on entry, allow the dungeon branch/log/knife route to make weapons and obtain a bronze axe from zombies, spawn/own the spirit on the first dramen-tree chop, block the tree while alive, and allow [dramen branch](https://oldschool.runescape.wiki/w/Dramen_branch) chopping only after victory. Implement branch-to-[dramen staff](https://oldschool.runescape.wiki/w/Dramen_staff), loss/replacement, and no ordinary spirit loot.
- [ ] **[Witch's House](https://oldschool.runescape.wiki/w/Witch%27s_House/Quick_guide) — [Witch's experiment](https://oldschool.runescape.wiki/w/Witch%27s_experiment).** Implement cheese/mouse, magnet/fountain, leather-glove gate, diary clue, front-door and shed keys, witch line-of-sight/ejection, ball search, then one owned NPC transforming rat→spider→bear→wolf while preserving HP/credit correctly. The ball is the post-fight objective; forms have no ordinary loot and leaving/resetting must not strand the encounter.
- [ ] **[Merlin's Crystal](https://oldschool.runescape.wiki/w/Merlin%27s_Crystal/Quick_guide) — Sir Mordred.** Gate the castle encounter through the correct route, stop the fight at the authored non-lethal outcome, and keep optional giant-bat bones, black candle, Excalibur, chaos-altar ritual, and crystal shatter as separate item interactions. Neither Mordred nor the bat directly completes the quest.
- [ ] **[Heroes' Quest](https://oldschool.runescape.wiki/w/Heroes%27_Quest/Quick_guide) — Ice Queen/firebird/Grip.** Make the [ice gloves](https://oldschool.runescape.wiki/w/Ice_gloves) a reusable guaranteed Ice Queen drop with possession/bank/reclaim logic; make the Entrana firebird drop the [fire feather](https://oldschool.runescape.wiki/w/Fire_feather) only when struck while wearing the gloves. Preserve gang-specific Grip combat and [candlestick](https://oldschool.runescape.wiki/w/Candlestick) progression, optional jailer/dusty-key routing, and partner item exchange without cross-route duplication.
- [ ] **[Scorpion Catcher](https://oldschool.runescape.wiki/w/Scorpion_Catcher/Quick_guide) — optional jailer.** Keep the dusty-key kill/shop alternatives equivalent, implement all three quest scorpions and every partial/full [scorpion cage](https://oldschool.runescape.wiki/w/Scorpion_cage) state, and never advance quest state merely for killing the jailer.
- [ ] **[Family Crest](https://oldschool.runescape.wiki/w/Family_Crest/Quick_guide) — [Chronozon](https://oldschool.runescape.wiki/w/Chronozon).** Require a successful hit from all four standard elemental Blast spells before Chronozon can die, persist the four-hit matrix only for the current owned fight, reset it correctly, and guarantee the correct crest-piece progression with full-inventory/loss recovery. Keep the poison cure, perfect gold, fish, gem, and gauntlet reward choices outside the boss drop.
- [ ] **[Temple of Ikov](https://oldschool.runescape.wiki/w/Temple_of_Ikov/Quick_guide) — Fire Warrior and branch finale.** Enforce [ice arrows](https://oldschool.runescape.wiki/w/Ice_arrow) with a valid bow against the Fire Warrior of Lesarkus, plus boots-of-lightness/lever/chest/weight gates. At the finale, make Lucien versus Guardians of Armadyl mutually exclusive, apply pendant allegiance and staff-of-Armardyl hand-in correctly, and prevent branch NPCs or items from leaking into the other route.
- [ ] **[Holy Grail](https://oldschool.runescape.wiki/w/Holy_Grail/Quick_guide) — [Black Knight Titan](https://oldschool.runescape.wiki/w/Black_Knight_Titan).** Spawn the Titan from the Fisher Realm bridge crossing, prevent passage until defeated, require [Excalibur](https://oldschool.runescape.wiki/w/Excalibur) for the final blow, and reset without losing legitimate damage/ownership. Whistles, napkin, bell, magic whistle teleports, Sir Percival, grail pickup, and no-loot victory must match the guide/transcript.
- [ ] **[Tree Gnome Village](https://oldschool.runescape.wiki/w/Tree_Gnome_Village/Quick_guide) — [Khazard warlord](https://oldschool.runescape.wiki/w/Khazard_warlord).** Preserve battlefield commander/catapult progression and all three orbs; own the warlord, guarantee recovery of the stolen [orbs of protection](https://oldschool.runescape.wiki/w/Orbs_of_protection) through the authored death/dialogue path, and prevent another player from consuming the fight. Completion remains King Bolren's hand-in.
- [ ] **[Fight Arena](https://oldschool.runescape.wiki/w/Fight_Arena/Quick_guide) — arena gauntlet.** Implement Khazard armour disguise, cell keys, Hengrad/Lady Servil dialogue, locked arena boundaries, then scorpion→ogre→[Bouncer](https://oldschool.runescape.wiki/w/Bouncer) with correct between-round cutscenes and food opportunities; General Khazard remains an optional fourth fight. Every actor is owner-scoped, has no ordinary quest loot, and advances exactly one stage on authoritative death. Reconcile the existing `quest_arena` partial scripts.
- [ ] **[Hazeel Cult](https://oldschool.runescape.wiki/w/Hazeel_Cult/Quick_guide) — Alomone branch.** Spawn and credit Alomone only on Ceril's route, make the Hazeel armour/chest key/poison/carnillean armour branch items and sewer valves route-correct, and prevent a kill or item from satisfying the opposing Hazeel route.
- [ ] **[The Grand Tree](https://oldschool.runescape.wiki/w/The_Grand_Tree/Quick_guide) — [Black demon](https://oldschool.runescape.wiki/w/Black_demon_(The_Grand_Tree)).** Gate the owned underground fight through Glough's key/chest/shipyard/translation sequence, use the quest black-demon version and arena geometry, and advance only on the owner's kill. There is no ordinary loot; the King completes the quest and unlocks travel.
- [ ] **[Underground Pass](https://oldschool.runescape.wiki/w/Underground_Pass/Quick_guide) — demon/paladin/Kalrag/Iban route.** Implement paladin badge/food interactions and kills, the three named demons and their amulets, well/door consumption, Kalrag's authored kill gate, Disciple drops/robes where applicable, Klank's gauntlets/barrel/rope/plank/orb interactions, and [Iban's staff](https://oldschool.runescape.wiki/w/Iban%27s_staff) acquisition and well destruction. Preserve agility failures, stat drain, poisonous spiders, item loss/reclaim, and no unintended generic boss loot.
- [ ] **[Observatory Quest](https://oldschool.runescape.wiki/w/Observatory_Quest/Quick_guide) — goblin guard.** Make the guard avoidable through collision/positioning, allow its observatory key to be obtained through the authored interaction, and keep plank/bronze bar/molten glass/lens mould/lens repairs independent of killing it.
- [ ] **[The Tourist Trap](https://oldschool.runescape.wiki/w/The_Tourist_Trap/Quick_guide) — Mercenary Captain.** Enforce the authored unarmed/unarmoured duel and dialogue provocation, grant access/key progress without ordinary loot, and implement slave-clothes disguise, cell door, barrel, winch, mine-cart, plans, prototype dart, and Ana-in-barrel interactions atomically.
- [ ] **[Watchtower](https://oldschool.runescape.wiki/w/Watchtower/Quick_guide) — Gorad.** Guarantee [Gorad's tooth](https://oldschool.runescape.wiki/w/Gorad%27s_tooth) to the eligible owner with ground/full-inventory and loss recovery; preserve optional bat-bone sourcing and the six crystals, relic, potion, shamans, ogre enclave, and spell completion path.
- [ ] **[Legends' Quest](https://oldschool.runescape.wiki/w/Legends%27_Quest/Quick_guide) — spirits and [Nezikchened](https://oldschool.runescape.wiki/w/Nezikchened).** Implement all two-time Ranalph/Irvig/San Tojalon and three-time Nezikchened versions at their exact map/cutscene checkpoints, including demon prayer drain/specials and state-specific stats. Bind golden bowl, bravery potion, yommi seeds/water/fruit, binding-book and totem interactions to the right fights; all quest versions are no-loot except authored quest items/state.
- [ ] **[Big Chompy Bird Hunting](https://oldschool.runescape.wiki/w/Big_Chompy_Bird_Hunting/Quick_guide) — Chompy.** Implement ogre bow/arrows, bloated-toad bait, spawn/flee/death behavior, raw chompy owner drop and Pluck operation; optional wolves only source wolf bones. Rantz's kill credit and cooked-chompy hand-in, not a generic bird death, complete the encounter.
- [ ] **[Elemental Workshop I](https://oldschool.runescape.wiki/w/Elemental_Workshop_I/Quick_guide) / [II](https://oldschool.runescape.wiki/w/Elemental_Workshop_II/Quick_guide) — earth elementals.** Mining the correct rock must transform/spawn the elemental and guarantee elemental ore to the eligible player; pre-owned bars bypass the EWII kills. Preserve battered/slashed book, key, furnace/bellows/water-wheel/hammer and elemental→primed bar→shield/helm item pipelines with replacement and no ordinary elemental loot.
- [ ] **[Nature Spirit](https://oldschool.runescape.wiki/w/Nature_Spirit/Quick_guide) — ghasts.** Implement druid pouch charges, ghast manifestation, combat and three credited kills, silver sickle/Bloom, spell/blessing interactions, Filliman transforms, and permanent grotto/world state. Ghast drops and rotten-food conversion must follow the quest state without duplicate credit.
- [ ] **[Priest in Peril](https://oldschool.runescape.wiki/w/Priest_in_Peril/Quick_guide) — guardian and monk.** Use the quest guardian's Magic immunity and gate access on its death; the correct Monk of Zamorak guarantees the [golden key](https://oldschool.runescape.wiki/w/Golden_key), which swaps with the iron key via the monument puzzle. Implement essence count/hand-in, Drezel rescue, bucket/well/blessed-water interactions, and no generic boss loot.
- [ ] **[Regicide](https://oldschool.runescape.wiki/w/Regicide/Quick_guide) — Tyras guard.** Tie the guard kill to the correct camp infiltration and item progression, preserving barrel/bomb (coal, sulphur, quicklime, naphtha), tinderbox/catapult use, unsafe self-damage, and Tyras tent destruction. A generic Tyras guard elsewhere must not satisfy the stage.
- [ ] **[Tai Bwo Wannai Trio](https://oldschool.runescape.wiki/w/Tai_Bwo_Wannai_Trio/Quick_guide) — monkey/raw meat source.** Use normal monkey combat only as one source of the required raw meat; implement the three brothers' separate spear/karambwan/antipoison and food pipelines, jogre bones/marrow/skin items, and no special monkey kill credit.
- [ ] **[Troll Stronghold](https://oldschool.runescape.wiki/w/Troll_Stronghold/Quick_guide) — Dad/general/prison.** Make Dad a non-lethal gate fight with the correct arena/knockback outcome; make one of the generals guarantee the prison key; keep Berry/Twig optional through Thieving; then use the correct keys/cells to free Eadgar and Godric. No troll's ordinary drop may replace authored key/state credit.
- [ ] **[Shades of Mort'ton](https://oldschool.runescape.wiki/w/Shades_of_Mort%27ton/Quick_guide) — Loar shades.** Require five qualifying Loar remains through ordinary shade kills, implement pyre logs/olive oil/sacred oil, shade remains, pyre ignition, keys/chests and sanctity/temple repair, and preserve the difference between quest hand-in and post-quest shade loot.
- [ ] **[The Fremennik Trials](https://oldschool.runescape.wiki/w/The_Fremennik_Trials/Quick_guide) — [Koschei](https://oldschool.runescape.wiki/w/Koschei_the_deathless) and [Draugen](https://oldschool.runescape.wiki/w/Draugen).** Enforce the basement equipment restriction, three mandatory Koschei victories, optional fourth-form loss/win outcome, and safe item return; implement hunter's-talisman direction/frequency, moving Draugen spawn, owner kill and talisman/signature progression. Optional citizens may drop the lyre, but crafting/enchanting a lyre remains an equal route.
- [ ] **[Horror from the Deep](https://oldschool.runescape.wiki/w/Horror_from_the_Deep/Quick_guide) — dagannoth pair.** Repair the lighthouse with plank/nails/molten glass, fuel/light the mechanism, descend through the journal/rusty-casket key, then own the Dagannoth and [Dagannoth Mother](https://oldschool.runescape.wiki/w/Dagannoth_mother). Implement the Mother's colour/style weakness cycle and exact resistances, arena transitions, death/reset, casket/reward-book selection, and no ordinary quest-boss loot.
- [ ] **[Monkey Madness I](https://oldschool.runescape.wiki/w/Monkey_Madness_I/Quick_guide) — [Jungle Demon](https://oldschool.runescape.wiki/w/Jungle_Demon).** Gate the arena through the completed greegree/talisman/enchantment, 10th-squad sigil and Zooknock route; run the squad cutscene, demon magic/melee, allied gnome damage and player kill-credit rules. Sigil loss/reclaim and post-kill return must be safe; the demon has no ordinary loot.
- [ ] **[Haunted Mine](https://oldschool.runescape.wiki/w/Haunted_Mine/Quick_guide) — [Treus Dayth](https://oldschool.runescape.wiki/w/Treus_Dayth).** Implement mine-cart/key/glowing-fungus/valve/lift/water route, moving cranes and haunted tools, Treus teleports/possession attacks and arena pathing, then allow the salve shard to be mined after victory. No direct boss loot; shard, crystal enchantment and loss replacement are separate interactions.
- [ ] **[Troll Romance](https://oldschool.runescape.wiki/w/Troll_Romance/Quick_guide) — [Arrg](https://oldschool.runescape.wiki/w/Arrg).** Gate Arrg through the flower, waxed sled and sledding route, preserve the unsafe-death warning/gravestone behavior, own the mountain fight and advance on Arrg's death without ordinary loot. Do not conflate the nearby Troll General with the quest boss.
- [ ] **[In Search of the Myreque](https://oldschool.runescape.wiki/w/In_Search_of_the_Myreque/Quick_guide) — Skeleton Hellhound.** Implement Cyreg boat/Swamp Boaty route and weapon hand-ins to each Myreque member, then own the hellhound ambush, preserve Vanstrom's cutscene and member survival/state, and advance only after the correct fight. The hellhound has no special quest drop.
- [ ] **[Creature of Fenkenstrain](https://oldschool.runescape.wiki/w/Creature_of_Fenkenstrain/Quick_guide) — Experiment.** Preserve the experiment cavern route and ordinary experiment combat as a source of the cavern key/bones only where authored; the defining item chain is decapitated head/body, limbs, conductor mould, silver bar and lightning conductor. No generic experiment kill completes the quest.
- [ ] **[Roving Elves](https://oldschool.runescape.wiki/w/Roving_Elves/Quick_guide) — [Moss Guardian](https://oldschool.runescape.wiki/w/Moss_Guardian).** Enforce Glarial's Tomb's weapon, armour and Prayer restrictions at entry and throughout the owned fight; guarantee the old/consecration seed progression, then implement the waterfall, chalice, seed planting and crystal reward. No ordinary moss-giant table applies.
- [ ] **[Ghosts Ahoy](https://oldschool.runescape.wiki/w/Ghosts_Ahoy/Quick_guide) — giant lobster.** Spawn the quest lobster at the correct chest/search, guarantee the map/sword progression with full-inventory recovery, and keep ghostspeak, nettle tea, dye/flag, translation/manual, ectotoken and petition interactions separate. The lobster has no normal special loot beyond the authored quest item.
- [ ] **[One Small Favour](https://oldschool.runescape.wiki/w/One_Small_Favour/Quick_guide) — [Slagilith](https://oldschool.runescape.wiki/w/Slagilith) and dwarfs.** Spawn Slagilith from the rock interaction, enforce its pickaxe/Mining-sensitive weakness and owner credit, then run the multicombat dwarf ambush at the authored hand-off. Bind animate-rock scroll, weather vane, mattress, cages, pot lid, breathing salts, herbal tincture and all exchanged favours atomically; no ordinary boss loot.
- [ ] **[Mountain Daughter](https://oldschool.runescape.wiki/w/Mountain_Daughter/Quick_guide) — [The Kendal](https://oldschool.runescape.wiki/w/The_Kendal).** Gate the cave through rope/mud/hamal route and daughter's remains/clue items, own the Kendal fight, then make the corpse/remains and burial interactions—not a generic drop—advance the quest. Implement bearhead reward/reclaim separately.
- [ ] **[Between a Rock...](https://oldschool.runescape.wiki/w/Between_a_Rock.../Quick_guide) — [Arzinian Avatar](https://oldschool.runescape.wiki/w/Arzinian_Avatar).** Follow the detailed [quest audit](../quests/between_a_rock.md): gold helmet/cannon launch, timed realm, carried gold absorption, colour/stat-counter version selection, gold-ore weakening tiers, owner-only kill, cleanup and post-quest re-entry. Return the helmet and grant/drop the rune pickaxe through the finale; the Avatar itself has no ordinary loot.
- [ ] **[The Feud](https://oldschool.runescape.wiki/w/The_Feud/Quick_guide) — champion and Tough Guy.** Tie both duels to the correct Menaphite/Black Arm gang dialogue stages, preserve blackjack and pickpocket training, poison/beer/camel-dung item routes, make each non-looting fight owner-scoped, and advance only the matching gang state.
- [ ] **[Desert Treasure I](https://oldschool.runescape.wiki/w/Desert_Treasure_I/Quick_guide) — four diamonds.** Implement [Dessous](https://oldschool.runescape.wiki/w/Dessous), [Kamil](https://oldschool.runescape.wiki/w/Kamil), [Fareed](https://oldschool.runescape.wiki/w/Fareed), and both [Damis](https://oldschool.runescape.wiki/w/Damis) forms with their exact attacks, immunities, stat/prayer effects, adds/routes and reset rules. Bind the blood path's garlic/spice/silver/pot/blood ingredients, ice path's cake/spiked boots/troll-child and five-troll gauntlet, smoke path's facemask and ice-glove restriction, and shadow path's ring-of-visibility/lockpick interactions. Each owned boss awards exactly its diamond with loss/reclaim; the pyramid consumes/places all four and unlocks Ancient Magicks.
- [ ] **[Icthlarin's Little Helper](https://oldschool.runescape.wiki/w/Icthlarin%27s_Little_Helper/Quick_guide) — flashback fights.** Preserve cat/start conditions, hypnosis/flashback state, jar identity and stat-selected Apmeken/Crondis/Scabaras/Het apparition, then the possessed-priest fight and burial. Canopic jar, linen, salt, bucket, charm, holy symbol and corpse interactions must survive logout/re-entry without duplicating or selecting a different guardian.
- [ ] **[Recruitment Drive](https://oldschool.runescape.wiki/w/Recruitment_Drive/Quick_guide) — [Sir Leye](https://oldschool.runescape.wiki/w/Sir_Leye).** Enforce the no-equipped-items entry, women-only/authored identity behavior where still applicable to the target revision, and Sir Leye's blade immunity/unarmed-or-blunt defeat. Keep every puzzle room, chemical items and outfit return atomic; Sir Leye drops nothing and only unlocks the next test.
- [ ] **[Mourning's End Part I](https://oldschool.runescape.wiki/w/Mourning%27s_End_Part_I/Quick_guide) — Mourner.** Apply the authored combat-stat reduction to 20 (including current/base Hitpoints handling) at the fight, guarantee/recover the gas-mask/mourner-clothes progression, and preserve poison, dye, toad, feather, barrel and disguise actions. No generic mourner elsewhere may satisfy the quest.
- [ ] **[Wanted!](https://oldschool.runescape.wiki/w/Wanted%21/Quick_guide) — Black Knight/Solus.** Implement CommOrb scanning, all informant/location transitions, the required Black Knight kill and combat cutscenes, then Solus's scripted teleport/chase/capture outcome rather than treating him as an ordinary death. Black armour/CommOrb/commorb v2 and clue item loss/reclaim must match the transcript.
- [ ] **[Rum Deal](https://oldschool.runescape.wiki/w/Rum_Deal/Quick_guide) — fever spider and [Evil spirit](https://oldschool.runescape.wiki/w/Evil_spirit).** Require slayer gloves for fever-spider safety and the correct ingredient drop, bind blindweed/bucket/sluglings/nuts/rum brewing and blessed-hatchet/cursed-brew actions, then own the spirit fight with exact prayer interaction. No normal spirit loot; completion follows the post-fight dialogue.
- [ ] **[Shadow of the Storm](https://oldschool.runescape.wiki/w/Shadow_of_the_Storm/Quick_guide) — [Agrith-Naar](https://oldschool.runescape.wiki/w/Agrith-Naar).** Implement the disguise, golem, candles/dyes, demonic sigil/incantation and summoning cutscene; require Silverlight for the authored final blow, transform it into [Darklight](https://oldschool.runescape.wiki/w/Darklight), and prevent normal demon loot. Wrong positioning/incantation and death/re-entry must reset safely.
- [ ] **[Ratcatchers](https://oldschool.runescape.wiki/w/Ratcatchers/Quick_guide) — cat versus [King rat](https://oldschool.runescape.wiki/w/King_rat).** Use the player's follower cat/kitten stats, food healing and pet-combat ownership in the pit; the player cannot damage the rat. Preserve rat poison, poisoned cheese, rat-pole/catch counts, cat death/escape, replacement and dialogue; the rat has no player loot.
- [ ] **[Spirits of the Elid](https://oldschool.runescape.wiki/w/Spirits_of_the_Elid/Quick_guide) — three golems.** Implement the black/grey/white golems as separate owned fights with their required combat-style vulnerabilities, exact room/statue/door interactions and no ordinary loot. Bind rope, knives, needle/thread, torn robes and weapon offerings, then restore each ancestral statue/water channel only after the matching kill.
- [ ] **[Cabin Fever](https://oldschool.runescape.wiki/w/Cabin_Fever/Quick_guide) — ship combat.** Implement cannon loading/firing, repair, sabotage, plunder and pirate waves across both ships, kill/loot counts, locker contents and contribution ownership. Quest pirates and ship loot must be state-gated; normal pirates elsewhere cannot satisfy the quotas.
- [ ] **[Fairytale I](https://oldschool.runescape.wiki/w/Fairytale_I_-_Growing_Pains/Quick_guide) — [Tanglefoot](https://oldschool.runescape.wiki/w/Tanglefoot).** Generate the account-specific three-item secateur enchantment request, create [magic secateurs](https://oldschool.runescape.wiki/w/Magic_secateurs), require them to damage Tanglefoot, and own the tunnel fight. Allow queen/guardian post-kill progression and secateur loss/reclaim; no ordinary Tanglefoot loot.

### 4.2 Recipe for Disaster

- [ ] **[Freeing the Mountain Dwarf](https://oldschool.runescape.wiki/w/Recipe_for_Disaster/Freeing_the_Mountain_Dwarf/Quick_guide).** Keep the icefiend kill optional when ice gloves are already possessed; otherwise use the ordinary ice-glove source. Implement rock cake ingredients, cooling while wearing gloves, coin/beer interactions, hand-in, and subquest completion without a special icefiend quest drop.
- [ ] **[Freeing Pirate Pete](https://oldschool.runescape.wiki/w/Recipe_for_Disaster/Freeing_Pirate_Pete/Quick_guide).** Implement diving apparatus/fishbowl helmet, underwater pressure/weight, five Mudskipper hides and crab claw/meat drops, needle/bronze wire, pestle/fish ingredients, stuffed snake preparation and loss recovery. Only the prepared food completes the subquest.
- [ ] **[Freeing Sir Amik Varze](https://oldschool.runescape.wiki/w/Recipe_for_Disaster/Freeing_Sir_Amik_Varze/Quick_guide).** Spawn the stat-scaled [Evil Chicken](https://oldschool.runescape.wiki/w/Evil_Chicken_(Recipe_for_Disaster)) and black dragon in the correct lair, guarantee the raw chicken/raw dragon token ingredients to the owner, and bind creme brulee, cinnamon, evil chicken egg, dragon token and brulee flambé item pipeline. Preserve ice-glove interaction and no extra quest-boss loot.
- [ ] **[Freeing King Awowogei](https://oldschool.runescape.wiki/w/Recipe_for_Disaster/Freeing_King_Awowogei/Quick_guide).** Preserve talisman/greegree crafting and optional monkey kills, make Big Snake corpses yield the required raw stuffed-snake component with cooking-failure retries, and implement red banana, tchiki nut, rope/agility and stuffed-snake cooking/hand-in atomically.
- [ ] **[Freeing Skrach Uglogwee](https://oldschool.runescape.wiki/w/Recipe_for_Disaster/Freeing_Skrach_Uglogwee/Quick_guide).** Implement ogre bow/arrows, bellows, bloated toads, rock bait, Jubbly spawn/kill/pluck and raw/cooked Jubbly ownership; bind the cooked Jubbly hand-in to the subquest without generic chompy credit.
- [ ] **[Defeating the Culinaromancer](https://oldschool.runescape.wiki/w/Recipe_for_Disaster/Quick_guide#Defeating_the_Culinaromancer).** Build one owned six-round instance for [Agrith-Na-Na](https://oldschool.runescape.wiki/w/Agrith-Na-Na), [Flambeed](https://oldschool.runescape.wiki/w/Flambeed), [Karamel](https://oldschool.runescape.wiki/w/Karamel), [Dessourt](https://oldschool.runescape.wiki/w/Dessourt), [Gelatinnoth Mother](https://oldschool.runescape.wiki/w/Gelatinnoth_Mother), and [Culinaromancer](https://oldschool.runescape.wiki/w/Culinaromancer), in order. Implement banana/slip/stat-drain, ice-glove requirement, freezing/stat drain, teleport/drain, colour-style cycle and final spell attacks; preserve between-round bank/exit/re-entry state, death cleanup and no normal loot. Only the final death triggers aggregate RFD completion, full Culinaromancer's Chest access and the authored reward sequence.

### 4.3 Mid-game and 2006–2007 boss encounters

- [ ] **[In Aid of the Myreque](https://oldschool.runescape.wiki/w/In_Aid_of_the_Myreque/Quick_guide) — Gadderanks and Juvinates.** Implement the Burgh de Rott repair/material pipeline, Gadderanks confrontation, sickle/flail-eligible vampyre damage, two level-54 Juvinates, then the route-dependent two level-75 or four level-50 ambush. Preserve Veliaf/Myreque ally ownership, rod-of-ivandis/silverthril item stages, food/bank/shop unlocks, no ordinary quest-NPC loot and the exact post-fight dialogue states.
- [ ] **[A Soul's Bane](https://oldschool.runescape.wiki/w/A_Soul%27s_Bane/Quick_guide) — four emotion rooms and [Tolna](https://oldschool.runescape.wiki/w/Tolna).** Follow the existing [quest audit](../quests/a_souls_bane.md): anger creatures and four supplied weapon types, fear reapers from the correct dark holes, six confusion beasts which must be survived rather than killed, hopeless-creature despair interactions, then three separately targetable Tolna heads/parts. Rope entry, room exits, death/re-entry, no-loot actors and Tolna rescue must be complete.
- [ ] **[Rag and Bone Man I](https://oldschool.runescape.wiki/w/Rag_and_Bone_Man_I/Quick_guide) / [II](https://oldschool.runescape.wiki/w/Rag_and_Bone_Man_II/Quick_guide) — bone-source kills.** Build an explicit species/version→special-bone ledger for all eight and twenty-seven requested bones, with quest-state-gated independent drops, owner visibility, bank/duplicate/full-inventory behavior, vinegar pots, pot-boiler/grate processing and hand-in. A same-name monster version not listed by the Wiki must not drop a quest bone; none of these ordinary kills is a boss.
- [ ] **[Swan Song](https://oldschool.runescape.wiki/w/Swan_Song/Quick_guide) — Sea Troll siege and [Queen](https://oldschool.runescape.wiki/w/Sea_Troll_Queen).** Implement all eleven fixed wave trolls, wall breach/repair, Wise Old Man and colony defenders, then the owned Queen fight with magic attacks and exact arena transitions. Bind five iron sheets, logs, tinderbox, swamp paste, rope, pot, garden/fishing/cooking errands and post-fight colony access; quest trolls/Queen have only authored loot/state.
- [ ] **[Royal Trouble](https://oldschool.runescape.wiki/w/Royal_Trouble/Quick_guide) — [Giant Sea Snake](https://oldschool.runescape.wiki/w/Giant_Sea_Snake).** Gate the cave through Miscellania approval, mining/woodcutting and rope/agility route; own the snake and its cave adds, award the authored journal/body/progression through the correct search/death interaction, and update kingdom-management capacity/rewards only at final dialogue. No repeatable sea-snake table applies to the quest version.
- [ ] **[Death to the Dorgeshuun](https://oldschool.runescape.wiki/w/Death_to_the_Dorgeshuun/Quick_guide) — [Sigmund](https://oldschool.runescape.wiki/w/Sigmund).** Preserve Zanik follower/infiltration, H.A.M. disguise, optional guards and mill trap; give/use the [bone dagger](https://oldschool.runescape.wiki/w/Bone_dagger) or [Dorgeshuun crossbow](https://oldschool.runescape.wiki/w/Dorgeshuun_crossbow) special to disable Sigmund's Protect from Melee, run Zanik/Sigmund NPC-vs-NPC actions and safe failure/retry. No ordinary Sigmund loot; completion follows Zanik rescue.
- [ ] **[Fairytale II](https://oldschool.runescape.wiki/w/Fairytale_II_-_Cure_a_Queen/Quick_guide) — Goraks.** Keep Gorak kills a source of the claw ingredient while preserving prayer ineffectiveness, Zanaris/Gorak-plane access, fairy-ring codes, starflower, vial of water and queen-healing potion. Exact claw normal-drop/quest possession rules apply; a kill is not itself a quest-state transition.
- [ ] **[Lunar Diplomacy](https://oldschool.runescape.wiki/w/Lunar_Diplomacy/Quick_guide) — Suqah and [Me](https://oldschool.runescape.wiki/w/Me).** Make Suqah teeth/hide drops and tiara/ceremonial outfit/staff item pipeline exact, prepare/use the waking-sleep and dream potions, then run the dream-platform trials and owned mirror fight. The boss copies the player's appearance/equipment as authored, grants no loot, and unlocks Lunar spellbook only after complete Oneiromancer dialogue.
- [ ] **[The Eyes of Glouphrie](https://oldschool.runescape.wiki/w/The_Eyes_of_Glouphrie/Quick_guide) — six Evil Creatures.** Spawn exactly six one-HP creatures from the machine/puzzle resolution at their authored positions, count only owner kills, and clean leftovers. Crystal discs, singing bowl/machine values, token/item exchanges, cutscenes and no-loot creatures must survive relog/retry.
- [ ] **[The Slug Menace](https://oldschool.runescape.wiki/w/The_Slug_Menace/Quick_guide) — [Slug Prince](https://oldschool.runescape.wiki/w/Slug_Prince).** Implement commorb, dead-sea-slug/rune-essence/door sequence, runecrafting altar, pillar puzzle and proselyte squad, then the owned Prince's melee/magic/ranged behavior and Mother Mallum cutscene. The quest boss drops nothing; completion/reward armour access occurs through Sir Tiffy/Sir Amik dialogue.
- [ ] **[My Arm's Big Adventure](https://oldschool.runescape.wiki/w/My_Arm%27s_Big_Adventure/Quick_guide) — Baby/Giant [Roc](https://oldschool.runescape.wiki/w/Giant_Roc).** Implement goutweed/tuber/compost/farming interactions, My Arm ally actions, Baby Roc pre-wave, then Giant Roc knockback/forced movement, multi-style attacks, arena edges and owner credit. Preserve disease-free herb patch unlock and no ordinary roc loot.
- [ ] **[Contact!](https://oldschool.runescape.wiki/w/Contact%21/Quick_guide) — [Giant Scarab](https://oldschool.runescape.wiki/w/Giant_scarab).** Follow the existing [quest audit](../quests/contact.md): torch/tinderbox/rope route, trap/floor/maze state, owned scarab, Locust Rider and two Scarab Mage versions, summon caps and cleanup. Guarantee/drop [Keris](https://oldschool.runescape.wiki/w/Keris) only for the eligible owner with full-inventory and reclaim logic; no generic scarab table or duplicate weapon.
- [ ] **[Cold War](https://oldschool.runescape.wiki/w/Cold_War/Quick_guide) — Icelords.** Follow the existing [quest audit](../quests/cold_war.md): penguin suit/disguise, clockwork suit controls, outpost/cowbell/bongos, agility and submarine route, then one-to-three stage-correct Icelords with Larry/Noodle state. Preserve variable count, owner credit and no special loot.
- [ ] **[The Fremennik Isles](https://oldschool.runescape.wiki/w/The_Fremennik_Isles/Quick_guide) — troll gauntlet and [Ice Troll King](https://oldschool.runescape.wiki/w/Ice_Troll_King).** Implement jester controls/report, bridges/rope, eight split logs, shield, raw tuna, yak hide/needle/thread and Neitiznot supplies; run ten qualifying ice-troll kills and the owned King with correct ranged/magic/melee/prayer behavior. The King's head/cutscene/return must be authored, with no unrelated troll kill credit.
- [ ] **[The Great Brain Robbery](https://oldschool.runescape.wiki/w/The_Great_Brain_Robbery/Quick_guide) — [Barrelchest](https://oldschool.runescape.wiki/w/Barrelchest).** Gate Harmony Island through diving/submarine/brain-transfer and repair interactions, run four Sorebones then Barrelchest's anchor/prayer-drain attacks in the owned arena, and create the broken [barrelchest anchor](https://oldschool.runescape.wiki/w/Barrelchest_anchor) through the authored reward/repair route. Handle prayer-book items and no extra quest-boss table.
- [ ] **[What Lies Below](https://oldschool.runescape.wiki/w/What_Lies_Below/Quick_guide) — outlaws and King Roald.** Guarantee the outlaw rat's paper/route items only at the correct state, implement five-outlaw count, chaos altar/beacon ring and Surok spell cutscene, then a non-lethal controlled King Roald fight which stops before killing him. Do not drop royal loot or allow normal death; restore state/dialogue safely on failure.
- [ ] **[Olaf's Quest](https://oldschool.runescape.wiki/w/Olaf%27s_Quest/Quick_guide) — skeletons and [Ulfric](https://oldschool.runescape.wiki/w/Ulfric).** Implement windswept logs/carvings, wet-plank bridge failures, four key pieces/doors and Skeleton Fremennik, then enforce [brine sabre](https://oldschool.runescape.wiki/w/Brine_sabre) damage against Ulfric. The boss grants the authored key/treasure access without an ordinary table; preserve unsafe cave exits, coffin/chest and item replacement.
- [ ] **[Another Slice of H.A.M.](https://oldschool.runescape.wiki/w/Another_Slice_of_H.A.M./Quick_guide) — H.A.M. pair and Sigmund.** Follow the existing [quest audit](../quests/another_slice_of_ham.md): Zanik follower, train/track/door route, archer/mage waves, Sigmund prayer and bone-weapon special, NPC-vs-NPC cutscenes, train collision/escape, owned credit, no-loot versions and safe follower cleanup.
- [ ] **[Dream Mentor](https://oldschool.runescape.wiki/w/Dream_Mentor/Quick_guide) — four dreams.** Build an owned uninterrupted sequence for [Inadequacy](https://oldschool.runescape.wiki/w/The_Inadequacy) plus Doubts, [Everlasting](https://oldschool.runescape.wiki/w/The_Everlasting), [Untouchable](https://oldschool.runescape.wiki/w/The_Untouchable), and [Illusive](https://oldschool.runescape.wiki/w/The_Illusive), with each exact summon/teleport/immunity/melee behavior. Enforce dream-vial/kindling, Cyris food/gear/health, arena item restrictions, between-fight state, death/re-entry and zero normal loot; unlock spells only after waking dialogue.
- [ ] **[Grim Tales](https://oldschool.runescape.wiki/w/Grim_Tales/Quick_guide) — [Glod](https://oldschool.runescape.wiki/w/Glod).** Implement beanstalk planting/climbing, shrinking/growing, mouse-hole/cage/witch puzzle and griffin item chain, then Glod's prayer disable, pull/push and multi-style attacks. The golden goblin/harp or key objective follows authored death/search; no ordinary giant table.
- [ ] **[Shilo Village](https://oldschool.runescape.wiki/w/Shilo_Village/Quick_guide) — [Nazastarool](https://oldschool.runescape.wiki/w/Nazastarool).** Implement the cave/door/rope/torch route and corpse-scroll/wooden-sword/beads/crystal item chain, then one owned actor transforming zombie→skeleton→ghost with exact stats and undead interactions. Guarantee Rashiliyia's corpse/remains progression, full-inventory/reclaim and no generic undead loot.
- [ ] **[Land of the Goblins](https://oldschool.runescape.wiki/w/Land_of_the_Goblins/Quick_guide) — five tribal priests.** Bind goblin-potion colours/forms and each tribal room to Snothead, Snailfeet, Mosschin, Redeyes and Strongbones in order; implement rising stats/mechanics, owner credit and the exact key/artefact/dialogue progression. Quest priest versions have no ordinary loot; Hopespear's Will uses a separate Strongbones version.
- [ ] **[While Guthix Sleeps](https://oldschool.runescape.wiki/w/While_Guthix_Sleeps/Quick_guide) — siege, Surok, [Balance Elemental](https://oldschool.runescape.wiki/w/Balance_Elemental), Tormented Demons.** Implement the assassins/mercenaries/elite Black Knights and ally survival, Surok's spell/prayer behavior, Balance Elemental's style/stat/appearance changes and arena hazards, then two quest Tormented Demons with shield/style-switch/firebomb mechanics. Bind elite Black armour, truth serum, strange teleorb, enriched snapdragon, broav/key/weight puzzle, dolmen and Stone-of-Jas interactions; no repeatable Tormented Demon loot during the quest. Post-quest TDs use their dedicated drop plan/scripts.
- [ ] **[Zogre Flesh Eaters](https://oldschool.runescape.wiki/w/Zogre_Flesh_Eaters/Quick_guide) — [Slash Bash](https://oldschool.runescape.wiki/w/Slash_Bash).** Implement disease and Relicym's balm, brutal-arrow/comp-ogre-bow eligibility, coffin keys/artefacts, portrait/book/potion item chain, quest zombie and owned Slash Bash. Preserve safe-spots as geometry, exact quest drops/state, and keep post-quest zogre/ogre-coffin loot separate.

### 4.4 OSRS-original and grandmaster encounters

- [ ] **[Monkey Madness II](https://oldschool.runescape.wiki/w/Monkey_Madness_II/Quick_guide) — Kruk, generals, gorillas and [Glough](https://oldschool.runescape.wiki/w/Glough).** Implement stealth/platform/explosive-satchel route, Kruk's dungeon and fight, mutually correct Keef/Kob interactions, tortured/demonic gorilla waves and all three Glough phases with room transitions, rockfall/knockback/prayer behavior. Preserve greegrees, translation book, chisel/charges, royal seed pod and owner/instance/death lifecycle; quest actors have no post-quest gorilla loot.
- [ ] **[Dragon Slayer II](https://oldschool.runescape.wiki/w/Dragon_Slayer_II/Quick_guide) — [Vorkath](https://oldschool.runescape.wiki/w/Vorkath), Robert, dragon gauntlet, [Galvek](https://oldschool.runescape.wiki/w/Galvek).** Implement map/key-piece and fleet interactions, quest Vorkath's dragonfire/acid/ice/zombified-spawn cycle, Robert's arena and special behavior, every listed dragon version in the Lithkren gauntlet, then Galvek's four phases/arena quadrants/waves/one-shot mechanics. Enforce dragonfire protection, insulated boots where required, quest-item loss/reclaim, owner instances/death bank and no post-quest Vorkath/dragon loot. Only authored head/state/reward interactions advance the quest.
- [ ] **[The Depths of Despair](https://oldschool.runescape.wiki/w/The_Depths_of_Despair/Quick_guide) — [Sand Snake](https://oldschool.runescape.wiki/w/Sand_Snake).** Gate the owned fight through the Client of Kourend/library/crabclaw cave route and supply chest/key/search interactions; advance on owner kill without ordinary snake loot, then return the captured information/item through Lord Kandur Hosidius dialogue.
- [ ] **[The Corsair Curse](https://oldschool.runescape.wiki/w/The_Corsair_Curse/Quick_guide) — [Ithoi](https://oldschool.runescape.wiki/w/Ithoi_the_Navigator).** Preserve all crew interviews/clues, doll/knife/book interactions and accusation state, then own Ithoi's cabin fight and post-defeat dialogue. No ordinary human loot; the curse resolution and Corsair Cove unlock occur only after final report.
- [ ] **[A Taste of Hope](https://oldschool.runescape.wiki/w/A_Taste_of_Hope/Quick_guide) — [Abomination](https://oldschool.runescape.wiki/w/Abomination) and [Ranis](https://oldschool.runescape.wiki/w/Ranis_Drakan).** Follow the complete [quest audit](../quests/a_taste_of_hope.md): Serafina weakening, four Vyrewatch, Ivandis flail-only damage, Ranis's 65%/25% add phases, blood attacks/enrage, owner instance and death bank. Implement every flail component, blood potion, notes, medallion and tome acquisition/reclaim; neither quest boss uses an ordinary drop table.
- [ ] **[Tale of the Righteous](https://oldschool.runescape.wiki/w/Tale_of_the_Righteous/Quick_guide) — Corrupt Lizardman.** Gate the owned fight through the Shayzien expedition, tablet/rope/pickaxe/cave interactions and corruption reveal; implement lizardman attacks and exact no-loot/state credit, then return the recovered tablet/report. Do not conflate this quest version with ordinary lizardmen/shamans.
- [ ] **[Making Friends with My Arm](https://oldschool.runescape.wiki/w/Making_Friends_with_My_Arm/Quick_guide) — Don't Know What and Mother.** Implement the Weiss route, fire pits and salt mining/combination, sled/climbing/obstacle state, owned Don't Know What fight, then Mother's phases/adds and My Arm/Burntmeat ally actions. Preserve goutweed, buckets/rope/planks/nails, three salts and herb-patch unlock; quest trolls have no normal loot.
- [ ] **[Song of the Elves](https://oldschool.runescape.wiki/w/Song_of_the_Elves/Quick_guide) — civil-war waves, Arianwyn/Essyllt and [Fragment of Seren](https://oldschool.runescape.wiki/w/Fragment_of_Seren).** Implement every fixed mourner/paladin/knight/Iorwerth wave with ally/owner credit, Arianwyn and Essyllt encounters, then Seren's four special attacks, clone/heal phases, unavoidable HP-scaled nuke and exact arena/death-retrieval lifecycle. Bind explosive potion, crystal, library-light puzzle and all quest items/cutscenes to durable stages; no quest combatant drops ordinary loot. Unlock Prifddinas only after final dialogue.
- [ ] **[The Ascent of Arceuus](https://oldschool.runescape.wiki/w/The_Ascent_of_Arceuus/Quick_guide) — souls.** Follow the existing quest audit when created: bind ensouled-head/library/tower interactions to five Tormented Souls and the Trapped Soul, preserve soul-bearing item ownership and no ordinary loot, then advance only from the authored tower cutscene/kill sequence.
- [ ] **[The Fremennik Exiles](https://oldschool.runescape.wiki/w/The_Fremennik_Exiles/Quick_guide) — basilisk chain and [Jormungand](https://oldschool.runescape.wiki/w/The_Jormungand).** Implement youngling/basilisk/monstrous basilisk/Typhor stages, exact mirror/V's shield protection and untradeable upgrade/reclaim rules, then Jormungand's acid, prayer and arena mechanics in an owned instance. Quest basilisks/Jormungand give only authored progression; post-quest basilisk-jaw loot remains on eligible Slayer versions.
- [ ] **[The Curse of Arrav](https://oldschool.runescape.wiki/w/The_Curse_of_Arrav/Quick_guide) — golem guard and [Arrav](https://oldschool.runescape.wiki/w/Arrav).** Implement Mahjarrat vault/tomb item and puzzle route, owned guard encounter, then Arrav's phases, undead mechanics and non-final narrative outcome. Preserve canopic/heart/shield or other linked quest items exactly as the pinned guide specifies; neither quest actor uses repeatable loot.
- [ ] **[Defender of Varrock](https://oldschool.runescape.wiki/w/Defender_of_Varrock/Quick_guide) — armoured-zombie defence.** Require at least six quest-version armoured-zombie kills, run chaos-golem waves/assault objectives and ally/civilian state, and bind the shield/key/teleorb/Varrock Palace interactions. Quest versions must not award the post-quest [zombie axe](https://oldschool.runescape.wiki/w/Zombie_axe) table unless the Wiki explicitly assigns it to that ID/version.
- [ ] **[Sins of the Father](https://oldschool.runescape.wiki/w/Sins_of_the_Father/Quick_guide) — trials and [Vanstrom](https://oldschool.runescape.wiki/w/Vanstrom_Klause).** Implement every Kroy/nail-beast/Juvinate/bloodveld/Damien/Juvenile stage and ally state, then require the [blisterwood flail](https://oldschool.runescape.wiki/w/Blisterwood_flail) for Vanstrom. Build both phases with darkness facing, blood-orb/bloodveld adds, lightning safe tiles, prayer/stat effects, exact death bank/gravestone and no-loot outcome. Preserve flail, medallion, lab and Icyene Graveyard item/cutscene/reclaim paths.
- [ ] **[A Kingdom Divided](https://oldschool.runescape.wiki/w/A_Kingdom_Divided/Quick_guide) — five staged fights.** Follow the existing [quest audit](../quests/a_kingdom_divided.md): Judge of Yama, two assassins, Lizardman brute, Xamphur and Barbarian Warlord each need the authored trigger, attacks, arena and no-loot state. Bind every council clue, diary/book/page/key/artefact, resurrection spellbook and final Book of the Dead reward/reclaim; generic versions elsewhere cannot satisfy a stage.
- [ ] **[A Porcine of Interest](https://oldschool.runescape.wiki/w/A_Porcine_of_Interest/Quick_guide) — [Sourhog](https://oldschool.runescape.wiki/w/Sourhog).** Follow the existing [quest audit](../quests/a_porcine_of_interest.md): suspicious-grocer route, poisoned food/rope/cave interactions, owned Sourhog attacks and safe failure, guaranteed report/progression and no post-quest sourhog drop table on the quest actor.
- [ ] **[Getting Ahead](https://oldschool.runescape.wiki/w/Getting_Ahead/Quick_guide) — [Headless Beast](https://oldschool.runescape.wiki/w/Headless_Beast).** Implement the tracking/lure/repair item sequence and owned beast fight, then make the severed/recovered head and taxidermy/quest reward interactions explicit with inventory-full/reclaim. Do not substitute an ordinary beast drop.
- [ ] **[Below Ice Mountain](https://oldschool.runescape.wiki/w/Below_Ice_Mountain/Quick_guide) — [Ancient Guardian](https://oldschool.runescape.wiki/w/Ancient_Guardian).** Follow the existing [quest audit](../quests/below_ice_mountain.md): preserve the optional combat route versus boostable 10 Mining/pickaxe pillar route, guardian attacks, pillar HP/mining, shared completion state and no loot. Implement all ruins clues, barronite/deposit items and Camdozaal unlock without forcing the kill.
- [ ] **[A Night at the Theatre](https://oldschool.runescape.wiki/w/A_Night_at_the_Theatre/Quick_guide) — quest fights and raid.** Follow the complete [quest audit](../quests/a_night_at_the_theatre.md): flail/saw crypt Vyrewatch and key, avoidable araxytes/acid/egg sac, quest Hespori/flowers/bark and all six Theatre rooms through Verzik. Preserve prior-kill skip, party/Entry Mode retry, raid supplies/Dawnbringer, raid loot versus quest lamps, item retrieval and owner/party lifecycle.
- [ ] **[Beneath Cursed Sands](https://oldschool.runescape.wiki/w/Beneath_Cursed_Sands/Quick_guide) — guard, mages, [Champion](https://oldschool.runescape.wiki/w/Champion_of_Scabaras), Akh.** Follow the existing [quest audit](../quests/beneath_cursed_sands.md): no-protection-prayer Head Guard, two Scarab Mages, Champion shadow/charge/plague mechanics and Menaphite Akh finale. Bind the Keris→[Keris partisan](https://oldschool.runescape.wiki/w/Keris_partisan), tablet, mirrors, moulds, sigils and tomb interactions; quest actors drop only authored items/state.
- [ ] **[The Path of Glouphrie](https://oldschool.runescape.wiki/w/The_Path_of_Glouphrie/Quick_guide) — Warped Terrorbirds.** Require the [crystal chime](https://oldschool.runescape.wiki/w/Crystal_chime) to make each of three quest terrorbirds vulnerable, implement their ranged/magic/melee transformations and owned kill count, then the one-HP Evil Creature/cutscene. Preserve machine/disc puzzle, crystal saw/chime charges/reclaim and no ordinary warped-creature loot.
- [ ] **[Desert Treasure II](https://oldschool.runescape.wiki/w/Desert_Treasure_II_-_The_Fallen_Empire/Quick_guide) — four paths and finale.** Implement every section-2 combatant, with separate owned instances for [Vardorvis](https://oldschool.runescape.wiki/w/Vardorvis), [Leviathan](https://oldschool.runescape.wiki/w/The_Leviathan), [Duke Sucellus](https://oldschool.runescape.wiki/w/Duke_Sucellus), and [Whisperer](https://oldschool.runescape.wiki/w/The_Whisperer), including axes/roots, shadow barrage/stun, potion ingredients/eye vents, sanity/tentacles/ghost realm and exact quest-version stats. Implement Kasonde/Jhallan, Ancient Guardian, Mysterious Figure and the four wight/finale encounters in order. Bind medallion/icons/keys/tablets/ancient sight and every path item/reclaim; quest versions have no post-quest unique tables, awakened variants or vestige rolls.
- [ ] **[Secrets of the North](https://oldschool.runescape.wiki/w/Secrets_of_the_North/Quick_guide) — Evelot, Assassin, Strange Creature.** Bind the murder investigation, key/lock/crevice/ancient icon route to three owned fights, implement the Assassin's smoke/shadow attacks and Strange Creature's quest-only Muspah mechanics, then create/reward the [ancient icon](https://oldschool.runescape.wiki/w/Ancient_icon) through the authored state. No post-quest Phantom Muspah loot applies.
- [ ] **[Perilous Moons](https://oldschool.runescape.wiki/w/Perilous_Moons/Quick_guide) — three [Moons of Peril](https://oldschool.runescape.wiki/w/Moons_of_Peril).** Implement dungeon supplies (grubs, lizards, fish, potions), Sulphur Nagua gate and Blue/Blood/Eclipse rooms with all weapon-style rules, parry/clones, healing, jaguars, weapon disable, tornado/orb/line specials and room reset. Quest completion and repeatable chest loot are separate: the first full cycle must advance the quest, while subsequent cycles use exact contribution/unique tables and duplicate protection.
- [ ] **[The Ribbiting Tale](https://oldschool.runescape.wiki/w/The_Ribbiting_Tale_of_a_Lily_Pad_Labour_Dispute/Quick_guide) — [Cuthbert](https://oldschool.runescape.wiki/w/Cuthbert).** Implement the authored joke interaction, single-damage/animation outcome and cutscene rather than generic boss combat. Preserve gnomeball/lily-pad/tool item actions, no loot and exact post-fight dialogue.
- [ ] **[Twilight's Promise](https://oldschool.runescape.wiki/w/Twilight%27s_Promise/Quick_guide) — knight/cultists.** Implement the owned Knight of Varlamore duel and eight cultist wave/count at their temple/infiltration stages, emissary disguise/items and cutscenes, no-loot actors and safe reset. Do not share progress with ordinary Varlamore knights/cultists.
- [ ] **[The Heart of Darkness](https://oldschool.runescape.wiki/w/The_Heart_of_Darkness/Quick_guide) — emissaries, Prince and [Amoxliatl](https://oldschool.runescape.wiki/w/Amoxliatl).** Implement brawler/conjurer waves, Prince Itzla Arkan and Amoxliatl's ice/minion/special mechanics in owned spaces, plus every temple key/puzzle/disguise/quest item and no-loot transition. Post-quest Amoxliatl's repeatable drop table is a separate version/row and must not leak into the quest kill.
- [ ] **[Death on the Isle](https://oldschool.runescape.wiki/w/Death_on_the_Isle/Quick_guide) — Adala and Naiatli.** Mark `blocked-cache-version` until assets exist. Then implement item confiscation/uniform disguise, all investigation/pickpocket clue items, the non-lethal unequipped Adala accusation fight, Naiatli chase/fight and evidence dialogue. Return held items exactly, make both fights non-looting, and keep costume needle/chest key/icon/pendant post-quest rewards outside combat.
- [ ] **[Meat and Greet](https://oldschool.runescape.wiki/w/Meat_and_Greet/Quick_guide) — Dire Wolf Alpha and Minotaur.** Implement hunt/tracking/carcass and arena triggers, each owned NPC's specials and no-loot quest credit, all meat/cooking or bait items and full-inventory/reclaim, then advance only from the authored NPC dialogue/cutscene.

### 4.5 Sailing-era encounters beyond revision 239

Every row in this subsection begins `blocked-cache-version`. Research and
acceptance still use the live Wiki; implementation starts only after an
approved cache/content revision supplies the symbolic assets.

- [ ] **[The Blood Moon Rises](https://oldschool.runescape.wiki/w/The_Blood_Moon_Rises/Quick_guide).** Inventory vyre noble outfit, Ivandis/blisterwood flail and 40,000-coin replacement, food given to Ivan, pickaxe/blockage, six named library books, Ivandis' writings and every later quest item from the guide/transcript. Implement Ivan-defence waves (Vyrewatch/sentinels/acidic bloodvelds), monastery/tunnel fights and blood orbs, Sanguidae/crows/Venator/ancient-vyre/Nylocas encounters, Maiden/Vasilias, [Wyrd](https://oldschool.runescape.wiki/w/Wyrd), and four distinct [Lowerniel Drakan](https://oldschool.runescape.wiki/w/Lowerniel_Drakan) fights through level 1063, including ally HP, healing, phases, wipes, death locations and no raid/post-quest loot leakage.
- [ ] **[The Final Dawn](https://oldschool.runescape.wiki/w/The_Final_Dawn/Quick_guide).** Inventory emissary robes/scroll, room key, canvas, dog food/bones or meat, potatoes/sack, knife, coin purse/sand/branch/makeshift blackjack, beverages and every later item. Implement Enforcer with locked gear/Prayer/teleport rules and directional specials, Chimalli/Lucius duo, cultist waves, Ennius Tullus and Augur Metzli with exact instances, safe tiles, deaths and no-loot state; the quest guide/transcript owns every infiltration, puzzle, cutscene and reclaim branch.
- [ ] **[Prying Times](https://oldschool.runescape.wiki/w/Prying_Times/Quick_guide).** Inventory Captain's log, cargo crate, crowbar, sealed crate and fish-bladder stout; implement boat cargo transfer and the spawned Drink Troll as a skippable disembark-or-kill encounter with no quest loot. Opening the crate and Steve dialogue, not the kill, completes the quest.
- [ ] **[Troubled Tortugans](https://oldschool.runescape.wiki/w/Troubled_Tortugans/Quick_guide).** Inventory seaweed/palm-leaf bandage, saw, hammer, six scutes, ten Jatoba logs, six shells, axe and [Tortugan shield](https://oldschool.runescape.wiki/w/Tortugan_shield). Implement footprint/object tracking and Gryphon, then force the cape-slot shield for [Shellbane gryphon](https://oldschool.runescape.wiki/w/Shellbane_gryphon_(Troubled_Tortugans)), including Little Pearl defence, specials, death/re-entry and exact quest/no-repeat loot separation.
- [ ] **[The Red Reef](https://oldschool.runescape.wiki/w/The_Red_Reef/Quick_guide).** Implement the two owned ship battles with cannon-or-ranged/magic crew alternatives, ship HP/cargo/lost-at-sea rules, six-pirate checkpoint, Black Eye Bethel's charged attack and dragon-scimitar prayer disable, then diving apparatus/fishbowl/Medallion of the Deep and giant-lobster blue-tile special before repairing the coral dredger. Track every named pirate version, reset boundary and no-loot quest actor exactly.
- [ ] **[Shadows of Custodia](https://oldschool.runescape.wiki/w/Shadows_of_Custodia/Quick_guide).** Inventory fishing rod/cloth, four willow longbows, four maple logs and hammer; implement trail inspections, barricade repairs and three Strange Creatures with melee plus bleed in the Antos encounter. They have no special loot; Antos/Captain dialogue advances and unlocks the Stalker Den.
- [ ] **[Scrambled!](https://oldschool.runescape.wiki/w/Scrambled%21/Quick_guide).** Inventory/implement hammer, Acatzin's axe and whetstone repairs, bowl/water/damiana leaves/tea/cup, two planks, twenty iron nails and saw/cart repairs. Searching each nest spawns one owned large chicken, black jaguar or red dragon; post-kill Search grants the matching large/jaguar/dragon egg, not the NPC drop. Enforce dragonfire protection and complete the egg-assembly interface/cutscene.
- [ ] **[Learning the Ropes](https://oldschool.runescape.wiki/w/Learning_the_Ropes) — giant rats.** Inventory the tutorial's supplied equipment and exact two-rat kill state from the article/transcript; ensure tutorial death protection, no special loot and no progress from outside rats.
- [ ] **[The Ides of Milk](https://oldschool.runescape.wiki/w/The_Ides_of_Milk/Quick_guide) — [Brutus](https://oldschool.runescape.wiki/w/Brutus).** Inventory The Groats Principles and two milk-sample states; opening the field gate starts the owned bull. Normal attacks cannot kill during the quest, special attacks hit up to 19 and ignore prayer, and snort/growl telegraphs require the exact sidestep/backstep rules. No loot; Gillie/Cassius dialogue advances and post-quest Gillie rewards remain separate.
- [ ] **[Fallen From Grace](https://oldschool.runescape.wiki/w/Fallen_From_Grace/Quick_guide) — trolls and [Mad Angel](https://oldschool.runescape.wiki/w/Mad_Angel).** Inventory pickaxe/hammer/chisel, sunstone core, notebook, large-hat clue, ancient core/key and body note; require an eligible skiff/raft rather than a sloop. Count the three nearest mountain trolls, then implement Mad Angel's melee wind-up, bouncing spiky ball, precisely timed Magic protection and faster enraged triple lightning. Searching the body grants/replaces the note; the boss itself has no repeatable loot.

### 4.6 Remaining conditional and ordinary-combat rows

These are still per-encounter TODOs. Their shorter entries mean they use
ordinary combat, not that item/drop/state behavior may be skipped.

- [ ] **[Biohazard](https://oldschool.runescape.wiki/w/Biohazard/Quick_guide) — Mourner.** Tie the level-13 Mourner to the correct plague-house/lab route, preserve the key/item access alternative and normal human drops only if the exact NPC version has them, and ensure killing an unrelated Mourner cannot advance the quest. Elena's samples, liquid containers, disguises and Guidor hand-in own progression; see the existing [quest audit](../quests/biohazard.md).
- [ ] **[Eagles' Peak](https://oldschool.runescape.wiki/w/Eagles%27_Peak/Quick_guide) — optional kebbit.** Preserve the no-kill trapping/catching path and ordinary kebbit loot; if killed, do not write quest state. The bronze feather, cave puzzle items, eagle disguise and Nickolaus interactions—not combat—advance the quest.
- [ ] **[Devious Minds](https://oldschool.runescape.wiki/w/Devious_Minds/Quick_guide) — conditional Abyssal creatures.** Keep large-pouch acquisition source-neutral: exact ordinary Abyssal drops are valid, Guardians of the Rift/banked pouches bypass combat, and no kill writes quest state. Implement mithril 2h→slender blade→bow-sword, bow string, glowing pouch, altar/monk and loss/reclaim interactions separately.
- [ ] **[The Ascent of Arceuus](https://oldschool.runescape.wiki/w/The_Ascent_of_Arceuus/Quick_guide) — ordinary souls.** Resolve all five Tormented Soul spawn versions and one Trapped Soul, enforce the exact location/count, and make any quest-item drop/state owner-only; no outside soul qualifies.
- [ ] **[The Path of Glouphrie](https://oldschool.runescape.wiki/w/The_Path_of_Glouphrie/Quick_guide) — Evil Creature cleanup.** After the three chime-gated Terrorbirds, spawn and credit the one-HP Evil Creature only in the authored cutscene/finale; it has no loot and cannot be a generic combat shortcut.
- [ ] **[Defender of Varrock](https://oldschool.runescape.wiki/w/Defender_of_Varrock/Quick_guide) — kill quotas.** Record the exact six qualifying armoured-zombie IDs/locations and every chaos-golem wave version in the encounter manifest; test count persistence, duplicate credit, ally kills and no post-quest table leakage.
- [ ] **[Learning the Ropes](https://oldschool.runescape.wiki/w/Learning_the_Ropes) — tutorial quota.** Record both tutorial-rat versions, equipment supplied/returned, kill-credit and non-lethal/tutorial death rules. Never use world rats to satisfy the tutorial.

### 4.7 Miniquest, replay, and post-quest encounter TODOs

- [ ] **[Curse of the Empty Lord](https://oldschool.runescape.wiki/w/Curse_of_the_Empty_Lord/Quick_guide).** Keep Troll General combat conditional on the chosen ghostly-robes route, use normal key/drop behavior only for the exact version, and make all ghostly robe pieces, ring-of-visibility and dialogue checkpoints independently reclaimable.
- [ ] **[The Enchanted Key](https://oldschool.runescape.wiki/w/The_Enchanted_Key/Quick_guide).** Reuse the correct Temple Guardian encounter without replaying Priest in Peril state or rewards; enchanted-key heat/scans, locations, time travel and artefact rewards own miniquest progress.
- [ ] **[Family Pest](https://oldschool.runescape.wiki/w/Family_Pest/Quick_guide).** Reuse a clearly versioned Chronozon contract without mutating Family Crest, then implement the three gauntlet recolour/fee choices and no duplicate crest-piece drops.
- [ ] **[The General's Shadow](https://oldschool.runescape.wiki/w/The_General%27s_Shadow/Quick_guide) — [Bouncer (ghost)](https://oldschool.runescape.wiki/w/Bouncer_(ghost)).** Own the ghost fight, bind shadow-sword/general clue items and no-loot death, and keep it independent of Fight Arena Bouncer and Nightmare Zone.
- [ ] **[Hopespear's Will](https://oldschool.runescape.wiki/w/Hopespear%27s_Will/Quick_guide) — Strongbones.** Use the miniquest's Strongbones version and exact Prayer/ancestral-bones requirement, grant the authored prayer XP/state rather than Land-of-the-Goblins drops, and protect against replay duplication.
- [ ] **[In Search of Knowledge](https://oldschool.runescape.wiki/w/In_Search_of_Knowledge/Quick_guide) — Undead Druid.** Implement exact pages/tomes as conditional drops across the listed Forthos monsters and the level-105 druid encounter; preserve duplicate-page and book assembly/hand-in behavior rather than giving completion on death.
- [ ] **[Lair of Tarn Razorlor](https://oldschool.runescape.wiki/w/Lair_of_Tarn_Razorlor/Quick_guide) — [Tarn](https://oldschool.runescape.wiki/w/Tarn_Razorlor).** Implement the full trap/maze/agility/armour/statue route, Tarn's two forms and terror dogs, diary drop/reclaim and salve-amulet enchantment. Treus Dayth's listing reflects the Haunted Mine prerequisite, not a second Treus fight inside Tarn's lair.
- [ ] **[Mage Arena I](https://oldschool.runescape.wiki/w/Mage_Arena_I/Quick_guide) — [Kolodion](https://oldschool.runescape.wiki/w/Kolodion).** Build all five consecutive forms with correct transformations, attacks, Wilderness arena/unsafe-death rules and no ordinary loot; then implement cape/staff choice, god spell unlock and replacement independently.
- [ ] **[Mage Arena II](https://oldschool.runescape.wiki/w/Mage_Arena_II/Quick_guide).** Implement enchanted-symbol tracking and Wilderness versions of Porazdir, Justiciar Zachariah and Derwen, exact special attacks/prayer interaction, guaranteed demon/justiciar components, death/loss/reclaim and three-god component hand-in. Wilderness PvP/teleblock and loot ownership must remain live.
- [ ] **[His Faithful Servants](https://oldschool.runescape.wiki/w/His_Faithful_Servants/Quick_guide).** Require all six Barrows brothers with the miniquest's count/state, preserve crypt/tunnel/chest logic and ordinary Barrows rewards only from the chest, then grant the miniquest prayer reward once. Individual brother deaths never drop their armour directly.
- [ ] **[Into the Tombs](https://oldschool.runescape.wiki/w/Into_the_Tombs/Quick_guide).** Require a complete Tombs of Amascut run through all path bosses and Wardens, with party/invocation/death/reward rules and miniquest dialogue state. Raid loot remains the raid's; miniquest completion must not add or replace a roll.
- [ ] **[The Frozen Door](https://oldschool.runescape.wiki/w/The_Frozen_Door/Quick_guide).** Use the four original GWD generals and conditional frozen-key-piece rolls, then assemble/use the key and unlock the Ancient Prison. Full combat, loot and tests are owned by [God Wars plan](../GOD_WARS_PLAN.md); this row tests only miniquest eligibility, pieces, assembly, loss/reclaim and door state.
- [ ] **Nightmare Zone replay variants.** For every eligible quest boss in [Nightmare Zone's boss table](https://oldschool.runescape.wiki/w/Nightmare_Zone#Bosses), record normal/hard stats, enabled-quest set, special-item dispenser interactions, point/absorption behavior and mechanics deliberately altered or disabled in NMZ. Never route NMZ deaths into quest progress, quest-item drops, quest cutscenes or ordinary loot.
- [ ] **Repeatable post-quest variants.** Create separate manifests for post-quest Vorkath, DT2 bosses, Phantom Muspah, Amoxliatl, Moons, Tormented Demons and any other replay unlocked by a row above. Their stats, death fees, kill counts, combat achievements, collection log and full loot tables are not accepted by merely completing the no-loot quest version.

## 5. Repository integration plan

### 5.1 Existing surfaces to build on

The implementation belongs primarily under
`OSRS-Content/osrs239-content/server/scripts/`, not in quest-specific C:

| Surface | Responsibility in this plan |
| --- | --- |
| `skill_combat/combat.rs2`, `skill_combat/combat_stats.rs2`, `skill_combat/npc_combat.rs2` | Shared attack dispatch, damage, default AI/death and the current single `death_drop`; boss scripts must override this where mechanics or drops differ |
| `skill_combat/scripts/{npc_combat_magic,npc_combat_ranged,dragonfire,poison,venom,disease,projectile}.rs2` | Shared style/status/projectile primitives, extended only for reusable capability |
| `player/death.rs2` and `interface_equipment/scripts/deathkeep.rs2` | Gravestone, unsafe-death and retained-item integration; add a reusable item-retrieval service rather than bespoke item deletion |
| `quests/quest_*/scripts` and `configs` | Quest state, start/re-talk, item interactions, boss orchestration, journal and completion; one quest/series owns each quest-version encounter |
| `bosses/boss_tormented_demons/` | Example of a dedicated multi-mechanic, separately dropped post-quest NPC; quest TDs remain distinct |
| `minigames/minigame_tob/` and future raid roots | Party/room/wipe/reward ownership for raid-gated quest rows |
| `areas/world/configs/*.spawn` and quest map/loc configs | Public/static spawns only where OSRS is public; owned/instanced bosses must not be global static actors |
| `interface_questjournal/scripts/quest_journal.rs2` and shared quest completion | Journal and idempotent reward lifecycle after an encounter advances its quest |
| [Quest modernization plan](../QUEST_MODERNIZATION_PLAN.md) | Governing per-quest discovery, engine migration, narrative and verification gates |

The current tree contains useful encounter fragments—such as `quest_dragon`,
`quest_arena`, `quest_troll_love`, `quest_grail`, `quest_zanaris`,
`quest_contact` and the partial Theatre—but their existence is not completion
evidence. Audit them against sections 3 and 4. The wildcard combat/death path
and a `death_drop` object are insufficient for multi-roll loot, non-lethal
victory, item-gated damage, phases, adds, owner credit or instances.

### 5.2 Shared capabilities to implement before mass porting

- [ ] **Generated encounter manifest.** Add a checked-in machine-readable
  ledger keyed by quest dbrow + encounter + cache gameval. Include Wiki
  revision, quest state, trigger, NPC versions, stats, attacks, items, drops,
  ownership model, death model, replay variant and test IDs. Generate section-2
  coverage from the pinned Wiki snapshot and fail CI when a combat-bearing
  quest or independently completed RFD subquest lacks a ledger row.
- [ ] **Owner/party encounter service.** Provide RuneScript-expressible owned
  NPC groups, phase state, authoritative kill/survival credit, spawn/transform,
  leave/logout/death callbacks and idempotent teardown. Add a real dynamic map
  instance service where static coordinates cannot isolate players/parties.
- [ ] **Boss combat primitives.** Add reusable timed specials, target selection,
  threshold transitions, add caps, ground hazards, safe-tile telegraphs, forced
  movement, damage caps/floors, immunities/attributes, style-gated damage,
  non-lethal defeat and NPC-vs-NPC/companion combat. Each primitive gets tick
  tests before a quest consumes it.
- [ ] **Loot service.** Replace one-object assumptions with named guaranteed,
  regular, unique and independent tertiary tables, condition predicates,
  quantities/noted state, owner/contribution visibility, ground fallback,
  collection log/kill count and explicit no-loot mode. Quest-item grants remain
  atomic with quest state and do not masquerade as repeatable drops.
- [ ] **Quest item/reclaim service.** Standardize inventory-or-ground grants,
  bank-aware duplicate checks, charge/item-var state, destroy text, NPC/loc
  replacement and reward-reclaim shops. Add table-driven invariant tests for
  every item named in section 4.
- [ ] **Death and item retrieval.** Implement public safe/unsafe death,
  gravestone relocation outside instances, encounter death banks, fee/expiry,
  simultaneous kill/death and reconnect/restart recovery. Boss-specific code
  declares policy; it does not manually delete the player's inventory.
- [ ] **Presentation/tick tracing.** Expose deterministic test hooks for
  animation/projectile/spotanim/sound/forced-movement and damage tick traces,
  with cache-symbol validation. The real client remains the visual authority.
- [ ] **Companion and ship combat.** Implement follower-owned combat for
  Ratcatchers/quest allies and ship/crew HP, cannons, boarding, cargo, sinking,
  leave/reset and contribution for Sailing quests before those rows begin.

Engine work is allowed only when the current VM/protocol cannot express a
general capability. Keep policy, Wiki IDs/state, attacks, items and drops in
RuneScript/config. Follow `CLAUDE.md`: invalid internal contracts assert; do
not turn missing owners, assets or encounter state into silent no-ops.

## 6. Dependency-ordered delivery plan

### Phase 0 — freeze the roster and make omissions fail the build

1. Generate the encounter manifest described in section 5.2 from the pinned
   Wiki source, current quest dbrows, quest roots and cache configs.
2. Add checks for: every section-2/miniquest row represented; every NPC gameval
   resolvable; every quest item resolvable; one combat/death owner per quest NPC;
   no quest actor falling into an undisclosed generic drop; and no raw numeric
   cache IDs.
3. Record current status per encounter as `absent`, `scaffold`, `partial`,
   `audit-pending`, `in-progress`, `blocked-engine`, `blocked-cache-version` or
   `verified-modern`. Never infer `verified` from a file/directory name.

### Phase 1 — shared lifecycle and three proving encounters

1. Implement owner/party lifecycle, non-lethal outcomes, item-gated damage,
   multi-table/no-loot death, reclaim and tick tracing.
2. Prove them on three small but different fights: Delrith (special item plus
   incantation), Witch's experiment (multi-form actor), and Fight Arena
   (multi-round owned gauntlet plus optional boss).
3. Run relog/death/full-inventory/concurrent-player tests before exposing the
   APIs to the rest of the quest tree.

### Phase 2 — finish the partial quest roots first

Use the partial-root order from the governing quest plan, because these already
block complete playable chains:

1. Scorpion Catcher, Fight Arena, Family Crest, Tourist Trap, Lost City.
2. Big Chompy Bird Hunting, Elemental Workshop I, Horror from the Deep, Shades
   of Mort'ton, Temple of Ikov.
3. Troll Stronghold, In Search of the Myreque, Shilo Village, Tribal Totem
   (non-combat, but required to close the partial-root set).

Each quest lands only after its complete route, not only its combat section,
passes Gates A–D and its encounter rows pass section 3.

### Phase 3 — free-to-play and early prerequisite chains

Implement in dependency order so later bosses reuse verified items and world
state:

1. Witch's Potion, Imp Catcher optional drops, Restless Ghost avoidance,
   Shield of Arrav branches, Prince Ali/Pirate optional fights.
2. Demon Slayer/Silverlight, Vampyre Slayer/staking and Below Ice Mountain's
   kill-or-mine choice.
3. Dragon Slayer I including the maze drop chain, Elvarg and head turn-in.
4. Druidic Ritual, Priest in Peril and Nature Spirit, which unlock many later
   Morytania bosses.

### Phase 4 — classic series and reusable restrictions

Run independent branches in parallel only after shared APIs are stable; within
each branch preserve this order:

1. **Gnome/elf:** Tree Gnome Village → Grand Tree → Monkey Madness I → Regicide
   / Roving Elves → Mourning's End → Song of the Elves; Path of Glouphrie and
   Monkey Madness II after their prerequisites.
2. **Fremennik/troll:** Fremennik Trials → Troll Stronghold/Romance → Fremennik
   Isles → Olaf/My Arm/Making Friends → Fremennik Exiles.
3. **Desert/Mahjarrat:** Tourist Trap/Feud/Icthlarin → Desert Treasure I →
   Wanted/What Lies Below → While Guthix Sleeps/Defender/Curse of Arrav →
   Desert Treasure II; Contact and Beneath Cursed Sands before Tombs content.
4. **Myreque:** In Search → In Aid → Darkness encounter → A Taste of Hope →
   Sins of the Father → A Night at the Theatre → Blood Moon Rises after the
   cache upgrade.
5. **Sea slug/pirate:** Ghosts Ahoy/Rum Deal/Cabin Fever → Great Brain Robbery;
   Slug Menace and Swan Song/Royal Trouble on their prerequisite branches.
6. **Fairy/Lunar:** Fairytale I/II and Lost City → Lunar Diplomacy → Dream
   Mentor.
7. **Standalone classic:** Holy Grail, Watchtower, Legends, Haunted Mine,
   Underground Pass, Mountain Daughter, Between a Rock, One Small Favour,
   Zogre, Spirits of the Elid, Soul's Bane and Grim Tales.

### Phase 5 — multi-boss quest systems

1. Complete every combat-bearing RFD subquest, then the six-round finale and
   one aggregate fresh-account regression.
2. Complete Dragon Slayer II, Monkey Madness II, Song of the Elves, While
   Guthix Sleeps and DT2 with one owned-instance/phase test suite per fight and
   one whole-quest resource/cleanup soak.
3. Complete Theatre Entry Mode and Tombs once as real party/raid systems; quest
   progress consumes the authoritative full-raid result, never a boss-room
   shortcut.
4. Complete Perilous Moons with first-cycle quest state separated from
   repeatable reward-chest state.

### Phase 6 — OSRS-original standalone encounters

Process by prerequisite depth and audit availability: Ascent of Arceuus,
Depths of Despair, Corsair Curse, Tale of the Righteous, Porcine, Getting Ahead,
Ribbiting Tale, Twilight's Promise, Heart of Darkness, Meat and Greet and all
ordinary/conditional combat rows in section 4.6. Close each quest's full
narrative/item route in the same change as its encounter; do not build a boss
island that cannot be reached legitimately.

### Phase 7 — miniquests, replay variants and system regression

1. Implement section 4.7 miniquests in prerequisite order.
2. Add Nightmare Zone variants only after the source quest encounter is
   verified; test that replay cannot mutate quest state or emit quest items.
3. Add repeatable post-quest bosses and loot as separate rows/build targets.
4. Run every combat-bearing quest from a clean account in topological order,
   verify quest-point/unlock totals, then fuzz death/logout/reconnect at every
   encounter stage.

### Phase 8 — cache upgrade and Sailing-era quests

1. Select and document the new cache revision; import maps/configs/interfaces/
   audio without overwriting revision-239 names blindly.
2. Regenerate the encounter/cache crosswalk and resolve every previously
   blocked gameval/item/loc/varbit before writing combat.
3. Implement ship/companion systems, then Death on the Isle, Prying Times,
   Troubled Tortugans, Red Reef, Shadows of Custodia, Scrambled!, Learning the
   Ropes, Ides of Milk, Fallen From Grace, Final Dawn and Blood Moon Rises in
   prerequisite order.
4. Re-run the entire pre-upgrade quest-boss suite against the upgraded cache to
   prove symbolic mappings and legacy encounters did not regress.

## 7. Per-change workflow and completion evidence

For each unchecked encounter or inseparable encounter group:

1. **Audit:** create/update the per-quest audit with pinned Wiki revisions,
   state table, complete NPC/item/loc/audio manifest, current code surface and
   all known differences.
2. **Design:** choose public versus owned/instanced lifecycle, declare every
   transition/failure/death/replay path, and identify reusable capability work
   which must land first.
3. **Implement:** add symbolic config, RuneScript, quest journal/re-talk,
   item/reclaim, combat, presentation, loot/no-loot and cleanup. Keep unrelated
   dirty-worktree changes untouched.
4. **Verify:** run compile/pack, deterministic mechanics, item invariants,
   statistical drops where applicable, real-client smoke and multiplayer soak.
5. **Record:** attach commands/results, packet/tick traces, screenshots for
   interfaces/telegraphs, exact Wiki revisions and any non-critical deviation;
   change status to `verified-modern` only when no required work remains.

The final program is complete only when every row in sections 2 and 4 has a
resolved manifest and verification record, every section-3 checkbox passes for
the applicable encounter, every post-quest/replay variant is either implemented
or explicitly out of product scope, and the full topological clean-account run
passes without debug state injection.
