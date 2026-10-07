-- A Soul's Bane: Launa, rope, rage, fear, confusion, hopelessness, Tolna; adapted from the parity driver
return {
    id = "soulsbane",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::give rope 1",
        "::give shark 26",
        -- Quest Helper / wiki: "combat equipment" for the rooms after Rage (the Rage room takes a
        -- rack weapon, so the scimitar is wielded only once the rack swords are gone). No dialogue
        -- branches on combat level (grep soulsbane*.rs2: no combat/stat read), so the staged
        -- 75/75/75/99 only buys the fight margin.
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
        t.ticks(3)
        -- Fight margin (brief: lowest hp >= 25% of 99 AND food left), one row per room; every kill
        -- wait feeds its room's tally from the verb's own "lowest hp N/" detail.
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
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))
        -- Launa stands inside the Dig Site fence (reach.py 3206,3233 -> 3307,3455: NEEDS-DOOR via
        -- vm_fencegate_r@3296,3429; 3297,3429 -> 3307,3455 REACH len=36). The selfstage double gate
        -- is pressed on foot (golem.lua / itexam.lua carry the same crossing).
        t.exec("goto-talkToLauna.fenceGate", t.player.goto_tile, 3293, 3429, 0)
        t.exec("talkToLauna.fenceGateEast", t.player.pass_door, { closed = "vm_fencegate_r", open = "vm_fencegate_r",
            at = { 3296, 3429, 0 }, near = { 3294, 3429 }, far = { 3297, 3429 } })
        t.exec("goto-launa", t.player.goto_tile, 3307, 3455, 0)
        t.exec("talkToLauna", t.player.talk_to, "soulbane_launa_multi", 1)
        t.exec("talkToLauna.drain", t.chat.drain, { stop_at = "options" })
        t.exec("talkToLauna.choose", t.chat.choose, "/Would you like/")
        t.exec("talkToLauna.tail", t.chat.drain, {})
        t.expect("quest.stage.started", t.quest.expect_stage("started"))
        t.exec("useRopeOnRift", t.player.use_on, "rope", t.player.by_symbol("loc", "soulbane_falloff2_rope_multi"))
        t.ticks(3)
        t.exec("useRopeOnRift.roped", t.var.await, "varb2032_soulbane_riftrope_pres", 1, 10)
        t.exec("enterRift", t.player.click_loc, "soulbane_falloff2_rope_multi", 1)
        t.ticks(4)
        t.exec("enterRift.drain", t.chat.drain, {})
        t.expect("quest.stage.anger_entered", t.quest.expect_stage("anger_entered"))
        local _, tile = t.world.tile()
        t.check("enterRift.tile", tile.x >= 3008 and tile.x <= 3039 and tile.z >= 5216, tostring(tile.x) .. "," .. tostring(tile.z))
        t.exec("takeWeapon", t.player.click_loc, "soulbane_rack_multi", 1)
        t.exec("takeWeapon.menu", t.chat.drain, { stop_at = "options" })
        t.exec("takeWeapon.choose", t.chat.choose, "The Sword")
        t.ticks(2)
        t.exec("takeWeapon.has", t.inv.await, "soulbane_anger_swordq", 1, 10)
        t.exec("takeWeapon.equip", t.player.equip, "soulbane_anger_swordq")
        t.ticks(2)
        local _, snap = t.skill.snapshot()
        local rage = new_fight()
        for i = 1, 8 do
            t.exec("killUnicorn" .. i, t.player.attack, "soulbane_anger_unicorn", 2, 60)
            kill_wait("killUnicorn" .. i .. ".dead", rage, 120, 8, 30)
            t.ticks(4)
            local _, dealt = t.var.server("varb2036_soulbane_anger_damagedealt")
            local _, st = t.var.server("varb2011_soulbane_prog")
            t.check("killUnicorn" .. i .. ".tally", true, "rage tally=" .. tostring(dealt) .. " stage=" .. tostring(st))
        end
        t.exec("rageDone.drain", t.chat.drain, {})
        t.expect("quest.stage.anger_cleared", t.quest.expect_stage("anger_cleared"))
        t.exec("rageDone.xp", t.skill.expect_gain, "attack", 40, snap)
        local _, tw = t.inv.count("soulbane_anger_swordq")
        t.check("rageDone.weapon-gone", tw == 0, "swords in pack=" .. tostring(tw))
        margin_row("rageDone.margin", rage, "Rage room (angry unicorns, rack sword)")
        -- The rack swords are gone: wield the brought weapon for the rest of the gauntlet.
        t.exec("rageDone.wield", t.player.equip, "rune_scimitar")
        t.ticks(2)
        -- Fear: east exit past the fire
        t.exec("leaveAngerRoom", t.player.click_loc, "soul_bane_awall_void_exit", 1)
        t.ticks(4)
        t.exec("leaveAngerRoom.drain", t.chat.drain, {})
        t.expect("quest.stage.fear_entered", t.quest.expect_stage("fear_entered"))
        local _, ft = t.world.tile()
        t.check("leaveAngerRoom.tile", ft.x >= 3040 and ft.x <= 3071 and ft.z >= 5216, tostring(ft.x) .. "," .. tostring(ft.z))
        local fear = new_fight()
        local holes = { "soul_bane_fwall_void", "soul_bane_fwall_void2", "soul_bane_fwall_void3", "soul_bane_fwall_void4", "soul_bane_fwall_void5", "soul_bane_fwall_void6" }
        for k = 1, 5 do
            local _, hidden = t.var.server("varb2012_soulbane_fear_enemydoor")
            -- first a hole that is NOT the hidden one: nothing may appear
            local wrong = (hidden + 1) % 6
            if k == 1 then
                local wr = t.player.click_loc(holes[wrong + 1], 1)
                t.ticks(6)
                local rn = t.npc.nearest("soulbane_fear_reaper", 20)
                t.check("lookInsideHoles.wrong.empty", rn ~= "ok", "wrong hole " .. wrong .. " (hidden " .. hidden .. ") click=" .. tostring(wr) .. ", reaper lookup=" .. tostring(rn))
            end
            local lr = t.player.click_loc(holes[hidden + 1], 1)
            t.exec("lookInsideHoles" .. k, t.npc.await_present, "soulbane_fear_reaper", 20, 30)
            t.exec("killReaper" .. k, t.player.attack, "soulbane_fear_reaper", 2, 80)
            kill_wait("killReaper" .. k .. ".dead", fear, 200, 8, 50)
            t.ticks(4)
            local _, tally = t.var.server("varb2019_soulbane_fear_killedtally")
            t.check("killReaper" .. k .. ".tally", true, "fear tally=" .. tostring(tally) .. " hidden was " .. tostring(hidden))
        end
        t.exec("fearDone.drain", t.chat.drain, {})
        t.expect("quest.stage.fear_cleared", t.quest.expect_stage("fear_cleared"))
        margin_row("fearDone.margin", fear, "Fear room (five fear reapers)")
        t.exec("leaveFearRoom", t.player.click_loc, "soulbane_fwall_exit_multi", 1)
        t.ticks(4)
        t.exec("leaveFearRoom.drain", t.chat.drain, {})
        t.expect("quest.stage.confu_entered", t.quest.expect_stage("confu_entered"))
        local _, ct = t.world.tile()
        t.check("leaveFearRoom.tile", ct.x >= 3040 and ct.x <= 3071 and ct.z <= 5215, tostring(ct.x) .. "," .. tostring(ct.z))
        -- Confusion
        local _, sk0 = t.skill.snapshot()
        t.exec("hitIllusion", t.player.attack, "soulbane_confu_creeper_fake1", 2, 60)
        t.exec("hitIllusion.gone", t.npc.await_gone, "soulbane_confu_creeper_fake1", 20, 120)
        t.ticks(2)
        local _, hc = t.var.server("varb2037_soulbane_confu_hitcount1")
        t.check("hitIllusion.count", true, "illusion 1 hit counter after vanish=" .. tostring(hc))
        local _, sk1 = t.skill.snapshot()
        t.check("hitIllusion.xp", true, "attack xp before/after: " .. tostring(sk0.attack and sk0.attack.xp or sk0.attack) .. " / " .. tostring(sk1.attack and sk1.attack.xp or sk1.attack))
        local confu = new_fight()
        for w = 1, 5 do
            t.exec("killRealConfusionBeast" .. w, t.player.attack, "soulbane_confu_creeper", 2, 80)
            kill_wait("killRealConfusionBeast" .. w .. ".dead", confu, 200, 8, 50)
            t.ticks(5)
            local _, d1 = t.var.server("varb2014_soulbane_confu_door1pres")
            local _, d2 = t.var.server("varb2015_soulbane_confu_door2pres")
            local _, d3 = t.var.server("varb2016_soulbane_confu_door3pres")
            local _, d4 = t.var.server("varb2017_soulbane_confu_door4pres")
            local _, d5 = t.var.server("varb2018_soulbane_confu_door5pres")
            t.check("killRealConfusionBeast" .. w .. ".doors", true, "doors gone " .. tostring(d1) .. tostring(d2) .. tostring(d3) .. tostring(d4) .. tostring(d5))
        end
        t.exec("confuDone.drain", t.chat.drain, {})
        t.expect("quest.stage.confu_cleared", t.quest.expect_stage("confu_cleared"))
        margin_row("confuDone.margin", confu, "Confusion room (five real confusion beasts)")
        t.exec("leaveConfusionRoom", t.player.click_loc, "soulbane_door_multi6", 1)
        t.ticks(4)
        t.exec("leaveConfusionRoom.drain", t.chat.drain, {})
        t.expect("quest.stage.hope_entered", t.quest.expect_stage("hope_entered"))
        local _, ht = t.world.tile()
        t.check("leaveConfusionRoom.tile", ht.x >= 3074 and ht.x <= 3102 and ht.z <= 5214, tostring(ht.x) .. "," .. tostring(ht.z))
        -- Hopelessness: five creatures, three forms each
        local forms = { "soulbane_hope_monst3", "soulbane_hope_monst2", "soulbane_hope_monst1" }
        local hope = new_fight()
        for c = 1, 5 do
            for f = 1, 3 do
                t.exec("killHopeless" .. c .. "-form" .. f, t.player.attack, forms[f], 2, 80)
                kill_wait("killHopeless" .. c .. "-form" .. f .. ".dead", hope, 200, 8, 40)
                t.ticks(3)
            end
            local _, tally = t.var.server("varb2021_soulbane_hope_killedtally")
            t.check("killHopeless" .. c .. ".tally", true, "hopeless tally=" .. tostring(tally))
        end
        t.exec("hopeDone.drain", t.chat.drain, {})
        t.expect("quest.stage.hope_cleared", t.quest.expect_stage("hope_cleared"))
        margin_row("hopeDone.margin", hope, "Hopelessness room (five hopeless creatures, three forms each)")
        t.exec("bridge.var", t.var.expect, "varb2020_soulbane_hope_bridgepres", 1)
        t.ticks(4)
        local _, et = t.world.tile()
        t.check("bridgeCopy.tile", et.x >= 3010 and et.x <= 3038 and et.z <= 5214, "carried to the bridge copy at " .. tostring(et.x) .. "," .. tostring(et.z))
        t.player.walk_to(3021, 5192, 60)
        t.ticks(2)
        t.drive.camera(1024, 383, 700)
        t.exec("leaveHopelessRoom", t.player.click_loc, "soul_bane_hwall_void_exit", 1, { at = { 3020, 5188 } })
        t.ticks(4)
        t.exec("leaveHopelessRoom.drain", t.chat.drain, {})
        t.expect("quest.stage.tolna_entered", t.quest.expect_stage("tolna_entered"))
        local _, tt = t.world.tile()
        t.check("leaveHopelessRoom.tile", tt.level == 1 and tt.x >= 2967 and tt.x <= 2993, tostring(tt.x) .. "," .. tostring(tt.z) .. "," .. tostring(tt.level))
        -- Tolna's room: Brana's scene, then the three heads
        local heads = { "soulbane_final_tolna1", "soulbane_final_tolna2", "soulbane_final_tolna3" }
        t.exec("heads.present", t.npc.await_present, "soulbane_final_tolna1", 30, 20)
        -- The wiki (A Soul's Bane oldid 15292369, "The final room is a multicombat zone, and all
        -- three heads will attack at once") makes any head a legal target, and soulbane_final_tolna1..3
        -- carry forcemulti=yes (soulsbane.npc:259/289/319). Press each living head in turn and fight the
        -- one the server accepts -- in this multicombat room that is simply the first living head.
        local head_bits = { "varb2022_soulbane_final_tol1dead", "varb2023_soulbane_final_tol2dead", "varb2024_soulbane_final_tol3dead" }
        local tolna = new_fight()
        for h = 1, 3 do
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
        t.exec("talkToBrana", t.player.talk_to, "soulbane_brana", 1)
        t.exec("talkToBrana.drain", t.chat.drain, {})
        t.exec("talkToTolna.present", t.npc.await_present, "soulbane_tolna", 30, 20)
        t.player.walk_to(2981, 5212, 30)
        t.ticks(2)
        t.drive.camera(1024, 383, 700)
        t.exec("talkToTolna", t.player.talk_to, "soulbane_tolna", 1)
        t.exec("talkToTolna.drain", t.chat.drain, {})
        t.ticks(4)
        t.expect("quest.stage.tolna_surface", t.quest.expect_stage("tolna_surface"))
        local _, st = t.world.tile()
        t.check("talkToTolna.surface", st.level == 0 and st.x > 3290 and st.z < 3500, tostring(st.x) .. "," .. tostring(st.z) .. "," .. tostring(st.level))
        local lr = t.npc.nearest("soulbane_launa", 12)
        t.check("talkToTolna.launa-hidden", lr ~= "ok", "launa lookup=" .. tostring(lr))
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
