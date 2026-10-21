-- A Night at the Theatre, end to end, in the Quest Helper guide's ladder
-- (tools/quest_gate/ladder.py nightatthetheatre: 35 steps, 4 legs) plus the
-- Theatre of Blood leg the guide names as one step (completeTob), played in
-- Entry Mode by one raider.
--
-- The content is rebuilt from RSProx index 1679 (rsprox.net 1695) and index 3
-- (rsprox.net 101); docs/rsprox-analysis/quests/a_night_at_the_theatre.md has
-- the timeline every page and step below was written from. The character has
-- never beaten the Theatre, so the Stranger's first talk is his long one
-- (index 3 [22597]..), the raid is not skippable and is played.
--
-- Tiles: the Stranger 3673,3223 (areas/world/configs/m57_50.spawn); the crypt
-- staircase 3682,3231; the spider cave mouth 3656,3408 (4x2); the Sanctuary
-- stairs 3728,3300 and Daer Krand 3822,9777,2 (m59_152.spawn); the Nature
-- Grotto door 3440,3337; the hespori's stepping stone 3499,3355.

local STRANGER_X, STRANGER_Z = 3671, 3223
local CRYPT_X, CRYPT_Z = 3681, 3231
local CAVE_X, CAVE_Z = 3658, 3407
local GROTTO_X, GROTTO_Z = 3440, 3334
local STONE_X, STONE_Z = 3497, 3355

-- The server's tick log, for the RSProx audit (tools/rsprox_tells.py --ours,
-- tools/rsprox/ticklog_tells.py): on from the first leg run in this process,
-- one mark per leg.
local function ticklog_leg(t, name)
    if t.ticklog.mark("leg." .. name) ~= "ok" then
        t.note(select(2, t.ticklog.start()))
        t.ticklog.mark("leg." .. name)
    end
end

-- Quest Helper's anti-venom, drunk the way a player would: only when venomed.
-- The egg sac's ambush bites 1 in 4 (natt_cave.rs2), nothing in the quest
-- cures it, and venom left running takes 6..20 every 30 ticks from then on --
-- through the Hespori and into the Theatre, whose solvers are judged on the
-- damage they take (2026-10-21: bitten at tick 528, 126 venom damage failed
-- Bloat).
local function cure_venom(t, name)
    local vr, venom = t.var.server("varp7031_venom")
    if vr ~= "ok" or venom <= 0 then
        return
    end
    for dose = 4, 1, -1 do
        local item = "antivenom" .. dose
        local cr, count = t.inv.count(item)
        if cr == "ok" and count > 0 then
            t.exec(name, t.player.inv_op, item, 1)
            t.expect(name .. ".cured", t.await({ level = function()
                local r, v = t.var.server("varp7031_venom")
                return r == "ok" and v <= 0
            end, note = "varp7031_venom <= 0" }, 5))
            return
        end
    end
    t.check(name, false, "venomed (" .. venom .. ") with no anti-venom in the pack")
end

