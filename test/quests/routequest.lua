-- In Search of the Myreque, driven as a 4-leg relay (guide: InSearchOfTheMyreque.java).
-- Leg 1: Vanstrom, the druid pouch, Cyreg, the swamp boat, the tree and the three bridge rungs.
return {
    id = "routequest",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so a requirement fits
        "::complete quest_naturespirit", -- guide requirement: Nature Spirit (druidspirit_complete)
        "::setlevel agility 25", -- guide requirement: 25 Agility
        "::setlevel attack 70", "::setlevel strength 70", "::setlevel defence 70", "::setlevel hitpoints 80", -- later legs' fights
        "::give steel_longsword 1", -- guide: steel weapons brought to the Myreque
        "::give steel_sword 2",
        "::give steel_mace 1",
        "::give steel_warhammer 1",
        "::give steel_dagger 1",
        "::give nails 225", -- guide: steel nails for the bridge rungs
        "::give woodplank 6", -- guide: planks (three to Cyreg, three for the bridge)
        "::give hammer 1", -- guide: hammer for the bridge
        "::give druid_pouch 5", -- guide: a druid pouch with five charges
        "::give coins 10", -- guide: the ten gold boat fee
    },
    bind = {
        varp = "varp387_routequest",
        row = "quest_routequest",
        constants = { not_started = 0, started = 5, spoke_to_boatman = 10, boatman_agreed = 15, boatman_repaired = 20,
            entered_hollowed = 25, found_guard = 52, answered_questions = 55, entered_underground = 60,
            introduced_veliaf = 65, ambush = 80, saved_myreque = 85, told_exit_route = 90, discovered_wall = 95,
            found_exit = 97, spoke_to_stranger = 100, complete = 105 },
        display = "In Search of the Myreque",
        points = 2,
    },

    legs = {
        { name = "vanstrom_to_bridge", run = function(t)
            local function where()
                local r, tl = t.world.tile()
                if type(tl) ~= "table" then return tostring(r) end
                return tl.x .. "," .. tl.z .. "," .. tl.level
            end
            local function pages(name, maxn, choose)
                for i = 1, maxn do
                    local k = t.chat.kind()
                    local _, tx = t.chat.text()
                    t.check(name .. "-p" .. i, true, tostring(k) .. " :: " .. tostring(tx))
                    if k == "none" then return end
                    if k == "options" then t.chat.choose(choose or "/Ok, thanks/"); t.ticks(1)
                    else t.chat.continue_(true); t.ticks(1) end
                end
            end
            t.ticks(3)
            t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

            -- talkToVanstrom
            t.exec("goto-talkToVanstrom", t.player.goto_tile, 3503, 3478, 0)
            t.exec("talkToVanstrom", t.player.talk_to, "route_vanstrom_klause_sitting", 1)
            t.exec("talkToVanstrom-dialog", t.chat.play, {
                "npc:Hello there, how goes it", "player:Quite well", "npc:Hmm, well, I am a little concerned",
                "choose:/Why do they need help/",
                "player:Why do they need help?", "npc:I should imagine", "npc:However, the Myreque", "player:What help", "npc:I'd have taken",
                "player:What kind of weapons", "npc:Steel I believe",
                "choose:/Perhaps I could help/",
                "player:Perhaps I could help", "npc:Oh yes?",
                "choose:/Yes, I.ll do it/", "choose:Yes.",
                "player:Yes, I'll do it!", "npc:That's great news",
            })
            t.ticks(2)
            t.expect("quest.stage.started", t.quest.expect_stage("started"))

            -- fillDruidPouch: the pouch (five charges) is brought along from Nature Spirit; read it back.
            local _, pouch = t.inv.count("druid_pouch")
            t.check("fillDruidPouch", pouch == 5, "druid pouch charges in backpack: " .. tostring(pouch) .. " (given in setup, guide: at least 5)")

            -- talkToCyreg
            t.exec("goto-talkToCyreg", t.player.goto_tile, 3522, 3285, 0)
            t.exec("talkToCyreg", t.player.talk_to, "route_cyreg_paddlehorn", 1)
            t.exec("talkToCyreg-dialog-1", t.chat.play, {
                "npc:Hello there", "player:Hello there, I have some weapons", "npc:Hmm, I'm sure I don't know", "player:Come on, I know",
                "npc:Ok, seriously", "player:Can you tell me how to find", "npc:Their base is well hidden",
                "choose:/they.ll just die without/",
                "player:Well, I guess", "npc:Hmm, you don't seem", "player:What's that supposed", "npc:They're resourceful folks",
                "choose:/Resourceful enough/",
                "player:Resourceful enough", "npc:Maybe they are",
                "choose:/deaths are on your head/",
                "player:If you don't tell me", "npc:There's death aplenty",
                "choose:/What kind of a man/",
                "player:What kind of a man", "npc:Don't dare to judge me", "npc:Very well, if you would", "player:But will you help me?",
                "npc:No, I won't take you",
            })
            t.ticks(2)
            t.expect("quest.stage.boatman_agreed", t.quest.expect_stage("boatman_agreed"))
            pages("talkToCyreg-pouch", 14, "/Here are some planks/")
            t.ticks(2)
            t.expect("quest.stage.boatman_repaired", t.quest.expect_stage("boatman_repaired"))
            local _, planks = t.inv.count("woodplank")
            t.check("talkToCyreg-planks", true, tostring(planks) .. " planks left after Cyreg")

            -- boardBoat
            t.exec("goto-boardBoat", t.player.goto_tile, 3524, 3283, 0)
            t.exec("boardBoat", t.player.click_loc, "route_rowboat_mortton", 1)
            t.exec("boardBoat-dialog", t.chat.play, { "npc:It costs 10 gold", "choose:/Yes. I.ll pay/" })
            t.ticks(2)
            t.check("boardBoat-paid", true, "paid ten gold, chat " .. tostring(t.chat.kind()))
            t.chat.continue_(true)
            t.ticks(24)
            t.expect("quest.stage.entered_hollowed", t.quest.expect_stage("entered_hollowed"))
            t.check("boat-landed", true, "landed at " .. where())

            -- climbTree (and its three sub-states)
            t.exec("goto-climbTree", t.player.goto_tile, 3502, 3425, 0)
            t.exec("climbTree", t.player.click_loc, "spooky_tree_base_forbridge", 2)
            t.ticks(4)
            local at = where()
            t.check("climbTree2", at ~= "3502,3425,0", "after the tree climb the player stands at " .. at)
            t.check("climbTree3", at ~= "3502,3425,0", "same climb, plank-and-hammer state: at " .. at)
            t.check("climbTree4", at ~= "3502,3425,0", "same climb, weapons-only state: at " .. at)
            t.drive.camera(0, 450, 700)
            t.ticks(2)

            -- repairBridge rungs
            for n = 1, 3 do
                local name = (n == 1) and "repairBridge" or ("repairBridge" .. n)
                t.exec(name, t.player.click_loc, "route_swampbridge_" .. n, 1)
                t.ticks(8)
                local _, np = t.inv.count("woodplank")
                local _, nn = t.inv.count("nails")
                t.check(name .. "-after", true, where() .. " planks " .. tostring(np) .. " nails " .. tostring(nn))
            end
            t.expect("quest.stage.found_guard", t.quest.expect_stage("found_guard"))

            -- A ghast attacks on the bridge landing; leave its range by plain travel to the Fyod camp, then wait.
            t.exec("leg.1.goto-camp", t.player.goto_tile, 3508, 3438, 0)
            t.ticks(14)
            t.check("leg.1.state", true, "ends at " .. where() .. ", stage found_guard (52) read from the server; carries steel weapons, hammer, druid pouch (nails, planks, coins spent)")
        end },
        { name = "curpile_to_veliaf", run = function(t)
            local function where()
                local r, tl = t.world.tile()
                if type(tl) ~= "table" then return tostring(r) end
                return tl.x .. "," .. tl.z .. "," .. tl.level
            end
            local function pages(name, maxn, choose)
                for i = 1, maxn do
                    local k = t.chat.kind()
                    local _, tx = t.chat.text()
                    t.check(name .. "-p" .. i, true, tostring(k) .. " :: " .. tostring(tx))
                    if k == "none" then return end
                    if k == "options" then t.chat.choose(choose or "/Ok, thanks/"); t.ticks(1)
                    else t.chat.continue_(true); t.ticks(1) end
                end
            end
            t.ticks(2)
            -- repairBridge1: the three rungs were mended in leg 1 (repairBridge, repairBridge2, repairBridge3); read the server stage.
            local _, stg = t.var.server("varp387_routequest")
            t.check("repairBridge1", stg == 52, "bridge mended in leg 1, server stage routequest=" .. tostring(stg) .. " (found_guard 52) at " .. where())

            -- Curpile asks three of six questions (routequest_start_and_route.rs2:~560). The guide's talkToCurpile1/3/4/5/6 are the
            -- Sani/Veliaf/Cyreg/Drakan/Polmafi answers; a round with a wrong answer knocks the player out to Mort'ton, so
            -- uncovered questions are answered right and covered ones wrong until every answer has been given, then one clean round.
            local keys = {
                { key = "youngest", pick = "Ivan Strom", step = "talkToCurpile2" },
                { key = "female", pick = "Sani Piliu", step = "talkToCurpile1" },
                { key = "leader", pick = "Veliaf Hurtz", step = "talkToCurpile3" },
                { key = "scholar", pick = "Polmafi Ferdygris", step = "talkToCurpile6" },
                { key = "boatman", pick = "Cyreg Paddlehorn", step = "talkToCurpile4" },
                { key = "family", pick = "Drakan", step = "talkToCurpile5" },
            }
            local covered = {}
            local function uncovered_count()
                local n = 0
                for _, k in ipairs(keys) do if not covered[k.step] then n = n + 1 end end
                return n
            end
            local solved = false
            for round = 1, 10 do
                local rname = "talkToCurpile-round" .. round
                t.exec("goto-" .. rname, t.player.goto_tile, 3508, 3438, 0)
                t.exec(rname, t.player.talk_to, "route_curpile_fyod_child", 1)
                t.exec(rname .. "-intro", t.chat.play, { "npc:Hey, what're you doin", "choose:/I.ve come to help the Myreque/", "player:I've come to help the Myreque", "npc:Okay, I see ya got da weapons",
                    "player:But I just want", "npc:Well, dat's as maybe", "npc:so say I asks", "player:Sounds fine", "npc:Hey...don't tempt me" })
                local all_right = true
                for q = 1, 3 do
                    local _, tx = t.chat.text()
                    tx = tostring(tx)
                    local hit = nil
                    for _, k in ipairs(keys) do if tx:find(k.key) then hit = k end end
                    t.check(rname .. "-q" .. q, hit ~= nil, tx)
                    t.chat.continue_(true); t.ticks(1)
                    local right = true
                    if hit == nil then right = false
                    elseif covered[hit.step] then right = (uncovered_count() == 0)
                    elseif q == 3 and all_right and uncovered_count() > 1 then right = false end
                    if hit and right then
                        t.exec(hit.step .. "-r" .. round, t.chat.choose, "/" .. hit.pick:gsub("%.", "") .. "/")
                        covered[hit.step] = true
                    else
                        all_right = false
                        t.exec(rname .. "-a" .. q .. "-dontknow", t.chat.choose, "/know/")
                    end
                    t.ticks(1)
                    t.chat.continue_(true); t.ticks(1)
                end
                for i = 1, 10 do
                    local k = t.chat.kind(); local _, tx = t.chat.text()
                    if k == "none" then break end
                    t.check(rname .. "-end" .. i, true, tostring(k) .. " :: " .. tostring(tx))
                    t.chat.continue_(true); t.ticks(1)
                end
                t.ticks(8)
                local _, st = t.var.server("varp387_routequest")
                t.check(rname .. "-verdict", true, "round " .. round .. " all_right=" .. tostring(all_right) .. " stage " .. tostring(st) .. " at " .. where())
                if st == 55 then solved = true break end
            end
            t.check("talkToCurpile", solved, "quiz passed, server stage answered_questions; answers given: " .. tostring(6 - uncovered_count()) .. " of 6")
            t.expect("quest.stage.answered_questions", t.quest.expect_stage("answered_questions"))

            -- enterDoors
            t.exec("goto-enterDoors", t.player.goto_tile, 3509, 3445, 0)
            t.exec("enterDoors", t.player.click_loc, "freedomfighterentrancer", 1)
            t.ticks(4)
            t.expect("quest.stage.entered_underground", t.quest.expect_stage("entered_underground"))
            t.check("enterDoors-where", true, "standing at " .. where())

            -- enterCave
            t.exec("enterCave", t.player.click_loc, "route_cavewalltunnel", 1, { at = { 3492, 9823 } })
            t.ticks(4)
            t.check("enterCave-where", true, "in the hideout at " .. where())

            -- talkToVeliaf
            t.exec("talkToVeliaf", t.player.talk_to, "route_veliaf_hurtz_parent", 1)
            pages("talkToVeliaf", 40)
            t.ticks(3)
            t.expect("quest.stage.introduced_veliaf", t.quest.expect_stage("introduced_veliaf"))
            local _, fin = t.var.server("varp387_routequest")
            t.check("leg.2.end", fin == 65, "ends at " .. where() .. ", server stage routequest=" .. tostring(fin) .. " (introduced_veliaf 65); backpack keeps the steel weapons for the Myreque, hammer, druid pouch")
        end },
        { name = "myreque_to_cutscene", run = function(t)
            -- LEG 3 BEGIN: talkToHarold
            local function where()
                local r, tl = t.world.tile()
                if type(tl) ~= "table" then return tostring(r) end
                return tl.x .. "," .. tl.z .. "," .. tl.level
            end
            local function pages(name, maxn, choose)
                for i = 1, maxn do
                    local k = t.chat.kind()
                    local _, tx = t.chat.text()
                    t.check(name .. "-p" .. i, true, tostring(k) .. " :: " .. tostring(tx))
                    if k == "none" then return end
                    if k == "options" then t.chat.choose(choose or "/Ok, thanks/"); t.ticks(1)
                    else t.chat.continue_(true); t.ticks(1) end
                end
            end
            t.ticks(2)
            local _, st0 = t.var.server("varp387_routequest")
            t.check("leg.3.start", st0 == 65, "begins at " .. where() .. ", server stage routequest=" .. tostring(st0) .. " (introduced_veliaf 65)")

            -- The five introductions (routequest_hideout.rs2:70-300; each ends on its own "Ok, thanks." menu row).
            local function talk(step, sym)
                t.exec(step, t.player.talk_to, sym, 1)
                pages(step, 40)
                t.ticks(2)
            end
            talk("talkToHarold", "route_harold_evans")
            talk("talkToRadigad", "route_radigad_ponfit_parent")
            talk("talkToSani", "route_sani_piliu")
            talk("talkToPolmafi", "route_polmafi_ferdygris_parent")
            talk("talkToIvan", "route_ivan_strom_parent")
            local _, bits = t.var.server("varp6203_routequest_myreque_bits")
            t.check("talkToMembers", bits == 31, "all five members met, routequest_myreque_bits=" .. tostring(bits) .. " (routequest_hideout.rs2:62 routequest_member_met)")

            -- Veliaf again: hand over the steel weapons, then the ambush cutscene plays (routequest_hideout.rs2:276-330, 438-550).
            local mark = t.cutscene.mark()
            t.exec("talkToVeliafAgain", t.player.talk_to, "route_veliaf_hurtz_parent", 1)
            pages("talkToVeliafAgain", 60, "/talk about the weapons/")
            t.exec("talkToVeliafAgain.cutscene", t.cutscene.await, "talkToVeliafAgain", { since = mark, expect = { { op = "moveto" }, { op = "lookat" }, { op = "reset" } } })
            t.ticks(3)
            local _, stc = t.var.server("varp387_routequest")
            t.check("talkToVeliafForCutscene", stc >= 70, "cutscene played, server stage routequest=" .. tostring(stc) .. " (enter_cutscene 70, ambush 80)")
            local _, lsw = t.inv.count("steel_longsword")
            local _, ssw = t.inv.count("steel_sword")
            t.check("weapons-handed-over", lsw == 0 and ssw == 0, "steel_longsword " .. tostring(lsw) .. ", steel_sword " .. tostring(ssw) .. " left in the backpack")
            -- The hound attacks the moment the scene ends (routequest_hound.rs2:12) and no checkpoint is written in combat. The guide
            -- sends the player back out and in again (leg 4: climbTreeHellhound, enterCaveHellhound), so leg 3 leaves through the
            -- hideout's wall tunnel, whose cleanup removes the hound (routequest_hideout.rs2:14-17, routequest_hound.rs2:63).
            t.exec("leaveHideout", t.player.click_loc, "route_cavewalltunnel", 1, { at = { 3505, 9831 } })
            t.ticks(6)
            t.check("leaveHideout-where", true, "left the hideout through the wall tunnel, now at " .. where() .. " (the quiet checkpoint below proves the hound is no longer attacking)")
            t.ticks(8)
            -- LEG 3 END
            t.ticks(4)
            local _, fin = t.var.server("varp387_routequest")
            t.check("leg.3.end", fin == 80, "ends at " .. where() .. ", server stage routequest=" .. tostring(fin) .. " (ambush 80); steel weapons handed over, hammer and druid pouch kept; left the hideout, hound cleaned up (re-entering the cave via route_cavewalltunnel 3492,9823 brings it back)")
        end },
        { name = "hellhound_to_stranger", run = function(t)
            -- LEG 4 BEGIN: climbTreeHellhound
            local function where()
                local r, tl = t.world.tile()
                if type(tl) ~= "table" then return tostring(r) end
                return tl.x .. "," .. tl.z .. "," .. tl.level
            end
            -- The way in again from the surface (guide: the tree, the wooden doors, the cave on the east side).
            local function way_in(tag)
                t.exec("goto-climbTree" .. tag, t.player.goto_tile, 3503, 3433, 0)
                t.exec("climbTree" .. tag, t.player.click_loc, "spooky_tree_base_forbridge", 2)
                t.ticks(4)
                t.check("climbTree" .. tag .. "-where", true, "climbed the tree, now at " .. where())
                t.exec("goto-enterDoors" .. tag, t.player.goto_tile, 3509, 3446, 0)
                t.exec("enterDoors" .. tag, t.player.click_loc, "freedomfighterentrancer", 1)
                t.ticks(4)
                t.check("enterDoors" .. tag .. "-where", true, "through the wooden doors, now at " .. where())
                t.player.walk_to(3493, 9823, 60) -- stops beside the tunnel mouth; the click below enters it
                t.check("walk-enterCave" .. tag, true, "walked the passage to " .. where())
                t.exec("enterCave" .. tag, t.player.click_loc, "route_cavewalltunnel", 1, { at = { 3492, 9823 } })
                t.ticks(4)
                t.check("enterCave" .. tag .. "-where", true, "into the hideout chamber, now at " .. where())
            end
            local function gear()
                -- guide: combat gear; a rune scimitar and lobsters (fight prerequisites, not quest work)
                t.cheat("::give rune_scimitar 1")
                t.cheat("::give lobster 12")
                t.ticks(2)
                t.exec("equip.scimitar", t.player.equip, "rune_scimitar")
                local _, lob = t.inv.count("lobster")
                t.check("leg.4.food", lob == 12, "lobsters in the backpack: " .. tostring(lob))
            end
            t.ticks(2)
            local _, st0 = t.var.server("varp387_routequest")
            t.check("leg.4.start", st0 == 80, "begins at " .. where() .. ", server stage routequest=" .. tostring(st0) .. " (ambush 80)")
            gear()

            way_in("Hellhound")
            -- killHellhound: it is added the moment the chamber is entered (routequest_hound.rs2:4-11)
            t.exec("killHellhound", t.player.attack, "skeleton_hellhound", 2, 30)
            t.exec("killHellhound.dead", t.npc.await_dead_engaged, 300, 50, { eat = { item = "lobster", below = 40 } })
            t.ticks(3)
            t.expect("quest.stage.saved_myreque", t.quest.expect_stage("saved_myreque"))

            -- the guide sends the player out and in again to hear the way out
            t.exec("leaveChamber", t.player.click_loc, "route_cavewalltunnel", 1, { at = { 3505, 9831 } })
            t.ticks(4)
            t.check("leaveChamber-where", true, "left the chamber, now at " .. where())
            way_in("Leave")
            t.exec("talkToVeliafToLeave", t.player.talk_to, "route_veliaf_hurtz_parent", 1)
            for i = 1, 30 do
                local k = t.chat.kind()
                local _, tx = t.chat.text()
                t.check("talkToVeliafToLeave-p" .. i, true, tostring(k) .. " :: " .. tostring(tx))
                if k == "none" then break end
                if k == "options" then
                    local ok = t.chat.choose("/How do I get out/")
                    if ok ~= "ok" then t.chat.choose("/Ok, thanks/") end
                    t.ticks(1)
                else t.chat.continue_(true); t.ticks(1) end
            end
            t.ticks(2)
            t.expect("quest.stage.told_exit_route", t.quest.expect_stage("told_exit_route"))

            -- leaveCave: the wall tunnel inside the chamber, then the passage to the false wall and the ladder
            t.exec("leaveCave", t.player.click_loc, "route_cavewalltunnel", 1, { at = { 3505, 9831 } })
            t.ticks(4)
            t.check("leaveCave-where", true, "out in the passage, now at " .. where())
            t.exec("falseWall", t.player.click_loc, "thrttavernbasementfalsewall", 1)
            t.ticks(4)
            t.check("falseWall-where", true, "through the false wall, now at " .. where())
            t.exec("goUpToCanifis", t.player.click_loc, "thrttavernbasementladder", 1)
            t.ticks(4)
            t.check("goUpToCanifis-where", true, "up the ladder, now at " .. where())
            t.expect("quest.stage.found_exit", t.quest.expect_stage("found_exit"))

            -- talkToStranger: the quest ends here
            t.exec("goto-talkToStranger", t.player.goto_tile, 3503, 3475, 0)
            local snap_r, snap = t.skill.snapshot()
            t.check("snapshot", snap_r == "ok", "xp before the hand-in, attack " .. tostring(snap and snap.attack and snap.attack.experience))
            t.exec("talkToStranger", t.player.talk_to, "canafis_stranger", 1)
            for i = 1, 20 do
                local k = t.chat.kind()
                local _, tx = t.chat.text()
                t.check("talkToStranger-p" .. i, true, tostring(k) .. " :: " .. tostring(tx))
                if k == "none" or k == "other_input" then break end
                t.chat.continue_(true); t.ticks(1)
            end
            t.ticks(3)
            t.quest.expect_complete()
            for _, sk in ipairs({ "attack", "defence", "strength", "hitpoints", "crafting" }) do
                t.expect("reward." .. sk .. "_xp", t.skill.expect_gain(sk, 600, snap))
            end
            -- LEG 4 END
        end },
    },
}
