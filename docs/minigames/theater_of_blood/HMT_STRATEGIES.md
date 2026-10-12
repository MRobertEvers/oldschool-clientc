# Hard Mode ToB (HMT), five raiders: strategy notes for the solvers

Collected 2026-10-12 for the five-man Hard Mode relay
(`script/plugins/quest_driver/tob_relay.lua`, `tob_<room>_hm.lua`). The team
is the Learner/Void kit (owner: "Same void + whips"), so every rule here that
leans on a scythe, a twisted bow or chinchompas is marked as such.

## Sources

- [S] Samsquanches Learner HMT Guide, https://pastebin.com/6VELEC6z (2025-07-17)
- [HMR] Help Me RNG, Learner Guide for HM ToB, https://www.youtube.com/watch?v=NWtY3_rqdt4
- [IND2] Indarkment, Hardmode TOB Secrets, https://www.youtube.com/watch?v=1RdKNlRIqqY
- [MINA-Y] RS Mina, HMT Verzik P3 yellows, https://www.youtube.com/watch?v=lJfX9btf3Q4
- [MINA-P2] RS Mina, HMT Verzik P2 scythe walk (tile markers in the description), https://www.youtube.com/watch?v=1EFBujyA170
- [SQP3] 07samsquanches, HMT learner P3 Verzik, https://www.youtube.com/watch?v=bShERSDggYw
- [ROB] Rob, 3000+ KC HMT clears, https://www.youtube.com/watch?v=0U8D9r1bPA8
- [BEL-S] Beleti south freeze, https://www.youtube.com/watch?v=XSvM0L-4TQk ; [BEL-R] range POV, https://www.youtube.com/watch?v=TLpbRqXiLQo
- [PLK-B] Plank2g HMT Bloat, https://www.youtube.com/watch?v=F39OKyUZUpc
- [TASTY] TastyLife HMT overview, https://www.youtube.com/watch?v=ia4Zhccwz1E
- [AVG] HMT melee DPS 5s/4s, https://www.youtube.com/watch?v=Xujtg9qW134
- [AATY] aatykon, The Art of the Tank, https://www.youtube.com/watch?v=Yv1bNQwvPXc
- [HMR2] Help Me RNG mentor raid, https://www.youtube.com/watch?v=2sI33QTHy-E
- [WHM] https://oldschool.runescape.wiki/w/Theatre_of_Blood/Hard_Mode (= sources/wiki_Theatre_of_Blood_Hard_Mode.wikitext)
- [BLM] https://blert.io/guides/tob/nylocas/mechanics
- local: sources/wiki_*.wikitext, sources/transcripts/ (Plank2g HMT Nylo, Indarkment [IND], Beleti mage [BEL-M], Define Alive yt__QXdNAZh7Yo)

## Checks against the content's numbers

| Number | Content | Sources | Verdict |
|---|---|---|---|
| Prinkipas hp | 300/350/400 by scale 3/4/5 | wiki infobox 400, scaledhp; Jagex trio scaling 75% | agrees (by scale, not by wave) |
| HM pillar hp at five | 150 (cache anchor) | Normal 130 at five (2018 newspost); Plank2g "in hard mode the pillars have less hp" | open: possibly too high |
| Vasilias ranged splash | radius 2 | wiki Vasilias "within 2 squares ... 3-tile gap"; HM page "within 3" | agrees; bots keep 4 apart where they can |
| Yellow strikes | 3 per player, 3 apart, 14/17/20 | [S] "3 ticks apart", [MINA-Y] ~13 ticks to the first | agrees |
| Green ball | kills un-bounced with 2+ alive | [WHM], wiki Verzik | agrees; bounce damage disputed (89/74/49 vs 74/54/24) |
| Realm tornado | last runner in orb order past row 3 | [WHM] :342 | content fixed 2026-10-12 (it was any runner) |
| Maiden crabs | 10 at 2-5 | wiki Maiden, blert | agrees |
| Nylo cap | 15 early, 24 from wave 20; a demi counts 3; demi waves stall 16 | [BLM] | agrees |

## Maiden

- Blood spawns are invulnerable and their trails permanent; freeze them before
  they spread ("essential", [IND]); a splat stepped in guarantees a spawn [S].
- Freezes: N1234 / S12 standard; learners N123 / S124 [ROB]; both freezers
  take 4s in learner teams [HMR]. Freezers never melee her.
- Kill every crab fully, never leak at 1 hp [S]. The 30s skip is a 4s/5s
  speed trick, aborted when two leak.
