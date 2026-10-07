-- raid_agent / act: ACT.  A bot's intent for the tick -> the commands a
-- client would send (raid seam55 bot_runner, 2026-10-07).
--
-- An intent has one entry per channel, and the channels are what may happen
-- in one tick without stepping on each other: one move OR one attack (a walk
-- cancels a swing and a swing walks), any number of held-item ops (eat, drink,
-- wield), prayer presses, the special attack orb, a cheat.  Arbitration
-- inside a channel is the policy's; this only spells the packets.
--
--   intent = { walk = {x, z}, attack = npc_slot, op = { {op, obj, slot}, ... },
--              pray = { "piety", ... } (presses, each toggles), spec = true,
--              cheat = { "text", ... }, why = "short note" }

local Act = {}

Act.PRAYER_NUMBER = {
    thickskin = 1, burstofstrength = 2, clarityofthought = 3, rockskin = 4, superhumanstrength = 5,
    improvedreflexes = 6, rapidrestore = 7, rapidheal = 8, protectitem = 9, steelskin = 10,
    ultimatestrength = 11, incrediblereflexes = 12, protectfrommagic = 13, protectfrommissiles = 14,
    protectfrommelee = 15, retribution = 16, redemption = 17, smite = 18, sharpeye = 19, hawkeye = 20,
    eagleeye = 21, mysticwill = 22, mysticlore = 23, mysticmight = 24, rigour = 25, chivalry = 26,
    piety = 27, augury = 28, preserve = 29,
}
-- prayerbook:prayerN is component 541:8+N (quest_driver/prayer.lua)
function Act.prayer_uid(name)
    local n = Act.PRAYER_NUMBER[name]
    assert(n, "Act.prayer_uid: unknown prayer " .. tostring(name))
    return (541 << 16) | (8 + n)
end
-- The special attack orb (orbs:specbutton, interfaces/orbs.compack 36).
Act.SPEC_ORB_UID = (160 << 16) | 36

-- Lines for one bot, in the order the server should see them: held-item ops
-- first (a wield lands before the swing that needs it), then prayers, the
-- orb, then the one move or attack.
function Act.lines(pid, intent)
    assert(intent, "Act.lines: intent")
    local out = {}
    for _, c in ipairs(intent.cheat or {}) do out[#out + 1] = pid .. "\tcheat\t" .. c end
    for _, o in ipairs(intent.op or {}) do out[#out + 1] = pid .. "\topheld\t" .. o[1] .. "\t" .. o[2] .. "\t" .. o[3] end
    for _, name in ipairs(intent.pray or {}) do out[#out + 1] = pid .. "\tbutton\t" .. Act.prayer_uid(name) end
    if intent.spec then out[#out + 1] = pid .. "\tbutton\t" .. Act.SPEC_ORB_UID end
    if intent.walk ~= nil then
        out[#out + 1] = pid .. "\twalk\t" .. intent.walk.x .. "\t" .. intent.walk.z
    elseif intent.attack ~= nil then
        out[#out + 1] = pid .. "\topnpc\t2\t" .. intent.attack
    end
    return out
end

return Act
