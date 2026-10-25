-- Vampyre Slayer: the Count's regenerate, then the stake. A targeted test for
-- the branch the quest gate never takes (test/quests/vampire.lua brings the
-- hammer). RSProx index 4373 [496]/[497]: with a stake and no hammer, zero
-- hitpoints is answered a tick later with "You're unable to push the stake far
-- enough in!", "The vampyre seems to regenerate!" and a heal of 20, and he
-- fights on; the recorder then left by the stairs ([509..511]) and came back
-- with a hammer (the stake is test/quests/vampire.lua's).

return {
    id = "vampire_regenerate",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        "::give stake 1",
        "::give garlic 1",
        "::give rune_scimitar 1",
        "::give adamant_platebody 1",
        "::give adamant_platelegs 1",
    },

    run = function(t)
        t.ticklog.start()
        local setvar_result = t.cheat("::setvar varp178_vampire 2")
        t.check("setup.stage", setvar_result == "ok", "::setvar varp178_vampire 2 -> " .. tostring(setvar_result))

        -- Into the manor and down the crypt stairs (vampire.lua's route).
        t.exec("goto-manor", t.player.goto_tile, 3108, 3351, 0)
        t.exec("manor.door", t.player.pass_door, { closed = "haunteddoorl", open = "haunteddoorl_inactive",
            at = { 3108, 3353, 0 }, near = { 3108, 3352 }, far = { 3108, 3354 } })
        t.exec("manor.hallDoor", t.player.pass_door, { closed = "draynor_panelled_door",
            open = "draynor_panelled_door_open", at = { 3109, 3358, 0 }, near = { 3109, 3357 }, far = { 3109, 3359 } })
        t.exec("manor.wingDoor", t.player.pass_door, { closed = "draynor_panelled_door",
            open = "draynor_panelled_door_open", at = { 3106, 3368, 0 }, near = { 3106, 3368 }, far = { 3106, 3369 } })
        t.exec("manor.toStairs", t.player.walk_route,
            { { 3106, 3369 }, { 3113, 3370 }, { 3114, 3362 }, { 3113, 3356 }, { 3115, 3356 } }, { level = 0 })
        t.exec("crypt.down", t.player.click_loc, "cryptstairsdown", 1, { at = { 3115, 3357, 0 } })
        local landed = t.await({
            level = function()
                local r, tt = t.world.tile()
                return r == "ok" and tt.x % 64 == 6 and tt.z % 64 == 43 and tt.x >= 6400
            end,
            note = "the crypt copy's landing (6,43)",
        }, 10)
        t.check("crypt.landed", landed == "ok", "await the copy's landing -> " .. tostring(landed))
        if landed ~= "ok" then
            t.blocked("the crypt stairs did not land in a copy")
            return
        end
        t.exec("equip.weapon", t.player.equip, "rune_scimitar")
        t.exec("equip.body", t.player.equip, "adamant_platebody")
        t.exec("equip.legs", t.player.equip, "adamant_platelegs")

        t.exec("openCoffin", t.player.click_loc, "vampcoffin", 1)
        t.check("draynor.present", t.npc.await_present("count_draynor", 10, 25))
        t.exec("attack", t.player.attack, "count_draynor", 2, 15)

        -- No hammer: he regenerates and stays.
        t.exec("regenerate.noHammer", t.msg.await, "unable to push the stake far enough in", 80)
        t.check("regenerate.line", t.msg.expect("The vampyre seems to regenerate"))
        t.ticks(3)
        local alive_result = t.npc.nearest("count_draynor", 10)
        t.check("regenerate.alive", alive_result == "ok",
            "count_draynor within 10 tiles three ticks after the regenerate: " .. tostring(alive_result))
        local stage_result = t.var.await_server("varp178_vampire", 2, 1)
        t.check("regenerate.stage", stage_result == "ok", "stage still 2: " .. tostring(stage_result))

        -- Out by the stairs, as the recorder did ([509..511]); the copy and the
        -- Count go with the move (vampire_crypt_stairs.rs2).
        local r, here = t.world.tile()
        local sx, sz = here.x - here.x % 64, here.z - here.z % 64
        t.exec("crypt.toStairs", t.player.walk_to, sx + 5, sz + 43, 30)
        t.exec("crypt.up", t.player.click_loc, "cryptstairsup", 1, { at = { sx + 5, sz + 40, 0 } })
        local out = t.await({
            level = function()
                local rr, tt = t.world.tile()
                return rr == "ok" and tt.x == 3115 and tt.z == 3356
            end,
            note = "the east wing 3115,3356",
        }, 10)
        t.check("crypt.left", out == "ok", "await the east wing -> " .. tostring(out))
        t.finish(0)
    end,
}
