# TECHNIQUES: the Colosseum techniques named on the pinned wiki pages

Source: the pinned wiki pages under `docs/minigames/colosseum/sources/wiki/` (revision ids in `manifest.tsv`). A technique that works is a test: each row names the sentence that states it, the mechanic it rests on, and the thing the wave test must show. Grade: each sentence is a written guide (D, or C when two pages agree); the mechanic lines are the checkable part.

## Frem kill order and standing still

- Sentence: `Fortis_Colosseum_Strategies:1241` "Kill fremmies in the order: melee, mage, range. The only exception to this is to kill the ranger with a [[Saradomin godsword]] special attack before the mager if the player is critically low on HP due to a mistake. '''Barraging the non-melee fremmies is slow, inconsistent, and may indirectly get you"
- Rests on: `Fortis_Colosseum_Strategies:1237` "Fremmies attack on a fixed 6-tick cycle relative to wave start. It is not worth devoting extra clicks to pray against their attacks, especially when south spawns are present. '''Kill them ASAP''' and get to dealing with south spawns. '''If and only if you can "
- Rests on: `Fremennik_warband_berserker:48` "They are able to use routefinding, and as such will move around pillars to reach players. They attack on a fixed 6 tick cycle, only attacking when they are in melee distance and are not moving."
- The test must show: A killed berserker/seer/archer stops attacking at once; all three attack on the 6-tick cycle counted from wave start; hit-and-run staggers the cycle.

## Frem weakness = guaranteed max hit

- Sentence: `Fortis_Colosseum:62` "|Part of the Fremennik warband trio, guaranteed to spawn in every wave except for wave 12. Has routefinding and runs to where the player currently is. '''Hitting with magic attacks will always result in a max hit'''."
- Rests on: `Fortis_Colosseum_Strategies:678` "When using a combat style corresponding to their weakness, the player's attacks will always hit and for a [[maximum hit]]. The only exception is [[Ice Barrage]] on the berserker; a minimum magic attack bonus of +104 and [[Augury]] is needed to guarantee a free"
- The test must show: Berserker dies to one 48+ magic hit (HP 48); seer to ranged, archer to melee; the other styles roll normally.

## 5th-tick start click toward tile A/B

- Sentence: `Fortis_Colosseum_Strategies:1204` "# On the 5th game tick after the handicap selection menu disappears, click towards either tile A or tile B as shown on the NW pillar. Using RuneLite metronome plugins, or simply walking in place, can be used to time this properly."
- Rests on: `Fortis_Colosseum_Strategies:1231` "* By clicking towards the pillar on the 5th tick, NPCs are prevented from spawning in the west area, minimising the number of NPCs the player has to deal with at wave start."
- The test must show: Spawn selection depends on the player position at a fixed tick after the wave starts; a player on the NW start tile has no spawn west of the pillar.

## Offtick / same-tick south spawns (A/B tile method)

- Sentence: `Fortis_Colosseum_Strategies:1326` "This method has you stand still on either the A or B tile (except for the double south manticore spawn) to offtick double south. You need to stay on the tile until the rear NPC sees you."
- Rests on: `Fortis_Colosseum_Strategies:1350` "These spawns will always be offticked by staying on the A or B tiles. The NPC that spawns close will attack first, and the rear NPC will attack 2 (if on B) or 3 (if on A) ticks later. Focus on getting the prayers right, equip your weapons, and click the frems "
- The test must show: Ranged/magic NPCs that see the player on the same tick attack on the same tick; different distance gives a fixed offset of 2 or 3 ticks.

## Rotating north for double south

- Sentence: `Fortis_Colosseum_Strategies:1254` "This method has you rotate north to set up a familiar offtick with double south. In many cases, you will exploit the long 10-tick charging time a manticore needs before it fires at you."
- Rests on: `Fortis_Colosseum_Strategies:1205` "# As you move towards the pillar, look to the south to see if any NPCs spawned in the two south spawn locations, and pray accordingly. The [[serpent shaman]] has a much lower maximum hit than the [[Javelin Colossus]], which in turn has a lower max hit than the"
- The test must show: Manticore attack lands 10 ticks after it first sees the player; the player leaves its line of sight and the charge restarts.

## Manticore flick: pray before launch

