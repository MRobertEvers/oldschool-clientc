## seam hero_partner_lures_grip (matthew-mbp-m4-b51-seam2, 2026-10-02) -- killGrip + getCandlestick unblocked

Content (OSRS-Content, uncommitted at time of writing): `quest_hero.rs2` gains two partner affordances beside
`::hero_partner`, documented in `docs/QUEST_SERVER_CHEATS.md` section D:

- `::hero_partner_lure` -- the Black Arm partner's cabinet search (`[label,summon_grip]` case 1): Grip walks to
  2777,3198 (the cabinet room west of the arrow slit), says "Stay out of my drinks cabinet!", 6-tick hold. No damage,
  no door, no item. Gate: Phoenix joined and `%heroquest >= hero_phoenix_talked_charlie`; refuses if Grip is not
  within 12 tiles. Grip's walk is cut off by the 6-tick hold when he has wandered south (spawn 2774,3192), so the row
  below searches again (up to 3 times), exactly as a partner re-searches the open cabinet.
- `::hero_partner_candlestick` -- the partner's trade of one `petecandlestick` (QH getCandlestick). Gate: Phoenix
  joined and `%heroquest == hero_phoenix_killed_grip` (your own kill credit); refuses if you already hold or bank one.

### Fix the committed file's side door first

The committed `goto-sidedoor` goes to 2780,3197, which is INSIDE QH's secretRoom (2780..2782 x 3197..3198,
HeroesQuest.java:258): the goto teleports past the door, and `useKeyOnSideDoor` then walks the player OUT to the garden
(the committed ledger reads `2780,3197,0 -> 2781,3196,0`). Go to the garden side instead; the key then lets you in:

```lua
        t.exec("goto-sidedoor", t.player.goto_tile, 2781, 3196, 0) -- garden side (QH garden2 2780..2786 x 3188..3196)
```

Setup additions (QH `rangedMage` bring-along, HeroesQuest.java:214, carried from talkToAlfonse):

```lua
        "::give magic_shortbow 1", -- QH rangedMage bring-along (HeroesQuest.java:214)
        "::give rune_arrow 100",
        "::setlevel ranged 99",
```

### Rows that replace the `t.blocked` (after `useKeyOnSideDoor`)

