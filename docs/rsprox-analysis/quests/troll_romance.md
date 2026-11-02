# Troll Romance — RSProx index 4456

Recorded 2026-11-02, rev 241.1, world 495, one session end to end (2,507 ticks), labelled
"Troll Romance": a glory to Edgeville and the Varrock underwall tunnel [124..173], the Grand
Exchange and a bank [186..311], the minigames necklace to Burthorpe [320], the stronghold
and Ug's start [383..584], Arrg and Aga [608..635], Ug [684], Tenzing [773..861], Dunstan
[896..1016], the wax and the waxed sled [1026..1033], back up through the stronghold, out
of its top exit onto the summit, the troll child and the ice gate [1381..1392], the
Trollweiss cave [1397..1446], the mountain and both sled rides [1454..1653], the flower
[1559], the exit tunnels [1691..1748], Ug with the flower [1983..1992], Rick Turpentine
[2000..2011], Aga, the challenge and the duel with Arrg [2034..2266], the arena and the
rocks [2274..2316], and Ug's reward [2414..2491]. The journal was opened eight times at
six stages. It is the only recording of the quest on the index (`search npc 640`: 4456 plus
two sessions passing Ug).

Before this port the quest was a LostCity (2004) port: Ug's start a four-way menu with no
start confirm, Tenzing's and Dunstan's Troll Romance pages invented (2026-08-12), an "Enter
the arena. This is not a safe death." confirm and "Arrg attacks immediately!", Ug's reward
gated on seven free slots with an invented line, a 2004 journal, the sled's stance the
generic human one, the flower pick with seqs, the wax put in a new slot with the bucket
lost elsewhere, and invented lines when the sled was stowed or lost.

