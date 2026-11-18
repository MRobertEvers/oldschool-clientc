# Single-way combat: what Jagex refuses, and when

Outside a multi-combat area, a player fights one npc at a time. This page sets out what the recordings show of that rule. It also says where the rule lives in our tree: in content, as the reference has it (`skill_combat/combat.rs2`, "Single-way combat").

Tool: `tools/rsprox/rsprox_singles.py`. It prints one row per Attack click (`opnpc2` / `opnpct`) by the recorder. Each row gives the ticks since the last npc swing at the recorder, whether that npc had died since, the `multiway_indicator` varbit, and the refusal line that followed, if any. It ends with a histogram.

```sh
cd ~/.rsprox-index-cache/home-default/.rsprox/binary/archive
python3 ~/Documents/git_repos/3draster/tools/rsprox/rsprox_singles.py \
    idx4523.txt idx4421.txt idx4402.txt idx1084.txt idx1686.txt idx4150.txt idx390.txt \
    idx1869.txt idx1847.txt idx231.txt idx4068.txt idx1846.txt idx213.txt idx1105.txt \
    idx1138.txt idx4042.txt idx4045.txt idx4075.txt idx1820.txt idx1828.txt
```

The sessions are every index hit for `search message_game "already under attack"` on a normal world at rev 241–242 (4523, 4421, 4402), plus the normal-world and Leagues sessions with the most hits. Index 4430 (A Soul's Bane) is left out of the counts. Its monsters change form under one index, and the tool's last-attacker attribution cannot follow them.

## The rules

| Rule | Recorded | Count |
|---|---|---|
| Attack on a second npc 0..7 ticks after another npc's swing at you | **"I'm already under attack."**, type `spam` | 15 of 16. The 16th is index 4523 [5204], where the "swing" was Zanik's pipe-squeeze emote and the click was replaced by a ladder op on the same tick. |
| The same, 8 ticks or more after | allowed | 13 of 13 |
| Attack on the npc that is hitting you | allowed, at any tick | 4523 [9715]: Sigmund hit at [9714], clicked at [9715] |
| The npc that hit you has since died | allowed at once, inside the 8 | 4 of 4: 4523 [9695] [9700] [10038] (tool), [9686] (by hand: guard 45879 swung [9682], died [9684]) |
| The npc that hit you changed form | counts as a different npc (refused) | 4523 [9720]: Sigmund melee swung [9714] and became `dttd_sigmund_ranged`. Jagex's uid carries the type. |
| A multi-combat zone | no refusal | 1 refusal of 245 had `multiway_indicator` 1 (4068 [90], session start, varbit possibly stale) |
| Attack on an npc fighting someone else | **"Someone else is fighting that."**, type `gamemessage` | 4402 [663]: a goblin fighting another goblin in the Goblin Village war |
| The refusal while walking in | the line repeats every tick of the walk, and the player keeps walking to the npc he clicked | 4523 [9650] twice, [9651] once |
| Attack while wearing a greegree | **"You cannot attack as a monkey."**, a mesbox | 4564 [24145], 4588 [17302] (Ape Atoll, rev 242) |
| Attack on an npc spawned for another player | **"It's not after you..."**, type `gamemessage` | 1752 [7848] (Armadylean guard), 31 [15541] (Double agent), both clue-scroll fighters |
| "I'm already under attack!" (with an exclamation mark) | a different line, from content's own non-Attack ops | 1105 [25208] (hunting a sunlight antelope) |

Message tally across the 20 sessions: "I'm already under attack." 571 times, all type `spam`; "Someone else is fighting that." 36 times, all type `gamemessage`.

## Where it lives

These are LostCity's own vars and procs, ported (`skill_combat/scripts/player/player_combat.rs2:98-130`, `skill_combat/scripts/npc/npc_combat.rs2:357-388`):

| | Ours |
|---|---|
| `%lastcombat`, `%aggressive_npc` (player) | `%varp9695_lastcombat`, `%varp6431_aggressive_npc` |
| `%npc_lastcombat`, `%npc_aggressive_player` (npc) | the same names, `skill_combat/configs/npc_combat.varn` |
| stamped on every npc swing | `[proc,npc_set_attack_vars]`: from the melee, ranged and magic attack procs, and from the engine's two swing points |
| stamped on every player attack | `[proc,npc_set_aggressor_vars]`: from `~npc_retaliate` and the melee swing |
| asked by every Attack and every targeted cast | `[proc,player_in_combat_check]` |
| asked before an npc's swing | `[proc,npc_check_notcombat]` (give the player up), `[proc,npc_check_notcombat_self]` (wait) |
| the hunt (a monster noticing a player) | `singles_hunt_refused` in torirs_server_combat.c reads the same vars. This is LostCity `Npc.ts` huntAll's `check_notcombat` / `check_notcombat_self`, asked at the player's tile. |
| a dead attacker frees the player | `ToriRSServer_CombatSinglesRelease` at the killing blow. This is Jagex's behaviour, not the reference's: LostCity keeps the stamp. |

Every read checks that the stamp is non-zero first. A fresh var is 0, and the boot clock is 0 too.

## Known divergences

- **A changed form keeps its uid.** Our `npc_uid` is generation:slot, and Jagex's carries the type. Sigmund's melee-to-ranged switch (4523 [9720]) is allowed here, where Jagex refuses it.
- **Npc-vs-npc fights do not stamp `%npc_lastcombat`.** So the goblin war's "Someone else is fighting that." (4402 [663]) is not given here.
- **"It's not after you..." is not ported.** No content spawns a personal npc yet. Also, a `player_uid` npc var reads 0 rather than null here, so the reference's `! null` test would refuse everyone.
- **No `%lastcombat_pvp`.** There is no player-vs-player combat.
