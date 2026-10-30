-- A Soul's Bane, played the way RSProx index 4430 (rev 241, 2026-10-29) played
-- it: Launa and the start confirm, the rope, the climb and the scene of the room
-- of rage, the rack and the rage bar to its tenth segment, the memory of Launa
-- and the boy, the room of fear's holes and five reapers, the boy at the hole,
-- the room of confusion's illusions and five real beasts, the boy scared, the
-- room of hopelessness' five creatures through their three forms, the bridge,
-- Brana and Tolna's scene, the three heads, the reunion and the surface.
-- Every room and scene is a private copy: positions are checked as the tile's
-- offset in its square (x % 64, z % 64), the template's offset.
-- docs/rsprox-analysis/quests/soulsbane.md has the audit.
return {
    id = "soulsbane",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::give rope 1",
        "::give shark 26",
        -- The recorder brought a rune scimitar for the rooms after the rage room
        -- (index 4430 [1483]); the rage room takes a rack weapon.
        "::give rune_scimitar 1",
        "::setlevel hitpoints 99",
        "::setlevel attack 75",
        "::setlevel strength 75",
        "::setlevel defence 75",
    },
    run = function(t)
        t.quest.bind({
            varp = "varb2011_soulbane_prog",
            constants = { not_started = 0, started = 1, anger_entered = 2, anger_cleared = 3, fear_entered = 4, fear_cleared = 5,
                confu_entered = 6, confu_cleared = 7, hope_entered = 8, hope_cleared = 9, tolna_entered = 10,
                tolna_human = 11, tolna_surface = 12, complete = 13 },
            display = "A Soul's Bane",
            points = 1,
        })
        t.ticklog.start()
        t.ticks(3)
        -- Fight margin (brief: lowest hp >= 25% of 99 AND food left), one row per room.
        local function new_fight() return { low = nil, kills = 0, waits = 0 } end
        local function kill_wait(name, fight, budget, attempts, below)
            local r, d = t.exec(name, t.npc.await_dead_engaged, budget, attempts, { eat = { item = "shark", below = below } })
            local low = tonumber(string.match(tostring(d), "lowest hp (%d+)/") or "")
            if low ~= nil and (fight.low == nil or low < fight.low) then
                fight.low = low
            end
            fight.waits = fight.waits + 1
            if r == "ok" then fight.kills = fight.kills + 1 end
            return r, d
        end
        local function margin_row(name, fight, what)
            local fr, food = t.inv.count("shark")
            t.check(name, fight.low ~= nil and fight.low >= 25 and fr == "ok" and food >= 1,
                what .. ": " .. fight.kills .. "/" .. fight.waits .. " kill wait(s) ok, lowest hp " .. tostring(fight.low)
                    .. "/99, sharks left " .. tostring(food) .. " of 26 staged (" .. tostring(fr)
                    .. ") (margin: lowest hp >= 25, a quarter of 99, AND food left)")
        end
        -- A scene: drain every page (the boxes with no continue are waited out)
        -- until the var it ends on reads `want`.
        local function scene(name, var, want, budget)
            local got = nil
            for i = 1, budget or 120 do
                t.chat.drain({ wait_ticks = 40 })
                local _, v = t.var.server(var)
                got = v
                if v == want then break end
                t.ticks(1)
            end
            t.check(name, got == want, var .. "=" .. tostring(got) .. " (want " .. tostring(want) .. ")")
        end
        local function tile_in_square(name, x, z, level)
            local _, tile = t.world.tile()
            local ok = tile and tile.x % 64 == x % 64 and tile.z % 64 == z % 64 and (level == nil or tile.level == level)
            t.check(name, ok, "at " .. tostring(tile and tile.x) .. "," .. tostring(tile and tile.z) .. "," .. tostring(tile and tile.level)
                .. " (template " .. x .. "," .. z .. ": offset " .. (x % 64) .. "," .. (z % 64) .. ")")
            return tile
        end
        -- A template tile in the copy the player stands in.
        local function copy_tile(x, z)
            local _, tile = t.world.tile()
            return tile.x - tile.x % 64 + x % 64, tile.z - tile.z % 64 + z % 64
        end

        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))
        -- Launa stands inside the Dig Site fence; the gate is crossed on foot.
        t.exec("goto-talkToLauna.fenceGate", t.player.goto_tile, 3293, 3429, 0)
        t.exec("talkToLauna.fenceGateEast", t.player.pass_door, { closed = "vm_fencegate_r", open = "vm_fencegate_r",
            at = { 3296, 3429, 0 }, near = { 3294, 3429 }, far = { 3297, 3429 } })
        t.exec("goto-launa", t.player.goto_tile, 3307, 3455, 0)
        -- [880]..[899]
        t.exec("talkToLauna", t.player.talk_to, "soulbane_launa_multi", 1)
        t.exec("talkToLauna.drain", t.chat.drain, { stop_at = "options" })
        t.exec("talkToLauna.confirm", t.chat.choose, "Yes.")
        t.exec("talkToLauna.tail", t.chat.drain, {})
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- [923] the rope on the edge Launa stands by.
        t.ticklog.mark("leg.rope")
        t.exec("useRopeOnRift", t.player.use_on, "rope", t.player.by_symbol("loc", "soulbane_falloff1"))
        t.exec("useRopeOnRift.roped", t.var.await, "varb2032_soulbane_riftrope_pres", 1, 10)
        local _, ropes = t.inv.count("rope")
        t.check("useRopeOnRift.gone", ropes == 0, "ropes left " .. tostring(ropes))

        -- [931]..[965] down the rope: the scene, then the room's own copy.
        t.ticklog.mark("leg.rage")
        t.exec("enterRift", t.player.click_loc, "soulbane_falloff2_rope_multi", 1)
        scene("enterRift.scene", "varb2011_soulbane_prog", 2, 120)
        t.expect("quest.stage.anger_entered", t.quest.expect_stage("anger_entered"))
        tile_in_square("enterRift.tile", 3015, 5244, 0)
        t.ticks(3)

        -- [970] the rack: the sword.
        t.exec("takeWeapon", t.player.click_loc, "soulbane_rack_multi", 1)
        t.exec("takeWeapon.menu", t.chat.drain, { stop_at = "options" })
        t.exec("takeWeapon.choose", t.chat.choose, "The Sword")
        t.exec("takeWeapon.has", t.inv.await, "soulbane_anger_swordq", 1, 10)
        t.exec("takeWeapon.equip", t.player.equip, "soulbane_anger_swordq")
        t.ticks(2)
        local _, snap = t.skill.snapshot()
        -- The bar fills with damage until the rage is let out. A death brings in
        -- the other kind, never the same one back (4430: unicorn [1006] -> a bear,
        -- bear [1068] -> a unicorn), so the weapon changes at the rack after
        -- every kill, as the recorder's did.
        local rage = new_fight()
        local kinds = {
            { npc = "soulbane_anger_unicorn", choice = "The Sword", obj = "soulbane_anger_swordq" },
            { npc = "soulbane_anger_bear", choice = "The Spear", obj = "soulbane_anger_spearq" },
        }
        local turn, wielded = 1, 1
        for i = 1, 30 do
            local _, st = t.var.server("varb2011_soulbane_prog")
            if st ~= 2 then break end
            local _, special = t.var.server("varb2042_soulbane_anger_donespecial")
            if special == 1 then break end
            local kind = kinds[turn]
            if wielded ~= turn then
                t.exec("rage.take" .. i, t.player.click_loc, "soulbane_rack_multi", 1)
                t.exec("rage.take" .. i .. ".menu", t.chat.drain, { stop_at = "options" })
                t.exec("rage.take" .. i .. ".choose", t.chat.choose, kind.choice)
                t.exec("rage.take" .. i .. ".has", t.inv.await, kind.obj, 1, 10)
                t.exec("rage.take" .. i .. ".equip", t.player.equip, kind.obj)
                t.ticks(2)
                wielded = turn
            end
            local ar = t.player.attack(kind.npc, 2, 60)
            if ar == "ok" then
                kill_wait("rage." .. kind.npc .. i .. ".dead", rage, 120, 8, 30)
                turn = 3 - turn
            else
                t.ticks(10)
            end
            t.ticks(2)
        end
        local _, dealt = t.var.server("varb2036_soulbane_anger_damagedealt")
        t.check("rage.bar", (dealt or 0) >= 1500, "damage dealt " .. tostring(dealt) .. " (the bar's tenth segment at 1500)")
        -- [1392]..[1436] the special, the page, the memory of Launa and the boy.
        scene("rageDone.memory", "varb2560_soulbane_watchedcutscene", 1, 120)
        t.ticks(5)
        t.expect("quest.stage.anger_cleared", t.quest.expect_stage("anger_cleared"))
        t.exec("rageDone.xp", t.skill.expect_gain, "attack", 40, snap)
        tile_in_square("rageDone.back", 3028, 5230, 0)
        margin_row("rageDone.margin", rage, "Rage room (angry unicorns, rack sword)")
        t.exec("rageDone.wield", t.player.equip, "rune_scimitar")
        t.ticks(2)

        -- [1439]..[1481] the exit past the dead fire: the scene and the page.
        t.ticklog.mark("leg.fear")
        t.exec("leaveAngerRoom", t.player.click_loc, "soul_bane_awall_void_exit", 1)
        scene("leaveAngerRoom.scene", "varb2011_soulbane_prog", 4, 120)
        t.expect("quest.stage.fear_entered", t.quest.expect_stage("fear_entered"))
        t.exec("fear.page", t.chat.drain, {})
        tile_in_square("leaveAngerRoom.tile", 3054, 5244, 0)
        local _, sword = t.inv.count("soulbane_anger_swordq")
        t.check("leaveAngerRoom.weapon-gone", sword == 0, "anger swords in pack " .. tostring(sword))
        local fear = new_fight()
        local holes = { "soul_bane_fwall_void", "soul_bane_fwall_void2", "soul_bane_fwall_void3", "soul_bane_fwall_void4", "soul_bane_fwall_void5", "soul_bane_fwall_void6" }
        for k = 1, 5 do
            local _, hidden = t.var.server("varb2012_soulbane_fear_enemydoor")
            if k == 1 then
                -- first a hole that is NOT the hidden one: nothing may appear
                local wrong = (hidden + 1) % 6
                local wr = t.player.click_loc(holes[wrong + 1], 1)
                t.ticks(10)
                local rn = t.npc.nearest("soulbane_fear_reaper", 20)
                t.check("lookInsideHoles.wrong.empty", rn ~= "ok", "wrong hole " .. wrong .. " (hidden " .. hidden .. ") click=" .. tostring(wr) .. ", reaper lookup=" .. tostring(rn))
            end
            t.player.click_loc(holes[hidden + 1], 1)
            t.exec("lookInsideHoles" .. k, t.npc.await_present, "soulbane_fear_reaper", 20, 30)
            t.ticks(3)
            t.exec("killReaper" .. k, t.player.attack, "soulbane_fear_reaper", 2, 80)
            kill_wait("killReaper" .. k .. ".dead", fear, 200, 8, 50)
            t.ticks(4)
            local _, tally = t.var.server("varb2019_soulbane_fear_killedtally")
            local _, nexth = t.var.server("varb2012_soulbane_fear_enemydoor")
            t.check("killReaper" .. k .. ".tally", tally == k and nexth ~= hidden,
                "fear tally=" .. tostring(tally) .. ", hole " .. tostring(hidden) .. " -> " .. tostring(nexth))
        end
        -- [1867]..[1901] the page, the boy and the hole.
        scene("fearDone.hole", "varb2560_soulbane_watchedcutscene", 1, 120)
        t.ticks(5)
        t.expect("quest.stage.fear_cleared", t.quest.expect_stage("fear_cleared"))
        tile_in_square("fearDone.back", 3050, 5221, 0)
        margin_row("fearDone.margin", fear, "Fear room (five fear reapers)")

        -- [1906]..[1957] the lit exit: the scene of the room of confusion.
        t.ticklog.mark("leg.confusion")
        t.exec("leaveFearRoom", t.player.click_loc, "soulbane_fwall_exit_multi", 1)
        scene("leaveFearRoom.scene", "varb2011_soulbane_prog", 6, 120)
        t.expect("quest.stage.confu_entered", t.quest.expect_stage("confu_entered"))
        tile_in_square("leaveFearRoom.tile", 3060, 5210, 0)
        -- An illusion: eight swings and it was never real.
        t.exec("hitIllusion", t.player.attack, "soulbane_confu_creeper_fake1", 2, 60)
        t.exec("hitIllusion.gone", t.npc.await_gone, "soulbane_confu_creeper_fake1", 20, 120)
        t.ticks(2)
        t.exec("hitIllusion.doubt", t.msg.expect, "Does this creature really exist?")
        t.exec("hitIllusion.unreal", t.msg.expect, "That monster wasn't even real!")
        local confu = new_fight()
        for w = 1, 5 do
            t.exec("killRealConfusionBeast" .. w, t.player.attack, "soulbane_confu_creeper", 2, 80)
            kill_wait("killRealConfusionBeast" .. w .. ".dead", confu, 200, 8, 50)
            t.ticks(3)
            local gone = 0
            for _, v in ipairs({ "varb2014_soulbane_confu_door1pres", "varb2015_soulbane_confu_door2pres", "varb2016_soulbane_confu_door3pres",
                "varb2017_soulbane_confu_door4pres", "varb2018_soulbane_confu_door5pres" }) do
                local _, d = t.var.server(v)
                if d == 1 then gone = gone + 1 end
            end
            t.check("killRealConfusionBeast" .. w .. ".doors", gone == w, "false doors gone " .. gone)
        end
        -- [2530]..[2561] the page, the boy scared.
        scene("confuDone.scared", "varb2560_soulbane_watchedcutscene", 1, 120)
        t.expect("quest.stage.confu_cleared", t.quest.expect_stage("confu_cleared"))
        margin_row("confuDone.margin", confu, "Confusion room (five real confusion beasts)")

        -- [2563]..[2604] the real door: the scene of the room of hopelessness.
        t.ticklog.mark("leg.hope")
        t.exec("leaveConfusionRoom", t.player.click_loc, "soulbane_door_multi6", 1)
        scene("leaveConfusionRoom.scene", "varb2011_soulbane_prog", 8, 120)
        t.expect("quest.stage.hope_entered", t.quest.expect_stage("hope_entered"))
        tile_in_square("leaveConfusionRoom.tile", 3085, 5210, 0)
        -- Five creatures, each through three forms of the SAME npc (a
        -- transformation, never a death, until the third: 4430 [2636], [2674],
        -- [2737]), so a creature is done when the tally moves, not its slot.
        local forms = { "soulbane_hope_monst1", "soulbane_hope_monst2", "soulbane_hope_monst3" }
        local hope = new_fight()
        for c = 1, 5 do
            local tally = 0
            for round = 1, 40 do
                local _, n = t.var.server("varb2021_soulbane_hope_killedtally")
                tally = n or 0
                if tally >= c then break end
                local pressed = false
                for _, f in ipairs(forms) do
                    if t.npc.nearest(f, 30) == "ok" and t.player.attack(f, 2, 40) == "ok" then
                        pressed = true
                        break
                    end
                end
                if pressed then
                    local _, d = t.npc.await_dead_engaged(25, 4, { eat = { item = "shark", below = 40 } })
                    local low = tonumber(string.match(tostring(d), "lowest hp (%d+)/") or "")
                    if low ~= nil and (hope.low == nil or low < hope.low) then hope.low = low end
                else
                    t.ticks(5)
                end
            end
            hope.waits = hope.waits + 1
            if tally >= c then hope.kills = hope.kills + 1 end
            t.check("killHopeless" .. c .. ".tally", tally >= c, "hopeless tally=" .. tostring(tally))
        end
        -- [3276]..[3282] white, the bridge room, the page.
        t.exec("hope.lifeforce", t.msg.expect, "These monsters seem to draw my very lifeforce away!")
        scene("hopeDone.bridge", "varb2020_soulbane_hope_bridgepres", 1, 60)
        t.ticks(4)
        t.exec("hopeDone.page", t.chat.drain, {})
        t.expect("quest.stage.hope_cleared", t.quest.expect_stage("hope_cleared"))
        tile_in_square("bridgeCopy.tile", 3021, 5210, 0)
        margin_row("hopeDone.margin", hope, "Hopelessness room (five hopeless creatures, three forms each)")

        -- [3293]..[3356] the bridge room's exit: Brana and Tolna's scene.
        t.ticklog.mark("leg.tolna")
        local bx, bz = copy_tile(3021, 5192)
        t.player.walk_to(bx, bz, 60)
        t.ticks(2)
        local ex, ez = copy_tile(3020, 5188)
        t.exec("leaveHopelessRoom", t.player.click_loc, "soul_bane_hwall_void_exit", 1, { at = { ex, ez } })
        scene("leaveHopelessRoom.scene", "varb2011_soulbane_prog", 10, 160)
        t.expect("quest.stage.tolna_entered", t.quest.expect_stage("tolna_entered"))
        tile_in_square("leaveHopelessRoom.tile", 2971, 5212, 1)
        local heads = { "soulbane_final_tolna1", "soulbane_final_tolna2", "soulbane_final_tolna3" }
        t.exec("heads.present", t.npc.await_present, "soulbane_final_tolna1", 30, 20)
        local head_bits = { "varb2022_soulbane_final_tol1dead", "varb2023_soulbane_final_tol2dead", "varb2024_soulbane_final_tol3dead" }
        local tolna = new_fight()
        -- A head is fought until it dies, but auto-retaliate can turn the player on
        -- another while they eat (the north head swings every tick), so one wait
        -- may see more than one die: the loop ends when every dead bit is set.
        local function heads_left()
            local n = 0
            for k = 1, 3 do
                local _, dead = t.var.server(head_bits[k])
                if dead ~= 1 then n = n + 1 end
            end
            return n
        end
        for h = 1, 3 do
            if heads_left() == 0 then break end
            local engaged, which, trail = "none", nil, ""
            for attempt = 1, 8 do
                for k = 1, 3 do
                    local _, dead = t.var.server(head_bits[k])
                    if dead ~= 1 and t.npc.nearest(heads[k], 20) == "ok" then
                        local ar, ad = t.player.attack(heads[k], 2, 40)
                        trail = trail .. heads[k] .. "=" .. tostring(ar) .. " | "
                        if ar == "ok" then
                            engaged, which = ad, k
                            break
                        end
                    end
                end
                if which ~= nil then break end
                t.ticks(3)
            end
            t.check("killHeads" .. h, which ~= nil, "engaged " .. tostring(which and heads[which]) .. ": "
                .. tostring(engaged) .. " -- presses: " .. trail:sub(1, 400))
            if which == nil then break end
            kill_wait("killHeads" .. h .. ".dead", tolna, 400, 12, 45)
            t.ticks(4)
            local _, bit = t.var.server(head_bits[which])
            t.check("killHeads" .. h .. ".bit", bit == 1, heads[which] .. " dead bit " .. head_bits[which] .. "=" .. tostring(bit))
        end
        margin_row("killHeads.margin", tolna, "Tolna's three heads")
        t.expect("quest.stage.tolna_human", t.quest.expect_stage("tolna_human"))
        -- [3579] Tolna wakes; [3586]..[3597] the reunion, then the surface.
        t.exec("talkToTolna.present", t.npc.await_present, "soulbane_tolna", 30, 20)
        t.exec("talkToTolna", t.player.talk_to, "soulbane_tolna", 1)
        t.exec("talkToTolna.drain", t.chat.drain, {})
        t.ticks(3)
        t.expect("quest.stage.tolna_surface", t.quest.expect_stage("tolna_surface"))
        local _, st = t.world.tile()
        t.check("talkToTolna.surface", st.level == 0 and st.x == 3309 and st.z == 3452, tostring(st.x) .. "," .. tostring(st.z) .. "," .. tostring(st.level))
        t.exec("talkToTolna.returned", t.msg.expect, "You've returned to the entrance.")
        local lr = t.npc.nearest("soulbane_launa", 12)
        t.check("talkToTolna.launa-hidden", lr ~= "ok", "launa lookup=" .. tostring(lr))

        -- [3609]..[3619] the surface Tolna: the hand-in on the last click.
        t.ticklog.mark("leg.complete")
        local _, snap2 = t.skill.snapshot()
        local _, coins0 = t.inv.count("coins")
        t.exec("talkToTolnaAgain", t.player.talk_to, "soulbane_tolna_multi", 1)
        t.exec("talkToTolnaAgain.drain", t.chat.drain, {})
        t.ticks(4)
        local _, coins1 = t.inv.count("coins")
        t.check("reward.coins", coins1 - coins0 == 500, "coins " .. tostring(coins0) .. " -> " .. tostring(coins1))
        t.exec("reward.defence", t.skill.expect_gain, "defence", 500, snap2)
        t.exec("reward.hitpoints", t.skill.expect_gain, "hitpoints", 500, snap2)
        t.quest.expect_complete()
        t.finish(0)
    end,
}
