-- quest-driver / raid_play_tob_nylocas: the Nylocas plan for t.raid.play
-- (raid_play.lua), its own driver part (raid seam29 play_library_own_files).
-- Written by raid seam30 play_tob_nylocas from the sources line by line;
-- PLAY_NOTES.md "Nylocas, Entry solo" is its strategy table.
--
-- Sources, abbreviated in the comments below:
--   E   docs/minigames/theater_of_blood/sources/wiki_Theatre_of_Blood_Entry_Mode.wikitext
--       (the Entry page's own Nylocas section and its "Solo strategy", :153-176)
--   W   .../sources/wiki_Theatre_of_Blood_Strategies.wikitext (:702-776)
--   NT  docs/minigames/theater_of_blood/encounters/nylocas.tsv (our spec rows)
--   DMG OSRS-Content .../minigame_tob/scripts/tob_damage.rs2 (nulling, reflect)
--   NB  OSRS-Content .../minigame_tob/scripts/tob_nylocas_boss.rs2 (Vasilias)
--   NR  OSRS-Content .../minigame_tob/scripts/tob_nylocas.rs2 (the waves)
--
-- What the plan sees is what a person at the screen sees: every npc's colour,
-- size, tile and health bar (api_drive.npcs), the boss's form, the tick a
-- copy first appeared, its own swings (player_anim, the library) and its own
-- hitpoints, prayer and lit prayers.  It never reads a server register.

QD.raid._play_plan("tob_nylocas", {
    room = "nylocas",
    -- Vasilias drops in as the spawning form and turns melee (NB :95-106);
    -- the library follows st.boss_symbol, which the see step below moves to
    -- her current form every tick (maiden's pattern).
    boss = { entry = "nylocas_boss_spawning_story", normal = "nylocas_boss_spawning" },
    modes = {
        -- NT nylocas.max_hit_small_entry 1-5, max_hit_big_entry 1-10,
        -- explosion_entry 1-8 (E :161 "about 8 damage in Entry Mode"),
        -- vasilias_max_hit_entry 1-24, pillar_collapse_entry_min 30 (E :171
        -- "30+ damage"); hp: small 2, big 3 (E :161), cadence 3 (NT attackrate)
        entry = { small_hit = 5, big_hit = 10, explode = 8, boss_hit = 24, prayed_hit = 17, collapse = 40, cadence = 3,
            form = { melee = "nylocas_boss_melee_story", magic = "nylocas_boss_magic_story",
                ranged = "nylocas_boss_ranged_story", spawning = "nylocas_boss_spawning_story" },
            suffix = "_story", support = "tob_nylocas_support_story", first_window = 14, window = 15 },
        -- raid seam32 play_tob_nylocas_normal: NORMAL, a party of three.  NT
        -- nylocas.max_hit_small 1-17, max_hit_big 1-24 ("a max hit of 17 ...
        -- a max hit of 24", W :731), explosion_max 18,21 ("small nylocas can
        -- deal up to 18 damage, while larger ones can deal up to 21", W :746),
        -- pillar_collapse_max 50 ("deal up to 50 damage to the entire team",
        -- W :726), vasilias_max_hit 1-70 and the prayed 17 ("can hit up to 70
        -- off-prayer, and 17 if prayed against (except melee, which is fully
        -- protected)", W :752); hp small 8, big 16 in a trio (W :731).  Her
        -- windows: "The boss will change forms every 10 ticks" (W :752), the
        -- first one a tick shorter (NT vasilias_first_switch 9, switch 10;
        -- tob.constant ^tob_vasilias_first_switch_ticks 9 / _switch_ticks 10).
        normal = { small_hit = 17, big_hit = 24, explode = 21, boss_hit = 70, prayed_hit = 17, collapse = 50, cadence = 3,
            form = { melee = "nylocas_boss_melee", magic = "nylocas_boss_magic",
                ranged = "nylocas_boss_ranged", spawning = "nylocas_boss_spawning" },
            suffix = "", support = "tob_nylocas_support", first_window = 9, window = 10 },
    },
    -- raid seam32: THE TRIO'S ROLES.  "Each player should be assigned a
    -- style of Nylocas to kill prior to starting the room ... Trio: x1 mager,
    -- x1 melee, x1 ranger" (W :706-711); the trio guide's three sections are
    -- "Mage Waves", "Ranger Waves", "Melee Waves" (blert_guides/
    -- tob_nylocas_trio_content.mdx :51, :196, and the melee one), each killing
    -- its own colour; and "always be on the attack; if your assigned nylocas
    -- are not currently near or in the room, switch weapons ... until the
    -- assigned nylocas return" (W :746).  "Barrages should only be used when
    -- the fight becomes hectic" (W :719): the mage alone freezes.  Seat 1 (the
    -- leader) is the mage, seat 2 the ranger, seat 3 the meleer.  A party of
    -- one has no role (the Entry plan: every colour, every freeze).
    roles = {
        -- raid seam33 play_tob_nylocas_normal_green: the mage's weapon is the
        -- source's own: "Mages should use an eye of ayak, as its 3 tick speed
        -- and fairly high damage makes clearing them incredibly trivial. If not
        -- available, use a 4 tick staff instead ... Barrages should only be used
        -- when the fight becomes hectic" (W :719).  A powered staff ("a
        -- category of magic weapons that possess a built-in magic spell", wiki
        -- Powered staff :8) swings on by itself once pressed, like the whip;
        -- speed 3, attack range 6 (wiki Eye of Ayak :69-70); its swing seq is
        -- human_eye_of_ayak_normal (powered_staff.rs2 ~powered_staff_fx), id
        -- 12397 (all.seq.compack "12397=human_eye_of_ayak_normal").  It lands
        -- magic damage on a Hagios since seam33 powered_staff_damage_type.  No
        -- freeze: in Normal a frozen copy keeps biting on this server
        -- (CONTENT_BUGS, Entry-only freeze stop), and the seam32 mage's Ice Rush
        -- at five ticks was the room's bottleneck (127-147 ticks of stall).
        -- raid seam47: `heart` -- the seat invigorates a saturated heart at the
        -- door (Blert's trio mages read Magic 112 = 99 + 4 + 10 percent, seam46;
        -- the harness presses it) and the plan re-uses it when it is ready
        { name = "mage", colour = "magic", freeze = false, home = { 31, 24 }, heart = "saturated_heart",
            -- raid seam40 play_tob_nylocas_follows_blert: the mage's other two
            -- colours as Blert's trio mages swing them (27 rooms, ny_blert.out):
            -- greens BLOWPIPE 116 of 138 small-green hits, greys SCYTHE/CLAW
            -- (CLAW 22 SCYTHE 15 on smalls, SCYTHE 19 of 27 on bigs), her melee
            -- form SCYTHE 141 of 184 -- not the Entry whip and shortbow.  The
            -- scythe: speed 5, swing seq 8056 (raid_play.lua weapon table).
            loadout = { magic = { item = "eye_of_ayak", speed = 3, reach = 6, powered = true, seqs = { [12397] = true } },
                ranged = { item = "toxic_blowpipe_loaded", speed = 2, reach = 5, seqs = { [5061] = true } },
                -- her ranged form: the TWISTED BOW (8357: TWISTED_BOW 127 of
                -- 158 mage hits); speed 6, 5 on rapid, seq 426 (raid_play_tob_maiden.lua)
                ranged_boss = { item = "twisted_bow", speed = 5, seqs = { [426] = true } },
                melee = { item = "scythe_of_vitur", speed = 5, seqs = { [8056] = true } } } },
        -- "Rangers should use a toxic blowpipe in this room" (W :717): two
        -- ticks on rapid, an attack range of 5 (wiki Toxic blowpipe), its
        -- swing seq 5061 (s32ny5 ticklog player_anim, every p2 swing;
        -- drive.symbol has no seq kind)
        -- raid seam33: every trio seat carries a powered staff for the blues
        -- it helps with: the trio ranger Ayaks mage bigs ("Path west and stand
        -- 1 tile away from the west barrier. Ayak the ...", trio guide :216;
        -- :251-255, :289, :334) and the trio meleer Sangs them ("Sang the wave
        -- 1 south mage, swift the east small, sang the south wave 4 big",
        -- trio guide :386-387; :434, :453).  The Sanguinesti staff: speed 4
        -- (wiki Powered staff :6, :10), its swing human_castwave_staff
        -- (powered_staff.rs2 ~powered_staff_fx), id 1167 (all.seq.compack
        -- "1167=human_castwave_staff"), reach 7 (the powered staves' 7;
        -- the Ayak's own 6, wiki Eye of Ayak :70).
        { name = "ranger", colour = "ranged", freeze = false, home = { 30, 23 },
            -- raid seam40: the ranger's greys and her melee form with the
            -- SCYTHE (Blert range|melee SCYTHE 27 of 47, big 24 of 31; on 8355
            -- SCYTHE 118 of 167), not the whip
            loadout = { ranged = { item = "toxic_blowpipe_loaded", speed = 2, reach = 5, seqs = { [5061] = true } },
                magic = { item = "eye_of_ayak", speed = 3, reach = 6, powered = true, seqs = { [12397] = true } },
                -- raid seam49: her ranged form with the TWISTED BOW (Blert
                -- range|boss TWISTED_BOW 20 of 27 rooms; the harness kit)
                ranged_boss = { item = "twisted_bow", speed = 5, seqs = { [426] = true } },
                -- raid seam51 play_tob_nylocas_whole: BLACK CHINCHOMPAS for a
                -- green clump (Blert range|wave10 CHIN_BLACK in 23 of 27 rooms,
                -- wave 21 in 21, wave 31 in 18, wave 6 in 12, wave 9 in 11;
                -- reference/nylocas_normal_3.json weapons).  Thrown on rapid
                -- (the medium fuse: full accuracy at 4-6 tiles, 75 percent
                -- elsewhere, player_ranged.rs2 ~player_chinchompa_hit_roll),
                -- the blast a 3x3 on the copy (~player_chinchompa_splash),
                -- swing human_chinchompa_attack_pvn 7618 (all.seq.compack; the
                -- it2 ticklogs: 10 throws, every one 7618, none 2779)
                -- speed 4, 3 on rapid, range 9 (wiki Black chinchompa)
                ranged_chin = { item = "chinchompa_black", speed = 3, seqs = { [7618] = true, [2779] = true } },
                melee = { item = "scythe_of_vitur", speed = 5, seqs = { [8056] = true } } } },
        { name = "melee", colour = "melee", freeze = false, home = { 31, 25 },
            -- raid seam40 play_tob_nylocas_follows_blert: the meleer as Blert's
            -- trio meleers (27 rooms, ny_blert.out): blues the EYE OF AYAK (203
            -- of 207 hits; no Sanguinesti staff in any room), greens the
            -- BLOWPIPE (75 of 91), small greys a 4-tick slash weapon (SULPHUR
            -- BLADES 682 of 998; ours is the whip: CONTENT_BUGS, the blades'
            -- obj has the cache's bonuses and no passive here), big greys and
            -- her melee form the SCYTHE (121 of 311 big-grey hits, the most of
            -- any weapon; 124 of 181 on 8355).
            loadout = { magic = { item = "eye_of_ayak", speed = 3, reach = 6, powered = true, seqs = { [12397] = true } },
                ranged = { item = "toxic_blowpipe_loaded", speed = 2, reach = 5, seqs = { [5061] = true } },
                melee_big = { item = "scythe_of_vitur", speed = 5, seqs = { [8056] = true } },
                melee_boss = { item = "scythe_of_vitur", speed = 5, seqs = { [8056] = true } },
                -- her ranged form: the TWISTED BOW (8357: 88 of 184, the pipe 68)
                ranged_boss = { item = "twisted_bow", speed = 5, seqs = { [426] = true } } } },
    },
    -- how much an own-colour copy is preferred over a colour another seat
    -- owns (score points; a tile of running is 3): another seat's copy is
    -- taken only when none of the raider's own is worth pressing, or when it
    -- is an aggro hitting this raider (it costs the raider, whoever owns it)
    -- (30 at first: the meleer walked the platform for greys, 58 swings in
    -- 340 wave ticks with 9-17 tick gaps, svaplaynyloc survey4)
    own_colour_bonus = 12,
    -- raid seam33: own colour first as a rule (see the target pick)
    own_first = true,
    -- raid seam50 play_tob_nylocas_whole: a helper melees a grey in reach (see
    -- A GREY AT A HELPER'S FEET) and a chewer stays a target until its last
    -- pop_skip ticks (see AN OLD CHEWER IS STILL A COPY)
    help_feet = true,
    pop_skip = 4,
    -- raid seam50: another seat's copy near its owner is left to it (see LEAVE
    -- IT TO ITS OWNER); score points (a tile of running is 3)
    leave_to_owner = 60,
    -- raid seam49: the mage stands two off her on her magic and ranged forms
    -- (see NOT HER NEAREST)
    mage_back = true,
    -- raid seam33: in a party a swap goes out only with the old weapon's next
    -- swing two or more ticks off (see THE SWAP, TIMED); alone, as before
    swap_timed = true,
    -- raid seam47 play_tob_nylocas_no_nulling: the swap's press goes out in
    -- the swap's own block, right after the equip (see THE PRESS)
    swap_press_together = true,
    -- raid seam47: the saturated heart's cooldown, map clock ticks (content
    -- skill_slayer/configs/imbued_heart.constant ^saturated_heart_cooldown
    -- 500; the wiki's five minutes)
    heart_cooldown = 500,
    -- raid seam47: where Blert's trios stand on her (offsets from her SW tile,
    -- reference/nylocas_normal_3.json positions mage|boss, range|boss,
    -- melee|boss, the most common first): see OUT FROM UNDER HER
    -- raid seam47: a party seat's preference for a big (see BIGS ARE NOT LEFT)
    big_first = 8,
    boss_stands = { { 2, 4 }, { 1, 4 }, { 3, 4 }, { -1, 2 }, { 2, -1 }, { 4, 3 }, { 4, 1 }, { 1, -1 } },
    -- raid seam47: THE SPECIAL ON HER MELEE FORM.  Blert's trios spend their
    -- special energy on Vasilias (ny_blert.out "on Vasilias by role|form id",
    -- 27 rooms: 8355 CLAW 17+18+12, BGS/GODSWORD 15+11, 8357 ZCB 21+20+18 --
    -- about five specials a room); the claws on her melee form are the ones
    -- this kit carries.  Dragon claws: 50 percent of the orb (wiki Dragon
    -- claws; content pvm_dragon_claws.rs2 set_sa_vars(oc_param(sa_energy)),
    -- 500 of varp300's 1000), speed 4, the special's swing
    -- human_dragon_claws_spec 7514 (pvm_dragon_claws.rs2 :27).  Pressed only
    -- with `room` ticks left in her form's window, armed from the orb in the
    -- swap's own block right before the attack press (the library's intent.spec
    -- pattern, raid_play.lua), proved by the energy it spends.
    spec = { item = "dragon_claws", cost = 500, speed = 4, seqs = { [7514] = true, [1067] = true }, room = 4, give_up = 4 },
    -- raid seam33: the meleer waits for (and walks to meet) its own greys in
    -- the tunnels before taking another seat's colour (measured: no gain on
    -- two seeds, s33nyseedoneD identical to B; off)
    own_wait = false,
    -- raid seam33: the Ayak mage bursts a pure-blue clump of this many (nil:
    -- off; measured at 3: one cast a room on two seeds, no gain, s33nyseedoneE)
    burst_clump = nil,
    -- the three protection prayers are the only prayers this plan lights:
    -- "always switch protection prayers ... When its form changes, the player
    -- should again switch prayers" (W :752)
    walk_prayers = { "protectfrommelee", "protectfrommagic", "protectfrommissiles" },
    -- raid seam40 play_tob_nylocas_follows_blert: a party seat's offensive
    -- prayer by the style it swings (THE OFFENSIVE PRAYER, below); the
    -- library puts out the one no longer wanted
    down_prayers = { "piety", "rigour", "augury" },
    attack_prayer_of = { melee = "piety", ranged = "rigour", magic = "augury" },
    prayer_of = { melee = "protectfrommelee", magic = "protectfrommagic", ranged = "protectfrommissiles" },
    -- One weapon per colour ("You will need all three attack styles for this
    -- room", E :155; "Ancient Magicks is highly recommended, and a fast ranged
    -- weapon such as a magic shortbow", E :157).  Speeds: abyssal whip 4,
    -- magic shortbow on rapid 3, a spell 5 (wiki item pages); the swing seqs
    -- as measured in build/quest_gate/tob_nylocas/ticklog.tsv (1658, 426,
    -- 1979; Ice Rush shares the rush/blitz cast 1978).
    loadout = {
        melee = { item = "abyssal_whip", speed = 4, seqs = { [1658] = true } },
        ranged = { item = "magic_shortbow", speed = 3, seqs = { [426] = true } },
        magic = { item = "lava_battlestaff", speed = 5, seqs = { [1978] = true, [1979] = true } },
    },
    -- the wave colours and kinds by symbol stem (NR :647-752)
    kinds = { "incoming", "fighting", "big_incoming", "big_fighting" },
    styles = { "melee", "ranged", "magic" },
    -- Room geometry local to the 64x64 square (NT nylocas.pillar_anchors
    -- 3289,4242 3300,4242 3289,4253 3300,4253 in region base 3264,4224 ->
    -- local 25,18 36,18 25,29 36,29; supports are 3x3).  "it's best to stay
    -- near the centre of the arena as much as possible, unless you are
    -- cleaning up greys" (E :162): home is the centre between the four.
    supports = { { 25, 18 }, { 36, 18 }, { 25, 29 }, { 36, 29 } },
    home = { 31, 24 },
    floor = { 19, 12, 44, 36 },
    -- NT nylocas.lifetime_small 52 (explodes on lifetime tick 52, i.e. 51
    -- ticks after the tick it appears), lifetime_big 53; the blast reaches two
    -- tiles from the footprint ("This damage can be avoided by being at least
    -- two tiles away", E :161; NR :1276 `npc_range(coord) <= 2`).  The T-1
    -- rule (ET 1.1): the tile read is the end of the tick before, so the
    -- raider is three tiles off by age `explode_age - 2` (margin one tick
    -- for the first-seen tick lagging the spawn).
    explode_age = 51, explode_age_big = 52, blast = 2,
    -- NT nylocas.flicker_first_wave 16, flicker_first_switch 5,
    -- flicker_hold 2: a flicker's colour is final from age 7, so from wave 16
    -- no copy younger than `settle_age` is hit (a wrong-colour hit nulls the
    -- raider on it for good: DMG :272 "this player is nulled on this nylocas
    -- from now on").
    flicker_wave = 16, settle_age = 7,
    -- reaches: the bow on rapid 7, a spell 10 (wiki Magic shortbow, Ice Rush)
    reach = { melee = 1, ranged = 7, magic = 10 },
    -- NB :445-464 / tob_nylocas.constant ^tob_vasilias_entry_window_ticks 15:
    -- the first window 14, every later one 15.
    first_window = 14, window = 15,
    -- raid seam32: no swing on her inside this many ticks before the
    -- predicted turn (the turn read can trail the server by a tick)
    turn_margin = 0,
    -- the plan's food order (_play_nylocas_supplies): Shark 20 (wiki Shark),
    -- the Theatre's bandages 20 and a boost (E :151; tob_spectate.rs2)
    -- raid seam32: the Normal party's anglerfish ("make sure that you eat your
    -- angler", transcripts/yt_KF9y2GYTJ-A.md:114; heals 22 at 99 Hitpoints,
    -- wiki Anglerfish), counted at its plain heal
    food_waves = { { item = "shark", heal = 20 }, { item = "anglerfish", heal = 22 }, { item = "tob_bandages", heal = 20 } },
    food_boss = { { item = "tob_bandages", heal = 20 }, { item = "anglerfish", heal = 22 }, { item = "shark", heal = 20 } },
    decide = "_play_nylocas_decide",
    -- one client.log line a tick while the plan is iterated from the log
    trace = false,
    -- raid seam32: the same line from one seat of a party only (its own
    -- client.log), while a trio plan is iterated; nil when kept
    trace_seat = nil,
    -- raid seam32: the swap's engagement-ending step (see the press); off
    swap_stop = false,
    -- raid seam35m play_tob_nylocas_normal_supports: in a party, never let a
    -- low support go (the Entry guide's "let one that's low die", E :171, is
    -- a solo's rule: 34 of 34 recorded Regular trios land her with all four
    -- standing), and weigh a chewer by its support's missing bar this much.
    -- Measured OFF: survey1 (seed_survey --party 3) stood 3,2,1,2,2 of 4 at
    -- her landing against 2,2,2,2,2 without it; the supports do not fall for
    -- being let go, they fall for the copies the trio does not kill in time
    -- raid seam51 play_tob_nylocas_whole: ON again, and stronger.  The
    -- seam50 surveys (it2: weakest 0.05, 0.00 (fallen), 0.10 at her landing)
    -- are the "let the lowest go" rule (+25 under 0.15) doing what it says;
    -- the reference trios stand all four in 34 of 34 rooms, weakest 0.10 or
    -- more.  A chewer on a support under `keep_low` is worth `keep_urgent`
    -- more points on top of the missing-bar weight.
    keep_all = true,
    keep_weight = 20,
    keep_low = 0.4,
    keep_urgent = 25,
    -- owner_nylocas: A LOW SUPPORT'S CHEWER IS EVERY SEAT'S (see the pick)
    low_any = true,
    -- raid seam51: A SPLIT DOES NOT FLICKER.  The flicker is a wave spawn's
    -- (NR :17 "FLICKER from wave 16 on: spawn as style a, switch to b five
    -- ticks after spawning"); a big's split is armed with no flicker chain
    -- (NR ~tob_nylo_spawn_split: ~tob_nylo_arm(..., 0), its colour rolled
    -- once).  The settle wait held every split seven ticks from wave 16:
    -- the cleanup's tail is splits (it2 _play_nylocas: 9 of the last 12
    -- copies to die spawned after wave 31, every one a split).
    split_settled = true,
    -- raid seam51: THE CHINS (the ranger's `ranged_chin`, see its loadout):
    -- thrown at a green whose 3x3 holds `clump` or more pressable greens (a
    -- big counts half: 16 hitpoints against one blast roll), from `reach`
    -- tiles or nearer without a step; a copy of another colour under the
    -- blast is nulled for the ranger (DMG :272), each costs `other` points
    chin = { clump = 2, reach = 9, other = 4 },
    -- raid seam51: ANOTHER SEAT'S SHOT IN THE AIR (see the target pick):
    -- score points on a copy another raider's projectile is flying at
    inbound = 35,
    -- raid seam51: once the 31st wave is out, the colour rule no longer
    -- holds a seat back (the reference's cleanup is 32 ticks from wave 31,
    -- outcome.phase.cleanup_end.start 292 - wave31.start 260; ours 47-51)
    cleanup_free = true,
    -- the cleanup is read off the screen: this many waves seen and none for
    -- `cleanup_quiet` ticks (a seat sees 29-31 of the 31: it2 p2 saw 29)
    cleanup_waves = 28, cleanup_quiet = 14,
    -- owner_nylocas: the blast window in the cleanup (see IN THE CLEANUP THE
    -- LAST GREYS ARE KILLED)
    cleanup_blast = 2,
    -- owner_nylocas: THE SCYTHE ON A GREY STACK (see the pick)
    grey_stack = 12,
    -- raid seam52 play_tob_nylocas_last: THE STANDS.  Where Blert's trios
    -- stand in each wave, per role: the most common tile of the wave's span
    -- (reference/nylocas_normal_3.json positions "<role>|wave<N>", offsets
    -- from Vasilias' south-west tile, local 30,23), kept when it holds 10
    -- percent or more of the role's ticks in that wave; elsewhere the seat's
    -- own home.  The tiles are the lane mouths (content tob_nylocas.rs2: a
    -- copy leaves its lane at "west x=26, east x=37, south z=19 room-local"):
    -- west -4,2 = 26,25; south 2,-4 / 1,-4 = 32,19 / 31,19; east 7,1 = 37,24.
    -- The plan sent every idle seat to the centre, and the south-west support
    -- (25,18), the one the west and south lanes both reach, was the weakest on
    -- every red seam51 name (0.01, 0.01, fallen; the others 0.15-0.39).  An
    -- idle seat walks to its role's stand for the wave it last saw.
    stand_anchor = { 30, 23 },
    -- The stand is also where a seat fights FROM: a ranged or magic copy
    -- further from the seat's stand (its home) than the weapon reaches costs
    -- `stand_leash` points a tile, as the walk back would.  An idle walk alone
    -- did nothing (seam52 it1: 1-5 walks a seat a room; every seat is busy):
    -- the seats drifted with their last kill (svaplaynyloc t330-380: the mage
    -- at 32,22 and the ranger at 35,20-36,21 swinging the east lane while a
    -- green big and a green small chewed the south-west support for 45 ticks
    -- unpressed).  "it's best to stay near the centre of the arena as much as
    -- possible, unless you are cleaning up greys" (E :162): greys are exempt.
    stand_leash = 4,
    stands = {
        mage = { [1] = { 1, -3 }, [2] = { 7, 1 }, [3] = { 7, 2 }, [4] = { 2, -4 }, [6] = { -4, 2 }, [7] = { 1, -4 }, [8] = { -4, 2 }, [10] = { 7, 1 }, [14] = { 0, 2 }, [15] = { 6, 2 }, [16] = { 7, 1 }, [18] = { 4, 1 }, [19] = { 2, 1 }, [21] = { -4, 1 }, [22] = { 7, 2 }, [23] = { 7, 1 }, [25] = { 3, 1 }, [26] = { -3, 1 }, [27] = { -3, 1 }, [28] = { 1, 2 }, [29] = { 1, 2 }, [30] = { 7, 1 }, [31] = { 3, 2 } },
        ranger = { [1] = { -4, 1 }, [2] = { 4, 1 }, [3] = { 2, -4 }, [4] = { -4, 2 }, [5] = { 1, -4 }, [6] = { 1, -4 }, [7] = { 1, -4 }, [8] = { -3, 2 }, [9] = { -1, 1 }, [10] = { 2, 2 }, [11] = { 0, 1 }, [12] = { -4, 1 }, [13] = { -4, 2 }, [16] = { 1, 1 }, [17] = { -1, 1 }, [18] = { 0, 1 }, [19] = { 1, 1 }, [21] = { 4, 1 }, [23] = { -4, 1 }, [24] = { -4, 1 }, [25] = { 1, 1 }, [27] = { 2, 0 }, [28] = { 1, 1 }, [29] = { 2, 1 }, [30] = { -4, 1 } },
        melee = { [1] = { 2, -3 }, [2] = { 7, 2 }, [3] = { 7, 2 }, [4] = { 1, -4 }, [7] = { 7, 1 }, [9] = { 1, -4 }, [10] = { -4, 2 }, [12] = { 7, 2 }, [13] = { 6, 2 }, [22] = { 1, -4 } },
    },
    -- raid seam52: THE CLEANUP'S ORDER.  "Kill any wave 29/31 smalls first as
    -- these will change color if you don't kill them quickly enough. Don't get
    -- baited into chinning a clump with 28s in it as 28s will expire" (trio
    -- guide, ranger cleanup :363-364); "Continue Ayaking smalls with the
    -- highest wave number" (mage waves 28-29 :175); "If a dimmed and undimmed
    -- Nylo are stacked ... target the bottom one ... as it is the newest"
    -- (mage cleanup :187).  Once the cleanup is on (cleanup_waves above), a
    -- copy's age weighs `age` more points a tick (newest first), and a copy
    -- with `expire` ticks or fewer to its pop is passed over (`pass` points)
    -- unless it is an aggro or chews a support under keep_low: it ends by
    -- itself.  The reference's cleanup is 32 ticks from wave 31 to the last
    -- death (outcome.phase.cleanup_end.start 292 - wave31.start 260); the
    -- seam51 trio's 55-63, the tail splits of bigs killed late.
    -- THE CLEANUP'S SIDES: "Prioritize cleaning up the north side of the room
    -- after the 31s are dead" (mage cleanup :185); "After killing any 29+
    -- rangers, look to kill other colors (mage > melee), working from the
    -- south to the north side" (ranger cleanup :365-366); "kill all relevant
    -- melees starting from the south" (melee cleanup :496).  The mage takes
    -- the north half (local z >= `north_z`), the ranger and the meleer the
    -- south; a copy on the other half costs `side` points.  seam52 it1: the
    -- cleanup was a dogpile, all three seats pressing one copy at a time
    -- (_play_nylocas t319-331: 8344 at 26,21, then 8342 at 28,19, then 8342
    -- at 37,21, each pressed by p0, p1 and p2 within two ticks) while a green
    -- split chewed the south-east support from t308 to t342.
    cleanup_order = { age = 0.5, expire = 10, pass = 30, side = 15, north_z = 24, north_role = "mage", rate = 0.71 },
    -- raid seam55 play_tob_nylocas_stands_like_blert: THE STYLE AT HER, BY
    -- NAME.  The style is one index (varp43) carried across every swap, and
    -- the harness sets it by name for the waves (Rapid, Lash).  On that index
    -- the scythe swings Chop, which this content's scythe table makes stab
    -- (seam52 kit.melee_damage_per_swing.md: "Leave style slot 0 (Reap, slash
    -- accurate) on the scythe"), and Blert's trios swing the scythe on her
    -- melee form in 26, 23 and 22 of 27 rooms (reference weapons mage|boss,
    -- range|boss, melee|boss).  From her landing a seat whose worn weapon is
    -- named here presses that style's button by its name, read off the combat
    -- tab (combat_interface:<n>_text, ui.lua QD.ui.style), the tick after the
    -- swap has landed: Reap on the scythe, Rapid back on the bow or the pipe.
    -- The press needs the combat tab shown (seam55 probe_style: with the
    -- inventory open the press leaves varp43 unchanged), so the tab is opened
    -- first when the names read as hidden.  `give_up` ticks without the name
    -- on any button ends the wait for that weapon.
    -- MEASURED OFF (seam55 it1/it2, three names): boss ticks 123, 139, 157
    -- against 129, 119, 131 without it, and 38 -> 60, 58 -> 66 ticks with no
    -- attack: the tab opens and presses cost swings that the slash bonus does
    -- not give back (her melee form's defence is 50, tob.npc).  Kept as the
    -- table to turn on once a press costs no swing:
    -- { scythe_of_vitur = "Reap", twisted_bow = "Rapid", toxic_blowpipe_loaded = "Rapid" }
    boss_styles = nil,
    boss_style_give_up = 6,
    -- owner_nylocas: HER FORMS IN THEIR OWN GEAR (the harness's kit; Blert's
    -- recorders on her, equipmentDeltas, 27 rooms: on her melee form every
    -- role wears torva / rancour / radiant oathplate / ferocious gloves, on
    -- her ranged form every role the void range set, on her magic form the
    -- mage its occult and the others the void range set).  The set of her
    -- form goes on in the swap's own block, before the weapon and its press;
    -- an item already worn (none of it in the backpack) is not pressed.
    boss_gear = {
        melee = { "torva_helm", "amulet_of_rancour", "radiant_oathplate_chest", "radiant_oathplate_legs", "ferocious_gloves" },
        ranged = {
            mage = { "game_pest_archer_helm", "occult_necklace", "elite_void_knight_top", "elite_void_knight_robes", "pest_void_knight_gloves" },
            ranger = { "game_pest_archer_helm", "necklace_of_rupture", "elite_void_knight_top", "elite_void_knight_robes", "pest_void_knight_gloves" },
            melee = { "game_pest_archer_helm", "necklace_of_rupture", "elite_void_knight_top", "elite_void_knight_robes", "pest_void_knight_gloves" },
        },
    },
})

-- raid seam55: THE STYLE AT HER (P.boss_styles).  Called once a tick from
-- the decide step while she is in the room; presses at most one style button
-- a tick, never blocks.
function QD.raid._play_nylocas_style(st, v)
    local P, ny = st.plan, st.ny
    local L = st.weapon
    local want = (L ~= nil) and P.boss_styles[L.item] or nil
    if want == nil or ny.style_done == L.item then return end
    if ny.style_item ~= L.item then
        ny.style_item, ny.style_since, ny.style_press_tick, ny.style_tab_for = L.item, v.tick, nil, nil
    end
    if ny.style_press_tick ~= nil and v.tick - ny.style_press_tick < 2 then return end
    if v.tick - ny.style_since > P.boss_style_give_up then
        ny.style_done = L.item
        ny.style_gave_up = (ny.style_gave_up or 0) + 1
        return
    end
    -- the names a weapon shows are learned once, while the tab is shown, and
    -- remembered per item: varp43 alone then says whether the style is right,
    -- and the tab is opened only for a press the remembered names call for
    ny.style_map = ny.style_map or {}
    local cur_result, cur = QD.var.varp("varp43_com_mode")
    if cur_result ~= "ok" then return end
    local map = ny.style_map[L.item]
    if map == nil then
        local names, hidden = {}, false
        for n = 0, 3 do
            local text_result, text = QD.ui.text("combat_interface:" .. n .. "_text")
            if text_result == "not_visible" then hidden = true end
            if text_result == "ok" then names[string.lower(text)] = names[string.lower(text)] or n end
        end
        -- (the names are the worn weapon's only once its swap has landed:
        -- a name this weapon must show proves the read is fresh)
        if not hidden and names[string.lower(want)] ~= nil then
            ny.style_map[L.item] = names
            map = names
        elseif hidden and ny.style_tab_for ~= L.item then
            QD.ui.tab("combat")
            ny.style_tab_for = L.item
            ny.style_tabs = (ny.style_tabs or 0) + 1
            return
        else
            return
        end
    end
    local slot = map[string.lower(want)]
    if slot == nil then ny.style_done = L.item return end
    if cur == slot then
        ny.style_done = L.item
        ny.style_set = (ny.style_set or 0) + 1
        return
    end
    -- a press needs the combat tab shown (seam55 probe_style)
    local text_result = QD.ui.text("combat_interface:" .. slot .. "_text")
    if text_result == "not_visible" then
        if ny.style_tab_for ~= L.item then
            QD.ui.tab("combat")
            ny.style_tab_for = L.item
            ny.style_tabs = (ny.style_tabs or 0) + 1
        end
        return
    end
    local widget_result, widget = QD.ui.widget("combat_interface:style_slot_" .. slot)
    if widget_result ~= "ok" then return end
    QD.ui.invoke(widget, 1)
    ny.style_press_tick = v.tick
    ny.style_presses = (ny.style_presses or 0) + 1
    st.inputs[v.tick] = (st.inputs[v.tick] or 0) + 1
end

-- The ids of the room's npcs, once per play (a symbol is a content name, an id
-- is what the npc rows carry).
function QD.raid._play_nylocas_ids(st)
    local P, N = st.plan, st.numbers
    local ids = { wave = {}, boss = {}, support = nil }
    for _, kind in ipairs(P.kinds) do
        for _, style in ipairs(P.styles) do
            local sym = "tob_nylocas_" .. kind .. "_" .. style .. N.suffix
            local r, id = api_drive.symbol("npc", sym)
            assert(r == "ok", "raid.play nylocas: no npc symbol " .. sym)
            ids.wave[id] = { symbol = sym, style = style, big = string.find(kind, "big", 1, true) ~= nil,
                fighting = string.find(kind, "fighting", 1, true) ~= nil }
        end
    end
    for form, sym in pairs(N.form) do
        local r, id = api_drive.symbol("npc", sym)
        assert(r == "ok", "raid.play nylocas: no npc symbol " .. sym)
        ids.boss[id] = { symbol = sym, form = form }
    end
    local r, id = api_drive.symbol("npc", N.support)
    assert(r == "ok", "raid.play nylocas: no npc symbol " .. N.support)
    ids.support = id
    return ids
end

-- Chebyshev distance from a tile to an npc's footprint (size n, south-west x,z).
function QD.raid._play_nylocas_dist(x, z, nx, nz, n)
    local dx, dz = 0, 0
    if x < nx then dx = nx - x elseif x > nx + n - 1 then dx = x - (nx + n - 1) end
    if z < nz then dz = nz - z elseif z > nz + n - 1 then dz = z - (nz + n - 1) end
    return math.max(dx, dz)
end

-- SEE (the room's part): every wave nylocas with its colour, size, tile, age
-- (ticks since this copy first appeared), the supports' bars and Vasilias.
function QD.raid._play_nylocas_see(st, v)
    local P, ny = st.plan, st.ny
    v.nylos, v.supports, v.vas = {}, {}, nil
    local nr, rows = api_drive.npcs(0)
    if nr ~= "ok" or type(rows) ~= "table" then return end
    local O = st.origin
    for _, row in ipairs(rows) do
        -- a row with no health bar yet reads -1/-1 (maiden seam30, mz30b): alive
        local alive = row.health_ratio == nil or row.health_ratio ~= 0
        local w = ny.ids.wave[row.npc_id] or ny.ids.wave[row.base_npc_id]
        local b = ny.ids.boss[row.npc_id] or ny.ids.boss[row.base_npc_id]
        if w ~= nil then
            local s = ny.seen[row.slot]
            -- raid seam32: a copy is NEW when its slot is new, or the slot was
            -- not seen for 8 ticks, or the size changed, or it stands further
            -- from where it was last seen than it can have walked.  The seam30
            -- rule ("not seen for 2 ticks") reset every age whenever the plan's
            -- own block ran three ticks (a swap and a press), so in a party,
            -- where blocks run long, every copy stayed "young" for good and
            -- the flicker guard passed over all of them (s32ny6 p2: pick=none
            -- with 22-33 copies present, every one filtered as young from
            -- wave 15), and the blast clock never reached a copy's last ticks.
            local gap = s ~= nil and (v.tick - s.last) or 0
            if s == nil or gap > 8 or s.big ~= w.big
                or math.max(math.abs(row.x - s.x), math.abs(row.z - s.z)) > gap + 2 then
                local lx, lz = row.x - O.x, row.z - O.z
                local lane = lx <= 18 or lx >= 45 or lz <= 10
                s = { first = v.tick, last = v.tick, style = w.style, flicker = false, lane = lane, big = w.big, x = row.x, z = row.z }
                ny.seen[row.slot] = s
                -- a wave is the tick new copies walk out of the tunnels
                -- (NT nylocas.lane_tiles); a split appears on the platform
                if lane and ny.wave_ticks[v.tick] == nil then
                    ny.wave_ticks[v.tick] = true
                    ny.waves = ny.waves + 1
                    ny.last_wave_tick = v.tick
                end
            end
            s.last = v.tick
            s.x, s.z = row.x, row.z
            if s.style ~= w.style then
                s.flicker = true
                s.style = w.style
                s.style_tick = v.tick
            end
            if alive then
                local age = v.tick - s.first
                local settled = ny.waves < st.plan.flicker_wave - 1 or age >= st.plan.settle_age
                    or (st.plan.split_settled and not s.lane)
                v.nylos[#v.nylos + 1] = { row = row, slot = row.slot, x = row.x, z = row.z, style = w.style, big = w.big,
                    fighting = w.fighting, symbol = w.symbol, size = w.big and 2 or 1, age = age, seen = s, settled = settled }
            end
        elseif b ~= nil then
            -- HER bar is no death sign: at 4 of 360 hitpoints it reads 0 of 30
            -- (svhplaynyloc, 2026-10-06: her bar read 0 from t861, the plan
            -- took her for gone, never prayed her melee form nor pressed her
            -- again, and died at t1237 with her alive at 4).  She is fought
            -- while her row is there; her npc_death row ends the play (library).
            v.vas = { row = row, slot = row.slot, x = row.x, z = row.z, form = b.form, symbol = b.symbol }
            if st.plan.trace and ny.vas_id_seen ~= tostring(row.npc_id) .. "/" .. tostring(row.base_npc_id) then
                ny.vas_id_seen = tostring(row.npc_id) .. "/" .. tostring(row.base_npc_id)
                local m = {}
                for id, e in pairs(ny.ids.boss) do m[#m + 1] = id .. "=" .. e.form end
                api_drive.report("nyplay vas t=" .. v.tick .. " row " .. ny.vas_id_seen .. " -> " .. b.form .. " ids " .. table.concat(m, " "))
            end
            if st.boss_symbol ~= b.symbol then st.boss_symbol = b.symbol end
        elseif (row.npc_id == ny.ids.support or row.base_npc_id == ny.ids.support) then
            local frac = 1
            if row.health_ratio ~= nil and row.health_scale ~= nil and row.health_scale > 0 and row.health_ratio >= 0 then
                frac = row.health_ratio / row.health_scale
            end
            v.supports[#v.supports + 1] = { x = row.x, z = row.z, frac = frac, alive = alive }
        end
    end
    -- her size from the library's own read of her (npc.state carries it)
    if v.vas ~= nil then
        v.vas.size = (v.boss ~= nil and type(v.boss.size) == "number") and v.boss.size or 3
    end
end

-- raid seam40 play_tob_nylocas_follows_blert: THE WEAPON PER COLOUR, AS
-- BLERT'S TRIOS SWING IT.  A seat's loadout may carry `<style>_big` (what it
-- wears on a big copy of that colour) and `<style>_boss` (on Vasilias in that
-- form) beside the plain style; the KEY is the loadout entry worn for the
-- target, the style is the colour the hit must be.  27 death-free Normal trio
-- rooms (reference/nylocas_normal_3.json; per colour in build/seam_state/
-- matthew-mbp-m4-raid-b1-seam40/ny40/ny_blert.out): the meleer's big greys
-- SCYTHE 121 of 311, every seat on her melee form SCYTHE (383 of 467 melee
-- hits on 8355).
function QD.raid._play_nylocas_key(ny, style, big, boss)
    if boss and ny.loadout[style .. "_boss"] ~= nil then return style .. "_boss" end
    if big and ny.loadout[style .. "_big"] ~= nil then return style .. "_big" end
    return style
end

-- A block of the plan's own: a loadout swap ("a gear swap is one tick",
-- PLAY_NOTES "Loadouts"; the library's SEND has no gear list), counted into
-- the record's inputs like the library's blocks (maiden's pattern).
-- `pray` (a prayer name or nil) rides in the same block, FIRST: the plan's
-- prayer goes out before its press, because a press can take a tick and the
-- library's own block comes after it (svbplaynyloc t664: her turn seen, the
-- cast pressed, the library's prayer read lit three ticks later on t667, her
-- first magic attack on t666 sent through Protect from Melee).
-- raid seam47 play_tob_nylocas_no_nulling: `then_press` (the decide's
-- press_now, or nil) is sent INSIDE the block right after the equip, and only
-- when the equip left (`pending`): the weapon click and the copy click land on
-- one server tick, in that order (see THE PRESS in the decide).
function QD.raid._play_nylocas_wear(st, v, style, pray, stop, then_press, gear)
    local ny = st.ny
    local L = style ~= nil and ny.loadout[style] or nil
    local r, d = QD.together(function()
        if pray ~= nil then QD.prayer.set(pray, true) end
        -- raid seam32: the step that ends the old engagement goes out with
        -- the swap, before it (P.swap_stop; see the caller)
        if stop ~= nil then QD.player.walk_to(stop.x, stop.z, 1) end
        -- owner_nylocas: her form's gear first (P.boss_gear)
        for _, item in ipairs(gear or {}) do
            QD.player.equip(item)
            ny.gear_equips = (ny.gear_equips or 0) + 1
            st.inputs[v.tick] = (st.inputs[v.tick] or 0) + 1
        end
        if L ~= nil then
            local er = QD.player.equip(L.item)
            if then_press ~= nil and er == "pending" then
                ny.block_pressed_now = true
                ny.block_presses = (ny.block_presses or 0) + 1
                ny.block_press_tick = QD.player._quick_now()
                then_press()
            end
        end
    end)
    st.inputs[v.tick] = (st.inputs[v.tick] or 0) + (L ~= nil and 1 or 0) + (pray ~= nil and 1 or 0) + (stop ~= nil and 1 or 0)
    if stop ~= nil then
        ny.swap_stops = (ny.swap_stops or 0) + 1
        st.engaged = false
        st.walk_target = stop
    end
    if pray ~= nil then
        -- the library's SEND reads v.lit: what this block lit is lit now
        for _, name in pairs(st.plan.prayer_of) do v.lit[name] = (name == pray) end
        ny.early_prayers = (ny.early_prayers or 0) + 1
    end
    if L == nil then
        st.blocks[r] = (st.blocks[r] or 0) + 1
        return r == "ok" or r == "split"
    end
    st.blocks[r] = (st.blocks[r] or 0) + 1
    ny.swaps = ny.swaps + 1
    if r ~= "ok" and r ~= "split" then
        st.refusals = st.refusals + 1
        if #st.lines < 6 then st.lines[#st.lines + 1] = "t" .. v.tick .. " wear " .. style .. " " .. tostring(r) .. ": " .. string.sub(tostring(d), 1, 140) end
        return false
    end
    ny.worn = style
    st.weapon = L
    return true
end

-- The hit delay of a launched attack, in ticks (wiki Hit delay: ranged
-- 1 + floor((3 + d) / 6), magic 1 + floor((1 + d) / 3), melee 0).
function QD.raid._play_nylocas_flight(style, d)
    if style == "ranged" then return 1 + math.floor((3 + d) / 6) end
    if style == "magic" then return 1 + math.floor((1 + d) / 3) end
    return 0
end

-- THE PLAN'S OWN SUPPLIES (raid seam31 play_tob_nylocas_green).  The
-- library's _play_supplies (raid_play.lua) eats and drinks whenever the
-- hitpoints are at or under the threat, and this room's threat (aggros in
-- reach, a support under a quarter, her prayed max) stays over the brew's
-- ceiling for hundreds of ticks: svaplaynyloc drank seven brew doses at 115
-- of 115 (t583-604) and ate six sharks at 82-99 around one collapse
-- (t406-418); 89-214 of the 760 healing carried were never realised in the
-- five 2026-10-06 runs, and every red name had drunk its last dose by t606-621
-- with Vasilias still to fight.  So the same rule, with two guards: a food is
-- eaten only when at least half its heal lands (it heals to the base), a brew
-- only when at least half its dose lands (it heals to base + 16, wiki
-- Saradomin brew).  And the chest's bandages (E :151 "After defeating the
-- Pestilent Bloat ... During Entry Mode this will always contain 10
-- bandages"; E :33 "Due to these bandages boosting the player's stats, combat
-- potions and ranging potions are not necessary except for the first two
-- bosses"): a food that heals 20 and boosts Attack/Strength/Defence 4+15%,
-- Ranged 4+10%, Magic 4 (tob_spectate.rs2 [opheld1,tob_bandages]).  The
-- sharks go first during the waves; from the interlude on the bandages go
-- first, and the interlude itself eats one ("During this brief interlude, the
-- team should heal up and boost", W :750) so she is fought boosted.
function QD.raid._play_nylocas_supplies(st, v, threat, interlude)
    local P = st.plan
    local boss_phase = interlude or v.vas ~= nil or st.ny.landed ~= nil
    local food, heal = nil, 0
    local order = boss_phase and P.food_boss or P.food_waves
    for _, row in ipairs(order) do
        local cr, n = QD.inv.count(row.item)
        if food == nil and cr == "ok" and n > 0 then food, heal = row.item, row.heal end
    end
    local brew = nil
    for _, name in ipairs(QD.RAID_PLAY_BREWS) do
        local cr, n = QD.inv.count(name)
        if brew == nil and cr == "ok" and n > 0 then brew = name end
    end
    local next_swing = QD.raid._play_next_swing(st, v)
    local free = (not st.engaged) or next_swing <= v.tick
    local eat_ready = v.tick - st.last_eat >= QD.RAID_PLAY_EAT_DELAY
    local drink_ready = v.tick - st.last_drink >= QD.RAID_PLAY_DRINK_DELAY
    -- the library's horizon, unchanged (raid_play.lua _play_supplies)
    local horizon = 2
    if free then
        horizon = (st.engaged and st.weapon.speed or QD.RAID_PLAY_EAT_DELAY) + QD.TOGETHER_CONFIRM_TICKS + 1
    end
    local need = threat(horizon)
    -- what each lands: the SERVER takes the library's block drink first, then
    -- the food (_play_send sends the drink before the eat; svdplaynyloc t401:
    -- brew 87 -> 103, then the shark at 103 healed 0), so a food in a combo
    -- lands only what the brew left under the base.
    -- A brew's overheal is not counted: on this server the boost over the
    -- base does not hold (svaplaynyloc t532-538, 2026-10-06: each brew read
    -- 99 -> 115 in its consume row and the raider row read 99 again the same
    -- tick and every tick after, with no hit landing; 30 doses went that way).
    -- That is the engine's stat snap-back (RAID_ORCHESTRATOR.md section 4,
    -- torirs_server_combat.c), not the game: a brew heals to the base here.
    local brew_heal = QD.RAID_PLAY_BREW_HEAL
    local food_alone = math.min(heal, v.hp_base - v.hp)
    local brew_alone = math.min(brew_heal, v.hp_base - v.hp)
    local eat, drink = nil, nil
    if v.hp <= need then
        if eat_ready and food ~= nil and food_alone * 2 >= heal then eat = food end
        if drink_ready and brew ~= nil and brew_alone * 2 >= brew_heal then
            if eat == nil then
                drink = brew
            elseif v.hp + food_alone <= need then
                local food_after = math.min(heal, v.hp_base - v.hp - brew_alone)
                if food_after * 2 >= heal then
                    drink = brew
                elseif brew_alone > food_alone then
                    eat, drink = nil, brew
                end
            end
        end
    end
    -- THE INTERLUDE: heal up and boost with a bandage (W :750), once
    if interlude and eat == nil and eat_ready and not st.ny.boosted then
        local cr, n = QD.inv.count("tob_bandages")
        if cr == "ok" and n > 0 then
            eat = "tob_bandages"
            st.ny.boosted = v.tick
        end
    end
    if eat ~= nil or drink ~= nil then
        st.ny.supply_need = need
    end
    if drink == nil and drink_ready then
        local missing = v.prayer_base - v.prayer
        if missing >= QD.RAID_PLAY_RESTORE_AMOUNT or v.prayer <= 2 then
            for _, name in ipairs(QD.RAID_PLAY_RESTORES) do
                local cr, n = QD.inv.count(name)
                if drink == nil and cr == "ok" and n > 0 then drink = name end
            end
        end
    end
    return eat, drink, need
end

-- THE NYLOCAS PLAN'S DECIDE (PLAY_NOTES.md "Nylocas, Entry solo").
function QD.raid._play_nylocas_decide(st, v)
    local P, N = st.plan, st.numbers
    if st.ny == nil then
        st.ny = { ids = QD.raid._play_nylocas_ids(st), seen = {}, wave_ticks = {}, waves = 0, worn = "ranged",
            swaps = 0, presses = 0, casts = 0, bursts = 0, holds = 0, escapes = 0, homes = 0, target = nil,
            doomed = {}, nulled = {}, frozen = {}, turns = {}, form = nil, next_turn = nil, prayer = nil, prayer_tick = -100,
            swing_seen = 0, flicker_cancels = 0, results = {}, vas_presses = {}, landed = nil }
        -- raid seam32: the seat's own loadout and reaches (a role may carry
        -- its own weapon for its colour: the ranger's blowpipe)
        local seat = (st.party ~= nil and st.party > 1) and P.roles[st.role] or nil
        st.ny.loadout, st.ny.reach = {}, {}
        for style, L in pairs(P.loadout) do st.ny.loadout[style] = L end
        for style, d in pairs(P.reach) do st.ny.reach[style] = d end
        if seat ~= nil and seat.loadout ~= nil then
            for style, L in pairs(seat.loadout) do
                st.ny.loadout[style] = { item = L.item, speed = L.speed, seqs = L.seqs, powered = L.powered }
                if L.reach ~= nil and P.reach[style] ~= nil then st.ny.reach[style] = L.reach end
            end
        end
        -- raid seam33: a trio seat starts with its own colour's weapon on
        -- (the harness wears it); alone the bow, as before
        if seat ~= nil then st.ny.worn = seat.colour end
        st.weapon = st.ny.loadout[st.ny.worn]
        -- raid seam47: the special's weapon, a party seat's (THE SPECIAL)
        if seat ~= nil and P.spec ~= nil then
            st.ny.loadout.melee_spec = { item = P.spec.item, speed = P.spec.speed, seqs = P.spec.seqs }
        end
    end
    local ny = st.ny
    -- (raid seam31 play_library_faults: the seam30 workaround for the
    -- library's death_serial starting at 0 is gone -- the library seeds it
    -- when the boss slot is first resolved, raid_play.lua _play_tick FAULT 2.)
    QD.raid._play_nylocas_see(st, v)
    -- raid seam55: THE STYLE AT HER, by name (P.boss_styles)
    if P.boss_styles ~= nil and st.party ~= nil and st.party > 1 and (v.vas ~= nil or ny.landed ~= nil) then
        QD.raid._play_nylocas_style(st, v)
    end
    -- raid seam47 play_tob_nylocas_no_nulling: THE HEART.  The seat that
    -- carries one (the mage) invigorated it at the door; it is pressed again
    -- once its cooldown is over and Magic has fallen under the heart's boost
    -- (base + 4 + 10 percent, imbued_heart.rs2 stat_boost(magic, 4, 10)), as
    -- Blert's mages hold 112 through the room.  The door press is the
    -- harness's, so the cooldown is counted from the plan's first tick.
    local seat_heart = (st.party ~= nil and st.party > 1 and P.roles[st.role] ~= nil) and P.roles[st.role].heart or nil
    if seat_heart ~= nil then
        if ny.heart_tick == nil then ny.heart_tick = v.tick end
        if v.tick - ny.heart_tick >= P.heart_cooldown then
            local cr, n = QD.inv.count(seat_heart)
            local mr, m = QD.skill.read("magic")
            if cr == "ok" and n > 0 and mr == "ok" and type(m) == "table"
                and m.level < m.base_level + 4 + math.floor(m.base_level / 10) then
                local hr = QD.player.inv_op(seat_heart, 1, { quick = true })
                st.inputs[v.tick] = (st.inputs[v.tick] or 0) + 1
                ny.heart_tick = v.tick
                ny.hearts = (ny.hearts or 0) + 1
                ny.heart_last = tostring(hr)
            end
        end
    end
    local dist = QD.raid._play_nylocas_dist
    local flight = QD.raid._play_nylocas_flight
    local intent = { want = {}, walk = nil, attack = false }
    local O, me = st.origin, v.me
    -- raid seam32: the raider's role in a party (nil alone: the Entry plan)
    local R = (st.party ~= nil and st.party > 1) and P.roles[st.role] or nil
    local home_tile = R ~= nil and R.home or P.home
    ny.role = R ~= nil and R.name or "solo"
    -- raid seam52: THE STANDS (see the plan's `stands`): through the waves a
    -- party seat's home is its role's reference stand for the wave it last saw
    ny.stand = nil
    if R ~= nil and P.stands ~= nil and v.vas == nil and ny.waves >= 1 then
        local row = P.stands[R.name]
        local s = row ~= nil and row[math.min(ny.waves, 31)] or nil
        if s ~= nil then
            home_tile = { P.stand_anchor[1] + s[1], P.stand_anchor[2] + s[2] }
            ny.stand = math.min(ny.waves, 31)
        end
    end
    -- raid seam52: the cleanup, read once a tick (THE CLEANUP'S ORDER and the
    -- own-colour rule below share it)
    ny.cleanup = P.cleanup_free and ny.waves >= P.cleanup_waves and v.tick - (ny.last_wave_tick or v.tick) >= P.cleanup_quiet
    ny.in_cleanup = ny.cleanup
    local home = { x = O.x + home_tile[1], z = O.z + home_tile[2] }
    local vas = v.vas

    -- raid seam32: THE PARTY'S OWN-SWING READ.
    -- (1) The leader's own pid in its tick log.  api_drive.players' `me` row
    -- (the library's st.my_pid) counts from 1 and the log's pid from 0 (the
    -- maiden seam32 finding, s32mzn1), so in a party the library read the
    -- ranger's bow swings as the mage's own.  Re-read once: the one log pid
    -- standing on this raider's tile, alone, at the newest tile tick.
    if st.party ~= nil and st.party > 1 and st.log and not ny.pid_fixed then
        local tr, rows = QD.ticklog.rows({ kind = "player_tile", since = ny.tile_serial or 0 })
        if tr == "ok" and type(rows) == "table" and #rows > 0 then
            local newest = rows[#rows].tick
            local mine, others = nil, 0
            for _, row in ipairs(rows) do
                ny.tile_serial = math.max(ny.tile_serial or 0, row.serial or 0)
                if row.tick == newest and row.x == me.x and row.z == me.z then
                    if mine == nil then mine = row.pid else others = others + 1 end
                end
            end
            if mine ~= nil and others == 0 then
                ny.pid_was = st.my_pid
                st.my_pid = mine
                ny.pid_fixed = true
                -- the swings read under the wrong pid are not this raider's
                st.swings, st.last_swing = {}, -1000
                ny.swing_seen = 0
            end
        end
    end
    -- (2) A member holds no tick log ("The world's one tick log lives with the
    -- leader", _party_smoke.lua), so it never read a swing of its own
    -- (s32ny2: p2 and p3 "0 swings" in 933 ticks) and its turn hold and its
    -- re-press ran blind.  What its own screen shows on every swing that hits
    -- is the experience it is paid: Hitpoints with every style, Magic with
    -- every cast (a person sees the XP drop).  A rise in either is a swing on
    -- the tick it is read; a 0 pays nothing and goes unread (the plan's
    -- re-press covers it).
    if st.party ~= nil and st.party > 1 then
        local _, hp_read = QD.skill.read("hitpoints")
        local _, mg_read = QD.skill.read("magic")
        local hx = type(hp_read) == "table" and hp_read.experience or nil
        local mx = type(mg_read) == "table" and mg_read.experience or nil
        if hx ~= nil and mx ~= nil then
            if ny.xp_hp ~= nil and (hx > ny.xp_hp or mx > ny.xp_mg) then
                ny.xp_swings = (ny.xp_swings or 0) + 1
                -- every raider records the XP read beside its swings (the
                -- harness compares the two: note.xp_swing_lag).  Raid seam48
                -- (closer): a member's swings are the starts its own screen
                -- shows (raid_play.lua _play_see, t.raid.own_anim), so an XP
                -- rise is no longer added as a swing -- it would count each
                -- swing twice, the second a tick or more late.
                ny.xp_probe = ny.xp_probe or {}
                if #ny.xp_probe < 40 then ny.xp_probe[#ny.xp_probe + 1] = v.tick end
            end
            ny.xp_hp, ny.xp_mg = hx, mx
        end
    end
    -- my own swings (the library reads them off player_anim): the copy I was
    -- on is doomed until the hit has had time to land (2 or 3 hitpoints, E :161)
    while ny.swing_seen < #st.swings do
        ny.swing_seen = ny.swing_seen + 1
        local tk = st.swings[ny.swing_seen]
        local t = ny.target
        if t ~= nil then
            t.swung = tk
            ny.swung_slot = t.slot
            if not t.vas then
                ny.doomed[t.slot] = tk + flight(t.style, t.d or 1) + 2
                for _, s in ipairs(t.also or {}) do ny.doomed[s] = tk + flight(t.style, t.d or 1) + 2 end
            end
        end
    end

    -- raid seam32: NULLED, read off the screen.  A raider's hit on a copy it
    -- is nulled on prints "Your attack has no effect on this Nylocas." (DMG
    -- :308-310 tob_nylo_no_effect, with the shielded spotanim on the copy).
    -- The copy the raider last swung at is struck off its list for good
    -- (s32ny4 p2: an Ice Burst's splash nulled the ranger on a green that
    -- spawned under it at t191-192, and it then shot that green twelve times
    -- for 0, t201-234, "tgt 1083" every raider row).
    local msr, serial = api_drive.message_serial()
    if msr == "ok" and type(serial) == "number" then
        if ny.msg_serial == nil then
            ny.msg_serial = serial
        elseif serial > ny.msg_serial then
            local mr, rows = api_drive.messages()
            if mr == "ok" and type(rows) == "table" then
                for i = 1, #rows do
                    if rows[i].serial > ny.msg_serial and string.find(rows[i].text or "", "no effect on this Nylocas", 1, true) ~= nil
                        and not (ny.chin_quiet ~= nil and v.tick <= ny.chin_quiet) then
                        local slot = ny.swung_slot or (ny.target and ny.target.slot)
                        if slot ~= nil and not ny.nulled[slot] then
                            ny.nulled[slot] = true
                            ny.null_reads = (ny.null_reads or 0) + 1
                            if ny.target ~= nil and ny.target.slot == slot then ny.target = nil end
                        end
                    end
                end
            end
            ny.msg_serial = serial
        end
    end

    -- HER FORM.  She lands melee (W :754 "Vasilias will always spawn in its
    -- melee form"), turns every 15 ticks after a first 14 (NB :436-464), and
    -- the turn stops every player's attack ("The player will stop attacking
    -- when Vasilias changes forms", W :752; NB :176 p_stopaction).
    if vas ~= nil and vas.form ~= ny.form then
        if ny.form ~= nil and ny.form ~= "spawning" and vas.form ~= "spawning" then
            ny.turns[#ny.turns + 1] = { tick = v.tick, form = vas.form }
            ny.next_turn = v.tick + (N.window or P.window)
        elseif vas.form ~= "spawning" then
            ny.landed = v.tick
            ny.next_turn = v.tick + (N.first_window or P.first_window)
            -- raid seam32: the supports' bars on the tick she lands (the
            -- report's "pillars at the boss")
            local bars = {}
            for _, s in ipairs(v.supports) do bars[#bars + 1] = string.format("%d,%d:%.2f%s", s.x - st.origin.x, s.z - st.origin.z, s.frac, s.alive and "" or "x") end
            ny.supports_at_landing = table.concat(bars, " ")
            ny.supports_alive_at_landing = 0
            for _, s in ipairs(v.supports) do if s.alive then ny.supports_alive_at_landing = ny.supports_alive_at_landing + 1 end end
            -- raid seam33: the weakest standing bar (the kept trio's row asks
            -- every support above half)
            ny.supports_min_at_landing = nil
            for _, s in ipairs(v.supports) do
                if s.alive and (ny.supports_min_at_landing == nil or s.frac < ny.supports_min_at_landing) then ny.supports_min_at_landing = s.frac end
            end
        end
        ny.form = vas.form
        ny.target = nil
    end

    -- PRAYER.  Vasilias: by her form, from the tick she is seen (W :752
    -- "always switch protection prayers to Protect from Melee before
    -- attacking ... When its form changes, the player should again switch";
    -- her first attack in a form comes 2-3 ticks after the turn, NB :146-150,
    -- so a switch read on the turn is in force for it).  The waves: the
    -- colour of the swinging majority among the aggros in reach (a big counts
    -- two, a grey only within two tiles: it swings adjacent, NR :1086), held
    -- until another colour is clearly heavier, so one prayer is on per hit.
    local pray_style = ny.prayer
    if vas ~= nil then
        pray_style = (vas.form == "spawning") and "melee" or vas.form
    else
        local weight = { melee = 0, ranged = 0, magic = 0 }
        for _, n in ipairs(v.nylos) do
            if n.fighting then
                local d = dist(me.x, me.z, n.x, n.z, n.size)
                local reach = (n.style == "melee") and 2 or 9
                if d <= reach then weight[n.style] = weight[n.style] + (n.big and 2 or 1) end
            end
        end
        local best, bw = nil, 0
        for _, s in ipairs(P.styles) do
            if weight[s] > bw then best, bw = s, weight[s] end
        end
        if best ~= nil and best ~= pray_style then
            local cur = pray_style ~= nil and weight[pray_style] or 0
            if pray_style == nil or bw >= cur + 2 or (cur == 0 and v.tick - ny.prayer_tick >= 3) then
                pray_style = best
            end
        end
    end
    if pray_style ~= ny.prayer then
        ny.prayer = pray_style
        ny.prayer_tick = v.tick
    end
    if pray_style ~= nil then
        intent.want[P.prayer_of[pray_style]] = true
        -- (raid seam31 play_library_faults: the old one is left to the server
        -- by the library now -- _play_pray sends no "off" for a prayer that
        -- shares an exclusion group with the one it lights, FAULT 1; the
        -- seam30 workaround that kept the lit one wanted is gone.)
    end

    -- THE BLAST.  A copy explodes 51 ticks after it appears (52 a big), within
    -- two tiles of its body (NT lifetime_small/big; E :161).  Danger runs from
    -- six ticks before to one after; a raider inside its reach leaves on sight.
    -- owner_nylocas: IN THE CLEANUP THE LAST GREYS ARE KILLED.  Six ticks of
    -- danger before a pop kept every seat off a clump of greys once its
    -- oldest was six from its pop, and the next aged into the window before
    -- that one went: the traced svcplaynyloc cleanup ended on three greys
    -- beside the north-west support popping at 52, every seat's pick none
    -- with why=blast for the last nine ticks (t441-450), the room 51 ticks
    -- after wave 31 against Blert's 32.  Blert's trios kill small greys in
    -- their last six ticks too (30 at ages 45-50 in the 27 rooms).  In the
    -- cleanup the danger is the last `cleanup_blast` ticks only.
    local blast_lead = (ny.in_cleanup and P.cleanup_blast ~= nil) and P.cleanup_blast or 6
    local danger = {}
    for _, n in ipairs(v.nylos) do
        local ea = n.big and P.explode_age_big or P.explode_age
        if n.age >= ea - blast_lead and n.age <= ea + 1 then danger[#danger + 1] = n end
    end
    local function in_support(x, z)
        for _, s in ipairs(P.supports) do
            local sx, sz = O.x + s[1], O.z + s[2]
            if x >= sx and x <= sx + 2 and z >= sz and z <= sz + 2 then return true end
        end
        return false
    end
    local function floor_ok(x, z)
        local lx, lz = x - O.x, z - O.z
        if lx < P.floor[1] or lx > P.floor[3] or lz < P.floor[2] or lz > P.floor[4] then return false end
        if in_support(x, z) then return false end
        if vas ~= nil and dist(x, z, vas.x, vas.z, vas.size) == 0 then return false end
        return true
    end
    local function unsafe(x, z, margin)
        for _, n in ipairs(danger) do
            if dist(x, z, n.x, n.z, n.size) <= P.blast + (margin or 0) then return true end
        end
        return false
    end
    if unsafe(me.x, me.z, 0) then
        local best, bx, bz = nil, nil, nil
        for dx = -2, 2 do
            for dz = -2, 2 do
                local x, z = me.x + dx, me.z + dz
                if (dx ~= 0 or dz ~= 0) and floor_ok(x, z) and not unsafe(x, z, 0) then
                    local score = math.max(math.abs(dx), math.abs(dz)) * 10
                        + math.max(math.abs(x - home.x), math.abs(z - home.z))
                    if best == nil or score < best then best, bx, bz = score, x, z end
                end
            end
        end
        if bx ~= nil then
            intent.walk = { x = bx, z = bz }
            ny.escapes = ny.escapes + 1
            ny.target = nil
        end
    end

    -- raid seam51 play_tob_nylocas_whole: ANOTHER SEAT'S SHOT IN THE AIR.
    -- What a person sees of the others' attacks: the dart or the orb flying
    -- from them to a copy (api_drive.projectiles: its target slot and the
    -- tile it left).  Seam50's dup.py: 81 of 199 copies pressed by two or
    -- three seats, the mage sharing 87 of 145 engagements, and a small has
    -- 8 hitpoints in a trio (W :731) -- the second shot lands on a corpse.
    -- A small with another raider's shot on it, or a big with two, is worth
    -- P.inbound points less to this raider while the shot flies.
    local inbound = {}
    if st.party ~= nil and st.party > 1 and P.inbound ~= nil and api_drive.projectiles ~= nil then
        local pr, prow = api_drive.projectiles(24)
        if pr == "ok" and type(prow) == "table" then
            for _, p in ipairs(prow) do
                local slot = p.target_npc_slot
                if type(slot) == "number" and slot >= 0 and p.src_x ~= nil and p.src_z ~= nil
                    and math.max(math.abs(p.src_x - me.x), math.abs(p.src_z - me.z)) > 1 then
                    inbound[slot] = (inbound[slot] or 0) + 1
                end
            end
        end
    end
    -- THE TARGET.
    local cur = ny.target
    local cur_row = nil
    if cur ~= nil and not cur.vas then
        for _, n in ipairs(v.nylos) do
            if n.slot == cur.slot then cur_row = n end
        end
        if cur_row ~= nil and cur.colour ~= nil and cur_row.style ~= cur.colour then
            -- a flicker turned under the press: never let the old colour land
            -- (DMG :272, the raider is nulled on it for good)
            ny.flicker_cancels = ny.flicker_cancels + 1
            cur_row = nil
            if intent.walk == nil then
                intent.walk = { x = me.x, z = me.z }
                if floor_ok(me.x + 1, me.z) then intent.walk = { x = me.x + 1, z = me.z } end
            end
        end
        if cur_row == nil then ny.target = nil cur = nil end
    end
    local pick = nil
    if vas ~= nil then
        if vas.form ~= "spawning" then
            local d = dist(me.x, me.z, vas.x, vas.z, vas.size)
            pick = { slot = vas.slot, style = vas.form, symbol = vas.symbol, vas = true, d = d, x = vas.x, z = vas.z,
                key = QD.raid._play_nylocas_key(ny, vas.form, false, true) }
            -- raid seam47: THE SPECIAL (P.spec): the claws on her melee form
            -- while the orb holds the cost and her window has room for it
            if vas.form == "melee" and R ~= nil and P.spec ~= nil and ny.loadout.melee_spec ~= nil then
                local sp = ny.spec or { fired = 0, arms = 0 }
                ny.spec = sp
                local _, energy = QD.var.varp("varp300_sa_energy")
                energy = tonumber(energy) or 0
                if sp.armed ~= nil and energy <= sp.energy0 - P.spec.cost then
                    sp.fired = sp.fired + 1
                    sp.last_fired = v.tick
                    sp.armed = nil
                elseif sp.armed ~= nil and v.tick - sp.armed > P.spec.give_up then
                    sp.lost = (sp.lost or 0) + 1
                    sp.armed = nil
                end
                local cr, n = QD.inv.count(P.spec.item)
                local have = (cr == "ok" and n > 0) or ny.worn == "melee_spec"
                local left = ny.next_turn ~= nil and (ny.next_turn - v.tick) or 99
                if sp.armed ~= nil then
                    pick.key = "melee_spec"
                elseif have and energy >= P.spec.cost and left >= P.spec.room and sp.arms < 4 then
                    pick.key = "melee_spec"
                    pick.spec = true
                    pick.energy = energy
                end
            end
        end
    else
        -- the support each chewer bites, and how much of it is left
        -- owner_nylocas: FOOTPRINT TO FOOTPRINT.  The second test measured
        -- from the support's south-west TILE to the copy, so only a copy beside
        -- that corner read as a chewer: a small at 28,19 or 26,21 beside the
        -- 3x3 support at 25,18 (its east and north faces) and every big on
        -- those faces were no support's (the traced svbplaynyloc run: a big
        -- green and a big grey beside 25,18 at 28,19 from t344, the big green
        -- popping there at 54 ticks unpressed while the support fell), so the
        -- keep rules (keep_all, keep_low, keep_urgent) never weighed them.
        -- A copy chews when its footprint touches the support's 3x3.
        local function support_of(n)
            for _, sp in ipairs(v.supports) do
                if sp.alive then
                    local gx = math.max(sp.x - (n.x + n.size - 1), n.x - (sp.x + 2), 0)
                    local gz = math.max(sp.z - (n.z + n.size - 1), n.z - (sp.z + 2), 0)
                    if math.max(gx, gz) == 1 then return sp end
                end
            end
            return nil
        end
        local alive_supports, lowest = 0, nil
        for _, sp in ipairs(v.supports) do
            if sp.alive then
                alive_supports = alive_supports + 1
                if lowest == nil or sp.frac < lowest.frac then lowest = sp end
            end
        end
        local best, cands, own_ok = nil, {}, false
        -- raid seam50 play_tob_nylocas_whole: THE COLOUR'S OWNER, ON SCREEN.
        -- What a person sees of the other two: their tiles (api_drive.players)
        -- and their names, which say their seats (QD.party.names(), seat i =
        -- role i).  owner_at[style] = the tile of the seat that owns it.
        local owner_at = {}
        if R ~= nil and P.leave_to_owner ~= nil then
            local seat_of = {}
            for i, nm in ipairs(QD.party.names()) do seat_of[string.lower(string.gsub(nm, "[ _]", ""))] = i end
            local pr, prow = api_drive.players()
            if pr == "ok" then
                for _, r in ipairs(prow) do
                    local seat = (not r.me and r.name ~= nil) and seat_of[string.lower(string.gsub(r.name, "[ _]", ""))] or nil
                    local role = seat ~= nil and P.roles[seat] or nil
                    if role ~= nil then owner_at[role.colour] = { x = r.x, z = r.z } end
                end
            end
            local seen = 0
            for _ in pairs(owner_at) do seen = seen + 1 end
            if (ny.owners_seen or -1) ~= seen then
                ny.owners_seen = seen
                api_drive.report("nyplay owners seen " .. seen .. " (seat " .. tostring(st.role) .. ")")
            end
        end
        local function consider(cand)
            if best == nil or cand.score < best.score then best = cand end
        end
        for _, n in ipairs(v.nylos) do
            local ea = n.big and P.explode_age_big or P.explode_age
            local d = dist(me.x, me.z, n.x, n.z, n.size)
            local lx, lz = n.x - O.x, n.z - O.z
            local on_floor = lx >= P.floor[1] and lx <= P.floor[3] and lz >= P.floor[2] and lz <= P.floor[4]
            local ok = (ny.doomed[n.slot] == nil or ny.doomed[n.slot] < v.tick) and not ny.nulled[n.slot]
                and (ny.blocked == nil or (ny.blocked[n.slot] or -1) < v.tick)
                -- from wave 16 a copy's colour may still turn (NT flicker_*)
                and n.settled
            if P.trace_seat ~= nil then
                local why = nil
                if not (ny.doomed[n.slot] == nil or ny.doomed[n.slot] < v.tick) then why = "doom"
                elseif ny.nulled[n.slot] then why = "null"
                elseif not (ny.blocked == nil or (ny.blocked[n.slot] or -1) < v.tick) then why = "block"
                elseif not n.settled then why = "young"
                elseif n.style == "melee" and not on_floor then why = "lane"
                elseif n.style == "melee" and not (n.age < ea - blast_lead and not unsafe(n.x, n.z, blast_lead < 6 and 0 or 1)) then why = "blast"
                elseif n.style ~= "melee" and not (d <= ny.reach[n.style] + 6) then why = "far" end
                if why ~= nil then ny.why = ny.why or {} ny.why[why] = (ny.why[why] or 0) + 1 end
            end
            if ok and n.style == "melee" then
                -- "cannot melee them until they reach said platform" (E :160),
                -- and never into a blast
                ok = on_floor and n.age < ea - blast_lead and not unsafe(n.x, n.z, blast_lead < 6 and 0 or 1)
            elseif ok then
                -- raid seam32: a seat goes further for its own colour (the
                -- trio guide's raiders walk to the lane their colour comes from:
                -- "Path west and stand 1 tile away from the west barrier",
                -- ranger waves 8-10)
                ok = d <= ny.reach[n.style] + ((R ~= nil and n.style == R.colour) and 14 or 6)
                -- raid seam32: in a party the blues are the mage's ("Trio: x1
                -- mager", W :711); another seat casts only at a blue aggro
                -- swinging at it.  A helper's Ice Rush is five ticks against its
                -- own weapon's two or four, and its press failed half the time
                -- (s32ny10 p3 t150-200: 13 casts pressed, 3 covered, 4 refused,
                -- seven swings in fifty ticks)
                -- (raid seam33: a seat with a powered staff takes blues like
                -- any other colour, under the own-colour-first rule)
                if ok and R ~= nil and n.style == "magic" and R.colour ~= "magic" and not ny.loadout.magic.powered
                    and not (n.fighting and d <= 8) then ok = false end
            end
            local sp = (not n.fighting) and support_of(n) or nil
            n.support = sp
            if ok then
                -- the cost of the kill in ticks: the run to reach (two tiles a
                -- tick, wiki Energy: run) and a swap, weighed against what the
                -- copy costs while it lives
                local walk = math.max(0, d - ny.reach[n.style]) / 2
                local score = walk * 6
                -- raid seam32: the kill's own time, the weapon's ticks a swing
                -- (s32ny8 p2: the blowpipe seat cast Ice Rush 16 times in the
                -- waves, five ticks each, 80 ticks that were 40 blowpipe darts)
                local key = QD.raid._play_nylocas_key(ny, n.style, n.big, false)
                -- owner_nylocas: THE SCYTHE ON A GREY STACK.  "Scythe the grey
                -- doubles" (trio guide :232-239, :415; PLAY_NOTES seam35m's
                -- next lever): the scythe's swing lands on the target's own
                -- tile and the two beside it, a copy counted when its tile is
                -- one of them, at 100 / 50 / 25 percent of its max (content
                -- scythe_of_vitur.rs2, NR ScytheOfViturCombat :139-159), so a
                -- stack of small greys on one tile is two or three kills a
                -- swing (8 hitpoints each in a trio).  The cleanups of the
                -- blast5 survey ended on two or three greys stacked beside the
                -- north-west support (28,30), whipped one at a time.  A grey
                -- with another small grey on its tile and no other colour
                -- within a tile (the arc would null the raider on it) is
                -- taken with the scythe, worth `grey_stack` points (a whip
                -- swing's ticks) for each other grey on the tile.
                n.stack = nil
                if R ~= nil and P.grey_stack ~= nil and n.style == "melee" and not n.big then
                    local scy = nil
                    if ny.loadout.melee ~= nil and ny.loadout.melee.item == "scythe_of_vitur" then scy = "melee"
                    elseif ny.loadout.melee_big ~= nil and ny.loadout.melee_big.item == "scythe_of_vitur" then scy = "melee_big" end
                    if scy ~= nil then
                        local stack, other = {}, false
                        for _, o in ipairs(v.nylos) do
                            if o.slot ~= n.slot and math.max(math.abs(o.x - n.x), math.abs(o.z - n.z)) <= 1 then
                                if o.style == "melee" and not o.big and o.x == n.x and o.z == n.z then
                                    if not ny.nulled[o.slot] then stack[#stack + 1] = o end
                                elseif o.style ~= "melee" then
                                    other = true
                                end
                            end
                        end
                        if #stack >= 1 and not other then
                            key = scy
                            n.stack = stack
                            score = score - P.grey_stack * #stack
                            ny.stack_cands = (ny.stack_cands or 0) + 1
                        end
                    end
                end
                if R ~= nil then score = score + ny.loadout[key].speed * 3 end
                if n.fighting then
                    -- aggros first: they "must be killed as fast as possible"
                    -- (E :160); the one hitting through the prayer before all
                    local hitting = (n.style == "melee" and d <= 1) or (n.style ~= "melee" and d <= 8)
                    local covered = n.style == pray_style
                    if hitting and not covered then score = score - 45
                    elseif hitting then score = score - 30
                    else score = score - 20 end
                    -- raid seam32: an aggro is EVERY seat's ("These aggro's
                    -- should be prioritised first", W :733): no colour penalty,
                    -- only its weapon's time below.  (A first cut kept another
                    -- seat's covered aggro for its owner: s32ny5 t389 the ranger
                    -- stood among 12 live aggros, f=12, and died to them.)
                end
                -- raid seam50 play_tob_nylocas_whole: A GREY AT A HELPER'S FEET.
                -- Blert's trio mage and ranger melee 9.5 and 8.1 percent of their
                -- swings (reference/nylocas_normal_3.json role.mage.melee_pct,
                -- role.range.melee_pct; the meleer 62.7).  Ours never did: the
                -- own-first rule left the split greys of a big a helper had just
                -- scythed chewing beside it (seam50 base, _play_nylocas: 13 of 29
                -- copies that popped were greys, 12 of them splits on a support;
                -- t46 (26,21) beside the mage at (27,21) for six ticks).  A
                -- helper takes a grey in reach with no step, at no colour cost.
                local helps = P.help_feet and R ~= nil and R.colour ~= "melee" and n.style == "melee" and d <= 1 and not n.fighting
                -- owner_nylocas: A LOW SUPPORT'S CHEWER IS EVERY SEAT'S.  The
                -- reference's trios land her with all four supports standing
                -- in 34 of 34 rooms (weakest 0.10-0.54); ours lost the south-west
                -- one (25,18) on two of five names in the gear5 survey, chewed
                -- by splits of the colour whose seat was across the room
                -- (svbplaynyloc: split greys 43+53+71+44 copy-ticks beside it,
                -- split blues 40+49+36).  A chewer on a support under keep_low
                -- is taken by whoever is near, like an aggro: the colour rules
                -- (own_first, own_colour_bonus, leave_to_owner) do not hold it back.
                n.lowchew = R ~= nil and P.low_any and not n.fighting and sp ~= nil and P.keep_low ~= nil and sp.frac < P.keep_low
                if n.lowchew then helps = true end
                n.helps = helps
                if not n.fighting and R ~= nil and n.style ~= R.colour and not helps then score = score + P.own_colour_bonus end
                -- raid seam50: LEAVE IT TO ITS OWNER.  Two or three seats pressed
                -- the same copy on 85 of the 199 targets of the base leader (its
                -- input rows: the mage shared 93 of 136 engagements -- blues and
                -- greens with the ranger 29, aggros by all three 21), and a
                -- small (8 hitpoints) takes one landed hit: the second swing is
                -- thrown at a corpse.  Blert's trios swing about one attack a
                -- kill (role.*.phase.waveN.attacks_add: 233 a room).  Another
                -- seat's copy within its owner's reach, and nearer its owner than
                -- this raider, is its owner's; an aggro too, unless it is hitting
                -- this raider through the prayer.
                if R ~= nil and n.style ~= R.colour and owner_at[n.style] ~= nil then
                    local ow = owner_at[n.style]
                    local od = dist(ow.x, ow.z, n.x, n.z, n.size)
                    local oreach = n.style == "melee" and 1 or (n.style == "ranged" and 5 or 6)
                    local mine_now = n.fighting and ((n.style == "melee" and d <= 1) or (n.style ~= "melee" and d <= 8)) and n.style ~= pray_style
                    if not mine_now and od <= oreach + 2 and od <= d then score = score + P.leave_to_owner end
                end
                if not n.fighting and sp ~= nil then
                    -- a chewer: "keep the pillars alive" (E :155); but "it's best to
                    -- let one that's low die and focus on the other three" (E :171)
                    -- raid seam35m: a trio keeps all four ("all four standing"
                    -- in 34 of 34 recorded Regular trios, weakest 0.10..0.54 at
                    -- her landing): the low one is defended, not let go
                    if sp == lowest and sp.frac < 0.15 and alive_supports > 1 and not (R ~= nil and P.keep_all) then score = score + 25
                    else
                        score = score - 12 - (1 - sp.frac) * (R ~= nil and P.keep_all and P.keep_weight or 12)
                        -- raid seam51: a low support's chewer before anything
                        -- but an aggro hitting through the prayer
                        if R ~= nil and P.keep_all and P.keep_low ~= nil and sp.frac < P.keep_low then score = score - P.keep_urgent end
                    end
                end
                -- "Focus the green (Ranged) Nylocas first" (E :162)
                if n.style == "ranged" then score = score - 4 end
                -- raid seam32: NEWER first, as the source says: "always kill
                -- newly spawned nylocas after dealing with aggro's, prioritising
                -- the smaller ones first" (W :746), and "Allow all the existing
                -- Nylos in the room to auto-pop" (trio guide :24).  A copy's
                -- worth is the chewing it has left: a new one bites for ~40
                -- ticks, one near its pop for a few.  (The seam30 line read the
                -- same quote as "older first"; Entry's pillars forgave it,
                -- Normal's do not: s32ny5 killed at an average age of 23.6
                -- ticks, half a life spent chewing.)
                if R ~= nil then
                    score = score + math.min(n.age, 45) * ((P.pop_skip ~= nil and sp ~= nil) and 0.1 or 0.3)
                    -- raid seam50: AN OLD CHEWER IS STILL A COPY.  The +20 from ten
                    -- ticks before its pop let every chewer that outlived its first
                    -- forty ticks pop on its support (base: 29 pops a room, 42 copies
                    -- living 35+ ticks, the alive cap held into a stall from wave 11;
                    -- Blert's trios pop 0.2-2.5).  A chewer is passed over only in
                    -- its last P.pop_skip ticks, when the kill saves no bite.
                    local skip = (P.pop_skip ~= nil and sp ~= nil) and P.pop_skip or 10
                    if not n.fighting and n.age >= ea - skip and not (n.big and P.big_first ~= nil) then score = score + 20 end
                    -- raid seam52: THE CLEANUP'S ORDER (the plan's
                    -- `cleanup_order`): newest first, and one about to pop
                    -- is left to pop
                    -- owner_nylocas: SMALLS ONLY.  The sources' cleanup order
                    -- is for smalls ("Kill any wave 29/31 smalls first",
                    -- ranger cleanup :363; "Continue Ayaking smalls with the
                    -- highest wave number", mage :175), and a big that blows
                    -- up still leaves its two splits (tob_nylocas.rs2
                    -- ~tob_nylo_detonate, blert 15 of 15): the sup5 survey's
                    -- cleanups ended on the splits of a wave-30 big left to
                    -- 46-55 ticks (svaplaynyloc +51 pop, +55 its splits).
                    -- Blert's trios let 0.2-2.5 bigs a room pop (seam47).
                    if ny.in_cleanup and P.cleanup_order ~= nil and not n.big then
                        score = score + math.min(n.age, 45) * P.cleanup_order.age
                        if P.cleanup_order.side ~= nil and not n.fighting
                            and ((n.z - O.z) >= P.cleanup_order.north_z) ~= (R.name == P.cleanup_order.north_role) then
                            score = score + P.cleanup_order.side
                        end
                        -- owner_nylocas: LEFT TO POP ONLY IF IT POPS BEFORE THE
                        -- ROOM WOULD BE CLEAR.  The room ends on its last copy's
                        -- death, a pop included, so a copy left to pop past the
                        -- time the others take to kill holds the room open: the
                        -- low5 survey's cleanups ended 51-64 ticks after wave 31
                        -- on the wave-31 copies' own pops (52), where Blert's
                        -- end 32 after it (outcome.phase.cleanup_end.start 292 -
                        -- wave31.start 260).  The others' time is their count
                        -- over the reference trio's kill cadence (0.71 a tick,
                        -- reference/nylocas_normal_3.json, PLAY_NOTES seam40).
                        if ny.clear_tick ~= v.tick then
                            local need = 0
                            for _, o in ipairs(v.nylos) do
                                local oea = o.big and P.explode_age_big or P.explode_age
                                if oea - o.age > P.cleanup_order.expire then need = need + 1 end
                            end
                            ny.clear_tick, ny.clear_time = v.tick, need / P.cleanup_order.rate
                        end
                        if not n.fighting and ea - n.age <= P.cleanup_order.expire and ea - n.age < ny.clear_time
                            and not (sp ~= nil and P.keep_low ~= nil and sp.frac < P.keep_low) then
                            score = score + P.cleanup_order.pass
                            ny.cleanup_passes = (ny.cleanup_passes or 0) + 1
                        end
                    end
                else
                    score = score - math.min(n.age, 45) * 0.2
                end
                -- raid seam47: BIGS ARE NOT LEFT.  Blert's trios kill a big at
                -- a median age of 16-19 (p75 22-27) and let 0.2-2.5 a room pop;
                -- this plan's bigs lived to p75 44 (blue) and 55 (grey) with
                -- 4.3 a room popping each (seam40 ny40/ny_age.py over the s47
                -- surveys, build/seam_state/...seam47/age_before_bigfirst.log):
                -- a big chews its pillar and holds the alive cap for its whole
                -- life.  In a party a big is preferred by P.big_first points and
                -- is never left to pop.
                if n.big then
                    if R ~= nil and P.big_first ~= nil then score = score - P.big_first else score = score + 2 end
                end
                -- raid seam52: THE STAND'S LEASH (the plan's `stand_leash`)
                if R ~= nil and P.stand_leash ~= nil and vas == nil and not n.fighting and n.style ~= "melee" then
                    local hd = dist(home.x, home.z, n.x, n.z, n.size)
                    if hd > ny.reach[n.style] then score = score + (hd - ny.reach[n.style]) * P.stand_leash end
                end
                if key ~= ny.worn then score = score + ((R ~= nil) and 10 or 5) end
                -- raid seam51: ANOTHER SEAT'S SHOT IN THE AIR (above); never
                -- on the copy this raider is on (its own shot is its own)
                local inb = inbound[n.slot] or 0
                if R ~= nil and P.inbound ~= nil and inb > 0 and not (cur ~= nil and cur.slot == n.slot)
                    and (not n.big or inb >= 2) and not (sp ~= nil and P.keep_low ~= nil and sp.frac < P.keep_low) then
                    score = score + P.inbound
                    ny.inbound_skips = (ny.inbound_skips or 0) + 1
                end
                -- the one already pressed keeps its press unless another is
                -- clearly worth more (no target flapping, ny30f t328-335)
                if cur ~= nil and cur.slot == n.slot then score = score - 12 end
                cands[#cands + 1] = { score = score, n = n, d = d, style = n.style, key = key, stack = n.stack }
                if R ~= nil and n.style == R.colour then own_ok = true end
            end
        end
        -- raid seam33 play_tob_nylocas_normal_green: OWN COLOUR FIRST, as a
        -- rule, not a weight.  "Each player should be assigned a style of
        -- Nylocas to kill" (W :706) and "if your assigned nylocas are not
        -- currently near or in the room, switch weapons ... until the assigned
        -- nylocas return" (W :746): another seat's colour is taken only while
        -- none of the raider's own can be pressed, except an aggro ("These
        -- aggro's should be prioritised first", W :733).  The seam32 weight
        -- (12 points, four tiles of running) left the meleer bowing greens 44
        -- times and whipping greys 34 (s33ny1 pid 2), and 38 of 70 greys and
        -- 31 of 72 blues chewed until they popped.
        -- raid seam33: "NOT CURRENTLY NEAR OR IN THE ROOM" (W :746).  The
        -- meleer's greys cannot be hit in their tunnel ("cannot melee them
        -- until they reach said platform", E :160) but they are near: the
        -- meleer does not take another seat's colour while one of its own
        -- walks in, it goes to meet it where it leaves the tunnel ("Claw the
        -- wave 6 east big as soon as it enters the room", trio guide, melee
        -- waves 6-9).  ny.wait_for is the nearest such grey (HOME below).
        ny.wait_for = nil
        if R ~= nil and P.own_wait and R.colour == "melee" and not own_ok then
            local bd = nil
            for _, n in ipairs(v.nylos) do
                if n.style == "melee" and not ny.nulled[n.slot] and (ny.doomed[n.slot] == nil or ny.doomed[n.slot] < v.tick) then
                    local lx, lz = n.x - O.x, n.z - O.z
                    local ea = n.big and P.explode_age_big or P.explode_age
                    if (lx < P.floor[1] or lx > P.floor[3] or lz < P.floor[2]) and n.age < ea - 12 then
                        local d = dist(me.x, me.z, n.x, n.z, n.size)
                        if bd == nil or d < bd then bd, ny.wait_for = d, n end
                    end
                end
            end
        end
        for _, c in ipairs(cands) do
            local cleanup = ny.cleanup
            if R ~= nil and (own_ok or ny.wait_for ~= nil) and P.own_first and not cleanup and c.style ~= R.colour and not c.n.fighting and not c.n.helps then c.score = c.score + 100 end
            consider(c)
        end
        -- (an only-other-colour pick while waiting is no pick: wait)
        if best ~= nil and ny.wait_for ~= nil and best.score >= 50 then best = nil end
        -- THE FREEZE: "Ice barrage/burst any clumps of Nylocas you will not be
        -- dealing with. Frozen nylocas cannot attack the pillars until
        -- unfrozen ... all colours can be frozen" (E :164).  A clump of three
        -- or more on a support that is being eaten, centred on a blue when one
        -- is in it (the blues die to it; the others are nulled for the raider:
        -- "they will only be removed from the arena when they explode", E :164).
        for _, n in ipairs(v.nylos) do
            local sp = n.support
            local d = dist(me.x, me.z, n.x, n.z, n.size)
            if (R == nil or R.freeze) and sp ~= nil and sp.frac < 0.7 and not (sp == lowest and sp.frac < 0.15 and alive_supports > 1)
                and d <= ny.reach.magic and (ny.frozen[n.slot] or -1) < v.tick
                and n.settled then
                local clump, blues = {}, 0
                for _, o in ipairs(v.nylos) do
                    if (ny.frozen[o.slot] or -1) < v.tick and (ny.doomed[o.slot] == nil or ny.doomed[o.slot] < v.tick)
                        and o.x <= n.x + 1 and o.x + o.size - 1 >= n.x - 1 and o.z <= n.z + 1 and o.z + o.size - 1 >= n.z - 1 then
                        clump[#clump + 1] = o
                        if o.style == "magic" then blues = blues + 1 end
                    end
                end
                if #clump >= 3 and (n.style == "magic" or blues == 0) then
                    consider({ score = -18 - 3 * #clump - (1 - sp.frac) * 10 + (n.style ~= "magic" and 2 or 0), n = n, d = d,
                        style = "magic", spell = "ice_burst", clump = clump })
                end
            end
        end
        -- raid seam33: THE MAGE'S BURST.  "Magers and rangers should
        -- prioritise killing clumps of nylocas with barrage" (W :729) and the
        -- trio mage's "Barrage the 11 east doubles ... a value clump" (trio
        -- guide, mage waves 10-12 and 21-23): with the Ayak worn, a clump of
        -- P.burst_clump or more pressable blues in one 3x3 and nothing else in
        -- it (another colour under the splash is nulled for the mage) is one
        -- Ice Burst (Ancient Magicks, runes in the backpack: no staff needed)
        -- instead of that many Ayak swings.
        if R ~= nil and P.burst_clump ~= nil and R.colour == "magic" and ny.loadout.magic.powered then
            for _, n in ipairs(v.nylos) do
                local d = dist(me.x, me.z, n.x, n.z, n.size)
                if n.style == "magic" and d <= P.reach.magic and not ny.nulled[n.slot]
                    and (ny.doomed[n.slot] == nil or ny.doomed[n.slot] < v.tick)
                    and n.settled then
                    local clump, pure = {}, true
                    for _, o in ipairs(v.nylos) do
                        if o.x <= n.x + 1 and o.x + o.size - 1 >= n.x - 1 and o.z <= n.z + 1 and o.z + o.size - 1 >= n.z - 1 then
                            if o.style ~= "magic" then pure = false
                            elseif not ny.nulled[o.slot] and (ny.doomed[o.slot] == nil or ny.doomed[o.slot] < v.tick) then clump[#clump + 1] = o end
                        end
                    end
                    if pure and #clump >= P.burst_clump then
                        consider({ score = -30 - 6 * #clump, n = n, d = d, style = "magic", spell = "ice_burst", clump = clump, burst = true })
                    end
                end
            end
        end
        -- raid seam51 play_tob_nylocas_whole: THE RANGER'S CHINS.  Blert's
        -- trio rangers throw black chinchompas at the waves that stall a room
        -- (range|wave10 CHIN_BLACK 23 of 27 rooms, 2 a room; wave 21 21 of 27;
        -- wave 31 18 of 27), and the kills per attack say why: the reference's
        -- ~0.85-1 against this plan's 0.6 (seam50 dup.py).  One throw at a
        -- green lands on every copy in the 3x3 round it (player_ranged.rs2
        -- ~player_chinchompa_splash, radius 1, one accuracy roll); only the
        -- greens take it, the rest null the ranger (DMG :272).  The clumps are
        -- the chewers on a support and a lane's wave walking in together.
        if R ~= nil and P.chin ~= nil and ny.loadout.ranged_chin ~= nil then
            if ny.chin_have == nil then
                local cr, cn = QD.inv.count(ny.loadout.ranged_chin.item)
                ny.chin_have = (cr == "ok") and cn or tostring(cr)
            end
            for _, n in ipairs(v.nylos) do
                local d = dist(me.x, me.z, n.x, n.z, n.size)
                if n.style == "ranged" and d <= P.chin.reach and not ny.nulled[n.slot]
                    and (ny.doomed[n.slot] == nil or ny.doomed[n.slot] < v.tick)
                    and (ny.blocked == nil or (ny.blocked[n.slot] or -1) < v.tick)
                    and n.settled then
                    local clump, worth, others = {}, 0, 0
                    for _, o in ipairs(v.nylos) do
                        if o.x <= n.x + 1 and o.x + o.size - 1 >= n.x - 1 and o.z <= n.z + 1 and o.z + o.size - 1 >= n.z - 1
                            and not ny.nulled[o.slot] then
                            if o.style == "ranged" and o.settled then
                                if ny.doomed[o.slot] == nil or ny.doomed[o.slot] < v.tick then
                                    clump[#clump + 1] = o
                                    worth = worth + (o.big and 0.5 or 1)
                                end
                            else
                                others = others + 1
                                clump[#clump + 1] = o
                            end
                        end
                    end
                    if ny.chin_best == nil or worth > ny.chin_best then ny.chin_best = worth end
                    if worth >= P.chin.clump then
                        consider({ score = -30 - 8 * worth + P.chin.other * others + ((ny.worn ~= "ranged_chin") and 10 or 0),
                            n = n, d = d, style = "ranged", key = "ranged_chin", clump = clump, chin = true })
                    end
                end
            end
        end
        if best ~= nil then
            local n = best.n
            pick = { slot = n.slot, style = best.style, symbol = n.symbol, vas = false, d = best.d, x = n.x, z = n.z, big = n.big,
                spell = best.spell, clump = best.clump, key = best.key or best.style, chin = best.chin, stack = best.stack }
        end
    end

    -- raid seam47 play_tob_nylocas_no_nulling: OUT FROM UNDER HER.  She drops
    -- in on the middle of the room, where the seats' homes are; a raider left
    -- standing inside her footprint never frames her (s47b: the mage and the
    -- ranger pressed her 325 times `not_visible` from 30,23 for 349 ticks
    -- while only the meleer swung).  Blert's trios stand just outside her,
    -- mostly north of her middle (reference/nylocas_normal_3.json positions,
    -- offsets from her SW tile: mage|boss 2,4 32% 1,4 16% 3,4 11%; range|boss
    -- 2,4 41%; melee|boss 1,4 28% 2,4 18%): a raider under her walks to the
    -- nearest of those that is floor.
    if vas ~= nil and vas.size ~= nil and intent.walk == nil and dist(me.x, me.z, vas.x, vas.z, vas.size) == 0 then
        local best, bd = nil, nil
        for _, o in ipairs(P.boss_stands) do
            local x, z = vas.x + o[1], vas.z + o[2]
            local dd = math.max(math.abs(x - me.x), math.abs(z - me.z))
            if floor_ok(x, z) and (bd == nil or dd < bd) then best, bd = { x = x, z = z }, dd end
        end
        if best ~= nil then
            intent.walk = best
            ny.unders = (ny.unders or 0) + 1
        end
    end

    -- raid seam49: NOT HER NEAREST.  She swings at the nearest raider
    -- (content tob_nylocas_boss.rs2 ~tob_vasilias_act, strict nearest, the
    -- first found on a tie).  This plan's mage stood nearest in 23 of her 27
    -- swings (svbplaynyloc) and took 18-22 of her 24-26 hits a room, where
    -- Blert's recorders take about a third each (boss.hit_on_recorder
    -- n 53+76+62 over 27 rooms, ~7 a room of her 19 attacks).  On her magic
    -- and ranged forms the mage (Ayak reach 6, bow 10) stands two tiles off.
    if R ~= nil and P.mage_back and R.name == "mage" and vas ~= nil and vas.size ~= nil and intent.walk == nil
        and vas.form ~= "melee" and vas.form ~= "spawning" and dist(me.x, me.z, vas.x, vas.z, vas.size) == 1 then
        local best, bd = nil, nil
        for dx = -2, 2 do
            for dz = -2, 2 do
                local x, z = me.x + dx, me.z + dz
                if floor_ok(x, z) and not unsafe(x, z, 0) and dist(x, z, vas.x, vas.z, vas.size) == 2 then
                    local dd = math.max(math.abs(dx), math.abs(dz))
                    if bd == nil or dd < bd then best, bd = { x = x, z = z }, dd end
                end
            end
        end
        if best ~= nil then
            intent.walk = best
            ny.target = nil
            ny.backs = (ny.backs or 0) + 1
        end
    end

    -- PRESS?  Melee and the bow swing on by themselves once pressed (wiki
    -- Attack speed); a spell is one cast a click.  A new copy is pressed as soon
    -- as the old one is doomed, so the next swing is queued on the cooldown.
    local press = false
    if pick ~= nil and intent.walk == nil then
        local speed = ny.loadout[pick.key].speed
        if cur == nil or cur.slot ~= pick.slot or cur.style ~= pick.style or cur.key ~= pick.key then
            press = true
        elseif pick.style == "magic" and not ny.loadout.magic.powered then
            press = cur.swung ~= nil and cur.swung >= cur.pressed and v.tick >= cur.swung + speed - 2
            -- raid seam32: a cast that never showed (the press answered
            -- `timeout` and no cast animation followed) is pressed again, as a
            -- swing is: the trio's mage stood 13-18 ticks beside a live blue
            -- holding a dead press (svaplaynyloc... _play_nylocas t113-130)
            if not press and (cur.swung == nil or cur.swung < cur.pressed) and v.tick - cur.pressed > speed + 2 then press = true end
        elseif v.tick - math.max(cur.pressed, cur.swung or -1000) > speed + 3 then
            press = true
        end
        -- raid seam47: a special is its own press (the orb arms the NEXT
        -- attack), so a fresh one is pressed even on the copy already engaged
        if pick.spec then press = true end
        -- HER TURN: the turn stops the attack, and a hit of the old colour is
        -- reflected and heals her (W :754; NB :176; DMG :278).  The colour is
        -- judged when the swing is MADE, not when it lands ("If player makes an
        -- attack just before the nylocas changes forms, they will still take
        -- damage from it", W :733; s32ny2: every npc_heal row on her sat on a
        -- tick a raider swung, t562 the whip, t572/t632 the bow, none on a
        -- landing).  So no swing of this colour may fall on the turn tick or
        -- after it: a press whose first swing would come within `turn_margin`
        -- of the predicted turn is not sent, and a weapon swinging on its own
        -- whose next swing falls there is stopped by a step the tick before
        -- (the library's walk clears the engagement; raid seam32 -- the seam30
        -- rule held only bows and spells, by their landing tick, and Normal's
        -- 10-tick window put a whip swing on the turn: s32ny2 t562 HEAL4).
        if pick.vas and ny.next_turn ~= nil then
            local stop_at = ny.next_turn - P.turn_margin
            local first = math.max(v.tick + 1, st.last_swing + speed)
            if press and first >= stop_at then
                press = false
                ny.holds = ny.holds + 1
            end
            -- (a powered staff swings on by itself: raid seam33)
            if not press and cur ~= nil and cur.slot == pick.slot and (pick.style ~= "magic" or ny.loadout.magic.powered) then
                local nxt = math.max(v.tick + 1, (cur.swung or cur.pressed) + speed)
                if nxt >= stop_at and v.tick + 1 >= nxt - 1 then
                    local sx = me.x + 1
                    if not floor_ok(sx, me.z) then sx = me.x - 1 end
                    intent.walk = { x = sx, z = me.z }
                    ny.target = nil
                    ny.turn_steps = (ny.turn_steps or 0) + 1
                end
            end
        end
    end
    -- raid seam33 play_tob_nylocas_normal_green: THE SWAP, TIMED.  A swap
    -- while the old weapon is engaged on a copy that still stands swings the
    -- NEW weapon at the OLD copy until the next press lands (seam32,
    -- svcplaynyloc p2 t163-t167), and a wrong-style hit nulls the raider on
    -- it for good ("If the nylocas is attacked with a wrong style, the player
    -- that attacked them can no longer damage them", W :731-733).  The attack
    -- cooldown is known (the raider's own last swing and the worn weapon's
    -- speed), so the swap and its press go out only when the old weapon's
    -- next swing is two or more ticks off -- the press lands first -- and
    -- otherwise wait a tick: right after that swing the window is open again
    -- (and the copy is usually dead).  s33nyseedoneA: the meleer's bow swung
    -- at greys 4 times, the ranger's whip at greens, 11 of 92 matched swings.
    if press and P.swap_timed and R ~= nil and ny.worn ~= pick.key and cur ~= nil and not cur.vas and cur_row ~= nil
        and st.engaged and ny.loadout[ny.worn] ~= nil then
        local next_old = math.max(st.last_swing, cur.swung or -1000) + ny.loadout[ny.worn].speed
        if next_old - v.tick <= 1 and next_old >= v.tick then
            press = false
            ny.swap_holds = (ny.swap_holds or 0) + 1
        end
    end
    local early = nil
    if pray_style ~= nil and v.prayer > 0 and v.lit[P.prayer_of[pray_style]] ~= true then
        local name = P.prayer_of[pray_style]
        if ny.early_sent ~= nil and ny.early_sent.name == name and v.tick - ny.early_sent.tick <= 3 then
            -- pressed already and not read back yet (a prayer reads lit a tick
            -- or two after its press): a second press is a toggle, it would put
            -- it OUT again; treat it as lit until the read catches up
            for _, o in pairs(P.prayer_of) do v.lit[o] = (o == name) end
            intent.want = { [name] = true }
        else
            early = name
            ny.early_sent = { name = name, tick = v.tick }
        end
    end
    -- raid seam47 play_tob_nylocas_no_nulling: THE PRESS, as a function, so a
    -- swap sends it INSIDE its own together block, right after the equip.
    -- The block waits for the equip to show (QD.TOGETHER_CONFIRM_TICKS), and
    -- the press used to go out only after that wait: for the tick or two
    -- between, the server held the NEW weapon on the OLD engagement and swung
    -- it at the old copy -- a wrong style, which nulls the raider on that copy
    -- for life ("If the nylocas is attacked with a wrong style, the player
    -- that attacked them can no longer damage them", W :724; DMG :272).  ny40j
    -- p2: t153 the whip pressed grey 1096, t154 the pipe worn with the target
    -- still 1096 and swung (hit 0 at t157), the green's press read only at
    -- t155, then the whip hit 1096 for 0 four times; 46 of 305 wave swings
    -- went that way (s47 ny_null.py, the swing's own-tick raider row).  A
    -- person clicks the weapon and then the copy, both inside one tick: the
    -- server applies the two in that order before the player's turn, so the
    -- next swing is the new weapon at the new copy.
    local function press_now()
        local r, d
        local also = {}
        -- raid seam47: THE SPECIAL is armed from the orb right before its
        -- press (raid_play.lua intent.spec: "Armed from the minimap orb, never
        -- the combat tab's bar")
        if pick.spec then
            local wr, wid = QD.ui.widget("orbs:specbutton")
            local ir = "no widget"
            if wr == "ok" then ir = QD.ui.invoke(wid, 1) end
            ny.spec.armed = v.tick
            ny.spec.energy0 = pick.energy
            ny.spec.arms = ny.spec.arms + 1
            ny.spec.arm_result = tostring(ir)
            st.inputs[v.tick] = (st.inputs[v.tick] or 0) + 1
        end
        -- raid seam33: a powered staff is pressed like a weapon (its
        -- built-in spell, wiki Powered staff :8); only a spellbook cast
        -- goes through the cast path
        if pick.style == "magic" and (not ny.loadout.magic.powered or pick.spell ~= nil) then
            -- Ice Burst only on a clump that is ALL blue: "freezing non-magic
            -- Nylocas means you will no longer be able to do damage to them"
            -- (E :164); a lone blue gets Ice Rush
            local spell, pure = "ice_rush", true
            if pick.vas then
                -- HER magic form: Ice Burst, the same five ticks as Ice
                -- Rush and a max of 22 against 18 (wiki Ice burst, Ice
                -- rush); "the boss is immune to damage of the wrong combat
                -- style" (W :756) and there is nothing else on the floor,
                -- so its area freezes nothing the plan still needs.
                spell = "ice_burst"
            elseif pick.spell ~= nil then
                spell = pick.spell
                ny.freezes = (ny.freezes or 0) + 1
                for _, o in ipairs(pick.clump) do
                    ny.frozen[o.slot] = v.tick + 16
                    if o.style ~= "magic" then ny.nulled[o.slot] = true else also[#also + 1] = o.slot end
                end
            elseif not pick.vas then
                for _, n in ipairs(v.nylos) do
                    if n.slot ~= pick.slot and n.x <= pick.x + 1 and n.x + n.size - 1 >= pick.x - 1 and n.z <= pick.z + 1 and n.z + n.size - 1 >= pick.z - 1 then
                        if n.style == "magic" then also[#also + 1] = n.slot else pure = false end
                    end
                end
                -- raid seam32: only the seat that freezes bursts; another
                -- seat's burst splashes a copy that walks in under it and
                -- nulls that raider on it (s32ny4 p2 t191-192)
                if pure and #also > 0 and (R == nil or R.freeze) then spell = "ice_burst" ny.bursts = ny.bursts + 1 else also = {} end
            end
            r, d = QD.player.cast(spell, pick.symbol, 1, 2, { slot = pick.slot, quick = true })
            ny.casts = ny.casts + 1
        else
            -- raid seam51: a chin throw dooms the clump's greens with its
            -- primary, and nulls the ranger on the others under the blast;
            -- their "no effect" lines must not strike off the primary
            -- owner_nylocas: the scythe's stack is doomed with its primary
            if pick.stack ~= nil then
                for _, o in ipairs(pick.stack) do also[#also + 1] = o.slot end
                ny.stack_swings = (ny.stack_swings or 0) + 1
            end
            if pick.chin and pick.clump ~= nil then
                local nulls = 0
                for _, o in ipairs(pick.clump) do
                    if o.slot ~= pick.slot then
                        if o.style == "ranged" then also[#also + 1] = o.slot else ny.nulled[o.slot] = true nulls = nulls + 1 end
                    end
                end
                ny.chins = (ny.chins or 0) + 1
                if nulls > 0 then ny.chin_quiet = v.tick + 8 end
            end
            r, d = QD.player.attack(pick.symbol, 2, 1, { slot = pick.slot, quick = true })
        end
        st.inputs[v.tick] = (st.inputs[v.tick] or 0) + 1
        ny.presses = ny.presses + 1
        if R ~= nil and not pick.vas then
            if pick.style == R.colour then ny.own_presses = (ny.own_presses or 0) + 1 else ny.other_presses = (ny.other_presses or 0) + 1 end
        end
        ny.results[tostring(r)] = (ny.results[tostring(r)] or 0) + 1
        ny.last_press = tostring(r) .. ":" .. string.sub(tostring(d), 1, 160)
        if r ~= "ok" and r ~= "timeout" and #st.lines < 6 then
            st.lines[#st.lines + 1] = "t" .. v.tick .. " press " .. pick.style .. " " .. tostring(r) .. ": " .. string.sub(tostring(d), 1, 140)
        end
        ny.target = { slot = pick.slot, style = pick.style, key = pick.key, symbol = pick.symbol, vas = pick.vas, pressed = v.tick, d = pick.d, also = also,
            colour = (not pick.vas and pick.spell == nil) and pick.style or nil }
        if r ~= "ok" and r ~= "timeout" then
            -- the press did not land on this copy (another one stands on its
            -- pixels: `covered`, or it is off the frame): it is passed over for
            -- a few ticks and the next tick presses another (ny30g t146-167:
            -- one covered copy re-pressed every 7 ticks, nothing else hit)
            ny.blocked = ny.blocked or {}
            ny.blocked[pick.slot] = v.tick + 3
            ny.target = nil
            ny.misses = (ny.misses or 0) + 1
        end
        if pick.vas then ny.vas_presses[#ny.vas_presses + 1] = { tick = v.tick, style = pick.style, form = vas.form } end
        st.engaged = true
        st.engaged_tick = v.tick
        st.walk_target = nil
    end
    ny.block_pressed_now = false
    if press and ny.worn ~= pick.key then
        -- raid seam32: a swap while the old weapon is still swinging at a
        -- copy keeps swinging at it WITH THE NEW WEAPON until the next press
        -- lands: svcplaynyloc p2 pressed a grey with the whip (t163), put the
        -- blowpipe back on for a green (t164) and darted the grey at t165 and
        -- t167 (0, 0: nulled on it) before its press on the green went out at
        -- t167 -- the cause of the trio's 8-11 wrong-style wave swings a
        -- room.  P.swap_stop sends a one-tile step toward the new copy with
        -- the swap, which ends the engagement: measured 0-2 wrong-style swings
        -- a room, but 3 of 5 names green against 5 of 5 without it (the step
        -- costs the tempo the pillars live on), so it is OFF in the kept plan
        -- and the next pass's row (PLAY_NOTES "Nylocas, Normal trio").
        local stop = nil
        if P.swap_stop and R ~= nil and not pick.vas and ny.worn ~= "magic" and st.engaged then
            local best = nil
            for dx = -1, 1 do
                for dz = -1, 1 do
                    local x, z = me.x + dx, me.z + dz
                    if (dx ~= 0 or dz ~= 0) and floor_ok(x, z) and not unsafe(x, z, 0) then
                        local dd = math.max(math.abs(x - pick.x), math.abs(z - pick.z))
                        if best == nil or dd < best then best, stop = dd, { x = x, z = z } end
                    end
                end
            end
        end
        -- raid seam47: the press rides in the swap's block, after the equip
        -- (a spellbook cast keeps its own path: it is magic whatever is worn)
        local then_press = nil
        if P.swap_press_together and not (pick.style == "magic" and (not ny.loadout.magic.powered or pick.spell ~= nil)) then
            then_press = press_now
        end
        -- owner_nylocas: HER FORMS IN THEIR OWN GEAR (P.boss_gear): the set of
        -- her form, the pieces of it still in the backpack
        local gear = nil
        if pick.vas and R ~= nil and P.boss_gear ~= nil then
            local set = (pick.style == "melee") and P.boss_gear.melee or P.boss_gear.ranged[R.name]
            for _, item in ipairs(set or {}) do
                local cr, cn = QD.inv.count(item)
                if cr == "ok" and cn > 0 then
                    gear = gear or {}
                    gear[#gear + 1] = item
                end
            end
        end
        QD.raid._play_nylocas_wear(st, v, pick.key, early, stop, then_press, gear)
    elseif early ~= nil then
        QD.raid._play_nylocas_wear(st, v, nil, early)
    end
    -- sent already: the library's block must neither re-press it nor light the
    -- old one again (the want keeps only what is lit now)
    if early ~= nil then intent.want = { [early] = true } end
    -- raid seam40 play_tob_nylocas_follows_blert: THE OFFENSIVE PRAYER, AS
    -- BLERT'S TRIOS PRAY.  The recorder's lit prayers in the same 27 rooms
    -- (prayerSet, Blert Prayer bits 26 piety, 27 rigour, 28 augury; ny40/
    -- ny_prayers.py): in the waves the mage has Augury on 51 percent of its
    -- ticks, the meleer Piety 69, the ranger Rigour 79, and a protection
    -- prayer 2-9; on her, Piety 42-44 and Rigour 31-35 with the protection
    -- by her form.  So a party seat lights, beside whatever protection the
    -- rule above wants, its OWN colour's prayer through the waves (one
    -- prayer held: a first cut that followed every swap, ny40e, put 3-4
    -- inputs on 50-77 ticks a seat and cost the meleer 18 of 105 swings) and
    -- on her the prayer of her form.  Alone (Entry) nothing changes.
    if R ~= nil and P.attack_prayer_of ~= nil then
        local swing = R.colour
        if vas ~= nil then swing = (pick ~= nil and pick.style) or nil end
        local name = swing ~= nil and P.attack_prayer_of[swing] or nil
        if name ~= nil then intent.want[name] = true end
        ny.attack_prayer = name
    end
    if press and not ny.block_pressed_now and ny.worn == pick.key then
        press_now()
    end

    -- raid seam33: the meleer meets its walking grey at the tunnel's mouth
    -- (the floor tile nearest it; see "NOT CURRENTLY NEAR OR IN THE ROOM")
    if pick == nil and intent.walk == nil and vas == nil and ny.wait_for ~= nil then
        local n = ny.wait_for
        local x = math.max(O.x + P.floor[1], math.min(O.x + P.floor[3], n.x))
        local z = math.max(O.z + P.floor[2], math.min(O.z + P.floor[4], n.z))
        if floor_ok(x, z) and not unsafe(x, z, 0) and (x ~= me.x or z ~= me.z)
            and (st.walk_target == nil or st.walk_target.x ~= x or st.walk_target.z ~= z) then
            intent.walk = { x = x, z = z }
            ny.meets = (ny.meets or 0) + 1
        end
    end
    -- HOME: nothing to hit and off the centre -> back to it (E :162)
    if pick == nil and intent.walk == nil and vas == nil and ny.wait_for == nil then
        local far = math.max(math.abs(me.x - home.x), math.abs(me.z - home.z))
        if far > (ny.stand ~= nil and 1 or 2) and not unsafe(home.x, home.z, 0)
            and (st.walk_target == nil or st.walk_target.x ~= home.x or st.walk_target.z ~= home.z) then
            intent.walk = { x = home.x, z = home.z }
            ny.homes = ny.homes + 1
        end
    end
    if intent.walk ~= nil and intent.walk.x == me.x and intent.walk.z == me.z then intent.walk = nil end

    -- SUPPLIES: the most that can land before the next chance to eat
    local function threat(h)
        local swings = math.ceil(h / N.cadence)
        -- a floor of one big's max hit twice over: a copy turns aggro (NT
        -- nylocas.aggro_swap) or a split lands next to the raider between reads
        local total = 2 * N.big_hit
        for _, n in ipairs(v.nylos) do
            local d = dist(me.x, me.z, n.x, n.z, n.size)
            -- a copy of the prayed colour still counts while the switch is in
            -- flight (the prayer is read lit a tick after the press)
            local prayed = n.style == pray_style and v.lit[P.prayer_of[n.style]] == true
            if n.fighting and not prayed and d <= ((n.style == "melee") and 2 or 9) then
                total = total + (n.big and N.big_hit or N.small_hit) * swings
            end
            local ea = n.big and P.explode_age_big or P.explode_age
            if n.age + h >= ea and n.age <= ea and d <= P.blast + 1 then total = total + N.explode end
        end
        -- her attack every 4 ticks (NT vasilias_attackrate): off prayer up to
        -- 24, and through the prayer of a magic or ranged form still up to
        -- 17 (NB :370-383, ^tob_vasilias_prayed_max; only melee is fully
        -- protected, W :752)
        if vas ~= nil and vas.form ~= "spawning" then
            if vas.form ~= pray_style then
                total = total + N.boss_hit * math.ceil(h / 4)
            elseif vas.form ~= "melee" then
                total = total + N.prayed_hit * math.ceil(h / 4)
            end
        end
        -- the interlude: "During this brief interlude, the team should heal up
        -- and boost" (W :750): all 31 waves out, none left, she has not landed
        if vas == nil and #v.nylos == 0 and ny.waves >= 31 then
            total = math.max(total, v.hp_base - 12)
        end
        -- a support's bar under a quarter: its collapse ("30+ damage", E :171)
        -- can land on any tick from here
        for _, s in ipairs(v.supports) do
            if s.alive and s.frac < 0.25 then total = total + N.collapse end
        end
        return total
    end
    intent.eat, intent.drink, intent.need = QD.raid._play_nylocas_supplies(st, v, threat, vas == nil and #v.nylos == 0 and ny.waves >= 31)
    -- a brew drains the attack stats; a super restore puts them back ("undo
    -- the brews' stat drain", tob_nylocas.lua :35; maiden's plan does the same
    -- for its bow): a 2-hitpoint nylocas missed is a nylocas left biting
    if intent.drink == nil and v.tick - st.last_drink >= QD.RAID_PLAY_DRINK_DELAY and (ny.stat_check or 0) <= v.tick then
        ny.stat_check = v.tick + 5
        local _, rg = QD.skill.read("ranged")
        local _, mg = QD.skill.read("magic")
        if (rg ~= nil and rg.level < 88) or (mg ~= nil and mg.level < 88) then
            for _, name in ipairs(QD.RAID_PLAY_RESTORES) do
                local cr, cnt = QD.inv.count(name)
                if intent.drink == nil and cr == "ok" and cnt > 0 then intent.drink = name end
            end
            if intent.drink ~= nil then ny.stat_restores = (ny.stat_restores or 0) + 1 end
        end
    end
    if P.trace or (P.trace_seat ~= nil and st.party ~= nil and st.party > 1 and (st.role == P.trace_seat or P.trace_seat == 0)) then
        local nf, w = 0, ""
        if v.tick % 20 == 0 then
            local sp = {}
            for _, s in ipairs(v.supports) do sp[#sp + 1] = string.format("%d,%d:%.2f%s", s.x - O.x, s.z - O.z, s.frac, s.alive and "" or "x") end
            api_drive.report("nyplay supports t=" .. v.tick .. " " .. table.concat(sp, " ") .. " refusals " .. st.refusals .. " " .. table.concat(st.lines, " | "))
        end
        for _, n in ipairs(v.nylos) do
            if n.fighting then nf = nf + 1 end
            if #w < 200 then
                w = w .. string.format(" %s%s%s@%d,%d/a%d", n.fighting and "F" or "i", n.big and "B" or "", string.sub(n.style, 1, 2), n.x - O.x, n.z - O.z, n.age)
            end
        end
        local whys = {}
        for k, c in pairs(ny.why or {}) do whys[#whys + 1] = k .. ":" .. c end
        ny.why = nil
        api_drive.report(string.format("nyplay why=%s t=%d me=%d,%d hp=%d n=%d f=%d waves=%d worn=%s pray=%s pick=%s press=%s walk=%s eat=%s drink=%s |%s",
            table.concat(whys, ","), v.tick, me.x - O.x, me.z - O.z, v.hp, #v.nylos, nf, ny.waves, ny.worn, tostring(pray_style),
            pick and (pick.style .. "/" .. pick.slot .. "/d" .. pick.d) or "none", tostring(press),
            intent.walk and (intent.walk.x - O.x .. "," .. intent.walk.z - O.z) or "-", tostring(intent.eat), tostring(intent.drink), w .. (press and (" PRESS " .. tostring(ny.last_press)) or "")))
    end
    return intent
end
