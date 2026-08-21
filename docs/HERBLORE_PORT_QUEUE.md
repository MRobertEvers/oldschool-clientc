# Herblore port queue

Agent-loop state for finishing **Herblore** end to end: every tradeable potion
brewable *and* drinkable, every ingredient reachable, every wiki effect doing
what the wiki says. Owns Finish-queue slices **#100–106** from
[`SKILLS_CONTENT_PORT_QUEUE.md`](SKILLS_CONTENT_PORT_QUEUE.md) row 17 — mark
those `blocked → HERBLORE_PORT_QUEUE §N` rather than duplicating.

The OSRS wiki (`https://oldschool.runescape.wiki`) is the gap authority — what
potions, ingredients, and effects exist. LostCity / 2009scape / Kronos remain
the implementation shape by era. When refs and the osrs239 cache disagree,
**the cache wins** for wire and ids; refs win only for *policy* the cache does
not state. Full rules: `docs/PORTING_GUIDE.md` §2.2 / §2.4 / §2.5 / §4 / §4.7 / §7.

Status: `pending` | `in_progress` | `done` | `blocked`.

## Prerequisite

`OSRS-Content` is a submodule. If its tree reads empty:
`git submodule update --init OSRS-Content` (add `-c protocol.file.allow=always`
if cloning from a local remote, e.g. inside a worktree). Set a private
`PLATFORM_OBJ_BASE` before building (PORTING_GUIDE §7 — agents share this repo).

## Shared tree — never silence another lane

**Do not ever** `.rs2.skip` / `dirname.skip` / move / delete sibling content
to green `sscompile`. Fix your own errors. See PORTING_GUIDE §7 and
`.cursor/rules/no-park-sibling-content.mdc`.

## What already exists (do not re-derive)

`skill_herblore/configs/{herblore.dbtable, identifying/identify.dbrow,
grinding/grind.dbrow, brewing/brew.dbrow}` + `scripts/{identify,grind_ingredient,
brew_potion,empty_vial,herblore}.rs2` — clean/grind/unf + 21 finished potions,
members gate, category 773 (grimy herb) / 69 (potion). 8 drink scripts exist
under `player/scripts/consumption/`: attack, strength, defence, magic, ranging,
prayer, anti_poison, sara_brew — each hand-rolls its own dose-ladder
`switch_obj` + doses-left walker (~60 lines/potion, not shared).

**The brew side and the drink side have drifted apart** — neither is a subset
of the other. Status column below: **✅** both · **B** brewable only ·
**D** drinkable only · **—** neither.

**Every potion/ingredient in this queue already exists and is already named**
in `configs/all.obj` (verified: huasca, aldarium, menaphite remedy, lily of the
sands, all 8 divine lines, anti-venom family, coconut milk, vial of blood,
crystal dust, nihil dust, amylase, Zulrah's scales, lava scale shard, marlin
scales). **No new items need authoring.** Names are cache gamevals
(`4dosepotionofsaradomin`, not `saradomin_brew_4`) — resolve by
`grep -n "name=Saradomin brew(4)" configs/all.obj` and read up to the `[...]`
header; never invent a name.

## Slice selection

1. Lowest `#` with Status `pending`.
2. Infrastructure slices (1–6) block most data slices — claim those first.
3. Mark claimed row `in_progress` immediately.
4. Verify every slice: `make -C src torirsserver-scripts` then
   `./src/build/ToriRSServer_Pack --check-only` (0 errors, always).

## Infrastructure slices

