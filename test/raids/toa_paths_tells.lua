-- toa_paths_tells: Walk the Path's level-ups (docs/rsprox-analysis/toa/invocations.md, "Paths").
-- `::toa` enters, `::toainv 12` turns Walk the Path on, and each path's boss is completed in turn
-- (`::toabossdone`), Crondis first as most recordings did: the first levels two DIFFERENT other
-- paths, the second one of the two left, the third the last one, the fourth none (38 of 38
-- recorded raids); never a finished path. (The start level's own rumblings on the way in need the
-- lobby's entry, which toa_lobby_tells exercises.)
local function said_lines(t, n)
    return t.raid.cox_messages(n or 60)
end
-- The rumblings since the newest "Challenge complete" line, oldest first.
local function rumbles_since_complete(t)
    local out = {}
    for _, line in ipairs(said_lines(t, 40)) do
        if line:find("Challenge complete", 1, true) then break end
        local p = line:match("mysterious rumbling coming from the Path of (%a+)")
        if p then table.insert(out, 1, p) end
    end
    return out
end
local function complete(t, room)
    t.cheat("::toa " .. room, false)
    t.ticks(8)
    t.cheat("::toabossdone", false)
    t.ticks(3)
    return rumbles_since_complete(t)
end
local function has(list, x)
    for _, v in ipairs(list) do if v == x then return true end end
    return false
end
local function said(t, text)
    for _, line in ipairs(said_lines(t, 30)) do
        if line:find(text, 1, true) then return true end
    end
    return false
end

return {
    id = "toa_paths_tells",
    fixture = "fresh_lumbridge.ini",
    max_frames = 60000,
    setup = { "::setlevel hitpoints 99" },
    run = function(t)
        t.cheat("::toa 3", false)
        t.ticks(2)
        t.cheat("::toainv 12", false)
        t.ticks(1)
        t.expect("paths.walk_the_path", said(t, "Invocation 12 on.") and "ok" or "fail")
        t.ticks(6)
        t.cheat("::toabossdone", false)
        t.ticks(3)
        local first = rumbles_since_complete(t)
        t.expect("paths.first", (#first == 2 and first[1] ~= first[2] and not has(first, "Crondis")) and "ok" or "fail",
            "after Zebak: " .. table.concat(first, ","))
        local second = complete(t, 5)
        t.expect("paths.second", (#second == 1 and second[1] ~= "Crondis" and second[1] ~= "Scabaras") and "ok" or "fail",
            "after Kephri: " .. table.concat(second, ","))
        local third = complete(t, 7)
        t.expect("paths.third", (#third == 1 and third[1] == "Apmeken") and "ok" or "fail",
            "after Akkha: " .. table.concat(third, ","))
        local fourth = complete(t, 9)
        t.expect("paths.fourth", #fourth == 0 and "ok" or "fail", "after Ba-Ba: " .. table.concat(fourth, ","))
    end,
}
