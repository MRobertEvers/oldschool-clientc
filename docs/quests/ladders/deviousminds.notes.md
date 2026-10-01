Devious Minds -- what the ladder cannot know

Dead monk: devious_monk_dead stands at 3405,3492 and the hooded monk at 3406,3492
(areas/world/configs/m53_54.spawn). Both are multinpc shells; clicking either from
3406,3494 works. Dead monk is a mesbox then one player line; stage 50 only.
Whetstone: use the mithril 2h on Doric's whetstone (2953,3452), answer Yes.
Bow string goes on the slender blade in the pack (item-on-item).

Abyss (Wilderness mage op4 at 3106,3556): you land on the OUTER ring. The passage
you click (the loc that resolves for your layout: teeth/tendrils/boil/eyes/agility)
carries you through 6-7 tiles of rock to the INNER ring; it may fail ("You fail to
get past"), click again. Needs pickaxe / axe / tinderbox for three of the kinds; keep
them in the pack. Fixed in skill_runecraft/scripts/runecraft_abyss.rs2
abyss_move_inward: nearest of the 12 shells picks a hand-picked inner tile (chosen
from maps/m47_75.jm2 f1 flags, BFS component of abyss_exit_to_law). The old
"5 tiles toward centre" landed inside rock.
Law rift abyss_exit_to_law is at 3049,4839; exit lawtemple_exit_portal (Law altar
2464,4817) puts you on Entrana at 2858,3378. Walk to the church 2851,3347.

Heist: use the illuminated pouch on devious_altar (oplocu, stage 30 only). A ~60 tick
cutscene runs (deviousminds_items.rs2:190+), then stage 40; large pouch is lost,
colossal survives. Talk to the High Priest (2851,3349): stages 40, 60 branch first
(areas/entrana/scripts/high_priest_of_entrana.rs2), his generic greeting follows.
Dialogue choices gating progress: monk "Yes." to start; Tiffy "Devious Minds."
Tiffy's stage-70 branch answers first in rd_teleporter_guy opnpc1
(quest_recruitmentdrive/scripts/recruitmentdrive.rs2:36), before RD/Wanted branches.
No fights. Abyssal creatures may attack in the outer ring; strip no gear needed here
(the outbound trip does not use Entrana's boat, it leaves the Law altar by the portal).

Stage 60 return to Entrana (guide talkToEntranaMonk + useGangPlank) is the ordinary
ferry, NOT a goto: goto 3045,3236 (shipmonk, Port Sarim), talk_to shipmonk op1,
chat "npc:Do you seek passage" / "choose:Yes, okay, I'm ready to go." / player / "npc:Very
well. One moment please." / "mesbox:The monk quickly searches you.". Since seam34
(parity3f_carry_overs) the crossing lands on the ship's DECK, 2834,3331 level 1 (LostCity
~set_sail ... 1_44_52_18_3); then click_loc ship_from_entrana_off op1 (the gangplank,
2834,3333 level 1) puts you on the pier at 2834,3335,0; goto the church after that.
Proof: build/quest_gate/p3f_dm_boat_after2 17/17, p3f_dm_authorboat rows 41-46.
The old landing 2831,3334,0 is in the sea -- never goto it. The way back (shipmonk2 on
the Entrana pier, 2830,3335) lands on the Port Sarim deck 3048,3231,1; ship_to_entrana_off
walks you ashore to 3048,3234,0. The monk does NOT search for weapons/armour in this pack
(LostCity's has_entrana_restricted_items is unported), so the pickaxe/axe can stay.
Abyss check: a test's "inner ring" box must exclude the mage's outer-ring landing tiles
(3020,4824 is one); a box of x3020-3056 z4818-4848 passes on the landing itself.
Reward XP is 6,500 Smithing / 5,000 Runecraft / 5,000 Fletching (deviousminds_tiffy.rs2,
constants are tenths): assert them with skill.expect_gain, not only quest.points.
