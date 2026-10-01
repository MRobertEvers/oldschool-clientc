# Tears of Guthix, cutscene 1
VIDEOS.tsv: tearsofguthix 1, 3VoeqTrlLYA 0:04:38-0:04:53 (15 s). Clip: tearsofguthix-01-after-agreeing-to-start-the-quest-and.mp4
Trigger: tearsofguthix.rs2 ~tog_juna("But first you will need to make a bowl ...") closes -> cut.  HUD: up (inventory, minimap, chatbox stay).  Fades: none.
Player stands: 3250,9517,2 (at Juna, level 2).  Ends: the follow camera returns when ~tog_juna("Mine some stone ...") closes (clip 18.0 s).
Landmarks (t.world.loc_near): tog_blue_stone_rocks2 3229,9497,2; tog_blue_stone_rocks1 3236,9498,2; tog_blue_stone_rocks3 3235,9498,2; the cave's south wall behind them; the chasm north of them.

| shot | clip s     | ticks | motion | camera                                             | sees                                                                  | on screen                                                | actors |
| 1    | 3.5 - 5.0  | 2     | cut    | low (about 400), 6 tiles NE of the rocks, looking SSW | rock pair lower-left, a large dark rock lower-right, cave wall across the top | Juna: "There is a cave on the south side of the chasm..." | none   |
| 2    | 5.0 - 17.0 | 20    | glide  | eye rises about 300 units and pulls back about 3 tiles NE; look-at fixed on the rocks | the same rocks drift down and shrink; the wall line stays across the top; more floor appears | Juna: "There is a cave..." then "Mine some stone from that cave..." | none |
| 3    | 18.0       | -     | reset  | follow camera on the player beside Juna             |                                                                       | chat closed                                              |        |

Solved (camsolve rounds 1-5, build/quest_gate/camsolve_tearsofguthix*/compare.png):
  shot 1 start: cam_moveto(2_50_148_37_31, 400, 100, 100); cam_lookat(2_50_148_31_25, 0, 100, 100);   -- round 5 candidates S2/S4 (eye 3237,9503 h400 / 3238,9504 h350)
  shot 2 end:   cam_moveto(2_50_148_39_33, 700, 1, 1);                                                 -- round 4 candidate P moved one tile NE and 100 higher (eye 3239,9505 h700); rate 1,1 gives about 5 s, the recording's rise is about 12 s: approximated
Rejected: round 1 A-D (compass at h600-700, all too far), round 2 E-H (north of the rocks, chasm in the foreground), round 3 I-L (too high and too central).
Notes: the recording is resizable-classic footage although VIDEOS.tsv says "fixed"; the first port used a single cut at the mid framing and the comparison sheet showed the drift.