- Sentence: `Fortis_Colosseum_Strategies:816` "'''Beware:''' Despite their visual similarity to the [[The Leviathan|The Leviathan's]] projectile attacks, '''you must pray accordingly against projectiles before they are launched''', as opposed to being able to pray against the projectiles while they are mid-flight."
- Rests on: `Manticore:41` "Upon spotting a player in attack range, the Manticore will proceed to charge up a triple attack with all three combat styles. It will either use a range-magic or magic-range as the first two hits; the last hit is always melee. These attacks are launched one ti"
- Rests on: `Fortis_Colosseum_Strategies:818` "The '''Mantimayhem 1''' modifier changes the Manticores damage output, causing 2 attacks per style for a total of 6, but not changing how you would handle prayer flicking them. These attacks roll separate accuracy checks and damage. While harmless if the playe"
- The test must show: Three orbs, one per tick, projectile travel time 0 ticks, last orb is melee unless Mantimayhem III; style picked at launch.

## Manticore stagger of the pair

- Sentence: `Fortis_Colosseum_Strategies:824` "In wave 9, 10 and 11, two manticores will appear. When one of these manticores has sight of the player, the other manticore that is within 15 tiles of them and also having sight on the player will copy the pattern of the first one. If the first manticore is killed before that happens, the second one"
- Rests on: `Manticore:47` "When a manticore attacks, any other manticore that is ready to attack (i.e., finished its full 10-tick charge-up) will have its attack delayed by 5 ticks. Manticores can overlap attacks on the player if one of them is not yet ready to attack when the other att"
- Rests on: `Manticore:45` "Starting from wave 9, manticores will begin spawning in pairs. If one manticore selects an attack pattern and the other is within 15 tiles of it with line of sight, the other manticore will copy its attack pattern."
- The test must show: A ready manticore delays the other by 5 ticks; the pair copies one pattern if within 15 tiles with line of sight.

## Do not let manticores charge

- Sentence: `Fortis_Colosseum_Strategies:1609` "Wherever possible, avoid letting east manticores gain line-of-sight until the south spawns and reinforcements are dealt with. Leaving the manticores in the stack uncharged makes it much easier to safely run to another pillar or destack."
- Rests on: `Fortis_Colosseum_Strategies:1205` "# As you move towards the pillar, look to the south to see if any NPCs spawned in the two south spawn locations, and pray accordingly. The [[serpent shaman]] has a much lower maximum hit than the [[Javelin Colossus]], which in turn has a lower max hit than the"
- The test must show: The 10-tick charge starts only on first sight; an uncharged manticore is harmless.

## Jaguar warrior safespot (NW pillar)

- Sentence: `Fortis_Colosseum_Strategies:1606` "Melee NPCs can be safespotted on NW pillar by hugging the west side of the pillar; if '''Red Flag''' is active then this will not work for minotaurs."
- Rests on: `Fortis_Colosseum_Strategies:1623` "You can use the jaguar safespot tiles from the tile pack above to safespot the reinforcement Jaguar Warrior as it spawns. Both tiles can be used; if you already have a ranger attacking you, you can use the tiles to dodge the sky javelins while praying against "
- Rests on: `Fortis_Colosseum:152` "|Has the highest single max hit of all the enemies in the Colosseum. If not in melee range of the player, it will attempt to heal other wounded enemies to full. Easily safespotted, unless the Red Flag modifier is active, which will give it routefinding abiliti"
- The test must show: Jaguar and minotaur have no routefinding (unless Red Flag), so a pillar blocks them; the minotaur (3x3) cannot use the jaguar tiles.

## Minotaur heal lure

- Sentence: `Minotaur_Fortis_Colosseum:43` "If the player is not within melee distance (including diagonally) the minotaur will attempt to heal other wounded monsters to full health within 6 tiles of it. Specifically the minotaur will heal another monster if:{{CiteReddit|author=Mod Arcane|url=https://www.reddit.com/r/2007scape/comments/1e90fk"
- Rests on: `Minotaur_Fortis_Colosseum:51` "* The monster's health is below 75%"
- Rests on: `Minotaur_Fortis_Colosseum:52` "* The monster's centre tile is within line of sight and no more than 7 tiles away from the minotaur's centre tile"
- The test must show: Minotaur out of melee scans for a non-minotaur below 75% HP within 7 tiles with centre-to-centre line of sight and heals it to full.

