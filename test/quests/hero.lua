-- Heroes' Quest -- driven for real through the Phoenix Gang route
-- (Achietties -> Straven -> Alfonse -> Charlie the cook -> kill Grip ->
-- loot the treasure chest -> Straven's candlestick hand-in for the
-- armband), then the two solo-collectible arms (Ice Queen -> ice gloves ->
-- Entrana firebird -> feather; Gerrant -> Blamish oil -> oily rod -> lava
-- eel -> cook it), then the real hand-in to Achietties. Never ::setvar on
-- %heroquest itself past `hero_started` -- every later value is written by
-- the content this file actually clicks/fights through.
--
-- Prerequisites cheated in setup, never the quest's own deliverable (trap
-- 16): 55 QP, Lost City (%zanaris), Dragon Slayer I (%dragonquest),
-- Merlin's Crystal (%arthur) and the Phoenix side of Shield of Arrav
-- (%phoenixgang) are FOUR OTHER quests achietties.rs2's own
-- ~has_hero_quest_requirements gate demands before Heroes' Quest can even
-- be accepted (docs/quests/heroes_quest.md section 2). ::complete has no
-- arm for quest_lostcity/quest_merlinscrystal/quest_dragonslayer1 in this
-- checkout's quest_cheat.rs2 (grepped -- Shield of Arrav's own arm
-- (quest_shieldofarrav) only ever writes %blackarmgang, never %phoenixgang
-- either), so the varps are set directly with ::setvar, the same documented
-- cheat ladder ::give/::setlevel/::setvar/::kill/::spawn/::tele/::goto
-- (QUEST_AUTHORING.md section 3) blackknight.lua already uses for its own
-- %qp prerequisite. phoenixkey2 is the tradeable key Straven's own
-- straven_gangmember label (areas/varrock/scripts/straven.rs2:97-102)
-- hands out on joining the Phoenix Gang -- a Shield-of-Arrav bring-along,
-- not this quest's own deliverable, so it is given rather than earned by
-- replaying that quest.
--
-- Combat/skill LEVELS and plain bring-along tools/food/ingredients are all
-- prerequisites, the same "gear is a prerequisite" idiom hunt.lua/
-- mortton.lua/rovingelves.lua already use for a quest fight -- never the
-- quest's own deliverable, which HeroesQuest.java's own ItemRequirement set
-- confirms: only `iceGloves` carries `.canBeObtainedDuringQuest()` (grepped
-- /Users/matthewevers/Documents/git_repos/quest-helper/.../HeroesQuest.java)
-- -- fishingRod, fishingBait, harralanderUnf (harralandervial, the
-- unfinished potion Blamish Slime mixes into) and pickaxe are NOT marked
-- that way, so they are given directly. blamish_snail_slime, blamish_oil,
-- oily_fishing_rod, raw_lava_eel, lava_eel, hot_feather, master_thief_armband,
-- petecandlestick and grip_keys ARE the quest's own deliverables and are
-- driven for real below.
--
-- The candlestick-chest content bug this file used to stop at
-- (brimhaven_scarface_mansion.rs2's opencandlechest write clobbering a
-- Phoenix player's %varp188_heroquest with the Black Arm route's own checkpoint,
-- past hero_phoenix_obtained_armband) is FIXED as of the committed source
-- (brimhaven_scarface_mansion.rs2:134, gated on
-- `%heroquest >= ^hero_blackarm_gangmember_spoken`, which a Phoenix player
-- sitting at hero_phoenix_killed_grip(5) never satisfies) -- confirmed
-- below by reading %heroquest right after the loot and asserting it is
-- STILL hero_phoenix_killed_grip, then driving on to Straven for the
-- armband for real.

