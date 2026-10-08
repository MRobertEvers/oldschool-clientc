-- _vzbot: the Normal Verzik trio PLAYED BY THE BOT AGENT in three real clients
-- (owner 2026-10-07: "make the client runs run like the server ones").
--
-- The leader's embedded server runs the bot runner's drive
-- (torirs_server_botrun.c ToriRSServer_BotDriveStep) before every world tick:
-- the agent tools/raid_agent/run.lua verzik decides for all three players and
-- its commands enter as their packets.  The clients only draw it -- so this
-- script does nothing but wait for content to announce the end.
--
--   RAID_AGENT_ROOT=$PWD RAID_AGENT_PARTY=3 \
--   TORIRS_BOTDRIVE_AGENT="lua $PWD/tools/raid_agent/run.lua verzik" \
--   [QUEST_WATCH=1] python3 tools/raid_gate/run.py _vzbot --party 3
local role = (QD_PARTY and QD_PARTY.role) or 1
return {
    id = "_vzbot",
    fixture = "fresh_lumbridge.ini",
    party = 3,
    max_frames = 120000,
    setup = {},
    run = function(t)
        local verdict, seen = nil, ""
        for _ = 1, 400 do
            t.ticks(10)
            local _, ml = t.msg.last(20)
            for _, msg in ipairs(ml or {}) do
                local text = tostring(msg.text)
                if text:find("Verzik Vitur has fallen", 1, true) then verdict = "fallen" end
                if text:find("Your party has failed", 1, true) then verdict = "failed" end
                if text:find("You have died", 1, true) then seen = text end
            end
            if verdict ~= nil then break end
        end
        t.check("vzbot.verdict", verdict == "fallen", "p" .. role .. " " .. tostring(verdict) .. " " .. seen)
    end,
}
