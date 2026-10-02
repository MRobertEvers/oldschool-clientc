Bear Your Soul - what the ladder cannot know

Book: the Soul journey sits on ONE rotating shelf in ~24 (category 907, any Arceuus Library
bookshelf). Rule: (x*31 + z*17 + level*5 + map_clock/8000) % 24 = 0 (bearyoursoul.rs2
bys_shelf_holds_book). Other shelves say "You find nothing of interest."
Test aid: ::bysshelf teleports to the library and prints the x,z,level of the shelf holding it now.
Reading it: three mesboxes; the third writes stage 1.

Aretha: talk with "I've been reading your book"; the Yes/Not now choice. Yes writes stage 2.
Dig: spade Dig within 8 tiles of 1699,3794 (plane 0), stage 2 only. Gives the damaged bearer.
After completion the same dig reclaims a lost Soul bearer.

Key Master: no generated spawn existed. Spawned at 1310,1251 by quests/quest_bearyoursoul/configs/bearyoursoul.spawn.
Talk-to repairs (damaged bearer consumed, Soul bearer granted, stage 3).
His op1 is the label bys_keymaster_talk; cerberus.rs2 just jumps to it.

Taverley: the cave entrances (hellhound_cave_entrance_a/b/c) land in the Key Master lobby
(^cerberus_lobby). The dusty-key / Agility gate in Taverley is not checked here; test uses goto.

Not ported: Soul bearer charging, Fill, Check, Uncharge; Biblia hints; Key Master Listen; Key master teleport.
