-- tfa_ocellus: Ocellus, solo, for the tick log and the wire log, to diff
-- against RSProx index 4301 and 4308 (docs/rsprox-analysis/guthixian_archive/
-- ocellus_plan.md):
--
--   the lobby, a party, the room; ::tfaroom straight to Ocellus's room
--   up the hall; Talk-to: his dialogue, "Yes.": the start, the bar and its
--   meter, the song
--   phase 1: two corners (volleys, seven cracks, the explosion), the middle
--   at 66% (four beats, the strike), a corner, the middle at a quarter
--   ::tfaboss 0: "You... will disappear...", the change to phase 2
--
--   ./src/build_tfa_opt/torirsserver --scriptrun test/guthixian_archive/tfa_ocellus.lua --bots 1 \
--       --name tfa0 --ticks 9000 --session /tmp/tfa --fixture test/raids/fixtures/fresh_lumbridge.ini
local function now(t) local _, k = t.tick() return k or -1 end

local function in_lobby(t)
    local r, tile = t.world.tile()
    return r == "ok" and tile.x >= 4096 and tile.x < 4160 and tile.z >= 5504 and tile.z < 5568
end

local function present(t, sym)
    local r = t.npc.nearest(sym, 40)
    return r == "ok"
end

local EAT = { eat = { item = "shark", below = 50 } }

local function fight(t, sym, ticks, stop, safe)
    local deadline = now(t) + ticks
    while now(t) < deadline do
        t.cheat("::tfaheal", false)
        if safe then
            -- Phase 2 asks nothing of the bot but where it stands.
            t.cheat("::tfaocsafe", false)
            if stop and stop() then return true end
            t.ticks(1)
            goto continue
        end
        if stop and stop() then return true end
        if present(t, sym) then
            t.player.attack(sym, 2, 1, EAT)
        elseif not stop then
            return true
        end
        t.ticks(1)
        ::continue::
    end
    return false
end

-- Phase 4: shoot him, take the orb and keep off the void floor (::tfaocorb
-- walks; sent after the attack so its walk wins), health only when low.
local function fight4(t, ticks)
    local deadline = now(t) + ticks
    while now(t) < deadline do
        local hr, hv = t.skill.read("hitpoints")
        if hr == "ok" and type(hv) == "table" and (hv.level or 99) < 50 then
            t.cheat("::tfaheal", false)
        end
        if present(t, "tfa_ocellus_p3") then
            t.player.attack("tfa_ocellus_p3", 2, 1, EAT)
        end
        t.cheat("::tfaocorb", false)
        t.ticks(1)
    end
end

-- 4301's health through phase 3, per mille of his, by the tick from his
-- change P: a quadrant falls when his health comes within 1% of the next
-- quarter, which one bow alone would take a thousand ticks to reach.
local P3_PERMILLE = { {0,922}, {10,920}, {12,914}, {16,910}, {18,903}, {21,898}, {22,897}, {23,894}, {24,886}, {26,885}, {28,880}, {31,870}, {33,861}, {34,856}, {36,854}, {41,844}, {44,835}, {46,830}, {48,822}, {49,816}, {53,808}, {58,807}, {59,806}, {62,798}, {65,790}, {67,785}, {71,779}, {73,768}, {77,765}, {78,761}, {82,758}, {83,754}, {87,749}, {91,745}, {93,742}, {111,739}, {113,737}, {122,733}, {124,732}, {125,727}, {127,722}, {132,711}, {136,710}, {140,709}, {145,699}, {146,690}, {149,683}, {150,676}, {151,671}, {155,670}, {160,666}, {167,665}, {170,656}, {174,655}, {175,651}, {179,650}, {188,649}, {191,648}, {192,643}, {195,641}, {198,637}, {199,631}, {202,624}, {205,614}, {206,607}, {209,595}, {210,593}, {215,592}, {218,589}, {220,585}, {222,583}, {223,582}, {225,581}, {228,580}, {231,579}, {232,577}, {233,575}, {236,569}, {238,564}, {239,556}, {241,549}, {243,538}, {244,532}, {246,528}, {247,527}, {248,526}, {249,522}, {251,517}, {254,512}, {255,511}, {256,508}, {259,505}, {260,501}, {262,500}, {264,496}, {265,491}, {268,486}, {270,484}, {273,481}, {275,476}, {278,472}, {279,471}, {303,463}, {305,457}, {317,456}, {320,437}, {328,435}, {333,434}, {334,426}, {336,404}, {337,403}, {345,402}, {355,392}, {361,391}, {368,389}, {369,388}, {370,381}, {373,374}, {380,358}, {381,357}, {388,352}, {392,342}, {394,341}, {396,337}, {400,336}, {405,326}, {406,325}, {409,315}, {412,314}, {419,305}, {420,304}, {424,286}, {428,276}, {434,272}, {435,262}, {440,259}, {450,258}, {451,257}, {456,253}, {462,248} }

