# Making History, cutscene 1
VIDEOS.tsv: makinghistory 1, m4H4nxjCMeg 0:02:14-0:02:49 (35 s). Clip: makinghistory-01-choosing-tell-me-more-at-the-quest-start.mp4
Trigger: makinghistory_jorral.rs2 ~chatplayer_anim("Tell me more.") closes -> chatbox line "Jorral talks about the outpost...", fade to black, flyover with five narration lines.  HUD: hidden (panel empty, minimap black, fades at both ends).  Fades: black at clip 3.5-5.5 s and 36.5-38 s.
Player stands: 2437,3347,0 (inside the outpost hut beside Jorral, 2436,3346).  Ends: the camera returns in the hut when Jorral's next page opens (recording: "If all goes well, I hope to be able to turn it into a museum ..."; the port's next page is "There are three who might know something ...").
Landmarks: the outpost hut (walls, crates on the roof, inner door), a grey boulder and trees west of it, dead trees west, steep brown slope south-west, green hills north-west.

| shot | clip s      | ticks | motion | camera                                                        | sees                                                                 | on screen                                   | actors |
| 0    | 3.0 - 5.5   | 4     | fade   | follow camera, panel still drawn, fade to black               | hut interior from above                                              | chatbox "Jorral talks about the outpost..." | none   |
| 1    | 5.5 - 10.5  | 8     | cut+glide | eye 24 tiles WSW, low (500), looking at the hillside in front of the hut, then glide in to 700 | hut small top-centre, boulder and trees left, green hill foreground, hut grows | "With many occupants over the years..." | none |
| 2    | 10.5 - 17.5 | 11    | glide  | eye rises slowly (700 -> 750), hut centre                      | hut with crates on the roof, trees left, hills behind                | "...the building has seen much action."     | none   |
| 3    | 17.5 - 23.5 | 10    | cut    | high (650) from the south, interior visible                    | hut interior: crates, inner door, floor; dark dead trees in the recording | "It started life as an outpost..."      | none   |
| 4    | 23.5 - 30.5 | 10    | cut    | low (350) west of the hut, horizon above it                    | hut centre, green hills and trees behind                              | "... its sole purpose being to see invading armies..." | none |
| 5    | 30.5 - 36.5 | 13    | cut    | hills north-west of the hut, eye 420                            | trees and open grass; recording adds a boulder and two figures       | "... before they saw the city of Ardougne." | none   |
| 6    | 36.5 - 38.0 | -     | reset  | fade to black, cam_reset, HUD restored, fade in in the hut      | hut from above, Jorral talking                                       | Jorral page                                 |        |

Solved (camsolve rounds 1-7, build/quest_gate/mhs1/compare.png; the same output dir was overwritten each round):
  shot 1 start: cam_moveto(0_37_52_46_12, 500, 100, 100); cam_lookat(0_37_52_62_16, 0, 100, 100);   -- round 6 candidate C (eye 2414,3340 h500 > 2430,3344 h0)
  shot 1 end:   cam_moveto(0_37_52_56_6, 700, 4, 2); cam_lookat(0_38_52_5_19, 250, 4, 2);            -- round 4 candidate C (eye 2424,3334 h700 > 2437,3347 h250), rate 4,2 is a guess
  shot 2:       cam_moveto(0_37_52_58_4, 750, 1, 1);                                                  -- a small rise, not solved against frames
  shot 3:       cam_moveto(0_37_52_62_5, 650, 100, 100); cam_lookat(0_38_52_4_19, 0, 100, 100);       -- round 5 candidate C
  shot 4:       cam_moveto(0_37_52_49_12, 350, 100, 100); cam_lookat(0_38_52_5_19, 350, 100, 100);    -- round 6 candidate B
  shot 5:       cam_moveto(0_38_52_2_16, 420, 100, 100); cam_lookat(0_37_52_56_30, 0, 100, 100);      -- round 7 candidate A
Rejected: round 1 (8 tiles from the walls, only brickwork in frame), round 2 (18 tiles on the four compass points at h800, pitch too steep), round 3 (NW of the hut, door wall fills frame), round 4 start candidates at 2412,3322 (brown slope fills frame).
Notes: the recording is the 2017 build: the hut's walls are lower and the sky black, so heights differ from the game's; the matching is of arrangement only. Narration appears in the recording as small text at the top of the viewport (not in the chatbox); the port prints each line with mes() in the chatbox, which is the nearest thing the engine has without a click. The recording's closing Jorral page ("If all goes well ... What do you think?" then "Start the Making History quest?") does not exist in the port.
