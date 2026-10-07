-- _play_normal: the WHOLE Theatre of Blood, NORMAL MODE, a trio, end to end
-- through the PLAY LIBRARY (raid seam39 play_tob_normal_relay).  An
-- underscore harness, not a kept room.  Run with --party 3 (run.py,
-- seed_survey.py); it declares no `party` field, as _play_verzik does, so a
-- copy declaring `party = 3` is what party_repeat.py runs.
--
-- It is _play_entry.lua (the Entry solo relay) with three raiders in
-- lockstep: the lobby the way _party_smoke.lua phase B forms a Normal party
-- (the notice board, t.party.form / apply / accept, the door's ready check,
-- t.party.follow_in); then each room is the pre-fight of its own trio
-- harness's party branch (_play_maiden.lua party_run, _play_bloat.lua,
-- _play_nylocas.lua size > 1, _play_sotetseg.lua trio_run, _play_xarpus.lua
-- trio_run, _play_verzik.lua party_run) and ONE t.raid.play per raider with
-- the room's Normal plan and the seat's role (raid_play_tob_<room>.lua roles:
-- Maiden p1 tank / p2 freezer / p3 ranger; Nylocas p1 mage / p2 ranger / p3
-- melee; Verzik p1 takes the Dawnbringer).  The leader starts every room and
-- walks every way out: the cleared barrier, the corridor, the passage
-- (tob_raid.rs2 ~tob_carry_party: whoever walks the passage carries every
-- raider standing in the old room's square into the next), the Dawnbringer
-- from the skeleton after Xarpus (E:214), Verzik's trapdoor (tob_vault.rs2)
-- and the reward chest.  Every raider then goes down the trapdoor too.
--
-- No ::tob* room cheat: t.raid.enter is not called.  Readers only:
-- t.raid.state (::tobstate), ::tobjail (the raid's death counter) and the
-- tick log, all on the leader.
--
-- THE SUPPLY CHEST.  In Normal it is a points store (tob_chest.rs2
-- [oploc1,tob_midway_chest_closed]: bandages are Entry's only; Normal opens
-- tob_midway_stores, a points store: 10-13 points a deathless chest,
-- tob.constant ^tob_chest_points_above_*), and it stands AFTER Bloat; the
-- relay does not shop (reported).  ~tob_restore heals after a boss in every
-- mode on this content, but only the raider the room's watchdog script runs
-- for (tob_raid.rs2 ~tob_room_cleared): the leader.  The members eat up in
-- the corridor (top_up).
local role = (QD_PARTY and QD_PARTY.role) or 1
local size = (QD_PARTY and QD_PARTY.size) or 1
assert(size == 3, "_play_normal is the Normal TRIO relay: run it with --party 3")

local LOBBY_X, LOBBY_Z = 3662, 3216

-- THE TRIO'S KIT.  One worn set for the raid, the switches each seat's rooms
-- name, and the rest supplies.  Sources, by room:
--  * worn: the Maiden trio's ranged set ("Everyone will be ranging in this
--    room", 10Boot yt_4i4lv-srJkw.md 0:07:06; _play_maiden.lua party_kit),
--    the bow Verzik's P2/P3 is shot with (raid_play_tob_verzik.lua), insulated
--    boots (E:94; _play_verzik.lua party_kit "the zap 48 -> 25") and the
--    Entry relay's zenyte amulet in the free neck slot;
--  * every seat: the Dragon warhammer (Maiden's opener "everyone should drop a
--    dragon warhammer spec", 10Boot 0:06:33; Bloat's run-by, W:687; Xarpus'
--    plan swings it when held), a loaded toxic blowpipe (Maiden's rangers,
--    10Boot 0:08:14; the Nylocas ranger, W:717), the scythe (Bloat, Sotetseg,
--    Xarpus, Verzik P1: W:891), a charged serpentine helm (the Athanatos
--    "has to be hit with poison or venom", W:923), the whip (the Nylocas
--    plan's melee loadout, raid_play_tob_nylocas.lua :131), super combats
--    (the melee rooms' boost; the Bloat plan re-sips on a walk);
--  * seat 1, the leader (Maiden's tank, the Nylocas ranger): the Ayak for
--    the mage bigs (trio guide :216) and nothing else, so it carries the
--    most food;
--  * seat 2 (Maiden's freezer, the Nylocas meleer): the freezer's +140 magic
--    set and Ice Barrage's runes (W:594, W:603; raid_play_tob_maiden.lua
--    :57), the meleer's Sanguinesti staff (trio guide :386), the magic
--    shortbow and rune arrows (the Nylocas plan's ranged loadout, :132);
--  * seat 3 (Maiden's north ranger, the Nylocas mage): the Eye of Ayak
--    (W:719), the shortbow and its arrows, Ice Burst's runes (the Nylocas
--    kit: _play_nylocas.lua).
-- The Ayak and the Sanguinesti staff are charged in run() by their own
-- Charge op (_play_nylocas.lua), then the supplies are handed over into the
-- slots the charges freed: brews and super restores (10Boot 0:04:23
-- "eight brews, four restores, and three anglers"; E:30 "one super restore
-- per three brews") and anglerfish, about as many as brews: the library
-- eats a fish and drinks a brew in one tick when a hit can outrun one alone
-- (raid_play.lua _play_supplies, combo eating), and a brew-only pack (11
-- brews, 1 fish: n4normal) left the Maiden tank at 26 hitpoints for six
-- ticks, then a 42; a fish a seat fewer than this (n1normal) ran the tank
-- dry at t576.
local KIT = {
    "::clearinv",
    "::setlevel attack 99", "::setlevel strength 99", "::setlevel defence 99",
    "::setlevel ranged 99", "::setlevel magic 99", "::setlevel hitpoints 99",
    "::setlevel prayer 99", "::setlevel agility 99", "::setlevel slayer 37",
    "::setvar varb4070_spellbook 1",
}
-- owner_tob_normal 2026-10-07: ONE WHOLE-RAID INVENTORY PER SEAT, the
-- Strategies page's Normal "Example setups"
-- (wiki_Theatre_of_Blood_Strategies.wikitext:292-520: the Melee, the
-- Mage/Freezer and the Range roles) -- gear switches, a few fish, one brew,
-- one restore, a divine and a super combat -- topped up from the supply
-- chest after Bloat and after Sotetseg with each raider's own points
-- (restock()).  The page's items are kept where our plans use them and
-- swapped for the plan's tool where they differ: the Tonalztics (our
-- Maiden opener) for the elder maul / BGS, the zaryte crossbow for the
-- freezer's Evoke, Dinh's bulwark (our 50-wave stack) for the halberd.
-- The seats: seat 1 (the party leader, orb 0) Maiden's freezer, the
-- Nylocas mage, Bloat role 1; seat 2 Maiden's dps1, the Nylocas ranger
-- (the page's Range role: chinchompas, W:717), Bloat role 3; seat 3
-- Maiden's dps2, the Nylocas meleer (the page's Melee role: a second melee
-- weapon for the room, W:723 -- sulphur blades -- and the whip), Bloat
-- role 2.
local MAIDEN_ROLE_OF_SEAT = { [1] = 2, [2] = 1, [3] = 3 }
local mrole = MAIDEN_ROLE_OF_SEAT[role]
if mrole == 2 then
    for _, c in ipairs({
        -- worn: the Maiden freezer's opener set (_play_maiden.lua party_kit)
        "::give twisted_bow", "::wield twisted_bow",
        "::give dragon_arrow 1000", "::wield dragon_arrow",
        "::give game_pest_archer_helm", "::wield game_pest_archer_helm",
        "::give elite_void_knight_top", "::wield elite_void_knight_top",
        "::give elite_void_knight_robes", "::wield elite_void_knight_robes",
        "::give pest_void_knight_gloves", "::wield pest_void_knight_gloves",
        "::give avas_assembler", "::wield avas_assembler",
        -- (Blert's Maiden freezers, build/blert_maiden scale 3, 26 seats: avernic
        -- treads max on 26 of 26 and the ultor ring on 20 at the room start,
        -- worn through every barrage, bow shot and scythe swing -- the boots and
        -- ring every melee room wants too, worn not carried)
        "::give avernic_treads_max", "::wield avernic_treads_max",
        "::give ultor_ring", "::wield ultor_ring",
        "::give occult_necklace", "::wield occult_necklace",
        -- the pack: the freezer's magic set, the scythe, the specials, runes
        "::give kodai_wand", "::give ancestral_hat", "::give ancestral_robe_top", "::give ancestral_robe_bottom", "::give arcane",
        "::fullscythe",
        "::give tonalztics_of_ralos_uncharged", "::give sunfiresplinter 100",
        "::give zaryte_xbow", "::give xbows_crossbow_bolts_adamantite_tipped_ruby_enchanted 50",
        "::give eye_of_ayak_uncharged", "::give demon_tear 2000",
        "::give water_rune 2000", "::give blood_rune 1000", "::give death_rune 1000",
        "::give necklace_of_rupture", "::give lotr_crystalshard_necklace_upgrade",
        "::give elder_maul", "::give dragon_warhammer",
        -- (seat 1 starts Bloat -- its role 1 must be the leader -- and swings
        -- the scythe there: owner_rooms4's snippet plays every Bloat seat in
        -- ::tobkitsalve's melee armour; relay15's seat 1 swung in void, Bloat
        -- 198-200 ticks against [75-195].  The torva helm and the oathplate
        -- body ride in the pack, in the slots of the divine ranging potion this
        -- seat never drinks and the splinters / demon tears the charges leave)
        "::give torva_helm", "::give radiant_oathplate_chest", "::give radiant_oathplate_legs",
        -- (and the melee set's amulet of rancour and ferocious gloves:
        -- relay24's Sotetseg missed on 35% of the party's hits against the
        -- harness's 20%, 3000 hp over 323 ticks against 221 -- seat 1 swung
        -- the scythe there in occult, void gloves, magus ring and Ava's.  The
        -- slots: its plain super combat -- the divine's four doses cover
        -- Bloat, Nylocas, Sotetseg and Xarpus -- and its Maiden fish, which
        -- it has not eaten in a relay run yet)
        "::give amulet_of_rancour", "::give ferocious_gloves",
        "::give saturated_heart", "::give 4doserangerspotion",
        "::give 4dosedivinecombat" }) do KIT[#KIT + 1] = c end
else
    for _, c in ipairs({ "::tobkit", "::blowpipe dragon_dart 2000 2000",
        "::give twisted_bow", "::wield twisted_bow",
        "::give dragon_arrow 1000", "::wield dragon_arrow",
        "::give tonalztics_of_ralos_uncharged", "::give sunfiresplinter 100",
        "::give game_pest_archer_helm", "::give elite_void_knight_top", "::give elite_void_knight_robes",
        "::give pest_void_knight_gloves", "::give necklace_of_rupture",
        "::give lotr_crystalshard_necklace_upgrade",
        "::give dinhs_bulwark", "::give dragon_claws",
        "::give elder_maul", "::give dragon_warhammer",
        -- (owner_verzik's slow snippet: seats 2 and 3 are the noxious halberd
        -- seats at her P3 -- the Blert team that reaches her ball)
        "::give noxious_halberd",
        -- (the Nylocas plan's magic weapon for EVERY role: owner_nylocas,
        -- relay19 -- seats 2 and 3 held the bow through all 60 ticks of
        -- Vasilias' magic form with no target, her phase 139 ticks against the
        -- harness's 91, and seat 1 took 112 of her damage against Blert's 31.5)
        "::give eye_of_ayak_uncharged", "::give demon_tear 2000",
        "::give 4dose2combat", "::give 4dosedivinecombat",
        "::give 4doserangerspotion", "::give 4dosedivinerange" }) do KIT[#KIT + 1] = c end
    if role == 2 then KIT[#KIT + 1] = "::give chinchompa_black 300" end
    if role == 3 then
        KIT[#KIT + 1] = "::give abyssal_whip"
        KIT[#KIT + 1] = "::give sulphur_blades"
    end
end
-- the food and the brew / restore, handed over after the charges free their
-- slots (run()); the chests top them up
local SUPPLIES = nil
-- (the freezer's fifth fish is the salve amulet's slot: its pack is full,
-- and the salve is what makes Bloat green in its room harness --
-- owner_rooms4's relay snippet DIFFERENCES 1; the freezer ate 4 at Maiden)
-- (owner_rooms4's Sotetseg and Xarpus snippets, coordinator 2026-10-07:
-- every seat also carries the elder maul -- Sotetseg's specials, each seat
-- owns two -- and the dragon warhammer -- Xarpus' specials, W:839 -- and
-- seats 2 and 3 owner_verzik's noxious halberd.  The slots: the stamina
-- (relay11's tick log: run energy never under 78 percent through
-- Sotetseg; the chest sells it), Dizana's quiver NOT carried (a convenience:
-- one bow volley's ranged bonus at Sotetseg's opener), and fish, the food
-- the chest after Bloat sells -- the brews kept, 64 Hitpoints a slot to a
-- fish's 22 (relay12 cut a brew and two fish a seat as well: every seat
-- reached Bloat with 0-1 fish and died there).  Food still comes first,
-- the brew after it (library 112c9eb06).)
if mrole == 2 then
    SUPPLIES = { "::give br_4dosepotionofsaradomin 1", "::give br_4dose2restore 2" }
elseif role == 2 then
    SUPPLIES = { "::give br_4dosepotionofsaradomin 2", "::give br_4dose2restore 2", "::give anglerfish 5" }
else
    SUPPLIES = { "::give br_4dosepotionofsaradomin 2", "::give br_4dose2restore 2", "::give anglerfish 4" }
end

-- What each seat leaves on the floor once no later room uses it (the Entry
-- relay's drop rule: the pack keeps room for the Dawnbringer, W:875).
local DROP_AFTER = {
    -- (Maiden-only pieces; the freezer keeps its barrage runes for the
    -- Nylocas mage)
    maiden = { [1] = { "kodai_wand", "ancestral_hat", "ancestral_robe_top", "ancestral_robe_bottom", "arcane", "tonalztics_of_ralos_charged",
        -- (the Tonalztics both ways -- its special spends the charge, and
        -- relay21's seats still held the uncharged one and its splinters)
        "tonalztics_of_ralos_uncharged", "sunfiresplinter", "zaryte_xbow", "xbows_crossbow_bolts_adamantite_tipped_ruby_enchanted" },
        -- (seats 2 and 3: the ranging potion's last doses too -- their Nylocas
        -- boost is the divine ranging potion -- so the chest after Bloat has
        -- one more slot for food)
        [2] = { "tonalztics_of_ralos_charged", "tonalztics_of_ralos_uncharged", "sunfiresplinter", "dinhs_bulwark", "3doserangerspotion", "2doserangerspotion", "1doserangerspotion" },
        [3] = { "tonalztics_of_ralos_charged", "tonalztics_of_ralos_uncharged", "sunfiresplinter", "dinhs_bulwark", "3doserangerspotion", "2doserangerspotion", "1doserangerspotion" } },
    bloat = { [1] = { "lotr_crystalshard_necklace_upgrade" }, [2] = { "lotr_crystalshard_necklace_upgrade" },
        [3] = { "lotr_crystalshard_necklace_upgrade" } },
    -- (and after the Nylocas every seat's room-only pieces: the Ayak, the
    -- ranging potions -- no later room drinks one -- and seats 2 and 3's
    -- blowpipe; the slots go to the Sotetseg chest's food)
    nylocas = {
        [1] = { "eye_of_ayak", "waterrune", "bloodrune", "deathrune", "saturated_heart",
            "1doserangerspotion", "2doserangerspotion", "3doserangerspotion", "4doserangerspotion" },
        [2] = { "chinchompa_black", "eye_of_ayak", "toxic_blowpipe_loaded",
            "1dosedivinerange", "2dosedivinerange", "3dosedivinerange", "4dosedivinerange" },
        [3] = { "abyssal_whip", "sulphur_blades", "eye_of_ayak", "toxic_blowpipe_loaded",
            "1dosedivinerange", "2dosedivinerange", "3dosedivinerange", "4dosedivinerange" },
    },
}

local SUPPLY_ITEMS = {
    anglerfish = "angler", mantaray = "angler", seaturtle = "angler", shark = "angler",
    ["4dosepotionofsaradomin"] = "brew", ["3dosepotionofsaradomin"] = "brew",
    ["2dosepotionofsaradomin"] = "brew", ["1dosepotionofsaradomin"] = "brew",
    ["4dose2restore"] = "restore", ["3dose2restore"] = "restore",
    ["2dose2restore"] = "restore", ["1dose2restore"] = "restore",
    br_4dosepotionofsaradomin = "brew", br_3dosepotionofsaradomin = "brew",
    br_2dosepotionofsaradomin = "brew", br_1dosepotionofsaradomin = "brew",
    br_4dose2restore = "restore", br_3dose2restore = "restore",
    br_2dose2restore = "restore", br_1dose2restore = "restore",
    ["4dose2combat"] = "combat", ["3dose2combat"] = "combat",
    ["2dose2combat"] = "combat", ["1dose2combat"] = "combat",
}
local DOSES = {
    ["4dosepotionofsaradomin"] = 4, ["3dosepotionofsaradomin"] = 3, ["2dosepotionofsaradomin"] = 2, ["1dosepotionofsaradomin"] = 1,
    ["4dose2restore"] = 4, ["3dose2restore"] = 3, ["2dose2restore"] = 2, ["1dose2restore"] = 1,
    br_4dosepotionofsaradomin = 4, br_3dosepotionofsaradomin = 3, br_2dosepotionofsaradomin = 2, br_1dosepotionofsaradomin = 1,
    br_4dose2restore = 4, br_3dose2restore = 3, br_2dose2restore = 2, br_1dose2restore = 1,
    ["4dose2combat"] = 4, ["3dose2combat"] = 3, ["2dose2combat"] = 2, ["1dose2combat"] = 1,
}

local R = {}
local ORDER = { "maiden", "bloat", "nylocas", "sotetseg", "xarpus", "verzik" }
local PLAN = { maiden = "tob_maiden", bloat = "tob_bloat", nylocas = "tob_nylocas",
    sotetseg = "tob_sotetseg", xarpus = "tob_xarpus", verzik = "tob_verzik" }
local P = "p" .. role .. " "

-- THE ROLES PER ROOM (opts.role, raid_play.lua): the plan's role number each
-- seat plays.  The seat is fixed by its kit, the role by the room.
--  * Maiden: seat = role.  Her tank (role 1) takes nearly every blackstorm
--    (_play_maiden.lua tech.tank "p1=26 p3=1"; 10Boot 0:06:33 "The person
--    closest to the boss becomes the tank and will take the most damage"; the
--    harness's tank ate 12 and drank 28), so the tank is the seat whose other
--    rooms need the fewest switches: the Nylocas ranger (the pipe every seat
--    carries and an Ayak).  With seat 3 tanking (n2normal) she cast at seat
--    1 as often: two raiders ate like tanks.
--  * Nylocas: seat 1 the ranger, seat 2 (the freezer's) the meleer, seat 3
--    (the north ranger's) the mage.
--  * Bloat: role 1 (the raider in the room on the first walk: it crosses and
--    starts the fight when Bloat is on the far side, does the Defence-drain
--    run-by, W:687, and runs beside a running Bloat while the others wait
--    for the first down, W:689) is seat 3, the seat with the most fish left
--    after Maiden.  That opener costs about 120 hitpoints in ten ticks in the
--    room harness too (s39bloat, _play_bloat run here: 19, 20, 10, 8, 10, 7,
--    12, 7, 9, 8, 12; it lives on 14 fish, 9 eats and 11 drinks).  The
--    leader as role 1 died on that walk with 0 fish and 4-5 brew doses
--    (t909, t891, t1028); seat 2 with 0 fish and 16 brew doses (t672, t687).
-- raid seam53: Bloat role 1 is the seat Maiden leaves the most: seat 2, her
-- freezer (survey1, own name, after Maiden: seat 1 0 fish / 2 brew doses / 0
-- restore / 0 combat, seat 2 0 / 14 / 6 / 4, seat 3 0 / 11 / 6 / 0; the
-- freezer is not a melee seat, so the Maiden plan's re-boost never drinks its
-- combat dose).  Role 1 is the heaviest Bloat role in its own harness
-- (seam51 _play_bloat: role 1 10 eats 9 drinks, role 2 7 and 8, role 3 8 and
-- 6); survey1's leader died at Bloat as role 3 on 2 brew doses, so seat 1
-- keeps role 3 and seat 3 takes role 2.
-- owner_tob_normal 2026-10-07: Maiden by the green harness (seat 1 freezer),
-- the Nylocas by the green harness (seat 1 mage, 2 ranger, 3 meleer --
-- relay_nylocas_snippet.lua), Bloat role 1 (the heaviest) to the seat Maiden
-- leaves the most: the freezer, now seat 1.
local ROLE_IN = { maiden = { [1] = 2, [2] = 1, [3] = 3 }, nylocas = { [1] = 1, [2] = 2, [3] = 3 },
    bloat = { [1] = 1, [2] = 3, [3] = 2 } }

local function origin_of(t)
    local _, here = t.world.tile()
    return math.floor(here.x / 64) * 64, math.floor(here.z / 64) * 64, here
end

local function supplies(t)
    local s = { angler = 0, brew = 0, restore = 0, combat = 0 }
    for item, kind in pairs(SUPPLY_ITEMS) do
        local r, n = t.inv.count(item)
        if r == "ok" and type(n) == "number" and n > 0 then
            s[kind] = s[kind] + n * (DOSES[item] or 1)
        end
    end
    return s
end

local function supplies_text(s)
    return string.format("anglerfish %d, brew doses %d, restore doses %d, combat doses %d", s.angler, s.brew, s.restore, s.combat)
end

local function used_text(a, b)
    return string.format("anglerfish %d, brew doses %d, restore doses %d, combat doses %d",
        a.angler - b.angler, a.brew - b.brew, a.restore - b.restore, a.combat - b.combat)
end

local function newest_serial(t)
    local r, list = t.msg.last(1)
    if r == "ok" and type(list) == "table" and #list > 0 then return list[#list].serial or 0 end
    return 0
end

local function line_since(t, since, needle)
    local r, list = t.msg.last(80)
    if r ~= "ok" or type(list) ~= "table" then return nil end
    local found = nil
    for i = 1, #list do
        if (list[i].serial or 0) > since and string.find(list[i].text, needle, 1, true) then found = list[i].text end
    end
    return found
end

local function plain(text)
    return (string.gsub(string.gsub(tostring(text), "<br>", " "), "<[^>]*>", ""))
end


-- The leader: the raid's room register reads `name` (with its boss in the
-- pool; the Nylocas has no boss row) and the leader stands still.
local function await_room(t, name, budget)
    local _, start = t.tick()
    local last_line = "none"
    while true do
        local r, st = t.raid.state()
        if r == "ok" then
            last_line = tostring(st.line)
            if st.room == name and not st.cleared and (name == "nylocas" or st.boss_slot ~= nil) then
                local _, a = t.world.tile()
                t.ticks(2)
                local _, b = t.world.tile()
                if a.x == b.x and a.z == b.z then return "ok", st end
            end
        end
        local _, now = t.tick()
        if now - start > budget then return "timeout", "no " .. name .. " room within " .. budget .. " ticks; last state " .. last_line end
        t.ticks(1)
    end
end

-- The leader walks the way out (_play_entry.lua leave_room, PASSAGE_SIDE).
local PASSAGE_SIDE = { maiden = { 40, 7 }, bloat = { 6, 31 }, nylocas = { 38, 51 }, sotetseg = { 15, 6 }, xarpus = { 33, 47 } }

local function leave_room(t, name, next_name, ox, oz)
    local exit_loc = (name == "xarpus") and "tob_dungeon_xarpus_arena_door_exit" or "tob_dungeon_walkway_exit_clickbox"
    local xr, xd = t.player.click_loc(exit_loc, 1)
    t.ticks(3)
    local ar, ad = await_room(t, next_name, 20)
    if ar ~= "ok" then
        local side = PASSAGE_SIDE[name]
        local wr = t.player.walk_to(ox + side[1], oz + side[2], 40)
        xd = tostring(xd) .. "; walked to its side " .. tostring(wr)
        ar, ad = await_room(t, next_name, 20)
    end
    t.check(name .. ".passage", ar == "ok", "clicked " .. exit_loc .. " (" .. tostring(xr) .. " " .. string.sub(tostring(xd), 1, 160)
        .. "); next room: " .. (ar == "ok" and tostring(ad.line) or tostring(ad)))
    return ar == "ok"
end

local function hits_between(t, a, b)
    local _, rows = t.ticklog.rows({ kind = "hit_player" })
    t.ticks(1)
    local per = {}
    for _, h in ipairs(rows or {}) do
        if h.tick >= a and h.tick <= b and (h.damage or 0) > 0 then
            local k = h.pid or -1
            per[k] = per[k] or { taken = 0, n = 0, big = 0 }
            per[k].taken = per[k].taken + h.damage
            per[k].n = per[k].n + 1
            if h.damage > per[k].big then per[k].big = h.damage end
        end
    end
    local parts = {}
    for pid, v in pairs(per) do parts[#parts + 1] = string.format("pid %s: %d in %d hits (largest %d)", tostring(pid), v.taken, v.n, v.big) end
    table.sort(parts)
    return table.concat(parts, "; ")
end

local function prayers_off(t, name)
    local r, d, set = t.prayer.read()
    local off = {}
    if r == "ok" and type(set) == "table" then
        for prayer, lit in pairs(set) do
            if lit then
                t.prayer.set(prayer, false)
                off[#off + 1] = prayer
            end
        end
    end
    local r2, d2 = t.prayer.read()
    t.check(name .. ".prayers_off", r2 == "ok" and string.find(tostring(d2), ": 0 of ", 1, true) ~= nil,
        P .. "switched off [" .. table.concat(off, " ") .. "]; now " .. string.sub(tostring(d2), 1, 120))
end

-- The empty vials every drink leaves (relay21: 2-4 slots a seat after Bloat,
-- when the chest's food had none left): on the floor after every room.
local VIALS = { "vial_empty", "br_vial_empty" }
local function drop_vials(t, name)
    local n = 0
    for _, v in ipairs(VIALS) do
        for _ = 1, 28 do
            local cr, c = t.inv.count(v)
            if cr ~= "ok" or (tonumber(c) or 0) == 0 then break end
            t.player.drop(v)
            n = n + 1
        end
    end
    t.check(name .. ".drop_vials", true, P .. "empty vials left on the floor: " .. n)
end
local function drop_spent(t, name)
    drop_vials(t, name)
    local per = DROP_AFTER[name]
    local list = per and per[role] or nil
    if list == nil then return end
    local dropped = {}
    for _, item in ipairs(list) do
        local wr, worn_n = t.inv.count(item)
        if wr == "ok" and worn_n == 0 then t.player.unequip(item) end
        local r = t.player.drop(item)
        dropped[#dropped + 1] = item .. "=" .. tostring(r)
    end
    t.check(name .. ".drop_spent", true, P .. "left on the floor: " .. table.concat(dropped, " "))
end

-- Put a switch on unless it is already worn (none of it in the backpack).
local function wear(t, row, item)
    local cr, n = t.inv.count(item)
    if cr == "ok" and n == 0 then
        t.check(row, true, P .. item .. " not in the backpack (worn since an earlier room)")
        return
    end
    local er, ed = t.player.equip(item)
    t.check(row, er == "ok", P .. tostring(er) .. " " .. string.sub(tostring(ed), 1, 120))
end

-- The combat tab's style slot (0 accurate/the melee rooms; 1 rapid for the
-- bow and the blowpipe: _play_maiden.lua party_run, _play_nylocas.lua) and
-- auto retaliate (off in the Nylocas only: _play_nylocas.lua).
-- The style by its NAME on the combat tab (ui.style reads the four buttons'
-- text and presses the one that shows it; seam54: a slot number pressed for
-- every seat put the scythe seats on Chop, stab in our content).
local function set_style(t, row, name)
    local result, detail = t.ui.style(name)
    t.check(row, result == "ok", P .. tostring(detail))
end

local function set_retaliate(t, row, off)
    local _, before = t.var.varp("varp172_option_nodef")
    local want = off and 1 or 0
    if before ~= want then
        -- (the button is the combat tab's and lands only while that tab is
        -- shown: an equip just before shows the inventory; _play_nylocas.lua)
        t.ui.tab("combat")
        t.ticks(1)
        local _, rw = t.ui.widget("combat_interface:retaliate")
        t.ui.invoke(rw, 1)
    end
    t.ticks(2)
    local _, after = t.var.varp("varp172_option_nodef")
    t.check(row, after == want, P .. "varp172_option_nodef " .. tostring(before) .. " -> " .. tostring(after))
end

-- The barrier's question (_play_entry.lua begin_fight): wait for the options
-- page and press again when none opened, up to three presses.
local function begin_fight(t, name)
    local presses, cr, cd, opened = 0, nil, nil, false
    while presses < 3 and not opened do
        presses = presses + 1
        cr, cd = t.player.click_loc("tob_arena_barrier", 1)
        t.await({ level = function() return t.chat.kind() == "options" end, note = name .. ": the barrier's question" }, 6)
        opened = t.chat.kind() == "options"
    end
    t.check(name .. ".barrier", opened, "the barrier's question open after " .. presses .. " press(es); last press "
        .. tostring(cr) .. " " .. string.sub(tostring(cd), 1, 120))
    local pr, pd = t.chat.play({ "options", "choose:Yes, begin the fight." })
    t.check(name .. ".begin", pr == "ok", tostring(pd))
end

-- A member crosses the started barrier (every party harness's member
-- branch); the press is repeated while the raider stands where it stood.
local function cross(t, name)
    local _, a = t.world.tile()
    local xr, xd, n = nil, nil, 0
    while n < 3 do
        n = n + 1
        xr, xd = t.player.click_loc("tob_arena_barrier", 1)
        t.ticks(2)
        local _, b = t.world.tile()
        if b.x ~= a.x or b.z ~= a.z then break end
    end
    local _, c = t.world.tile()
    t.check(name .. ".cross", xr == "ok", P .. "crossed after the start (" .. n .. " press(es)): " .. a.x .. "," .. a.z .. " -> " .. c.x .. "," .. c.z
        .. " " .. string.sub(tostring(xd), 1, 120))
end

local function await_stat(t, stat, from, ticks)
    local r, s = t.skill.read(stat)
    local waited = 0
    while waited < ticks and (r ~= "ok" or s.level == from) do
        t.ticks(1)
        waited = waited + 1
        r, s = t.skill.read(stat)
    end
    return s, waited
end

local RESTORE_DOSES = { "br_1dose2restore", "br_2dose2restore", "br_3dose2restore", "br_4dose2restore",
    "1dose2restore", "2dose2restore", "3dose2restore", "4dose2restore" }
-- (the plain super combat first; then the kit's divine super combat, which
-- no room otherwise drinks: relay8 reached Sotetseg with the plain one
-- spent and its divine untouched, attack and strength 78 after Nylocas' brews)
local COMBAT_DOSES = { "1dose2combat", "2dose2combat", "3dose2combat", "4dose2combat",
    "1dosedivinecombat", "2dosedivinecombat", "3dosedivinecombat", "4dosedivinecombat" }

local function first_held(t, list)
    for _, dose in ipairs(list) do
        local cr, n = t.inv.count(dose)
        if cr == "ok" and n > 0 then return dose end
    end
    return nil
end

-- The super combat before a melee room's barrier on the stats the raider
-- reads NOW (_play_entry.lua boost): a super restore while attack or
-- strength reads under base, then the combat dose pressed until strength
-- moves (a press inside the restore's potion delay answers timeout).
local function boost(t, name)
    local _, a0 = t.skill.read("attack")
    local _, s0 = t.skill.read("strength")
    local restored, rtext = 0, ""
    while restored < 3 and (a0.level < a0.base_level or s0.level < s0.base_level) do
        local dose = first_held(t, RESTORE_DOSES)
        if dose == nil then break end
        t.player.inv_op(dose, 1, { quick = true })
        restored = restored + 1
        local s1, w = await_stat(t, "strength", s0.level, 4)
        local _, a1 = t.skill.read("attack")
        rtext = rtext .. string.format(" %s: attack %d -> %d, strength %d -> %d (+%d tick(s));", dose, a0.level, a1.level, s0.level, s1.level, w)
        a0, s0 = a1, s1
    end
    local dose = first_held(t, COMBAT_DOSES)
    local r, w, presses = "none", 0, 0
    local s2 = s0
    while dose ~= nil and presses < 3 and s2.level == s0.level do
        presses = presses + 1
        r = t.player.inv_op(dose, 1, { quick = true })
        local ws
        s2, ws = await_stat(t, "strength", s0.level, 4)
        w = w + ws
        dose = s2.level == s0.level and first_held(t, COMBAT_DOSES) or dose
    end
    local _, a2 = t.skill.read("attack")
    t.check(name .. ".potion", s2.level > s2.base_level and a2.level > a2.base_level,
        P .. "super combat before the barrier: strength " .. s0.level .. " -> " .. s2.level .. "/" .. s2.base_level
        .. ", attack " .. a0.level .. " -> " .. a2.level .. "/" .. a2.base_level .. " (" .. tostring(dose) .. " " .. tostring(r) .. ", "
        .. presses .. " press(es), +" .. w .. " tick(s)); restores " .. restored .. ":" .. (rtext == "" and " none needed" or rtext))
end

-- Between rooms a raider eats up before the next barrier, as a player does
-- in the corridor.  ~tob_restore heals Hitpoints and Prayer after a boss, but
-- only the raider the room's watchdog runs for (tob_raid.rs2 ~tob_room_cleared
-- calls it once, in one raider's script): svbplaynorma's p3 reached Bloat on
-- 34 hitpoints and died on the second tick of the fight.  Fish first, then
-- brews (the brew's drain is the boost's to restore), up to 99; a super
-- restore when prayer is under half.
local FOOD_FIRST = { "anglerfish", "mantaray", "seaturtle", "shark", "br_1dosepotionofsaradomin", "br_2dosepotionofsaradomin",
    "br_3dosepotionofsaradomin", "br_4dosepotionofsaradomin",
    "1dosepotionofsaradomin", "2dosepotionofsaradomin", "3dosepotionofsaradomin", "4dosepotionofsaradomin" }
local function top_up(t, name)
    local _, h0 = t.skill.read("hitpoints")
    local _, p0 = t.skill.read("prayer")
    local n, used = 0, {}
    local _, h = t.skill.read("hitpoints")
    while n < 12 and h.level < h.base_level - 10 do
        local item = first_held(t, FOOD_FIRST)
        if item == nil then break end
        t.player.inv_op(item, 1, { quick = true })
        used[#used + 1] = item
        n = n + 1
        t.ticks(3)
        _, h = t.skill.read("hitpoints")
    end
    local _, pr = t.skill.read("prayer")
    if pr.level < pr.base_level / 2 then
        local dose = first_held(t, RESTORE_DOSES)
        if dose ~= nil then
            t.player.inv_op(dose, 1, { quick = true })
            used[#used + 1] = dose
            t.ticks(3)
            _, pr = t.skill.read("prayer")
        end
    end
    t.check(name .. ".top_up", h.level >= h.base_level - 10, string.format("%shitpoints %d -> %d, prayer %d -> %d before the barrier (%s)", P,
        h0.level, h.level, p0.level, pr.level, #used > 0 and table.concat(used, " ") or "nothing needed"))
end

local STAMINA = { "1dosestamina", "2dosestamina", "3dosestamina", "4dosestamina" }
local function stamina(t, name)
    local dose = first_held(t, STAMINA)
    if dose == nil then
        -- (none carried: the whole-raid pack's slot went to the room tools;
        -- the plans' run keep still presses the orb when run reads off)
        t.check(name .. ".stamina", true, P .. "no stamina dose carried")
        return
    end
    -- pressed again while the pack still reads the same dose (a press inside
    -- the combat dose's potion delay answers timeout: config D's first try)
    local r, d, n, after = nil, nil, 0, dose
    while n < 4 and after == dose do
        n = n + 1
        r, d = t.player.inv_op(dose, 1, { quick = true })
        t.ticks(2)
        after = first_held(t, STAMINA)
    end
    t.check(name .. ".stamina", after ~= dose, P .. dose .. " -> " .. tostring(after) .. " after " .. n .. " press(es); last " .. tostring(r) .. " "
        .. string.sub(tostring(d), 1, 80))
end

local function mark_tick(t, label)
    t.ticklog.mark(label)
    local _, rows = t.ticklog.rows({ kind = "mark" })
    local tick = nil
    for i = 1, #(rows or {}) do
        if rows[i].label == label then tick = rows[i].tick end
    end
    return tick
end

-- The leader starts the fight; every raider meets at `entrance` first and
-- at `started` after (the party harnesses' two barriers), then a member
-- crosses (`member_cross` runs on members after `started`).
-- `starter` is the seat that starts the fight (default the leader, seat 1);
-- the leader marks the tick log either way (a member reads no log).
local function start_room(t, name, leader_pre, member_cross, starter)
    starter = starter or 1
    t.expect(name .. ".barrier.entrance", t.party.barrier(name .. "_entrance", 600))
    -- owner_tob_normal: every seat through the barrier on one tick (t.raid.
    -- cross_together, raid_play.lua), the starter's own walk first (`leader_pre`);
    -- the members used to press after a "started" barrier, three to four ticks
    -- behind (eighteen tiles at the Nylocas door).  `member_cross` (a member's
    -- walk before its press) is not needed: every seat walks to the door tile.
    local xr, xd = t.raid.cross_together(name, { starter = starter, before = leader_pre,
        at_answer = function() if role == 1 then R[name].mark = mark_tick(t, name .. " start") end end })
    t.check(name .. ".cross", xr == "ok", P .. tostring(xd))
    if role == 1 and R[name].mark == nil then R[name].mark = mark_tick(t, name .. " start (seat " .. starter .. " began it)") end
end

-- THE SUPPLY CHEST (tob_chest.rs2: Normal opens tob_midway_stores, a
-- points store, after Bloat and after Sotetseg; each raider's own points,
-- 10-13 a deathless chest, Chest:20).  Every seat walks to it, opens it and
-- buys what the teams restock (Strategies: brews, restores, food): a super
-- restore and a brew first, then the most food its points and free slots
-- buy.  Stock and costs are enum_1952 / enum_1953: slot 2 brew 3 points,
-- slot 3 super restore 3, slot 7 manta ray 2, slot 5 shark 1.
local CHEST_SLOT = { brew = 2, restore = 3, manta = 7, shark = 5 }
local CHEST_COST = { brew = 3, restore = 3, manta = 2, shark = 1 }
local function free_slots(t)
    local n = 0
    for i = 0, 27 do
        local r, sl = t.inv.slot(i)
        if r == "ok" and type(sl) == "table" and sl.count == 0 then n = n + 1 end
    end
    return n
end
-- The pack, slot by slot, for the chest's row (which slots hold what).
local function pack_text(t)
    local seen, order = {}, {}
    for i = 0, 27 do
        local r, sl = t.inv.slot(i)
        if r == "ok" and type(sl) == "table" and sl.count > 0 then
            if seen[sl.name] == nil then seen[sl.name] = 0 order[#order + 1] = sl.name end
            seen[sl.name] = seen[sl.name] + 1
        end
    end
    local parts = {}
    for _, n in ipairs(order) do parts[#parts + 1] = n .. (seen[n] > 1 and ("x" .. seen[n]) or "") end
    return table.concat(parts, " ")
end
local function restock(t, name)
    local chr, chrow = t.world.loc_near("tob_midway_chest_closed", 40)
    t.check(name .. ".chest_found", chr == "ok" and chrow ~= nil, P .. tostring(chr) .. " " .. tostring(chrow and chrow.tile_x) .. "," .. tostring(chrow and chrow.tile_z))
    if chr ~= "ok" or chrow == nil then return end
    -- (the first press walks to the chest and the store opens on arrival: a
    -- relay run's seats timed out at 6 ticks two tiles away; up to three
    -- presses from beside it)
    local cr, cd, or_ = nil, nil, "timeout"
    for try = 1, 3 do
        if or_ == "ok" then break end
        -- (to the chest's own approach tile, tob_bloat.constant: Bloat's chest
        -- is local (5,33) approached from (6,33), Sotetseg's (17,5) from
        -- (17,6) -- the press's own path ended on 6,32, beside the passage at
        -- (5,31), which carried the party on before the store opened)
        if name == "bloat" then
            t.player.walk_to(chrow.tile_x + 1, chrow.tile_z, 40)
        else
            t.player.walk_to(chrow.tile_x, chrow.tile_z + 1, 40)
        end
        cr, cd = t.player.click_loc("tob_midway_chest_closed", 1)
        or_ = t.ui.await_open("tob_midway_stores", 20)
    end
    t.check(name .. ".chest_open", or_ == "ok", P .. "chest " .. tostring(cr) .. " " .. string.sub(tostring(cd), 1, 80) .. "; store " .. tostring(or_))
    if or_ ~= "ok" then t.key("escape") return end
    local _, points = t.var.varbit("varb6460_tob_midwaychest_points")
    points = tonumber(points) or 0
    local p0 = points
    local bought = {}
    local function buy(kind)
        local wr, cell = t.ui.widget("tob_midway_stores:items", CHEST_SLOT[kind])
        if wr ~= "ok" then return false end
        local ir = t.ui.invoke(cell, 2)
        t.ticks(1)
        if ir ~= "ok" then return false end
        points = points - CHEST_COST[kind]
        bought[#bought + 1] = kind
        return true
    end
    -- (relay10: two super restores and sharks.  The seats left Nylocas
    -- with every restore dose drunk -- 7 a seat on its prayers -- and came
    -- to Sotetseg with none; and they ate none of the chest's manta rays
    -- there: the Nylocas plan's food lists are shark, anglerfish and
    -- bandages (raid_play_tob_nylocas.lua food_waves / food_boss), so a
    -- seat with only mantas left drank brews instead, 9-17 doses a seat.
    -- A shark is 1 point and 20 Hitpoints against a manta's 2 and 22.)
    -- (restores only up to two potions held -- 8 doses -- the rest food:
    -- relay16's seat 1 reached Sotetseg with 6 restore doses and 2 fish and
    -- died there; its pack carries the melee armour now)
    -- (one potion's worth only: the chest is POINTS-bound, not slot-bound --
    -- relay27's seat 1 spent 6 of its 11 points on two restores, left the
    -- chest with 5 slots free and 5 sharks, and reached Sotetseg with 1 fish)
    if supplies(t).restore < 4 and points >= CHEST_COST.restore and free_slots(t) > 1 then buy("restore") end
    -- The food: every free slot filled, as many of them manta rays (22, 2
    -- points) as the points allow and the rest sharks (20, 1 point) -- relay18
    -- left 3-6 points unspent with the pack full of sharks, and every seat
    -- reached Sotetseg with 0-1 fish.  One slot is kept after Sotetseg only:
    -- the Dawnbringer at Xarpus' skeleton (W:875; the Entry relay lost it to
    -- a full pack); the Bloat chest's food is all eaten before then.
    local keep = (name == "sotetseg") and 1 or 0
    local slots = free_slots(t) - keep
    local guard = 0
    while guard < 30 and slots > 0 and points >= CHEST_COST.shark do
        guard = guard + 1
        -- a manta while the points left still buy a shark for every other slot
        local kind = (points - CHEST_COST.manta >= (slots - 1) * CHEST_COST.shark) and "manta" or "shark"
        if not buy(kind) then
            if kind == "manta" and buy("shark") then else break end
        end
        slots = free_slots(t) - keep
    end
    local _, left = t.var.varbit("varb6460_tob_midwaychest_points")
    t.check(name .. ".chest_pack", true, P .. "after the chest, free " .. free_slots(t) .. ": " .. pack_text(t))
    t.check(name .. ".chest_bought", #bought > 0, P .. "points " .. p0 .. " -> " .. tostring(left) .. "; bought " .. table.concat(bought, " ") .. "; now " .. supplies_text(supplies(t)))
    t.key("escape")
    t.ticks(1)
end

-- THE NYLOCAS ROWS (owner_nylocas' relay snippet rev 2, THE FIVE ROWS: the
-- harness's code, test/raids/_play_nylocas.lua :578-833, on the leader after
-- the room, from the relay's own room-start mark "nylocas start").  Bounds:
-- last wave [244, 293], her phase [75, 123], four supports standing at her
-- landing with the weakest >= 0.10; her start and the room's ticks report
-- (owner ruling 2026-10-07, "Call nylocas good yeah").
local function nylocas_rows(t, rec)
    local ny = rec and rec.ny or {}
    local ids = ny.ids or { wave = {}, boss = {} }
    local tick0 = R.nylocas.mark or (rec and rec.start_tick) or 0
    local _, spawns = t.ticklog.rows({ kind = "npc_spawn" })
    t.ticks(1)
    local _, deaths = t.ticklog.rows({ kind = "npc_death" })
    t.ticks(1)
    local origin = rec and rec.origin or { x = 0, z = 0 }
    local waves, seen, boss_spawn, boss_death = {}, {}, nil, nil
    for index, r in ipairs(spawns) do
        if index % 60 == 0 then t.ticks(1) end
        if r.tick >= tick0 then
            if ids.wave[r.type] ~= nil and r.coord ~= nil then
                local x, z = math.floor(r.coord / 16384) % 16384, r.coord % 16384
                local lx, lz = x - origin.x, z - origin.z
                if (lx <= 18 or lx >= 45 or lz <= 10) and seen[r.tick] == nil then
                    seen[r.tick] = true
                    waves[#waves + 1] = r.tick - tick0
                end
            end
            if ids.boss[r.type] ~= nil and boss_spawn == nil then boss_spawn = r.tick end
        end
    end
    for _, r in ipairs(deaths) do
        if r.tick >= tick0 and ids.boss[r.type] ~= nil and boss_death == nil then boss_death = r.tick end
    end
    local last_wave = waves[#waves]
    local ref_last = last_wave and (last_wave + 1) or nil
    local ref_start = boss_spawn and (boss_spawn - tick0 + 1) or nil
    local ref_phase = (boss_spawn and boss_death) and (boss_death - boss_spawn) or nil
    local ref_death = boss_death and (boss_death - tick0 + 1) or nil
    t.check("nylocas.tech.waves", #waves == 31, "waves out " .. #waves .. " (spec 31), the last " .. tostring(last_wave) .. " ticks after the mark")
    t.check("nylocas.tech.pillars_at_boss", (ny.supports_alive_at_landing or 0) == 4 and (ny.supports_min_at_landing or 0) >= 0.10,
        "supports standing when Vasilias landed: " .. tostring(ny.supports_alive_at_landing) .. " of 4 (need 4, the weakest >= 0.10), bars "
        .. tostring(ny.supports_at_landing))
    t.check("nylocas.ref.last_wave", ref_last ~= nil and ref_last >= 244 and ref_last <= 293, "wave 31 out at room tick " .. tostring(ref_last)
        .. "; reference outcome.phase.wave31.start 260 [244-293]")
    t.check("nylocas.ref.boss_start", ref_start ~= nil, "Vasilias spawned at room tick " .. tostring(ref_start)
        .. "; reference 308 [296-357] -- reported (owner ruling 2026-10-07)")
    t.check("nylocas.ref.boss_ticks", ref_phase ~= nil and ref_phase >= 75 and ref_phase <= 123, "her phase " .. tostring(ref_phase)
        .. " ticks; reference outcome.phase.boss.ticks 95 [75-123]")
    t.check("nylocas.ref.room_ticks", ref_death ~= nil, "her death at room tick " .. tostring(ref_death)
        .. "; reference 410 [371-471] -- reported (owner ruling 2026-10-07)")
end

-- ------------------------------------------------- each room's pre-fight
-- The serial of the newest mark row on tick `tick0` (a room's own mark), so
-- a room's rows are read from it on; 0 (the whole log) when none is found.
local function mark_serial(t, tick0)
    local _, rows = t.ticklog.rows({ kind = "mark" })
    local serial = 0
    for i = 1, #(rows or {}) do
        if rows[i].tick == tick0 then serial = rows[i].serial end
    end
    return serial
end

-- THE BOSS-UID GUARD (owner 2026-10-07: "make sure that bug doesn't occur in
-- any other room").  The content keeps the room's boss uid on the room's
-- register and copies it into every raider's own %varp6886_tob_boss_uid
-- (~tob_boss_uid_publish, tob_raid.rs2); the wake, rescale and teardown read
-- the copy of whichever raider's turn runs them.  In the party relay the
-- leader's copy stayed Maiden's for the whole raid and Xarpus never woke
-- (relay15, CONTENT_BUGS 2026-10-07).  Every seat checks its own copy against
-- the room's register (::tobstate boss_uid / roomboss / bosslive): at the
-- start, the room's boss alive (the Nylocas: none yet, -1); after the fight,
-- the same npc (Vasilias in the Nylocas).
local function boss_guard(t, name, when)
    -- (each seat its own reading: ::tobstate prints the asking raider's copy;
    -- t.raid.state is the leader's only)
    -- (the reply to THIS ask: a line after the serial read before it -- an
    -- older ::tobstate line in the ring is another room's reading)
    local since = newest_serial(t)
    t.cheat("::tobstate")
    local line = nil
    for _ = 1, 8 do
        t.ticks(1)
        line = line_since(t, since, "roomboss=")
        if line ~= nil then break end
    end
    local mine = line and tonumber(string.match(line, "boss_uid=(%-?%d+)"))
    local room = line and tonumber(string.match(line, "roomboss=(%-?%d+)"))
    local live = line and tonumber(string.match(line, "bosslive=(%d+)"))
    local ok = mine ~= nil and room ~= nil and mine == room
    if when == "start" then
        if name == "nylocas" then
            ok = ok and room == -1
        else
            ok = ok and room ~= -1 and live == 1
        end
    else
        ok = ok and room ~= -1
    end
    t.check(name .. ".guard.boss_uid_" .. when, ok, P .. "own boss_uid " .. tostring(mine) .. ", the room's " .. tostring(room)
        .. " (alive " .. tostring(live) .. ")" .. ((name == "nylocas" and when == "start") and "; no boss before Vasilias lands" or "")
        .. (line == nil and "; no ::tobstate line with roomboss= (a pack before the fix)" or ""))
end

-- The boss woke: a form change or an animation of the boss's own slot after
-- the room's mark (the leader's tick log).  Xarpus in relay15 had neither for
-- 1400 ticks.
local function boss_woke(t, name, rec)
    local slot = rec and rec.boss_slot
    local mark = R[name].mark
    local first = nil
    if slot ~= nil and mark ~= nil then
        for _, kind in ipairs({ "npc_retype", "npc_anim" }) do
            local _, rows = t.ticklog.rows({ kind = kind, slot = slot, since = mark_serial(t, mark) })
            for i = 1, #(rows or {}) do
                if rows[i].tick >= mark and (first == nil or rows[i].tick < first.tick) then first = { tick = rows[i].tick, kind = kind } end
            end
            t.ticks(1)
        end
    end
    t.check(name .. ".guard.boss_woke", first ~= nil, "boss slot " .. tostring(slot) .. ": first "
        .. (first and (first.kind .. " " .. (first.tick - mark) .. " ticks after the mark") or "form change or animation: none") )
end

-- THE MAIDEN ROWS: test/raids/_play_maiden.lua's freezer and leader reads at
-- 5b61b41c7 (the room GREEN on the owner's ruling of 2026-10-07), lifted
-- whole and prefixed "maiden.", from the relay's own mark ("maiden start").
-- The relay's leader (seat 1) is Maiden's freezer, so both run on seat 1.
-- Read right after her room, the first, so no earlier room's rows are in
-- the log.  The relay splits her storms by its standing tiles
-- (storm_split): maiden.ref.storm_share and maiden.tech.tank report it.
local function maiden_rows(t, rec)
    local m = rec.m or {}
    local casts, nc = "", 0
    for _, c in ipairs(m.casts or {}) do
        nc = nc + 1
        if nc <= 40 then casts = casts .. "[w" .. c.wave .. " t" .. c.tick .. " s" .. c.slot .. " " .. tostring(c.why) .. " " .. c.result .. "]" end
    end
    local seen = m.body_seen or {}
    t.check("maiden.casts", true, string.sub("Ice Barrage casts " .. nc .. " " .. casts .. (m.cast_none and (" no cast: " .. table.concat(m.cast_none, " ")) or ""), 1, 1500))
    if mrole == 2 then
        -- raid seam40: the thresholds the casts were made in (the reference's
        -- freezer attacks adds in every crab phase: role.freezer.phase.70/50/30
        -- .attacks_add 4 [2-7] / 5 [3-8] / 4 [2-7], reference/maiden_normal_3.json)
        local forms_cast = {}
        local nforms = 0
        for _, c in ipairs(m.casts or {}) do
            if c.form ~= nil and not forms_cast[c.form] then forms_cast[c.form] = true nforms = nforms + 1 end
        end
        t.check("maiden.tech.freezer_casts", nc > 0 and nforms >= 3, "the freezer cast in " .. nforms .. " of her 3 thresholds (counter waves " .. tostring(m.waves) .. "), casts " .. nc)
        -- raid seam35m: what the freezer's plan said each wave could be frozen
        -- (raid_play_tob_maiden.lua _play_maiden_ice_plan, its first cast)
        local plans = {}
        for k, pl in pairs(m.ice_plans or {}) do plans[#plans + 1] = "w" .. k .. " t" .. pl.tick .. " " .. pl.frozen .. " of " .. pl.total end
        table.sort(plans)
        -- (reported: the cast plan that filled ice_plans was replaced by the
        -- arrival casts, 78a7d1646; the casts row above names every cast)
        t.check("maiden.play.ice_plan", true, "the plan's best at each wave's first cast: " .. table.concat(plans, "; "))
    end
    local ws = rec.boss_slot
    local mark_tick = R.maiden.mark
    t.ticks(1)
    local _, spawn_rows = t.ticklog.rows({ kind = "npc_spawn" })
    t.ticks(1)
    local _, death_rows = t.ticklog.rows({ kind = "npc_death" })
    t.ticks(1)
    local _, free_rows = t.ticklog.rows({ kind = "npc_free" })
    t.ticks(1)
    local _, retype_rows = t.ticklog.rows({ kind = "npc_retype" })
    t.ticks(1)
    local _, ntile_rows = t.ticklog.rows({ kind = "npc_tile" })
    t.ticks(1)
    local _, hitp_rows = t.ticklog.rows({ kind = "hit_player" })
    t.ticks(1)
    local _, hitn_rows = t.ticklog.rows({ kind = "hit_npc" })
    t.ticks(1)
    local _, anim_rows = t.ticklog.rows({ kind = "npc_anim", slot = ws })
    t.ticks(1)
    local ids = m.ids or { boss = {}, crab = {}, slug = {} }
    -- her free tick and her footprint (her npc_tile row; 6x6)
    local free_tick = nil
    for _, r in ipairs(free_rows) do
        if r.slot == ws and (mark_tick == nil or r.tick >= mark_tick) then free_tick = r.tick end
    end
    local bx, bz = seen.x, seen.z
    for _, r in ipairs(ntile_rows) do
        if r.slot == ws then bx, bz = r.x, r.z end
    end
    local function gap(x, z)
        if bx == nil then return 99 end
        return math.max(math.max(bx - x, 0, x - (bx + 5)), math.max(bz - z, 0, z - (bz + 5)))
    end
    -- raid seam35m: the content's own arrival rectangle for a size-2 crab
    -- over its south-west anchor (tob.constant:480-494, ^tob_maiden_arrive_*:
    -- two tiles out on her west and south faces, one on her north and east)
    local function arrived_at(x, z)
        if bx == nil then return false end
        return x - bx >= -2 and x - bx <= 6 and z - bz >= -2 and z - bz <= 6
    end
    -- the thresholds: her retypes to the 70/50/30 forms
    local thresholds = {}
    for _, r in ipairs(retype_rows) do
        if r.slot == ws and ids.boss[r.to_type] ~= nil and (mark_tick == nil or r.tick >= mark_tick) then
            thresholds[#thresholds + 1] = { tick = r.tick, symbol = ids.boss[r.to_type], spawned = 0, frozen = 0, reached = 0, killed = 0, heal = 0, alive_end = 0 }
        end
    end
    -- every Matomenos: spawn, tiles, death; frozen = two ticks or more on one
    -- tile outside her gap (they step every tick, maiden.crab_walk);
    -- reached = it died at her gap (maiden.crab_arrive_gap 1); the heal is
    -- twice the absorb's blow (its remaining hitpoints, maiden.leak_heal_multiplier)
    local crabs = {}
    for _, r in ipairs(spawn_rows) do
        if ids.crab[r.type] ~= nil and (mark_tick == nil or r.tick >= mark_tick) then
            crabs[#crabs + 1] = { slot = r.slot, spawn = r.tick, tiles = {}, death = nil, frozen = false, reached = false }
        end
    end
    for _, c in ipairs(crabs) do
        for _, r in ipairs(death_rows) do
            if r.slot == c.slot and r.tick >= c.spawn and c.death == nil then c.death = r.tick end
        end
        for _, r in ipairs(ntile_rows) do
            if r.slot == c.slot and r.tick >= c.spawn and (c.death == nil or r.tick <= c.death) then c.tiles[#c.tiles + 1] = r end
        end
        local last_end = c.death or free_tick or (c.tiles[#c.tiles] and c.tiles[#c.tiles].tick) or c.spawn
        for k = 1, #c.tiles do
            local nxt = (k < #c.tiles) and c.tiles[k + 1].tick or last_end
            if nxt - c.tiles[k].tick >= 3 and not arrived_at(c.tiles[k].x, c.tiles[k].z) then c.frozen = true end
        end
        -- raid seam35m: reached = it stood in the arrival rectangle at the end
        -- of a tick and died on a later one (the content absorbs it at the
        -- start of the next tick, before any hit: tob_maiden.rs2
        -- [proc,tob_maiden_crab_tick]); one killed on the tick it stepped in
        -- was killed (seam33's gap <= 1 missed every south absorb at gap 2:
        -- m35a w1 S1 absorbed on 5,-2 counted as killed)
        local lt = c.tiles[#c.tiles]
        if c.death ~= nil and lt ~= nil and arrived_at(lt.x, lt.z) and c.death > lt.tick then
            c.reached = true
            for _, r in ipairs(hitn_rows) do
                if r.slot == c.slot and r.tick == c.death then c.heal = 2 * r.damage end
            end
        end
        local w = nil
        for k = 1, #thresholds do
            if c.spawn >= thresholds[k].tick - 1 then w = thresholds[k] end
        end
        if w ~= nil then
            w.spawned = w.spawned + 1
            if c.frozen then w.frozen = w.frozen + 1 end
            if c.reached then
                w.reached = w.reached + 1
                w.heal = w.heal + (c.heal or 0)
            elseif c.death ~= nil then
                w.killed = w.killed + 1
            else
                w.alive_end = w.alive_end + 1
            end
        end
    end
    local wave_text_rows, freeze_ok, reached_total, heal_total = {}, #thresholds == 3, 0, 0
    for k, w in ipairs(thresholds) do
        wave_text_rows[#wave_text_rows + 1] = string.format("w%d %s t%d: %d spawned, %d frozen, %d reached her (heal %d), %d killed, %d alive at the end",
            k, w.symbol, w.tick, w.spawned, w.frozen, w.reached, w.heal, w.killed, w.alive_end)
        -- The bar is what real trios reach, not the guide's ideal: across 26 Regular
        -- scale-3 Maiden rooms on Blert (sources/blert_api/maiden_trio_crabs/README.md)
        -- the median stationary-20+-ticks crabs per wave are 5 / 3 / 4 of 6 at
        -- 70 / 50 / 30 percent, and "all but one" is reached in 26 of 78 waves.
        if w.spawned == 0 or w.frozen < 3 then freeze_ok = false end
        reached_total = reached_total + w.reached
        heal_total = heal_total + w.heal
    end
    t.check("maiden.tech.freeze", freeze_ok, "every threshold's Matomenos frozen at the real trios' rate, at least 3 of 6 a wave (Blert 26 Regular trio rooms, median 5/3/4 of 6; W:639-643; 10Boot 0:08:48): "
        .. table.concat(wave_text_rows, "; "))
    -- raid seam40: the bar is the reference's RANGE, not its median
    -- (docs/minigames/theater_of_blood/sources/blert_api/reference/
    -- maiden_normal_3.json, 24 death-free Normal scale-3 rooms:
    -- outcome.leaks 5 [0-13], outcome.boss_heal 223.5 [0-821])
    t.check("maiden.tech.crabs_killed", #thresholds == 3 and reached_total <= 13 and heal_total <= 821, "Matomenos that reached her " .. reached_total .. " (heals " .. heal_total
        .. "; W:593 'she will be healed by double the amount of their current Hitpoints'); the reference (reference/maiden_normal_3.json): leaks 5 [0-13], heal 223.5 [0-821]")
    -- raid seam40 THE REFERENCE'S CLOCK: her death and her first threshold
    -- from the room's start (the mark), the reference's outcome.room_ticks
    -- 157.5 [132-204] and outcome.phase.100.ticks 42 [32-52]
    -- (reference/maiden_normal_3.json; raid_report.py --against reads the same
    -- two from this tick log)
    local death_tick = nil
    for _, r in ipairs(death_rows) do
        if r.slot == ws and (mark_tick == nil or r.tick >= mark_tick) and death_tick == nil then death_tick = r.tick end
    end
    local room_ticks = (death_tick and mark_tick) and (death_tick - mark_tick) or nil
    -- OWNER RULING 2026-10-07 (owner_tob_normal): "Maiden is fine. You're barely
    -- off blert. Count it as good."  Measured on 24 names at 6cba215fd on the
    -- pinned pack (content e11e84535d): mean 207, range 182-234, 0 deaths,
    -- against the reference's 157.5 [132-204].  The bound is the
    -- ruling's: our 24-name range, 240 at most.
    -- (LENGTHS REPORT, MECHANICS ARE JUDGED: the owner's rulings of
    -- 2026-10-07, "Maiden is fine. You're barely off blert. Count it as
    -- good." and, of a 2-point miss, "Missing by 2 hitpoints is fine. Maybe
    -- loosen the requirements." -- the room and phase lengths print against
    -- the reference; the crab, freeze, storm and guard rows stay judged)
    t.check("maiden.ref.room_ticks", room_ticks ~= nil,
        "her death " .. tostring(room_ticks) .. " ticks after the room's start (mark " .. tostring(mark_tick) .. ", death " .. tostring(death_tick) .. "); reference/maiden_normal_3.json outcome.room_ticks 157.5 [132-204]; bound 240 by the owner's ruling of 2026-10-07")
    local p100 = (thresholds[1] and mark_tick) and (thresholds[1].tick - mark_tick) or nil
    t.check("maiden.ref.phase_100_ticks", p100 ~= nil,
        "her 70 percent form " .. tostring(p100) .. " ticks after the room's start; reference/maiden_normal_3.json outcome.phase.100.ticks 42 [32-52] -- reported (owner rulings 2026-10-07)")
    -- who took her blackstorms, and the blood (pools and trails: hitsplat 28 with no npc)
    -- the log's pid of each seat: the leader's is the plan's own (re-read by
    -- tile, raid_play_tob_maiden.lua), the freezer's is the one whose
    -- player_anim rows carry Ice Barrage's cast (seq 1979), the third is the
    -- remaining one (s32mzn7: a tile match at the end could not tell them)
    local pid_seat = {}
    local _, cast_rows = t.ticklog.rows({ kind = "player_anim", seq = 1979 })
    t.ticks(1)
    local _, ptile_rows = t.ticklog.rows({ kind = "player_tile" })
    t.ticks(1)
    -- (sm153: the pids follow the join order, seat 1 pid 0 .. seat 3 pid 2;
    -- each is named by the role its seat plays)
    for _, r in ipairs(ptile_rows) do
        if pid_seat[r.pid] == nil then pid_seat[r.pid] = MAIDEN_ROLE_OF_SEAT[r.pid + 1] or ("_pid" .. tostring(r.pid)) end
    end
    local storm_by, blood_by, taken_by = {}, {}, {}
    for _, r in ipairs(hitp_rows) do
        local seat = pid_seat[r.pid] or ("_pid" .. tostring(r.pid))
        if mark_tick == nil or r.tick >= mark_tick then
            taken_by[seat] = (taken_by[seat] or 0) + r.damage
            if r.npc_slot == ws then storm_by[seat] = (storm_by[seat] or 0) + 1 end
            if r.npc_slot == -1 and r.hitsplat == 28 then blood_by[seat] = (blood_by[seat] or 0) + r.damage end
        end
    end
    local function seats(tbl)
        local parts = {}
        for k, val in pairs(tbl) do parts[#parts + 1] = "p" .. tostring(k) .. "=" .. tostring(val) end
        table.sort(parts)
        return table.concat(parts, " ")
    end
    -- raid seam40: the two scythe seats share her storms (the leader's step
    -- out on every other attack, raid_play_tob_maiden.lua THE SHARED TANK);
    -- the reference's role.dps1.boss_targeted_pct 38 [5.6-58.8] and dps2
    -- 41.45 [18.8-64.3] (reference/maiden_normal_3.json): each seat's share
    -- of all her storms inside [5.6, 64.3]
    local storm_all = 0
    for _, n in pairs(storm_by) do storm_all = storm_all + n end
    local share1 = storm_all > 0 and math.floor(100 * (storm_by[1] or 0) / storm_all + 0.5) or 0
    local share3 = storm_all > 0 and math.floor(100 * (storm_by[3] or 0) / storm_all + 0.5) or 0
    -- (reported, not judged, under the owner's ruling of 2026-10-07: her storm
    -- goes to the raider nearest her centre, ties to orb -- stormrule.py 293
    -- of 293 -- and our dps1 takes the ties the reference's dps split by
    -- standing off her for crabs)
    t.check("maiden.ref.storm_share", storm_all > 0,
        "her storms by seat " .. seats(storm_by) .. ": seat 1 " .. share1 .. "%, seat 3 " .. share3 .. "%; reference/maiden_normal_3.json dps boss_targeted_pct 38 [5.6-58.8] / 41.45 [18.8-64.3]")
    -- (reported, not judged: the streams' freezer, orb 0, takes 24% of her
    -- storms -- 57 of 81 in her 30 form -- so "never the closest" is not the
    -- reference; owner's ruling of 2026-10-07)
    t.check("maiden.tech.tank", storm_all > 0, "blackstorm hits by seat: " .. seats(storm_by) .. "; the freezer (p2); the streams' freezer takes 24% (stormtiles.py)")
    local storms, bloods = 0, 0
    for k = 1, #anim_rows do
        if anim_rows[k].seq == 8092 then storms = storms + 1 elseif anim_rows[k].seq == 8091 then bloods = bloods + 1 end
    end
    t.check("maiden.play.measure_party", true, string.format("room %s ticks (mark %s, npc_free %s); her attacks %d blackstorm, %d blood; damage taken by seat %s; blood (pools+trails) by seat %s; pids %s",
        tostring(free_tick and mark_tick and (free_tick - mark_tick)), tostring(mark_tick), tostring(free_tick), storms, bloods, seats(taken_by), seats(blood_by), seats(pid_seat)))
end

-- THE BLOAT ROWS (owner_rooms4's relay snippet, build/seam_state/owner_rooms4/
-- relay_bloat_snippet.lua ROWS): the leader's tick-log reads of
-- test/raids/_play_bloat.lua at 5e555aa62, lifted whole -- the same rows,
-- prefixed "bloat.", from the relay's own mark ("bloat start"); every row
-- read is cut to the ticks from that mark on, so an npc that held Bloat's
-- slot in an earlier room is not his.
local function bloat_rows(t, rec, result, detail, ox, oz)
    local mode, boss = "normal", "tob_bloat"
    local tick0 = R.bloat.mark or (rec and rec.start_tick) or 0
    -- (read from the mark's own serial on: the log is the whole raid's, and
    -- a whole-log read and filter ran the leader out of its instruction
    -- budget after Sotetseg, relay13)
    local since = mark_serial(t, tick0)
    local function rows_of(query)
        local q = {}
        for k, v in pairs(query) do q[k] = v end
        q.since = since
        return t.ticklog.rows(q)
    end
    -- THE TICK LOG (leader): the same reads tob_bloat.lua's ANALYSIS makes
    t.ticks(1)
    local mark_tick = R.bloat.mark
    local sr0, row0 = t.npc.state(boss)
    local boss_slot = rec.boss_slot
    local death_tick = rec.death_tick
    local dead = result == "ok" and death_tick ~= nil
    local player_dead = result == "died"
    local end_tick = death_tick or 0
    local mine = function(row) return size == 1 or rec.my_pid == nil or row.pid == rec.my_pid end
    local downs = {}
    local _, anim_rows = rows_of({ kind = "npc_anim", slot = boss_slot, seq = 8082 })
    for i = 1, #anim_rows do downs[#downs + 1] = anim_rows[i].tick end
    t.ticks(1)
    local bx_at, bz_at, move_ticks = {}, {}, {}
    local _, boss_tiles = rows_of({ kind = "npc_tile", slot = boss_slot })
    for i = 1, #boss_tiles do
        bx_at[boss_tiles[i].tick] = boss_tiles[i].x
        bz_at[boss_tiles[i].tick] = boss_tiles[i].z
        move_ticks[#move_ticks + 1] = boss_tiles[i].tick
    end
    local bxf, bzf = {}, {}
    local last_bx, last_bz = nil, nil
    for tk = 0, end_tick + 40 do
        if bx_at[tk] ~= nil then last_bx, last_bz = bx_at[tk], bz_at[tk] end
        bxf[tk], bzf[tk] = last_bx, last_bz
    end
    t.ticks(1)
    local px_at, pz_at = {}, {}
    local _, player_tiles = rows_of({ kind = "player_tile" })
    for i = 1, #player_tiles do
        if mine(player_tiles[i]) then
            px_at[player_tiles[i].tick] = player_tiles[i].x
            pz_at[player_tiles[i].tick] = player_tiles[i].z
        end
    end
    t.ticks(1)
    local fly_ticks, fly_list, fly_at_me = {}, {}, {}
    local _, projectiles = rows_of({ kind = "projectile", spotanim = 1568 })
    for i = 1, #projectiles do
        local row = projectiles[i]
        if row.spotanim == 1568 and fly_ticks[row.tick] == nil then
            fly_ticks[row.tick] = true
            fly_list[#fly_list + 1] = row.tick
        end
        -- raid seam32: in a party a fly flies at EVERY raider Bloat sees and
        -- spreads between raiders (W:673), so the hide row counts only the
        -- flies whose landing tile (dst, a packed coord) is the leader's own
        -- tile that tick or the one before
        if row.spotanim == 1568 and row.dst ~= nil then
            local dx, dz = math.floor(row.dst / 16384) % 16384, row.dst % 16384
            if (px_at[row.tick] == dx and pz_at[row.tick] == dz) or (px_at[row.tick - 1] == dx and pz_at[row.tick - 1] == dz) then
                fly_at_me[row.tick] = true
            end
        end
    end
    t.ticks(1)
    local shadows = {}
    for sid = 1570, 1573 do
        local _, shadow_rows = rows_of({ kind = "map_spotanim", spotanim = sid })
        for i = 1, #shadow_rows do
            local row = shadow_rows[i]
            if row.spotanim == sid then shadows[#shadows + 1] = { tick = row.tick, x = row.x, z = row.z } end
        end
        t.ticks(1)
    end
    local splat_ticks = {}
    local _, splat_rows = rows_of({ kind = "map_spotanim", spotanim = 1576 })
    for i = 1, #splat_rows do splat_ticks[splat_rows[i].tick] = true end
    -- owner_rooms4: A HAND HIT IS WHERE A HAND LANDED.  The rows below
    -- counted any hit of 15 or more on a tick some splat showed, so the
    -- leader's two unprayed flies at the barrier (19 and 20, _play_bloat
    -- t61-62 on 6439,94, no splat within 4 tiles) read as hands and made
    -- tech.step_off_shadow red on a run where no hand touched a raider.
    -- A hand hits the raider standing on its splat's tile at the end of
    -- the tick before it (ET 3.4): the hit's pid, its tile then, the
    -- splat (1576) on that tile on the hit's tick.
    local splat_at = {}
    for i = 1, #splat_rows do splat_at[splat_rows[i].tick .. ":" .. splat_rows[i].x .. "," .. splat_rows[i].z] = true end
    local tile_rows = {}
    for i = 1, #player_tiles do
        local row = player_tiles[i]
        local key = tostring(row.pid)
        tile_rows[key] = tile_rows[key] or {}
        tile_rows[key][row.tick] = row
    end
    local function hand_landed_on(row)
        local at = tile_rows[tostring(row.pid)]
        if at == nil then return false end
        for tk = row.tick - 1, row.tick - 60, -1 do
            local tr = at[tk]
            if tr ~= nil then return splat_at[row.tick .. ":" .. tr.x .. "," .. tr.z] == true end
        end
        return false
    end
    t.ticks(1)
    -- Protect from Missiles as the player READ it lit, per tick (the library's record)
    local shield_filled = {}
    local carried = false
    for tk = 0, end_tick + 40 do
        local lit = rec.prayer_at[tk]
        if lit ~= nil then carried = lit.protectfrommissiles == true end
        shield_filled[tk] = carried
    end
    -- the most one fly lands unprotected: Entry 8 (tob.constant :752),
    -- Normal 20 (tob.constant :746; W:673 "up to 20 damage every tick")
    local fly_max = (mode == "entry") and 8 or 20
    local fly_hits_protected, stomp_hits, hand_hits = {}, {}, {}
    local taken = 0
    local _, player_hits = rows_of({ kind = "hit_player", slot = boss_slot })
    for i = 1, #player_hits do
        local row = player_hits[i]
        if mine(row) then
            taken = taken + row.damage
            local in_down = false
            for k = 1, #downs do
                if row.tick > downs[k] and row.tick < downs[k] + 33 then in_down = true end
            end
            if hand_landed_on(row) and not in_down then
                hand_hits[#hand_hits + 1] = row.damage
            elseif in_down and row.damage >= 10 then
                stomp_hits[#stomp_hits + 1] = { tick = row.tick, damage = row.damage }
            elseif row.damage <= fly_max then
                -- raid seam29: the window is the fly's own flight, its launch
                -- tick to its landing.  The damage is rolled and the prayer read
                -- AT LAUNCH (tob_bloat.rs2 ~tob_bloat_fly_hit queues
                -- combat_damage_player with ~tob_bloat_fly_damage, which reads
                -- ~prayer_is_on(protectfrommissiles)).  tob_bloat.lua's six
                -- ticks back could only count a fly that landed 7+ ticks after
                -- the prayer went up, so a raider who hid on every walk had only
                -- the rise tick's fly (prayer up on T+33, the plan's) and the row
                -- had no evidence (seam29 survey: _play_smoke, svcplaysmoke).
                -- Which fly landed is not in the log, so the window starts at the
                -- EARLIEST launch that could have (six ticks back at most): svbplaysmoke's
                -- 7 at t27 was launched t24, not t25, before its prayer was in force
                -- (a press is in force from the NEXT tick's npc phase, DRIVER_NOTES
                -- "A prayer press is in force for the next npc phase").
                -- No launch row within six ticks: the six-tick rule, unchanged.
                local from = row.tick - 6
                for k = #fly_list, 1, -1 do
                    if fly_list[k] <= row.tick and fly_list[k] >= row.tick - 6 then from = fly_list[k] end
                end
                local shielded = true
                for tk = from, row.tick do
                    if shield_filled[tk] ~= true then shielded = false end
                end
                if shielded then fly_hits_protected[#fly_hits_protected + 1] = row.damage end
            end
        end
    end
    t.ticks(1)
    local up_ticks, stomp_tick_of = {}, {}
    for i = 1, #downs do
        for k = 1, #move_ticks do
            if move_ticks[k] > downs[i] and up_ticks[i] == nil then up_ticks[i] = move_ticks[k] end
        end
        for k = 1, #stomp_hits do
            if stomp_hits[k].tick > downs[i] and stomp_hits[k].tick < downs[i] + 33 and stomp_tick_of[i] == nil then
                stomp_tick_of[i] = stomp_hits[k].tick
            end
        end
    end
    local pxf, pzf = {}, {}
    local last_px, last_pz = nil, nil
    for tk = 0, end_tick + 40 do
        if px_at[tk] ~= nil then last_px, last_pz = px_at[tk], pz_at[tk] end
        pxf[tk], pzf[tk] = last_px, last_pz
    end
    local down_at = {}
    for i = 1, #downs do
        for tk = downs[i], downs[i] + 32 do down_at[tk] = true end
    end
    t.ticks(1)

    -- THE TECHNIQUE ROWS, each check copied unchanged from tob_bloat.lua :1795-1849,
    -- except protect_from_missiles' window: the fly's flight (raid seam29, above)
    local behind_ticks, behind_flies = 0, 0
    if mark_tick ~= nil then
        for tk = mark_tick + 2, end_tick do
            if not down_at[tk] and not down_at[tk - 1] and bxf[tk - 1] ~= nil and pxf[tk - 1] ~= nil then
                local mirror_x = 2 * ox + 59 - bxf[tk - 1]
                local mirror_z = 2 * oz + 61 - bzf[tk - 1]
                if math.max(math.abs(pxf[tk - 1] - mirror_x), math.abs(pzf[tk - 1] - mirror_z)) <= 1 then
                    behind_ticks = behind_ticks + 1
                    if (size == 1 and fly_ticks[tk]) or (size > 1 and fly_at_me[tk]) then behind_flies = behind_flies + 1 end
                end
            end
        end
    end
    local walk_total_ticks = 0
    if mark_tick ~= nil and downs[1] ~= nil then walk_total_ticks = downs[1] - mark_tick end
    for i = 1, #downs do
        if up_ticks[i] ~= nil then
            local next_down = downs[i + 1] or end_tick
            if next_down > up_ticks[i] then walk_total_ticks = walk_total_ticks + (next_down - up_ticks[i]) end
        end
    end
    t.check("bloat.tech.hide_behind_tank", behind_ticks > 0 and behind_flies == 0,
        "Bloat was up on " .. walk_total_ticks .. " ticks and a fly projectile flew on " .. #fly_list .. " of them; on the " .. behind_ticks .. " walking ticks the player stood directly behind the tank " .. behind_flies .. " flies flew" .. (size > 1 and " at the player's own tile (a party: flies fly at every raider Bloat sees and spread, W:673)" or ""))
    -- raid seam54 play_tob_bloat_on_tobkit: in a party the row reads EVERY
    -- raider's own tile (player_tile rows carry the pid; the leader's log
    -- has all three), each raider held to the same rule (off within two
    -- ticks for at least half the shadows that fell on it).  With the
    -- recorded kit the room is two downs (138-148 ticks), and on
    -- svaplaybloat no shadow fell on the leader's own tile at all: the row
    -- read "0 of 0" as a failed technique when it was no evidence, while
    -- the same plan steps every raider.  Solo: the leader alone, unchanged.
    local tiles_by = {}
    for i = 1, #player_tiles do
        local row = player_tiles[i]
        local key = (size > 1 and row.pid ~= nil) and tostring(row.pid) or "me"
        if size == 1 and not mine(row) then key = nil end
        if key ~= nil then
            if tiles_by[key] == nil then tiles_by[key] = {} end
            tiles_by[key][row.tick] = row
        end
    end
    local on_my_tile, stayed, every_raider_ok, per_raider = 0, 0, true, {}
    for key, at in pairs(tiles_by) do
        local fx, fz, lx, lz = {}, {}, nil, nil
        for tk = 0, end_tick + 40 do
            if at[tk] ~= nil then lx, lz = at[tk].x, at[tk].z end
            fx[tk], fz[tk] = lx, lz
        end
        local on, st = 0, 0
        for i = 1, #shadows do
            local row = shadows[i]
            if (fx[row.tick] == row.x and fz[row.tick] == row.z) or (fx[row.tick - 1] == row.x and fz[row.tick - 1] == row.z) then
                on = on + 1
                if fx[row.tick + 2] == row.x and fz[row.tick + 2] == row.z then st = st + 1 end
            end
        end
        if on > 0 and (on - st) * 2 < on then every_raider_ok = false end
        on_my_tile, stayed = on_my_tile + on, stayed + st
        per_raider[#per_raider + 1] = (key == "me" and "" or "pid" .. key .. " ") .. (on - st) .. " of " .. on
    end
    table.sort(per_raider)
    -- The hands fall on random tiles, not at a raider (tob_bloat.rs2
    -- ~tob_bloat_drop_hand: sixteen of the room's 220 tiles a volley), so a
    -- short room can drop none on any raider: svaplaybloat, 94 shadows in a
    -- 138-tick room, 0 on a raider's tile.  Then the row has no step to
    -- judge and says so; what it still asserts is the outcome -- no hand
    -- landed on ANY raider (every pid's hit_player row is in this log).
    local party_hand_hits = 0
    for i = 1, #player_hits do
        local row = player_hits[i]
        local in_down = false
        for k = 1, #downs do
            if row.tick > downs[k] and row.tick < downs[k] + 33 then in_down = true end
        end
        if hand_landed_on(row) and not in_down then party_hand_hits = party_hand_hits + 1 end
    end
    local shadow_ok = every_raider_ok
    if on_my_tile == 0 then shadow_ok = size > 1 and party_hand_hits == 0 end
    t.check("bloat.tech.step_off_shadow", shadow_ok,
        on_my_tile .. " falling-flesh shadows appeared on a raider's own tile, the raider was off it two ticks later for " .. (on_my_tile - stayed) .. " of them (" .. table.concat(per_raider, ", ") .. ")"
        .. (on_my_tile == 0 and ("; no step to judge: " .. #shadows .. " shadows in the room, none on a raider") or "")
        .. "; hand hits: " .. #hand_hits .. " on the leader, " .. party_hand_hits .. " on the party")
    -- NORMAL (raid seam32 play_tob_bloat_normal): the Entry rows above
    -- assert Entry's numbers and Entry's stay-and-flinch; Normal leaves.
    -- "Unless the boss is below 3% health, it is recommended to run
    -- away after the last attack, since stomps and flies have a high
    -- chance of killing a player" (W:689): no raider takes a stomp on
    -- any down that reached it (T+29, ET 3.1; a hand's splat tick is
    -- not a stomp).  Every raider's hit_player row is in the leader's log.
    local reached, stomp_taken, stomp_list = 0, 0, {}
    for i = 1, #downs do
        if downs[i] + 29 <= end_tick then reached = reached + 1 end
    end
    for i = 1, #player_hits do
        local row = player_hits[i]
        for k = 1, #downs do
            if row.tick == downs[k] + 29 and not splat_ticks[row.tick] then
                stomp_taken = stomp_taken + 1
                stomp_list[#stomp_list + 1] = "p" .. tostring(row.pid) .. " " .. row.damage .. " t" .. row.tick
            end
        end
    end
    t.check("bloat.tech.leave_before_stomp", reached > 0 and stomp_taken == 0,
        reached .. " downs reached the stomp (T+29) and " .. stomp_taken .. " stomp hits landed on the party" .. (#stomp_list > 0 and (": " .. table.concat(stomp_list, ", ")) or ""))
    local protected_max = 0
    for i = 1, #fly_hits_protected do
        if fly_hits_protected[i] > protected_max then protected_max = fly_hits_protected[i] end
    end
    -- W:673 "up to 20 damage every tick, reduced by 25% if Protect from Missiles are active"
    t.check("bloat.tech.protect_from_missiles", #fly_hits_protected > 0 and protected_max <= 15,
        #fly_hits_protected .. " fly hits landed with Protect from Missiles lit from the fly's launch to its landing, the largest was " .. protected_max .. " against the protected Normal maximum of 15 (unprotected 20)")
    -- raid seam42: THE REFERENCE ROWS.  The room against Blert's 19 recorded
    -- death-free Normal trio rooms (docs/minigames/theater_of_blood/sources/
    -- blert_api/reference/bloat_normal_3.json): outcome.room_ticks 137
    -- [75-195], and no recorded room needed more than three downs inside
    -- that range (outcome.phase.down3 n=2, no down4).
    t.ticks(1)
    local _, boss_hits = rows_of({ kind = "hit_npc", slot = boss_slot })
    local room_ticks = (death_tick ~= nil and mark_tick ~= nil) and (death_tick - mark_tick) or nil
    t.check("bloat.blert.room_ticks", room_ticks ~= nil and room_ticks >= 75 and room_ticks <= 195,
        "room " .. tostring(room_ticks) .. " ticks from the mark to Bloat's death; the reference's 19 rooms: 137 [75-195]")
    t.check("bloat.blert.downs", #downs >= 1 and #downs <= 3,
        #downs .. " downs to the kill; the reference: 2 in 17 of 19 rooms, 3 in 2 (outcome.phase.down3 n=2)")
    -- the measure per down: dealt, zeros (hit_npc carries no dealer pid,
    -- so zeros are per down)
    local per_down = {}
    for i = 1, #downs do
        local dealt, hits, zeros = 0, 0, 0
        for k = 1, #boss_hits do
            local row = boss_hits[k]
            if row.tick > downs[i] and row.tick <= downs[i] + 33 then
                dealt, hits = dealt + row.damage, hits + 1
                if row.damage == 0 then zeros = zeros + 1 end
            end
        end
        per_down[#per_down + 1] = string.format("d%d t%d %d in %d hits, %d zeros", i, downs[i], dealt, hits, zeros)
    end
    local taken_by, deaths = {}, {}
    for i = 1, #player_hits do
        local pid = tostring(player_hits[i].pid)
        taken_by[pid] = (taken_by[pid] or 0) + player_hits[i].damage
    end
    local taken_list = {}
    for pid, n in pairs(taken_by) do taken_list[#taken_list + 1] = "pid" .. pid .. " " .. n end
    table.sort(taken_list)
    t.check("bloat.measure_party", true, "downs " .. #downs .. " to the kill; " .. table.concat(per_down, "; ") .. "; damage taken by pid: " .. table.concat(taken_list, ", "))
    t.check("bloat.killed", dead and not player_dead, "Bloat's npc_death row on tick " .. tostring(death_tick) .. "; " .. tostring(detail))

    -- THE MEASURE (reported against the kept tob_bloat run): duration, damage, supplies, inputs per tick
    local hist = { 0, 0, 0, 0 }
    for _, n in pairs(rec.inputs) do
        if n > 0 then hist[math.min(n, 4)] = hist[math.min(n, 4)] + 1 end
    end
    t.check("bloat.measure", true, string.format("room %s ticks (mark %s, death %s); damage taken %d; food %d, drinks %d; swings %d; inputs per tick: 1 on %d, 2 on %d, 3 on %d, 4+ on %d",
        tostring(death_tick and mark_tick and (death_tick - mark_tick)), tostring(mark_tick), tostring(death_tick), taken, #rec.eats, #rec.drinks, #rec.swings, hist[1], hist[2], hist[3], hist[4]))
end

-- THE SOTETSEG ROWS (owner_rooms4's relay snippet ROWS): the leader's reads
-- of test/raids/_play_sotetseg.lua at c58005d4d, lifted whole and prefixed
-- "sotetseg.", from the relay's own mark ("sotetseg start"), every row read
-- cut to the ticks from that mark on.  Every death ball: all three within a
-- tile at the landing and struck with the same share
-- (sotetseg.technique.trio.death_ball_shared, a row per ball).
local function sotetseg_rows(t, rec, result, detail)
    local mode, BOSS = "normal", "tob_sotetseg_combat"
    local MELEE, BALL = 8138, 8139
    local DEATH_BALL, RED, GREY = 1604, 1606, 1607
    local S = rec.sote or { mazes = {}, follows = {} }
    local tick0 = R.sotetseg.mark or rec.start_tick or 0
    -- (read from the mark's own serial on: the log is the whole raid's, and
    -- a whole-log read and filter ran the leader out of its instruction
    -- budget after Sotetseg, relay13)
    local since = mark_serial(t, tick0)
    local function rows_of(query)
        local q = {}
        for k, v in pairs(query) do q[k] = v end
        q.since = since
        return t.ticklog.rows(q)
    end
    -- THE TICK LOG (leader): what the room did, per raider (pids as the log counts them)
    t.ticks(1)
    local mark_tick = R.sotetseg.mark or rec.start_tick
    local wslot = rec.boss_slot
    local death_tick = rec.death_tick
    t.expect("sotetseg.fight.over", (death_tick ~= nil) and "ok" or "fail", "boss npc_death at tick " .. tostring(death_tick)
        .. " (room ticks " .. tostring(death_tick and (death_tick - mark_tick)) .. "), slot " .. tostring(wslot) .. "; " .. tostring(detail))
    local end_tick = death_tick or rec.end_tick or mark_tick
    local _, hits = rows_of({ kind = "hit_player" })
    t.ticks(1)
    local _, projs = rows_of({ kind = "projectile" })
    local _, rts = rows_of({ kind = "npc_retype", slot = wslot })
    t.ticks(1)
    local _, ptl = rows_of({ kind = "player_tile" })
    local _, anims = rows_of({ kind = "npc_anim", slot = wslot })
    -- damage per raider, and what dealt it
    local taken, melee_n, pids = {}, 0, {}
    for _, h in ipairs(hits) do
        if h.tick > mark_tick and h.tick <= end_tick then
            taken[h.pid] = (taken[h.pid] or 0) + h.damage
            pids[h.pid] = true
        end
    end
    for _, a in ipairs(anims) do
        if a.seq == MELEE and a.tick > mark_tick then melee_n = melee_n + 1 end
    end
    -- the mazes: his two retypes per maze (proc, re-activation); the raider in
    -- the realm (level 3) is the one the room chose
    local mazes = {}
    for i = 1, #rts, 2 do
        mazes[#mazes + 1] = { proc = rts[i].tick, react = rts[i + 1] and rts[i + 1].tick or nil, runners = {}, arena_grid = {}, blasts = 0 }
    end
    for _, r in ipairs(ptl) do
        for _, mz in ipairs(mazes) do
            if r.tick > mz.proc and r.tick <= (mz.react or end_tick) then
                if r.level == 3 then mz.runners[r.pid] = (mz.runners[r.pid] or 0) + 1 end
                local ox, oz = math.floor(r.x / 64) * 64, math.floor(r.z / 64) * 64
                local lx, lz = r.x - ox - 9, r.z - oz - 22
                if r.level == 0 and lx >= 0 and lx < 14 and lz >= 0 and lz < 15 then
                    mz.arena_grid[r.pid] = (mz.arena_grid[r.pid] or 0) + 1
                end
            end
        end
    end
    for _, h in ipairs(hits) do
        for _, mz in ipairs(mazes) do
            if h.tick > mz.proc + 3 and h.tick <= (mz.react or end_tick) and h.damage > 3 then mz.blasts = mz.blasts + 1 end
        end
    end
    local maze_lines, one_runner, no_blast, walked = {}, #mazes >= 1, true, true
    for k, mz in ipairs(mazes) do
        local rs, gs = {}, {}
        local nr = 0
        for pid, c in pairs(mz.runners) do nr = nr + 1 rs[#rs + 1] = "p" .. pid .. " " .. c end
        for pid, c in pairs(mz.arena_grid) do gs[#gs + 1] = "p" .. pid .. " " .. c end
        table.sort(rs) table.sort(gs)
        if nr ~= 1 then one_runner = false end
        if mz.blasts > 0 then no_blast = false end
        if #gs < 1 then walked = false end
        maze_lines[#maze_lines + 1] = "maze " .. k .. ": t" .. mz.proc .. "-t" .. tostring(mz.react) .. " (" .. tostring(mz.react and (mz.react - mz.proc))
            .. " ticks), runner " .. table.concat(rs, ",") .. ", arena grid ticks " .. table.concat(gs, ",") .. ", hits >3 in it " .. mz.blasts
    end
    t.expect("sotetseg.technique.trio.maze_runner_one", one_runner and "ok" or "fail", #mazes .. " mazes; " .. table.concat(maze_lines, "; "))
    t.expect("sotetseg.technique.trio.maze_no_blast", (#mazes >= 1 and no_blast) and "ok" or "fail",
        "no hit over 3 (a wrong tile's blast, 15 + 6.67% hp; the tornado's 35-45) on anyone while a maze was on: " .. table.concat(maze_lines, "; "))
    t.check("sotetseg.technique.trio.maze_followed", #mazes >= 1 and walked, "the raiders thrown to the far end walked the arena's grid behind the glow: " .. table.concat(maze_lines, "; "))
    -- the death balls: each landing's splats, by raider (W:794 split; all three on
    -- the front tile, yt_4i4lv-srJkw.md:95)
    local balls, shared_all, ball_lines = 0, true, {}
    for _, r in ipairs(projs) do
        if r.spotanim == DEATH_BALL and r.tick > mark_tick and r.tick <= end_tick then
            balls = balls + 1
            local land = r.tick + math.floor((r.end_cycle or 0) / 30)
            -- the share is one splat of one size on each raider within a tile
            -- of the target, on one tick (S tob_sote_ball_impact: total /
            -- sharing to each): the largest group of equal splats from him
            -- from the landing to two after (a raider earlier in the tick's
            -- order shows its splat a tick later: _play_sotetseg t206/t207)
            local got, n = {}, 0
            local by = {}
            for _, h in ipairs(hits) do
                if h.tick >= land and h.tick <= land + 2 and h.npc_slot == wslot and h.damage > 0 then
                    by[h.damage] = by[h.damage] or {}
                    by[h.damage][h.pid] = h.damage
                end
            end
            for _, group in pairs(by) do
                local c = 0
                for _ in pairs(group) do c = c + 1 end
                if c > n then got, n = group, c end
            end
            local parts = {}
            for pid, d in pairs(got) do parts[#parts + 1] = "p" .. pid .. " " .. d end
            table.sort(parts)
            -- "incoming attacks made by Sotetseg, including the big red ball,
            -- will not deal damage" while a maze is on (W:799; S
            -- tob_sote_maze_nulls_hit): a ball landing inside one is not judged
            local nulled = false
            for _, mz in ipairs(mazes) do
                if land >= mz.proc and land <= (mz.react or end_tick) then nulled = true end
            end
            if nulled then
                balls = balls - 1
                parts[#parts + 1] = "nulled by the maze"
            elseif land <= end_tick then
                -- the technique: every raider within a tile of one raider's
                -- tile at the landing (S tob_sote_ball_impact hunts radius 1
                -- around the target) -- the splats themselves can be 0 for all
                -- but one (1 + random(121) split three ways, through a prayer)
                local at = {}
                for _, pt in ipairs(ptl) do
                    if pt.tick == land - 1 and pt.level == 0 then at[#at + 1] = pt end
                end
                local stacked = false
                for _, c in ipairs(at) do
                    local all = true
                    for _, o in ipairs(at) do
                        if math.abs(o.x - c.x) > 1 or math.abs(o.z - c.z) > 1 then all = false end
                    end
                    if all then stacked = true end
                end
                stacked = stacked and #at == size
                parts[#parts + 1] = (stacked and "all " .. #at .. " within a tile at t" .. (land - 1)) or ("NOT stacked at t" .. (land - 1))
                if not stacked then shared_all = false end
                -- OWNER 2026-10-07: "the raiders should share the red ball" -- and
                -- the content splits it among every raider within a tile of the
                -- target (tob_sotetseg.rs2 [queue,tob_sote_ball_impact], radius 1,
                -- the wiki's 3x3): every raider struck by THIS ball, the same
                -- share each, or it was not shared (owner_rooms4)
                if n > 0 and n ~= size then   -- (a roll under 3 shares 0 each: no splat to count)
                    shared_all = false
                    parts[#parts + 1] = "struck " .. n .. " of " .. size
                end
            end
            if not nulled and land > end_tick then parts[#parts + 1] = "landed after his death" end
            ball_lines[#ball_lines + 1] = "t" .. r.tick .. " land t" .. land .. ": " .. n .. " raiders (" .. table.concat(parts, ",") .. ")"
        end
    end
    -- raid seam50: a room whose only death ball landed inside a maze (nulled,
    -- W:799) has nothing to judge: the row is absent on that seed, never a
    -- FAIL on nothing (seam49 s2 _play_sotetseg: t190 land t205, nulled) nor
    -- a PASS on nothing; play.measure carries the line
    if balls >= 1 then
        t.expect("sotetseg.technique.trio.death_ball_shared", shared_all and "ok" or "fail",
            balls .. " death balls, the party stacked within a tile at every landing (the splats: the largest equal group, landing to +2): " .. table.concat(ball_lines, "; "))
    end
    -- the balls: blocked splats (hitsplat 26, 0) against ones that hurt
    local blocked, hurt = 0, 0
    for _, h in ipairs(hits) do
        if h.tick > mark_tick and h.tick <= end_tick then
            if h.hitsplat == 26 and h.damage == 0 and h.npc_slot == -1 then blocked = blocked + 1 end
        end
    end
    -- the room's end: the real chat line (tob_sotetseg.lua :1578-1590's read)
    if death_tick ~= nil then
        t.ticks(10)
        local _, wl = t.msg.last(30)
        local wave_raw = nil
        for _, m in ipairs(wl or {}) do
            if string.find(m.text, "complete!", 1, true) and string.find(m.text, "Wave", 1, true) then wave_raw = m.text end
        end
        local wave_txt = wave_raw and string.gsub(string.gsub(wave_raw, "<br>", " "), "<[^>]*>", "") or "none"
        t.check("sotetseg.room.wave_line", wave_raw ~= nil and string.find(wave_txt, "(Normal Mode) complete!", 1, true) ~= nil,
            "chat line after his death: " .. wave_txt)
    end
    local per = {}
    for pid, _ in pairs(pids) do per[#per + 1] = "p" .. pid .. " " .. (taken[pid] or 0) end
    table.sort(per)
    local hist = { 0, 0, 0, 0 }
    for _, n in pairs(rec.inputs) do
        if n > 0 then hist[math.min(n, 4)] = hist[math.min(n, 4)] + 1 end
    end
    -- raid seam50 play_tob_sotetseg_whole: THE REFERENCE ROWS.  The room
    -- against the 20 recorded death-free Normal trio rooms on Blert
    -- (docs/minigames/theater_of_blood/sources/blert_api/reference/
    -- sotetseg_normal_3.json): outcome.room_ticks 212.5 [164-262];
    -- outcome.hp_lost per raider melee1 92.5 [27-155], melee2 108 [3-225],
    -- melee3 105 [83-135] (the roles are the recorder's labels, so a raider
    -- is held to their union [3-225]); outcome.deaths 0 [0-0]; the maze, proc
    -- to his combat form back, 28 [14-47] over 26 mazes (blert_api/
    -- sote_maze.csv reactivate_tick - proc_tick).
    -- OWNER RULING 2026-10-07 (owner_rooms4, via the coordinator): "If sotetseg
    -- is completing but just a bit slower, count it as good, don't worry about
    -- meeting blert times."  So for SOTETSEG ONLY the room's green is that it
    -- completes (his death row, no raider dead: the deaths row below); the
    -- reference's [164-262] is printed beside it, not asserted.  The other rooms'
    -- time bounds stand.
    local room_ticks = death_tick and (death_tick - mark_tick) or nil
    t.check("sotetseg.blert.room_ticks", room_ticks ~= nil,
        "room " .. tostring(room_ticks) .. " ticks from the mark to his death (completion is the bound, owner 2026-10-07); the reference's 20 rooms: 212.5 [164-262]"
        .. ((room_ticks ~= nil and room_ticks > 262) and " -- slower than every recorded room" or ""))
    -- OWNER RULING 2026-10-07 (owner_rooms4, via the coordinator): "Missing by 2
    -- hitpoints is fine. Maybe loosen the requirements."  The bound was the
    -- reference's largest seat, 225 (also the largest of the 37 seats with
    -- hitpoints in build/blert/sotetseg's scale-3 PLAYER updates); _play lost
    -- 227 on one seat (two melees on a ball's landing tick).  It is 250 now,
    -- the coordinator's cap for the loosened bound; the reference is printed.
    local HP_LOST_BOUND = 250
    local hp_ok = #per >= 1
    for pid, _ in pairs(pids) do
        if (taken[pid] or 0) > HP_LOST_BOUND then hp_ok = false end
    end
    t.check("sotetseg.blert.hp_lost", hp_ok, "hitpoints lost a raider " .. table.concat(per, ", ") .. " (bound " .. HP_LOST_BOUND .. ", owner 2026-10-07); the reference: melee1 92.5 [27-155], melee2 108 [3-225], melee3 105 [83-135]")
    local maze_ok, maze_d = #mazes >= 1, {}
    for _, mz in ipairs(mazes) do
        local d = mz.react and (mz.react - mz.proc) or nil
        maze_d[#maze_d + 1] = tostring(d)
        if d == nil or d < 14 or d > 47 then maze_ok = false end
    end
    t.check("sotetseg.blert.maze_ticks", maze_ok, "mazes proc to back " .. table.concat(maze_d, ", ") .. " ticks; the reference's 26 mazes: 28 [14-47]")
    t.check("sotetseg.play.measure", true, string.format("room %s ticks (mark %s, death %s); damage taken %s; his melee swings %d; "
        .. "balls blocked %d; death balls %d; mazes %d; leader eats %d, drinks %d, swings %d; inputs 1/2/3/4+ %d/%d/%d/%d",
        tostring(death_tick and (death_tick - mark_tick)), tostring(mark_tick), tostring(death_tick), table.concat(per, ", "),
        melee_n, blocked, balls, #mazes, #rec.eats, #rec.drinks, #rec.swings, hist[1], hist[2], hist[3], hist[4]))
end

-- THE XARPUS ROWS (owner_rooms4's relay snippet ROWS): the leader's reads of
-- test/raids/_play_xarpus.lua at 02a87d0d8, lifted whole and prefixed
-- "xarpus.", from the relay's own mark ("xarpus start"); his slot is the
-- plan's (rec.boss_slot: the static form retypes in place), the fight's
-- first tick the mark.  The exit rows are the relay's own POST.xarpus.
local function xarpus_rows(t, rec, result, detail)
    local mode = "normal"
    local XA = rec.xa or { covers = {}, turns = {}, phase = 0, stops = 0, moves3 = 0, waits3 = 0 }
    local bslot = rec.boss_slot
    local ft0 = R.xarpus.mark
    local tick0 = R.xarpus.mark or rec.start_tick or 0
    -- (read from the mark's own serial on: the log is the whole raid's, and
    -- a whole-log read and filter ran the leader out of its instruction
    -- budget after Sotetseg, relay13)
    local since = mark_serial(t, tick0)
    local function rows_of(query)
        local q = {}
        for k, v in pairs(query) do q[k] = v end
        q.since = since
        return t.ticklog.rows(q)
    end
    -- THE TICK LOG (leader): every raider's evidence
    local mark_tick = R.xarpus.mark
    ft0 = ft0 or mark_tick or 0
    local _, drr = rows_of({ kind = "npc_death", slot = bslot })
    local kill = nil
    for i = 1, #drr do if kill == nil and drr[i].tick >= ft0 then kill = drr[i] end end
    local kill_tick = kill and kill.tick or 1000000
    local _, rtu = rows_of({ kind = "npc_retype", slot = bslot })
    local U, seen = nil, 0
    for i = 1, #rtu do
        if rtu[i].tick >= ft0 then
            seen = seen + 1
            if seen == 2 then U = rtu[i].tick end
        end
    end
    U = U or ft0
    local _, ptr = rows_of({ kind = "player_tile" })
    local tile_at, pids, pid_list = {}, {}, {}
    local died = {}
    local last_tile = {}
    for i = 1, #ptr do
        local r = ptr[i]
        if pids[r.pid] == nil then pids[r.pid] = true; pid_list[#pid_list + 1] = r.pid; tile_at[r.pid] = {} end
        tile_at[r.pid][r.tick] = { x = r.x, z = r.z }
        local lt = last_tile[r.pid]
        if lt ~= nil and r.tick > ft0 and r.tick <= kill_tick and math.abs(r.x - lt.x) + math.abs(r.z - lt.z) > 20 then died[r.pid] = r.tick end
        last_tile[r.pid] = { x = r.x, z = r.z }
    end
    table.sort(pid_list)
    local function anyone_on(tick, x, z)
        for _, pid in ipairs(pid_list) do
            local tt = tile_at[pid][tick]
            if tt ~= nil and tt.x == x and tt.z == z then return pid end
        end
        return nil
    end
    -- the exhumed: risen, stood on (any raider on its tile before it closed), heal orbs
    local _, lsr = rows_of({ kind = "loc_set" })
    local rises, ring_pools, pools = {}, 0, 0
    local ring_list = ""
    local function fdist(x, z)
        return math.max(6432 - x, x - 6436, 97 - z, z - 101, 0)
    end
    for i = 1, #lsr do
        local r = lsr[i]
        if r.tick >= ft0 and r.tick <= kill_tick then
            if r.loc == 32743 then
                rises[#rises + 1] = { tick = r.tick, x = r.x, z = r.z }
            elseif r.loc == 32744 and r.tick >= U then
                pools = pools + 1
                if fdist(r.x, r.z) == 1 then
                    ring_pools = ring_pools + 1
                    if ring_pools <= 8 then ring_list = ring_list .. string.format(" t%d %d,%d", r.tick, r.x, r.z) end
                end
            end
        end
    end
    local stood, stood_by, late = 0, "", 0
    for _, e in ipairs(rises) do
        local who, when = nil, nil
        for tk = e.tick, e.tick + 11 do
            if who == nil then
                who = anyone_on(tk, e.x, e.z)
                if who ~= nil then when = tk end
            end
        end
        if who ~= nil then
            stood = stood + 1
            stood_by = stood_by .. string.format(" %d,%d:p%d+%d", e.x, e.z, who, when - e.tick)
        else
            stood_by = stood_by .. string.format(" %d,%d:MISSED", e.x, e.z)
        end
    end
    local _, pjr = rows_of({ kind = "projectile" })
    local orbs, orbs_covered = 0, 0
    for i = 1, #pjr do
        if pjr[i].tick >= ft0 and pjr[i].spotanim == 1550 then
            orbs = orbs + 1
            if anyone_on(pjr[i].tick - 1, pjr[i].src_x, pjr[i].src_z) ~= nil then orbs_covered = orbs_covered + 1 end
        end
    end
    local _, nhr = rows_of({ kind = "npc_heal" })
    local healed = 0
    for i = 1, #nhr do
        if nhr[i].tick >= ft0 and nhr[i].tick <= kill_tick then healed = healed + (nhr[i].amount or 0) end
    end

    -- damage taken per raider: phase 2 (the spit, his stomp) and phase 3 (a
    -- retaliation is 50 and up on Normal, X xarpus.p3.retaliate_base)
    local p3 = XA.p3_tick or kill_tick
    local _, hur = rows_of({ kind = "hit_player" })
    local taken, retal = {}, {}
    local retal_n = 0
    for _, pid in ipairs(pid_list) do taken[pid] = { dmg = 0, n = 0 } end
    for i = 1, #hur do
        local h = hur[i]
        if h.tick >= ft0 and h.tick <= kill_tick and (h.damage or 0) > 0 and taken[h.pid] ~= nil then
            taken[h.pid].dmg = taken[h.pid].dmg + h.damage
            taken[h.pid].n = taken[h.pid].n + 1
            if h.tick > p3 and h.damage >= 30 then
                retal_n = retal_n + 1
                retal[#retal + 1] = string.format("t%d p%d %d", h.tick, h.pid, h.damage)
            end
        end
    end
    local _, par = rows_of({ kind = "player_anim" })
    local swings = {}
    for _, pid in ipairs(pid_list) do swings[pid] = 0 end
    for i = 1, #par do
        if par[i].tick >= ft0 and par[i].tick <= kill_tick and par[i].seq == 8056 and swings[par[i].pid] ~= nil then
            swings[par[i].pid] = swings[par[i].pid] + 1
        end
    end
    local _, hnr = rows_of({ kind = "hit_npc", slot = bslot })
    local dealt, dealt_n, zeros = 0, 0, 0
    for i = 1, #hnr do
        if hnr[i].tick >= ft0 and hnr[i].tick <= kill_tick then
            dealt = dealt + (hnr[i].damage or 0)
            dealt_n = dealt_n + 1
            if (hnr[i].damage or 0) == 0 then zeros = zeros + 1 end
        end
    end
    local per = {}
    local dead_list = {}
    for _, pid in ipairs(pid_list) do
        per[#per + 1] = string.format("pid%d took %d in %d, %d swings", pid, taken[pid].dmg, taken[pid].n, swings[pid])
        if died[pid] ~= nil then dead_list[#dead_list + 1] = "pid" .. pid .. " t" .. died[pid] end
    end
    local hist = { 0, 0, 0, 0 }
    for _, n in pairs(rec.inputs or {}) do
        local k = math.min(n, 4)
        if k >= 1 then hist[k] = hist[k] + 1 end
    end
    local tr = XA.trio or { outs = 0, ins = 0, no_out = 0, home_walks = 0 }

    -- the spread (phase 2, raid seam42): real Normal trios hold their own
    -- sides -- each raider's nearest other raider is a median 5 tiles away
    -- over the phase, 3 to 6 across 13 death-free Blert rooms
    -- (docs/minigames/theater_of_blood/sources/blert_api/reference/
    -- xarpus_normal_3.json, role.melee1/2/3.phase.phase1.dist_raider), and
    -- 0 of the phase's ticks in 20 rooms had all three on one tile
    local spread_ok, spread_list = true, {}
    for _, pid in ipairs(pid_list) do
        local ds = {}
        for tk = U + 8, p3 - 1 do
            local me = tile_at[pid][tk]
            if me ~= nil then
                local near = nil
                for _, o in ipairs(pid_list) do
                    local ot = (o ~= pid) and tile_at[o][tk] or nil
                    if ot ~= nil then
                        local dd = math.max(math.abs(ot.x - me.x), math.abs(ot.z - me.z))
                        if near == nil or dd < near then near = dd end
                    end
                end
                if near ~= nil then ds[#ds + 1] = near end
            end
        end
        table.sort(ds)
        local med = ds[math.floor((#ds + 1) / 2)]
        spread_list[#spread_list + 1] = "pid" .. pid .. " " .. tostring(med)
        if med == nil or med < 3 or med > 6 then spread_ok = false end
    end

    -- THE ROWS
    t.expect("xarpus.fight.done", (kill ~= nil) and "ok" or "bad", "boss npc_death row " .. tostring(kill and kill.tick) .. ", mark " .. tostring(mark_tick) .. ", stand-up U " .. tostring(U) .. ", screech seen " .. tostring(XA.p3_tick))
    t.check("xarpus.trio.no_death", #pid_list == size and #dead_list == 0, #pid_list .. " raiders in the tick log; deaths: " .. (#dead_list > 0 and table.concat(dead_list, ", ") or "none"))
    t.check("xarpus.trio.exhumed_cover", #rises >= 12 and stood == #rises, "stood on " .. stood .. " of " .. #rises .. " exhumed (X exhumed_count.normal 12 at party 3); heal orbs " .. orbs .. " (" .. orbs_covered .. " from a covered tile), healed " .. healed .. ";" .. stood_by)
    -- raid seam42: the bound is the real rooms', not zero: 13 death-free
    -- Blert Normal trio rooms leave 2 to 7 (median 4) phase 2 splats on
    -- distinct melee tiles (reference/xarpus_normal_3.json
    -- extra_seam42.ring_splat_tiles); the spread leaves 0-3
    t.check("xarpus.trio.ring_clean", ring_pools <= 7, ring_pools .. " of " .. pools .. " phase 2 puddles on a melee tile (one from his footprint; Blert death-free Normal trios leave 2-7, median 4, on distinct melee tiles: xarpus_blert/ring_splats, reference/xarpus_normal_3.json extra_seam42.ring_splat_tiles)" .. ring_list .. "; steps back " .. tr.outs .. ", steps in " .. tr.ins .. ", no clean tile " .. tr.no_out)
    t.check("xarpus.trio.spread", spread_ok, "phase 2 (U+8 to the screech): each raider's median distance to the nearest other raider " .. table.concat(spread_list, ", ") .. " (Blert Normal trios 5 [3-6], reference/xarpus_normal_3.json)")
    t.check("xarpus.play.gaze_kept", XA.phase == 3 and retal_n == 0, retal_n .. " retaliation hitsplat(s) after the screech" .. (#retal > 0 and (": " .. table.concat(retal, ", ")) or "") .. "; " .. #(XA.turns or {}) .. " turns seen by p1")
    t.check("xarpus.play.measure_trio", true, string.format("p1 specials%s (energy before each); p1 steps back %s; kill %s ticks from the mark (%s to %s), U %s (p1 saw %s), screech %s; dealt %d in %d hits (%d zeros); %s; p1 food %d drinks %d; p1 inputs per tick 1:%d 2:%d 3:%d 4+:%d; %s",
        tostring(tr.spec_log), table.concat(tr.out_list or {}, " "),
        tostring(kill and mark_tick and (kill.tick - mark_tick)), tostring(mark_tick), tostring(kill and kill.tick), tostring(U), tostring(XA.u_tick), tostring(XA.p3_tick),
        dealt, dealt_n, zeros, table.concat(per, "; "), #rec.eats, #rec.drinks, hist[1], hist[2], hist[3], hist[4], string.sub(tostring(detail), 1, 400)))
end

local PRE = {}

-- _play_maiden.lua party_run: the bow on rapid (raid seam33), the loaded
-- pipe read back on its rangers, the leader's barrier, the members across.
PRE.maiden = function(t, ox, oz)
    -- owner_tob_normal 2026-10-07: _play_maiden.lua party_run (green on the
    -- owner's ruling): the freezer's bow on Rapid; a scythe seat puts the
    -- scythe on to press Chop, then the bow back for the run-in shot
    local mr = ROLE_IN.maiden[role]
    if mr ~= 2 then wear(t, "maiden.equip.scythe", "scythe_of_vitur") end
    set_style(t, "maiden.style", mr == 2 and "Rapid" or "Chop")
    if mr ~= 2 then wear(t, "maiden.equip.bow", "twisted_bow") end
    -- the boosts at the door: a scythe seat's super combat, every seat's
    -- ranging potion, the freezer's saturated heart, a fish eaten at full
    -- (the 24 streams' rows: 118 / 112 / 112 / 120-121)
    if mr ~= 2 then
        t.player.inv_op("4dose2combat", 1, { quick = true })
        t.ticks(1)
    end
    t.ticks(3)
    t.player.inv_op("4doserangerspotion", 1, { quick = true })
    t.ticks(2)
    if mr == 2 then
        t.player.inv_op("saturated_heart", 1, { quick = true })
        t.ticks(2)
    end
    t.player.inv_op("anglerfish", 1, { quick = true })
    t.ticks(3)
    local _, rg = t.skill.read("ranged")
    local _, mg = t.skill.read("magic")
    t.check("maiden.boosts", rg ~= nil and rg.level > 99, P .. "at the door: ranged " .. tostring(rg and rg.level) .. " magic " .. tostring(mg and mg.level))
    if mr ~= 2 then
        local bpr, bp = t.inv.blowpipe()
        t.check("maiden.blowpipe", bpr == "ok" and type(bp) == "table" and bp.darts > 0 and bp.scales > 0,
            P .. tostring(bpr) .. " " .. tostring(type(bp) == "table" and bp.line or bp))
    end
    start_room(t, "maiden", nil, nil)
    return { weapon = (mr == 2) and "twisted_bow" or "scythe_of_vitur", max_ticks = 1400 }
end

-- _play_bloat.lua: the super combat; the scythe (the plan's weapon); p1 is
-- in the room on the first walk, crossing when Bloat is on the far row
-- heading west (W:687); p2/p3 enter on the first down, seq 8082 (W:689).
local BLOAT_STARTER = 1  -- the seat playing Bloat role 1 (ROLE_IN.bloat)
-- Seat 1 (Maiden's freezer, whose worn set is the void opener) into its melee
-- armour for a melee room, one piece a tick; the void pieces go to the pack.
local SEAT1_MELEE = { "torva_helm", "radiant_oathplate_chest", "radiant_oathplate_legs", "amulet_of_rancour", "ferocious_gloves" }
local function seat1_melee(t, name)
    if role ~= 1 then return end
    for _, it in ipairs(SEAT1_MELEE) do wear(t, name .. ".equip." .. it, it) end
end
local function seat1_void(t, name)
    if role ~= 1 then return end
    for _, it in ipairs({ "game_pest_archer_helm", "elite_void_knight_top", "elite_void_knight_robes", "pest_void_knight_gloves" }) do wear(t, name .. ".equip." .. it, it) end
end
PRE.bloat = function(t, ox, oz)
    wear(t, "bloat.equip.scythe", "scythe_of_vitur")
    seat1_melee(t, "bloat")
    -- the salve amulet(ei) (owner_rooms4's snippet: Bloat is undead; the
    -- Blert raiders wear it on 74 of 90 seats; seam54 moved the room from
    -- 202 ticks to 133-148 with it)
    wear(t, "bloat.equip.salve", "lotr_crystalshard_necklace_upgrade")
    set_style(t, "bloat.style", "Reap")
    boost(t, "bloat")
    stamina(t, "bloat")
    start_room(t, "bloat", function()
        t.player.walk_to(ox + 42, oz + 31, 1)
        local wait_ticks, wait_x = 0, nil
        while wait_ticks < 60 do
            local wr, wb = t.npc.state("tob_bloat")
            if wr == "ok" and wb.z == oz + 24 and wb.x <= ox + 32 and wb.x >= ox + 31 and wait_x ~= nil and wb.x < wait_x then break end
            if wr == "ok" then wait_x = wb.x end
            wait_ticks = wait_ticks + 1
            t.ticks(1)
        end
    end, function()
        -- raid seam53: the whole trio in from the start, as _play_bloat.lua
        -- since raid seam42 (15 of 22 recorded death-free Normal trio rooms;
        -- a raider who enters on the first down swings first at age 7-9
        -- against the reference's 3).  The relay still waited for the down
        -- (survey1: 37 ticks).
        cross(t, "bloat")
    end, BLOAT_STARTER)
    return { weapon = "scythe_of_vitur", max_ticks = 1400 }
end

-- _play_nylocas.lua size > 1: each seat its own colour's weapon on (the
-- mage the Ayak, the ranger the pipe, the meleer the whip), rune arrows for
-- the shortbow, rapid, auto retaliate off; the leader from beside the fight
-- tile.
PRE.nylocas = function(t, ox, oz)
    -- owner_nylocas' relay snippet (build/seam_state/owner_nylocas/
    -- relay_nylocas_snippet.lua, the room green in its harness at feac33107):
    -- the mage's style set on the pipe, then the Ayak (a powered staff's tab
    -- reads Accurate / Accurate / Longrange on this content now: no Pound)
    local nrole = ROLE_IN.nylocas[role]
    if nrole == 1 then
        -- (the relay's mage is Maiden's freezer, whose whole-raid pack has no
        -- blowpipe: the Ayak on, then its Accurate by name -- the slot the
        -- harness's pipe "Rapid" (1) leaves on the Ayak reads Accurate too)
        wear(t, "nylocas.equip.own", "eye_of_ayak")
        set_style(t, "nylocas.style", "Accurate")
    elseif nrole == 2 then
        -- (the harness's ranger WEARS the void ranged set and rupture -- the
        -- snippet's KIT -- with the melee set in the pack for her melee form;
        -- relay20-22's ranger stayed in ::tobkit's melee armour from Bloat and
        -- took 107-137 from the big nylos against the harness's 13)
        for _, it in ipairs({ "game_pest_archer_helm", "elite_void_knight_top", "elite_void_knight_robes",
            "pest_void_knight_gloves", "necklace_of_rupture" }) do
            wear(t, "nylocas.equip." .. it, it)
        end
        wear(t, "nylocas.equip.own", "toxic_blowpipe_loaded")
        set_style(t, "nylocas.style", "Rapid")
    else
        wear(t, "nylocas.equip.own", "abyssal_whip")
        set_style(t, "nylocas.style", "Lash")
    end
    wear(t, "nylocas.equip.arrows", "dragon_arrow")
    set_retaliate(t, "nylocas.retaliate_off", true)
    -- the boosts at the door, as owner_nylocas' snippet and harness drink them
    -- (the room was measured boosted: Magic and Ranged 112, Attack and
    -- Strength 118): a divine super combat, a divine ranging potion -- seat 1,
    -- whose pack holds its melee armour instead, a dose of its Maiden ranging
    -- potion -- and the mage's saturated heart
    -- (pressed again while the pack still holds the same dose: a press inside
    -- the last potion's delay answers nothing -- relay18's seat 2 read ranged
    -- 88 after its divine ranging press)
    local function door_dose(list)
        local dose = first_held(t, list)
        if dose == nil then return nil end
        local _, n0 = t.inv.count(dose)
        for _ = 1, 3 do
            t.player.inv_op(dose, 1, { quick = true })
            t.ticks(3)
            local _, n1 = t.inv.count(dose)
            if n1 ~= n0 then break end
        end
        return dose
    end
    -- a super restore first while a combat stat is under its base (owner_nylocas,
    -- relay19: seat 2 came in at Ranged 75 / Magic 67, brewed down at Bloat,
    -- and its divine ranging only reached 88)
    for _ = 1, 3 do
        local low = false
        for _, sk in ipairs({ "attack", "strength", "ranged", "magic" }) do
            local _, rd = t.skill.read(sk)
            if rd ~= nil and rd.level < rd.base_level then low = true end
        end
        if not low or door_dose(RESTORE_DOSES) == nil then break end
    end
    -- (the plain super combat first: the divine's doses are Sotetseg's --
    -- relay25's seats 1 and 2 reached him with none, at Attack 115-116
    -- against the harness's 118)
    local dc = door_dose({ "1dose2combat", "2dose2combat", "3dose2combat", "4dose2combat",
        "1dosedivinecombat", "2dosedivinecombat", "3dosedivinecombat", "4dosedivinecombat" })
    local dr = door_dose({ "1dosedivinerange", "2dosedivinerange", "3dosedivinerange", "4dosedivinerange",
        "1doserangerspotion", "2doserangerspotion", "3doserangerspotion", "4doserangerspotion" })
    if nrole == 1 then
        -- (pressed again until Magic moves: relay19's mage stayed at 99)
        local _, m0 = t.skill.read("magic")
        for _ = 1, 3 do
            t.player.inv_op("saturated_heart", 1, { quick = true })
            t.ticks(2)
            local _, m1 = t.skill.read("magic")
            if m1.level > m0.level then break end
        end
    end
    local _, rg = t.skill.read("ranged")
    local _, mgk = t.skill.read("magic")
    local _, sg = t.skill.read("strength")
    t.check("nylocas.boosts", rg.level > rg.base_level, P .. tostring(dc) .. ", " .. tostring(dr) .. " at the door: ranged " .. rg.level
        .. ", strength " .. sg.level .. ", magic " .. mgk.level)
    stamina(t, "nylocas")
    -- the leader beside the fight tile BEFORE the crossing, the members to
    -- the barrier's north side (harness door.together)
    if role == 1 then
        local fr, fight, ftext = t.raid.start_tile()
        t.check("nylocas.start_tile", fr == "ok", tostring(ftext))
        t.player.walk_to(fight.x + 1, fight.z, 20)
    else
        t.player.walk_to(ox + 29 + role, oz + 33, 30)
    end
    start_room(t, "nylocas", nil, nil)
    t.drive.camera(0, 512, 1100)
    return { max_ticks = 3000 }
end

-- _play_sotetseg.lua trio_run: the super combat, Protect from Magic ("to
-- start the room pray magic and piety", yt_KF9y2GYTJ-A.md:151), the leader
-- from two tiles south of the fight tile.
-- owner_rooms4's relay snippet (build/seam_state/owner_rooms4/
-- relay_sotetseg_snippet.lua; the room green in _play_sotetseg.lua --party 3
-- at c58005d4d): the scythe on and "Reap" BEFORE the bow (the style is per
-- weapon kind), the dragon arrows, the bow opener's ranged set worn one a
-- tick (seven in one block put on only some; the snippet's Dizana's quiver
-- is not carried, see SUPPLIES), the bow, the divine super combat at the door (Blert's seats hold Attack and
-- Strength 118 five ticks into every phase; no brews at Sotetseg).  The
-- plan puts the melee set back itself; the seat owning the coming special
-- puts the elder maul on.
local DIVINE_COMBAT = { "1dosedivinecombat", "2dosedivinecombat", "3dosedivinecombat", "4dosedivinecombat" }
PRE.sotetseg = function(t, ox, oz)
    wear(t, "sotetseg.equip.scythe", "scythe_of_vitur")
    set_style(t, "sotetseg.style", "Reap")
    wear(t, "sotetseg.equip.arrows", "dragon_arrow")
    for _, it in ipairs({ "game_pest_archer_helm", "elite_void_knight_top", "elite_void_knight_robes",
        "pest_void_knight_gloves", "necklace_of_rupture" }) do
        wear(t, "sotetseg.equip." .. it, it)
    end
    wear(t, "sotetseg.equip.bow", "twisted_bow")
    -- a super restore first when a brew left Attack or Strength under base
    -- (boost()'s rule), then the divine dose (a plain one when none is left)
    local divine = first_held(t, DIVINE_COMBAT)
    if divine ~= nil then
        local _, s0 = t.skill.read("strength")
        if s0.level < s0.base_level then
            local dose = first_held(t, RESTORE_DOSES)
            if dose ~= nil then t.player.inv_op(dose, 1, { quick = true }) t.ticks(3) end
        end
        t.player.inv_op(divine, 1, { quick = true })
        t.ticks(3)
        local _, s1 = t.skill.read("strength")
        t.check("sotetseg.potion", s1.level > s1.base_level, P .. divine .. " at the door: strength " .. s1.level .. "/" .. s1.base_level)
    else
        boost(t, "sotetseg")
    end
    t.exec("sotetseg.prayer", t.prayer.set, "protectfrommagic", true)
    start_room(t, "sotetseg", function()
        local fr, fight, ftext = t.raid.start_tile()
        t.check("sotetseg.start_tile", fr == "ok", tostring(ftext))
        t.player.walk_to(fight.x, fight.z - 2, 20)
    end, nil)
    -- (a member walks in off the barrier before its play: relay25-30's seats
    -- 2 and 3 stood on the barrier tile 20 tiles from him, their bow press
    -- framed nothing -- "attack tob_sotetseg_combat op2: pose 3 framed nothing
    -- (not_visible)", no input reached the server for 10 ticks -- and they
    -- reached him 6-10 ticks after seat 1, whose corner then took a ricochet
    -- of each style 0.9 ticks apart at room tick 22: the missed prayer blocks
    -- protection for 5 ticks (tob_sotetseg.rs2 ~prayer_block_protection) and
    -- his next four hits killed it.  The harness's members are walking in
    -- when they loose the bow, and arrive with the leader.)
    if role ~= 1 then
        local _, here = t.world.tile()
        t.player.walk_to(here.x, here.z + 8, 6)
    end
    return { mode = "normal", weapon = "scythe_of_vitur", max_ticks = 2400 }
end

-- _play_xarpus.lua trio_run: the super combat; the fight tile's local 34,28
-- (a member has no ::tob readout), three tiles south of it.
PRE.xarpus = function(t, ox, oz)
    wear(t, "xarpus.equip.scythe", "scythe_of_vitur")
    seat1_melee(t, "xarpus")
    boost(t, "xarpus")
    stamina(t, "xarpus")
    start_room(t, "xarpus", function()
        local fr, fight, ftext = t.raid.start_tile()
        t.check("xarpus.start_tile", fr == "ok", tostring(ftext))
        t.player.walk_to(fight.x, fight.z - 3, 20)
    end, function()
        t.player.walk_to(ox + 34, oz + 28 - 3, 20)
        cross(t, "xarpus")
    end)
    return { weapon = "scythe_of_vitur", max_ticks = 1400 }
end

-- _play_verzik.lua party_run: the scythe for P1 (W:891), the dragon arrows
-- back on for the bow, Protect from Magic (W:871 "All players must have
-- Protect from Magic on before starting the fight"); the leader talks (re-
-- talked from up the carpet when no dialogue opened: _play_entry.lua).
PRE.verzik = function(t, ox, oz)
    wear(t, "verzik.equip.arrows", "dragon_arrow")
    wear(t, "verzik.equip.scythe", "scythe_of_vitur")
    seat1_melee(t, "verzik")
    stamina(t, "verzik")
    -- owner_verzik's slow snippet (build/seam_state/owner_verzik/
    -- relay_verzik_snippet.lua; green in _play_verzik_slow --party 3 at
    -- 1717b0427 + 5f2717f23): the slow pace -- every P3 mechanic, crabs, webs,
    -- yellows, the shared green ball (owner: "The relay should use the slow
    -- verzik script") -- and seats 2 and 3 carry no super combat (the room
    -- was measured without their boosts; the brew recovery would drink one)
    t.raid.verzik_pace = "slow"
    if role >= 2 then
        local left = {}
        for _, dose in ipairs(COMBAT_DOSES) do
            local cr, n = t.inv.count(dose)
            if cr == "ok" and (tonumber(n) or 0) > 0 then
                for _ = 1, tonumber(n) do t.player.drop(dose) end
                left[#left + 1] = dose .. " x" .. n
            end
        end
        t.check("verzik.no_combat", true, P .. "super combat doses left on the floor: " .. (#left > 0 and table.concat(left, " ") or "none held"))
    end
    t.exec("verzik.prayer", t.prayer.set, "protectfrommagic", true)
    t.expect("verzik.barrier.ready", t.party.barrier("verzik_ready", 600))
    if role == 1 then
        local tr, td, talks, opened = nil, nil, 0, false
        while talks < 3 and not opened do
            talks = talks + 1
            if talks > 1 then
                local _, here = t.world.tile()
                t.player.walk_to(here.x, here.z + 4, 10)
            end
            tr, td = t.player.talk_to("verzik_initial", 1)
            opened = tr == "ok" and t.chat.kind() ~= "none"
        end
        t.check("verzik.talk", opened, "dialogue open after " .. talks .. " talk(s): " .. string.sub(tostring(td), 1, 200))
        local cr, cd = t.chat.play({ "npc:So, you wish to entertain me", "options", "choose:Yes, begin the fight." })
        t.check("verzik.begin", cr == "ok", tostring(cd))
        R.verzik.mark = mark_tick(t, "verzik start")
    end
    t.expect("verzik.barrier.started", t.party.barrier("verzik_started", 900))
    return { weapon = "scythe_of_vitur", max_ticks = 2400 }
end

-- ------------------------------------------- after each fight, every seat
local AFTER = {}
-- the freezer's plan may end in its magic set: the ranged set back on, then
-- the magic set left on the floor
AFTER.maiden = function(t)
    if ROLE_IN.maiden[role] == 2 then
        for _, item in ipairs({ "twisted_bow", "dragon_arrow", "game_pest_archer_helm", "elite_void_knight_top", "elite_void_knight_robes", "pest_void_knight_gloves" }) do
            local cr, n = t.inv.count(item)
            if cr == "ok" and n > 0 then t.player.equip(item) end
        end
    end
    drop_spent(t, "maiden")
end
-- out of Bloat: each seat's own neck back on (the freezer's occult, the
-- others' rancour), the salve left on the floor
AFTER.bloat = function(t)
    wear(t, "bloat.after.neck", (ROLE_IN.maiden[role] == 2) and "occult_necklace" or "amulet_of_rancour")
    -- (seat 1 is the Nylocas mage: its void set back on, the melee set in the
    -- pack for Sotetseg, Xarpus and Verzik)
    seat1_void(t, "bloat.after")
    drop_spent(t, "bloat")
end
-- out of the Nylocas: the scythe and the dragon arrows back on, style 0 and
-- auto retaliate on (every other room's harness plays so), the room's
-- switches left on the floor
AFTER.nylocas = function(t)
    wear(t, "nylocas.after.scythe", "scythe_of_vitur")
    wear(t, "nylocas.after.arrows", "dragon_arrow")
    set_style(t, "nylocas.after.style", "Reap")
    set_retaliate(t, "nylocas.after.retaliate_on", false)
    -- (the camera back to the default every other room's harness plays at --
    -- yaw 0, pitch 128, zoom 600 in the room harnesses' shot rows: the
    -- Nylocas pose 0/512/1100 left seats 2 and 3 unable to frame Sotetseg
    -- from his barrier -- "attack tob_sotetseg_combat op2: pose 3 framed
    -- nothing (not_visible)" -- so their bow opener gave up and they reached
    -- him 6-10 ticks after seat 1, relay25-29)
    t.drive.camera(0, 128, 600)
    drop_spent(t, "nylocas")
end

-- ------------------------------------------------- the leader's way out
local POST = {}

POST.maiden = function(t, ox, oz)
    local br, bd = t.player.click_loc("tob_arena_barrier", 1)
    t.check("maiden.exit_gate", br == "ok", tostring(bd))
    t.ticks(2)
    return leave_room(t, "maiden", "bloat", ox, oz)
end

-- _play_entry.lua POST.bloat: the west barrier, the corridor west, the
-- passage (the chest is the Normal store: not opened, see the header)
POST.bloat = function(t, ox, oz)
    local tries = { { 23, 31 }, { 24, 31 }, { 23, 30 }, { 22, 31 } }
    local result, detail = "not_tried", ""
    for i = 1, #tries do
        local _, at = t.world.tile()
        if at.x >= ox + 23 then
            result, detail = t.player.click_loc("tob_arena_barrier", 1, { at = { ox + tries[i][1], oz + tries[i][2] } })
            t.ticks(2)
        end
    end
    t.ticks(2)
    local _, bt = t.world.tile()
    t.check("bloat.exit_gate", bt.x < ox + 23, "west barrier " .. tostring(result) .. ", at " .. bt.x .. "," .. bt.z .. " " .. string.sub(tostring(detail), 1, 120))
    t.player.walk_to(ox + 6, oz + 31, 40)
    return leave_room(t, "bloat", "nylocas", ox, oz)
end

POST.nylocas = function(t, ox, oz)
    local br, bd = t.player.click_loc("tob_arena_barrier", 1)
    t.check("nylocas.exit_gate", br == "ok", tostring(bd))
    t.ticks(2)
    t.player.walk_to(ox + 38, oz + 51, 30)
    return leave_room(t, "nylocas", "sotetseg", ox, oz)
end

POST.sotetseg = function(t, ox, oz)
    -- (the chest step already took every seat out through the barrier, to
    -- the chest by the exit: pressing the barrier again walked the leader
    -- back into the arena, where the passage is not framed -- relay14)
    if R.sotetseg.restocked then
        t.check("sotetseg.exit_gate", true, "out through the barrier already, at the supply chest")
    else
        local br, bd = t.player.click_loc("tob_arena_barrier", 1)
        t.check("sotetseg.exit_gate", br == "ok", tostring(bd))
        t.ticks(3)
    end
    return leave_room(t, "sotetseg", "xarpus", ox, oz)
end

-- the north gate, the skeleton's Dawnbringer ("a player must pick up the
-- Dawnbringer", E:214; the trio shares it in Verzik, W:875), the door
POST.xarpus = function(t, ox, oz)
    local wr, wd = t.player.walk_to(ox + 34, oz + 42, 12)
    t.check("xarpus.walk_to_gate", wr == "ok", tostring(wr) .. " " .. string.sub(tostring(wd), 1, 120))
    local gr = t.player.click_loc("tob_arena_barrier", 1, { at = { ox + 34, oz + 43 } })
    t.ticks(2)
    local _, gat = t.world.tile()
    t.check("xarpus.exit_gate", gat.z >= oz + 44, "pressed the gate (" .. tostring(gr) .. "), stood on " .. gat.x .. "," .. gat.z)
    local xr, xd = t.player.click_loc("tob_skeleton_with_weapon", 1)
    t.check("xarpus.skeleton", xr == "ok", tostring(xd))
    t.chat.continue_()
    local wok = t.inv.await("verzik_special_weapon", 1, 5)
    t.check("xarpus.dawnbringer", wok == "ok", "verzik_special_weapon in the pack: " .. tostring(wok))
    return leave_room(t, "xarpus", "verzik", ox, oz)
end

-- every raider: the trapdoor into the reward room; the leader opens a chest
local function reward_room(t)
    local dr, dd = "not_found", ""
    for _ = 1, 20 do
        dr, dd = t.player.click_loc("tob_dungeon_verzik_throne_door_opened", 1)
        if dr == "ok" then break end
        t.ticks(2)
    end
    t.ticks(4)
    local vr, vrow, chest_sym = "not_found", nil, nil
    for k = 0, 4 do
        local sym = "tob_treasureroom_chest_loc" .. k
        local lr, row = t.world.loc_near(sym, 30)
        if lr == "ok" and row ~= nil then vr, vrow, chest_sym = "ok", row, sym break end
    end
    t.check("raid.reward_room", dr == "ok" and vr == "ok", P .. "trapdoor " .. tostring(dr) .. " " .. string.sub(tostring(dd), 1, 120)
        .. "; reward chest " .. tostring(chest_sym) .. " at " .. tostring(vrow and vrow.tile_x) .. "," .. tostring(vrow and vrow.tile_z))
    if role == 1 and chest_sym ~= nil then
        local cr, cd = t.player.click_loc(chest_sym, 1)
        t.ticks(3)
        t.check("raid.reward_chest", cr == "ok", tostring(cd))
    end
    return vr == "ok"
end

-- the raid's death counter (read-only ::tobjail, _play_verzik.lua)
local function jail(t)
    t.cheat("::tobjail")
    t.ticks(2)
    local _, jl = t.msg.last(40)
    local line = ""
    for _, m in ipairs(jl or {}) do
        local jt = tostring(m.text)
        if jt:find("tobjail jailed=", 1, true) then line = jt end
    end
    return line
end

return {
    id = "_play_normal",
    fixture = "fresh_lumbridge.ini",
    max_frames = 480000,
    setup = KIT,

    run = function(t)
        local names = t.party.names()
        local leader = names[1]
        if role == 1 then
            t.check("spec.scope", true, "mode=normal party=3, the whole raid")
            local lr, ld = t.ticklog.start()
            t.check("ticklog.start", lr == "ok", tostring(ld))
        end
        -- the powered staves charged by their own Charge op (_play_nylocas.lua),
        -- then the supplies into the slots the charges freed
        -- the Tonalztics charged by its own Charge op (_play_maiden.lua), and
        -- the freezer seat's Eye of Ayak (_play_nylocas.lua)
        local tcr, tcd = t.player.inv_op("tonalztics_of_ralos_uncharged", 4)
        t.ticks(2)
        local tir, tin = t.inv.count("tonalztics_of_ralos_charged")
        t.check("kit.tonalztics", tir == "ok" and tin == 1, P .. "Charge " .. tostring(tcr) .. " " .. string.sub(tostring(tcd), 1, 80) .. "; charged " .. tostring(tin))
        do
            local chr, chd = t.player.inv_op("eye_of_ayak_uncharged", 3)
            t.ticks(2)
            local sr, sn = t.inv.count("eye_of_ayak")
            t.check("kit.charge", sr == "ok" and sn == 1, P .. "Charge on eye_of_ayak_uncharged " .. tostring(chr) .. " " .. string.sub(tostring(chd), 1, 80)
                .. "; eye_of_ayak in the pack " .. tostring(sn))
        end
        -- (what the charges leave -- splinters, demon tears -- is no use after
        -- them: on the floor, so the supplies have their slots)
        for _, left in ipairs({ "sunfiresplinter", "demon_tear" }) do
            local lr, ln = t.inv.count(left)
            if lr == "ok" and (tonumber(ln) or 0) > 0 then t.player.drop(left) t.ticks(1) end
        end
        for _, c in ipairs(SUPPLIES) do t.cheat(c) end
        t.ticks(2)
        local kit0 = supplies(t)
        t.check("kit.supplies", kit0.restore > 0 and (kit0.angler > 0 or mrole == 2), P .. "one raid's supplies at the start: " .. supplies_text(kit0)
            .. "; free slots " .. free_slots(t))

        -- THE LOBBY (_party_smoke.lua phases A and B): the board, the party, the door
        t.exec("lobby.goto", t.player.goto_tile, LOBBY_X - 1 + role, LOBBY_Z, 0)
        t.expect("party.barrier.lobby", t.party.barrier("lobby", 200))
        if role == 1 then t.exec("party.form", t.party.form, "normal") end
        t.expect("party.barrier.formed", t.party.barrier("formed", 300))
        if role ~= 1 then t.exec("party.apply", t.party.apply, leader) end
        t.expect("party.barrier.applied", t.party.barrier("applied", 300))
        if role == 1 then
            for n = 2, size do t.exec("party.accept." .. n, t.party.accept, names[n]) end
        end
        t.expect("party.barrier.accepted", t.party.barrier("accepted", 300))
        t.key("escape")
        t.ticks(2)
        t.expect("party.barrier.closed", t.party.barrier("closed", 300))
        local raid_start = nil
        if role == 1 then
            local rr, rd = t.party.ready()
            t.check("party.ready", rr == "ok" and string.find(tostring(rd), "Members: 3. Mode: Normal.", 1, true) ~= nil, tostring(rd))
            raid_start = mark_tick(t, "raid start")
        end
        t.expect("party.barrier.leader_in", t.party.barrier("leader_in", 300))
        if role ~= 1 then
            t.msg.await("has entered the Theatre of Blood (Normal Mode)", 20)
            local fr, fd = t.party.follow_in()
            t.check("party.follow_in", fr == "ok", P .. tostring(fd))
        end

        local reached = 0
        local _, _, here0 = origin_of(t)
        local last_square = nil
        for _, name in ipairs(ORDER) do
            -- THE ARRIVAL: the leader reads the register; every raider meets
            if role == 1 then
                local ar, ast = await_room(t, name, 600)
                t.check(name .. ".arrived", ar == "ok", ar == "ok" and tostring(ast.line) or tostring(ast))
                if ar ~= "ok" then break end
            end
            t.expect(name .. ".barrier.arrived", t.party.barrier(name .. "_arrived", 3000))
            local ox, oz, here = origin_of(t)
            local square = ox .. "," .. oz
            R[name] = { before = supplies(t) }
            local lv = {}
            for _, sk in ipairs({ "attack", "strength", "defence", "ranged", "magic", "hitpoints", "prayer" }) do
                local _, rd = t.skill.read(sk)
                lv[#lv + 1] = sk .. " " .. tostring(rd and rd.level)
            end
            local _, energy = t.var.varp("varp300_sa_energy")
            t.check(name .. ".start", square ~= last_square, string.format("%sat %d,%d (square %s, before %s); %s; %s; special energy %s", P, here.x, here.z, square,
                tostring(last_square), supplies_text(R[name].before), table.concat(lv, ", "), tostring(energy)))
            last_square = square
            top_up(t, name)
            -- (before the barrier: a read after the cross would start the
            -- seat's play a tick or two late)
            boss_guard(t, name, "start")
            local spec = PRE[name](t, ox, oz)
            local since = newest_serial(t)

            -- THE FIGHT: the library and the room's plan, nothing else
            local seat_role = ROLE_IN[name] and ROLE_IN[name][role] or role
            -- Maiden in the relay splits her storms by the standing tiles
            -- (raid_play_tob_maiden.lua THE RELAY'S STORM SPLIT; off in the
            -- room harness, which stays frozen green)
            local result, detail, rec = t.raid.play(PLAN[name], { mode = "normal", weapon = spec.weapon, max_ticks = spec.max_ticks, role = seat_role,
                variant = (name == "maiden") and "storm_split" or nil })
            t.check(name .. ".fight", result == "ok", P .. string.sub(tostring(detail), 1, 600))
            R[name].result = result
            if name == "maiden" and rec ~= nil and rec.m ~= nil then
                local m = rec.m
                t.check("maiden.role", m.role ~= nil, P .. "plays role " .. tostring(seat_role) .. " " .. tostring(m.role) .. "; her tile "
                    .. tostring(m.body_seen and m.body_seen.x) .. "," .. tostring(m.body_seen and m.body_seen.z) .. " (offset " .. tostring(m.ox) .. ","
                    .. tostring(m.oz) .. "); dodges " .. tostring(m.dodges) .. ", add presses " .. tostring(m.add_presses) .. ", Ice Barrage casts " .. #(m.casts or {}))
            end
            R[name].death = rec and rec.death_tick or nil
            boss_guard(t, name, "after")
            if role == 1 then boss_woke(t, name, rec) end
            if name == "nylocas" and role == 1 and result == "ok" then nylocas_rows(t, rec) end
            if name == "maiden" and role == 1 and rec ~= nil then maiden_rows(t, rec) end
            if name == "bloat" and role == 1 and rec ~= nil then bloat_rows(t, rec, result, detail, ox, oz) end
            if name == "sotetseg" and rec ~= nil then
                -- every seat's own account (_play_sotetseg.lua play.mazes): the
                -- balls, the bow opener, the maul's specials, his attacks
                local S = rec.sote or {}
                local mz = {}
                for k, m in ipairs(S.mazes or {}) do mz[#mz + 1] = "ran maze " .. k .. " landed t" .. tostring(m.land) .. " back t" .. tostring(m.back) end
                t.check("sotetseg.seat", true, P .. (#mz > 0 and table.concat(mz, ", ") or "ran no maze") .. "; death balls seen " .. tostring(S.death_balls)
                    .. ", gathers " .. tostring(S.gathers) .. " (" .. tostring(S.gather_ticks) .. " ticks), ticks a ball flew at me " .. tostring(S.aimed)
                    .. ", bow " .. tostring(S.bow_log) .. "; elder maul " .. ((S.em and #S.em.log > 0) and table.concat(S.em.log, ", ") or "none")
                    .. "; his attacks seen " .. tostring(S.attacks_seen or 0) .. ", melee prayer on his due tick " .. tostring(S.melee_due or 0)
                    .. " ticks, super combat sips " .. tostring(S.reboosts or 0))
            end
            if name == "sotetseg" and role == 1 and rec ~= nil then sotetseg_rows(t, rec, result, detail) end
            -- (only on a kill: its reads run tick by tick to the kill, which a
            -- failed fight reads as tick 1000000 -- relay15 ran the leader out
            -- of its instruction budget there)
            if name == "xarpus" and role == 1 and rec ~= nil and result == "ok" then xarpus_rows(t, rec, result, detail) end
            if name == "verzik" and role == 1 and rec ~= nil then
                -- owner_verzik's slow snippet: crabs, webs, yellows, the shared
                -- green ball, the rotation (raid_play_tob_verzik.lua
                -- QD.raid.verzik_p3_rows)
                t.raid.verzik_p3_rows(t, { pace = "slow", cycle = true }, rec, R.verzik.mark)
            end
            if result == "ok" then prayers_off(t, name) end
            if AFTER[name] ~= nil then AFTER[name](t) else drop_spent(t, name) end
            R[name].after = supplies(t)
            t.check(name .. ".supplies", true, P .. "used " .. used_text(R[name].before, R[name].after) .. "; eats "
                .. tostring(rec and #(rec.eats or {})) .. ", drinks " .. tostring(rec and #(rec.drinks or {})) .. "; left " .. supplies_text(R[name].after))
            t.expect(name .. ".barrier.done", t.party.barrier(name .. "_done", 9000))
            -- every seat out of the arena to the supply chest and back to the
            -- party (after Bloat, after Sotetseg)
            if result == "ok" and (name == "bloat" or name == "sotetseg") then
                if name == "bloat" then
                    for _, at in ipairs({ { 23, 31 }, { 24, 31 }, { 23, 30 }, { 22, 31 } }) do
                        local _, here1 = t.world.tile()
                        if here1.x >= ox + 23 then
                            t.player.click_loc("tob_arena_barrier", 1, { at = { ox + at[1], oz + at[2] } })
                            t.ticks(2)
                        end
                    end
                else
                    t.player.click_loc("tob_arena_barrier", 1)
                    t.ticks(3)
                end
                restock(t, name)
                R[name].restocked = true
                t.expect(name .. ".barrier.chest", t.party.barrier(name .. "_chest", 3000))
            elseif name == "bloat" or name == "sotetseg" then
                -- (a seat that died in the room still meets the party at the
                -- chest barrier -- relay23's two living seats waited 3000 ticks
                -- for it; its death is already red in <room>.fight / player.died)
                t.expect(name .. ".barrier.chest", t.party.barrier(name .. "_chest", 3000))
            end
            if role == 1 then
                local wave = nil
                for _ = 1, 30 do
                    wave = line_since(t, since, "(Normal Mode) complete!")
                    if wave ~= nil then break end
                    t.ticks(1)
                end
                t.check(name .. ".complete_line", wave ~= nil, "chat line: " .. plain(wave))
                local _, now = t.tick()
                local jl = jail(t)
                t.check(name .. ".deathless", jl:find("died_in=0 deaths=0", 1, true) ~= nil, "after the room: " .. jl)
                t.check(name .. ".measure", R[name].death ~= nil, string.format("%s ticks (mark %s, death %s); damage taken by pid: %s",
                    tostring(R[name].death and R[name].mark and (R[name].death - R[name].mark)), tostring(R[name].mark), tostring(R[name].death),
                    hits_between(t, R[name].mark or 0, R[name].death or now)))
                if result ~= "ok" then break end
                if POST[name] ~= nil then
                    local ok = POST[name](t, ox, oz)
                    if not ok then break end
                end
            end
            if result == "ok" then reached = reached + 1 end
        end

        -- THE REWARD ROOM: every raider down the trapdoor
        if reached == #ORDER then reward_room(t) end
        t.expect("party.barrier.end", t.party.barrier("end", 3000))
        if role == 1 then
            t.check("raid.complete", reached == #ORDER, reached .. " of " .. #ORDER .. " rooms cleared and left")
            local jl = jail(t)
            t.check("raid.deathless", jl:find("died_in=0 deaths=0", 1, true) ~= nil, "the raid's death counter: " .. jl)
            local vd = R.verzik and R.verzik.death or nil
            t.check("raid.measure", vd ~= nil and raid_start ~= nil, string.format("raid start tick %s, Verzik's death %s: %s ticks in all",
                tostring(raid_start), tostring(vd), tostring(vd and raid_start and (vd - raid_start))))
        end
        t.check("raid.supplies_left", true, P .. "left after the raid: " .. supplies_text(supplies(t)))
        t.finish(0)
    end,
}
