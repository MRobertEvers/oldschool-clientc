Tai Bwo Wannai Trio -- driving notes (source: LostCity quest_tbwt, ported)
Monkeys: spawns at 2755,3170 / 2758,3178 (Tai Bwo Wannai jungle). While %tbwt_main is 3..5 melee
 (weapon range 1) is dodged with "The monkey deftly avoids your melee attack"; use ranged/magic/
 a reach weapon (spear) for the corpse. The monkey does not retaliate during the quest
 (tbwt_monkey.rs2 [ai_queue1,monkey]). A worn greegree blocks Attack entirely.
 A driver Attack row on a dodging monkey ends in "no hit landed": that is the expected dodge.
Brothers: Tamayu jungle form east (2844,3042 multinpc), Tiadeche shore (2912,3118), Tinsay island
 (2764,2976), Lubufu 2770,3169, Timfraku 2780,3087; each has a house multinpc form that only
 appears after the reward.
Bamboo door: tbwt_bamboo_door opens with op1, closes with op2 (tbwt_quest.rs2 oploc2).
Tamayu cutscene: dialogue "When will you succeed?" -> Yes starts it (tbwt_tamayu.rs2
 [label,tbwt_tamayu_cutscene]); the killing hunt needs an acceptable spear, a karambwan-paste
 spear handed over and 4 agility successes.
Pestle and mortar grinds the karambwan products via configs/tbwt_grind.dbrow.
Forms differing from the guide: grinding via dbrow table (not LostCity's attempt_grind), spear
 acceptance lists in quest_tbwt.rs2 proc tbwt_is_acceptable_tamayu_spear (OSRS wiki era).
Jingles (music_jingle) are omitted: the pack has no ~music_jingle proc.
