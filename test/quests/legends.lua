-- Legends' Quest (quest_legends), a 9-leg relay (docs/quest_authoring/relay.md); ladder:
-- python3 tools/quest_gate/ladder.py legends --leg K. Each leg is self-contained: it shares no local
-- with another leg (a resumed leg never ran the others).
--
-- Quest Helper legendsquest requirements: the guard (legends_guard.rs2 legends_guard_eligible) reads
-- the Family Crest / Shilo Village / Underground Pass varps and qp; quest_cheat.rs2 has no ::complete
-- arm for those three, so they are staged with ::setvar on the varps the guard reads (prerequisite
-- quests only, never legendsquest).

return {
    id = "legends",
    fixture = "fresh_lumbridge.ini",
    max_frames = 240000, -- the relay runs ~8000 server ticks; the default budget is ~2000 (run.unfinished names an exhausted one)
    setup = {
        "::clearinv",
        -- prerequisite quests (Quest Helper: Family Crest, Heroes' Quest, Shilo Village, Underground
        -- Pass, Waterfall Quest) and 107 quest points
        "::complete quest_heroes", "::complete quest_waterfall",
        -- Druidic Ritual unlocks Herblore, which the bravery potion (leg 6) needs; it pays 4 quest
        -- points, so it runs before the qp is set and before the quest binds its qp baseline
        "::complete quest_druidicritual",
        "::setvar varp148_crestquest ^crest_complete", "::setvar varp116_zombiequeen ^zombiequeen_complete",
        "::setvar varp161_upass ^upass_complete",
        "::setvar varp101_qp 107",
        -- Quest Helper skill requirements: Crafting 50, Herblore 45, Magic 56, Mining 52, Prayer 42,
        -- Smithing 50, Strength 50, Thieving 50, Woodcutting 50, Agility 50
        "::setlevel crafting 50", "::setlevel woodcutting 50", "::setlevel agility 50",
        "::setlevel herblore 45", "::setlevel magic 56", "::setlevel mining 55", "::setlevel prayer 60",
        "::setlevel smithing 50", "::setlevel thieving 50", "::setlevel strength 80",
        -- The Kharazi jungle animals attack a character with a fresh 10 hitpoints while the
        -- bullroarer is swung (spinBull died at tick 247); Quest Helper lists combat gear for the
        -- fights of later legs (Nezikchened, the heart-crystal trio), so the character is armed once
        "::setlevel hitpoints 99", "::setlevel attack 80", "::setlevel defence 80",
        "::give rune_full_helm 1", "::wield rune_full_helm", "::give rune_chainbody 1", "::wield rune_chainbody",
        "::give rune_platelegs 1", "::wield rune_platelegs", "::give rune_kiteshield 1", "::wield rune_kiteshield",
        "::give rune_scimitar 1", "::wield rune_scimitar",
        -- Quest Helper item requirements for leg 1: any axe, papyrus, charcoal (the machete is
        -- Radimus's cupboard, the map and the Radimus notes come from the quest)
        "::give rune_axe 1", "::give papyrus 10", "::give charcoal 5",
        -- Quest Helper item requirements for leg 2 (enterMossyRockAgain): lockpicks (multiple in
        -- case they break), any pickaxe, Soul/Mind/Earth/Law runes (Law twice: the wall wants
        -- S-M-E-L-L). The seven gems the guide also lists do not fit the 28-slot backpack beside
        -- leg 1's papyrus and charcoal: leg 2 gives them after dropping those (leg.2.pack).
        "::give lockpick 3", "::give rune_pickaxe 1",
        "::give soulrune 1", "::give mindrune 1", "::give earthrune 1", "::give lawrune 2",
    },
    bind = {
        varp = "varp139_legendsquest",
        constants = {
            not_started = 0, started = 1, mapped_jungle = 2, got_bullroarer = 3,
            swung_bullroarer = 4, accepted_rescue_ungadulu = 5,
            complete = 75,
        },
        row = "quest_legends",
        display = "Legends' Quest",
        points = 4,
    },
    legs = {
        { name = "start_and_bullroarer", run = function(t)
            -- LEG 1 BEGIN: talkToGuard
            -- Pages a script opens one after another (p_delay between boxes) are answered by
            -- draining to each options page, choosing, and draining to the end.
            local function converse(name, choices)
                t.await({ level = function() return t.chat.kind() ~= "none" end, note = "page" }, 25)
                for i, c in ipairs(choices or {}) do
                    local _, kind = t.chat.drain({ stop_at = "options", max_pages = 25 })
                    if kind ~= "options" then
                        t.step(name .. "-opt" .. i, "FAIL", "wanted options for '" .. c .. "', got " .. tostring(kind))
                        return false
                    end
                    local _, rows = t.chat.options()
                    local texts = {}
                    for _, row in ipairs(rows or {}) do
                        texts[#texts + 1] = type(row) == "table" and tostring(row.text) or tostring(row)
                    end
                    t.note("options: " .. table.concat(texts, " / "))
                    t.exec(name .. "-choose" .. i, t.chat.choose, c)
                    t.ticks(1)
                end
                local result, kind = t.chat.drain({ max_pages = 25 })
                if kind == "options" then
                    t.step(name .. "-end", "FAIL", "unanswered options page")
                    return false
                end
                t.check(name .. "-end", result == "ok", "drained to " .. tostring(kind))
                return true
            end

            -- kharazi_dense_jungle (legends_zones.dbrow): chop the jungle loc straight ahead
            -- (jungle_tree.rs2 moves the player two tiles through it); a free tile ahead is walked.
            local function cross_jungle(name, want_south, limit)
                local symbols = { "kharazi_jungle_plant1", "kharazi_jungle_plant2", "kharazi_jungle_tree1",
                                  "kharazi_jungle_tree2", "kharazi_jungle_tree_logs" }
                local dz = want_south and -1 or 1
                local stuck = 0
                for i = 1, limit do
                    local _, tile = t.world.tile()
                    if want_south and tile.z < 2934 then return true end
                    if (not want_south) and tile.z > 2942 then return true end
                    local pressed = false
                    for _, symbol in ipairs(symbols) do
                        if t.world.loc_near(symbol, 1) == "ok" then
                            local result, detail = t.player.click_loc(symbol, 1, { at = { tile.x, tile.z + dz } })
                            if result == "ok" then
                                t.step(name .. "-chop-" .. i, result == "ok" and "PASS" or "FAIL", symbol .. " at " .. tile.x .. "," .. (tile.z + dz) .. ": " .. tostring(detail))
                                pressed = true
                                break
                            end
                        end
                    end
                    if not pressed then
                        local walked = t.player.walk_to(tile.x, tile.z + dz, 10)
                        t.step(name .. "-walk-" .. i, walked == "ok" and "PASS" or "FAIL",
                            "walk to " .. tile.x .. "," .. (tile.z + dz) .. ": " .. tostring(walked))
                    end
                    local moved = t.await({ level = function()
                        local _, now = t.world.tile()
                        return now.x ~= tile.x or now.z ~= tile.z
                    end, note = "moved" }, 40)
                    if moved ~= "ok" then
                        -- jungle_tree.rs2: "This way is blocked off" when the tile two ahead is
                        -- blocked; side-step one column and cut there instead
                        stuck = stuck + 1
                        local side = (stuck % 2 == 1) and 1 or -1
                        local sidestep = t.player.walk_to(tile.x + side, tile.z, 10)
                        t.step(name .. "-sidestep-" .. i, sidestep == "ok" and "PASS" or "FAIL",
                            "blocked ahead of " .. tile.x .. "," .. tile.z .. ", side-step to "
                            .. (tile.x + side) .. "," .. tile.z .. ": " .. tostring(sidestep)
                            .. "; last messages: " .. tostring(select(2, t.msg.last(2))))
                    end
                end
                return false
            end

            ---------------------------------------------------------------- 0: the guild guard
            t.exec("goto-talkToGuard", t.player.goto_tile, 2728, 3345, 0)
            t.exec("talkToGuard", t.player.talk_to, "legends_guild_guard1")
            t.exec("talkToGuard-dialog", t.chat.play, {
                "npc:how can I help you",
                "choose:Can I speak to someone in charge?",
                "player:Can I speak to someone in charge",
                "npc:Radimus Erkle is the Grand Vizier",
                "choose:Can I go on the quest?",
                "player:Can I go on the quest",
                "mesbox:scroll of paper",
                "npc:eligible for the quest",
                "choose:Yes, I'd like to talk to Grand Vizier Erkle.",
                "player:I'd like to talk",
                "npc:building on the left",
                "mesbox:unlocks the gate",
                "npc:Good Luck",
            })
            t.ticks(4)
            local _, inside = t.world.tile()
            t.check("talkToGuard-gate", inside.z >= 3350,
                "the guard's gate carried the player inside the guild: tile " .. inside.x .. "," .. inside.z)

            ---------------------------------------------------------------- 0: Radimus Erkle
            local walked = t.player.walk_to(2727, 3368, 30)
            local _, hut = t.world.tile()
            t.check("goto-talkToRadimus", walked == "ok", "walked to the hut door: now at " .. hut.x .. "," .. hut.z)
            t.exec("talkToRadimus-door", t.player.click_loc, "poshdoor", 1, { at = { 2726, 3368 } })
            t.ticks(2)
            t.exec("talkToRadimus", t.player.talk_to, "radimus_erkle_hut")
            converse("talkToRadimus-dialog", { "Yes actually, what's involved?", "Yes, it sounds great!" })
            t.ticks(2)
            t.expect("quest.stage.started", t.quest.expect_stage("started"))
            t.exec("talkToRadimus-cupboard", t.player.click_loc, "legends_cupboard", 1)
            t.ticks(2)
            t.exec("talkToRadimus-search", t.player.click_loc, "legends_cupboardopen", 2)
            t.exec("talkToRadimus-machete", t.inv.await, "machette", 1, 10)
            t.chat.drain({ max_pages = 3 })

            ---------------------------------------------------------------- 1: the jungle, the sketch
            t.exec("goto-enterJungle", t.player.goto_tile, 2795, 2943, 0)
            local crossed = cross_jungle("enterJungle", true, 12)
            local _, jungle = t.world.tile()
            t.check("enterJungle", crossed, "cut through the dense band with the axe and machete: now at "
                .. jungle.x .. "," .. jungle.z)
            for _, sketch in ipairs({ { "moveToWest", 2791, 2917 }, { "moveToMiddle", 2852, 2915 }, { "moveToEast", 2910, 2916 } }) do
                local step, x, z = sketch[1], sketch[2], sketch[3]
                t.exec("goto-" .. step, t.player.goto_tile, x, z, 0)
                local _, bits_before = t.var.server("varp6202_legends_bits")
                local drawn = false
                for try = 1, 5 do
                    t.exec(step .. "-map-" .. try, t.player.inv_op, "thkaramjamap", 2)
                    t.chat.drain({ max_pages = 4 })
                    t.ticks(3)
                    local _, bits_after = t.var.server("varp6202_legends_bits")
                    local _, complete = t.inv.count("thkaramjamapcomp")
                    if bits_after ~= bits_before or complete > 0 then
                        t.ticks(3)
                        t.chat.drain({ max_pages = 6 })
                        t.ticks(2)
                        t.chat.drain({ max_pages = 6 })
                        t.check(step, true, "sketch drawn at " .. x .. "," .. z .. " on try " .. try
                            .. ": legends_bits " .. tostring(bits_before) .. " -> " .. tostring(bits_after)
                            .. ", completed maps in the backpack " .. tostring(complete))
                        drawn = true
                        break
                    end
                end
                if not drawn then
                    t.check(step, false, "no sketch progress at " .. x .. "," .. z .. " after 5 tries")
                end
            end
            t.ticks(2)
            t.exec("moveToEast-complete", t.inv.await, "thkaramjamapcomp", 1, 10)
            t.expect("quest.stage.mapped_jungle", t.quest.expect_stage("mapped_jungle"))

            ---------------------------------------------------------------- 2: the forester copies the notes
            t.exec("goto-useNotes", t.player.goto_tile, 2795, 2930, 0)
            local left = cross_jungle("leaveJungle", false, 12)
            local _, north = t.world.tile()
            t.check("useNotes-leave", left, "north of the dense band again: " .. north.x .. "," .. north.z)
            local forester = t.player.by_symbol("npc", "jungleforester_m")
            t.exec("useNotes-approach", t.player.walk_near, forester, 30)
            t.exec("useNotes", t.player.use_on, "thkaramjamapcomp", forester)
            converse("useNotes-dialog", { "Yes, go ahead make a copy!" })
            t.exec("useNotes-bullroarer", t.inv.await, "bullroarer", 1, 10)
            t.expect("quest.stage.got_bullroarer", t.quest.expect_stage("got_bullroarer"))

            ---------------------------------------------------------------- 3: the bullroarer
            t.exec("goto-enterJungleWithRoarer", t.player.goto_tile, 2795, 2943, 0)
            -- ::goto can land one column west (2794), where the tile two ahead is blocked; the cut
            -- that worked runs down x=2795
            t.player.walk_to(2795, 2943, 10)
            local in_again = cross_jungle("enterJungleWithRoarer", true, 12)
            local _, jungle_again = t.world.tile()
            t.check("enterJungleWithRoarer", in_again, "south of the dense band again with the bullroarer: "
                .. jungle_again.x .. "," .. jungle_again.z)
            t.exec("goto-spinBull", t.player.goto_tile, 2791, 2917, 0)
            -- jungle animals nearby cancel Gujuo (1/6 each): swing up to sixteen times, 14 ticks apart
            local met = false
            local swings = 0
            for i = 1, 16 do
                t.exec("spinBull-" .. i, t.player.inv_op, "bullroarer", 1)
                swings = i
                t.ticks(14)
                if t.npc.nearest("gujuo", 14) == "ok" then met = true break end
            end
            t.check("spinBull", met, "Gujuo appeared after " .. swings .. " swing(s) of the bullroarer")
            t.exec("talkToGujuo", t.player.talk_to, "gujuo")
            converse("talkToGujuo-dialog", {
                "I was hoping to attract the attention of a native.",
                "I want to develop friendly relations with your people.",
                "Can you get your people together?",
                "What can we do instead then?",
                "How do we make the totem pole?",
                "I will release Ungadulu...",
            })
            t.ticks(2)
            t.expect("quest.stage.accepted_rescue_ungadulu", t.quest.expect_stage("accepted_rescue_ungadulu"))
            -- the jungle cuts left logs in the backpack (jungle_tree.rs2 hands out a log per roll);
            -- later legs need the slots, so drop them at the boundary
            local _, logs_before = t.inv.count("logs")
            local dropped = 0
            for _ = 1, 30 do
                local _, remaining = t.inv.count("logs")
                if remaining == 0 then break end
                t.player.drop("logs")
                dropped = dropped + 1
                t.ticks(1)
            end
            local _, logs_after = t.inv.count("logs")
            t.check("leg.1.logs", logs_after == 0, "dropped " .. dropped .. " logs from the jungle cuts: "
                .. tostring(logs_before) .. " -> " .. tostring(logs_after))
            local _, stage = t.var.server("varp139_legendsquest")
            local _, at = t.world.tile()
            local _, roarer = t.inv.count("bullroarer")
            t.check("leg.1.end", true, "tile " .. at.x .. "," .. at.z .. " level " .. (at.level or 0)
                .. ", legendsquest=" .. tostring(stage) .. " read from the server, bullroarer x" .. tostring(roarer)
                .. ", rune axe, machete, papyrus, charcoal, completed sketch map carried on")
            -- LEG 1 END
        end },
        { name = "mossy_rock_to_marked_wall", run = function(t)
            -- LEG 2 BEGIN: enterMossyRock
            -- Pages a script opens one after another (p_delay between boxes) are answered by
            -- draining to each options page, choosing, and draining to the end.
            local function converse(name, choices)
                t.await({ level = function() return t.chat.kind() ~= "none" end, note = "page" }, 25)
                for i, c in ipairs(choices or {}) do
                    local _, kind = t.chat.drain({ stop_at = "options", max_pages = 40 })
                    if kind ~= "options" then
                        t.step(name .. "-opt" .. i, "FAIL", "wanted options for '" .. c .. "', got " .. tostring(kind))
                        return false
                    end
                    t.exec(name .. "-choose" .. i, t.chat.choose, c)
                    t.ticks(1)
                end
                local result, kind = t.chat.drain({ max_pages = 40 })
                if kind == "options" then
                    t.step(name .. "-end", "FAIL", "unanswered options page")
                    return false
                end
                t.check(name .. "-end", result == "ok", "drained to " .. tostring(kind))
                return true
            end

            local function underground()
                local _, at = t.world.tile()
                return at.z > 9000
            end
            local function await_underground(note)
                return t.await({ level = underground, note = note }, 16)
            end

            -- legends_search_rocks: an agility roll (quest_legends.rs2:335); a failed squeeze costs
            -- 5 hitpoints and leaves the player on the surface, so the press is repeated
            local function crawl_in(name)
                for i = 1, 12 do
                    t.exec(name .. "-press-" .. i, t.player.click_loc, "lgshamancaverock1", 1)
                    if not converse(name .. "-dialog-" .. i, { "/crawl through/" }) then return false end
                    if await_underground(name .. " underground") == "ok" then return true end
                end
                return false
            end

            ---------------------------------------------------------------- the pack for the rest of the quest
            -- Quest Helper lists one of each gem for enterMossyRockAgain (leg 3 places them). The map
            -- is drawn, so leg 1's leftover papyrus and charcoal are dropped to make room.
            for _, junk in ipairs({ "papyrus", "charcoal" }) do
                for _ = 1, 12 do
                    local _, remaining = t.inv.count(junk)
                    if remaining == 0 then break end
                    t.player.drop(junk)
                    t.ticks(1)
                end
            end
            for _, gem in ipairs({ "opal", "jade", "red_topaz", "sapphire", "emerald", "ruby", "diamond" }) do
                t.cheat("::give " .. gem .. " 1")
                t.ticks(1)
            end
            local _, junk_papyrus = t.inv.count("papyrus")
            local _, junk_charcoal = t.inv.count("charcoal")
            local _, diamonds = t.inv.count("diamond")
            local _, emeralds = t.inv.count("emerald")
            t.check("leg.2.pack", junk_papyrus == 0 and junk_charcoal == 0 and diamonds == 1 and emeralds == 1,
                "dropped the spent papyrus/charcoal (" .. junk_papyrus .. "/" .. junk_charcoal .. " left) and "
                .. "gave the seven gems the guide lists (diamond x" .. diamonds .. ", emerald x" .. emeralds .. ")")

            ---------------------------------------------------------------- enterMossyRock
            -- plain travel to the rock (2782,2937), north-west of the Kharazi jungle band
            t.exec("goto-enterMossyRock", t.player.goto_tile, 2782, 2935, 0)
            local inside = crawl_in("enterMossyRock")
            local _, cave_at = t.world.tile()
            t.check("enterMossyRock", inside, "crawled through the Mossy Rocks to " .. cave_at.x .. "," .. cave_at.z
                .. " (underground z>9000), legendsquest read next")
            t.ticks(2)
            t.expect("quest.stage.found_entrance", t.quest.expect_stage(6))

            ---------------------------------------------------------------- investigateFireWall
            -- the wall's north face (2790,9333), outside the octagram; Search (op2) opens
            -- ungadulu_no_closer (ungadulu.rs2:67)
            t.exec("investigateFireWall-press", t.player.click_loc, "lqfirewall_straight", 2, { at = { 2790, 9333 } })
            converse("investigateFireWall-dialog", {
                "How can I extinguish the flames?",
                "Where do I get pure water from?",
            })
            t.ticks(2)
            t.expect("quest.stage.spoke_ungadulu", t.quest.expect_stage(7))

            ---------------------------------------------------------------- leaveCave
            t.exec("leaveCave", t.player.click_loc, "shaman_entrance_caver", 1)
            local out = t.await({ level = function() return not underground() end, note = "surface" }, 20)
            local _, surface_at = t.world.tile()
            t.check("leaveCave-surface", out == "ok", "back on the surface at " .. surface_at.x .. "," .. surface_at.z)

            ---------------------------------------------------------------- spinBullAgain
            local met = false
            local swings = 0
            for i = 1, 16 do
                t.exec("spinBullAgain-" .. i, t.player.inv_op, "bullroarer", 1)
                swings = i
                t.ticks(14)
                if t.npc.nearest("gujuo", 14) == "ok" then met = true break end
            end
            t.check("spinBullAgain", met, "Gujuo appeared after " .. swings .. " swing(s) of the bullroarer")

            ---------------------------------------------------------------- talkToGujuoAgain
            -- gujuo.rs2 gujuo_how_goes_ungadulu -> gujuo_pure_water (Ungadulu's 'who' page was not
            -- asked, so the pool branch gujuo_pool_where sets asked_gujuo_holy_water)
            t.exec("talkToGujuoAgain", t.player.talk_to, "gujuo")
            converse("talkToGujuoAgain-dialog", {
                "/pure water/",
                "Where is the pool of sacred water?",
                "Ok, thanks... Goodbye.",
            })
            t.ticks(2)
            t.expect("quest.stage.asked_gujuo_holy_water", t.quest.expect_stage(8))

            ---------------------------------------------------------------- enterMossyRockAgain
            local inside_again = crawl_in("enterMossyRockAgain")
            local _, cave_again = t.world.tile()
            t.check("enterMossyRockAgain", inside_again, "underground again at " .. cave_again.x .. "," .. cave_again.z)

            ---------------------------------------------------------------- enterBookcase
            -- quest_legends.rs2:91: mesbox, options, an agility roll (stat_random(agility,60,254));
            -- a failure slides the player back out to 2794,9339
            local past_bookcase = false
            for i = 1, 12 do
                t.exec("enterBookcase-press-" .. i, t.player.click_loc, "shaman_bookcase", 1)
                if not converse("enterBookcase-dialog-" .. i, { "Yes please!" }) then break end
                local squeezed = t.await({ level = function()
                    local _, at = t.world.tile()
                    return at.x >= 2798
                end, note = "squeezed past the bookcase" }, 14)
                if squeezed == "ok" then past_bookcase = true break end
            end
            local _, book_at = t.world.tile()
            t.check("enterBookcase", past_bookcase, "in the tunnel behind the bookcase at " .. book_at.x .. "," .. book_at.z)

            ---------------------------------------------------------------- enterGate1
            -- lockpick gate (quest_legends.rs2:400 op2 search): a thieving roll, repeated on failure
            local through_gate1 = false
            for i = 1, 12 do
                t.exec("enterGate1-press-" .. i, t.player.click_loc, "lglockpickgatebottoml", 2)
                -- the first press only walks to the gate; the lock script opens its boxes on arrival
                -- the lock script closes its box (if_close + p_delay) between the third and fourth
                -- page, so drain, wait for the next box and drain again until none comes
                local wait = 25
                for _ = 1, 8 do
                    local came = t.await({ level = function() return t.chat.kind() ~= "none" end, note = "lock page" }, wait)
                    if came ~= "ok" then break end
                    t.chat.drain({ max_pages = 40 })
                    wait = 6
                end
                t.ticks(3)
                local _, at = t.world.tile()
                if at.z < 9332 then through_gate1 = true break end
            end
            local _, gate1_at = t.world.tile()
            t.check("enterGate1", through_gate1, "picked the lock and stands south of the gate at " .. gate1_at.x .. "," .. gate1_at.z)

            ---------------------------------------------------------------- enterGate2 (boulders, then the doors)
            local boulder_z = { mine_test_boulder1 = 9327, mine_test_boulder2 = 9323, mine_test_boulder3 = 9319 }
            for _, symbol in ipairs({ "mine_test_boulder1", "mine_test_boulder2", "mine_test_boulder3" }) do
                local passed = false
                for i = 1, 14 do
                    t.exec("enterGate2-" .. symbol .. "-press-" .. i, t.player.click_loc, symbol, 1)
                    t.ticks(6)
                    local _, at = t.world.tile()
                    if at.z < boulder_z[symbol] then passed = true break end
                end
                local _, at = t.world.tile()
                t.check("enterGate2-" .. symbol, passed, "smashed the rock and stands past it at " .. at.x .. "," .. at.z)
            end

            local through_gate2 = false
            for i = 1, 12 do
                t.exec("enterGate2Door-press-" .. i, t.player.click_loc, "lgstrengthtrialgatel", 1)
                converse("enterGate2Door-dialog-" .. i, { "/very strong/" })
                t.ticks(3)
                local _, at = t.world.tile()
                if at.z < 9314 then through_gate2 = true break end
            end
            local _, gate2_at = t.world.tile()
            t.check("enterGate2Door", through_gate2, "forced the strength doors and stands south of them at " .. gate2_at.x .. "," .. gate2_at.z)

            ---------------------------------------------------------------- searchMarkedWall
            -- "Follow the cave around": the passage south of the strength doors winds down to the
            -- crumbled wall (2789,9295), which is jumped over (quest_legends.rs2:615, Agility 50)
            t.player.walk_to(2790, 9294, 160)
            do
                local _, south = t.world.tile()
                t.check("searchMarkedWall-follow-cave", south.z < 9295, "followed the cave to the crumbled wall's south side at "
                    .. south.x .. "," .. south.z)
            end
            local jumped = false
            for i = 1, 8 do
                t.exec("searchMarkedWall-jump-" .. i, t.player.click_loc, "crumbled_wall", 1)
                local over = t.await({ level = function()
                    local _, at = t.world.tile()
                    return at.z >= 9296
                end, note = "over the crumbled wall" }, 14)
                if over == "ok" then jumped = true break end
            end
            do
                local _, north = t.world.tile()
                t.check("searchMarkedWall-jump", jumped, "jumped the crumbled wall, now at " .. north.x .. "," .. north.z)
            end
            t.player.walk_to(2780, 9306, 60)
            do
                local _, near = t.world.tile()
                t.check("searchMarkedWall-walk", near.z >= 9304 and near.x <= 2785, "walked on to the marked wall, now at " .. near.x .. "," .. near.z)
            end
            t.exec("searchMarkedWall-press", t.player.click_loc, "lgancientwalldoor", 2, { at = { 2779, 9305 } })
            converse("searchMarkedWall-dialog", { "Yes, I'll read it." })

            ---------------------------------------------------------------- useSoul
            local wall = t.player.by_symbol("loc", "lgancientwalldoor")
            t.exec("useSoul", t.player.use_on, "soulrune", wall, { at = { 2779, 9305 } })
            t.chat.drain({ max_pages = 10 })
            t.ticks(2)
            t.inv.expect_absent("soulrune")

            local _, stage = t.var.server("varp139_legendsquest")
            local _, at = t.world.tile()
            t.check("leg.2.end", true, "tile " .. at.x .. "," .. at.z .. " level " .. (at.level or 0)
                .. ", legendsquest=" .. tostring(stage) .. " read from the server, soul rune set into the marked wall; "
                .. "carrying mind, earth, 2 law runes, 7 gems, lockpicks, pickaxe, bullroarer, rune axe, machete")
            -- LEG 2 END
        end },
        { name = "runes_and_gems", run = function(t)
            -- LEG 3 BEGIN: useMind
            local function converse(name, choices)
                t.await({ level = function() return t.chat.kind() ~= "none" end, note = "page" }, 25)
                for i, c in ipairs(choices or {}) do
                    local _, kind = t.chat.drain({ stop_at = "options", max_pages = 25 })
                    if kind ~= "options" then
                        t.step(name .. "-opt" .. i, "FAIL", "wanted options for '" .. c .. "', got " .. tostring(kind))
                        return false
                    end
                    t.exec(name .. "-choose" .. i, t.chat.choose, c)
                    t.ticks(1)
                end
                local result, kind = t.chat.drain({ max_pages = 25 })
                if kind == "options" then
                    t.step(name .. "-end", "FAIL", "unanswered options page")
                    return false
                end
                t.check(name .. "-end", result == "ok", "drained to " .. tostring(kind))
                return true
            end

            ---------------------------------------------------------------- the runes S-M-E-L-L
            -- quest_legends.rs2:647-745: each rune is slid into the next depression of the marked
            -- wall (soul was set in leg 2); the second law rune opens the door (a choice)
            local wall_at = { 2779, 9305 }
            local function slide(step, rune, expect_left)
                local wall = t.player.by_symbol("loc", "lgancientwalldoor")
                t.exec(step, t.player.use_on, rune, wall, { at = wall_at })
                t.chat.drain({ max_pages = 10 })
                t.ticks(2)
                local _, left = t.inv.count(rune)
                t.check(step .. "-merged", left == expect_left, rune .. " left in the backpack: " .. tostring(left)
                    .. " (wanted " .. expect_left .. ")")
            end
            slide("useMind", "mindrune", 0)
            slide("useEarth", "earthrune", 0)
            slide("useLaw", "lawrune", 1)

            local wall = t.player.by_symbol("loc", "lgancientwalldoor")
            t.exec("useLaw2", t.player.use_on, "lawrune", wall, { at = wall_at })
            converse("useLaw2-door", { "Yes, I'll go through!" })
            t.ticks(3)
            do
                local _, laws = t.inv.count("lawrune")
                local _, at = t.world.tile()
                t.check("useLaw2-through", laws == 0 and at.x < 2779 and at.z < 9305,
                    "no law rune left (" .. tostring(laws) .. "); walked through the wall into the gem cavern at "
                    .. at.x .. "," .. at.z)
            end

            ---------------------------------------------------------------- the gems (rs2:780)
            -- each gem goes on its own carved rock (the rock's coordinate is checked by the script)
            local function place(step, gem, x, z)
                local rock = t.player.by_symbol("loc", "lg_gemplacerock")
                t.exec(step, t.player.use_on, gem, rock, { at = { x, z } })
                t.chat.drain({ max_pages = 6 })
                t.ticks(2)
                local _, left = t.inv.count(gem)
                local spinning = t.msg.expect("starts spinning")
                t.check(step .. "-placed", left == 0 and spinning == "ok",
                    gem .. " left in the backpack: " .. tostring(left) .. "; server line 'starts spinning' -> "
                    .. tostring(spinning))
            end
            place("useSapphire", "sapphire", 2781, 9291)
            place("useDiamond", "diamond", 2774, 9287)
            place("useRuby", "ruby", 2767, 9289)
            place("useTopaz", "red_topaz", 2772, 9295)
            place("useJade", "jade", 2771, 9303)
            -- the far west rocks stand beside a carved-rock copy that steals the press from a distance:
            -- stand beside the emerald's rock first
            do
                local walked = t.player.walk_to(2758, 9298, 20)
                local _, at = t.world.tile()
                t.check("goto-useEmerald", math.abs(at.x - 2758) <= 3 and math.abs(at.z - 9298) <= 3, "beside the emerald's rock at " .. at.x .. "," .. at.z .. ", walk " .. tostring(walked))
            end
            place("useEmerald", "emerald", 2757, 9297)

            local _, opal = t.inv.count("opal")
            local _, stage = t.var.server("varp139_legendsquest")
            local _, at = t.world.tile()
            t.check("leg.3.end", opal == 1 and t.chat.kind() == "none", "tile " .. at.x .. "," .. at.z .. " level "
                .. (at.level or 0) .. ", legendsquest=" .. tostring(stage) .. " read from the server; six gems set "
                .. "above their rocks, the opal (x" .. tostring(opal) .. ") is left for leg 4's far west rock; "
                .. "carrying bullroarer, rune axe, machete, map, pickaxe, lockpicks")
            -- LEG 3 END
        end },
        { name = "book_bowl_and_fire_demon", run = function(t)
            -- LEG 4 BEGIN: useOpal
            local function converse(name, choices)
                t.await({ level = function() return t.chat.kind() ~= "none" end, note = "page" }, 25)
                for i, c in ipairs(choices or {}) do
                    local _, kind = t.chat.drain({ stop_at = "options", max_pages = 40 })
                    if kind ~= "options" then
                        t.step(name .. "-opt" .. i, "FAIL", "wanted options for '" .. c .. "', got " .. tostring(kind))
                        return false
                    end
                    t.exec(name .. "-choose" .. i, t.chat.choose, c)
                    t.ticks(1)
                end
                local result, kind = t.chat.drain({ max_pages = 40 })
                if kind == "options" then
                    t.step(name .. "-end", "FAIL", "unanswered options page")
                    return false
                end
                t.check(name .. "-end", result == "ok", "drained to " .. tostring(kind))
                return true
            end
            local function underground()
                local _, at = t.world.tile()
                return at.z > 9000
            end

            ---------------------------------------------------------------- useOpal (rs2:808)
            -- the seventh gem: placing it starts the puzzle-complete cutscene (rs2:932), which
            -- blows the player to the middle of the room and, ~13 ticks later, drops the book
            do
                -- leg 3's emerald press can miss its rock (a carved-rock copy in front of it); the
                -- puzzle needs all seven gems, so finish it here before the opal
                local _, emeralds = t.inv.count("emerald")
                if emeralds > 0 then
                    local erock = t.player.by_symbol("loc", "lg_gemplacerock")
                    t.player.walk_to(2758, 9298, 8)
                    t.ticks(2)
                    t.exec("useEmerald-retry", t.player.use_on, "emerald", erock, { at = { 2757, 9297 } })
                    t.chat.drain({ max_pages = 6 })
                    t.ticks(2)
                    local _, left = t.inv.count("emerald")
                    t.check("useEmerald-retry-placed", left == 0, "emerald left in the backpack: " .. tostring(left))
                end
            end
            local rock = t.player.by_symbol("loc", "lg_gemplacerock")
            t.exec("useOpal", t.player.use_on, "opal", rock, { at = { 2764, 9309 } })
            t.chat.drain({ max_pages = 6 })
            t.ticks(2)
            do
                local _, left = t.inv.count("opal")
                local spinning = t.msg.expect("starts spinning")
                t.check("useOpal-placed", left == 0 and spinning == "ok",
                    "opal left in the backpack: " .. tostring(left) .. "; server line 'starts spinning' -> " .. tostring(spinning))
            end

            ---------------------------------------------------------------- waitForBook / pickUpBook
            local appeared = t.await({ level = function() return t.world.obj_near("book_of_binding", 20) == "ok" end,
                note = "the book of binding appears" }, 80)
            local _, book_at = t.world.obj_near("book_of_binding", 20)
            t.check("waitForBook", appeared == "ok" and type(book_at) == "table",
                "the book of binding lies at " .. (type(book_at) == "table" and (book_at.tile_x .. "," .. book_at.tile_z) or "?")
                .. " (search result " .. tostring(appeared) .. ")")
            t.exec("pickUpBook", t.player.click_obj, "book_of_binding", 3)
            t.ticks(1)
            do
                local _, books = t.inv.count("book_of_binding")
                t.check("pickUpBook-count", books == 1, "book_of_binding in the backpack: " .. tostring(books))
            end

            ---------------------------------------------------------------- the pack for the rest of the leg
            -- the lockpicks and swamp rocks are spent (the gates are behind us): dropped to make room
            -- for what Quest Helper lists for makeBowl (gold bars, hammer), the blessing (prayer
            -- potions) and enterMossyRockWithBowl / the fight (combat gear, food)
            for _, junk in ipairs({ "lockpick", "swamprocks1", "swamprocks2", "swamprocks3" }) do
                for _ = 1, 6 do
                    local got, remaining = t.inv.count(junk)
                    if got ~= "ok" or remaining == 0 then break end
                    t.player.drop(junk)
                    t.ticks(1)
                end
            end
            -- gold bar x2 makes the bowl (quest_legends.rs2:1284); a failed forge burns one or two,
            -- so six; hammer: the anvil wants one; 4-dose prayer restores: the blessing's failure
            -- costs 5 Prayer (gujuo.rs2:519) and Gujuo refuses below 42; sharks for the fight
            t.cheat("::give hammer 1")
            for _ = 1, 6 do t.cheat("::give gold_bar 1") t.ticks(1) end
            for _ = 1, 2 do t.cheat("::give 4doseprayerrestore 1") t.ticks(1) end
            for _ = 1, 6 do t.cheat("::give shark 1") t.ticks(1) end
            do
                local _, bars = t.inv.count("gold_bar")
                local _, hammers = t.inv.count("hammer")
                local _, pots = t.inv.count("4doseprayerrestore")
                local _, sharks = t.inv.count("shark")
                t.check("leg.4.pack", bars == 6 and hammers == 1 and pots == 2 and sharks == 6,
                    "gold bars " .. bars .. ", hammer " .. hammers .. ", prayer restores " .. pots .. ", sharks " .. sharks)
            end

            ---------------------------------------------------------------- makeBowl
            -- Quest Helper: any anvil (the Varrock west smithy, three copies of `anvil`). The gold bar
            -- on the anvil with legendsquest >= asked_gujuo_holy_water forges the bowl
            -- (smithing.rs2:274 -> quest_legends.rs2:1284): a "Yes" choice, 4 ticks, stat_random
            t.exec("goto-makeBowl", t.player.goto_tile, 3187, 3424, 0)
            local forged = false
            for i = 1, 6 do
                local anvil = t.player.by_symbol("loc", "anvil")
                t.exec("makeBowl-press-" .. i, t.player.use_on, "gold_bar", anvil, { at = { 3188, 3424 } })
                converse("makeBowl-dialog-" .. i, { "Yes" })
                t.ticks(6)
                local _, bowls = t.inv.count("goldbowl_empty")
                local _, bars = t.inv.count("gold_bar")
                if bowls == 1 then
                    forged = true
                    t.check("makeBowl", true, "golden bowl forged on try " .. i .. "; gold bars left " .. bars)
                    break
                end
                if bars < 2 then break end
            end
            if not forged then
                t.check("makeBowl", false, "no golden bowl after the gold bars ran out")
                t.blocked("makeBowl: six gold bars did not forge a bowl (stat_random(smithing,31,256) misses); rerun")
                return
            end
            -- The forging roll (quest_legends.rs2:1286-1300: stat_random, random(256) < 135 burns one or
            -- two bars) leaves 0-4 of the six bars; any left over fill the backpack before Ungadulu's holy
            -- force (b51-seam1: 'Your inventory is full.' three runs of three). They are spares the
            -- setup staged, not quest items, so drop every one. The drop verb grades on the ground
            -- count rising, which a second identical bar on the same tile does not do, so the backpack
            -- count below is the evidence, not each drop.
            for _ = 1, 6 do
                local _, left = t.inv.count("gold_bar")
                if left == 0 then break end
                t.player.drop("gold_bar")
                t.ticks(1)
            end
            do
                local _, left = t.inv.count("gold_bar")
                t.check("makeBowl.spare-bars-dropped", left == 0, "gold bars left " .. tostring(left))
            end

            ---------------------------------------------------------------- enterJungleWithBowl
            t.exec("goto-enterJungleWithBowl", t.player.goto_tile, 2791, 2917, 0)
            do
                local _, at = t.world.tile()
                t.check("enterJungleWithBowl", at.z < 2934 and at.x > 2780, "in the Kharazi jungle south of the dense band at "
                    .. at.x .. "," .. at.z .. ", golden bowl, bullroarer, book, machete, axe, food carried")
            end

            ---------------------------------------------------------------- spinBullToBless
            local met = false
            local swings = 0
            for i = 1, 16 do
                t.exec("spinBullToBless-" .. i, t.player.inv_op, "bullroarer", 1)
                swings = i
                t.ticks(14)
                if t.npc.nearest("gujuo", 14) == "ok" then met = true break end
            end
            t.check("spinBullToBless", met, "Gujuo appeared after " .. swings .. " swing(s) of the bullroarer")

            ---------------------------------------------------------------- talkToGujuoWithBowl
            -- gujuo.rs2:30 gujuo_start -> gujuo_bless_bowl (:490): Prayer >= 42, a chant of p_delays,
            -- then stat_random(prayer,80,250) TRUE is the FAILURE (-5 Prayer, "try again?"). Below 42
            -- Gujuo refuses, so the choice depends on the live Prayer level and a prayer restore is
            -- drunk between attempts.
            local function prayer_level()
                local _, reading = t.skill.read("prayer")
                return type(reading) == "table" and reading.level or 0
            end
            local function drink_restore(step)
                for _, pot in ipairs({ "4doseprayerrestore", "3doseprayerrestore", "2doseprayerrestore", "1doseprayerrestore" }) do
                    local got, n = t.inv.count(pot)
                    if got == "ok" and n > 0 then
                        t.exec(step, t.player.inv_op, pot, 1)
                        t.ticks(3)
                        return true
                    end
                end
                return false
            end
            local function bless_chat(name)
                t.await({ level = function() return t.chat.kind() ~= "none" end, note = "page" }, 25)
                for n = 1, 40 do
                    local _, kind = t.chat.drain({ stop_at = "options", max_pages = 40 })
                    if kind ~= "options" then return kind end
                    local patterns
                    if prayer_level() >= 42 then
                        patterns = { "/bless my gold bowl/", "/thanks for your help/", "/Goodbye/" }
                    else
                        patterns = { "/help with something else/", "/I.ll wait/", "/thanks for your help/", "/Goodbye/" }
                    end
                    local chose
                    for _, p in ipairs(patterns) do
                        if t.chat.choose(p) == "ok" then chose = p break end
                    end
                    t.step(name .. "-choose" .. n, chose and "PASS" or "FAIL",
                        "prayer " .. prayer_level() .. ", chose " .. tostring(chose))
                    if not chose then return "options" end
                    t.ticks(1)
                end
                return "options"
            end
            local blessed = false
            for attempt = 1, 5 do
                if prayer_level() < 42 then
                    if not drink_restore("talkToGujuoWithBowl-restore-" .. attempt) then break end
                end
                -- Gujuo walks off when a conversation ends: swing the bullroarer again for him
                for swing = 1, 16 do
                    if t.npc.nearest("gujuo", 14) == "ok" then break end
                    t.exec("talkToGujuoWithBowl-reswing-" .. attempt .. "-" .. swing, t.player.inv_op, "bullroarer", 1)
                    t.ticks(14)
                end
                local step = attempt == 1 and "talkToGujuoWithBowl" or ("talkToGujuoWithBowl-retry-" .. attempt)
                t.exec(step, t.player.talk_to, "gujuo")
                local kind = bless_chat(step .. "-dialog")
                t.ticks(2)
                local _, blessed_bowls = t.inv.count("goldbowlbless_empty")
                if blessed_bowls == 1 then
                    blessed = true
                    t.check(step .. "-blessed", true, "blessed golden bowl in the backpack after " .. attempt
                        .. " attempt(s); Prayer now " .. prayer_level() .. "; dialogue ended on " .. tostring(kind))
                    break
                end
            end
            if not blessed then
                t.check("talkToGujuoWithBowl-blessed", false, "the bowl was not blessed in five attempts")
                t.blocked("talkToGujuoWithBowl: five blessing attempts failed (stat_random(prayer,80,250)); rerun")
                return
            end

            ---------------------------------------------------------------- useMacheteOnReeds
            -- rs2:1129: the machete on the tall reeds cuts a hollow reed
            t.exec("goto-useMacheteOnReeds", t.player.goto_tile, 2834, 2916, 0)
            local reeds = t.player.by_symbol("loc", "tall_reeds")
            t.exec("useMacheteOnReeds", t.player.use_on, "machette", reeds, { at = { 2836, 2916 } })
            t.exec("useMacheteOnReeds-reed", t.inv.await, "reed_hollow", 1, 10)

            ---------------------------------------------------------------- useReedOnPool
            -- rs2:1039: with the blessed bowl the reed syphons pure water into it and stage 8 -> 10
            local pool = t.player.by_symbol("loc", "sacred_water")
            t.exec("useReedOnPool", t.player.use_on, "reed_hollow", pool, { at = { 2837, 2915 } })
            t.chat.drain({ max_pages = 6 })
            t.ticks(2)
            do
                local _, pure = t.inv.count("goldbowlbless_pure")
                local _, empty = t.inv.count("goldbowlbless_empty")
                t.check("useReedOnPool-filled", pure == 1 and empty == 0,
                    "blessed pure-water bowl x" .. pure .. ", empty blessed bowl x" .. empty)
            end
            t.expect("quest.stage.filled_bowl", t.quest.expect_stage(10))

            ---------------------------------------------------------------- enterMossyRockWithBowl
            local function await_underground(note)
                return t.await({ level = underground, note = note }, 16)
            end
            t.exec("goto-enterMossyRockWithBowl", t.player.goto_tile, 2782, 2935, 0)
            local inside = false
            for i = 1, 12 do
                t.exec("enterMossyRockWithBowl-press-" .. i, t.player.click_loc, "lgshamancaverock1", 1)
                if not converse("enterMossyRockWithBowl-dialog-" .. i, { "/crawl through/" }) then break end
                if await_underground("enterMossyRockWithBowl underground") == "ok" then inside = true break end
            end
            do
                local _, at = t.world.tile()
                t.check("enterMossyRockWithBowl", inside, "crawled into the Ungadulu cave at " .. at.x .. "," .. at.z
                    .. " with the blessed pure-water bowl")
            end

            ---------------------------------------------------------------- useBowlOnFireWall
            -- rs2:301: pure water on the wall of fire deletes the wall and walks the player through
            local wall = t.player.by_symbol("loc", "lqfirewall_straight")
            t.exec("useBowlOnFireWall", t.player.use_on, "goldbowlbless_pure", wall, { at = { 2790, 9333 } })
            t.chat.drain({ max_pages = 6 })
            t.ticks(3)
            do
                local _, at = t.world.tile()
                t.check("useBowlOnFireWall-through", at.z < 9333 and at.z > 9322,
                    "walked through the extinguished wall into the octagram at " .. at.x .. "," .. at.z)
            end

            ---------------------------------------------------------------- useBindingBookOnUngadulu
            -- ungadulu.rs2:500: mesbox, Ungadulu's chat, mesbox, then Nezikchened appears (owner-private)
            -- and the stage moves to summoned_nezikchened_fire
            local shaman = t.player.by_symbol("npc", "ungadulu_good")
            t.exec("useBindingBookOnUngadulu", t.player.use_on, "book_of_binding", shaman)
            t.chat.drain({ max_pages = 12 })
            t.ticks(2)
            t.expect("quest.stage.summoned_nezikchened_fire", t.quest.expect_stage(11))

            ---------------------------------------------------------------- fightNezikchenedInFire
            t.exec("fightNezikchenedInFire", t.player.attack, "nezikchened", 2, 20)
            do
                local _, sharks_before = t.inv.count("shark")
                local _, detail = t.exec("fightNezikchenedInFire.dead", t.npc.await_dead_engaged, 500, 60, { eat = { item = "shark", below = 50 } })
                local lowest = tonumber(tostring(detail):match("lowest hp (%d+)/"))
                local _, sharks_left = t.inv.count("shark")
                t.check("fightNezikchenedInFire-margin", lowest ~= nil and lowest >= 25 and (sharks_left or 0) >= 1,
                    "lowest hp " .. tostring(lowest) .. "/99, sharks " .. tostring(sharks_before) .. " -> " .. tostring(sharks_left)
                    .. " (margin: lowest hp >= 25 AND sharks left >= 1)")
            end
            -- nezikchened.rs2:99: the corpse stage, then a last-ditch hit and Ungadulu's mesbox
            t.ticks(6)
            t.chat.drain({ max_pages = 8 })
            t.ticks(2)
            t.expect("quest.stage.defeated_nezikchened_fire", t.quest.expect_stage(12))

            local _, stage = t.var.server("varp139_legendsquest")
            local _, at = t.world.tile()
            local _, sharks_left = t.inv.count("shark")
            local _, pure_bowl = t.inv.count("goldbowlbless_pure")
            local _, hp = t.skill.read("hitpoints")
            t.check("leg.4.end", stage == 12 and t.chat.kind() == "none",
                "tile " .. at.x .. "," .. at.z .. " level " .. (at.level or 0) .. ", legendsquest=" .. tostring(stage)
                .. " read from the server; Nezikchened's first form is dead, sharks left " .. tostring(sharks_left)
                .. ", blessed pure-water bowl x" .. tostring(pure_bowl) .. ", hitpoints " .. tostring(type(hp) == "table" and hp.level or hp)
                .. ", Prayer " .. prayer_level())
            -- LEG 4 END
        end },
        { name = "seeds_and_dried_pool", run = function(t)
            -- LEG 5 BEGIN: talkToUngadulu
            local function converse(name, choices)
                t.await({ level = function() return t.chat.kind() ~= "none" end, note = "page" }, 25)
                for i, c in ipairs(choices or {}) do
                    local _, kind = t.chat.drain({ stop_at = "options", max_pages = 40 })
                    if kind ~= "options" then
                        t.step(name .. "-opt" .. i, "FAIL", "wanted options for '" .. c .. "', got " .. tostring(kind))
                        return false
                    end
                    t.exec(name .. "-choose" .. i, t.chat.choose, c)
                    t.ticks(1)
                end
                local result, kind = t.chat.drain({ max_pages = 40 })
                if kind == "options" then
                    t.step(name .. "-end", "FAIL", "unanswered options page")
                    return false
                end
                t.check(name .. "-end", result == "ok", "drained to " .. tostring(kind))
                return true
            end
            local function underground()
                local _, at = t.world.tile()
                return at.z > 9000
            end

            ---------------------------------------------------------------- talkToUngadulu
            -- ungadulu.rs2:116 ungadulu_after_demon at stage 12: the "Yommi tree seeds" option
            -- (:394) gives three yommi seeds after an objbox, then a three-option page
            t.exec("talkToUngadulu", t.player.talk_to, "ungadulu_good")
            converse("talkToUngadulu-dialog", { "/Yommi tree seeds/", "/Ok, thanks/" })
            t.ticks(2)
            do
                local _, seeds = t.inv.count("yommiseeds")
                t.check("talkToUngadulu-seeds", seeds == 3, "yommiseeds in the backpack: " .. tostring(seeds))
            end

            ---------------------------------------------------------------- useBowlOnSeeds
            -- quest_legends.rs2:1208 [opheldu,goldbowlbless_pure] with the seeds: the seeds germinate,
            -- the pure water is used up (the bowl is left empty) and stage 12 -> 13
            t.exec("useBowlOnSeeds", t.player.use_item_on_item, "yommiseeds", "goldbowlbless_pure")
            t.chat.drain({ max_pages = 6 })
            t.ticks(2)
            do
                local _, germ = t.inv.count("yommiseeds_germ")
                local _, plain = t.inv.count("yommiseeds")
                local _, empty = t.inv.count("goldbowlbless_empty")
                t.check("useBowlOnSeeds-germinated", germ == 3 and plain == 0 and empty == 1,
                    "germinated yommi seeds x" .. germ .. ", plain seeds x" .. plain .. ", empty blessed bowl x" .. empty)
            end
            t.expect("quest.stage.germinated_seeds", t.quest.expect_stage(13))

            ---------------------------------------------------------------- leaveCaveWithSeed
            -- quest_legends.rs2:392: the cave exit crawls the player back out to 2781,2934
            -- the octagram's wall of fire lies between the player and the exit; with Nezikchened dead
            -- the flames let the player through (quest_legends.rs2:221 legends_touch_fire_wall)
            t.exec("leaveCaveWithSeed-firewall", t.player.click_loc, "lqfirewall_straight", 1, { at = { 2790, 9333 } })
            t.ticks(4)
            do
                local _, at = t.world.tile()
                t.check("leaveCaveWithSeed-firewall-through", at.z > 9333,
                    "walked out through the wall of fire to " .. at.x .. "," .. at.z)
            end
            t.player.walk_to(2775, 9341, 20)
            t.ticks(2)
            local out = false
            for i = 1, 6 do
                t.exec("leaveCaveWithSeed-press-" .. i, t.player.click_loc, "shaman_entrance_caver", 1)
                if t.await({ level = function() return not underground() end, note = "back on the surface" }, 16) == "ok" then
                    out = true
                    break
                end
            end
            do
                local _, at = t.world.tile()
                t.check("leaveCaveWithSeed", out and at.z < 3000, "crawled out of the Ungadulu cave to " .. at.x .. "," .. at.z
                    .. " carrying the germinated seeds")
            end

            ---------------------------------------------------------------- useMacheteOnReedsAgain
            t.exec("goto-useMacheteOnReedsAgain", t.player.goto_tile, 2834, 2916, 0)
            local reeds = t.player.by_symbol("loc", "tall_reeds")
            t.exec("useMacheteOnReedsAgain", t.player.use_on, "machette", reeds, { at = { 2836, 2916 } })
            t.exec("useMacheteOnReedsAgain-reed", t.inv.await, "reed_hollow", 1, 10)

            ---------------------------------------------------------------- useReedOnPoolAgain
            -- quest_legends.rs2:1033: with the seeds germinated the pool is a dried sludge and stage
            -- 13 -> 14 (the reed is not used up)
            local pool = t.player.by_symbol("loc", "sacred_water")
            t.exec("useReedOnPoolAgain", t.player.use_on, "reed_hollow", pool, { at = { 2837, 2915 } })
            t.chat.drain({ max_pages = 6 })
            t.ticks(2)
            t.expect("quest.stage.water_pool_dried_up", t.quest.expect_stage(14))

            ---------------------------------------------------------------- spinBullAfterSeeds
            -- bullroarer.rs2:19: only inside the kharazi_jungle zone does a native answer; the pool's
            -- reeds at x=2836 lie outside it, so go back to the jungle spot leg 4 swung at
            t.exec("goto-spinBullAfterSeeds", t.player.goto_tile, 2791, 2917, 0)
            local met = false
            local swings = 0
            for i = 1, 16 do
                if t.npc.nearest("gujuo", 14) == "ok" then met = true break end
                t.exec("spinBullAfterSeeds-" .. i, t.player.inv_op, "bullroarer", 1)
                swings = i
                t.ticks(14)
                if t.npc.nearest("gujuo", 14) == "ok" then met = true break end
            end
            t.check("spinBullAfterSeeds", met, "Gujuo appeared after " .. swings .. " swing(s) of the bullroarer")

            ---------------------------------------------------------------- talkToGujuoAfterSeeds
            -- gujuo.rs2:72 (stage 14) -> gujuo_pool_dried (:318) -> gujuo_source (:342) -> gujuo_helpme
            -- (:369) which sets stage 15 (talk_gujuo_pool) after the recipe hint
            t.exec("talkToGujuoAfterSeeds", t.player.talk_to, "gujuo")
            converse("talkToGujuoAfterSeeds-dialog", {
                "/pool has dried up/", "/Where is the source/", "/could you help me/", "/thanks for your help/" })
            t.ticks(2)
            t.expect("quest.stage.talk_gujuo_pool", t.quest.expect_stage(15))

            local _, stage = t.var.server("varp139_legendsquest")
            local _, at = t.world.tile()
            local _, germ = t.inv.count("yommiseeds_germ")
            local _, reed = t.inv.count("reed_hollow")
            t.check("leg.5.end", stage == 15 and t.chat.kind() == "none",
                "tile " .. at.x .. "," .. at.z .. " level " .. (at.level or 0) .. ", legendsquest=" .. tostring(stage)
                .. " read from the server; germinated yommi seeds x" .. tostring(germ) .. ", hollow reed x" .. tostring(reed)
                .. ", empty blessed golden bowl, bullroarer, machete, axe, pickaxe carried")
            -- LEG 5 END
        end },
        { name = "bravery_and_heart_crystal", run = function(t)
            -- LEG 6 BEGIN: addArdrigal
            local function converse(name, choices)
                t.await({ level = function() return t.chat.kind() ~= "none" end, note = "page" }, 25)
                for i, c in ipairs(choices or {}) do
                    local _, kind = t.chat.drain({ stop_at = "options", max_pages = 40 })
                    if kind ~= "options" then
                        t.step(name .. "-opt" .. i, "FAIL", "wanted options for '" .. c .. "', got " .. tostring(kind))
                        return false
                    end
                    t.exec(name .. "-choose" .. i, t.chat.choose, c)
                    t.ticks(1)
                end
                local result, kind = t.chat.drain({ max_pages = 40 })
                if kind == "options" then
                    t.step(name .. "-end", "FAIL", "unanswered options page")
                    return false
                end
                t.check(name .. "-end", result == "ok", "drained to " .. tostring(kind))
                return true
            end
            -- drain pages until none reopens for 6 ticks (scripts with p_delay between boxes)
            local function settle_chat(name)
                for _ = 1, 8 do
                    local came = t.await({ level = function() return t.chat.kind() ~= "none" end, note = "page" }, 6)
                    if came ~= "ok" then return true end
                    local _, kind = t.chat.drain({ stop_at = "options", max_pages = 20 })
                    if kind == "options" then
                        t.step(name .. "-options", "FAIL", "an options page is still open")
                        return false
                    end
                end
                return true
            end
            local function underground()
                local _, at = t.world.tile()
                return at.z > 9000
            end
            local function kill(name, symbol)
                t.exec(name, t.player.attack, symbol, 2, 30)
                t.exec(name .. "-dead", t.npc.await_dead_engaged, 300, 6, { eat = { item = "shark", below = 50 } })
                settle_chat(name .. "-settle")
            end

            ---------------------------------------------------------------- the pack for the descent
            -- Quest Helper (enterJungleToGoToSource): a bravery potion (made below from an ardrigal, a
            -- snake weed and a vial of water), Charge Orb runes and an unpowered orb, a rope, lockpicks,
            -- combat gear and food. The backpack is full of leg 4/5's leftovers, so drop the hammer, the
            -- unused book of binding first.
            for _, junk in ipairs({ "book_of_binding", "hammer" }) do
                for _ = 1, 8 do
                    local _, remaining = t.inv.count(junk)
                    if remaining == 0 then break end
                    t.player.drop(junk)
                    t.ticks(1)
                end
            end
            -- (Druidic Ritual, which unlocks Herblore for the bravery potion, is completed in setup)
            for _, gift in ipairs({ "ardrigal 1", "snake_weed 1", "vial_water 1", "rope 1", "stafforb 2", "cosmicrune 6",
                                    "waterrune 60", "lockpick 3", "shark 4" }) do
                t.cheat("::give " .. gift)
                t.ticks(1)
            end
            do
                local _, ardrigal = t.inv.count("ardrigal")
                local _, snake = t.inv.count("snake_weed")
                local _, vial = t.inv.count("vial_water")
                local _, rope = t.inv.count("rope")
                local _, orbs = t.inv.count("stafforb")
                local _, sharks = t.inv.count("shark")
                local _, picks = t.inv.count("lockpick")
                t.check("leg.6.pack", ardrigal == 1 and snake == 1 and vial == 1 and rope == 1 and orbs == 2 and sharks >= 4,
                    "ardrigal " .. ardrigal .. ", snake weed " .. snake .. ", vial of water " .. vial .. ", rope " .. rope
                    .. ", unpowered orbs " .. orbs .. ", sharks " .. sharks .. ", lockpicks " .. picks)
            end

            ---------------------------------------------------------------- addArdrigal
            -- shared Herblore recipe (ardrigal + vial of water) then quest_legends.rs2 snake weed on the
            -- ardrigal solution; the potion is the permanent-bravery access receipt
            t.exec("addArdrigal", t.player.use_item_on_item, "ardrigal", "vial_water")
            settle_chat("addArdrigal-settle")
            t.exec("addSnake", t.player.use_item_on_item, "snake_weed", "ardrigal_sol")
            settle_chat("addSnake-settle")
            t.exec("addArdrigalToSnake", t.inv.await, "bravery_pot", 1, 10)

            ---------------------------------------------------------------- enterJungleToGoToSource
            do
                local _, at = t.world.tile()
                local _, axe = t.inv.count("rune_axe")
                local _, machete = t.inv.count("machette")
                local _, pickaxe = t.inv.count("rune_pickaxe")
                local _, potion = t.inv.count("bravery_pot")
                local _, bowl = t.inv.count("goldbowlbless_empty")
                t.check("enterJungleToGoToSource", axe == 1 and machete == 1 and pickaxe == 1 and potion == 1 and bowl == 1,
                    "standing in the Kharazi jungle at " .. at.x .. "," .. at.z .. " with rune axe x" .. axe .. ", machete x" .. machete
                    .. ", rune pickaxe x" .. pickaxe .. ", bravery potion x" .. potion .. ", blessed bowl x" .. bowl
                    .. ", the charge orb runes, orb, rope, lockpicks and food")
            end

            ---------------------------------------------------------------- enterMossyRockToSource
            -- plain travel to the rock (2782,2937); legends_search_rocks is an agility roll, repeated
            t.exec("goto-enterMossyRockToSource", t.player.goto_tile, 2782, 2935, 0)
            local inside = false
            for i = 1, 12 do
                t.exec("enterMossyRockToSource-press-" .. i, t.player.click_loc, "lgshamancaverock1", 1)
                if not converse("enterMossyRockToSource-dialog-" .. i, { "/crawl through/" }) then break end
                if t.await({ level = underground, note = "underground" }, 16) == "ok" then inside = true break end
            end
            do
                local _, at = t.world.tile()
                t.check("enterMossyRockToSource", inside, "crawled through the Mossy Rocks to " .. at.x .. "," .. at.z)
            end

            ---------------------------------------------------------------- the cave up to the marked wall
            -- the same trials as the first trip (bookcase, lockpick gate, boulders, strength doors, the
            -- crumbled wall): none of them stays open behind the player
            local past_bookcase = false
            for i = 1, 12 do
                t.exec("src-enterBookcase-press-" .. i, t.player.click_loc, "shaman_bookcase", 1)
                if not converse("src-enterBookcase-dialog-" .. i, { "Yes please!" }) then break end
                local squeezed = t.await({ level = function()
                    local _, at = t.world.tile()
                    return at.x >= 2798
                end, note = "squeezed past the bookcase" }, 14)
                if squeezed == "ok" then past_bookcase = true break end
            end
            do
                local _, at = t.world.tile()
                t.check("src-enterBookcase", past_bookcase, "in the tunnel behind the bookcase at " .. at.x .. "," .. at.z)
            end
            -- the double doors this leg opened on the first trip (temporary loc_del/loc_add swings,
            -- legends_procs.rs2 legends_double_door_swing) are back in the world: the loc revert queue
            -- keeps one lifecycle per loc (seam32), so they are picked/forced as on the first trip.
            local gate1_present = t.world.loc_near("lglockpickgatebottoml", 30) == "ok"
            do
                local _, at = t.world.tile()
                t.check("src-gate1-state", gate1_present, "at " .. at.x .. "," .. at.z .. " lockpick gate present: " .. tostring(gate1_present))
            end
            local through_gate1 = false
            for i = 1, 14 do
                t.exec("src-enterGate1-press-" .. i, t.player.click_loc, "lglockpickgatebottoml", 2)
                local wait = 25
                for _ = 1, 8 do
                    local came = t.await({ level = function() return t.chat.kind() ~= "none" end, note = "lock page" }, wait)
                    if came ~= "ok" then break end
                    t.chat.drain({ max_pages = 40 })
                    wait = 6
                end
                t.ticks(3)
                local _, at = t.world.tile()
                if at.z < 9332 then through_gate1 = true break end
            end
            do
                local _, at = t.world.tile()
                t.check("src-enterGate1", through_gate1, "picked the lock and stands south of the gate line at " .. at.x .. "," .. at.z)
            end
            local boulder_z = { mine_test_boulder1 = 9327, mine_test_boulder2 = 9323, mine_test_boulder3 = 9319 }
            for _, symbol in ipairs({ "mine_test_boulder1", "mine_test_boulder2", "mine_test_boulder3" }) do
                local passed = false
                for i = 1, 14 do
                    t.exec("src-mine-" .. symbol .. "-press-" .. i, t.player.click_loc, symbol, 1)
                    t.ticks(6)
                    local _, at = t.world.tile()
                    if at.z < boulder_z[symbol] then passed = true break end
                end
                local _, at = t.world.tile()
                t.check("src-mine-" .. symbol, passed, "smashed the rock and stands past it at " .. at.x .. "," .. at.z)
            end
            local through_gate2 = false
            for i = 1, 12 do
                t.exec("src-enterGate2-press-" .. i, t.player.click_loc, "lgstrengthtrialgatel", 1)
                converse("src-enterGate2-dialog-" .. i, { "/very strong/" })
                t.ticks(3)
                local _, at = t.world.tile()
                if at.z < 9314 then through_gate2 = true break end
            end
            do
                local _, at = t.world.tile()
                t.check("src-enterGate2", through_gate2, "forced the strength doors and stands south of them at " .. at.x .. "," .. at.z)
            end
            -- the deathwings (m43_145.spawn) near the crumbled wall are aggressive: fight those that engage
            for i = 1, 4 do
                if t.npc.nearest("deathwing", 6) ~= "ok" then break end
                t.exec("src-deathwing-" .. i, t.player.attack, "deathwing", 2, 20)
                t.exec("src-deathwing-dead-" .. i, t.npc.await_dead_engaged, 80, 2, { eat = { item = "shark", below = 50 } })
            end
            t.player.walk_to(2790, 9294, 160)
            local jumped = false
            for i = 1, 8 do
                t.exec("src-jumpCrumbledWall-" .. i, t.player.click_loc, "crumbled_wall", 1)
                local over = t.await({ level = function()
                    local _, at = t.world.tile()
                    return at.z >= 9296
                end, note = "over the crumbled wall" }, 14)
                if over == "ok" then jumped = true break end
            end
            do
                local _, at = t.world.tile()
                t.check("src-jumpCrumbledWall", jumped, "jumped the crumbled wall, now at " .. at.x .. "," .. at.z)
            end
            t.player.walk_to(2780, 9306, 60)

            ---------------------------------------------------------------- searchMarkedWallToSource
            -- quest_legends.rs2:647; the five runes were set on the first trip, so op2 offers the door
            t.exec("searchMarkedWallToSource", t.player.click_loc, "lgancientwalldoor", 2, { at = { 2779, 9305 } })
            converse("searchMarkedWallToSource-dialog", { "Investigate the outline of the door.", "Yes, I'll go through!" })
            settle_chat("searchMarkedWallToSource-settle")
            do
                local _, at = t.world.tile()
                t.check("searchMarkedWallToSource-through", at.x < 2790 and at.z < 9305 and at.z > 9295,
                    "through the marked wall into the gem room at " .. at.x .. "," .. at.z)
            end

            ---------------------------------------------------------------- useSpellOnDoor
            -- LostCity charge_orb.rs2:16 -> legends_cast_orb_door: Charge Water Orb on the fused gate
            t.player.walk_to(2763, 9311, 60)
            do
                local _, at = t.world.tile()
                t.check("walk-magicDoor", at.z >= 9309 and at.z <= 9313, "beside the ancient gate at " .. at.x .. "," .. at.z)
            end
            t.exec("useSpellOnDoor", t.player.cast, "charge_water_orb", t.player.by_symbol("loc", "lgmagictrialgateclosed"), 14)
            settle_chat("useSpellOnDoor-settle")
            do
                local _, at = t.world.tile()
                t.check("useSpellOnDoor-through", at.z >= 9318, "through the magic gate to " .. at.x .. "," .. at.z)
            end

            ---------------------------------------------------------------- useRopeOnWinch
            t.exec("useRopeOnWinch", t.player.use_on, "rope", t.player.by_symbol("loc", "lg_winchdown_norope"))
            settle_chat("useRopeOnWinch-settle")

            ---------------------------------------------------------------- drinkBraveryPotionAndClimbDown
            t.exec("drinkBraveryPotionAndClimbDown", t.player.inv_op, "bravery_pot", 1)
            converse("drinkBraveryPotionAndClimbDown-dialog", { "Yes, I'll bravely drink the bravery potion." })
            settle_chat("drinkBraveryPotionAndClimbDown-settle")
            for i = 1, 4 do
                if t.world.loc_near("lg_winchdown_rope", 8) ~= "ok" then
                    t.exec("searchWinch-" .. i, t.player.click_loc, "lg_winchdown_norope", 1)
                    t.ticks(2)
                end
                t.exec("climbDownWinch-" .. i, t.player.click_loc, "lg_winchdown_rope", 1)
                converse("climbDownWinch-dialog-" .. i, { "Yes, I'll shimmy down the rope into possible doom." })
                settle_chat("climbDownWinch-settle-" .. i)
                if t.await({ level = function()
                    local _, at = t.world.tile()
                    return at.x < 2500
                end, note = "down the winch" }, 10) == "ok" then break end
            end
            do
                local _, at = t.world.tile()
                t.check("climbDownWinch", at.x < 2500, "down the winch into the Viyeldi caves at " .. at.x .. "," .. at.z)
            end
            t.expect("quest.stage.enter_lower_dungeon", t.quest.expect_stage(16))

            ---------------------------------------------------------------- the ledges and climbing rocks
            -- quest_legends.rs2 rocky_ledge/1/2, viycaves_climbrock1-3 (each asks before the crossing)
            for _, ob in ipairs({ { "rocky_ledge", "Yes, I can think of nothing more exciting!" },
                                  { "rocky_ledge1", "Yes, I can think of nothing more exciting!" },
                                  { "rocky_ledge2", "Yes, I can think of nothing more exciting!" },
                                  { "viycaves_climbrock1", "Yes, I want to climb over the rocks." },
                                  { "viycaves_climbrock2", "Yes, I want to climb over the rocks." },
                                  { "viycaves_climbrock3", "Yes, I want to climb over the rocks." } }) do
                local _, from = t.world.tile()
                t.exec(ob[1], t.player.click_loc, ob[1], 1)
                converse(ob[1] .. "-dialog", { ob[2] })
                settle_chat(ob[1] .. "-settle")
                local _, to = t.world.tile()
                t.check(ob[1] .. "-moved", to.x ~= from.x or to.z ~= from.z, from.x .. "," .. from.z .. " -> " .. to.x .. "," .. to.z)
            end

            ---------------------------------------------------------------- the three heroes' crystal pieces
            -- san_tojalon.rs2 / irvig_senay.rs2 / ranalph_devere.rs2 ai_queue3: one piece each
            kill("killSan", "san_tojalon")
            t.exec("sectionA", t.inv.await, "heartcrystal_sectiona", 1, 10)
            kill("killIrvig", "irvig_senay")
            t.exec("sectionB", t.inv.await, "heartcrystal_sectionb", 1, 10)
            kill("killRanalph", "ranalph_devere")
            t.exec("sectionC", t.inv.await, "heartcrystal_sectionc", 1, 10)

            ---------------------------------------------------------------- useCrystalsOnFurnace
            t.player.walk_to(2425, 4724, 80)
            local furnace = t.player.by_symbol("loc", "furnace_legendsquest")
            for _, piece in ipairs({ "heartcrystal_sectiona", "heartcrystal_sectionb", "heartcrystal_sectionc" }) do
                t.exec("useCrystalsOnFurnace-" .. piece, t.player.use_on, piece, furnace)
                converse("useCrystalsOnFurnace-dialog-" .. piece, {})
                settle_chat("useCrystalsOnFurnace-settle-" .. piece)
            end
            t.exec("useCrystalsOnFurnace", t.inv.await, "heartcrystal", 1, 10)
            t.expect("quest.stage.crystal_pieces_smelted", t.quest.expect_stage(17))

            ---------------------------------------------------------------- useHeartOnRock
            t.exec("useHeartOnRock", t.player.use_on, "heartcrystal", t.player.by_symbol("loc", "dragons_eye_rock"))
            settle_chat("useHeartOnRock-settle")
            t.exec("useHeartOnRock-glow", t.inv.await, "heartcrystal_glow", 1, 10)

            ---------------------------------------------------------------- useHeartOnRecess
            t.exec("useHeartOnRecess", t.player.use_on, "heartcrystal_glow", t.player.by_symbol("loc", "heart_recess_empty"))
            settle_chat("useHeartOnRecess-settle")
            t.expect("quest.stage.heart_in_recess", t.quest.expect_stage(18))
            t.exec("forceBarrier", t.player.click_loc, "legendsquest_force_barrier", 1)
            settle_chat("forceBarrier-settle")
            do
                local _, at = t.world.tile()
                t.check("forceBarrier-through", at.z <= 4690, "through the force barrier to " .. at.x .. "," .. at.z)
            end

            ---------------------------------------------------------------- pushBoulder
            t.player.walk_to(2395, 4679, 60)
            t.exec("pushBoulder", t.player.press, "boulder_legends", 1, 10, { at = { 2392, 4678 } })
            -- legends_boulder.rs2:6: below "defeated Nezikchened" the push conjures Echned Zekin from the
            -- mist and opens echned_dialogue on the spot; close it, the talk is the next step
            do
                local wait = 12
                for _ = 1, 6 do
                    if t.await({ level = function() return t.chat.kind() ~= "none" end, note = "echned page" }, wait) ~= "ok" then break end
                    local _, kind = t.chat.drain({ stop_at = "options", max_pages = 20 })
                    if kind == "options" then
                        -- "Er... me?" / "Who's asking?": walk away from it (Escape), talkToEchned reopens it
                        t.chat.close()
                        t.ticks(2)
                        t.check("pushBoulder-echned-closed", t.chat.kind() == "none", "closed Echned's first options page, kind " .. tostring(t.chat.kind()))
                        break
                    end
                    wait = 6
                end
            end
            settle_chat("pushBoulder-settle")
            t.expect("quest.stage.pushed_boulder", t.quest.expect_stage(19))

            ---------------------------------------------------------------- talkToEchned
            -- echned_zekin.rs2 echned_dialogue -> echned_who -> echned_whatdo -> echned_mustwater
            t.exec("talkToEchned", t.player.talk_to, "echned_zekin")
            converse("talkToEchned-dialog", { "Who's asking?", "What can I do about that?",
                "I'll do what I must to get the water.", "Ok, I'll do it." })
            settle_chat("talkToEchned-settle")
            t.exec("talkToEchned-dagger", t.inv.await, "deathdagger", 1, 10)
            t.expect("quest.stage.received_dagger", t.quest.expect_stage(20))

            local _, stage = t.var.server("varp139_legendsquest")
            local _, at = t.world.tile()
            t.check("leg.6.end", stage == 20 and t.chat.kind() == "none",
                "tile " .. at.x .. "," .. at.z .. " level " .. (at.level or 0) .. ", legendsquest=" .. tostring(stage)
                .. " read from the server; Echned Zekin's death dagger carried, the south room beyond the force barrier "
                .. "of the Viyeldi caves; rune armour worn, sharks, bowl, rune axe, machete, pickaxe carried")
            -- LEG 6 END
        end },
        { name = "holy_force_to_the_source", run = function(t)
            -- LEG 7 BEGIN: pickUpHat
            local function converse(name, choices)
                t.await({ level = function() return t.chat.kind() ~= "none" end, note = "page" }, 25)
                for i, c in ipairs(choices or {}) do
                    local _, kind = t.chat.drain({ stop_at = "options", max_pages = 40 })
                    if kind ~= "options" then
                        t.step(name .. "-opt" .. i, "FAIL", "wanted options for '" .. c .. "', got " .. tostring(kind))
                        return false
                    end
                    t.exec(name .. "-choose" .. i, t.chat.choose, c)
                    t.ticks(1)
                end
                local result, kind = t.chat.drain({ max_pages = 40 })
                if kind == "options" then
                    t.step(name .. "-end", "FAIL", "unanswered options page")
                    return false
                end
                t.check(name .. "-end", result == "ok", "drained to " .. tostring(kind))
                return true
            end
            -- drain pages until none reopens for 6 ticks (scripts with p_delay between boxes)
            local function settle_chat(name)
                for _ = 1, 8 do
                    local came = t.await({ level = function() return t.chat.kind() ~= "none" end, note = "page" }, 6)
                    if came ~= "ok" then return true end
                    local _, kind = t.chat.drain({ stop_at = "options", max_pages = 20 })
                    if kind == "options" then
                        t.step(name .. "-options", "FAIL", "an options page is still open")
                        return false
                    end
                end
                return true
            end
            local function underground()
                local _, at = t.world.tile()
                return at.z > 9000
            end

            ---------------------------------------------------------------- the pack for the second fight
            -- Quest Helper (pushBoulderWithForce): combat gear, food and potions. Leg 6 left one shark: the
            -- junk (swamp rocks, the empty vial) is dropped and the food topped up to ten (brought-along food).
            for _, junk in ipairs({ "swamprocks1", "vial_empty" }) do
                for _ = 1, 4 do
                    local _, remaining = t.inv.count(junk)
                    if remaining == 0 then break end
                    t.player.drop(junk)
                    t.ticks(1)
                end
            end
            do
                local _, have = t.inv.count("shark")
                if have < 9 then t.cheat("::give shark " .. (9 - have)) end
            end
            t.ticks(1)
            do
                local _, sharks = t.inv.count("shark")
                local _, dagger = t.inv.count("deathdagger")
                local _, picks = t.inv.count("lockpick")
                local _, cosmic = t.inv.count("cosmicrune")
                local _, water = t.inv.count("waterrune")
                local _, orbs = t.inv.count("stafforb")
                t.check("leg.7.pack", sharks >= 9 and dagger == 1 and orbs == 1 and cosmic >= 3 and water >= 30,
                    "sharks " .. sharks .. ", death dagger " .. dagger .. ", unpowered orb " .. orbs .. ", cosmic runes " .. cosmic
                    .. ", water runes " .. water .. ", lockpicks " .. picks)
            end

            ---------------------------------------------------------------- pickUpHat (killViyeldi)
            -- the south room is beyond the force barrier: click it from the south side to cross back
            t.exec("forceBarrierBack", t.player.click_loc, "legendsquest_force_barrier", 1)
            t.ticks(5)
            t.player.walk_to(2390, 4718, 80)
            -- back over the climbing rocks and ledges to the winch landing (the hat lies beside the rope)
            for _, ob in ipairs({ "viycaves_climbrock3", "viycaves_climbrock2", "viycaves_climbrock1",
                                  "rocky_ledge2", "rocky_ledge1", "rocky_ledge" }) do
                local _, from = t.world.tile()
                for attempt = 1, 4 do
                    t.exec("back-" .. ob .. "-" .. attempt, t.player.click_loc, ob, 1)
                    if t.await({ level = function() return t.chat.kind() ~= "none" end, note = "crossing page" }, 5) == "ok" then
                        local _, kind = t.chat.drain({ stop_at = "options", max_pages = 10 })
                        if kind == "options" then
                            t.chat.choose("/Yes/")
                            t.ticks(1)
                        end
                        settle_chat("back-" .. ob .. "-settle-" .. attempt)
                    end
                    t.ticks(4)
                    local _, to = t.world.tile()
                    if to.x ~= from.x or to.z ~= from.z then break end
                end
                local _, to = t.world.tile()
                t.check("back-" .. ob, to.x ~= from.x or to.z ~= from.z, from.x .. "," .. from.z .. " -> " .. to.x .. "," .. to.z)
            end
            t.exec("goto-pickUpHat", t.world.obj_near, "viyeldihat", 8)
            local picked = false
            for _, yaw in ipairs({ 1024, 0, 512, 1536, 256, 768, 1280, 1792 }) do
                t.drive.camera(yaw, 383, 500)
                t.ticks(1)
                local hat_result = t.player.click_obj("viyeldihat", 3)
                t.ticks(2)
                if t.chat.kind() ~= "none" then picked = true break end
            end
            t.check("pickUpHat", picked, "the hat pressed, chat page " .. tostring(t.chat.kind()))
            -- viyeldi.rs2:1 opobj3: the hat animates Viyeldi (owner-summoned) and opens his dialogue, which ends
            -- with npc_del; leaving the page before it ends keeps him standing so the dagger can be used
            t.chat.close()
            t.ticks(3)
            t.check("pickUpHat-viyeldi", t.npc.nearest("viyeldi", 8) == "ok", "Viyeldi stands beside the hat, chat " .. tostring(t.chat.kind()))

            ---------------------------------------------------------------- killViyeldi
            -- viyeldi.rs2:20 opnpcu: the death dagger becomes the glowing dagger and the spirit crumples
            t.exec("killViyeldi", t.player.use_on, "deathdagger", t.player.by_symbol("npc", "viyeldi"))
            settle_chat("killViyeldi-settle")
            t.exec("killViyeldi-glowing", t.inv.await, "deathdaggerdone", 1, 10)
            t.exec("killViyeldi-gone", t.npc.await_gone, "viyeldi", 8, 30)

            ---------------------------------------------------------------- enterMossyRockHolyForce
            -- guide (pickUpHat): "teleport out now, and you'll be guided to get the holy force" -- the
            -- teleport is plain travel to the surface; the rocks are then entered by hand
            t.exec("goto-enterMossyRockHolyForce", t.player.goto_tile, 2782, 2935, 0)
            local inside = false
            for i = 1, 12 do
                t.exec("enterMossyRockHolyForce-press-" .. i, t.player.click_loc, "lgshamancaverock1", 1)
                if not converse("enterMossyRockHolyForce-dialog-" .. i, { "/crawl through/" }) then break end
                if t.await({ level = underground, note = "underground" }, 16) == "ok" then inside = true break end
            end
            do
                local _, at = t.world.tile()
                t.check("enterMossyRockHolyForce", inside, "crawled through the Mossy Rocks to " .. at.x .. "," .. at.z)
            end

            ---------------------------------------------------------------- talkToUngaduluForForce
            -- quest_legends.rs2:221 legends_touch_fire_wall: with Nezikchened's first form dead the flames let the player in
            t.exec("talkToUngaduluForForce-firewall", t.player.click_loc, "lqfirewall_straight", 1, { at = { 2790, 9333 } })
            t.ticks(4)
            do
                local _, at = t.world.tile()
                t.check("talkToUngaduluForForce-inside", at.z < 9333, "inside the octagram at " .. at.x .. "," .. at.z)
            end
            -- ungadulu.rs2:538: the glowing dagger handed over; "I've killed Viyeldi." earns the Holy Force
            t.exec("talkToUngaduluForForce", t.player.use_on, "deathdaggerdone", t.player.by_symbol("npc", "ungadulu_good"))
            converse("talkToUngaduluForForce-dialog", { "/killed Viyeldi/" })
            settle_chat("talkToUngaduluForForce-settle")
            t.exec("talkToUngaduluForForce-holyforce", t.inv.await, "holyforce", 1, 10)
            t.exec("talkToUngaduluForForce-dagger", t.inv.expect_absent, "deathdaggerdone")
            -- back out through the wall of fire, then the same trials as the first two trips
            t.exec("hf-leaveOctagram", t.player.click_loc, "lqfirewall_straight", 1, { at = { 2790, 9333 } })
            t.ticks(4)
            local past_bookcase = false
            for i = 1, 12 do
                t.exec("hf-enterBookcase-press-" .. i, t.player.click_loc, "shaman_bookcase", 1)
                if not converse("hf-enterBookcase-dialog-" .. i, { "Yes please!" }) then break end
                local squeezed = t.await({ level = function()
                    local _, at = t.world.tile()
                    return at.x >= 2798
                end, note = "squeezed past the bookcase" }, 14)
                if squeezed == "ok" then past_bookcase = true break end
            end
            do
                local _, at = t.world.tile()
                t.check("hf-enterBookcase", past_bookcase, "in the tunnel behind the bookcase at " .. at.x .. "," .. at.z)
            end
            -- the double doors this leg opened on the first trip (temporary loc_del/loc_add swings,
            -- legends_procs.rs2 legends_double_door_swing) are back in the world: the loc revert queue
            -- keeps one lifecycle per loc (seam32), so they are picked/forced as on the first trip.
            local gate1_present = t.world.loc_near("lglockpickgatebottoml", 30) == "ok"
            do
                local _, at = t.world.tile()
                t.check("hf-gate1-state", gate1_present, "at " .. at.x .. "," .. at.z .. " lockpick gate present: " .. tostring(gate1_present))
            end
            local through_gate1 = false
            for i = 1, 14 do
                t.exec("hf-enterGate1-press-" .. i, t.player.click_loc, "lglockpickgatebottoml", 2)
                local wait = 25
                for _ = 1, 8 do
                    local came = t.await({ level = function() return t.chat.kind() ~= "none" end, note = "lock page" }, wait)
                    if came ~= "ok" then break end
                    t.chat.drain({ max_pages = 40 })
                    wait = 6
                end
                t.ticks(3)
                local _, at = t.world.tile()
                if at.z < 9332 then through_gate1 = true break end
            end
            do
                local _, at = t.world.tile()
                t.check("hf-enterGate1", through_gate1, "picked the lock and stands south of the gate line at " .. at.x .. "," .. at.z)
            end
            local boulder_z = { mine_test_boulder1 = 9327, mine_test_boulder2 = 9323, mine_test_boulder3 = 9319 }
            for _, symbol in ipairs({ "mine_test_boulder1", "mine_test_boulder2", "mine_test_boulder3" }) do
                local passed = false
                for i = 1, 14 do
                    t.exec("hf-mine-" .. symbol .. "-press-" .. i, t.player.click_loc, symbol, 1)
                    t.ticks(6)
                    local _, at = t.world.tile()
                    if at.z < boulder_z[symbol] then passed = true break end
                end
                local _, at = t.world.tile()
                t.check("hf-mine-" .. symbol, passed, "smashed the rock and stands past it at " .. at.x .. "," .. at.z)
            end
            local through_gate2 = false
            for i = 1, 12 do
                t.exec("hf-enterGate2-press-" .. i, t.player.click_loc, "lgstrengthtrialgatel", 1)
                converse("hf-enterGate2-dialog-" .. i, { "/very strong/" })
                t.ticks(3)
                local _, at = t.world.tile()
                if at.z < 9314 then through_gate2 = true break end
            end
            do
                local _, at = t.world.tile()
                t.check("hf-enterGate2", through_gate2, "forced the strength doors and stands south of them at " .. at.x .. "," .. at.z)
            end
            -- the deathwings (m43_145.spawn) near the crumbled wall are aggressive: fight those that engage
            for i = 1, 4 do
                if t.npc.nearest("deathwing", 6) ~= "ok" then break end
                t.exec("hf-deathwing-" .. i, t.player.attack, "deathwing", 2, 20)
                t.exec("hf-deathwing-dead-" .. i, t.npc.await_dead_engaged, 80, 2, { eat = { item = "shark", below = 50 } })
            end
            t.player.walk_to(2790, 9294, 160)
            local jumped = false
            for i = 1, 8 do
                t.exec("hf-jumpCrumbledWall-" .. i, t.player.click_loc, "crumbled_wall", 1)
                local over = t.await({ level = function()
                    local _, at = t.world.tile()
                    return at.z >= 9296
                end, note = "over the crumbled wall" }, 14)
                if over == "ok" then jumped = true break end
            end
            do
                local _, at = t.world.tile()
                t.check("hf-jumpCrumbledWall", jumped, "jumped the crumbled wall, now at " .. at.x .. "," .. at.z)
            end
            t.player.walk_to(2780, 9306, 60)

            ---------------------------------------------------------------- hf-searchMarkedWall
            -- quest_legends.rs2:647; the five runes were set on the first trip, so op2 offers the door
            t.exec("hf-searchMarkedWall", t.player.click_loc, "lgancientwalldoor", 2, { at = { 2779, 9305 } })
            converse("hf-searchMarkedWall-dialog", { "Investigate the outline of the door.", "Yes, I'll go through!" })
            settle_chat("hf-searchMarkedWall-settle")
            do
                local _, at = t.world.tile()
                t.check("hf-searchMarkedWall-through", at.x < 2790 and at.z < 9305 and at.z > 9295,
                    "through the marked wall into the gem room at " .. at.x .. "," .. at.z)
            end

            ---------------------------------------------------------------- useSpellOnDoorHolyForce
            -- LostCity charge_orb.rs2:16 -> legends_cast_orb_door: Charge Water Orb on the fused gate
            t.player.walk_to(2763, 9311, 60)
            do
                local _, at = t.world.tile()
                t.check("hf-walk-magicDoor", at.z >= 9309 and at.z <= 9313, "beside the ancient gate at " .. at.x .. "," .. at.z)
            end
            t.exec("useSpellOnDoorHolyForce", t.player.cast, "charge_water_orb", t.player.by_symbol("loc", "lgmagictrialgateclosed"), 14)
            settle_chat("useSpellOnDoorHolyForce-settle")
            do
                local _, at = t.world.tile()
                t.check("useSpellOnDoorHolyForce-through", at.z >= 9318, "through the magic gate to " .. at.x .. "," .. at.z)
            end

            ---------------------------------------------------------------- climbDownWinchHolyForce
            -- the rope stays tied (bit legends_tied_rope_winch); the bravery potion is still in effect
            for i = 1, 4 do
                if t.world.loc_near("lg_winchdown_rope", 8) ~= "ok" then
                    t.exec("hf-searchWinch-" .. i, t.player.click_loc, "lg_winchdown_norope", 1)
                    t.ticks(2)
                end
                t.exec("climbDownWinchHolyForce-" .. i, t.player.click_loc, "lg_winchdown_rope", 1)
                converse("climbDownWinchHolyForce-dialog-" .. i, { "Yes, I'll shimmy down the rope into possible doom." })
                settle_chat("climbDownWinchHolyForce-settle-" .. i)
                if t.await({ level = function()
                    local _, at = t.world.tile()
                    return at.x < 2500
                end, note = "down the winch" }, 10) == "ok" then break end
            end
            do
                local _, at = t.world.tile()
                t.check("climbDownWinchHolyForce", at.x < 2500, "down the winch into the Viyeldi caves at " .. at.x .. "," .. at.z)
            end
            -- back over the ledges and climbing rocks (quest_legends.rs2:1456-1665), forwards as on the first trip.
            -- A rock crossed from its far side climbs without a question, and a slip moves the player: click until
            -- the tile changes, answering a page only when one opens.
            for _, ob in ipairs({ "rocky_ledge", "rocky_ledge1", "rocky_ledge2",
                                  "viycaves_climbrock1", "viycaves_climbrock2", "viycaves_climbrock3" }) do
                local _, from = t.world.tile()
                local is_rock = string.find(ob, "climbrock", 1, true) ~= nil
                for attempt = 1, 8 do
                    t.player.click_loc(ob, 1)
                    if t.await({ level = function() return t.chat.kind() ~= "none" end, note = "crossing page" }, 5) == "ok" then
                        local _, kind = t.chat.drain({ stop_at = "options", max_pages = 10 })
                        if kind == "options" then
                            t.chat.choose("/Yes/")
                            t.ticks(1)
                        end
                        settle_chat("hf-" .. ob .. "-settle-" .. attempt)
                    end
                    t.ticks(4)
                    local _, to = t.world.tile()
                    local moved = to.x ~= from.x or to.z ~= from.z
                    if moved and not is_rock then break end
                    -- a slip ("You slip and fall!") drops the player beside the rock: cross again
                    if moved and string.find(tostring(select(2, t.msg.last(3))), "easily", 1, true) then break end
                end
                local _, to = t.world.tile()
                t.check("hf-" .. ob, to.x ~= from.x or to.z ~= from.z, from.x .. "," .. from.z .. " -> " .. to.x .. "," .. to.z .. "; last messages: " .. tostring(select(2, t.msg.last(2))))
            end
            -- the barrier lets a player with the heart in the recess (stage >= 18) through (quest_legends.rs2:1813)
            for _ = 1, 4 do
                t.player.walk_to(2421, 4693, 80)
                local _, near = t.world.tile()
                if math.abs(near.x - 2421) <= 3 and math.abs(near.z - 4693) <= 3 then break end
            end
            t.exec("hf-forceBarrier", t.player.click_loc, "legendsquest_force_barrier", 1)
            settle_chat("hf-forceBarrier-settle")
            do
                local _, at = t.world.tile()
                t.check("hf-forceBarrier-through", at.z <= 4690, "through the force barrier to " .. at.x .. "," .. at.z)
            end
            -- The three heroes' skeletons (ranalph_devere.rs2:19 ai_opplayer) respawn behind the barrier and
            -- keep the player engaged at the barrier line (checkpoint 7 was refused with "Ranalph Devere is
            -- attacking", and Attack answers "I can't reach that!" across it): walk on into the south room,
            -- out of their reach, and let the engagement lapse before the boundary
            t.player.walk_to(2396, 4679, 60)
            t.ticks(12)
            do
                local _, at = t.world.tile()
                t.check("hf-southRoom", at.x <= 2400 and at.z <= 4685, "in the south room beside the boulders at " .. at.x .. "," .. at.z)
            end

            ---------------------------------------------------------------- the quiet point
            local _, stage = t.var.server("varp139_legendsquest")
            local _, at = t.world.tile()
            t.check("leg.7.end", t.chat.kind() == "none",
                "tile " .. at.x .. "," .. at.z .. " level " .. (at.level or 0) .. ", legendsquest=" .. tostring(stage)
                .. " read from the server; holy force in the backpack, in the south room beside the boulders (Echned not yet summoned)")
            -- LEG 7 END
        end },
        { name = "nezikchened_at_the_source", run = function(t)
            -- LEG 8 BEGIN: pushBoulderWithForce
            local function settle_chat(name)
                for _ = 1, 8 do
                    local came = t.await({ level = function() return t.chat.kind() ~= "none" end, note = "page" }, 6)
                    if came ~= "ok" then return true end
                    local _, kind = t.chat.drain({ stop_at = "options", max_pages = 20 })
                    if kind == "options" then
                        t.step(name .. "-options", "FAIL", "an options page is still open")
                        return false
                    end
                end
                return true
            end

            ---------------------------------------------------------------- the pack for the fight
            -- Quest Helper (pushBoulderWithForce): combat gear, food and potions; the character wears full
            -- rune from setup, sharks were topped up to nine in leg 7, two prayer restores are carried
            do
                local _, sharks = t.inv.count("shark")
                local _, force = t.inv.count("holyforce")
                local _, bowl = t.inv.count("goldbowlbless_empty")
                t.check("leg.8.pack", force == 1 and bowl == 1 and sharks >= 5,
                    "holy force " .. force .. ", empty blessed golden bowl " .. bowl .. ", sharks " .. sharks)
            end

            ---------------------------------------------------------------- pushBoulderWithForce
            -- legends_boulder.rs2:6 below stage 22 summons Echned and opens echned_dialogue itself;
            -- echned_zekin.rs2:75 asks for the dagger, the Holy Force answers by inventory op instead
            -- ANY-OF: pushBoulderAgain pushBoulderWithForce the boulder press summons Echned for the holy force route: OSRS-Content/osrs239-content/server/scripts/quests/quest_legends/scripts/legends_boulder.rs2:6
            -- ANY-OF: giveDaggerToEchned castForce the holy force card stands in for the glowing dagger (long route): OSRS-Content/osrs239-content/server/scripts/quests/quest_legends/scripts/echned_zekin.rs2:39
            t.player.walk_to(2395, 4679, 60)
            t.exec("pushBoulderWithForce", t.player.press, "boulder_legends", 1, 10, { at = { 2392, 4678 } })
            do
                local wait = 12
                for _ = 1, 6 do
                    if t.await({ level = function() return t.chat.kind() ~= "none" end, note = "echned page" }, wait) ~= "ok" then break end
                    local _, kind = t.chat.drain({ stop_at = "options", max_pages = 20 })
                    if kind == "options" then
                        t.chat.close()
                        t.ticks(2)
                        break
                    end
                    wait = 6
                end
            end
            settle_chat("pushBoulderWithForce-settle")
            t.check("pushBoulderWithForce-echned", t.npc.nearest("echned_zekin", 8) == "ok",
                "Echned Zekin stands in the mist, chat " .. tostring(t.chat.kind()))

            ---------------------------------------------------------------- castForce (giveDaggerToEchned)
            -- echned_zekin.rs2:39 opheld1 holyforce -> legends_use_holy_force: the spirit is exposed as Nezikchened
            t.exec("castForce", t.player.inv_op, "holyforce", 1)
            settle_chat("castForce-settle")
            t.exec("castForce-nezikchened", t.npc.await_present, "nezikchened", 10, 20)

            ---------------------------------------------------------------- fightNezikchenedAtSource
            t.exec("fightNezikchenedAtSource", t.player.attack, "nezikchened", 2, 30)
            do
                local _, sharks_before = t.inv.count("shark")
                local _, detail = t.exec("fightNezikchenedAtSource-dead", t.npc.await_dead_engaged, 500, 60,
                    { eat = { item = "shark", below = 50 } })
                local lowest = tonumber(tostring(detail):match("lowest hp (%d+)/"))
                local _, sharks_left = t.inv.count("shark")
                t.check("fightNezikchenedAtSource-margin", lowest ~= nil and lowest >= 25 and (sharks_left or 0) >= 1,
                    "lowest hp " .. tostring(lowest) .. "/99, sharks " .. tostring(sharks_before) .. " -> " .. tostring(sharks_left)
                    .. " (margin: lowest hp >= 25 AND sharks left >= 1)")
            end
            settle_chat("fightNezikchenedAtSource-settle")
            t.ticks(1)
            t.expect("quest.stage.defeated_nezikchened_water", t.quest.expect_stage(22))

            ---------------------------------------------------------------- pushBoulderAfterFight
            t.player.walk_to(2395, 4679, 60)
            t.exec("pushBoulderAfterFight", t.player.press, "boulder_legends", 1, 10, { at = { 2392, 4678 } })
            settle_chat("pushBoulderAfterFight-settle")
            t.exec("pushBoulderAfterFight-pool", t.world.loc_near, "lgwaterpool", 8)

            ---------------------------------------------------------------- useBowlOnSacredWater
            t.exec("useBowlOnSacredWater", t.player.use_on, "goldbowlbless_empty", t.player.by_symbol("loc", "lgwaterpool"))
            settle_chat("useBowlOnSacredWater-settle")
            t.exec("useBowlOnSacredWater-bowl", t.inv.await, "goldbowlbless_pure", 1, 10)

            ---------------------------------------------------------------- the quiet point
            t.ticks(12)
            local _, stage = t.var.server("varp139_legendsquest")
            local _, at = t.world.tile()
            local _, pure = t.inv.count("goldbowlbless_pure")
            t.check("leg.8.end", t.chat.kind() == "none" and pure == 1,
                "tile " .. at.x .. "," .. at.z .. " level " .. (at.level or 0) .. ", legendsquest=" .. tostring(stage)
                .. " read from the server; the blessed golden bowl full of sacred water (goldbowlbless_pure x" .. pure .. ") carried")
            -- LEG 8 END
        end },
        { name = "yommi_totem", run = function(t)
            -- LEG 9 BEGIN: returnToSurface
            local function settle_chat(name)
                for _ = 1, 8 do
                    local came = t.await({ level = function() return t.chat.kind() ~= "none" end, note = "page" }, 6)
                    if came ~= "ok" then return true end
                    local _, kind = t.chat.drain({ stop_at = "options", max_pages = 20 })
                    if kind == "options" then
                        t.step(name .. "-options", "FAIL", "an options page is still open")
                        return false
                    end
                end
                return true
            end
            -- the yommi tree stands on the soil copy at 2778,2916 (the guide lists 2779,2917)
            local function stands(symbol)
                local result, row = t.world.loc_near(symbol, 8)
                return result == "ok" and row.tile_x == 2778 and row.tile_z == 2916
            end
            local function underground()
                local _, at = t.world.tile()
                return at.z > 9000
            end

            ---------------------------------------------------------------- the pack
            -- Quest Helper (enterJungleToPlant): radimus notes, a rune axe, a machete, the blessed golden
            -- bowl, the yommi seeds, combat gear and food -- all carried from earlier legs
            do
                local _, axe = t.inv.count("rune_axe")
                local _, machete = t.inv.count("machette")
                local _, seeds = t.inv.count("yommiseeds_germ")
                local _, bowl = t.inv.count("goldbowlbless_pure")
                local _, sharks = t.inv.count("shark")
                t.check("leg.9.pack", axe == 1 and machete == 1 and seeds >= 1 and bowl == 1 and sharks >= 1,
                    "rune axe " .. axe .. ", machete " .. machete .. ", germinated seeds " .. seeds
                    .. ", golden bowl of pure sacred water " .. bowl .. ", sharks " .. sharks)
            end

            ---------------------------------------------------------------- returnToSurface
            -- guide: "Teleporting out will evaporate the water" -- the teleport is plain travel (the
            -- content has no teleport hook: only cutting jungle with a full bowl, jungle_tree.rs2:58, drains
            -- it), so the water is poured out by hand, the bowl's own Empty op (quest_legends.rs2:1148),
            -- which is what the guide's step lists: the bowl is empty when the reeds are used
            t.exec("returnToSurface", t.player.goto_tile, 2836, 2914, 0)
            t.check("returnToSurface-surface", not underground(), "back on the surface")
            t.exec("returnToSurface-empty", t.player.inv_op, "goldbowlbless_pure", 1)
            t.exec("returnToSurface-bowl", t.inv.await, "goldbowlbless_empty", 1, 10)

            ---------------------------------------------------------------- enterJungleToPlant
            -- travel into the Kharazi jungle beside the pool; the fights the guide warns of are the animals
            t.exec("enterJungleToPlant", t.player.goto_tile, 2834, 2916, 0)
            do
                local _, at = t.world.tile()
                t.check("enterJungleToPlant-tile", at.x >= 2830 and at.x <= 2840,
                    "in the Kharazi jungle beside the pool at " .. at.x .. "," .. at.z)
            end

            ---------------------------------------------------------------- useMacheteOnReedsEnd
            local reeds = t.player.by_symbol("loc", "tall_reeds")
            t.exec("useMacheteOnReedsEnd", t.player.use_on, "machette", reeds, { at = { 2836, 2916 } })
            t.exec("useMacheteOnReedsEnd-reed", t.inv.await, "reed_hollow", 2, 10)

            ---------------------------------------------------------------- useReedOnPoolEnd
            -- quest_legends.rs2:1020: stage 25 is past the dried-up window, the empty blessed bowl is filled
            local pool = t.player.by_symbol("loc", "sacred_water")
            t.exec("useReedOnPoolEnd", t.player.use_on, "reed_hollow", pool, { at = { 2837, 2915 } })
            settle_chat("useReedOnPoolEnd-settle")
            t.exec("useReedOnPoolEnd-bowl", t.inv.await, "goldbowlbless_pure", 1, 10)

            ---------------------------------------------------------------- plantSeed
            -- legends_yommi.rs2:5: a germinated seed on the fertile soil; Herblore 45 rolls stat_random(40, 243)
            -- (about one in two), a failed roll costs the seed, the pack carries three
            t.exec("goto-plantSeed", t.player.goto_tile, 2780, 2916, 0)
            local soil = t.player.by_symbol("loc", "fertilesoil")
            local planted = false
            for i = 1, 3 do
                t.exec("plantSeed-" .. i, t.player.use_on, "yommiseeds_germ", soil, { at = { 2778, 2916 } })
                settle_chat("plantSeed-settle-" .. i)
                if t.await({ level = function()
                    return stands("yommitree_sapling") or stands("yommitree_baby")
                end, note = "a yommi tree grows" }, 12) == "ok" then planted = true break end
            end
            do
                local _, seeds = t.inv.count("yommiseeds_germ")
                t.check("plantSeed", planted, "the yommi seed grew on the fertile soil, germinated seeds left " .. tostring(seeds))
            end
            t.exec("plantSeed-sapling", t.await, { level = function() return stands("yommitree_sapling") end,
                note = "the sapling stands" }, 30)

            ---------------------------------------------------------------- useWaterOnTree
            t.exec("useWaterOnTree", t.player.use_on, "goldbowlbless_pure", t.player.by_symbol("loc", "yommitree_sapling"), { at = { 2778, 2916 } })
            settle_chat("useWaterOnTree-settle")
            t.exec("useWaterOnTree-bowl", t.inv.await, "goldbowlbless_empty", 1, 10)
            t.exec("useWaterOnTree-adult", t.await, { level = function() return stands("yommitree_adult") end,
                note = "the adult yommi tree stands" }, 20)

            ---------------------------------------------------------------- useAxe
            t.exec("useAxe", t.player.use_on, "rune_axe", t.player.by_symbol("loc", "yommitree_adult"), { at = { 2778, 2916 } })
            settle_chat("useAxe-settle")
            t.exec("useAxe-felled", t.await, { level = function() return stands("yommitree_felled") end,
                note = "the yommi tree lies felled" }, 20)

            ---------------------------------------------------------------- useAxeAgain
            t.exec("useAxeAgain", t.player.use_on, "rune_axe", t.player.by_symbol("loc", "yommitree_felled"), { at = { 2778, 2916 } })
            settle_chat("useAxeAgain-settle")
            t.exec("useAxeAgain-trimmed", t.await, { level = function() return stands("yommitree_trimmed") end,
                note = "the yommi trunk is trimmed" }, 20)

            ---------------------------------------------------------------- craftTree
            t.exec("craftTree", t.player.use_on, "rune_axe", t.player.by_symbol("loc", "yommitree_trimmed"), { at = { 2778, 2916 } })
            settle_chat("craftTree-settle")
            t.exec("craftTree-totem", t.await, { level = function() return stands("yommitree_totem") end,
                note = "the totem pole is carved" }, 20)

            ---------------------------------------------------------------- pickUpTotem
            -- legends_yommi.rs2:148: stage 25 -> 30 (collected_totem), the totem pole joins the backpack
            t.exec("pickUpTotem", t.player.click_loc, "yommitree_totem", 1, { at = { 2778, 2916 } })
            settle_chat("pickUpTotem-settle")
            t.exec("pickUpTotem-totem", t.inv.await, "thtotempole", 1, 10)
            t.ticks(1)
            t.expect("quest.stage.collected_totem", t.quest.expect_stage(30))

            ---------------------------------------------------------------- the quiet point
            -- the jungle wolves the guide warns of ("be prepared for some fights") engage the character while it
            -- works (a checkpoint refuses "attacking Jungle Wolf"): finish any that is still fighting
            for i = 1, 4 do
                if t.npc.nearest("jungle_wolf", 8) ~= "ok" then break end
                t.exec("leg.9.wolf-attack-" .. i, t.player.attack, "jungle_wolf", 2, 20)
                t.exec("leg.9.wolf-dead-" .. i, t.npc.await_dead_engaged, 120, 6, { eat = { item = "shark", below = 50 } })
            end
            t.ticks(6)
            do
                local _, stage = t.var.server("varp139_legendsquest")
                local _, at = t.world.tile()
                local _, totem = t.inv.count("thtotempole")
                t.check("leg.9.end", t.chat.kind() == "none" and totem == 1,
                    "tile " .. at.x .. "," .. at.z .. " level " .. (at.level or 0) .. ", legendsquest=" .. tostring(stage)
                    .. " read from the server; the yommi totem pole (thtotempole x" .. totem .. ") carried")
            end
            -- LEG 9 END
        end },
        { name = "totems_and_the_guild", run = function(t)
            -- LEG 10 BEGIN: useTotemOnTotem
            local function settle_chat(name)
                for _ = 1, 8 do
                    local came = t.await({ level = function() return t.chat.kind() ~= "none" end, note = "page" }, 6)
                    if came ~= "ok" then return true end
                    local _, kind = t.chat.drain({ stop_at = "options", max_pages = 20 })
                    if kind == "options" then
                        t.step(name .. "-options", "FAIL", "an options page is still open")
                        return false
                    end
                end
                return true
            end
            local function prayer_level()
                local _, reading = t.skill.read("prayer")
                return type(reading) == "table" and reading.level or 0
            end

            -- Prayer does not regenerate: every point the final fight spends comes from a prayer potion.
            -- Prayer 60 (setup) is 60 points; a dose restores 7 + 60/4 = 22. Protect from Melee drains
            -- one point per 5 ticks at +0 prayer bonus (full rune), so a fight of T ticks costs T/5.
            local potions = { "1doseprayerrestore", "2doseprayerrestore", "3doseprayerrestore", "4doseprayerrestore" }
            local function doses_left()
                local doses = 0
                for size, pot in ipairs(potions) do
                    local got, n = t.inv.count(pot)
                    if got == "ok" and type(n) == "number" then doses = doses + size * n end
                end
                return doses
            end
            -- drink the smallest potion first until the points reach `want`; the row asserts they rose
            local function drink_to(step, want)
                local before = prayer_level()
                local drank = 0
                for _ = 1, 4 do
                    if prayer_level() >= want then break end
                    local pot
                    for _, p in ipairs(potions) do
                        local got, n = t.inv.count(p)
                        if got == "ok" and type(n) == "number" and n > 0 then pot = p break end
                    end
                    if not pot then break end
                    t.player.inv_op(pot, 1)
                    t.ticks(3)
                    drank = drank + 1
                end
                local after = prayer_level()
                t.check(step, (drank > 0 and after > before and after >= want) or (drank == 0 and before >= want),
                    "prayer " .. before .. " -> " .. after .. " after " .. drank .. " dose(s) (want >= " .. want
                    .. "), doses left " .. doses_left())
            end
            local function melee_varbit()
                local _, on = t.var.varbit("varb4118_prayer_protectfrommelee")
                return on
            end
            -- Protect from Melee on if a dry prayer book switched it off; the row asserts it is on
            local function protect_on(step)
                if melee_varbit() ~= 1 then
                    t.ui.tab("prayer")
                    t.ticks(1)
                    local widget_result, widget = t.ui.widget("prayerbook:prayer15")
                    if widget_result == "ok" then t.ui.invoke(widget, 1) end
                    t.ticks(2)
                end
                local on = melee_varbit()
                t.check(step, on == 1, "prayer_protectfrommelee varbit " .. tostring(on) .. ", prayer " .. prayer_level())
            end

            ---------------------------------------------------------------- the pack
            -- Quest Helper (useTotemOnTotem): the Yommi totem, combat gear, food and potions. The
            -- character wears full rune (setup); food is topped up here, four hero fights follow.
            -- The spent lockpicks, swamp rocks and pickaxe of legs 2-7 fill the backpack first: with them
            -- carried the sharks below take the last free slots and the two prayer potions never land
            -- (leg.10.pack read sharks 11 and only leg 4's two potions, 2026-10-03).
            for _, junk in ipairs({ "lockpick", "swamprocks1", "swamprocks2", "swamprocks3", "rune_pickaxe" }) do
                for _ = 1, 4 do
                    local got, remaining = t.inv.count(junk)
                    if got ~= "ok" or remaining == 0 then break end
                    t.player.drop(junk)
                    t.ticks(1)
                end
            end
            t.cheat("::give shark 12")
            t.ticks(1)
            t.cheat("::give 4doseprayerrestore 2")
            t.ticks(1)
            do
                local _, totem = t.inv.count("thtotempole")
                local _, sharks = t.inv.count("shark")
                local _, pots = t.inv.count("4doseprayerrestore")
                t.check("leg.10.pack", totem == 1 and sharks >= 10,
                    "yommi totem pole " .. totem .. ", sharks " .. sharks .. ", 4-dose prayer restores " .. pots
                    .. ", prayer " .. prayer_level())
            end
            -- the plan below drinks about 8 doses from an empty book (3 to fill, one before Irvig, one
            -- before Ranalph, 3 after Nezikchened's arrival drain): carry at least 10
            do
                local doses = doses_left()
                t.check("leg.10.pack-prayer", doses >= 10, "prayer potion doses " .. doses .. " (need >= 10), prayer "
                    .. prayer_level() .. " of 60")
            end

            ---------------------------------------------------------------- Protect from Melee
            -- the guide: "Put Protect from Melee on" -- the prayer book's own button (prayerbook:prayer15).
            -- Fill the book first: it arrives empty (leg 4's fire chamber drains 90%, nothing regenerates)
            drink_to("useTotemOnTotem-drink", 50)
            do
                local tab_result, tab_detail = t.ui.tab("prayer")
                t.check("useTotemOnTotem-prayertab", tab_result == "ok", "prayer tab -> " .. tostring(tab_result) .. " " .. tostring(tab_detail))
                t.ticks(1) -- verbs-combat.md: the widget resolves a tick after the tab switch (the drink above left the inventory open)
            end
            do
                local widget_result, widget = t.ui.widget("prayerbook:prayer15")
                t.check("useTotemOnTotem-prayerwidget", widget_result == "ok", "prayerbook:prayer15 -> " .. tostring(widget_result) .. " " .. tostring(widget))
                t.ui.invoke(widget, 1)
                t.ticks(2)
                local _, on = t.var.varbit("varb4118_prayer_protectfrommelee")
                t.check("useTotemOnTotem-protect", on == 1, "prayer_protectfrommelee varbit " .. tostring(on))
            end

            ---------------------------------------------------------------- useTotemOnTotem
            -- quest_legends.rs2:1835 oplocu lg_ord_totem_pole: below stage 35 the pole conjures the demon's
            -- heroes (nezikchened.rs2 summon_nezi_part3): San, Irvig and Ranalph, then Nezikchened himself
            t.exec("goto-useTotemOnTotem", t.player.goto_tile, 2850, 2917, 0)
            local pole = t.player.by_symbol("loc", "lg_ord_totem_pole")
            t.exec("useTotemOnTotem", t.player.use_on, "thtotempole", pole, { at = { 2852, 2917 } })
            settle_chat("useTotemOnTotem-settle")

            -- `want`: drink up to it once the fighter is present (Nezikchened's arrival takes 75% of the
            -- current Prayer, nezikchened.rs2:189, so his top-up waits for him); `need`: the points the
            -- fight costs, from its length on the v3 run of 2026-10-03 (San 121, Irvig 113, Ranalph 147,
            -- Nezikchened 269 ticks) at one point per 5 ticks
            local function hero(step, symbol, want, need)
                t.exec(step .. "-present", t.npc.await_present, symbol, 12, 40)
                drink_to(step .. "-drink", want)
                protect_on(step .. "-protect")
                t.check(step .. "-prayer", prayer_level() >= need, "prayer " .. prayer_level() .. " (need >= " .. need
                    .. " for the fight), doses left " .. doses_left())
                local _, sharks_before = t.inv.count("shark")
                t.exec(step, t.player.attack, symbol, 2, 30)
                local _, detail = t.exec(step .. "-dead", t.npc.await_dead_engaged, 400, 30, { eat = { item = "shark", below = 50 } })
                local lowest = tonumber(tostring(detail):match("lowest hp (%d+)/"))
                local _, sharks_left = t.inv.count("shark")
                t.check(step .. "-margin", lowest ~= nil and lowest >= 25 and (sharks_left or 0) >= 1,
                    "lowest hp " .. tostring(lowest) .. "/99, sharks " .. tostring(sharks_before) .. " -> " .. tostring(sharks_left)
                    .. ", prayer after " .. prayer_level() .. " (margin: lowest hp >= 25 AND sharks left >= 1)")
                settle_chat(step .. "-settle")
            end
            hero("killSan", "san_tojalon", 45, 30)
            hero("killIrvig", "irvig_senay", 45, 30)
            hero("killRanalph", "ranalph_devere", 45, 35)
            hero("defeatDemon", "nezikchened", 55, 55)
            t.ticks(2)
            t.expect("quest.stage.defeated_nezikchened_final", t.quest.expect_stage(35))
            -- the rest of the leg is talk and travel: Protect from Melee off
            do
                if melee_varbit() == 1 then
                    t.ui.tab("prayer")
                    t.ticks(1)
                    local widget_result, widget = t.ui.widget("prayerbook:prayer15")
                    if widget_result == "ok" then t.ui.invoke(widget, 1) end
                    t.ticks(2)
                end
                local on = melee_varbit()
                t.check("defeatDemon-protectOff", on == 0, "prayer_protectfrommelee varbit " .. tostring(on) .. ", prayer " .. prayer_level()
                    .. ", doses left " .. doses_left())
            end

            ---------------------------------------------------------------- useTotemOnTotemAgain
            -- quest_legends.rs2:1817: stage 35 -> 40, the corrupted pole is replaced and Gujuo comes
            t.exec("useTotemOnTotemAgain", t.player.use_on, "thtotempole", t.player.by_symbol("loc", "lg_ord_totem_pole"), { at = { 2852, 2917 } })
            do
                local pages = {}
                for _ = 1, 30 do
                    if t.await({ level = function() return t.chat.kind() ~= "none" end, note = "page" }, 8) ~= "ok" then break end
                    local _, text = t.chat.text()
                    local _, head = t.chat.head()
                    pages[#pages + 1] = t.chat.kind() .. ":" .. tostring(head) .. "/" .. tostring(text)
                    t.chat.drain({ stop_at = "options", max_pages = 1 })
                end
                t.check("useTotemOnTotemAgain-pages", true, table.concat(pages, " | "))
            end
            t.ticks(2)
            -- the pages above are the whole scene: the pole is replaced (stage 40), Gujuo appears beside
            -- the player on his own (quest_legends.rs2:1820-1826 npc_add gujuo + opplayer2) and his stage-40
            -- talk (gujuo.rs2:113) hands over the gilded totem pole and moves the stage to 45
            t.exec("useTotemOnTotemAgain-gift", t.inv.await, "thtotempolegift", 1, 10)
            t.expect("quest.stage.got_gilded_totem", t.quest.expect_stage(45))
            -- ANY-OF: summonGujou useTotemOnTotemAgain the replaced pole itself brings Gujuo, no bullroarer swing is asked for: OSRS-Content/osrs239-content/server/scripts/quests/quest_legends/scripts/quest_legends.rs2:1820
            -- ANY-OF: talkToGujouForTotem useTotemOnTotemAgain Gujuo opens his own stage-40 talk and gives the gift, then leaves: OSRS-Content/osrs239-content/server/scripts/quests/quest_legends/scripts/gujuo.rs2:113

            ---------------------------------------------------------------- returnToRadimus
            -- the guild grounds stand behind the mithril gates (legends_gate.rs2): plain travel to the
            -- road, the gate is clicked open
            t.exec("goto-returnToRadimus", t.player.goto_tile, 2728, 3346, 0)
            t.exec("returnToRadimus-gate", t.player.click_loc, "legendsguildgatel", 1, { at = { 2728, 3349 } })
            t.ticks(3)
            do
                local walked = t.player.walk_to(2727, 3368, 40)
                local _, at = t.world.tile()
                t.check("returnToRadimus-inside", walked == "ok" and at.z >= 3360, "inside the grounds at " .. at.x .. "," .. at.z)
            end
            t.exec("returnToRadimus-door", t.player.click_loc, "poshdoor", 1, { at = { 2726, 3368 } })
            t.ticks(2)
            t.exec("returnToRadimus", t.player.talk_to, "radimus_erkle_hut")
            settle_chat("returnToRadimus-dialog")
            t.ticks(2)
            t.expect("quest.stage.returned_to_radimus", t.quest.expect_stage(50))

            ---------------------------------------------------------------- talkToRadimusInGuild
            -- legends_door.rs2:1: from stage 50 the guild's main doors open (walk in from the hut, the
            -- doors stand at 2728,3373 / 2729,3373)
            do
                local walked = t.player.walk_to(2728, 3371, 30)
                local _, at = t.world.tile()
                t.check("goto-talkToRadimusInGuild", walked == "ok", "at the hall doors " .. at.x .. "," .. at.z .. ", walk " .. tostring(walked))
            end
            t.exec("talkToRadimusInGuild-doors", t.player.click_loc, "legendsguilddoorl", 1, { at = { 2728, 3373 } })
            t.ticks(3)
            do
                local walked = t.player.walk_to(2729, 3380, 30)
                local _, at = t.world.tile()
                t.check("talkToRadimusInGuild-hall", walked == "ok" and at.z >= 3374,
                    "in the main hall at " .. at.x .. "," .. at.z .. ", walk " .. tostring(walked))
            end
            -- radimus_erkle.rs2:13: he greets, offers the training; each pick (label radimus_train, :143) adds
            -- 30,000 xp (stat_advance $skill, 300000 tenths) and 5 to the stage; four picks -> stage 70
            local xp_before_result, xp_before = t.skill.snapshot()
            t.check("talkToRadimusInGuild-snapshot", xp_before_result == "ok", "skill.snapshot before the hand-in -> " .. tostring(xp_before_result))
            t.exec("talkToRadimusInGuild", t.player.talk_to, "radimus_erkle_guild")
            local function to_options(name)
                t.await({ level = function() return t.chat.kind() ~= "none" end, note = "page" }, 12)
                local _, kind = t.chat.drain({ stop_at = "options", max_pages = 30 })
                if kind ~= "options" then
                    t.step(name, "FAIL", "wanted an options page, got " .. tostring(kind))
                    return false
                end
                return true
            end
            if to_options("talkToRadimusInGuild-welcome") then
                t.exec("talkToRadimusInGuild-train", t.chat.choose, "Yes, I'll train now.")
            end
            -- attack, defence, strength (menu 1), hitpoints (menu 2)
            local picks = { { 1 }, { 2 }, { 3 }, { 4, 1 } }
            local names = { "attack", "defence", "strength", "hitpoints" }
            for i, path in ipairs(picks) do
                for j, index in ipairs(path) do
                    if to_options("talkToRadimusInGuild-menu-" .. names[i] .. "-" .. j) then
                        t.exec("talkToRadimusInGuild-pick-" .. names[i] .. "-" .. j, t.chat.choose, index)
                    end
                end
                t.ticks(1)
            end
            -- the fourth pick reaches stage 70 and radimus_training (:92) queues legends_quest_complete
            -- (:95) in the same talk, so the guide's second talk (talkToRadimusInGuildAgain) has no scene left
            -- ANY-OF: talkToRadimusInGuildAgain talkToRadimusInGuild the fourth training pick already runs radimus_training's stage-70 branch and queues legends_quest_complete in the same talk: OSRS-Content/osrs239-content/server/scripts/quests/quest_legends/scripts/radimus_erkle.rs2:95
            t.exec("talkToRadimusInGuild-finish", t.chat.drain, { max_pages = 20 })
            t.ticks(4)

            ---------------------------------------------------------------- the rewards
            -- Quest Helper rewards: 4 quest points, 30,000 xp in each of four skills of the player's
            -- choice (120,000 in all), access to the Legends' Guild, the dragon square shield
            for _, skill in ipairs(names) do
                local gain_result, gain_detail = t.skill.expect_gain(skill, 30000, xp_before)
                t.check("reward." .. skill .. "_xp", gain_result, skill .. " gain 30,000 xp -> " .. tostring(gain_detail))
            end
            t.quest.expect_complete()
            -- LEG 10 END
            t.finish(0)
        end },
    },
}
