-- master_farmer: Draynor's Master Farmer offered Pickpocket and did nothing
-- before 2026-11-17. Now the Master Farmer row (pickpocket.dbrow) and its seed
-- table (master_farmer.rs2): every successful pick leaves exactly one kind of
-- seed more in the pack, and the line is the recorded one (RSProx 1099).
return {
    id = "master_farmer",
    fixture = "fresh_lumbridge.ini",
    max_frames = 40000,
    setup = {
        "::clearinv",
        "::setlevel thieving 99",
        "::setlevel farming 99",
        "::setlevel hitpoints 99",
        "::goto 3079 3249 0",
    },
    run = function(t)
        t.ticks(3)
        local picked = 0
        for _ = 1, 12 do
            t.player.talk_to("master_farmer_1", 3)
            t.ticks(4)
            if t.msg.expect("You pick the Master Farmer's pocket.") == "ok" then
                picked = picked + 1
            end
            if picked >= 3 then break end
        end
        t.check("picked", picked >= 3, "successful picks " .. tostring(picked))
        local _, slot0 = t.inv.slot(0)
        t.check("seed", slot0 ~= nil and slot0.name ~= nil and string.find(slot0.name, "seed") ~= nil,
            "slot 0 " .. tostring(slot0 and slot0.name))
    end,
}