## Stand beside the minotaur spawn so the shaman spawns central

- Sentence: `Fortis_Colosseum_Strategies:878` "Generally, the hardest waves are waves 6, 8, 10, and 11 due to the large number of NPCs. The [[serpent shaman]] reinforcements in 4, 5, 6, 10, and 11 can be challenging as there is a chance for them to spawn in front of the melee NPC. There is an exception on waves 10 and 11; standing next to the mi"
- Rests on: `Serpent_shaman:44` "During waves 4, 5, 6, 10, and 11, if the player stands next to the [[Jaguar warrior]] or [[Minotaur (Fortis Colosseum)|Minotaur]] spawn point when reinforcements would have arrived, the serpent shaman will instead spawn in the centre of the arena. There is alm"
- The test must show: Reinforcement shaman spawn tile depends on the player's position at spawn time (waves 4-6, 10, 11).

## Tick-eating the minotaur

- Sentence: `Minotaur_Fortis_Colosseum:53` "Like Vardorvis, the minotaur can be tick eaten, as its damage is calculated one tick later than its attack animation despite being a melee attack. If standing next to a minotaur as it spawns to prevent it from healing on waves 10 and 11, the serpent shaman will spawn in the centre of the colosseum i"
- Rests on: `Fortis_Colosseum_Strategies:867` "Unlike most melee-using monsters, a minotaur's damage is calculated one tick after the melee attack animation."
- The test must show: Minotaur hit is applied one tick after its attack animation.

## Dodge the javelin special by moving

- Sentence: `Fortis_Colosseum_Strategies:788` "After every four auto-attacks, the colossus will launch a javelin into the air, which will proceed to land on the player's position 6 ticks afterwards. ''If they do not move out of the way'', they will take up to 40 [[typeless]] damage."
- Rests on: `Javelin_Colossus:40` "'''Javelin Colossi''' are enemies encountered in [[Fortis Colosseum]] who attack with ranged, throwing their javelins at the player with a range of 15 tiles. Every five attacks, they will launch a javelin high into the air, landing on the player's current posi"
- The test must show: Every fifth attack (after four autos) is a ground-targeted javelin landing 6 ticks later on the tile targeted; it ignores Protect from Missiles.

## Destack / pillar run

- Sentence: `Fortis_Colosseum_Strategies:1360` "Running to a different pillar is a flimsy survival strategy often used to cover for not learning frem procedure and panicking. In tanky armour such as [[Torva armour]], you can sometimes buy a few seconds of survival if you are lucky, but there are many downsides:"
- Rests on: `Fortis_Colosseum_Strategies:869` "Similarly to the [[Yt-MejKot]] of the Fight Caves, Minotaurs will heal other enemies if they have been damaged. However, the minotaur will continuously heal them to '''full health''' if they are within 6 tiles of them and in line of sight. Their line of sight "
- The test must show: 3x3 NPCs without routefinding bunch against a pillar; line of sight is computed from the centre tile.

## Stand on tile A as reinforcements spawn

- Sentence: `Fortis_Colosseum_Strategies:1626` "If dealing with a triangle or T stack, this prevents the reinforcements from getting caught on the stack."
- Rests on: `Fortis_Colosseum_Strategies:876` "If the wave is not cleared within 40 seconds, the reinforcement NPCs will spawn. Realistically, only waves 1, 2, and 4 can be cleared without letting any reinforcements spawn. If reinforcements spawn in the same tick that the wave is considered cleared, they w"
- The test must show: Reinforcements spawn 40 seconds after wave start from the north or south gate depending on the player's position.

## Reinforcement timer (plugin timer)

- Sentence: `Fortis_Colosseum_Strategies:1598` "* '''Fortis Colosseum''': displays wave information, a handicap overlay, and a timer (used to anticipate reinforcements spawning, which occurs 40 seconds after the wave starts)."
- Rests on: `Fortis_Colosseum:35` "Like the [[TzHaar Fight Cave]]s and [[Inferno]], players will face various enemies as they progress through the waves and their spawn locations are completely random. If the player does not complete a wave within 40 seconds, additional enemy reinforcements wil"
- The test must show: Reinforcements arrive 40 s (about 67 ticks) after wave start; if the wave clears on that tick they are killed automatically.

## Hover method (Sol)

