-- _legs: the relay harness's own fixture (docs/quest_authoring/relay.md
-- "Checkpoints"). Not a quest -- the leading underscore keeps it out of
-- run.py --all and gate.py --all. Three short Lumbridge legs, each a
-- different kind of work: a TALK (Cook's Assistant's start, the stage moves
-- 0 -> 1), a BUY (the Lumbridge General Store, the backpack moves), a WALK
-- (the tile moves). The harness writes `leg.<k>.<name>` before each leg and
-- `::checkpoint k` after each all-PASS leg; `run.py _legs --from-leg 3`
-- resumes from checkpoint 2 and must end where the full run ends.
--
-- Rows are copied from test/quests/cooks_assistant.lua (the same talk and
-- the same shop, green there); the tiles are the same spawn rows it cites.

-- File-level constants are shared by every leg; local state is not.
local STORE_X, STORE_Z = 3209, 3247       -- generalshopkeeper1's *.spawn row (m50_50.spawn)
local WALK_X, WALK_Z = 3215, 3243         -- open ground south-east of the store's door

-- One reading of the three things a checkpoint must carry back: the tile, the
-- quest stage (server), the backpack. The same text in the full run and the
-- --from-leg run is the determinism proof.
local function end_state(t)
    local _, tile = t.world.tile()
    local _, stage = t.var.server("varp29_cookquest")
    local held = {}
    for slot = 0, 27 do
        local slot_result, cell = t.inv.slot(slot)
        if slot_result == "ok" and cell.name ~= "" and cell.count ~= 0 then
            held[#held + 1] = cell.name .. "x" .. cell.count
        end
    end
    return "tile=" .. tostring(tile and (tile.x .. "," .. tile.z .. "," .. tile.level))
        .. " stage=cookquest=" .. tostring(stage)
        .. " inv=" .. (#held > 0 and table.concat(held, ",") or "empty")
end

return {
    id = "_legs",
    fixture = "fresh_lumbridge.ini",
    setup = { "::cook" },
    bind = {
        varp = "varp29_cookquest",
        constants = { not_started = 0, started = 1, complete = 2 },
        row = "quest_cooksassistant",
        display = "Cook's Assistant",
        points = 1,
    },

    legs = {
        {
            name = "talk",
            run = function(t)
                t.ticks(3)
                t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))
                t.exec("legs.greet", t.player.talk_to, "cook")
                local d1r, d1d = t.chat.drain({ stop_at = "options" })
                t.expect("legs.drain_to_opener", d1r, d1d)
                t.exec("legs.choose_whats_wrong", t.chat.choose, "What's wrong?")
                local d2r, d2d = t.chat.drain({ stop_at = "options" })
                t.expect("legs.drain_to_offer", d2r, d2d)
                t.exec("legs.choose_help", t.chat.choose, "Yes, I'll help you.")
                local d3r, d3d = t.chat.drain({ stop_at = "none" })
                t.expect("legs.drain_close", d3r, d3d)
                t.expect("quest.stage.started", t.quest.expect_stage("started"))
            end,
        },
        {
            name = "buy",
            run = function(t)
                t.exec("legs.goto_store", t.player.goto_tile, STORE_X, STORE_Z, 0)
                t.exec("legs.shop_open", t.shop.open, "generalshopkeeper1", 3, "generalshop1")
                t.exec("legs.buy_bucket", t.shop.buy, "bucket_empty", 1)
                t.exec("legs.buy_pot", t.shop.buy, "pot_empty", 1)
                local close_result, close_detail = t.shop.close()
                t.check("legs.shop_close", close_result == "ok",
                    "shop.close -> " .. tostring(close_result) .. " " .. tostring(close_detail))
            end,
        },
        {
            name = "walk",
            run = function(t)
                local walk_result, walk_detail = t.player.walk_to(WALK_X, WALK_Z, 30)
                local _, at = t.world.tile()
                t.check("legs.walk", walk_result == "ok" and at and at.x == WALK_X and at.z == WALK_Z,
                    "walk_to " .. WALK_X .. "," .. WALK_Z .. " -> " .. tostring(walk_result) .. " "
                        .. tostring(walk_detail) .. "; at "
                        .. tostring(at and (at.x .. "," .. at.z .. "," .. at.level)))
                t.expect("legs.end_state", t.inv.expect_has("bucket_empty", 1), end_state(t))
                t.finish(0)
            end,
        },
    },
}
