# Verbs: combat -- `npc.await_dead*`, `player.attack`, `player.alive`, `player.cast` (section 3)

The fight verbs from section 3. Trap 31 (the `[opnpc2]` binding) and `gaps-combat.md` (single-way,
`::passive`, crowded spawns, the one-blow fight) hold the rest.

## `t.npc.await_dead` / `t.npc.await_dead_engaged` (section 3, `ui` / `npc`)

### `t.npc.await_dead(npc, ticks=60, radius=10, attempts=6)` -- also `t.npc.await_dead_engaged`

`t.npc.await_dead(npc, ticks=60, radius=10, attempts=6)` -> `ok` `timeout` `not_found`. Resolves
when the npc's SLOT LEAVES THE POOL with a corroboration the detail names --
`corroborated by the ZERO BAR` (it read 0 earlier in the wait, then was released) or
`corroborated by ABSENCE` (missing for 2 consecutive polls the pool vouches for: under 64 rows, or
its farthest row beyond the slot's last tile). A ZERO BAR ALONE IS NOT A KILL (seam19): the server's
bar is floor(hp x width / max), so a 170-hp Khazard warlord reads 0/30 ALIVE at 1-5 hp; after a zero
bar the wait keeps polling up to 12 ticks past its deadline for the corpse's release
(`the corpse grace` in the detail), and a timeout names any zero bar or missing slot it saw.

One `await_dead_engaged(<ticks>, attempts)` is enough for a big-hp boss -- no ground-truth re-press
loop. And it answers on the release, BEFORE the boss's `[ai_queue3]` outcome necessarily lands
(stage write, stake consumed, a regenerate heal, the completion queue): `t.msg.await(<line>)` or
`t.ticks(10)` before reading them (Vampyre Slayer, `build/quest_gate/s19fh_stake_fixed`). It
RE-ISSUES Attack when the fight has stopped -- health unmoved for five server ticks AND no new
hitsplat AND the player idle, all three, because each alone is ordinary mid-fight.

`radius` only picks WHICH npc; from then on the fight is tracked by that npc's SERVER SLOT, because
a symbol is not a target (`falador_gardener` has three spawn rows and a second one is inside the
loaded scene). The detail always carries the re-engagement count and the last health reading.

Since seam31 both kill waits also carry a progress trail at the end of their detail, `ok` or
`timeout`: `; progress t+10 hp 44/60, t+20 hp 43/60 re-engaged 1 ate 2, ...`, one sample every
10 ticks, the last 12 kept (`(N earlier dropped)` says how many fell off). Each sample is also a
`QUEST progress ... tick=T` line in `client.log`, which is what `run.unfinished` reads back when a
run ends inside the wait (running.md). Read the trail for a fight's hp curve: a flat trail is a
fight that stopped, a trail that ends early is a run that ran out of frames.

#### A kill is never proved by an empty pool (2026-09-21)

A KILL IS NEVER PROVED BY AN EMPTY POOL (2026-09-21): the npc pool is the CLIENT's, so a slot leaves
it for three reasons and only one is a death -- the npc died, the npc ranked past the 64 nearest the
pool holds (Mort'ton's shade street holds more than that), or THE PLAYER LEFT THE SCENE, which
mid-fight almost always means he died. Both await verbs now corroborate a bare absence before
calling it a kill: the death fence runs first (and it reads a STATED hitpoints of 0 as well as the
chat line, because `[queue,player_death]` prints "Oh dear, you are dead!" about twenty ticks AFTER
the killing blow), and `await_dead_engaged` additionally requires that a health bar was ever sent
for the slot -- a bar is only sent once something has HIT an npc, so gone-with-no-bar answers
`no_row` and does NOT consume the stamp, and your hunt loop presses Attack again instead of
crediting a corpse it never made.

Roving Elves reported `dead after 131 tick(s)` for a Moss Guardian standing at 2/30 while the
character was the one who fell, then failed three more rows on the seed it never dropped.

#### `t.npc.await_dead_engaged(ticks=60, attempts=6)`

`t.npc.await_dead_engaged(ticks=60, attempts=6)` -> `ok` `timeout` `no_row` `refused` takes NO
target at all: it holds the slot the last `t.player.attack` pressed an Attack row on, follows an
`npc_changetype` (a Loar Shadow BECOMES a Loar Shade on its first hit, and the symbol the file
attacked with stops naming what it is fighting), re-engages on the id the slot wears NOW, and
CONSUMES the stamp -- a second call with no new Attack answers `no_row`, so one kill can never be
waited out twice.

Use it for every hunt: a loop that re-resolves its symbol each attempt abandons the half-killed npc
it already engaged, and an abandoned npc in retaliation keeps hitting. Through `t.exec`, spell the
ticks (`t.exec("shade.dead", t.npc.await_dead_engaged, 40)`).

#### A boss whose death spawns its next form: the wait fights on into it (matthew-mbp-m4-b47)

In The Fremennik Trials, Koschei's third form dies and the fourth appears at once
(`viking_thorvald.rs2:229`). `await_dead_engaged` did not return on the third form's death: it went
on fighting the fourth, so a row named for the third kill would grade the wrong fight. `viking.lua`
leg 6 waits on the quest's own phase var instead: it loops `await_dead_engaged` and reads
`varp6759_viking_koschei_phase` after each call until the phase says the form is beaten. For any
boss that chains forms, grade each form on its phase var or stage, not on the wait's `ok`. The fourth
form attacks at once, so the leg cannot end in the arena (relay: Still "in combat" after the fight).

#### Eating (seam27)

EATING (seam27): `t.npc.await_dead(npc, ticks, radius, attempts, opts)` and
`t.npc.await_dead_engaged(ticks, attempts, opts)` take
`opts = { eat = { item = <food symbol>, below = <hitpoints>, op = 1 } }`: each tick, STATED
hitpoints under `below` eat the food through `inv_op` (at most once per 3 ticks; the death fence's
own reading); the detail appends
`; eat <food> below N: ate <food> K time(s) (hp a->b, ...), lowest hp x/y` or `never needed to eat`,
and `OUT OF <food>`. A malformed `opts.eat` or unknown food symbol ends the run. Never hand-write a
cast/attack-and-eat loop:
`t.exec('boss.dead', t.npc.await_dead_engaged, 240, 40, { eat = { item = 'shark', below = 70 } })`.

#### Eating inside an attack press and a re-engagement (b63-seam1)

The kill waits used to eat only BETWEEN their wait ticks, and an Attack press is not a wait tick: a
`covered` first press, its cover recovery (settled camera, poses), its `walk_near` and two more
pose-and-probe presses can spend tens of ticks inside one call. Haunted Mine's Treus Dayth fight
died there in runs 12, 15 and 18 (hp 95 -> 0 while one re-attack probed). Now:

- `t.player.attack(npc, op, ticks, opts)` takes `opts.eat` (the kill waits' table and asserts;
  `{ eat = ... }` alone means the nearest copy, or put it beside `slot=`/`at=`). Its detail gains the
  same `; eat <food> below N: ...` tag, and it eats every tick of its settle.
- With an eater, every attack press -- `t.player.attack`'s, `await_dead`'s re-attack and
  `await_dead_engaged`'s re-engagement (and the cast fight's re-cast) -- eats before each press
  attempt, after a failed cover recovery and after the walk. A press due while hp still reads under
  `below` AFTER that eat (food delay, out of food, a burst bigger than a meal) is the bounded fast
  press (seam5's `quick`, about two ticks), never the hunt.
- The tag says where: `, 4 of them inside an attack press (after the cover recovery 68->88, before
  press 3 69->89, ...)` and `, N press(es) made by the fast path because hp was under 80`. `lowest hp`
  is now the lowest the eater ever READ (inside the food delay too).
- `t.player.cast` takes no `opts.eat` (it raises); eat in the kill wait that follows it. A re-cast
  that must eat between presses cancels the armed spell first, so the Eat is never a cast on the
  food.

So a re-attack after a kill wait that ended with the boss alive is
`t.player.attack(boss, 2, 12, { eat = { item = "shark", below = 85 } })`, not a hand-written
eat-then-attack. Proof: `build/quest_gate/b63s1_eatpress1` (a hellhound on a defence-1 player, every
npc press forced `covered` for ~25 ticks): without `opts.eat` hp 93 -> 41 with 18 sharks untouched;
with it two eats inside the press, lowest 67/99; the re-engage wait ate 4 of its 5 sharks inside its
two forced re-engagements. Conformance: `seam.attack_eats_inside_its_press` (99/99 hp, `below = 100`:
the eat before the press, the fast press, a shark gone; the same press without `opts.eat` eats
nothing).

#### Cast fights re-cast (seam27)

A CAST fight (after `t.player.cast`) now judges its stall on the health reading alone, so it
RE-CASTS with auto-retaliate on too (a melee retaliation's 0 splat no longer counts as progress;
Chronozon 0 -> 8 re-casts in 60 ticks, `build/quest_gate/s27re_before3` vs `s27re_after1`) -- a
Chronozon kill is each element cast until its own 'weakens' line, then
`t.player.cast('fire_blast', 'chronozon', 14)` and `await_dead_engaged(240, 40, {eat=...})`.

#### A kill's bones are on the floor, not in the backpack

A death drop lands on the kill tile as a ground obj. `await_dead` does not pick it up, and neither
does anything else. Take it in the same leg: `t.player.click_obj("<bones symbol>", 3)` (op 3 is
Take), then `t.inv.await("<bones symbol>", 1, 6)`. Monkey Madness took
`mm_small_zombie_monkey_bones` this way after `killZombie`. A private drop lags the zone packet, so
poll `t.world.obj_near` first (gaps-combat: "Three world facts"). Never teleport back later for a
drop you left behind (sampler-findings: Sample sonnet-b43, (c)).

## `t.player.attack`, `t.player.alive`, `t.player.cast` (section 3, `world` / `drive` / `player`)

### `t.player.attack(npc, op=2, ticks=10)` -- also `t.player.alive`, `t.player.cast`

`t.player.attack(npc, op=2, ticks=10)` -> `ok` `timeout` `refused` / click_minimenu's own results.
ONE Attack click (re-pressed up to three times a tick apart -- an Attack target closes on you and
the projected pixel goes stale), then settles on the npc's first hitsplat or health-bar move. The
detail names the npc's health before and after as `<ratio>/<scale>`: a client is never told an npc's
hitpoints, only a fill out of the healthbar type's own width, so four of seven hp reads `17/30` and
an npc nothing has hit yet reads `no bar`.

`op` is an argument because Attack is op 2 only where the npc also has Talk-to; the row that gets
PRESSED is checked and a non-Attack row answers `refused` naming it.

#### `refused`, meaning two: single-way combat refused the swing

`refused` HAS A SECOND MEANING and it is the one to read first when a fight does nothing: THE SERVER
REFUSED THE SWING, with the engine's own sentence in the detail ("I'm already under attack." /
"Someone else is fighting that.", printed by `ToriRSServer_CombatSinglewayRefuses`). That is
single-way combat: the press landed, `p_opnpc` took its silent return, and the engine has no attack
clock of its own, so no swing was ever made.

