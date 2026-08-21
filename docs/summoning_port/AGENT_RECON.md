

> **Binding corrections for every report below:** an OSRS239 NPC_INFO v5 add carries a 16-bit
> per-client NPC index (`0xffff` terminator), then a 14-bit initial NPC definition. Definition ids
> 16384..65535 set the add's extended/update flag and use update-mask `0x1` to replace the
> definition in the same packet with a transformed unsigned 16-bit `p2Alt3` / `UShortLEAdd`
> value. Any older ceiling, free-run, tier, or id-budget conclusion based on the direct initial
> field is void. Also, a 727 CS2
> decompile is not trusted until raw instruction/operand and stack-effect disassembly has been
> preserved and an explicit 727 dialect has produced the readable form; relevant logic must then
> be rewritten as fresh osrs239 CS2. Separately, rev239 `LOC_ADD_CHANGE_V2` carries loc config ids
> in an exact 16-bit `p2Alt3`; the generic loc base 70000 truncates and must not be used for runtime
> placement. The Summoning obelisk maps source 28716 to target 62201.

===== RECON: scape-summoning-logic =====
# 2009scape Summoning — server behavioural spec (recon)

Root: `Server/src/main/content/global/skill/summoning/` (102 files). Package `content.global.skill.summoning[.familiar|.pet]`.

---

## 1. Familiar lifecycle

**Owners:** `familiar/Familiar.java` (919 L, abstract, `extends NPC implements Plugin<Object>`) = the entity + timer + special engine. `familiar/FamiliarManager.java` (541 L, one per `Player`) = ownership, summon gate, save/load, pets.

### Summon (pouch use)
- Entry: `familiar/SummonFamiliarPlugin.java:23` registers `ItemDefinition.setOptionHandler("summon", …)`. Gate at `:30` — **requires `Quests.WOLF_WHISTLE` complete** (unless `in-cutscene`). Calls `FamiliarManager.summon(item, false)`.
- `FamiliarManager.summon(Item,boolean pet,boolean deleteItem)` `:184-240`:
  1. `:186-193` if a familiar exists and `familiar.getPouchId() == item.getId()` → **renew** (timer reset only, no re-spawn); otherwise `"You already have a follower."`
  2. `:194` `ZoneRestriction.FOLLOWERS` + lock `"enable_summoning"` → `"This is a Summoning-free area."`
  3. `:206` **static** level ≥ `pouch.getLevelRequired()`; `:210` **dynamic** level (= summoning points) ≥ `pouch.getSummonCost()`
  4. `:215` `FAMILIARS.get(npcId)` (static registry, populated by `Familiar.newInstance` `:774-785` at plugin scan) → `fam.construct(player, npcId)`
  5. `:222` `getSpawnLocation() == null` → `"The spirit in this pouch is too big to summon here."`
  6. `:228` remove pouch; `:231` `updateLevel(SUMMONING, -summonCost, 0)`; `:232` `addExperience(SUMMONING, pouch.getSummonExperience())`
  7. `:235` `spawnFamiliar()` → `familiar.init()` + `openTab(new Component(662))` + `setViewedTab(7)`; renew path → `familiar.refreshTimer()` (`Familiar.java:455`)
  8. `:239` `player.getAppearance().sync()` (combat level changes)
- `Familiar.init(Location, boolean call)` `:204-220`: sets location (null → owner loc + `setInvisible(true)`), `super.init()`, `startFollowing()`, `sendConfiguration()`, `call()`, `openInfoBars()`, and **if in Wilderness zone → `transform()`** (`:449-453`, `transform(getOriginalId()+1)`).

### Tick / decay
`Familiar.handleTickActions()` `:227-279` (NPC tick, 600 ms):
- `ticks--`
- **Summoning-point drain** `:230-234`: `fracDrain += pointsPerTick`; if `>1.0 && ticks>0` drain 1 point. `pointsPerTick` computed once in ctor `:183-184`:
  `drain = pouch.getLevelRequired() - pouch.getSummonCost() + 1; pointsPerTick = drain / maximumTicks` — i.e. **total points spent over the familiar's life == its level requirement** (documented at `:126-135` with worked examples).
- `:235-240` every `ticks % 50 == 0`: `updateSpecialPoints(-15)` (**+15 special**, cap 60 → full bar in 200 ticks / 2 min) and forced chat `getText()`.
- `:241` `sendTimeRemaining()` (`:462-467`): `setVarbit(owner, 4534, ticks/100)` (minutes) and `setVarbit(owner, 4290, (ticks%100)>49 ? 1 : 0)` (half-minute).
- `:242-257` warnings at `ticks==100` ("1 minute"), `ticks==50` ("30 seconds"), `ticks==0` → BoB-drop message then `dismiss()`.
- `:258-270` **combat assist** (see §6).
- `:271-277` **recall**: if `!invisible && owner.dist > 12`, or `invisible && ticks%25==0` → `call()`; else if no pulse running → `startFollowing()`.
- `:278` `handleFamiliarTick()` hook (overridden by `Forager`, etc.).

### Dismiss
`Familiar.dismiss()` `:749-760`: `clear()`, `getPulseManager().clear()`, `owner.getInterfaceManager().removeTabs(7)`, `setFamiliar(null)`, varps `448=-1`, `1176=0`, `1175=182986`, `1174=-1`, `appearance.sync()`, `setViewedTab(3)`.
UI paths: `SummoningTabListener.kt:38-54` — iface **662 button 53**, opcode `155` → dialogue `"dismiss_dial"` (`familiar/DismissDialoguePlugin.java`, "Dismiss Familiar / Yes / No"; pets get "Free pet"), opcode `196` → immediate dismiss.

### Death
`Familiar.finalizeDeath(Entity killer)` `:672-675` → `dismiss()`. `BurdenBeast.dismiss()` `:53-67` **drops the whole container as GroundItems at the familiar's tile** (`500`-tick despawn, owner = player) before `super.dismiss()`.
Player death: `Player.java:711` `familiarManager.dismiss()` inside `finalizeDeath`.

### Teleport
No special handling — `call()` (`Familiar.java:700-736`) does `getProperties().setTeleportLocation(destination)` where `destination = RegionManager.getSpawnLocation(owner, this)`. The `dist > 12` check in the tick re-teleports the familiar to the owner after any player teleport. `onRegionInactivity()` `:397-400` also calls `call()`.

### Logout / login
- Logout: `Player.finishClear()` `:378-380` → `familiar.getFamiliar().clear()` (**not** `dismiss()`, so the BoB container survives).
- Save: `PlayerSaver.kt:269-300` `saveFamiliarManager` writes `familiarManager.familiar = {originalId, ticks, specialPoints, inventory (BoB only), lifepoints}` plus `petDetails` / `currentPet`.
- Load: `FamiliarManager.parse(JSONObject)` `:82-162` — reconstructs via `FAMILIARS.get(familiarId).construct(...)`, restores `ticks`, `specialPoints`, BoB `container.parse`, and `setAttribute("hp", …)` which `NPC.initConfig()` `:280-282` reads back into lifepoints.
- `FamiliarManager.login()` `:167-176`: `familiar.init()` if present; sets **varbit 4280** (orb visibility) = Wolf Whistle complete, **4281**=0, **4282**=7 (all packed into varp **1160**; comment at `:36-42` notes two are still unidentified).

### "Call familiar"
`SummoningTabListener.kt:11-17` — iface **662 button 51** → `familiar.call()`.
`Familiar.call()` `:700-736`: gets spawn location (null → returns false, caller sets invisible); `setInvisible(FOLLOWERS-restricted && !locked("enable_summoning"))`; teleport; for non-pets `playAudio(owner, Sounds.SUMMON_NPC_188)` + gfx **1315** if `size()>1` else **1314**; re-`startFollowing()` if attacking, else `face(owner)`. **No cost.**
`startFollowing()` `:662-670` = `MovementPulse(this, owner, Pathfinder.DUMB)` at `PulseType.STANDARD` + `face(owner)`.

---

## 2. Summoning points

- **Storage:** ordinary skill dynamic level. `Skills.SUMMONING = 23` (`core/game/node/entity/skill/Skills.java:57`). Static level = max points.
- **Not auto-restored:** `core/game/system/timer/impl/SkillRestore.kt:22` explicitly `continue`s for `PRAYER` and `SUMMONING`.
- **Consumed by:** summon cost (`FamiliarManager:231`), per-tick drain (`Familiar:233`), Unicorn Stallion "cure" option (`UnicornStallionNPC.java:84`, −2).
- **Obelisk recharge:** `ObeliskOptionPlugin.java` — scenery option **`"renew-points"`** (`:31-41`): if already full → message; else `visualize(Animation 8502, Graphics 1308)`, `playAudio(Sounds.DREADFOWL_BOOST_4214)`, `setLevel(SUMMONING, staticLevel)`, `player.dispatch(new SummoningPointsRechargeEvent(node))` (hooked by `content/region/fremennik/diary/FremennikAchievementDiary.kt:286`). Option **`"infuse-pouch"`** (`:28`) opens the creation UI.
- **Potions:** `content/data/consumables/Consumables.java:350` `SUMMONING(new Potion({12140,12142,12144,12146}, MultiEffect(RestoreSummoningSpecial(), SummoningEffect(7,0.25))))`; `:372` Super restore mix; `:398` `SC_SUMMONING` ({14277…14285}, no spec restore).
  - `data/consumables/effects/SummoningEffect.java` = `+ (base + staticLevel*bonus)` points, capped at static level.
  - `data/consumables/effects/RestoreSummoningSpecial.kt` = `familiar.updateSpecialPoints(-15)` (+15 special).
- **Combat-level contribution:** `Skills.java:488` `summoningCombatLevel = staticLevels[SUMMONING] / 8`, stored on `FamiliarManager` and added in `Properties.java:237-241`; `PlayerFlags530.kt:107-123` sends the split/combined combat level based on `familiarManager.isUsingSummoning`.

---

## 3. Pouches / infusion

**`SummoningPouch.java`** (585 L) — enum, 82 entries. Ctor `:93`: `(slot, pouchId, levelRequired, createExperience, npcId, summonExperience, summonCost, peaceful, Item... items)`; a second ctor `:105` adds a leading `boolean abyssal` (only `ABYSSAL_PARASITE_POUCH:217`, `ABYSSAL_TITAN_POUCH:392`). `POUCHES` map keyed by pouch item id; `forSlot(int)` for the UI slot.

Examples (line, slot, pouchItem, lvl, createXP, npcId, summonXP, summonCost, peaceful, ingredients):
```
:17  SPIRIT_WOLF     0  12047  1   4.8   6829 0.1  1 false  [12158 gold charm, 12155 pouch, 2859 wolf bones, 12183 x7 shards]
:22  DREADFOWL       1  12043  4   9.3   6825 0.1  1 false  [12158, 12155, 2138 raw chicken, 12183 x8]
:282 BUNYIP         47  12029  68  119.2 6813 1.4  7 true   [12159 green charm, 12155, 383 raw shark, 12183 x110]
:357 PACK_YAK       75  12093  96  422.4 6873 4.8 10 true   [12160 crimson charm, 10818 yak-hide, 12155, 12183 x211]
:402 STEEL_TITAN    76  12790  99  435.2 7343 4.9 10 false  [12160, 1119 steel platebody, 12155, 12183 x178]
:404-408 SACRED_CLAY_POUCH_1..5 (slot -1, Stealing Creation, 0 xp)
```
Ingredient vocabulary (names resolved from `Server/data/configs/item_configs.json`): `12155` Pouch, `12183` Spirit shards, charms `12158` Gold / `12159` Green / `12160` Crimson / `12163` Blue / `12161` Abyssal / `12162` Talon beast / `12164` Ravager / `12165` Shifter / `12166` Spinner / `12167` Torcher / `12168` Obsidian; plus one tertiary item per pouch.

**Infusion UI** — `SummoningCreator.java`:
- `configure(player, pouch)` `:63-67`: opens `Component(669)` (pouch) or `Component(673)` (scroll); `sendRunScript(757, "Iiissssss", POUCH_PARAMS)` / `sendRunScript(765, "Iiisssss", SCROLL_PARAMS)`; `sendIfaceSettings(190|126, 15, 669|673, 0, 78)`.
  - `POUCH_PARAMS` `:32` = `{"List","Infuse-X","Infuse-All","Infuse-10","Infuse-5","Infuse", 20, 4, 669<<16|15}`
  - `SCROLL_PARAMS` `:38` = `{"Transform-X","Transform-All","Transform-10","Transform-5","Transform", 20, 4, 673<<16|15}`
- `SummoningCreationPlugin.java` handles both components. Opcode→amount `:74-76`: `155→1, 196→5, 124→10, 199→28`, `234→` input dialogue (X), `166→ list` (`SummoningCreator.list` `:86-88` prints `CS2Mapping.forId(1186).getMap().get(pouchId)` — **the requirement text is a client CS2 string map, id 1186**). Slot fixup `slot > 50 ? slot-1 : slot`.
- `CreatePulse` `:94-182`: closes iface; static level ≥ `type.getLevel()`; animation **9068**; obelisk scenery hard-coded at `Location(2209, 5344, 0)` `:122` animating **8509** (start) / **8510** (stop); `playAudio(Sounds.CRAFT_POUCH_4164)`; per-item `remove(required)` → `add(product)` → `addExperience(SUMMONING, type.getExperience(), true)`.
- `SummoningNode.parse` `:283-289`: pouch → product `Item(pouchId,1)`, xp `createExperience`, level `levelRequired`; scroll → required `[pouch x1]`, product **`Item(scrollItemId, 10)`**, xp `scroll.getExperience()`, level `scroll.getLevel()`.
- **Alt entry:** `SummoningCreationPlugin.ObeliskHandler` `:83-111` — *use a pouch on an obelisk* (scenery ids `28716, 28719, 28722, 28725, 28278, 28731, 28734`) opens the **scroll** UI.

**XP split:** infuse → `pouch.createExperience` (per pouch); summon → `pouch.summonExperience` (per summon); special move → `scroll.getExperience()` (per scroll used, `Familiar.java:502`).

**Reverse exchange:** `content/region/kandarin/feldip/gutanoth/handlers/BogrogPouchSwapper.kt` — pouch → `ceil(shardsInRecipe * 0.7)` shards; scroll → `shardsInRecipe*0.7/20` shards. Shops: `Server/data/configs/shops.json` "Pikkupstix's Summoning Shop" / "Bogrog's Summoning Shop" stock `12183` (125k/65k) and `12155` (5000).

---

## 4. Scrolls / special moves

**`SummoningScroll.java`** (215 L) — enum, 82 entries, ctor `:126` `(slotId, itemId, xp, level, int... items)` where `items[0]` is the **pouch item id**. Lookups: `forId(slot)`, `forItemId(id)`, `forPouch(pouchId)` `:206-212`. Examples: `HOWL_SCROLL(0, 12425, 0.1, 1, 12047)`, `STEEL_OF_LEGENDS_SCROLL(76, 12825, 4.9, 99, 12790)`. Note `DOOMSPHERE_SCROLL(40,12455,5.8,58,-1)` has **pouch = -1** (Karamthulhu pouch 12023 does not map back) and `THIEVING_FINGERS_SCROLL(31,12426,47,47,12041)` has **xp = 47** — looks like a typo, real value 0.9.

**Creation:** 1 pouch → 10 scrolls (see §3). No separate scroll interface logic beyond component 673.

**Special execution** — `Familiar.executeSpecialMove(FamiliarSpecial)` `:473-505`:
1. `special.getNode() == this` → false
2. `specialCost > specialPoints` → "not enough special move points"
3. `SummoningScroll.forPouch(pouchId)`; must have ≥1 in inventory → "not enough scrolls"
4. owner→familiar distance > 15 → "too far away, or it cannot see you"
5. `specialMove(special)` (abstract, per-familiar) — if true: `setAttribute("special-delay", GameWorld.getTicks()+3)`, remove 1 scroll, `playAudio(Sounds.SPELL_4161)`, `visualizeSpecialMove()` (default: owner anim **7660** + gfx **1316**, `:510-512`), deduct `specialCost` unless attribute `"infinite-special-move"`, `addExperience(SUMMONING, scroll.getExperience(), true)`.

**Energy bar:** `specialPoints` field defaults **60** (`:84`), max 60 (`updateSpecialPoints` `:766-772`), `+15` per 50 ticks, written to **varp 1177**. Per-familiar `specialCost` is the 5th ctor arg (values seen: 1,3,4,6,8,12,20); pushed to the client as **varp 1175 = specialCost << 23** (`sendConfiguration` `:688-694`).

**`FamiliarSpecial.java`** (126 L) — plain DTO: `{Node node, int interfaceID, int component, Item item}`. Constructed at three sites:
- `SummoningTabListener.kt:57` — any 662 button other than 51/53/67 → `FamiliarSpecial(player)` (self-targeted specials)
- `core/net/packet/PacketProcessor.kt:449-453` — the **spell-cast packet family** with `iface == 662`: `FamiliarSpecial(target, iface, child, target as? Item)`. So the summoning tab behaves like a spellbook: pick a scroll button, then click an NPC / player / ground item / inventory item.
- `DreadfowlNPC.java:51`, `GiantChinchompaNPC.java:50` (self-triggered)

---

## 5. Beast of Burden

**`familiar/BurdenBeast.java`** (212 L, abstract `extends Familiar`):
- `protected Container container` sized by ctor arg `containerSize` `:35-38`, registered with `BurdenContainerListener(owner)`.
- Sizes (from the `super(owner,id,ticks,pouch,specCost,size,style)` calls): ThornySnail **3**, SpiritKalphite **6**, AbyssalParasite/AbyssalLurker/AbyssalTitan **7**, BullAnt **9**, SpiritTerrorbird **12**, WarTortoise **18**, PackYak **30**, and **`Forager` always 30** (`Forager.java:34`).
- `isAllowed` `:85-109`: value > 50 000 → refuse; untradeable → refuse; rune/pure essence (`1436`/`7936`) refused unless `pouch.abyssal`; `ItemConfigParser.BANKABLE` respected; abyssal pouches **only** accept unnoted essence.
- `transfer(item, amount, withdraw)` `:117-157` — `Forager` subclasses are **withdraw-only** (`:118-121`).
- `withdrawAll()` `:162-180`; `openInterface()` `:185-202`.
- `isPoisonImmune()` → true `:74-77`.
- `dismiss()` `:53-67` — closes 671 if open, **spills the whole container to the ground**, `container.clear()`.

**Interfaces:** main **671** (familiar side, 30 slots at child 27, "Withdraw-1/5/10/All/X/Examine"), single-tab **665** (player inventory side, child 0, "Store-1/5/10/All/X/Examine") — see `openInterface()` `:190-201` and the `InterfaceContainer.generateItems` calls.
**`familiar/BurdenInterfacePlugin.java`** registers `ComponentDefinition.put(665/671)`; opcode map `155→1, 196→5, 124→10, 199→all, 234→X, 168→examine`; `button == 29` on iface 671 = *withdraw all*.
**`familiar/BurdenContainerListener.java`** — pushes `ContainerPacket` with `containerId = -2 / 30` (`ContainerContext(player, -1, -2, 30, …)`).
Tab shortcut: `SummoningTabListener.kt:18-37` — **662 button 67** = "take BoB" → `beast.withdrawAll()` with distance/invisibility guards.
NPC options `"store"`/`"withdraw"` → `openInterface()` (`FamiliarNPCOptionPlugin.java:50-57`).
Core integration: `core/api/ContentAPI.kt:364, 404, 428, 463, 1740, 2313-2347` (`Container.BoB` enum member, `dumpBeastOfBurden`, item-count sweeps).

---

## 6. Familiar combat

Familiars **are** NPCs (`Familiar extends NPC`) and full combat entities.
- `combatFamiliar` is derived, not declared: `Familiar.java:167` `NPCDefinition.forId(getOriginalId()+1).getName().equals(getName())` — i.e. *a familiar is a combat familiar iff `id+1` is its wilderness-combat variant with the same name*. **This is a hard cache dependency.**
- `isPeacefulFamiliar()` `:846-848` = `pouch.getPeaceful()` (the 8th enum field; true for Beaver, Void Spinner, Macaw, Magpie, Spirit Terrorbird, Ibis, War Tortoise, Bunyip, Fruit Bat, Unicorn Stallion, Pack Yak).
- **Auto-assist** `:258-270`: only when `!isInvisible()`, familiar not already attacking, owner attacking/in combat, victim not invisible, **and all three of familiar/owner/victim are in `isMultiZone()`**, and `isCombatFamiliar() && !isBurdenBeast() && !isPeacefulFamiliar()`.
- **Familiar as target** `isAttackableBy` `:356-395`: owner can never attack own familiar; a player attacker must be in wilderness, the *owner* must be in wilderness, `owner.isAttackable(attacker)`, attacker must be in multi, familiar must be in multi.
- **Familiar as attacker** `canAttackTarget` `:311-344` (mirror rules) and `canAttack(target,message)` `:570-590`: target within **8 tiles**, players require owner in multi, requires `isCombatFamiliar()`.
- `isOwnerAttackable()` `:635-641`: "Your familiar cannot fight whilst you are not in combat."
- `canCombatSpecial` `:605-620`: `canAttack` + `isOwnerAttackable` + `special-delay` attribute + **Slayer-task gate** (target with `task.levelReq > owner slayer level` blocks the special).
- Damage helper `sendFamiliarHit(target, maxHit, gfx)` `:521-547`: impact delay `2 + floor(dist*0.5)`, `setNextAttack(4)`, credits the hit to **the owner** (`target.getImpactHandler().handleImpact(owner, …)`), Slayer-level neutralisation. Mirrored in `NPC.java:399-405`.
- Default `getCombatStyle()` = `CombatStyle.MAGIC` `:647-649`; the ctor's `attackStyle` (`WeaponInterface.STYLE_*`) drives XP style only (`getAttackStyle()` `:908`).
- Follow distance: recall threshold **12 tiles**; special-move range **15**; attack range **8**.

---

## 7. Familiar behaviour categories (≈60 NPC classes)

1. **Beasts of burden** (`extends BurdenBeast`, 8 direct): `ThornySnailNPC`, `SpiritKalphiteNPC`, `AbyssalParasiteNPC`, `AbyssalLurkerNPC`, `AbyssalTitanNPC.kt`, `BullAntNPC`, `SpiritTerrorbirdNPC`, `WarTortoiseNPC`, `PackYakNPC`. §5.
2. **Foragers** (`extends Forager`, 15): `AlbinoRatNPC, BeaverNPC, CockatriceFamiliarNPC, CompostMoundNPC, DesertWyrmNPC, EvilTurnipNPC, FruitBatNPC, GiantEntNPC, GraniteCrabNPC, GraniteLobsterNPC, IbisNPC, MacawNPC, MagpieNPC, StrangerPlantNPC, VoidFamiliarNPC`. `Forager.java:52-60`: every `random(100,440)` ticks, `random(11) < 4` chance to `produceItem()` into a 30-slot **withdraw-only** container.
3. **Invisible skill boosts** (`boosts: List<SkillBonus>`, read via `Familiar.getBoost` / `ContentAPI.getFamiliarBoost`): Hunter +7 `ArcticBearNPC:34`, +5 `SpiritKyattNPC:30` `SpiritLarupiaNPC:33` `SpiritGraahkNPC.kt:24` `WolpertingerNPC:32`; Woodcutting +2 `BeaverNPC:53`; Mining +1 `DesertWyrmNPC:53`, +7 `ObsidianGolemNPC:31`, +10 `LavaTitanNPC:30`; Farming +1 `DreadfowlNPC:76`; Firemaking +3 `PyreLordNPC:54`, +4 `ForgeRegentNPC:54`, +10 `LavaTitanNPC:31`; Fishing +1 `GraniteCrabNPC:41`, +3 `IbisNPC:44`, +4 `GraniteLobsterNPC:41`; Thieving +3 `MagpieNPC:39`.
4. **Healers / sustain**: `BunyipNPC` (passive `heal(2)` every 25 ticks, `tick()` `:62-75`; poison-immune; special converts raw fish to food-heal; use-fish-on-familiar → water runes `:130-155`), `UnicornStallionNPC` (special heals 15 % max LP; NPC option `"cure"` cures poison for 2 summoning points `:67-84`), `ElementalTitanNPC` (abstract base for Fire/Moss/Ice titans: +12.5 % Defence and heal 8 with over-heal `:20-38`), `VampireBatNPC`, `VoidFamiliarNPC` (Void Shifter teleports owner to `2659,2658` below 10 % LP `:146-152`).
5. **Combat specials** (`sendFamiliarHit` / manual hits): `AbyssalParasiteNPC, ArcticBearNPC, BarkerToadNPC, CockatriceFamiliarNPC, DesertWyrmNPC, EvilTurnipNPC, GeyserTitanNPC, GraniteLobsterNPC, MinotaurFamiliarNPC, SpiritJellyNPC, SpiritKalphiteNPC, SpiritLarupiaNPC, SpiritTzKihNPC, StrangerPlantNPC`. AoE variants: `SmokeDevilNPC` (radius 1, 0-6), `GiantChinchompaNPC` (radius 6, 0-13, **then self-dismiss** `:50-64`).
6. **Charge / next-hit modifiers** (`charged` flag + `adjustPlayerBattle(BattleState)`): `SpiritScorpionNPC:47-56` (poisons the next accurate ranged hit), `SteelTitanNPC` / `IronTitanNPC` (`specialMove` sets a `specialMove` charge flag, gfx 1449/1450).
7. **Stat-drain / stat-boost specials**: `SpiritJellyNPC` (−3 target Attack), `SpiritLarupiaNPC` (−1 target Strength), `EvilTurnipNPC` (−1 target Magic), `AbyssalLurkerNPC` (+4 Agility, +4 Thieving), `GraniteCrabNPC` (+4 Defence), `WarTortoiseNPC` (+9 Defence over-boost), `ObsidianGolemNPC` (+9 Strength), `WolpertingerNPC` (+7 Magic), `MagpieNPC` (+2 Thieving), `SpiritTerrorbirdNPC` (+2 Agility).
8. **Item-generation specials**: `MacawNPC` (herbcall, 100-tick internal cooldown), `FruitBatNPC` (fruitfall, 3×3 ground spawn), `SpiritSpiderNPC` (`createEggs`), `SpiritCobraNPC` (egg→hatched egg swap on inventory item), `CompostMoundNPC` (fills a Compost Bin, 1/10 supercompost), `AlbinoRatNPC` (cheese).
9. **Banking specials**: `PackYakNPC` (winter storage — notes+banks one item, with an explicit dupe-recovery path and `PlayerMonitor.log(DUPE_ALERT)` `:50-80`), `AbyssalTitanNPC.kt:39-80` (essence shipment — banks all rune/pure essence from inventory **and** its own container).
10. **Remote viewing**: `MacawNPC` + `RemoteViewer.java` (camera pan to a location, `HEIGHT = 1000`, dialogue key `"remote-view"`) + `RemoteViewDialogue.java`.
11. **Teleport-option familiars**: `SpiritKyattOptionPlugin.java`, `SpiritGraahkOptionPlugin.kt`, `LavaTitanOptionPlugin.java` (each registers `option:interact` on the NPC → a dialogue offering a hunting-area teleport; Kyatt also owns scenery 28741/28743/14910/14912 around the summoning obelisk area).
12. **Pets** (`pet/` — a separate lifecycle sharing `Familiar`): `Pet.java` (`super(owner, id, -1, -1, -1)` → **no timer, no drain, no special**; hunger/growth ticked in `handleTickActions` `:66-120`; runs away at hunger ≥ 100; varp 1175 = `(growth<<1)|(hunger<<9)`), `Pets.java` (419 L enum: baby/grown/overgrown item ids, 3 npc ids, growth rate, summoning level, food list), `PetDetails.java`, `IncubatorEgg/IncubatorHandler.kt/IncubatorTimer.kt` (`PersistTimer("incubation")`, per-region eggs), `KittenInteractDialogue.java`.

---

## 8. Server-side player state required

| Kind | Value | Written where |
|---|---|---|
| Skill | `Skills.SUMMONING = 23` (static = max points, dynamic = current points); **excluded from auto-restore** | `Skills.java:57`, `SkillRestore.kt:22` |
| Derived | `FamiliarManager.summoningCombatLevel = staticLevel/8`; `hasPouch` flag; `isUsingSummoning()` | `Skills.java:488`, `FamiliarManager:501-519` |
| Save (JSON) | `familiarManager.familiar = {originalId, ticks, specialPoints, lifepoints, inventory[]}`; `familiarManager.petDetails = {itemId: [{hunger, growth}]}`; `familiarManager.currentPet` | `PlayerSaver.kt:269-300`, `FamiliarManager.parse:82-162` |
| Timers (in-memory) | `Familiar.ticks` (down-counter), `maximumTicks`, `fracDrain`, `specialPoints` (0-60), `special-delay` attribute, `charged`, `Forager.passiveDelay`, `MacawNPC.specialDelay`, `Pet.hasWarned` | `Familiar.java:74-148` |
| Varps | **448** = pouch item id (pet: pet item id), −1 on dismiss · **1174** = familiar NPC id, −1 on dismiss · **1175** = `specialCost << 23` (pet: `growth<<1 \| hunger<<9`), `182986` on dismiss · **1176** = 0 on dismiss (purpose unidentified) · **1177** = special points (0-60) · **1178** = Wolf Whistle trapdoor state | `Familiar.java:688-694, 749-760, 766-772`; `Pet.java:59-64`; `SummoningTrainingRoom.java:89,98,104` |
| Varbits | **4534** = minutes remaining (`ticks/100`) · **4290** = half-minute flag · **4280** = summoning orb visible (Wolf Whistle) · **4281** = 0 · **4282** = 7 (4280-4282 pack into varp **1160**) | `Familiar.java:462-467`; `FamiliarManager.java:40-42, 173-175` |
| Interfaces | **662** summoning sidebar tab (slot 7; buttons 51=call, 53=dismiss, 67=take-BoB, else=special) · **747** summoning orb (window slot 16 resizable / 73 fixed) · **669** infuse-pouch · **673** transform-scroll · **671** BoB container · **665** BoB inventory side · CS2 runscripts **757** / **765** · CS2 string map **1186** (pouch requirement text) | `SummoningTabListener.kt`, `InterfaceManager.java:397,422,442`, `SummoningCreator.java:43-67`, `BurdenBeast.java:190-201` |
| Attributes | `"infinite-special-move"`, `"special-delay"`, `"hp"` (familiar LP restore), `"fruit-bat"`, `"petrate"`, `"has-key"` | various |
| Locks / zones | lock `"enable_summoning"`; `ZoneRestriction.FOLLOWERS` (Duel arena, MTA, Pest Control lander, Fight Caves, Castle Wars, Clan Wars, Fishing Trawler, Bounty Hunter, all random events) → `MapZone.java:74` sets familiar invisible on enter | `Familiar.java:708`, `FamiliarManager.java:194` |
| Quest gate | `Quests.WOLF_WHISTLE` required to summon at all; grants 276 Summoning xp and sets varbit 4280 | `SummonFamiliarPlugin.java:30`, `WolfWhistle.java:169-172` |
| Sounds | `SUMMON_NPC_188` (call), `SPELL_4161` (special), `CRAFT_POUCH_4164` (infuse), `DREADFOWL_BOOST_4214` (renew points), `HEALING_AURA_4372` | as cited |

---

## Full file inventory (grouped)

### Skill root — `summoning/` (8)
| File | Purpose |
|---|---|
| `SummoningPouch.java` | 82-entry enum: pouch id, level, create/summon XP, npc id, summon cost, peaceful flag, ingredient list, `abyssal` flag |
| `SummoningScroll.java` | 82-entry enum: scroll item id, XP, level, source pouch id; `forPouch`/`forItemId`/`forId` lookups |
| `SummoningCreator.java` | Infuse/transform UI configuration + `CreatePulse` skill pulse + `SummoningNode` recipe wrapper |
| `SummoningCreationPlugin.java` | ComponentPlugin for ifaces 669/673 (button/opcode → amount); nests `ObeliskHandler` (use pouch on obelisk) |
| `ObeliskOptionPlugin.java` | Scenery options `"infuse-pouch"` and `"renew-points"` (point recharge + `SummoningPointsRechargeEvent`) |
| `SummoningTabListener.kt` | Iface 662 buttons: 51 call, 53 dismiss, 67 take-BoB, else special move |
| `SummoningTrainingRoom.java` | Wolf Whistle quest area: trapdoor/ladders (varp 1178), Fluffy cutscene, Pikkupstix training room |
| `CarvedEvilTurnipListener.kt` | Knife + evil turnip (12134) → carved evil turnip (12153), the Evil Turnip pouch tertiary |

### Core familiar framework — `summoning/familiar/` (12)
`Familiar.java` (abstract base: lifecycle, timer, point drain, special engine, combat rules, varps) · `FamiliarManager.java` (per-player owner: summon/dismiss/renew, pet details, save-parse, login varbits) · `FamiliarSpecial.java` (DTO for special-move context) · `BurdenBeast.java` (container familiar base) · `BurdenContainerListener.java` (container→client packet, id −2/30) · `BurdenInterfacePlugin.java` (ifaces 665/671 store/withdraw) · `Forager.java` (passive item-producing 30-slot BoB, withdraw-only) · `SummonFamiliarPlugin.java` (item `"summon"` option, Wolf Whistle gate, Falador diary hook) · `FamiliarNPCOptionPlugin.java` (NPC options pick-up/interact/interact-with/store/withdraw) · `FamiliarItemOptionPlugin.java` (pet item drop/release) · `FamiliarDialoguePlugin.java` (generic "interact-with" chatter, key 343823) · `FamiliarFeedPlugin.java` (food→pet use-with, ~160 pet NPC ids) · `DismissDialoguePlugin.java` (`dismiss_dial` yes/no) · `RemoteViewer.java` + `RemoteViewDialogue.java` (camera pan mechanic).

### Familiar NPCs — `summoning/familiar/*NPC.*` (~62)
Beasts of burden: `ThornySnailNPC, SpiritKalphiteNPC, AbyssalParasiteNPC, AbyssalLurkerNPC, AbyssalTitanNPC.kt, BullAntNPC, SpiritTerrorbirdNPC, WarTortoiseNPC, PackYakNPC`.
Foragers: `AlbinoRatNPC, BeaverNPC, CockatriceFamiliarNPC (7 inner classes), CompostMoundNPC, DesertWyrmNPC, EvilTurnipNPC, FruitBatNPC, GiantEntNPC, GraniteCrabNPC, GraniteLobsterNPC, IbisNPC, MacawNPC, MagpieNPC, StrangerPlantNPC, VoidFamiliarNPC (4 inner classes)`.
Combat/utility: `SpiritWolfNPC, DreadfowlNPC, SpiritSpiderNPC, SpiritMosquitoNPC, SpiritScorpionNPC, SpiritTzKihNPC, GiantChinchompaNPC, VampireBatNPC, HoneyBadgerNPC, BloatedLeechNPC, PyreLordNPC, SpiritJellyNPC, SpiritKyattNPC, SpiritLarupiaNPC, SpiritGraahkNPC.kt, KaramthulhuOverlordNPC, SmokeDevilNPC, SpiritCobraNPC, BarkerToadNPC, BunyipNPC, RavenousLocustNPC, ArcticBearNPC, ObsidianGolemNPC, PrayingMantisNPC, TalonBeastNPC, HydraNPC, SpiritDagannothNPC, UnicornStallionNPC, WolpertingerNPC, ForgeRegentNPC, SpiritPengatriceNPC, MinotaurFamiliarNPC (6 inner classes), ElementalTitanNPC (abstract), FireTitanNPC, MossTitanNPC, IceTitanNPC, LavaTitanNPC, SwampTitanNPC, GeyserTitanNPC, IronTitanNPC, SteelTitanNPC`.
Companion plugins/dialogues: `LavaTitanOptionPlugin.java`, `LavaTitanDialogue.java`, `SpiritKyattOptionPlugin.java`, `SpiritKyattDialogue.java`, `SpiritGraahkOptionPlugin.kt`, `SpiritGraahkDialogue.kt`, `BeaverDialogue.java`.

### Pets — `summoning/pet/` (7)
`Pet.java` (Familiar subclass with hunger/growth instead of a timer) · `PetDetails.java` (hunger/growth doubles) · `Pets.java` (419 L enum of every pet: item/npc ids per stage, growth rate, level, food) · `KittenInteractDialogue.java` · `IncubatorEgg.java` (egg→product table) · `IncubatorHandler.kt` (egg-on-incubator, scenery 28550/28352/28359) · `IncubatorTimer.kt` (`PersistTimer("incubation")`, 500-tick, per-region).

### Cross-references outside the tree (load-bearing)
`core/game/node/entity/player/Player.java:41,242,378-380,711,1268` · `core/game/node/entity/npc/NPC.java:280-282,399-405` · `core/game/node/entity/skill/Skills.java:57,153,488` · `core/game/node/entity/impl/Properties.java:220-241` · `core/game/world/update/flag/PlayerFlags530.kt:107-123` · `core/game/node/entity/player/info/login/PlayerSaver.kt:269-300` · `core/game/node/entity/player/link/InterfaceManager.java:44,362,397,414-425,442` · `core/net/packet/PacketProcessor.kt:449-453` · `core/api/ContentAPI.kt:364,404,428,463,1740,2313-2360` · `core/game/world/map/zone/MapZone.java:74` · `core/game/event/Events.kt:48` + `core/api/Event.kt:36` · `core/game/system/timer/impl/SkillRestore.kt:22` · `content/data/consumables/Consumables.java:350,372,398` + `effects/SummoningEffect.java`, `effects/RestoreSummoningSpecial.kt` · `content/region/asgarnia/taverley/quest/WolfWhistle.java` · `content/region/kandarin/feldip/gutanoth/handlers/BogrogPlugin.java`, `BogrogPouchSwapper.kt` · `content/global/skill/skillcapeperks/SkillcapePerks.kt:126` (`PET_MASTERY`) · `Server/data/configs/shops.json` (Pikkupstix / Bogrog / Pet Shop) · `Server/src/test/kotlin/content/familiar/special/BloodDrainTests.kt` (the **only** summoning test).

---

## RISKS / UNKNOWNS

1. **`combatFamiliar` is inferred from the cache**, not declared: `NPCDefinition.forId(originalId+1).getName().equals(getName())` (`Familiar.java:167`). If the osrs239 cache has no `npcId+1` wilderness variants (it has no summoning NPCs at all → **ABSENT**), every familiar will be classified non-combat and `transform()` becomes a no-op. This needs an explicit per-familiar flag in the ported data.
2. **Two varbits in varp 1160 are unidentified** — `FamiliarManager.java:41-42` names them `VARBIT_SUMMONING_UNKNOWN1 = 4281` (written 0) and `UNKNOWN2 = 4282` (written 7). Also **varp 1176** is only ever written 0 on dismiss; its meaning is unknown. Copying the magic values blind is the only known-good option.
3. **Varp 1175 dismiss value `182986`** and **`specialCost << 23`** are unexplained magic. `182986 = 0x2CACA`; `specialCost<<23` overlaps nothing else in the varp — GUESS: the client packs cost + pet growth/hunger into disjoint bit ranges of one varp, and 182986 is a "cleared" bit pattern. Needs cache-side CS2 verification.
4. **All interface ids (662, 669, 671, 673, 747), runscripts (757, 765), and the CS2 string map 1186 are rev-530-specific.** None of them exist in osrs239 — **ABSENT**. Either the 530 interface defs get transcoded into the osrs239 cache or the whole UI must be re-authored against osrs239 IF3 layouts.
5. **Client-side rendering of familiars requires ~120 NPC defs, ~60 models, the summon gfx (1314/1315), special gfx 1316, anim 7660, and per-familiar anims** — all ABSENT from osrs239 and all needing transcode from the rev-530 cache.
6. **`SummoningScroll.THIEVING_FINGERS_SCROLL(31, 12426, 47, 47, 12041)`** — xp field is `47`, almost certainly a typo for `0.9`; and **`DOOMSPHERE_SCROLL(40, 12455, 5.8, 58, -1)`** has pouch `-1`, so `SummoningScroll.forPouch(12023)` (Karamthulhu) returns null and that familiar's special will always error out ("Invalid scroll for pouch"). Do not port these two verbatim.
7. **`SummoningCreator.CreatePulse` hard-codes the obelisk scenery at `Location(2209, 5344, 0)`** (`:122`) for the infuse animation — it will misbehave at any other obelisk. A port should look up the actual scenery the player interacted with.
8. **`ObeliskHandler` id list (`SummoningCreationPlugin.java:88,94`) contains typos** vs `SummoningPouch`: `2067` (should be `12067`), `12064`, `12781`, `12710` are not pouch ids. Do not treat that list as authoritative.
9. **Summoning-point drain math is a 2009scape invention** (documented at `Familiar.java:126-185`, GL #1903), not a Jagex-verified formula. It is self-consistent but is a design decision the port inherits.
10. **`FamiliarManager.FAMILIARS` is a static `HashMap` populated by plugin scan** — the port needs an equivalent registration mechanism (a table keyed by npc id → constructor) since 3draster has no `Plugin`/`ClassScanner` equivalent in the same shape.
11. **Multiway/wilderness gating depends on `isMultiZone()`, `SkullManager.isWilderness()`, and `ZoneRestriction.FOLLOWERS`.** Whether ToriRSServer has equivalents is unverified by this recon — if absent, familiar PvP rules degrade to "always allowed" or "never allowed".
12. **Pet lifecycle is entangled with familiar lifecycle** (`Pet extends Familiar`, `FamiliarManager` owns both, one save blob). A summoning-only port must decide whether to carry the pet subsystem or stub it; `FamiliarManager.parse` will NPE on `petDetails` if it is dropped carelessly.
13. **`BurdenBeast.dismiss()` drops items to the ground** — but logout uses `clear()`, not `dismiss()`. If the ported logout path calls dismiss, players lose BoB contents on every logout. This asymmetry is easy to get wrong.
14. Only one test exists (`BloodDrainTests.kt`); there is **no coverage of summon/dismiss/drain/BoB/infusion** to port as a correctness oracle.
15. `Server/src/main/content/global/skill/summoning/` also holds Stealing Creation (`SACRED_CLAY_POUCH_*`, `CLAY_DEPOSIT_SCROLL_*`) and Phoenix (`PHOENIX_POUCH`, item 14623/npc 8575) entries that are **not core summoning** — they will pull in unrelated minigame content if ported wholesale.

===== RECON: scape-summoning-data-ids =====
# 2009scape Summoning — DATA / PORT MANIFEST

Source root: `/Users/matthewevers/Documents/git_repos/2009scape` (all paths below are repo-relative to that unless prefixed `3draster/`).

Primary files:

| File | Lines | Contains |
|---|---|---|
| `Server/src/main/content/global/skill/summoning/SummoningPouch.java` | 585 | 83 enum rows: 78 familiar pouches + 5 sacred-clay |
| `Server/src/main/content/global/skill/summoning/SummoningScroll.java` | 215 | 82 enum rows, 67 distinct scroll item ids |
| `Server/src/main/content/global/skill/summoning/familiar/Familiar.java` | 919 | base class: varps, gfx/anim constants, drain formula |
| `Server/src/main/content/global/skill/summoning/familiar/*.java|.kt` | 87 files | per-familiar ticks / special cost / BoB slots / anims |
| `Server/src/main/content/global/skill/summoning/SummoningCreator.java` | 304 | infusion interfaces, runscripts, accessmasks |
| `Server/src/main/content/global/skill/summoning/SummoningCreationPlugin.java` | 113 | obelisk use-with, component button map |
| `Server/src/main/content/global/skill/summoning/ObeliskOptionPlugin.java` | 52 | Renew-points / Infuse-pouch |
| `Server/src/main/content/global/skill/summoning/SummoningTabListener.kt` | 67 | interface 662 buttons |
| `Server/src/main/content/global/skill/summoning/pet/Pets.java` | 419 | 85 pet rows |
| `Server/data/configs/npc_configs.json` | — | combat stats + anims for all 166 familiar NPC ids |
| `Server/data/configs/drop_tables.json` | 435 entries | per-NPC `"charm"` sub-table, 179 entries / 1222 NPC ids |
| `Server/data/configs/item_configs.json` | 11995 | names/examines for pouches, scrolls, charms |
| `dumps/498/498_object_dump.txt`, `dumps/530/530_interface_names.txt`, `dumps/530/gfxs.txt` | — | scenery options, interface internal names, gfx descriptions |

**ABSENT: there is no JSON/resource config for summoning.** No `Server/src/main/resources` directory exists. No file under `Server/data/configs/` names a pouch, scroll, familiar, or obelisk. All pouch/scroll/familiar data is hardcoded in Java enums — the port must transcribe from source, not parse data files. `Server/data/configs/varbit_definitions.json` also does **not** contain any summoning varbit (only a 2-entry custom set) — varbit defs must come from the rev-530 cache (`Server/data/cache/`, idx2).

---

## 1. Master pouch table (78 familiars + 5 sacred clay)

Columns: `slot` = interface-669 list slot; `pt cost` = summoning points drained on summon; `ticks` = familiar lifetime (600ms/tick); `spec` = special-move point cost (out of 60); `BoB` = beast-of-burden slots (blank = not a BoB).

| Pouch enum | slot | pouch id | pouch name | lvl | pouch XP | NPC id | NPC name | summon XP | pt cost | peaceful | ticks | spec | BoB | scroll id | scroll name | scroll XP | ingredients |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| SPIRIT_WOLF_POUCH | 0 | 12047 | Spirit wolf pouch | 1 | 4.8 | 6829 | Spirit wolf | 0.1 | 1 | false | 600 | 3 |  | 12425 | Howl scroll | 0.1 | Gold charm(12158), Pouch(12155), Wolf bones(2859), Spirit shards(12183)x7 |
| DREADFOWL_POUCH | 1 | 12043 | Dreadfowl pouch | 4 | 9.3 | 6825 | Dreadfowl | 0.1 | 1 | false | 400 | 3 |  | 12445 | Dreadfowl strike scroll | 0.1 | Gold charm, Pouch, Raw chicken(2138), Shards x8 |
| SPIRIT_SPIDER_POUCH | 2 | 12059 | Spirit spider pouch | 10 | 12.6 | 6841 | Spirit spider | 0.2 | 2 | false | 1500 | 6 |  | 12428 | Egg spawn scroll | 0.2 | Gold charm, Pouch, Spider carcass(6291), Shards x8 |
| THORNY_SNAIL_POUCH | 3 | 12019 | Thorny snail pouch | 13 | 12.6 | 6806 | Thorny snail | 0.2 | 2 | false | 1600 | 3 | 3 | 12459 | Slime spray scroll | 0.2 | Gold charm, Pouch, Thin snail(3363), Shards x9 |
| GRANITE_CRAB_POUCH | 4 | 12009 | Granite crab pouch | 16 | 21.6 | 6796 | Granite crab | 0.2 | 2 | false | 1800 | 12 | 30 | 12533 | Stony shell scroll | 0.2 | Gold charm, Pouch, Iron ore(440), Shards x7 |
| SPIRIT_MOSQUITO_POUCH | 5 | 12778 | Spirit mosquito pouch | 17 | 46.5 | 7331 | Spirit mosquito | 0.5 | 2 | false | 1200 | 3 |  | 12838 | Pester scroll | 0.5 | Gold charm, Pouch, Proboscis(6319), Shards x1 |
| DESERT_WYRM_POUCH | 6 | 12049 | Desert wyrm pouch | 18 | 31.2 | 6831 | Desert wyrm | 0.4 | 1 | false | 1900 | 6 | 30 | 12460 | Electric lash scroll | 0.4 | Green charm(12159), Pouch, Bucket of sand(1783), Shards x45 |
| SPIRIT_SCORPION_POUCH | 7 | 12055 | Spirit scorpion pouch | 19 | 83.2 | 6837 | Spirit scorpion | 0.9 | 2 | false | 1700 | 6 |  | 12432 | Venom shot scroll | 0.9 | Crimson charm(12160), Pouch, Bronze claws(3095), Shards x57 |
| SPIRIT_TZ_KIH_POUCH | 8 | 12808 | Spirit tz-kih pouch | 22 | 96.8 | 7361 | Spirit Tz-Kih | 1.1 | 3 | false | 1800 | 6 |  | 12839 | Fireball assault scroll | 1.1 | Crimson charm, Obsidian charm(12168), Pouch, Shards x64 |
| ALBINO_RAT_POUCH | 9 | 12067 | Albino rat pouch | 23 | 202.4 | 6847 | Albino rat | 2.3 | 1 | false | 2200 | 6 | 30 | 12430 | Cheese feast scroll | 2.3 | Blue charm(12163), Pouch, Raw rat meat(2134), Shards x75 |
| SPIRIT_KALPHITE_POUCH | 10 | 12063 | Spirit kalphite pouch | 25 | 220.0 | 6994 | Spirit kalphite | 2.5 | 3 | false | 2200 | 6 | 6 | 12446 | Sandstorm scroll | 2.5 | Blue charm, Pouch, Potato cactus(3138), Shards x51 |
| COMPOST_MOUND_POUCH | 11 | 12091 | Compost mound pouch | 28 | 49.8 | 6871 | Compost mound | 0.6 | 6 | false | 2400 | 12 | 30 | 12440 | Generate compost scroll | 0.6 | Green charm, Pouch, Compost(6032), Shards x47 |
| GIANT_CHINCHOMPA_POUCH | 12 | 12800 | Giant chinchompa pouch | 29 | 255.2 | 7353 | Giant chinchompa | 2.9 | 1 | false | 3100 | 3 |  | 12834 | Explode scroll | 2.9 | Blue charm, Pouch, Chinchompa(10033), Shards x84 |
| VAMPIRE_BAT_POUCH | 13 | 12053 | Vampire bat pouch | 31 | 136.0 | 6835 | Vampire bat | 1.5 | 4 | false | 3300 | 4 |  | 12447 | Vampire touch scroll | 1.5 | Crimson charm, Pouch, Vampire dust(3325), Shards x81 |
| HONEY_BADGER_POUCH | 14 | 12065 | Honey badger pouch | 32 | 140.8 | 6845 | Honey badger | 1.6 | 4 | false | 2500 | 4 |  | 12433 | Insane ferocity scroll | 1.6 | Crimson charm, Pouch, Honeycomb(12156), Shards x84 |
| BEAVER_POUCH | 15 | 12021 | Beaver pouch | 33 | 57.6 | 6808 | Beaver | 0.7 | 4 | **true** | 2700 | 6 | 30 | 12429 | Multichop scroll | 0.7 | Green charm, Pouch, Willow logs(1519), Shards x72 |
| VOID_RAVAGER_POUCH | 16 | 12818 | Void ravager pouch | 34 | 59.6 | 7370 | Void ravager | 0.7 | 4 | false | 2700 | 3 | 30 | 12443 | Call to arms scroll | 0.7 | Green charm, Ravager charm(12164), Pouch, Shards x74 |
| VOID_SPINNER_POUCH | 17 | 12780 | Void spinner pouch | 34 | 59.6 | 7333 | Void spinner | 0.7 | 4 | **true** | 2700 | 3 |  | 12443 | Call to arms scroll | 0.7 | Blue charm, Spinner charm(12166), Pouch, Shards x74 |
| VOID_TORCHER_POUCH | 18 | 12798 | Void torcher pouch | 34 | 59.6 | 7351 | Void torcher | 0.7 | 4 | false | **9400** | 3 |  | 12443 | Call to arms scroll | 0.7 | Blue charm, Torcher charm(12167), Pouch, Shards x74 |
| VOID_SHIFTER_POUCH | 19 | 12814 | Void shifter pouch | 34 | 59.6 | 7367 | Void shifter | 0.7 | 4 | false | **9400** | 3 |  | 12443 | Call to arms scroll | 0.7 | Blue charm, Shifter charm(12165), Pouch, Shards x74 |
| BRONZE_MINOTAUR_POUCH | 64 | 12073 | Bronze minotaur pouch | 36 | 316.8 | 6853 | Bronze minotaur | 3.6 | 3 | false | 3000 | 6 |  | 12461 | Bronze bull rush scroll | 3.6 | Blue charm, Pouch, Bronze bar(2349), Shards x102 |
| IRON_MINOTAUR_POUCH | 65 | 12075 | Iron minotaur pouch | 46 | 404.8 | 6855 | Iron minotaur | 4.6 | 9 | false | 3700 | 6 |  | 12462 | Iron bull rush scroll | 4.6 | Blue charm, Pouch, Iron bar(2351), Shards x125 |
| STEEL_MINOTAUR_POUCH | 66 | 12077 | Steel minotaur pouch | 56 | 492.8 | 6857 | Steel minotaur | 5.6 | 9 | false | 4600 | 6 |  | 12463 | Steel bull rush scroll | 5.6 | Blue charm, Pouch, Steel bar(2353), Shards x141 |
| MITHRIL_MINOTAUR_POUCH | 67 | 12079 | Mithril minotaur pouch | 66 | 580.8 | 6859 | Mithril minotaur | 6.6 | 9 | false | 5500 | 6 |  | 12464 | Mith bull rush scroll | 6.6 | Blue charm, Pouch, Mithril bar(2359), Shards x152 |
| ADAMANT_MINOTAUR_POUCH | 68 | 12081 | Adamant minotaur pouch | 76 | 668.8 | 6861 | Adamant minotaur | 7.6 | 9 | false | 6600 | 6 |  | 12465 | Addy bull rush scroll | 7.6 | Blue charm, Pouch, Adamantite bar(2361), Shards x144 |
| RUNE_MINOTAUR_POUCH | 69 | 12083 | Rune minotaur pouch | 86 | 756.8 | 6863 | Rune minotaur | 8.6 | 9 | false | **15100** | 6 |  | 12466 | Rune bull rush scroll | 8.6 | Blue charm, Pouch, Runite bar(2363), Shards x1 |
| BULL_ANT_POUCH | 20 | 12087 | Bull ant pouch | 40 | 52.8 | 6867 | Bull ant | 0.6 | 5 | false | 3000 | 12 | 9 | 12431 | Unburden scroll | 0.6 | Gold charm, Pouch, Marigolds(6010), Shards x11 |
| MACAW_POUCH | 21 | 12071 | Macaw pouch | 41 | 72.4 | 6851 | Macaw | 0.8 | 5 | **true** | 3100 | 12 | 30 | 12422 | Herbcall scroll | 0.8 | Green charm, Pouch, Clean guam(249), Shards x78 |
| EVIL_TURNIP_POUCH | 22 | 12051 | Evil turnip pouch | 42 | 184.8 | 6833 | Evil turnip | 2.1 | 5 | false | 3000 | 6 | 30 | 12448 | Evil flames scroll | 2.1 | Crimson charm, Pouch, Carved evil turnip(12153), Shards x104 |
| SPIRIT_COCKATRICE_POUCH | 23 | 12095 | Sp. cockatrice pouch | 43 | 75.2 | 6875 | Spirit cockatrice | 0.9 | 5 | false | 3600 | 3 | 30 | 12458 | Petrifying gaze scroll | 0.9 | Green charm, Pouch, Cockatrice egg(12109), Shards x88 |
| SPIRIT_GUTHATRICE_POUCH | 24 | 12097 | Sp. guthatrice pouch | 43 | 75.2 | 6877 | Spirit guthatrice | 0.9 | 5 | false | 3600 | 3 | 30 | 12458 | Petrifying gaze scroll | 0.9 | Green charm, Pouch, Guthatrice egg(12111), Shards x88 |
| SPIRIT_SARATRICE_POUCH | 25 | 12099 | Sp. saratrice pouch | 43 | 75.2 | 6879 | Spirit saratrice | 0.9 | 5 | false | 3600 | 3 | 30 | 12458 | Petrifying gaze scroll | 0.9 | Green charm, Pouch, Saratrice egg(12113), Shards x88 |
| SPIRIT_ZAMATRICE_POUCH | 26 | 12101 | Sp. zamatrice pouch | 43 | 75.2 | 6881 | Spirit zamatrice | 0.9 | 5 | false | 3600 | 3 | 30 | 12458 | Petrifying gaze scroll | 0.9 | Green charm, Pouch, Zamatrice egg(12115), Shards x88 |
| SPIRIT_PENGATRICE_POUCH | 27 | 12103 | Sp. pengatrice pouch | 43 | 75.2 | 6883 | Spirit pengatrice | 0.9 | 5 | false | 3600 | 3 | 30† | 12458 | Petrifying gaze scroll | 0.9 | Green charm, Pouch, Pengatrice egg(12117), Shards x88 |
| SPIRIT_CORAXATRICE_POUCH | 28 | 12105 | Sp. coraxatrice pouch | 43 | 75.2 | 6885 | Spirit coraxatrice | 0.9 | 5 | false | 3600 | 3 | 30 | 12458 | Petrifying gaze scroll | 0.9 | Green charm, Pouch, Coraxatrice egg(12119), Shards x88 |
| SPIRIT_VULATRICE | 29 | 12107 | Sp. vulatrice pouch | 43 | 75.2 | 6887 | Spirit vulatrice | 0.9 | 5 | false | 3600 | 3 | 30 | 12458 | Petrifying gaze scroll | 0.9 | Green charm, Pouch, Vulatrice egg(12121), Shards x88 |
| PYRELORD_POUCH | 30 | 12816 | Pyrelord pouch | 46 | 202.4 | 7377 | Pyrelord | 2.3 | 5 | false | 3200 | 6 |  | 12829 | Immense heat scroll | 2.3 | Crimson charm, Pouch, Tinderbox(590), Shards x111 |
| MAGPIE_POUCH | 31 | 12041 | Magpie pouch | 47 | 83.2 | 6824 | Magpie | 0.9 | 5 | **true** | 3400 | 3 | 30 | 12426 | Thieving fingers scroll | **47.0** | Green charm, Pouch, Gold ring(1635), Shards x88 |
| BLOATED_LEECH_POUCH | 32 | 12061 | Bloated leech pouch | 49 | 215.2 | 6843 | Bloated leech | 2.4 | 5 | false | 3400 | 6 |  | 12444 | Blood drain scroll | 2.4 | Crimson charm, Pouch, Raw beef(2132), Shards x117 |
| SPIRIT_TERRORBIRD_POUCH | 33 | 12007 | Spirit terrorbird pouch | 52 | 68.4 | 6794 | Spirit terrorbird | 0.8 | 6 | **true** | 3600 | 8 | 12 | 12441 | Tireless run scroll | 0.8 | Gold charm, Pouch, Raw bird meat(9978), Shards x12 |
| ABYSSAL_PARASITE_POUCH | 34 | 12035 | Abyssal parasite pouch | 54 | 94.8 | 6818 | Abyssal parasite | 1.1 | 6 | false | 3000 | 1 | 7 | 12454 | Abyssal drain scroll | 1.1 | Green charm, Pouch, Abyssal charm(12161), Shards x106 |
| SPIRIT_JELLY_POUCH | 35 | 12027 | Spirit jelly pouch | 55 | 484.0 | 6992 | Spirit jelly | 5.5 | 6 | false | 4300 | 6 |  | 12453 | Dissolve scroll | 5.5 | Blue charm, Pouch, Jug of water(1937), Shards x151 |
| IBIS_POUCH | 36 | 12531 | Ibis pouch | 56 | 98.8 | 6991 | Ibis | 1.1 | 6 | **true** | 3800 | 12 | 30 | 12424 | Fish rain scroll | 1.1 | Green charm, Pouch, Harpoon(311), Shards x109 |
| SPIRIT_KYATT_POUCH | 37 | 12812 | Spirit kyatt pouch | 57 | 501.6 | 7365 | Spirit kyatt | 5.7 | 6 | false | 4900 | 3 |  | 12836 | Ambush scroll | 5.7 | Blue charm, Pouch, Kyatt fur(10103), Shards x153 |
| SPIRIT_LARUPIA_POUCH | 38 | 12784 | Spirit larupia pouch | 57 | 501.6 | 7337 | Spirit larupia | 5.7 | 6 | false | 4900 | 6 |  | 12840 | Rending scroll | 5.7 | Blue charm, Pouch, Larupia fur(10095), Shards x155 |
| SPIRIT_GRAAHK_POUCH | 39 | 12810 | Spirit graahk pouch | 57 | 501.6 | 7363 | Spirit graahk | 5.7 | 6 | false | 4900 | 3 |  | 12835 | Goad scroll | 5.7 | Blue charm, Pouch, Graahk fur(10099), Shards x154 |
| KARAMTHULHU_POUCH | 40 | 12023 | Karam. overlord pouch | 58 | 510.4 | 6809 | Karamthulhu overlord | 5.8 | 6 | false | 4400 | 3 |  | **none** (12455 mis-keyed) | Doomsphere scroll | 5.8 | Blue charm, Pouch, Fishbowl(6667), Shards x144 |
| SMOKE_DEVIL_POUCH | 41 | 12085 | Smoke devil pouch | 61 | 268.0 | 6865 | Smoke devil | 3.0 | 7 | false | 4800 | 6 |  | 12468 | Dust cloud scroll | 3.0 | Crimson charm, Pouch, Goat horn dust(9736), Shards x141 |
| ABYSSAL_LUKRER | 42 | 12037 | Abyssal lurker pouch | 62 | 109.6 | 6820 | Abyssal lurker | 1.9 | 9 | false | 4100 | 3 | 7 | 12427 | Abyssal stealth scroll | 1.9 | Green charm, Pouch, Abyssal charm(12161), Shards x119 |
| SPIRIT_COBRA_POUCH | 43 | 12015 | Spirit cobra pouch | 63 | 276.8 | 6802 | Spirit cobra | 3.1 | 6 | false | 5600 | 3 |  | 12436 | Oph. incubation scroll | 3.1 | Crimson charm, Pouch, Snake hide(6287), Shards x116 |
| STRANGER_PLANT_POUCH | 44 | 12045 | Stranger plant pouch | 64 | 281.6 | 6827 | Stranger plant | 3.2 | 6 | false | 4900 | 6 | 30 | 12467 | Poisonous blast scroll | 3.2 | Crimson charm, Pouch, Bagged plant 1(8431), Shards x128 |
| BARKER_TOAD_POUCH | 45 | 12123 | Barker toad pouch | 66 | 87.0 | 6889 | Barker toad | 1.0 | 7 | false | 800 | 6 |  | 12452 | Toad bark scroll | 1.0 | Gold charm, Pouch, Swamp toad(2150), Shards x11 |
| WAR_TORTOISE_POUCH | 46 | 12031 | War tortoise pouch | 67 | 58.6 | 6815 | War tortoise | 0.7 | 7 | **true** | 4300 | 20 | 18 | 12439 | Testudo scroll | 0.7 | Gold charm, Pouch, Tortoise shell(7939), Shards x1 |
| BUNYIP_POUCH | 47 | 12029 | Bunyip pouch | 68 | 119.2 | 6813 | Bunyip | 1.4 | 7 | **true** | 4400 | 3 |  | 12438 | Swallow whole scroll | 1.4 | Green charm, Pouch, Raw shark(383), Shards x110 |
| FRUIT_BAT_POUCH | 48 | 12033 | Fruit bat pouch | 69 | 121.2 | 6817 | Fruit bat | 1.4 | 8 | **true** | 4500 | 6 | 30 | 12423 | Fruitfall scroll | 1.4 | Green charm, Pouch, Banana(1963), Shards x130 |
| RAVENOUS_LOCUST_POUCH | 49 | 12820 | Ravenous locust pouch | 70 | 132.0 | 7372 | Ravenous locust | 1.5 | 4 | false | 2400 | 12 |  | 12830 | Famine scroll | 1.5 | Crimson charm, Pouch, Pot of flour(1933), Shards x79 |
| ARCTIC_BEAR_POUCH | 50 | 12057 | Arctic bear pouch | 71 | 93.2 | 6839 | Arctic bear | 1.1 | 8 | false | 2800 | 6 |  | 12451 | Arctic blast scroll | 1.1 | Gold charm, Pouch, Polar kebbit fur(10117), Shards x14 |
| PHOENIX_POUCH | **50 (dup)** | 14623 | Phoenix pouch | 72 | 93.2 | 8575 | Phoenix | 1.1 | 8 | false | **ABSENT** | — |  | 14622 (commented out) | Rise from the ashes | — | Crimson charm, Shards x165, Pouch, Phoenix quill(14616) |
| OBSIDIAN_GOLEM_POUCH | 51 | 12792 | Obsidian golem pouch | 73 | 642.4 | 7345 | Obsidian golem | 7.3 | 8 | false | 5500 | 12 |  | 12826 | Volcanic str. scroll | 7.3 | Blue charm, Pouch, Obsidian charm(12168), Shards x195 |
| GRANITE_LOBSTER_POUCH | 52 | 12069 | Granite lobster pouch | 74 | 325.6 | 6849 | Granite lobster | 3.7 | 8 | false | 4700 | 6 | 30 | 12449 | Crushing claw scroll | 3.7 | Crimson charm, Pouch, Granite 500g(6979), Shards x166 |
| PRAYING_MANTIS_POUCH | 53 | 12011 | Praying mantis pouch | 75 | 329.6 | 6798 | Praying mantis | 3.6 | 8 | false | 6900 | 6 |  | 12450 | Mantis strike scroll | 3.7 | Crimson charm, Pouch, Flowers(2460), Shards x168 |
| FORGE_REGENT_BEAST | 54 | 12782 | Forge regent pouch | 76 | 134.0 | 7335 | Forge regent | 1.5 | 9 | false | 4500 | 6 |  | 12841 | Inferno scroll | 1.5 | Green charm, Pouch, Ruby harvest(10020), Shards x141 |
| TALON_BEAST_POUCH | 55 | 12794 | Talon beast pouch | 77 | 1015.2 | 7347 | Talon beast | 3.8 | 9 | false | 4900 | 6 |  | **none** (12831 keyed to 12162) | Deadly claw scroll | 11.0 | Crimson charm, Pouch, Talon beast charm(12162), Shards x174 |
| GIANT_ENT_POUCH | 56 | 12013 | Giant ent pouch | 78 | 136.8 | 6800 | Giant ent | 1.6 | 8 | false | 4900 | 6 | 30 | 12457 | Acorn missile scroll | 1.6 | Green charm, Willow branch(5933), Pouch, Shards x124 |
| HYDRA_POUCH | 60 | 12025 | Hydra pouch | 80 | 140.8 | 6811 | Hydra | 1.6 | 9 | false | 4900 | 6 |  | 12442 | Regrowth scroll | 1.6 | Green charm, Water orb(571), Pouch, Shards x128 |
| SPIRIT_DAGANNOTH_POUCH | 61 | 12017 | Spirit dagannoth pouch | 83 | 364.8 | 6804 | Spirit dagannoth | 4.1 | 9 | false | 5700 | 6 |  | 12456 | Spike shot scroll | 4.1 | Crimson charm, Dagannoth hide(6155), Pouch, Shards x1 |
| UNICORN_STALLION_POUCH | 70 | 12039 | Unicorn stallion pouch | 88 | 154.4 | 6822 | Unicorn stallion | 1.8 | 9 | **true** | 5400 | 20 |  | 12434 | Healing aura scroll | 1.8 | Green charm, Unicorn horn(237), Pouch, Shards x140 |
| WOLPERTINGER_POUCH | 72 | 12089 | Wolpertinger pouch | 92 | 404.8 | 6869 | Wolpertinger | 4.5 | 10 | false | 6200 | 1 |  | 12437 | Magic focus scroll | 4.6 | Crimson charm, Wolf bones(2859), Raw rabbit(3226), Pouch, Shards x203 |
| PACK_YAK_POUCH | 75 | 12093 | Pack yak pouch | 96 | 422.4 | 6873 | Pack yak | 4.8 | 10 | **true** | 5800 | 12 | 30 | 12435 | Winter storage scroll | 4.8 | Crimson charm, Yak-hide(10818), Pouch, Shards x211 |
| FIRE_TITAN_POUCH | 57 | 12802 | Fire titan pouch | 79 | 695.2 | 7355 | Fire titan | 7.9 | 9 | false | 6200 | 20 |  | 12824 | Titan's con. scroll | 7.9 | Blue charm, Fire talisman(1442), Pouch, Shards x198 |
| MOSS_TITAN_POUCH | 58 | 12804 | Moss titan pouch | 79 | 695.2 | 7357 | Moss titan | 7.9 | 9 | false | 5800 | 20 |  | 12824 | Titan's con. scroll | 7.9 | Blue charm, Earth talisman(1440), Pouch, Shards x198 |
| ICE_TITAN_POUCH | 59 | 12806 | Ice titan pouch | 79 | 695.2 | 7359 | Ice titan | 7.9 | 9 | false | 6400 | 20 |  | 12824 | Titan's con. scroll | 7.9 | Blue charm, Air talisman(1438), Water talisman(1444), Pouch, Shards x198 |
| LAVA_TITAN_POUCH | 62 | 12788 | Lava titan pouch | 83 | 730.4 | 7341 | Lava titan | 8.3 | 9 | false | 6100 | 4 |  | 12837 | Ebon thunder scroll | 8.3 | Blue charm, Obsidian charm(12168), Pouch, Shards x219 |
| SWAMP_TITAN_POUCH | 63 | 12776 | Swamp titan pouch | 85 | 373.6 | 7329 | Swamp titan | 4.2 | 9 | false | 5600 | 6 |  | 12832 | Swamp plague scroll | 4.1 | Crimson charm, Swamp lizard(10149), Pouch, Shards x150 |
| GEYSER_TITAN_POUCH | 71 | 12786 | Geyser titan pouch | 89 | 783.2 | 7339 | Geyser titan | 8.9 | 9 | false | 6900 | 6 |  | 12833 | Boil scroll | 8.9 | Blue charm, Water talisman(1444), Pouch, Shards x222 |
| ABYSSAL_TITAN_POUCH | 73 | 12796 | Abyssal titan pouch | 93 | 163.2 | 7349 | Abyssal titan | 1.9 | 10 | false | 3200 | 6 | 7 | 12827 | Essence shipment scroll | 1.9 | Green charm, Abyssal charm(12161), Pouch, Shards x113 |
| IRON_TITAN_POUCH | 74 | 12822 | Iron titan pouch | 95 | 417.6 | 7375 | Iron titan | 4.7 | 10 | false | 6000 | 12 |  | 12828 | Iron within scroll | 4.7 | Crimson charm, Iron platebody(1115), Pouch, Shards x198 |
| STEEL_TITAN_POUCH | 76 | 12790 | Steel titan pouch | 99 | 435.2 | 7343 | Steel titan | 4.9 | 10 | false | 6400 | 12 |  | 12825 | Steel of legends scroll | 4.9 | Crimson charm, Steel platebody(1119), Pouch, Shards x178 |
| SACRED_CLAY_POUCH_1 | -1 | 14422 | (Stealing Creation) | 1 | 0 | 8240 | Clay familiar cls1 | 0 | 1 | false | ABSENT | — |  | 14421 | Clay deposit scroll | 0 | Sacred clay cls1(14182) |
| SACRED_CLAY_POUCH_2 | -1 | 14424 | — | 20 | 0 | 8242 | Clay familiar cls2 | 0 | 3 | false | ABSENT | — |  | 14421 | — | 0 | 14184 |
| SACRED_CLAY_POUCH_3 | -1 | 14426 | — | 40 | 0 | 8244 | Clay familiar cls3 | 0 | 5 | false | ABSENT | — |  | 14421 | — | 0 | 14186 |
| SACRED_CLAY_POUCH_4 | -1 | 14428 | — | 60 | 0 | 8246 | Clay familiar cls4 | 0 | 7 | false | ABSENT | — |  | 14421 | — | 0 | 14188 |
| SACRED_CLAY_POUCH_5 | -1 | 14430 | — | 80 | 0 | 8248 | Clay familiar cls5 | 0 | 9 | false | ABSENT | — |  | 14421 | — | 0 | 14190 |

† `SpiritPengatriceNPC.java:24` (plain `Familiar`, no BoB) and `CockatriceFamiliarNPC.java:194 SpiritPengatrice` (a `Forager`, BoB 30) **both register NPC ids 6883/6884** — duplicate registration; `FamiliarManager.newInstance` logs "already registered" and drops one. Pick one for the port.

Every familiar also has a **wilderness combat form at `npcId + 1`** (`Familiar.transform()`, `Familiar.java:449-453`); `combatFamiliar` is decided by `NPCDefinition.forId(id+1).getName().equals(getName())`.

Familiar lifetime/spec-cost/BoB values were read from the `super(...)` calls; the per-familiar file:line map is in §3.

---

## 2. Charms, shard, and tertiary ingredients

| Item id | Name | Role |
|---|---|---|
| 12158 | Gold charm | tier-1 charm |
| 12159 | Green charm | tier-2 charm |
| 12160 | Crimson charm | tier-3 charm |
| 12163 | Blue charm | tier-4 charm |
| 12161 | Abyssal charm | special (abyssal familiars) |
| 12162 | Talon beast charm | special |
| 12164 | Ravager charm | Pest Control |
| 12165 | Shifter charm | Pest Control |
| 12166 | Spinner charm | Pest Control |
| 12167 | Torcher charm | Pest Control |
| 12168 | Obsidian charm | TzHaar |
| 12183 | Spirit shards | consumable currency |
| 12155 | Pouch | blank pouch |
| 12527 | Gold charm (quest copy, Destroy) | Wolf Whistle |
| 12530 | Spirit shards (quest copy, Destroy) | Wolf Whistle |
| 12528 | Trapdoor key | Wolf Whistle |

Tertiary ingredient item ids (distinct, from the master table): 237, 249, 311, 383, 440, 571, 590, 1115, 1119, 1438, 1440, 1442, 1444, 1519, 1635, 1783, 1933, 1937, 1963, 2132, 2134, 2138, 2150, 2349, 2351, 2353, 2359, 2361, 2363, 2460, 2859, 3095, 3138, 3226, 3325, 3363, 5933, 6010, 6032, 6155, 6287, 6291, 6319, 6667, 6979, 7939, 8431, 9736, 9978, 10020, 10033, 10095, 10099, 10103, 10117, 10149, 10818, 12109, 12111, 12113, 12115, 12117, 12119, 12121, 12153, 12156, 14182/14184/14186/14188/14190, 14616.

**Charm drop rates**: `Server/data/configs/drop_tables.json` — each entry has a `"charm"` array of `{id, weight, minAmount, maxAmount}` with `id:"0"` = nothing. 179 entries carry non-zero charm drops, spanning **1222 distinct NPC ids**. Example: `{"charm":[{"id":"0","weight":"91.6064"},{"id":"12158","weight":"7.0005"},{"id":"12159","weight":"0.7927"},{"id":"12160","weight":"0.4372"},…]}`. This is the largest single data blob in the port.

---

## 3. SPOTANIM / ANIMATION / SOUND manifest

### 3a. Global (non-familiar-specific)

| Kind | Id | Where | Purpose |
|---|---|---|---|
| GFX | 1314 | `familiar/Familiar.java:49` | small-familiar summon puff |
| GFX | 1315 | `familiar/Familiar.java:54` | large-familiar (size>1) summon puff |
| GFX | 1316 | `familiar/Familiar.java:64,511` | player special-move gfx |
| ANIM | 7660 | `familiar/Familiar.java:59,511` | player special-move anim |
| ANIM | 9068 | `SummoningCreator.java:26` | infuse-pouch player anim |
| ANIM | 8509 | `SummoningCreator.java:162` | obelisk scenery anim (charging) |
| ANIM | 8510 | `SummoningCreator.java:155,166` | obelisk scenery anim (idle/reset) |
| ANIM | 8502 + GFX 1308 | `ObeliskOptionPlugin.java:36` | Renew-points |
| ANIM | 827 | `familiar/FamiliarManager.java:307,363,381`, `SpiritKyattOptionPlugin.java:44` | pet pickup / feed / take |
| ANIM | 828 | `SummoningTrainingRoom.java:175`, `SpiritKyattOptionPlugin.java:48` | climb |
| ANIM 2836 / GFX 1522 / ANIM 8506 / ANIM 8507 | `SummoningTrainingRoom.java:254,259,264,269` | Wolf Whistle cutscene (scared / wolpertinger gfx / shudder / death) |
| PROJ | 1333 | `SummoningTrainingRoom.java:549` | howl-scroll projectile in cutscene |
| ANIM 9224 / 9173 | `pet/KittenInteractDialogue.java:25-26` | player stroke / kitten stroke |

### 3b. Sounds (`org.rs09.consts.Sounds`; the numeric suffix **is** the sound id)

| Sound id | Const | Call site | Notes |
|---|---|---|---|
| 188 | `SUMMON_NPC_188` | `familiar/Familiar.java:714,717` | `summon_npc=188` also exists in the OSRS synth name list (`dumps/530/530_synth_debug_names.txt:193`) → likely already in osrs239 |
| 4161 | `SPELL_4161` | `familiar/Familiar.java:497` | special-move cast |
| 4164 | `CRAFT_POUCH_4164` | `SummoningCreator.java:163` | (comment: 4277 sounds the same) |
| 4214 | `DREADFOWL_BOOST_4214` | `ObeliskOptionPlugin.java:37` | renew-points |
| 4265 | `WOLF_HOWL2_4265` | `familiar/SpiritWolfNPC.java:111` | Howl special |
| 4372 | `HEALING_AURA_4372` | `familiar/UnicornStallionNPC.java:49,81` | Healing aura |

Sounds 4161/4164/4214/4265/4372 are **above id 3826, the documented OSRS divergence point** (`dumps/530/530_synth_debug_names.txt` header) → they must be transcoded from the rev-530 cache. Only 188 is safe. `Familiar.java:713` explicitly TODOs the missing per-familiar summon sounds — **ABSENT**: 2009scape has no per-familiar summon-sound table.

### 3c. Per-familiar anim / gfx / projectile

| File | Refs (line:kind(id)) |
|---|---|
| `familiar/AbyssalLurkerNPC.java` | 50:ANIM(7682) GFX(0); 58:ANIM(7660) GFX(1296) |
| `familiar/AbyssalParasiteNPC.java` | 64:ANIM(7672) GFX(1422); 65:PROJ(1423) |
| `familiar/AbyssalTitanNPC.kt` | 83:ANIM(7660) GFX(1316); 84:ANIM(7694) GFX(1457) |
| `familiar/AlbinoRatNPC.java` | 48:ANIM(4934) GFX(1384) |
| `familiar/ArcticBearNPC.java` | 48:ANIM(4926); 49:GFX(1405); 50:PROJ(1406); 53:GFX(1407) |
| `familiar/BarkerToadNPC.java` | 44:GFX(1403); 45:GFX(1404) |
| `familiar/BeaverNPC.java` | 87:ANIM(7722) |
| `familiar/BullAntNPC.java` | 45:ANIM(7896) GFX(1382); 52:ANIM(7660) GFX(1296) |
| `familiar/BunyipNPC.java` | 67:GFX(1507); 107:ANIM(7747); 108:GFX(1481); 121:ANIM(7660) GFX(1316); 148:ANIM(2779); 149:PROJ(1435) |
| `familiar/CockatriceFamiliarNPC.java` | 58:ANIM(7762) GFX(1467); 67:PROJ(1468); 68:GFX(1469) |
| `familiar/CompostMoundNPC.java` | 91:ANIM(7775); 92:GFX(1424); 129:ANIM(895); 130:ANIM(7775) |
| `familiar/DesertWyrmNPC.java` | 68:ANIM(7795) GFX(1410); 69:PROJ(1411); 106:ANIM(7800) GFX(1412) |
| `familiar/DreadfowlNPC.java` | 99:ANIM(5387) GFX(1523); 100:PROJ(1318) |
| `familiar/EvilTurnipNPC.java` | 57:ANIM(8251); 58:GFX(1329); 60:PROJ(1330) |
| `familiar/ForgeRegentNPC.java` | 38:ANIM(8085); 95:GFX(1394); 96:GFX(1393) |
| `familiar/FruitBatNPC.java` | 100:ANIM(8320); 101:GFX(1332); 102:ANIM(8321); 103:GFX(1331) |
| `familiar/GeyserTitanNPC.java` | 46:ANIM(7883) GFX(1375); 59:PROJ(1376); 60:GFX(1377) |
| `familiar/GiantChinchompaNPC.java` | 63:ANIM(7758); 64:GFX(1364) |
| `familiar/GraniteCrabNPC.java` | 53:ANIM(8107); 64:ANIM(8109) GFX(1326); 70:ANIM(7660) GFX(1296) |
| `familiar/GraniteLobsterNPC.java` | 55:ANIM(8118); 56:GFX(1351); 57:PROJ(1352); 66:ANIM(8107) |
| `familiar/HoneyBadgerNPC.java` | 41:ANIM(7660) GFX(1399); 55:ANIM(7928) GFX(1397) |
| `familiar/IbisNPC.java` | 72:ANIM(8201) |
| `familiar/IceTitanNPC.java` | 40:ANIM(7660) GFX(1306) |
| `familiar/IronTitanNPC.java` | 30,84:ANIM(8183); 84:GFX(1450) |
| `familiar/MacawNPC.java` | 68,160:ANIM(8013); 69:GFX(1321) |
| `familiar/MagpieNPC.java` | 49:ANIM(8020) GFX(1336); 56:ANIM(7660) GFX(1296) |
| `familiar/MinotaurFamiliarNPC.java` | 55:PROJ(1497); 56:ANIM(8026) GFX(1496) |
| `familiar/ObsidianGolemNPC.java` | 41:GFX(1465) |
| `familiar/PackYakNPC.java` | 77:GFX(1358); 87:ANIM(7660) GFX(1316) |
| `familiar/PyreLordNPC.java` | 38:ANIM(8085); 75:ANIM(8081); 76:GFX(1463) |
| `familiar/RavenousLocustNPC.java` | 47:GFX(1346); 48:GFX(1347) |
| `familiar/SmokeDevilNPC.java` | 76:ANIM(7820) GFX(1375) |
| `familiar/SpiritGraahkNPC.kt` | 41:ANIM(7660) GFX(1316) |
| `familiar/SpiritJellyNPC.java` | 47:ANIM(8575); 48:PROJ(1360) |
| `familiar/SpiritKalphiteNPC.java` | 52:ANIM(8517) GFX(1350); 64:PROJ(1349) *(commented out)* |
| `familiar/SpiritLarupiaNPC.java` | 51:ANIM(5229) GFX(1370) |
| `familiar/SpiritMosquitoNPC.java` | 45:ANIM(8032) GFX(1442) |
| `familiar/SpiritScorpionNPC.java` | 64:GFX(1355); 65:ANIM(6261) GFX(1354); 66:PROJ(1355) |
| `familiar/SpiritSpiderNPC.java` | 85:ANIM(5328) |
| `familiar/SpiritTerrorbirdNPC.java` | 41:ANIM(1009) GFX(1521); 49:ANIM(7660) GFX(1295) |
| `familiar/SpiritTzKihNPC.java` | 60:GFX(1329); 64:ANIM(8257) |
| `familiar/SpiritWolfNPC.java` | 113:ANIM(8293) GFX(1334); 114:PROJ(1333) |
| `familiar/SteelTitanNPC.java` | 31:ANIM(8190)+PROJ(1445) x2, ANIM(8183); 90:ANIM(8183) GFX(1449) |
| `familiar/StrangerPlantNPC.java` | 51:ANIM(8211); 52:PROJ(1508); 53:GFX(1511) |
| `familiar/ThornySnailNPC.java` | 52:ANIM(8148) GFX(1385); 53:PROJ(1386); 67:GFX(1387) |
| `familiar/UnicornStallionNPC.java` | 50,82:ANIM(8267) GFX(1356); 93:ANIM(7660) GFX(1298) |
| `familiar/VampireBatNPC.java` | 50:ANIM(8275) GFX(1323) |
| `familiar/VoidFamiliarNPC.java` | 63:ANIM(8136) GFX(1503); 68:ANIM(8137) GFX(1502) |
| `familiar/WarTortoiseNPC.java` | 41:ANIM(8288) GFX(1414); 47:ANIM(7660) GFX(1310) |
| `familiar/WolpertingerNPC.java` | 43:ANIM(8267) GFX(1464); 49:ANIM(7660) GFX(1306) |

Distinct SPOTANIM ids to import: **1295, 1296, 1298, 1306, 1308, 1310, 1314, 1315, 1316, 1318, 1321, 1323, 1326, 1329, 1330, 1331, 1332, 1333, 1334, 1336, 1346, 1347, 1349, 1350, 1351, 1352, 1354, 1355, 1356, 1358, 1360, 1364, 1370, 1375, 1376, 1377, 1382, 1384, 1385, 1386, 1387, 1393, 1394, 1397, 1399, 1403, 1404, 1405, 1406, 1407, 1410, 1411, 1412, 1414, 1422, 1423, 1424, 1435, 1442, 1445, 1449, 1450, 1457, 1463, 1464, 1465, 1467, 1468, 1469, 1481, 1496, 1497, 1502, 1503, 1507, 1508, 1511, 1521, 1522, 1523** (80). All are ≥1295, i.e. squarely in the 530-only band. `dumps/530/gfxs.txt` has human descriptions for ~20 of them (e.g. 1314 "Small Blue summon familiar graphic", 1315 "Big Blue summon familiar graphic", 1316 "Fruit bat special", 1333 "Tornado", 1502/1503 "Portal").

Distinct player/summoning-system SEQ ids from source: **827, 828, 895, 1009, 2779, 2836, 4926, 4934, 5229, 5328, 5387, 6261, 7660, 7672, 7682, 7694, 7722, 7747, 7758, 7762, 7775, 7795, 7800, 7820, 7883, 7896, 7928, 8013, 8020, 8026, 8032, 8081, 8085, 8107, 8109, 8118, 8136, 8137, 8148, 8183, 8190, 8201, 8211, 8251, 8257, 8267, 8275, 8288, 8293, 8320, 8321, 8502, 8506, 8507, 8509, 8510, 8517, 8575, 9068, 9173, 9224**.

Plus the **per-NPC combat anims** from `Server/data/configs/npc_configs.json` (melee/magic/range/defence/death), which cover all 166 familiar NPC ids. Sample of the base rows:

| npc (+wild form) | name | LP | atk/str/def/rng/mag | melee | magic | range | defence | death |
|---|---|---|---|---|---|---|---|---|
| 6829+6830 | Spirit wolf | 15 | 16/18/21/9/7 | 8292 | 8292 | 8292 | 8294 | 8295 |
| 6825+6826 | Dreadfowl | 16 | 17/15/22/16/23 | 7810 | 7810 | 7810 | 5388 | 5389 |
| 6841+6842 | Spirit spider | 18 | 17/16/17/12/15 | 5327 | 5327 | 5327 | 5328 | 5329 |
| 6806+6807 | Thorny snail | 28 | 19/18/22/21/18 | 8143 | — | — | 8145 | 8143 |
| 6796+6797 | Granite crab | 39 | 18/17/22/16/13 | 8104 | — | — | 8105 | 8106 |
| 7331+7332 | Spirit mosquito | 43 | 28/24/25/25/21 | 8032 | — | — | 8034 | 8033 |
| 6831+6832 | Desert wyrm | 25 | 18/… | 7795 | 0 | 0 | 0 | 7797 |
| 6837+6838 | Spirit scorpion | 27 | 19/… | 6254 | 0 | 0 | 0 | 6256 |
| 7361+7362 | Spirit Tz-Kih | 31 | 22/… | 8257 | 0 | 0 | 0 | 8258 |
| 6994+6995 | Spirit kalphite | 35 | 25/… | 6223 | 0 | 0 | 0 | 6228 |
| 6871+6872 | Compost mound | 40 | 28/… | 7769 | 0 | 0 | 0 | 7770 |
| 7353+7354 | Giant chinchompa | 41 | 29/… | 7755 | 0 | 0 | 0 | 7758 |
| 6835+6836 | Vampire bat | 44 | 31/… | 8275 | 0 | 0 | 0 | 8276 |
| 6845+6846 | Honey badger | 45 | 32/… | 7928 | 0 | 0 | 0 | 7925 |
| 7370+7371 | Void ravager | 48 | 34/… | 8086 | 0 | 0 | 0 | 8087 |
| 7333+7334 | Void spinner | 48 | 34/… | 8172 | 0 | 0 | 0 | 8176 |
| 7351+7352 | Void torcher | 48 | 34/… | 8235 | 0 | 0 | 0 | 8236 |
| 7367+7368 | Void shifter | 48 | 34/… | 8131 | 0 | 0 | 0 | 8133 |
| 6853..6864 (minotaurs) | Bronze→Rune minotaur | 133/193/260/340/441/570 | 19..40 | 8024 | — | — | 8023 | 8025 |
| 6867+6868 | Bull ant | 57 | 40/… | 7896 | 0 | 0 | 0 | 7897 |
| 6833+6834 | Evil turnip | 60 | 42/… | 8248 | 0 | 0 | 0 | 8250 |
| 6875..6888 (cockatrices) | Spirit *atrice | 61 | 43/… | 7762 | 0 | 0 | 0 | 7763 |
| 7377+7378 | Pyrelord | 65 | 46/… | 8080 | 0 | 0 | 0 | 8078 |
| 6843+6844 | Bloated leech | 70 | 49/… | 7657 | 0 | 0 | 0 | 7656 |
| 6794+6795 | Spirit terrorbird | 74 | 50/… | 1010 | 0 | 0 | 0 | 1013 |
| 6818+6819 | Abyssal parasite | 77 | 50/… | 8910 | 0 | 0 | 0 | 7671 |
| 6992+6993 | Spirit jelly | 78 | 50/… | 8569 | 0 | 0 | 0 | 8570 |
| 7365+7366 / 7337+7338 / 7363+7364 | kyatt / larupia / graahk | 81 | 50/… | 5228/5228/5229 | 0 | 0 | 0 | 5230 |
| 6809+6810 | Karamthulhu overlord | 82 | 50/… | 7963 | 0 | 0 | 0 | 7964 |
| 6865+6866 | Smoke devil | 87 | 55/… | 7816 | 0 | 0 | 0 | 7818 |
| 6820+6821 | Abyssal lurker | 88 | 55/… | 7680 | 0 | 0 | 0 | 7684 |
| 6802+6803 | Spirit cobra | 90 | 55/… | 8152 | 0 | 0 | 0 | 8153 |
| 6827+6828 | Stranger plant | 91 | 55/… | 8208 | 0 | 0 | 0 | 8209 |
| 6889+6890 | Barker toad | 94 | 55/… | 7260 | 0 | 0 | 0 | 7256 |
| 6815+6816 | War tortoise | 95 | 55/… | 8286 | 0 | 0 | 0 | 8285 |
| 6813+6814 | Bunyip | 40 | 1/… | 7741 | — | — | 7742 | 7740 |
| 6839+6840 | Arctic bear | 10 | 60/… | 4925 | — | — | 4928 | 4929 |
| 7345+7346 | Obsidian golem | 10 | 60/… | 8050 | — | — | 8051 | 8052 |
| 6849+6850 | Granite lobster | 10 | 60/… | 8112 | — | — | 8114 | 8113 |
| 6798+6799 | Praying mantis | 107 | 60/… | 8069 | 0 | 0 | 0 | 8065 |
| 7335+7336 | Forge regent | 108 | 60/… | 7863 | 0 | 0 | 0 | 7864 |
| 7347+7348 | Talon beast | 110 | 60/… | 5989 | 0 | 0 | 0 | 5990 |
| 6800+6801 | Giant ent | 111 | 60/… | 7853 | 0 | 0 | 0 | 7854 |
| 6811+6812 | Hydra | 10 | 1/… | 7935 | — | — | 7936 | 7937 |
| 6804+6805 | Spirit dagannoth | 115 | 65/… | 7786 | 0 | 0 | 0 | 7780 |
| 6822+6823 | Unicorn stallion | 100 | 64/62/70/72/69 | 6376 | 6376 | 6376 | 6375 | 6377 |
| 6869+6870 | Wolpertinger | 619 | 1/1/95/95/95 | 8304 | 8304 | 8304 | 8306 | 8305 |
| 6873+6874 | Pack yak | 710 | 112/104/121/86/91 | 5782 | 5782 | 5782 | 5783 | 5784 |
| 7355..7360 | Fire/Moss/Ice titan | 476 | 40/30/40/30/40 | 7834/7844/7845 | 7834 | 7834 | 7832 | 7833/7843/7846 |
| 7341+7342 | Lava titan | 115 | 65/… | 7980 | 0 | 0 | 0 | 7979 |
| 7329+7330 | Swamp titan | 556 | 1/1/78/70/70 | 8222 | — | — | 8224 | 8226 |
| 7339+7340 | Geyser titan | 620 | 70/… | 7879 | 422 | 422 | 7878 | 7880 |
| 7349+7350 | Abyssal titan | 125 | 70/… | 7693 | 0 | 0 | 0 | 7692 |
| 7375+7376 | Iron titan | 694 | 65/… | 7946 | — | — | 7948 | 7947 |
| 7343+7344 | Steel titan | 750 | 70/… | 8183 | 8183 | 8183 | 8185 | 8184 |
| 8240..8249 | Clay familiar cls1..5 | — | — | — | — | — | — | — |

Rows with `-`/blank were not populated in `npc_configs.json` (Albino rat 6847, Beaver 6808, Ibis 6991, Fruit bat 6817, Ravenous locust 7372, Phoenix 8575, all clay familiars) — those must come from the rev-530 cache npc defs.

---

## 4. Interface ids

Internal names from `dumps/530/530_interface_names.txt` (`<id>\t<name>`, id = line-1):

| Interface | Internal name (530) | Used at | Purpose |
|---|---|---|---|
| **662** | `lore_stats_side` | `familiar/FamiliarManager.java:328,337` (`openTab(new Component(662))`, `setViewedTab(7)`); `SummoningTabListener.kt:9` | Summoning side tab (tab index **7**). Buttons: `51`=call familiar, `53`=dismiss (op 155 → confirm dialogue, op 196 → dismiss now), `67`=take BoB items, **any other button** = execute special move |
| **665** | `lore_bank_side` | `familiar/BurdenBeast.java:198` `openSingleTab` | BoB inventory-side panel; `InterfaceContainer.generateItems(..., 665, 0, 7, 4, 93)` opts `Examine/Store-X/All/10/5/1` |
| **669** | `pouch creation` (498 dump: `[14: Summoning Pouch Creation  ]`) | `SummoningCreator.java:43,64-66`; `SummoningCreationPlugin.java:26` | Infuse-pouch screen |
| **671** | `lore_bank` (498 dump: `[14: Familiar Inventory]`) | `familiar/BurdenBeast.java:55,190` | BoB container; `generateItems(..., 671, 27, 5, 6, 30)` opts `Examine/Withdraw-X/All/10/5/1` |
| **673** | `scroll creation again` (498 dump: `[14: Scroll Creation  ]`) | `SummoningCreator.java:48,64-66`; `SummoningCreationPlugin.java:27` | Transform-scroll screen |
| **747** | `topstat_lore` | `core/game/node/entity/player/link/InterfaceManager.java:422` (`openInfoBars`) | Summoning orb; pane slot `16` resizable / `73` fixed |
| **149** | inventory | `SummoningTrainingRoom.java:494` | cutscene tab restore |
| 666 | `summoning creation screen` | not referenced | ABSENT from code — candidate unused/alt |
| 672 | `pouch creation again` | not referenced | ABSENT from code |
| 722 | `summoning_side` | not referenced | ABSENT from code |
| 679 | `banner_summoning` | not referenced | — |
| 716 | `summoning_chocatrice` | not referenced | — |
| 664 | `bash focus summon` | not referenced | — |
| 663 | `lore_cats_side` | not referenced | pet side panel |
| 668 | `pick a puppy` | not referenced | pet shop |

Interface plumbing (`SummoningCreator.java:63-67`):
- `sendRunScript(757, "Iiissssss", ["List<col=FF9040>","Infuse-X…","Infuse-All…","Infuse-10…","Infuse-5…","Infuse…", 20, 4, 669<<16|15])` for pouches
- `sendRunScript(765, "Iiisssss", ["Transform-X…","Transform-All…","Transform-10…","Transform-5…","Transform…", 20, 4, 673<<16|15])` for scrolls
- `sendIfaceSettings(190, 15, 669, 0, 78)` / `sendIfaceSettings(126, 15, 673, 0, 78)` — accessmask on child **15**, slots 0..78

Component-669/673 opcodes (`SummoningCreationPlugin.java:44-60`): `155`→1, `196`→5, `124`→10, `199`→28, `234`→prompt amount, `166`→"List" (prints `CS2Mapping.forId(1186).getMap().get(pouchId)`). Buttons `17`/`18` swap between pouch and scroll modes. Slot fix-up: `slot > 50 ? slot-1 : slot`.

**Cache ENUM 1186** (`CS2Mapping`, i.e. idx2 `enum` config) maps *pouch item id → ingredient description string*. Needed for the "List" option. Also `component 168` is referenced in `SummoningCreationPlugin.java:62` for scroll examine.

Familiar NPC options registered (`familiar/FamiliarNPCOptionPlugin.java:21-25`): `pick-up`, `interact-with`, `interact`, `store`, `withdraw`. Item option `summon` (`familiar/SummonFamiliarPlugin.java:23`); pet item options `drop` / `release`.

---

## 5. Varp / varbit writes

| Var | Kind | Written at | Value |
|---|---|---|---|
| **448** | varp | `familiar/Familiar.java:689` / `:754`; `pet/Pet.java:63` | pouch item id (familiar) or pet item id; `-1` on dismiss |
| **1174** | varp | `familiar/Familiar.java:690` / `:757`; `pet/Pet.java:62` | familiar NPC original id; `-1` on dismiss |
| **1175** | varp | `familiar/Familiar.java:691` / `:756`; `pet/Pet.java:61,94,119` | `specialCost << 23` for familiars; for pets `(growth<<1) | (hunger<<9)`; dismiss writes literal `182986` |
| **1176** | varp | `familiar/Familiar.java:755` | `0` on dismiss (never written elsewhere — GUESS: unused/legacy) |
| **1177** | varp | `familiar/Familiar.java:771` | special-move points, 0..60 |
| **1178** | varp | `SummoningTrainingRoom.java:89,98,104`; `WolfWhistle.java:179-190` | Wolf Whistle quest state + Pikkupstix trapdoor open/closed flag: `(1<<11)+questVal` closed, `(2<<11)+questVal` open, `28893`/`32989` complete |
| **1160** | varp | (parent of 4280/4281/4282 per comment) | ORB visibility packing; legacy code added `243269632` raw |
| **4280** | varbit | `familiar/FamiliarManager.java:173`; `WolfWhistle.java:28` | summoning orb visibility (1 after Wolf Whistle) |
| **4281** | varbit | `familiar/FamiliarManager.java:174` | UNKNOWN, written 0 |
| **4282** | varbit | `familiar/FamiliarManager.java:175` | UNKNOWN, written 7 |
| **4534** | varbit | `familiar/Familiar.java:465` | familiar timer, whole minutes (`ticks/100`) |
| **4290** | varbit | `familiar/Familiar.java:466` | familiar timer half-minute flag (`ticks%100 > 49`) |
| **4277** | varbit | `pet/IncubatorTimer.kt:71` | incubator busy, Taverley region 11573 |
| **4221** | varbit | `pet/IncubatorTimer.kt:73` | incubator busy, Yanille region 10288 |

**Skill id: `Skills.SUMMONING = 23`** (`core/game/node/entity/skill/Skills.java:57`). `Skills.java:488` uses `staticLevels[SUMMONING] / 8` in combat-level.

Point-drain model (`familiar/Familiar.java:169-186, 228-241`): on summon drain `pouch.summonCost`; then drain `(levelRequired - summonCost + 1)` points evenly over `maximumTicks`. Special points cap 60, regen `+15` every 50 ticks. Warnings at ticks 100 ("1 minute") and 50 ("30 seconds"); dismiss at 0.

**ABSENT**: none of 4280/4281/4282/4534/4290/4277/4221 are in `Server/data/configs/varbit_definitions.json` — parent varp + bit range must be read from the rev-530 cache idx2 varbit group.

---

## 6. Obelisk / summoning LOCs

From `dumps/498/498_object_dump.txt` (`<id> - <name> - [op1..op5]`):

| Loc id | Name | Options |
|---|---|---|
| **28716** | Obelisk | `Infuse-Pouch`, `Renew-Points` |
| **28719** | Obelisk | `Infuse-Pouch`, `Renew-Points` |
| **28722** | Obelisk | `Infuse-Pouch`, `Renew-Points` |
| **28725** | Obelisk | `Infuse-Pouch`, `Renew-Points` |
| **28728** | Obelisk | `Infuse-Pouch`, `Renew-Points` |
| **28731** | Obelisk | `Infuse-Pouch`, `Renew-Points` |
| **28734** | Obelisk | `Infuse-Pouch`, `Renew-Points` |
| 28717/28718, 28720/28721, 28723/28724, 28726/28727, 28729/28730, 28732/28733, 28735/28736 | Inert obelisk | none — the "off" states, likely anim/transform partners |

**BUG in the source**: `SummoningCreationPlugin.java:88` lists `{28716, 28719, 28722, 28725, 28278, 28731, 28734}` — **28278 is a typo for 28728**. Port with 28728.

`ObeliskOptionPlugin` binds by option name (`SceneryDefinition.setOptionHandler("infuse-pouch"/"renew-points")`), so any loc with those ops works; the id list is only for the use-pouch-on-obelisk handler.

Known obelisk **locations**:
- Taverley / Pikkupstix cellar obelisk: **(2209, 5344, 0)** — hardcoded in `SummoningCreator.java:122` (`RegionManager.getObject(new Location(2209, 5344, 0))`) for the create-anim. Cellar region **11573**.
- Cellar ladders: trapdoor down at (2927, 3444, 0) → (2209, 5348, 0); ladder up at (2209, 5349, 0) → (2926, 3444, 0) (`SummoningTrainingRoom.java:120-130`).
- Other obelisk world coordinates: **ABSENT from the codebase** — they live in the rev-530 map/landscape data, not in any config. GUESS: must be recovered by scanning `Server/data/cache/` map groups for loc ids 28716/28719/28722/28725/28728/28731/28734, or authored from the wiki.

Related Wolf Whistle scenery (`SummoningTrainingRoom.java:70-76`): 28675 Trapdoor `Open`; 28676 Trapdoor `Climb-down`/`Close`; 28653 Ladder `Climb-down`; 28572 Ladder `Climb-up`; 28714 Ladder `Climb`; 28586 Dead body `Search` (gives 2× Wolf bones 2859).

Pet-incubator scenery (relevant if pets are in scope): 28336, 28359 (`Take-Egg`,`Inspect`), 29839, 29853/29854, 29855/29856, 29857 (`Add-Egg`), 29858..29863, 29864 (`Adjust-Temperature`).

---

## 7. Secondary data tables (also needed for a complete port)

**Familiar invisible skill boosts** (`SkillBonus`, applied via `Familiar.getBoost`):

| Familiar | Skill | +Level | Site |
|---|---|---|---|
| Dreadfowl | Farming | 1 | `DreadfowlNPC.java:76` |
| Granite crab | Fishing | 1 | `GraniteCrabNPC.java:41` |
| Desert wyrm | Mining | 1 | `DesertWyrmNPC.java:53` |
| Void ravager | Mining | 1 | `VoidFamiliarNPC.java:98` |
| Beaver | Woodcutting | 2 | `BeaverNPC.java:53` |
| Magpie | Thieving | 3 | `MagpieNPC.java:39` |
| Ibis | Fishing | 3 | `IbisNPC.java:44` |
| Pyrelord | Firemaking | 3 | `PyreLordNPC.java:54` |
| Forge regent | Firemaking | 4 | `ForgeRegentNPC.java:54` |
| Granite lobster | Fishing | 4 | `GraniteLobsterNPC.java:41` |
| Spirit larupia / kyatt / graahk / wolpertinger | Hunter | 5 | `SpiritLarupiaNPC.java:33`, `SpiritKyattNPC.java:30`, `SpiritGraahkNPC.kt:24`, `WolpertingerNPC.java:32` |
| Arctic bear | Hunter | 7 | `ArcticBearNPC.java:34` |
| Obsidian golem | Mining | 7 | `ObsidianGolemNPC.java:31` |
| Lava titan | Mining 10 + Firemaking 10 | | `LavaTitanNPC.java:30-31` |

**Pets** (`pet/Pets.java`, 85 rows; ctor = `babyItem, grownItem, overgrownItem, babyNpc, grownNpc, overgrownNpc, growthPerTick, summoningLevel, food...`):

| Group | item id range | npc id range | summ lvl | growth/tick |
|---|---|---|---|---|
| CAT / CAT_1..5 / HELLCAT / CAT_6 | 1555-1572, 7581-7583, 14089/14090/15092 | 761-779, 3503-3505, 8214/8216/8217 | 0 | 0.01543209876 |
| CLOCKWORK_CAT | 7771/7772 | 3598 | 0 | 0.0 |
| BULLDOG/DALMATIAN/GREYHOUND/LABRADOR/SHEEPDOG/TERRIER (+_1,_2 each) | 12512-12523, 12700-12723 | 6958-6969, 7237-7260 | 4 | 0.00333333 |
| GECKO (+4) | 12488/12489, 12738-12745 | 6915/6916, 7277-7284 | 10 | 0.005 |
| PLATYPUS (+2) | 12548-12553 | 7015-7020 | 10 | 0.00462963 |
| BROAV | 14533 | 8491 | 23 | 0.0 |
| PENGUIN (+2) | 12481/12482, 12762-12765 | 6908/6909, 7313-7317 | 30 | 0.00462963 |
| GIANT_CRAB (+4) | 12500/12501, 12746-12753 | 6947/6948, 7293-7300 | 40 | 0.00694444 |
| RAVEN (+5) | 12484/12485, 12724-12733 | 6911/6912, 7261-7270 | 50 | 0.00698888 |
| SQUIRREL (+4) | 12490/12491, 12754-12761 | 6919/6920, 7301-7308 | 60 | 0.00712250 |
| SARADOMIN_OWL / ZAMORAK_HAWK / GUTHIX_RAPTOR | 12503-12511 | 6949-6957 | 70 | 0.00694444 |
| EX_EX_PARROT | 13335 | 7844 | 71 | 0.0 |
| CUTE/MEAN_PHOENIX_EGGLING | 14627 / 14626 | 8578 / 8577 | 72 | 0.0 |
| RACCOON (+2) | 12486/12487, 12734-12737 | 6913/6914, 7271-7274 | 80 | 0.00294444 |
| VULTURE (+5) | 12498/12499, 12766-12775 | 6945/6946, 7319-7328 | 85 | 0.0078 |
| CHAMELEON | 12492/12493 | 6922/6923 | 90 | 0.00694444 |
| MONKEY (+9) | 12496/12497, 12682-12699 | 6942/6943, 7210-7227 | 95 | 0.00694444 |
| BABY_DRAGON (+3) | 12469-12476 | 6900-6907 | 99 | 0.0052 |

**Incubator eggs** (`pet/IncubatorEgg.java:9-21`): `(eggItem, level, incubationTime[ticks×100], productItem)` — PENGUIN(12483, 30, 30, 12481); RAVEN(11964, 50, 30, 12484); SARADOMIN_OWL(5077, 70, 60, 12503); ZAMORAK_HAWK(5076, 70, 60, 12506); GUTHIX_RAPTOR(5078, 70, 60, 12509); VULTURE(11965, 85, 60, 12498); CHAMELEON(12494, 90, 60, 12492); RED_DRAGON(12477, 99, 60, 12469); BLACK_DRAGON(12480, 99, 60, 12475); BLUE_DRAGON(12478, 99, 60, 12471); GREEN_DRAGON(12479, 99, 60, 12473).

**Quest gate**: `Quests.WOLF_WHISTLE` (`content/data/Quests.kt:136`), quest index **146**, journal index **145**, 1 quest point (`WolfWhistle.java:34`). `SummonFamiliarPlugin.java:29` blocks all summoning until complete. Dialogue key `392932` for the Fluffy cutscene; dynamic region **11573**.

**Consumable effect**: `content/data/consumables/effects/SummoningEffect.java` (summoning potion: `base + level*bonus`), `RestoreSummoningSpecial.kt`.

---

## 8. Target-repo absence checks (3draster / OSRS-Content)

- `OSRS-Content/osrs239-content/pack/stat.pack` ends at `22=construction`. **No `23=summoning`.** CONFIRMED ABSENT.
- `grep -ril "gold_charm|spirit_shard|summoning_pouch" OSRS-Content` → **no hits**. CONFIRMED ABSENT.
- `OSRS-Content/osrs239-content/pack/npc.server` contains only unrelated `*summon*` names (`rt_summon_elemental_fire`, `summonedzombie`, …) — no familiar.
- Existing pack layout to mirror: `OSRS-Content/osrs239-content/pack/` (`{loc,npc,varp,enum,param,struct,category,dbtable,dbrow}.{alloc,client,server}` + `N_*.pack` group files) and `OSRS-Content/osrs239-content/port/` (`names.map`, `vars.map`, `configs.map`, `constants.map`, `cs2_varps.map`, `categories*.map`). A ported-content folder would need its own allocation band — see the `servsplit-alloc-ledger` memory note (allocation-is-membership routing, one-cache rule).

---

## RISKS / UNKNOWNS

1. **Obelisk world coordinates are ABSENT from 2009scape source.** Only the Taverley cellar obelisk (2209, 5344, 0) is hardcoded. The other 6 obelisk placements exist only inside the rev-530 map data; they must be recovered by scanning `Server/data/cache/` landscape groups or authored by hand.
2. **Loc id 28278 in `SummoningCreationPlugin.java:88` is a typo for 28728.** Copying the list verbatim silently drops one obelisk.
3. **Scroll↔pouch mapping has three data bugs** that must be fixed during the port, not copied: `DOOMSPHERE_SCROLL(…, -1)` leaves Karamthulhu overlord (12023) scroll-less; `DEADLY_CLAW_SCROLL(…, 12162)` keys the Talon beast scroll to the *charm* not the pouch (12794); `THIEVING_FINGERS_SCROLL` has xp `47` where every neighbour has ≤8 (should be 0.9). `Familiar.executeSpecialMove` will print "Invalid scroll for pouch N - report!" for the first two.
4. **Familiar lifetimes look wrong for four rows**: VOID_TORCHER/VOID_SHIFTER 9400 ticks vs 2700 for the other two Void familiars; RUNE_MINOTAUR 15100 vs ≤6600 for the rest. These pass straight into the point-drain divisor, so copying them changes drain rate too. GUESS: these are 2009scape bugs, not the real values.
5. **Duplicate NPC registration for 6883/6884** (`SpiritPengatriceNPC.java` plain Familiar vs `CockatriceFamiliarNPC$SpiritPengatrice` Forager). Only one wins at runtime; BoB slots differ (0 vs 30). Must decide.
6. **Phoenix (pouch 14623, npc 8575) has no Familiar class** — ticks/special cost ABSENT; the scroll (14622) is commented out at `SummoningScroll.java:64`. **Sacred clay familiars (8240-8249)** also have no Familiar class. Both will need authored values.
7. **All 80 SPOTANIM ids are ≥1295 and 5 of 6 sound ids are >3826** — the documented OSRS/RS2 divergence point. There is no guarantee those cache groups transcode cleanly into the osrs239 dat2 format; models/anims for 166 familiar NPCs are the bulk of the work and none of them exist in the target cache.
8. **Interfaces 662/665/669/671/673/747 are 530-era IF3 layouts.** The target repo's era-feature table (`src/features/`) keys off lineage; per the `rs634-gameframe-session` memory note, IF3 layout families differ per era. Ported summoning interfaces may not mount in the rev-239 gameframe without rework, and `sendRunScript(757/765)` requires the 530 clientscripts which do not exist in osrs239.
9. **Cache ENUM 1186** (pouch id → ingredient text) is required by the "List" option. Its 530 contents were not dumped here — must be extracted from `Server/data/cache/` idx2 enum group.
10. **Varbit definitions for 4280/4281/4282/4534/4290/4277/4221 are unknown** (parent varp + bit range). 4281/4282 are literally commented "UNKNOWN" in `FamiliarManager.java:41-42`. Guessing bit ranges will corrupt varp 1160 neighbours.
11. **Charm drop data is 1222 NPC ids across 179 drop-table entries** keyed by rev-530 NPC ids. Those NPC ids do not map 1:1 onto osrs239 NPC ids; an id-translation table is required, and many 530 NPCs simply do not exist in 239.
12. **Skill id 23 must be added to `stat.pack`**, but the memory note says that file is "Fixed by the protocol (UPDATE_STAT carries this index) and by the client's own stat table, so they are authored, not imported." The osrs239 client has no slot 23 stat table entry — this is a client-side change, not just content.
13. `dumps/530/gfxs.txt` only describes ~25% of the needed gfx ids; the rest have no human-readable label, so verifying a transcoded spotanim is visually correct will be manual.
14. GUESS: `varp 1176` is legacy/unused (only ever written 0 on dismiss); `varp 1175`'s dismiss value `182986` = `0x2CA0A` is unexplained by any code path.

===== RECON: scape-cache-assets =====
# RECON: rev-530 cache at `2009scape/Server/data/cache/` — summoning assets & how to read them

## 0. TL;DR

- The 530 cache is a **dat2/JS5 cache on the RS2 lineage** (`game=rs2, epoch=dat2`), the same branch 3draster already reads as `643` / `727`. Reference tables are **format 6** (idx27 is format 5) — all supported by `3rd/rscache/src/reference_table.c`.
- **All summoning content is present**: 84 pouch objs, 114+ scroll objs, ~120 familiar NPCs, 6 interfaces, models, seqs, spotanims.
- Existing 3draster tooling (`find_named`, `find_anims`, `dump_stats`, `port_npc`, `cs2`, `audioprobe`) **already reads this cache** when pointed at it with `--rev 643`, but **four decoders are wrong at 530** (frame, sequence, component, obj). Two of those (frame, obj) are fixed by adding a `rev_dat2_rs530.c` profile; two need genuinely new codec versions.
- `cachepack` **cannot** unpack it (OldSchool config-group layout only).
- Existing docs explicitly exclude Summoning: `docs/PORTING_GUIDE.md:35` ("skip bots/holiday/Summoning/RS2-only") and `docs/SCAPE2009_CONTENT_PORT_QUEUE.md:65` ("Summoning is not in OSRS"). This port reverses both.

---

## 1. Cache format / revision + idx layout

Files: `main_file_cache.dat2` (91,702,293 bytes) + `idx0..idx28` + `idx255`. 29 indices. Standard 520-byte sectors, 8-byte headers (no group id exceeds 0xFFFF, so the large-header path is never taken — `3rd/rscache/src/dat2disk.c:99 header_size_for_archive`).

Measured from idx255 (reference tables) — `format`, group count, total file count:

| idx | 530 role (confirmed) | fmt | groups | files | named |
|---|---|---|---|---|---|
| 0 | animation frames | 6 | 2724 | 219678 | no |
| 1 | framemaps / skeletons | 6 | 2435 | 2435 | no |
| 2 | configs (sharded by kind) | 6 | 20 | 8288 | no |
| 3 | interfaces | 6 | 834 | 31622 | **yes** |
| 4 | sound effects | 6 | 6750 | 6750 | no |
| 5 | maps (`mX_Z`/`lX_Z`) | 6 | 3682 | 3682 | **yes** |
| 6 | music tracks | 6 | 668 | 668 | **yes** |
| 7 | models | 6 | 45472 | 45472 | no |
| 8 | sprites | 6 | 1709 | 1709 | **yes** |
| 9 | textures | 6 | 680 | 680 | no |
| 10 | binary (huffman) | 6 | 1 | 1 | yes |
| 11 | music jingles | 6 | 390 | 390 | no |
| 12 | clientscripts (CS2) | 6 | 2063 | 2063 | no |
| 13 | fonts | 6 | 18 | 18 | yes |
| 14 | music samples | 6 | 486 | 486 | no |
| 15 | music patches | 6 | 148 | 148 | no |
| 16 | **loc** (id>>8 / &0xFF) | 6 | 165 | 42004 | no |
| 17 | **enum** (id>>8 / &0xFF) | 6 | 9 | 2196 | no |
| 18 | **npc** (id>>7 / &0x7F) | 6 | 68 | 8590 | no |
| 19 | **obj** (id>>8 / &0xFF) | 6 | 58 | 14654 | no |
| 20 | **seq** (id>>7 / &0x7F) | 6 | 88 | 11155 | no |
| 21 | **spotanim** (id>>8 / &0xFF) | 6 | 8 | 1993 | no |
| 22 | **varbit** (id>>10 / &0x3FF) | 6 | 6 | 5826 | no |
| 23 | unidentified | 6 | 3 | 216 | yes |
| 24 | quickchat | 6 | 2 | 1031 | no |
| 25 | quickchat menus | 6 | 2 | 86 | no |
| 26 | **materials** (texture props) | 6 | 1 | 1 | no |
| 27 | particles | **5** | 1 | 1 | no |
| 28 | defaults/billboards | 6 | 8 | 20 | no |

`idx2` config groups present (group → file count): `1:127, 2:270, 3:308, 4:185, 5:546, 7:297, 11:670, 15:57, 16:1372, 18:552, 19:632, 20:20, 21:8, 22:842, 23:229, 24:11, 25:128, 26:507, 32:1431, 34:96`. 2009scape names three of them: **3 = cloth/idk** (`ClothDefinition.java:61`), **26 = struct** (`Struct.java:57`), **32 = bas/RenderAnimation** (`RenderAnimationDefinition.java:72`).

### idx-number diff vs osrs239

`cache.osrs239` ships idx0–15, 17–22, 24 (**no idx16, no idx23**, no 25–28). The 0..15 block agrees id-for-id with 530. Above 15 the branches disagree, and 3draster **already encodes exactly this split**:

- `3rd/rscache/src/dat2disk.h:169-200` — `enum RSCache_Dat2Rs2DiskTable`: LOC=16, ENUM=17, NPC=18, OBJ=19, SEQ=20, SPOTANIM=21, VARBIT=22, MATERIALS=26, PARTICLES=27, DEFAULTS=28.
- `3rd/rscache/src/dat2disk.h:130-158` — `enum RSCache_Dat2OsrsDiskTable`: 18=worldmap geography, 19=worldmap, 20=worldmap ground, 21=dbtable index, 22=animayas, 24=gamevals; 16/17/23/25–28 unused.
- The RS2 shard widths are in `3rd/rscache/src/rscache_profile.c:124-180` (`RSCache_RecordAddressFor`, `IsRs2Dat2` branch) and match 2009scape's shifts exactly (loc>>8, npc>>7, obj>>8, seq>>7, spotanim>>8, enum>>8, varbit>>10, struct = config group 26).

**So the 530 idx layout is byte-for-byte the layout 3draster already calls "RS2 dat2".** No new table mapping is needed.

---

## 2. 2009scape's cache-reading code — the 530 config spec

`/Users/matthewevers/Documents/git_repos/2009scape/Server/src/main/core/cache/` (7540 lines total):

| file | lines | what it is |
|---|---|---|
| `Cache.java` | 240 | index opener; names indices at `:179` (3=iface), `:197` (18=npc), `:207` (21=gfx), `:217` (20=anim), `:227` (16=scenery), `:237` (19=item) |
| `CacheFile.java` / `CacheFileManager.java` | 148/293 | sector chain + container decompress |
| `misc/ContainersInformation.kt` | 145 | reference-table parser (fmt 5/6) |
| `def/impl/ItemDefinition.java` | 1699 | **530 obj opcode table**, `parseDefinition` at `:299`, opcode chain `:304-450` |
| `def/impl/NPCDefinition.java` | 1119 | 530 npc opcode table, `:266` |
| `def/impl/SceneryDefinition.java` | 1695 | 530 loc opcode table, `parseDefinition` `:615`, opcode 1/5 model nesting `:626` |
| `def/impl/AnimationDefinition.java` | 208 | **530 seq opcode table**, `readValues` `:108` |
| `def/impl/IfaceDefinition.java` | 554 | **530 IF1+IF3 component decoder**, `:131` picks IF1/IF3, `decodeIf1` `:139`, `decodeIf3` `:338`, `parseIf3Type` `:488` |
| `def/impl/GraphicDefinition.java` | 188 | spotanim |
| `def/impl/VarbitDefinition.java` | 173 | varbit (idx22) |
| `def/impl/CS2Mapping.java` / `DataMap.java` | 255/117 | enums (idx17) |
| `def/impl/Struct.java` | 100 | struct (idx2 g26) |
| `def/impl/RenderAnimationDefinition.java` | 330 | BAS (idx2 g32) |
| `def/impl/ClothDefinition.java` | 204 | identkit (idx2 g3) |

### Format deltas that matter (530 vs what rscache implements)

**obj (ItemDefinition.java:304-450)** — 530 uses opcodes rscache has no case for:
- `96` = itemType (1 byte) — **not in rscache**, 2405 records stop here
- `121`/`122` = lendId / lendTemplateId (u16 each) — **not in rscache**
- `125`/`126` = 3 bytes each (wear offsets); `127`–`130` = byte+short each — **not in rscache**
- `23`/`25` read a bare u16 with **no trailing offset byte** (2009scape comments out `buffer.get()` at `:339`/`:345`)
- rscache's default codec has cases `13/14/15` (OSRS wearpos/untradeable), `44-54`, `139/140/148/149/160/200-202` which do not exist at 530 → they desync mid-record.
  - rscache table: `3rd/rscache/src/datatypes/dat2_config_obj.c:917-1231` (case labels).

**seq (AnimationDefinition.java:108-175)** — 530 layout:
- `1` = u16 count, then durations[u16], frame-lo[u16], frame-hi[u16]<<16 — **same as OSRS**
- `12` = u8 count + chat frame ids — same as OSRS
- `13` = **u16 count**, then per-entry `u8 n` + `medium` + `(n-1)×u16` frame sounds — **matches no rscache codec** (V1/V2 use a `u8` count; V3 reads `13` as `anim_maya_id`)
- `14` = bare flag, no payload (V3 reads `14` as framed frame-sounds)
- unknown opcodes silently continue (a 2009scape bug, but it tells you 530's set stops at 14)

**IF3 component (IfaceDefinition.java:488-540)** — 530 is a **hybrid**:
- type 5 (SPRITE): `spriteId(4) angle(2) flags(1) alpha(1) outline(1) shadow(4) vFlip(1) hFlip(1)` — **no trailing colour int, flips in V,H order** = the **OSRS** rule, *not* 643's (`3rd/rscache/src/revisions/rev_dat2_rs643.c:13-17` documents 643 as H,V + trailing colour)
- type 6 (MODEL): the two size-override shorts are each gated on **its own** mode (`if dynWidth != 0` / `if dynHeight != 0`) = the **643** rule, not OSRS's
- ⇒ `RSCACHE_DAT2_COMPONENT_DECODE_ERA_643` (selected purely by `RSCache_IsRs2Dat2` at `3rd/rscache/src/datatypes/dat2_component.c:245`) will mis-decode every 530 sprite component.
- 530 interfaces are a **mix of IF1 and IF3** (`IfaceDefinition.java:131`). Measured: iface 662/665/669/671/673/747 are 100% IF3; iface 149 is IF1.

**npc** — no delta: `dump_stats --rev 643` decodes **8590/8590 to the terminator, 0 short**.

---

## 3. Tools that can already dump/unpack the 530 cache

### In 3draster (all built, all work on the 530 cache with `--rev 643`)

| tool | path | verified on 530 |
|---|---|---|
| `find_named` | `3rd/rscache/tools/find_named/find_named` | ✅ `--npc/--obj/--seq/--spotanim/--framemap`, `--name … --type npc\|obj\|spotanim`. `--type loc` returns nothing (locs are not name-scanned) |
| `find_anims` | `3rd/rscache/tools/find_anims/find_anims` | ✅ walks npc→bas→seq→framemap (framemap id is wrong, see §4) |
| `dump_stats` | `tools/dump_stats/dump_stats` | ✅ wrote `npc530.csv` (8590 rows) + `obj530.csv` (14654 rows); `--raw-dir` also dumps undecoded record bytes |
| `port_npc` | `3rd/rscache/tools/port_npc/port_npc` | ✅ `--from-rev 643 <530> --to-rev osrs239 cache.osrs239 --npc 6830` produced a full remap plan (npc 6830→16175, model 30443→61615, chathead 31211→61616, seq 8297→8542, 8291→8543) |
| `cs2` | `3rd/rscache/tools/cs2/cs2` | ✅ `decompile --cache <530> --rev 643 --out DIR 100` decompiled cleanly |
| `audioprobe` | `3rd/rscache/tools/audioprobe/audioprobe` | ✅ `--sweep`: 485 samples, 668 tracks, 390 jingles, 148 patches — **0 failures** |
| `cachepack` | `3rd/rscache/tools/cachepack/cachepack` | ❌ **refuses**: `"obj is sharded across groups in this cache — this tool handles the OldSchool config-group layout only"` (same for seq/spotanim/varbit); npc/param/varp/varc/hitsplat/struct all report 0% |
| `dump_interface` | `tools/dump_interface/dump_interface` | ❌ no `--rev`; uses the legacy `src/osrs/rscache` API and the OSRS component era only |

**ABSENT: there is no tool that can unpack the 530 cache into an editable tree.** `cachepack` is the only unpack/pack machinery and it is OldSchool-config-group only.

### In 2009scape
- `Tools/Frostys Cache Editor/` — Java Eclipse project (`src/com/alex/store`, `src/com/editor/{item,npc,object,model}`) — a 530-era cache editor. Not built.
- `Tools/RSDataSuite v1.2.2.jar`, `Tools/Drop Table Tool.html`, `Tools/diff_json_simple.py`.
- `Server/data/configs/xteas.json` — map XTEA keys (needed: `RSCache_MapLocsEncrypted` returns true for RS2 ≥414, `3rd/rscache/src/datatypes/maps.c:413-418`).

---

## 4. Does 3draster already decode 530-era assets? — yes, mostly, via the RS2 lineage

`3rd/rscache/src/revisions/revisions.c:42-45` registers `643/rs643` and `727/rs727`. **ABSENT: no `530` profile.** But `src/app.c:3051` builds the profile from the manifest's four stated fields via `RSCache_ProfileForIdentity`, so `[cache:boot] epoch=dat2 game=rs2 revision=530` boots today with every codec on AUTO.

What AUTO gives at revision 530, per-datatype:

| datatype | gate | 530 result | correct? |
|---|---|---|---|
| model | `model.c:2869` `IsRs2Dat2` → OB3 (authoring default only; decode reads the 2-byte **trailer** magic, `model.c:1982-1990`) | census of all 45,472 models: **39,694 OB3 + 5,778 OB2, zero V2/V3** | ✅ both already decoded |
| framemap | `dat2_framemap.c:18-24`, RS2 ≥530 → V3 | V3 (masks u16 present) | ✅ `--framemap 0` → 251 transforms, sane labels |
| **frame** | `dat2_frame.c:19-22`, RS2 ≥610 → V2 | **V1** under AUTO — but the **643 profile pins V2** (`rev_dat2_rs643.c:50`) | ⚠️ AUTO is right; using `--rev 643` is **wrong**. Proof: `idx0` group 1662 file 0 head `0000cf00…` → framemap id **1491** at offset 0 (V1, in range: idx1 has 2435 groups) vs **54090** at offset 1 (V2, garbage). `port_npc` reported exactly `framemaps: 54090 -> 54090` |
| loc / flo | `dat2_config_loc.c:36`, `dat2_config_flo.c:12` `IsRs2Dat2` → RS2 | RS2 | ✅ (matches `SceneryDefinition.java:626` opcode-1/5 nesting) UNVERIFIED at record level |
| npc | `dat2_config_npc.c:26-42`, RS2 ≥669 → BUILD669 | base RS2 codec | ✅ 8590/8590 exact |
| **obj** | `dat2_config_obj.c:22-38`, RS2 ≥670 → BUILD670 | base codec | ❌ **9178/14654 exact, 5476 short** — missing opcodes 96/121/122/125-130 (§2) |
| **sequence** | `dat2_config_sequence.c:1139-1155` — calls `RevisionAtLeastOsrs` with `default_when_unknown=true`, which an `rs2` profile can never satisfy → falls through to **V3** | **V3 (wrong)** | ❌ 649 desync errors across 11,155 seqs (~5.8%). **Pre-existing on the whole RS2 branch**: `cache.void634` = 2977 errors, `cache.rs727_preeoc` = 3139, `cache.osrs239` = 0 |
| **component** | `dat2_component.c:245` `IsRs2Dat2` → `ERA_643` | **ERA_643 (wrong for type 5)** | ❌ needs a third era (§2) |
| texture / materials | `dat2_proctexture.c:60-64` RS2 ≥474 + table 26 present → procedural. Flags at 537/555/582/629 (`:23-34`) — 530 satisfies **none** | procedural, no MOD_OP / no anim-wins / no combine / **no alpha-blending** | ✅ **exact**: material table decoder `dat2_proctexture.c:197-214` yields `1+1+1+1+1+1+1+1+1+2 = 11` bytes/material without `HAS_ALPHA_BLENDING`. Measured idx26 g0 = **7482 = 2 + 680×11**. (634 control: 21842 = 2 + 910×24.) |
| sound / music | `sound_synth.c:20-24` RS2 ≥377 → SYNTH | SYNTH | ✅ `--sweep` 0 failures |
| font metrics | `dat2_font_metrics.c:14` `IsRs2Dat2` → V2 | V2 | UNVERIFIED |
| CS2 dialect | `src/engine/cs2_opcode_dialect.h:43` `RS2_DAT2` (*"verified against the 634 client"*) | RS2_DAT2 | ⚠️ 530 vs 634 opcode numbering UNVERIFIED; script 100 round-tripped |
| maps | `maps.c:413-418` RS2 ≥414 → **XTEA required** | encrypted | needs `2009scape/Server/data/configs/xteas.json` |

`src/features/features.c:188-194` (`ToriRS_Features_ForCache`): anything not `(dat2, oldschool)` resolves to **`ToriRS_Features_LostCity()`** — an rs2/530 cache would get 2004 pathing/approach behaviour unless a manifest states `[features:boot] era=`.

---

## 5. What summoning content actually lives in the 530 cache

All ids below are **rev-530 ids** and collide with unrelated osrs239 records — every one must be remapped.

**Objs (idx19)** — from `obj530.csv`:
- **84 objs carry inv-op `"Summon"`**, id range **12007–14623**. Pouches are the odd ids 12007..12123 (`Spirit terrorbird pouch` 12007, `Dreadfowl pouch` 12043, `Spirit wolf pouch` 12047, `Pack yak pouch` 12093, …); the even id +1 is the "null" noted/uncharged template (`noted_id` pairs, e.g. 12047→12048).
- **73 distinct pouch inventory models**, range **30582–45437**.
- Scrolls at **12421+** (`Herbcall scroll` 12422, `Fruitfall scroll` 12423, `Howl scroll` 12425, `Thieving fingers scroll` 12426, …), 114 records ≥12400.
- `Summoning obelisk` obj **14657** (model 31686).
- Every summoning obj decodes exactly (`record_bytes == decoded_bytes`, `stop_opcode = -1`) — the 5476 short obj decodes are elsewhere in the table.

**NPCs (idx18)** — ~120 familiars. Sampled: `Spirit wolf` 6829 (unattackable)/6830 (lvl 26); `Spirit terrorbird` 6794/6795; `Spirit cobra` 6802/6803; `Spirit dagannoth` 6804/6805; `Spirit scorpion` 6837/6838; `Spirit spider` 6841/6842; the 7 `Sp. *atrice` pairs 6875–6888; `Spirit jelly` 6992/6993; `Spirit kalphite` 6994/6995; `Spirit mosquito` 7331/7332; `Spirit larupia` 7337/7338; `Spirit Tz-Kih` 7361/7362; `Spirit graahk` 7363/7364; `Spirit kyatt` 7365/7366.
- Example record (`npc 6830`): `models 30443`, `chathead 31211`, `bas_type_id 1326`, `size 2`, `scale 104x104`, ops `[0] Interact, [1] Attack`, all anim slots `-1` (animation comes from BAS 1326 → seqs **8297** idle / **8291** walk → frame group **1662** → framemap **1491**).

**Interfaces (idx3)** — all present, all IF3:

| id | children | role (2009scape) |
|---|---|---|
| 662 | **198** | Summoning sidebar tab (`SummoningTabListener.kt:9`; buttons 51 call, 53 dismiss, 67 BoB withdraw-all) |
| 665 | 1 | BoB single-tab (`BurdenBeast.java:198`) |
| 669 | 24 | Summoning creation / obelisk infusion (`SummoningCreator.java:43`) |
| 671 | 31 | Beast-of-burden container (`BurdenBeast.java:190`) |
| 673 | 23 | Scroll creation (`SummoningCreator.java:48`) |
| 747 | 6 | (present; not referenced by the summoning dir) |

**Seqs (idx20)** — all decode: 7660 (summon, 16 frames), 827/828, 8320/8321, 8506/8507, 8510, 9068, 9173, 9224.
**Spotanims (idx21)** — 1296 (model 31427, seq 7662), 1314 (31388/7663), 1387 (31442/8151), 1522 (31638/8587), plus 1295/1306/1331/1332/1334/1342/1354/1355/1375/1410/1412.
**Sprites (idx8)** are name-hashed; **none** of `summoning`, `staticons`, `sideicons`, `skillicons`, … match. The summoning tab icon must be found by decoding iface 662's type-5 components (blocked on the component-era fix) — GUESS.

**Server-side facts from 2009scape** (12,424 lines under `Server/src/main/content/global/skill/summoning/`): skill id **`SUMMONING = 23`** (`Skills.java:57`) — one past OSRS's 0–22; familiar state packs into **varp 1160** (`FamiliarManager.java:36`).

---

## RISKS / UNKNOWNS

1. **Sequence codec is wrong for the entire RS2 branch, not just 530.** `dat2_config_sequence.c:1139-1155` uses `RevisionAtLeastOsrs(..., default_when_unknown=true)`, which an `rs2` profile structurally cannot satisfy → always V3. 649 bad seqs on 530, 2977 on `cache.void634`, 3139 on `cache.rs727_preeoc`. Fixing this is a **pre-existing-bug** touch that will change 634/727 behaviour; A/B before blaming the summoning work.
2. **Component decoder needs a third era.** 530 is 643's type-6 rule + OSRS's type-5 rule. `dat2_component.c:245` derives the era from `IsRs2Dat2` alone — there is no seam for a third value without touching that function. Every summoning interface is IF3, so this is on the critical path.
3. **obj codec: 37% of 530 objs don't decode.** All summoning pouches/scrolls happen to decode, but any obj-adjacent crawl (drop tables, category walks) over the 530 cache is reading truncated records.
4. **`--rev 643` silently corrupts animation frames on 530** (FRAME_V2 pin). Anything already run with `--rev 643` against this cache — including the `port_npc` plan above — has garbage framemap ids. A `rev_dat2_rs530.c` that leaves FRAME on AUTO fixes it; do not copy 643's pin list wholesale.
5. **Framemap V3 threshold is exactly 530** (`dat2_framemap.c:19-21` comment "RS >= 481/530"). 530 sits on the boundary; `--framemap 0` looks sane but a systematic consumption check across all 2435 framemaps has not been run.
6. **`port_npc` does not remap framemaps or frame archives** (`framemaps: 54090 -> 54090`, `frame_archives: 1662 -> 1662`). On a real port those ids will collide with osrs239's existing idx0/idx1 groups. UNVERIFIED whether the tool checks for collision.
7. **No unpacker.** `cachepack` refuses RS2 sharded configs, so there is no path today from the 530 cache to an editable `configs/all.<type>` tree. Either teach `cachepack` the RS2 shard layout or write a one-off exporter. This is probably the single largest engineering item.
8. **CS2 opcode numbering at 530 vs 634 is unverified.** `cs2_opcode_dialect.h:43` says RS2_DAT2 was verified against the *634* client only. Interface 662's 198 components will carry CS2 hooks; if the numbering moved between 530 and 634, hooks land on the wrong opcodes.
9. **Skill id 23 exceeds OSRS's stat space.** `SUMMONING = 23` in a 0–22 world; osrs239 stat-transmit packets, the skill-guide tables, and XP-drop plumbing all assume 23 skills. Whether the wire and the 239 interfaces can carry a 24th stat at all is unexamined here.
10. **Map/loc XTEA** for the 530 cache lives in `2009scape/Server/data/configs/xteas.json` and has not been format-checked against `3rd/rscache/src/xtea_config.c`.
11. **Texture ids are cache-local.** 530 has 680 materials, osrs239 uses the sprite-backed system entirely. Every `retexture` and every model face texture on a ported summoning model needs an explicit `--texture-map` (`EXCEPTIONS.md` A5) or it will index into an unrelated osrs239 texture.
12. **Sprites are unreachable by name.** No summoning sprite name hash matched; the tab icon / orb art can only be found by decoding components (blocked on risk 2) or by brute-forcing the hash space. GUESS.
13. **Loc records at 530 are entirely unverified.** No tool dumps them (`find_named --type loc` returns nothing) and `dump_stats` covers only npc/obj. The summoning obelisk *loc* (as opposed to obj 14657) has not been located.
14. **`ToriRS_Features_ForCache` maps rs2 → LostCity features** (`features.c:188-194`). Anything booting a 530 cache to compare behaviour gets 2004 pathing unless the manifest overrides `[features:boot] era=`.
15. **Documented policy conflict.** `docs/PORTING_GUIDE.md:35` and `docs/SCAPE2009_CONTENT_PORT_QUEUE.md:65` both instruct agents to skip Summoning. Any loop-driven agent reading those files will refuse or de-prioritise this work until they are amended.
16. **`OSRS-Content/osrs239-content/port/` is NOT a ported-content home** — it holds `names.map`, `configs.map`, `constants.map`, `vars.map`, `name_diff.signed`, i.e. cross-revision *mapping* tables. The "distinct folder clearly marked as ported" the goal asks for does not exist yet and has no precedent in this tree.

Scratch artifacts (read-only inspection, outside both repos): `/private/tmp/claude-501/-Users-matthewevers-Documents-git-repos-3draster/24607df3-1c87-44c1-b268-71a42635e4ab/scratchpad/` — `rt530.py` (dat2 reference-table reader), `fl530.py` (filelist splitter), `npc530.csv`, `obj530.csv`, `obj_pouch.txt`.

===== RECON: scape-client-interfaces =====
# Recon: rev-530 CLIENT side of Summoning (2009scape)

## 1. Is there a client? — **ABSENT**

`/Users/matthewevers/Documents/git_repos/2009scape` has **no client source and no client jar**. Verified:

| Path | What it actually is |
|---|---|
| `build`, `run` | bash wrappers that only maven-build/run `Server/` → `builddir/server.jar` |
| `Proto/` | `Management.proto` only (server management RPC) |
| `Tools/` | `RSDataSuite v1.2.2.jar`, `Drop Table Tool.html`, `Frostys Cache Editor/` (a **cache editor**, decompiled `.class` tree + `nullmain_file_cache.dat2`/`.idx255` stubs) |
| `Server/libs/` | `ConstLib-1.4.jar`, `PrimitiveExtensions-1.0.jar` — server deps |
| `Server/data/cache/` | **the real rev-530 cache**, `main_file_cache.dat2` (91.7 MB) + idx0–28 + idx255 |
| `dumps/` | `498/`, `530/`, `562/`, and **`scripts/` — 5378 decompiled `.cs2` files** |

`find . -iname "*.jar"` returns only the three above. There is no `client/`, no `Client-TS`, no deob.

**But**: the cache + the CS2 dumps together give you everything the client side needs. I decoded the 530 cache directly with ~60 lines of Python (sector walk → container `bzip2`/`gzip` → reftable → group split) — no tooling required. Everything below marked "decoded" came out of `Server/data/cache/` itself, not from the dumps.

Cache index map confirmed by decode:

| idx | contents | slots | addressing |
|---|---|---|---|
| 2 | configs (groups 1,2,3,4,5,7,11,15,16,18,19,20,21,22,23,24,25,26,32,34) | 35 | — |
| 3 | **interfaces** | 838 | group = iface id, file = component id |
| 12 | clientscripts | **2065** | group = script id |
| 16 | obj (item) defs | 42004 | `id>>8`, `id&0xFF` |
| 17 | **enums** | 2304 | `id>>8`, `id&0xFF` (`CS2Mapping.forId`, `Server/src/main/core/cache/def/impl/CS2Mapping.java:106`) |
| 22 | **varbits** | 5826 | `id>>10`, `id&0x3FF` |

`idx3` has exactly 838 slots and `dumps/530/530_interface_names.txt` has exactly 838 lines — that dump **is** rev 530's interface name table.

**Key decode**: Summoning's internal Jagex codename in 530 is **`lore`**. That is why grepping for "summoning" misses most of it.

---

## 2. rev-530 interface ids

All confirmed present in `Server/data/cache/main_file_cache.idx3` (size, sector) and cross-referenced to `dumps/530/530_interface_names.txt` + `ConstLib-1.4.jar → org/rs09/consts/Components`.

| Purpose | id | cache name | components (decoded) | server reference |
|---|---|---|---|---|
| **Summoning tab / familiar panel** | **662** | `lore_stats_side` | **198** (0–197) | `InterfaceManager.java:44,397,442`; `FamiliarManager.java:328,337` |
| **Summoning orb (minimap)** | **747** | `topstat_lore` | **6** (0–5) | `InterfaceManager.java:421` |
| Pet side panel | 663 | `lore_cats_side` | 28 | ABSENT from server (unused) |
| **BoB — inventory half** | **665** | `lore_bank_side` | 1 | `BurdenInterfacePlugin.java:23`, `BurdenBeast.java:198` |
| **BoB — familiar half** | **671** | `lore_bank` | 31 | `BurdenInterfacePlugin.java:24`, `BurdenBeast.java:190` |
| **Pouch infusion (obelisk)** | **669** | `pouch creation` | 24 | `SummoningCreator.java:43`, `SummoningCreationPlugin.java:26` |
| **Scroll transform** | **673** | `scroll creation again` | 23 | `SummoningCreator.java:48`, `SummoningCreationPlugin.java:27` |
| Summoning login banner | 679 | `banner_summoning` | 7 | `LoginConfiguration.java:258` (message child = 1) |
| **Skills/stats tab** | **320** | `stats` | 152 | `Components.STATS_320`, `InterfaceManager.java:390` |
| **Skill guide** | **499** | `skill_guide_v2` | 28 | `StatsTabInterface.kt:21,28` |
| Level-up chatbox | 740 | — | 5 | `LevelUp.java:117-118`, `sendFlashingIcons` |
| Level-up "advance" popup | 741 | — | 12 | `StatsTabInterface.kt:24` |
| Tooltip host (used by 669/673) | 79 | — | 34 | `765.cs2` → `WidgetPointer(79,31)`/`(79,17)` |
| Unused-by-server summoning ifaces | 666 (`summoning creation screen`), 672 (`pouch creation again`), 716 (`summoning_chocatrice`), 717 (incubator controls), 722 (`summoning_side`), 668/675/737 | — | — | **ABSENT** from server code — decorative/legacy |

**Dismiss dialogue = NOT an interface.** It is the standard 5-option chatbox dialogue: `SummoningTabListener.kt:42` → `open("dismiss_dial")` → `DismissDialoguePlugin.java:41` → `interpreter.sendOptions("Dismiss Familiar", "Yes", "No")` (or `"Free pet"` for pets). No dedicated widget group.

**Infusion = no dedicated obelisk interface.** `ObeliskOptionPlugin.java` is a scenery-option handler (`infuse-pouch`, `renew-points`); `infuse-pouch` just opens 669.

### Interface 662 button contract (`SummoningTabListener.kt:9-64`)

| child | op | action |
|---|---|---|
| 51 | any | Call familiar |
| 53 | 155 | Dismiss → opens `dismiss_dial` |
| 53 | 196 | Dismiss now (no confirm) |
| 67 | any | BoB withdraw-all |
| *else* | any | `executeSpecialMove(FamiliarSpecial(player))` |

Decoded 662 geometry (from the cache, not the dumps):

| child | type | x,y,w,h | role |
|---|---|---|---|
| 1 | 6 model | 53,13,78,86 | familiar head (`setWidgetNpcHead`) |
| 20–39 | 6 model ×20 | 3,0 … 129,0 | special-point bar, **3 points per segment** (`756.cs2`) |
| 41 | 4 text | 0,-1,146,15 | `"<cur>/<max>"` summoning points (`755.cs2`) |
| 43 | 4 text | 29,200,32,15 | time remaining `"N.00"` / `"N.30"` (`752.cs2`) |
| 45 | 4 text | 131,200,32,15 | pet growth % |
| 46,48 | 5 sprite | 126,173 / 31,173 | pet growth/hunger icons |
| 51 | 5 sprite | 29,225,25,25 | Call button |
| 53 | 5 sprite | 137,225,25,25 | Dismiss button |
| 54 | 5 sprite | 13,145,166,20 | familiar-name plate |
| 66 | 0 layer | 76,218,38,38 | shard/scroll count holder |
| 67 | 5 sprite | 84,227,21,21 | BoB button |
| 71,72 | 0 layer | 7,27,32,32 | pouch/pet mode swap |
| **74** | 0 layer | 31,51,32,32 | **special-move button** (dynamic children) |
| 197 | 0 layer | 0,0,190,261 | root |

Type histogram for 662: 74 layers, 4 rects, 7 texts, 91 sprites, 22 models.

---

## 3. The summoning ORB (747) — how the server drives it

**In rev 530 the orb has NO special-move button.** Decoded, 747 is structurally *identical* to the hitpoints orb 748 — 6 components, same geometry:

| child | type | x,y,w,h | 747 sprite | 748 sprite |
|---|---|---|---|---|
| 0 | layer | 0,0,57,34 | — | — |
| 1 | sprite | 0,0,57,34 | 1206 | 1206 |
| 2 | sprite | 1,1,31,31 | **1244** | 1208 |
| 3 | sprite | 1,1,31,31 | 1245 | 1245 |
| 4 | **text** | 31,15,24,14 | — | — |
| 5 | sprite | 6,7,20,20 | **1200** | 1197 |

Child 4 is the number, child 5 is the skill icon, children 2/3 are the fill+drain pair. The special-move button lives on the **tab** (662:74), not the orb. (The `747:9…26` special-move components in `dumps/scripts` are a **later** revision — see RISKS.)

Mounting (`Server/src/main/core/game/node/entity/player/link/InterfaceManager.java:410-424`, `openInfoBars()`):

```java
PacketRepository.send(Interface.class, new InterfaceContext(player, getWindowPaneId(),
        isResizable() ? 16 : 73, Components.TOPSTAT_LORE_747, true));
```
window pane = `Components.TOPLEVEL_548` (`getWindowPaneId()`, line 739). Note the dead line 388 `//sendTab(16, 747);` — the live path is `openInfoBars()`.

**The orb's number is not a varp.** It is the *dynamic level of stat 23*: `810.cs2` does `setWidgetText(747:5, intToStr(getSkillCurrentLvl(23)))` and `817.cs2` sizes the drain sprite from `getSkillCurrentLvl(23)/getSkillActualLvl(23)`. Server side, "renew points" is literally `getSkills().setLevel(Skills.SUMMONING, staticLevel)` (`ObeliskOptionPlugin.java:38`).

### Varps / varbits — decoded from `idx22`, exact bit ranges

| varbit | varp | bits | meaning | server writer |
|---|---|---|---|---|
| **4280** | **1160** | 23 | **summoning orb visible** (gated on Wolf Whistle complete) | `FamiliarManager.java:40,173` |
| 4281 | 1160 | 24 | unknown, written 0 | `FamiliarManager.java:41,174` |
| 4282 | 1160 | 25–31 | familiar idle-anim selector (>50 → enum 1275, else enum 1276) | `FamiliarManager.java:42,175` (written 7) |
| 4285 | 1175 | 1–8 | pet growth % (101 = "NA") | `Pet.java:61,119` |
| 4286 | 1175 | 9–16 | pet hunger % (101 = "NA", red if >74) | `Pet.java:61,119` |
| **4288** | **1175** | **23–27** | **special-move cost** | `Familiar.java:691` (`specialCost << 23`) |
| 4290 | 1176 | 6 | half-minute flag (`.00` vs `.30`) | `Familiar.java:466` |
| 4534 | 1176 | 7–31 | minutes of familiar time left | `Familiar.java:465` |
| 3288 | 965 | 0–9 | skill-guide skill selector | `StatsTabInterface.kt:29` |
| 3289 | 965 | 10–14 | skill-guide sub-tab | `StatsTabInterface.kt:30` |

| varp | meaning | writer |
|---|---|---|
| **448** | pouch item id (−1 = none) | `Familiar.java:689,754`; `Pet.java:63` |
| **1174** | familiar NPC id (−1 = none) | `Familiar.java:690,757`; `Pet.java:62` |
| **1175** | packed: growth/hunger/special-cost | `Familiar.java:691,756`; `Pet.java:61,94,119` |
| **1176** | packed: familiar timer (via 4290/4534) | `Familiar.java:755` (0 on dismiss) |
| **1177** | **special-move points, 0–60** | `Familiar.java:771` |
| 1160 | packed: orb visibility + anim selector | via varbits |
| 1179 | flashing skill icons bitmask | `LevelUp.java:187` |
| 1230 | level-up "advance" config | `LevelUp.java:168`, `StatsTabInterface.kt:23` |
| 965 | skill-guide selector | `StatsTabInterface.kt:22` |

Dismiss sentinel: `setVarp(1175, 182986)` (`Familiar.java:756`) decodes to growth=101, hunger=101 → both render "NA". Verified arithmetically.

---

## 4. The SKILLS TAB in rev 530

**Skill index is 23, not 21.** Client-side too: `755.cs2`/`810.cs2` call `getSkillCurrentLvl(23)` / `getSkillActualLvl(23)`, and `659.cs2` registers `setScriptCallOnSkillChange(..., 23, 1, ...)`. Server: `Server/src/main/core/game/node/entity/skill/Skills.java:57` — `SUMMONING = 23`, 24 skills total. `Skills.refresh()` loops `i < 24` (line 419).

**Layout is baked into interface 320, NOT CS2-generated.** Decoded: components **125–148** are 24 `type=4` text cells in a 3-column × 8-row grid, 52×32 each:
- col 1 = x=2, col 2 = x=56, col 3 = x=110; rows at y = 2,34,66,98,130,162,194,226.
- **Summoning = component 148**, at x=110 y=226 — the bottom-right cell.

Only two CS2 scripts touch group 320 at all in the whole 5378-file dump (`4102.cs2`, `4103.cs2`), and both only set sprites on 320:203–210, which **do not exist** in 530's 152-component 320. So in 530 the stats tab is essentially IF3-hook driven, not runScript driven.

Server mapping (`Server/src/main/content/global/handlers/iface/tabs/StatsTabInterface.kt:44-68`), `SkillConfig(buttonID, configID, skillID)`:

| skill | 320 child | guide configID (varbit 3288) | Skills.* |
|---|---|---|---|
| SUMMONING | **148** | **24** | 23 |
| CONSTRUCTION | 132 | 22 | 22 |
| HUNTER | 140 | 23 | 21 |
| FARMING | 147 | 21 | 19 |

Note the guide `configID` space (1..24) is a **different numbering** from the stat index — Summoning is 24 there, 23 as a stat.

Tab slot: summoning tab occupies **tab index 7** (`InterfaceManager.java:44` `DEFAULT_TABS[7] = LORE_STATS_SIDE_662`; `interface_configs.json` → `{'id':'662','interfaceType':'2','walkable':'true','tabIndex':'7'}`). It is only opened when a familiar exists (`InterfaceManager.java:396-398`, `restoreTabs` line 360-363). Toplevel child for tab i = `(i < 7 ? 38 : 13) + i` → **child 20** and its mirror **27** (line 371-373). Tab switching is `sendRunScript(115, "i", tabIndex)` (`setViewedTab`, line 593).

`runScript` ids actually used for summoning:

| script | where | signature |
|---|---|---|
| **757** | pouch infusion setup on 669 | `"Iiissssss"` — `SummoningCreator.java:62` |
| **765** | scroll transform setup on 673 | `"Iiisssss"` — `SummoningCreator.java:62` |
| 115 | tab switch | `"i"` |
| 101 | chatbox reset | `""` |

Plus `sendIfaceSettings(190|126, 15, 669|673, 0, 78)` (`SummoningCreator.java:63`) — accessmask on child 15 of both.

---

## 5. Skill guide (499) — fully CS2-driven

Flow: click 320:148 → `setVarp(965, 24)` + `openInterface(499)` (`StatsTabInterface.kt:20-23`). Click a 499 tab → `setVarbit(3288, skillMenu)`, `setVarbit(3289, buttonID - 10)` (lines 28-31). The client script re-renders from those two varbits.

Content lives entirely in CS2 (`dumps/scripts/`):

| script | role | Summoning branch |
|---|---|---|
| `23.cs2` | 499 renderer — reads `bitconfig_3288`/`3289`, sets 499:6 title, 499:10–25 tab labels, 499:3 tab-strip model, builds rows into 499:7 | — |
| `24.cs2` | one row: level text + icon + description into 499:7 (fonts 494/495/497) | — |
| `12.cs2` | `skillMenu → (tabCount, title)` | `case 24: (8, "Summoning")` |
| `13.cs2` | `skillMenu → dispatcher for tab names` | `case 24 → script_1019` |
| `14.cs2` | `skillMenu → dispatcher for rows` | `case 24 → script_1020` |
| **`1019.cs2`** | Summoning tab names | Familiars, Summoning Scrolls, Pets, Equipment, Other, Minigames, Dungeoneering, Milestones |
| **`1020.cs2`** | **Summoning guide rows — 691 lines**, `(level, itemId, "name<br>ingredients", "unlock text")` | e.g. `(1, 12047, "Spirit wolf - Attack XP<br>Gold charm, wolf bones, 7 shards", …)` |

Tab-strip models by tab count: 20838 (2) … 20850 (14), 43501 (15), 43500 (16). Summoning has 8 tabs → model **20844**. Placeholder icon sprite = 2287, blank item = 7620.

**Icons/text source**: the row icon is an **item sprite** (`setItemOnWidgetMethod1200(itemId, -1)`) using the pouch/scroll obj ids in `1020.cs2`; the level number and both strings are **literal strings inside the clientscript**. There is no enum/dbtable behind the guide. Enum **696** (`script_23`'s `setScriptCallOnClickContextMenu(212, enum(i→s, 696, 3288))`) is **ABSENT in the 530 cache** (decoded: empty).

---

## 6. Skill-21/23 wire format

**Packet: opcode 38, fixed length.** `Server/src/main/core/net/packet/out/SkillLevel.java`:

```java
final IoBuffer buffer = new IoBuffer(38);              // ← opcode 38 (IoBuffer.java:55)
buffer.putA(skills.getLevel(skillId, true));           // dynamic level, +128
buffer.putIntA((int) skills.getExperience(skillId));   //乱 int, A-transform
buffer.put(skillId);                                   // 23 for Summoning
```
Payload = 6 bytes: `u8 level+128`, `i32(A) xp`, `u8 skillId`. Prayer sends `ceil(prayerPoints)` and Hitpoints sends `lifepoints` instead of the dynamic level; **Summoning takes the plain `getLevel(23, true)` path** — i.e. the summoning-points value rides the ordinary stat packet, which is exactly why the orb can read it with `getSkillCurrentLvl(23)`.

Senders: `Skills.java:261` (setLevel), `:420` (`refresh()`, loops 0..23 on login), `:572`, `:783`, `:819`.

**Level-up** (`Server/src/main/core/game/node/entity/skill/LevelUp.java`), Summoning = index 23:

| table | value at [23] | line |
|---|---|---|
| `SKILLCAPES` | 12169 | :46 |
| `SKILL_ICON` | 1610612736 | :57 |
| `FLASH_ICONS` | **8388608** (`1 << 23`) → varp 1179 | :62, :187 |
| `ADVANCE_CONFIGS` | **705** → varp 1230 | :74, :168 |
| `CLIENT_ID` | 24 | :86 |

`levelup()` sends `sendGraphic(199)`, two `sendString(..., 740, 0/1)` lines, and opens chatbox 740.

**XP DROPS: ABSENT.** No XP-counter/XP-drop system exists in the 530 server — `grep -rniE "xp ?drop|xpdrop|experience ?counter"` over `Server/src` returns nothing. RS didn't ship the XP counter until 2010. XP reaches the client only as the `i32` inside opcode 38.

---

## 7. Data the client needs, decoded from the 530 cache (`idx17` enums)

All present and decoded — this is the machine-readable half of the port:

| enum | key→val | n | default | meaning | sample |
|---|---|---|---|---|---|
| **1320** | obj→npc | 83 | 6988 | **pouch item → familiar NPC** | 12047→6829, 12043→6825, 12059→6841 |
| **1279** | npc→string | 83 | "Animal" | **familiar name** | 6829→"Spirit wolf", 6825→"Dreadfowl" |
| **1185** | obj→int | 83 | 0 | **required Summoning level** | 12047→1, 12043→4, 12059→10 |
| **1186** | obj→string | 79 | "You may not check this pouch…" | **infusion ingredients text** (server `SummoningCreator.java:85`) | 12047→"This pouch requires 1 set of wolf bones, 1 gold charm and 7 spirit shards." |
| 1187 | obj→string | 148 | "Familiar" | pouch/scroll display name | 12047→"Spirit wolf pouch" |
| 1182 | int→obj | 83 | 526 | infusion slot → pouch id | 1→12047, 2→12043 |
| 1183 | int→obj | 78 | 526 | slot → scroll id | 1→12231 |
| 1184 | int→obj | 78 | 526 | slot → greyed item | 1→12377 |
| 1188 | int→obj | 79 | 526 | slot → produced scroll | 1→12425 |
| 1277 | int→obj | 83 | 526 | slot → charm | mirrors 1182 |
| 1283 | obj→obj | 83 | 526 | pouch → scroll (526 = "is a pet") | 12047→12425 |
| **1282** | npc→**component** | 83 | 43384879 (=662:47) | **familiar → special-move button component** | 6829→662:69, 6825→662:71 |
| 1275 | int→anim | 35 | 8374 | tab idle anim (selector > 50) | 1→4844 |
| 1276 | int→anim | 35 | 8373 | tab idle anim (selector ≤ 50) | 1→4846 |
| 1098 | int→npc | 12 | 6565 | pet-selection heads | 1→6568 |
| **1092** | — | **0** | — | **ABSENT in 530** (referenced by later `751.cs2`) | — |
| **696** | — | **0** | — | **ABSENT in 530** (referenced by later `23.cs2`) | — |

Obj param **394** on the pouch (`getItemHashmapData(448, 394)`, `751.cs2`/`606.cs2`) gates the special-move button; params 743–748 (`930.cs2`) gate row visibility. Both are obj params in `idx16`.

Familiar NPCs (6796–6841+) live in `idx18`; pouch/scroll objs (12009–12466+) in `idx19`/`idx16` — both decode.

---

## 8. Target-side context (3draster / OSRS-Content)

- **Summoning is ABSENT from OSRS-Content.** `grep -rli summon OSRS-Content/` hits only NPC combat names (`npc_combat/s/summonedzombie.combat` etc.). No skill, no pouches, no familiars, no 662/747 analogue.
- **Skill array already has headroom**: `src/game/rs_player_stats.h:11` — `#define RS_PLAYER_STATS_SKILL_COUNT 25`, so stat index 23 is already addressable. `src/game/rs_gameproto_exec.c:563` bounds-checks against it.
- Interfaces in the target are authored as text: `OSRS-Content/osrs239-content/interfaces/*.if` + `*.compack` (e.g. `orbs.if` = OSRS group 160, 57 components, `if3=yes`, `type=`, `graphic=`, `onload=i:8220,…`). A ported 530 interface would be transcoded into this format.
- `OSRS-Content/osrs239-content/content.ini` already defines a `names` ownership value **`imported` — "a foreign revision's table — every line is a *claim*"**, and `membership` files (`pack/<ns>.client` / `.server`) route what reaches the client cache. That is the existing seam for "ported content in a distinct folder".
- `OSRS-Content/osrs239-content/port/` exists (`configs.map`, `constants.map`, `cs2_varps.map`, `names.map`, `vars.map`, `name_diff.signed`) — an established cross-revision mapping area.

---

## RISKS / UNKNOWNS

1. **`dumps/scripts/` is NOT rev 530 — it is a later cache (~rev 600–670).** Hard evidence: (a) `idx12` in the 530 cache has **2065** script slots, the dump has **5378**; (b) `script_12.cs2` names skill 6 "Constitution" (renamed ~2010) and has `case 25: Dungeoneering` (April 2010); (c) `751.cs2` calls `script_2671()`, id > 2064; (d) `810.cs2` writes `747:6` and `747:7` but 530's group 747 only has components 0–5; (e) enums 696 and 1092, referenced by the dumped scripts, decode **empty** in the 530 cache. **Do not treat any dumped script as 530-exact.** Scripts with id < 2065 are *plausibly* close (varbit/varp ids all match the 530 server exactly), but at least two 662 component roles have drifted: the dumps call `setWidgetText(662:54)` and `setWidgetText(662:48)`, while 530's 662:54 and 662:48 are both **sprites**, not text.
2. **The 530 orb has no special-move button; the dumps' orb does.** If the plan is "orb with points + special button", that is a *later* RS design. In 530 the special button is 662:74 on the tab. Decide which era you are porting before authoring the widget.
3. **I did not decode 530's actual clientscripts.** Everything script-level above is from the later dump. Extracting real 530 CS2 from `idx12` needs a 530-era CS2 disassembler; 3draster's `cs2vm2` targets OSRS 239 opcodes, which differ substantially. Whether any 530 CS2 is reusable at all vs. rewriting in the OSRS dialect is **UNKNOWN** and is probably the single largest scope question.
4. **530 if3 ≠ OSRS-239 if3.** I parsed only the fixed header (type/contentType/x/y/w/h) and it looked sane across 320/662/747/748, but I did not parse opacity/parent/hooks/ops. Per `docs`-noted prior pain (`dat2-if3-decoder-validation`), a misaligned type-5/6 field silently corrupts hook ids. The full 530 field set must be validated by round-trip before trusting any transcode.
5. **Interface-id collision.** 662, 747, 320, 499 are all live ids in the *530* space. In OSRS 239 those group numbers mean something else entirely. Ported groups must be allocated in the target's server-owned id band, not carried over — which means every hardcoded `662`/`747` in ported logic is a rename site.
6. **Skill index 23 vs the brief's "21".** The brief says skill 21 = Summoning; rev 530 uses **23** (21 = Hunter). Confirmed both server-side (`Skills.java:57`) and client-side (`getSkillCurrentLvl(23)`). If any downstream plan assumes 21, it is wrong.
7. **OSRS 239's stat packet has 23 skills.** Adding a 24th stat is a **protocol change**, not just a content change — the `UPDATE_STAT` handler, the mock server, and any RSProt-generated codec all need to agree. `RS_PLAYER_STATS_SKILL_COUNT 25` gives array room but says nothing about the wire.
8. **Skill-guide content is 691 lines of string literals in `1020.cs2`** — from the *later* cache. The 530 guide text will differ (no Dungeoneering tab; 530's `script_12` equivalent would say 7 tabs, not 8). Sourcing accurate 530 guide text may require decoding 530's own `idx12`, or accepting the later text as an approximation.
9. **Not investigated**: sprite/model asset ids for the summoning tab beyond 747's five (1206/1244/1245/1200); the 662 hook/onload script bindings; the `binary`/`compack` archive format constraints on the target side; whether `Frostys Cache Editor`'s `clientscripts.txt` (unread) holds a 530-specific script listing.
10. **`interface_configs.json` covers only 154 of 838 interfaces** and lists just 320/662/665/740 of the summoning set — 663/669/671/673/747 have no server-side type/walkable/tabIndex declaration, so their `InterfaceType` is inferred at runtime with a `Log.WARN` fallback (`InterfaceManager.java:276-279`). Those defaults are not authoritative.

===== RECON: rs-content-tree =====
# RECON — `OSRS-Content/` tree organisation & where a "ported" folder goes

All paths repo-relative to `/Users/matthewevers/Documents/git_repos/3draster`.

---

## 0. HEADLINE FINDING (read first)

**Summoning is on three explicit skip lists in this repo.** This task reverses a documented decision, so the plan must say so out loud:

- `docs/SCAPE2009_CONTENT_PORT_QUEUE.md:65` — `| content/global/skill/summoning/**, Wolf Whistle | Summoning is not in OSRS |`
- `docs/SCAPE2009_CONTENT_PORT_QUEUE.md:68` — `| Evil Turnip / summoning-linked patches | Summoning ecosystem |`
- `docs/SKILLS_CONTENT_PORT_QUEUE.md:101` — `| Summoning / Fist of Guthix / RS2-only | not in OSRS |`
- `docs/PORTING_GUIDE.md:683` — §4.5 step 3 tells the agent loop to skip it.

Grep for `summon` across `src/`, `tools/`, `OSRS-Content/` returns **zero** implementation hits — only quest-script "summon an npc" uses (`aa_summon_guards`, `summonedimp`) and boss anim names. **ABSENT: no summoning skill, stat, pouch, familiar, obelisk, or interface anywhere in the tree.**

2009scape footprint to port: 107 paths under `2009scape/Server/src/main/content/global/skill/summoning/` + `data/consumables/effects/SummoningEffect.java` + `RestoreSummoningSpecial.kt`, split `familiar/`, `pet/`, and 6 top-level plugin classes.

---

## 1. `OSRS-Content/README.md` + `osrs239-content/content.ini`

### The tree shape

`OSRS-Content/` holds one thing: `osrs239-content/` — an unpacked OldSchool rev-239 cache **plus** the server content that never enters a cache. `README.md:9-11`. It is a git submodule mounted at `OSRS-Content/`.

`README.md:17` — *"One tree, two halves, and the split is whether the client can see it."*

| Path | Half | Authority |
|---|---|---|
| `meta.ini` | — | cache identity (`game=1 epoch=2 revision=239 rev_name=osrs239`) |
| `configs/`, `models/`, `sprites/`, `maps/`, `interfaces/`, … | cache | everything `cachepack pack` encodes |
| `server/scripts/**` | server | RuneScript + config overlays |
| `server/pack/` | server | **generated dat2**, gitignored |
| `pack/` | shared | id/name/membership authority |

`cachepack` writes and reads everything outside `server/`; it does not look inside `server/` at all (`README.md:36-38`).

### The namespace register (`osrs239-content/content.ini`, 302 lines, all header prose + 15 `[namespace:*]` blocks)

One declaration of what namespaces exist and who owns each part, **read by every tool that loads symbols**. It replaced three tables in two languages that "agreed by accident rather than by construction" (`content.ini:5-8`): `torirs_server_content.c`'s `k_namespaces[]`, `ssc_symbols.c`'s `kind_for_pack()`, `cachepack`'s `cp_names_load`.

**Loaders start from built-in defaults (`src/content/content_register.c:59-202`) and this file *overlays* them.** A namespace that behaves normally can be omitted entirely.

Four axes (`content.ini:13-49`):

| axis | values | meaning |
|---|---|---|
| `ids` | `cache` / `server` / `protocol` | who may choose the number. `cache` = never allocate; `server` = from one past the largest id the cache states; `protocol` = the wire fixes it |
| `names` | `cache` / `authored` / `derived` / `imported` | who owns `pack/<ns>.pack`. `imported` = "a foreign revision's table — **every line is a claim**" |
| `membership` | `authored` (only supported value) | who owns `pack/<ns>.client` / `pack/<ns>.server` — which **entities** have a half on each side |
| `vardomain` | `1` | shares the RuneScript `%name` domain (varp/varbit/varn/vars) |

Hard rule (`content.ini:51-56`): **`names = cache` requires a gameval archive, and having one requires it.** The loaders refuse to start on a namespace that breaks it. `ContentRegister_Validate` enforces.

The 15 declared blocks: `varp`(90), `varbit`(95), `stat`(103), `param`(142), `hitsplat`(150), `category`(187), `component`(213), `3_interfaces`(222), `dbtable`(263), `dbrow`(267), `npc`(288), `loc`(293), `enum`(301). Everything else runs on defaults.

### Ids / names / membership ownership, in practice

- **ids** — `src/content/content_register.c` column 5 is `server_base`, the floor for new allocations. `ss_allocate.py`'s `declared_base()` reads `content.ini` first and falls back to this table (`tools/ss_allocate.py:60-64`).
- **names** — for **config** namespaces the name table is `configs/all.<ns>.compack` (a member index), **not** `pack/<ns>.pack`. `pack/` holds `<ns>.pack` for only two config-ish namespaces (`category.pack`, `stat.pack`) plus 21 archive-index packs (`0_animations.pack` … `22_animayas.pack`). See `src/torirsserver/torirs_server_content.c:593-611` `pack_kind_is_config()`.
- **membership** — 5 namespaces have a `.client`/`.server` pair: `enum`, `loc`, `npc`, `param`, `varp`. The other 15 have none, and *"that absence is a fact rather than an omission"* (`content.ini:70-72`).

### "Server band"

The **server band** is a per-record binary field block written by `cachepack pack` into `<src>/server/pack` — a dat2 with no reference table, one archive per record keyed `(config kind, record id)`, each archive carrying `'S' 'P' version kind crc32(payload)` (`docs/CONTENT_PACK_PLAN.md:574-587`).

It carries *"the half of each record no client opcode can express"* (`content.ini:61-64`), declared by `fields/<type>.ini`. Example — `fields/loc.ini`:

```ini
[loc.next_loc_stage]
server = opcode:150:u4
ref    = loc
```

`server/pack/` is **generated and gitignored** (`OSRS-Content/.gitignore:6-11`). On disk right now: `main_file_cache.dat2` (4.3 MB) + `idx6`, `idx9`, `idx128`, `idx129`.

"Has a server half" ≠ "the packer writes a band for it" — `content.ini:139-141` flags this as the clearest instance of what the split is for.

---

## 2. Top-level dir walk of `osrs239-content/`

24 directories + `content.ini` + `meta.ini`.

| dir | size | count | format | written by | summoning port adds files? |
|---|---:|---:|---|---|---|
| `animayas/` | 355M | 914 `.animaya` | cache-native skeletal curves | `cachepack unpack` | **Maybe** — only if a 530 familiar model is Animaya-rigged. rev 530 predates Animaya → GUESS: no |
| `animsets/` | 66M | 10,902 `.anim` | cache-native anim frame archives | `cachepack unpack` | **Yes** — every familiar/pouch/obelisk anim |
| `binary/` | 676K | 4 (`.jpg`/`.png`/`.bin`) | title screen assets | `cachepack unpack` | No |
| `configs/` | 48M | 40 files (`all.<type>` + `all.<type>.compack`) | text `[name]` blocks / `id=name` index | `cachepack unpack` (machine export of the cache) | **Yes** — new `obj`/`npc`/`inv`/`varbit`/`enum`/`struct` records. ⚠ this dir is *machine-owned*; `test-server-clean` (`src/Makefile:1876-1886`) FAILS if a server build dirties it |
| `dbindex/` | 1.4M | 294 (`.dbi` + `.compack`) | client DB index | `cachepack unpack` | Unlikely |
| `fields/` | 44K | 8 `.ini` (`dbrow, dbtable, enum, loc, npc, obj, param, varp`) | field register: `scope`/`client`/`server = opcode:N:uW`/`ref` | authored | **Probably** — if a summoning-specific npc/obj field needs a band row |
| `fonts/` | 168K | 21 `.fm` | font metrics | `cachepack unpack` | No |
| `framemaps/` | 11M | 2,674 `.base` | anim rigs | `cachepack unpack` | **Yes** — familiar rigs (rev-530 framemaps; see codec note §7) |
| `interfaces/` | 9.4M | 968 pairs (`.if` + `.compack`) | `[com_<name>]` blocks, `if3=yes`, `type=`, `onload=i:2544,…`; `.compack` = `child=name` | `cachepack unpack` | **Yes** — summoning tab + creation/BoB/familiar panels |
| `jingles/` | 2.3M | 315 `.jmid` | Jagex MIDI container | `cachepack unpack` | Only for a level-up jingle (already exists generically) |
| `maps/` | 461M | 11,734 (`.jm2`, `.jl2`, `.filepack`, `.extra<N>.bin`) | `==== MAP ====` / `==== LOC ====` (cache) — README says NPC/OBJ too but spawns actually live in `.spawn` (see below) | `cachepack unpack` (first two sections only) | **Yes** — obelisk loc placements, Taverley/Pisc training-area edits. ⚠ `map` is `CP_ASSET_ENCRYPTED`; authoring maps means owning `xteas.json` (`docs/CONTENT_PACK_PLAN.md:754`) |
| `models/` | 327M | 8,199 loose + `idk/ loc/ npc/ obj/ spot/` subdirs, 61,615 total `.model` | cache-native OSRS v2/v3; named after the config that references them | `cachepack unpack` | **Yes, heavily** — every familiar, every pouch/scroll icon, obelisk |
| `npc_combat/` | 64M | 16,292 `.combat` in a-z/0-5 shards | `key = value  // layer note`, `source = generated\|authored` | `tools/gen_npc_combat.py --write`; **the server does not read it** — it compiles to `server/scripts/npc/configs/npc_anims.generated.npc` (`tools/gen_npc_combat.py:23`) | **Yes** — one `.combat` per new familiar npc |
| `pack/` | 3.4M | 39 files | see §5/§7 | mixed: cachepack, `ss_allocate.py`, humans | **Yes** — new `.client`/`.server` pairs, `.alloc` lines, `category.pack` names |
| `patches/` | 748K | 187 `.patch` | **AUDIO** — MIDI instrument patch banks (binary; `README.md:89` groups it with `songs/ jingles/ synth/ samples/`) | `cachepack unpack` | **No.** ⚠ NOT a patch/override mechanism — see §4 |
| `port/` | 816K | 8 `.map`/`.signed` | tab-separated ledgers | `tools/port_*.py` | **Yes** — see §3 |
| `samples/` | 4.9M | 581 `.sample` | audio | `cachepack unpack` | Possibly (familiar sounds) |
| `scripts/` | 39M | 9,042 `.cs2` + 683 `.bin` + `.cs2b` | decompiled CS2 clientscript source | `cachepack unpack` (needs `CACHEPACK_CS2_NAMES`) | **Yes** — summoning tab / creation UI CS2. ⚠ **8,220/9,042 (90.9%) source fixed point**; a CS2 the compiler declines is silently replaced by base-cache bytes (`README.md:169-175`) |
| `server/` | 30M | `pack/` (generated dat2) + `scripts/` | see §5 | `torirsserver-scripts`, `cachepack pack` | **Yes — the bulk of the port** |
| `songs/` | 67M | 881 `.jmid` | audio | `cachepack unpack` | No |
| `sprites/` | 149M | 8,534 dirs of `N.bmp` + `pack.meta` (`count=`, `palette=`, `pN=0xRRGGBB`, `spriteN=w,h,x,y,…`) | `cachepack unpack`, codec `cp_codec_sprite` | **Yes** — tab icon, pouch/scroll inventory sprites, familiar orb |
| `synth/` | 54M | 12,010 `.synth` | sound effects (cache idx 4) | `cachepack unpack` | Possibly |
| `textures/` | 16K | 1 `.texture` + `.compack` | a text record, not a bitmap; `CP_ASSET_MULTIFILE` | `cachepack unpack` | Unlikely |
| `worldmap/` | 141M | `areas/` (`.wma`, `.wmc`, `.wml`), `geography/`, `ground/` | world map | `cachepack unpack` | Maybe (obelisk map icons) |

---

## 3. `port/` — **yes, this is the existing cross-revision port-mapping layer**

8 files, all tab-separated with long prose headers:

| file | lines | read/written by | what it maps |
|---|---:|---|---|
| `categories.map` | 88 | `tools/port_category_crawl.py`, `tools/port_droptables_check.py` | npc category names → osrs239 ids, with disposition `minted / split / collision / broader / orphan` |
| `categories_loc.map` | 136 | `tools/port_category_crawl.py` | same for loc categories |
| `configs.map` | 1,794 | `tools/port_config_diff.py` | every LostCity block in `param, struct, enum, dbtable, dbrow` → `landed / present / defer-slice / …` |
| `constants.map` | 1,934 | `tools/port_constant_diff.py` | every `^constant` → `present / landed / rederived / …` + the reference's value |
| `cs2_varps.map` | 132 | `tools/cs2_varp_audit.py` | varps shared by CS2 and server scripts; 20 columns, half generated / half human |
| `names.map` | 572 | `tools/port_names_diff.py` | npc/loc/seq/spotanim names the reference uses and this tree cannot provide |
| `vars.map` | 398 | `tools/port_vars_diff.py` | every `%name` the reference's scripts use → `varbit / present / clean-varp / server-varp / varn / vars` |
| `name_diff.signed` | 4,292 | `tools/port_name_diff.py` | `namespace \t name \t verdict \t signoff` (`unreviewed / ok / wrong-record / deferred`) |

All are enforced by `make -C src test-port` (`src/Makefile:1992-2016`) — 10 `--check` invocations.

**It is LostCity-specific today, by default only.** `tools/port_names_diff.py:79`:

```python
DEFAULT_REF = os.path.expanduser("~/Documents/git_repos/LostCity_Server")
```

…with `--reference` / `--ref` CLI overrides (`port_names_diff.py:847`, `port_config_diff.py:491`).

**Could it host a 530→239 mapping? Yes, but not by reusing the files.** Every row is `<name> <disposition> …` keyed on a *reference name*, and the reference tree is a RuneScript content checkout, not a Java server. 2009scape has no `.rs2`, no `^constant`, no `pack/` — so `port_constant_diff.py` and `port_vars_diff.py` have nothing to diff against. GUESS: the right shape is **new sibling files** (`port/summoning_530.map`, `port/names_530.map`) written by a new `tools/port_scape2009_*.py`, following the same "generated columns re-derived by `--check`, human columns never regenerated" contract, wired into `test-port`.

Note `port/names.map:22-25` states the hard rule that applies verbatim here:
> `lc_id` — the reference's own id. **NEVER copy it** (§7 item 1: 1,329 names would land on a real, occupied, wrong record here)

---

## 4. `patches/` — **NOT the additions mechanism. ABSENT.**

`patches/patch_0.patch` is binary (`0000 0000 0003 0400 0032 0a10 0f00 e600 …`). `README.md:89` lists it under **audio**: `| songs/, jingles/, synth/, samples/, patches/ | audio |`. It is cache index 15 (`pack/15_musicpatches.pack`, `content_register.c:191`) — MIDI instrument patch banks.

**There is no "patch over the base cache" directory in this tree.** The additions mechanism is instead:

1. an authored block in `server/scripts/**/configs/*.<type>` (an **overlay**: starts from what the cache's record says and adds fields), or a new record entirely;
2. an id, from `pack/<ns>.alloc` (allocator-swept namespaces) or hand-authored into `configs/all.<ns>.compack`;
3. **client visibility** via `pack/<ns>.client`;
4. `cachepack pack --base cache.osrs239 --out <patched>` emits a derived cache.

`3rd/rscache/tools/cachepack/cp_pack.c:512` — *"A record no cache layer states is **new**, and new records are opt-in."*

---

## 5. `.client` / `.server` membership routing — a real example

### Files on disk (`osrs239-content/pack/`)

| file | header lines | **data lines** |
|---|---:|---:|
| `npc.server` | 12 | **7,487** |
| `loc.server` | 12 | **851** |
| `enum.server` | 12 | **33** |
| `varp.server` | 12 | **26** |
| `param.server` | 12 | **0** |
| `npc.client` / `loc.client` / `varp.client` / `enum.client` / `param.client` | 12 | **0** ← every one is empty |

Plus 6 allocation ledgers: `varp.alloc` (533), `dbrow.alloc` (1,035), `param.alloc` (112), `dbtable.alloc` (68), `enum.alloc` (55), `struct.alloc` (19).

### The gate, as shipped (`docs/PACK_ENTITY_SPLIT_PLAN.md:964-984`, implemented in `cp_pack.c:683-760` `routing_client_member`)

| side | rule, in order |
|---|---|
| **client** | named in `pack/<ns>.client`; **or** the base cache already holds `(config kind, id)` (the "substrate clause"); **or** claimed by `pack/<ns>.alloc`; **or** the type default `records = client\|server` from `fields/<type>.ini` |
| **server** | named in `pack/<ns>.server`; **or** — if `pack/<ns>.server` does not exist — the old field-presence gate |

Measured last run: **173,000 of 173,046** client-routed records came via the substrate clause, **44** via `pack/param.client`, **2** via the default.

Three error cells (`PACK_ENTITY_SPLIT_PLAN.md:986-1013`):
- **(a)** record states a band field but `<ns>.server` exists and does not name it → **error**, no band written
- **(b)** server-only record stating a client field → **counted warning** (`enum` 32/32; `varp` 0/22)
- **(c)** neither file names it AND the base cache does not hold its id → **error**, non-zero exit, default still routes it

### Worked example: `next_loc_stage` on a door

1. `fields/loc.ini` declares the field:
   ```ini
   [loc.next_loc_stage]
   scope  = server
   client = drop
   param  = next_loc_stage
   [loc.next_loc_stage]
   server = opcode:150:u4
   ref    = loc
   ```
2. `server/scripts/doors/configs/doors.loc` states `next_loc_stage=poordooropen` on a `[poordoor]` block.
3. `pack/loc.server` names `poordoor` (851 such names; the file's own header: *"776 locs, every one of them a cache loc the tree overlays with a door stage"*, `content.ini:291-292`).
4. `pack/loc.client` is empty → `poordoor` still reaches the client because **the base cache already holds loc id 1535** (substrate clause).
5. `cachepack pack` writes the band entry into `server/pack/main_file_cache.idx6` at `(loc kind, 1535)`; the client record is unchanged.

Key asymmetry (`PACK_ENTITY_SPLIT_PLAN.md:978-982`): *"Being in `<ns>.server` does not take a record off the client."* Naming 2,199 npcs in `npc.server` left all 16,292 in the client cache.

### ⚠ For summoning specifically

**`obj` has no membership pair.** Only `enum, loc, npc, param, varp` do. A summoning **pouch** is a new `obj` with an id ≥ 40000 that the base cache does not hold → **cell (c), hard error, `cachepack pack` returns non-zero**. Creating `pack/obj.client` (via `cachepack membership --src … --rev osrs239 --types obj`) plus an `[namespace:obj] membership = authored` block in `content.ini` is a **required prerequisite**. Same for `inv`, `varbit`, `struct`, `seq`, `spotanim`, `3_interfaces` if new records land there.

**No tree has ever exercised the add path.** `PACK_ENTITY_SPLIT_PLAN.md:1126-1130` §11.1 "Step 4 — author": *"The feature the first three steps exist to make safe: a record moves sides by editing a file. **Nothing on this tree has used it yet.**"* All five `.client` files being empty is the evidence.

---

## 6. Conventions for marking content ported / foreign

### There is **no directory-naming convention**. ABSENT.

`server/scripts/` has 64 top-level dirs, all functional, none provenance-marked:

```
areas/  bosses/  build/  configs/  doors/  drop_tables/  general/  general_use/
interface_{account,bank,bankpin,chat,chrome,collection,combat,combat_achievements,
  diaries,emote,equipment,farming,friends,journal,loottools,music,orbs,
  questjournal,settings_side,skill_guide,slayer,summary}/
ladders_stairs/  levelup/  minigames/  npc/  player/  quests/  shop/
skill_{agility,combat,construction,cooking,crafting,farming,firemaking,fishing,
  fletching,herblore,hunter,magic,mining,prayer,runecraft,slayer,smithing,
  thieving,woodcutting}/
```

Each is `<domain>_<name>/{configs,scripts}`. **LostCity-, Kronos-, 2009scape- and QuestHelper-ported content all land in the same `skill_*` dirs.** `skill_farming/` (2009scape) and `skill_slayer/` (Kronos) sit beside `skill_prayer/` (LostCity) with nothing in the path saying so.

### The convention that *does* exist: a per-file comment header

`server/scripts/skill_farming/scripts/farming_bush.rs2:1-4`:
```
// Bushes — redberry on classic patches 1–4.
// Policy: 2009scape Patch / HealthChecker / FruitAndBerryPicker / DigUpPatchDialogue.
// Mapzones (jl2): Champions 0_49_52, Rimmington 0_45_50, Etceteria 0_40_60, Ardougne 0_40_50.
```

Grep counts under `osrs239-content/server/`:

| term | files |
|---|---:|
| `lostcity` | 1,757 |
| `ported` | 1,101 |
| `2009scape` | 295 |
| `Kronos` | 134 |
| `questhelper` | 4 |
| `foreign` | 2 |
| `imported` | **0** |

Provenance is also tracked out-of-tree, per-slice, in `docs/{CONTENT,SCAPE2009,KRONOS,QUESTHELPER,SKILLS}_CONTENT_PORT_QUEUE.md`.

### The one precedent for a marked folder name

`3rd/rscache/tools/port_lostcity/main.c:53,175`:
```
--area DIR    config subdirectory under scripts/ (default areas/area_ported)
snprintf(manifest.area, sizeof(manifest.area), "%s", "areas/area_ported");
```

**`areas/area_ported` is the only "clearly marked as ported" folder name anywhere in the codebase** — a default in the asset transplanter, not currently instantiated in the tree.

### Register vocabulary that exists and is unused

`content.ini:24` declares a `names` value **`imported` — "a foreign revision's table — every line is a *claim*"**. **No namespace in the tree currently declares it.** This is the exact, pre-existing vocabulary for what a 530-sourced name table is, and using it is the strongest available in-register marker.

### RECOMMENDATION (GUESS)

Nothing forbids a new top-level `server/scripts/` dir. The plan should combine:
1. `server/scripts/ported_scape2009_summoning/{configs,scripts}` (or `skill_summoning_ported/`) — new top-level dir, provenance in the name;
2. `[namespace:<ns>] names = imported` blocks in `content.ini` for any name table crawled out of the 530 cache;
3. the existing per-file `// Policy: 2009scape <Class>.java` header on every `.rs2`;
4. a new `port/summoning_530.map` ledger under `test-port`.

---

## 7. Where NEW ids come from — the server band, and `ss_allocate.py`

### The allocator

`tools/ss_allocate.py` (23,514 bytes), run by `make -C src torirsserver-scripts` **before** sscompile (`src/Makefile:1616-1622`):

```make
torirsserver-scripts: sscompile check-crystal-set-contract
	@python3 $(REPO_ROOT)/tools/ss_allocate.py --tree $(TORIRSSERVER_CONTENT_DIR)
	./$(OBJ_DIR)/sscompile --src $(TORIRSSERVER_CONTENT_DIR)/server/scripts …
```

Three rules (`ss_allocate.py:23-49`):
- base = **layer 0's high-water mark**, not a round number (`configs/all.<ns>.compack` + numeric `[<ns>_N]` blocks), floored by `server_base` from the register;
- **an assignment, once made, is never changed** — a rename is a new name plus a stale line, never a renumber;
- everything above `// --- allocated below this line by tools/ss_allocate.py; do not hand-edit ---` is human.

Output goes to `pack/<ns>.alloc`, **never** to `configs/all.<ns>.compack`.

**Swept namespaces (`ss_allocate.py:83-93`) — this is the complete list:**
```python
SERVER_NAMESPACES = ('enum', 'struct', 'dbtable', 'dbrow', 'param', 'mesanim', 'inv', 'varp')
```

**⚠ `npc`, `obj`, `loc`, `seq`, `spotanim`, `varbit`, `3_interfaces`, `7_models`, `8_sprites` are NOT swept.** There is **no allocator** for them. `docs/CONTENT_PACK_PLAN.md:447-452` §4.2 describes the intended path ("just a declared base per namespace … Allocate upward from it with `lc_pack_alloc`") but that is plan text, not shipped code for these namespaces.

### The bases (`src/content/content_register.c:60-202`, column 5) vs. what the cache actually reaches

Measured just now from `configs/all.*.compack` and `pack/*.pack`:

| namespace | reg line | `server_base` | **max id in tree** | headroom |
|---|---:|---:|---:|---|
| `npc` | :63 | **20000** | 16,293 | ✅ |
| `obj` | :64 | **40000** | 34,304 | ✅ |
| `loc` | :65 | **70000** | 62,200 | ❌ runtime `LOC_ADD_CHANGE_V2` is 16-bit; Summoning uses 62201 |
| `seq` | :66 | **20000** | 14,428 | ✅ |
| `spotanim` | :67 | **6000** | 4,009 | ✅ |
| `inv` | :68 | **2000** | 1,025 | ✅ (swept) |
| `3_interfaces` | :77 | **2000** | 968 | ✅ |
| `dbrow` | :108 | **65536** | (cache 0..16,939) | ✅ (swept) |
| `dbtable` | :109 | **2048** | (cache 0..258) | ✅ (swept) |
| `param` | :113 | **2634** | cache 0..2633; alloc 2634..2745 | ✅ (swept) |
| `category` | :135 | **8192** | 8192..8205 allocated (`door_closed`, `climb_up`, …) | ✅ |
| `enum` | :137 | **5995** | 5,994 | ⚠ base == max+1, zero margin |
| `struct` | :138 | **8000** | 6,499 | ✅ (swept) |
| `varp` | :170 | **5705** | alloc 5705..6225 | ⚠ see below |
| `varbit` | :171 | **25000** | 20,410 | ✅ |
| `8_sprites` | :183 | **20000** | 8,534 | ✅ |
| `7_models` | :184 | **100000** | 61,614 | ✅ |
| `4_soundeffects` | :186 | **20000** | — | ✅ |
| `stat` | :144 | **0** | 0..22 | ❌ **`ids = protocol` — never allocate** |

### ⚠ Two hard ceilings

**(a) `stat` is protocol-fixed and C-capped.** `pack/stat.pack` is 23 lines `0=attack` … `22=construction`, headed *"Skill ids. Fixed by the protocol (UPDATE_STAT carries this index) and by the client's own stat table, so they are authored, not imported."* `content.ini:103-105` sets `ids = protocol`. `src/torirsserver/torirs_server.h:517` hardcodes `TORIRSSERVER_STAT_COUNT = 23`. Adding Summoning as stat 23 is a **C change plus a protocol claim the rev-239 client does not carry** — the client's own stat table has 23 entries. This is the single biggest structural blocker.

**(b) `varp` allocation is bounded at 6217.** `content_register.c:151-163`: `TORIRSSERVER_VARP_COUNT = TORIRSSERVER_VARP_CACHE_MAX + 512 = 6217`. Current high-water in `pack/varp.alloc` is **6225** (`6225=bank_wornview`) — already past 6217. Any varp allocated past the array end is **silently dropped** by `ToriRSServer_WorldSetVarp`'s bounds check. A summoning port allocating ~20 varps walks straight into this.

### Test gates a port must pass

`make -C src test-content` (`src/Makefile:1861-1864`) chains: `test-content-register`, `test-servercodec`, `test-ss-symbols`, `torirsserver-scripts`, `torirsserver-servpack`, `test-membership`, `torirsserver-pack`, `test-server-clean`, `test-port`, then `ToriRSServer_Pack` itself.

`test-server-clean` (`src/Makefile:1876-1886`) is the one to watch: after the whole server pipeline, `git status --porcelain -- configs 'pack/*.pack' 'pack/*.client'` must be **empty**. A summoning port that hand-edits `configs/all.obj.compack` will make this target's contract ambiguous (the edit is committed, so it's clean — but the tool that regenerates it must merge, not truncate; `content.ini:19-22` says pack saves merge).

---

## 8. Bonus: the two existing cross-revision asset porters

Both already exist and neither has been pointed at 2009scape.

**`3rd/rscache/tools/port_lostcity/`** — exports dat2 cache assets as **LostCity source files** (`.ob2`, `.anim`, text `.npc`/`.seq`/`.spotanim`/`.loc`/`.flo`, a `.jm2`, and `content/pack` id lines). Manifest-driven: `dragon_claws.ini`, `ghrazi_rapier.ini`, `scythe_of_vitur.ini`, `tormented_demon.ini`. Runs **osrs239 → lc254**, i.e. the *wrong direction* and the *wrong destination format* for this job. Its `[port:lostcity]` manifest grammar (`rev`, `cache`, `content`, `rig_framemaps`, `rig_inert`, `area`, `prefix`, then `[export:obj]` / `[export:seq]` / `[export:spotanim]` id=name lists) is the closest existing template.

**`3rd/rscache/tools/port_npc/`** — *"port an NPC and its asset closure between cache revisions"*, `--from-rev A <src_dir> --to-rev B <dst_dir> --npc ID [--out DIR] [--apply] [--include-related-anims] [--emit-bas]`. Writes a **binary destination cache**. Refuses any record that does not decode byte-exactly (`port_npc/main.c:31-38`). Normalises anims across eras via `Tool_AnimSlot` (`common/port_plan.h:10-29`). **This is the right shape for 530 familiars**, but its output is a cache, not the `OSRS-Content` tree.

### The 530 revision profile is ABSENT

`3rd/rscache/src/revisions/` holds 16 modules; the registry is `revisions.c:22-45`:
```
lc254, lc245_2, osrs184/kronos, osrs230..osrs239, xrsps233, 643/rs643, 727/rs727
```
**No `rs530`.** Adding it is one file + one row (`revisions.c` header: *"adding one is a single row"*).

Good news: the lineage `(rs2, dat2)` covers "377 onward" (`rscache_profile.h:47`), 2009scape's cache **is** dat2 (`Server/data/cache/main_file_cache.dat2`, 91.7 MB, `idx0..idx22`), and `rev_dat2_rs643.c:44-47` already names the exact codec boundary at 530:
> `framemap — RS >= 481/530: transform_actor bytes and masks shorts sit between the type list and the bone-group lengths. Without this pin every bone group is read as empty and animation moves nothing.`
> `cache.codec[RSCACHE_TYPE_FRAMEMAP] = RSCACHE_CODEC_FRAMEMAP_V3;`

GUESS: `rs530` needs `FRAMEMAP_V3` (530 is exactly the threshold — verify which side), `LOC_RS2`, `FLO_RS2`, and **not** `FRAME_V2` (that's rev 610+).

---

## RISKS / UNKNOWNS

1. **Summoning is on three documented skip lists** (§0). The plan must explicitly override `docs/SCAPE2009_CONTENT_PORT_QUEUE.md:65,68`, `docs/SKILLS_CONTENT_PORT_QUEUE.md:101`, `docs/PORTING_GUIDE.md:683`, or those files' `--check` gates and the agent loops that read them will keep fighting it.

2. **`stat` cannot be allocated.** `ids = protocol`, `TORIRSSERVER_STAT_COUNT = 23` in C, and the rev-239 client's own stat table has 23 entries. Summoning as skill 23 is not a content change. UNKNOWN: whether the rev-239 client tolerates an out-of-range `UPDATE_STAT` index, or whether Summoning must be faked on a varp + a client-side CS2 skill panel.

3. **`varp` allocation is already past its runtime ceiling** — `pack/varp.alloc` reaches 6225, `TORIRSSERVER_VARP_COUNT` is 6217, and over-range writes are **silently dropped**. Verify before allocating anything.

4. **`obj`, `inv`, `varbit`, `struct`, `seq`, `spotanim`, `3_interfaces` have NO membership pair.** Every new record in those namespaces is `cachepack pack` **cell (c) — a hard error**. Creating the pairs (`cachepack membership`) + `content.ini` blocks is a prerequisite, not a detail.

5. **The add path has never been run.** All five `.client` files are empty (0 data lines). `PACK_ENTITY_SPLIT_PLAN.md` §11.1 says step 4 "author" is unexercised. A summoning port is the *first* consumer of a designed-but-unproven mechanism. Budget for finding its bugs.

6. **No allocator for `npc`/`obj`/`loc`/`seq`/`spotanim`/`models`/`sprites`/`interfaces`.** `ss_allocate.py` sweeps only 8 namespaces. Bases exist in `content_register.c` but nothing consumes them for these. Either extend `SERVER_NAMESPACES` (needs care: `configs/all.<ns>.compack` is machine-owned and `test-server-clean` guards it) or hand-author, which drifts.

7. **`rs530` revision profile does not exist** in `3rd/rscache/src/revisions/`. UNKNOWN whether rev-530 obj/npc/seq/model/framemap/interface records decode with the existing codec ladder. `port_npc` **refuses any record that does not decode byte-exactly**, so a decode gap becomes a hard stop, not a degradation.

8. **`configs/` is machine-owned and `test-server-clean` enforces it.** UNKNOWN how a hand-authored id line in `configs/all.obj.compack` survives the next `cachepack unpack`. `content.ini:19-22` claims pack saves merge rather than truncate — but `configs/all.<ns>.compack` is regenerated by unpack, and the merge guarantee is stated for `pack/<ns>.pack`. **Verify empirically before designing around it.**

9. **`enum` base has zero margin** (`server_base = 5995`, cache max `5994`). A cache bump that adds one enum collides with the first allocated summoning enum.

10. **CS2 round-trip is 90.9%, and failure is silent.** `README.md:169-175`: a source spelling the compiler cannot resolve is declined, the base cache's bytes ship in its place, *"and only a counter says the edit went nowhere."* A summoning tab CS2 that fails to compile produces a working-looking cache with no summoning tab.

11. **`maps/` is `CP_ASSET_ENCRYPTED`** — obelisk placements mean owning `xteas.json` (`CONTENT_PACK_PLAN.md:754`).

12. **Rig retarget is the classic trap.** `dragon_claws.ini:22-27` documents the exact failure: running a player rig correspondence over a non-player framemap renumbered joints 1..10 into 35/1/41/37/20/254 against a model carrying labels 1..18, producing "claw streaks". rev-530 familiars carry their own framemaps; `--label-map FROM=TO` exists for this.

13. **UNKNOWN: no runtime feature flag mechanism for content.** `[features:boot] era=` (`manifests/manifest_osrs239.ini:126`, `src/app.c:3448`) is a **client-behaviour** table, not a content gate. Both content walkers — `walk_configs` (`src/torirsserver/torirs_server_content.c:2879`) and `collect_sources` (`src/serverscript/ssc_compile.c:2883`) — skip entries whose name starts with `.`, so a dot-prefixed directory is a *compile-time* on/off switch and the only one that exists. `PORTING_GUIDE` §7 / `.cursor/rules/no-park-sibling-content.mdc` forbid `.skip` parking of *sibling* content but say nothing about your own new lane. A genuine runtime flag would be new engine surface.

14. **UNKNOWN: `README.md` is stale in two places.** `README.md:32` and `:80-82` claim `pack/<type>.pack` is "the id authority, 40 of them" — the tree has 23 `.pack` files and config names actually live in `configs/all.<ns>.compack`. `README.md:33,268` claim `server/pack/*.pack` holds "skills, and varp aliases" — `content.ini:61-64` says `server/pack/` is now a generated, gitignored dat2 and `stat.pack` moved to `pack/`. `README.md:41-43` claims `maps/*.jm2` carries `==== NPC ====`/`==== OBJ ====`; the actual spawns are 972 `.spawn` files under `server/scripts/areas/world/configs/` generated by `tools/gen_spawns.py`. **Do not plan against README prose without checking the tree.**

===== RECON: rs-cachepack-assets =====
# RECON: cachepack + asset pipeline — can new assets be injected into osrs239?

**Verdict: YES for injection; PARTIAL for cross-revision import.** cachepack can author brand-new records/archives at any id and grows the reference table to match. What does **not** exist is a rev-530→osrs239 importer, and two format seams (framemap codec, sharded config layout) will silently corrupt data if crossed naively.

---

## 1. cachepack — location, entry point, commands

| item | path |
|---|---|
| source | `3rd/rscache/tools/cachepack/` (23,555 LOC incl. `config/`) |
| entry | `3rd/rscache/tools/cachepack/main.c` (usage at `main.c:9-92`) |
| built binary | `3rd/rscache/tools/cachepack/cachepack` (present, works) |
| docs | `3rd/rscache/tools/cachepack/README.md` (tree layout), `cachepack.h` (architecture) |
| build | `make -C 3rd/rscache/tools cachepack` |

Commands (verbatim from `main.c`):
```
cachepack unpack --cache DIR --rev NAME --src DIR [--types a,b] [--compare DIR]
                 [--assets[=models,songs]] [--binary[=1,2]] [--raw-assets]
cachepack pack   --src DIR --out DIR [--base DIR] [--rev NAME] [--types a,b]
                 [--assets] [--binary] [--gamevals]
cachepack pack   --src DIR --server-only
cachepack verify --cache DIR --rev NAME --src DIR [--types a,b] [--assets[=...]] [--tmp DIR]
cachepack membership --src DIR --rev NAME [--types a,b] [--check-only]
--list  --list-assets
```
`--check-only` belongs to `membership` only (routing audit), **not** to pack/verify.

Repo build integration:
- `src/makefile:1677` `torirsserver-cache` → `cachepack pack --src OSRS-Content/osrs239-content --base cache.osrs239 --out cache.osrs239.baked --rev osrs239 --assets --binary --gamevals`, then `torirsserver-cache-check` (asserts idx `0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 18 19 20 21 22 24 255` all exist).
- `src/makefile:1632` `torirsserver-servpack` → `pack --server-only` (server band, no cache).
- `the osrs239-net profile (profiles/osrs239-net.ini):24` `dir=cache.osrs239.baked` — the baked cache is already the boot target for the net manifest. `TORIRSSERVER_CACHE_DIR_DEFAULT "cache.osrs239"` (`src/torirsserver/torirs_server.h:99`) is the pristine default; the one-cache rule says point BOTH the world and JS5 at the bake.

---

## 2. Per-asset-kind capability

### 2a. Config types (`cachepack --list`, 20 types)
`underlay(1) overlay(4) idk(3) inv(5) loc(6) enum(8) npc(9) obj(10) param(11) seq(12) spotanim(13) varbit(14) varp(16) varc(19) hitsplat(32) healthbar(33) struct(34) mapelement(35) dbrow(38) dbtable(39)`

- **On-disk source**: `OSRS-Content/osrs239-content/configs/all.<type>` — `[name]` blocks of `key=value`; member index `configs/all.<type>.compack` (`id=name`).
- **Encoders: ALL 20 present.** `CP_TYPE_NO_ENCODER` (`cachepack.h:118`) is set on **zero** types today — `--list` shows no `unpack-only`. EXCEPTIONS **H1 (dbrow/dbtable unpack-only) is STALE**: `RSCache_Dat2ConfigDbRowEncode` / `...DbTableEncode` exist and are held to byte-identity (`src/content/content_register.c:88-100` records the correction).
- **Decoders**: all present (`3rd/rscache/src/datatypes/dat2_config_*.c`).
- **Lossy** (flagged in `--list`): loc, enum, npc, obj, seq, spotanim, mapelement — see EXCEPTIONS B2 for the exact opcode lists. Lossy = semantic round-trip only, not byte-exact.
- **Tests**: `3rd/rscache/test/test_roundtrip.c`, `test_config_var.c`, `test_db_encode.c`, `test_fields.c`, `test_rs2_sweep.c`; `test/test_cachepack_fidelity.sh` enforces `lost-here == 0` per type against `cache.osrs239` (skips loudly with no cache).
- **ABSENT**: no encoder for `bas` in the cachepack type list — `RSCache_Dat2ConfigBasEncode` exists in the library but BasType is not a cachepack CP_Type. Summoning familiars in rev530 use `bas_type_id` (e.g. npc 6829 → bas 1326). **This is a gap.**

### 2b. Asset tables (`cachepack --list-assets`, 20 kinds)

| dir | pack file | raw ext | codec (friendly form) | authorable? |
|---|---|---|---|---|
| `animsets` (frames) | `0_animations` | `.anim` | — pass-through | YES (raw bytes) |
| `framemaps` | `1_skeletons` | `.base` | — pass-through | YES (raw bytes) |
| `interfaces` | `3_interfaces` | `.ifb` | `.if` text (IF3) | YES (full text) |
| `synth` | `4_soundeffects` | `.synth` | — pass-through | YES (raw) |
| `maps` | `5_maps` | `.map` | `.jm2`/`.jl2` text, XTEA flag | YES |
| `songs` | `6_musictracks` | `.jmid` | — | YES (raw) |
| `models` | `7_models` | `.model` | — pass-through | YES (raw) |
| `sprites` | `8_sprites` | `.sprite` | `<name>/N.bmp` + `pack.meta` | YES (BMP) |
| `textures` | `9_textures` | `.texb` | `.texture` text | YES |
| `binary` | `10_binary` | `.bin` | — | YES |
| `jingles` | `11_musicjingles` | `.jmid` | — | YES |
| `scripts` | `12_clientscripts` | `.cs2b` | `.cs2` source (compiler) | YES |
| `fonts` | `13_fonts` | `.fmb` | `.fm` metrics text | YES |
| `samples` | `14_musicsamples` | `.sample` | — | YES (raw) |
| `patches` | `15_musicpatches` | `.patch` | — | YES (raw) |
| `worldmap/geography` | `18_...` | `.wmgb` | `.wmg` text | YES |
| `worldmap/areas` | `19_worldmap` | `.wmab` | `.wma` text | YES |
| `worldmap/ground` | `20_...` | `.wmgr` | — | YES |
| `dbindex` | `21_dbtableindex` | `.dbib` | `.dbi` text | YES |
| `animayas` | `22_animayas` | `.animaya` | — pass-through | **read-only** — no `RSCache_*AnimayaEncode` anywhere. ABSENT. |

Table def: `3rd/rscache/tools/cachepack/cp_assets.c:84-165` (`g_assets[]`).

**Library encoder census** (grep over `src/datatypes/*.h`): present for model, frame, framemap, sprite pack, texture, component/IF3, clientscript, sound effect + sound bank, maps terrain/locs, mapelement, worldmap area, entity ops, dat1 anim base/frames, all dat2 configs. **ABSENT**: animaya, skeletalbase, proctexture (decode+evaluator only, EXCEPTIONS B18), music song/patch/vorbis (raw pass-through only, fine), worldmap geography (codec lives inside cachepack).

**Fidelity bars** (`test_cachepack_fidelity.sh`): configs → `lost-here == 0`; assets → `differ == 0` (length change fails; same-length mismatch is byte ordering); scripts → semantic only (`test/test_cs2.c`).

---

## 3. `3rd/rscache/EXCEPTIONS.md` — constraints that bind a cache write

Required reading; 2,600 lines, 60+ entries. The ones that matter here:

- **A1** — bzip2 encoder is in-tree, valid but **not byte-identical to Jagex**. Fine offline.
- **A5 (cross-cache porting)** — *"the library ships no automatic porting layer inside the codecs — decode with the source profile, encode with the destination's, and let the caller decide fields the destination added."* Known-lossy: dat2→dat1 models drop OB3/V2/V3 render types + animaya skinning; dat2→dat1 framemap drops `transform_actor`/`masks`/tail; **retexture/texture ids are cache-local and do not map across revisions unless `--texture-map` is supplied**.
- **B2/B3** — lossy decoders + ascending-opcode ordering cap byte-exactness. Whole-cache byte identity is **not** the bar and never will be.
- **B3b** — sprite packs must be decoded with `RSCACHE_SPRITELOAD_FLAG_NONE`; `NORMALIZE` rewrites in place and a repack of a normalised pack ships full-size unoffset sprites.
- **B4** — `RSCache_Dat2DiskWriteArchive` **appends and re-points, orphaning old sectors**: repacking in place grows the file forever. Always `--base` + fresh `--out`. Sector 0 is reserved.
- **B7** — `LARGE_MODEL_IDS` defined, never set by any loc profile.
- **H4** — reference-table CRC covers container **minus** the u16 version trailer (measured 10/10 vs 0/10). Getting it wrong → client rejects the archive.
- **H5** — `--binary` (raw containers, byte-exact) and `--assets` (payloads, readable) are the two exits, **neither transcodes**. `--assets` round-trips `cache.osrs239` byte-identically at payload level (117,086 files) and the client boot log matches line-for-line.
- **H10** — two crashes the round-trip found in the library.
- **G4/G7/G12/G13** — CS2: 227 of OSRS 230's scripts don't decompile; rev-239 typed-stack commands and array slots diverge from the 2021 reference. 4,139 of osrs239's 9,725 scripts fall back to `.cs2b` raw.

---

## 4. On-disk source format in `OSRS-Content/osrs239-content/`

**Raw decompressed archive payloads, never a decoded intermediate**, for every kind with no codec. Verified by hexdump:

| dir | files (git-tracked) | format |
|---|---|---|
| `models/` | 61,615 | raw payload; format sniffed from the **trailer magic** (`model.h:167-179`: OB2 / OB3 `FF FF` / V2 `FF FE` / V3 `FF FD`). Subdirectories used today (`models/npc/…`, `models/idk/`, `models/loc/`). |
| `framemaps/` | 2,674 `base_N.base` | raw payload |
| `animsets/` | 10,902 `animset_N.anim` | raw payload (whole archive = hundreds of frames; the archive is the unit) |
| `animayas/` | 914 `.animaya` | raw payload |
| `sprites/` | 8,534 archives / 20,266 files | **decoded**: `sprites/<name>/N.bmp` + `pack.meta` (palette + count) |
| `synth/` | 12,010 `.synth` | raw payload |
| `samples/` 581, `patches/` 187 | | raw payload |
| `songs/` 881 `.jmid` | | raw Jagex container (`17 07 f6 02`), NOT MIDI |
| `textures/` | `texture_0.texture` + `.compack` | **decoded text** (`[mat_0]` blocks) |
| `interfaces/` | 969 archives, 1,936 files | **decoded text** `.if` (`if3=yes`, one block per component) + `.compack` |
| `scripts/` | 9,725 | 9,368 `.cs2` source + 357 `.cs2b` raw |

Extensions come from the payload magic, not the era (EXCEPTIONS H6, `cp_assets.h:20-33`).

---

## 5. Existing foreign-revision import paths

**In-tree converters that exist:**
- `3rd/rscache/tools/port_npc/` (804 LOC) — port an NPC + asset closure (models, seqs, frames, framemaps, optional related anims / BasType) **cache→cache**. `tool_port_commit_dat2` (`tools/common/cache_write.c`) is the dat2→dat2 path; ids kept when free, remapped on collision; `--texture-map`, `--label-map`, `--strict-models`, `--emit-bas`. Refuses a source NPC whose record does not decode byte-exactly.
- `3rd/rscache/tools/port_lostcity/` (5,556 LOC) — dat2 → LostCity **dat1 source tree** (`.ob2`, `.anim`, text configs, `.jm2`, pack lines). Wrong direction for this task.
- `3rd/rscache/tools/common/transcode.c` — dat1↔dat2 framemap/model/frame transcode.
- `3rd/rscache/tools/find_anims`, `find_named`, `anim_compare`, `poser-gl-c`.

**Precedent**: `cache.rs254_steeltitan/` (7.6 MB dat1) in the repo root is a Summoning familiar (Steel Titan, npc 7343/7344, model 30469) already ported cross-revision by this toolchain (643→254). Referenced in `readme.md:1103`, `manifests/manifest_void634.ini:266`, `3rd/rscache/src/datatypes/dat2_config_bas.h:12`.

**Not relevant / red herrings:**
- `tools/proctex_port/` — a TypeScript reference (`RasterizerOperation.ts`) + `gen_verify_rand.c` for procedural-texture *evaluation*. **Not an asset importer.**
- `tools/Rl239TransplantMethods.java` — JVM bytecode method transplant for bisecting a *decompiler* regression. **Nothing to do with assets.**
- `tools/port_*.py` (`port_config_diff.py`, `port_name_diff.py`, `port_category_crawl.py`, `port_weapon_fx.py`) — diff/crawl reporting over already-unpacked trees, not importers.

**ABSENT**: any path that writes a foreign-revision asset **into `OSRS-Content/osrs239-content/`**. Every existing converter writes into a *cache directory* or a *LostCity content tree*. A rev-530 → osrs239-content importer must be written (or synthesised as: `port_npc --to-rev osrs239 --out cache.tmp` → `cachepack unpack cache.tmp`).

---

## 6. Reading the 2009scape rev-530 cache — MEASURED

`/Users/matthewevers/Documents/git_repos/2009scape/Server/data/cache/` is a **dat2 cache** (88 MB, `main_file_cache.dat2` + idx0..idx28, idx255). It is the **RS2 sharded layout**: loc=idx16, enum=17, npc=18, obj=19, seq=20, spotanim=21, varbit=22 (`3rd/rscache/src/dat2disk.h:187-193`), same as rs643.

**No `rev530`/`rs530` profile exists** — `3rd/rscache/src/revisions/revisions.c:22-46` lists lc254, lc245_2, osrs184/kronos, osrs230..239, 643/rs643, 727/rs727. **ABSENT.**

Probes run with `--rev rs643` (read-only):

| probe | result |
|---|---|
| `find_named --npc 6807` | ✅ `"Thorny snail"` models 30435, chathead 31168, bas 1329 |
| `find_named --name spirit --type npc` | ✅ full familiar list: 6794/6795 terrorbird, 6802/6803 cobra, 6804/6805 dagannoth, 6829/6830 wolf, 6837/6838 scorpion, 6841/6842 spider, 6875/6876 cockatrice… |
| `find_named --model 30435` | ✅ 328 verts / 599 faces, `format_version 1`, 59 vertex labels |
| `find_named --framemap 0` | ✅ 251 transforms decoded |
| `find_named --seq 8297 / 8291` | ✅ 17 and 16 frames, durations correct |
| `find_anims --npc 6829` | ✅ bas 1326 → seqs 8297, 8291 → framemap 54090 |
| `cachepack unpack --rev rs643 --assets=textures,framemaps,sprites` | ✅ 4,824 files, 6.5 MB, sprites→BMP, textures→text |
| `cachepack unpack --types npc,obj,seq,spotanim,loc,enum,varbit` | ❌ **`"X is sharded across groups in this cache — this tool handles the OldSchool config-group layout only"`** |

**Reference-table counts in the 530 cache**: models 45,472 · animations 2,724 · skeletons 2,435 · sprites 1,709 · soundeffects 6,750 · clientscripts 2,063 · interfaces 834 · textures 680 · maps 3,682.

### Two hard format seams

1. **Framemaps: rev530 = V3, osrs239 = V1.** `dat2_framemap.h:85-87` — `V1 = OSRS/RS2 <481`, `V2 = RS2 ≥481` (adds `transform_actor`), `V3 = RS2 ≥530` (adds `masks u16`). A byte-copied 530 framemap decoded as V1 reads the actor bytes as group lengths → *"every bone group is read as empty and animation moves nothing"* (`rev_dat2_rs643.c:41-45`). **Framemaps MUST be transcoded.** The downgrade is mechanical: decode V3, clear `has_transform_actor` / `has_masks` / `tail`, `RSCache_Dat2FramemapEncode` then emits V1.
2. **Frames: rev530 = V1, osrs239 = V1 → byte-compatible**, but only the framemap id in the head (2 bytes) needs remapping. **However `rev_dat2_rs643.c:51` pins `FRAME_V2` (rev 610+)**, so decoding 530 frames with `--rev rs643` is WRONG. A `rev530` profile at `revision = 530` auto-derives correctly (`dat2_frame.c:19-22` threshold 610, `dat2_framemap.c:16-25` thresholds 530/481) and needs only `LOC_RS2` + `FLO_RS2` pins.

### LATENT BUG (report to planner)
`3rd/rscache/tools/common/cache_write.c:545-580` — the cross-codec framemap branch decodes with the source profile and calls `RSCache_Dat2FramemapEncode` **without clearing `has_transform_actor` / `has_masks` / `tail`**. `RSCache_Dat2FramemapEncode` (`src/datatypes/dat2_framemap.c:212-250`) emits those blocks whenever the flags are set. So a V3→V1 port through `port_npc` is a **no-op that silently ships V3 bytes into a V1 cache**. Only `tools/common/transcode.c:118` (the dat2→**dat1** path) warns. Confirmed: `grep -rn has_transform_actor src tools` finds exactly one writer (`dat2_framemap.c:150`, the decoder).

---

## 7. Id allocation + how JS5 serves new records

### Allocation bases — `src/content/content_register.c` `k_defaults[]`

| namespace | base | cache high-water (measured) | headroom |
|---|---|---|---|
| `npc` | 20000 (`:63`) | 16,293 | ✅ |
| `obj` | 40000 (`:64`) | 34,304 | ✅ |
| `loc` | 70000 (`:65`) | 62,200 | ❌ runtime loc-add wire is 16-bit; use a collision-checked id ≤65535 |
| `seq` | 20000 (`:66`) | 14,428 | ✅ |
| `spotanim` | 6000 (`:67`) | 4,009 | ✅ |
| `struct` | 8000 (`:137`) | 6,499 | ✅ |
| `enum` | 5995 (`:136`) | 5,994 | ✅ |
| `param` | 2634 (`:113`) | 2,633 | ✅ |
| `varp` | 5705 (`:170`) | — | ✅ |
| `varbit` | 25000 (`:171`) | — | ✅ |
| `7_models` | 100000 (`:184`) | 61,615 | ✅ |
| `0_animations` | 20000 (`:193`) | 10,902 | ✅ |
| `1_skeletons` | 8000 (`:194`) | 2,674 | ✅ |
| `22_animayas` | 2000 (`:195`) | 914 | ✅ |
| `8_sprites` | 20000 (`:183`) | 8,534 | ✅ |
| `4_soundeffects` | 20000 (`:185`) | 12,010 | ✅ |
| `9_textures` | 1000 (`:191`) | 1 archive | ✅ |
| `3_interfaces` | 2000 (`:77`) | 968 | ✅ |
| `12_clientscripts` | 12000 (`:143`) | 9,725 | ✅ |
| `stat` | **0 = do not allocate** (`:145`) — *"the wire fixes this one — UPDATE_STAT carries the index"* | 23 skills (0..22, `pack/stat.pack`) | ❌ **BLOCKER for a 24th skill** |

Model archive id 100000 > 65535: **safe**. Verified `header_size_for_archive` (`src/dat2disk.c:95-97`) writes the 10-byte extended sector header for `id > 0xFFFF` on both read and write, and every osrs239 reference table is **format 7** (usmart ids) — measured by decoding idx255 directly: idx0/1/2/3/4/7/8/12/22 all `format 7`.

### Who allocates
`tools/ss_allocate.py:84-92` — `SERVER_NAMESPACES = ('enum','struct','dbtable','dbrow','param','mesanim','inv','varp')`. **`npc`, `obj`, `loc`, `seq`, `spotanim`, and every asset namespace are NOT swept.** Their ids must be hand-written into `configs/all.<type>.compack` / `pack/<ns>.pack`. Those saves are **merges** (`lc_pack_save`, `test/test_pack.c`), so a hand-added line above the base survives a re-unpack.

### Making a new record reach the client cache
- **Assets**: `README.md` §"Adding an asset" — drop the file in the table's dir, add `pack/<ns>.pack` line at/above the base, reference it from a config. `cachepack pack --assets` writes the payload **and extends the reference table** via `cp_reference_sync` (`tools/cachepack/cp_binary.c:375-470`) — it grows both `archives` (indexed *by archive id*, gaps at `index == -1`) and `ids` (the ascending list the encoder walks). Before this it warned and shipped 61,615 of 61,616.
- **Configs**: the entity routing gate (`tools/cachepack/cp_pack.c:680-760`). A record reaches the client cache if: named in `pack/<ns>.client` **OR** already in the base cache **OR** `origin_rank == 0`. An **authored** (rank-1) record not in the base cache and not in `<ns>.client` falls to `!(origin_rank > 0 && !fields->records_client)` (`cp_pack.c:528`) → **excluded unless `fields/<type>.ini` says `records = client`**. Only npc, loc, enum, param, varp have membership files today; `obj`/`seq`/`spotanim`/`struct` have none.

### JS5
`docs/JS5_SERVER.md` + `docs/JS5_INCREMENTAL_CACHE.md`: `js5_server` (`src/js5/`) opens whatever cache dir it is pointed at through rscache's read-only dat2 handle, validates every physical index/reference pair, and constructs `255/255`. New archives are served with **no extra wiring** — the client's sparse cache fills from the master index. The metadata-prime barrier runs **before `App_Init`**. So: bake to `cache.osrs239.baked`, point BOTH `js5_server --cache` and the world at it.

---

## 8. "Distinct folder, clearly marked as ported" — feasibility

**Assets: works today, zero tool changes.** The pack name *is* the path under the table dir (`cp_assets.c:1355-1400` `import_one` walks the pack, `snprintf(root, "%s/%s", ctx->srcdir, asset->dir)`). Already in use (`models/npc/…`, `models/idk/`, `models/loc/`). So:
```
pack/7_models.pack        100000=ported/scape2009/summoning/spirit_wolf
models/ported/scape2009/summoning/spirit_wolf.model
```

**Configs: works today via rank-1 overlays.** `cp_pack.c:1669` (and `:1788, :2169, :2605`) — `static const char* const ROOTS[] = { "configs", "server/scripts" };` ranks `{0,1}`. Any `.npc`/`.obj`/`.seq`/`.param` file anywhere under `server/scripts/**` is a rank-1 overlay. So `server/scripts/ported_summoning/configs/*.npc` works with no change. `CP_WALK_MAX_ROOTS = 4` (`cp_walk.h:35`) leaves room for a real third root (e.g. `ported/`) at the cost of editing 4 identical `ROOTS[]` literals.

**Note**: `configs/all.<type>` (rank 0) is the machine export of the cache and gets rewritten by `unpack`. Ported records **must not** live there.

---

## 9. Governance finding

`docs/SCAPE2009_CONTENT_PORT_QUEUE.md:65` — the existing 2009scape→OSRS-Content port queue **explicitly skip-lists Summoning**:
```
| content/global/skill/summoning/**, Wolf Whistle | Summoning is not in OSRS |
| Evil Turnip / summoning-linked patches          | Summoning ecosystem       |
```
This task reverses a documented decision. The queue's methodology (§"Methodology (non-negotiable)") also states: *"No game-facing strings / ids / config constants in C"*, *"Resolve names through the pack — never copy 2009scape / rev-530 ids into osrs239 content"*, *"Interfaces: drive the rev-230 panel; do not invent IF1"*.

Also relevant: `docs/PORTING_GUIDE.md` §1 (pipeline diagram, lines 47-64), §3.5 (`:485-495`, "new client-visible content"), §3.2 item 5 (*"cachepack does not look inside server/scripts at all"* — false for configs, true for `.rs2`).

---

## RISKS / UNKNOWNS

1. **No rev-530 profile. ABSENT.** Must add `src/revisions/rev_dat2_rs530.c` + one row in `revisions.c:22`. `--rev rs643` is WRONG for frames (pins `FRAME_V2`, a rev-610 format). A `revision = 530` profile auto-derives `FRAMEMAP_V3` + `FRAME_V1`; needs explicit `LOC_RS2` + `FLO_RS2` pins. Untested whether rev-530 loc/obj/npc/seq opcode sets match 643's — the `find_anims` sweep printed dozens of `Unrecognized opcode 89/24/27/177/47/55/32/20/21/72/26/99` when walking all seqs, so **at minimum the seq opcode table diverges**. Needs a full exact-consumption sweep before trusting any config decode.
2. **cachepack cannot unpack sharded RS2 configs.** MEASURED: `npc/obj/loc/seq/spotanim/enum/varbit` → *"sharded across groups in this cache"*. Any config port must go through `rscache` directly (as `find_named`/`port_npc` do) or through a new sharded-layout reader in `cp_unpack.c`.
3. **Framemap V3→V1 transcode is a silent no-op today.** `cache_write.c:545-580` does not clear `has_transform_actor`/`has_masks`/`tail`. Confirmed by grep. Familiars would load with dead rigs and no error.
4. **Rig label mismatch across eras.** Model vertex labels + framemap transform labels are rig-local and era-specific (`RIGGING_OSRS_RS2.md`; `port_lostcity` needs `--label-map` + `[export:rig_map]`). Familiars have their own rigs (framemap 54090 for spirit wolf) so they should port self-consistently — **but only if the framemap ports alongside the model**, and only if labels ≤ 255 survive the V3→V1 downgrade. GUESS: self-consistent familiar rigs are the easy case; player-worn summoning items (pouches in the inventory, cape) are the hard case.
5. **Texture ids do not map across revisions** (EXCEPTIONS A5). rev530 has 680 texture archives; osrs239's whole texture table is 1 archive of materials. Any 530 model with a textured face will reference a material id that means something else in osrs239 → wrong texture or a hard drop. `--texture-map` exists in `port_npc`/`port_lostcity` but building the map is manual.
6. **Summoning skill id 23 has no home.** `pack/stat.pack` is 0..22; `content_register.c:145` gives `stat` base 0 = do-not-allocate ("the wire fixes this one"). `TORIRSSERVER_STAT_COUNT = 23` (`src/torirsserver/torirs_server.h:558`) vs `RS_PLAYER_STATS_SKILL_COUNT = 25` (`src/game/rs_player_stats.h:11`) — the two already disagree. The osrs239 client's skill tab, XP drops, skill guide and `UPDATE_STAT` handler are cache/CS2-driven; adding a 24th skill is a client+cache+wire change, **not an asset-pipeline change**. This is likely the single largest unknown.
7. **CS2 cannot be ported.** The command table (`src/cs2/cs2_command.gen.h`, 1,157 lines) is generated from RuneStar's **OSRS** opcode table. rev-530 CS2 uses a different opcode numbering entirely. The Summoning interface's scripts must be authored fresh against osrs239 opcodes. ABSENT for import.
8. **Interfaces**: rev530 IF3 uses the RS2 type-5/type-6 component layout (`rev_dat2_rs643.c:15-18`: type 5 carries a trailing colour int and orders flips H,V; type 6 carries `aShort49`+`aBoolean411`). Whether rev530 matches 643 here is UNVERIFIED. Plus every hook in a ported interface references a rev-530 script id → dead. GUESS: author the summoning tab as a new osrs239 `.if` rather than porting.
9. **animaya is decode-only.** Irrelevant for rev530 (which predates skeletal animation) but blocks any modern-OSRS-sourced skeletal familiar.
10. **BasType has no cachepack type.** `RSCache_Dat2ConfigBasEncode` exists in the library; there is no `bas` row in `cp_types.c`, so `configs/all.bas` does not exist and BasType cannot be authored from the tree. Every rev-530 familiar probed uses `bas_type_id` (1326, 1329) with `standing_anim = -1`. **This must be closed or familiars will T-pose.**
11. **`--base` + fresh `--out` is mandatory.** EXCEPTIONS B4: the dat2 writer appends and orphans; repacking in place grows without bound. `make -C src torirsserver-cache` already does `rm -rf $(TORIRSSERVER_CACHE_DIR)` first.
12. **`cachepack --gamevals` skips archive 14** (interfaces/components nesting), so ported interface component names never reach the cache's own symbol table. Cosmetic — nothing outside cachepack reads it.
13. **Fidelity suite skips when no cache is present** — and a skip reads as a pass (known trap, `MEMORY.md` "Pristine baseline skips"). Any CI claim about a summoning bake must assert the suite actually ran.
14. **UNKNOWN**: whether the 2009scape cache's XTEA-encrypted map archives matter. rev530 maps are pre-237 so `CP_ASSET_ENCRYPTED` applies; no `xteas.json` was located in `2009scape/Server/data/`. Not needed if no new map squares are ported.
15. **UNKNOWN**: what feature-flag mechanism to use. `src/features/features.h` is a *client engine era* table (`ToriRS_FeatureTable`, keyed by `ToriRS_Features_ForCache(game, epoch, revision)`) — it has no per-content flags and no notion of a content feature. There is **no existing content feature-flag seam**; one must be designed (GUESS: a varbit + a `content.ini` key + a compile-time gate on the ported `server/scripts` subtree).

Scratch probe tree (safe to delete): `/private/tmp/claude-501/-Users-matthewevers-Documents-git-repos-3draster/24607df3-1c87-44c1-b268-71a42635e4ab/scratchpad/rev530probe`

===== RECON: rs-skills-system =====
RECON: 3draster skill system end to end — adding a 24th skill

## 0. HEADLINE (read this first)

**Stat index 23 is NOT free.** The osrs239 cache already assigns **23 = Sailing**.
- `OSRS-Content/osrs239-content/configs/all.enum` → `[enum_681]` (int→stat, the client's canonical skill roster) has `val=24,23`.
- `[enum_108]` (int→string) has `valstr=24,Sailing`; `[enum_680]` (stat→string) has `valstr=23,Sailing`; `[enum_255]` (stat→graphic) has `val=23,228`.
- `scripts/script_8950.cs2` is `[proc,script8950](int)` → `switch_int { case 23: return(~script8951(1)) }` — a hard-coded 23 gating Sailing out of total level / total xp.
- `interfaces/stats.compack` child 24 = `sailing`; `interfaces/levelup_display.compack` child 57 = `sailing`.

2009scape's `Server/src/main/core/game/node/entity/skill/Skills.java:57` defines `SUMMONING = 23` — a **direct collision** with the cache. Summoning must take **24** (or displace Sailing, which would corrupt cache-native CS2).

Also: `OSRS-Content/osrs239-content/pack/stat.pack` stops at `22=construction` — **sailing (23) is ABSENT from stat.pack** while it is present in the cache enums. The pack and the cache already disagree; whichever id Summoning takes, this gap must be resolved first.

---

## 1. `src/game/rs_player_stats.h` / `.c`

| Fact | Location |
|---|---|
| `#define RS_PLAYER_STATS_SKILL_COUNT 25` | `src/game/rs_player_stats.h:11` |
| Named ids: ATTACK 0, DEFENCE 1, STRENGTH 2, HITPOINTS 3, RANGED 4, PRAYER 5, MAGIC 6, RUNECRAFT 20 | `rs_player_stats.h:15-23` |
| `struct RS_PlayerStats { int base_level[25]; current_level[25]; xp[25]; }` | `rs_player_stats.h:27-29` |
| Total level = `for(i=0;i<=17;i++) + base_level[RUNECRAFT]` — **hardcoded 2004-era 19-skill sum** | `rs_player_stats.c:51-54` |
| Combat level: def+hp+pray/2, melee/ranged/magic max — Summoning cannot enter | `rs_player_stats.c:78-88` |
| xp table built in `RS_PlayerStats_Init`, `level_xp[99]`, `floor(l + 300*2^(l/7))/4` | `rs_player_stats.c:19-24` |

**Where indices come from:** nowhere in C. The client's stat index is purely the wire index (`UPDATE_STAT`'s `stat` byte) used as a raw array subscript. No name table, no enum, no validation beyond `0 <= stat < 25`.

**Is 23 usable in the C client?** Yes mechanically (25 slots). **24 is also usable** and is the last slot. But 23 is semantically Sailing per the cache — see §0.

**Consumers of RS_PLAYER_STATS_SKILL_COUNT** (all bound at 25, none at 23):
- `src/game/rs_gameproto_exec.c:561-575` (UPDATE_STAT apply)
- `src/game/rs_cs2_host.c:5336` (CS2 `STAT`/`STAT_BASE`/`STAT_XP`)
- `src/game/rs_cs1_host.c:177` (CS1 stat opcodes)
- `src/game/rs_player_stats.c:64,103`

---

## 2. The wire — is the stat index bounded by 23?

**No. Nowhere is 23 a bound on the client wire path.** The stat field is a `p1` (byte) in every revision.

| Rev | Decode | Layout |
|---|---|---|
| osrs239 | `src/net/rev/osrs239/osrs239_parse.c:755-763` | `g1 invisibleBoostedLevel`, `g1 level`, `g1_add128 stat`, `g4_3412 xp` |
| osrs230 | `src/net/rev/osrs230/osrs230_parse.c:380-400` | `g1 stat`, `g1 base`, `g4 xp`, `g1 boosted` (mock's own order) |
| lc245_2 / lc254 | `src/net/rev/gameproto_parse.c:284-290` | `g1 stat`, `g4 xp`, `g1 level` |
| RSProt bridge | `src/net/rev/rsprot_bridge.c:447-454` → `3rd/rsprot/packets/update_stat_v2.h` `MsgUpdateStatV2{stat,experience,invisible_boosted_level,current_level}` | 14 layouts, revs 226-239; stat always a byte |

Packet sizes: `osrs239/packetin.h:175` op 46 len 7; `osrs230/packetin.h:55` op 114 len 7; `lc254/packetin.h:144` len 6; `lc245_2/packetin.h:142` len 6.

**Only a comment claims 23:** `src/net/rev/revpacket.h:116` — `int stat; /* g1: 0-22 */`. Stale, not enforcing.

Apply site: `src/game/rs_gameproto_exec.c:561-575` — bounds against 25, calls `RS_PlayerStats_SetXp`, writes `current_level`, calls `RS_CS2Host_NotifyStatChanged` then `RS_PlayerStats_RecomputeCombatLevel`.

---

## 3. Skill NAMES and ids

Three independent tables, and they must agree:

1. **`OSRS-Content/osrs239-content/pack/stat.pack`** — the *server/compiler* name table. Authored, 23 lines `0=attack` … `22=construction`. Header comment: "Fixed by the protocol … so they are authored, not imported." Declared in `content.ini:103-105`:
   ```
   [namespace:stat]
   ids   = protocol
   names = authored
   ```
   Read by: `torirs_server_content.c` (pack kind `TORIRSSERVER_PACK_STAT`, name `"stat"`, `torirs_server_content.c:571`), `src/serverscript/ssc_symbols.c:505` (`{"stat", SSC_SYM_STAT}`), `tools/gen_levelrequire_dbrow.py:107`.

2. **Cache enums** — the *client's* roster (see §0): `enum_681` int→stat, `enum_108` int→string, `enum_680` stat→string, `enum_255` stat→graphic, `enum_1497` stat→members-only bool, `enum_256` level→xp threshold. All in `configs/all.enum`.

3. **Component names** — `interfaces/stats.compack` (25 named cells + total), `interfaces/levelup_display.compack` (per-skill + `combat` + `sailing`).

`pack/stat.pack` is **not** swept by `tools/ss_allocate.py` (`SERVER_NAMESPACES` at `tools/ss_allocate.py:84-92` — no `stat`). Adding a line is a hand edit.

---

## 4. XP drops / level-up jingle / level-up interface

**XP drops — client CS2 only, ABSENT server-side.**
- Interface `122 = xp_drops` (`pack/3_interfaces.pack:123`), plus `xpdrops_setup` (`pack/3_interfaces.pack` — see `xpdrops_setup.if`).
- Mounted by content: `server/scripts/interface_orbs/scripts/orbs.rs2:41-68` (`[proc,xpdrops_sync_mount]`, `%xpdrops_enabled`).
- The drops themselves are computed **in CS2** from `if_setonstattransmit`. 27 clientscripts read `enum_681`: `script_393/999/1004/1007/1008/1010/1022/1026/1320/1521/1904/2091/3201/3806/3807/4375/5446/5451/5452/5455/5466/5469/5833/702/7628/7629/9343`.
- **Hardcoded 24 in CS2:** `scripts/script_1010.cs2:19-21` — `def_stat $statarray0(24); while ($int48 <= 24) { … if ($int48 < 24) … }`. Also literal `24` counts in `script_1004` (3), `script_1026` (12), `script_1904` (13), `script_999` (11), `script_9343` (3). A 25th skill requires **recompiling these CS2 scripts and re-baking the cache**.

**Level-up interface `233 = levelup_display`** (`pack/3_interfaces.pack:234`). It is in the cache; **nothing in this tree ever mounts it** — `grep levelup_display` over `server/scripts/` and `src/` returns nothing. ABSENT.

**Level-up jingle: ABSENT end-to-end.** `SS_OP_MIDI_JINGLE 2064` (`src/serverscript/ss_opcode.h:138`) has **no handler** in `src/torirsserver/` (only a name in `torirs_server_wire.c:1282`). Not in `torirs_server_opcode_coverage.gen.h`.

**What level-up actually does today:** `ToriRSServer_CombatAddXp` fires `SS_TRIGGER_ADVANCESTAT` (160) when the base level changes → `OSRS-Content/osrs239-content/server/scripts/levelup/scripts/levelup.rs2`, which is **23 `[advancestat,<skill>]` lines** all routing to `[label,levelup]` → `mes("You feel yourself getting stronger."); ~summary_combat_level_push;`. No sailing line. A Summoning line must be added here.

**Total level / total xp in CS2** are enum-driven and therefore self-extending: `script_1007.cs2` (`[proc,stat_totallevel]`) and `script_1008.cs2` (`[proc,stats_totalxp]`) walk `enum_681` until null, skipping anything `~script8950` gates. Adding a row to `enum_681` puts Summoning in total level automatically. Adding a `case 24:` to script 8950 is how you'd gate it.

---

## 5. ToriRSServer server skill implementation

**Storage** — `src/torirsserver/torirs_server.h`:
- `TORIRSSERVER_STAT_COUNT = 23` at **`torirs_server.h:558`** (enum with ATTACK 0 … MAGIC 6, AGILITY 16). Header comment claims "the array is the full 23 so a stat id from the wire is never out of range" — that claim is now false vs. the cache (sailing 23).
- `int stat_level[23]; int stat_boosted[23]; int stat_xp_tenths[23];` (~`torirs_server.h:2462-2464`)
- `uint32_t stat_dirty;` (`torirs_server.h:2466`) — **bitmask, hard ceiling 32 stats**. Set at `torirs_server_combat.c:354` (`1u << stat`), read at `torirs_server_world.c:8927`.
- Npc drain: `int stat_drain[TORIRSSERVER_STAT_COUNT];` (`torirs_server.h:~1711`)

**XP table** — `src/torirsserver/torirs_server_combat.c:400-441`: `static int g_xp_table[99]`, `ensure_xp_table()` floors each term before summing (83 @ L2, 13,034,431 @ L99). `ToriRSServer_CombatLevelForXp` / `ToriRSServer_CombatXpForLevel`.

**Stat-advance path** — `src/torirsserver/torirs_server_combat.c:469-508` `ToriRSServer_CombatAddXp(srv, stat, tenths)`:
1. reject `stat < 0 || stat >= TORIRSSERVER_STAT_COUNT || tenths <= 0`
2. `stat_xp_tenths[stat] += tenths`; `stat_level[stat] = level_for_xp(xp/10)`
3. on level change: HP sync, then `ToriRSServer_ScriptsRunTriggerSpecific(srv, SS_TRIGGER_ADVANCESTAT, stat, -1, -1)`
4. raise `stat_boosted` (except HP); `ToriRSServer_CombatStatMark`

Only production caller: `SS_OP_STAT_ADVANCE` at `torirs_server_scripts.c:7666-7681`. Debug caller: `::setlevel` → `ToriRSServer_CombatSetLevel` at `torirs_server_world.c:4885`.

**Transmit** — `torirs_server_world.c:8925-8935` (phase 10 `phase_client_out`): walks `0..TORIRSSERVER_STAT_COUNT`, tests `stat_dirty` bit, calls `ToriRSServer_SendStat(player, stat, level, xp_tenths/10, boosted)` → `torirs_server_encode.c:1572-1600` → `pl->update_stat` (rev-dispatched) or fallback `p1 stat, p1 level, p4 xp, p1 boosted`. Cleared in `phase_cleanup_player`.

**Persistence** — `torirs_server_save.c:201-210` writes `[stats] <id> = <boosted> <xp_tenths>`, skipping untouched stats; load at `:480-511` rejects `stat >= TORIRSSERVER_STAT_COUNT`. **Widening TORIRSSERVER_STAT_COUNT is save-compatible** (id-keyed ini).

**Seed** — `torirs_server_world.c:7865` loops 0..COUNT setting level/boosted 1, xp 0; content raises HP via `[proc,newplayer_stats]` in `server/scripts/player/newplayer.rs2:66-70` (`stat_advance(hitpoints, 11540)`).

**Combat level (server)** — `torirs_server_combat.c:913-925` `ToriRSServer_CombatLevel()`: def+hp+pray/2 and atk+str only (melee-only; no ranged/magic term — a separate pre-existing inaccuracy). Content's version: `server/scripts/player/scripts/combat_level.rs2:5-11` `[proc,player_combat_level]`. **Neither reads Summoning.** ✅

---

## 6. ServerScript stat commands

Opcodes, `src/serverscript/ss_opcode.h:187-196`:

| Op | Id | Impl |
|---|---|---|
| `STAT_ADD` | 2113 | `torirs_server_scripts.c:7509` |
| `STAT_ADVANCE` | 2114 | `torirs_server_scripts.c:7666` |
| `STAT_BASE` | 2115 | `torirs_server_scripts.c:7389` |
| `STAT_BOOST` | 2116 | `torirs_server_scripts.c:7436` |
| `STAT_DRAIN` | 2117 | `torirs_server_scripts.c:7444` |
| `STAT_HEAL` | 2118 | `torirs_server_scripts.c:7596` |
| `STAT_RANDOM` | 2119 | `torirs_server_scripts.c:7644` |
| `STAT_SUB` | 2120 | `torirs_server_scripts.c:7443` |
| `STAT_TOTAL` | 2121 | `torirs_server_scripts.c:7572` — **loops `0..TORIRSSERVER_STAT_COUNT` summing `stat_level`** |
| `STAT` | 2122 | `torirs_server_scripts.c:7369` (returns *boosted*) |
| `NPC_BASESTAT` / `NPC_STAT` / `NPC_STATADD` / `NPC_STATHEAL` / `NPC_STATSUB` | 2504, 2538-2541 | `torirs_server_ops_npc.c` |

**Id-driven at runtime, name-driven at compile time.** Every handler aborts on `stat < 0 || stat >= TORIRSSERVER_STAT_COUNT` (`SSVM_Abort`, not a silent no-op — good).

Name resolution: `ssc_compile.c:770-772` sets `base_hint = SSC_SYM_STAT` when the opcode name matches `STAT_*` / `STAT` / `NPC_STAT*` / `NPC_BASESTAT` (the list is spelled out at `ssc_compile.c:755-771` — a new stat command must be added by hand). So `stat_base(summoning)` would resolve **as soon as `summoning` is in `pack/stat.pack`**.

⚠️ **The trigger header does NOT use the stat hint.** `ssc_compile.c:2286-2296` resolves `[advancestat,<subject>]` with `SSC_SymbolsFind(..., SSC_SYM_UNKNOWN)` — an any-namespace lookup. `[advancestat,summoning]` will resolve to whatever namespace sorts first. **Currently safe**: no exact `summoning` symbol exists in any pack (nearest: npc `summonedzombie` 69, obj `regicide_quest_kings_summons` 3206, dbrow `synth_summon` 4546, varbit `delrith_seen_summoning_cutscene` 2569). But a ported `summoning` obj/loc/npc later would silently mis-resolve the trigger — the exact class of bug documented at `ssc_compile.c:755-770` for `npc_basestat(hitpoints)`.

`SSC_SYM_STAT` enum: `ssc.h:129`. Pack→kind map: `ssc_symbols.c:505`.

---

## 7. Total level / combat level — "all skills" iterations

| Site | Bound | Includes Summoning if widened? |
|---|---|---|
| `SS_OP_STAT_TOTAL` `torirs_server_scripts.c:7572-7582` | `TORIRSSERVER_STAT_COUNT` | **Yes** (correct for RS2, but no gate exists) |
| `RS_PlayerStats_TotalLevel` `rs_player_stats.c:48-56` | hardcoded `0..17` + `RS_SKILL_RUNECRAFT` | **No** — 2004-era, would need explicit edit |
| CS2 `[proc,stat_totallevel]` `scripts/script_1007.cs2` | walks `enum_681` until null, minus `~script8950` | **Yes**, automatically |
| CS2 `[proc,stats_totalxp]` `scripts/script_1008.cs2` | same | **Yes** |
| CS2 `[proc,stat_f2plevel]` `scripts/script_1320.cs2` | same | **Yes** |
| `ToriRSServer_CombatLevel` `torirs_server_combat.c:913-925` | 5 named stats | **No** ✅ |
| `[proc,player_combat_level]` `combat_level.rs2:5-11` | 7 named stats | **No** ✅ |
| `RS_PlayerStats_RecomputeCombatLevel` `rs_player_stats.c:76-89` | 7 named stats | **No** ✅ |

**Combat level is safe by construction in all three implementations.** Total level is the only place a 24th skill leaks in, and that is arguably desired.

---

## 8. Complete inventory of hardcoded skill-count / skill-id constants

**C — engine/client**
- `src/game/rs_player_stats.h:11` `RS_PLAYER_STATS_SKILL_COUNT 25`
- `src/game/rs_player_stats.h:15-23` `RS_SKILL_ATTACK…MAGIC` (0-6), `RS_SKILL_RUNECRAFT 20`
- `src/game/rs_player_stats.c:51` `for(i=0; i<=17; i++)` — total level
- `src/net/rev/revpacket.h:116` comment `/* g1: 0-22 */` (stale)

**C — mock server**
- `src/torirsserver/torirs_server.h:558` `TORIRSSERVER_STAT_COUNT = 23` + named ids 0-6, `AGILITY 16`
- `src/torirsserver/torirs_server.h:2466` `uint32_t stat_dirty` (32-stat ceiling)
- 28 uses in `torirs_server_world.c`; 10 in `torirs_server_scripts.c`; 3 in `torirs_server_combat.c`; 2 in `torirs_server_save.c`; 1 in `torirs_server_pack.c` (50 total repo-wide)
- `src/torirsserver/torirs_server_world.c:11803-11812` — `k_cells[]`, **24 hardcoded `stats:<skill>` component names including `stats:sailing`**, with `CELL_COUNT` selftest asserting all 24 bind distinct skills
- `src/torirsserver/torirs_server_world.c:23478-23480` — selftest `cooking > 0 && cooking < TORIRSSERVER_STAT_COUNT`

**Content — server half**
- `OSRS-Content/osrs239-content/pack/stat.pack` — 23 rows, `0=attack`…`22=construction`. **No sailing.**
- `server/scripts/levelup/scripts/levelup.rs2:8-30` — 23 `[advancestat,X]` blocks
- `server/scripts/interface_skill_guide/scripts/skill_guide.rs2:96-119` — 24 `if_setevents(stats:X, …)` incl. `stats:sailing`; `:153+` — 24 `[if_button2,stats:X]` blocks
- `server/scripts/player/scripts/combat_level.rs2:5-11` — 7 stats
- `server/scripts/player/newplayer.rs2:66-70` — hitpoints seed

**Content — cache half (client-visible, requires a bake)**
- `configs/all.enum` `[enum_681]` 24 rows (1→0 … 24→23)
- `configs/all.enum` `[enum_108]` 24 valstr rows
- `configs/all.enum` `[enum_680]` 24 valstr rows
- `configs/all.enum` `[enum_255]` 24 stat→graphic rows (197-228)
- `configs/all.enum` `[enum_1497]` 9 members-only rows
- `configs/all.enum` `[enum_256]` 98 level→xp rows
- `interfaces/stats.compack` — cells 1-24 + `total`; `interfaces/stats.if` per-cell `onload=i:393,…,i:<ordinal>,i:<col>`
- `interfaces/levelup_display.compack` — 59 children, per-skill + `combat` + `sailing`
- `scripts/script_8950.cs2` — `case 23:` (Sailing gate)
- `scripts/script_1010.cs2:19,21,42` — `def_stat $statarray0(24)`, `while ($int48 <= 24)`, `if ($int48 < 24)`
- literal-24 caps also in `script_1004`, `script_1026`, `script_1904`, `script_999`, `script_9343`, `script_4375`, `script_5446`

**Python tools**
- `tools/gen_levelrequire_dbrow.py:105-113` — parses `pack/stat.pack` (no hardcoded count) ✅
- `tools/ss_allocate.py:84-92` — `SERVER_NAMESPACES` does **not** include `stat`

**ABSENT / not found anywhere:** any `MAX_SKILLS`, any skill-name string table in C, any `.py` skill-name list, any per-skill varp table in the server.

---

## 9. Feature-flag mechanism — what exists, what doesn't

**Exists (client behaviour only, wrong shape for this):** `src/features/features.h` / `features.c` — `struct ToriRS_FeatureTable`, eras `LOSTCITY`/`OSRS`/`SERVER_ROUTED`. Resolution order (`src/app.c:3446-3448`): `[features:boot] era=` in the manifest → `TORIRS_FEATURES_ERA` env → `ToriRS_Features_ForCache()`. Server side reads its own copy in `src/torirsserver/torirs_server_boot.c:88-116`. Every field is an *era* switch (pathing, painter, lighting, audio) — there is no boolean "content feature" slot and no per-skill/per-content notion.

**ABSENT: any content feature-flag mechanism.**
- `sscompile` has **no include/exclude flag** — `src/serverscript/ssc_main.c:50-72` accepts only `--src`, `--out`, `--pack`, `--constants`.
- `collect_sources` (`ssc_compile.c:2866-2910`) recurses everything under `--src` taking every `*.rs2`; skips only names starting with `.`.
- `walk_configs` (`torirs_server_content.c:2868-2890`) recurses everything under `server/scripts` by extension; skips only names starting with `.`.
- So a new directory `OSRS-Content/osrs239-content/server/scripts/ported_summoning/` is **picked up automatically with no opt-in**, and the only existing way to exclude it is a leading `.` in the directory name (a hack, and `docs/SKILLS_CONTENT_PORT_QUEUE.md:33-37` explicitly forbids `.skip`-style parking of content).

GUESS: the three plausible flag designs are (a) a new `content.ini` key + a directory filter shared by `collect_sources` and `walk_configs` (touches two walkers in two binaries), (b) a varbit/varp guard at the top of every ported script + a `stat.pack` entry that is always present (cheap, but Summoning still appears in the skills tab), (c) an env var read at `ToriRSServer_BootLoad` gating a directory. None of these exist today.

**Precedent for "clearly marked as ported":** the tree already uses a `_lostcity` file suffix (`server/scripts/skill_combat/configs/equipment_lostcity.obj`) and a `port/` directory of ledgers (`port/names.map`, `port/configs.map`, `port/constants.map`, `port/vars.map`, `port/categories.map`, `port/name_diff.signed`) checked by `make -C src test-port` (`src/Makefile:1992-2016`).

---

## 10. Build / verification path (what a change has to pass)

- `make -C src torirsserver-scripts` — `tools/ss_allocate.py` then `sscompile --src <tree>/server/scripts --out .../build --pack .../pack --pack .../configs` (`src/Makefile:1616-1622`)
- `make -C src torirsserver-servpack` — `cachepack pack --server-only` → `<tree>/server/pack` (gitignored)
- `make -C src torirsserver-cache` — **required for any client-visible edit** (CS2, interfaces, enums, sprites, models); `--base cache.osrs239 --out cache.osrs239.baked` (`src/Makefile:1671-1690`)
- `make -C src test-content` → `test-content-register`, `test-servercodec`, `test-ss-symbols`, `torirsserver-scripts`, `torirsserver-servpack`, `test-membership`, `torirsserver-pack`, `test-server-clean`, `test-port` (`src/Makefile:1861`)
- `make -C src test-ToriRSServer` — runs `torirs_server_world.c`'s selftests, incl. the 24-cell `k_cells` skill-guide assertion at `:11803`
- cachepack can encode `enum` (config record), `interface`, `model`, `sprite`, `script` (`3rd/rscache/tools/cachepack/README.md` "Every table"). Enum routing: `fields/enum.ini` says `records = server` by default; a record already in the base cache still packs — so editing `enum_681`/`680`/`108`/`255` lands without touching `pack/enum.client`.

---

## RISKS / UNKNOWNS

1. **Stat 23 is Sailing in cache.osrs239.** Taking 23 for Summoning (2009scape's own index) breaks 27 cache-native CS2 scripts that read `enum_681`. Taking 24 diverges from 2009scape and consumes the last slot in `RS_PLAYER_STATS_SKILL_COUNT 25`. Neither is free. **Decision required before anything else.**
2. **`pack/stat.pack` is already inconsistent with the cache** (no `23=sailing`). Whether that gap is deliberate is not documented anywhere I found. Fixing it is a prerequisite and may itself break `[advancestat]`/skill-guide assumptions.
3. **`TORIRSSERVER_STAT_COUNT` widening touches 50 sites**, including a `uint32_t` bitmask (`stat_dirty`, safe to 32) and stack VLAs (`int saved_level[TORIRSSERVER_STAT_COUNT]` x6 in `torirs_server_world.c` selftests). Also silently expands `stat_total` for every existing script.
4. **No content feature-flag mechanism exists.** Building one means editing two independent tree walkers (`ssc_compile.c:collect_sources`, `torirs_server_content.c:walk_configs`) plus, probably, `content.ini` and `content_register.c`. There is currently no way for the client cache half to be flagged at all — a baked cache either has the Summoning enum rows or it doesn't.
5. **Standing policy says no.** `docs/PORTING_GUIDE.md:35,683` and `docs/SCAPE2009_CONTENT_PORT_QUEUE.md:65,68` explicitly exclude Summoning; `docs/SKILLS_CONTENT_PORT_QUEUE.md:101` lists it as "not in OSRS". These must be amended or the port contradicts the repo's own port rules.
6. **`[advancestat,summoning]` resolves unhinted** (`ssc_compile.c:2286-2296`). Safe today, becomes a silent mis-resolution the moment a Summoning obj/loc/npc named exactly `summoning` is imported.
7. **CS2 recompile risk.** `script_1010.cs2` etc. carry literal `24`. This tree has an open CS2 round-trip failure list (`docs/CS2_REV239_ROUNDTRIP_FAILURES.tsv`, `CS2_REV239_ROUNDTRIP_QUEUE.md`) — UNKNOWN whether these specific scripts round-trip; if they don't, they can't be edited safely.
8. **Level-up UI and jingle are ABSENT for every existing skill** (interface 233 never mounted, `MIDI_JINGLE` 2064 unimplemented). Summoning would inherit that gap, not create it — but "the level-up card doesn't show" will be reported as a Summoning bug.
9. **Summoning points are not a stat.** In RS2 they are a separate resource (varbit) with its own orb/HUD. Nothing in this recon covers where that would live; there is no per-player scalar slot for it outside varps (`TORIRSSERVER_VARP_COUNT` is already a boot-time hard check at `torirs_server_content.c:3191-3210`).
10. **Working tree is live.** `src/torirsserver/torirs_server_world.c` and `torirs_server.h` shifted ~175 lines during this session (concurrent edits). Line numbers above are as-read; re-verify before patching.
11. **UNKNOWN:** whether `cachepack pack` round-trips `configs/all.enum` edits byte-faithfully for enums with mixed `val=`/`valstr=` — not tested here.
12. **UNKNOWN:** the 2009scape rev-530 asset side (familiar models/animations/interfaces, ~104 files under `Server/src/main/content/global/skill/summoning/`) — model/anim transcoding from dat1-era rev-530 to osrs239 dat2 was not investigated in this recon.

===== RECON: rs-cs2-skilltab =====
Recon complete. Findings below.

---

# 3draster CS2 toolchain + rev-239 skills tab — recon

## 1. CS2 toolchain: decompiler AND compiler both exist, and both are wired into the content pipeline

**Library** (the real implementation): `3rd/rscache/src/cs2/`
- `cs2_decompile.c` / `cs2_compile.c` — `RSCache_CS2_Decompile` / `RSCache_CS2_Compile`
- `cs2_command.gen.h` (opcode signature table, generated by `3rd/rscache/tools/cs2/gen_cs2_tables.py` + `local_commands.py`), `cs2_names.c`, `cs2_lossless.c`, `cs2_cfa.c`/`cs2_dfa.c`/`cs2_ir.c`/`cs2_gen.c`

**CLI**: `3rd/rscache/tools/cs2/main.c` → built binary `3rd/rscache/tools/cs2/cs2` (present, built 2026-08-09)
```
cs2 decompile   (--cache DIR | --raw DIR) [--names DIR] [--out DIR] [id ...]
cs2 compile     --src DIR [--cache DIR] [--names DIR] [--out DIR]     # --src must be a DIRECTORY
cs2 roundtrip   (--cache DIR | --raw DIR) [--names DIR] [--dump DIR] [id ...]
cs2 disassemble (--cache DIR | --raw DIR) id ...
cs2 codec / cs2 infer-arity
```
`compile` ignores explicit ids — it `opendir`s `--src`, compiles every `*.cs2`, and takes the script id **from the source's own `// <id>` header**, writing `<out>/<id>` (`main.c:563-643`).

**The VM in `src/cs2vm2/` is a third, separate thing** — the *runtime* interpreter, with its own opcode table (`cs2_opcode_meta.c`, `cs2_opcode.h`) and a generated stack bridge (`gen_opcode_stack.py` → `cs2vm2_opcode_stack.gen.h`). It is not a decompiler. Keeping it in sync with `cs2_command.gen.h` is a documented failure mode (`docs/CS2_REV239_ROUNDTRIP_QUEUE.md` §10.4, last bullet: stale 6231/6232/8001 shapes).

`tools/cs2_gen_opcodes/` is a *different* generator (`gen_opcodes.py`, `local_opcodes.py`, `opcode_docs.py`, `validate_cache.py`) — vendored opcode metadata, not a compiler.

### Round-trip status — two different numbers, and the second one is the one that matters

**(a) Fresh decompile→compile from the cache: 100%.** `docs/CS2_REV239_ROUNDTRIP_FAILURES.tsv` (3 lines total):
```
# round-trip: 9724/9725 decompiled, 9724 compiled, 9724 same-length, 9724 exact
0  decompile  absent_cache_entry  script 0 is not in this cache
```
Verified live this session: `cs2 roundtrip --cache cache.osrs239 --rev osrs239 393 395 396 1902 1904 1007` → `6/6 exact`. This uses `RSCache_CS2_DecompileOptions.lossless` (§10.6: `@rscache-lossless-v1` hex snapshot, invalidated by any source edit — 539 of 9724 need it).

**(b) The committed tree's `.cs2` sources: 98.99%.** MEASURED THIS SESSION:
```
OSRS-Content/osrs239-content/scripts/   9368 *.cs2   +   357 *.cs2b   = 9725
cs2 compile --cache cache.osrs239 --rev osrs239 --names <runestar> \
    --src OSRS-Content/osrs239-content/scripts --out /tmp/…
  → compiled 9273, failed 95
```
The 95 failing ids (an edit to any of these **cannot ship**):
`228 1904 7919 8157 8185-8189 8192 8195 8196 8203-8213 8224 8317 8329 8330 8339-8358 8361 8368 8376-8388 8401-8429 8441-8444 8450 8476-8481 8944 9054 9124 9127 9134 9140 9180 9189 9190 9232 9238 9244 9250 9363-9374 9390 9415 9476 9517 9593 9596 9621 9630 9720`

Failure taxonomy: 68 `unexpected '('`, 11 `expected ','`, 8 `'(' is not a comparison`, 4 `unknown command '_1703'`, 3 `expected ')'`, 1 `'true' is neither a command nor a resolvable constant`.

**`script_1904.cs2` is on that list** — `line 62: unknown command '_1703'`. 1904 is the skill-guide panel builder. But `cs2 roundtrip … 1904 228 8157 9189` → `4/4 exact` against the cache. **Conclusion: the tree's committed `.cs2` files are STALE — written by an older cachepack build.** The fix is a re-unpack of `scripts/`, not a compiler fix. Not yet done; nobody has needed to edit those 95.

### Names are a hard dependency, and missing them is near-silent

Measured: compiling `OSRS-Content/osrs239-content/scripts/script_393.cs2` **without** `--names` → `FAIL script_393.cs2: line 8: unknown constant '^iftype_graphic'`. With `--names ~/Documents/git_repos/cs2/src/main/resources/org/runestar/cs2` → `compiled 1, failed 0`.

`src/makefile:1675` — `RUNESTAR_CS2_NAMES ?= $(HOME)/Documents/git_repos/cs2/src/main/resources/org/runestar/cs2` (this directory **exists** on this machine; it has `stat-names.tsv` with ids 0..22 only). `torirsserver-cache` exports it as `CACHEPACK_CS2_NAMES` when set. In `cachepack`, a compile failure prints `cachepack: script <id>: <error>` to stderr and then **ships the base cache's bytes** (`cp_decode.c:2447-2452`) — the edit goes nowhere and only a counter says so.

`stat_24` is a legal spelling without a name table: `cs2_names.c:630-677` — `RSCACHE_CS2_TYPE_STAT` falls back to `<literal>_<id>` and the compiler reads it back. The comment even names `stat_23` = Sailing as the case that forced this. **No `stat-names.tsv` edit is required to write Summoning CS2.**

## 1b. THE EDIT-AND-REPACK PATH (the crux)

The content tree already holds every clientscript as source. A component/sprite/script id is claimed by a `pack/` line; assets are imported by walking that pack, not the directory (`cp_assets.c:1225-1231`, `:1383-1387`) — *"a file whose name it does not list has no id to be written to… adding an asset is a two-step (name it, then place it)"*.

```sh
# 0. (once, recommended) refresh the stale sources for the scripts you must edit
CACHEPACK_CS2_NAMES=~/Documents/git_repos/cs2/src/main/resources/org/runestar/cs2 \
  3rd/rscache/tools/cachepack/cachepack unpack --cache cache.osrs239 --rev osrs239 \
    --src OSRS-Content/osrs239-content --assets=scripts

# 1. edit the source in place
$EDITOR OSRS-Content/osrs239-content/scripts/script_393.cs2      # keep the `// 393` header

# 2. prove it compiles BEFORE baking (this is the check that is otherwise near-silent)
mkdir -p /tmp/cs2src && cp OSRS-Content/osrs239-content/scripts/script_393.cs2 /tmp/cs2src/
3rd/rscache/tools/cs2/cs2 compile --cache cache.osrs239 --rev osrs239 \
  --names ~/Documents/git_repos/cs2/src/main/resources/org/runestar/cs2 \
  --src /tmp/cs2src --out /tmp/cs2bin           # want: compiled 1, failed 0

# 3. bake the whole tree into the cache the client boots
make -C src torirsserver-cache          # src/makefile:1676-1692
   # = cachepack pack --src OSRS-Content/osrs239-content --base cache.osrs239 \
   #       --out cache.osrs239.baked --rev osrs239 --assets --binary --gamevals
   # then torirsserver-cache-check asserts all 23 idx tables landed

# 4. run
./src/torirs --manifest manifests/manifest_osrs230_embed.ini --user testc --pass test
```

`manifest_osrs230*.ini` and `the osrs239-net profile (profiles/osrs239-net.ini)` all boot `dir=cache.osrs239.baked`. **`manifests/manifest_osrs239.ini` boots the pristine `cache.osrs239` and will NOT see any edit** (`BUILD_AND_RUN.md:212`). `src/makefile:1636-1652` states the rule: only *client-visible* edits (CS2, interfaces, sprites, configs) need a bake; pure `.rs2` server work travels via `torirsserver-scripts` + `torirsserver-servpack`.

`--base cache.osrs239` copies the pristine cache first, so untouched records keep their bytes; the output is ~440 MB and the container appends rather than compacts (re-packing the same `--out` grows it — the recipe `rm -rf`s first).

Prereq gate: `torirsserver-cache` depends on `check-crystal-set-contract` (`tools/check_crystal_set_contract.py`), which reads `scripts/script_73.cs2` and `script_7304.cs2` and will fail the bake if their emote-alias shape regresses.

## 2. The rev-239 skills tab

**Interface 320 = `stats`** (`pack/3_interfaces.pack:321`). Source: `interfaces/stats.if` (481 lines, 34 components) + `interfaces/stats.compack` (the block-name → file-id map; ids are data, holes survive — `cp_decode.c:1287-1295`).

**IT ALREADY HAS 24 SKILLS, NOT 23.** Slot 24 is Sailing (stat id 23), shipped but gated off.

### Layout algorithm — a static 3×8 grid, hand-authored in the interface, not computed

`[universe]` is 190×261 (`stats.if:5-12`). Every skill cell is a 62-wide component:

| col x | y values | cells |
|---|---|---|
| 1 | 1,31,61,91,121,151,181,211 | attack strength defence ranged prayer magic runecraft construction |
| 64 | same | hitpoints agility herblore thieving crafting fletching slayer hunter |
| 127 | same | mining smithing fishing cooking firemaking woodcutting farming sailing |

Rows 0-6 are `height=30`; row 7 (y=211) is `height=32`. `[total]` sits at `y=241, 190×19` (`stats.if:374-382`). `1 + 8*30 + 19 + 1 = 261`. **There is no free row and no free column — a 25th cell needs a geometry change to `[universe]`, `[total]`, and the sidebar container.**

Each cell's whole content is one onload:
```
onload = i:393, i:-2147483645, i:20971553, i:<slot>, i:<icon x-nudge>
```
- `393` = clientscript id · `-2147483645` (0x80000003) = "this component" placeholder
- `20971553` = `(320<<16)|33` = `stats:tooltip`
- `<slot>` = **an `enum_681` key, 1..24 — NOT a stat id.** attack=1, strength=2, ranged=3, magic=4, defence=5, hitpoints=6, prayer=7, agility=8, herblore=9, thieving=10, crafting=11, runecraft=12, mining=13, smithing=14, fishing=15, cooking=16, firemaking=17, woodcutting=18, fletching=19, slayer=20, farming=21, construction=22, hunter=23, **sailing=24**
- `<icon x-nudge>` = `cc_setposition(calc(3 + $int3), 4, …)` in 393; observed values -1,0,1,2,3

### The five clientscripts (all `.cs2` source, all compile, all roundtrip exact)

| id | file | name | role |
|---|---|---|---|
| 393 | `scripts/script_393.cs2` | `stats_init` | builds one cell: icon, level text ×2, ops, transmit hooks, lock overlay |
| 395 | `scripts/script_395.cs2` | `[proc,stats_setlevels]` | `stat()`, `stat_base()`, `stat_xp()`; builds the tooltip strings |
| 394 | `scripts/script_394.cs2` | `[clientscript,stats_setlevels]` | the transmit trampoline |
| 396 | `scripts/script_396.cs2` | `stats_skilltotal` | the "Total level:" row |
| 2366 | `scripts/script_2366.cs2` | `stats_init_tooltip` | `[universe]` onload |
| 1007/1008/1320 | `.cs2` | `stat_totallevel` / `stats_totalxp` / `stat_f2plevel` | **iterate `enum_681` from key 1 until `null`** — they auto-extend |

### Data sources — five enums in `configs/all.enum` (line anchors)

| enum | line | shape | what it holds |
|---|---|---|---|
| `enum_681` | 3288 | int → stat | slot 1..24 → stat 0..23. `default=-1`; the `!= null` loop terminator |
| `enum_108` | 326 | int → string | slot → "Attack"…"Sailing"; `defaultstr=this skill` |
| `enum_255` | 1027 | stat → graphic | **the skill icon.** stat 0→sprite 197 … stat 23→228 |
| `enum_5917` | 57474 | stat → graphic | the silhouette icon set, 7431..7454 |
| `enum_1497` | 12767 | stat → boolean | members-only. Set for stats 9,15,16,17,18,19,21,22,23 |
| `enum_256` | 1054 | int → int | level → xp threshold (used by 395 for "Next level at") |

XP comes from opcodes only — `stat($stat)`, `stat_base($stat)`, `stat_xp($stat)` in script 395. Tooltips are built as `|`-joined strings in 395 and pushed through `script992`/`stats_op` to `stats:tooltip` (320:33).

### The hide/lock gate — this is the feature-flag precedent

```
script_8950.cs2:  [proc,script8950](int $int0)(int)
                  switch_int ($int0) { case 23 : return(~script8951(1)); case default : return(0); }
script_8951.cs2:  case 1 : return(~script607);
script_607.cs2:   return(~int_to_bool(%varbit18166));
```
`~script8950(stat)` is consulted by 393 (draws the two 90%-transparent `miscgraphics,4`/`,6` lock overlays), by `stat_totallevel` (1007), `stats_totalxp` (1008) and `stat_f2plevel` (1320) (all *skip* a gated skill). **Sailing is a fully-shipped, varbit-gated 24th skill in this cache.** A Summoning flag is `case 24: return(<not-flag>)` in `script_8950.cs2` plus one varbit.

Also per-stat: `script_9348.cs2` maps stat → a "new unlock" varbit (`%varbit20161`..`20183`) consumed by `script_9337`/`9338`/`9339` for the pulsing icon.

## 3. Stat opcodes and their bounds

| layer | file:line | symbol | bound |
|---|---|---|---|
| decompiler table | `3rd/rscache/src/cs2/cs2_command.gen.h:575-576` | `stat_base` 3306, `stat_xp` 3307 | none |
| VM opcodes | `src/cs2vm2/cs2_opcode.h:1728-1729` (+ `CS2_OP_STAT` 3305) | `CS2_OPERAND_INT8`, `CS2_HANDLER_HOST` | none |
| VM dispatch | `src/cs2vm2/cs2vm2.c:10437-10452` | pops one int, forwards to host | none |
| host | **`src/game/rs_cs2_host.c:5329-5346`** | `if( host->stats && stat >= 0 && stat < RS_PLAYER_STATS_SKILL_COUNT )` | **25** |
| storage | `src/game/rs_player_stats.h:11` | `#define RS_PLAYER_STATS_SKILL_COUNT 25` | **25** |
| CS1 host | `src/game/rs_cs1_host.c:177` | same constant | 25 |
| wire in | `src/game/rs_gameproto_exec.c:562-563` | `UPDATE_STAT` guard, same constant | 25 |

**Answer: NOT bounded to 23 on the client — it is 25.** Stat ids 23 (sailing) and 24 (summoning) both fit today with zero C changes.

**The server IS bounded to 23**: `src/torirsserver/torirs_server.h:558` `TORIRSSERVER_STAT_COUNT = 23`, backing `stat_level[]`/`stat_boosted[]`/`stat_xp_tenths[]` (`:2462-2464`) and `stat_drain[]` (`:1711`); persistence clamps at `torirs_server_save.c:491`. Comment at `:544-547` says *"the array is the full 23 so a stat id from the wire is never out of range"* — which is now wrong for sailing.

`pack/stat.pack` is authored (`content.ini` `[namespace:stat] ids = protocol, names = authored`) and stops at `22=construction` — **ABSENT: sailing (23) is not named.**

Hook-list ceilings are fine: `RS_CS2_HOST_TRANSMIT_TRIGGER_MAX 32` (`rs_cs2_host.h:185`). Longest `if_setonstattransmit` list in the tree is 10 (script 2069's pest_rewards). Script 393's `if_setonvartransmit` for `stats_setlevels` lists 25 varps (`var427, var1588, var1435-var1457`) — a 26th fits.

## 4. The skill guide

**Interface 214 = `skill_guide` (legacy), interface 860 = `skill_guide_v2` (the live one)** — `pack/3_interfaces.pack:215`, `:861`. 860 draws none of itself; everything is built by clientscript **1902** → `1903` → `1904` (`~script1911`, `if_callonresize`). Fully documented in **`docs/skill_guide.md`** (a case file) and **`docs/skill_guide_server_reqs.md`**.

Server side already exists and is complete for 24 skills:
- `OSRS-Content/osrs239-content/server/scripts/interface_skill_guide/scripts/skill_guide.rs2` — 24 `if_setevents(stats:<skill>, 0, 0, ^if_event_op2)` + 24 `[if_button2,stats:<skill>]` handlers + `[proc,skill_guide_open]` (`if_opensub` then `runclientscript*`, order load-bearing)
- `.../configs/skill_guide.constant` — `^clientscript_skill_guide_init = 1902`, `^skill_guide_tab_default = 0`, and `^skill_guide_attack..^skill_guide_sailing = 1..24`

### Per-skill content is DBTABLE-sourced (cache tables, not server tables)

`configs/all.dbtable` (line anchors):
- **212 `skill_guide_subsections`** (line 1771): `0:skill,int · 1:id,int · 2:header,string · 3:membersonly,boolean` — **196 rows**
- **213 `skill_features`** (line 1779): `0:icon,obj(default 7620) · 1:sprite,graphic,int,int,int,int · 2:text,string · 3:skill,int,int,int · 4:quest,dbrow · 5:otherreq,string · 6:membersonly,boolean · 7-9:otherdata_{magic,sailing,construction},obj` — **3,447 rows**
- 209 `skill_guide_v2_inline_icon`, 79 `hiscores_skill_info` are adjacent

Script 1904 reads them as `db_find(868352, %varcint1172, 0)` = `(212<<12)|(0<<4)`; `db_find_filter(868368,…)` = column 1; `db_getfield(…, 868384/868400, 0)` = columns 2/3. **Both `.skill` columns are keyed by the `enum_681` slot, not the stat id** (`skill_guide.constant` says so explicitly).

### `docs/DBTABLES.md` and `EXCEPTIONS.md` H1 are STALE — dbrow/dbtable ARE packable

`3rd/rscache/EXCEPTIONS.md:1902` H1 says "unpack-only", and `OSRS-Content/README.md` repeats it. The big comment at `3rd/rscache/tools/cachepack/config/cp_db.c:303-325` also still says "Both packers refuse". **The code disagrees:** `cp_pack_dbrow` (`cp_db.c:536`) and `cp_pack_dbtable` (`:579`) are wired in `cp_types.c:97-104`, call the validated `RSCache_Dat2ConfigDbRowEncode` / `…DbTableEncode`, and `parse_columns` (`:382`) reads the modern `columndef=`/`values=` spelling with `cp_unescape` + `split_escaped` — the comma-in-string problem the old comment cites was fixed.

MEASURED THIS SESSION:
```
cachepack verify --cache cache.osrs239 --rev osrs239 --src OSRS-Content/osrs239-content --types dbrow,dbtable
  type      records    exact  same-len  differ  lost-here
  dbrow       16711    16711         0       0          0
  dbtable       246      246         0       0          0
```

### What adding a Summoning guide entry requires
1. New `[name]` blocks in `configs/all.dbrow` with `table=skill_guide_subsections` (≥2: Overview id 0 + one real section) and `table=skill_features` (one per unlock row), `columndef=`/`values=` per the schemas above, `skill` column = the new `enum_681` slot (25).
2. New id lines in `configs/all.dbrow.compack`. `content.ini` declares `[namespace:dbrow] ids = server` — new ids come from the fixed server base in `src/torirsserver/content_register.c`, recorded in `pack/dbrow.alloc`. **`DBTABLES.md` §3 warns `dbrow` declares `server_base = 0`, so `validate_id_bases` skips it entirely and allocation runs off the high-water mark** — a hazard if the cache is ever bumped.
3. **Hand-edit `dbindex/dbindex_212.dbi` and `dbindex_213.dbi`** — `[master]` and `[column_0]` `index=0:<skill>:<row>,…` lines. `db_find` reads cache table 21 (`rs_cs2_host.c:4121-4122`, `db_index_or_yield` at `:4236`), so a row not in the index is invisible. The `.dbi` header says *"Derived from the dbrows rather than authored"*, but **ABSENT: there is no regenerator** — nothing in `tools/` or `src/makefile` rebuilds it, and `dbindex_read` (`cp_decode.c:4759`) packs the text as written.
4. One `^skill_guide_summoning = 25` constant + `if_setevents` + `[if_button2,stats:summoning]` in `skill_guide.rs2`.
5. **`script_1904.cs2` currently does not compile from the tree** (stale source, `unknown command '_1703'`). If any layout change to 1904 is needed, re-unpack it first.

## 5. Skill icons / sprites

`enum_255` (stat → graphic) and `enum_5917` (stat → silhouette) name sprite **pack ids**, which `pack/8_sprites.pack` maps to directory names:

| ids | pack name | contents |
|---|---|---|
| 197-214 | `staticons_0..17` | the 18 original skill icons |
| 215-232 | `staticons2_0..17` | 215=`staticons2_0`(slayer) … 228=`staticons2_13`(sailing) |
| 7431-7454 | `stat_silhouette_0..23` | the silhouette set |

Layout: `sprites/<name>/0.bmp` + `sprites/<name>/pack.meta`:
```
sprites/staticons2_13/pack.meta
  count=1
  palette=5
  p0=0x000000 … p4=0x767777
  sprite0=25,25,23,23,2,1        # mem_w,mem_h,crop_w,crop_h,off_x,off_y
```
**FREE SLOTS ALREADY EXIST**: `staticons2_14..17` = sprite pack ids **229, 230, 231, 232**, each `count=1, palette=1, p0=0x000000, sprite0=25,25,0,0,0,0` and **no `0.bmp` at all** — 25×25 blanks the cache already reserves. A Summoning icon can drop into `sprites/staticons2_14/0.bmp` (id 229) with an updated `pack.meta` palette, or be given a brand-new id.

**Implemented correction:** the source art is rev-530 sprite pack **222**, the wolf head visible at
the right of the source stats row. It is exported byte-exactly with
`sprite0=25,25,22,23,0,2` and remapped to the marked target name `summoning_staticon` at target
id 229. The previous blue circular placeholder was not source art and has been removed.

Adding a *new* sprite id (safer for a "ported content" folder): append `8535=summoning_icon` to `pack/8_sprites.pack`, create `sprites/summoning_icon/{0.bmp,pack.meta}`. Import walks the pack, not the directory (`cp_assets.c:1383-1387`) — *"a file whose name it does not list has no id to be written to"*. Current ceiling: max sprite id **8534**. `sprite_write`/`sprite_read` note (`cp_decode.c:2475-2497`): the palette is written and read back, never re-derived — a colour not in `pack.meta`'s palette snaps to the nearest entry and says so, so **a new icon needs its palette written into `pack.meta`**.

## 6. Precedent for modifying a cache clientscript

**YES for the mechanism; NO for a semantic change.**

- `OSRS-Content/osrs239-content/scripts/script_73.cs2` and `script_7304.cs2` are hand-edited and committed (commits `9079d4d8c0` "Fix crystal command contract after content merge", `f562c8174d`) — but the diffs are **comment-only** (a 4-line and a 2-line explanatory comment about the `cry` / `::crystal_set` alias collision). No opcode changed. There is a CI-style guard on them: `tools/check_crystal_set_contract.py`, a hard prerequisite of `make -C src torirsserver-cache`.
- `src/makefile:1636-1652` is the design statement that the path is real: *"a `.cs2` edit packed into no cache is an edit that ships nowhere, and the Display panel's layout dropdown was the case that found it"* — i.e. `torirsserver-cache` exists **because** a CS2 edit was made and did not reach the client.
- `docs/CS2_REV239_ROUNDTRIP_QUEUE.md` §10.6 documents an explicit edit-safety test: a blank line inserted into lossless script 15 invalidated the snapshot fingerprint and the canonical 35-byte compile shipped instead of the original 38 bytes.
- **ABSENT: no authored/new clientscript** anywhere. Every `pack/12_clientscripts.pack` line is `<id>=script_<id>`; there are zero named script files and zero ids above the cache's. Max clientscript id 9726.
- **ABSENT: no `.cs2` in the repo outside the content tree** except `decompiled/` (3 scratch files) and `build/cachepack_verify/` (tool output).
- Precedent for *authored interface* edits does exist: `interfaces/chatmenu.if` and `interfaces/barrows_puzzle.if` were hand-modified (commits `20de8327a2` "Place revision 239 choice menus from content", `f8db65f0c2`).

---

## Concrete inventory: what "a 25th skill in the tab" touches

| # | file | edit |
|---|---|---|
| 1 | `configs/all.enum` :3288 `[enum_681]` | `val=25,24` |
| 2 | `configs/all.enum` :326 `[enum_108]` | `valstr=25,Summoning` |
| 3 | `configs/all.enum` :1027 `[enum_255]` | `val=24,<sprite id>` |
| 4 | `configs/all.enum` :57474 `[enum_5917]` | `val=24,<silhouette id>` |
| 5 | `configs/all.enum` :12767 `[enum_1497]` | `val=24,1` (members) |
| 6 | `interfaces/stats.if` | `[summoning_stats_cell] x=127,y=183`; Sailing `x=1,y=209`; Total `x=64,y=209,width=126`; keep `[universe]` 190×261 |
| 7 | `interfaces/stats.compack` | `34=summoning` |
| 8 | `scripts/script_8950.cs2` | `case 24 : return(<flag>)` — the feature gate |
| 9 | `scripts/script_9348.cs2` | `case 24 : return(%varbit…)` (unlock pulse; harmless if omitted, defaults 0) |
| 10 | `pack/stat.pack` | `23=sailing` (missing today) + `24=summoning` |
| 11 | `sprites/<name>/{0.bmp,pack.meta}` + `pack/8_sprites.pack` | the icon(s) |
| 12 | `server/scripts/interface_skill_guide/{configs/skill_guide.constant, scripts/skill_guide.rs2}` | `^skill_guide_summoning = 25`, `if_setevents`, `[if_button2,stats:summoning]` |
| 13 | `configs/all.dbrow` + `.compack` + `dbindex/dbindex_21{2,3}.dbi` | the guide content |
| 14 | `src/torirsserver/torirs_server.h:558` | `TORIRSSERVER_STAT_COUNT` 23 → 25 (the only C change required) |

Client C changes needed: **one line** (#14 is server-side; `RS_PLAYER_STATS_SKILL_COUNT` is already 25).

---

## RISKS / UNKNOWNS

1. **The tree's `.cs2` sources are stale — 95 of 9368 do not compile back today**, including `script_1904.cs2` (the skill-guide layout builder, `unknown command '_1703'`). Fresh decompiles round-trip exactly, so a `cachepack unpack --assets=scripts` refresh is the fix — but that refresh **will overwrite the hand-authored comments in `script_73.cs2` and `script_7304.cs2`** and trip `check_crystal_set_contract.py` if the new decompile spells the aliases differently. Re-apply those comments or unpack selectively.
2. **A failed CS2 compile at bake time is near-silent.** `cachepack` prints one stderr line and ships the base cache's bytes. Always run the standalone `cs2 compile` gate (step 2 above) and check `compiled N, failed 0`.
3. **The RuneStar names dir is a hard, undeclared dependency.** Without `CACHEPACK_CS2_NAMES`, `^iftype_graphic` and friends do not resolve and *every* edited script silently reverts to base bytes. `src/makefile:1675` hard-codes `$(HOME)/Documents/git_repos/cs2/…`; it exists on this machine and nowhere else guaranteed. `stat-names.tsv` there stops at 22 — harmless (`stat_24` is legal), but it means the tree's decompiled sources will spell the new skill `stat_24`, not `summoning`.
4. **`docs/DBTABLES.md`, `3rd/rscache/EXCEPTIONS.md` H1 and `OSRS-Content/README.md` all still claim dbrow/dbtable are unpack-only.** They are not (verified 16711/16711 + 246/246 exact this session). Planning around the stale claim would wrongly rule out the skill guide.
5. **No `dbindex` regenerator exists.** Adding a dbrow means hand-editing `dbindex/dbindex_<table>.dbi` `[master]` + `[column_N]` lines, in "binary order" per the file's own header. Get the order wrong and `db_find` silently misses the row. This is the single most fragile step.
6. **`pack/dbrow.alloc` has `server_base = 0`**, so `validate_id_bases` (`torirs_server_pack.c:553`) skips dbrow entirely and allocation runs off the high-water mark — the exact pattern `DBTABLES.md` §3 says a future cache bump will collide with.
7. **`enum_681`'s `!= null` loop terminator** is what makes total-level/total-XP auto-extend. If key 25 is added with `default=-1` intact, 1007/1008/1320 pick it up for free — but a **gap** in the key sequence (e.g. adding 26 without 25) truncates the loop and silently drops every later skill from Total Level.
8. **Layout is hand-authored, not computed.** 190×261 with 3×8 cells and zero slack. A 4th column (x=190) needs the sidebar container widened; a 9th row needs `[universe]` ≥291 and `[total]` moved. Neither is inside interface 320's control — the sidebar geometry comes from the gameframe (interface 161 / `toplevel_osrs_stretch`). **Unverified: whether the rev-239 sidebar will render a taller/wider 320 without clipping.** `docs/gameframe_layout_resize.md` is the place to look.
9. **Sailing (stat 23) is already wired end to end in the cache but ABSENT from `pack/stat.pack` and from `TORIRSSERVER_STAT_COUNT`.** Deciding whether Summoning takes id 23 (displacing sailing) or 24 (coexisting) is a fork with consequences for the wire, saves (`torirs_server_save.c`), and every `enum_681` key. GUESS: 24 is right — the cache's own `enum_681`/`enum_108`/`enum_255` already spend 23 on sailing, and reusing it would make the client draw a boat icon for Summoning.
10. **No content-level feature-flag mechanism exists.** `src/features/` is a *rendering-era* table (`ToriRS_FeatureTable`, keyed off cache lineage), not a content toggle. The only real precedent for gating a skill is the cache's own `script_8950` → `script_607` → `%varbit18166` chain. GUESS: reuse that shape with a new varbit; it is the one the client already honours in the tab, the totals, and the F2P total.
11. **`--binary` is passed to the bake** (`src/makefile:1691`), which moves non-config tables as raw container bytes. `OSRS-Content/README.md` describes `--binary` as an unreadable byte-exact mode; unverified whether `--assets --binary` together change which writer wins for `12_clientscripts`. Worth confirming an edited script actually lands before building on top of it — diff `cache.osrs239.baked` against `cache.osrs239` for archive 12/393.
12. `script_9176.cs2b` (the guide's Overview-tab builder) is bytecode-only — one of the 357 declined. If Summoning needs Overview-tab work, that script cannot be edited as source today.
13. Not investigated (other agent's scope): where "ported" content should physically live under `OSRS-Content`. Note `content.ini`'s `names = imported` value already exists for "a foreign revision's table — every line is a *claim*", and `port/` holds `*.map` diff ledgers — both plausible hooks.

===== RECON: rs-feature-flags =====
# Recon: 3draster feature-flag mechanism vs. a Summoning port

## 1. How a flag is declared / resolved

**The seam is `struct ToriRS_FeatureTable`** — `src/features/features.h:112-245`, defined in `src/features/features.c:19-104`.

- **Type**: every slot is a plain `int` (plus `enum ToriRS_FeatureEra era` and `char const* name`). No bitfields, no strings.
- **Rule (stated at `features.h:10-14`, restated at `features.c:10-13`)**: *"a zero field means classic"*. A new field MUST default to the 2004/Client-TS behaviour so that `{0}` is a working table and adding a field cannot move an existing era. This is load-bearing for the port: **a `summoning` field whose 0 value means "off" satisfies the rule; one whose 0 means "on" breaks it.**
- **Three static singletons**, one per era: `k_features_lostcity` (c:19), `k_features_osrs` (c:56), `k_features_server_routed` (c:85), exposed by `ToriRS_Features_LostCity/OSRS/ServerRouted()` (c:106-122) and `ToriRS_Features_ByName()` (c:124-136, accepts only `"lostcity"|"osrs"|"server_routed"`).

**Resolution order** — `src/app.c:3442-3501` (documented at `features.h:18-21`):

1. `cfg->features_era` ← `[features:boot] era=` (manifest) — `app.c:3446`
2. `getenv("TORIRS_FEATURES_ERA")` — `app.c:3448`
3. `ToriRS_Features_ForCache(cache_game, cache_epoch, cache_revision)` — `app.c:3455`, impl `features.c:187-194`. Keys off **lineage, not revision**: dat2 + oldschool → `osrs`, everything else → `lostcity`. `cache_revision` is explicitly `(void)`-discarded (`features.c:190`). Nothing derives `server_routed`.
4. `assert(app->features)` — `app.c:3457`. Never NULL after init.

**Per-item overrides on top of the era** — `app.c:3459-3491`. Because the getters hand back shared statics, `app->features_storage = *app->features; app->features = &app->features_storage;` (`app.c:3468-3469`), then two knobs are merged in: `ground_click_nearest` (manifest `features_ground_click_nearest_set`, env `TORIRS_GROUND_CLICK_NEAREST`) and `painter_draw_distance`. **This copy-then-override idiom is the template for any new per-item flag.**

**Manifest plumbing** (4 files must all change to add a manifest key):
- `src/bootmanifest/bootmanifest.h:36-56` (schema prose), `:211-218` (struct fields)
- `src/bootmanifest/bootmanifest.c:186` (`BM_SECTION_FEATURES`), `:214-215` (section dispatch on prefix `features:`), `:659-710` (key parsing + validation — a typo is reported *at load time next to the file*, `:664-671`), `:812` (defaults; note the `-1` = "not stated" idiom because `0` is a real model), `:978-988` (`BootManifest_ApplyToConfig` → `AppConfig`)
- `src/app.h:190-204` (`AppConfig.features_*` + `_set` companions), `:626-635` (`App.features` / `features_storage`)

**Call sites read it** as `app->features-><field>` (client) or `ToriRSServer_SceneFeatures()-><field>` (server). Examples: `app.c:7412`, `7513`, `7626`, `11421`, `4277`; `torirs_server_scene.c:1542`, `1679`, `1709`, `1757`. `rs_audio.c:67` and `app.c:3506-3508` *copy* the value out at init into their own struct rather than dereferencing per-frame (documented at `app.h:637`).

**ABSENT**: there is **no command-line option** for any feature flag. `src/main.c:2167-2365` lists every `--` option; none is `--features-*`. Manifest + env only.

**Tests**: `src/world/test/world_test_route.c:640-717` asserts `ByName`, `ForCache` and every per-era field value. `src/bootmanifest/test/bootmanifest_test.c:125,193-197,220,235-236` + fixture `src/bootmanifest/test/fixture_manifest.ini:67-70`. A new flag is expected to add rows to both.

---

## 2. Every field in the table

| field | `features.h` line | meaning | readers |
|---|---|---|---|
| `era` | 114 | `enum ToriRS_FeatureEra` (1 lostcity / 2 osrs / 3 server_routed) | logging |
| `name` | 115 | era name string | `app.c:3497`, `torirs_server_boot.c:117` |
| `pathing_mode` | 120 | 0 = client BFS (`tryMove`), 1 = server-authoritative | `app.c:7412, 7496, 7558` |
| `approach_model` | 122 | 0 = legacy placed-shape tests, 1 = rsmod RECT | `app.c:7627,7650,7688,7750`; `torirs_server_scene.c:1679,1710,1737` |
| `npc_approach_uses_size` | 130 | 0 = NPC is a 1×1 target, 1 = use NPC size rect | `app.c:7626`; `torirs_server_scene.c:1709` |
| `op_click_nearest_range` | 141 | radius of the "walk as close as possible" box for an **interaction** click (0 / 10) | `app.c:7513`; `torirs_server_scene.c:1757` |
| `nearest_ranks_by_rect_distance` | 148 | 0 = fewest-steps-first, 1 = squared distance to target rect | `app.c:7515`; `torirs_server_scene.c:1759` |
| `ground_click_nearest_model` | 164 | `enum ToriRS_NearestModel` for a **ground/minimap** click (ring3 / box10_rect / none) | `app.c:7425`; `torirs_server_boot.c:112` |
| `los_symmetric_pvp` | 171 | 1 = symmetric PvP LoS (2019 LMS). PvM stays asymmetric | `torirs_server_scene.c:1542` |
| `route_window_tiles` | 185 | BFS window width (0 = whole map, 128 = rsmod) | `app.c:4277`; `torirs_server_scene.c:732` |
| `target_mask_held` | 202 | which bit of a component target mask means "cast on held item" (0x10 classic / 0x20 OSRS) | `app.c:11421` → `rs_minimenu_build.c:276` |
| `painter_draw_distance` | 211 | painter radius, 0 = classic 25, else clamp 25..90 (`features.c:163-174`) | `ToriRS_Features_PainterDrawDistance` |
| `npc_light_uses_type_ambient_contrast` | 221 | 1 = apply NpcType opcodes 100/101 to body lighting | `app.c:3506` |
| `player_head_light_ambient` | 229 | absolute ambient for chathead lighting (0 = scene regime, 128 = xrsps) | `app.c:3508` |
| `effects_monophonic` | 244 | 1 = refuse a sound while a longer one plays (2004 device) | `rs_audio.c:67,466` |

`target_mask_held` is **uncommitted work-in-progress** (`git diff src/features/`, +17/+7). It is the freshest worked example of adding a field: struct slot + comment, three era tables, one call site, tests.

---

## 3. Are any flags CONTENT flags? **No.**

**ABSENT: not one of the 14 fields gates content.** Every field selects between two *behaviours the engine already implements* for data that exists in every era. The header says so explicitly (`features.h:5-6`): *"Per-era client feature table — THE modularity seam for **client behaviour**"*. There is no precedent for "this record type exists / does not exist" living here.

**ABSENT: `grep -rn "feature flag\|feature-flag" docs/*.md` → no hits.** The project has no content-flag concept at all today.

How a flag would reach each consumer:

**(a) Engine C code** — works today. `app->features-><field>`, resolved as §1. Manifest `[features:boot]` + `TORIRS_*` env.

**(b) ToriRSServer server** — **the manifest does not reach it.** `torirs_server_boot.c:98` builds the server's *own* copy: `features = *ToriRS_Features_ForCache(RSCACHE_GAME_OLDSCHOOL, RSCACHE_EPOCH_DAT2, TORIRSSERVER_CACHE_REVISION)`, then applies exactly one env override (`TORIRSSERVER_GROUND_CLICK_NEAREST`, `:104-113`) and calls `ToriRSServer_SceneSetFeatures(&features)` (`:114`). Even under `transport=embed` the bridge passes **only `rev_name`** (`src/platform/net_transport_embed.c:50-54, 98`). No `setenv` anywhere in `src/` propagates client config to the server (`grep setenv src/` → only test harnesses). **So a server-side flag needs its own channel: a `TORIRSSERVER_*` env var, or content-tree data.**

**(c) ServerScript `.rs2`** — **no build-time exclusion exists.** `SSC_CompileDir` → `collect_sources` (`src/serverscript/ssc_compile.c:2867-2911`) walks `--src` recursively and compiles **everything**; `ssc_main.c` (`:1-17`) exposes `--src / --out / --pack / --constants` and nothing else. Available in-language gates: a `^constant` in `server/scripts/**/*.constant` (compile-time value, runtime test — flip = `make -C src torirsserver-scripts`), or a `%varp` (runtime, per player; grammar keys `transmit`/`scope`/`clientcode` at `torirs_server_content.c:2136-2143`).
  ⚠️ The one *mechanical* exclusion that exists — a leading `.` on a directory, skipped identically by `ssc_compile.c:2883`, `torirs_server_content.c:2881`, and `cp_walk.c:58` — is **explicitly forbidden**: `docs/PORTING_GUIDE.md:1205-1213` "Do not ever skip / silence / park another lane… Forbidden: `*.rs2.skip`, `dirname.skip`, moving trees aside", enforced by `.cursor/rules/no-park-sibling-content.mdc`. Do not build the Summoning gate on it.

**(d) CS2 clientscripts** — CS2 lives *inside the cache* (`OSRS-Content/osrs239-content/scripts/*.cs2`, 9,725 files, packed to idx 12). It cannot read a C-side flag at all. Its only inputs are varps/varbits (server-transmitted) and whether the interface/component exists in the loaded cache. **A CS2 gate is necessarily a varbit or a varp.**

**(e) cachepack** — a real, already-working content gate, in two layers:
  - **Per-type**: `fields/<type>.ini` `records = client|server` → `CP_Fields.records_client` (`cp_fields.c:297`, default **0 via `memset` at `cp_fields.c:222`**). `record_is_client()` (`cp_pack.c:524-530`) = `!(origin_rank > 0 && !records_client)`. **A new record authored under `server/scripts/` therefore does NOT reach the client cache unless the type opts in.** Only `dbrow/dbtable/enum/param` state a value today (all `= server`); `obj`, `npc`, `loc` state none → default server → **new Summoning pouches/scrolls would be silently server-only**.
  - **Per-entity**: `pack/<ns>.client` / `pack/<ns>.server`, parsed by `3rd/rscache/tools/cachepack/cp_membership.c`, routed by `cp_pack.c:690-760`. Exists for exactly 5 namespaces today: `enum, loc, npc, param, varp` (`ls OSRS-Content/osrs239-content/pack/`). **ABSENT for `obj`, `seq`, `spotanim`, `struct`, `inv`, `varbit`, models, sprites, interfaces.**
  - cachepack CLI (`3rd/rscache/tools/cachepack/main.c:16-90`) has `--types a,b` (config types) but **no subtree/lane exclusion**. And `cachepack does not look inside server/scripts at all` (`docs/PORTING_GUIDE.md` §3.2 item 5).

---

## 4. Boot manifest format + a real `[features:boot]`

- Schema prose: `src/bootmanifest/bootmanifest.h:6-126`. House style `[type:name]`, lowercase `key=value`, `;`/`#` comments. Relative paths resolve against the manifest's directory.
- Manifests live at the **repo root**, not `saves/`: `manifest_osrs230*.ini`, `manifest_osrs239*.ini`, `manifests/manifest_rs254.ini`, `manifests/manifest_rs377.ini`, `manifests/manifest_void634.ini`, `manifests/manifest_xrsps.ini`.
- **`saves/*.ini` are NOT manifests** — they are ToriRSServer player saves written by `torirs_server_save.c` (`saves/asdf.ini:1-3`: `; ToriRSServer player save. Written by torirs_server_save.c`).
- **`src/.last_flavor` is NOT config** — it holds `build_es`, the objdir stamp used by `src/makefile:513-523` to force a relink when debug/release swap. Unrelated to features.

Real section, `manifests/manifest_osrs239.ini:130-132` (with ~25 lines of preceding rationale comment):

```ini
[features:boot]
era=osrs
ground_click_nearest=box10_rect
```

Other live examples: `manifests/manifest_xrsps.ini:33-37` (`era=server_routed`, with the reason: xrsps paths server-side over a rev-233 cache so the era cannot be derived); `manifests/manifest_osrs230_zuk.ini:24-25` (`painter_draw_distance=90` only); test fixture `src/bootmanifest/test/fixture_manifest.ini:67-70` (all three keys).

Cache selection is a *separate* section and is the de-facto biggest content switch: `[cache:boot] dir=` — `manifests/manifest_osrs239.ini:16` `dir=cache.osrs239` (pristine) vs `manifest_osrs230*.ini` `dir=cache.osrs239.baked` vs `manifests/manifest_osrs239_packed.ini:20` `dir=cache.osrs239_packed`.

---

## 5. Server runtime toggles for content subsets

**ABSENT — nothing enables/disables a content subset.** `grep -rn "_enabled\|_disable" src/torirsserver/*.h` → zero hits. The complete env surface of ToriRSServer (`grep getenv src/torirsserver/`):

`TORIRSSERVER_CACHE`, `TORIRSSERVER_SCRIPTS`, `TORIRSSERVER_CONTENT`, `TORIRSSERVER_HOME`, `TORIRSSERVER_SAVES`, `TORIRSSERVER_REV`, `TORIRSSERVER_JS5_CACHE`, `TORIRSSERVER_JS5_REV`, `TORIRSSERVER_GROUND_CLICK_NEAREST`, `TORIRSSERVER_STAFF_LEVEL`, `TORIRSSERVER_SONG`, `TORIRSSERVER_AMBIENT`, `TORIRSSERVER_GEARRUN`, `TORIRSSERVER_VERBOSE`, `TORIRSSERVER_TRACE_IN/OUT/JS5`, `TORIRSSERVER_EXT_DEBUG`, `TORIRSSERVER_PROJ_DEBUG`, `TORIRSSERVER_NPC_INFO_DEBUG`, `TORIRSSERVER_MOVE_TRACE`, `TORIRSSERVER_ECHO_MES`, `TORIRS_COLLISION_DOORS_FULL`.

All are paths, debug tracing, or the one pathing knob. The closest thing to a content gate is `TORIRSSERVER_CONTENT` / `TORIRSSERVER_SCRIPTS` — **whole-tree** redirection, not a subset.

Two adjacent mechanisms worth knowing:
- `enum ToriRSServerFallback` (`torirs_server.h:3786-3833`): the engine's built-in C answers for `OPNPC`/`INV_BUTTON`/`IF_BUTTON` are switched **off** when a script pack loads. Content presence, not a flag.
- `content.ini` (`OSRS-Content/osrs239-content/content.ini`) is read by `ContentRegister_Load` (`src/content/content_register.c:458`), which **ignores every section that is not `[namespace:*]`** ("Anything else in the file is not ours"). So a `[feature:summoning] enabled = 1` section could be added there today without breaking any existing reader — that is the cleanest place to put a *content-tree-owned* gate.

---

## 6. Recommendation — how many flags, and where

**Opinion: three gates, one authored source of truth, and probably NO new `ToriRS_FeatureTable` field.**

### 6a. Do NOT put "summoning" in `features.h` (unless you find real engine divergence)

The table is documented as *client behaviour that changed between game generations* (`features.h:5-26`), resolved from **cache lineage** (`features.c:191`). Summoning is not an era divergence — it is a content lane, and it is one the cache lineage cannot imply. Adding `int summoning_enabled` would be the first field with no era semantics and would make `ToriRS_Features_ForCache` a liar.

More importantly, the engine probably doesn't need it: a familiar is an NPC, a pouch is an obj, a scroll is an obj, the tab is CS2, the orb is a widget. The one place C is definitely wrong today:

- `src/torirsserver/torirs_server.h:550` — `TORIRSSERVER_STAT_COUNT = 23`. Summoning is stat 23 in the rev-530 lineage → the array must become 24. `src/game/rs_player_stats.h:11` already has `RS_PLAYER_STATS_SKILL_COUNT 25`, so the **client already has room** and needs no change. Bump the server constant *unconditionally* — an extra zeroed stat slot is inert and flag-gating it would only create two ABI shapes for `torirs_server_save.c`.

If, during implementation, you do find engine divergence (e.g. a follower-slot render rule), *then* add a field and follow `target_mask_held` (`git diff src/features/`) exactly: slot + comment, three era tables, one call site, `world_test_route.c` rows.

### 6b. Gate 1 — the bake (whether the content physically exists client-side)

This is the strongest gate and it already defaults to OFF. Evidence: `cp_fields.c:222` zeroes `records_client`, and `cp_pack.c:524-530` refuses any rank-1-only record for a type that has not opted in. So:

- Put ported records under a distinct rank-1 root, e.g. `OSRS-Content/osrs239-content/server/scripts/ported_2009scape/summoning/**` (the walkers are recursive — `torirs_server_content.c:2868`, `ssc_compile.c:2867` — so no loader change is needed).
- Client-visible records (pouch objs, familiar npcs, obelisk locs, the tab interface) must be opted in **per entity** via `pack/obj.client`, `pack/npc.client`, `pack/loc.client`. `npc.client` and `loc.client` already exist; **`pack/obj.client` does NOT and must be created** (`cp_membership.h` "creates only files that do not exist"; `cachepack membership` seeds it). Prefer this over flipping `fields/obj.ini` `records = client`, which is one boolean for the whole 30k-obj namespace.
- The bake target is a **different cache directory**: `make -C src torirsserver-cache TORIRSSERVER_CACHE_DIR=$PWD/cache.osrs239.summoning` (`src/makefile:1677-1690`) and a `manifests/manifest_osrs239_summoning.ini` whose only real diff is `[cache:boot] dir=`. **Booting `manifests/manifest_osrs239.ini` (pristine `cache.osrs239`) is the flag-off client, for free.**

### 6c. Gate 2 — the server runtime gate

Build-time exclusion is unavailable (§3c) and dot-parking is forbidden (`PORTING_GUIDE.md:1205`). So: **one server-allocated varp, `%feature_summoning`**, declared in `server/scripts/ported_2009scape/summoning/configs/*.varp`, and checked at the top of every Summoning entry point (`[opheld1,summoning_pouch]`, `[oploc1,summoning_obelisk]`, `[if_button,…]`). `pack/varp.alloc` + `tools/ss_allocate.py` already handle server varp allocation (`content.ini` `[namespace:varp] ids = server`).

Two variants, pick one:
- `transmit = no` — pure server gate, invisible to CS2. Safest.
- `transmit = yes` + `clientcode` — the same varp becomes the CS2 gate (§6d), one flag instead of two. **This is what I'd do**, because two flags that can disagree is exactly the failure mode `features.h:160-163` warns about ("Both halves of this tree read the same field… They must not disagree").

Optional convenience: honour `TORIRSSERVER_FEATURE_SUMMONING` in `torirs_server_boot.c` to seed the varp default, mirroring the `TORIRSSERVER_GROUND_CLICK_NEAREST` pattern at `torirs_server_boot.c:104-113`.

### 6d. Gate 3 — the CS2 / UI gate

CS2 can only see a varp/varbit or the absence of an interface. Reuse the §6c varp (transmit=yes). Tab visibility at rev 230 is "purely `if_hassub`" per `docs/REV230_UI_OWNERSHIP.md` — so with the varp at 0 the server simply never `IF_OPENSUB`s the summoning tab, and no CS2 edit is strictly required for the off case.

### 6e. Failure modes across the flag boundary

| scenario | what actually happens | severity |
|---|---|---|
| flag-off client receives `UPDATE_STAT` stat=23 | **Harmless.** `RS_PlayerStats_SetXp` bounds-checks `skill >= RS_PLAYER_STATS_SKILL_COUNT(25)` (`rs_player_stats.c:103`); 23 passes, the xp is stored, and nothing displays it because no interface reads it. Silent no-op. | none |
| flag-off client (pristine `cache.osrs239`) receives `IF_OPENSUB` for a summoning interface id the cache has no group for | **This is the dangerous one.** Client asks the cache for a group that doesn't exist. GUESS: an interface load that never completes, i.e. a stuck open-sub, per the `docs/CS2_*`/"never yield un-loadable ids" pattern. Not verified — no explicit "missing interface" handler found (`grep "no such interface"` → ABSENT). | **high** |
| flag-off client receives a familiar NPC id absent from its cache | NpcType lookup falls back to the nameless placeholder (`torirs_server_content.c` band glue comment `seed_sees_cache`). Renders as a blank/blocking entity rather than crashing. GUESS. | medium |
| flag-off client receives a pouch obj id absent from its cache | Inventory slot with no model/name. GUESS. | medium |
| flag-**on** client, flag-off server | Benign: the tab is never opened, the varp reads 0, no scripts fire. The extra records sit unused in the cache. | none |

**Conclusion: the asymmetry is not symmetric.** A newer client against an older server is safe; an older client against a summoning-enabled server is not. Therefore the server gate must be **per-connection-safe** — if you ever run one server for both cache flavours, gate the outbound `IF_OPENSUB` / npc-spawn on something the *server knows about the client*, not on a global. Today the only such signal is `rev_name` (`ToriRSServer_EmbedStart`) and `[cache:boot] revision`, neither of which distinguishes baked-from-pristine. **GUESS/RECOMMENDATION: keep it simple — one embedded server per manifest, and make the summoning manifest the only one whose server varp defaults to 1.**

### 6f. Policy conflict you must resolve first

`docs/PORTING_GUIDE.md:683` and `docs/SCAPE2009_CONTENT_PORT_QUEUE.md:65` **explicitly list Summoning on the skip list** ("Summoning is not in OSRS", "Evil Turnip / summoning-linked patches — Summoning ecosystem"), and `docs/SKILLS_CONTENT_PORT_QUEUE.md:101` marks it "not in OSRS". This port reverses standing project policy; those three lines must be edited in the same change or a future agent will re-delete the lane citing them.

---

## RISKS / UNKNOWNS

1. **Missing-interface behaviour is unverified.** No explicit handler found for `IF_OPENSUB` targeting a group absent from the cache. If it hangs rather than logs-and-drops, the "flag-off client, flag-on server" case is a hard failure. **Verify before designing around it.**
2. **`pack/obj.client` does not exist.** Summoning pouches/scrolls/charms are objs, and `obj` has neither a membership file nor `records = client` in `fields/obj.ini`. Without one of the two, every authored pouch is server-only and the client shows an empty slot. This is the single most likely silent failure.
3. **`TORIRSSERVER_STAT_COUNT = 23` → 24** touches `torirs_server.h:1703, 2454-2456` (three arrays) and `torirs_server_save.c`'s `[stats]` section. Saves written by a 24-stat server and read by a 23-stat one: unverified. The save header claims forward-compat ("a key this server does not know is skipped") — **check that the stat loop actually bounds-checks the id**.
4. **No manifest→server channel.** `[features:boot]` cannot reach ToriRSServer even under `transport=embed` (`net_transport_embed.c:98` passes only `rev_name`). Any design assuming one flag file drives both halves is wrong today; it needs either a new env var or a content-tree-read gate.
5. **`ToriRS_Features_ForCache` ignores revision** (`features.c:190`). A "summoning cache" cannot be auto-detected by lineage — the era system genuinely cannot express this, which is the strongest argument for *not* putting the flag there.
6. **Script id churn.** `sscompile` assigns ids in sorted path order (`ssc_compile.c:2925-2927`); adding `server/scripts/ported_2009scape/` shifts every id after it. Harmless *within* one build (nothing outside `script.dat` references server script ids) — but any stored/logged id is invalidated. Unverified whether anything persists one.
7. **rev-530 → rev-239 id space.** `docs/PORTING_GUIDE.md:35` — "Never copy rev-530 ids". Every 2009scape pouch/familiar/scroll/interface id is meaningless here; all 107 summoning files' numeric constants must be re-derived or newly allocated. Volume: `2009scape/Server/src/main/content/global/skill/summoning/` = 107 files, ~50 familiar NPC classes.
8. **ABSENT: models/sprites/interfaces have no membership gate.** `pack/7_models.pack`, `pack/8_sprites.pack`, `pack/3_interfaces.pack` are `ids = cache, names = cache`; there is no `.client`/`.server` split for asset namespaces. Transcoded rev-530 models can only be gated by *not putting them in the tree*, i.e. by which cache you bake — reinforcing §6b as the primary gate.
9. **The dot-prefix skip is a trap.** All three walkers honour it (`ssc_compile.c:2883`, `torirs_server_content.c:2881`, `cp_walk.c:58`), so it *works* — and `PORTING_GUIDE.md:1205-1213` forbids it by name with a Cursor rule attached. Expect an agent to reach for it; it must be called out in the plan.
10. **GUESS: `content.ini` non-`[namespace:*]` sections.** `content_register.c:458` ignores them today, but `ToriRSServer_Pack`'s validator and cachepack's `cp_register.c` also read the file — unverified whether either is stricter about unknown sections.

===== RECON: rs-torirsserver-server =====
# Recon: ToriRSServer server + ServerScript VM — where Summoning server logic would live

## 0. Orientation

| Thing | Path |
|---|---|
| Server ("ToriRSServer" is a misnomer — it *is* the server) | `src/torirsserver/` (88 files, `torirs_server_world.c` alone is 1.15 MB) |
| ServerScript compiler + VM | `src/serverscript/` |
| Content tree (submodule) | `OSRS-Content/osrs239-content/` |
| Server content half | `OSRS-Content/osrs239-content/server/{pack,scripts}/` |
| Port operating guide | `docs/PORTING_GUIDE.md` |

**Summoning in the target tree: ABSENT everywhere.** No `summon*` npc/obj/inv/interface record in `OSRS-Content/osrs239-content/configs/all.*.compack` beyond unrelated names (`synth_summon` dbrow 4546, `summon_spotanim` spotanim 480, `regicide_quest_kings_summons` obj 3206). No stat, no scripts, no varbits.

**Summoning is on two written SKIP lists** — reversing an existing recorded decision, not filling a gap:
- `docs/SCAPE2009_CONTENT_PORT_QUEUE.md:71` — `| content/global/skill/summoning/**, Wolf Whistle | Summoning is not in OSRS |`, plus `| Evil Turnip / summoning-linked patches | Summoning ecosystem |`
- `docs/SKILLS_CONTENT_PORT_QUEUE.md:101` — `| Summoning / Fist of Guthix / RS2-only | not in OSRS |`
- `docs/PORTING_GUIDE.md:35,683` — "skip bots/holiday/Summoning/RS2-only"

Both queue docs must be edited or the loop agents will keep skipping the slice.

---

## 1. Architecture: boot, seam, tick, player state

### Boot order (`src/torirsserver/torirs_server_boot.c`)
`ToriRSServer_BootLoad()` is a *function* precisely because the order fails silently when reversed (header comment `torirs_server_boot.h:1-21`):

1. `ToriRSServer_WorldSetCacheDir` + `ToriRS_Features_ForCache(RSCACHE_GAME_OLDSCHOOL, RSCACHE_EPOCH_DAT2, 239)` → `ToriRSServer_SceneSetFeatures`
2. Cache tables: `ToriRSServer_ObjInfoLoad` / `npcinfo` / `seqinfo` / `locinfo` / `structinfo` / `varbit_load`
3. `ToriRSServer_ContentLoad(content_dir)` — the text overlay tree (`server/scripts/**/configs/*.{param,constant,enum,varp,npc,obj,loc,spawn}`, `torirs_server_content.c:3228-3251`)
4. `ToriRSServer_ContentLoadServerBand` — reads `server/pack/` dat2, verifies identical to the text parse, then applies (gitignored; built by `make -C src torirsserver-servpack`)
5. `ToriRSServer_DbLoad` (authored `.dbtable`/`.dbrow`) then `ToriRSServer_DbLoadCache`
6. `ToriRSServer_IdsResolve()` — resolves the ~70 engine-addressed names (`torirs_server_ids.c:33-118`: interfaces, invs, components, varbits) out of the packs
7. `ToriRSServer_BankLoad(cache_dir)` — inv sizes from cache config group 5 + varbit bit ranges
8. `ToriRSServer_WorldSetHome` (default 3222,3218 Lumbridge)

Content dir resolution: `TORIRSSERVER_CONTENT` env → `OSRS-Content/osrs239-content` → `../OSRS-Content/osrs239-content`. Scripts: `TORIRSSERVER_SCRIPTS` → `<content>/server/scripts/build`.

### Transport / session / embed seam
- `torirs_server_transport.h` — 4-fn vtable (`recv/send/pollfd/close`). Two impls: socket (`ToriRSServer_TransportSocket`, wraps `ToriRSServerConn`, which itself sniffs raw TCP vs RFC-6455 WebSocket) and memory (`ToriRSServer_TransportMemory` over a `ToriRSServerPipe` FIFO pair).
- `torirs_server_session.c` — login handshake as a state machine INIT→LOGIN→ONLINE. Non-blocking is load-bearing: in-process, client + server share one thread.
- `torirs_server_embed.h` — `ToriRSServer_EmbedStart/connect/write/read/pump/world/player`; used by `src/platform/net_transport_embed.c` (`make -C src torirs EMBED_SERVER=1`).
- `torirs_server_main.c` — socket accept loop + 600 ms tick (`TORIRSSERVER_TICK_MS 600`, line 60; `ToriRSServer_WorldTick(srv)` at line 181, schedule-anchored).

### Tick loop — `ToriRSServer_WorldTick` (`torirs_server_world.c:9029-9067`)
Eleven phases in LostCity `World.cycle()` order (`torirs_server_world.c:7820-8890`):

| # | fn | line | does |
|---|---|---|---|
| 1 | `phase_world` | 7837 | `ToriRSServer_ScriptsResumeWorld`, `ToriRSServer_CombatRespawnTick` |
| 2 | `phase_clients_in` | 7852 | **EMPTY** |
| 3 | `phase_npc_events` | 7878 | `[ai_spawn]` for npcs with `spawn_pending` |
| 4 | `phase_npcs` | 7893 | per-npc: `resume_npc`, `combat_npc_tick`, then `advance_npcs` (2951) → delay/timer/queue/hunt/`npc_run_mode`/roam |
| 5 | `phase_player`/`phase_players` | 7967/8032 | `resume_player` → `process_queues` → `process_timers` → `process_engine_queue` → face → interaction try → `combat_player_approach` → walktrigger → `advance_player` → interaction try → reorient → `run_energy_tick` → `combat_player_tick` |
| 6 | `phase_logouts` | 8056 | **EMPTY** (`[logout]` lives in `ToriRSServer_WorldRemovePlayer`) |
| 7 | `phase_logins` | 8063 | `[login]` trigger |
| 8 | `phase_zones` | 8422 | |
| 9 | (worldmap) | 9056 | |
| 10 | `phase_info` | 8439 | PLAYER_INFO / NPC_INFO |
| 10b | `phase_clients_out` | 8769 | |
| 11 | `phase_cleanup` | 8820 | clears `masks`, `tele`, `step_dir` |

### Player state — `struct ToriRSServerPlayer` (`torirs_server.h:1908-2432`)
Pool of `TORIRSSERVER_PLAYER_MAX = 8` in `struct ToriRSServer.players[]` (`torirs_server.h:2527`); logout leaves a hole, never compacts. Relevant fields:

- `inv[28]`, `worn[14]`, `containers[TORIRSSERVER_CONTAINER_MAX=16]`, `bank`
- `varps[TORIRSSERVER_VARP_COUNT]` (`= TORIRSSERVER_VARP_CACHE_MAX + TORIRSSERVER_VARP_SERVER_HEADROOM`, `torirs_server.h:326`)
- `stat_level[23]`, `stat_boosted[23]`, `stat_xp_tenths[23]`, `stat_dirty` (uint32 bitmask)
- `queue[16]`, `engine_queue[]`, `timers[TORIRSSERVER_TIMER_MAX=8]`
- `active_script` (`SSVM_State*`), `delayed_until`
- `combat_target` (npc slot), `attack_clock`
- **No familiar/pet/follower field. ABSENT.**

`srv->active_player` (`torirs_server.h:2543`) is the ambient "whose turn is it" pointer, set by `ToriRSServer_WorldSetActive` (`torirs_server_world.c:6960`).

Persistence: `torirs_server_save.c` — one ini per player under `saves/` (`TORIRSSERVER_SAVES` overrides). Sections `[player] [stats] [inv] [worn] [bank] [bank_var] [container.<inv>] [container_var.<inv>] [varps]`. **Only varps whose `.varp` config says `scope=perm` persist** (`torirs_server_save.c:260`). Unknown keys are skipped, so adding fields is backwards-compatible.

---

## 2. ServerScript

### Triggers — `src/serverscript/ss_trigger.h` (generated, 182 ids)
Full LostCity `ServerTriggerType.ts` enum: `PROC 0`, `LABEL 1`, `APNPC1..OPNPCT 3..16`, `AI_APNPC/AI_OPNPC 17..28`, obj `31..56`, loc `59..84`, player `87..112`, `QUEUE 116`, `AI_QUEUE1..20 117..136`, `SOFTTIMER 137`, `TIMER 138`, `AI_TIMER 139`, `OPHELD1..OPHELDT 140..146`, `IF_BUTTON 147`, `IF_CLOSE 148`, `INV_BUTTON1..D 149..154`, `WALKTRIGGER 155`, `AI_WALKTRIGGER 156`, `LOGIN 157`, `LOGOUT 158`, `TUTORIAL 159`, `ADVANCESTAT 160`, `MAPZONE/MAPZONEEXIT/ZONE/ZONEEXIT 161..164`, `CHANGESTAT 165`, `AI_SPAWN 166`, `AI_DESPAWN 167`.

Plus rev-230-only **EXTRA_TRIGGERS** allocated above 167 (`gen_opcode_meta.py:469-511`): `IF_BUTTON1..10 = 168..177`, `IF_OPEN 178`, `FRIENDLOGIN 179`, `FRIENDLOGOUT 180`, `PLAYERDEATH 181`.

**Which the engine actually dispatches** (grep of `SS_TRIGGER_*` in `src/torirsserver/*.c`): OPNPC1/2/5/T, APNPC1/U/T, OPLOC1/2/3/T, APLOC1/U/T, OPOBJ1/3/T, APOBJ1/U/T, OPHELD1..5/U/T, OPPLAYER1/T, APPLAYER1/U/T, IF_BUTTON, IF_BUTTON1, IF_CLOSE, IF_OPEN, INV_BUTTON1/D, ZONE/ZONEEXIT/MAPZONE/MAPZONEEXIT, AI_SPAWN, AI_DESPAWN, AI_TIMER, AI_QUEUE1/3/20, AI_OPNPC5, AI_OPLOC5, AI_OPOBJ5, AI_OPPLAYER1/2/5, AI_APNPC1, AI_APPLAYER1, AI_WALKTRIGGER, LOGIN, LOGOUT, PLAYERDEATH, ADVANCESTAT, FRIENDLOGIN/LOGOUT.
**Not dispatched: `CHANGESTAT` (165), `TUTORIAL` (159), `AI_DESPAWN` is only referenced, `phase_npc_events` explicitly declines to fire it** (`torirs_server_world.c:7873`).
`TIMER`/`SOFTTIMER`/`QUEUE` are dispatched by *script id*, not by trigger id — `settimer(<script>, n)` names the script directly.

### Where `.rs2` lives and how it compiles
- Sources: `OSRS-Content/osrs239-content/server/scripts/**` — **1,464 `.rs2` files** across ~70 subject folders (`skill_*`, `interface_*`, `quests/`, `npc/`, `areas/`, `minigames/`, `shop/`, `bosses/`, `player/`, `levelup/`, `drop_tables/`). Configs colocated: 972 `.spawn`, 267 `.constant`, 234 `.varp`, 58 `.dbrow`, 46 `.dbtable`, 33 `.npc`, 23 `.param`, 21 `.enum`, 20 `.loc`, 16 `.obj`, 1 `.struct`.
- Compile: `make -C src torirsserver-scripts` (`src/makefile:1616-1622`) → `tools/ss_allocate.py --tree <content>` (appends ids for new `.enum`/`.dbtable`/`.param` blocks to `pack/*.alloc`) → `sscompile --src server/scripts --out server/scripts/build --pack pack --pack configs`.
- Output: `server/scripts/build/{script.dat,script.idx}` — **gitignored** (`OSRS-Content/.gitignore`).
- Discovery is recursive over `*.rs2` only (`ssc_compile.c:2896`). **Compile order is sorted path order** — a gosub compiles to a script *id*, so an unstable order silently repoints every call.
- Compiler is two-pass over the file set (names, then code); `SSC_MAX_SCRIPTS 16384`.
- Sibling target `make -C src torirsserver-servpack` writes `server/pack/` (also gitignored).
- `make -C src test-content` is the aggregate.

### Opcodes
`src/serverscript/ss_opcode.h` declares **426**; `torirs_server_opcode_coverage.gen.h` says **343 implemented** (`make -C src test-torirsserver-coverage` fails when stale; regenerate with `python3 torirsserver/gen_opcode_coverage.py`).

Extending the engine vocabulary is an established path: **EXTRA_OPCODES at 11000+** (`gen_opcode_meta.py:135-`, one band past LostCity's highest 10003) — currently `IF_SETEVENTS 11000` … `OC_UNPLACEHOLDER 11021`, incl. the six map-instance ops 11009-11014 and `NPC_FREEZE 11018` / `NPC_FROZEN 11019`. New summoning opcodes belong there, per `PORTING_GUIDE §2.4/§2.5` ("plan + implement in the same slice").

---

## 3. NPC spawning at runtime / can a familiar follow?

### npc_add / npc_del
- `SS_OP_NPC_ADD 2500` — `torirs_server_scripts.c:4611`. `npc_add(coord, npctype, duration)` → `ToriRSServer_WorldNpcSpawn` (`torirs_server_world.c:2454` → static `npc_spawn` at 2302). Sets `despawn_tick = tick + duration` (0 = permanent), leaves the new npc as `ACTIVE_NPC` so the script can act on it. Soft-fails (no abort) when the pool is full.
- `SS_OP_NPC_DEL 2510` — `torirs_server_scripts.c:4646`, just clears `npc->active`.
- `SS_OP_NPC_UID 2544` / `NPC_FINDUID 2521` — `torirs_server_scripts.c:4572` / `4585`. **A uid IS the slot index; there is no generation counter.** The code says so explicitly: "a uid that outlives its npc resolves to whatever took the slot."
- Pool: `TORIRSSERVER_NPC_MAX = 4096` (`torirs_server.h:204`); spawn scans linearly for a free slot.
- Also implemented: `NPC_TELE 2542`, `NPC_WALK 2545` (queue one waypoint; the stepper advances a tile/tick), `NPC_ANIM`, `NPC_SAY`, `NPC_DAMAGE 2509`, `NPC_QUEUE 2531`, `NPC_SETTIMER 2537`, `NPC_CHANGETYPE 2507`, `NPC_HUNT/HUNTALL`, `NPC_FINDALLZONE 2516`, `NPC_FREEZE 11018`.
- Missing: `NPC_CHANGETYPE_KEEPALL 2506`, `NPC_HEROPOINTS 2524`, `NPC_INRANGE 2527`, `NPC_DESTINATION 2528`, `NPC_SETHUNT 2534`, `NPC_WALKTRIGGER 2546`.

### Following
`npc_setmode(playerfollow)` exists and works: `TORIRSSERVER_NPCMODE_PLAYERFOLLOW = 4` (`torirs_server.h:1550`), handled in `npc_run_mode` (`torirs_server_world.c:2752`, follow case at 2782) via `npc_walk_to_player(npc, player, 1)` — closes to range 1 and stops. Movement goes through `ToriRSServer_WorldNpcWalkTo` (`torirs_server_world.c:2574`): greedy step, then `ToriRSServer_SceneRoute` **BFS** when the greedy step is refused. Selftest exists (`server/scripts/selftest.rs2:407-410`, C assertions `torirs_server_world.c:14794-14804`, `21422`).

### The blocker
**`npc_run_mode` follows `srv->active_player`, not an owner.** `torirs_server_world.c:2764`:
```c
struct ToriRSServerPlayer* player = srv->active_player;
if( !player || npc->mode == ... ) return 0;
```
and the in-source comment at 2743-2751 admits it: *"this mode machine asks 'whose turn is it' in a phase where it is nobody's … That is a separate defect (osrs230_mockserver.md §6.1)."*

`phase_npcs` (7893) and `advance_npcs` (2951) never call `ToriRSServer_WorldSetActive`, so `active_player` during phase 4 is whatever leaked from the previous tick's `phase_players`/`phase_cleanup`. With `TORIRSSERVER_PLAYER_MAX = 8`, two summoners means both familiars follow the same (arbitrary) player.

Same hazard for scripts: `run_trigger_script` (`torirs_server_scripts.c:1638`, line 1666) does `SSVM_SetActive(state, SSVM_ENT_PLAYER, SSVM_PRIMARY, srv->active_player)` for **every** trigger including `[ai_timer]`/`[ai_queue]`/`[ai_spawn]` — so `%summoning_ticks` read inside a familiar's `[ai_timer]` reads a stale player's varps.

### Ownership / visibility
`struct ToriRSServerNpc` (`torirs_server.h:1563-1848`) has **no owner field**. NPC_INFO is derived per-player from `active` + range each tick (`ToriRSServerPlayer.npc_tracked[]`), so a familiar would be visible to *all* nearby players — which is correct for RS, but there is no mechanism to make an npc owner-only if wanted.

Aggro/leash for reference: `maybe_aggress` (`torirs_server_combat.c:993`), `nearest_victim` (966), `target_within_maxrange` (940) — `maxrange` + `givechase` measured from the **spawn tile**. A familiar spawned next to a moving player would leash to its spawn point; content must set `maxrange` or the engine must exempt owned npcs.

---

## 4. Containers / Beast of Burden

### Model
`struct ToriRSServerContainer` (`torirs_server.h:1202-1268`) + registry `src/torirsserver/ToriRSServer_Container.{h,c}`. This is a port of LostCity `Player.getInventory(inv)`: **resolve-or-create by inv id** (`ToriRSServer_ContainerResolve`, header doc at `torirs_server_container.h:57-96`). A row carries `inv_id`, `slots`, `items`, `owner_kind`, per-slot dirty mask (only when `slots <= 32` — UPDATE_INV_PARTIAL can address 32), `appearance` flag, and up to `TORIRSSERVER_CONTAINER_LISTENERS_MAX` component listeners.

Per player: `containers[TORIRSSERVER_CONTAINER_MAX = 16]` (backpack/worn/bank are *adopted* rows over pre-existing storage via `ToriRSServer_ContainerAdopt`). World-scoped table: `world_containers[16]`.

27 `inv_*` opcodes implemented (`torirs_server_ops_inv.c` + `torirs_server_scripts.c`): `INV_ADD/DEL/CLEAR/SIZE/TOTAL/TOTALCAT/FREESPACE/GETOBJ/GETNUM/SETSLOT/GETVAR/SETVAR/MOVEITEM/MOVEITEM_CERT/MOVEFROMSLOT/MOVETOSLOT/CHANGESLOT/DELSLOT/DROPSLOT/DROPALL/DROPITEM_DELAYED/ITEMSPACE/ITEMSPACE2/TRANSMIT/STOPTRANSMIT/DEBUGNAME`.
Missing: `INV_ALLSTOCK 4303`, `INV_DROPITEM 4311`, `INV_STOCKBASE 4325`, `INV_TOTALPARAM(_STACK) 4329/4330`, `INVOTHER_TRANSMIT 4332`, `BOTH_DROPSLOT 4300`, `BOTH_MOVEINV 4301`.

### Can a BoB container be added?
**Yes, with one hard prerequisite: the inv id must be sized by the cache.**
`ToriRSServer_ContainerResolve` gets the slot count from `ToriRSServer_BankInvSize(inv_id)` (`torirs_server_bank.c:259`), which reads config group 5 at boot. It returns **0** for an id the cache does not size, and `ToriRSServer_ContainerResolve` returns NULL, and every container op **aborts** on NULL. Deliberate: `torirs_server_container.h:38-42` — "*No size constant enters C.* An inv the cache does not size … is not a container, it is a typo."

### Is there an inv type registry in content? **ABSENT.**
- `content.ini` declares namespaces for varp, varbit, stat, param, hitsplat, category, component, 3_interfaces, dbtable, dbrow, npc, loc, enum. **No `[namespace:inv]`.**
- `fields/` has `dbrow/dbtable/enum/loc/npc/obj/param/varp.ini`. **No `fields/inv.ini`.**
- `torirs_server_content.c` has **no `.inv` walker** (`3228-3251` covers `.param .constant .enum .varp .npc .obj .loc .spawn` only).
- Consequence stated in code (`torirs_server_container.h:44-49`): `ToriRSServer_ContainerScope()` returns `TORIRSSERVER_CONTAINER_PLAYER` unconditionally; the world-scoped table is empty by construction; **this is why `shop` is still blocked.**
- Cache side: `configs/all.inv` holds **1026 inv records, ids 0..1025, dense** — e.g. `[trawler_rewardinv] size=13`. A BoB inv must be either a reused existing id or a new id 1026+ packed into the cache via `cachepack pack` (i.e. a client-cache bake, `make -C src torirsserver-cache`).

---

## 5. Timers (familiar decay)

Two independent systems, both live and both used by real content:

**Player timers** — `struct ToriRSServerTimer` (`torirs_server.h:1372-1384`), `player->timers[TORIRSSERVER_TIMER_MAX = 8]`.
- `SS_OP_SETTIMER 2108` / `SOFTTIMER 2109` (`torirs_server_scripts.c:6766`), `CLEARTIMER 2013` / `CLEARSOFTTIMER 2012` (6823), `GETTIMER 2022` (6839).
- Keyed by script id; re-setting re-arms rather than stacking. `clock` is the **absolute** world tick, never a countdown (because `gettimer` returns it).
- `interval` 0 is legal and means "every tick"; only `cleartimer` stops a timer.
- Drained in `ToriRSServer_ScriptsProcessTimers` (`torirs_server_scripts.c:1075`), two passes NORMAL-then-SOFT: NORMAL needs `canAccess()` and runs with protected access, SOFT runs while the player is busy without it.
- **8 slots is a real ceiling for a summoning port** if it wants decay + special-restore + BoB timers alongside existing content timers.

**NPC timers** — `npc->timer_interval` / `timer_clock` + `[ai_timer,<npc>]` (`advance_npcs`, `torirs_server_world.c:2990-2997`); `npc_settimer(n)`, `npc_settimer(0)` stops. Resolved by npc **type** at fire time, so a `npc_changetype` picks up the new type's `[ai_timer]`. Live content examples: `quests/quest_sheepherder/scripts/diseased_sheep.rs2:99-106`, `quests/quest_ball/scripts/nora_t_hagg.rs2:11-41`, `quests/quest_grandtree/scripts/grandtree_black_demon.rs2:12`.
`npc_queue(n, arg, delay)` → `[ai_queue<n>]`, 4 slots per npc (`TORIRSSERVER_NPC_QUEUE_MAX = 4`).

Also: `npc_add(coord, type, duration)` gives a free expiry — `despawn_tick` is checked at the top of `advance_npcs` (`torirs_server_world.c:2963`), so a fixed-lifetime familiar needs no timer at all.

---

## 6. Varps a client CS2 reads

`%name = value` in a script → `SS_OP_POP_VARP` (`torirs_server_scripts.c:3962`); varbits via `SS_OP_POP_VARBIT 2130` (`torirs_server_scripts.c:8137`) → `ToriRSServer_BankSetVarbit`.

Transmit path: `ToriRSServer_WorldMarkVarp` (`torirs_server_world.c:6752`) sends **immediately, inside the setter**, not batched — because a script's source order must be the packet order (documented incident: Slayer Rewards, `torirs_server_world.c:6709-6746`). Encoder chosen by value width: `ToriRSServer_SendVarpSmall` (single signed byte, `mock239_varp.c` `VarpSmallEncoder`: `p1Alt1(value)`, `p2Alt3(id)`) vs `_large`.

Gate: `ToriRSServer_ContentVarp(varp)->transmit`. Declared in a `.varp` config; `fields/varp.ini` routes `transmit`/`scope`/`protect`/`wholewrite` as `scope = server, client = drop`.

**Rules a summoning port must obey:**
- `content.ini [namespace:varp] ids = server` — the server may allocate new varps, listed in `pack/varp.server` (currently 54 lines). A server-allocated varp must be `transmit=no` (a real rev-230 client has no varp of that id).
- **Carrier rule:** `sscompile` refuses `%carrier = value` when varbits are packed into that varp, unless `wholewrite = allow`; the runtime re-checks in `check_carrier_write` (`torirs_server_world.c:6820`) and the selftest asserts the count is zero. 2,872 of 5,705 varps carry varbits.
- Engine-side mirrors go in `varp_side_effects` (`torirs_server_world.c:6859`) — the shared seam for both writers (`ToriRSServer_WorldSetVarp` and `POP_VARP`).
- `scope=perm` is what makes a varp persist (`torirs_server_save.c:260`).

Reference values from 2009scape for orientation (rev-530 ids, **must not be copied**): `FamiliarManager.java:36-42` — varp **1160** carrying varbits **4280** (orb visibility), **4281**, **4282**; interfaces **662** (summoning tab) and **671** (BoB container).

---

## 7. Skills — data or hardcoded? **Both, and the split matters.**

**Content side:** `OSRS-Content/osrs239-content/pack/stat.pack` — authored, 23 entries `0=attack … 21=hunter, 22=construction`. `content.ini [namespace:stat] ids = protocol, names = authored` — "No gameval archive names the skills … the ids are the protocol's: UPDATE_STAT carries this index." `torirs_server_content.c:1563` errors with "`%s` is not in `pack/stat.pack`" for an unknown skill name.

**Engine side, hardcoded:** `torirs_server.h:506-518`
```c
TORIRSSERVER_STAT_ATTACK = 0, ... TORIRSSERVER_STAT_AGILITY = 16,
TORIRSSERVER_STAT_COUNT = 23,
```
with the comment *"the array is the full 23 so a stat id from the wire is never out of range."* `stat_level[23]`, `stat_boosted[23]`, `stat_xp_tenths[23]`, `stat_drain[23]` on npcs, `stat_dirty` a uint32 mask.

Adding `23=summoning` is: one line in `pack/stat.pack` + `TORIRSSERVER_STAT_COUNT = 24`. The uint32 dirty mask still fits (32 max). `torirs_server_combat.c:353,452,477` bound-check against `TORIRSSERVER_STAT_COUNT`.

Stat opcodes implemented: `STAT 2122`, `STAT_ADD/ADVANCE/BASE/BOOST/DRAIN/HEAL/RANDOM/SUB/TOTAL 2113-2121`. `[advancestat]` is dispatched (level-up). **`SET_SKILL_LEVEL 2106` is MISSING**, and `[changestat] 165` is never dispatched.

Client-side: the rev-239 stat panel is CS2 over the cache's own stat table — a 24th skill has **no client UI, no XP-drop sprite, no level-up interface**. ABSENT and out of this topic's scope, but it is where "the skill exists" becomes visible.

---

## 8. Combat — could a familiar fight for the player?

`torirs_server_combat.c` (55 KB) header (`:1-30`): *"Combat, minus the combat."* Every number — effective levels, rolls, max hit, accuracy, XP, animations, prayers — is content in `server/scripts/skill_combat/combat_stats.rs2`, fired via `[opnpc2,<npc>]` (player swing), `[ai_opplayer2,<npc>]` (npc swing), `[advancestat,<stat>]`.

What the engine holds:
- `ToriRSServer_CombatEngage(srv, slot)` (`:765`) — `player->combat_target = <npc slot>`; arms `TORIRSSERVER_INTERACT_NPC` op 2 so content owns the swing loop.
- `ToriRSServer_CombatPlayerApproach` (`:832`) — re-path every tick *before* the step.
- `ToriRSServer_CombatNpcTick` (`:1214`), `maybe_aggress` (`:993`), `npc_death_step` (`:1071`), `ToriRSServer_CombatRespawnTick`.
- `ToriRSServer_CombatHitNpc` (`:551`) / `hit_player` (`:689`), `ToriRSServer_CombatAddXp` (`:470`), `ToriRSServer_CombatAttackable(npc_type)` (`:751`, looks for the literal op string `"Attack"`).

**The shape is strictly player ↔ npc:**
- `ToriRSServerPlayer.combat_target` (`torirs_server.h:2408`) is an **npc slot**.
- `ToriRSServerNpc.combat_target` (`torirs_server.h:1806`) is a **player pid**.
- There is **no npc↔npc combat loop anywhere**. ABSENT.

A familiar *can* deal damage today, but only script-driven: `[ai_timer,<familiar>]` → `npc_findallzone`/`npc_findall` to pick a target → `npc_damage(...)` (`SS_OP_NPC_DAMAGE 2509`, implemented). What it cannot do is participate in the engine's approach/attack-clock/retaliation/death-credit machinery.

Death credit is player-indexed: `ToriRSServerNpc.death_credit_players[TORIRSSERVER_PLAYER_MAX]` and `ToriRSServer.loot_credit_players[]` — a familiar kill would credit nobody unless the owner is threaded in.

---

## ENGINE WORK NEEDED

- **Owner binding on an npc.** No `ToriRSServerNpc.owner_pid`. Every follow/AI path resolves the player through the ambient `srv->active_player` (`npc_run_mode`, `torirs_server_world.c:2764`) — with 8 player slots, both familiars follow one arbitrary player. Needs an owner field + `npc_run_mode` and `run_trigger_script` resolving the player from it.
- **`run_trigger_script` sets ACTIVE_PLAYER from `srv->active_player` for `ai_*` triggers** (`torirs_server_scripts.c:1666`). A familiar's `[ai_timer]` reading `%summoning_*` reads a stale player. Needs owner-aware dispatch (or an explicit `npc_owner` pointer op).
- **`phase_npcs` never calls `ToriRSServer_WorldSetActive`** — phase 4 runs with a leftover `active_player`. Any per-owner familiar behaviour must not run in phase 4 as written.
- **npc uid has no generation counter** (`torirs_server_scripts.c:4585` states this outright). A familiar uid stashed in a varp across a despawn resolves to whatever npc took the slot. Needed for `npc_finduid($familiar)` to be safe over a session.
- **CORRECTED: npc definition ids are not bounded by the 14-bit initial field.** NPC_INFO v5
  carries a 16-bit per-client index, then the 14-bit initial definition; high ids use the add's
  extended/update flag plus update-mask `0x1` and its transformed unsigned 16-bit replacement in
  the same packet. Type 20000 therefore has an extended-path writer/reader regression. The
  measured 16294..16383 gap is not an NPC-definition budget.
- **`pack/npc.client` currently lists ZERO entities** — no npc has ever been added to the client cache from this tree. Familiars would be the first, exercising an untested `cachepack pack` route (`docs/PACK_ENTITY_SPLIT_PLAN.md §4 step 3`).
- **No inv namespace / no inv type registry in content** (`[namespace:inv]` ABSENT, `fields/inv.ini` ABSENT, no `.inv` walker in `torirs_server_content.c`). A BoB inv id must be sized by the cache's config group 5 or `ToriRSServer_ContainerResolve` returns NULL and every op aborts. Same blocker that keeps `shop` blocked.
- **`ToriRSServer_ContainerScope()` returns PLAYER unconditionally** — no shared/world scope, so a familiar-owned container can only be a player row (16 max per player). Fine for BoB; not fine if the container must survive the familiar's despawn independently.
- **`TORIRSSERVER_STAT_COUNT = 23` is a C constant** — needs 24 for summoning, plus `pack/stat.pack` line `23=summoning`.
- **`SS_OP_SET_SKILL_LEVEL 2106` unimplemented**; `[changestat] 165` never dispatched.
- **No npc↔npc combat**. A combat familiar attacking an NPC on the player's behalf has no engine loop, no approach, no attack clock, no retaliation, no death credit. Either content drives it entirely (`ai_timer` + `npc_damage` + `npc_findallzone`, all implemented) or the engine grows a second combat pairing.
- **`TORIRSSERVER_TIMER_MAX = 8` player timers** — a summoning port adding decay + special-restore competes with existing content.
- **No content feature-flag mechanism at all.** `src/features/` is the *client-era* table (`ToriRS_FeatureTable`, pathing/painter/audio), not a content gate. `content.ini` has only `[namespace:*]`. `TORIRSSERVER_*` env vars are all debug/paths (`TORIRSSERVER_VERBOSE`, `TORIRSSERVER_CACHE`, `TORIRSSERVER_CONTENT`, `TORIRSSERVER_SCRIPTS`, `TORIRSSERVER_HOME`, `TORIRSSERVER_SAVES`, `TORIRSSERVER_STAFF_LEVEL`, `TORIRSSERVER_TRACE_*`, …). `sscompile` compiles every `*.rs2` under `--src` recursively with **no skip mechanism** (`ssc_compile.c:2896` is a bare extension test; `docs/SCAPE2009_CONTENT_PORT_QUEUE.md:27-32` calls `.rs2.skip` renaming an abuse and forbids it). The flag has to be built: a `content.ini` section or a `--exclude`/`[feature:...]` gate in `sscompile` + a runtime gate in `torirs_server_boot.c`.
- **Missing ops likely wanted:** `NPC_SETHUNT 2534`, `NPC_INRANGE 2527`, `NPC_DESTINATION 2528`, `NPC_CHANGETYPE_KEEPALL 2506` (familiar morph), `HINT_NPC 2028` / `HINT_STOP 2030` (familiar hint arrow), `IF_SETTAB 2049` / `IF_SETTABACTIVE 2050` (summoning tab), `P_TRANSMOGRIFY 2092`, `INVOTHER_TRANSMIT 4332`.

---

## RISKS / UNKNOWNS

1. **CORRECTED: there is no 14-bit cache NPC-id ceiling.** The add's 14-bit field is only the
   initial definition; definitions 16384..65535 replace it through the same-packet
   extended/update + mask-`0x1` transformed-16-bit path. Do not use the free-id count below 16384
   to scope or tier the port.
2. **Everything about the client half is out of this recon's scope and is ABSENT**: no summoning tab interface, no orb, no XP drop, no familiar models, no pouch/scroll sprites, no CS2. A working server with no client surface is invisible.
3. `ToriRSServer_ContentLoadServerBand` prints LOADED/MISSING/STALE — a stale band silently falls back to text overlays. New summoning npc/obj fields must survive `make -C src torirsserver-servpack` and the identical-to-text verification.
4. `server/scripts/build/` and `server/pack/` are gitignored; a fresh checkout has no script pack, and the engine's trigger fallbacks are *gated on a pack being loaded* — so a summoning slice that appears not to run may just be an unbuilt pack.
5. **GUESS:** the client does not need an `inv` config record to render an arbitrary inv id in an UPDATE_INV-bound component (it keys containers by id at runtime); only the *server's* size lookup needs the cache record. Not verified — check `src/inv/inv_manager.c` `InvManager_EnsureContainer` against the rev-239 UPDATE_INV decoder before assuming.
6. **GUESS:** `npc_setmode(playerfollow)` + `npc_finduid` + a player `settimer` is enough to express a non-combat familiar in pure content *today* for a single-player world. Untested; the `active_player` staleness makes it wrong the moment a second player logs in.
7. rev-530 ids (varp 1160, varbits 4280-4282, ifaces 662/671, npc/obj ids) from 2009scape are a *behaviour* reference only — `PORTING_GUIDE.md` and both port queues forbid copying them into osrs239 content. Every id must be allocated fresh through `pack/*.alloc` + `tools/ss_allocate.py`.
8. `ToriRSServer_ContainerScope` and the absent inv namespace are a known, documented blocker (`shop` is blocked on the same thing). Landing `fields/inv.ini` + `[namespace:inv]` is arguably a prerequisite slice of its own, not part of Summoning.
9. "Distinct folder clearly marked as ported" has no precedent in the tree: the two `port`-named things are `osrs239-content/port/` (id-mapping `.map` files from a cache diff, not scripts) and `docs/*_PORT_QUEUE.md`. `server/scripts/` is organised strictly by subject (`skill_*`, `quests/`, …). A `server/scripts/ported_summoning/` breaks that convention; `skill_summoning/` matches it but is not marked. Needs a decision.
10. I did not audit: `torirs_server_zone.c` event buffering, `torirs_server_mapinstance.c` interaction with familiars, whether `TORIRSSERVER_PLAYER_MAX = 8` × 16 containers × a BoB row hits any static-size ceiling, or the RSProt/osrs239 codec path (`docs/RSPROT_OSRS239_PORT.md`).

===== RECON: rs-interfaces-ui =====
# 3draster IF3 authoring path — recon

## 1. On-disk interface format + encoder

**Location:** `OSRS-Content/osrs239-content/interfaces/` — 1936 files, 9.4 MB, **968 interfaces × 2 files each**.

Two plain-text files per interface:

| file | role |
|---|---|
| `<name>.if` | UTF-8 INI. One `[block]` per component, in file order. Blocks emit only fields that differ from a default-constructed component. |
| `<name>.compack` | `<file_id>=<block_name>`, one line per component. **The id authority** — a block's position in the `.if` is NOT its child id. Sparse ids are legal (holes survive round-trip). |

Real example — `interfaces/orbs.if:1` (interface 160):

```
// Interface 160 — 57 components.
[universe]
if3=yes
type=0
widthmode=1
heightmode=1
onload=i:8220,i:10485760,i:10485761

[orb_prayer]
if3=yes
type=0
y=71
width=57
height=34
layer=10485760
onload=i:82,i:-2147483645,i:10485781,...
```

- `layer=` is written **absolute** (`10485760` = `160<<16|0` = `orbs:universe`); the codec subtracts the interface id on encode (`dat2_component.h` doc comment ~line 380).
- `onload=`/hook fields are `i:<int>,s:<string>` tagged args (`emit_script_vars`, `cp_decode.c:541`; parse `parse_script_vars`, `cp_decode.c:889`).
- `empty` key marks a zero-length member.
- `if3=yes|no` selects the field set per-component (IF1 records still exist: 2,096 in osrs239).

**Encoder: YES, full and proven.**
- Codec registration: `3rd/rscache/tools/cachepack/cp_decode.c:1373` — `cp_codec_interface = { "if", NULL, interface_write, interface_read, 0 }`.
- Text → bytes: `interface_read`, `cp_decode.c:1242`. Loads `.if`, requires the `.compack` (fails hard without it), encodes each block with `RSCache_Dat2ComponentEncodeIf3` (`3rd/rscache/src/datatypes/dat2_component.c:1419`), assembles a `RSCache_FileList` with explicit `file_ids`.
- Bytes → text: `interface_write`, `cp_decode.c:740` / `emit_component`, `cp_decode.c:587`.
- Asset table row: `cp_assets.c:90` — `{"interfaces", "3_interfaces", "interface", "ifb", TABLE_INTERFACES, 0, gameval_archive 14, &cp_codec_interface}`.
- Fidelity: `3rd/rscache/EXCEPTIONS.md:2089` — **74,719 / 74,719 components byte-exact** across three caches.

**Archive index:** `pack/3_interfaces.pack`, 969 lines, `0=100guide_eggs_overlay` … `968=myq6_integrity_bar`. (969 names / 968 real archives — one phantom from gameval 14.)

**Bake pipeline:** `make -C src torirsserver-cache` (`src/makefile:1676`) → `cachepack pack --src OSRS-Content/osrs239-content --base cache.osrs239 --out cache.osrs239.baked --rev osrs239 --assets --binary --gamevals`. Client-visible edits (interfaces, CS2, sprites, configs) reach a running client **only** through this bake; pure server `.rs2` does not need it.

## 2. Rules for rev-230/239 UI (`docs/UI_ERA_PORTING_GUIDE.md`, `docs/REV230_UI_OWNERSHIP.md`)

**The triage question** (`REV230_UI_OWNERSHIP.md:29`): *who owns this pixel — the client, the clientscript, or the cache record?* Then check exactly one of them writes it. Three of four documented bugs had two writers or zero.

Ownership split:

| owner | owns |
|---|---|
| **cache record** | geometry, sprites, text defaults, `op1..op10` verb names, `noclickthrough`, hook wiring (`onload`, `onop`, `onvartransmit`, …) |
| **CS2 clientscript** | everything dynamic — `cc_create`s the rows/cells, reads varps/varbits/enums/obj params/stats, composes coloured strings, chat input line, tab sprite swaps |
| **server** | *facts and permission only*: set the varbit/varp/stat, `if_opensub` into a slot, `if_setevents` to arm ops |

Key rules:

1. **Nothing is clickable until `IF_SETEVENTS`** (`UI_ERA_PORTING_GUIDE.md` §2.2). The cache still shows the verb and runs the local clientscript; no packet leaves. Wire: we speak **v1 (i32)** even against a rev-239 cache — bit 0 = op-less click, bits 1-10 = ops 1..10, bits 11-16 = target kinds, 17-19 drag depth, 20 drag target, 21 target. A v2 (i64) mask over a v1 packet arms nothing and reports no error.
2. **Events are purged when the interface unmounts.** Anything armed must be re-armed on every open. Landed mechanism: `[if_open,<interface>]` trigger (`SS_TRIGGER_IF_OPEN` = 178, `src/serverscript/ss_trigger.h:178`), dispatched from `ToriRSServer_SendIfOpensub` at `src/torirsserver/torirs_server_encode.c:933`. **Doc drift:** `UI_ERA_PORTING_GUIDE.md` §4 still lists "an if-open trigger" as *not landed* — it is landed and in use (`server/scripts/interface_journal/scripts/journal.rs2:47`, `interface_bank/scripts/bank.rs2:80`).
3. **`IF_CLOSESUB` slot poison** (`memory/if-closesub-slot-poison.md`): close must hide the outgoing **group** (`hide` + `hide_unmounted` on its roots), never the slot. Nothing clears a slot's `behavior.hide`, so hiding the slot poisons it forever and every later mount lays out but never draws. **Test open → close → REOPEN**; the bug only shows on the second open.
4. **Clipping is per-surface, never compounded** (`memory/interface-layer-clip-surface.md`). A layer clips children to `own_bounds ∩ surface_clip`, not `∩ parent_clip`. Rule lives in `UITree_LayerChildClip` + `UITree_ComponentEstablishesSurface` (`src/ui/uitree_scroll.c`), called by all four walks (emit / hit-test / menu-collect / hover / drop-target). Do not re-implement inline. Exception: an IF3 **scroll viewport** *does* establish a surface.
5. **Input blocking is declared, never named in C** (`REV230_UI_OWNERSHIP.md` §4): an interface is opaque to the world where a `noclickthrough=yes` layer covers the point, **or** where a modal (`IF_OPENSUB type == 0`) mount covers it. Overlays (type 1) are click-through unless they raise the flag themselves. Answered by `UITree_PointBlocksWorld()` (`src/ui/uitree_input.c`), consumed by `app_world_mouse_gate()`.
6. **`TYPE_INV` does not exist at rev 230.** Items are `cc_create`d type-5 graphics with `SETOBJECT`. Three separate features (stack counts, drag ghost, selection outline) were each implemented only on the dead grid path. Anything written against `emit_rs_inv_slots` is missing from the CS2 cell path until checked.
7. **`app->slots.*` is dat1-only** — permanently `-1` at rev 230. Any `if (app->slots.X)` in a rev-230 path is dead code.
8. **Colour is in the text.** `<col=…>` markup; anything measuring string width must call `ToriDraw_FontMarkupTokenLength`, not its own byte loop.
9. **Trigger-key ceiling:** a compiled ServerScript trigger key puts the subject at bit 10, so subjects must be < 2²¹. `orbs:runbutton` = `160<<16` does not fit — those compile **name-addressed** and resolve via `ToriRSServer_ScriptsRunIfButtonNamed`. Do not widen the on-disk key.
10. **A gameval symbol from a neighbouring revision is not evidence.** Read the `.compack` and the component's own `onload`.

## 3. Opening an interface; the rev-230 root; sidebar tabs

**Root is 161** = `toplevel_osrs_stretch` (assert at `src/torirsserver/torirs_server_world.c:9754`, manifest `[ui:boot] interface_id=161` in `manifests/manifest_osrs230.ini:40`). Siblings: `toplevel` = 548 (Fixed–Classic), `toplevel_pre_eoc` = 164 (Resizable–Modern).

**Wire / senders** (`src/torirsserver/torirs_server_encode.c`):
- `ToriRSServer_SendIfOpentop(player, group)` :763 — 2-byte interface id; opcode 60 @230, 96 @239 (`torirs_server_wire.h:20`, `w239_if_opentop` at `torirs_server_wire.c:148`). Mutates `ToriRSServer_IfStateOpenTop` **before** the packet is observable.
- `ToriRSServer_GameframeOpentop(player, group)` :836 — the real entry. Sends OPENTOP, binds `player->gameframe_{mainmodal,sidemodal,floater}` by resolving `<top_name>:<role>` through the component pack, then walks the **content enum named after the top interface** and issues one `IF_OPENSUB` per row, then `IF_RESYNC_V2`.
- `ToriRSServer_SendIfOpensub(player, parent, child, group, type)` :888 — `p1 type, p2Alt2 interfaceId, p4Alt3 (parent<<16|child)`. `type`: 0 modal, 1 overlay, 3 tab/sidemodal. Fires `SS_TRIGGER_IF_OPEN` on `group` at :933.
- `ToriRSServer_SendIfSetevents` :1018 / `_v2` :1033. `ToriRSServer_SendIfMovesub` :783. `ToriRSServer_SendIfClosesub` (SS op 11005).
- Slot-role aliasing: `ToriRSServer_RemapGameframeSlotUid` :713 rewrites `toplevel*:mainmodal|sidemodal|floater` (and any `<top>:<role>` where the source interface itself has a `:mainmodal`) onto the session's live top. So content can name `toplevel_osrs_stretch:sidemodal` and it survives a Fixed/Modern layout switch.

**ServerScript ops** (`src/serverscript/ss_opcode.h:429`): `IF_SETEVENTS` 11000, `IF_OPENSUB` 11001, `RUNCLIENTSCRIPT_SS` 11002, `RUNCLIENTSCRIPTVARARG` 11003, `P_COUNTDIALOG_NOPROMPT` 11004, `IF_CLOSESUB` 11005, `IF_OPENTOP` 11006, `IF_MOVESUB` 11007, `IF_GETMAIN` 11008.

**The mount table is content, not C:** `OSRS-Content/osrs239-content/server/scripts/player/configs/gameframe.enum` — `[toplevel_osrs_stretch]` `inputtype=component outputtype=interface`, 24 rows: 9 HUD overlays (`chat_container→chatbox`, `orbs→orbs`, `hpbar_hud`, `buff_bar`, `stat_boosts_hud`, `pm_container`, `pvp_icons`, `popout`, `tli_listener`) + `side0..side13`. **Order matters** — mounted in file order.

**Sidebar tabs: visibility is purely `if_hassub`.** `scripts/script_912.cs2` = `[proc,toplevel_sidebuttons_enable]`:

```
$component2 = enum(int, component, enum_1137, $int3);
if (if_hassub(enum(component, component, $enum0, $component2)) = true) {
    if_sethide(false, $component4);   // stone
    if_sethide(false, $component5);   // icon
} else { if_sethide(true, …); }
```

No varbit gates them. A slot the server never opens is a tab that does not exist on screen. The loop terminates when `enum_1138`/`enum_1139` return null at index N.

- `enum_1137` → `side0..side13` = 161:76..161:89 (10551372..10551385)
- `enum_1138` → `stone0..stone13` = 161:59..65, 43..49
- `enum_1139` → `icon0..icon13` = 161:66..72, 50..56
- 161's compack has exactly 99 components, **side0..side13 and no spare** (`interfaces/toplevel_osrs_stretch.compack`).

**Content idiom for a panel** (`server/scripts/interface_journal/scripts/journal.rs2:35`):
```
if_setevents(side_journal:quest_list, 0, 0, ^if_event_op1);
[if_open,side_journal]
~journal_show(%side_journal_tab);
[proc,journal_show](int $tab)
  if_opensub(side_journal:tab_container, questlist, 1);
[if_button,side_journal:quest_list]
  ~journal_show(^journal_tab_quests);
```

## 4. Precedent for authored/modified interfaces; the "interface editor"

**Modified-from-cache: 3 interfaces, all small.** (`git log` in the `OSRS-Content` submodule)
- `interfaces/chatmenu.if` — commit `20de8327a2` "Place revision 239 choice menus from content": `[options]` block, replaced `xmode=1/ymode=1` with `x=0 / y=-13`. Pure geometry.
- `interfaces/barrows_puzzle.{if,compack}` — commit `f8db65f0c2`: renamed components `3=1 → 3=puzzle_q0` etc. Pure renames (compack + block headers).
- `interfaces/combat_interface.compack` — commit `d19f754f4a` "move hardcoding into content": renames.

**ABSENT: no interface has ever been created from scratch in this tree.** `pack/3_interfaces.pack` is exactly ids 0..968, all cache names. `pack/enum.client` and `pack/varp.client` are header-only (**zero entries**) — no authored record has ever been routed *into* the client cache via the membership files. That path exists (`cp_membership.c`, documented in `pack/enum.client`'s header) but is untested.

**The "interface editor" is the client itself.** `src/main.c` → target `torirs`, built by `make -C src` (NOT the root CMake — the root `CMakeLists.txt` does not reference `src/main.c`, `src/ui`, `src/engine` or `src/game` at all).
- `./src/torirs [cache_dir] [interface_id] [--manifest boot.ini] [--bmp] [--uncapped]` (`main_print_usage`, `src/main.c:2136`; `App_OpenRootInterface(&app, cfg.interface_id)` at :2652). Opens an arbitrary interface id as the root and renders it — a viewer/inspector, **not a WYSIWYG editor**: there is no write-back to `.if`.
- `--bmp` writes `build/interface_<id>.bmp` then still enters the SDL loop, so headless runs need a kill/alarm.
- Reference implementation being matched: the TypeScript editor at `xrsps-typescript/tools/interface-editor`.
- Stale-`.o` gotcha: dep tracking (`-MMD -MP`) was added 2026-07-21; before that a header change did not rebuild dependents and mid-struct field insertions silently read wrong offsets.

**Inspection tools:**
- `tools/dump_interface/dump_interface <cache> --iface N [--dat1|--dat2] [--child C] [--json]` — component-by-component dump.
- `tools/dump_interface_layout/` — resolved geometry.
- `TORIRS_DUMP_BOUNDS=<group>` — post-net resolved boxes + size modes + scroll extents. **First move on a blank panel.**
- `3rd/rscache/tools/cs2/cs2 decompile --rev osrs239 --cache cache.osrs239 --out /tmp/cs2 <id>` — read a panel's onload before writing anything.
- `TORIRS_CS2_TRACE=1`, `TORIRS_ANIM_DEBUG=1`, `TORIRS_DUMP_EMIT_EXIT=cover|<group>|all`.

## 5. Feasibility per summoning surface

Baseline facts:
- osrs239 content has **zero** summoning: `grep -ril summon configs/` returns only unrelated names (`summonedzombie` npc 69, `regicide_quest_kings_summons` obj 3206, `delrith_seen_summoning_cutscene` varbit 2569). **ABSENT.**
- `pack/stat.pack` is 23 stats, `0=attack` … `22=construction`. The index is **protocol-fixed by UPDATE_STAT**. Summoning = stat 23 → engine + wire + stats-panel (interface 320) work, not just content.
- 2009scape rev-530 interface ids (from `Server/src/main/content/global/skill/summoning/`):
  - **662** familiar/summoning tab (`FamiliarManager.java:328` `openTab(new Component(662))`), 198 components, contains `S P E C I A L   M O V E`, `9999`, `100%` text and 22 model widgets
  - **665** BoB single-tab (`BurdenBeast.java:198`)
  - **669** pouch infusion (`SummoningCreator.java:43`), 24 components
  - **671** BoB main container (`BurdenBeast.java:190`), 31 components
  - **673** scroll creation (`SummoningCreator.java:48`)
  - **747** summoning **orb** — `Components.TOPSTAT_LORE_747`, windowpane slot 16 resizable / 73 fixed. Note `InterfaceManager.java:388`: `//sendTab(16, 747); // Summoning bar` is **commented out** — 2009scape never displays it.
- The rev-530 cache (`2009scape/Server/data/cache/`, dat2, idx0..25, 88 MB) **decodes with this repo's IF3 decoder today**. Verified: `tools/dump_interface/dump_interface .../Server/data/cache --iface 747 --dat2` yields a clean 6-component tree — layer `57x34` (identical to `orbs:orb_prayer`'s `width=57 height=34`), `graphic=1206` backing, `graphic=1244`/`1245` icons, one text child. 662 yields readable strings. **Caveat:** `tools/dump_interface_common.c` uses the legacy `RSCacheDat2A_ComponentDecodeIf3` with **no era/rev selection** — the decode is plausible but unvalidated for 530. There is **no rev-530 profile**: `3rd/rscache/src/revisions/` has `rs643`, `rs727`, osrs184/230-239, dat1 lc245_2/lc254. **ABSENT.**

| surface | verdict | how |
|---|---|---|
| **Summoning orb** in minimap chrome | **IMPLEMENTED / VERIFIED.** Chrome is cache-built. | Interface 160 components 57..64 use exact interface-747 source sprites 1200/1206/1244/1245 under target ids 20000..20003. Authored clientscript 12000 reads stat **24** directly and redraws on its transmit. `(54,158)` was measured behind the fixed tab strip; visible placement is `(89,128)`. Component 64 op1 calls the familiar in the real client. |
| **BoB container** | **PORT 530's 671 layout + AUTHOR the cells; needs a new inv.** | Geometry can be transcoded from 530:671 (31 components). But rev 230 has **no `TYPE_INV`** — cells must be `cc_create`d by a new `.cs2` (copy the bank's `script274` `cc_create($component7, 5, …) + cc_setoutline(1) + cc_setsize(36,32,0,0)` loop). Server side: a new record in `configs/all.inv` (currently 1026 records, ids 0..1025; **no `pack/inv.alloc` exists** — inv is a cache-id namespace, so a new inv needs either an id ≥1026 routed via `pack/inv.client` (untested path) or an unused cache id). Mount as a **modal** (`if_opensub type 0`) so it blocks world input. |
| **Familiar special-move button** | **AUTHORABLE, easy.** | It is just a `type=0` layer with `clickmask` + `op1` + a `<col>` text child and a percentage bar — exactly `orbs:specbutton`/`orbs:orb_specenergy` (`orbs.if`). Either add it to the summoning orb block, or as a component in the familiar panel. Needs `if_setevents(…, ^if_event_op1)` + `[if_button,<iface>:<com>]` in `.rs2`. |
| **Infusion interface** | **PORT 530's 669, or author fresh.** Medium. | 24 components; a list + model preview + buttons. Rev-230 idiom is a CS2 builder reading an enum of pouches + `oc_param` per obj (the prayerbook pattern, `UI_ERA_PORTING_GUIDE.md` §2.4). Cheaper to **author** a new `.if` in the 239 visual vocabulary than to transcode 530 pixel-for-pixel, because a 530 record's `graphic=`/`font=` ids point at 530 sprites/fonts that mean different things in 239 — every id needs remapping regardless. |
| **Summoning sidebar tab** (not asked but implied) | **AUTHORABLE, highest cost.** | 161 has exactly `side0..side13` / `stone0..13` / `icon0..13` and **no spare slot**. Needs: 3 new components appended to `toplevel_osrs_stretch.{if,compack}` (×3 tops if you want Fixed/Modern too), row 14 added to cache enums `enum_1137/1138/1139` in `configs/all.enum`, a row in `server/scripts/player/configs/gameframe.enum`, new stone/icon sprites, and stone-strip geometry (currently 7+7). |
| **New standalone interface (id ≥ 969)** | **Mechanically supported, never done.** | `cp_assets.c:1400` walks `pack/3_interfaces.pack` (the id authority) rather than the directory, and `put_archive` updates the reference table — so `969=summoning_infusion` + `interfaces/summoning_infusion.{if,compack}` mints a new archive in the baked cache. **Zero precedent** (see §4). |

**Ported-content folder.** GUESS, worth testing early: because `import_one` builds the path as `snprintf(base, "%s/%s", root, name)` and `torirs_server_content.c:284` does `snprintf(path, "%s/interfaces/%s.compack", dir, iface_name)`, a pack line `969=ported530/summoning_infusion` would resolve to `interfaces/ported530/summoning_infusion.{if,compack}` on both sides. The cost is that the *symbol* then contains a slash, which leaks into `[if_button,ported530/summoning_infusion:foo]`. Untested.

## 6. The rev-239 minimap orb area

**Interface 160 = `orbs`**, mounted into **`toplevel_osrs_stretch:orbs` = 161:33** by `server/scripts/player/configs/gameframe.enum:33` (`val=toplevel_osrs_stretch:orbs,orbs`). 57 components, ids 0..56, listed in `interfaces/orbs.compack`.

Container blocks and their `onload` scripts (from `interfaces/orbs.if`):

| block | id | x,y | w×h | onload script |
|---|---|---|---|---|
| `universe` | 0 | — | mode 1/1 | 8220 |
| `triggers` | 1 | — | — | — |
| `xp_drops` | 6 | 0,17 | 27×27 | 1039 |
| `orb_health` | 7 | 0,37 | 57×34 | 446 |
| `orb_prayer` | 18 | 0,71 | 57×34 | **82** |
| `orb_runenergy` | 26 | 10,103 | 57×34 | 447 |
| `orb_specenergy` | 34 | 32,128 | 57×34 | 2069 |
| `orb_store` | 43 | 85,143 | 34×34 | 2396 |
| `orb_contentrecom` | 48 | 54,163 | 34×34 | 2480 |
| `orb_worldmap` | 49 | 0,30 (mode 2/2) | 30×30 | 1492 |
| `wiki` | 50 | 1,0 (mode 2/2) | 40×34 | 3304 |
| `tooltip` | 51 | — | 1×1 | — |

Minimap itself: `toplevel_osrs_stretch:minimap` = 161:30, `map_minimap` = 161:22, `map_container` = 161:95, `compassclick` = 161:31.

**Can new children be appended to a cache interface via the content tree? YES.**

Mechanism, end to end:
1. Add block(s) to `interfaces/orbs.if` and matching lines `57=orb_summoning` … to `interfaces/orbs.compack`.
2. `interface_read` (`cp_decode.c:1242`) re-encodes **the whole archive** from the `.if` — there is no partial merge, so a grown component list is just a longer filelist. Ids come from the compack, not from position, so holes and non-contiguous ids are fine.
3. `put_archive` writes the container **and the reference table's child list** (`cp_assets.c:1520`+, `cp_reference_write`).
4. `make -C src torirsserver-cache` bakes; boot with `manifests/manifest_osrs239_packed.ini` / `cache.osrs239.baked`.
5. The **server** picks the new name up from the same `.compack` (`src/torirsserver/torirs_server_content.c:284` composes `(interface<<16)|child` from `pack/3_interfaces.pack` + the compack), so `[if_button,orbs:orb_summoning]` resolves with no C change.

**Caveat — gameval archive 14 is NOT written back.** `cp_names_emit_gamevals` explicitly skips archive 14 (`cp_names.c:1412`): "archive 14 is nested (interface + components) and this writes flat records only". So a new component's *name* lives only in the content tree, never in the baked cache's gameval table. Harmless at runtime (the client never reads gamevals), but a later `cachepack unpack` of the baked cache would not recover the name. Note `lc_pack_save` **merges** rather than truncates, and `seed_interface_names` never renames an id the pack already lists — so authored names/comments do survive a re-seed.

---

## RISKS / UNKNOWNS

1. **No new interface has ever been minted here.** `pack/3_interfaces.pack` is exactly ids 0..968 (all cache). The code path for id ≥ 969 exists and looks correct (`cp_assets.c:1400` + `put_archive` + `cp_reference_write`) but is unexercised. Prove it with a throwaway 2-component interface **before** designing around it.
2. **`pack/enum.client` / `pack/varp.client` are empty of entries.** Routing an authored record *into* the client cache is a documented but unexercised path. If summoning needs a client-visible enum (e.g. a pouch table for a CS2 builder), prefer **editing an existing cache enum** in `configs/all.enum` — proven — over minting a new one.
3. **No rev-530 cache profile.** `3rd/rscache/src/revisions/` has no `rev_dat2_rs530.c`. `dump_interface` decoded 530 interfaces plausibly, but it uses the legacy `RSCacheDat2A_*` API with **no era selection** at all, so the decode is unvalidated. `cachepack`'s rev-aware path (`RSCache_Dat2ComponentDecodeRevFromProfile`) would refuse or mis-select. Cross-family risk is real: `decode_if3_rs2` (`dat2_component.c:595`) is a genuinely different layout and `RSCache_Dat2ComponentEncodeIf3` **refuses to invert it** (`:1428`).
4. **Every ported graphic/font/model id must be remapped.** 530:747 references `graphic=1206/1244/1245`; those ids mean unrelated sprites in osrs239. Sprite ids in the tree run 0..8534 (`pack/8_sprites.pack`); sprites are directories of BMPs + `pack.meta` (palette + per-sprite offset/size). `port_npc` (`3rd/rscache/tools/port_npc/`) is a cross-revision closure porter but takes `--from-rev`/`--to-rev` profile names, so 530 needs a profile first. `port_lostcity` runs the wrong direction (dat2 → LostCity source).
5. **Stat 23 does not exist.** `pack/stat.pack` stops at 22; the index is protocol-fixed by UPDATE_STAT. A summoning *level* needs engine + wire + interface-320 work, or must be faked in a varp. Decide this before authoring any orb that calls `stat_base(...)`.
6. **`port/` is LostCity-only.** `OSRS-Content/osrs239-content/port/{names,configs,vars,constants,categories}.map` and `make -C src test-port` are all keyed to the rev-254 LostCity reference. A 530 port has no equivalent ledger, and `test-port` will not gate it. GUESS: a parallel `port530/` ledger is the honest shape.
7. **`cachepack pack` declines silently when a codec refuses.** With `--base`, an archive the codec cannot re-encode keeps the *base* bytes and is byte-indistinguishable from success (counted as `codec-declined`, printed but easy to miss). Read cachepack's summary line on every bake; 219 of osrs239's clientscripts already decompile-but-do-not-recompile.
8. **Double-bake trap** (`memory/interface-pack-double-bake.md`): CS2 pre-bakes packs (script 901 pre-bakes 149/163/728/896 during the 161 boot) before the server's `IF_OPENSUB`. `UITree_FindByComponentId` returns the **lowest** node index, so all CS2 mutations go into the first (hidden) copy and the mounted copy renders blank. Any new panel the gameframe touches early is exposed to this.
9. **`orbs` is a *shared* record.** Appending to `interfaces/orbs.if` means the summoning orb ships to every player whether or not the feature flag is on. Gating has to be a `if_sethide` from CS2/content (the `xp_drops` precedent: not in `gameframe.enum` at all, mounted by `~xpdrops_sync_mount` behind `xpdrops_enabled`), not a build-time exclusion.
10. **Feature-flag seam is era-keyed, not per-skill.** `src/features/features.{c,h}` resolves by `[features:boot] era=` / `TORIRS_FEATURES_ERA` / `ToriRS_Features_ForCache(epoch, revision)` — there is no per-feature toggle, and no `TORIRSSERVER_*` env gate for content. GUESS: the right flag is a **varbit + `gameframe.enum`/mount gate in content**, plus a manifest `[ui:*]` key if the client needs to know.
11. **Sidebar tab-strip geometry is hand-laid.** `stone0..13` are two rows of 7 at fixed positions in 161. A 15th tab is not a matter of adding an enum row — it needs new pixel positions, and Fixed (548) / Modern (164) each have their own copy.
12. Two doc statements are **stale**: `UI_ERA_PORTING_GUIDE.md` §4 lists the if-open trigger and `if_closesub` as not landed (both are — `SS_TRIGGER_IF_OPEN` 178, `SS_OP_IF_CLOSESUB` 11005). Trust the code.

===== RECON: rs-docs-process =====
# RECON: required process for a content port in `3draster`

All paths repo-relative to `/Users/matthewevers/Documents/git_repos/3draster` unless prefixed.

---

## 1. `docs/PORTING_GUIDE.md` (1,353 lines) — section-by-section

| § | lines | content |
|---|---|---|
| header | 1–24 | Goal + the three failure modes the guide exists to prevent: (1) hardcoding content in C, (2) confusion over which pack data belongs in, (3) treating a modern client feature as un-portable. |
| **1 The map** | 26–83 | 6 repos. `OSRS-Content/osrs239-content` = **the** destination tree. `2009scape` row (L35) says *"Never copy rev-530 ids; skip bots/holiday/**Summoning**/RS2-only"*. Pipeline diagram L46–71: `cachepack unpack --assets` → tree → `cachepack pack` (**ONE baker**) → client cache (`--out`) + `server/pack`; `make -C src torirsserver-scripts` → `server/scripts/build/{script.dat,script.idx}`. Boot order L65–71: cache → content tree → script pack. |
| **2.1 The rule** | 89–103 | *"If LostCity states it in a `.rs2` proc or a config field, it is content's."* Engine keeps only: tick clock, HP/death bookkeeping, accuracy/max-hit rolls, wire encoders, collision map, dispatch. |
| **2.2 The procedure** | 104–129 | **Mandatory greps before implementing anything:** `grep -ril '<kw>' LostCity_Server/engine/src` and `.../content/scripts`. Interpretation rules: engine hits that are vars/flags don't count; **port the proc, not the field**; a trigger with no script is answered by the `_` wildcard, never by a C fallback. |
| 2.3 | 131–161 | Ownership table. CONTENT: all combat formulas, shops, dialogue, **all 14 skills + levelup + level requirements**, quests, drop tables, minigames, npc AI policy, hunt profiles, cheats. |
| **2.4 Checklist** | 163–202 | 7 items, any "yes" ⇒ write content. Item 4: *"A namespace that cannot grow is a bug, not a constraint."* Item 5: 10 sanctioned named hooks only. Item 6: engine-answers-unbound-trigger ⇒ must be a row in `enum ToriRSServerFallback` (currently 4–5, may shrink, must not grow). Item 7: re-read fallback rows after landing an opcode — blockers decay silently. |
| **2.5 Violation worklist** | 204–396 | Landed evictions + measured blocked rows; `ToriRSServer_ScriptsStaleBlockers` machine-checks opcode-shaped blockers. |
| **3.1 The model** | 402–436 | One tree, one baker, two outputs. **`content.ini`** = namespace register (id authority `cache`/`server`/`protocol`; name authority `cache`/`authored`/`derived`/`imported`; `server_base`; `cache_index`). **`fields/<type>.ini`** = field register — **default is `scope = server`, `client = drop`**. Dispositions: `client=<native>` / `client=param:<n>` / `client=drop` / `client=error` / `server=opcode:<N>` (64..255) / `ref=<ns>`. Server-only record *types* get dat2 group 128..255. Merge: `configs/all.<type>` rank 0, `server/scripts/**/configs/*` rank 1; rank 1 wins per key; duplicate at same rank = error. |
| **3.2 Decision table** | 438–459 | 5 ordered questions for new data (native → param → server field opcode → new record type → it's RuneScript). |
| 3.3 | 461–474 | Recipe: new field on existing type (5 steps, incl. `torirs_server_servercodec.c` decode table + a `fields/<name>.ini` or the test fails). |
| 3.4 | 476–486 | Recipe: new server-only record type — **prefer a `.dbtable`** before minting a namespace + 128..255 group. |
| **3.5 New client-visible content** | 488–495 | Assets go in the tree's asset dirs, ids from `pack/<ns>.pack`. **Cross-era asset conversion is `tools/port_lostcity`'s job** (LostCity `.ob2`/`.anim` → dat2); *"cachepack deliberately transcodes nothing"*. Hand-authored additions go in manifest `[extra:<name>]` sections so re-export is idempotent. |
| 3.6 | 497–531 | Phase-0 pipeline gaps (3 of 4 closed; `gameval_import.py` truncation hazard still open). |
| **4.1 The workflow** | 539–568 | **6 steps, this order:** (1) measure opcode gaps first (`ToriRSServer_ScriptsReportGaps`); (2) symbols before scripts — constants → categories → params/structs/enums/dbtables → varps → name maps; (3) configs before scripts that name them, interfaces before scripts that drive them; (4) **re-resolve every id by name; never copy ids**; (5) port scripts, `make -C src torirsserver-scripts`; (6) **verify in the real client, headlessly, and leave the check permanent**. |
| 4.2 Era translation | 569–631 | Traps: interfaces are the wall (35/1,415 names resolve) — drive the rev-230 interface, decompile CS2 before guessing; `~p_choice*` → `runclientscript_ss` (11002) + `last_slot`; varp↔varbit reclass; bare stat names collide; npc/loc categories are crawled not authored; `port/names.map` for false friends. |
| **4.3 Definition of done** | 633–640 | Boot server, perform the content in the real client (headless), state persists across logout/login, `ToriRSServer_Pack` 0 errors, **existing content untouched**, gap report shows no new silently-missing opcodes. *"It compiles" is not done.* |
| 4.4 Kronos | 642–661 | post-2009 lane. |
| **4.5 2009scape** | 665–692 | **The lane summoning would land on.** Step 1: confirm LostCity has no proc. Step 2: read `2009scape/Server/src/main/content/{global/skill,minigame,region,global/activity}/` for *policy*; cache wins for *wire*. **Step 3 (L682–683): "Skip the custom / non-OSRS skip-list … (bots, holiday events, Summoning, Fist of Guthix, While Guthix Sleeps, …)"** ← must be amended. Step 4: same §4.1 order; new opcodes go in the queue's opcode-gap log **and are implemented in the same slice** — never a one-off C hook. Step 6: never park sibling lanes. |
| 4.6 / 4.7 | 694–768 | QuestHelper lane; skills wiki-finish lane (`SKILLS_CONTENT_PORT_QUEUE.md`). |
| 5 | 770–905 | Modern client features: client already implements them; discover cache surface first; §5.4 = 19 already-surveyed interfaces. |
| 6 | 908–1199 | Phase plan 0–6. Phase 4 = content in slices. |
| **7 Guardrails** | 1203–1276 | **Never park/skip/silence another lane** (`*.rs2.skip`, `dirname.skip`, moving trees to `/tmp`, wiping configs, stripping sibling hooks). Live trees: `minigame_mta/`, `skill_construction/`. Build = `make -C src` (**not CMake**); agents sharing the repo must set `PLATFORM_OBJ_BASE`. Tests listed. `ToriRSServer_Pack --check-only` at 0 errors, **always**. In-client verification env vars. Determinism seed `0x5eed1234`. **Never `git stash`.** `pkill -f build/torirsserver` also kills `ToriRSServer_Dev`. Distrust prose counts. Blank panel = client bug until proven otherwise. **A bare name in an argument is ambiguous and the wrong answer never fails** — 4 recorded incidents; rules (a) put the name in data, (b) state the namespace in `parse_command`, (c) leave a load-time check. Docs are part of done. |
| 8 | 1280–1352 | Reading order per task type. |

### Non-negotiable rules extracted (checklist form)

1. Grep LostCity `engine/src` **and** `content/scripts` before writing anything; state in one sentence where the reference puts it (§2.2).
2. No game-facing string, id, or config-shaped constant in C (§2.4 items 2–3).
3. Port the **proc**, not the field it reads (§2.2, `CONTENT_ARCHITECTURE.md` §8.2(b)).
4. Never spell a script's name in C — dispatch a trigger (§2.4 item 5, `CONTENT_ARCHITECTURE.md` §8.6).
5. `enum ToriRSServerFallback` may shrink, must not grow (§2.4 item 6).
6. New Server VM opcode ⇒ log it in the queue's opcode-gap table **and implement it in the same slice** (§4.4/4.5 step 4).
7. Never copy reference ids; resolve every name through `pack/` (§4.1 step 4).
8. No content file carries a bare id (triage §13 bar 5).
9. Measure opcode gaps **before** starting a slice (§4.1 step 1).
10. Symbols → configs/interfaces → scripts, in that order (§4.1 steps 2–3).
11. Verify headlessly in the real client; leave a **permanent** check (§4.1 step 6, §4.3).
12. `ToriRSServer_Pack --check-only` 0 errors; existing content untouched (§4.3).
13. **Never park a sibling lane** (§7 + `.cursor/rules/no-park-sibling-content.mdc`, alwaysApply).
14. Distrust prose counts; re-measure from generated sources (§7).
15. Update the topic doc + the queue log as part of "done" (§7 last bullet).

---

## 2. `docs/SCAPE2009_CONTENT_PORT_QUEUE.md` (486 lines) — exact amendments needed

Structure: header (1–24) → shared-tree warning (26–37) → **Methodology (non-negotiable) 39–57** → **Skip list 59–71** → ownership vs Kronos (73–78) → Queue table (80–263, rows 0…35, ~150 slices, all `done`/`blocked`/`lc`/`skip`) → **Opcode gap log 265–282** → **Log 284–486**.

**Lines that must change:**

| line | current text | why |
|---|---|---|
| **65** | `| `content/global/skill/summoning/**`, Wolf Whistle | Summoning is not in OSRS |` | The whole skill + its quest are skipped here. Delete or convert to an owned row. |
| **68** | `| Evil Turnip / summoning-linked patches | Summoning ecosystem |` | Blocks the Evil Turnip farming patch (`2009scape/.../skill/farming/{PatchType,Patch,Plantable,FarmingPatch}.kt` all reference it) and `CarvedEvilTurnipListener.kt`. |
| 80–263 | Queue table | Needs new slice rows (e.g. `36a…36n`) for summoning; existing numbering is exhausted through `35`. |
| 265–282 | Opcode gap log | Any new VM op (familiar follow/entity kind, summoning points timer, BoB container) must be logged here **before** C is written. |
| 284+ | Log | One line per completed slice (`done` + `scripts N; pack 0 errors`) is the house format. |

**Sibling entries affected:**
- **Wolf Whistle** — `2009scape/Server/src/main/content/region/asgarnia/taverley/quest/WolfWhistle.java`. Named on line 65; it is the summoning-unlock quest. Not a separate queue row today.
- **Evil Turnip** — line 68. Sources: `.../skill/summoning/CarvedEvilTurnipListener.kt`, `.../summoning/familiar/EvilTurnipNPC.java`, `.../data/consumables/Consumables.java`, and the four farming files above. Un-skipping it touches the **live** `skill_farming/` tree (SCAPE2009 rows 1a–1g, all `done`) → §7 no-park rule applies in reverse: additive only.
- **Taverley / Pikkupstix** — ABSENT from all four queue docs (`grep -ri pikkupstix docs/` → 0 hits). Pikkupstix lives only under `2009scape/.../region/asgarnia/taverley/`. There is **no Taverley area row** on the SCAPE2009 queue at all; the area would be a new slice.

**Also carrying Summoning skips (must be amended in the same pass):**
- `docs/PORTING_GUIDE.md:35` — *"skip bots/holiday/Summoning/RS2-only"*.
- `docs/PORTING_GUIDE.md:683` — *"(bots, holiday events, Summoning, Fist of Guthix, …)"*.
- `docs/SKILLS_CONTENT_PORT_QUEUE.md:101` — `| Summoning / Fist of Guthix / RS2-only | not in OSRS |`.
- `docs/SKILLS_CONTENT_PORT_QUEUE.md:119–148` — Audit roster is 23 rows, "**Audit roster complete** (23/23)" at line 148 and 348. Summoning would be row **#24** and the roster's completeness claim breaks.
- KRONOS and QUESTHELPER skip lists: **no** summoning entry (`docs/KRONOS_CONTENT_PORT_QUEUE.md:56–68`, `docs/QUESTHELPER_CONTENT_PORT_QUEUE.md:85–99`) — nothing to amend there.

---

## 3. Content-architecture rules a new skill must satisfy

From `docs/CONTENT_ARCHITECTURE.md` (1,074 lines), `docs/CONTENT_PACK_PLAN.md` (899), `docs/PACK_ENTITY_SPLIT_PLAN.md` (1,203).

**Namespace / id rules**
- Three axes, never collapsed (`CONTENT_ARCHITECTURE.md` §4, L268–283): **A** id authority, **B** name authority, **C** field scope.
- `content.ini` is the register; loaders start from built-in defaults and it *overlays* them (file header, L10–13). `names = cache` requires a gameval archive and vice-versa, enforced at load.
- Server-allocatable bases (`src/content/content_register.c:63–109`): `npc 20000`, `obj 40000`, `loc 70000`, `seq 20000`, `dbtable 2048`, `dbrow 65536`, sprites/sounds/animations 20000. **Allocation is `max+1` off layer 0, recorded in `pack/<ns>.alloc`** — never a hand-picked "surely nothing is up here" constant (§3.3, §4.4, §6.7 item 1). Binding exception: rev239 runtime loc-add has a 16-bit config field, so loc 70000 cannot cross that wire; allocate a collision-checked id ≤65535 (62201 for this obelisk).
- **A server-allocated varp must be `transmit=no`** (§8.3) — a real rev-230 client has no varp of that id.
- Table sizing: anything indexed by a namespace the server can allocate into needs headroom (`TORIRSSERVER_VARP_CACHE_MAX 5705 + HEADROOM 512`); a silent bounds-check return is the failure mode (§8.3).
- `varp`/`varbit`/`varn`/`vars` share one `%name` domain; a cross-namespace collision is a load error (§4.1).

**Field rules**
- Every field of every config type is declared in `fields/<type>.ini`. Today only 8 exist: `dbrow dbtable enum loc npc obj param varp` (`OSRS-Content/osrs239-content/fields/`). **There is no `fields/inv.ini` and no `[namespace:inv]`** — this is the single blocker that has kept `shop` un-portable (PORTING_GUIDE §6 Phase 4, L1174–1184) and it will bite any summoning BoB/pouch container.
- `client = param:` projections were **retired** 2026-08-06 (§6.7 item 3): server fields now say `client = drop` + `param = <name>` for the runtime binding.

**Entity membership (`PACK_ENTITY_SPLIT_PLAN.md`)**
- `pack/<ns>.client` / `pack/<ns>.server` list **entity names, not ids** (§3.1). Five namespaces have them: `enum loc npc param varp` (`ls OSRS-Content/osrs239-content/pack/`).
- Routing rule as built (§10.1): client = named in `<ns>.client` **or** the base cache already holds (kind, id) **or** the type default; server = named in `<ns>.server` **or** (if the file is absent) the old field-presence gate.
- Three fatal/counted cells (§10.2): **(a)** record states a band field but `<ns>.server` doesn't name it ⇒ **error**; **(b)** server-only record stating a client field ⇒ counted warning; **(c)** neither file names it and the cache doesn't hold its id ⇒ **error**. Any *new* summoning npc/obj/loc lands squarely in cell (c) unless the plan adds membership lines.

**Baker / substrate rules (`CONTENT_PACK_PLAN.md`)**
- Four decisions (§0, L29–41): client cache frozen; content authored in its own schema ("the cache cannot express this" is a normal declared condition); one-time full export is the baseline; **nothing is secret**.
- Substrate rule (§0, L47–62): tree is truth for what it states, pristine cache is substrate for what it doesn't. `cachepack pack --base` copies first.
- **`dbtable`/`dbrow` are `CP_TYPE_NO_ENCODER`** (`cp_types.c:98,:101`) — cachepack refuses to write them; base records pass through. Server-side dbtables are read from text by ToriRSServer, so this only blocks *client-visible* dbrow authoring.
- Lossy encoders (§11 table): `npc obj loc seq spotanim enum mapelement` are `CP_TYPE_LOSSY`; `loc` drops ~24 opcodes at the export border. **An unknown opcode cannot be preserved opaquely** (§10, L830–836).
- New assets (§6.1, L742–758): drop the file in `assets/<type>/`, name it in `pack/<type>.pack`, reference the name from a config. `sprite`/`map`/`script` go through codecs; `map` is encrypted (owning `xteas.json`); `texture`/`dbindex` are multifile.
- Server RS2 **never enters the client cache**; client CS2 does (§6.2).
- Pure-server loop (`CONTENT_ARCHITECTURE.md` §6.7, L843–853): edit `server/scripts/…` → `make -C src torirsserver-scripts` → `make -C src torirsserver-servpack` → boot. `git status` on `configs/`, `pack/*.pack`, `pack/*.client` must stay clean (pinned by `test-server-clean`).

---

## 4. `docs/SKILLS_CONTENT_PORT_QUEUE.md` — state + effort template

- **Audit loop: complete, 23/23** (L22–23, L148, L348). Sailing omitted, Summoning skip-listed (L101). Skills and their trees: `skill_combat/` (Attack/Str/Def/HP/Ranged), `skill_prayer/ skill_magic/ skill_runecraft/ skill_crafting/ skill_mining/ skill_smithing/ skill_fishing/ skill_cooking/ skill_firemaking/ skill_woodcutting/ skill_agility/ skill_herblore/ skill_thieving/ skill_fletching/ skill_slayer/ skill_farming/ skill_construction/ skill_hunter/`.
- **Port loop: active.** Finish queue = 153 rows (L156–311). Selection is fixed (L46–52): lowest `#` pending, deps-first, mark `in_progress` immediately.
- **Most recent completions** (Log, L350–375): #1 specials → #3 → #5/#6/#7/#9/#10/#11/#12 → #17 → #13/#14/#15/#16 → #21 → #18 → #20/#23/#26 → #27 → #22 → #24 → #28 → **#29 Prayer cape (last `done`)**; next = #37 Runecraft. These are *finish slices*, not whole skills.
- **Opcode gap log** (L313–321) is a required artifact: `inv_dropitem_delayed` done; PvP secondary-player dialect blocked.

### Template for effort estimation — the last whole-skill ports (both on the SCAPE2009 lane)

`skill_hunter/` — the most recently completed whole skill (SCAPE2009 rows 2a–2at, ~40 queue ticks):

```
OSRS-Content/osrs239-content/server/scripts/skill_hunter/   38 files, 5,589 lines
  scripts/  16 × .rs2      pitfall 333, net_trap 332, common_trail 320, polar_trail 288,
                           desert_jungle_trail 252, bird_snare 229, deadfall 228,
                           box_trap 213, rabbit_snare 206, magic_box 196, falconry 188,
                           impling 176, butterfly 115, imp_box 90, hunter_traps 75, rabbit_hole 62
  configs/  10 × .dbrow    impling_loot 1060, implings 143, pitfall 121, bird_snare 63,
                           net_trap 55, butterflies 51, deadfall 31, box_trap 25,
                           rabbit_snare 23, falconry 23
            10 × .dbtable  7–16 lines each
             1 × .varp     hunter_runtime.varp 402
             1 × .constant hunter.constant 188
```

`skill_farming/` — 31 files / 4,870 lines (12 `.rs2`, 8 `.dbtable`, 8 `.dbrow`, 2 `.varp`, 1 `.constant`), SCAPE2009 rows 1a–1g (~12 ticks).
`skill_slayer/` — 23 files / 1,590 lines. `skill_construction/` — 8 files / 424 lines (deliberately minimal, POH 4a/4b).

**Shape to copy:** exactly two subdirectories, `configs/` and `scripts/`; state carried in one `*_runtime.varp` + one `*.constant`; every table is a `.dbtable` + `.dbrow` pair; one `::debugproc` per slice for headless proof; one queue log line per slice recording `scripts N; pack 0 errors`.

**Where summoning is off-template (biggest estimation risk):** neither hunter nor farming needed a single new *client asset* — every npc/obj/loc already existed in the osrs239 cache. Summoning has none, so it additionally needs the §3.5 asset path (models, seqs, spotanims, interface, sprites) + membership lines + `pack/<ns>.alloc` entries, none of which appear in either template's footprint.

---

## 5. Verification surface

**Content validation**
```sh
make -C src torirsserver-pack                      # builds src/build/ToriRSServer_Pack (validator only)
src/build/ToriRSServer_Pack --check-only           # MUST be 0 errors  (PORTING_GUIDE §7)
```
`src/makefile:1730–1745`. `torirs_server_pack.c:1–26` documents that `--cache-out` is deleted; the tool validates only.

**Script pack**
```sh
make -C src torirsserver-scripts     # tools/ss_allocate.py --tree ... ; then sscompile --src server/scripts
                                #   --out server/scripts/build --pack pack --pack configs   (makefile:1616–1622)
make -C src torirsserver-servpack    # cachepack pack --src OSRS-Content/osrs239-content --server-only (1631–1634)
make -C src torirsserver-cache       # ONLY for client-visible edits: cachepack pack --base --out --rev osrs239
                                #   --assets --binary --gamevals, then torirsserver-cache-check (1676–1717)
```

**Aggregate content gate — the one to run**
```sh
make -C src test-content        # makefile:1861 — runs, in order:
  test-content-register  test-servercodec  test-ss-symbols
  torirsserver-scripts  torirsserver-servpack  test-membership  torirsserver-pack
  test-server-clean  test-port          then ToriRSServer_Pack itself
```
- `test-membership` (1907–1911): `cachepack membership --src <tree> --check-only`.
- `test-server-clean` (1876–1885): after the full server pipeline, `git status` under `configs/`, `pack/*.pack`, `pack/*.client` must be empty.
- `test-port` (1992–2016): `ss_unresolved.py --check`, `port_name_diff.py --check`, `port_constant_diff.py --check`, `port_category_crawl.py --check -v`, `port_config_diff.py --check -v`, `port_vars_diff.py --check`, `cs2_varp_audit.py --check`, `port_names_diff.py --check`, `port_droptables_check.py --check -v`, `ladder_import.py --check`, `bank_import.py --check`.

**Server/VM tests**
```sh
make -C src test-ToriRSServer           # builds + ./src/build/torirsserver --selftest   (1755)
make -C src test-torirsserver-coverage  # gen_opcode_coverage.py --check — fails on a stale table (1765)
make -C src test-ss-provider       # trigger lookup order
make -C src test-db  test-torirsserver-param  test-torirsserver-loc  test-torirsserver-npc
make -C src test-torirsserver-embed     # in-process client+server, two byte buffers
make -C src test-collision-doors
make -C 3rd/rscache test           # cache fidelity; read 3rd/rscache/EXCEPTIONS.md FIRST
```

**Headless in-client verification** (PORTING_GUIDE §7 L1234–1237)
```sh
SDL_VIDEODRIVER=dummy TORIRSSERVER_VERBOSE=1 \
TORIRS_SIM_CLICK_AT=... | TORIRS_SIM_MOUSE_CLICK=... TORIRS_EXIT_BMP=out.bmp  src/build/torirs ...
```
In-loop variants exist and are documented at their `getenv` sites in `src/main.c`: `TORIRS_SIM_HOOK` (788–1030), `TORIRS_SIM_RUNSCRIPT` (1063–1090), `TORIRS_SIM_TYPE` (1157–1169), `TORIRS_SIM_HOTKEY` (1210–1233), `TORIRS_SIM_DRAG` (864–880), `TORIRS_SIM_WHEEL` (954–966), `TORIRS_SIM_SETHIDE`, `TORIRS_SIM_SOUND`, plus `TORIRS_BMP_SERIES`. Blank-panel triage order: `TORIRS_DUMP_TREE_EXIT=1` → `TORIRS_DUMP_BOUNDS` → `TORIRS_DUMP_SETSIZE`.
Full stack against a real client: `./run-osrs239.sh` (ToriRSServer + JS5 on 43594, jav_config on 8080, RuneLite).
Memory debugging: `MallocScribble`, **not** ASAN.

**Build**
`make -C src` (plain make; CMake tree deprecated). Parallel agents: set `PLATFORM_OBJ_BASE`. `make -C src lane-check-all`.

---

## 6. Binding project rules

**Resolved governance note:** `CLAUDE.md` is intentionally absent and is not a prerequisite.
The user explicitly rejected restoring an agent-specific file. Its stale citations were removed;
the applicable rules remain in `PORTING_GUIDE.md`, the queue documents, and
`.cursor/rules/no-park-sibling-content.mdc`.

**`.cursor/rules/` — 2 files, both `alwaysApply: true`:**
- `no-park-sibling-content.mdc` — forbids `*.skip` directories/files, moving trees to `/tmp` or `.mta_restore_backup`, deleting/emptying another lane's `configs/`/`scripts/`, stripping sibling hooks from shared spell/combat/skill files, and *instructing* a park in summaries/todos/loop prompts. Live trees named: `skill_construction/`, `minigame_mta/`. (Note: a `.mta_restore_backup/` directory exists at repo root — evidence this has been violated before.)
- `c-assert-invariants.mdc` (globs `{src,src2,tools,test}/**/*.{c,h}`) — assert programming errors, never silently return; do not combine programming-error checks with runtime checks; allocation failure / lookup miss / optional param are handled explicitly, not asserted; convert silent-return guards to asserts in any function you touch.

`.claude/settings.local.json` contains only a Bash/Read permission allowlist — no behavioural rules.

---

## 7. CHECKLIST the final plan must satisfy

**Doc amendments (do these first — the plan is illegal under the current docs)**
1. `docs/SCAPE2009_CONTENT_PORT_QUEUE.md:65` — remove/convert the `summoning/**`, Wolf Whistle skip row.
2. `docs/SCAPE2009_CONTENT_PORT_QUEUE.md:68` — remove/convert the Evil Turnip row.
3. `docs/PORTING_GUIDE.md:35` and `:683` — drop "Summoning" from both skip clauses.
4. `docs/SKILLS_CONTENT_PORT_QUEUE.md:101` — drop the Summoning skip row; decide whether a #24 Audit roster row is added (and correct the "23/23 complete" claims at L148/L348 if so).
5. Add new SCAPE2009 queue rows (`36…`) + an entry in that file's **Opcode gap log** (L265–282) for every new VM op, **before** any C is written.
6. ~~Restore `CLAUDE.md`~~ — resolved by explicit user decision: do not restore it; remove stale
   citations instead.
7. Write a topic doc (`docs/SUMMONING*.md`) as part of "done", per §7 last bullet, and log each slice in the queue.

**Method (per §2 / §4.1)**
8. For each behaviour: grep `LostCity_Server/{engine/src,content/scripts}` and state in one sentence where the reference puts it. Summoning postdates rev 254 ⇒ expect "LC has none" — record it, don't skip the grep.
9. Read `2009scape/Server/src/main/content/global/skill/summoning/**` (+ `familiar/`, `pet/`, `SummoningPouch.java`, `CarvedEvilTurnipListener.kt`, `region/asgarnia/taverley/quest/WolfWhistle.java`) for **policy only**; cache wins for wire/ids.
10. Measure the opcode gap list **before** starting (`ToriRSServer_ScriptsReportGaps` at load; `torirs_server_opcode_coverage.gen.h`). Do not start a slice whose gaps aren't listed.
11. Order within each slice: constants → categories → params/structs/enums/dbtables → varps → name maps → configs → interface → scripts.
12. Re-resolve every id by name through `pack/`; **no bare id in any content file**; no rev-530 id copied.
13. Zero game-facing strings / ids / config constants in C; zero script names in C; no new `enum ToriRSServerFallback` row; no one-off C↔script hook.
14. If a name won't resolve or a namespace won't grow, fix the namespace policy — do not route the rule back into C.
15. Beware bare names in argument position (§7 L1248–1273): prefer the name in data (`.enum` with `inputtype=`, a `.dbtable` column); otherwise state the namespace in `parse_command` from `engine.rs2`'s typed signature, plus a load-time check.

**Architecture / pack**
16. New namespaces (e.g. `familiar`, if one is minted) ⇒ a `[namespace:*]` block in `content.ini` + `fields/<name>.ini` + a decode struct; prefer a **`.dbtable`** before minting anything (§3.4).
17. New server fields on npc/obj/loc ⇒ `fields/<type>.ini` row with `server = opcode:<N>` (64..255) + `torirs_server_servercodec.c` decode table + a `fields/<type>.ini` entry, or `ToriRSServer_ServerCodecTest` fails.
18. Any new npc/obj/loc/enum/param/varp record ⇒ add it to `pack/<ns>.client` and/or `pack/<ns>.server`, or it lands in split-plan **cell (c)** and `cachepack pack` returns non-zero.
19. Server-allocated ids come from `pack/<ns>.alloc` via `tools/ss_allocate.py` (bases: npc 20000, obj 40000, loc 70000, seq 20000, dbtable 2048, dbrow 65536). Never a hand-picked constant. For locs sent through rev239 `LOC_ADD_CHANGE_V2`, the 70000 base is invalid because the config id is 16-bit; use a collision-checked allocation ≤65535.
20. Every server varp: `transmit=no`. Check table sizing headroom for anything indexed by an allocatable namespace.
21. Client-visible assets: file in `assets/<type>/`, id from `pack/<type>.pack`, hand-authored additions in manifest `[extra:<name>]` sections; transcode via `tools/port_lostcity`, **not** cachepack. `make -C src torirsserver-cache` is required for any client-visible edit, and then **both** the world and JS5 must point at the baked cache.
22. Server RS2 must never enter the client cache; `test-server-clean` must stay green (`git status` clean under `configs/`, `pack/*.pack`, `pack/*.client`).
23. Container work (pouches / Beast of Burden) will hit the missing `fields/inv.ini` + `[namespace:inv]` — the same blocker that stalls `shop`. Plan it explicitly or route it.

**Feature flag**
24. **ABSENT: there is no content-level feature-flag mechanism today.** `src/features/` is the *client-behaviour-per-era* seam (`ToriRS_FeatureEra` LOSTCITY/OSRS/SERVER_ROUTED), not a content gate; the mock server's env vars (`TORIRSSERVER_*`, `torirs_server_boot.c:43–104`) are paths/verbosity, not feature toggles; no `enabled`/`feature` key exists in `content.ini` or any `.constant`. The plan must **invent** the flag and say which layer owns it. GUESS at the cheapest legal shape: a `^summoning_enabled` in a `summoning.constant` read by every entry-point script (pure content, no C, no new namespace) — an env var or a C branch would be a §2.4-item-2/3 violation.

**Distinct ported folder**
25. **ABSENT: there is no existing "ported content" folder convention.** Every tree under `server/scripts/` is subject-named (`skill_*`, `quests/`, `areas/`, `minigames/`, `interface_*`); provenance is recorded in queue Notes and `port/*.map`, not in directory names. A new marked folder (e.g. `skill_summoning/` with a README stating provenance, or a `ported_2009scape/` prefix) is a **new convention** the plan must justify against §7's "live trees must stay as normal paths" and against `sscompile --src` walking `server/scripts` recursively (`ssc_main.c:6`) — a directory name has no meaning to any tool, so the marker must be a README/comment, not a path the toolchain interprets.

**Verification / done**
26. `ToriRSServer_Pack --check-only` 0 errors; `make -C src test-content` green (incl. `test-membership`, `test-server-clean`, `test-port`); `make -C src test-ToriRSServer` selftest green; `test-torirsserver-coverage` green if any opcode landed.
27. Verified in the real client headlessly (SDL dummy + `TORIRS_SIM_*` + `TORIRS_EXIT_BMP`), with a `::` debugproc per slice, and the check left **permanent** (a `ToriRSServer_Pack` rule, a test, or a selftest stanza).
28. State persists across logout/login. No new silently-missing opcodes in the gap report.
29. Existing content untouched — no `*.skip`, no moved trees, no stripped sibling hooks; `skill_farming/`, `skill_construction/`, `minigame_mta/` stay live paths.
30. Prove the assertions can fail (mutate the impl / unbind the script) before claiming a behaviour is covered — PORTING_GUIDE §6 Phase 3, `opobj` precedent.
31. Never `git stash`; never bare `pkill -f build/torirsserver`; set `PLATFORM_OBJ_BASE` if another agent shares the repo.

---

## RISKS / UNKNOWNS

1. **No feature-flag mechanism exists** (finding 24). Any C-side flag violates §2.4 items 2–3; a content-side `.constant` gate is untested at this scale and cannot gate *asset presence* in a baked cache — a summoning interface baked into `cache.osrs239.baked` ships whether the flag is on or not.
2. **No "ported content" folder convention exists** (finding 25). Whatever is chosen is invisible to `sscompile`/`cachepack`/`ToriRSServer`, so the marker is documentation only and can drift silently — the exact failure mode §2.4 item 7 is written against.
3. **Off-template asset work.** Both effort templates (hunter, farming) needed **zero** new client assets. Summoning needs models/seqs/spotanims/sprites/an interface. `tools/port_lostcity` is documented as the cross-era transcoder for **dat1 `.ob2`/`.anim` → dat2**; 2009scape's cache is already **dat2** (`2009scape/Server/data/cache/main_file_cache.dat2`), so the rev530→osrs239 dat2→dat2 import path is **unverified and possibly ABSENT**. Not measured in this recon.
4. **Cell-(c) blast radius unmeasured.** Every new npc/obj/loc record needs `pack/<ns>.{client,server}` lines or `cachepack pack` returns non-zero. Only 5 namespaces have membership files today; `spotanim`, `seq`, `struct`, `interface` do **not**, and §8.3 says that absence means "everything there is the cache's" — adding a server-authored seq/spotanim may require creating the pair, which is new ground.
5. **`fields/inv.ini` / `[namespace:inv]` gap** blocks any container work (pouches, BoB, shop). It has blocked `shop` since 2026-08-02 and is not scheduled.
6. **`dbtable`/`dbrow` have no client encoder** (`cp_types.c:98,:101`). Fine for server-side tables; fatal if summoning needs a client-readable dbrow (e.g. a CS2-driven familiar list).
7. **Interfaces are the wall.** The osrs239 cache has no summoning orb/panel; §4.2 says port per-interface on demand by *driving an existing rev-230 interface* — but there is nothing to drive. Authoring a new IF3 interface + CS2 is not something any existing content slice has done; the 19-interface survey (§5.4) covers only interfaces the cache already carries.
8. **Doc-amendment blast radius.** Un-skipping Evil Turnip touches the live `skill_farming/` tree (rows 1a–1g `done`); un-skipping Wolf Whistle creates a Taverley area with no existing queue row. Both risk a §7 violation by a parallel agent that reads the old skip list.
9. ~~Missing `CLAUDE.md` citation~~ — resolved. It is intentionally absent and stale citations
   were removed by explicit user direction.
10. **Unmeasured in this recon:** whether summoning gamevals/varbits exist in the osrs239 cache (only 5 `summon`-matching obj names and 5 npc names, plus `delrith_seen_summoning_cutscene` varbit 2569 and `league_task_cerberus_summoned` 10923 — all unrelated); the true opcode gap for familiar entities (a familiar is neither `SSVM_ENT_NPC` nor a player and may need a new entity kind, which is the shape that blocked `opobj` for months); the rev-530 → osrs239 name-resolution rate for summoning subjects.
11. **Numbers in this report taken from prose in the docs** (fallback-row counts 4 vs 5, opcode coverage 260/401, "23/23 skills") — §7 says distrust them; re-measure from `torirs_server_opcode_coverage.gen.h` and boot output under `TORIRSSERVER_VERBOSE=1` before planning against any of them.
