-- _toa_lobby_party: two raiders form a party at the Grouping Obelisk and go in
-- (docs/rsprox-analysis/toa/lobby.md; RSProx 1845, 1852): the leader creates
-- it; the member opens it from the list and applies ("You have applied to join
-- the party of <leader>."); the leader accepts ("Your application to the party
-- of <leader> has been accepted."); the member at the door before the leader
-- ("Your leader, <leader>, must enter first."); the leader in, and the member
-- told ("<leader> has entered the Tombs of Amascut. Step inside to join
-- her/him..."); the member in, into the leader's Nexus.
-- Not a room test (the leading `_`).
local function said(t, text)
    for _, line in ipairs(t.raid.cox_messages(60)) do
        if line:find(text, 1, true) then return true end
    end
    return false
end

return {
    id = "_toa_lobby_party",
    fixture = "fresh_lumbridge.ini",
    party = 2,
    setup = { "::setlevel hitpoints 99" },
    max_frames = 60000,
    run = function(t)
        local role = t.party.role()
        local names = t.party.names()
        local leader = names[1] or "?"
        t.cheat("::tele 0_52_142_30_2" .. tostring(4 + role), false)
        t.ticks(3)
        t.player.click_loc("toa_grouping_board", 1)
        t.ui.await_open("toa_partylist", 20)
        if role == 1 then
            t.ui.press_pause("toa_partylist:contents", 1, "toa_partylist:myparty")
            t.ticks(2)
        end
        t.expect("party.barrier.formed", t.party.barrier("formed", 300))
        if role == 2 then
            -- The list again, now holding the leader's party on line 0.
            t.ui.press_pause("toa_partylist:contents", 0, "toa_partylist:refresh")
            t.ticks(2)
            t.ui.press_pause("toa_partylist:list", 0, "toa_partylist:list")
            t.ticks(2)
            t.ui.press_pause("toa_partydetails:pausebuttons", 4, "toa_partydetails:action")
            t.ticks(2)
            t.expect("party.applied", said(t, "You have applied to join the party of " .. leader .. ".") and "ok" or "fail")
        end
        t.expect("party.barrier.applied", t.party.barrier("applied", 300))
        if role == 1 then
            t.ui.press_pause("toa_partydetails:pausebuttons", 1, "toa_partydetails:refresh")
            t.ticks(2)
            t.ui.press_pause("toa_partydetails:pausebuttons", 36, "toa_partydetails:pausebuttons")
            t.ticks(2)
        end
        t.expect("party.barrier.accepted", t.party.barrier("accepted", 300))
        if role == 2 then
            t.expect("party.accepted", said(t, "Your application to the party of " .. leader .. " has been accepted.") and "ok" or "fail")
            local _, status = t.var.varbit("varb14345_toa_client_partystatus")
            t.expect("party.status", status == 1 and "ok" or "fail", tostring(status))
            t.ui.close_modal()
            t.player.click_loc("toa_lobby_raid_entry", 1)
            t.ticks(4)
            local _, box = t.chat.text()
            t.expect("party.must_enter_first", tostring(box):find("Your leader, " .. leader .. ", must enter first.", 1, true) and "ok" or "fail", tostring(box))
            t.chat.close()
        end
        t.expect("party.barrier.tried", t.party.barrier("tried", 300))
        if role == 1 then
            t.ui.close_modal()
            t.player.click_loc("toa_lobby_raid_entry", 1)
            t.ticks(4)
            t.chat.continue_()
            t.ticks(1)
            t.chat.choose("Yes.")
            t.ticks(4)
            t.expect("party.leader_in", said(t, "You enter the Tombs of Amascut") and "ok" or "fail")
        end
        t.expect("party.barrier.leader_in", t.party.barrier("leader_in", 300))
        if role == 2 then
            t.expect("party.told", said(t, leader .. " has entered the Tombs of Amascut. Step inside to join") and "ok" or "fail")
            local _, status = t.var.varbit("varb14345_toa_client_partystatus")
            t.expect("party.step_inside", status == 2 and "ok" or "fail", tostring(status))
            t.player.click_loc("toa_lobby_raid_entry", 1)
            t.ticks(4)
            t.chat.continue_()
            t.ticks(1)
            t.chat.choose("Yes.")
            t.ticks(4)
            t.expect("party.member_in", said(t, "You enter the Tombs of Amascut") and "ok" or "fail")
        end
        t.expect("party.barrier.both_in", t.party.barrier("both_in", 300))
        local _, at = t.world.tile()
        t.expect("party.where", "ok", string.format("seat %d at %d,%d", role, at.x, at.z))
        t.expect("party.barrier.done", t.party.barrier("done", 300))
    end,
}