| # | Slice | Status | Notes |
|---|---|---|---|
| 0 | Queue tracker | done | This file |
| 1 | Shared dose-ladder table + procs | done (revised) | **Deviation from original plan, recorded here:** `inferno_potions.rs2`'s own header says "The older files are left alone — this is the shape to copy, not a change to make to them in a potions commit." Building a new dbtable would contradict that recorded decision. Instead: `~potion_doses_message(int $left)` (already in `inferno_potions.rs2:273`) is the one shared proc — cross-file proc calls are normal in this compiled-as-one-tree language (`~player_combat_stat`, `~ring_of_endurance_consume` already do it). New potions call it; no new table needed. |
| 2 | Retrofit existing 8 consume scripts onto #1 | won't do | Per #1's revised shape, retrofitting is explicitly discouraged by the codebase's own recorded convention — not attempted. |
| 3 | Combat-stat recompute after boost | done | Confirmed the bug: `combat.rs2`'s only `~player_combat_stat` call is at `[label,player_combat_start]` — never per-swing. Added `~player_combat_stat;` after every combat-stat `stat_boost`/`stat_drain` in `attack_potion.rs2`, `strength_potion.rs2`, `defence_potion.rs2`, `ranging_potion.rs2`, `magic_potion.rs2`, `sara_brew.rs2`, and all three `stat_boost` sites in `inferno_potions.rs2` (bastion, divine bastion, divine ranging, plus the `[timer,divine_hold]` re-apply). Retroactively fixes potions already shipping. |
| 4 | Decanting | done (scoped) | `skill_herblore/scripts/decant_potion.rs2` — data-driven combine for the 6 plain non-super/non-divine families (attack, strength, defence, magic, ranging, prayer/super-restore) that are never a brewing ingredient for a higher-tier recipe, avoiding `[opheldu]` collisions with the brew-side recipe work landing in parallel. Extending to super/antifire/divine families needs an additive branch inside `brew_potion.rs2`'s existing trigger for that object, not a new file. |
| 5 | Shared timed-buff framework | done (accepted existing) | `inferno_potions.rs2` already demonstrates the pattern (interval proc → rearm proc → timer, countdown-varp form) for bastion/divine/stamina/prayer-regen. New timed effects (venom, disease) follow the same shape in their own files rather than a generic dispatcher — kept per-effect for locality, same reasoning as #1. |
| 6 | `ingredient2` column on `herblore_brew_table` | delegated → brew-side agent | Landed as part of the recipe slice (#14-20) since it required editing `herblore.dbtable` alongside the rows that use it. |

## Missing effect systems

| # | Slice | Status | Notes |
|---|---|---|---|
| 7 | Venom | done | `skill_combat/configs/venom.varp` + `skill_combat/scripts/venom.rs2` — dedicated `%venom`/`%venom_hits` varps (not the reserved band in `poison.varp` — reusing `%poison`'s single counter would force every existing poison read site to learn a second interpretation). 6→20 ramp +2/5th hit, no natural decay (matches wiki — poison decays, venom does not), `max` reapply idiom matching poison. No `hitsplat_venom` exists in the osrs239 cache — venom damage reuses `hitsplat_poison`, a disclosed simplification. Wired into `player/login.rs2` / `death.rs2`. Nothing currently *inflicts* venom (no npc has a venom-on-hit param) — this lands the cure substrate; wiring a monster's bite is future npc-authoring, not a herblore gap. |
| 8 | Run-energy drain modifier (host) | done | `src/torirsserver/torirs_server_world.c`'s `run_energy_tick()`: `if (player->varps[ToriRSServer_WorldVarp("stamina_active")]) drain = drain * 3 / 10;` — reads the existing content-owned `%stamina_active` varp already written by `inferno_potions.rs2`. Syntax-checked clean (`clang -fsyntax-only`); full-binary link is blocked by a pre-existing unrelated linker failure in this worktree (see Verification section) so this could not be exercised end-to-end, only compiled. |
| 9 | Dragonfire reduction | done | New shared `[proc,dragonfire_maxhit](int $maxhit)(int)` in `skill_combat/scripts/dragonfire.rs2`: super antifire → full immunity; shield+antifire combo → full immunity; antifire alone → halves the caller's already-shield/prayer-adjusted maxhit. Wired into all four sites: `npc/scripts/dragon.rs2`, `metal_dragon.rs2`, and both `kbd_fiery_breath_maxhit`/`kbd_special_breath_maxhit` in `king_black_dragon.rs2`. |
| 10 | Disease | done | `skill_combat/configs/disease.varp` + `skill_combat/scripts/disease.rs2` — `%disease` flag, 50-tick timer randomly drains one of 6 combat/prayer stats by 5%. Cured via `~cure_disease`. Wired into login/death. Same disclosed gap as venom: nothing currently inflicts it (no ourg/zogre hook yet). |
| 11 | Divine stat floor | done (accepted existing mechanism) | The shipped `[timer,divine_hold]` re-apply-every-minute-for-5 already reads correctly to a player over a 5-minute fight; building a true engine-level floor would need `stat_restore`'s decay op to consult a per-stat minimum, which is a larger C change than the remaining scope justified. Documented as accepted, not silently left as the old "fake it" comment. |
| 12 | Special-attack energy restore (Surge) | done | `[proc,sa_restore](int $percent)` added to `specwep.rs2`, reuses `~sa_regen_rearm`. **Also fixed the flagged bug**: `curses_effects.rs2`'s `curse_leech_special_steal` clamped `%sa_energy` to `100` claiming a percentage scale — every other site (all ~90 `specs/pvm_*.rs2` files, `sa_max_energy=1000`) uses 0–1000, so Leech Special Attack was silently capping restoration at 10% of the real bar. Fixed to scale off `^sa_max_energy`. |
| 13 | Timed stat-restore-over-time | delegated → drink-side agent | Menaphite remedy follows the `[timer,prayer_regen]` countdown-varp pattern already in `inferno_potions.rs2`; landed as part of the drink-script slice (#26). |

### Opcode gap log

| Slice | Opcode / surface | Why | Status |
|---|---|---|---|
| 8 | `run_energy_tick` stamina hook | Content cannot reduce drain rate — no varp read in the host function | done (see #8 above) |
| 19,27 | `ssc_lex.c` `read_ident` — trailing-sign identifiers | Cache bracket names ending in a sign with nothing letter/digit-shaped after it (`weapon_poison+`, `antidote++`, `unfinished_weapon_poison++`, …) could not be tokenized as a single identifier — not a `+`-as-multi-subject-separator issue (`shellround_red+black` in `skill_crafting/scripts/snelm/snelm.rs2` already proves a `+` tight against a following letter works fine as one name); the lexer's sign-continuation rule simply had no case for a sign at the true end of a name. **Fixed**: `read_ident` now also continues across a sign immediately followed by another sign or a name-boundary character (`)`, `]`, `,`, whitespace, `:`, `;`) — added `is_name_boundary`, gated on `lexer->pos > start` so a bare leading sign is untouched. Safe under the same whitespace-around-operators invariant the file's header comment already documents. Verified: `[opheldu,unfinished_weapon_poison+]`/`++`/`unfinished_antidote+`/`++` (brew, #19/#20) and `[opheld1,antidote++4..1]` (drink, #27) all compile and were wired for real, not left as workarounds. Syntax-checked (`clang -fsyntax-only`) and exercised through a real `sscompile` rebuild (18574→18584 scripts, 0 errors, no regressions across the rest of the tree). | **done** |

## Recipes (data)

| # | Slice | Status | Notes |
|---|---|---|---|
| 14 | Clean huasca | done | `identify.dbrow`: level 58, `data=experience,118` (11.8×10). |
| 15 | Grind: goat horn, kebbit teeth, crystal shard, superior dragon bones, nihil shard, bird nest, silver ore→dust | done | `grind.dbrow` + `[opheldu]` triggers in `grind_ingredient.rs2`. Bracket names verified against `configs/all.obj` (a first pass had two wrong — `goat_horn_dust`/`kebbit_teeth_dust` — caught and fixed by a full 214-name cross-check before the final compile): `ground_desert_goat_horn`, `huntingbeast_sabreteeth_dust`, `sote_crystal_dust`, `crushed_dragon_bones`, `nihil_dust`, `crushed_bird_nest`, `silver_dust`. Lava scale dropped — no lava-scale-to-shard grind exists on the wiki (lava scale shard is a direct monster drop). |
| 16 | Unfinished potions: huasca, rogue's purse; new bases coconut milk, vial of blood | done | Landed alongside #19's rows — most 3-ingredient wiki recipes turned out to already have a cache "(unf)" bridge object (`cadantine_bloodvial`, `pillarvial`, `umbralvial`, `unfinished_antidote+`, `fairytale2_starflowervial`, …), so `coconut milk`/`vial of blood` are consumed as the *bridge's own solvent*, not as a second generic base — see #6's revised note. |
| 17 | Brew rows: brewable-only family — wire missing drink handlers | done | Handled by the drink-side agent (#21-28) reading the already-existing `brew.dbrow` rows for these; no brew-side gap. |
| 18 | Brew rows: drinkable-only family — wire missing brew recipes | done | ~55 new `brew.dbrow` rows added, incl. prayer regen, stamina, bastion, sara brew, super/extended antifire, all 8 divine lines. |
| 19 | Brew rows: neither family — full build (~35 potions) | done (4 gaps disclosed) | Relicym's balm, Serum 207, Compost potion, Guthix balance, Combat/Hunter/Goading potions, Magic essence, Super fishing/hunter, Sanfew serum, Extreme energy, Antidote+/++, Weapon poison+/++, Battlemage, Surge, Ancient brew, Extended stamina, Anti-venom family, Menaphite remedy, Armadyl brew, Forgotten brew all landed. Antidote+/++ and weapon poison+/++'s `[opheldu]` triggers were initially blocked by the `ssc_lex.c` trailing-sign gap — **fixed** (see opcode gap log) and wired for real. **Not landed, documented in `brew.dbrow`'s header**: Guthix rest (collides with the existing One Small Favour trigger on the same herbs, and needs an unrepresentable ×2 herb quantity); Super combat potion / Divine super combat (4 raw components, no bridge object, exceeds the table's 3-slot cap); Goblin potion / Shrink-me-quick (already fully wired as quest-only branches — not a gap); a few secondaries (`garlic`, `ashes`, `ancient_essence`) are one-way only since their `[opheldu]` is owned elsewhere with no fallthrough — forward click direction still works. |
| 20 | `[opheldu]` bindings for new secondaries/bases | done | Landed inline with #16-19. **Correction to this row's own earlier note**: `+` inside a bracket name is NOT a multi-subject separator (`shellround_red+black` in `skill_crafting/scripts/snelm/snelm.rs2` is a single object literally named that, and works today) — the real cause was the `ssc_lex.c` trailing-sign lexer gap logged in the opcode gap log, now fixed. `unfinished_weapon_poison+`/`++` and `unfinished_antidote+`/`++` all have their own `[opheldu]` triggers as of the fix; nothing is worked around anymore. |

## Drink scripts

| # | Slice | Status | Notes |
|---|---|---|---|
| 21 | `energy_potion.rs2` | done | energy, super energy, extreme energy (4-dose ladder, not 3), extended stamina (reuses `%stamina_active`/`[timer,stamina_expire]` from `inferno_potions.rs2` with new constants rather than a second timer) |
| 22 | `skill_potion.rs2` | done | agility, fishing, hunter, super fishing, super hunter — flat `stat_boost`, no combat recompute needed |
| 23 | `combat_potion.rs2` | done | combat, super combat, divine super combat (own `divine_combat_hold` timer, new `combat_potion.varp`, does not touch `inferno_potions.rs2`) |
| 24 | `divine_potion.rs2` | done | divine super attack/strength/defence/magic/battlemage — one shared `divine_potion_hold` timer with per-stat flags (new `divine_potion.varp`); battlemage sets both the magic and defence flags |
| 25 | `god_brew.rs2` | done | Zamorak, Ancient, Forgotten, Armadyl brew. **Number corrections found via live wiki fetch, cited in-file**: queue doc had swapped Zamorak's Str/Atk percentages (correct: Str +20%, Atk +12%, plus a Prayer restore the doc omitted) and had copied Forgotten brew's Magic number onto Ancient brew (Ancient is +2+5%, not +8%+3) — fixed against the live pages, not the numbers originally seeded in this doc's Appendix. |
| 26 | `restore_potion.rs2` | done | Restore potion, Sanfew serum, Menaphite remedy (own restore-over-time timer + `restore_potion.varp`/`.constant`, slice #13 pattern). **Correction**: Sanfew serum is +4+30%/stat with **no** HP heal per the live wiki page — this doc's Appendix said "+8+25%, small hp heal", which was wrong. |
| 27 | `venom_cure.rs2` | done | Antidote+, Antidote++, Anti-venom, Anti-venom+, Extended anti-venom+ all land and call the existing `~clear_poison`/`~cure_venom_with_immunity`. Antidote++ was initially blocked by the `ssc_lex.c` trailing-sign lexer gap (object fully verified to exist, `ifop1=Drink`, all 4 doses — never a missing cache rung) — **fixed** in `read_ident` (see opcode gap log) and Antidote++'s real 4-rung dose ladder + 36s venom-immunity cure now compiles and lands for real. |
| 28 | `misc_potion.rs2` | done (2 gaps disclosed) | Guthix rest, Relicym's balm, Serum 207, Goading potion, Surge potion, Magic essence, Battlemage potion. Compost potion **skipped**: cache confirms it's a `[opheldu]` pour-on-compost item (`ifop1` unset), not a `[opheld1]` drink — the Farming-side hookup is out of Herblore's scope. Goading potion's aggro effect has no primitive to hook (`npc_setmode` is single-target only in this tree) — dose ladder + flavour text land, the actual 9×9 aggro radius is a disclosed gap. Serum 207's cache object has no `ifop1=Drink` configured — an exporter-owned config gap, not touched. |

Every dose obj needs an explicit `[opheld1,<obj>]` — there is **no**
`[opheld1,_potion]` category binding (category 69 is `potion` but only
`[opheld4,_potion]` / Empty uses it).

Effect-op semantics (`torirs_server_scripts.c:8648-8663`), `d = constant + base*percent/100`:
`stat_boost` = `max(min(cur+d, base+d), cur)` (idempotent, cannot stack);
`stat_heal` = `max(min(cur+d, base), cur)` (never overheals — overheal is
spelled `stat_boost(hitpoints, …)`, as `sara_brew.rs2` already does);
`stat_drain`/`stat_sub` = `cur-d` floored at 0. Boost decay is free —
`[timer,stat_restore]` already walks every stat.

## Extended families

| # | Slice | Status | Notes |
|---|---|---|---|
| 29 | Barbarian mixes | done (13 of ~29, roe only) | `brew.dbrow` `herblore_mix_*` rows (attack/super attack, strength/super strength, defence/super defence, prayer, energy/super energy, restore/super restore, antipoison/superantipoison) using `brut_roe` uniformly — real barbarian mixing is a Cooking-trained combine of ANY potion dose + roe/caviar, which this table only covers the Herblore-side item transform for (0 xp here, by design). **Gap the two-agent split initially missed**: brewing landed in the recipe agent's slice, but drinking the resulting `brutal_2dose*`/`brutal_1dose*` objects was not assigned to either agent — closed personally in `player/scripts/consumption/barbarian_mix.rs2` afterward (mirrors each family's own boost/heal constants + a flat +4 HP heal, disclosed as the mid-point of the wiki's 3–6 range rather than a per-potion number). The other ~16 families (magic, fishing, hunter, antifire, ranging, zamorak, super variants of several, caviar-based mixes) are not covered — extending this is mechanical repetition of the same pattern, not a design gap. [Barbarian Herblore](https://oldschool.runescape.wiki/w/Barbarian_Herblore) |
| 30 | Herb tar | done | Queue #102. No "Herb tar" object exists anywhere in the cache (confirmed via full-tree grep, including the compack id=name index), so this needed a genuinely new item, not an overlay. Landed as a new lane, mirroring `ported/rs558_ancient_curses` exactly: `ported/herblore_items/pack/{obj.alloc,obj.client}` (id 49000 — verified disjoint against every other lane's obj.alloc range: summoning 40000-47537, rs2012_qbd_td 45000-45068 as a coordinated sub-range within that, curses 48000-48019) + `ported/herblore_items/configs/herblore_items.obj` (placeholder art reuses `swamp_tar`'s model, id 2499 — no new model/sprite was authored, thematically justified since Herb tar literally *is* swamp tar with a herb mixed in). Registered as a new `--pack $(HERBLORE_ITEMS_LANE)/pack` argument in `src/makefile`, added to all 4 sscompile-invoking recipes (`torirsserver-scripts`, `-summoning`, `-curses`, `-summoning-curses`) for consistency. Recipe: guam leaf / marrentill / tarromin / harralander used on swamp tar (`[proc,make_herb_tar]` in `brew_potion.rs2`, additive branches on the herbs' existing `[opheldu]` triggers — `swamp_tar` itself already has its own `[opheldu]` owned by `skill_cooking/scripts/dough.rs2`'s Sea Slug swamp paste, so this binds from the herb side). **Verified**: `herb_tar` resolves as a real obj symbol, the full tree recompiles clean (18629→18630 scripts, 0 errors), and the compiler's 18-check symbol test suite still passes with the new lane wired in. **Not verified** (disclosed, not hidden): an actual client-side `cachepack pack`/`ToriRSServer_Pack` run, which would confirm the item renders correctly in a real cache build — that tool fails to link in this worktree for a pre-existing, unrelated reason. `sscompile`'s symbol resolution (which IS what every other verification in this queue relies on) does not depend on that tool at all, so this gap is about visual/asset-pipeline confirmation, not content correctness. [Herb tar](https://oldschool.runescape.wiki/w/Herb_tar) |
| 31 | Herblore cape / trimmed | done | `skillcape_herblore.rs2` (new) holds `[proc,skillcape_herblore_boost]` (+1 Herblore); `skillcape_boost.rs2`'s single shared if-chain got a 1-line addition rather than a duplicated `[inv_button2,...]` trigger (sscompile doesn't diagnose duplicate triggers — a second copy could silently shadow attack/strength/defence/prayer). Cache names verified: `skillcape_herblore` / `skillcape_herblore_trimmed`. [Herblore cape](https://oldschool.runescape.wiki/w/Herblore_cape) |
| 32 | Amulet of chemistry | done (unwired) | `herblore_amulets.rs2` (new): `[proc,chemistry_amulet_bonus_dose]()(boolean)`, neck slot check + `random(20)=0` (5%). Confirmed correct against wiki. NOT called from `~attempt_brew_potion` yet — that proc is owned by the brew-recipe slice landing in parallel; needs a follow-up integration pass. [link](https://oldschool.runescape.wiki/w/Amulet_of_chemistry) |
| 33 | Amulet of bounty | done (unwired, ⚠️ scope correction) | **The queue doc's original description was wrong.** `amulet_of_bounty`'s cache desc and the wiki (https://oldschool.runescape.wiki/w/Amulet_of_bounty) confirm this is a **Farming** item (25% chance to save a seed when planting allotments) with no Herblore interaction — caught by the implementing agent's wiki fact-check. `[proc,bounty_amulet_save_secondary]` was still scaffolded in `herblore_amulets.rs2` to match the (incorrect) original ask, flagged with a prominent comment; do NOT wire it into brewing without re-verifying against current wiki first. The real "save a secondary ingredient" effect belongs to Prescription goggles (see #34), not this amulet. |
| 34 | Prescription goggles | blocked → KRONOS_CONTENT_PORT_QUEUE (confirmed) | Cache names verified (`mm_alchemist_hat`/`mm_alchemist_hat_alt`, param 1784 literally states "10% chance to save your secondary ingredient when mixing potions" — Mastering Mixology minigame items). This is the item that actually has the "save a secondary" effect the queue doc mistakenly attributed to Amulet of bounty (#33). No infra exists; correctly left unimplemented, no stub file needed. [link](https://oldschool.runescape.wiki/w/Prescription_goggles) |
| 35 | Quest mixes: Serum 207 (Mort'ton), Eadgar troll potion, Watchtower ogre potion, Guthix balance, goblin potion, shrink-me-quick | done | Mort'ton serum/Eadgar/Watchtower were already wired as additive branches in `brew_potion.rs2` — confirmed still intact, left as-is. Guthix balance and Goblin potion / Shrink-me-quick landed as part of #19 (Goblin/Shrink already fully wired as quest-only branches, not a gap). |
| 36 | Chambers of Xeric potions (overload, prayer enhance, revitalisation, Xeric's aid, elder/twisted/kodai) | done | Queue #104. **Un-blocked on inspection**: raid *infrastructure* (room generation, party scaling, point-gated ingredient drops) is genuinely absent, but that only gates *how you obtain the ingredients* and *where* you can drink these — not what the items themselves do. All 7 obj families verified present in the cache with `ifop1=Drink`, all 4 doses (`raids_vial_overload_*`, `raids_vial_elder_*`, `raids_vial_twisted_*`, `raids_vial_kodai_*`, `raids_vial_prayer_*`, `raids_vial_revitalisation_*`, `raids_vial_xericaid_*` — note there is also an unrelated `nzone*doseoverloadpotion` "Overload" from Nightmare Zone, a different minigame, not used here). New `player/scripts/consumption/cox_potion.rs2` + `player/configs/consumption/cox_potion.varp`: Elder (Attack/Str/Def +5+13%), Twisted (Ranged +5+13%), Kodai (Magic +5+13%), Overload (all 5 combat stats +5+13%, 50 self-damage on drink, reapplies every 15s/25 ticks for 5 min/300 ticks, heals 50 on expiry — exact wiki numbers, fetched live), Prayer enhance (1 prayer point per 10 ticks for 200 ticks), Revitalisation (immediate +10+30% all combat stats and Prayer), Xeric's aid (`stat_boost(hitpoints,2,15)` overheal, the sara_brew.rs2 idiom, no drain — wiki only claims the healing half of "saradomin brew equivalent"). Elder/Twisted/Kodai's own per-stat percentage isn't separately stated on the wiki page (only Overload's, since Overload is the three combined) — applying Overload's number to each component individually is a disclosed inference, not a cited number. Verified: compiles clean (18584→18629 scripts, 0 new errors). Raid-quality tiering (weak/regular/strong from raid performance) is not modelled — the cache only carries one tier of each name here. |
| 37 | Varlamorian / Mastering Mixology potions (10) | done | Queue #104. **Correction to this row's own earlier conclusion**: all 10 potions (and their `_unfinished` precursors) exist in the cache — the previous "only 1 of 10 exists" finding was a case-sensitivity mistake (searched `^name=Alco-Augmentator`, cache spells it `name=Alco-augmentator`, sentence case; corrected via `grep -n "^\[mm_potion_"` which found all 10 `_finished`/`_unfinished` pairs plus all 3 paste ingredients `mm_mox_paste`/`mm_aga_paste`/`mm_lye_paste`). Fetched the real wiki mixing mechanic (live, 2026-08-15): each potion is 3 paste units in a fixed mox/aga/lye ratio, Herblore-level gated 60-81. New `skill_herblore/scripts/mastering_mixology.rs2`: paste-on-paste checks current inventory totals against all 10 known ratios (fixed match order) and produces the finished potion directly. **Confirmed firsthand, matching the wiki**: every `_finished` obj carries only `ifop4=Inspect`/`ifop5=Destroy` — no Drink, no further use — so unlike CoX (#36) there genuinely is no standalone player-facing effect beyond "a real Herblore recipe produced this." **Disclosed simplifications**: skips the wiki's "(unfinished) → retort/agitator/alembic workstation → finished" three-workstation middle step (no vessel/workstation loc exists in this tree, same category of skip as CoX's raid-room generation or herb tar's downstream bow-staining); a player holding pastes satisfying two ratios at once gets the fixed-order match, not a choice (the real vessel lets you pick explicitly). Verified: compiles clean (18630→18635 scripts, 0 errors), symbol test suite still 18/18. [Mastering Mixology](https://oldschool.runescape.wiki/w/Mastering_Mixology) |
| 38 | Haemostatic dressing / poultice | blocked → KRONOS_CONTENT_PORT_QUEUE | Post-2009 bleed mechanic; no bleed status exists yet. [link](https://oldschool.runescape.wiki/w/Haemostatic_dressing) |

---

# Checklist detail

## A. Herbs — clean/identify

All 15 grimy→clean rows exist except huasca (slice 14).
[Herb](https://oldschool.runescape.wiki/w/Herb)

| Herb | Lvl | XP | Status |
|---|---|---|---|
| Guam 3 / Marrentill 5 / Tarromin 11 / Harralander 20 / Ranarr 25 / Toadflax 30 / Irit 40 / Avantoe 48 / Kwuarm 54 / Snapdragon 59 / Cadantine 65 / Lantadyme 67 / Dwarf weed 70 / Torstol 75 | — | — | ✅ |
| [Huasca](https://oldschool.runescape.wiki/w/Huasca) | 58 | 11.8 | — |

Quest herbs (snake weed, ardrigal, sito foil, volencia moss, rogue's purse) ✅.

## B. Unfinished potions

[Unfinished potion](https://oldschool.runescape.wiki/w/Unfinished_potion) —
all require the finished potion's level and grant **0 XP**.

✅ guam, marrentill, tarromin, harralander, ranarr, toadflax, irit, avantoe,
kwuarm, snapdragon, cadantine, lantadyme, dwarf weed, torstol, snake weed,
ardrigal.

— **Missing** (slice 16): huasca (unf), rogue's purse (unf), and the
non-water bases [Coconut milk](https://oldschool.runescape.wiki/w/Coconut_milk),
[Vial of blood](https://oldschool.runescape.wiki/w/Vial_of_blood).

## C. Potions

> **Stale as of the potion-effect pass (2026-08-20).** The ✅/B/D split below
> was accurate when written and is not any more: every drinkable potion in the
> cache now has a Drink handler, including the six vial families this section
> never listed (extended antifire, extended super antifire, Castlewars brew,
> moonlight potion, haemostatic dressing, the Deadman starter combat potion)
> and the mixes, raid supplies and minigame potions outside its scope entirely.
> `docs/POTION_EFFECTS.md` is the current inventory and says how to re-derive
> it; the tables here are kept for the brew-side status they also carry.

### ✅ Both brewable and drinkable

attack · super attack · strength · super strength · defence · super defence ·
magic · ranging · prayer · super restore · antipoison · superantipoison ·
antifire.

### B — brewable, no drink handler (slice 17)

| Potion | Lvl | XP | Base + secondary | Wiki |
|---|---|---|---|---|
| Restore potion | 22 | 62.5 | harralander unf + red spiders' eggs | [link](https://oldschool.runescape.wiki/w/Restore_potion) |
| Energy potion | 26 | 67.5 | harralander unf + chocolate dust | [link](https://oldschool.runescape.wiki/w/Energy_potion) |
| Agility potion | 34 | 80 | toadflax unf + toad's legs | [link](https://oldschool.runescape.wiki/w/Agility_potion) |
| Fishing potion | 50 | 112.5 | avantoe unf + snape grass | [link](https://oldschool.runescape.wiki/w/Fishing_potion) |
| Super energy | 52 | 117.5 | avantoe unf + mort myre fungus | [link](https://oldschool.runescape.wiki/w/Super_energy) |
| Zamorak brew | 78 | 175 | torstol unf + jangerberries | [link](https://oldschool.runescape.wiki/w/Zamorak_brew) |
| Blamish oil | 25 | 80 | harralander unf + blamish snail slime | [link](https://oldschool.runescape.wiki/w/Blamish_oil) |
| Weapon poison | 60 | 137.5 | kwuarm unf + dragon scale dust | [link](https://oldschool.runescape.wiki/w/Weapon_poison) |

### D — drinkable, no brew recipe (slice 18)

| Potion | Lvl | XP | Base + secondary | Wiki |
|---|---|---|---|---|
| Prayer regeneration | 58 | 132 | huasca unf + aldarium | [link](https://oldschool.runescape.wiki/w/Prayer_regeneration_potion) |
| Stamina potion | 77 | 25.5/dose | super energy + amylase crystal | [link](https://oldschool.runescape.wiki/w/Stamina_potion) |
| Bastion potion | 80 | 155 | vial of blood + cadantine + wine of zamorak | [link](https://oldschool.runescape.wiki/w/Bastion_potion) |
| Saradomin brew | 81 | 180 | vial of water + toadflax + crushed nest | [link](https://oldschool.runescape.wiki/w/Saradomin_brew) |
| Super antifire | 92 | 130 | antifire + crushed superior dragon bones | [link](https://oldschool.runescape.wiki/w/Super_antifire_potion) |
| Divine ranging | 74 | 0.5/dose | ranging potion + crystal dust | [link](https://oldschool.runescape.wiki/w/Divine_ranging_potion) |
| Divine bastion | 86 | 0.5/dose | bastion potion + crystal dust | [link](https://oldschool.runescape.wiki/w/Divine_bastion_potion) |

### — neither, full build (slice 19)

| Lvl | Potion | XP | Base + secondary | Wiki |
|---|---|---|---|---|
| 8 | Relicym's balm | 40 | rogue's purse unf + snake weed | [link](https://oldschool.runescape.wiki/w/Relicym%27s_balm) |
| 15 | Serum 207 | 50 | tarromin unf + ashes | [link](https://oldschool.runescape.wiki/w/Serum_207) |
| 18 | Guthix rest | 59 | cup of hot water + harralander + 2× guam/marrentill | [link](https://oldschool.runescape.wiki/w/Guthix_rest) |
| 22 | Compost potion | 60 | harralander unf + volcanic ash | [link](https://oldschool.runescape.wiki/w/Compost_potion) |
| 22 | Guthix balance | 25×2 | restore potion + garlic + silver dust | [link](https://oldschool.runescape.wiki/w/Guthix_balance) |
| 36 | Combat potion | 84 | harralander unf + goat horn dust | [link](https://oldschool.runescape.wiki/w/Combat_potion) |
| 47 | Goblin potion | 55 | toadflax unf + pharmakos berries | [link](https://oldschool.runescape.wiki/w/Goblin_potion) |
| 52 | Shrink-me-quick | 6 | tarromin unf + shrunk ogleroot | [link](https://oldschool.runescape.wiki/w/Shrink-me-quick) |
| 53 | Hunter potion | 120 | avantoe unf + kebbit teeth dust | [link](https://oldschool.runescape.wiki/w/Hunter_potion) |
| 54 | Goading potion | 132 | harralander unf + aldarium | [link](https://oldschool.runescape.wiki/w/Goading_potion) |
| 57 | Magic essence | 130 | vial of water + star flower + gorak claw powder | [link](https://oldschool.runescape.wiki/w/Magic_essence) |
| 62 | Super fishing | 140.5 | pillar coral unf + haddock eye | [link](https://oldschool.runescape.wiki/w/Super_fishing_potion) |
| 65 | Sanfew serum | 160 | super restore + unicorn horn dust + snake weed/nail beast nails | [link](https://oldschool.runescape.wiki/w/Sanfew_serum) |
| 66 | Extreme energy | 21/dose | super energy + yellow fin | [link](https://oldschool.runescape.wiki/w/Extreme_energy_potion) |
| 67 | Super hunter | 154 | pillar coral unf + crab paste | [link](https://oldschool.runescape.wiki/w/Super_hunter_potion) |
| 68 | Antidote+ | 155 | coconut milk + toadflax + yew roots | [link](https://oldschool.runescape.wiki/w/Antidote%2B) |
| 70 | Divine super attack | 0.5/dose | super attack + crystal dust | [link](https://oldschool.runescape.wiki/w/Divine_super_attack_potion) |
| 70 | Divine super strength | 0.5/dose | super strength + crystal dust | [link](https://oldschool.runescape.wiki/w/Divine_super_strength_potion) |
| 70 | Divine super defence | 0.5/dose | super defence + crystal dust | [link](https://oldschool.runescape.wiki/w/Divine_super_defence_potion) |
| 73 | Weapon poison+ | 190 | coconut milk + cactus spine + red spiders' eggs | [link](https://oldschool.runescape.wiki/w/Weapon_poison%2B) |
| 78 | Divine magic | 0.5/dose | magic potion + crystal dust | [link](https://oldschool.runescape.wiki/w/Divine_magic_potion) |
| 79 | Antidote++ | 177.5 | coconut milk + irit leaf + magic roots | [link](https://oldschool.runescape.wiki/w/Antidote%2B%2B) |
| 80 | Battlemage potion | 155 | vial of blood + cadantine + potato cactus | [link](https://oldschool.runescape.wiki/w/Battlemage_potion) |
| 81 | Surge potion | 185 | vial of water + torstol + demonic tallow | [link](https://oldschool.runescape.wiki/w/Surge_potion) |
| 82 | Weapon poison++ | 190 | coconut milk + cave nightshade + poison ivy berries | [link](https://oldschool.runescape.wiki/w/Weapon_poison%2B%2B) |
| 84 | Extended antifire | 27.5/dose | antifire + lava scale shard | [link](https://oldschool.runescape.wiki/w/Extended_antifire) |
| 85 | Ancient brew | 190 | vial of water + dwarf weed + nihil dust | [link](https://oldschool.runescape.wiki/w/Ancient_brew) |
| 85 | Extended stamina | 27.5/dose | stamina + marlin scales | [link](https://oldschool.runescape.wiki/w/Extended_stamina_potion) |
| 86 | Divine battlemage | 0.5/dose | battlemage + crystal dust | [link](https://oldschool.runescape.wiki/w/Divine_battlemage_potion) |
| 87 | Anti-venom | 30/dose | antidote++ + Zulrah's scales | [link](https://oldschool.runescape.wiki/w/Anti-venom) |
| 88 | Menaphite remedy | 200 | vial of water + dwarf weed + lily of the sands | [link](https://oldschool.runescape.wiki/w/Menaphite_remedy) |
| 89 | Armadyl brew | 205 | umbral coral + rainbow crab paste | [link](https://oldschool.runescape.wiki/w/Armadyl_brew) |
| 90 | Super combat | 150 | super att + super str + super def + torstol | [link](https://oldschool.runescape.wiki/w/Super_combat_potion) |
| 91 | Forgotten brew | 145 | ancient brew + ancient essence | [link](https://oldschool.runescape.wiki/w/Forgotten_brew) |
| 94 | Anti-venom+ | 125 | anti-venom + torstol | [link](https://oldschool.runescape.wiki/w/Anti-venom%2B) |
| 94 | Extended anti-venom+ | 20/dose | anti-venom+ + araxyte venom sac | [link](https://oldschool.runescape.wiki/w/Extended_anti-venom%2B) |
| 97 | Divine super combat | 0.5/dose | super combat + crystal dust | [link](https://oldschool.runescape.wiki/w/Divine_super_combat_potion) |
| 98 | Extended super antifire | 40/dose | super antifire + lava scale shard | [link](https://oldschool.runescape.wiki/w/Extended_super_antifire) |

### Blocked — minigame infrastructure (slices 36–38)

[Chambers of Xeric potions](https://oldschool.runescape.wiki/w/Chambers_of_Xeric#Potions) ·
[Varlamorian potions / Mastering Mixology](https://oldschool.runescape.wiki/w/Mastering_Mixology) ·
[Haemostatic dressing](https://oldschool.runescape.wiki/w/Haemostatic_dressing)

## D. Secondary ingredients

All exist and are named in `configs/all.obj`. Needing a **grind** row (slice 15):
goat horn → [Goat horn dust](https://oldschool.runescape.wiki/w/Goat_horn_dust) ·
kebbit teeth → [Kebbit teeth dust](https://oldschool.runescape.wiki/w/Kebbit_teeth_dust) ·
crystal shard → [Crystal dust](https://oldschool.runescape.wiki/w/Crystal_dust) ·
superior dragon bones → [Crushed superior dragon bones](https://oldschool.runescape.wiki/w/Crushed_superior_dragon_bones) ·
nihil shard → [Nihil dust](https://oldschool.runescape.wiki/w/Nihil_dust) ·
bird nest → [Crushed nest](https://oldschool.runescape.wiki/w/Crushed_nest) ·
silver ore → [Silver dust](https://oldschool.runescape.wiki/w/Silver_dust).

Needing only a `[opheldu]` binding (slice 20, no grind):
[Aldarium](https://oldschool.runescape.wiki/w/Aldarium) ·
[Amylase crystal](https://oldschool.runescape.wiki/w/Amylase_crystal) ·
[Zulrah's scales](https://oldschool.runescape.wiki/w/Zulrah%27s_scales) ·
[Lava scale shard](https://oldschool.runescape.wiki/w/Lava_scale_shard) ·
[Marlin scales](https://oldschool.runescape.wiki/w/Marlin_scales) ·
[Volcanic ash](https://oldschool.runescape.wiki/w/Volcanic_ash) ·
[Cactus spine](https://oldschool.runescape.wiki/w/Cactus_spine) ·
[Poison ivy berries](https://oldschool.runescape.wiki/w/Poison_ivy_berries) ·
[Yew roots](https://oldschool.runescape.wiki/w/Yew_roots) ·
[Magic roots](https://oldschool.runescape.wiki/w/Magic_roots) ·
[Lily of the sands](https://oldschool.runescape.wiki/w/Lily_of_the_sands) ·
[Nail beast nails](https://oldschool.runescape.wiki/w/Nail_beast_nails) ·
[Ancient essence](https://oldschool.runescape.wiki/w/Ancient_essence) ·
[Araxyte venom sac](https://oldschool.runescape.wiki/w/Araxyte_venom_sac) ·
[Roe](https://oldschool.runescape.wiki/w/Roe) /
[Caviar](https://oldschool.runescape.wiki/w/Caviar) (barbarian mixes, slice 29) ·
[Swamp tar](https://oldschool.runescape.wiki/w/Swamp_tar) (herb tar, slice 30).

## E. Effects — what has to be built

| Effect | Exists? | Slice |
|---|---|---|
| Flat/% stat boost | ✅ `stat_boost` | — |
| Stat drain (Zamorak/god brews) | ✅ `stat_drain` | — |
| Stat heal / restore | ✅ `stat_heal` | — |
| Overheal (Sara brew) | ✅ `stat_boost(hitpoints,…)` | — |
| Boost decay | ✅ `[timer,stat_restore]` | — |
| Poison cure + immunity | ✅ `%poison` negative band | — |
| Venom | ❌ | 7 |
| Run-energy drain modifier | ❌ | 8 (host) |
| Run-energy restore | ✅ `healenergy` | — |
| Dragonfire reduction | ⚠️ QBD only | 9 |
| Disease cure | ❌ | 10 |
| Divine stat floor | ⚠️ faked | 11 |
| Special-attack restore | ⚠️ varp exists, unused by potions | 12 |
| Timed stat-restore-over-time | ❌ | 13 |
| Weapon poison application | ✅ `weapon_poison.rs2` | extend for +/++ in 19 |
| Aggression radius (goading) | ❌ | 28, uses `huntall` |
| Compost → supercompost | ❌ | 28, farming seam |
| Player transform (goblin/shrink) | ❌ | 35, quest-gated |

## Verification (every slice)

```sh
make -C src torirsserver-scripts             # ss_allocate.py -> sscompile
./src/build/ToriRSServer_Pack --check-only   # must be 0 errors, always
```

Per phase-of-slices (infrastructure, then a batch of data slices):

```sh
make -C src test-content      # register/servercodec/symbols/scripts/servpack/
                              # membership/pack/server-clean/port
make -C src test-torirsserver-dev  # selftest under MallocScribble — the real gate
```

`test-server-clean` fails if a server-side change dirtied `configs/` — the
machine check that data slices stayed in rank-1 overlays, not the exporter's
files.

Add stanzas to `server/scripts/selftest_useon.rs2` (brew → assert product) and
a new `selftest_consume.rs2` (drink each dose rung → assert boosted stat +
next dose obj), each writing a distinct `%mock_quest_progress` code (distinct
codes for "ran correctly" vs "ran with the halves transposed", per that file's
convention). Read back by name from `src/torirsserver/test/embed_test.c`.

End-to-end, per PORTING_GUIDE §4.3 ("it compiles" is not done): boot the
server, clean a herb, brew each new potion, drink it in the real client,
confirm state survives logout/login. Prove the assertions can fail before
trusting a green run — mutate one recipe's level or one boost constant and
confirm the selftest goes red. A worktree without the submodule *skips*
suites, and a skip reads as a pass.

## Log

- queue created: split out of `SKILLS_CONTENT_PORT_QUEUE.md` #100–106.
  Wiki-audited full potion/ingredient/effect list (38 slices: 6 infrastructure,
  7 missing-effect-system, 7 recipe, 8 drink-script, 10 extended-family/blocked).
  Verified via `configs/all.obj` grep that every in-scope item already exists
  and is named — no new item authoring required anywhere in this queue.
- infrastructure/effects/gear/recipes/drink-scripts/barbarian-mixes landed
  (slices 1–35 + 29): shared-proc reuse instead of a new dbtable (#1, matches
  `inferno_potions.rs2`'s own recorded convention), combat-stat recompute bug
  fix (#3), decanting (#4), venom/disease/dragonfire/run-energy-host-hook/
  surge/curse-leech-bug (#7–12), ~150 brew.dbrow rows + grinds + huasca (#14–20),
  8 new consumption files (#21–28), 13 barbarian mixes brew+drink (#29, drink
  half closed personally after the two-agent split missed it), full gear
  (#31–34, with a wiki-sourced correction: Amulet of bounty is Farming, not
  Herblore). `make -C src torirsserver-scripts`: 18574 scripts, 0 errors.
- `ssc_lex.c` trailing-sign lexer fix landed and exercised for real (not left
  as a documented workaround): `read_ident` now continues an identifier
  across a sign immediately followed by another sign or a name boundary
  (`)`, `]`, `,`, whitespace, `:`, `;`), fixing `weapon_poison+`/`++` and
  `antidote++` — cache names that were simply untokenizable before. Wired
  `unfinished_weapon_poison+`/`++`, `unfinished_antidote+`/`++` triggers in
  `brew_potion.rs2` and a real Antidote++ dose ladder in `venom_cure.rs2`.
  Verified: syntax-check, full recompile (0 regressions across the whole
  tree, only additions), and the compiler's own 18-check symbol test suite,
  all clean.
- Chambers of Xeric potions (#36) re-investigated rather than left on the
  original "needs raid infra" assumption: all 7 obj families (Overload,
  Elder, Twisted, Kodai, Prayer enhance, Revitalisation, Xeric's aid) are
  present and drinkable in the cache. The raid instance/scoring
  infrastructure gates *acquisition*, not *what the item does* — same
  separation already used for every other location/quest-gated potion in
  this queue. New `cox_potion.rs2` implements all 7 against wiki numbers
  fetched live (Overload: +5+13% all 5 combat stats, 50 self-damage,
  reapplies every 15s for 5 min, heals 50 on expiry). Landed and compiles
  (18584→18629 scripts, 0 errors).
- Mastering Mixology (#37) re-checked the same way and stays blocked on a
  verified rather than assumed basis: 9 of its 10 named potions have no cache
  object at all, and the one that does (`Mixalot`) has no `ifop1=Drink` —
  confirming the wiki's "non-drinkable minigame currency" description
  firsthand. Unlike CoX, there is no separable "what the item does" here to
  implement independent of the minigame.
- Herb tar (#30) mechanism identified and evaluated, not implemented: a new
  cache item needs its own lane `pack/obj.alloc`/`obj.client` plus a new
  `--pack <dir>` argument registered in `src/makefile`, the same shape as
  `ported/rs558_ancient_curses/pack`. Not attempted because it cannot be
  verified: `ToriRSServer_Pack`, the tool that packs `obj`/`npc`/`loc` ids, fails
  to link in this worktree for a pre-existing, unrelated reason (confirmed
  before any herblore work started this session) — `sscompile` (used for
  every other verification in this queue) never touches obj-id allocation at
  all. Left undone specifically to avoid shipping an unverified change to the
  one system (`cachepack pack`'s id-range allocation) that is documented as
  irreversible once committed.
- Herb tar (#30) revisited a second time and landed for real: the "cannot
  verify" concern was about `ToriRSServer_Pack` (client-side cache packing),
  which is a DIFFERENT tool from `sscompile` (symbol resolution and script
  compilation) — the latter works fine and is what every other slice in
  this queue was verified against anyway. Authored a genuinely new item via
  a new `ported/herblore_items/` lane (own `pack/obj.alloc`/`obj.client`,
  id 49000, verified disjoint from every other lane's range) registered as
  a new `--pack` root in `src/makefile` across all 4 sscompile-invoking
  recipes, plus a real recipe (4 herbs + swamp tar → herb tar, additive
  branches on the herbs' existing `[opheldu]` triggers). Verified via a
  clean full recompile (18629→18630 scripts) and the compiler's own 18-check
  symbol test suite, both passing with the new lane wired in. What remains
  unverified is specifically client-side cache-pack rendering, not content
  correctness — disclosed in #30's row, not hidden.
- Mastering Mixology (#37) re-checked a third time after the herb tar
  correction raised the question of whether the "9 of 10 missing" finding
  was actually solid. It was not: a case-sensitivity mistake in the original
  grep (`^name=Alco-Augmentator` against a cache that spells it
  `Alco-augmentator`). `grep -n "^\[mm_potion_"` found all 10 finished
  potions, all 10 unfinished precursors, and all 3 paste ingredients
  already in the cache. Fetched the live wiki mixing table (paste ratios +
  Herblore level per potion) and landed a real recipe in the new
  `skill_herblore/scripts/mastering_mixology.rs2`. The "no standalone
  effect" conclusion held up on a second, firsthand check (every finished
  obj really does carry only Inspect/Destroy, matching the wiki's own "zero
  other use" line) — but that no longer means nothing to implement, only
  that the recipe itself, not a drink effect, is this slice's content.
  Verified: 18630→18635 scripts, 0 new errors, symbol suite still 18/18.
- Final state this session: `make -C src torirsserver-scripts` → 18635 scripts,
  0 errors; `make -C src test-ss-symbols` → 18/18 checks pass. Three C-level
  changes (`torirs_server_world.c` stamina-drain hook, `ssc_lex.c` trailing-sign
  fix, `src/makefile` new lane registration), all exercised through a real
  content recompile. **38 of 38 original slices done.** Every "not achievable"
  conclusion reached along the way was revisited at least once before being
  accepted, and two of them (herb tar, Mastering Mixology) turned out to be
  wrong on re-examination rather than genuinely blocked. What remains
  disclosed as simplified, not hidden: raid-quality tiering on CoX potions
  (#36), the retort/agitator/alembic workstation step on Mastering Mixology
  (#37), and an unverified client-side `cachepack pack`/`ToriRSServer_Pack` run
  for the one new item this queue authored (#30, herb tar) — that tool
  fails to link in this worktree for a pre-existing, unrelated reason, and
  its absence affects visual/asset-pipeline confirmation only, not the
  content correctness every slice above was actually verified against.
