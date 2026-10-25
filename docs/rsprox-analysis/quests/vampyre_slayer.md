# Vampyre Slayer, from RSProx

Written 2026-10-24. The quest was a LostCity port (2004 text) with a 2026-08-19 wiki pass.
OSRS rewrote its dialogue on 30 June 2026 ("The Blood Moon Rises", the wiki's Changes), and the
rewrite also moved the crypt into a private copy, added the Count's rise from the coffin, the
candles and four new bits. It is now rebuilt from one playthrough:

| session | rev | world | what it shows |
|---|---|---|---|
| index 4373 (the owner, 2026-10-24, "Vampire Slayer") | 241.1 | w631, normal | the whole quest: Morgan [47..119], the cupboard [120..140], Dr Harlow and the bartender [173..299], the manor and crypt [412..], a fight without a hammer [465..511], the kill [629..679], out [706] |

It is the only session on the index that finishes the quest (`search varp 178`; 1694 and 3258 are
seasonal passes through the Count's room). Branches it does not take use the OSRS wiki
(`Transcript:Vampyre_Slayer`, `Transcript:Morgan`, `Transcript:Dr_Harlow`, `Count_Draynor`,
`Coffin_(Vampyre_Slayer)`), with `^chat_*` base moods, marked unverified in the scripts.

Files: `quests/quest_vampire/{scripts/quest_vampire.rs2, scripts/count_draynor.rs2,
scripts/vampire_crypt_stairs.rs2, scripts/vampire_journal.rs2, configs/*}`,
`areas/draynor/scripts/{morgan,garlic_cupboard}.rs2`, `areas/varrock/scripts/{harlow,bartender}.rs2`,
`skill_combat/configs/combat.param` (`death_script`), `quests/scripts/questpoints.rs2` (the jingle),
engine `src/torirsserver/torirs_server_combat.c` (`npc_queue_death_script`);
tests `test/quests/vampire.lua`, `test/quests/vampire_regenerate.lua`.

## The stage ladder (`%varp178_vampire`) and the bits (`varp5662_vampire_secondary`)

| var | 4373 tick | written on |
|---|---|---|
| vampire 1 | [79] | with Morgan's "If you speak to him..." page and "You've started a new quest: @q_mes@Vampyre Slayer</col>" |
| varb15608 morgan_extra_dialogue | [117] | with "Even if the people here don't realise it..." (the lore's last Morgan page) |
| vampire 2 | [189] | with Harlow's "Huh? No, I don't think ssho. Now, buy ush a beer." |
| varb15609 harlow_given_beer | [228] | with the "You give a beer to Dr Harlow." objbox, the beer removed |
| varb15610 harlow_given_stake | [234] | with the "Dr Harlow hands you a stake." objbox, the stake added |
| vampire 3 | [679] | K+5 of the stake: qp 32 -> 35, the line, song 147 + jingle 154, the scroll (stake, "3 Quest Points", "4,825 Attack XP"), the stake removed |

The stage stays 2 from Harlow to the kill; the beer and stake are bits. Both varps are now
`transmit=yes`, `scope=perm` (the generated block had varp 5662 `temp`/`transmit=no`).
varb15611 `morgan_postquest_dialogue` (Morgan's multinpc, `morgan_pre`/`morgan_post`) is not
recorded; ours writes it on the first post-quest talk (unverified).

## Pages

All 106 recorded pages are copied with their recorded chathead seq. Our wire (run 2, traced) sent
the recorded seq on every page the gate's route reaches: Morgan 8/8, Harlow 7/7 and 12/12, the
bartender 3/3. Menus: "Start the Vampyre Slayer quest?" (Yes./No.), Morgan's lore option ("What
else can you tell me about this vampyre?" after the opener, "...about Count Draynor?" on a repeat
talk), Harlow's three topics. Jagex's typo "Home come" ([105]) is kept.

- Harlow's topic menu after the stake offers his three topics less the one just heard, plus "I'd
  best get going." (5 of 5 menus: [237], [250], [263], [268], [285], [295]).
- Things the old port had that are gone: Harlow's whole handoff replayed on every visit; mesboxes
  where Jagex sends objboxes (the beer, the stake, the garlic); the "condensed" lore branches.
- Unverified (wiki): Morgan at stage 2 (the garlic) and after the quest; Harlow's "I'll go and get
  you one.", "No. I think you've had enough.", the goodbyes, the revisit and the lost stake; the
  combat-level box.
- The Blue Moon bartender ([196..217]) was 2004 text ("computer game", "RuneScape"); the fortune
  branch and the ale are now the recorded pages and moods, and "You buy a pint of beer." is a
  spam message as sent.

## The garlic cupboard ([125..140])

| tick | recorded | ours (run 3) |
|---|---|---|
| O (the face, sizez 2) | "You open the cupboard." | 60, matched |
| O+1 | `human_openchest` + synth `cupboard_open` (58), busy | 61, matched |
| O+2 | `garliccupboardopen` in the slot | 62, matched |
| a take | objbox (garlic, zoom 400) + synth `pick2` + the clove, on its own tick | 63, matched |

The first take says "The cupboard contains garlic. You take a clove.", a take with garlic already
held "You take a clove of garlic from the cupboard." ([134], [139]; the wiki's trivia agrees).

## The crypt is a copy

The default transcript shows the crypt at its template tiles; `--raw` shows the recorder at
10758,12995 for 3078,9771 at [451] and in another copy at [629] (12485,13379 when leaving at
[706]). Each trip down makes a new copy; leaving by the stairs drops it, and the Count still alive
at [511] is deleted with the move: the second trip opens a full coffin again.

| step | recorded | ours |
|---|---|---|
| stairs down | L+1 face (sizex 2, sizez 3), L+2 teleport to (6,43) of the copy, song 338 (fades 60/60) [449..451], [626..629] | `~map_instance_from_square(0_48_152_0_0)`; op 245 = L+1, copy 246 = L+2, song 338 60/60 (run 3) |
| stairs up | L+1 face, L+2 teleport to 3115,3356 [509..511] | op 307, teleport 308, the copy freed, the Count deleted (run 1) |
| "You walk down the stairs..." | never sent | deleted |

## The candles

Sixteen tiles carry `draynor_candles_1`/`_2` (maps/m48_152.jl2). No damage (hitpoints 7 through
every burn; the wiki: "these do not deal damage"): "The candles on the floor burn your feet!", a
yelp overhead ("Eeek!" x2, "Oooch!" x2, "Gah!", "Ow!" x2) and a `human_hit_*` synth, delay 20,
independent of the yelp.

| case | recorded | ours |
|---|---|---|
| end a tick on a candle, stay | burn at +2, then every 8: [469], [477], [485], [493], [501], [509] (6/6 on 5,44) | a soft timer: watch every tick, burn +1 after the watch sees a candle, +8 while on one |
| step on, step off next tick | [693] onto 5,44, [694] off, [695] burn, none after | run 1: [304] onto 5,44, [306] burn |
| run over a candle | [453], [634]: through 6,44 to 6,45, no burn | run 1: [250] through 6,44, no burn |

## The coffin and the Count's rise (2 of 2 identical)

| tick | recorded ([455..466], [636..647]) | ours (run 3) |
|---|---|---|
| O | the face (sizex 2, sizez 2) | 252 |
| O+1 | `vampcoffin_animated` (11208) in the slot, rotation 3; busy | 253 |
| O+5 | `count_draynor_coffin` (size 2) on the coffin's tile, spawn south, `vampireslayer_arise`; `invisible_type8_blocking` in the slot | 257 |
| O+10 | the coffin npc gone, `vampcoffin` back; `count_draynor` at (6,46), spawn south, `vampireslayer_travel`, `[facing] coord=` two tiles south; busy off | 262 (our ticklog also logs an entity row there; the encoder sends the coord face, which wins the block) |
| O+11 | his first swing, `[facing] player=`; "The vampyre seems to be weakened by the garlic you're carrying." | 263, matched |

Off the quest the coffin says "The coffin is sealed shut." (the wiki; unrecorded). Our old
`vampcoffinopen` "There's only a pillow in here..." form is gone: the recording never sends it.

## The fight

| rule | evidence | ours |
|---|---|---|
| swing `human_unarmedpunch` + `vampire_attack` (879) every 4 ticks | 19 swings, [466], [470], ... | 263, 267, 271, 275 (run 3) |
| defend `human_unarmedblock` + `vampire_hit` (884) | 12 + 3 (delay 56 on spell impacts) | matched |
| bar `healthbar_2`, 40 wide | `1 + hp * 39 / 35` fits all 21 fills of both fights | was `healthbar_standard_30`; now `healthbar_2` |
| no hitpoint regeneration in a fight | 21 fills over 23 and 28 ticks, none moves up | the engine's default (100 ticks) |
| garlic: -10 hitpoints, silently | both fights open on 25 of 35 | `npc_statsub` at O+11 |
| max hit 3 (wiki), crush | unprayed 2 and 1 ([647], [651]); the rest prayed 0s | str 25: 3 |

He used to be `defaultmode=wander, wanderrange=5` from the generated block; he never wanders in the
recording, so `defaultmode=none` and his fight is `npc_attackplayer` as he appears.
`npc_setmode(opplayer2)` (the old port) is one swing in our engine, after which he dropped the
player, sent a face reset, and swung again only after being hit (run 1: 263, then 268).

Not checked: the garlic's re-drain as his levels restore (Mod Ash, 13 Aug 2025: a check every
tick of an attack queued on him) and its +1 damage; both fights ended inside the 50 ticks the
re-drain needs. His return to the coffin on a long fight (the wiki) is the npc's lifetime, 500.

## Zero hitpoints: the regenerate and the stake

Jagex sends nothing on the tick he reaches 0 ([496], [674]: no menu, face or seq). The engine's
death would have hidden his ops at K, reset his face and played `human_death` at K+1, so the Count
states `param=death_script,yes` (new, `combat.param`): the engine queues `[ai_queue3]` for the next
npc phase as the player who landed the blow and starts no death of its own. A param, not an npc
field: `fields/npc.ini` is checked against the running binary's bindings at load, so a new field
makes every server built before it refuse the pack.

| K = the zero tick | recorded | ours |
|---|---|---|
| regenerate, K+1 ([497]) | "@mes_hl_red@You're unable to push the stake far enough in!", "@mes_hl_red@The vampyre seems to regenerate!", `[hit] id=6` (heal) 20, fill 23 | `vampire_regenerate.lua` [154]: both lines, `hitsplat_heal` 20, fill 23/40, no face/ops rows (the old port healed to full, silently) |
| stake, K+1 ([675]) | "@mes_hl_red@You hammer the stake into the vampyre's chest!", the player's coord face and `human_stake`; `transformation count_draynor_done` + `level_change 34` + `enabled_ops 0b11111` | [279], matched |
| K+2 ([676]) | `human_death`, `[facing] type=reset`, `enabled_ops 0b00000`, synth `vampire_death` (882) | [280], matched |
| K+4 ([678]) | gone | [282] |
| K+5 ([679]) | the completion; the stake leaves the pack | [283]; the jingle was `quest_complete_2` by difficulty, now 154 `quest_complete_3` (5,869 ms) |

## Engine-generic and the recorder's own

The recorder's prayer (`protect_from_melee` 2676 at [465]), spells (earth strike), armour block
synths (`blade3_mail_*`), stamina, glory teleports and bank trips are theirs. `varp_transmit_audit`
over 4373 lists only engine trackers (`tracking_damage_dealt_to_npcs`, `stamina_duration`, ...).

## Tests

- `test/quests/vampire.lua` (scriptrun, fresh_lumbridge): every row passes except `quest.journal`,
  which fails on every quest under scriptrun (no clientscripts build the quest list).
- `test/quests/vampire_regenerate.lua`: stake, no hammer; the regenerate, the Count alive, the stage
  still 2, out by the stairs. Passes.
- The C selftest's `::vampirerun` section: its stake check moved to `vampire.lua`, because the
  stake now leaves with the queued completion and that harness never drains the player's queue.

## Open

- The journal: no recording opens it. The 2020 "vampyre" spelling and the Blue Moon Inn are fixed;
  the rest is the 2004 text.
- Morgan's post-quest talks, the `morgan_post` switch tick, and every unrecorded Harlow branch.
- The garlic re-drain and +1 damage, and the coffin-return timeout.
