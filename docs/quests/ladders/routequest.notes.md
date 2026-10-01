# routequest -- what the ladder cannot know (driven 2026-09-30)

Rungs: the bridge is whole in the cache map (swamp_bridge1 on 3502,3427..3430 level 1) and the
server holds none of it, so climbing the tree (Climb, op 2) loc_adds route_swampbridge_1..3 on
3502,3428/29/30 (routequest_start_and_route.rs2 routequest_bridge_prepare). Click "Repair" on each
in turn; each takes 1 plank, 75 nails, a hammer, then walks you onto it. The third one also walks
you off and down the tree to 3503,3432 (found_guard 52). Back: tree base "Cross-bridge" (op 1).
Camera pitch 450 helps the click on the rungs.

Boat: Cyreg stands at 3522,3285. Board the boat at 3524,3283; the fee dialogue is Cyreg's. The
13-tick journey interface plays, then a mesbox. Landing 3501,3377. The first trip needs the pouch.

Curpile (3508,3440): three different questions of six, all asked, then the verdict. Any wrong
answer: knocked out after the third, you wake in Mort'ton (3522,3285), stage stays 52. "Don't know!"
is always wrong. Press him from 3508,3438.

Doors 3509,3447 -> dungeon 3500,9811 (3500,9813 is a sealed pocket). Walk to the cave mouth at
3492,9823 (route_cavewalltunnel) -> hideout 3505,9832. Its wall at 3505,9831 leads back out.

Members: Veliaf 3506,9838, Harold 3504,9833, Radigad 3509,9831, Sani 3510,9836, Ivan 3513,9843,
Polmafi 3514,9838. Click their spawn (_parent) symbols; each first talk sets a bit; "Ok, thanks."
ends it. Veliaf at 65 with all five met offers "Let's talk about the weapons." (needs the 6 steel
weapons in the backpack, not worn). The cutscene plays inside the hideout chamber (LostCity's room
copies do not exist in this map): 18 camera keyframes, Vanstrom as myq3_vanstrom_klause_human then
route_vanstrom_vampire (no wings-grow form). Sani and Harold leave for this player at stage 80.

Hellhound: private to you (npc_setowner, stage 80), 3506,9839, level 97; melee works, attack 70+
and food. Dying leaves no loot. Stage 85 on its death; Veliaf's "How do I get out of here?" -> 90.

Exit: false wall 3480,9837 (click from the east side), ladder 3477,9846 -> 3495,3464 (stage 97).
The Stranger is the pub seat's second form (thsfm_vanstrom_hide, set at the cutscene or login):
3503,3477; click the multi wrapper, dialogue finishes the quest. The varp routequestmulti needed a
server declaration (transmit, perm) -- without it the hide bit and the rungs never reached the client.