It is not a `timeout` and not an opening -- waiting longer cannot help while whatever holds the
claim keeps swinging, and no stamp is written, so an `await_dead_engaged` behind it answers
`no_row`. Mort'ton's hunt believed it was fighting for twenty rounds and 1,643 ticks on that
reading, because an Afflicted villager had engaged the player and claimed him. A `timeout` is a miss
streak or a click issued while another action is still in flight, NOT a failed click -- raise
`ticks` and read `npc.await_dead`'s row for whether the fight was won.

#### `t.player.alive()` and the `player.died` rule; carry food and wear the weapon

`t.player.alive()` -> `ok` `refused` is the reading under all of this and takes NO ARGUMENT, so
record it with `t.expect`, never `t.exec` (a nil first argument is FAIL `bad verb/target`). The RULE
on top of it is not yours to write: the first time a death is visible the driver writes a terminal
FAIL row `player.died` with its own screenshot and ENDS the run -- armed on every click settle,
every attack and every tick of both await_dead verbs -- because every click after a death is issued
from the respawn tile with an empty backpack, so a run that ground on past one would be producing
evidence of a different world.

Carry food and EAT IT, and WEAR the weapon you were given: `::give rune_scimitar` is not
`t.player.equip("rune_scimitar")`, `::give shark 5` with nothing that ever eats one is the same bug
as no food at all, and `::setlevel` is not a substitute for either. Mort'ton's hunt died at four of
five kills over exactly that, and Roving Elves' Moss Guardian -- 120 hp, +62 strength, a
prayer-bypassing roll, fought bare-handed because the tomb forbids a loadout -- won at the
character's last 8 hitpoints with eight of its own left, on a setup that carried no food and never
ate.

