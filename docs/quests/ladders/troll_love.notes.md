Troll Romance -- what the ladder cannot know (driven 2026-10-01)

Source: LostCity_Server quest_troll_love. Ug/Aga/Arrg dialogue match it; Arrg's fight is the OSRS
wiki form (arrg.rs2 keeps its own stat block, owner-only duel, 7 free slots for the gems).

The slope (trollromance_sled.rs2):
- Both rides start from the Slide op of a barrier loc, with the waxed sled WORN. Wear it with
  the sled's Ride op (inv op 2) first; Ride only works inside mapsquares 43_58..43_60.
- Ride 1 barrier: (2772..2773,3835). Lands at (2790,3794). 1-in-250 it crashes instead
  (::trollromance_crash runs that path). Needs agility 28.
- Flowers (rareflowers op2) stand at x2775..2781, z3780..3785, a short walk from the landing.
- Ride 2 barrier: (2785..2786,3771), just south of the flowers. Lands at (2794,3719), beside the
  Rellekka exit tunnel (maplink). Do not click it before picking: it is one-way.
- The "up" slope barrier only says nothing interesting happens.
- Leaving the mountain squares with the sled worn stows it (full pack: it is lost), the wiki's
  "teleport with a full inventory loses it" (sled monitor timer).

Doors and tunnels between steps: Trollheim cave entrance, the crevasse, the piste top and the
exit tunnels are cache maplink rows (ladders_stairs/configs/maplink.dbrow), not scripts.

Item flow: swamp tar used ON the bucket of wax (cake tin in pack) makes wax and eats the tin;
wax on the sled gives the waxed sled and hands the tin back (LostCity). Stage 22 -> 25 on waxing.
Picking sets 25 -> 30; Ug takes the flower (30 -> 35).

Ug will not talk while the flower is wielded. Climbing boots (12 coins at Tenzing) are needed to
use the stronghold shortcut; walking in from Trollheim needs none.

Not ported: no wiki cutscene exists (CUTSCENES.tsv row says none); the two rides are LostCity's.
Engine gaps: p_temprun and p_animprotect are no-ops here (ssvm warns once); the rides still walk.
