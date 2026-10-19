# Chambers of Xeric points: recorded behaviour

The raider's score is varp `raids_playerscore` (4609) and the party's is varbit `raids_client_partyscore` (5431).
Both are read from all five raid recordings (820, 843, 3282, 3285, 3389; see [README.md](README.md)), about 15,000
updates, and set against what happened on the same tick. Tool: `tools/cox/rsprox_points.py` (sections `changes`,
`damage`, `other`, `attack`, `channels`). The content cites these rows as `RP-*` (`cox_points.rs2`,
`cox_potion.rs2`, `cox.constant`).

    python3 tools/cox/rsprox_points.py ~/.rsprox/binary/archive/{3282,3285,843,3389,820}.txt --section channels

All five are Grid Master worlds. Its perks change the damage dealt, and the recordings show only the local raider's
score, so a party's shared caps can't be read here.

## Combat

| Row | Recorded |
|---|---|
| RP-C1 | Points are credited on the **attack tick**: when the raider swings or shoots, with the projectile still in the air, not when the hit lands. 3285's first +95 comes 2 ticks before its 19 hitsplat. Matching each hitsplat to its attack (the projectile launched at that npc that lands on that tick), the gain on the attack tick is **exactly 5 × the damage** on 393 attacks: every npc of the raid, and every hit of the first part of each npc's life. |
| RP-C2 | Each npc's **last stretch** pays less than 5 per damage, falling as it dies: 3285's three mystics went 5, 5, 4.4, 3.1, 2.3, and the next mystic started at 5 again. It is per npc and is not the room's total. The CM shamans paid 5 all the way (285 damage → 1425). No rule found fits it, and it is not a Grid Master perk the raiders' weapons had. |

## Potions (`strong` = the (+) tier)

| Row | Recorded |
|---|---|
| RP-P1 | A dose of **overload (+)** pays **400** on the sip when the overload it renews is due: its timer (`raids_overload_timer`, 20 steps of 25 ticks) at 0-2 before the sip (13 of 13 solo doses). At 6 or 7 it pays nothing (3 of 3), and the dose still goes down, with no refusal ever recorded. The message is "You drink some of your strong overload potion." 820's party paid 400 nine times, then 0, 0, 105 and 0 with the timer at 0-2: a cap the recording can't read (the wiki: 5 sips × raiders, then 5 at a reduced amount). |
| RP-P2 | A dose of **prayer enhance (+)** pays **112** when none is running (timer 0, 2 of 2) and nothing while one runs (2 of 2). The message is "You drink some of your strong prayer enhance potion." |
| RP-P3 | The other potions drunk in the raids, none of them raid potions (super restore, super combat, Sanfew serum, bastion, antipoison, the foul liquid), pay nothing. The raid's other potions (revitalisation, Xeric's aid, elder, twisted, kodai) and the (-) and standard tiers were never drunk. |

## The rest

| Row | Recorded |
|---|---|
| RP-K1 | The tightrope's keystone pays **2000 the tick after** "Your keystone glows as it is absorbed into the barrier, which disperses." (6 of 6). |
| RP-T1 | Every landed chop on the Muttadiles' meat tree pays **3** the tick after "You hack away …" (14 of 18; 2 three times, 28 once), the final chop included. |
| RP-B1 | A brazier lit in the Ice Demon's room pays on the light: 1040 (9; 28 × 36 + 32, our formula), 1392 twice (CM), then smaller amounts (824, 392, 320) and 0, the kindling cap. |

## Fixed in our content (2026-10-19)

| Was | Now |
|---|---|
| combat points paid when the hit landed | paid on the attack, 5 × the damage the player funnel prepared (`~cox_on_attack` from `~player_hit_npc_prepare`) (RP-C1) |
| no potion points | overload (+) 400 when it is due (the overload clock at 50 ticks or less), prayer enhance (+) 112 when none runs (RP-P1, P2) |
| a raid overload refused for its whole five minutes | refused only while the fresh dose's five hits land, as its message says; after that it goes down (RP-P1) |
| "You drink some of your overload potion." / "… prayer enhance." | the recorded "strong" wording for the (+) tier |
| the keystone's 2000 on the message's tick | the tick after (RP-K1) |
| no points for the meat tree | 3 the tick after each landed chop (RP-T1) |

On scriptrun (`cox_mystics.lua`), 31 of the 33 score rises fall on the raider's attack ticks; the other two are the
overload's 400 and one 5.

## Open

| Item | State |
|---|---|
| RP-C2 the end-of-life taper | Not modelled. Ours pays 5 per damage to the last hitpoint (capped at the npc's hitpoints, Near-Reality's rule). |
| Points for damage not from an attack | Venom, recoil and thralls never reach the funnel and pay nothing now; no recording settles them. |
| The potion cap, the other potions and tiers | Unrecorded (RP-P1, P3). |
| The overload's due window | 0-2 pays, 6-7 doesn't; 3-5 is not recorded. Ours pays at 50 ticks or less. |
| The chop's 2s | 3 of 18 chops paid 2; when is not settled. |
| Kindling past the first light | The taper (824, 392, 320, 0) is the Ice Demon room's own; not re-read here. |
| Party | The party score, team caps and the 820 cap are not readable from a local-only view. |
