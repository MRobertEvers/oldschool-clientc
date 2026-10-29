-- bird_snare: laying, Reset and the catch at the Feldip hunting area's jungle birds,
-- as RSProx index 4426 recorded them (skill_hunter/scripts/bird_snare.rs2's header):
--   - the lay: the trap three ticks after the op, the player stepped off two ticks
--     after that, west first [369..374];
--   - Reset on a broken or full snare: the snare back, a full one's catch (5..10
--     feathers, raw bird meat, bones), then the snare laid again on its tile
--     [406..412], [580..585], [1164..1175];
--   - Aleck's Hunter Emporium on its Trade op (op5, [209]).
--
--   ./src/build_opt/torirsserver --scriptrun test/hunter/bird_snare.lua --bots 1 \
--       --name snare0 --ticks 2000 --session /tmp/snare --fixture test/quests/fixtures/fresh_lumbridge.ini

local function count(t, obj) local _, n = t.inv.count(obj) return tonumber(n) or 0 end

local function tile(t)
    local res, v = t.world.tile()
    if res == "ok" and type(v) == "table" then return v end
    return nil
end

return {
    id = "bird_snare",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", "::setlevel hunter 9", "::give hunting_ojibway_bird_snare 2",
    },
    run = function(t)
        t.ticklog.start()
        t.exec("goto", t.player.goto_tile, 2609, 2922, 0)
        t.ticks(3)

        t.ticklog.mark("lay")
        t.exec("lay.op", t.player.inv_op, "hunting_ojibway_bird_snare", 1)
        t.ticks(3)
        t.check("lay.trap", t.world.loc_near("hunting_ojibway_trap", 1) == "ok", "the trap stands on the lay tile three ticks on")
        t.ticks(3)
        local v = tile(t)
        t.check("lay.stepped_off", v ~= nil and v.x == 2608 and v.z == 2922,
            "stepped west to 2608,2922, read " .. (v and (v.x .. "," .. v.z) or "?"))
        t.check("lay.snares", count(t, "hunting_ojibway_bird_snare") == 1, "snares " .. count(t, "hunting_ojibway_bird_snare"))

        -- Wait for a bird or a miss; a miss is Reset and waited on again.
        local caught = false
        for round = 1, 6 do
            if caught then break end
            local state = nil
            for _ = 1, 200 do
                if t.world.loc_near("hunting_ojibway_trap_full_jungle", 3) == "ok" then state = "full" break end
                if t.world.loc_near("hunting_ojibway_trap_broken", 3) == "ok" then state = "broken" break end
                t.ticks(1)
            end
            t.check("wait." .. round, state ~= nil, "the snare resolved: " .. tostring(state))
            if state == nil then break end
            local feathers = count(t, "hunting_jungle_feather")
            local sym = state == "full" and "hunting_ojibway_trap_full_jungle" or "hunting_ojibway_trap_broken"
            t.ticklog.mark("reset." .. round .. "." .. state)
            t.exec("reset." .. round, t.player.click_loc, sym, 2)
            t.ticks(4)
            if state == "full" then
                local got = count(t, "hunting_jungle_feather") - feathers
                t.check("catch.feathers", got >= 5 and got <= 10, "feathers +" .. got .. " (5..10)")
                t.check("catch.meat", count(t, "spit_raw_bird_meat") >= 1, "raw bird meat " .. count(t, "spit_raw_bird_meat"))
                t.check("catch.bones", count(t, "bones") >= 1, "bones " .. count(t, "bones"))
                caught = true
            end
            t.ticks(6)
            t.check("relaid." .. round, t.world.loc_near("hunting_ojibway_trap", 3) == "ok", "the snare laid again where it stood")
        end
        t.check("caught", caught, "a crimson swift within six rounds")

        -- Aleck's Trade (op5).
        t.exec("goto-aleck", t.player.goto_tile, 2566, 3084, 0)
        t.exec("aleck.trade", t.player.talk_to, "hunting_shop_owner_yanille", 5)
        t.expect("aleck.shop", t.ui.await_open("shopmain", 5))
        t.finish(0)
    end,
}
