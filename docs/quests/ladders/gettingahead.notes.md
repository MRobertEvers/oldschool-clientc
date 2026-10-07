# Getting Ahead - driving notes (what the ladder cannot know)

Farmhouse (ground floor): front door canafis_door_ground (1245,3685) from the yard. Inside
you are in the east vestibule. Door (1242,3682) opens the west rooms: shelves
(1240,3688, reached from the WEST tile 1239,3688), Mounted Head Space (1240,3683),
Mary, sink, knife, bucket. Door (1237,3683) leads on west. Doors toggle: a second
click closes them.
Stairs (1239,3685): climb from the SOUTH tile 1240,3684 only (east is walled).
Up lands 1242,3685,1; down lands 1240,3684,0.
Upstairs: from the landing open doors (1243,3682), (1243,3681), (1240,3681) in that
order. Needle 1244,3685 and thread 1244,3684 are beside the landing. The empty pot
is at 1239,3681 (moved from 1239,3682, a blocked sack-pile tile: Take said
"I can't reach that!") and the flour barrel at 1238,3677 (stand 1238,3678).
Flour on the gate: use the pot on ga_fencegate_l (the shell; it resolves to
ga_fencegate_l_normal at state 4). Writes 8 then 10 (trail). Pot comes back empty.
The gates have no Open handler (only the flour use).
Hammer 1259,3686: take it from 1259,3684 (south side); a trough hides it from
other poses. Saw 1239,3696 and nails (workbench 1239,3698) are in the north building:
stand at 1239,3697. Planks: two at 1202,3649 and 1203,3652.
Cave entrance (ga_cave, 1211,3646) is reached from the NORTH (1212,3650); the
south side is cliff. Yes/No prompt. Lands 1190,10026 (static lair, not instanced).
Cave exit (ga_cave_out) lands at 1212,3648, the opening in front of the mouth.
Beast (level 82, 100 hp): aggressive, crush melee up to ~10 every 4 ticks; one
blow in six it stamps and drops a rock on your tile 3 ticks later (12-16) -
step off. Out of its reach (pond) it cannot stamp. Bring 99 combat-ish gear
and food. Drops big bones (+ long/curved bone rarely). Kill writes 14.
Skeleton (1187,10018) gives Neilan's journal once; Read shows two pages.
Knife-on-soft-clay: bind is [opheldu,softclay]; use_item_on_item("knife",
"softclay"). Wetting clay: use_item_on_item("clay","bucket_water") (order
matters). After the sink fill the player is busy ~4 ticks: wait before using.
Fur: any of fur / grey_wolf_fur / werewolve_fur, plus needle and thread (thread
is consumed). Bear fur drops from brownbear west of the farm (1219,3679).
Gordon dialogue gates: talk with the head in the pack at 20, 24, 28; he
advances only then. Build needs Construction 26 (boosted ok), hammer, poh_saw,
bloody head, 2 woodplank, 6 nails of any grade; Yes/No; no Construction XP.
Differences from the guide: private instanced lair is a static shared lair;
overnight scene is a 6-tick wait with messages; final cutscene absent (spec
pending); Mary Trade/tannery, Sergeant topic, Gordon item-show branches left.