- Tank: swap after 3 storms (her third consecutive hit is the big one);
  the melee steps 2 tiles back between hits to pass aggro [IND], [S].
- Void kit: the warhammer is the only defence spec, so every seat hammers.

## Bloat

- Flesh falls all fight, faster from down 3 [PLK-B]; he can turn every 16
  ticks, often 2-4 times a lap, and right as he wakes.
- Let him chase you; stay diagonally opposite; tick-walk one tile at a time
  so a shadow can be stepped off [HMR], [AVG], [S].
- Leave the down after 5-6 whip hits; never take a stomp; a deathless 3-down
  beats a 2-down [IND], [PLK-B].

## Nylocas (the room the bots fail)

- Wrong style is reflected and nulls: AoE only on a same-style clump.
- Priority: aggros (dead before the middle), smalls on pillars, bigs on
  pillars; walkers not yet chewing can wait. Late waves: the weakest pillar
  whatever its colour ([HMR2]: "don't care if it's not your colour").
- Commit early: save a sinking pillar or let it go and guard the other three;
  be at high hp when it falls (50 to everyone).
- Roles for five: two mages (split east/west to wave 21, then one on new
  spawns and one on chewers), one ranger on the blowpipe at 2 ticks, two
  melee on melee doubles and bigs on pillars. Tick-fill always: time out of
  combat is the first reason teams fail the waves [Plank2g].
- Prinkipas: wave 10 by everyone but the ranger; wave 20 by everyone;
  wave 30 prayed against and left until the waves are clear (unless the
  pillars are above 30%). Each style hits only on its colour; never mage it
  unless nearly dead [HMR], [IND2], [ROB].
- Vasilias: positions called before it lands, the ranger anchors on the
  death-dot tile and never moves. Ranged form: 3+ apart (bots 4). Magic form:
  everyone on the anchor. Melee form: stand anywhere, one off the dot. Pre-move
  after its second attack of a form: in ranged -> to the dot; in magic ->
  spread [HMR]. Prayer before movement before attacking.

## Sotetseg

- All but one in the realm; the maze split by orb order into contiguous
  segments; each runner sees only its own; runner 1 leads, the rest follow and
  take over; hand off fast, standing still invites the tornado; a runner can
  always step one tile north of its segment for room.
- The realm tornado rises when the LAST runner passes row 3; the one up top
  waits on row 3 until the team is past row 6.
- Two death balls: everyone else on the death-dot tile in front of his head,
  the two targets beside it, one west one east (both on one side: the one
  nearer the dot stays); split the first set, tick-eat later ones.
  The solver puts the targets on the dot too: the content splits a ball among
  everyone within 1 of its target, so beside-the-dot targets made each ball a
  four-way split with the dot raiders taking a share of both (up to 94 of a
  188 pair); all five on it is a five-way split of each, 75 at most.
- The quadrant called here is your Xarpus, Verzik P2 and yellows quadrant.

## Xarpus

- The outer two rings poisoned except the six entrance tiles: enter through
  the middle; melee him; keep a clean tile in each quadrant for P3.
- P3: he faces the quadrant that hit him, the tick after. Attack only when he
  is not facing you, quadrants on different ticks; two sharing a quadrant
  attack on the same tick; no weapon switches.

## Verzik

- P1: each hit pillar drops three 1x1 rubble within 3 of its edge (35 + stun):
  alternate the two front pillars after each attack.
- P2: pools last 16 (four live at once): rotate a 2x2 square behind the
  attack tile with a fifth bail-out diagonal; always Protect from Missiles;
  stay above 70-80; nylos persist, killed on their colour; surviving reds
  explode on re-absorb (~19 to adjacent).
- P3: the tank is re-picked every special, everyone tanks; crabs never expire,
  kill them before webs; stop near 25% until yellows are done.
- Yellows: three pools per player in the Sotetseg quadrant (middle three if
  your quadrant has none); two one-tick moves between strikes; kite the
  tornado away before the pools come.
- Webs: she throws at the raiders' own tiles, three at a time (wiki); the
  five stack on one tile beside her from the south and step one tile
  clockwise together when a web thrown at it is a tick from landing (every
  4 ticks here), swinging on every tile (`QD.raid._vzp3_web_run`). Enraged,
  each seat kites its tornado instead, avoiding the webs its own tiles draw.
- Green ball: the target follows a teammate (it bounces to an adjacent player
  at impact); never back to the last holder; pass twice if possible.
- 5% heal: whoever makes the second webs gets the kill.
