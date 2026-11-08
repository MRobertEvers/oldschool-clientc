return {
    id = "icthlarin",
    fixture = "fresh_lumbridge.ini",
    max_frames = 180000,
    -- Follows RSProx 4481/4486/4487 (docs/rsprox-analysis/quests/icthlarins_little_helper.md): a cat
    -- out as a follower at the Wanderer's start, the hypnosis, the first memory (temple door, pit, the
    -- tile game), the Sphinx's riddle, the High Priest, the jar's memory (Apparition, the jar), the
    -- ceremony preparation, the third memory, the ceremony and the Possessed Priest, the fourth memory.
    setup = {
        "::clearinv",
        "::complete quest_gertrudescat",
        "::give growncatobject 1",
        "::setlevel agility 99",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        "::setlevel magic 99",
        "::setlevel prayer 99",
        "::setlevel ranged 99",
        "::setlevel thieving 99",
        "::setlevel woodcutting 99",
        "::setlevel firemaking 99",
        "::give rune_scimitar 1",
        "::wield rune_scimitar",
        "::give lobster 8",
        "::give tinderbox 1",
        "::give water_skin4 1",
        "::give bucket_empty 2",
        "::give knife 1",
        "::give coins 200",
        "::give willow_logs 1",
        "::icthlarinenergy",
    },
    run = function(t)
        t.quest.bind({
            varp = "varb418_ics_little_var",
            constants = { not_started = 0, need_supplies = 1, entered_city = 2, first_memory = 3, in_pyramid = 4, sphinx = 5,
                high_priest = 6, return_jar = 7, jar_entered = 8, jar_memory = 9, jar_guardian = 10, jar_killed = 11,
                jar_crossed = 12, place_jar = 13, jar_done = 14, embalm = 15, ritual = 16, place_symbol = 17, symbol_placed = 18,
                ceremony = 19, ceremony_scene = 21, possessed = 22, priest_dead = 23, meet_god = 24, finish_talk = 25, complete = 26 },
            row = "quest_icthlarinslittlehelper", display = "Icthlarin's Little Helper", points = 2,
        })
        t.ticklog.start()
        t.ticklog.mark("leg.icthlarin")
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        local function tile() local _, p = t.world.tile(); return p end
        local function stage() return select(2, t.var.server("varb418_ics_little_var")) end

        -- Dialogue: drain to each choice, answer it, and on to the end.
        local function dlg(name, picks)
            for i, pick in ipairs(picks or {}) do
                t.exec(name .. "-to-" .. i, t.chat.drain, { stop_at = "options" })
                t.exec(name .. "-pick-" .. i, t.chat.choose, pick)
            end
            t.exec(name .. "-end", t.chat.drain, { max_pages = 120 })
            t.ticks(2)
        end
        local function talk(name, npc, picks, op)
            t.exec(name, t.player.talk_to, npc, op or 1)
            dlg(name, picks)
        end
        local function goto_(name, x, z) t.exec("goto-" .. name, t.player.goto_tile, x, z, 0) end

        -- The copy: the player arrives on (dx, dz) inside it; the copy's own
        -- (0,0) is read off the landing tile.
        local bx, bz = 0, 0
        local function copy_base(dx, dz)
            local p = tile()
            t.check("copy." .. dx .. "," .. dz, p ~= nil and p.x > 6400, "tile " .. tostring(p and p.x) .. "," .. tostring(p and p.z))
            bx, bz = p.x - dx, p.z - dz
        end
        local function at(dx, dz) return { bx + dx, bz + dz } end
        local function goto_copy(name, dx, dz) t.exec("goto-" .. name, t.player.goto_tile, bx + dx, bz + dz, 0) end
        local function ix(sym) return select(2, t.inv.count(sym)) end

        local function jump_pit(name, sym, tx, tz, want_dz)
            for attempt = 1, 6 do
                local p0 = tile()
                t.exec(name .. "-" .. attempt, t.player.click_loc, sym, 2, { at = at(tx, tz) })
                t.ticks(14)
                local p = tile()
                if p and p.z - bz == want_dz then break end
                -- A fall (agility roll) lands in the chest room, 3311,9180: walk back in.
                if p and p.z - bz == 28 then
                    t.exec(name .. "-back-" .. attempt, t.player.goto_tile, p0.x, p0.z, 0)
                else
                    t.ticks(30)
                    t.cheat("::icthlarinenergy")
                end
            end
        end

        -- The tile game: 5x5, a press flips the tile's 3x3 block; solved = all 0.
        local function solve_tiles(name)
            local n, st = 25, {}
            for i = 1, n do st[i] = select(2, t.var.server("varb" .. (419 + i) .. "_ics_tile" .. i)) end
            local A = {}
            for i = 0, n - 1 do
                A[i + 1] = {}
                for j = 0, n - 1 do
                    local ri, ci, rj, cj = i // 5, i % 5, j // 5, j % 5
                    A[i + 1][j + 1] = (math.abs(ri - rj) <= 1 and math.abs(ci - cj) <= 1) and 1 or 0
                end
                A[i + 1][n + 1] = st[i + 1]
            end
            local row, piv = 1, {}
            for col = 1, n do
                local p
                for r = row, n do if A[r][col] == 1 then p = r break end end
                if p then
                    A[row], A[p] = A[p], A[row]
                    for r = 1, n do
                        if r ~= row and A[r][col] == 1 then
                            for c = col, n + 1 do A[r][c] = (A[r][c] + A[row][c]) % 2 end
                        end
                    end
                    piv[col] = row
                    row = row + 1
                end
            end
            local presses = {}
            for col = 1, n do
                if piv[col] and A[piv[col]][n + 1] == 1 then presses[#presses + 1] = col end
            end
            t.check(name .. ".solvable", #presses > 0, "presses " .. #presses)
            for _, j in ipairs(presses) do
                local _, w = t.ui.widget("icthalarins_tile_game:ics_" .. j)
                local a,b = t.ui.invoke(w, 1)
                t.ticks(1)
                t.note("press " .. j .. " " .. tostring(a) .. " " .. tostring(b) .. " checker " .. tostring(select(2, t.var.server("varb445_ics_tilechecker"))) .. " w " .. tostring(w))
            end
            t.ticks(4)
        end

        -- ---- The Wanderer (4486 [256..328]): a cat out, the supplies at hand.
        t.player.inv_op("growncatobject", 5)
        t.ticks(4)
        t.cheat("::icsjar")
        goto_("wanderer", 3315, 2850)
        do
            local r, d, pose = t.npc.pose("ics_little_multi_wanderer")
            t.note("wanderer pose: " .. tostring(r) .. " " .. tostring(d) .. " " .. tostring(pose))
        end
        talk("wanderer", "ics_little_multi_wanderer", { "Yes." })
        t.ticks(12)
        t.expect("quest.stage.first_memory", t.quest.expect_stage("first_memory"))
        t.check("hypnosis.jar", ix("ics_little_canopic_jar_liver") + ix("ics_little_canopic_jar_stomach") + ix("ics_little_canopic_jar_intestines") == 1, "a burial jar")
        t.check("hypnosis.supplies", ix("tinderbox") == 0, "tinderbox left " .. tostring(ix("tinderbox")))
        local p = tile()
        t.check("hypnosis.wake", p.x == 3294 and p.z == 2782, "woke at " .. p.x .. "," .. p.z)
        t.exec("wake-drain", t.chat.drain, { max_pages = 20 })

        -- ---- The Sphinx first (4487 [820..857]): no memory yet to tell her.
        goto_("sphinx-first", 3301, 2787)
        talk("sphinx-first", "ics_little_sphinx")
        t.check("sphinx.met", select(2, t.var.server("varb13882_ics_met_sphinx")) == 1, "ics_met_sphinx")

        -- ---- The first memory (4487 [887..1265]).
        goto_("temple-door", 3295, 2781)
        t.exec("temple-door", t.player.click_loc, "icthalarins_temple_door", 1)
        t.exec("temple-door-dlg", t.chat.drain, { max_pages = 20 })
        t.ticks(4)
        t.expect("quest.stage.in_pyramid", t.quest.expect_stage("in_pyramid"))
        copy_base(13, 19)
        goto_copy("pit-south", 28, 41)
        jump_pit("pit-1", "ics_little_pit_to", 28, 42, 45)
        goto_copy("west-door", 16, 48)
        t.exec("west-door", t.player.click_loc, "icthalarins_ancient_temple_door_1", 1, { at = at(16, 47) })
        t.exec("tile-open", t.ui.await_open, "icthalarins_tile_game", 20)
        solve_tiles("memory1")
        t.expect("quest.stage.sphinx", t.quest.expect_stage("sphinx"))
        t.exec("memory1-end", t.chat.drain, { max_pages = 20 })
        p = tile()
        t.check("memory1.street", p.x == 3294 and p.z == 2781, "at " .. p.x .. "," .. p.z)

        -- ---- The Sphinx's riddle (4487 [1589..1816]) and her menu.
        goto_("sphinx", 3301, 2787)
        talk("sphinx", "ics_little_sphinx", { "Okay, that sounds fair.", "9.", "Totally positive.", "I'll go speak with the High Priest." })
        t.expect("quest.stage.high_priest", t.quest.expect_stage("high_priest"))
        t.inv.expect_has("ics_little_sphinxstatue", 1)

        -- ---- The High Priest (4487 [2004..2217]).
        goto_("high-priest", 3283, 2772)
        talk("high-priest", "ics_little_hipriest_town", { "I'd better get going." })
        t.expect("quest.stage.return_jar", t.quest.expect_stage("return_jar"))

        -- ---- The jar's memory (4487 [2338..2767]).
        -- The npc-info of the town people the flow does not need: facing on a talk.
        goto_("jex", 3306, 2801)
        t.exec("jex", t.player.talk_to, "contact_jex", 1)
        t.chat.drain({ max_pages = 3 })
        goto_("siamun", 3315, 2787)
        t.exec("siamun", t.player.talk_to, "ics_little_linen2", 1)
        t.chat.drain({ max_pages = 6 })
        goto_("tarik", 3289, 2788)
        t.exec("tarik", t.player.talk_to, "ntk_tarik", 1)
        t.chat.drain({ max_pages = 60 })
        goto_("priest", 3283, 2771)
        t.exec("priest", t.player.talk_to, "ics_little_priest", 1)
        t.chat.drain({ max_pages = 8 })
        goto_("worker1", 3298, 2797)
        t.exec("worker1", t.player.talk_to, "ics_little_worker1", 1)
        t.chat.drain({ max_pages = 8 })
        goto_("worker2", 3296, 2794)
        t.exec("worker2", t.player.talk_to, "ics_little_worker2", 1)
        t.chat.drain({ max_pages = 8 })
        goto_("town-cat", 3298, 2758)
        t.exec("town-cat", t.player.talk_to, "ics_little_cat", 1)
        t.ticks(3)
        -- Capt' Arnav's chest, 4487 [1500..1541].
        t.cheat("::arnav")
        t.ticks(2)
        t.exec("arnav", t.player.talk_to, "macro_combilock_pirate", 1)
        t.exec("arnav-to-menu", t.chat.drain, { stop_at = "options", max_pages = 6 })
        t.exec("arnav-yes", t.chat.choose, "Yes, I'll help you unlock your chest.")
        t.chat.drain({ max_pages = 3 })
        t.exec("arnav-open", t.ui.await_open, "pirate_combilock", 10)
        t.cheat("::arnavsolve")
        t.ticks(1)
        t.ui.invoke((26 << 16) | 5, 1)
        t.ticks(1)
        t.cheat("::arnavsolve")
        t.ticks(1)
        t.ui.invoke((26 << 16) | 25, 1)
        t.ticks(2)
        t.exec("arnav-done", t.chat.drain, { max_pages = 3 })
        t.ticks(5)
        goto_("temple-door-2", 3295, 2781)
        t.exec("temple-door-2", t.player.click_loc, "icthalarins_temple_door", 1)
        t.exec("temple-door-2-dlg", t.chat.drain, { max_pages = 20 })
        t.ticks(4)
        t.expect("quest.stage.jar_entered", t.quest.expect_stage("jar_entered"))
        copy_base(13, 19)
        goto_copy("pit-south-2", 28, 41)
        t.exec("pit-2", t.player.click_loc, "ics_little_pit_to", 2, { at = at(28, 42) })
        t.exec("pit-2-dlg", t.chat.drain, { max_pages = 20 })
        t.ticks(4)
        t.expect("quest.stage.jar_memory", t.quest.expect_stage("jar_memory"))
        copy_base(16, 48)
        local _, JAR = t.var.server("varb397_ics_little_jar_multi")
        local POT = { [1] = { "ics_little_pot_liver", 22, 42, "ics_little_het", "ics_little_canopic_jar_liver", "4dose1defense" },
                      [2] = { "ics_little_pot_stomach", 22, 44, "ics_little_scabaras", "ics_little_canopic_jar_stomach", "4dose1agility" },
                      [3] = { "ics_little_pot_intestines", 22, 41, "ics_little_apmeken", "ics_little_canopic_jar_intestines", "4dose1attack" },
                      [4] = { "ics_little_pot_lungs", 22, 43, "ics_little_crondis", "ics_little_canopic_jar_lungs", "4dose1magic" } }
        local pot = POT[JAR]
        t.exec("west-door-2", t.player.click_loc, "icthalarins_ancient_temple_door_1", 1, { at = at(16, 47) })
        t.ticks(3)
        p = tile()
        t.check("jar.door", p.z - bz == 46, "at z " .. (p.z - bz))
        goto_copy("pot", pot[2] - 1, pot[3])
        t.player.click_loc(pot[1], 1, { at = at(pot[2], pot[3]) })
        t.ticks(3)
        t.expect("quest.stage.jar_guardian", t.quest.expect_stage("jar_guardian"))
        for i = 1, 40 do
            if stage() >= 11 then break end
            t.exec("kill-apparition-" .. i, t.player.attack, pot[4], 2, 30)
            t.ticks(4)
        end
        t.ticks(6)
        t.expect("quest.stage.jar_killed", t.quest.expect_stage("jar_killed"))
        t.player.click_loc(pot[1], 1, { at = at(pot[2], pot[3]) })
        t.ticks(4)
        t.exec("pot-take-dlg", t.chat.drain, { max_pages = 5 })
        t.inv.expect_has(pot[5], 1)
        goto_copy("door-inside", 16, 46)
        t.player.click_loc("icthalarins_ancient_temple_door_1", 1, { at = at(16, 47) })
        t.ticks(3)
        goto_copy("pit-north", 28, 45)
        t.exec("pit-from-end", t.player.click_loc, "ics_little_pit_from", 2, { at = at(28, 44) })
        t.ticks(4)
        t.exec("memory2-end", t.chat.drain, { max_pages = 20 })
        t.ticks(2)
        t.expect("quest.stage.jar_crossed", t.quest.expect_stage("jar_crossed"))
        copy_base(28, 40)
        -- Stand where the recorder was hunted (4487 [933], [940]): mummy swings on the tick log.
        goto_copy("mummy-stand-1", 44, 20)
        t.ticks(14)
        goto_copy("mummy-stand-2", 39, 33)
        t.ticks(14)
        -- 4487 [968], [973], [1045]: mummies 1, 2 and 4.
        goto_copy("mummy-stand-3", 14, 24)
        t.ticks(14)
        goto_copy("mummy-stand-4", 13, 34)
        t.ticks(14)
        goto_copy("mummy-stand-5", 21, 36)
        t.ticks(14)
        goto_copy("pit-south-3", 28, 41)
        jump_pit("pit-3", "ics_little_pit_to", 28, 42, 45)
        goto_copy("west-door-3", 16, 48)
        t.exec("west-door-3", t.player.click_loc, "icthalarins_ancient_temple_door_1", 1, { at = at(16, 47) })
        t.exec("tile-open-2", t.ui.await_open, "icthalarins_tile_game", 20)
        solve_tiles("memory2")
        t.expect("quest.stage.place_jar", t.quest.expect_stage("place_jar"))
        t.ticks(3)
        t.exec("return-jar", t.player.inv_op, pot[5], 5)
        t.exec("return-jar-dlg", t.chat.drain, { max_pages = 3 })
        t.expect("quest.stage.jar_done", t.quest.expect_stage("jar_done"))
        goto_copy("door-inside-2", 16, 46)
        t.player.click_loc("icthalarins_ancient_temple_door_1", 1, { at = at(16, 47) })
        t.ticks(3)
        goto_copy("pit-north-2", 28, 45)
        jump_pit("pit-out", "ics_little_pit_from", 28, 44, 41)
        goto_copy("ladder", 13, 21)
        t.exec("ladder", t.player.click_loc, "ics_ladder", 1)
        t.ticks(5)

        -- ---- The High Priest's ceremony, the Embalmer, the Carpenter (4487 [3051..3345]).
        goto_("high-priest-2", 3283, 2772)
        talk("high-priest-2", "ics_little_hipriest_town", { "I'd better get going." })
        t.expect("quest.stage.embalm", t.quest.expect_stage("embalm"))
        goto_("salt-lake", 3287, 2842)
        t.exec("salt-lake", t.player.click_loc, "icthalarins_waters_edge", 1)
        t.exec("salt-lake-dlg", t.chat.drain, { max_pages = 3 })
        t.inv.expect_has("ics_little_saltwaterbucket", 1)
        goto_("suntrap", 3305, 2757)
        t.exec("suntrap", t.player.use_on, "ics_little_saltwaterbucket", t.player.by_symbol("loc", "icthalarins_suntrap_centre"))
        t.exec("suntrap-dlg", t.chat.drain, { max_pages = 3 })
        t.inv.expect_has("ics_little_pileofsalt", 1)
        t.cheat("::give ics_little_sap_bucket 1")
        goto_("embalmer", 3288, 2756)
        talk("embalmer", "ics_little_embalmer")
        t.check("embalmer.met", select(2, t.var.server("varb399_ics_metembalmer")) == 1, "met embalmer")
        t.check("embalmer.first", select(2, t.var.server("varb401_ics_gotsalt")) == 1 and select(2, t.var.server("varb402_ics_gotsap")) == 1, "salt " .. tostring(select(2, t.var.server("varb401_ics_gotsalt"))) .. " sap " .. tostring(select(2, t.var.server("varb402_ics_gotsap"))))
        goto_("raetul", 3311, 2788)
        talk("raetul", "ics_little_linen1")
        t.inv.expect_has("ics_little_linen", 1)
        goto_("embalmer-2", 3288, 2756)
        talk("embalmer-2", "ics_little_embalmer")
        t.check("embalmer.done", select(2, t.var.server("varb403_ics_gotlinen")) == 1, "linen handed in")
        goto_("carpenter", 3314, 2772)
        talk("carpenter", "ics_little_carpenter")
        t.expect("quest.stage.ritual", t.quest.expect_stage("ritual"))
        t.inv.expect_has("ics_little_holy_symbol", 1)

        -- ---- The third memory (4487 [3628..3692]).
        goto_("temple-door-3", 3295, 2781)
        t.exec("temple-door-3", t.player.click_loc, "icthalarins_temple_door", 1)
        t.exec("temple-door-3-dlg", t.chat.drain, { max_pages = 20 })
        t.ticks(4)
        copy_base(13, 19)
        goto_copy("pit-south-4", 28, 41)
        t.exec("pit-4", t.player.click_loc, "ics_little_pit_to", 2, { at = at(28, 42) })
        t.exec("pit-4-dlg", t.chat.drain, { max_pages = 20 })
        t.ticks(4)
        t.expect("quest.stage.place_symbol", t.quest.expect_stage("place_symbol"))
        copy_base(31, 46)
        goto_copy("east-door", 42, 48)
        t.exec("east-door", t.player.click_loc, "icthalarins_ancient_temple_door_2", 1, { at = at(42, 47) })
        t.ticks(3)
        goto_copy("sarcophagus", 47, 43)
        t.exec("sarcophagus", t.player.click_loc, "ics_sarcophigi_door_2", 1, { at = at(48, 43) })
        t.exec("sarcophagus-dlg", t.chat.drain, { max_pages = 8 })
        t.ticks(6)
        t.exec("memory3-end", t.chat.drain, { max_pages = 8 })
        t.expect("quest.stage.ceremony", t.quest.expect_stage("ceremony"))
        copy_base(28, 40)
        t.inv.expect_has("ics_little_holy_symbol", 1)

        -- ---- The ceremony (4487 [3970..4028]) and the Possessed Priest.
        goto_copy("pit-south-5", 28, 41)
        jump_pit("pit-5", "ics_little_pit_to", 28, 42, 45)
        goto_copy("east-door-2", 42, 48)
        t.exec("east-door-2", t.player.click_loc, "icthalarins_ancient_temple_door_2", 1, { at = at(42, 47) })
        t.ticks(4)
        -- 4487 [3996..4011]: the cat in the pack ends the scene by itself; drop it and reopen.
        t.chat.drain({ max_pages = 12 })
        t.ticks(14)
        t.check("ceremony.no-cat-ends", stage() == 21, "stage " .. tostring(stage()))
        t.player.inv_op("growncatobject", 5)
        t.ticks(3)
        t.exec("east-door-2b", t.player.click_loc, "icthalarins_ancient_temple_door_2", 1, { at = at(42, 47) })
        t.ticks(4)
        t.exec("ceremony-dlg", t.chat.drain, { max_pages = 40 })
        t.ticks(4)
        t.expect("quest.stage.possessed", t.quest.expect_stage("possessed"))
        for i = 1, 40 do
            if stage() >= 23 then break end
            t.exec("kill-priest-" .. i, t.player.attack, "ics_little_possessedpriest", 2, 30)
            t.ticks(4)
        end
        t.ticks(6)
        t.expect("quest.stage.priest_dead", t.quest.expect_stage("priest_dead"))
        goto_copy("altar", 42, 44)
        talk("hi-ceremony", "ics_little_hipriest_ceremony")
        t.expect("quest.stage.meet_god", t.quest.expect_stage("meet_god"))

        -- ---- The fourth memory (4487 [4405..4434]).
        goto_copy("east-door-3", 42, 46)
        t.exec("east-door-3", t.player.click_loc, "icthalarins_ancient_temple_door_2", 1, { at = at(42, 47) })
        t.ticks(6)
        t.exec("vision", t.chat.drain, { max_pages = 20 })
        t.ticks(4)
        t.exec("vision-2", t.chat.drain, { max_pages = 20 })
        t.expect("quest.stage.finish_talk", t.quest.expect_stage("finish_talk"))
        p = tile()
        t.check("vision.street", p.x == 3294 and p.z == 2781, "at " .. p.x .. "," .. p.z)

        -- ---- Completion (4487 [4517..]).
        goto_("high-priest-3", 3283, 2772)
        t.exec("finish", t.player.talk_to, "ics_little_hipriest_town", 1)
        t.ticks(4)
        t.exec("finish-drain", t.chat.drain, { max_pages = 3 })
        t.ticks(4)
        t.expect("quest.stage.complete", t.quest.expect_stage("complete"))
        t.inv.expect_has("ics_little_amulet_of_catspeak", 1)
        t.quest.expect_complete()
        t.ticks(3)
        -- Past the scroll ([4526..4544]): the talk runs on into Contact!'s start, declined here.
        t.exec("finish-rest", t.chat.drain, { stop_at = "options", max_pages = 40 })
        t.exec("finish-contact-no", t.chat.choose, "No.")
        t.exec("finish-end", t.chat.drain, { max_pages = 6 })
        t.ticks(4)
        -- Paths no other row reaches, and the recorded generic interactions once each.
        t.chat.close()
        t.exec("wear-catspeak", t.player.inv_op, "ics_little_amulet_of_catspeak", 2)
        t.ticks(2)
        goto_("after-cat", 3294, 2781)
        t.ticks(6)
        t.exec("cat-talk", t.player.talk_to, "growncat", 3)
        t.exec("cat-talk-menu", t.chat.drain, { stop_at = "options", max_pages = 4 })
        t.exec("cat-talk-1", t.chat.choose, "How are you doing?")
        t.exec("cat-talk-2", t.chat.drain, { stop_at = "options", max_pages = 4 })
        t.chat.close()
        t.ticks(2)
        goto_("gate-priest", 3321, 2762)
        t.exec("gate-priest", t.player.talk_to, "ics_little_priestdoorman", 1)
        t.exec("gate-priest-menu", t.chat.drain, { stop_at = "options", max_pages = 3 })
        t.exec("gate-priest-pick", t.chat.choose, "What's that place to the south?")
        t.exec("gate-priest-end", t.chat.drain, { max_pages = 12 })
        t.cheat("::give toa_akkha_ashes 1")
        t.cheat("::give ntk_scarab_gold 1")
        t.cheat("::give ntk_jewelled_sceptre_3 1")
        t.cheat("::give ics_little_bookofembalming 1")
        t.cheat("::give bucket_water 1")
        t.ticks(2)
        goto_("hp-after", 3282, 2772)
        t.exec("ashes", t.player.use_on, "toa_akkha_ashes", t.player.by_symbol("npc", "ics_little_hipriest_town"))
        t.exec("ashes-dlg", t.chat.drain, { max_pages = 10 })
        t.inv.expect_has("toa_akkha_ashes", 0)
        goto_("tarik-after", 3289, 2788)
        t.exec("tarik-artefact", t.player.use_on, "ntk_scarab_gold", t.player.by_symbol("npc", "ntk_tarik"))
        t.exec("tarik-artefact-dlg", t.chat.drain, { max_pages = 8 })
        t.exec("tarik-sceptre", t.player.use_on, "ntk_jewelled_sceptre_3", t.player.by_symbol("npc", "ntk_tarik"))
        t.exec("tarik-sceptre-dlg", t.chat.drain, { max_pages = 10 })
        t.exec("book", t.player.inv_op, "ics_little_bookofembalming", 1)
        t.exec("book-open", t.ui.await_open, "indexed_book", 10)
        t.ui.close_modal()
        t.ticks(2)
        t.exec("empty-bucket", t.player.inv_op, "bucket_water", 5)
        t.ticks(2)
        t.inv.expect_has("bucket_empty", 1)
        goto_("altar", 3281, 2775)
        t.exec("altar", t.player.click_loc, "contact_high_priest_temple_altar", 1)
        t.ticks(3)
        -- 4486 [561], [596]: a town ladder, up and down.
        goto_("ladder-town", 3285, 2768)
        t.exec("ladder-up", t.player.click_loc, "contact_ladder", 1)
        t.ticks(4)
        t.exec("ladder-down", t.player.click_loc, "contact_ladder_top", 1)
        t.ticks(4)
        -- 4481 [80]: bank at the Grand Exchange; [373..382]: Shantay; [320]: the glory.
        goto_("banker", 3167, 3489)
        t.exec("banker", t.player.talk_to, "banker1_east", 3)
        t.exec("banker-open", t.ui.await_open, "bankmain", 10)
        t.ui.close_modal()
        t.ticks(2)
        t.cheat("::give amulet_of_glory_4 1")
        t.ticks(1)
        t.exec("glory-wear", t.player.equip, "amulet_of_glory_4")
        t.ticks(2)
        t.ui.tab("equipment")
        t.ticks(1)
        do
            local _, slot = t.ui.widget("wornitems:slot2")
            t.ui.invoke(slot, 5)
        end
        t.ticks(6)
        goto_("shantay", 3302, 3124)
        t.exec("shantay", t.player.talk_to, "shantay", 3)
        t.exec("shantay-open", t.ui.await_open, "shopmain", 10)
        t.ui.close_modal()
        t.ticks(2)
        -- 4481 [394..455]: Shantay's carpet to Pollnivneach north.
        t.cheat("::give coins 200")
        goto_("carpet", 3308, 3110)
        t.exec("carpet", t.player.talk_to, "magic_carpet_seller1", 3)
        t.exec("carpet-menu", t.chat.drain, { stop_at = "options", max_pages = 4 })
        t.exec("carpet-pick", t.chat.choose, "I want to travel to Pollnivneach.")
        t.exec("carpet-fare", t.chat.drain, { max_pages = 5 })
        t.ticks(70)
    end,
}
