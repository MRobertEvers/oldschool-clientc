-- Darkness of Hallowvale -- driven as a 6-leg relay (docs/quest_authoring/relay.md).
-- Leg 1 = guide steps 1-11 (climbOverBrokenWall .. talkToCitizen).
-- Notes: docs/quests/ladders/darknessofhallowvale.notes.md.
-- Readings for row details (file-level helpers, no state shared between legs).
local function at(t)
    local _, p = t.world.tile()
    return tostring(p.x) .. "," .. tostring(p.z) .. "," .. tostring(p.level)
end
local function said(t, n)
    local _, l = t.msg.last(n)
    local o = {}
    for i = 1, #l do
        local m = l[i]
        o[#o + 1] = type(m) == "table" and tostring(m.text or m.message or m[1]) or tostring(m)
    end
    return table.concat(o, " | ")
end

return {
    id = "darknessofhallowvale",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::complete quest_inaidofthemyreque", -- guide prerequisite: In Aid of the Myreque
        "::darknessofhallowvale", -- stages the quest at the top (doh_shared.rs2:410), never past it
        "::give hammer 1", -- guide: usePlankOnBoat items (Hammer)
        "::give woodplank 2", -- guide: usePlankOnBoat/usePlankOnChute items (Plank, one each)
        "::give nails 8", -- guide: usePlankOnBoat/usePlankOnChute items (four nails each)
        "::setlevel construction 5", -- requirement: Construction 5
        "::setlevel mining 20", -- requirement: Mining 20
        "::setlevel thieving 22", -- requirement: Thieving 22
        "::setlevel agility 26", -- requirement: Agility 26
        "::setlevel crafting 32", -- requirement: Crafting 32
        "::setlevel magic 33", -- requirement: Magic 33
        "::setlevel strength 40", -- requirement: Strength 40
    },
    bind = {
        varp = "varb2573_myq3_main_quest",
        constants = {
            not_started = 0,
            started = 10,
            boat_fixed = 20,
            chute_fixed = 30,
            arrived_wall = 40,
            floor_kicked = 50,
            ral_directions = 60,
            complete = 320,
        },
        row = "quest_darknessofhallowvale",
        display = "Darkness of Hallowvale",
        points = 2,
    },

    legs = {
        {
            name = "burgh_to_citizen",
            run = function(t)
                -- LEG 1 BEGIN: climbOverBrokenWall
                t.ticks(3)
                t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

                t.exec("goto-climbOverBrokenWall", t.player.goto_tile, 3491, 3230, 0)
                t.exec("climbOverBrokenWall", t.player.click_loc, "burgh_inn_climb_over", 1)
                t.ticks(4)
                t.check("climbOverBrokenWall.reading", true, "tile " .. at(t) .. " messages: " .. said(t, 1))
                t.blocked("content_bug: loc burgh_inn_climb_over (Broken wall, op1 Climb-over, placed at 3491,3230,0 in maps/m54_50.jl2:1516) has no [oploc1,burgh_inn_climb_over] handler in any OSRS-Content/osrs239-content/server/scripts/*.rs2 (grep finds only the loc config at configs/all.loc:142551), so the click answers 'Nothing interesting happens.' and the player never crosses the wall; the trapdoor at 3490,3232 (guide step enterBurghPubBasement) is then unreachable ('I can't reach that!' from every approach tile)")
                return
                -- LEG 1 END
            end,
        },
    },
}
