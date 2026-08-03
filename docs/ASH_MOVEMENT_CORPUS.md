# Mod Ash (JagexAsh) corpus — pathfinding, movement, reach, interaction

Authority note: statements in this document are from Mod Ash (Jagex OSRS senior content
developer, 2004–2025) and are treated as **higher authority than any server codebase**,
including LostCity. Where a LostCity file cites a tweet, the file:line is given so you can
see what behaviour the quote was used to justify.

## 0. Provenance and method

Sources mined:

1. **LostCity_Server** (`engine/` + `content/`), plus the sibling forks
   `2004scapeServer`, `Engine-TS`, `RS2004Server`, and `2009scape`.
   Grepped for `JagexAsh`, `x.com/JagexAsh`, `twitter.com/JagexAsh`, `fxtwitter`,
   `vxtwitter`, `fixupx`, `nitter`, `Ash said`, `per Ash`.
   **73 distinct tweet IDs** are cited across those trees; the forks cite the same set
   (they are downstream of LostCity) plus three non-movement extras.
2. **api.fxtwitter.com** — all 73 cited tweets resolved with full text, plus their
   parent (question) tweets. Mod Ash's account still resolves through fxtwitter.
3. **Bulk tweet archive**, 332,045 JagexAsh tweets, 2013-08-30 → 2021-08-30.
4. **devtrackers.gg/osrs/twitter**, full crawl of 4,196 listing pages →
   78,076 JagexAsh tweets, 2021-09-02 → 2023-07-12.

Combined coverage is effectively continuous from Aug 2013 to Jul 2023, plus every
LostCity-cited tweet through Aug 2024.

**Everything below is CONFIRMED VERBATIM.** All 73 code-cited tweets were recovered in
full, so there are no paraphrase-only entries in the code-citation set. A sample of the
archive-sourced quotes was re-verified live against fxtwitter and matched exactly. The
one gap is listed in §15.

Quote convention: `> Ash:` is verbatim tweet text (leading @-handles stripped; `|` marks
an original line break). `Q:` is the question he was answering, where recoverable.
URLs are canonical `https://x.com/JagexAsh/status/<id>`.

---

## 1. Routefinding architecture, and the 2013 move to the server

This is the single most load-bearing fact: **since late 2013 the server does all
routefinding**; before that the client did it.

