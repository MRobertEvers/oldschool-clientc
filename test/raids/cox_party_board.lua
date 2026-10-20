-- cox_party_board: two raiders form a party at the Recruiting board on Mount
-- Quidamortem and go down the steps into ONE raid (cox_party.rs2;
-- docs/rsprox-analysis/cox/quidamortem.md). The leader makes the party and
-- advertises it; the member opens it from the party list ("View party") and
-- joins; the leader goes down and the member is called ("Your party has entered
-- the dungeons! Come and join them now.", RSProx 820 [2324]) and follows, landing
-- in the leader's start room as a member, with the party of two on both panels.
--
-- GREEN: both raiders in the same instance, the member not the leader.
--
--   ./src/build_<you>/torirsserver --scriptrun test/raids/cox_party_board.lua --bots 2 \
--       --name pb1 --ticks 3000 --session /tmp/partyboard --fixture test/raids/fixtures/fresh_lumbridge.ini

local LIST = "raids_lobby_partylist"
local DETAILS = "raids_lobby_partydetails"

return {
    id = "cox_party_board",
    fixture = "fresh_lumbridge.ini",
    party = 2,
    frames = 40000,
    setup = { "::maxstats", "::tele 0_19_55_26_44" },
    run = function(t)
        local role, size = t.party.role(), t.party.size()
        local function barrier(name)
            local r, d = t.party.barrier(name, 600)
            t.check("party.barrier." .. name, r == "ok", tostring(d))
            return r == "ok"
        end
        local function press(iface, button, sub)
            local r = t.ui.press_pause(iface .. ":contents", sub, iface .. ":" .. button)
            t.ticks(2)
            return r
        end
        local function sv(name)
            local r, v = t.var.server(name)
            return r == "ok" and v or nil
        end
        t.check("party.size", size == 2, "raiders " .. tostring(size))
        if role == 1 then
            t.check("party.ticklog", t.ticklog.start() == "ok", "")
        end
        t.ticks(2)
        t.player.click_loc("raids_party_recruitment", 1, { at = { 1246, 3562 } })
        t.check("party.list", t.ui.await_open(LIST, 20) == "ok", "")
        if role == 1 then
            press(LIST, "myparty", 1)
            t.check("party.made", t.ui.await_open(DETAILS, 10) == "ok", "")
            press(DETAILS, "advertise", 1)
        end
        if not barrier("advertised") then return t.finish(1) end
        if role ~= 1 then
            -- the party list again, now with the leader's row; View party on it
            press(LIST, "refresh", 0)
            local vr = t.ui.press_pause(LIST .. ":list", 0, LIST .. ":00")
            t.ticks(2)
            t.check("party.view", vr == "ok" and t.ui.await_open(DETAILS, 10) == "ok", tostring(vr))
            press(DETAILS, "join", 0)
            local mine = sv("varp1427_raids_party_groupholder")
            t.check("party.joined", mine ~= nil and mine > 0, tostring(mine))
            t.ui.close_modal()
        end
        if not barrier("joined") then return t.finish(1) end
        if role == 1 then
            t.ui.close_modal()
            t.player.click_loc("raids_entrance_steps", 1)
            local cr, cd = t.chat.play({ "mesbox:WARNING!", "options", "choose:Accept warning and proceed." })
            t.check("party.leader_in", cr == "ok", tostring(cd))
            t.ticks(4)
        end
        if not barrier("leader_down") then return t.finish(1) end
        if role ~= 1 then
            t.check("party.called", t.msg.expect("Your party has entered the dungeons! Come and join them now.") == "ok", "")
            t.player.click_loc("raids_entrance_steps", 1)
            local cr, cd = t.chat.play({ "mesbox:WARNING!", "options", "choose:Accept warning and proceed." })
            t.check("party.member_in", cr == "ok", tostring(cd))
            t.ticks(4)
            local hr, here = t.raid.cox_here()
            t.check("party.member_room", hr == "ok" and here.name == "raid_start", tostring(hr))
            t.check("party.member_not_leader", sv("varb5423_raids_client_isleader") == 0, "")
            t.check("party.member_sees_two", sv("varb5424_raids_client_partysize") == 2,
                tostring(sv("varb5424_raids_client_partysize")))
        end
        if not barrier("both_down") then return t.finish(1) end
        -- one raid: each raider sees the other in the start room
        local seen = t.party.see(nil, 20, 20)
        t.check("party.same_raid", seen == "ok", tostring(seen))
        t.finish(0)
    end,
}