- Sentence: `Fortis_Colosseum_Strategies:1453` "Based on Sol's previous attack, there will be a tile that is '''always safe from his next regular attack.''' See the video below for demonstrations."
- Rests on: `Fortis_Colosseum_Strategies:1431` "The attack patterns follow a rule in which if two of the same style, Spear attack or Shield attack, are used in a row, the second one will use a different pattern. Using a single Spear attack and following up with a Shield attack, or vice-versa, will reset the"
- Rests on: `Fortis_Colosseum_Strategies:1431` "The attack patterns follow a rule in which if two of the same style, Spear attack or Shield attack, are used in a row, the second one will use a different pattern. Using a single Spear attack and following up with a Shield attack, or vice-versa, will reset the"
- The test must show: Sol's pattern selection is a function of his previous attack: two of the same style in a row use pattern 2; a different style resets to pattern 1; a special attack resets; first attack after a phase is a Spear.

## L method (Sol)

- Sentence: `Fortis_Colosseum_Strategies:1477` "By moving in an L shape from Sol's corner, you will '''automatically dodge Spear 1, Spear 2, and Shield 2.''' This is '''always safe after a phase transition''', as Sol is guaranteed to use a Spear attack out of phasing."
- Rests on: `Fortis_Colosseum_Strategies:1431` "The attack patterns follow a rule in which if two of the same style, Spear attack or Shield attack, are used in a row, the second one will use a different pattern. Using a single Spear attack and following up with a Shield attack, or vice-versa, will reset the"
- The test must show: After a phase transition Sol uses a Spear, so Spear 1/Spear 2/Shield 2 tiles are fixed; Shield 1 must be reacted to.

## Delay Sol by walking away

- Sentence: `Fortis_Colosseum_Strategies:1433` "Sol will be unable to move for 4 ticks starting on the tick he uses an AOE attack. Additionally, Sol must be next to the player at the start of a tick (before movement is calculated) to initiate an AOE attack. If the player walks away from Sol on a tick before Sol finishes his attack cooldown, they "
- Rests on: `Fortis_Colosseum_Strategies:1433` "Sol will be unable to move for 4 ticks starting on the tick he uses an AOE attack. Additionally, Sol must be next to the player at the start of a tick (before movement is calculated) to initiate an AOE attack. If the player walks away from Sol on a tick before"
- The test must show: Sol attacks only when adjacent at the start of a tick; he cannot move for 4 ticks after an AoE; attack interval 7 (spear) / 6 (shield), minus 1 below 75%.

## Triple attack parry (pray on the tick before)

- Sentence: `Fortis_Colosseum_Strategies:1502` "Sol Heredit will charge up an attack and strike 3 times. This attack must be prayed against with [[Protect from Melee]], or you will receive heavy damage. The attack requires you to activate your prayer tick-perfectly with the attack landing. The initial attack hits 3 ticks after the start of the ch"
- Rests on: `Fortis_Colosseum_Strategies:1503` "*Activating any protection prayer too early will disable it and force you to take the damage."
- Rests on: `Fortis_Colosseum_Strategies:1502` "Sol Heredit will charge up an attack and strike 3 times. This attack must be prayed against with [[Protect from Melee]], or you will receive heavy damage. The attack requires you to activate your prayer tick-perfectly with the attack landing. The initial attac"
- The test must show: Hits land 3, 3, 3 ticks apart (3, 3, 4 from the 50% phase); an early prayer is removed and the hit lands; damage 15/25/35, then 15/30/45.

## Grapple parry (click the named slot)

- Sentence: `Fortis_Colosseum_Strategies:1526` "Sol Heredit will announce a message over his model and in the chat-box indicating one of 5 slots he will be targeting. If the player does not defend the armour slot in time, or clicks on the wrong slot, he will deal heavy damage (up to 45). Defending a gear slot will not unequip the item for the dur"
- Rests on: `Fortis_Colosseum_Strategies:1528` "*Countering the attack within 3 ticks will result in the chat message "''You successfully defend from Sol Heredit's grapple!",'' blocking the damage. Countering it on the last possible tick, however,  results in the chat message ''"You perfectly parry Sol Here"
- Rests on: `Sol_Heredit:96` "His grapple attack can only be performed below 75% HP; he will drop his shield and call out a body part, and the player will have 4 ticks to click on the item in the respective slot to parry the attack. If the item is not clicked on or an incorrect item is sel"
- The test must show: Five slots named by message; a click inside the window (3 or 4 ticks, the two pages disagree) blocks; the last tick is a perfect parry giving a guaranteed max hit within 5 ticks.