return {
    id = "nightatthetheatre",
    fixture = "fresh_lumbridge.ini",
    max_frames = 400000,
    setup = {
        "::clearinv",
        -- Prerequisites the quest reads: A Taste of Hope (the Stranger's
        -- qualify, nightatthetheatre.rs2 ~natt_qualify_fail_reason) and Nature
        -- Spirit (Filliman's talk, filliman.rs2 [label,filliman_ns_dialogue]).
        "::complete quest_tasteofhope",
        "::complete quest_naturespirit",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        "::setlevel prayer 99",
        "::setlevel ranged 99",
        "::setlevel magic 99",
        "::setlevel woodcutting 99",
        -- Quest Helper's items: the flail (worn: "You must have your flail
        -- equipped"), a saw, an axe, a ghostspeak amulet (worn), food, anti-venom.
        "::give ivandis_flail 1",
        "::wield ivandis_flail",
        "::give amulet_of_ghostspeak 1",
        "::wield amulet_of_ghostspeak",
        "::give poh_saw 1",
        "::give dragon_axe 1",
        "::give abyssal_whip 1",
        "::give antivenom4 2",
        "::give shark 18",
        -- A Taste of Hope's reward (::complete grants no items): the way to Ver
        -- Sinhaza, which no walk from Lumbridge reaches.
        "::give drakans_medallion 1",
    },
    bind = {
        varp = "varb12276_tobquest",
        constants = {
            not_started = 0, started = 2, crypt = 8, crypt_entered = 10, coffin = 12, head = 14,
            more = 16, memory1 = 18, spider = 20, skeleton = 22, note_found = 24, daer = 26,
            eggs_cave = 28, eggs = 30, cutscene1 = 32, memory2 = 34, memory3 = 36, grotto = 38,
            hespori = 40, hespori_fight = 42, bark_chop = 44, bark = 46, cutscene2 = 48,
            memory4 = 50, memory5 = 52, tob = 54, finish = 80, memory6 = 82, master = 84,
            complete = 86,
        },
        row = "quest_nightatthetheatre",
        display = "A Night at the Theatre",
        points = 2,
    },

    legs = {
        -- Leg 1: the Stranger, Ranis' crypt, the head, the first memory.
        {
            name = "crypt",
            run = function(t)
                ticklog_leg(t, "crypt")
                t.ticks(3)
                t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))
                -- To Ver Sinhaza by Drakan's medallion: Teleport > Ver Sinhaza (op 2,
                -- the obj record's subaction=2,1; RSProx index 225 [6080] lands on
                -- 3649,3230,0), then on foot to the Stranger.
                t.exec("travel-verSinhaza", t.player.inv_op, "drakans_medallion", 2, { subop = 1 })
                t.expect("travel-verSinhaza.landed", t.await({ level = function()
                    local _, here = t.world.tile()
                    return here ~= nil and here.x == 3649 and here.z == 3230 and here.level == 0
                end, note = "Drakan's medallion's landing, 3649,3230,0" }, 10))
                t.exec("goto-speakWithMysteriousStrangerToStart", t.player.goto_tile, STRANGER_X, STRANGER_Z, 0)
                t.exec("speakWithMysteriousStrangerToStart", t.player.talk_to, "tob_stranger")
                local r1, d1 = t.chat.drain({ stop_at = "options", max_pages = 200 })
                t.expect("speakWithMysteriousStrangerToStart.intro", r1, d1)
                t.exec("speakWithMysteriousStrangerToStart.really", t.chat.choose, "What's all this really about?")
                local r2, d2 = t.chat.drain({ stop_at = "options", max_pages = 200 })
                t.expect("speakWithMysteriousStrangerToStart.ask", r2, d2)
                t.exec("speakWithMysteriousStrangerToStart.yes", t.chat.choose, "Yes.")
                local r3, d3 = t.chat.drain({ max_pages = 200 })
                t.expect("speakWithMysteriousStrangerToStart.task", r3, d3)
                t.expect("quest.stage.crypt", t.quest.expect_stage("crypt"))

                t.exec("goto-enterVerSinhazaCrypts", t.player.goto_tile, CRYPT_X, CRYPT_Z, 0)
                t.exec("enterVerSinhazaCrypts", t.player.click_loc, "tobquest_crypt_entrance", 1)
                t.exec("enterVerSinhazaCrypts.yes", t.chat.play, { "options", "choose:Yes." })
                t.expect("quest.stage.crypt_entered", t.var.await_server("varb12276_tobquest", 10, 10))

                -- The crypt is the player's own copy (natt_crypt.rs2): its tiles are known only
                -- relative to the arrival tile, 3680,9651 in the template; the first vyrewatch
                -- (male_1) stands at 3675,9647, five west and four south of it.
                t.ticks(3)
                local _, arrive = t.world.tile()
                t.exec("killVyrewatchForKey.approach", t.player.walk_to, arrive.x - 3, arrive.z - 3, 20)
                t.exec("killVyrewatchForKey", t.player.attack, "tobquest_vyrewatch_male_1", 2, 30)
                t.exec("killVyrewatchForKey.dead", t.npc.await_dead_engaged, 300, 40, { eat = { item = "shark", below = 50 } })
                t.exec("pickupCryptKey", t.player.click_obj, "tobquest_key", 3)
                t.expect("pickupCryptKey.held", t.inv.await("tobquest_key", 1, 10))
                t.exec("unlockTheCryptGate", t.player.click_loc, "tobquest_crypt_inner_door", 1)
                t.expect("quest.stage.coffin", t.var.await_server("varb12276_tobquest", 12, 10))
                t.exec("searchTheCoffinInVerSinhazaCrypts", t.player.click_loc, "tobquest_crypt_coffin_op", 1)
                -- The player's line, the sawing under a black screen (a box with no
                -- continue), the head's box, two more lines (index 1679 [9742]..[9756]).
                local cr, cd = t.chat.drain({ max_pages = 20 })
                t.expect("searchTheCoffinInVerSinhazaCrypts.dialog", cr, cd)
                t.expect("searchTheCoffinInVerSinhazaCrypts.head", t.inv.await("tobquest_head", 1, 10))
                t.expect("quest.stage.head", t.quest.expect_stage("head"))
                -- Back through the gate (it shuts two ticks after each crossing,
                -- index 1679 [9735]; unlocked once is unlocked), then the door out.
                t.exec("leaveCrypts.gate", t.player.click_loc, "tobquest_crypt_inner_door", 1)
                t.exec("leaveCrypts", t.player.click_loc, "tobquest_crypt_exit", 1)
                t.exec("goto-speakWithMysteriousStrangerWithRanisHead", t.player.goto_tile, STRANGER_X, STRANGER_Z, 0)
                t.exec("speakWithMysteriousStrangerWithRanisHead", t.player.talk_to, "tob_stranger")
                local r4, d4 = t.chat.drain({ stop_at = "options", max_pages = 200 })
                t.expect("speakWithMysteriousStrangerWithRanisHead.greet", r4, d4)
                t.exec("speakWithMysteriousStrangerWithRanisHead.choose", t.chat.choose, "I managed to recover Ranis' head.")
                local r5, d5 = t.chat.drain({ max_pages = 200 })
                t.expect("speakMoreWithMysteriousStranger", r5, d5)
                t.exec("speakMoreWithMysteriousStranger.cutscene", t.cutscene.await, "memoryMaiden", { expect = {
                    { op = "moveto" }, { op = "lookat" }, { op = "reset" },
                } })
                local r6, d6 = t.chat.drain({ max_pages = 200 })
                t.expect("speakMoreWithMysteriousStranger.after", r6, d6)
                t.expect("quest.stage.spider", t.quest.expect_stage("spider"))
            end,
        },
        -- Leg 2: the spider cave, the note, Daer Krand's acid.
        {
            name = "cave",
            run = function(t)
                ticklog_leg(t, "cave")
                t.exec("goto-enterSpiderCave", t.player.goto_tile, CAVE_X, CAVE_Z, 0)
                t.exec("enterSpiderCave", t.player.click_loc, "araxyte_cave_entry", 1)
                t.exec("enterSpiderCave.yes", t.chat.play, { "options", "choose:Yes." })
                t.expect("quest.stage.skeleton", t.var.await_server("varb12276_tobquest", 22, 10))
                t.exec("searchSpiderCaveSkeleton", t.player.click_loc, "tobquest_skeleton_op", 1)
                t.expect("searchSpiderCaveSkeleton.note", t.inv.await("tobquest_note", 1, 30))
                t.exec("searchSpiderCaveSkeleton.box", t.chat.play, { "*" })
                t.exec("readStickyNote", t.player.inv_op, "tobquest_note", 1)
                t.expect("quest.stage.daer", t.var.await_server("varb12276_tobquest", 26, 10))
                t.key("escape")
                t.exec("exitSpiderCave", t.player.click_loc, "araxyte_cave_exit", 1)
                -- The crawl out lands two ticks after the click (natt_cave.rs2, index 1679 [9985]).
                t.expect("exitSpiderCave.out", t.await({ level = function()
                    local _, here = t.world.tile()
                    return here ~= nil and here.x < 6400 and here.z < 6400
                end, note = "out of the cave's copy" }, 20))
                t.exec("goto-enterSisterhoodSanctuary", t.player.goto_tile, 3727, 3302, 0)
                t.exec("enterSisterhoodSanctuary", t.player.click_loc, "slp_church_crypt_south_ladder_down", 1)
                t.expect("enterSisterhoodSanctuary.down", t.await({ level = function()
                    local _, here = t.world.tile()
                    return here ~= nil and here.level == 1
                end, note = "in the Sanctuary" }, 20))
                t.exec("goto-climbStairsToSisterhoodSanctuaryF1", t.player.goto_tile, 3828, 9777, 1)
                t.exec("climbStairsToSisterhoodSanctuaryF1", t.player.click_loc, "sanctuary_stairs_tall_2x3", 1)
                t.expect("climbStairsToSisterhoodSanctuaryF1.up", t.await({ level = function()
                    local _, here = t.world.tile()
                    return here ~= nil and here.level == 2
                end, note = "up to Daer Krand's floor" }, 20))
                t.exec("speakWithDaerKrand", t.player.talk_to, "sanctuary_dark_wizard")
                local r1, d1 = t.chat.drain({ stop_at = "options", max_pages = 200 })
                t.expect("speakWithDaerKrand.greet", r1, d1)
                t.exec("speakWithDaerKrand.acid", t.chat.choose, "Got any sulphuric acid?")
                local r2, d2 = t.chat.drain({ stop_at = "options", max_pages = 200 })
                t.expect("speakWithDaerKrand.story", r2, d2)
                t.exec("speakWithDaerKrand.bye", t.chat.choose, "See you later.")
                local r3, d3 = t.chat.drain({ max_pages = 200 })
                t.expect("speakWithDaerKrand.end", r3, d3)
                t.expect("speakWithDaerKrand.held", t.inv.await("tobquest_acid", 1, 10))
                t.expect("quest.stage.eggs_cave", t.quest.expect_stage("eggs_cave"))
            end,
        },
        -- Leg 3: the egg sac, the second and third memories, the Nature Spirit.
        {
            name = "eggs",
            run = function(t)
                ticklog_leg(t, "eggs")
                t.exec("climbStairsDownSisterhoodF0", t.player.click_loc, "sanctuary_stairs_tall_top_2x3", 1)
                t.exec("goto-exitSisterhoodSanctuary", t.player.goto_tile, 3739, 9703, 1)
                t.exec("exitSisterhoodSanctuary", t.player.click_loc, "slp_church_crypt_south_ladder_exit", 1)
                t.exec("goto-returnToSpiderCave", t.player.goto_tile, CAVE_X, CAVE_Z, 0)
                t.exec("returnToSpiderCave", t.player.click_loc, "araxyte_cave_entry", 1)
                t.exec("returnToSpiderCave.yes", t.chat.play, { "options", "choose:Yes." })
                t.exec("useSulphuricAcidOnEggSac", t.player.use_on, "tobquest_acid", t.player.by_symbol("loc", "tobquest_egg_sac_full"))
                t.expect("useSulphuricAcidOnEggSac.eggs", t.inv.await("tobquest_araxyte_eggs", 1, 40))
                t.expect("quest.stage.eggs", t.quest.expect_stage("eggs"))
                t.exec("useSulphuricAcidOnEggSac.flee", t.player.click_loc, "araxyte_cave_exit", 1)
                t.expect("useSulphuricAcidOnEggSac.out", t.await({ level = function()
                    local _, here = t.world.tile()
                    return here ~= nil and here.x < 6400 and here.z < 6400
                end, note = "out of the cave's copy" }, 30))
                -- The exit runs two more p_delay(1)s after its telejump
                -- (natt_cave.rs2 [oploc1,araxyte_cave_exit]); a drink clicked
                -- inside them is dropped, as on Jagex's engine.
                t.ticks(4)
                cure_venom(t, "useSulphuricAcidOnEggSac.antivenom")
                t.exec("goto-returnToMysteriousStrangerWithEggs", t.player.goto_tile, STRANGER_X, STRANGER_Z, 0)
                t.exec("returnToMysteriousStrangerWithEggs", t.player.talk_to, "tob_stranger")
                local r1, d1 = t.chat.drain({ stop_at = "options", max_pages = 200 })
                t.expect("returnToMysteriousStrangerWithEggs.greet", r1, d1)
                t.exec("returnToMysteriousStrangerWithEggs.choose", t.chat.choose, "I found some of those eggs.")
                local r2, d2 = t.chat.drain({ max_pages = 200 })
                t.expect("speakWithMysteriousStrangerAndWatchCutscenes", r2, d2)
                t.exec("mysteriousStrangerCutscenes.bloat", t.cutscene.await, "memoryBloat", { expect = {
                    { op = "moveto" }, { op = "lookat" }, { op = "reset" },
                } })
                local r3, d3 = t.chat.drain({ max_pages = 200 })
                t.expect("mysteriousStrangerCutscenes.between", r3, d3)
                t.exec("mysteriousStrangerCutscenes.nylocas", t.cutscene.await, "memoryNylocas", { expect = {
                    { op = "moveto" }, { op = "lookat" }, { op = "reset" },
                } })
                local r4, d4 = t.chat.drain({ max_pages = 200 })
                t.expect("mysteriousStrangerCutscenes.after", r4, d4)
                t.expect("quest.stage.grotto", t.quest.expect_stage("grotto"))
                t.exec("goto-goToNatureGrotto", t.player.goto_tile, GROTTO_X, GROTTO_Z, 0)
                t.exec("goToNatureGrotto", t.player.click_loc, "grotto_door_druidicspirit", 1)
                -- The Nature Spirit comes with the grotto's teleport (index 1679 [10500]).
                t.expect("goToNatureGrotto.spirit", t.npc.await_present("filliman_tarlock_ns", 10, 20))
                t.exec("speakToNatureSpiritInGrotto", t.player.talk_to, "filliman_tarlock_ns")
                local r5, d5 = t.chat.drain({ max_pages = 200 })
                t.expect("speakToNatureSpiritInGrotto.dialog", r5, d5)
                t.expect("quest.stage.hespori", t.quest.expect_stage("hespori"))
            end,
        },
        -- Leg 4: the island, the hespori, the bark, the last two of Ranis'
        -- memories.
        {
            name = "hespori",
            run = function(t)
                ticklog_leg(t, "hespori")
                t.exec("exitNatureGrotto", t.player.click_loc, "underground_rootwall_door_green", 1)
                t.exec("goto-goToHesporiFight", t.player.goto_tile, STONE_X, STONE_Z, 0)
                t.exec("goToHesporiFight", t.player.click_loc, "tobquest_stepping_stone", 1)
                t.ticks(6)
                t.exec("activateHesporiFight", t.player.click_loc, "tobquest_hespori_ready", 1)
                t.expect("quest.stage.hespori_fight", t.var.await_server("varb12276_tobquest", 42, 10))
                t.exec("fightHespori.wield", t.player.equip, "abyssal_whip")
                -- The fight a player fights (the recordings: index 1679 [10599]..
                -- [10754], index 3 [24661]..[24727]). The hespori swings every six
                -- ticks: magic until it is first struck, ranged after, so pray
                -- Protect from Magic, then Protect from Missiles. An open flower first
                -- (it is immune while one is open; they open again at 66% and 33%),
                -- else the hespori. The vines are tugged free at once, one tug a tick
                -- (six; recorded [10691]..[10696]). Eat below 60 between swings.
                t.exec("fightHespori.prayMagic", t.prayer.set, "protectfrommagic", true)
                local serial = t.player._message_serial() or 0
                local struck = false
                local rounds = 0
                local _, stage = t.var.server("varb12276_tobquest")
                while stage == 42 and rounds < 120 do
                    rounds = rounds + 1
                    local entangled = false
                    local lr, lines = t.chat.lines(serial)
                    if lr == "ok" then
                        for i = 1, #lines do
                            if string.find(lines[i].text, "entangles you", 1, true) then
                                entangled = true
                            end
                            serial = math.max(serial, lines[i].serial)
                        end
                    end
                    if entangled then
                        local _, here = t.world.tile()
                        for tug = 1, 6 do
                            t.player.walk_to(here.x + (tug % 2 == 0 and 1 or -1), here.z, 1)
                        end
                        t.note("fightHespori: tugged free of the vines in round " .. rounds)
                    end
                    local hp = t.player._quick_hp()
                    if hp ~= nil and hp < 60 then
                        t.player.eat({ "shark" })
                    end
                    -- Auto-attack often fells the target between rounds: a dying
                    -- one (its bar at 0, gone two or four ticks later) is not pressed.
                    local fr, flower = t.npc.nearest("tobquest_hespori_healer_active", 12)
                    local _, boss = t.npc.nearest("tobquest_hespori", 12)
                    if fr == "ok" and flower.health_ratio == 0 then
                        t.ticks(2)
                    elseif fr ~= "ok" and type(boss) == "table" and boss.health_ratio == 0 then
                        t.ticks(2)
                    elseif fr == "ok" then
                        t.exec("fightHespori.flower" .. rounds, t.player.attack, "tobquest_hespori_healer_active", 2, 20)
                        -- The fallen flower closes for two ticks before it goes.
                        t.ticks(4)
                    else
                        t.exec("fightHespori.hespori" .. rounds, t.player.attack, "tobquest_hespori", 2, 20)
                        if not struck then
                            struck = true
                            t.exec("fightHespori.prayMissiles", t.prayer.set, "protectfrommissiles", true)
                        end
                        t.ticks(6)
                    end
                    _, stage = t.var.server("varb12276_tobquest")
                end
                t.check("fightHespori", stage == 44, "hespori fight ended at stage " .. tostring(stage) .. " after " .. rounds .. " round(s)")
                t.exec("fightHespori.prayOff", t.prayer.set, "protectfrommissiles", false)
                t.exec("chopHesporiForBark", t.player.click_loc, "tobquest_hespori_dead", 1)
                t.expect("chopHesporiForBark.bark", t.inv.await("tobquest_bark", 1, 20))
                t.exec("chopHesporiForBark.box", t.chat.play, { "*" })
                t.exec("goto-returnToMysteriousStrangerWithBark", t.player.goto_tile, STRANGER_X, STRANGER_Z, 0)
                t.exec("returnToMysteriousStrangerWithBark", t.player.talk_to, "tob_stranger")
                local r1, d1 = t.chat.drain({ stop_at = "options", max_pages = 200 })
                t.expect("returnToMysteriousStrangerWithBark.greet", r1, d1)
                t.exec("returnToMysteriousStrangerWithBark.choose", t.chat.choose, "I found a hespori.")
                local r2, d2 = t.chat.drain({ max_pages = 200 })
                t.expect("speakWithMysteriousStrangerAndWatchCutscenes2", r2, d2)
                t.exec("mysteriousStrangerCutscenes2.sotetseg", t.cutscene.await, "memorySotetseg", { expect = {
                    { op = "moveto" }, { op = "lookat" }, { op = "reset" },
                } })
                local r3, d3 = t.chat.drain({ max_pages = 200 })
                t.expect("mysteriousStrangerCutscenes2.between", r3, d3)
                t.exec("mysteriousStrangerCutscenes2.xarpus", t.cutscene.await, "memoryXarpus", { expect = {
                    { op = "moveto" }, { op = "lookat" }, { op = "reset" },
                } })
                local r4, d4 = t.chat.drain({ max_pages = 200 })
                t.expect("mysteriousStrangerCutscenes2.after", r4, d4)
                t.expect("quest.stage.tob", t.quest.expect_stage("tob"))
            end,
        },
        -- Leg 5: the Theatre, Entry Mode, one raider (QD.raid.tob_entry_solo).
        {
            name = "theatre",
            run = function(t)
                ticklog_leg(t, "theatre")
                -- Before the kit, which clears the pack and the anti-venom with it.
                cure_venom(t, "completeTob.antivenom")
                -- The raid's own kit (script/plugins/quest_driver/tob_relay.lua
                -- QD.raid.tob_entry_kit: the solo Entry Mode loadout, which clears the
                -- pack -- every quest item has been handed over by now).
                local kit = QD_ROOT.raid.tob_entry_kit()
                local kit_bad = 0
                for i = 1, #kit do
                    local kr = t.cheat(kit[i])
                    if kr ~= "ok" then kit_bad = kit_bad + 1 end
                end
                t.check("completeTob.kit", kit_bad == 0, #kit .. " kit line(s), " .. kit_bad .. " refused")
                -- In the way a player goes in: the notice board's Entry Mode party of
                -- one, then the door, its warning and the ready check; every room by
                -- its solver (QD.raid.tob_entry_solo), the reward chest and out to the
                -- lobby.
                t.exec("completeTob.party", t.party.form, "entry")
                local ok, rd = QD_ROOT.raid.tob_entry_solo(t, { enter = "door" })
                t.check("completeTob", ok == true, tostring(rd))
                t.expect("quest.stage.finish", t.quest.expect_stage("finish"))
            end,
        },
        -- Leg 6: the doctor's memory, the Mysterious Master, the lamps.
        {
            name = "finish",
            run = function(t)
                ticklog_leg(t, "finish")
                t.exec("goto-speakWithMysteriousStrangerToFinish", t.player.goto_tile, STRANGER_X, STRANGER_Z, 0)
                local _, snapshot = t.skill.snapshot()
                t.exec("speakWithMysteriousStrangerToFinish", t.player.talk_to, "tob_stranger")
                local r1, d1 = t.chat.drain({ stop_at = "options", max_pages = 200 })
                t.expect("speakWithMysteriousStrangerToFinish.greet", r1, d1)
                t.exec("speakWithMysteriousStrangerToFinish.choose", t.chat.choose, "Did you manage to get any useful memories from Verzik?")
                local r2, d2 = t.chat.drain({ max_pages = 200 })
                t.expect("speakWithMysteriousStrangerToFinish.ask", r2, d2)
                t.exec("speakWithMysteriousStrangerToFinish.cutscene", t.cutscene.await, "memoryDoctor", { expect = {
                    { op = "moveto" }, { op = "lookat" }, { op = "reset" },
                } })
                local r3, d3 = t.chat.drain({ max_pages = 200 })
                t.expect("speakWithMysteriousStrangerToFinish.between", r3, d3)
                t.exec("speakWithMysteriousStrangerToFinish.masterCutscene", t.cutscene.await, "mysteriousMaster", { expect = {
                    { op = "moveto" }, { op = "lookat" }, { op = "reset" },
                } })
                t.expect("speakWithMysteriousStrangerToFinish.lamps", t.inv.await("tobquest_lamp", 4, 20))
                t.quest.expect_complete()
                t.exec("rubLamp", t.player.inv_op, "tobquest_lamp", 1)
                t.exec("rubLamp.skill", t.chat.play, { "options", "choose:Attack.", "mesbox:Your wish has been granted!" })
                t.expect("rubLamp.xp", t.skill.expect_gain("attack", 20000, snapshot))
                t.finish(0)
            end,
        },
    },
}
