# legends -- relay notes (seam30, LostCity port landed and driven)

Each leg was driven in a scratch run (build/seam_state/seam30/legends_scratch/leg*.lua); leg 10 reached
the completion scroll. Source: LostCity_Content2 quest_legends.

Setup: `::complete quest_heroes quest_waterfall quest_druidicritual`; `::setvar crestquest ^crest_complete`,
`zombiequeen ^zombiequeen_complete`, `upass ^upass_complete` (no ::complete arm for those three);
`::setvar qp 107`; Quest Helper levels; armour + food (deathwings lvl 83, Nezikchened x3).

- L1: guard -> "Can I speak to someone in charge?" / "Can I go on the quest?" / "Yes, I'd like to talk to Grand
  Vizier Erkle.". Hut door = `poshdoor` at 2726,3368. Talk `radimus_erkle_hut`.
  Machete: `legends_cupboard` op1 then `legends_cupboardopen` op2. Jungle band at x=2795: chop the loc
  straight ahead (plant2 2942, plant1 2940), then walk. Map op2 at 2791,2917 / 2852,2915 /
  2910,2916; failed crafting rolls eat papyrus. Forester: use the completed map.
- Bullroarer: jungle animals nearby cancel Gujuo (1/6 each) -- swing up to ~16 times, 14 ticks apart.
- L2: rock `lgshamancaverock1` op1, agility roll -- retry. Fire wall op2 -> "/extinguish/", "/pure water/".
  Gujuo: "/pure water/", "Where is the pool of sacred water?" -> stage 8. Bookcase = op1.
  Lockpick gate op2 (~50%), 3 boulders (misses drain Mining),
  strength gate, jump `crumbled_wall` at 2789,9295, wall 2779,9305 op2, runes S,M,E,L,L, op2 -> through.
- L3-4: gems on `lg_gemplacerock` at the constant coords; book lands ~15 ticks after the 7th.
  Bowl: gold bar on an anvil ("Yes"). Bless: the stat_random TRUE branch is the FAILURE (-5 Prayer);
  below 42 Prayer Gujuo refuses. Machete on `tall_reeds`, reed on `sacred_water`.
- L5: blessed pure bowl on the fire wall, book on `ungadulu_good`, fight. Ungadulu: "I need to collect some
  Yommi tree seeds for Gujuo."; seeds on the bowl -> 13; reed on the pool again -> 14 (dried).
- L6: Gujuo "/dried up/", "Where is the source...", "/could you help me/" -> 15. Snake weed on ardrigal_sol.
  Cast Charge Water Orb on `lgmagictrialgateclosed` (needs a stafforb). Rope on the winch, drink, winch op1, climb.
- L7: rocky_ledge, rocky_ledge1, rocky_ledge2, viycaves_climbrock1-3; kill San, Irvig, Ranalph (a crystal
  each); furnace, `dragons_eye_rock`, recess at 2422,4691, barrier op1, boulder op1 -> Echned: "Who's asking?",
  "What can I do about that?", "I'll do what I must to get the water.", "Ok, I'll do it.". Hat at 2379,4712
  (back through barrier + obstacles): use the dagger on Viyeldi WHILE he speaks (speech ends in npc_del).
- L8: boulder -> Echned takes the glowing dagger -> Nezikchened #2; boulder from the east -> `lgwaterpool`.
- L9-10: germinated seed on `fertilesoil` 2778,2916 (roll: may take 2), bowl, rune axe x3, take totem.
  Totem on `lg_ord_totem_pole` 2852,2917: San, Irvig, Ranalph, Nezikchened in turn. Totem again; Gujuo walks
  up and gives the gift. Radimus in the hut, then the hall door and
  `radimus_erkle_guild`: "Yes, I'll train now." + four skills -> 75.
