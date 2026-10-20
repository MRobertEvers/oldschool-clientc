-- _toa_party_doors: two raiders through a Nexus door one at a time, the way
-- Jagex runs it (docs/rsprox-analysis/toa/README.md, "Moving between rooms";
-- RSProx 1852, 1845): the member tries the Het door before the leader and is
-- told "Your leader, <name>, must go first."; the leader takes it and the
-- member reads "<name> has chosen to walk the Path of Het. Join him/her...";
-- the member then takes the same door and lands in the leader's instance.
-- Quick-Enter (op 2), as the parties in 1845 and 1852 cross: Enter (op 1)
-- asks "Do you wish to walk the Path of Het?" first.
-- Not a room test (the leading `_`).
local function said(t, text)
    for _, line in ipairs(t.raid.cox_messages(60)) do
        if line:find(text, 1, true) then return true end
    end
    return false
end

return {
    id = "_toa_party_doors",
    fixture = "fresh_lumbridge.ini",
    party = 2,
    setup = { "::setlevel hitpoints 99" },
    max_frames = 60000,
    run = function(t)
        local role = t.party.role()
        local names = t.party.names()
        if role == 1 then
            t.cheat("::toa 1", false)
        end
        t.expect("party.barrier.raid", t.party.barrier("raid", 300))
        if role == 2 then
            t.cheat("::toa 1", false)
        end
        t.ticks(4)
        t.expect("party.barrier.inside", t.party.barrier("inside", 300))
        if role == 2 then
            t.player.click_loc("toa_nexus_het_door", 2)
            t.ticks(3)
            local _, box = t.chat.text()
            box = tostring(box)
            t.expect("door.must_go_first", box:find("must go first", 1, true) and "ok" or "fail", box)
            t.chat.close()
        end
        t.expect("party.barrier.tried", t.party.barrier("tried", 300))
        if role == 1 then
            t.player.click_loc("toa_nexus_het_door", 2)
            t.ticks(8)
        end
        t.expect("party.barrier.leader_in", t.party.barrier("leader_in", 300))
        if role == 2 then
            t.expect("door.chosen_line", said(t, "has chosen to walk the Path of Het") and "ok" or "fail",
                "member told the leader's choice")
            t.player.click_loc("toa_nexus_het_door", 2)
            t.ticks(8)
        end
        t.expect("party.barrier.both_in", t.party.barrier("both_in", 300))
        local _, at = t.world.tile()
        t.expect("door.where", "ok", string.format("seat %d at %d,%d (%s)", role, at.x, at.z, names[role] or "?"))
        t.expect("party.barrier.done", t.party.barrier("done", 300))
    end,
}