local function p3_permille(k)
    local pm = P3_PERMILLE[1][2]
    for _, row in ipairs(P3_PERMILLE) do
        if row[1] > k then break end
        pm = row[2]
    end
    return pm
end

-- Phase 3: shoot him, and leave a quadrant before it falls. With `P`, his
-- health 4301's at P+k (a cheat lands on the next tick: the next tick's).
local function fight3(t, ticks, P)
    local function replay()
        if P and present(t, "tfa_ocellus_p3") then
            local k = now(t) - P
            if k >= 6 then
                t.cheat("::tfaboss " .. tostring(math.floor(5250 * p3_permille(k + 1) / 1000)), false)
            end
        end
    end
    local deadline = now(t) + ticks
    while now(t) < deadline do
        replay()
        t.cheat("::tfaheal", false)
        t.cheat("::tfaocsafe", false)
        if present(t, "tfa_ocellus_p3_void_vine") then
            t.player.attack("tfa_ocellus_p3_void_vine", 2, 1, EAT)
        elseif present(t, "tfa_ocellus_p3") then
            t.player.attack("tfa_ocellus_p3", 2, 1, EAT)
        end
        replay()
        t.ticks(1)
    end
end

return {
    id = "tfa_ocellus",
    fixture = "fresh_lumbridge.ini",
    max_frames = 900000,
    setup = {
        "::clearinv",
        "::complete quest_whileguthixsleeps",
        "::setvar varb16354_wgs_tfa_shortcut_unlocked 1",
        "::setvar varb16426_tfa_miniquest 2",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        "::setlevel ranged 99",
        "::setlevel magic 99",
        "::setlevel prayer 99",
        "::give bow_of_faerdhinen 1",
        "::wield bow_of_faerdhinen",
        "::give black_dragonhide_body 1",
        "::wield black_dragonhide_body",
        "::give shark 20",
        "::goto 4128 5556 0",
    },
    run = function(t)
        t.player._death_fence = function() return false end
        t.ticklog.start()
        t.ticks(2)
        t.player.click_loc("tfa_lobby_raid_entry", 1, { at = { 4123, 5562, 0 } })
        t.chat.choose("Form or join a party.")
        t.check("partylist.open", t.ui.await_open("tfa_partylist", 10))
        t.ticks(1)
        t.ui.press_pause("tfa_partylist:contents", 1)
        t.check("partydetails.open", t.ui.await_open("tfa_partydetails", 10))
        t.ticks(1)
        t.player.click_loc("tfa_lobby_raid_entry", 1, { at = { 4123, 5562, 0 } })
        local ok = t.await({ level = function() return not in_lobby(t) end, note = "into the room" }, 20)
        t.check("room.enter", ok == "ok", "still in the lobby")
        t.ticks(10)
        -- Straight to Ocellus (stage 4: the Guardian and three more beaten).
        t.cheat("::tfaroom 6 4", false)
        ok = t.await({ level = function() local r, tl = t.world.tile(); return r == "ok" and tl.level == 1 end, note = "the hall" }, 30)
        t.ticks(4)
        -- Up the hall to the arena (4301 [877..892]: straight north).
        local _, tile = t.world.tile()
        t.player.walk_to(tile.x, tile.z + 29, 30)
        ok = t.await({ level = function() return present(t, "tfa_ocellus_intro") end, note = "ocellus" }, 30)
        t.check("ocellus.room", ok == "ok", "no Ocellus")
        -- The arena's middle: the intro stands at mid + (-1,-4).
        local _, intro = t.npc.nearest("tfa_ocellus_intro", 40)
        local mid = { x = intro.x + 1, z = intro.z + 4 }
        t.ticks(6)
        t.ticklog.mark("proceed")
        local r, d = t.player.talk_to("tfa_ocellus_intro", 1)
        -- The Hooded Figure's first words (the wiki's transcript), then the
        -- question; Yes begins the fight.
        t.exec("ocellus.dialogue", t.chat.drain, { stop_at = "options" })
        t.chat.choose("Yes.")
        ok = t.await({ level = function() return present(t, "tfa_ocellus_p1") end, note = "phase 1" }, 40)
        t.check("ocellus.start", ok == "ok", tostring(r) .. " " .. tostring(d))
        t.prayer.set("protectfrommissiles", true)
        t.ticklog.mark("phase1")
        fight(t, "tfa_ocellus_p1", 70)
        t.ticklog.mark("mid66")
        t.cheat("::tfaboss 1600", false)
        fight(t, "tfa_ocellus_p1", 70)
        t.ticklog.mark("mid25")
        t.cheat("::tfaboss 600", false)
        fight(t, "tfa_ocellus_p1", 90)
        t.ticklog.mark("zero")
        -- Held at his last point each tick: the solo bot never blocks a line,
        -- so every closing line heals him 2 (a line is blocked only with every
        -- raider on it), faster than its shots take it off.
        fight(t, "tfa_ocellus_p1", 40, function()
            if present(t, "tfa_ocellus_p2") then return true end
            t.cheat("::tfaboss 0", false)
            return false
        end)
        t.check("ocellus.phase2", present(t, "tfa_ocellus_p2"), "no phase 2")
        -- Phase 2: The Blade takes the hits; three rounds of portals; phase 3
        -- 131 ticks after the change.
        t.ticklog.mark("phase2")
        t.prayer.set("protectfrommissiles", false)
        fight(t, "tfa_ocellus_p2_sword_hitbox", 150, function() return present(t, "tfa_ocellus_p3") end, true)
        t.check("ocellus.phase3", present(t, "tfa_ocellus_p3"), "no phase 3")
        -- Phase 3: three quadrant segments, each ended by its destruction (his
        -- health replayed from 4301's).
        t.ticklog.mark("phase3")
        local P = now(t)
        t.prayer.set("protectfrommelee", true)
        fight3(t, 30, P)
        -- The censer of the quadrant the bot fights in, lit from where it
        -- stands (NE Bandosian, NW Zarosian, SW Armadylean, SE Saradomin).
        local _, me = t.world.tile()
        local sx = me.x >= mid.x and 1 or -1
        local sz = me.z >= mid.z and 1 or -1
        local censer = (sx > 0 and sz > 0 and "tfa_ocellus_artifact_damage_melee")
            or (sx < 0 and sz > 0 and "tfa_ocellus_artifact_spec")
            or (sx < 0 and "tfa_ocellus_artifact_damage_ranged")
            or "tfa_ocellus_artifact_heal"
        -- The rift kinds by the second segment's, not the roll: Void Rifts,
        -- then the healing rifts.
        t.cheat("::tfaocrifts 1", false)
        t.ticklog.mark("censer")
        r, d = t.player.click_loc(censer, 1, { at = { mid.x + 11 * sx, mid.z + 11 * sz, 1 }, from_here = true })
        t.check("ocellus.censer", r == "ok", censer .. " " .. tostring(r) .. " " .. tostring(d))
        -- U the censer lights, U+1 the glow, U+3 it is spent.
        t.ticks(4)
        local lr = t.world.loc_near(censer .. "_inactive", 16)
        t.check("ocellus.censer_spent", lr == "ok", censer .. "_inactive not there")
        fight3(t, 490, P)
        -- Phase 4: two cycles of 60, then the kill. The bot takes its orb as
        -- it comes ready and is healed only when low, so the orb's heal shows.
        t.ticklog.mark("phase4")
        fight4(t, 125)
        t.ticklog.mark("kill")
        t.cheat("::tfaboss 1", false)
        -- Long enough to outlast his deflect against missiles (D+31..52):
        -- the bow does nothing under it.
        fight3(t, 40)
        t.check("ocellus.line", t.msg.expect("Challenge complete: Ocellus."))
        t.check("ocellus.total", t.msg.expect("The Fractured Archive: Total completion time:"))
        ok = t.await({ level = function() return present(t, "tfa_blade_npc_vault_entry") end, note = "the blade" }, 30)
        t.check("ocellus.blade", ok == "ok", "no Blade to the vault")
        t.ticks(10)
        -- Travel: the vault.
        t.player.talk_to("tfa_blade_npc_vault_entry", 3)
        ok = t.await({ level = function() return present(t, "tfa_rewards_chest_vault_1_closed") end, note = "the vault" }, 30)
        t.check("ocellus.vault", ok == "ok", "not in the vault")
        t.ticks(4)
        t.ticklog.mark("end")
        t.expect("tfa_ocellus.run", "ok", string.format("ended on tick %d", now(t)))
    end,
}
