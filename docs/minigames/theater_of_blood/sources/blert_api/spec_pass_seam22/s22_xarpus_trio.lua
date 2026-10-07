-- seam22 tob_normal_trio_findings (2): Xarpus Normal, a party of three, phase 2's chain.
-- Every raider stands on its own tile and steps one tile on a square every 4 ticks (no one
-- stands in acid for long); the leader reads the world's tick log: for every spit (an acidspit
-- 1555 whose source is not a landed tile) how many orbs are thrown FROM its landing tile within
-- 12 ticks (the spec row xarpus.p2.chain_count), and where they go (offline: an_ours_xarpus.py).
return {
    id = "s22_xarpus_trio",
    fixture = "fresh_lumbridge.ini",
    party = 3,
    max_frames = 30000,
    setup = {
        "::clearinv",
        "::setlevel hitpoints 99", "::setlevel defence 99", "::setlevel prayer 99",
        "::give anglerfish 20",
    },
    run = function(t)
        local role = t.party.role()
        if role == 1 then
            local lr, lg = t.ticklog.start()
            t.check("ticklog", lr == "ok", tostring(lg))
        end
        local er, ed = t.raid.enter("tob", "xarpus", { mode = "normal" })
        t.check("enter", er == "ok", "p" .. role .. " " .. tostring(ed))
        t.expect("party.barrier.entrance", t.party.barrier("entrance", 300))
        local fight
        if role == 1 then
            local fr
            fr, fight = t.raid.start_tile()
            t.player.walk_to(fight.x, fight.z - 3, 20)
            local cr, cd = t.player.click_loc("tob_arena_barrier", 1)
            t.check("barrier.click", cr == "ok", tostring(cd))
            local pr, pd = t.chat.play({ "options", "choose:Yes, begin the fight." })
            t.check("barrier.begin", pr == "ok", tostring(pd))
        end
        t.expect("party.barrier.started", t.party.barrier("started", 400))
        if role ~= 1 then
            local xr, xd = t.player.click_loc("tob_arena_barrier", 1)
            t.check("barrier.cross", xr == "ok", "p" .. role .. " " .. tostring(xd))
        end
        t.party.allow_death("scratch s22_xarpus_trio: standing in the arena to be spat at")
        -- a measurement of HIS orbs, not of survival: no raider may die before phase 2 is sampled
        t.cheat("::god 1")
        t.ticks(2)
        local _, at0 = t.world.tile()
        -- three stations, west / centre / east of where the raiders came in
        local base_x, base_z = at0.x + (role - 2) * 4, at0.z + 4
        local square = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } }
        local k = 0
        local start = select(2, t.tick())
        while select(2, t.tick()) < start + 220 do
            k = k + 1
            local s = square[(k % 4) + 1]
            t.player.walk_to(base_x + s[1] * 2, base_z + s[2] * 2, 3)
            local _, hp = t.skill.read("hitpoints")
            if hp and hp.level and hp.level < 60 then t.player.eat("anglerfish") end
            t.ticks(3)
        end
        t.expect("party.barrier.p2_done", t.party.barrier("p2_done", 400))
        if role == 1 then
            local _, pj = t.ticklog.rows({ kind = "projectile" })
            local spits, chains, landed = {}, {}, {}
            for _, r in ipairs(pj or {}) do
                if r.spotanim == 1555 then
                    local key = tostring(r.src_x) .. "," .. tostring(r.src_z)
                    if landed[key] then chains[#chains + 1] = r else spits[#spits + 1] = r end
                    landed[tostring(r.dst_x) .. "," .. tostring(r.dst_z)] = true
                end
            end
            local per, seq = {}, {}
            for i, s in ipairs(spits) do
                local n = 0
                for _, c in ipairs(chains) do
                    if c.src_x == s.dst_x and c.src_z == s.dst_z and c.tick > s.tick and c.tick <= s.tick + 12 then n = n + 1 end
                end
                per[n] = (per[n] or 0) + 1
                seq[#seq + 1] = tostring(n)
            end
            local txt = {}
            for v = 0, 6 do if per[v] then txt[#txt + 1] = v .. "x" .. per[v] end end
            t.check("chain.per_spit", #spits > 0, #spits .. " spits, " .. #chains .. " chained orbs; orbs from each spit's landing tile: " .. table.concat(txt, " ") .. "; in order " .. table.concat(seq, ","))
            local ok = #spits > 3 and seq[1] == "1"
            for i = 2, #seq do if seq[i] ~= "2" then ok = false end end
            t.check("spec.xarpus.p2.chain_count", ok, "first spit 1, every later spit 2 (wiki Strategies :836): " .. table.concat(seq, ","))
        end
        t.expect("party.barrier.done", t.party.barrier("done", 200))
    end,
}