#### Attack fights ONE copy (seam21); `refused`, meaning three

`t.player.attack(npc, op, ticks, opts)` fights ONE copy (seam21): nearest by default, or `opts`
`{slot=n}` / `{at={x,z[,level]}}` exactly as `talk_to`; the camera turns to it, the press is aimed
by its element and re-aimed as itself, and the detail reads
`pressed slot N (element E) at x,z, watching slot N`. A named copy the press finds no pixel for is
walked toward once; a selector that matches no copy is `no_row` naming every live copy;
`npc.await_dead` re-engages by slot and `await_dead_engaged` by the slot's own element
(`build/quest_gate/s21as_gob8`).

`refused` has a THIRD meaning: "I can't reach that!" / "You can't reach that." -- no route to the
copy, no stamp (the Lumbridge goblin house interior and the river bank are measured traps).

#### Fever spiders (seam22) and melee reach reads walls (seam22)

Rum Deal's fever spiders take the player's damage with or without gloves (seam22, wiki
Fever_spider): WITHOUT `deal_slayer_gloves` every spider attack is a forced 12.5%-of-Hitpoints hit
plus disease (`%disease=1`, "You feel yourself becoming diseased."), so wear the gloves or eat
(`build/quest_gate/s22fs_spider3`). MELEE REACH READS WALLS (seam22): a melee swing from either end,
and an npc's `opplayer` arrival, needs the footprints flush on a cardinal side with no wall on the
shared edge (the reference's reachExclusiveRectangle) -- a monster behind a closed door, fence or
bars cannot hit you and you cannot hit it; before, Druidic Ritual's prison-door suit hit the player
through the door.

#### Casting: `t.player.cast` (seam19)

**Casting:** `t.player.cast(spell, npc, ticks=10, attack_op=2, opts)` -> `ok` `refused` `no_runes`
`timeout` `no_row` `not_visible` `unsupported` / click_minimenu's own results (`spell.lua`, seam19).
A spellbook spell cast on an npc through the client: opens the magic tab, arms the spell's own
"Cast" row (`api_drive.spell_arm`, the TGT_BUTTON row the real menu offers -- `t.ui.invoke` cannot
arm a spell, it sends IF_BUTTON), then presses the npc's one "Cast <spell> -> <npc>" row with
attack's framing and re-aim, and puts the backpack tab back.

`spell` is the `magic_spellbook:` component name (`"wind_strike"`, `"fire_blast"`; `"Wind Strike"`
folds to it); `no_row` = no such component. `ok` means THE CAST HAPPENED -- Magic XP rose (runes
spent, base XP paid) and six ticks of flight were watched -- NOT that it landed: a splash pays the
same XP, and a splat on the npc afterwards can be your own melee auto-retaliation (Chronozon: four
`0` splats, no landing). So a quest that needs a spell to LAND waits on the content's own line and
casts again: Family Crest is `repeat t.player.cast("water_blast", "chronozon", 14) until` a NEW
"Chronozon weakens..." line (compare `t.msg.last()` serials before the cast -- `t.msg.await`
registered after the cast misses a line that already arrived).

`no_runes` carries magic.rs2's "You do not have enough <Rune> Runes to cast this spell."; `refused`
the level/members/immunity/frozen sentences, single-way's "I'm already under attack.", or "I can't
reach that!" (the nearest copy stands where you cannot path -- `::spawn` your subject beside you) --
`::passive` every aggressive type in the field first (a Lumbridge goblin cast was refused by a giant
spider's claim). `cast` fights ONE copy exactly as `attack` does (seam22): nearest by default, or
`opts` `{slot=n}` / `{at={x,z[,level]}}`; the press is aimed by the copy's element and asserted to
land on it, the detail reads `pressed slot N (element E) at x,z, watching slot N`, and a selector
matching no copy is `no_row`.

A cast that went out stamps `QD._combat_last` (with `spell=`), and `t.npc.await_dead_engaged`
RE-CASTS that spell on the stamped copy when the fight stalls (never an Attack press; runes are
spent per re-cast -- carry enough); its row ends
`[re-engagements re-CAST <spell>: N press(es) ok, M not; ...]` and names a server refusal a re-cast
provoked. A magic kill is `t.player.cast` once, then
`t.exec("x.dead", t.npc.await_dead_engaged, ticks, attempts)`; with auto-retaliate on, melee
retaliation lands too, so read the rune count if the kill must be magic's
(`build/quest_gate/s22cast_after5`).

#### Crumble Undead, rune symbols, magic accuracy, Chronozon

Crumble Undead on a non-undead npc answers `refused` "This spell only affects skeletons, zombies,
ghosts and shades." (Slash Bash is undead since seam23, and a real 100-hp level-111 boss with his
wiki infobox stats -- bring food and the wiki's safespot/Protect from Missiles). Rune symbols are
`airrune`/`mindrune`/`waterrune`/`earthrune`/`firerune`/`deathrune`/..., not `air_rune`. Magic
accuracy is LostCity's (seam20, `combat_stats.rs2`): the player rolls
`(magic_eff + 8 + 1) * (magic attack bonus + 64)` and an npc defends magic with its MAGIC level,
`(npc magic + 9) * (magicdefence + 64)` -- 18 of 20 Wind Blasts land on Chronozon at 99 Magic
(`build/quest_gate/s20mdr_after3`).

A Chronozon fight: `::setlevel magic` >= 41, runes, food and armour, cast each element until its own
"Chronozon weakens..." line, then keep casting until the slot leaves the pool; credit the kill by
the vile ashes.

#### A spell on a ground obj or a loc (seam23)

**A spell on a ground obj or a loc (seam23):** the target may be a `{kind='obj'|'loc', id=<symbol>}`
table, as `use_on` takes -- `t.player.cast('telegrab', {kind='obj', id='elid_key'})` (Telekinetic
Grab: level 33, 1 air + 1 law, 43 XP),
`t.player.cast('charge_water_orb', {kind='loc', id='obelisk_water'})`. An obj cast's `ok` means the
obj ARRIVED in the backpack, a loc cast's that Magic XP was paid; the detail carries the backpack
diff and chat lines.

The verb does NOT walk first (the server paths into spell range), so stand where the guide stands;
'Too late - it's gone!' is `refused` though the runes were spent. A `covered` whose detail says
`every press's menu held only Cancel` usually means the spell cannot target that kind (a combat
spell on an obj), not a camera problem. A second `await_dead_engaged` on a fight a CAST opened
answers `no_row ... -- cast <spell> again (t.player.cast)`.

The server now takes its map flag down the tick a cast or an in-range bow fires (LostCity
`unsetMapFlag`), so `t.player.idle()` goes true during a ranged fight (seam23).

#### A spell on a carried item (seam27)

A CARRIED ITEM (seam27): `t.player.cast(spell, {kind='held', id=<obj symbol>}, ticks=10)` --
Superheat Item `'superheat'`, `'low_alchemy'`/`'high_alchemy'`, `'enchant_1'`..`'enchant_6'`
(spellbook component names, never display names). It arms the spell, waits for the backpack to take
the sidebar, and presses the cell's `<spell> -> <item>` row (`api_drive.inv_cast`, OPHELDT). `ok` =
Magic XP paid AND the item's stack went down, detail naming the backpack diff; `refused` = the
server's line ('You need to cast superheat item on ore.') or a spell with no held-item bit;
`not_found` = not carried.

#### A spell with no target (seam28)

A SPELL WITH NO TARGET (seam28): `t.player.cast(spell)` or
`t.player.cast(spell, {kind='self'}, ticks)` presses the spellbook cell's own op 1 (IF_BUTTON1,
`[if_button,magic_spellbook:<spell>]`), never target mode. `ok` `TELEPORTED to x,z,l` when the
player leaves the tile (more than 2 tiles or another level); `no_runes` on magic.rs2's rune line;
`refused` quoting any other line with no move and no XP (Ardougne Teleport before Plague City: 'You
must have completed Plague City to use this spell.'; before the scroll is read: 'You havn't learnt
how to cast this spell yet.'); `ok` `no teleport` when only Magic XP was paid (Charge); `timeout`
otherwise (a target spell pressed this way -- give it a target). Read the landed tile from the
detail; never `goto_tile` where a teleport spell is the guide's step.

A press while the player is DELAYED is dropped by the server with nothing on screen (LostCity's
protected `[if_button]`; verbose `IF_BUTTONN ... refused: player is delayed (p_delay)`), so since
seam33 the self-cast re-presses the cell after `SELF_CAST_REPRESS_TICKS` (3) of total silence, up
to `SELF_CAST_PRESSES` (3) presses, and the detail ends `(press N: ...)` when it took more than one.
A cast right after a step that ends in `p_delay` (Lost City's Dramen chop) needs no `t.ticks`
before it. A cast on a HELD item still presses once (gaps-world: Leaving the Entrana dungeon).

#### A covered Attack press, a boss that teleports, a timed walk (seam35)

*Origin: seam pass 35, `covered_press_and_timed_lift_without_drive_op` (Haunted Mine, sonnet-b43).*

Never send a guide step through `t.drive.op` because a press answered `covered` or a window looked
short. Use these instead (all three are real presses):

- **A covered Attack press recovers itself.** When `t.player.attack`'s first press answers
  `covered` (a real press, not a pixel under the UI), the verb takes it again from a SETTLED camera
  (poses 1 and 4, the eye allowed to stop moving), and then from a clear tile two squares off a
  copy at the player's feet. Only after that does it run the old unsettled pose loop. The row says
  which rung landed: `player.attack: first press covered (...) -> settled camera: ... pressed at
  x,y`. Treus Dayth rises one row after a press that walked the player. He used to answer
  `covered` for 64 ticks and the character died; now he is pressed in 14 ticks
  (`build/quest_gate/s35cp_fix7_dayth_repro_cam`).
- **A teleporting boss is followed, not counted as killed.** `npc_tele` re-adds the npc under a
  NEW client slot (TORIRSSERVER_NPC_TRACE `RELEASE why=tele`). `await_dead_engaged` and
  `await_dead` now follow that new slot and press Attack again at once. The note reads
  `slot N left the pool ... came back as slot M ... -- followed it`. Before seam35 the old slot's
  absence was graded `ok` "corroborated by ABSENCE" while Dayth stood alive: always read the
  quest's own stage (`t.var.await`) after a kill.
- **A loc press whose walk outlasts the settle is followed** (verbs-pointer: `click_loc`). Haunted
  Mine's valve-to-lift route is 60 steps, and late in the quest the character walks it. The click
  now answers on the lift's own sentence (`click_loc: the walk outlasted the 20-tick settle;
  followed it N more tick(s)`), well inside the valve's 80 ticks. Write
  `t.exec("goDownLift", t.player.click_loc, "lift_side_r", 1)`. Do not follow it with a ::goto
  until the lift has answered: a teleport cancels the walk.

Dayth hits up to 15 every 4 ticks. A crane next to the player adds up to 10, and standing on the
track rows (`[proc,hmq_on_dayth_track]`) adds up to 9 every tick at low boss hitpoints. With 99
Ranged, a magic shortbow and 20-27 sharks and no prayer, seam35 won the fight in 2 of 7 runs (the
character died in the other five, three of them in the full quest). Bring Protect from
Missiles (the `.rs2` cuts his pickaxe hit by a third) or more food, and fight from off the track.

Arrg (Troll Romance, `trollromance_arrg_attackable`) is the OSRS wiki stat block (oldid=15215810:
140 hp, Attack 70, Strength 140, Defence 40, Ranged 70, +60/+100, a 4-tick attack, max hit 38 melee
and 30 ranged); LostCity's 2004 Arrg is weaker and the port took the OSRS form on purpose. With
75/75/75, no armour and 14 sharks the character died (matthew-mbp-m4-b47). With 85/85/85, a dragon
scimitar, a rune full helm, chainbody, platelegs and kiteshield worn from the start (the rune
platebody needs Dragon Slayer) and 16 sharks eaten below 50, Arrg died in 108-123 ticks after 6-9
sharks (matthew-mbp-m4-b49-seam1: `tlseam_arrg_kit`, `tlseam_copy_after`, `closer_tl_shared`).

## Turning on a protection prayer: there is no verb, use the tab, the widget and `invoke`

No `t.player.*` verb prays. Open the prayer tab, wait a tick, then invoke the prayer's widget with op 1:
`t.ui.tab("prayer")`, `t.ticks(1)`, `local _, w = t.ui.widget("prayerbook:prayer13")`, `t.ui.invoke(w, 1)`,
`t.ticks(2)`. Then read the prayer's varbit and write it into a `t.check` detail. The widgets are
`prayer13` Protect from Magic (`varb4116_prayer_protectfrommagic`), `prayer14` Protect from Missiles,
`prayer15` Protect from Melee (`varb4118_prayer_protectfrommelee`). Without the tick after the tab switch,
the invoke did nothing and the varbit stayed 0 (seam scratch `b54s3_queen_pray_b`). A green example is
`test/quests/thefremennikisles.lua` (`killTrolls-protectMelee`). `t.ui.invoke` answers a bare `ok`, so
the varbit read is the evidence.