return {
    id = "hero",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots
        "::setvar varp101_qp 55", -- prerequisite quest points (hero_required_questpoints), not this quest's own reward
        "::setvar varp147_zanaris 6", -- Lost City complete (zanaris_complete) -- no ::complete arm for quest_lostcity exists
        "::setvar varp176_dragonquest 10", -- Dragon Slayer I complete (dragon_complete) -- no ::complete arm for quest_dragonslayer1 exists
        "::setvar varp14_arthur 7", -- Merlin's Crystal complete (arthur_complete) -- no ::complete arm for quest_merlinscrystal exists
        "::setvar varp145_phoenixgang 10", -- Shield of Arrav, Phoenix side, complete (phoenixgang_complete) -- ::complete quest_shieldofarrav only ever writes %blackarmgang
        "::complete quest_druidicritual", -- Druidic Ritual, the DBROW name (all.dbrow.compack:35), not the quest_druid folder name (QUEST_AUTHORING.md docs/quests notes) -- ~herblore_unlocked (quest_druid.rs2:35-39) gates ~attempt_brew_potion on %druidquest >= ^druid_complete, and Heroes' Quest's own Blamish-oil mix (brew_potion.rs2:513-516) needs Herblore unlocked; this is a bring-along prerequisite (trap 16), not the quest's own deliverable
        "::give phoenixkey2 1", -- Shield-of-Arrav bring-along Straven's own script hands a joined Phoenix member, not Heroes' Quest's own deliverable
        -- Combat levels: prerequisite for BOTH real fights below (Grip,
        -- level 22; Ice Queen, level 111, hp104/atk95/str94/def95,
        -- combat_stats.generated.npc:14496-14516) -- same idiom
        -- rovingelves.lua uses for its own level-111-class moss guardian.
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        "::give rune_mace 1", -- crush weapon: Ice Queen's own lowest defence stat is crushdefence=20 (combat_stats.generated.npc:14515), vs slashdefence=40/stabdefence=30 -- dragon_mace is unusable here, levelrequire.rs2:171-176 refuses to Wear it until %varp188_heroquest >= ^hero_complete, which is this quest's OWN completion; rune_mace carries no such gate
        "::give rune_platebody 1",
        "::give rune_platelegs 1",
        "::give rune_full_helm 1",
        "::give rune_kiteshield 1",
        "::give shark 8", -- food for the Ice Queen fight, same idiom as mortton.lua/rovingelves.lua's own "::give shark" food
        -- Fire-arm + lava-eel-arm bring-alongs -- none of these carry
        -- .canBeObtainedDuringQuest() in HeroesQuest.java, unlike iceGloves.
        "::give fishing_rod 1",
        "::give fishing_bait 20",
        "::give harralandervial 1", -- "Harralander potion (unf)" -- the solvent brew.dbrow's herblore_blamish_oil row names
        "::give logs 3",
        "::give tinderbox 1",
        "::give rune_pickaxe 1", -- White Wolf Mountain rockslide fallback (mine_ice_queen_lair_rockslide, white_wolf_mountain.rs2:23-32) if the ice queen's own tile answers screen_position through goto_tile
        "::setlevel mining 50", -- rockslide fallback's own gate (white_wolf_mountain.rs2:29)
        "::setlevel fishing 99", -- lava eel fishing gate is level 53 (lavafish.rs2:90)
        "::setlevel cooking 99", -- lava eel cooking gate is level 53 (cooking_generic.dbrow's cooking_generic_lava_eel row, never burns)
        "::setlevel herblore 99", -- Blamish oil mixing gate is level 25 (brew.dbrow's herblore_blamish_oil row)
        "::setlevel firemaking 99", -- lighting the cooking fire is a roll (stat_random(firemaking,64,512), firemaking.rs2:80) -- near-certain first swing at 99, never a level GATE (logs need only level 1)
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varp188_heroquest",
            constants = {
                not_started = 0,
                started = 1,
                phoenix_gangmember_spoken = 2,
                phoenix_talked_alfonse = 3,
                phoenix_talked_charlie = 4,
                phoenix_killed_grip = 5,
                phoenix_obtained_armband = 6,
                blackarm_gangmember_spoken = 7,
                blackarm_hq_door_unlocked = 8,
                blackarm_id_papers_obtained = 9,
                blackarm_mansion_unlocked = 10,
                blackarm_id_papers_given = 11,
                blackarm_looted_chest = 12,
                blackarm_obtained_armband = 13,
                complete = 15,
            },
            row = "quest_heroes", -- docs/quests/heroes_quest.md section 2: "Cache row | quest_heroes"
            display = "Heroes' Quest",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)

        t.ticks(3) -- the ::setvar/::give cheats above are not client-side yet
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- Equip the fight gear now, before anything else -- frees five
        -- backpack slots the fishing-bait/harralander/logs stack still needs,
        -- and both real fights below want it worn from the first swing.
        t.exec("equipMace", t.player.equip, "rune_mace")
        t.exec("equipPlatebody", t.player.equip, "rune_platebody")
        t.exec("equipPlatelegs", t.player.equip, "rune_platelegs")
        t.exec("equipFullHelm", t.player.equip, "rune_full_helm")
        t.exec("equipKiteshield", t.player.equip, "rune_kiteshield")

        -- ---------------------------------------------------------------
        -- Achietties: accept the quest for real.
        -- ---------------------------------------------------------------
        t.exec("goto-achietties", t.player.goto_tile, 2903, 3510, 0)
        t.exec("talkToAchietties", t.player.talk_to, "achietties")
        t.exec("talkToAchietties-dialog", t.chat.play, {
            "npc:Greetings. Welcome to the Heroes",
            "npc:Only the greatest heroes of this land",
            "choose:I'm a hero, may I apply to join?",
            "player:I'm a hero. May I apply to join?",
            "npc:Well, you have a lot of quest points",
            "npc:for entrance are:",
            "choose:I'll start looking for all those things then.",
            "player:I'll start looking for all those things then.",
            "npc:Good luck with that.",
        })
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- ---------------------------------------------------------------
        -- Straven: join the Phoenix side of the armband chain.
        -- ---------------------------------------------------------------
        t.exec("goto-straven", t.player.goto_tile, 3246, 9780, 0)
        t.exec("talkToStraven-1", t.player.talk_to, "straven")
        t.exec("talkToStraven-1-dialog", t.chat.play, {
            "npc:Greetings fellow gang member.",
            "choose:Is there any way I can get the rank of master thief?",
            "player:Is there any way I can get the rank of master thief?",
            "npc:As it happens, yes. Head to our restaurant front in Brimhaven",
        })
        t.expect("quest.stage.phoenix_gangmember_spoken", t.quest.expect_stage("phoenix_gangmember_spoken"))

        -- ---------------------------------------------------------------
        -- Alfonse: the gherkin password.
        -- ---------------------------------------------------------------
        t.exec("goto-alfonse", t.player.goto_tile, 2793, 3188, 0)
        t.exec("talkToAlfonse", t.player.talk_to, "alfonse_the_waiter")
        t.exec("talkToAlfonse-dialog", t.chat.play, {
            "npc:Welcome to the Shrimp and Parrot.",
            "choose:Do you sell Gherkins?",
            "player:Do you sell Gherkins?",
            "npc:Hmmmm Gherkins eh? Ask Charlie the cook",
            "mesbox:Alfonse winks at you.",
        })
        t.expect("quest.stage.phoenix_talked_alfonse", t.quest.expect_stage("phoenix_talked_alfonse"))

        -- ---------------------------------------------------------------
        -- Into the kitchen (herokitchendoor opens once heroquest >=
        -- phoenix_talked_alfonse & %phoenixgang = phoenixgang_complete,
        -- brimhaven_restaurant.rs2:8-13).
        -- ---------------------------------------------------------------
        t.exec("openKitchenDoor", t.player.click_loc, "herokitchendoor")

        -- ---------------------------------------------------------------
        -- Charlie the cook: the secret door into Mr Olbors' garden.
        -- ---------------------------------------------------------------
        t.exec("talkToCharlie", t.player.talk_to, "charlie_the_cook")
        t.exec("talkToCharlie-dialog", t.chat.play, {
            "npc:Hey! What are you doing back here?",
            "choose:I'm looking for a gherkin...",
            "player:I'm looking for a gherkin...",
            "npc:Aaaaaah... a fellow Phoenix! So, tell me compadre",
            "choose:I want to steal Scarface Pete's candlesticks.",
            "player:I want to steal Scarface Pete's candlesticks.",
            "npc:Ah yes, of course. The candlesticks.",
            "npc:a little assistance. The setting up of this restaurant",
            "npc:Now, at the other side of Mr Olbors",
            "npc:and we can't seem to find a way through.",
            "player:Mind if I check it out for myself?",
            "npc:Not at all! The more minds we have",
        })
        t.expect("quest.stage.phoenix_talked_charlie", t.quest.expect_stage("phoenix_talked_charlie"))

        -- ---------------------------------------------------------------
        -- Into Mr Olbors' garden (herokitchenpanel writes no state, plain
        -- p_teleport walk-through, brimhaven_restaurant.rs2:16-22).
        -- ---------------------------------------------------------------
        t.exec("openKitchenPanel", t.player.click_loc, "herokitchenpanel")
        t.ticks(2) -- let the teleport settle

        -- ---------------------------------------------------------------
        -- pete_sidedoor: [oploc1] now ALWAYS refuses with "This door is
        -- locked." (2026-09-23 content parity, c8b3e2fede) -- only
        -- [oplocu,pete_sidedoor] with misc_key opens it
        -- (brimhaven_scarface_mansion.rs2:12-58). misc_key is the real
        -- two-player Black-Arm-partner hand-off (Grip's own dialogue,
        -- grip.rs2:50-58) -- this server drives one account, so
        -- ::hero_partner is the documented TEST AFFORDANCE standing in for
        -- exactly that partner action (docs/QUEST_SERVER_CHEATS.md section
        -- D, quest_hero.rs2:266-307), gated the same way the real hand-off
        -- would be (%phoenixgang >= phoenixgang_joined, %heroquest >=
        -- hero_phoenix_talked_charlie -- both already true here). Never a
        -- silent grant; the real click through the door is still driven.
        -- ---------------------------------------------------------------
        local partner_result, partner_detail = t.cheat("::hero_partner")
        t.step("hero_partner.cheat", partner_result == "ok" and "PASS" or "FAIL",
            "t.cheat(::hero_partner) -> " .. tostring(partner_result) .. " " .. tostring(partner_detail))
        -- The cheat's chat reply outruns its own inv_add by up to a tick
        -- (the same shape trap 23 documents for a setup ::give) -- poll,
        -- never a bare count read on the line right after.
        local misckey_result, misckey_detail = t.inv.await("misc_key", 1, 10)
        t.check("misc_key.granted", misckey_result == "ok",
            "inv.await(misc_key, 1, 10) -> " .. tostring(misckey_result) .. " " .. tostring(misckey_detail))

        -- pete_sidedoor is a single placement (m43_49.jl2, id 2622, rot 3)
        -- at 2781,3197,0 -- [oploc1]'s own coordz(coord)=coordz(loc_coord)
        -- test says it is approached along its z-row, and a bare goto onto
        -- its own square lands one tile short at 2780,3197 (the wall side
        -- is not walkable) -- the west approach herokitchenpanel does not
        -- already stand us on. Position there explicitly rather than
        -- relying on use_on's own approach-tile hunt from wherever the
        -- panel walk-through left us (measured: every hunted side from
        -- there answered "I can't reach that!").
        t.exec("goto-sidedoor", t.player.goto_tile, 2780, 3197, 0)

        -- trap 298: use_on's backpack-tab press is not settled before its
        -- own arming, so an arm issued right after another action can
        -- silently fail and the click degrades to a bare "Walk here".
        t.check("sideDoor.tabInventory", t.ui.tab("inventory") == "ok", "t.ui.tab(inventory)")
        t.ticks(2)
        -- [oplocu,pete_sidedoor]'s own success path is a BARE
        -- ~hero_pete_walk_door -> p_teleport, no chat line and no mesbox
        -- (same shape section 3's own banner documents for a silent
        -- [oploc<n>] teleport door) -- _settle_after_click has nothing to
        -- resolve on, so this is graded on the tile the walk-door proc
        -- actually moved the player to, not on the click verb's own
        -- settle word (measured: the press lands and hunts a pixel every
        -- time, but the settle reads a bare, uncategorised timeout).
        local predoor_result, predoor_tile = t.world.tile()
        local sidedoor_target = t.player.by_symbol("loc", "pete_sidedoor")
        local sidedoor_click_result, sidedoor_click_detail = t.player.use_on("misc_key", sidedoor_target)
        t.ticks(2)
        local postdoor_result, postdoor_tile = t.world.tile()
        local sidedoor_moved = predoor_result == "ok" and postdoor_result == "ok"
            and predoor_tile ~= nil and postdoor_tile ~= nil
            and (predoor_tile.x ~= postdoor_tile.x or predoor_tile.z ~= postdoor_tile.z)
        local predoor_str = (predoor_result == "ok" and predoor_tile ~= nil)
            and string.format("%s,%s,%s", tostring(predoor_tile.x), tostring(predoor_tile.z), tostring(predoor_tile.level))
            or tostring(predoor_result)
        local postdoor_str = (postdoor_result == "ok" and postdoor_tile ~= nil)
            and string.format("%s,%s,%s", tostring(postdoor_tile.x), tostring(postdoor_tile.z), tostring(postdoor_tile.level))
            or tostring(postdoor_result)
        t.check("useKeyOnSideDoor", sidedoor_moved,
            "use_on(misc_key, pete_sidedoor) -> " .. tostring(sidedoor_click_result) .. " (" .. tostring(sidedoor_click_detail)
                .. "), t.world.tile() " .. predoor_str .. " -> " .. postdoor_str .. " (hero_pete_walk_door's own bare p_teleport)")

        -- killGrip (HeroesQuest.java:411): a PARTNER lures Grip into the room next
        -- to the secret room (gripcbshut, brimhaven_scarface_mansion.rs2:11-30,
        -- needs a pirate_guard within 4 of the PLAYER at the cabinet, 2776,3196)
        -- and the Phoenix player snipes him through snipable_wall 2780,3198.
        -- A solo Phoenix player cannot lure: garvdoor refuses without
        -- %blackarmgang complete + hero_blackarm_mansion_unlocked (garv.rs2:40-60)
        -- and the cabinet is >4 tiles from the secret room. No ::hero_partner-style
        -- lure affordance exists (QUEST_SERVER_CHEATS.md lists only misc_key).
        t.blocked("content_bug: killGrip needs a partner to lure Grip (gripcbshut, brimhaven_scarface_mansion.rs2:11-30) into the room next to the secret room; no solo lure exists (garv.rs2:40 gates the mansion to the Black Arm route; QUEST_SERVER_CHEATS.md has only ::hero_partner for misc_key), and goto out of the sealed room is a cheat")
        return
    end,
}