- **[1132253909869891584](https://x.com/JagexAsh/status/1132253909869891584)** (2019-05-25)
  > Ash: Since about the end of 2013, the server does pathfinding. The client wouldn't be able to do it in that situation anyway, since it's an unloaded chunk.

- **[1094312677915197440](https://x.com/JagexAsh/status/1094312677915197440)** (2019-02-09)
  > Ash: Yeah, back when PC was launched, routefinding was done client-side rather than server-side, and it couldn't handle blocking NPCs like those. While the engine now does routing server-side, and could handle them fine, we asked that team not to change the in-game behaviour.

- **[521588626116063232](https://x.com/JagexAsh/status/521588626116063232)** (2014-10-13)
  > Ash: When routefinding was clientside, it couldn't handle NPCs. We moved routefinding serverside, but preserved the behaviour.

- **[821759067802705920](https://x.com/JagexAsh/status/821759067802705920)** (2017-01-18)
  > Ash: Rubber-banding used to be a routefinding issue. To solve it, the engine team moved the entire routing system to the server.

- **[1306140247634448385](https://x.com/JagexAsh/status/1306140247634448385)** (2020-09-16)
  > Ash: Prior to that, routefinding was performed client-side, meaning that if you were affected by lag at the time, your routefinding was out of synch with your actual position, leading to that.

- **[466635085651775488](https://x.com/JagexAsh/status/466635085651775488)** (2014-05-14)
  > Ash: Whew. That'd be interesting, since the rubber-banding is caused by race conditions. Could be a fun challenge for the Engine guys.

- **[1143272005841674242](https://x.com/JagexAsh/status/1143272005841674242)** (2019-06-24) — the client is not trusted:
  > Ash: Mostly the server only accepts certain types of input from the client, and it does the processing at the server end. For example, if your client tells the server you want to walk somewhere, the server does the line-of-walk checks rather than trusting the client to do them.

- **[1634723648865959936](https://x.com/JagexAsh/status/1634723648865959936)** (2023-03-12) — confirms where routefinding lives:
  > Ash: New engine features for enabling navigation to NPCs that cannot be reached for line-of-walk. Which did need engine-level work, since that's where routefinding is handled :( Also, by getting it as a flexible feature there, it should be expandable in future rather than... (1/2)

---

## 2. One global routefinding algorithm; content devs cannot override it

Repeatedly and unambiguously: **there is exactly one pathing system, it is inside the
engine, it is used everywhere, and content developers cannot read or tune it.**

- **[466156765550497792](https://x.com/JagexAsh/status/466156765550497792)** (2014-05-13)
  > Ash: There's only one pathing system in the game.

- **[480295033766416384](https://x.com/JagexAsh/status/480295033766416384)** (2014-06-21)
  > Ash: OSRS has only one pathing system, used everywhere. Could you specify what exactly you'd like to happen, and what happens instead?

- **[573892810312548352](https://x.com/JagexAsh/status/573892810312548352)** (2015-03-06)
  > Ash: All NPCs use the same routefinding algorithm. If Ian makes it smarter, that'll potentially break a *lot* of popular safespots.

- **[732602430651826177](https://x.com/JagexAsh/status/732602430651826177)** (2016-05-17)
  > Ash: The route-finding algorithm in the game engine is used globally. If we change it for that place, it may affect many safe-spots.

- **[1328842654529048577](https://x.com/JagexAsh/status/1328842654529048577)** (2020-11-17)
  > Ash: The game doesn't have an alternate pathing algorithm that's just used in that cave; it'd use the same algorithm as other terrain. But that particular exit is under review.

- **[1461687803344039949](https://x.com/JagexAsh/status/1461687803344039949)** (2021-11-19), asked specifically about CoX skeletal mystic line-of-walk:
  > Ash: There isn't a separate routefinding algorithm just for use in that area. I'm afraid I wouldn't be able to see exactly what the game engine does under the hood.

- **[1280491086729940993](https://x.com/JagexAsh/status/1280491086729940993)** (2020-07-07)
  > Ash: The game doesn't have different routefinding in just that area, and probably can't [...]

- **[1321498000997441542](https://x.com/JagexAsh/status/1321498000997441542)** (2020-10-28)
  > Ash: [...] the routefinding works the same way as in other areas.

- **[730786583486578688](https://x.com/JagexAsh/status/730786583486578688)** (2016-05-12)
  > Ash: The routefinding system used in the game is the same for members and F2P.

- **[966647597749415936](https://x.com/JagexAsh/status/966647597749415936)** (2018-02-22)
  > Ash: [...] there isn't a specific pathing system just for Hunter.

Content-side visibility and control:

- **[1125305760387350528](https://x.com/JagexAsh/status/1125305760387350528)** (2019-05-06)
  > Ash: I'm afraid not; the engine's path-finding code is not something I'd be able to read.
- **[1155068542045306880](https://x.com/JagexAsh/status/1155068542045306880)** (2019-07-27)
  > Ash: Sorry, no, the pathing code would be inside the engine where I can't read it.
- **[1259182257950072832](https://x.com/JagexAsh/status/1259182257950072832)** (2020-05-09)
  > Ash: I'm afraid I'm not able to read the engine code to see exactly how pathing's done.
- **[1055394409200713728](https://x.com/JagexAsh/status/1055394409200713728)** (2018-10-25)
  > Ash: Yeah, sadly pathing isn't something where the engine exposed a lot of control for us to override it.
- **[951448791512698880](https://x.com/JagexAsh/status/951448791512698880)** (2018-01-11)
  > Ash: We don't have a lot of fine control over how the engine routefinds you around specific scenery, tbh.
- **[928236300200759296](https://x.com/JagexAsh/status/928236300200759296)** (2017-11-08)
  > Ash: We don't have a huge amount of control for overriding routefinding in the event that someone's misclicked.
- **[1154897802243260416](https://x.com/JagexAsh/status/1154897802243260416)** (2019-07-26)
  > Ash: I'm afraid the engine doesn't offer a lot of scope for tweaking details of routefinding.
- **[802178715564343297](https://x.com/JagexAsh/status/802178715564343297)** (2016-11-25)
  > Ash: Routefinding is inside the game engine; if we did get that changed, though, it would likely affect a LOT of NPC behaviours.
- **[687565832382210050](https://x.com/JagexAsh/status/687565832382210050)** (2016-01-14)
  > Ash: Getting the engine's routefinding reprogrammed would have a lot of potential knock-on effects that would not be appreciated.
- **[1050020226207764480](https://x.com/JagexAsh/status/1050020226207764480)** (2018-10-10)
  > Ash: [...] if the engine team did change the core algorithms, that can lead to unexpected side-effects in a lot of places, and they may not be popular.

Range/complexity limits of routefinding:

- **[867658596334673920](https://x.com/JagexAsh/status/867658596334673920)** (2017-05-25)
  > Ash: The game's not going to be able to routefind you the kind of distances that the map shows, sorry. And no, it doesn't do free teleports.
- **[1374070452155523073](https://x.com/JagexAsh/status/1374070452155523073)** (2021-03-22)
  > Ash: The game's not likely to be able to offer route-finding over really long distances [...]
- **[1085925038598049794](https://x.com/JagexAsh/status/1085925038598049794)** (2019-01-17)
  > Ash: No, the door can't automatically route-find you like you're wanting. Sorry.
- **[1388135891932221443](https://x.com/JagexAsh/status/1388135891932221443)** (2021-04-30)
  > Ash: The route-finding algorithm may not be randomising your direction in such situations. It doesn't sound like it'd benefit from being different, even.
- **[1597276786018381824](https://x.com/JagexAsh/status/1597276786018381824)** (2022-11-28) — his own guess at axis order:
  > Ash: Their code is the same each time, though I'd wonder if the orientation of the room affects how they navigate - maybe they get the X-coordinate right before the Z-coordinate, and this gives the behaviour you describe. (I can't see what the engine's doing under the hood.)
- **[1291303640871952385](https://x.com/JagexAsh/status/1291303640871952385)** (2020-08-06) — RS3 comparison:
  > Ash: [...] Their language has some differences too, e.g. 2D data structures that we haven't got, and more features that can help with routefinding to scenery.

Cost:

- **[1617883305269133314](https://x.com/JagexAsh/status/1617883305269133314)** (2023-01-24)
  > Ash: It's the kind of thing we'd be glad to roll out more widely, tbh. But for pathing we may be limited for really widespread roll-out since we've been warned that it adds quite a bit more server load.

Regressions caused by engine routefinding changes (useful as behavioural negatives):

- **[1468945975100289024](https://x.com/JagexAsh/status/1468945975100289024)** (2021-12-09)
  > Ash: The two engine-related issues involved NPC routefinding and skilling process rates. The engine team has set out to roll back the changes that can wait for another week, but could not roll back everything because some changes were needed now.
- **[1517811736958619650](https://x.com/JagexAsh/status/1517811736958619650)** (2022-04-23)
  > Ash: There was a problem after the initial update where if you used an item on a piece of scenery or an NPC, you'd walk towards it in a straight line and get stuck on any obstacles, rather than pathing to it properly. [...]

---

## 3. Reach, adjacency and diagonals

### 3.1 "Adjacent" excludes diagonals

The definitive statement:

- **[1659582689119174658](https://x.com/JagexAsh/status/1659582689119174658)** (2023-05-19)
  Q (@Lone_Identity): *Why are most melee attacks unable to hit diagonally? Do you think it would be game-breaking if this were to be changed?*
  > Ash: "an adjacent tile" is defined to NOT include diagonals, for most purposes, and melee's based on that. I'd have expected safe-spots to be extremely heavily impacted if we changed it. To some that'd probably be called game-breaking.

- **[476820561234915328](https://x.com/JagexAsh/status/476820561234915328)** (2014-06-11)
  > Ash: Interesting. You should need to be adjacent with line-of-walk, same as for melee. But maybe not.

- **[1135921963254161410](https://x.com/JagexAsh/status/1135921963254161410)** (2019-06-04)
  > Ash: Almost nothing allows diagonal interaction with scenery like that.

- **[1025330403526303746](https://x.com/JagexAsh/status/1025330403526303746)** (2018-08-03)
  > Ash: Melee isn't really supposed to do that kind of thing, except for halberds that can work diagonally.

### 3.2 Diagonal *movement* is allowed; diagonal *interaction* generally is not

- **[1463902690560331777](https://x.com/JagexAsh/status/1463902690560331777)** (2021-11-25)
  > Ash: Diagonal walk is permitted, where line-of-walk exists [...]
- **[1271874752886554627](https://x.com/JagexAsh/status/1271874752886554627)** (2020-06-13)
  > Ash: Diagonal is permitted, yeah. No worries.
- **[1141023715871920134](https://x.com/JagexAsh/status/1141023715871920134)** (2019-06-18) — corner-cutting is explicitly detected:
  > Ash: The game might have thought you moved diagonally through the block that was just east of you - it goes to some effort to detect that kind of thing.

LostCity implements exactly this ordering (diagonal first, then E/W, then N/S, then
stop), and cites Ash for the "give up" branch:

```663:678:engine/src/engine/entity/PathingEntity.ts
        // Move diagonal
        if (this.width === 1 && canTravel(this.level, this.x, this.z, dx, dz, this.width, extraFlag, collisionStrategy)) {
            return [dx, dz];
        }

        // Move E/W
        if (dx != 0 && canTravel(this.level, this.x, this.z, dx, 0, this.width, extraFlag, collisionStrategy)) {
            return [dx, 0];
        }

        // Move N/S
        if (dz != 0 && canTravel(this.level, this.x, this.z, 0, dz, this.width, extraFlag, collisionStrategy)) {
            return [0, dz];
        }
        // https://x.com/JagexAsh/status/1727609489954664502
        return [0, 0];
```
(`LostCity_Server/engine/src/engine/entity/PathingEntity.ts:677`)

### 3.3 Why NPCs appear to melee diagonally

- **[1357788909921587207](https://x.com/JagexAsh/status/1357788909921587207)** (2021-02-05)
  Q: *why can metallic dragons, corp, KQ, shamans melee diagonally but dark beasts / basilisk knights can't?*
  > Ash: NPCs with ranged attacks will often consider themselves to be ranging you (which is permitted on diagonals) even if what they perform from that distance is a melee attack. However not all are written to act that way.
- **[1308026526462545920](https://x.com/JagexAsh/status/1308026526462545920)** (2020-09-21)
  > Ash: If a developer wrote the melee attack as though it were a ranged attack, but performed from a distance of 1 tile, that would include diagonals. It's not the most standard way of writing a melee attack, though.
- **[1522606535914004480](https://x.com/JagexAsh/status/1522606535914004480)** (2022-05-06)
  > Ash: [...] The dragons are likely going through ranged code, which allows diagonals.
- **[1404401564840861698](https://x.com/JagexAsh/status/1404401564840861698)** (2021-06-14)
  > Ash: NPCs have often struggled with diagonals like that. I doubt it's intentional - it'd be an odd way of implementing it, if it were.
- **[729962277370236928](https://x.com/JagexAsh/status/729962277370236928)** (2016-05-10)
  > Ash: The last change we made in the new area was to make the demonic monkeys less likely to get stuck diagonally from you.

### 3.4 "You can't reach that" / picking the approach tile

- **[1115516301974360064](https://x.com/JagexAsh/status/1115516301974360064)** (2019-04-09)
  > Ash: So if you're trying to melee someone who's not adjacent to you, the game would just say "you can't reach that"? Are you sure?
- **[1251596563559915521](https://x.com/JagexAsh/status/1251596563559915521)** (2020-04-18) — confirms the exact message string exists as a distinct restriction:
  > Ash: And what message is it giving you when you try? It's not the "You can't quite reach the tree!" restriction, is it?
- **[770370354762088448](https://x.com/JagexAsh/status/770370354762088448)** (2016-08-29) — names the mechanism:
  > Ash: The game's pick-an-adjacent-tile system is not particularly flexible. It looks like you have line-of-walk to him there anyway.
- **[487002330530078720](https://x.com/JagexAsh/status/487002330530078720)** (2014-07-09) — the classic unreachable-interior-of-a-multi-LOC case:
  > Ash: Since Farming's launch. That patch consists of ~25 identical LOCs. If you click on a middle one, you can't reach it.
- **[555815982586155010](https://x.com/JagexAsh/status/555815982586155010)** (2015-01-15)
  > Ash: The game does not try to routefind you past the monkeys to the trapdoor. You must get close enough to it manually.
- **[825033223688318976](https://x.com/JagexAsh/status/825033223688318976)** (2017-01-27)
  > Ash: Not all parts of the scenery are reachable from where you're standing, by the sound of it.
- **[872866346383683585](https://x.com/JagexAsh/status/872866346383683585)** (2017-06-08) — wall-shaped locs:
  > Ash: The game's definition of "in the adjacent tile" is a little awkward for wall-shaped pieces.
- **[1541478148172390402](https://x.com/JagexAsh/status/1541478148172390402)** (2022-06-27) — if you're already adjacent, no movement is requested:
  > Ash: Interacting with scenery does not tend to trigger movement restrictions if you're adjacent to the scenery already.
- **[841275358968201216](https://x.com/JagexAsh/status/841275358968201216)** (2017-03-13)
  > Ash: So you're saying you click on an NPC, and can't reach it, and it promptly runs further away from you?

### 3.5 Interaction range caps

- **[1541484420347076614](https://x.com/JagexAsh/status/1541484420347076614)** (2022-06-27)
  > Ash: The engine currently supports up to 10 tiles for such interactions. I thought telegrab was already at that cap, actually.
- **[938093338888167426](https://x.com/JagexAsh/status/938093338888167426)** (2017-12-05)
  > Ash: We're actually limited to 10 tiles, I think, without engine changes. [...]
- **[1368504345277399042](https://x.com/JagexAsh/status/1368504345277399042)** (2021-03-07)
  > Ash: Making one's attack range depend on where one's standing is feasible, up to the engine's max of 10. So this could help some weapons, but not all.
- **[1095667167813660673](https://x.com/JagexAsh/status/1095667167813660673)** (2019-02-13)
  > Ash: Yes. Also it won't work beyond 10 tiles.
- **[599950957808594944](https://x.com/JagexAsh/status/599950957808594944)** (2015-05-17) / **[1040975909535268865](https://x.com/JagexAsh/status/1040975909535268865)** (2018-09-15) — separate 15-tile *visibility* limit:
  > Ash: You can't see players more than 15 tiles away, however big the screen gets.
  > Ash: Clients can only see you within 15 tiles, outside of special places like raiding dungeons [...]

---

## 4. `op` vs `ap` — the interaction trigger model

### 4.1 The canonical definition

- **[1095653912743460865](https://x.com/JagexAsh/status/1095653912743460865)** (2019-02-13)
  > Ash: ap triggers can be executed at a distance, requiring line-of-sight. op triggers must be executed from an adjacent tile, requiring line-of-walk. | Basically, one's like ranging, the other's like melee.

- **[1432312444400119808](https://x.com/JagexAsh/status/1432312444400119808)** (2021-08-30)
  Q (@ZenKris21): *Why do you define opnpc and apnpc both for NPCs which only seem to use apnpc? As far as I can tell, apnpc should always execute prior (as long as the distance is greater than 0).*
  > Ash: In case the player's already adjacent, and the op-trigger inadvertently takes precedent.

- **[1780904271610867780](https://x.com/JagexAsh/status/1780904271610867780)** (2024-04-18) — "op/ap trigger" as the natural unit of an interaction:
  > Ash: Emotes use a walktrigger to call p_stopaction(), actually. Maybe the difference is that it's via a walktrigger rather than being done directly in the ranging target's op/ap trigger.
  Cited at `LostCity_Server/engine/src/engine/script/handlers/PlayerOps.ts:429` (`P_STOPACTION`).

### 4.2 Concrete ap↔op switching rates (Elvarg)

- **[1756992041777561878](https://x.com/JagexAsh/status/1756992041777561878)** (2024-02-12)
  Q (@Tannerdino1): *what are the odds of elvarg switching from modes: AP -> OP, and vice versa[?]*
  > Ash: AP -> OP: 1/2, subject to a line-of-walk check. | OP -> AP: 1/8

  Cited at `LostCity_Server/content/scripts/quests/quest_dragon/scripts/elvarg.rs2:19`:
```19:35:content/scripts/quests/quest_dragon/scripts/elvarg.rs2
// https://twitter.com/JagexAsh/status/1756992041777561878
// "AP -> OP: 1/2, subject to a line-of-walk check.
// OP -> AP: 1/8"
[ai_applayer2,elvarg]
//mes("<tostring(map_clock)>: Ap");
if (%npc_action_delay > map_clock) {
    return;
}
if (~npc_check_notcombat = false) {
    npc_setmode(null);
    return;
}
if (~npc_check_notcombat_self = false) {
    return;
}
// 1/2 chance to switch to op, with line of walk check
if (random(2) = 0 & lineofwalk(npc_coord, coord) = true) {
```

### 4.3 op2 == "attackable"

- **[1821492251429679257](https://x.com/JagexAsh/status/1821492251429679257)** (2024-08-08)
  > Ash: npc_hasop(2) would return 1 if the NPC has op2, indicating that it is attackable. | Which is why we don't use op2 for anything else on NPCs, otherwise you get issues where they can be killed via manual autocast or ranging. It's happened with a few shopkeepers over the years.
  Cited at `LostCity_Server/engine/src/engine/script/handlers/NpcOps.ts:540` (`NPC_HASOP`).

- **[1821236327150710829](https://x.com/JagexAsh/status/1821236327150710829)** (2024-08-07)
  Q (@jamesmonger): *you said npc_huntall finds "attackable" npcs. Does this just look for if "op[1]" has a value? If a `.hunt` file targets an npc, does the same "attackable" restriction apply?*
  > Ash: 1. Yes, though it'd be looking for op2 rather than op1. | 2. No, though a HUNT config would have to specify the type/category of NPC being hunted, rather than being able to hunt for 'any attackable NPC'. The intended targets could even have no ops at all.
  Cited at `NpcOps.ts:339` (`NPC_HUNTALL`) and `LostCity_Server/engine/src/engine/entity/Npc.ts:254`.

- **[847092818023759872](https://x.com/JagexAsh/status/847092818023759872)** (2017-03-29) — op slot pressure:
  > Ash: Ah, I see. Trouble is, wielding has to be op2 in this codebase, and we don't have enough ops below that :(

### 4.4 `p_op*` commands drive interaction, and imply a walk

- **[1791472651623370843](https://x.com/JagexAsh/status/1791472651623370843)** (2024-05-17)
  Q (@NobodyImpo74600): *`obj_add(...); p_opobj(#);` — Would the player in context here automatically walk to the Obj target here?*
  > Ash: Yes.
  Cited six times, at `PlayerOps.ts:386` (`P_OPLOC`), `:403` (`P_OPNPC`), `:417` (`P_OPNPCT`),
  `:1045` (`P_OPOBJ`), `:1065` (`P_OPPLAYER`), `:1191` (`P_OPPLAYERT`).

- **[1508857967256522755](https://x.com/JagexAsh/status/1508857967256522755)** (2022-03-29)
  > Ash: We do something :) The commands in question are called p_oploc() or p_opnpc().
- **[1509143242998231040](https://x.com/JagexAsh/status/1509143242998231040)** (2022-03-30) — the loc is already "in focus":
  > Ash: [...] the p_oploc() command just takes the op number, not the LOC's type, since it acts on the LOC that's already "in focus" from the player clicking it in the first place.
- **[1517089157566410753](https://x.com/JagexAsh/status/1517089157566410753)** (2022-04-21)
  > Ash: For p_opheld, there isn't already a LOC or NPC in focus, so one would have to specify (1) which option and (2) which inventory the item's in and (3) which slot in the inventory it's in. (2) is always the main inventory, so far.
- **[1790684996480442796](https://x.com/JagexAsh/status/1790684996480442796)** (2024-05-15) — retrying an interaction:
  > Ash: p_teleport() | You can succeed in lighting the fire immediately, but if a re-try is required, p_opobj() is used to request it.
  Cited at `PlayerOps.ts:447` and `:1046`.

---

## 5. Line of sight vs line of walk

### 5.1 Two distinct checks; LOS is a geometry test on tiles and obstacles

- **[1161972319566356480](https://x.com/JagexAsh/status/1161972319566356480)** (2019-08-15)
  > Ash: The engine considers line-of-sight in terms of tiles and obstacles, rather than using different rules for PvP, PvM, MvP or MvM (or geometry checks that aren't for combat). The level of customisation we can get for it is very low [...]
- **[1162057134290407434](https://x.com/JagexAsh/status/1162057134290407434)** (2019-08-15)
  > Ash: It's correct that we're limited for what we can offer, considering the engine was never designed to have multiple definitions of "line of sight" to be chosen based on context - it should just be a geometry thing! [...]
- **[938534916194750464](https://x.com/JagexAsh/status/938534916194750464)** (2017-12-06)
  > Ash: It's simply how the game detects line-of-sight. If we ask the engine team to redefine that, you should expect a *lot* of potential changes across the whole game wherever ranging can happen.
- **[836877488357789696](https://x.com/JagexAsh/status/836877488357789696)** (2017-03-01)
  > Ash: The game engine's notion of what constitutes 'line-of-walk' is tied to a *lot* of behaviour. If it changes, expect side-effects.
- **[969130765870157824](https://x.com/JagexAsh/status/969130765870157824)** (2018-03-01)
  > Ash: If we ask the engine team to change the game's definitions of line-of-walk or line-of-sight for that, it's the kind of change that would have a *lot* of unexpected side-effects on players' familiar safe-spots and other such behaviour. We don't ask for such changes lightly.
- **[831205061816643584](https://x.com/JagexAsh/status/831205061816643584)** (2017-02-13)
  > Ash: Like finding line-of-sight and line-of-walk around odd scenery are no longer how they were before. In arbitrary places.

### 5.2 Asymmetric LOS: acknowledged as a 2004-era engine bug, fixed ~Aug/Sep 2019

- **[1110702061002375171](https://x.com/JagexAsh/status/1110702061002375171)** (2019-03-27)
  > Ash: The engine team's been working on something that'd let us change the line-of-sight rules to be symmetrical, to fix that. It's going to need more extensive testing (and possibly a beta) before we can launch it though.
- **[1150166052002775040](https://x.com/JagexAsh/status/1150166052002775040)** (2019-07-13)
  > Ash: The line-of-sight issue is an engine problem dating back to 2004. Mod Atlas has kindly written something for it, but it's going to take a lot of testing before we can roll it out. [...]
- **[1148128726481276929](https://x.com/JagexAsh/status/1148128726481276929)** (2019-07-08) — the old check is explicitly called asymmetric:
  > Ash: Depends whether the engine's asymmetric line-of-sight check works with the command we'd need to call to verify that. It might work - thanks for the idea.
- **[1153053841971134465](https://x.com/JagexAsh/status/1153053841971134465)** (2019-07-21)
  > Ash: We've mentioned before that Mod Atlas has made a fix for it. It's going to want some internal testing, then possibly a public trial on the beta worlds we'd been using for LMS, since this would affect line-of-sight in *all* situations, not just PvP.
- **[1141636516424441857](https://x.com/JagexAsh/status/1141636516424441857)** (2019-06-20)
  > Ash: The engine team's worked on a potential change to our line-of-sight definitions that'd remove such spots, but it's got a *lot* of scope to change other aspects of the game in ways that no-one wants (including patrolling guard puzzles) so it's going to need a long time in testing.
- **[1169656251057356805](https://x.com/JagexAsh/status/1169656251057356805)** (2019-09-05) — shipped:
  > Ash: The engine team changed the game's line-of-sight to solve the situations where it gave nonsensical results around obstacles including trees, but I'm afraid we're unlikely to make it impossible to run near the things.
- **[1151090028346564608](https://x.com/JagexAsh/status/1151090028346564608)** (2019-07-16)
  > Ash: [...] We do, however, have an engine change in testing from Mod Atlas that would make the safe-spot plugins far less effective.
- **[1084603795013804035](https://x.com/JagexAsh/status/1084603795013804035)** (2019-01-14)
  > Ash: That'll be one for the engine team [...] though a change to the game's line-of-sight definition will have a LOT of effects on safe spots across the game, and those won't be avoidable.

**Note for porting:** OSRS pre-Sept-2019 has asymmetric LOS; post-Sept-2019 it is symmetric.
Pick the one matching your target revision.

### 5.3 LOS/LOW as content-visible checks

- **[1321912968574046211](https://x.com/JagexAsh/status/1321912968574046211)** (2020-10-29)
  Q: *1. How close do additional targets have to be to the primary target? 2. Does damage travel through walls/obstacles? 3. Is it toggleable?*
  > Ash: 1. 1 tile | 2. Requires line-of-sight, but not line-of-walk. | 3. Not that I know of.
- **[872085798568505345](https://x.com/JagexAsh/status/872085798568505345)** (2017-06-06)
  > Ash: We don't currently have a way to avoid it needing line-of-sight, though we did change it to not require line-of-walk.
- **[561537505884110848](https://x.com/JagexAsh/status/561537505884110848)** (2015-01-31) / **[715926535077756928](https://x.com/JagexAsh/status/715926535077756928)** (2016-04-01) — LOS-free player clicks are post-2007 additions:
  > Ash: The engine feature for clicking people at a distance without line-of-sight was added long after 2007.
  > Ash: The ability to run code from clicking on a player without trying to get line-of-sight was added to the engine long after 2007.
- Line-of-walk gates in content: Ava's device
  (**[553251962607910912](https://x.com/JagexAsh/status/553251962607910912)**, 2015-01-08,
  *"Ava's kit works only when you have line-of-walk to the target (which I think is a weird requirement)"*),
  cats catching rats (**[582497338322739200](https://x.com/JagexAsh/status/582497338322739200)**,
  *"If the cat doesn't have line-of-walk to the rat, it will fail."*).
- **[1656959840575537153](https://x.com/JagexAsh/status/1656959840575537153)** (2023-05-12) — NPC style selection consults LOW:
  > Ash: They'd look at your prayers, and your relative defence bonuses for melee/magic/ranged, and whether they seem to have line-of-walk to try melee, but there'd still be a random factor. [...]

### 5.4 Teleport landing tiles require line-of-walk to the centre

- **[1463172737229283332](https://x.com/JagexAsh/status/1463172737229283332)** (2021-11-23)
  Q (@ProfesorCaos2): *When you teleport to a location, there is an area that you appear in. What happens if there are obstacles within that rectangle?*
  > Ash: It'd require line-of-walk to the centre. So scenery could 'cast a shadow' that would omit tiles that could otherwise be valid - i.e. you probably won't land on the far side of a bush, or something like that. Also, I believe the chances are skewed higher towards the west.
- **[1453834029703352323](https://x.com/JagexAsh/status/1453834029703352323)** (2021-10-28) — empirically confirmed by a player over 3k teleports:
  > Ash: It's picking a tile with line-of-walk to the centre of the square. The two omitted tiles may be considered to be obscured by the stone pillars.
- **[1529131971279998982](https://x.com/JagexAsh/status/1529131971279998982)** (2022-05-24)
  > Ash: A line-of-walk requirement is standard for such teleports. In this image, I've spawned a man at the central point of the teleport; the radius is 2 tiles. The root highlighted in red blocks walk, so you shouldn't land behind it.

---

## 6. Collision configuration: `blockwalk`, `blockrange`, `moverestrict`, `forceapproach`, `breakroutefinding`

### 6.1 `blockwalk`

- **[1677654049238265857](https://x.com/JagexAsh/status/1677654049238265857)** (2023-07-08)
  Q (@ZenKris21): *Is it a single property that blocks both or can you configure either walking or projectiles separately? What's that property called anyway?*
  > Ash: It's called blockwalk. | For NPCs, it can be 'none', 'NPC' or 'all'. 'NPC' is the default; 'all' includes line-of-sight whether we want it or not. | For scenery, there are two settings: blockwalk & blockrange, either of which can be 1 or 0.

  This tweet is the sole justification for LostCity's enum:
```1:7:engine/src/engine/entity/BlockWalk.ts
// https://x.com/JagexAsh/status/1677654049238265857
export const enum BlockWalk {
    NONE,
    NPC,
    ALL,
    PLAYER
}
```
  (`LostCity_Server/engine/src/engine/entity/BlockWalk.ts:1`. Note Ash lists three NPC
  values — `none` / `NPC` / `all`; LostCity adds a fourth, `PLAYER`, which Ash does not
  mention.)

- **[1678784805457068033](https://x.com/JagexAsh/status/1678784805457068033)** (2023-07-11)
  > Ash: The ability to walk through it applies to both players and NPCs, but okay, you clearly understood the meaning. As I mentioned a couple of posts ago, it's called "blockwalk".

### 6.2 `moverestrict`

- **[1678810351091974159](https://x.com/JagexAsh/status/1678810351091974159)** (2023-07-11)
  Q (@ZenKris21): *What makes it so Splatters in Pest Control are able to walk through Brawlers, and other NPCs that normally block movement?*
  > Ash: There's a separate property called 'moverestrict': | 'normal' - walks on normal terrain | 'blocked' - walks only on blocked terrain like water | 'blocked+normal' - walks on either | 'nomove' - doesn't walk | 'passthru' - can walk through players or NPCs that'd normally block NPCs.

- **[1605183976028741632](https://x.com/JagexAsh/status/1605183976028741632)** (2022-12-20) — the *fuller* list, including `indoors`/`outdoors`:
  > Ash: 'moverestrict' can be normal, blocked (e.g. ducks walk on water), blocked+normal (e.g. implings walk over land and water), indoors, outdoors or nomove.

  LostCity's enum merges both tweets but cites only the later one:
```1:10:engine/src/engine/entity/MoveRestrict.ts
// https://x.com/JagexAsh/status/1678810351091974159
export const enum MoveRestrict {
    NORMAL,
    BLOCKED,
    BLOCKED_NORMAL,
    INDOORS,
    OUTDOORS,
    NOMOVE,
    PASSTHRU
}
```
  (`LostCity_Server/engine/src/engine/entity/MoveRestrict.ts:1`)

- **[1312901650818924548](https://x.com/JagexAsh/status/1312901650818924548)** (2020-10-04) — the same concept described two years earlier in plain language:
  > Ash: Map tiles can be blocked or not. An NPC can be defined as "able to walk on unblocked tiles", which is the default, or "able to walk on blocked tiles", used on ducks, or "able to walk on both", used on flying creatures. | But I wouldn't be able to export a list of the tiles.

### 6.3 `forceapproach`

- **[1640686954256769026](https://x.com/JagexAsh/status/1640686954256769026)** (2023-03-28)
  Q (@PhoenixHadyn): *What property is used to prevent stuff like stairs from being interacted from all directions? And how do you specify directions?*
  > Ash: It's called "forceapproach", specified as north, south, east or west. | Scenery can be rotated; the "forceapproach" property is relative to the scenery's orientation. So if we put "forceapproach=north", then rotate the scenery 90 degrees clockwise, it'll be usable from the east.

### 6.4 `breakroutefinding`

- **[1443150721734660096](https://x.com/JagexAsh/status/1443150721734660096)** (2021-09-29)
  Q (@ZenKris21): *There's a property that seems to "unclip" the loc client-side, making it appear as if you could walk on-top of the loc - however it seems the server skips that stage as you obviously can't.*
  > Ash: Yes, the config would have 'breakroutefinding=yes' to enable that property. It means you'd navigate 'through' the booth when you click on the banker, though the server would stop you when you reach the booth - and that's where we'd want you to be. It works better on bigger banks.

### 6.5 Blocking NPCs deliberately disrupt routefinding

- **[521419151869755394](https://x.com/JagexAsh/status/521419151869755394)** (2014-10-12)
  > Ash: NPCs that block walk are programmed to disrupt routefinding. This is needed for CW barricades. I can change the Soulless.
- **[554365183628673024](https://x.com/JagexAsh/status/554365183628673024)** (2015-01-11)
  > Ash: Those trees are NPCs acting as scenery, like CW barricades. The game could never routefind past blocking NPCs in case they move.
- **[677939326706143233](https://x.com/JagexAsh/status/677939326706143233)** (2015-12-18)
  > Ash: Many monsters in there block walk. Such monsters have always required players to navigate round them.
- **[1268255463403069440](https://x.com/JagexAsh/status/1268255463403069440)** (2020-06-03)
  > Ash: [...] Do try not to get under the blocking NPCs, as they are expected to disrupt movement.
- **[697800330424291328](https://x.com/JagexAsh/status/697800330424291328)** (2016-02-11)
  > Ash: Blocking NPCs always do that, though I know a trick that'd help for those static portals.
- **[1727609489954664502](https://x.com/JagexAsh/status/1727609489954664502)** (2023-11-23) — the "trick": static map blocking under an immobile NPC
  Q (@NobodyImpo74600): *some of the npc trees in Draynor have map clipping while some others do not. If a player step collided with a "non clipped tree" the player actually pauses walking.*
  > Ash: Likely, yes. We used map blocking like that to stop the Pest Control portals from being so obstructive when players tried to walk round the sides of them, since they're another example of one that doesn't move.
  Cited at `LostCity_Server/engine/src/engine/entity/PathingEntity.ts:677` (see §3.2).
- **[826844111726661637](https://x.com/JagexAsh/status/826844111726661637)** (2017-02-01)
  > Ash: Not very easily - NPCs that block walk can be extremely annoying for players to navigate around.
- **[580809647591976960](https://x.com/JagexAsh/status/580809647591976960)** (2015-03-25)
  > Ash: Routefinding gets messed up if the blocking is changed dynamically like that.
- **[1409939197599813637](https://x.com/JagexAsh/status/1409939197599813637)** (2021-06-29) — why players never block:
  > Ash: I rather doubt we'd be able to make their blocking behaviour change like that, and you'll have perhaps seen with NPCs that block walk that they're pretty disruptive to the navigation in their entire area - that's not likely to go down well if it happens to players too.
- **[1450476141345652749](https://x.com/JagexAsh/status/1450476141345652749)** (2021-10-19)
  > Ash: We'd not be able to stop players going underneath each other, not without scuppering routefinding around all players. [...]
- **[1597568744326565888](https://x.com/JagexAsh/status/1597568744326565888)** (2022-11-29)
  > Ash: The game's not going to be able to block you from standing under each other, not without seriously messing up pathing. NPCs that block movement can be horrible for walking around - see the temple monkeys at Marim. So such overlaps seem likely to remain.
- **[1415013845723226112](https://x.com/JagexAsh/status/1415013845723226112)** (2021-07-13)
  > Ash: That tends to block walk. Might mean everyone just stands next to each other in Lumbridge, and can't move anywhere!

### 6.6 Random-tile selection respects blocking

- **[1232268615489130497](https://x.com/JagexAsh/status/1232268615489130497)** (2020-02-25)
  > Ash: They spawn on just about any unblocked tile, whether or not there's a route for players to walk to it. Imps can teleport there too.
- **[1288217064914800641](https://x.com/JagexAsh/status/1288217064914800641)** (2020-07-28)
  > Ash: They pick a random unblocked tile; it's allowed to be inside an enclosure. So yup, that can happen :)
- **[429240806776377345](https://x.com/JagexAsh/status/429240806776377345)** (2014-01-31) — drop placement:
  > Ash: The game's picking an unblocked tile that's between you and the kraken. It's not always under you. [...]
- **[686597892279283714](https://x.com/JagexAsh/status/686597892279283714)** (2016-01-11)
  > Ash: If you're on a blocked tile when the event begins, the game will avoid sending you back to it.
- **[1553352575092756486](https://x.com/JagexAsh/status/1553352575092756486)** (2022-07-30) — spawn-space checks are purely per-tile:
  > Ash: It doesn't really have a concept of "safe-spotting on a corner" as something it could penalise by denying you superiors - it just looks at whether the individual tiles are empty of obstacles.

---

## 7. NPC movement: modes, wander, patrol, escape, hunt, aggression

### 7.1 The mode list (this is the authoritative enumeration)

- **[1527626332884111362](https://x.com/JagexAsh/status/1527626332884111362)** (2022-05-20)
  Q (@ZenKris21): *What's the purpose of npc_setmode(mode) command? Are the modes defined in their own config files?*
  > Ash: Modes are defined in the engine. They include 'wander' (random walk within radius), 'patrol' (see Hans), 'playerescape' (for retreating), 'playerfollow' (for pets) and various player-interaction ones that are used for performing attacks.

- **[1574688183597555716](https://x.com/JagexAsh/status/1574688183597555716)** (2022-09-27)
  > Ash: npc_setmode() would tell it to attempt a particular trigger once it's within range.
- **[1606697959571300352](https://x.com/JagexAsh/status/1606697959571300352)** (2022-12-24)
  > Ash: It's a command like you guessed - npc_setmode(playerfaceclose), or equivalent.
- **[1795184135327089047](https://x.com/JagexAsh/status/1795184135327089047)** (2024-05-27) — retreat
  Q (@NobodyImpo74600): *for the few amount of npcs that retreat when their HP gets low, is this defined as like a npc_setmode(retreat) and handled on engine? or is it like a param on the npc[?]*
  > Ash: npc_setmode(playerescape) can be called in script, and is called via the generic combat code. A param on the NPC specifies the HP level at which that will happen; by default it's zero, so most NPCs don't retreat.
  Cited at `LostCity_Server/engine/src/engine/script/handlers/NpcOps.ts:204` (`NPC_SETMODE`).
- **[1549466743273316353](https://x.com/JagexAsh/status/1549466743273316353)** (2022-07-19) — how escape moves:
  > Ash: The engine's got a predefined behaviour mode called "playerescape" that makes the NPC move away from the specified player. Usually in a straight line, until it reaches obstacles.
- **[1604391207421071361](https://x.com/JagexAsh/status/1604391207421071361)** (2022-12-18)
  > Ash: P.S. Note that his behaviour mode is specified as 'patrol', so that's what he reverts to doing when he resets at the end of dialogue, etc.

### 7.2 Wander

- **[1645694456824201217](https://x.com/JagexAsh/status/1645694456824201217)** (2023-04-11)
  Q (@arravs27): *we have patrolling for Hans, and then wander that seems to be fired at 1/8 chance every tick, but [...] the tutors only seem to wander within a bounding box[.] Is this a separate mode?*
  > Ash: No, any wandering NPC will have a permitted radius for their wandering. Bob the Cat is famous for having a huge one, but the tutors would have been given much smaller ones so that they can be found reliably by newbies.
- **[1467208406243192837](https://x.com/JagexAsh/status/1467208406243192837)** (2021-12-04)
  > Ash: The engine was designed to treat their permitted wandering as a radius from their spawn point; it doesn't really have a concept of "this room" for them at present.
- **[1274621763431866368](https://x.com/JagexAsh/status/1274621763431866368)** (2020-06-21)
  > Ash: They've got the same random wandering code as other NPCs.
- **[1122838317068890115](https://x.com/JagexAsh/status/1122838317068890115)** (2019-04-29)
  > Ash: It's not a bug that NPCs can wander like that, even if it'd be convenient for training if they didn't.
- **[935556639842820096](https://x.com/JagexAsh/status/935556639842820096)** (2017-11-28)
  > Ash: No, though if something's adjacent to you and then allowed to wander, it's inevitably going to move further away.
- **[1541478026235580416](https://x.com/JagexAsh/status/1541478026235580416)** (2022-06-27) — a concrete number:
  > Ash: Their wander radius is about 5 tiles, and they moo every 42-54 secs.

### 7.3 Patrol

- **[1603790584908488705](https://x.com/JagexAsh/status/1603790584908488705)** (2022-12-16)
  Q (@ZenKris21): *Is there anything else that's configurable for patrols? I've noticed that you can only block the NPC for so long until it teleports to the next checkpoint [...]*
  > Ash: Ah - it's a list of coordinates and a number of ticks to pause at each one. | From trying things in-game, I got the impression that the NPC would teleport past obstacles after whatever time it was due to have reached its next patrol point and finished the pause there.
- **[1243107390825934849](https://x.com/JagexAsh/status/1243107390825934849)** (2020-03-26) — the waypoint cap:
  > Ash: NPCs have a max radius specified on them, and they can't patrol outside it. However it can be set arbitrarily high, so that's not an issue. A more relevant limitation would be the cap of about 40 waypoints on a defined route.
- **[1449452349207060487](https://x.com/JagexAsh/status/1449452349207060487)** (2021-10-16)
  > Ash: NPCs with preset patrol routes will do that, and if lots of them are mapped down, they'll form a queue like this. Years ago Hans got something of the sort, even!

### 7.4 `maxrange` vs `attackrange` (the two NPC distance settings)

- **[1445060418163073034](https://x.com/JagexAsh/status/1445060418163073034)** (2021-10-04)
  > Ash: No, 'maxrange' is correct. Aggression is 'attackrange', and that one's relative to the NPC's current position rather than its spawn point, though I think it won't attack outside of its 'maxrange' radius regardless.
- **[1605493805922144258](https://x.com/JagexAsh/status/1605493805922144258)** (2022-12-21)
  Q (@ZenKris21): *Do you have the ability to define the distance at which an interaction stops from the engine's perspective?*
  > Ash: Are you aware of the 'maxrange' setting that NPCs can have? That determines the distance for most interactions, e.g. combat aggression. | One special case is the mode used by standard dialogue, though, which always resets when they stop being adjacent.
- **[1605513048898162688](https://x.com/JagexAsh/status/1605513048898162688)** (2022-12-21)
  > Ash: The engine has that one special mode for 'reset when no longer adjacent'. | Banker dialogue tends not to use that mode, as most bankers aren't adjacent. (Even if the Zanaris ones actually are.) So those would more likely depend on the NPC's maxrange setting I mentioned.
- **[1604909367735754755](https://x.com/JagexAsh/status/1604909367735754755)** (2022-12-19)
  > Ash: Not specifically, though the NPC's maxrange setting (part of the NPC's own config) would determine how far it can go while following you or fleeing you.
- **[1444818004882120708](https://x.com/JagexAsh/status/1444818004882120708)** (2021-10-04)
  > Ash: No, that radius can be modified in the NPC's definition files. Devs may often set it to be slightly larger than the wander range, as you describe, but that's not a rule.
- **[1605490868219019267](https://x.com/JagexAsh/status/1605490868219019267)** (2022-12-21)
  > Ash: No, they have an attackrange specified in their config instead, which has a similar effect.
- **[1614498680144527360](https://x.com/JagexAsh/status/1614498680144527360)** (2023-01-15) — attackrange makes the engine close the gap:
  > Ash: The engine uses it alright, bringing NPCs closer to their target if they're trying to interact from a distance. My point was that this could have been scripted behaviour, like it is for players for whom it's scripted based on their current weapon. | npc_attackrange() returns it.
  Cited at `LostCity_Server/engine/src/engine/script/handlers/NpcOps.ts:535` (`NPC_ATTACKRANGE`).

### 7.5 Hunt configs (target acquisition) and visibility flags

- **[1608034999005024258](https://x.com/JagexAsh/status/1608034999005024258)** (2022-12-28) — **the huntvis encoding**
  Q (@ZenKris21): *What do the integer arguments stand for in this: huntall($adjacent,2,1);*
  > Ash: The first is the radius of the search zone, the second can be: | - 2 for line-of-walk required | - 1 for line-of-sight required | - 0 for neither
- **[1602228868370268167](https://x.com/JagexAsh/status/1602228868370268167)** (2022-12-12)
  > Ash: 'off', i.e. no check for line of walk/sight at all.
- **[1634698932130394112](https://x.com/JagexAsh/status/1634698932130394112)** (2023-03-11)
  > Ash: 'lineofsight' is the only documented option. If that's not set, it defaults to the legacy behaviour, which I'd call 'lineofwalk' though the documentation doesn't mention whether that word would be recognised here.
- **[1441467088523055113](https://x.com/JagexAsh/status/1441467088523055113)** (2021-09-24)
  > Ash: In each NPC config, we specify a 'huntmode'. This is a separate config, with different huntmodes having different settings for how the NPC should care about combat level or AFKness.
- **[1646208883910754324](https://x.com/JagexAsh/status/1646208883910754324)** (2023-04-12)
  > Ash: In the huntmodes, the 'check_nottoostrong' property accepts 'off' or 'outside_wilderness'. That man may simply not have hunting enabled at all.
- **[1602394283897651264](https://x.com/JagexAsh/status/1602394283897651264)** (2022-12-12)
  > Ash: There's a "rate" setting available in .hunt files, though for hunting non-player targets, the engine won't accept low numbers of ticks (presumably to minimise load). I don't recall what the minimum is.
- **[1602398304784027663](https://x.com/JagexAsh/status/1602398304784027663)** (2022-12-12)
  > Ash: 1. Yes. | 2. Having defined different .hunt configs, one can tell an NPC which one to use. And GWD creatures switch occasionally.
- **[1799793914595131463](https://x.com/JagexAsh/status/1799793914595131463)** (2024-06-09)
  > Ash: Yes, a specific LOC/NPC/OBJ can be specified. Alternatively, a category of LOCs/NPCs/OBJs can be specified, for it to hunt anything in that category. I don't think it's got a quantity filter though.
  Cited at `LostCity_Server/engine/src/engine/entity/Npc.ts:255` (`huntAll`).
- **[1796460129430433930](https://x.com/JagexAsh/status/1796460129430433930)** (2024-05-31)
  > Ash: 1. Yes - npc_find(). | 2. Yes, for generating a list of attackable NPCs within a specified radius of a specified coordinate, optionally respecting line-of-walk, line-of-sight or neither.
  Cited at `NpcOps.ts:338` (`NPC_HUNTALL`) and `NpcOps.ts:350` (`NPC_FIND`).
- **[1796878374398246990](https://x.com/JagexAsh/status/1796878374398246990)** (2024-06-01)
  > Ash: npc_findallany()
  Cited at `NpcOps.ts:417` (`NPC_FINDALLANY`).

### 7.6 Aggression radius is measured from the SW corner, and is therefore asymmetric

- **[1468895368092033030](https://x.com/JagexAsh/status/1468895368092033030)** (2021-12-09)
  > Ash: The game engine mostly does aggro based on how close you are to the NPC, and I think it's biased towards their SW corner, which can be very noticeable on a large NPC like that. While it'd be changeable, I wouldn't expect it to be appreciated very well in that particular room!
- **[1668875823078686723](https://x.com/JagexAsh/status/1668875823078686723)** (2023-06-14)
  > Ash: I've never been fond of how the aggression radius seems to be based off the NPC's SW corner rather than the distance from their nearest side, yeah.
- **[1368183551347265541](https://x.com/JagexAsh/status/1368183551347265541)** (2021-03-06)
  > Ash: It's quite common for NPCs such as dragons to be a bit like that, since they're physically large, but have a small aggression radius which I think the engine centres on their SW corner. We'd be able to change it, but it'd affect *all* of them, not just the two you've listed.
- **[1334958848231305217](https://x.com/JagexAsh/status/1334958848231305217)** (2020-12-04)
  > Ash: Ideally we'd seek an engine change to make the aggression range extend in all directions equally - accepting that some players may have been relying on the asymmetric behaviour [...]
- **[1287106545185169408](https://x.com/JagexAsh/status/1287106545185169408)** (2020-07-25)
  > Ash: I believe that's arising from their small aggro range and the directions they're looking for targets - do you ever get the sense that they're more aggressive if you're on the west or south sides rather than the others?
- **[1300433656054513667](https://x.com/JagexAsh/status/1300433656054513667)** (2020-08-31)
  > Ash: [...] their aggro range isn't much bigger than the NPC itself, so I'd wonder if its south/west sides are different to its north/east sides.
- **[1320075659620896768](https://x.com/JagexAsh/status/1320075659620896768)** (2020-10-24)
  > Ash: Different NPCs can have different aggro ranges. Hobgoblins seem to have been given relatively large ones.
- **[1393263491285389313](https://x.com/JagexAsh/status/1393263491285389313)** (2021-05-14) — target selection among candidates:
  > Ash: NPCs would have an internal order for their actions to be processed, like PID, yup. When there are multiple viable targets in their aggression range, the engine's documentation says they pick randomly.
- **[1586311832108679169](https://x.com/JagexAsh/status/1586311832108679169)** (2022-10-29) — NPCs don't coordinate:
  > Ash: [...] they currently do their aggro decisions independently of each other, meaning that a creature from across the room will wish to target you rather than leaving you to its mate who's standing nearer. [...]
- **[1213895840760242176](https://x.com/JagexAsh/status/1213895840760242176)** (2020-01-05) — de-aggro resets to wander:
  > Ash: If a creature loses its focus on you and "resets" to its default wandering behaviour, it'll pause its poison/venom effect. [...] It's been that way since 2004, tbh.

### 7.7 NPCs getting stuck, and the fail-safe teleport

- **[757988068108492800](https://x.com/JagexAsh/status/757988068108492800)** (2016-07-26)
  Q: *Howcome this knight just randomly walks thru the wall to escape?*
  > Ash: If NPCs get stuck for a very long time in one tile, they're programmed to fail-safe like that.
- **[1156319148722597888](https://x.com/JagexAsh/status/1156319148722597888)** (2019-07-30)
  > Ash: If you trap any NPC at a distance from its spawn point, and it's not in some player-interaction mode, it'll teleport away presently. I think the engine does that as a fail-safe in case they get stuck.
- **[1003963857570861056](https://x.com/JagexAsh/status/1003963857570861056)** (2018-06-05)
  > Ash: If an NPC is stuck on a single tile away from its spawn point, it tends to unstick itself. The direction from which players are clicking on it shouldn't be relevant.
- **[725115496836685824](https://x.com/JagexAsh/status/725115496836685824)** (2016-04-27)
  > Ash: No, as far as I know the game engine was programmed to do that to help when a wandering NPC got stuck.
- **[1157913237318512640](https://x.com/JagexAsh/status/1157913237318512640)** (2019-08-04)
  > Ash: Although wandering NPCs will normally teleport a fairly short distance [...]
- **[491874831726440448](https://x.com/JagexAsh/status/491874831726440448)** (2014-07-23)
  > Ash: Turns out the game engine has a bug involving NPCs failing to attack or retreat at one end of their range.
- **[627041630357319680](https://x.com/JagexAsh/status/627041630357319680)** (2015-07-31)
  > Ash: It was a change to make NPCs attack or retreat instead of standing like lemons. It wouldn't affect individual attacks.
- **[1290698942519422986](https://x.com/JagexAsh/status/1290698942519422986)** (2020-08-04)
  > Ash: I'd not have expected an NPC to get stuck on a corner like that, yeah.

### 7.8 NPC must close to melee before attacking

- **[973895294604767233](https://x.com/JagexAsh/status/973895294604767233)** (2018-03-14)
  > Ash: If the NPC isn't adjacent to you, it will have to move first. Once it's arrived, it'll attack if its tick rate permits.
- **[1381994147112964097](https://x.com/JagexAsh/status/1381994147112964097)** (2021-04-13) — spawn-then-step-adjacent:
  > Ash: We mostly spawn it on the player's coordinate, at which point it'll pick a random one itself for moving adjacent to you. And if the NPC doesn't render before the move starts, it may just appear on the new tile directly.

---

## 8. NPC pathing quality is deliberately poor, because safespots depend on it

This is the strongest normative statement in the whole corpus: **do not "improve" NPC
pathfinding.**

- **[628524513454501890](https://x.com/JagexAsh/status/628524513454501890)** (2015-08-04)
  Q: *when doing hunter the npc gets stuck in a corner trying to path find to the trap.*
  > Ash: NPC routefinding is deliberately not good - if NPCs could navigate better, many safe-spots would break.
- **[654377490169860097](https://x.com/JagexAsh/status/654377490169860097)** (2015-10-14)
  > Ash: If we improve NPC routefinding, it might have a terrible effect on everyone's favourite safe spots.
- **[894912908190199809](https://x.com/JagexAsh/status/894912908190199809)** (2017-08-08)
  > Ash: While the engine team could change the algorithm, you should expect a LOT of safe spots to change as a result, which may not be appreciated.
- **[922496409995276288](https://x.com/JagexAsh/status/922496409995276288)** (2017-10-23)
  > Ash: Perhaps the engine's routefinding algorithm isn't as smart as it could be. Though it'd make a *huge* difference to ranging spots if it were.
- **[905840966866784256](https://x.com/JagexAsh/status/905840966866784256)** (2017-09-07) — the general rule for where safespots exist:
  > Ash: Any NPC that's not in a rectangular enclosure with no blocking scenery will have safe spots. But it's been changed to make that harder.
- **[708713739147735041](https://x.com/JagexAsh/status/708713739147735041)** (2016-03-12)
  > Ash: The engine isn't likely to get separate code for salamanders. The change would therefore affect all NPCs, affecting safe spots.
- **[667368736085680128](https://x.com/JagexAsh/status/667368736085680128)** (2015-11-19)
  > Ash: Yeah, you'll find them in Draynor too. NPCs mess up routefinding; we could change that, but in CW and Ape Atoll it's useful.
- **[847029640153759744](https://x.com/JagexAsh/status/847029640153759744)** (2017-03-29)
  > Ash: I think that might be the best the engine knows how to do for their routefinding.
- **[1046725483969355776](https://x.com/JagexAsh/status/1046725483969355776)** (2018-10-01) — the standard workaround:
  > Ash: Making the player step back should be viable; you're right that we'd be unlikely to get a different pathing algorithm added to the engine for just those creatures.
- **[377187554371448834](https://x.com/JagexAsh/status/377187554371448834)** (2013-09-09) — mapping is the lever, not the algorithm:
  > Ash: GWD progress: Corners of boss rooms are now square to discourage safe-spotting. [...]

---

## 9. Multi-tile entities: coordinate is the SW corner

- **[1351281815408013318](https://x.com/JagexAsh/status/1351281815408013318)** (2021-01-18)
  > Ash: All NPCs' coordinates are treated as being their SW corner. I don't know of anything that'd make that one different.
- **[1415010866794225669](https://x.com/JagexAsh/status/1415010866794225669)** (2021-07-13)
  > Ash: All NPCs have their official coordinate at their SW corner. For large NPCs, quite a lot of loot will aim to go for their centre instead, since that can look better. Though a few core features like the RDT don't fully support that yet.
- **[725118632540295168](https://x.com/JagexAsh/status/725118632540295168)** (2016-04-27)
  > Ash: For big NPCs, the position is defined by the SW corner, though some things aim for the middle, yup.
- **[1202175148905893890](https://x.com/JagexAsh/status/1202175148905893890)** (2019-12-04)
  > Ash: Large NPCs are still defined as being on the tile of their south-west corner, and combat will tend to put ammo there, even if the NPC's large geometry appears to be centred slightly north-east of that tile.
- **[1252306380163354624](https://x.com/JagexAsh/status/1252306380163354624)** (2020-04-20)
  > Ash: I suspect it's counting from the NPC's south-west corner; many parts of the engine work off that rather than the centre, when an NPC is large.
- **[1328847272017338368](https://x.com/JagexAsh/status/1328847272017338368)** (2020-11-17)
  > Ash: An NPC's tile tends to be its SW corner. The distance from that may make a difference.
- **[764941453504249856](https://x.com/JagexAsh/status/764941453504249856)** (2016-08-14) — scenery too:
  > Ash: Yeah, it's always annoyed me that the game engine assumes it needs the SW corner for scenery interactions.
- **[983257234707755008](https://x.com/JagexAsh/status/983257234707755008)** (2018-04-09) — routefinding target bias:
  > Ash: It is; the game has always tended to favour the SW corner for routefinding to those patches. Might be possible to improve.
- **[1176867969063931904](https://x.com/JagexAsh/status/1176867969063931904)** (2019-09-25)
  > Ash: If you're interacting with large bits of scenery the game will sometimes aim you towards their south-west corners. [...]
- **[1021201779328016390](https://x.com/JagexAsh/status/1021201779328016390)** (2018-07-23) — AoE search origin:
  > Ash: The game looks for potential targets in a 3x3 grid around the SW corner of the original target.
- **[1552713048351285250](https://x.com/JagexAsh/status/1552713048351285250)** (2022-07-28) — placing an oversized NPC:
  > Ash: It should consider whether the superior could have its centre, SW corner, SE corner, NW corner or NE corner at the tile of the dying dust devil. [...]

Pathing consequences of size:

- **[1453853110380769329](https://x.com/JagexAsh/status/1453853110380769329)** (2021-10-28)
  > Ash: The engine's routefinding for size-2+ NPCs may not be flexible enough to accommodate that. Though if we stopped insisting on 1 melee minion per boss room, it could be given a (very) short-range ranged attack, which may make its navigation a lot simpler.
- **[1598353821872308225](https://x.com/JagexAsh/status/1598353821872308225)** (2022-12-01)
  > Ash: It's quite a big bear even without that - size 5, same as the Corp. And the bigger they get, the more awkward pathing can become.
- **[1244041352201097217](https://x.com/JagexAsh/status/1244041352201097217)** (2020-03-28)
  > Ash: The Jad-sized cows. As cows, they'd not have the distant special attacks, and they'd struggle to route-find around a player to do melee.
- **[1288905999097757698](https://x.com/JagexAsh/status/1288905999097757698)** (2020-07-30) — players are hard-wired 1x1:
  > Ash: [...] our game is not able to handle the rendering or routefinding for players who aren't 1x1 tiles [...]
- **[1289263907232788481](https://x.com/JagexAsh/status/1289263907232788481)** (2020-07-31)
  > Ash: [...] rendering aside, the routefinding would still need to know how to handle players of that size.
- **[1114700304153567233](https://x.com/JagexAsh/status/1114700304153567233)** (2019-04-07)
  > Ash: [...] if you needed to approach scenery with routefinding that knew you were no longer 1x1 :P

(LostCity's `PathingEntity` reflects this: the diagonal step is only attempted when
`this.width === 1`.)

---

## 10. Following and the "follow dance"

**Follow uses different routing code from every other interaction, on purpose.**

- **[1156689108531646464](https://x.com/JagexAsh/status/1156689108531646464)** (2019-07-31)
  Q (@Will_SIA): *Why does the 'follow' command cause you to get caught on entities (trees, rocks, walls), but 'trade' correctly paths you to another player around the same entities?*
  > Ash: The 'Follow' one has very custom routing code built into the engine to make it work for dancing. I'm sure they could have more easily made it act like any other option, but the last time players became unable to dance, it was seen as a catastrophe :)
- **[1227166364366000129](https://x.com/JagexAsh/status/1227166364366000129)** (2020-02-11)
  > Ash: The engine uses a modified version of pathing for the Follow option solely to support the dancing-in-a-circle behaviour that players have used since 2004. At one point an engine update in about 2015 removed that, and players were adamant it needed to be brought back.
- **[1600176973854040066](https://x.com/JagexAsh/status/1600176973854040066)** (2022-12-06)
  > Ash: The 'Follow' option uses some custom engine code that deliberately doesn't follow its target perfectly so that the 'dancing' behaviour can work. When that was last updated, and it stopped players dancing, they mostly didn't seem appreciative of it :(
- **[1613873228866265088](https://x.com/JagexAsh/status/1613873228866265088)** (2023-01-13)
  Q (@ZenKris21): *Are player modes (in contrast to npc modes) a thing? If not, how would you tell the game to follow another player when you right-click them and select "Follow"?*
  > Ash: They're not. | The game engine deals with 'Follow', quite possibly treating it like a mode, but I don't have visibility on the details. For what it's worth, the routing used by 'Follow' is NOT like other player interactions, so that it can support the dancing behaviour.
- **[1136770312798986240](https://x.com/JagexAsh/status/1136770312798986240)** (2019-06-06)
  > Ash: The 'Follow' option was given custom behaviour in the engine to ensure the dancing behaviour remained available in the game. For anything else, you're most welcome to use a different option.
- **[1341706718707916802](https://x.com/JagexAsh/status/1341706718707916802)** (2020-12-23)
  > Ash: While it would, that option has custom engine code to make it support the 'following dance' that players have been accustomed to for so many years. The last time that stopped working, it was a very big controversy, and we're not inclined to change it again.
- **[1325172470622531584](https://x.com/JagexAsh/status/1325172470622531584)** (2020-11-07)
  > Ash: [...] they had to put some custom code in that to let players 'dance' around each other via Following, since players were very upset last time that stopped working.
- **[1125165161776603143](https://x.com/JagexAsh/status/1125165161776603143)** (2019-05-05)
  > Ash: I'm afraid in-game following is just for dancing.
- **[695010120716128257](https://x.com/JagexAsh/status/695010120716128257)** (2016-02-03)
  > Ash: The behaviour when you follow someone round a wall is part of how the game engine does routefinding; we cannot change it for CWA.
- **[900646635017039872](https://x.com/JagexAsh/status/900646635017039872)** (2017-08-24) — trade/use-item is the *normal* pathing option:
  > Ash: You'd be automatically walking towards the target player to trade with them, so you could use it to follow them.
- **[1097670153058373633](https://x.com/JagexAsh/status/1097670153058373633)** (2019-02-19) — interaction targets are chased:
  > Ash: Just like if you click Attack on a monster, and it moves, you'll follow it.
- **[1166505512017584128](https://x.com/JagexAsh/status/1166505512017584128)** (2019-08-28)
  > Ash: I don't see it helping for that, sorry. The butler is not using line-of-sight to follow you.
- Pets use `playerfollow` (see §7.1); pet desync outside instances is fixed with the
  Call button, e.g. **[1290080029745938433](https://x.com/JagexAsh/status/1290080029745938433)**.

---

## 11. Movement speed: ticks, walking, running, ctrl-click

- **[835966573986865155](https://x.com/JagexAsh/status/835966573986865155)** (2017-02-26)
  Q: *sometimes when I click on a square my character w/ run on walks instead of running.*
  > Ash: Running = 2 tiles per tick. If you've got an odd number of tiles to go, you're going to walk one of those steps.
- **[919173571108360192](https://x.com/JagexAsh/status/919173571108360192)** (2017-10-14)
  > Ash: Running is 2 tiles per tick; when you're traveling an odd number of tiles, the game will often draw it as though you walked the odd one.
- **[1018589293692977152](https://x.com/JagexAsh/status/1018589293692977152)** (2018-07-15)
  > Ash: Running is 2 tiles per tick, regardless of your stats.
- **[1568292309158199297](https://x.com/JagexAsh/status/1568292309158199297)** (2022-09-09)
  > Ash: At present, the game engine can offer walking at 1 tile per tick or running at 2 tiles per tick. If it's ever able to do faster travel for players, that'd be great, though I wouldn't expect it imminently.
- **[1553380603973730304](https://x.com/JagexAsh/status/1553380603973730304)** (2022-07-30)
  > Ash: For now the game engine doesn't do more than 2 tiles per tick, and I gather it'd need to change how rapidly it streams map if it went faster than this. [...]
- **[1582711021197672449](https://x.com/JagexAsh/status/1582711021197672449)** (2022-10-19)
  > Ash: Quite impactful for a LOT of activities across the game, if so, since that'd be at least a 50% increase if you're going at 3 tiles per tick. But it's not something the game engine currently supports, I'm afraid.
- **[1227156207510659076](https://x.com/JagexAsh/status/1227156207510659076)** (2020-02-11) — running can skip trap tiles:
  > Ash: If you're running, you go 2 tiles per tick, and might therefore get over the thing before it triggers. [...]
- **[1560221037907873799](https://x.com/JagexAsh/status/1560221037907873799)** (2022-08-18)
  > Ash: The custom running animation made for players wielding nets may be a tad cartoonish, but you'd still move the same number of tiles per tick :)

NPC speed:

- **[1179011519830462464](https://x.com/JagexAsh/status/1179011519830462464)** (2019-10-01)
  > Ash: [...] NPCs go 1 tile per tick, though for *pre-defined* routes we can double that.
- **[1414623886583418880](https://x.com/JagexAsh/status/1414623886583418880)** (2021-07-12)
  > Ash: We're quite limited for making NPCs move faster than 1 tile per tick, except in preset routes (e.g. Pestilent Bloat). Sorry, no.
- **[1513930997020114954](https://x.com/JagexAsh/status/1513930997020114954)** (2022-04-12) — NPCs gained real 2-tile movement around early 2022:
  > Ash: Currently the game engine only supports 2 tiles - and it only just got that recently for NPCs! I'm not optimistic we'll get more of them imminently :)
- **[1226657323810160640](https://x.com/JagexAsh/status/1226657323810160640)** (2020-02-10)
  > Ash: We're a little restricted at present for making NPCs run, and the kind of hack that we usually do to pretend they're running would also block such an option from working! [...]

Ctrl-click:

- **[798572200785743872](https://x.com/JagexAsh/status/798572200785743872)** (2016-11-15)
  > Ash: [...] Unfortunately there was no way at the time to override the CTRL+click Run behaviour.
- **[1015612691204005888](https://x.com/JagexAsh/status/1015612691204005888)** (2018-07-07)
  > Ash: I'm afraid the engine feature we got for CTRL+Click running isn't flexible enough to do what you ask [...]
- **[1430953178950930433](https://x.com/JagexAsh/status/1430953178950930433)** (2021-08-26) — ctrl-click-to-*walk* was added Aug 2021:
  > Ash: They did change. I understand the ability to CTRL+click for walking while you'd got Run mode enabled had been a player request for years, though the engine's only just got the ability to do it. A toggle does sound very appealing for it, if the engine can offer that in future.
- **[1654115792643035136](https://x.com/JagexAsh/status/1654115792643035136)** (2023-05-04)
  > Ash: Might it be associated with your 'Ctrl+click to invert run mode' setting? That can enable/disable walking while Run is enabled.

Run mode affects script delays, not the path — see `playerwalk3`:

```1:8:content/scripts/player/playerwalk.rs2
// https://x.com/JagexAsh/status/1605130887292751873
[proc,playerwalk3](coord $coord)
p_walk($coord);
if (%option_run = ^player_run_off) {
    p_delay(max(0, sub(distance(coord, $coord), 1)));
} else {
    p_delay(max(0, sub(divide(distance(coord, $coord), 2), 1)));
}
```
(`LostCity_Server/content/scripts/player/playerwalk.rs2:1`)

---

## 12. Tick order and PID

- **[1092485515859902464](https://x.com/JagexAsh/status/1092485515859902464)** (2019-02-04) — the definition:
  > Ash: PID's not the same as the random number picked for a loot table. It's about the order in which the game processes each player's actions for each server tick.
- **[720915119308468224](https://x.com/JagexAsh/status/720915119308468224)** (2016-04-15)
  > Ash: One player's action will always be processed before the other. Randomising it each tick didn't please players either.
- **[736538634942308352](https://x.com/JagexAsh/status/736538634942308352)** (2016-05-28)
  > Ash: They're all executed one at a time independently of each other, even if it's within a tick.
- **[878296697566351360](https://x.com/JagexAsh/status/878296697566351360)** (2017-06-23)
  > Ash: It's going to have to execute actions in one order or the other, even if they occur in the same tick.
- **[878223073811419137](https://x.com/JagexAsh/status/878223073811419137)** (2017-06-23)
  > Ash: Even within a tick, actions occur in a particular order. [...]
- **[943518554225364999](https://x.com/JagexAsh/status/943518554225364999)** (2017-12-20)
  > Ash: It'd execute one at a time, even if that's within one tick. The order would depend on the players' PID.
- **[1268264332418322433](https://x.com/JagexAsh/status/1268264332418322433)** (2020-06-03) — state is mutated between players in the same tick:
  > Ash: It'd process each player's action, player by player, in an order determined by PID. So the first player would change its defence, and that's the state it would be in when the second player's spec got processed.
- **[1124053687918374925](https://x.com/JagexAsh/status/1124053687918374925)** (2019-05-02)
  > Ash: It'd process them one at a time, in the order of PID. The second would calculate off whatever stat the first had left, and the third would calculate off whatever stat the second had left.
- **[1092812078778384390](https://x.com/JagexAsh/status/1092812078778384390)** (2019-02-05)
  > Ash: PID originally was based on IP address, which was indeed eventually changed.
- **[1628331875029725185](https://x.com/JagexAsh/status/1628331875029725185)** (2023-02-22) — PID shuffles; client ordering is separate:
  > Ash: The game client has an internal ID for each player too, and prioritises them based on that (unless there's a special override in effect, which we sometimes use). It's distinct from the PID system you're familiar with, since that shuffles periodically.

### 12.1 NPC vs player processing order — explicitly NOT known to Ash

Important caveat for anyone wanting to cite Ash on "NPCs move before players":

- **[1227772829036425217](https://x.com/JagexAsh/status/1227772829036425217)** (2020-02-13)
  > Ash: NPCs will have internal IDs like players do, and will presumably have their actions executed in that order. **I can't see the engine's code to confirm whether NPCs are processed before or after players**, but it probably doesn't change back and forth.
- **[1261710733466886149](https://x.com/JagexAsh/status/1261710733466886149)** (2020-05-16)
  > Ash: NPCs have internal IDs, like players have PID. It should depend on that.
- **[1118574335080501248](https://x.com/JagexAsh/status/1118574335080501248)** (2019-04-17)
  > Ash: The engine doesn't document how it'd sort that list, and I wouldn't be able to inspect its code to find out. I suspect it'd be based on the NPCs' internal IDs (a bit like PID) in some way.
- **[1381630889088671746](https://x.com/JagexAsh/status/1381630889088671746)** (2021-04-12)
  > Ash: The engine doesn't document a priority order for them. It may well turn out to be the NPC equivalent of PID, though for players I believe the spells tend to pick the north/west players slightly more than south/east.
- **[631405994225049600](https://x.com/JagexAsh/status/631405994225049600)** (2015-08-12) — one concrete intra-tick ordering he *does* give:
  > Ash: The retaliation queue and damage queue are processed in the same tick, in that order, but the NPC starts moving in the next tick.

### 12.2 Latency and ticks

- **[1001819030011904001](https://x.com/JagexAsh/status/1001819030011904001)** (2018-05-30)
  > Ash: It wouldn't change how the server ticks, but it'd affect when your instructions reach the server and when its response reaches you. So your instructions might end up arriving in a later tick than you'd hoped, for example.

---

## 13. Client/server position sync, animation lag, map flag

- **[1393588233112629249](https://x.com/JagexAsh/status/1393588233112629249)** (2021-05-15) — the cleanest statement of the model:
  > Ash: It can sometimes be unclear exactly what tile you're in at what moment, since the server treats movement on a tick-by-tick basis, yet the client shows it more smoothly with animations. A little latency at the time could affect you very badly in such gameplay. Sorry.
- **[1004794088560971776](https://x.com/JagexAsh/status/1004794088560971776)** (2018-06-07)
  > Ash: NPCs' walk animations tend to make them lag their true position by a tick. [...]
- **[540095582422183936](https://x.com/JagexAsh/status/540095582422183936)** (2014-12-03)
  > Ash: Some of that is from the client smoothing the NPC's walk, so it's not always drawn in the right spot. Not sure it's fixable.
- **[1318850063532175360](https://x.com/JagexAsh/status/1318850063532175360)** (2020-10-21)
  > Ash: If they walk a tile in the same tick as they spawn, your client may show that as though it simply spawned on the other tile. [...]
- **[1575532939974164482](https://x.com/JagexAsh/status/1575532939974164482)** (2022-09-29)
  > Ash: [...] sometimes they might not be transmitted to your client until after they've walked to an adjacent tile.
- Desync/relog family: **[828664080307195909](https://x.com/JagexAsh/status/828664080307195909)**,
  **[931950104159969280](https://x.com/JagexAsh/status/931950104159969280)**,
  **[1224645174720114689](https://x.com/JagexAsh/status/1224645174720114689)**,
  **[749248277627961344](https://x.com/JagexAsh/status/749248277627961344)** — all say the
  same thing: the *server* position is authoritative, the client sometimes draws you
  elsewhere (usually after map loading at a bad moment), and relogging resyncs.
- **[634644392352063488](https://x.com/JagexAsh/status/634644392352063488)** (2015-08-21)
  > Ash: The game's currently unable to show accurately where anyone's even standing. Once that's sorted, we can look at the pathing.
- **Map flag** — the closest statement found (§15 notes this is otherwise a gap):
  **[1247806482529153024](https://x.com/JagexAsh/status/1247806482529153024)** (2020-04-08)
  > Ash: The yellow cross thing appeared, so your client knew you'd clicked, but I guess the server didn't find out in time.

  i.e. the click marker is drawn client-side on click, independent of the server acting on it.

---

## 14. Movement/interaction script surface (with LostCity citations)

Every entry here is a tweet the LostCity engine cites directly at the given file:line.

| Command | Tweet | Verbatim | LostCity file:line |
|---|---|---|---|
| `p_walk()` | [1605130887292751873](https://x.com/JagexAsh/status/1605130887292751873) | "The underlying command is p_walk(), which sets the player's destination. The procs apply different delays while the player walks there; that particular one respects the player's run mode setting and reduces the delay if they're running instead of walking." | `PlayerOps.ts:463`; `content/scripts/player/playerwalk.rs2:1` |
| `p_walk()` respects collision | [1698248664349614138](https://x.com/JagexAsh/status/1698248664349614138) | "p_walk() respects collision, which is why it is not suitable for forcing the player through that kind of door." | `PlayerOps.ts:464` |
| `p_teleport()` / `p_telejump()` | [1697517518007541917](https://x.com/JagexAsh/status/1697517518007541917) | "p_teleport() - a command that 'forcibly' moves the player and enables walk animations if the distance is short, so it's good for doors like that. \| p_telejump() is an alternative command that forcibly moves the player and never plays walk animations." | `PlayerOps.ts:439`, `:446` |
| `p_teleport()` for firemaking | [1790684996480442796](https://x.com/JagexAsh/status/1790684996480442796) | "p_teleport() \| You can succeed in lighting the fire immediately, but if a re-try is required, p_opobj() is used to request it." | `PlayerOps.ts:447`, `:1046` |
| `p_exactmove()` | [1684174294086033410](https://x.com/JagexAsh/status/1684174294086033410) | "It's p_exactmove(). It takes start+end coordinates, start+end delays, and a direction to face on arrival." | `PlayerOps.ts:938` |
| `p_exactmove()` does not self-delay | [1684478874703343616](https://x.com/JagexAsh/status/1684478874703343616) | "Any p_delay() would be called separately. This allows a dev to trigger server effects mid-way through the p_exactmove(), such as making the player say 'ow' as they fall down a long slope, etc." | `PlayerOps.ts:374` |
| `p_arrivedelay()` | [1648254846686904321](https://x.com/JagexAsh/status/1648254846686904321) | "The arrivedelay command was in use all the way back to 2004, from what I recall. However, an animation can be configured to defer itself until walking is complete; changing the animation's settings may give the impression that arrivedelay has been added." | `PlayerOps.ts:358` |
| `p_arrivedelay()` max 1 tick; `npc_arrivedelay()` exists | [1432296606376906752](https://x.com/JagexAsh/status/1432296606376906752) | "Only via p_arrivedelay(), which has a max duration of 1 tick. If we wanted to pause until something arrived, we'd need to call p_delay() repeatedly in a loop until it arrived. Which should be done *very* cautiously since it might never arrive. npc_arrivedelay() does exist, yes." | `NpcOps.ts:556` |
| `p_stopaction()` | [1780904271610867780](https://x.com/JagexAsh/status/1780904271610867780) | "Emotes use a walktrigger to call p_stopaction(), actually. Maybe the difference is that it's via a walktrigger rather than being done directly in the ranging target's op/ap trigger." | `PlayerOps.ts:429` |
| `p_clearpendingaction()` | [1780230057023181259](https://x.com/JagexAsh/status/1780230057023181259) | "p_clearpendingaction() can stop your pending interactions but leave an existing walk-request. For example, if you click to chop a tree then equip an item before you reach it, you carry on walking to the tree." | `PlayerOps.ts:434` |
| `p_op*` implies a walk | [1791472651623370843](https://x.com/JagexAsh/status/1791472651623370843) | "Yes." (to: would `p_opobj` make the player walk to the obj?) | `PlayerOps.ts:386`, `:403`, `:417`, `:1045`, `:1065`, `:1191` |
| `p_locmerge()` | [1684232225397657602](https://x.com/JagexAsh/status/1684232225397657602) | "p_locmerge()" | `PlayerOps.ts:977` |
| `p_animprotect()` | [1806246992797921391](https://x.com/JagexAsh/status/1806246992797921391) | "There is indeed a command that blocks the standard anim() command until the effect is revoked. It's called p_animprotect(), which takes an INT for 'on' or 'off'." | `PlayerOps.ts:195`, `:1235` |
| `busy()` | [1653407769989349377](https://x.com/JagexAsh/status/1653407769989349377) | "Yup, it's called busy() :P" | `PlayerOps.ts:949` |
| `busy2()` includes walking | [1791053667228856563](https://x.com/JagexAsh/status/1791053667228856563) | "busy2() would detect that the player is engaged in some action, which I believe would include walking somewhere, but also actions like mining a rock." | `PlayerOps.ts:954` |
| `getwalktrigger()` | [1779778790593372205](https://x.com/JagexAsh/status/1779778790593372205) | "Yes for getwalktrigger(). We don't have clearwalktrigger(), though one could always set a walktrigger that does nothing." | `PlayerOps.ts:1096` |
| `npc_walktrigger()` | [1780932943038345562](https://x.com/JagexAsh/status/1780932943038345562) | "By using the command npc_walktrigger(X,VAL) we can tell the NPC to execute its [ai_queueX,NPC] trigger, with VAL passed in as an INT value, when the NPC next tries to walk. \| It may call npc_walk(npc_coord)." | `NpcOps.ts:465`, `:497`; `PlayerOps.ts:375` |
| `npc_walk()` / `npc_coord()` | [1821835323808026853](https://x.com/JagexAsh/status/1821835323808026853) | "maybe npc_setmode(none) needn't be there. But it does no harm." | `NpcOps.ts:87`, `:205`, `:464` |
| `npc_setmode(playerescape)` | [1795184135327089047](https://x.com/JagexAsh/status/1795184135327089047) | see §7.1 | `NpcOps.ts:204` |
| `npc_attackrange()` | [1614498680144527360](https://x.com/JagexAsh/status/1614498680144527360) | see §7.4 | `NpcOps.ts:535` |
| `npc_hasop()` | [1821492251429679257](https://x.com/JagexAsh/status/1821492251429679257) | see §4.3 | `NpcOps.ts:540` |
| `npc_huntall()` / `npc_find()` | [1796460129430433930](https://x.com/JagexAsh/status/1796460129430433930) | see §7.5 | `NpcOps.ts:338`, `:350` |
| `npc_findallany()` | [1796878374398246990](https://x.com/JagexAsh/status/1796878374398246990) | "npc_findallany()" | `NpcOps.ts:417` |
| hunt targets by type/category | [1799793914595131463](https://x.com/JagexAsh/status/1799793914595131463) | see §7.5 | `entity/Npc.ts:255` |
| hunt "attackable" == op2 | [1821236327150710829](https://x.com/JagexAsh/status/1821236327150710829) | see §4.3 | `entity/Npc.ts:254`; `NpcOps.ts:339` |
| `npc_queue()` | [1570357528172859392](https://x.com/JagexAsh/status/1570357528172859392) | "It's triggered via RuneScript. The command would be npc_queue(3,0,0) - the first zero is an integer parameter [...] and the second is a number of ticks before the queue should be allowed to execute." | `NpcOps.ts:158` |
| `blockwalk` | [1677654049238265857](https://x.com/JagexAsh/status/1677654049238265857) | see §6.1 | `entity/BlockWalk.ts:1` |
| `moverestrict` | [1678810351091974159](https://x.com/JagexAsh/status/1678810351091974159) | see §6.2 | `entity/MoveRestrict.ts:1` |
| step fallback / map blocking | [1727609489954664502](https://x.com/JagexAsh/status/1727609489954664502) | see §6.5 | `entity/PathingEntity.ts:677` |
| `p_finduid()` | [1652956821798223873](https://x.com/JagexAsh/status/1652956821798223873) | "Yes, that's what the command's primarily for." (acquiring protected access) | `PlayerOps.ts:77` |
| queue types | [1698973910048403797](https://x.com/JagexAsh/status/1698973910048403797) | "When I started in 2004, the language had normal & long queues. weakqueue() came later that year, and I think strongqueue() came after that." | `PlayerOps.ts:99`, `:123`, `:147` |
| `clearqueue()` / `getqueue()` | [1821831590906859683](https://x.com/JagexAsh/status/1821831590906859683) | "It's quite common for a queue to use clearqueue() when it executes, thus getting rid of any duplicate requests for it. And there's nothing wrong with using getqueue() to ask if any more requests are pending beyond the current one." | `PlayerOps.ts:148`, `:959`, `:1101` |
| `world_delay()` | [1814230119411540058](https://x.com/JagexAsh/status/1814230119411540058) | "world_delay() doesn't set queues. It merely pauses the existing script, which continues after the command has finished. Other scripts cannot query/cancel/modify the script while it's paused." | `ServerOps.ts:166` |
| `p_pausebutton()` | [1389465615631519744](https://x.com/JagexAsh/status/1389465615631519744) | "The p_pausebutton() command sets that pointer to the pause button that the player clicks [...]" | `PlayerOps.ts:424` |
| `last_int()` | [1782377050021523947](https://x.com/JagexAsh/status/1782377050021523947) | "The command is indeed last_int()." | `PlayerOps.ts:254` |

Additional movement-adjacent commands, not cited by LostCity but confirmed verbatim:

- **[1653385326763692035](https://x.com/JagexAsh/status/1653385326763692035)** (2023-05-02)
  > Ash: Arrivedelay is a thing. Loaddelay exists simply by calling p_delay() if map-loading is in progress at the time, rather than being a separate command. There is no turndelay, though it'd be useful for such moments as you describe.
- **[1653406411689189378](https://x.com/JagexAsh/status/1653406411689189378)** (2023-05-02)
  > Ash: Yes, the character's rotation is known, and developers will often use it to add a p_delay() manually for such situations. It can feel kinda clunky for the player, by the way.
- **[1117568216195305472](https://x.com/JagexAsh/status/1117568216195305472)** (2019-04-14) — interrupt model:
  > Ash: No, the repeated action system was intentionally added to the engine in a way that just about anything could interrupt, which would include the action you're doing. Sorry. I think it was to prevent players being stuck in a skill loop.
- **[1410652521115291653](https://x.com/JagexAsh/status/1410652521115291653)** (2021-07-01) — the 2021 pending-action rework:
  > Ash: [...] the updated version had more predictable behaviour about what order we could expect pending actions to take place, and let us check info about what the player had clicked to do, e.g. what NPC they clicked on, where they were walking to, etc.

---

## 15. Gaps and cautions

Things that were searched for exhaustively and **not** found — do not invent an Ash
quote for these:

1. **Map flag / minimap flag semantics.** Beyond
   [1247806482529153024](https://x.com/JagexAsh/status/1247806482529153024) (§13), there is no
   Ash statement describing when the flag is set, cleared, or how it relates to the walk
   queue. Every other "flag" hit in 410k tweets is about world-list country flags,
   quest varbits, or the Report button.
2. **The pathfinding algorithm itself** (BFS, checkpoint/waypoint compression, the
   64/128-tile search bound, the "closest reachable tile" fallback). Ash repeatedly says
   he cannot read the engine's pathing code (§2). He never describes the algorithm.
3. **Explicit NPC-before-player tick ordering.** He explicitly declines to confirm it
   (§12.1). Any such claim must come from client/server behavioural evidence, not Ash.
4. **`blockwalk` value `PLAYER`.** Ash lists only `none`/`NPC`/`all` for NPCs; LostCity's
   fourth enum value is not Ash-sourced.
5. **Aug 2024 → Nov 2025.** The archive ends Jul 2023 and the last LostCity-cited tweet is
   2024-08-13. Statements from the final ~15 months of Ash's account are not covered here.

### Corroborating detail on where Ash's own knowledge stops

He is a content developer, not an engine developer, and says so constantly. Treat his
statements about **config surface, script commands, content-side behaviour and
observed in-game outcomes** as authoritative; treat his statements about **engine
internals** as informed inference unless he says he checked. The relevant admissions are
gathered in §2 ("not something I'd be able to read"), §12.1, and
[1461687803344039949](https://x.com/JagexAsh/status/1461687803344039949),
[1594221116431732738](https://x.com/JagexAsh/status/1594221116431732738)
("Can't see what the game engine's doing under the hood for line-of-sight checks, sorry.").