**Ours:** `quests/quest_troll_love/` (trollromance_ug.rs2, trollromance_aga.rs2,
trollromance_arrg.rs2 (pages and the challenge), trollromance_arrg_fight.rs2 (the duel,
audited separately below), trollromance_burthorpe.rs2 (new: Tenzing's and Dunstan's
arms), trollromance_sled.rs2, troll_love_journal.rs2, quest_troll_love.obj (new),
quest_troll_love.constant); the hooks in `quests/quest_death/scripts/death_tenzing.rs2`
and `death_dunstan.rs2` (3draster-e9's files, edited with their agreement);
`interface_questjournal/scripts/quest_overview_start.rs2`; `quests/scripts/questpoints.rs2`
(the jingle row); `macro_rick_turpentine/` (new); `skill_agility/scripts/
agility_shortcuts_osrs.rs2` (the Varrock tunnel); two generated blocks
(`server_fields.generated.obj` [trollromance_toboggon_waxed]'s stance lines,
`server_fields.generated.npc` [trollromance_ice_wolf_1b]'s sounds);
`tools/check_quest_combat_contract.py` (check_troll_romance). Tests:
`test/quests/troll_love.lua`, `test/randoms/rick_turpentine.lua`.

## Actions (rsprox_actions.py) — a verdict per row

| first tick | action | verdict |
|---|---|---|
| 7, 26..39, 56..80, 352, 361 | welcome screen, skill guide, quest list, overviews, world map | interface generic; the three overviews' start strings were missing: added (Land of the Goblins, Legends' Quest, Troll Romance) |
| 85 | deadtree2 Chop | no answer (the recorder moved on) |
| 124 | glory (worn) Edgeville | generic jewellery, audited 2026-10-29 |
| 131..311 | drop, bank booth and banker, GE clerk and offers | generic |
| 166 | varrock_sc_tunnel_west | **fixed**: was a Kronos port (three anims over four ticks, a telejump to 3144,3514); now the recorded three exactmove legs to 3142,3513 (section below) |
| 320, 709, 872, 1745 | minigames necklace Rub > Burthorpe | generic jewellery, audited 2026-10-29 |
| 383..556, 1134, 1298, 2274..2316 | Tenzing's gate/doors, the stile, the climbing rocks, the stronghold's doors, stairs, exits, the arena gate | Troll Stronghold's (3draster-e9, index 4455) |
| 489, 495, 507 | Eadgar, Godric, the rack | Troll Stronghold's; pages present in quest_troll/ and quest_eadgar/ |
| 409.., 539.., 647.. | potions, prayers, run, equips, lobster, bones | the recorder's kit / generic |
| 561, 1983, 1995, 2414, 2474 | Ug | **this content** |
| 607, 2048..2063, 2439 | Arrg | **this content** |
| 613, 2440 | crate (366) by Aga | no answer (clicked through) |
| 614, 2034, 2449 | Aga | **this content** |
| 773..861 | Tenzing | **this content** (stage 10 arm) |
| 864 | death_boxes_2 | no answer |
| 896 | poordoor (Dunstan's) | door generic (opened on L+1, synth 62) |
| 902..1016 | Dunstan | **this content** |
| 1026, 1033 | wax on tar, wax on sled | **this content** |
| 1381, 1389 | troll child, ice gate | Desert Treasure's (deserttreasure.rs2): both pages match, moods match |
| 1397..1736 | cave entrance, crevice, piste tunnels, exit tunnel | maplinks: every landing and the L+1 shape match (section below) |
| 1454, 1585, 1624, 1664, 1681 | waxed sled Ride / Remove | **this content** |
| 1463..1544 | snow trees | cache category 189 (tree): woodcutting's "You need an axe..." as recorded |
| 1498..1691 | piste barriers down/up | **this content** |
| 1524, 1529, 1596, 2268..2273, 2503 | pick-ups (the spawned sled, a chaos rune, coins, bones, an emerald) | generic; the sled at (2804,3785) is a spawn in m43_59.spawn |
| 1559 | Trollweiss | **this content** |
| 2004 | Rick Turpentine | **new**: macro_rick_turpentine (section below) |
| 2075 | Arrg (attackable) | the duel, audited separately |
| 2496, 2507 | quest scroll close, logout | generic |

**Teleports:** every row is one of the above: the glory, the tunnel's three legs, the necklace
(x4), the stile, the rocks, the stronghold entrance and stairs (e9's), the maplinks
(below), the arena (the challenge, below), and the climbing rocks after the arena.

## Stages (varp 385, transmit=yes, perm)

| value | recorded | goes out with | ours (tick log, run trollrom3) |
|---|---|---|---|
| 5 | [581] | the start menu's "Yes." | same click |
| 10 | [629] | the click of Aga's last page, with the menu | same |
| 15 | [857] | the click of "Like who?", with Tenzing's Dunstan page | same |
| 20 | [984] | the click of Dunstan's last page ("...all the materials.") | same |
| 22 | [994] | the click of "Well, here's the sled...", with the materials gone and the sled | same |
| 25 | [1033] | the wax used on the sled | same |
| 30 | [1561] | the flower, op+1 | same |
| 35 | [1992] | the click of Ug's last page, the flower gone | same |
| 40 | [2264] | Arrg's death | the duel's audit |
| 45 | [2491] | the click of Ug's last page | same |

`varp_transmit_audit.py` on 4456: every recorded var we never send is engine-generic
(tracking_damage_*, tracking_food_eaten, levelup_list_*, xptracker_*, bank_add_lastslot,
last_npc_*, fossil_mine_temp_pts, autocast_spell_obj); none is Troll Romance's.

## The start and the end

- Start [561..584]: linear, no 2004 menu: "Awww..." -> Ug's two pages -> the player's "Who is
  Aga?" -> Ug's two -> the combat warning mesbox (recommended 50) -> "Start the Troll Romance
  quest?" Yes./No. -> stage 5 + "You've started a new quest" with the next page. "No." is
  the wiki's page (unverified). Ug's second page starts with a space (" Ahhh, it no use,
  I too sad!"), as Jagex sends it.
- End [2474..2491]: on the click of "Whaaat? ... Ug go to Aga now." (a walk away from that
  page, [2427], completes nothing): stage 45; the seven gems into the pack one by one, the one
  that did not fit on the floor (`obj_add_specific ... despawn=200, ownership=self,
  neverturnpublic=true`); qp 110 -> 112; the line; disease -50/0; music 147 and jingle 152
  (quest_complete_1: the jingle row added); the scroll "2 Quest Points | 8,000 Agility XP |
  4,000 Strength XP | 7 Uncut Gems" with the Trollweiss's model; the xp. Ours: the same, the
  floor gem `obj_add_private(.., 200, 200)`. The invented "seven pretty rocks, pack too full"
  gate is gone.

## Pages

`rsprox_pages.py` lists 222 rows. Every Troll Romance page (150 of them, both repeats
included) is in our scripts verbatim with its recorded mood, and lays out row for row as
Jagex's: checked offline with the q8_full, 380 px greedy wrap our `~chat_layout` uses,
which reproduces every recorded natural break. The forced breaks (`|`) are exactly
Jagex's: "Errrr... thanks...|I think?", Aga's "I not know if he love Aga,|", "Aga can't
wait for Trollweiss.|", Ug's "...in mountains.|Ug no know...", "I suppose I am a bit of a
legend.|", Tenzing's "...their prowess.|", "...inaccessible.|", "...get down there|",
Dunstan's "Haha, very funny!|", "Yeah, that should do it.|", the objbox's "...making a
sled.|". The two parenthesised thoughts go out with `if_setanim -1` (no anim):
`^troll_love_chat_noanim`. Menus and their order are as recorded (Tenzing's hub with the
Trollweiss question in slot 4 at stage 10, no greeting; Dunstan's "Talk about a quest." /
"Talk about something else."; Dunstan's wax questions: the first menu has all three, each
later one the other two plus "See you!").

Unverified (wiki transcript, moods by tone): Ug at stage 5 ("Sorry not yet..."), Ug
between 15 and 30 without the flower, Ug after the quest; Aga before and after; Aga's and
Arrg's "I've got to go." answers; Arrg's Trollweiss talk (stages 10..25); "I am here to kill
you!"; Tenzing's "I've got to go." ("See you later"); Dunstan's "Yes." branch, "without
materials" and "Hello again." before waxing; "See you!" ("Thank you, come again.").

## The journal

Recorded at stages 5 [589], 10 [661], 15 [885], 25 [1044], 30 [1752], 35 [2000]; current
format (struck rows, no blank rows). Ours sends those rows verbatim
(`~quest_journal_rows`). Stages 20, 22, 40: the nearest recorded stage below (unverified).
45: stage 35's struck rows + a blank row + QUEST COMPLETE! (unverified). "I got some yew
logs..." is Jagex's row whatever logs were handed over (the recorder gave maple). Not
started: the overview, "talking to <col=800000>Ug<col=000080> in the <col=800000>Troll
Stronghold<col=000080>." [80].

## The wax and the sled

| | recorded | ours (before) | ours now |
|---|---|---|---|
| tar on wax [1026] | spam "You make some sled wax."; the wax in the bucket's slot, the tin's slot empty, tar 3->2 | game line, bucket/tin/tar deleted and the wax added to the first free slot | as recorded (`mes_spam`, `inv_setslot`) |
| wax on sled [1033] | the line, stage 25, seq 1470 delay 0, the waxed sled in the sled's slot and the tin in the wax's, synth wax_sled 1871 | the same packets, items re-added to free slots | in place (`inv_setslot`) |
| Ride [1454], [1624], [1681] | synth equip_fun (2238) (+1539 shield_appear when it pushed the shield off); stance ready 1461, every move 1468 | equip_sled 1864; stance human_* | equip_fun; the stance in quest_troll_love.obj, the generated block's lines removed |
| no sled worn [1615] | "You need to have a waxed sled equipped!" | same | same |
| barrier up [1673] | "Nothing interesting happens." (engine) | same | same |
| off the mountain [1696] | the sled back to the pack on the exit tunnel's teleport, no line | an invented line | no line; a full pack loses it (the wiki's; unverified) |

**Ride 1** (glide G = [1499]; ours G = 607): every tile on the recorded tick (2 a tick,
2772,3834 -> 2790,3794), "Here we go!" and sled_start (1868) on G, `busy`, the cameras at
G+8 (2770,3822 h300 / 2773,3809 h300, 20/50, 50/50) and G+11 (2787,3812 h300 / 2780,3807
**h250**, ours had 300: fixed), sled_jump (1865) and the flying stance at G+13, sled_stop
(1869), the moving stance and cam_reset at G+16, the landing, "That was quite a ride!", the
walking stance and the xp at G+18, the face to the barrier at G+19 (ours had it on G+18:
fixed). sled_loop (1866, loops 5) at G+4 with the step to 2769,3826 (ours G+5: fixed).

**Ride 2** (G = [1625]; ours 724): sled_start_longer (1863) on G, sled_loop (loops 10) at
G+6, every tile, the cameras at G+19 (2790,3723 h300 / 2782,3729 h250) and G+23
(2783,3729 h1000 / 2792,3721 h150), sled_jump at G+23, sled_stop delay 30 on the landing
G+26, the face a tick later (ours on the landing: fixed), the stance, cam_reset and the xp
at G+28. No overhead line on ride 2.

The crash (1 in 250, LostCity's) has no recording: unverified.

**The flower** [1559..1561]: the face on the op tick, then stage 30 and the flower together a
tick later, from standing, with no seq and no line (the recorder was on the sled). Ours had
the stage on the op tick, the flower a tick later and two seqs: now `p_delay(0)` then both;
the seqs only off the sled (LostCity's; unverified).

## Moves and maplinks

| loc | recorded | ours |
|---|---|---|
| trollromance_caveentrance [1400] | L+1 teleport to 2803,10187, no seq | landing matches; from standing on the op tick |
| trollromance_snow_cavewall_crevis [1446] | L+1 to 2778,3869 | matches |
| trollromance_piste_exit_tunnel_exit [1696], [1704] | L+1 to 2799,10134 | matches |
| trollromance_piste_exit_tunnel_top [1702] | to 2797,3719 | not driven by the test |
| trollromance_piste_exit_tunnel_bottom [1733] | L+1 to 2730,3713 | matches |
| trollromance_stronghold_exit_tunnel [1736] | from standing, op tick, to 2773,10162 | not driven by the test |
| varrock_sc_tunnel_west [166..173] | face L+1; L+2 exactmove (3137,3516)->(3138,3516) 0..30 angle 1536 + agilty_shortcut_enter_hole + climb_under (2452); L+3 ->(3141,3513) + tunnel_walk; L+4 ->(3142,3513) + exit_hole | fixed to this; the east mouth is still Kronos's (no recording) |

## The zone

Ice trolls in the cave (861 / 2508 attack sounds), ice wolves on the mountain (909), death
trolls in the exit tunnel (867): all spawned, aggressive, sounds as recorded except
**trollromance_ice_wolf_1b**, whose generated block stated null sounds while Jagex plays
wolf_attack (909) on its swing [1455]: set to 909 / 912 / 911 (the defend and death sounds
are its sibling `_1`'s: unverified). The gnome explorer wanders as recorded.

## Rick Turpentine (random event)

Built new (`macro_rick_turpentine/`, `::rick`), on the Genie's frame:

| | recorded | ours |
|---|---|---|
| S | added beside the player facing south (3 of 3) | same |
| S+1 | emote_bow, face the player, "Good day to you, milord." (no name, 3/3), smokepuff on his tile + sound 1930 range 7 | same (milady for female: unverified) |
| S+10 | bow, `[transformation]` re-sent, "<name>, please speak to me, milord!" (4216) | same; the wiki's other line alternates (unverified); the rest of the schedule is the Genie's (unverified) |
| talk [2004] | the reward line + item + page "Today is your lucky day, sirrah!\|I am donating..." (chathap) on the op tick | same; the roll is the wiki's 150-row table (4456's cosmic talisman is on it) |
| talk again [2005] | "I must leave you now, sirrah!" | same |
| leave [2009]->[2010] | gone the tick after the last page closes, the puff with it | same (scriptrun, rick0: click 9 -> gone 10) |
| Dismiss, another's Rick, a full pack | — | unverified (wiki lines; the ground form of the reward line is the recorded Sins of the Father one) |

## Arrg (the duel)

One session (`search npc 643`: 4456 only): 3 swings by him (two rocks under Protect from
Missiles, one punch), 37 swings at him (3 melee, 34 Earth Strikes), 29 splats on him, one
death. A = [2071] (the click of the last challenge page), H = [2263] (the killing splat).
Test: `test/quests/_troll_love_arrg.lua` (scriptrun; 6 tick-log rows, all pass; the old
packs fail it: the old Arrg threw rocks from 12 tiles into the safespot and killed the
player).

| aspect | recorded | ours before | ours now |
|---|---|---|---|
| A: the challenge | added 2909,3611 facing south; the player teleported to 2912,3611 and facing him; hint varbit 14192 0->7, `hint_arrow reset`, the npc arrow; no fade, confirm or line | a confirm menu, "Arrg attacks immediately!", npc arrow only, the player stepped west and swung on A | as recorded (the attack queued to A+1: our engine walks a player on a `p_teleport` tick) |
| A+1 | he steps east to 2910,3611 and faces the player; the player swings with no click | never stepped | matched (1/1) |
| attack clock | swings at A+5, A+10, A+16 (the gaps over 4 are the player out of reach); attackrate 4 | own 4-tick timer, first at A+3 | the engine clock, first at A+5 (`npc_attackdelay(4)`), 15/15 gaps of 4 beside him |
| reach | melee reach only: no swing from 2 tiles [2086], none in 174 ticks from 4 tiles [2090..2263] | attackrange 12, rocks from anywhere (9 into the gate) | attackrange 1, chases a tile a tick |
| safespot | stuck on 2901,3619 against the wall, facing held, no swing | — | stuck on 2901,3619, silent 18+ ticks (2 runs) |
| punch | troll_attack 284 + synth warhammer_crush 2567; the splat on T; human_hit_1 518 delay 20 | no 2567; 1 in 5 through prayer (invented) | 284 + 2567 + `~npc_meleeattack` (cache Str 140, +100: max 38) |
| rock | troll_rock_throw_lefthand 1488; sound_area troll_throw_rock 870 on his SW tile r10; map_projanim troll_rock_travel 276 start 31 end 47 angle 8 progress 128, heights 146/146 raw; stone_impact 860 on the player's tile r10; block at delay 47; splat T+1 (2/2) | throw seq 1142, a launch spotanim, no 870/860, heights 144, accuracy x4, 1 in 5 through prayer | every field as recorded |
| facing | `[facing] player=` re-sent as the angle turns (15); never a coord; reset on H+1 | a coord face every swing | as recorded (one extra face on A: engine) |
| defend | troll_block 285 on every swing at him (37/37: melee delay 0, spells delay 86); troll_hit 868 on every splat (29/29), none on splashes | same (melee) | same; the spell path not run |
| damage | prayed rocks 2/2 zero; one unprayed punch 6 | — | 2 runs: prayed rocks 8/8 zero, prayed punches 16/16 zero, unprayed max 32 <= 38 |
| death | H+1: troll_death 287, synth 866, facing reset, ops 0, npc `say "Arrg!"`, stage 40. H+3: bones + one roll (35 coins), reveal 100 despawn 200 ownership self, clientscript 7192 per item, del | stage, drop and `mes("Arrg!")` at H+3 | H+1 as recorded, H+3 drop + 7192 + del (`death_delay=0`) |
| monitor | — | an invented "Arrg has tired of waiting..." line | deleted; finds the player's own Arrg by owner |
| block kinds | enabled_ops, facing, headbar, hit, say, sequence, spotanim (the recorder's Earth Strike impacts only) | — | all present |
| every splat a cause | 3/3 in the window caused; no heals | — | 26/26 caused; no heals |
| levels | cache 70/40/140/140/70/0; earth weakness 50% fits (max splat 12 = Earth Strike 8 + 4; 26/34 landed vs ~72% predicted) | same | same (`check_npc_cache_levels.py` 0) |

Open (engine, not this content): the player's face on Arrg a tick late and a face reset on A
(the same-tick walk after `p_teleport`); one extra `[facing]` on his add (an owned npc faces
its owner); enabled_ops 0 on H, Jagex H+1; a hint reset on his del, Jagex none; the rev-241
`OBJ_ADD` sends reveal 0 / ownership 0. Other owners: `elemental_shield` has no
`defend_anim` (ours human_sword_def 388, Jagex human_shield_defence 1156, 2/2); Earth Strike's
base max 6, the wiki and the max splat say 8; the arena's music (Jagex midi 257 on A; ours
258 or 187), e9's arena. Unverified (one session): rocks vs punches (2:1 recorded, ours
50/50), the max hits 38/30, the rock's accuracy and the wiki's prayer penetration (0/2
prayed rocks through), the rock's end time from 2 tiles, slash attack +60 and ranged defence
200 (not in the rev-241 cache), maxrange 20, the 500-tick lifetime, a death while moving.

## Verification

- `test/quests/troll_love.lua` (rewritten to the recorded flow, tick log on): run
  trollrom3 and, with the rebuilt duel, trollrom4: 168/169, every quest leg green; the one failure is the shared driver's
  `teleportCamelotToTenzing.cast` row: the server lands the teleport at C+3 (tick log
  267 -> 270) but the verb's settle ends a tick early when a troll hits the player at C+2.
  Not this content.
- `test/randoms/rick_turpentine.lua`: scriptrun PASS, all three endings.
- `test/quests/_troll_love_arrg.lua`: scriptrun PASS (6 tick-log rows).
- `test/agility/varrock_underwall_tunnel.lua`: scriptrun PASS; legs at L+2..L+4 as recorded.
- `tools/check_quest_combat_contract.py`: check_troll_romance passes (pins rewritten to the
  recorded behaviour).

## Open

- The duel's open items (see the Arrg section).
- The Varrock tunnel's east mouth (no recording).
- The sled crash, the full-pack sled loss, the flower seqs off the sled: no recording.
- Journal stages 20, 22, 40, 45: never opened in a recording.
- Equipping the sled over a shield also sent shield_appear (1539) [1454]: engine-generic,
  not checked.
- The shared driver's self-cast settle (above).
- The engine-generic tracking varps above are never sent (not this content).