## Dodge the light beam

- Sentence: `Fortis_Colosseum_Strategies:1509` "*If your position is targeted, you have 3 ticks to react and move out of the way."
- Rests on: `Fortis_Colosseum_Strategies:1559` "Light beams '''fire much faster''' and more frequently, sending out an attack approximately every 7 seconds. '''You have only 2 ticks to react''' from the moment a beam targets your position. During the chaos of the fight, dodging the light beams must be of hi"
- The test must show: Beam targets the player's tile; 3 ticks to leave (2 in the enrage phase); the ball does 70+ damage.

## Solarflare skip

- Sentence: `Fortis_Colosseum_Strategies:1632` "At Solarflare I & II, the flare moves every two ticks between corners. Only the second tick is damaging; you can wait until the flare's true tile overlaps with yours, and immediately click away to move without taking any damage."
- Rests on: `Fortis_Colosseum_Strategies:1634` "At Solarflare III, the flare moves every tick between corners. You can skip it easily by clicking on its true tile as it is moving; by the time you move, the flare would have moved to the next tile."
- The test must show: Only the second tick of the flare's two-tick move damages; at III it moves every tick.

## Kill the totem with one manual spell

- Sentence: `Fortis_Colosseum_Strategies:1569` "*If '''Totemic''' is active, the totems will begin spawning when Sol Heredit reaches 50% of his health and will heal him for 75 hitpoints every 4.2 seconds if given the chance until destroyed. You can safely one-shot it with any manually casted spell; any weapon attack also works, but be careful of "
- Rests on: `Fortis_Colosseum_Modifiers:211` "They have 1 Hitpoint and will respawn two minutes after being destroyed, or after the enemy dies. Additionally, it will not heal the enemy if they are destroyed before their healing projectile reaches them."
- The test must show: Totem has 1 hit point; heals 30% (75 HP vs Sol) every few ticks (4.2 s vs Sol) after the enemy reaches 50%.

## Manual offtick of a reinforcement shaman

- Sentence: `Fortis_Colosseum_Strategies:880` "If this is not met and you are busy handling a south NPC and the shaman spawns in front, you may need to manually offtick it before it arrives. If it spawns behind, the melee NPC will block the shaman until it is un-safespotted or killed."
- Rests on: `Fortis_Colosseum_Strategies:1350` "These spawns will always be offticked by staying on the A or B tiles. The NPC that spawns close will attack first, and the rear NPC will attack 2 (if on B) or 3 (if on A) ticks later. Focus on getting the prayers right, equip your weapons, and click the frems "
- The test must show: A shaman that spawns in front of the melee reinforcement reaches the player first; the player steps off the pillar to shift its attack tick by one.

## Tile-pack and radius markers need the Red Flag npc id

- Sentence: `Fortis_Colosseum_Strategies:871` "If the handicap '''Red Flag''' is active, the minotaur will gain the ability to routefind and become capable of moving around pillars; other NPCs, such as the reinforcement serpent shamans in waves 10 and 11, will also be able to move into the minotaur's occupied tiles. Routefinding minotaurs have a"
- Rests on: `Fortis_Colosseum_Strategies:1586` "** 12813 Minotaur (with Red Flag active)"
- The test must show: Red Flag swaps the minotaur to id 12813 (12812 otherwise).

## Ice Barrage freeze threshold on the frems

- Sentence: `Fremennik_warband_berserker:44` "If attacking with [[Ice Barrage]], the player needs at least a +104 magic attack bonus with [[Augury]], or +140 without Augury, to guarantee a freeze. Another option is to wear [[Elite Void Knight equipment]] with at least +52 magic attack bonus and Augury."
- Rests on: `Fortis_Colosseum_Strategies:1206` "# Kill the fremmies. Start by killing the melee, followed by the mage and finally the ranger. You can use a [[Saradomin godsword]] special attack to get a large heal off the ranger if needed. It is best to kill them one by one, as fast as possible. Another goo"
- The test must show: Freeze needs +104 magic attack with Augury (+140 without); freeze is a normal accuracy roll against Magic 110, Defence 80 plus the style bonus.

