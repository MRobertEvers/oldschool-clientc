# Tale of the Righteous (wiki-pinned brief)

Sources: https://oldschool.runescape.wiki/w/Tale_of_the_Righteous?oldid=15316635,
Transcript:Tale_of_the_Righteous oldid 14898483, Corrupt_Lizardman oldid 15200061,
Unstable_Altar oldid 15320666, Strange_Device_(Tale_of_the_Righteous) oldid 14770159,
Dusty_note oldid 15190210, Transcript:Archeio/Istoria/Pagida (oldids 14288961/14295190/14304573),
Quest Helper taleoftherighteous. LostCity has no such quest (2018).

Start: Phileas Rimor, house west of The Cloak and Stagger, Shayzien (1542,3570). Needs Client of Kourend,
X Marks the Spot, Strength 16, Mining 10 (not boostable). Bring a pickaxe, a rope, a melee weapon, a ranged
weapon with ammunition and runes for a combat spell, and gear for a level 46 Corrupt Lizardman.
Rewards: 1 quest point, 8,000 coins, Kharedst's memoirs page (History and hearsay), Graceful recolour.

Ladder (varbit 6358; the cache's multinpc/multiloc tables hide Phileas from 15 and Duffy's tent copy from 12,
show the roped crevice from 9 and the trashed house from 15):
0 Phileas (1) -> Archeio teleports to the Library Historical Archive -> Pagida's question about King
Shayzien VII, "Yes please" teleports to the Tower of Magic prison (2). Prison: push the strange device west,
attack it from the north with magic (SW crystal), from the south with melee (NW), push it east, attack from
the south with ranged (NE); all four white opens the north cell gate (4); investigate the skeleton (5).
Optional dusty note from the south-west skeleton.
5 Phileas (6) -> Lord Shiro upstairs in the War Tent (7) -> Historian Duffy at the peak (8) -> use a rope on the
crevice on the west face (9): enter, mine the rockfall, push the boulder, try the magic gate: a Corrupt
Lizardman appears; kill it (10); inspect the Unstable Altar (11, "Rickard! Turn away!") -> climb out, tell Duffy
(12) -> go back down: Duffy (13), Gnosi (14) -> Lord Shiro (15) -> Phileas's house is trashed and he is gone
(16) -> Lord Shiro pays 8,000 coins (17).

Port notes: the prison and the cave are per-player copies of map squares m24_159 and m18_155; the stage
numbers are Quest Helper's. Not ported: the freeze from attacking the device with an ice spell, the
lizardman's ranged style (see docs/bosses/quest_combat_manifest.json known_gaps).