```lua
        -- SEAM hero_partner_lures_grip (matthew-mbp-m4-b51-seam2).
        -- QH secretRoom zone = 2780..2782 x 3197..3198 (HeroesQuest.java:258);
        -- the side door (2781,3197, south edge) opens it from the garden side.
        -- ---------------------------------------------------------------
        local room_result, room_tile = t.world.tile()
        local in_room = room_result == "ok" and room_tile ~= nil and room_tile.level == 0
            and room_tile.x >= 2780 and room_tile.x <= 2782 and room_tile.z >= 3197 and room_tile.z <= 3198
        t.check("inSecretRoom", in_room, "t.world.tile() -> " .. tostring(room_result) .. " "
            .. (room_tile and string.format("%s,%s,%s", tostring(room_tile.x), tostring(room_tile.z), tostring(room_tile.level)) or "nil")
            .. " (QH secretRoom 2780..2782 x 3197..3198)")

        -- Stand at the arrow slit (snipable_wall 2780,3198, blockrange=0): a
        -- plain walk inside the room, never a goto.
        local slit_walk = t.player.walk_to(2780, 3198, 10)
        local slit_r, slit_tile = t.world.tile()
        t.check("walkToSlit", slit_r == "ok" and slit_tile.x == 2780 and slit_tile.z == 3198,
            "walk_to(2780,3198) -> " .. tostring(slit_walk) .. ", tile " .. tostring(slit_tile and (slit_tile.x .. "," .. slit_tile.z) or slit_r))

        t.exec("equipShortbow", t.player.equip, "magic_shortbow")
        t.exec("equipArrows", t.player.equip, "rune_arrow")

        -- BEFORE the lure: Grip at his spawn (2774,3192) is behind walls.
        local g0r, g0 = t.npc.nearest("grip", 15)
        t.check("grip.unlured.where", g0r == "ok",
            "npc.nearest(grip,15) -> " .. tostring(g0r) .. " " .. (g0 and (tostring(g0.x) .. "," .. tostring(g0.z)) or "nil"))

        -- The seam, reproduced: with no partner's lure Grip is out of sight
        -- of the slit and the server refuses the swing.
        local u_r, u_d = t.player.attack("grip", 2, 10)
        t.check("killGrip.unlured.refused", u_r == "refused",
            "attack(grip) before any lure -> " .. tostring(u_r) .. " " .. tostring(u_d))
        t.player.walk_to(2780, 3198, 10)

        -- The gate holds before the kill: the partner has no candlestick to
        -- trade until THIS player has killed Grip.
        local pre_r = t.cheat("::hero_partner_candlestick")
        t.ticks(2)
        local pre_c_r, pre_c = t.inv.count("petecandlestick")
        local pre_msg_r, pre_msg = t.msg.expect("HERO_PARTNER_CANDLESTICK FAILED")
        t.check("candlestick.refusedBeforeKill", pre_c_r == "ok" and pre_c == 0 and pre_msg_r == "ok",
            "t.cheat(::hero_partner_candlestick) at stage 4 -> " .. tostring(pre_r) .. ", petecandlestick " .. tostring(pre_c)
            .. ", msg.expect -> " .. tostring(pre_msg_r) .. " " .. tostring(pre_msg))

        -- The partner's lure: ::hero_partner_lure (quest_hero.rs2
        -- [debugproc,hero_partner_lure], mirrors [label,summon_grip] case 1).
        -- One cabinet search moves Grip for the 6-tick hold; a partner whose
        -- Grip has not reached the cabinet room searches again (the cabinet
        -- stays searchable, [oploc1,gripcbopen] -> @summon_grip).
        local lured, g1, lure_tries, lure_trail = false, nil, 0, ""
        for attempt = 1, 3 do
            lure_tries = attempt
            local lure_r, lure_d = t.cheat("::hero_partner_lure")
            local said_r = t.msg.await("Grip lured", 12)
            local g1r
            g1r, g1 = t.npc.nearest("grip", 15)
            lure_trail = lure_trail .. string.format(" #%d cheat=%s done=%s grip=%s", attempt, tostring(lure_r), tostring(said_r),
                g1 and (tostring(g1.x) .. "," .. tostring(g1.z)) or tostring(g1r))
            if g1r == "ok" and g1 ~= nil and g1.z >= 3196 and g1.z <= 3198 and g1.x >= 2770 and g1.x <= 2779 then
                lured = true
                break
            end
        end
        t.check("hero_partner_lure", lured,
            "::hero_partner_lure x" .. lure_tries .. ":" .. lure_trail .. " (want Grip in the cabinet room next to the secret room, 2770..2779 x 3196..3198, walk target 2777,3198)")

        -- killGrip: ranged through the slit, without leaving the room.
        local ga_r, ga_d = t.player.attack("grip", 2, 20)
        t.check("killGrip.attack", ga_r == "ok" or ga_r == "timeout", "attack(grip) -> " .. tostring(ga_r) .. " " .. tostring(ga_d))
        t.exec("killGrip", t.npc.await_dead_engaged, 60)
        t.ticks(3)
        t.expect("quest.stage.phoenix_killed_grip", t.quest.expect_stage("phoenix_killed_grip"))
        local k_r, k_tile = t.world.tile()
        t.check("killGrip.stillInRoom", k_r == "ok" and k_tile.x >= 2780 and k_tile.x <= 2782 and k_tile.z >= 3197 and k_tile.z <= 3198,
            "t.world.tile() after the kill -> " .. tostring(k_tile and (k_tile.x .. "," .. k_tile.z .. "," .. k_tile.level) or k_r))

        -- getCandlestick (HeroesQuest.java:412): the partner's trade.
        local cs_r, cs_d = t.cheat("::hero_partner_candlestick")
        t.check("hero_partner_candlestick.cheat", cs_r == "ok", "t.cheat(::hero_partner_candlestick) -> " .. tostring(cs_r) .. " " .. tostring(cs_d))
        local cw_r, cw_d = t.inv.await("petecandlestick", 1, 10)
        t.check("getCandlestick", cw_r == "ok", "inv.await(petecandlestick, 1, 10) -> " .. tostring(cw_r) .. " " .. tostring(cw_d))

        t.exec("reequipMace", t.player.equip, "rune_mace")
        t.exec("reequipKiteshield", t.player.equip, "rune_kiteshield")
```

Put a verified marker on its own comment line just above the `cs_r, cs_d = t.cheat("::hero_partner_candlestick")` call
(closer check: `helper_coverage.py hero --lua <copy> --ledger build/quest_gate/seam2_hero_full_a/ledger.tsv` printed
`verified PARTNER marker ... (getCandlestick): ::hero_partner_candlestick (QUEST_SERVER_CHEATS.md:441)`, VERDICT FULL 43):

```lua
        -- PARTNER: getCandlestick ::hero_partner_candlestick the partner loots the chest behind garvdoor and trades one (HeroesQuest.java:412)
```

Then continue with the Straven hand-in (`goto-straven-2`, `talkToStraven-2`, `choose:I have a candlestick now.`) and the
rest of the quest. Do NOT pick up Grip's keyring or loot the chest: the keyring drops in Grip's room, which the Phoenix
player cannot enter (garvdoor), and the chest is the partner's.

### Proof

`build/seam_hero_lure/hero.lua` = committed `hero.lua` with the three edits above, then the reverted green file's tail
(9bdd40c4d, from "Straven, second visit" on). Run `seam2_hero_full_a` (`run.py --script ... --no-build --no-publish`),
`gate.py seam2_hero_full_a` -> green, `pass=125 fail=0`, scroll `shots/147-quest.scroll.png`:

