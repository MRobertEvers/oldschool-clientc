-- In Search of Knowledge. Spec: docs/quests/ladders/insearchofknowledge.notes.md, parity script isok.lua.
return {
    id = "insearchofknowledge",
    max_frames = 480000,
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        -- Combat staging, for the getPages grind only: the pages are a 1/10 tertiary off the Forthos red dragons
        -- (isok_page_drop, insearchofknowledge_locs.rs2:166), ~170-200 real kills, and the whole run must fit the
        -- 480000-frame (~16000 tick) ceiling. Measured (scratch probes iskprf/iskprg): a fang at 82/82 kills in
        -- 69-89 ticks, at 99/99 in 49-74, so Attack/Strength 99 is what fits the ceiling (b71 run 1: ~57-tick kill
        -- waits + ~17 ticks between kills). Prayer 90 (Protect from Melee needs 43): a fight costs ~9 points at +19
        -- prayer bonus, and 16 Prayer potions x 4 doses x (7 + 90/4) + 90 = 1946 points carry ~210 fights (b71
        -- run 2 drank 58 doses of 27 points over 210 kills); at Prayer 70 with 15 potions the prayer ran out after
        -- 136 kills (b71 run 1). Hitpoints 50 is a cushion should the prayer lapse. Defence is NOT staged
        -- (Protect from Melee blocks every dragon's melee; shield + antifire blocks red_dragon's breath). No ISoK script reads a stat or the combat level (grep of quest_insearchofknowledge: no stat( /
        -- combat level read), so none of this moves the quest itself.
        "::setlevel attack 99", "::setlevel strength 99", "::setlevel prayer 90", "::setlevel hitpoints 50",
        "::give osmumtens_fang 1", "::wield osmumtens_fang",
        -- dragonfire protection for red_dragon's breath (1 swing in 4, npc/scripts/dragon.rs2): the anti-dragon
        -- shield (dragon_fire: max 5) plus extended antifire (skill_combat/scripts/dragonfire.rs2
        -- dragonfire_maxhit: shield + antifire = 0), 1200 ticks a dose (antifire.constant), 3 potions = 14400
        -- ticks for a ~11000-13500 tick grind. Prayer gear (+19) slows the Protect from Melee drain.
        "::give antidragonbreathshield 1", "::wield antidragonbreathshield",
        "::give monkrobetop 1", "::wield monkrobetop", "::give monkrobebottom 1", "::wield monkrobebottom",
        "::give blessedstar 1", "::wield blessedstar",
        "::give lobster 5", "::give bread 1", "::give knife 1",
        "::give 4doseprayerrestore 16", "::give 4dose2antidragon 3", "::give shark 2",
        -- incidental aggressive npcs the quest never fights (owner ruling 2026-10-06); every red dragon kind
        -- (red_dragon, red_dragon2/3/4) is a getPages target and is NOT passive.
        "::passive babyreddragon", "::passive hosdun_druid", "::passive hosdun_spider",
        -- b71: no ::insearchofknowledge placement (it p_teleports onto the entrance ladder tile inside Great
        -- Kourend); Veos at Port Sarim sails to Kourend once X Marks the Spot is done (xmarksthespot.rs2:27).
        "::complete quest_xmarksthespot",
    },

    run = function(t)
        t.quest.bind({
            varp = "varb8403_hosdun_knowledge_search",
            constants = { not_started = 0, tomes = 1, logosia = 2, complete = 3 },
            row = "miniquest_insearchofknowledge",
            display = "In Search of Knowledge",
            points = 0,
        })
        t.ticks(4)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- Port Sarim -> Veos -> Port Piscarilius dock 1824,3690 -> overland (REACH 236) to the Forthos ladder.
        t.exec("goto-veosSarim", t.player.goto_tile, 3054, 3246, 0)
        t.exec("talkToVeos", t.player.talk_to, "veos_sarim", 1)
        t.exec("talkToVeos-menu", t.chat.drain, { stop_at = "options" })
        t.exec("talkToVeos-sail", t.chat.choose, "Can you take me to Great Kourend?")
        t.exec("talkToVeos-done", t.chat.drain, {})
        t.ticks(4)
        local _, dock = t.world.tile()
        t.check("talkToVeos-landed", dock ~= nil and dock.x >= 1800 and dock.x < 1850 and dock.z > 3600 and dock.z < 3750,
            "landed at the Piscarilius dock at " .. tostring(dock and (dock.x .. "," .. dock.z)))
        t.exec("goto-forthosLadder", t.player.goto_tile, 1703, 3575, 0)
        t.exec("enterDungeon", t.player.click_loc, "hosdun_entrance_ladder_hole", 1)
        t.ticks(4)
        local _, tile = t.world.tile()
        t.check("enterDungeon.landed", tile and tile.z > 9000, "tile " .. tostring(tile and tile.x) .. "," .. tostring(tile and tile.z))

        -- Forthos route: webs (bigweb_slashable) sit across the passage; cut each with the knife by click, then walk on
        local reached, cuts = false, 0
        local sr, sd = t.player.walk_to(1841, 9945, 60)
        local legs = { "first web: walk_to 1841,9945 -> " .. tostring(sr) .. " " .. tostring(sd) }
        for leg = 1, 8 do
            local wr = t.player.walk_to(1842, 9928, 60)
            local _, wt = t.world.tile()
            legs[#legs + 1] = "leg " .. leg .. ": walk_to 1842,9928 -> " .. tostring(wr) .. " now " .. tostring(wt and wt.x) .. "," .. tostring(wt and wt.z)
            if wr == "ok" then reached = true; break end
            for try = 1, 6 do
                if t.world.loc_near("bigweb_slashable", 12) ~= "ok" then break end
                cuts = cuts + 1
                t.exec("cutWeb" .. cuts, t.player.click_loc, "bigweb_slashable", 1)
                t.ticks(3)
            end
        end
        t.check("walkToAimeri.arrived", reached, "reached Aimeri's room after cutting " .. cuts .. " web click(s) :: " .. table.concat(legs, "; "))
        local aim = t.player.by_symbol("npc", "hosdun_aimeri_injured")
        t.exec("useFoodOnAimeri.bread", t.player.use_on, "bread", aim)
        t.check("useFoodOnAimeri.breadRefused", t.msg.expect("isn't suitable food"), "bread refused and kept")
        for i = 1, 5 do
            t.exec("useFoodOnAimeri" .. i, t.player.use_on, "lobster", aim)
            t.ticks(2)
        end
        t.expect("aimeri.fed", t.var.expect("varb8393_hosdun_aimeri_status", 5))

        t.exec("talkToAimeri", t.player.talk_to, "hosdun_aimeri_healed")
        t.exec("talkToAimeri.place", t.chat.play, {
            "npc:Thanks for helping me",
            "choose:What is this place?",
            "player:What is this place?",
            "npc:This place probably had a name",
            "npc:It is rumoured",
            "player:Who are Ralos and Ranul?",
            "npc:According to religious texts",
            "npc:If you find any artifacts",
            "player:I'll see what I find.",
            "npc:You need only to look up",
        })
        t.expect("quest.stage.deferred", t.quest.expect_stage("not_started"))
        t.exec("talkToAimeriAgain", t.player.talk_to, "hosdun_aimeri_healed")
        t.exec("talkToAimeriAgain.dialog", t.chat.play, {
            "npc:Thanks for helping me",
            "choose:Who are you?",
            "player:Who are you",
            "npc:My name is Brother Aimeri",
            "npc:I worship Ralos",
            "player:I'll keep my eyes open.",
            "npc:You have my gratitude.",
            "player:The greater good.",
        })
        t.expect("quest.stage.tomes", t.quest.expect_stage("tomes"))

        -- The bookcases stand beside the red dragons (red_dragon2 1815,9937, wander 10, huntrange 5; m28_155.spawn),
        -- which are no longer passive: Protect from Melee and an extended antifire go up before the first of them.
        local function count(sym)
            local _, n = t.inv.count(sym)
            return n or 0
        end
        -- one dose of the emptiest Prayer potion (1-dose first, so the vials free up in order)
        local function drink_prayer()
            for d = 1, 4 do
                local sym = d .. "doseprayerrestore"
                if count(sym) > 0 then
                    t.player.inv_op(sym, 1)
                    t.ticks(2)
                    return sym
                end
            end
            return nil
        end
        local function prayer_points()
            local pr, reading = t.prayer.points()
            return pr == "ok" and type(reading) == "table" and reading.points or nil
        end
        local pray_drinks, pray_low, fire_drinks, fire_low = 0, 99, 0, 99
        -- extended antifire: %varb3981_antifire_potion counts down in 30-tick bands (40 per dose, antifire.constant);
        -- re-dose with a minute left. Prayer: re-dose under 35 points (a fight costs ~9). Then light Protect from Melee.
        local function ready()
            local _, fire = t.var.server("varb3981_antifire_potion")
            if (fire or 0) <= 2 then
                for d = 1, 4 do
                    local sym = d .. "dose2antidragon"
                    if count(sym) > 0 then
                        t.player.inv_op(sym, 1)
                        t.ticks(2)
                        fire_drinks = fire_drinks + 1
                        break
                    end
                end
                _, fire = t.var.server("varb3981_antifire_potion")
            end
            if (fire or 0) < fire_low then fire_low = fire or 0 end
            for _ = 1, 2 do
                local points = prayer_points()
                if points == nil or points >= 35 then break end
                if not drink_prayer() then break end
                pray_drinks = pray_drinks + 1
            end
            t.prayer.set("protectfrommelee", true)
            local points = prayer_points()
            if points and points < pray_low then pray_low = points end
        end
        ready()
        local _, melee_on = t.var.server("varb4118_prayer_protectfrommelee")
        local _, fire_on = t.var.server("varb3981_antifire_potion")
        t.check("dragonProtection.up", melee_on == 1 and (fire_on or 0) > 0,
            "Protect from Melee " .. tostring(melee_on) .. ", antifire bands " .. tostring(fire_on) .. ", prayer " .. tostring(prayer_points())
                .. ", anti-dragon shield worn")

        t.exec("goto-temple", t.player.goto_tile, 1797, 9934, 0)
        t.exec("searchBookcasesForTemple", t.player.click_loc, "hosdun_temple_bookcase", 1)
        t.expect("temple.tome", t.inv.await("hosdun_temple_tome", 1, 10))
        t.exec("goto-sun", t.player.goto_tile, 1805, 9935, 0)
        t.exec("searchBookcasesForSun", t.player.click_loc, "hosdun_sun_bookcase", 1)
        t.expect("sun.tome", t.inv.await("hosdun_sun_tome", 1, 10))
        t.exec("goto-moon", t.player.goto_tile, 1804, 9942, 0)
        t.exec("searchBookcasesForMoon", t.player.click_loc, "hosdun_moon_bookcase", 1)
        t.expect("moon.tome", t.inv.await("hosdun_moon_tome", 1, 10))

        -- getPages: real kills; each red dragon rolls a 1/10 page (isok_page_drop, insearchofknowledge_locs.rs2:166).
        -- All four kinds fight back (no ::passive): melee max 14, blocked by Protect from Melee; red_dragon also
        -- breathes 1 swing in 4 (npc/scripts/dragon.rs2), zeroed by shield + antifire. Single-way combat refuses a
        -- swing at one dragon while another holds the claim, so each round tries the four kinds in turn.
        t.exec("goto-dragons", t.player.goto_tile, 1815, 9934, 0)
        t.ticks(7) -- an idle beat moves the run onto another roll stream
        local pages = {
            { page = "hosdun_sun_page", tome = "hosdun_sun_tome", var = "varb8399_hosdun_sun_pages", name = "sun" },
            { page = "hosdun_moon_page", tome = "hosdun_moon_tome", var = "varb8400_hosdun_moon_pages", name = "moon" },
            { page = "hosdun_temple_page", tome = "hosdun_temple_tome", var = "varb8401_hosdun_temple_pages", name = "temple" },
        }
        local targets = { "red_dragon", "red_dragon2", "red_dragon3", "red_dragon4" }
        local eat = { eat = { item = "shark", below = 30 } }
        local kills, misses, despawns, attackfails, lastmiss, lastattack = 0, 0, 0, 0, "", ""
        local picked, pickfails, lastpick = 0, 0, ""
        local hp_low, hp_base = nil, nil
        local function note_hp(low, base)
            low, base = tonumber(low), tonumber(base)
            if low and (hp_low == nil or low < hp_low) then hp_low = low end
            if base then hp_base = base end
        end
        local done = false
        for k = 1, 900 do
            if kills >= 400 then break end -- bounded: getPages.kills fails loudly below
            ready()
            local engaged = false
            for j = 0, #targets - 1 do
                local tgt = targets[((k + j) % #targets) + 1]
                local ra, rad = t.player.attack(tgt, 2, 10, eat)
                if ra == "ok" then engaged = true; break end
                lastattack = tgt .. " " .. tostring(ra) .. " " .. tostring(rad):sub(-160)
            end
            if engaged then
                local rd, dd = t.npc.await_dead_engaged(150, 8, eat)
                if rd == "ok" then
                    kills = kills + 1
                else
                    misses = misses + 1
                    if rd == "despawned" then despawns = despawns + 1 end
                    lastmiss = tostring(rd) .. " " .. tostring(dd):sub(1, 200)
                end
                local low, base = string.match(tostring(dd), "lowest hp (%d+)/(%d+)")
                note_hp(low, base)
            else
                attackfails = attackfails + 1
                t.ticks(2)
            end
            local hr, hp = t.skill.read("hitpoints")
            if hr == "ok" and type(hp) == "table" then note_hp(hp.level, hp.base_level) end
            done = true
            for _, p in ipairs(pages) do
                local _, ins = t.var.varbit(p.var)
                if ins and ins < 4 then
                    -- a page whose tome still wants one; a full tome's pages are left on the floor
                    if count(p.page) == 0 and t.world.obj_near(p.page, 14) == "ok" then
                        local pr, pd = t.player.click_obj(p.page, 3)
                        t.ticks(2)
                        if count(p.page) > 0 then
                            picked = picked + 1
                        else
                            pickfails = pickfails + 1 -- retried next round, the page stays on the floor
                            lastpick = p.page .. " " .. tostring(pr) .. " " .. tostring(pd):sub(1, 120)
                        end
                    end
                    if count(p.page) > 0 then
                        t.exec("insertPage." .. p.name .. kills, t.player.use_item_on_item, p.page, p.tome)
                        t.ticks(2)
                        _, ins = t.var.varbit(p.var)
                    end
                end
                if not ins or ins < 4 then done = false end
            end
            if done then break end
        end
        for _, p in ipairs(pages) do
            t.expect("getPages." .. p.name .. ".full", t.var.await(p.var, 4, 6))
        end
        t.check("getPages.kills", done and despawns == 0, "red dragon kills=" .. kills .. " waitmiss=" .. misses
            .. " despawned=" .. despawns .. " (" .. lastmiss .. ") attackfail=" .. attackfails .. " (" .. lastattack .. ")"
            .. "; pages picked up " .. picked .. ", pickup retries " .. pickfails .. " (" .. lastpick .. ")")
        -- Margin over the whole kill loop: the lowest hitpoints any kill wait's eater (or the per-round read) saw,
        -- against a quarter of the maximum, AND food left.
        local sharks_left = count("shark")
        local pray_left = 0
        for d = 1, 4 do pray_left = pray_left + d * count(d .. "doseprayerrestore") end
        t.check("getPages.margin", hp_low ~= nil and hp_base ~= nil and hp_low * 4 >= hp_base and sharks_left >= 1,
            "lowest hp " .. tostring(hp_low) .. "/" .. tostring(hp_base) .. " over " .. kills .. " kills, sharks left "
                .. sharks_left .. " of 2; Protect from Melee all fight long, lowest prayer at a round start " .. pray_low
                .. ", prayer doses drunk " .. pray_drinks .. " (" .. pray_left .. " left); extended antifire doses drunk "
                .. fire_drinks .. ", lowest antifire bands at a round start " .. fire_low .. " -- margin: lowest hp >= a"
                .. " quarter of max AND food left")

        -- back to the exit ladder ON FOOT: the webs cut on the way in respawn (general_use/scripts/web.rs2), so cut
        -- any that stand across the passage again, by click, the way the inbound leg does (b52 round 3: a goto here
        -- teleported past them)
        -- the way out runs back through Aimeri's room (probe build/quest_gate/isok_exit_probe3: 1816,9939 -> 1820,9930
        -- -> 1843,9926 on foot); from there north to the ladder the webs stand again
        for i, hop in ipairs({ { 1820, 9930 }, { 1843, 9926 } }) do
            local hr = t.player.walk_to(hop[1], hop[2], 60)
            local _, ht = t.world.tile()
            t.check("walkBackToAimeri" .. i, hr == "ok", "walk_to " .. hop[1] .. "," .. hop[2] .. " -> " .. tostring(hr) .. " now " .. tostring(ht and ht.x) .. "," .. tostring(ht and ht.z))
        end
        -- The two webs on the way out, in route order (maps/m28_155.jl2 bigweb_slashable): the pair across the passage
        -- north of Aimeri at 1841-1842,9933 (wall on the tile's north edge) and the one at 1833,9944 (south edge) on
        -- the way north to the ladder. A cut succeeds 1 in 2 and opens the web for 100 ticks (general_use/scripts/
        -- web.rs2, LostCity's), so each web is cut and then stepped through at once.
        local function cut_through(name, web_x, web_z, past_x, past_z)
            local note = ""
            for attempt = 1, 10 do
                local wr = t.player.walk_to(past_x, past_z, 15)
                local _, w = t.world.tile()
                note = note .. "[" .. attempt .. " walk " .. tostring(wr) .. " -> " .. tostring(w and w.x) .. "," .. tostring(w and w.z) .. "] "
                if wr == "ok" then
                    t.check(name, w ~= nil and w.x == past_x and w.z == past_z,
                        "through the web at " .. web_x .. "," .. web_z .. " to " .. past_x .. "," .. past_z .. " :: " .. note)
                    return true
                end
                t.exec(name .. "-cut" .. attempt, t.player.click_loc, "bigweb_slashable", 1, { at = { web_x, web_z } })
                t.ticks(3)
            end
            t.check(name, false, "never got through the web at " .. web_x .. "," .. web_z .. " :: " .. note)
            return false
        end
        cut_through("cutWebOut-aimeri", 1842, 9933, 1842, 9934)
        cut_through("cutWebOut-north", 1833, 9944, 1833, 9945)
        local lr = t.player.walk_to(1830, 9972, 60)
        local _, lt = t.world.tile()
        t.check("walkToExitLadder.arrived", lr == "ok", "walk_to 1830,9972 -> " .. tostring(lr) .. " now " .. tostring(lt and lt.x) .. "," .. tostring(lt and lt.z))
        t.exec("leaveDungeon", t.player.click_loc, "hosdun_entrance_ladder", 1)
        t.ticks(4)
        local _, ot = t.world.tile()
        t.check("leaveDungeon.surface", ot and ot.z < 9000, "tile " .. tostring(ot and ot.x) .. "," .. tostring(ot and ot.z))
        t.exec("protectFromMelee.off", t.prayer.set, "protectfrommelee", false)
        -- Arceuus Library: the east double door is the only way in (depthsofdespair.lua); overland to it, pressed, walked in.
        t.exec("goto-libraryEastDoor", t.player.goto_tile, 1643, 3805, 0)
        t.exec("enterLibrary.eastDoor", t.player.pass_door, { closed = "archeuus_door_double_right_green",
            open = "archeuus_door_double_right_green_open", at = { 1642, 3805, 0 }, near = { 1643, 3805 }, far = { 1641, 3805 } })
        t.exec("walk-logosia", t.player.walk_to, 1634, 3807, 30)
        local lib = t.player.by_symbol("npc", "arceuus_library_librarian")
        t.exec("useMoonOnLogosia", t.player.use_on, "hosdun_moon_tome", lib)
        t.expect("quest.stage.logosia", t.quest.expect_stage("logosia"))
        t.expect("tomes.handed", t.inv.expect_absent("hosdun_sun_tome"))

        local lamp_result, lamp_before = t.inv.count("thosf_reward_lamp")
        t.exec("talkToLogosia", t.player.talk_to, "arceuus_library_librarian")
        t.exec("talkToLogosia.dialog", t.chat.play, { "npc:Your contributions" })
        t.quest.expect_complete()
        local lamp_result2, lamp_after = t.inv.count("thosf_reward_lamp")
        t.check("reward.thosf_reward_lamp", lamp_result == "ok" and lamp_result2 == "ok" and lamp_after == lamp_before + 1,
            string.format("lamp %s -> %s (want +1)", tostring(lamp_before), tostring(lamp_after)))
        t.finish(0)
    end,
}
