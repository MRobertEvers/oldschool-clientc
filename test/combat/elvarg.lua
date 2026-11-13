-- elvarg: Dragon Slayer I's boss fought the way RSProx index 4558 (rev 242.1, world 386)
-- fights her, [5118]..[5175], for the tick log diff in
-- docs/rsprox-analysis/quests/dragon_slayer_i_elvarg.md. Run it on its own:
--
--   python3 tools/quest_gate/run.py --script test/combat/elvarg.lua --name elvarg_audit --no-publish
--
-- The recorder's kit (the --bas transcript's appearance at [5133]): robin hood hat, Ava's
-- accumulator, necklace of fangs, adamant knives (rapid), Saradomin d'hide body and chaps,
-- anti-dragon shield, combat bracelet, blessed Saradomin boots; his levels from the login
-- ([0]: attack 58, defence 40, strength 42, hitpoints 61, ranged 70, magic 50; prayer past
-- Eagle Eye's 44, a ranging potion drunk at [5114]). Stage 8, outside the lair's low wall.
--
-- The fight follows his: over the wall ([5121]), Protect from Melee and Eagle Eye up
-- ([5112]), four tiles in and throwing ([5125]..[5127]), Protect from Magic for melee after
-- her second breath ([5133]), throwing from three tiles until [5149], one step aside and a
-- shark ([5151], [5155]) so she walks in, throwing again beside her to the kill. Then the
-- death (her seq, the two area sounds, the west face, the body) and the head.
local function tile_text(t)
    local r, tt = t.world.tile()
    if r ~= "ok" or type(tt) ~= "table" then
        return tostring(r)
    end
    return tt.x .. "," .. tt.z .. "," .. tostring(tt.level)
end

return {
    id = "elvarg_audit",
    fixture = "fresh_lumbridge.ini",
    max_frames = 160000,
    setup = {
        "::clearinv",
        "::setlevel attack 58", "::setlevel defence 40", "::setlevel strength 42",
        "::setlevel hitpoints 61", "::setlevel ranged 70", "::setlevel prayer 45",
        "::setlevel magic 50",
        "::give robinhoodhat", "::give anma_30_reward", "::give necklace_of_fangs",
        "::give adamant_knife 400", "::give trail_saradomin_chest",
        "::give antidragonbreathshield", "::give trail_saradomin_chaps",
        "::give jewl_bracelet_of_combat_6", "::give blessed_boots_saradomin",
        "::give 4doserangerspotion", "::give 4doseprayerrestore 2", "::give shark 14",
        "::setvar varp176_dragonquest 8",
        "::goto 2844 9635 0",
    },
    run = function(t)
        for _, item in ipairs({ "robinhoodhat", "anma_30_reward", "necklace_of_fangs",
            "adamant_knife", "trail_saradomin_chest", "antidragonbreathshield",
            "trail_saradomin_chaps", "jewl_bracelet_of_combat_6", "blessed_boots_saradomin" }) do
            t.exec("equip." .. item, t.player.equip, item)
        end
        t.exec("drink.ranging", t.player.drink, "4doserangerspotion")
        t.exec("pray.melee", t.prayer.set, "protectfrommelee", true)
        t.exec("pray.eagleeye", t.prayer.set, "eagleeye", true)

        t.ticklog.start()
        t.ticklog.mark("elvarg.wall")
        t.exec("goto.wall", t.player.walk_to, 2845, 9635, 6)
        t.exec("climb", t.player.click_loc, "dragon_slayer_qip_stalagtite_jump", 1)
        t.ticks(3)
        t.check("climb.tile", select(2, t.world.tile()).x == 2847, "over the wall at " .. tile_text(t))

        -- [5125]..[5126] four tiles in, [5126] Attack, [5127] the first throw.
        t.ticklog.mark("elvarg.range")
        t.exec("walk.in", t.player.walk_to, 2851, 9635, 8)
        t.exec("attack.range", t.player.attack, "elvarg_alive", 2, 4)
        -- [5133] Protect from Magic once she has breathed twice.
        t.ticks(5)
        t.exec("pray.magic", t.prayer.set, "protectfrommagic", true)
        t.ticks(16)

        -- [5151] a step aside and no throw for five ticks: she closes in ([5153]).
        t.ticklog.mark("elvarg.step")
        t.exec("step.aside", t.player.walk_to, 2852, 9636, 4)
        t.ticks(3)
        t.exec("eat.shark", t.player.eat, { "shark" })
        t.ticks(1)

        t.ticklog.mark("elvarg.melee")
        -- Beside her now: the await re-presses Attack on the copy attack.range engaged.
        t.exec("kill", t.npc.await_dead_engaged, 400, 30, { eat = { item = "shark", below = 30 } })

        -- K+1..K+26: her death (elvarg.rs2 ~elvarg_death), the stage and the head
        -- (quest_dragon.rs2 [queue,dragon_elvarg_slain]).
        t.ticklog.mark("elvarg.death")
        t.exec("stage", t.var.await, "varp176_dragonquest", 9, 14)
        t.ticks(30)
        t.check("head", select(2, t.inv.count("dragon_slayer_qip_elvargs_head")) == 1,
            "head count " .. tostring(select(2, t.inv.count("dragon_slayer_qip_elvargs_head"))))
        -- The spam lines of the fight (the tick log has no message rows): every breath
        -- sends the shield's, and the prayer's while Protect from Magic is up.
        local shield, prayer = 0, 0
        local lr, lines = t.msg.last(100)
        if lr == "ok" and type(lines) == "table" then
            for i = 1, #lines do
                local text = tostring(lines[i].text)
                if text == "Your shield absorbs most of the dragon fire." then
                    shield = shield + 1
                elseif text == "Your prayers resist some of the dragon fire!" then
                    prayer = prayer + 1
                end
            end
        end
        t.check("lines.ranged", shield > 0 and prayer > 0,
            "shield lines " .. shield .. ", prayer lines " .. prayer)

        -- Leg 2, a melee fight beside her for the claw and the AP/OP chain: her
        -- respawn (50 ticks after the npc goes, elvarg.rs2 ~elvarg_death_gone) at stage 8,
        -- a bronze sword so the fight lasts, Protect from Magic kept up.
        t.cheat("::setvar varp176_dragonquest 8")
        t.cheat("::give bronze_sword")
        t.ticks(2)
        t.exec("equip.sword", t.player.equip, "bronze_sword")
        -- His prayer ran dry in leg 1; restore it so Protect from Magic holds.
        t.exec("drink.restore", t.player.drink, "4doseprayerrestore")
        t.ticks(2)
        t.exec("drink.restore2", t.player.drink, "3doseprayerrestore")
        t.exec("pray.magic2", t.prayer.set, "protectfrommagic", true)
        t.ticklog.mark("elvarg.respawn")
        t.exec("respawn", t.npc.await_present, "elvarg_alive", 16, 70)
        t.ticks(2)
        t.ticklog.mark("elvarg.melee2")
        t.exec("attack.melee", t.player.attack, "elvarg_alive", 2, 6)
        t.exec("kill.melee", t.npc.await_dead_engaged, 900, 60, { eat = { item = "shark", below = 25 } })
        t.ticks(10)
        t.expect("elvarg.done", "ok", "fight and death recorded at " .. tile_text(t))
    end,
}
