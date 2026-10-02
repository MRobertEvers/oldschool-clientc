Eagles' Peak -- what the ladder cannot know (driven 2026-10-02, parity b50)

Main cavern (level 3). Entry lands at 1994,4983, east of the Exit wall (1993,4983).
Walk to the feather piles (2005,4972); stand at 2005,4970 to shout.
Nickolaus (2006,4959) is across a chasm: the shout is [apnpc1] (eaglepeak.rs2),
not a walked talk. He and the eagle guard have wanderrange=0 (eaglepeak.npc).
The camera must look north or the npc is not hittable: t.drive.camera(0,330,900),
try other yaws if talk_to says "framed nothing".

Rooms (levels 2). Tunnel mouths in the cavern, one click each:
bronze 1986,4949 / silver 1986,4972 / gold 2023,4982 (approach from 2022,4982).
Bronze: the map has no Pedestal (net trap id 19980 is in no square). The tunnel
mouth stands it up at runtime (bronze_room.rs2, bronze_ensure_pedestal). Take once
(net drops), operate all four winches, Take again. Stand at 1974,4912, not on it.
Silver: pedestal, east rocks, NE rocks, opening (1971,4884), then Threaten the
kebbit (op 3, choose "Taunt the kebbit."). The feather lands on the FLOOR; pick it
up. Re-inspect the opening if it despawned. Needs Hunter 27.
Gold: lever4 sits in a 5x2 strip; stand at 1977,4891. Five birds, five effective
feeds (feeders 4,3,2,1,2a); the guide's sixth seed (feeder 2 again) is refused and
kept: the port maps feeder 2 to one bird only (gold_room.rs2 feeder_bird).

Stone door (2003,4948) is a wall on the WEST edge of that tile. Use the three
feathers from 2002,4948 (cavern side). Open teleports to 2004,4949 (east); the
guard side is reached from there by walking to 2007,4952. Opening again from the
east returns you to 2002,4948. Nothing walks through it.

Eagle guard (3x3, 2007,4954): Walk-past with beak (hat) + cape (back) WORN, door
complete. It teleports you into the nest (2006,4960). From the nest the same op
walks you back out (the nest is sealed). Without the disguise: 1 damage.
Nickolaus (nest) takes ONE spare beak+cape from the backpack, so keep a spare.

Asyff (tailorp, 3281,3398): the bird-costume row is in the ordinary peruse menu,
only after the shout (nickolaus_chat=3). Needs 10 feathers, dye, tar, 50 coins and
4 free slots after the trade. Second visit: "I've got the feathers..." then Eagle me up.

Camp (2317,3504): eaglepeak_nickolaus_campsite is a multinpc, drawn only at quest
values 25,30,35,40. The demo is three mesboxes, then ferret + box trap. Charlie
takes the ferret and completes the quest (stage 40, 25000 Hunter xp, 2 qp).

Cutscene: none ported (2 scenes pending the owner's spec session).