```
29	useKeyOnSideDoor	PASS	12	052-useKeyOnSideDoor	use_on(misc_key, pete_sidedoor) -> ok (teleport: 2781,3196,0 -> 2781,3197,0 (moved with no route issued, held 2 tick(s))), t.world.tile() 2781,3196,0 -> 2781,3197,0 (hero_pete_walk_door's own bare p_teleport)
30	inSecretRoom	PASS	0	053-inSecretRoom	t.world.tile() -> ok 2781,3197,0 (QH secretRoom 2780..2782 x 3197..3198)
31	walkToSlit	PASS	0	054-walkToSlit	walk_to(2780,3198) -> ok, tile 2780,3198
32	equipShortbow	PASS	1	055-equipShortbow	equip magic_shortbow: worn 0 -> more
33	equipArrows	PASS	1	056-equipArrows	equip rune_arrow: worn 0 -> more
34	grip.unlured.where	PASS	1	057-grip.unlured.where	npc.nearest(grip,15) -> ok 2774,3189
35	killGrip.unlured.refused	PASS	1	058-killGrip.unlured.refused	attack(grip) before any lure -> refused attack grip op2 [Attack @yel@Grip@gre@ (level-22)] in 1 press(es), pressed slot 90 (element 1073756488) at 2774,3189, watching slot 90: hp no bar -> no bar -- the SERVER refused the swing: 'I can't reach that!' (no route to the copy pressed; the interaction was dropped, so no swing was ever made -- stand where the 
36	candlestick.refusedBeforeKill	PASS	3	059-candlestick.refusedBeforeKill	t.cheat(::hero_partner_candlestick) at stage 4 -> ok, petecandlestick 0, msg.expect -> ok matched: HERO_PARTNER_CANDLESTICK FAILED: your partner only trades you a candlestick after YOU have killed Grip.
37	hero_partner_lure	PASS	13	060-hero_partner_lure	::hero_partner_lure x2: #1 cheat=ok done=ok grip=2777,3195 #2 cheat=ok done=ok grip=2777,3197 (want Grip in the cabinet room next to the secret room, 2770..2779 x 3196..3198, walk target 2777,3198)
38	killGrip.attack	PASS	3	061-killGrip.attack	attack(grip) -> ok attack grip op2 [Attack @yel@Grip@gre@ (level-22)] in 1 press(es), pressed slot 90 (element 1073756488) at 2777,3197, watching slot 90: hp no bar -> 22/30, hitsplat 6
39	killGrip	PASS	8	062-killGrip	await_dead_engaged: slot 90 dead after 8 tick(s), 0 re-engagement(s), last hp 0/30; held Grip (npc 4919) -- corroborated by the ZERO BAR: slot 90 read 0/30 at tick 109 and then left the npc pool (the corpse was released)
40	quest.stage.phoenix_killed_grip	PASS	3		5 -- varp188_heroquest (varp) = 5 (phoenix_killed_grip)
41	killGrip.stillInRoom	PASS	0	063-killGrip.stillInRoom	t.world.tile() after the kill -> 2780,3198,0
42	hero_partner_candlestick.cheat	PASS	0	064-hero_partner_candlestick.cheat	t.cheat(::hero_partner_candlestick) -> ok nil
43	getCandlestick	PASS	0	065-getCandlestick	inv.await(petecandlestick, 1, 10) -> ok petecandlestick 0 -> 1 (>= 1) after 0 tick(s)
48	talkToStraven-2-dialog	PASS	1	070-npc-p1,071-options-p2,072-player-p3,073-npc-p4,074-talkToStraven-2-dialog	4 page(s): npc:Greetings fellow gang member., options:choose:I have a candlestick no, player:I have a candlestick now., npc:Excellent work. Here — a mas
49	quest.stage.phoenix_obtained_armband	PASS	0		6 -- varp188_heroquest (varp) = 6 (phoenix_obtained_armband)
50	straven.armbandGranted	PASS	0	075-straven.armbandGranted	inv.count(master_thief_armband) -> ok 1
107	quest.varp_complete	PASS	3		varp188_heroquest (varp): client=15 server=15 complete=15 [client+server]
108	quest.scroll_title	PASS	0	147-quest.scroll	expected a title containing Heroes' Quest got=You have completed Heroes' Quest! (ok) {name=You have completed Heroes' Quest! points=Total Quest Points: 60}
109	quest.points	PASS	0		qp (varp) 59 -> 60 delta=1 expected=1
110	quest.journal	PASS	4		title=Heroes' Quest (expected any non-empty title) complete=true lines=5 scroll.close=ok journal.close=ok questjournal closed
SUMMARY	125	PASS	378	exit=0	pass=125 fail=0
```

Short run `seam2_hero_lure_c` (`build/seam_hero_lure/hero_short.lua`, same rows ending at the armband): `pass=50 fail=0`.
The first short run (`seam2_hero_lure_a`, single lure) reproduced the seam: lure left Grip at 2777,3194 and
`attack(grip)` answered `refused ... 'I can't reach that!'`.
