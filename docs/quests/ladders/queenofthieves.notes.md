The Queen of Thieves - what the ladder cannot know

Source: wiki + Quest Helper only (LostCity has no such quest).

Starting needs: Client of Kourend + X Marks the Spot done, Thieving 20.
Tomas Lawry stands at 1796,3782 (spawn name piscquest_official).
Wrong prereq: he says "Busy" and shows which one is missing.

Dialogue gates:
- Tomas: "Yes." starts it (stage 1). "Not now." leaves it at 0.
- Poor woman (1803,3742) only talks at stage 1.
- Robert O'Reilly (1794,3758): first talk asks for stew (stage 3),
  second talk with a stew in the pack takes it (stage 4).
  He eats ONE stew; make it before you go (potato, meat, bowl of water).
- Devan Rutter: choose "Nope, sounds good to me." (stage 6).
  "I won't kill anyone." changes nothing.
- Conrad King: choose brutal or soft, both end the same.

Doors and stairs between steps:
- Manhole at 1813,3745 (closed: Open, then Climb-down).
  Entering sets stage 5. Warrens ladder at 1813,10145 goes back up.
- Warrens: Devan 1766,10148; Queen 1764,10158.
- Conrad stands on the dock at 1847,3734. He is only visible while
  stage is 6 (multinpc shell piscquest_target, value 6).
- Hughes' house: stairs at 1672,3681 (shared fai_varrock_stairs).
  Chest at 1681,3677 plane 1: Picklock/Search gives the letter.
  The cache chest is a multiloc keyed on varb12296_akd (Kingdom Divided).

Differences from the guide:
- Conrad's death is a dialogue plus "I'm coming, Elizabeth!" and the
  npc is removed (queenofthieves.rs2, qot target branch). No fight.
- The Queen reveal (stage 8) is one talk; stage 9 is never written.
- Rewards: Piscarilius favour and Graceful recolour interface are not
  authored (named leftovers).
