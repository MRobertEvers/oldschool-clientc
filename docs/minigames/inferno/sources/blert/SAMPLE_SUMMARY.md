# Blert Inferno sample: 20 challenges, 1235 wave streams

## Tick-0 set per wave (pillars excluded)
Event: NPC_SPAWN(7) on tick 0 (the wave's set; spawn TIME is clamped, see above).
Each line: how many runs showed that exact multiset.

wave 1  runs 20  batx1 nibblerx3
wave 2  runs 20  batx2 nibblerx3
wave 3  runs 20  nibblerx6
wave 4  runs 20  blobx1 nibblerx3
wave 5  runs 20  batx1 blobx1 nibblerx3
wave 6  runs 20  batx2 blobx1 nibblerx3
wave 7  runs 20  blobx2 nibblerx3
wave 8  runs 20  nibblerx6
wave 9  runs 20  meleerx1 nibblerx3
wave 10 runs 20  batx1 meleerx1 nibblerx3
wave 11 runs 20  batx2 meleerx1 nibblerx3
wave 12 runs 20  blobx1 meleerx1 nibblerx3
wave 13 runs 20  batx1 blobx1 meleerx1 nibblerx3
wave 14 runs 20  batx2 blobx1 meleerx1 nibblerx3
wave 15 runs 20  blobx2 meleerx1 nibblerx3
wave 16 runs 20  meleerx2 nibblerx3
wave 17 runs 20  nibblerx6
wave 18 runs 20  nibblerx3 rangerx1
wave 19 runs 20  batx1 nibblerx3 rangerx1
wave 20 runs 20  batx2 nibblerx3 rangerx1
wave 21 runs 20  blobx1 nibblerx3 rangerx1
wave 22 runs 20  batx1 blobx1 nibblerx3 rangerx1
wave 23 runs 20  batx2 blobx1 nibblerx3 rangerx1
wave 24 runs 20  blobx2 nibblerx3 rangerx1
wave 25 runs 20  meleerx1 nibblerx3 rangerx1
wave 26 runs 20  batx1 meleerx1 nibblerx3 rangerx1
wave 27 runs 20  batx2 meleerx1 nibblerx3 rangerx1
wave 28 runs 19  blobx1 meleerx1 nibblerx3 rangerx1
wave 29 runs 19  batx1 blobx1 meleerx1 nibblerx3 rangerx1
wave 30 runs 19  batx2 blobx1 meleerx1 nibblerx3 rangerx1
wave 31 runs 19  blobx2 meleerx1 nibblerx3 rangerx1
wave 32 runs 18  meleerx2 nibblerx3 rangerx1
wave 33 runs 18  nibblerx3 rangerx2
wave 34 runs 18  nibblerx6
wave 35 runs 18  magerx1 nibblerx3
wave 36 runs 18  batx1 magerx1 nibblerx3
wave 37 runs 18  batx2 magerx1 nibblerx3
wave 38 runs 18  blobx1 magerx1 nibblerx3
wave 39 runs 18  batx1 blobx1 magerx1 nibblerx3
wave 40 runs 18  batx2 blobx1 magerx1 nibblerx3
wave 41 runs 18  blobx2 magerx1 nibblerx3
wave 42 runs 18  magerx1 meleerx1 nibblerx3
wave 43 runs 18  batx1 magerx1 meleerx1 nibblerx3
wave 44 runs 18  batx2 magerx1 meleerx1 nibblerx3
wave 45 runs 17  blobx1 magerx1 meleerx1 nibblerx3
wave 46 runs 17  batx1 blobx1 magerx1 meleerx1 nibblerx3
wave 47 runs 17  batx2 blobx1 magerx1 meleerx1 nibblerx3
wave 48 runs 17  blobx2 magerx1 meleerx1 nibblerx3
wave 49 runs 16  magerx1 meleerx2 nibblerx3
wave 50 runs 16  magerx1 nibblerx3 rangerx1
wave 51 runs 16  batx1 magerx1 nibblerx3 rangerx1
wave 52 runs 16  batx2 magerx1 nibblerx3 rangerx1
wave 53 runs 16  blobx1 magerx1 nibblerx3 rangerx1
wave 54 runs 16  batx1 blobx1 magerx1 nibblerx3 rangerx1
wave 55 runs 16  batx2 blobx1 magerx1 nibblerx3 rangerx1
wave 56 runs 15  blobx2 magerx1 nibblerx3 rangerx1
wave 57 runs 15  magerx1 meleerx1 nibblerx3 rangerx1
wave 58 runs 15  batx1 magerx1 meleerx1 nibblerx3 rangerx1
wave 59 runs 15  batx2 magerx1 meleerx1 nibblerx3 rangerx1
wave 60 runs 15  blobx1 magerx1 meleerx1 nibblerx3 rangerx1
wave 61 runs 15  batx1 blobx1 magerx1 meleerx1 nibblerx3 rangerx1
wave 62 runs 15  batx2 blobx1 magerx1 meleerx1 nibblerx3 rangerx1
wave 63 runs 15  blobx2 magerx1 meleerx1 nibblerx3 rangerx1
wave 64 runs 15  magerx1 meleerx2 nibblerx3 rangerx1
wave 65 runs 15  magerx1 nibblerx3 rangerx2
wave 66 runs 14  magerx2 nibblerx3
wave 67 runs 14  jadx1
wave 68 runs 14  jadx3
wave 69 runs 13  zukx1 zuk_shieldx1

## Spawn tiles per wave
Event: NPC_SPAWN(7). 'first tick' = tick 0 = the tick the chat message `Wave: N`
arrived; every npc the client already held then is stamped tick 0 (a set, not a
spawn time). 'later' spawns carry a real spawn tick. 'runs' = runs that recorded
the wave; 'seen k of n' counts npc spawns at that tile over those runs.

wave 1  (runs 20)
    first tick  bat           (2280,5346)  seen 5 of 20 runs
    first tick  bat           (2272,5330)  seen 4 of 20 runs
    first tick  bat           (2258,5330)  seen 3 of 20 runs
    first tick  bat           (2280,5333)  seen 2 of 20 runs
    first tick  bat           (2262,5335)  seen 2 of 20 runs
    first tick  bat           (2260,5347)  seen 2 of 20 runs
    first tick  bat           (2279,5353)  seen 1 of 20 runs
    first tick  bat           (2273,5341)  seen 1 of 20 runs
    first tick  nibbler       (2266,5345)  seen 11 of 20 runs
    first tick  nibbler       (2265,5346)  seen 9 of 20 runs
    first tick  nibbler       (2266,5346)  seen 8 of 20 runs
    first tick  nibbler       (2267,5346)  seen 7 of 20 runs
    first tick  nibbler       (2267,5347)  seen 6 of 20 runs
    first tick  nibbler       (2265,5345)  seen 6 of 20 runs
    first tick  nibbler       (2266,5347)  seen 6 of 20 runs
    first tick  nibbler       (2265,5347)  seen 5 of 20 runs
    first tick  nibbler       (2267,5345)  seen 2 of 20 runs
    first tick  pillar        (2257,5349)  seen 20 of 20 runs
    first tick  pillar        (2267,5335)  seen 20 of 20 runs
    first tick  pillar        (2274,5351)  seen 20 of 20 runs
wave 2  (runs 20)
    first tick  bat           (2262,5335)  seen 7 of 20 runs
    first tick  bat           (2279,5353)  seen 6 of 20 runs
    first tick  bat           (2258,5330)  seen 6 of 20 runs
    first tick  bat           (2280,5333)  seen 5 of 20 runs
    first tick  bat           (2273,5341)  seen 4 of 20 runs
    first tick  bat           (2272,5330)  seen 4 of 20 runs
    first tick  bat           (2280,5346)  seen 3 of 20 runs
    first tick  bat           (2260,5347)  seen 3 of 20 runs
    first tick  bat           (2258,5353)  seen 2 of 20 runs
    first tick  nibbler       (2267,5346)  seen 12 of 20 runs
    first tick  nibbler       (2266,5346)  seen 11 of 20 runs
    first tick  nibbler       (2266,5347)  seen 8 of 20 runs
    first tick  nibbler       (2267,5347)  seen 8 of 20 runs
    first tick  nibbler       (2265,5346)  seen 7 of 20 runs
    first tick  nibbler       (2266,5345)  seen 4 of 20 runs
    first tick  nibbler       (2267,5345)  seen 4 of 20 runs
    first tick  nibbler       (2265,5347)  seen 4 of 20 runs
    first tick  nibbler       (2265,5345)  seen 2 of 20 runs
    first tick  pillar        (2257,5349)  seen 20 of 20 runs
    first tick  pillar        (2267,5335)  seen 20 of 20 runs
    first tick  pillar        (2274,5351)  seen 20 of 20 runs
wave 3  (runs 20)
    first tick  nibbler       (2267,5345)  seen 16 of 20 runs
    first tick  nibbler       (2265,5346)  seen 15 of 20 runs
    first tick  nibbler       (2266,5345)  seen 15 of 20 runs
    first tick  nibbler       (2267,5347)  seen 14 of 20 runs
    first tick  nibbler       (2266,5346)  seen 14 of 20 runs
    first tick  nibbler       (2266,5347)  seen 12 of 20 runs
    first tick  nibbler       (2267,5346)  seen 12 of 20 runs
    first tick  nibbler       (2265,5345)  seen 11 of 20 runs
    first tick  nibbler       (2265,5347)  seen 11 of 20 runs
    first tick  pillar        (2257,5349)  seen 20 of 20 runs
    first tick  pillar        (2267,5335)  seen 20 of 20 runs
    first tick  pillar        (2274,5351)  seen 20 of 20 runs
wave 4  (runs 20)
    first tick  blob          (2258,5330)  seen 4 of 20 runs
    first tick  blob          (2280,5333)  seen 4 of 20 runs
    first tick  blob          (2273,5341)  seen 3 of 20 runs
    first tick  blob          (2260,5347)  seen 3 of 20 runs
    first tick  blob          (2280,5346)  seen 2 of 20 runs
    first tick  blob          (2262,5335)  seen 1 of 20 runs
    first tick  blob          (2279,5353)  seen 1 of 20 runs
    first tick  blob          (2258,5353)  seen 1 of 20 runs
    first tick  blob          (2272,5330)  seen 1 of 20 runs
    first tick  nibbler       (2267,5347)  seen 11 of 20 runs
    first tick  nibbler       (2265,5347)  seen 9 of 20 runs
    first tick  nibbler       (2265,5345)  seen 8 of 20 runs
    first tick  nibbler       (2266,5345)  seen 7 of 20 runs
    first tick  nibbler       (2267,5346)  seen 6 of 20 runs
    first tick  nibbler       (2267,5345)  seen 6 of 20 runs
    first tick  nibbler       (2265,5346)  seen 5 of 20 runs
    first tick  nibbler       (2266,5346)  seen 5 of 20 runs
    first tick  nibbler       (2266,5347)  seen 3 of 20 runs
    first tick  pillar        (2257,5349)  seen 20 of 20 runs
    first tick  pillar        (2267,5335)  seen 20 of 20 runs
    first tick  pillar        (2274,5351)  seen 20 of 20 runs
    later       bloblet_mage  (2275,5343)  seen 3
    later       bloblet_mage  (2262,5349)  seen 3
    later       bloblet_mage  (2282,5335)  seen 3
    later       bloblet_mage  (2260,5332)  seen 2
    later       bloblet_mage  (2274,5350)  seen 2
    later       bloblet_mage  (2264,5337)  seen 1
    later       bloblet_mage  (2280,5352)  seen 1
    later       bloblet_mage  (2269,5353)  seen 1
    later       bloblet_mage  (2263,5335)  seen 1
    later       bloblet_mage  (2282,5348)  seen 1
    later       bloblet_mage  (2279,5355)  seen 1
    later       bloblet_mage  (2265,5337)  seen 1
wave 5  (runs 20)
    first tick  bat           (2258,5330)  seen 5 of 20 runs
    first tick  bat           (2260,5347)  seen 5 of 20 runs
    first tick  bat           (2280,5333)  seen 4 of 20 runs
    first tick  bat           (2272,5330)  seen 3 of 20 runs
    first tick  bat           (2258,5353)  seen 1 of 20 runs
    first tick  bat           (2273,5341)  seen 1 of 20 runs
    first tick  bat           (2262,5335)  seen 1 of 20 runs
    first tick  blob          (2280,5346)  seen 5 of 20 runs
    first tick  blob          (2280,5333)  seen 3 of 20 runs
    first tick  blob          (2273,5341)  seen 3 of 20 runs
    first tick  blob          (2258,5353)  seen 3 of 20 runs
    first tick  blob          (2279,5353)  seen 2 of 20 runs
    first tick  blob          (2260,5347)  seen 2 of 20 runs
    first tick  blob          (2262,5335)  seen 1 of 20 runs
    first tick  blob          (2258,5330)  seen 1 of 20 runs
    first tick  nibbler       (2266,5346)  seen 10 of 20 runs
    first tick  nibbler       (2267,5345)  seen 9 of 20 runs
    first tick  nibbler       (2265,5345)  seen 9 of 20 runs
    first tick  nibbler       (2267,5346)  seen 8 of 20 runs
    first tick  nibbler       (2266,5345)  seen 7 of 20 runs
    first tick  nibbler       (2267,5347)  seen 5 of 20 runs
    first tick  nibbler       (2265,5346)  seen 5 of 20 runs
    first tick  nibbler       (2265,5347)  seen 5 of 20 runs
    first tick  nibbler       (2266,5347)  seen 2 of 20 runs
    first tick  pillar        (2257,5349)  seen 20 of 20 runs
    first tick  pillar        (2267,5335)  seen 20 of 20 runs
    first tick  pillar        (2274,5351)  seen 20 of 20 runs
    later       bloblet_mage  (2279,5352)  seen 3
    later       bloblet_mage  (2282,5335)  seen 2
    later       bloblet_mage  (2274,5350)  seen 2
    later       bloblet_mage  (2260,5355)  seen 2
    later       bloblet_mage  (2282,5348)  seen 2
    later       bloblet_mage  (2275,5343)  seen 2
    later       bloblet_mage  (2262,5349)  seen 2
    later       bloblet_mage  (2279,5354)  seen 1
    later       bloblet_mage  (2264,5337)  seen 1
    later       bloblet_mage  (2261,5333)  seen 1
    later       bloblet_mage  (2273,5351)  seen 1
    later       bloblet_mage  (2279,5355)  seen 1
wave 6  (runs 20)
    first tick  bat           (2273,5341)  seen 7 of 20 runs
    first tick  bat           (2280,5333)  seen 7 of 20 runs
    first tick  bat           (2279,5353)  seen 5 of 20 runs
    first tick  bat           (2280,5346)  seen 5 of 20 runs
    first tick  bat           (2260,5347)  seen 5 of 20 runs
    first tick  bat           (2262,5335)  seen 3 of 20 runs
    first tick  bat           (2258,5330)  seen 3 of 20 runs
    first tick  bat           (2272,5330)  seen 3 of 20 runs
    first tick  bat           (2258,5353)  seen 2 of 20 runs
    first tick  blob          (2279,5353)  seen 3 of 20 runs
    first tick  blob          (2260,5347)  seen 3 of 20 runs
    first tick  blob          (2272,5330)  seen 3 of 20 runs
    first tick  blob          (2258,5353)  seen 2 of 20 runs
    first tick  blob          (2273,5341)  seen 2 of 20 runs
    first tick  blob          (2280,5333)  seen 2 of 20 runs
    first tick  blob          (2280,5346)  seen 2 of 20 runs
    first tick  blob          (2258,5330)  seen 2 of 20 runs
    first tick  blob          (2262,5335)  seen 1 of 20 runs
    first tick  nibbler       (2265,5347)  seen 11 of 20 runs
    first tick  nibbler       (2267,5346)  seen 9 of 20 runs
    first tick  nibbler       (2266,5347)  seen 7 of 20 runs
    first tick  nibbler       (2265,5345)  seen 7 of 20 runs
    first tick  nibbler       (2265,5346)  seen 6 of 20 runs
    first tick  nibbler       (2267,5345)  seen 6 of 20 runs
    first tick  nibbler       (2266,5346)  seen 6 of 20 runs
    first tick  nibbler       (2266,5345)  seen 6 of 20 runs
    first tick  nibbler       (2267,5347)  seen 2 of 20 runs
    first tick  pillar        (2257,5349)  seen 20 of 20 runs
    first tick  pillar        (2267,5335)  seen 20 of 20 runs
    first tick  pillar        (2274,5351)  seen 20 of 20 runs
    later       bloblet_mage  (2274,5350)  seen 3
    later       bloblet_mage  (2260,5355)  seen 2
    later       bloblet_mage  (2279,5354)  seen 2
    later       bloblet_mage  (2279,5355)  seen 2
    later       bloblet_mage  (2262,5349)  seen 2
    later       bloblet_mage  (2273,5333)  seen 2
    later       bloblet_mage  (2276,5348)  seen 1
    later       bloblet_mage  (2264,5337)  seen 1
    later       bloblet_mage  (2274,5349)  seen 1
    later       bloblet_mage  (2282,5348)  seen 1
    later       bloblet_mage  (2273,5355)  seen 1
    later       bloblet_mage  (2261,5333)  seen 1
wave 7  (runs 20)
    first tick  blob          (2273,5341)  seen 7 of 20 runs
    first tick  blob          (2280,5333)  seen 7 of 20 runs
    first tick  blob          (2279,5353)  seen 5 of 20 runs
    first tick  blob          (2260,5347)  seen 5 of 20 runs
    first tick  blob          (2272,5330)  seen 4 of 20 runs
    first tick  blob          (2258,5330)  seen 3 of 20 runs
    first tick  blob          (2280,5346)  seen 3 of 20 runs
    first tick  blob          (2258,5353)  seen 3 of 20 runs
    first tick  blob          (2262,5335)  seen 3 of 20 runs
    first tick  nibbler       (2267,5347)  seen 13 of 20 runs
    first tick  nibbler       (2266,5347)  seen 9 of 20 runs
    first tick  nibbler       (2265,5346)  seen 8 of 20 runs
    first tick  nibbler       (2267,5345)  seen 8 of 20 runs
    first tick  nibbler       (2265,5345)  seen 7 of 20 runs
    first tick  nibbler       (2267,5346)  seen 6 of 20 runs
    first tick  nibbler       (2266,5345)  seen 5 of 20 runs
    first tick  nibbler       (2265,5347)  seen 3 of 20 runs
    first tick  nibbler       (2266,5346)  seen 1 of 20 runs
    first tick  pillar        (2257,5349)  seen 20 of 20 runs
    first tick  pillar        (2267,5335)  seen 20 of 20 runs
    first tick  pillar        (2274,5351)  seen 20 of 20 runs
    later       bloblet_mage  (2262,5349)  seen 4
    later       bloblet_mage  (2275,5343)  seen 4
    later       bloblet_mage  (2282,5335)  seen 3
    later       bloblet_mage  (2274,5350)  seen 3
    later       bloblet_mage  (2273,5333)  seen 2
    later       bloblet_mage  (2279,5351)  seen 2
    later       bloblet_mage  (2279,5355)  seen 2
    later       bloblet_mage  (2278,5350)  seen 2
    later       bloblet_mage  (2264,5337)  seen 2
    later       bloblet_mage  (2266,5339)  seen 1
    later       bloblet_mage  (2273,5351)  seen 1
    later       bloblet_mage  (2282,5348)  seen 1
wave 8  (runs 20)
    first tick  nibbler       (2266,5346)  seen 16 of 20 runs
    first tick  nibbler       (2266,5345)  seen 15 of 20 runs
    first tick  nibbler       (2267,5346)  seen 15 of 20 runs
    first tick  nibbler       (2267,5345)  seen 14 of 20 runs
    first tick  nibbler       (2267,5347)  seen 13 of 20 runs
    first tick  nibbler       (2265,5347)  seen 13 of 20 runs
    first tick  nibbler       (2265,5345)  seen 12 of 20 runs
    first tick  nibbler       (2265,5346)  seen 12 of 20 runs
    first tick  nibbler       (2266,5347)  seen 10 of 20 runs
    first tick  pillar        (2257,5349)  seen 20 of 20 runs
    first tick  pillar        (2267,5335)  seen 20 of 20 runs
    first tick  pillar        (2274,5351)  seen 20 of 20 runs
wave 9  (runs 20)
    first tick  meleer        (2273,5341)  seen 5 of 20 runs
    first tick  meleer        (2279,5353)  seen 4 of 20 runs
    first tick  meleer        (2262,5335)  seen 2 of 20 runs
    first tick  meleer        (2258,5353)  seen 2 of 20 runs
    first tick  meleer        (2272,5330)  seen 2 of 20 runs
    first tick  meleer        (2260,5347)  seen 2 of 20 runs
    first tick  meleer        (2258,5330)  seen 2 of 20 runs
    first tick  meleer        (2280,5333)  seen 1 of 20 runs
    first tick  nibbler       (2265,5347)  seen 12 of 20 runs
    first tick  nibbler       (2266,5345)  seen 9 of 20 runs
    first tick  nibbler       (2266,5346)  seen 7 of 20 runs
    first tick  nibbler       (2267,5347)  seen 7 of 20 runs
    first tick  nibbler       (2265,5346)  seen 7 of 20 runs
    first tick  nibbler       (2266,5347)  seen 6 of 20 runs
    first tick  nibbler       (2267,5346)  seen 5 of 20 runs
    first tick  nibbler       (2267,5345)  seen 4 of 20 runs
    first tick  nibbler       (2265,5345)  seen 3 of 20 runs
    first tick  pillar        (2257,5349)  seen 20 of 20 runs
    first tick  pillar        (2267,5335)  seen 20 of 20 runs
    first tick  pillar        (2274,5351)  seen 20 of 20 runs
wave 10  (runs 20)
    first tick  bat           (2280,5333)  seen 6 of 20 runs
    first tick  bat           (2260,5347)  seen 4 of 20 runs
    first tick  bat           (2273,5341)  seen 3 of 20 runs
    first tick  bat           (2279,5353)  seen 2 of 20 runs
    first tick  bat           (2262,5335)  seen 2 of 20 runs
    first tick  bat           (2258,5353)  seen 1 of 20 runs
    first tick  bat           (2272,5330)  seen 1 of 20 runs
    first tick  bat           (2258,5330)  seen 1 of 20 runs
    first tick  meleer        (2273,5341)  seen 4 of 20 runs
    first tick  meleer        (2258,5353)  seen 4 of 20 runs
    first tick  meleer        (2280,5346)  seen 4 of 20 runs
    first tick  meleer        (2262,5335)  seen 3 of 20 runs
    first tick  meleer        (2258,5330)  seen 2 of 20 runs
    first tick  meleer        (2272,5330)  seen 2 of 20 runs
    first tick  meleer        (2279,5353)  seen 1 of 20 runs
    first tick  nibbler       (2266,5347)  seen 11 of 20 runs
    first tick  nibbler       (2267,5345)  seen 10 of 20 runs
    first tick  nibbler       (2266,5346)  seen 10 of 20 runs
    first tick  nibbler       (2267,5346)  seen 7 of 20 runs
    first tick  nibbler       (2265,5347)  seen 6 of 20 runs
    first tick  nibbler       (2265,5346)  seen 5 of 20 runs
    first tick  nibbler       (2266,5345)  seen 5 of 20 runs
    first tick  nibbler       (2265,5345)  seen 4 of 20 runs
    first tick  nibbler       (2267,5347)  seen 2 of 20 runs
    first tick  pillar        (2257,5349)  seen 20 of 20 runs
    first tick  pillar        (2267,5335)  seen 20 of 20 runs
    first tick  pillar        (2274,5351)  seen 20 of 20 runs
wave 11  (runs 20)
    first tick  bat           (2260,5347)  seen 6 of 20 runs
    first tick  bat           (2272,5330)  seen 6 of 20 runs
    first tick  bat           (2258,5353)  seen 5 of 20 runs
    first tick  bat           (2258,5330)  seen 5 of 20 runs
    first tick  bat           (2280,5333)  seen 5 of 20 runs
    first tick  bat           (2279,5353)  seen 4 of 20 runs
    first tick  bat           (2262,5335)  seen 3 of 20 runs
    first tick  bat           (2280,5346)  seen 3 of 20 runs
    first tick  bat           (2273,5341)  seen 3 of 20 runs
    first tick  meleer        (2272,5330)  seen 7 of 20 runs
    first tick  meleer        (2273,5341)  seen 3 of 20 runs
    first tick  meleer        (2280,5346)  seen 3 of 20 runs
    first tick  meleer        (2262,5335)  seen 2 of 20 runs
    first tick  meleer        (2258,5330)  seen 1 of 20 runs
    first tick  meleer        (2279,5353)  seen 1 of 20 runs
    first tick  meleer        (2260,5347)  seen 1 of 20 runs
    first tick  meleer        (2280,5333)  seen 1 of 20 runs
    first tick  meleer        (2258,5353)  seen 1 of 20 runs
    first tick  nibbler       (2267,5346)  seen 9 of 20 runs
    first tick  nibbler       (2266,5345)  seen 9 of 20 runs
    first tick  nibbler       (2266,5347)  seen 8 of 20 runs
    first tick  nibbler       (2265,5345)  seen 8 of 20 runs
    first tick  nibbler       (2266,5346)  seen 7 of 20 runs
    first tick  nibbler       (2267,5347)  seen 6 of 20 runs
    first tick  nibbler       (2267,5345)  seen 5 of 20 runs
    first tick  nibbler       (2265,5347)  seen 5 of 20 runs
    first tick  nibbler       (2265,5346)  seen 3 of 20 runs
    first tick  pillar        (2257,5349)  seen 20 of 20 runs
    first tick  pillar        (2267,5335)  seen 20 of 20 runs
    first tick  pillar        (2274,5351)  seen 20 of 20 runs
wave 12  (runs 20)
    first tick  blob          (2279,5353)  seen 5 of 20 runs
    first tick  blob          (2272,5330)  seen 5 of 20 runs
    first tick  blob          (2280,5333)  seen 3 of 20 runs
    first tick  blob          (2258,5353)  seen 2 of 20 runs
    first tick  blob          (2273,5341)  seen 2 of 20 runs
    first tick  blob          (2262,5335)  seen 2 of 20 runs
    first tick  blob          (2260,5347)  seen 1 of 20 runs
    first tick  meleer        (2258,5330)  seen 5 of 20 runs
    first tick  meleer        (2279,5353)  seen 3 of 20 runs
    first tick  meleer        (2258,5353)  seen 2 of 20 runs
    first tick  meleer        (2262,5335)  seen 2 of 20 runs
    first tick  meleer        (2280,5333)  seen 2 of 20 runs
    first tick  meleer        (2260,5347)  seen 2 of 20 runs
    first tick  meleer        (2280,5346)  seen 2 of 20 runs
    first tick  meleer        (2272,5330)  seen 2 of 20 runs
    first tick  nibbler       (2267,5345)  seen 11 of 20 runs
    first tick  nibbler       (2265,5347)  seen 8 of 20 runs
    first tick  nibbler       (2266,5347)  seen 8 of 20 runs
    first tick  nibbler       (2267,5346)  seen 8 of 20 runs
    first tick  nibbler       (2265,5345)  seen 7 of 20 runs
    first tick  nibbler       (2266,5346)  seen 6 of 20 runs
    first tick  nibbler       (2267,5347)  seen 5 of 20 runs
    first tick  nibbler       (2266,5345)  seen 4 of 20 runs
    first tick  nibbler       (2265,5346)  seen 3 of 20 runs
    first tick  pillar        (2257,5349)  seen 20 of 20 runs
    first tick  pillar        (2267,5335)  seen 20 of 20 runs
    first tick  pillar        (2274,5351)  seen 20 of 20 runs
    later       bloblet_mage  (2279,5354)  seen 3
    later       bloblet_mage  (2271,5334)  seen 3
    later       bloblet_mage  (2260,5355)  seen 2
    later       bloblet_mage  (2275,5343)  seen 2
    later       bloblet_mage  (2282,5335)  seen 2
    later       bloblet_mage  (2276,5345)  seen 1
    later       bloblet_mage  (2278,5354)  seen 1
    later       bloblet_mage  (2275,5336)  seen 1
    later       bloblet_mage  (2263,5350)  seen 1
    later       bloblet_mage  (2265,5338)  seen 1
    later       bloblet_mage  (2279,5352)  seen 1
    later       bloblet_mage  (2277,5350)  seen 1
wave 13  (runs 20)
    first tick  bat           (2258,5330)  seen 6 of 20 runs
    first tick  bat           (2260,5347)  seen 3 of 20 runs
    first tick  bat           (2258,5353)  seen 3 of 20 runs
    first tick  bat           (2272,5330)  seen 2 of 20 runs
    first tick  bat           (2273,5341)  seen 2 of 20 runs
    first tick  bat           (2279,5353)  seen 2 of 20 runs
    first tick  bat           (2280,5333)  seen 1 of 20 runs
    first tick  bat           (2262,5335)  seen 1 of 20 runs
    first tick  blob          (2258,5330)  seen 7 of 20 runs
    first tick  blob          (2279,5353)  seen 4 of 20 runs
    first tick  blob          (2260,5347)  seen 2 of 20 runs
    first tick  blob          (2258,5353)  seen 2 of 20 runs
    first tick  blob          (2280,5346)  seen 2 of 20 runs
    first tick  blob          (2280,5333)  seen 1 of 20 runs
    first tick  blob          (2273,5341)  seen 1 of 20 runs
    first tick  blob          (2262,5335)  seen 1 of 20 runs
    first tick  meleer        (2262,5335)  seen 5 of 20 runs
    first tick  meleer        (2273,5341)  seen 4 of 20 runs
    first tick  meleer        (2280,5333)  seen 4 of 20 runs
    first tick  meleer        (2280,5346)  seen 3 of 20 runs
    first tick  meleer        (2258,5353)  seen 2 of 20 runs
    first tick  meleer        (2272,5330)  seen 1 of 20 runs
    first tick  meleer        (2260,5347)  seen 1 of 20 runs
    first tick  nibbler       (2267,5345)  seen 10 of 20 runs
    first tick  nibbler       (2267,5346)  seen 9 of 20 runs
    first tick  nibbler       (2265,5346)  seen 7 of 20 runs
    first tick  nibbler       (2266,5345)  seen 7 of 20 runs
    first tick  nibbler       (2265,5347)  seen 7 of 20 runs
    first tick  nibbler       (2265,5345)  seen 6 of 20 runs
    first tick  nibbler       (2266,5347)  seen 6 of 20 runs
    first tick  nibbler       (2266,5346)  seen 4 of 20 runs
    first tick  nibbler       (2267,5347)  seen 4 of 20 runs
    first tick  pillar        (2257,5349)  seen 20 of 20 runs
    first tick  pillar        (2267,5335)  seen 20 of 20 runs
    first tick  pillar        (2274,5351)  seen 20 of 20 runs
    later       bloblet_mage  (2274,5350)  seen 2
    later       bloblet_mage  (2261,5333)  seen 2
    later       bloblet_mage  (2279,5355)  seen 2
    later       bloblet_mage  (2281,5336)  seen 1
    later       bloblet_mage  (2275,5343)  seen 1
    later       bloblet_mage  (2273,5355)  seen 1
    later       bloblet_mage  (2279,5352)  seen 1
    later       bloblet_mage  (2274,5354)  seen 1
    later       bloblet_mage  (2276,5350)  seen 1
    later       bloblet_mage  (2266,5353)  seen 1
    later       bloblet_mage  (2279,5354)  seen 1
    later       bloblet_mage  (2265,5337)  seen 1
wave 14  (runs 20)
    first tick  bat           (2258,5330)  seen 9 of 20 runs
    first tick  bat           (2280,5333)  seen 8 of 20 runs
    first tick  bat           (2272,5330)  seen 6 of 20 runs
    first tick  bat           (2258,5353)  seen 4 of 20 runs
    first tick  bat           (2279,5353)  seen 3 of 20 runs
    first tick  bat           (2280,5346)  seen 3 of 20 runs
    first tick  bat           (2273,5341)  seen 3 of 20 runs
    first tick  bat           (2262,5335)  seen 2 of 20 runs
    first tick  bat           (2260,5347)  seen 2 of 20 runs
    first tick  blob          (2272,5330)  seen 4 of 20 runs
    first tick  blob          (2279,5353)  seen 4 of 20 runs
    first tick  blob          (2273,5341)  seen 4 of 20 runs
    first tick  blob          (2258,5330)  seen 2 of 20 runs
    first tick  blob          (2260,5347)  seen 2 of 20 runs
    first tick  blob          (2258,5353)  seen 1 of 20 runs
    first tick  blob          (2262,5335)  seen 1 of 20 runs
    first tick  blob          (2280,5333)  seen 1 of 20 runs
    first tick  blob          (2280,5346)  seen 1 of 20 runs
    first tick  meleer        (2280,5346)  seen 5 of 20 runs
    first tick  meleer        (2279,5353)  seen 4 of 20 runs
    first tick  meleer        (2272,5330)  seen 4 of 20 runs
    first tick  meleer        (2258,5330)  seen 3 of 20 runs
    first tick  meleer        (2262,5335)  seen 2 of 20 runs
    first tick  meleer        (2273,5341)  seen 1 of 20 runs
    first tick  meleer        (2280,5333)  seen 1 of 20 runs
    first tick  nibbler       (2265,5345)  seen 12 of 20 runs
    first tick  nibbler       (2266,5346)  seen 10 of 20 runs
    first tick  nibbler       (2265,5346)  seen 8 of 20 runs
    first tick  nibbler       (2266,5347)  seen 8 of 20 runs
    first tick  nibbler       (2266,5345)  seen 5 of 20 runs
    first tick  nibbler       (2265,5347)  seen 5 of 20 runs
    first tick  nibbler       (2267,5345)  seen 5 of 20 runs
    first tick  nibbler       (2267,5346)  seen 4 of 20 runs
    first tick  nibbler       (2267,5347)  seen 3 of 20 runs
    first tick  pillar        (2257,5349)  seen 20 of 20 runs
    first tick  pillar        (2267,5335)  seen 20 of 20 runs
    first tick  pillar        (2274,5351)  seen 20 of 20 runs
    later       bloblet_mage  (2279,5355)  seen 3
    later       bloblet_mage  (2261,5333)  seen 2
    later       bloblet_mage  (2277,5350)  seen 2
    later       bloblet_mage  (2272,5334)  seen 1
    later       bloblet_mage  (2276,5348)  seen 1
    later       bloblet_mage  (2272,5352)  seen 1
    later       bloblet_mage  (2274,5350)  seen 1
    later       bloblet_mage  (2278,5348)  seen 1
    later       bloblet_mage  (2275,5350)  seen 1
    later       bloblet_mage  (2279,5353)  seen 1
    later       bloblet_mage  (2282,5335)  seen 1
    later       bloblet_mage  (2262,5349)  seen 1
wave 15  (runs 20)
    first tick  blob          (2258,5330)  seen 8 of 20 runs
    first tick  blob          (2258,5353)  seen 7 of 20 runs
    first tick  blob          (2273,5341)  seen 5 of 20 runs
    first tick  blob          (2279,5353)  seen 4 of 20 runs
    first tick  blob          (2280,5333)  seen 4 of 20 runs
    first tick  blob          (2260,5347)  seen 4 of 20 runs
    first tick  blob          (2272,5330)  seen 3 of 20 runs
    first tick  blob          (2280,5346)  seen 3 of 20 runs
    first tick  blob          (2262,5335)  seen 2 of 20 runs
    first tick  meleer        (2272,5330)  seen 3 of 20 runs
    first tick  meleer        (2279,5353)  seen 3 of 20 runs
    first tick  meleer        (2258,5330)  seen 3 of 20 runs
    first tick  meleer        (2258,5353)  seen 3 of 20 runs
    first tick  meleer        (2262,5335)  seen 2 of 20 runs
    first tick  meleer        (2273,5341)  seen 2 of 20 runs
    first tick  meleer        (2280,5333)  seen 2 of 20 runs
    first tick  meleer        (2280,5346)  seen 1 of 20 runs
    first tick  meleer        (2260,5347)  seen 1 of 20 runs
    first tick  nibbler       (2266,5347)  seen 14 of 20 runs
    first tick  nibbler       (2265,5347)  seen 7 of 20 runs
    first tick  nibbler       (2267,5345)  seen 7 of 20 runs
    first tick  nibbler       (2267,5346)  seen 7 of 20 runs
    first tick  nibbler       (2265,5346)  seen 6 of 20 runs
    first tick  nibbler       (2266,5346)  seen 6 of 20 runs
    first tick  nibbler       (2267,5347)  seen 5 of 20 runs
    first tick  nibbler       (2265,5345)  seen 5 of 20 runs
    first tick  nibbler       (2266,5345)  seen 3 of 20 runs
    first tick  pillar        (2257,5349)  seen 20 of 20 runs
    first tick  pillar        (2267,5335)  seen 20 of 20 runs
    first tick  pillar        (2274,5351)  seen 20 of 20 runs
    later       bloblet_mage  (2275,5343)  seen 4
    later       bloblet_mage  (2262,5349)  seen 3
    later       bloblet_mage  (2279,5352)  seen 2
    later       bloblet_mage  (2279,5354)  seen 2
    later       bloblet_mage  (2274,5350)  seen 2
    later       bloblet_mage  (2265,5337)  seen 2
    later       bloblet_mage  (2260,5355)  seen 2
    later       bloblet_mage  (2275,5350)  seen 2
    later       bloblet_mage  (2261,5333)  seen 2
    later       bloblet_mage  (2282,5348)  seen 1
    later       bloblet_mage  (2274,5334)  seen 1
    later       bloblet_mage  (2277,5350)  seen 1
wave 16  (runs 20)
    first tick  meleer        (2262,5335)  seen 8 of 20 runs
    first tick  meleer        (2258,5330)  seen 7 of 20 runs
    first tick  meleer        (2280,5333)  seen 5 of 20 runs
    first tick  meleer        (2258,5353)  seen 5 of 20 runs
    first tick  meleer        (2273,5341)  seen 5 of 20 runs
    first tick  meleer        (2260,5347)  seen 4 of 20 runs
    first tick  meleer        (2280,5346)  seen 3 of 20 runs
    first tick  meleer        (2279,5353)  seen 3 of 20 runs
    first tick  nibbler       (2267,5345)  seen 11 of 20 runs
    first tick  nibbler       (2267,5347)  seen 8 of 20 runs
    first tick  nibbler       (2266,5347)  seen 8 of 20 runs
    first tick  nibbler       (2265,5347)  seen 8 of 20 runs
    first tick  nibbler       (2265,5346)  seen 8 of 20 runs
    first tick  nibbler       (2266,5345)  seen 7 of 20 runs
    first tick  nibbler       (2266,5346)  seen 5 of 20 runs
    first tick  nibbler       (2267,5346)  seen 3 of 20 runs
    first tick  nibbler       (2265,5345)  seen 2 of 20 runs
    first tick  pillar        (2257,5349)  seen 20 of 20 runs
    first tick  pillar        (2267,5335)  seen 20 of 20 runs
    first tick  pillar        (2274,5351)  seen 20 of 20 runs
wave 17  (runs 20)
    first tick  nibbler       (2267,5346)  seen 20 of 20 runs
    first tick  nibbler       (2266,5345)  seen 16 of 20 runs
    first tick  nibbler       (2265,5346)  seen 16 of 20 runs
    first tick  nibbler       (2266,5346)  seen 15 of 20 runs
    first tick  nibbler       (2266,5347)  seen 14 of 20 runs
    first tick  nibbler       (2265,5347)  seen 12 of 20 runs
    first tick  nibbler       (2265,5345)  seen 11 of 20 runs
    first tick  nibbler       (2267,5345)  seen 9 of 20 runs
    first tick  nibbler       (2267,5347)  seen 7 of 20 runs
    first tick  pillar        (2257,5349)  seen 20 of 20 runs
    first tick  pillar        (2267,5335)  seen 20 of 20 runs
    first tick  pillar        (2274,5351)  seen 20 of 20 runs
wave 18  (runs 20)
    first tick  nibbler       (2265,5346)  seen 9 of 20 runs
    first tick  nibbler       (2265,5347)  seen 9 of 20 runs
    first tick  nibbler       (2265,5345)  seen 8 of 20 runs
    first tick  nibbler       (2267,5347)  seen 8 of 20 runs
    first tick  nibbler       (2267,5345)  seen 8 of 20 runs
    first tick  nibbler       (2266,5346)  seen 6 of 20 runs
    first tick  nibbler       (2266,5345)  seen 4 of 20 runs
    first tick  nibbler       (2266,5347)  seen 4 of 20 runs
    first tick  nibbler       (2267,5346)  seen 4 of 20 runs
    first tick  pillar        (2257,5349)  seen 20 of 20 runs
    first tick  pillar        (2267,5335)  seen 20 of 20 runs
    first tick  pillar        (2274,5351)  seen 20 of 20 runs
    first tick  ranger        (2272,5330)  seen 4 of 20 runs
    first tick  ranger        (2260,5347)  seen 4 of 20 runs
    first tick  ranger        (2262,5335)  seen 3 of 20 runs
    first tick  ranger        (2280,5333)  seen 3 of 20 runs
    first tick  ranger        (2279,5353)  seen 3 of 20 runs
    first tick  ranger        (2258,5353)  seen 1 of 20 runs
    first tick  ranger        (2280,5346)  seen 1 of 20 runs
    first tick  ranger        (2258,5330)  seen 1 of 20 runs
wave 19  (runs 20)
    first tick  bat           (2262,5335)  seen 6 of 20 runs
    first tick  bat           (2279,5353)  seen 4 of 20 runs
    first tick  bat           (2260,5347)  seen 3 of 20 runs
    first tick  bat           (2258,5353)  seen 2 of 20 runs
    first tick  bat           (2273,5341)  seen 2 of 20 runs
    first tick  bat           (2280,5333)  seen 1 of 20 runs
    first tick  bat           (2258,5330)  seen 1 of 20 runs
    first tick  bat           (2280,5346)  seen 1 of 20 runs
    first tick  nibbler       (2267,5347)  seen 9 of 20 runs
    first tick  nibbler       (2267,5346)  seen 8 of 20 runs
    first tick  nibbler       (2266,5347)  seen 8 of 20 runs
    first tick  nibbler       (2267,5345)  seen 8 of 20 runs
    first tick  nibbler       (2265,5347)  seen 8 of 20 runs
    first tick  nibbler       (2266,5346)  seen 7 of 20 runs
    first tick  nibbler       (2266,5345)  seen 6 of 20 runs
    first tick  nibbler       (2265,5346)  seen 3 of 20 runs
    first tick  nibbler       (2265,5345)  seen 3 of 20 runs
    first tick  pillar        (2257,5349)  seen 20 of 20 runs
    first tick  pillar        (2267,5335)  seen 20 of 20 runs
    first tick  pillar        (2274,5351)  seen 20 of 20 runs
    first tick  ranger        (2258,5330)  seen 4 of 20 runs
    first tick  ranger        (2258,5353)  seen 3 of 20 runs
    first tick  ranger        (2262,5335)  seen 2 of 20 runs
    first tick  ranger        (2272,5330)  seen 2 of 20 runs
    first tick  ranger        (2280,5346)  seen 2 of 20 runs
    first tick  ranger        (2260,5347)  seen 2 of 20 runs
    first tick  ranger        (2279,5353)  seen 2 of 20 runs
    first tick  ranger        (2273,5341)  seen 2 of 20 runs
    first tick  ranger        (2280,5333)  seen 1 of 20 runs
wave 20  (runs 20)
    first tick  bat           (2272,5330)  seen 9 of 20 runs
    first tick  bat           (2262,5335)  seen 5 of 20 runs
    first tick  bat           (2279,5353)  seen 5 of 20 runs
    first tick  bat           (2280,5346)  seen 5 of 20 runs
    first tick  bat           (2280,5333)  seen 5 of 20 runs
    first tick  bat           (2273,5341)  seen 4 of 20 runs
    first tick  bat           (2260,5347)  seen 3 of 20 runs
    first tick  bat           (2258,5353)  seen 3 of 20 runs
    first tick  bat           (2258,5330)  seen 1 of 20 runs
    first tick  nibbler       (2267,5347)  seen 10 of 20 runs
    first tick  nibbler       (2266,5345)  seen 9 of 20 runs
    first tick  nibbler       (2267,5345)  seen 8 of 20 runs
    first tick  nibbler       (2265,5345)  seen 8 of 20 runs
    first tick  nibbler       (2266,5347)  seen 7 of 20 runs
    first tick  nibbler       (2265,5347)  seen 5 of 20 runs
    first tick  nibbler       (2267,5346)  seen 5 of 20 runs
    first tick  nibbler       (2266,5346)  seen 5 of 20 runs
    first tick  nibbler       (2265,5346)  seen 3 of 20 runs
    first tick  pillar        (2257,5349)  seen 20 of 20 runs
    first tick  pillar        (2267,5335)  seen 20 of 20 runs
    first tick  pillar        (2274,5351)  seen 20 of 20 runs
    first tick  ranger        (2258,5330)  seen 4 of 20 runs
    first tick  ranger        (2272,5330)  seen 4 of 20 runs
    first tick  ranger        (2279,5353)  seen 3 of 20 runs
    first tick  ranger        (2260,5347)  seen 3 of 20 runs
    first tick  ranger        (2280,5333)  seen 2 of 20 runs
    first tick  ranger        (2262,5335)  seen 2 of 20 runs
    first tick  ranger        (2273,5341)  seen 2 of 20 runs
wave 21  (runs 20)
    first tick  blob          (2262,5335)  seen 4 of 20 runs
    first tick  blob          (2273,5341)  seen 4 of 20 runs
    first tick  blob          (2272,5330)  seen 3 of 20 runs
    first tick  blob          (2258,5353)  seen 2 of 20 runs
    first tick  blob          (2258,5330)  seen 2 of 20 runs
    first tick  blob          (2260,5347)  seen 2 of 20 runs
    first tick  blob          (2280,5333)  seen 2 of 20 runs
    first tick  blob          (2279,5353)  seen 1 of 20 runs
    first tick  nibbler       (2267,5347)  seen 10 of 20 runs
    first tick  nibbler       (2266,5347)  seen 9 of 20 runs
    first tick  nibbler       (2265,5347)  seen 9 of 20 runs
    first tick  nibbler       (2266,5345)  seen 8 of 20 runs
    first tick  nibbler       (2267,5345)  seen 7 of 20 runs
    first tick  nibbler       (2265,5345)  seen 5 of 20 runs
    first tick  nibbler       (2265,5346)  seen 5 of 20 runs
    first tick  nibbler       (2267,5346)  seen 4 of 20 runs
    first tick  nibbler       (2266,5346)  seen 3 of 20 runs
    first tick  pillar        (2257,5349)  seen 20 of 20 runs
    first tick  pillar        (2267,5335)  seen 20 of 20 runs
    first tick  pillar        (2274,5351)  seen 20 of 20 runs
    first tick  ranger        (2262,5335)  seen 4 of 20 runs
    first tick  ranger        (2280,5346)  seen 4 of 20 runs
    first tick  ranger        (2273,5341)  seen 3 of 20 runs
    first tick  ranger        (2279,5353)  seen 3 of 20 runs
    first tick  ranger        (2280,5333)  seen 3 of 20 runs
    first tick  ranger        (2258,5330)  seen 1 of 20 runs
    first tick  ranger        (2260,5347)  seen 1 of 20 runs
    first tick  ranger        (2258,5353)  seen 1 of 20 runs
    later       bloblet_mage  (2274,5350)  seen 3
    later       bloblet_mage  (2275,5343)  seen 3
    later       bloblet_mage  (2260,5355)  seen 2
    later       bloblet_mage  (2261,5333)  seen 2
    later       bloblet_mage  (2274,5354)  seen 1
    later       bloblet_mage  (2280,5337)  seen 1
    later       bloblet_mage  (2275,5350)  seen 1
    later       bloblet_mage  (2271,5334)  seen 1
    later       bloblet_mage  (2279,5355)  seen 1
    later       bloblet_mage  (2276,5347)  seen 1
    later       bloblet_mage  (2264,5337)  seen 1
    later       bloblet_mage  (2265,5338)  seen 1
wave 22  (runs 20)
    first tick  bat           (2279,5353)  seen 3 of 20 runs
    first tick  bat           (2280,5346)  seen 3 of 20 runs
    first tick  bat           (2280,5333)  seen 3 of 20 runs
    first tick  bat           (2258,5330)  seen 3 of 20 runs
    first tick  bat           (2273,5341)  seen 2 of 20 runs
    first tick  bat           (2272,5330)  seen 2 of 20 runs
    first tick  bat           (2258,5353)  seen 2 of 20 runs
    first tick  bat           (2260,5347)  seen 1 of 20 runs
    first tick  bat           (2262,5335)  seen 1 of 20 runs
    first tick  blob          (2272,5330)  seen 5 of 20 runs
    first tick  blob          (2280,5333)  seen 4 of 20 runs
    first tick  blob          (2260,5347)  seen 4 of 20 runs
    first tick  blob          (2279,5353)  seen 2 of 20 runs
    first tick  blob          (2273,5341)  seen 2 of 20 runs
    first tick  blob          (2262,5335)  seen 1 of 20 runs
    first tick  blob          (2258,5330)  seen 1 of 20 runs
    first tick  blob          (2280,5346)  seen 1 of 20 runs
    first tick  nibbler       (2265,5346)  seen 9 of 20 runs
    first tick  nibbler       (2266,5346)  seen 8 of 20 runs
    first tick  nibbler       (2267,5346)  seen 8 of 20 runs
    first tick  nibbler       (2266,5347)  seen 7 of 20 runs
    first tick  nibbler       (2265,5347)  seen 7 of 20 runs
    first tick  nibbler       (2267,5345)  seen 6 of 20 runs
    first tick  nibbler       (2267,5347)  seen 6 of 20 runs
    first tick  nibbler       (2266,5345)  seen 5 of 20 runs
    first tick  nibbler       (2265,5345)  seen 4 of 20 runs
    first tick  pillar        (2257,5349)  seen 20 of 20 runs
    first tick  pillar        (2267,5335)  seen 20 of 20 runs
    first tick  pillar        (2274,5351)  seen 20 of 20 runs
    first tick  ranger        (2258,5353)  seen 5 of 20 runs
    first tick  ranger        (2273,5341)  seen 4 of 20 runs
    first tick  ranger        (2280,5333)  seen 3 of 20 runs
    first tick  ranger        (2258,5330)  seen 2 of 20 runs
    first tick  ranger        (2279,5353)  seen 2 of 20 runs
    first tick  ranger        (2262,5335)  seen 2 of 20 runs
    first tick  ranger        (2260,5347)  seen 2 of 20 runs
    later       bloblet_mage  (2274,5350)  seen 3
    later       bloblet_mage  (2262,5349)  seen 3
    later       bloblet_mage  (2278,5351)  seen 1
    later       bloblet_mage  (2273,5333)  seen 1
    later       bloblet_mage  (2278,5348)  seen 1
    later       bloblet_mage  (2276,5345)  seen 1
    later       bloblet_mage  (2278,5347)  seen 1
    later       bloblet_mage  (2267,5334)  seen 1
    later       bloblet_mage  (2275,5350)  seen 1
    later       bloblet_mage  (2261,5333)  seen 1
    later       bloblet_mage  (2278,5350)  seen 1
    later       bloblet_mage  (2282,5335)  seen 1
wave 23  (runs 20)
    first tick  bat           (2260,5347)  seen 8 of 20 runs
    first tick  bat           (2272,5330)  seen 6 of 20 runs
    first tick  bat           (2258,5330)  seen 5 of 20 runs
    first tick  bat           (2273,5341)  seen 5 of 20 runs
    first tick  bat           (2279,5353)  seen 4 of 20 runs
    first tick  bat           (2280,5333)  seen 4 of 20 runs
    first tick  bat           (2262,5335)  seen 4 of 20 runs
    first tick  bat           (2258,5353)  seen 3 of 20 runs
    first tick  bat           (2280,5346)  seen 1 of 20 runs
    first tick  blob          (2272,5330)  seen 5 of 20 runs
    first tick  blob          (2262,5335)  seen 3 of 20 runs
    first tick  blob          (2258,5353)  seen 3 of 20 runs
    first tick  blob          (2280,5346)  seen 2 of 20 runs
    first tick  blob          (2258,5330)  seen 2 of 20 runs
    first tick  blob          (2279,5353)  seen 2 of 20 runs
    first tick  blob          (2260,5347)  seen 2 of 20 runs
    first tick  blob          (2280,5333)  seen 1 of 20 runs
    first tick  nibbler       (2265,5345)  seen 12 of 20 runs
    first tick  nibbler       (2267,5347)  seen 11 of 20 runs
    first tick  nibbler       (2266,5346)  seen 8 of 20 runs
    first tick  nibbler       (2267,5346)  seen 7 of 20 runs
    first tick  nibbler       (2265,5346)  seen 6 of 20 runs
    first tick  nibbler       (2266,5345)  seen 6 of 20 runs
    first tick  nibbler       (2265,5347)  seen 4 of 20 runs
    first tick  nibbler       (2267,5345)  seen 3 of 20 runs
    first tick  nibbler       (2266,5347)  seen 3 of 20 runs
    first tick  pillar        (2257,5349)  seen 20 of 20 runs
    first tick  pillar        (2267,5335)  seen 20 of 20 runs
    first tick  pillar        (2274,5351)  seen 20 of 20 runs
    first tick  ranger        (2280,5333)  seen 6 of 20 runs
    first tick  ranger        (2262,5335)  seen 3 of 20 runs
    first tick  ranger        (2258,5353)  seen 3 of 20 runs
    first tick  ranger        (2258,5330)  seen 2 of 20 runs
    first tick  ranger        (2272,5330)  seen 2 of 20 runs
    first tick  ranger        (2279,5353)  seen 2 of 20 runs
    first tick  ranger        (2280,5346)  seen 2 of 20 runs
    later       bloblet_mage  (2273,5333)  seen 3
    later       bloblet_mage  (2273,5351)  seen 3
    later       bloblet_mage  (2264,5337)  seen 2
    later       bloblet_mage  (2274,5350)  seen 2
    later       bloblet_mage  (2279,5354)  seen 2
    later       bloblet_mage  (2279,5353)  seen 1
    later       bloblet_mage  (2276,5350)  seen 1
    later       bloblet_mage  (2278,5354)  seen 1
    later       bloblet_mage  (2262,5350)  seen 1
    later       bloblet_mage  (2268,5353)  seen 1
    later       bloblet_mage  (2264,5338)  seen 1
    later       bloblet_mage  (2276,5347)  seen 1
wave 24  (runs 20)
    first tick  blob          (2272,5330)  seen 9 of 20 runs
    first tick  blob          (2258,5330)  seen 8 of 20 runs
    first tick  blob          (2262,5335)  seen 7 of 20 runs
    first tick  blob          (2260,5347)  seen 5 of 20 runs
    first tick  blob          (2280,5346)  seen 4 of 20 runs
    first tick  blob          (2258,5353)  seen 3 of 20 runs
    first tick  blob          (2279,5353)  seen 2 of 20 runs
    first tick  blob          (2273,5341)  seen 1 of 20 runs
    first tick  blob          (2280,5333)  seen 1 of 20 runs
    first tick  nibbler       (2265,5347)  seen 12 of 20 runs
    first tick  nibbler       (2266,5347)  seen 10 of 20 runs
    first tick  nibbler       (2267,5346)  seen 8 of 20 runs
    first tick  nibbler       (2265,5346)  seen 7 of 20 runs
    first tick  nibbler       (2267,5345)  seen 6 of 20 runs
    first tick  nibbler       (2267,5347)  seen 6 of 20 runs
    first tick  nibbler       (2265,5345)  seen 4 of 20 runs
    first tick  nibbler       (2266,5345)  seen 4 of 20 runs
    first tick  nibbler       (2266,5346)  seen 3 of 20 runs
    first tick  pillar        (2257,5349)  seen 20 of 20 runs
    first tick  pillar        (2267,5335)  seen 20 of 20 runs
    first tick  pillar        (2274,5351)  seen 20 of 20 runs
    first tick  ranger        (2260,5347)  seen 4 of 20 runs
    first tick  ranger        (2279,5353)  seen 3 of 20 runs
    first tick  ranger        (2258,5353)  seen 3 of 20 runs
    first tick  ranger        (2273,5341)  seen 2 of 20 runs
    first tick  ranger        (2280,5346)  seen 2 of 20 runs
    first tick  ranger        (2258,5330)  seen 2 of 20 runs
    first tick  ranger        (2262,5335)  seen 2 of 20 runs
    first tick  ranger        (2280,5333)  seen 1 of 20 runs
    first tick  ranger        (2272,5330)  seen 1 of 20 runs
    later       bloblet_mage  (2274,5350)  seen 6
    later       bloblet_mage  (2261,5333)  seen 4
    later       bloblet_mage  (2264,5337)  seen 3
    later       bloblet_mage  (2279,5355)  seen 2
    later       bloblet_mage  (2279,5354)  seen 2
    later       bloblet_mage  (2273,5354)  seen 2
    later       bloblet_mage  (2273,5355)  seen 2
    later       bloblet_mage  (2278,5350)  seen 2
    later       bloblet_mage  (2273,5351)  seen 2
    later       bloblet_mage  (2275,5343)  seen 1
    later       bloblet_mage  (2271,5333)  seen 1
    later       bloblet_mage  (2282,5335)  seen 1
wave 25  (runs 20)
    first tick  meleer        (2262,5335)  seen 4 of 20 runs
    first tick  meleer        (2260,5347)  seen 3 of 20 runs
    first tick  meleer        (2279,5353)  seen 3 of 20 runs
    first tick  meleer        (2258,5353)  seen 3 of 20 runs
    first tick  meleer        (2280,5346)  seen 3 of 20 runs
    first tick  meleer        (2258,5330)  seen 2 of 20 runs
    first tick  meleer        (2273,5341)  seen 1 of 20 runs
    first tick  meleer        (2272,5330)  seen 1 of 20 runs
    first tick  nibbler       (2265,5347)  seen 9 of 20 runs
    first tick  nibbler       (2265,5345)  seen 8 of 20 runs
    first tick  nibbler       (2266,5345)  seen 8 of 20 runs
    first tick  nibbler       (2266,5346)  seen 8 of 20 runs
    first tick  nibbler       (2267,5345)  seen 8 of 20 runs
    first tick  nibbler       (2267,5347)  seen 6 of 20 runs
    first tick  nibbler       (2265,5346)  seen 6 of 20 runs
    first tick  nibbler       (2266,5347)  seen 4 of 20 runs
    first tick  nibbler       (2267,5346)  seen 3 of 20 runs
    first tick  pillar        (2257,5349)  seen 20 of 20 runs
    first tick  pillar        (2267,5335)  seen 20 of 20 runs
    first tick  pillar        (2274,5351)  seen 20 of 20 runs
    first tick  ranger        (2258,5330)  seen 5 of 20 runs
    first tick  ranger        (2280,5346)  seen 3 of 20 runs
    first tick  ranger        (2279,5353)  seen 3 of 20 runs
    first tick  ranger        (2260,5347)  seen 3 of 20 runs
    first tick  ranger        (2262,5335)  seen 2 of 20 runs
    first tick  ranger        (2280,5333)  seen 2 of 20 runs
    first tick  ranger        (2258,5353)  seen 1 of 20 runs
    first tick  ranger        (2272,5330)  seen 1 of 20 runs
wave 26  (runs 20)
    first tick  bat           (2280,5333)  seen 4 of 20 runs
    first tick  bat           (2280,5346)  seen 3 of 20 runs
    first tick  bat           (2272,5330)  seen 3 of 20 runs
    first tick  bat           (2260,5347)  seen 3 of 20 runs
    first tick  bat           (2279,5353)  seen 2 of 20 runs
    first tick  bat           (2258,5330)  seen 2 of 20 runs
    first tick  bat           (2273,5341)  seen 1 of 20 runs
    first tick  bat           (2262,5335)  seen 1 of 20 runs
    first tick  bat           (2258,5353)  seen 1 of 20 runs
    first tick  meleer        (2273,5341)  seen 4 of 20 runs
    first tick  meleer        (2279,5353)  seen 3 of 20 runs
    first tick  meleer        (2258,5330)  seen 3 of 20 runs
    first tick  meleer        (2262,5335)  seen 3 of 20 runs
    first tick  meleer        (2258,5353)  seen 3 of 20 runs
    first tick  meleer        (2280,5333)  seen 2 of 20 runs
    first tick  meleer        (2260,5347)  seen 1 of 20 runs
    first tick  meleer        (2280,5346)  seen 1 of 20 runs
    first tick  nibbler       (2266,5346)  seen 12 of 20 runs
    first tick  nibbler       (2265,5345)  seen 9 of 20 runs
    first tick  nibbler       (2267,5347)  seen 8 of 20 runs
    first tick  nibbler       (2267,5346)  seen 7 of 20 runs
    first tick  nibbler       (2267,5345)  seen 6 of 20 runs
    first tick  nibbler       (2266,5345)  seen 6 of 20 runs
    first tick  nibbler       (2265,5347)  seen 5 of 20 runs
    first tick  nibbler       (2266,5347)  seen 4 of 20 runs
    first tick  nibbler       (2265,5346)  seen 3 of 20 runs
    first tick  pillar        (2257,5349)  seen 20 of 20 runs
    first tick  pillar        (2267,5335)  seen 20 of 20 runs
    first tick  pillar        (2274,5351)  seen 20 of 20 runs
    first tick  ranger        (2273,5341)  seen 4 of 20 runs
    first tick  ranger        (2280,5346)  seen 4 of 20 runs
    first tick  ranger        (2258,5330)  seen 3 of 20 runs
    first tick  ranger        (2260,5347)  seen 2 of 20 runs
    first tick  ranger        (2262,5335)  seen 2 of 20 runs
    first tick  ranger        (2272,5330)  seen 2 of 20 runs
    first tick  ranger        (2258,5353)  seen 1 of 20 runs
    first tick  ranger        (2279,5353)  seen 1 of 20 runs
    first tick  ranger        (2280,5333)  seen 1 of 20 runs
wave 27  (runs 20)
    first tick  bat           (2280,5346)  seen 8 of 20 runs
    first tick  bat           (2272,5330)  seen 6 of 20 runs
    first tick  bat           (2260,5347)  seen 6 of 20 runs
    first tick  bat           (2280,5333)  seen 5 of 20 runs
    first tick  bat           (2279,5353)  seen 4 of 20 runs
    first tick  bat           (2273,5341)  seen 4 of 20 runs
    first tick  bat           (2258,5353)  seen 3 of 20 runs
    first tick  bat           (2262,5335)  seen 2 of 20 runs
    first tick  bat           (2258,5330)  seen 2 of 20 runs
    first tick  meleer        (2280,5333)  seen 5 of 20 runs
    first tick  meleer        (2262,5335)  seen 4 of 20 runs
    first tick  meleer        (2258,5353)  seen 2 of 20 runs
    first tick  meleer        (2273,5341)  seen 2 of 20 runs
    first tick  meleer        (2260,5347)  seen 2 of 20 runs
    first tick  meleer        (2258,5330)  seen 2 of 20 runs
    first tick  meleer        (2279,5353)  seen 1 of 20 runs
    first tick  meleer        (2272,5330)  seen 1 of 20 runs
    first tick  meleer        (2280,5346)  seen 1 of 20 runs
    first tick  nibbler       (2267,5345)  seen 10 of 20 runs
    first tick  nibbler       (2266,5345)  seen 9 of 20 runs
    first tick  nibbler       (2265,5345)  seen 8 of 20 runs
    first tick  nibbler       (2267,5347)  seen 7 of 20 runs
    first tick  nibbler       (2266,5347)  seen 7 of 20 runs
    first tick  nibbler       (2265,5346)  seen 6 of 20 runs
    first tick  nibbler       (2267,5346)  seen 6 of 20 runs
    first tick  nibbler       (2266,5346)  seen 5 of 20 runs
    first tick  nibbler       (2265,5347)  seen 2 of 20 runs
    first tick  pillar        (2257,5349)  seen 20 of 20 runs
    first tick  pillar        (2267,5335)  seen 20 of 20 runs
    first tick  pillar        (2274,5351)  seen 20 of 20 runs
    first tick  ranger        (2279,5353)  seen 6 of 20 runs
    first tick  ranger        (2262,5335)  seen 5 of 20 runs
    first tick  ranger        (2258,5330)  seen 3 of 20 runs
    first tick  ranger        (2280,5333)  seen 2 of 20 runs
    first tick  ranger        (2258,5353)  seen 1 of 20 runs
    first tick  ranger        (2280,5346)  seen 1 of 20 runs
    first tick  ranger        (2273,5341)  seen 1 of 20 runs
    first tick  ranger        (2260,5347)  seen 1 of 20 runs
wave 28  (runs 19)
    first tick  blob          (2280,5346)  seen 4 of 19 runs
    first tick  blob          (2258,5353)  seen 3 of 19 runs
    first tick  blob          (2279,5353)  seen 3 of 19 runs
    first tick  blob          (2262,5335)  seen 3 of 19 runs
    first tick  blob          (2272,5330)  seen 3 of 19 runs
    first tick  blob          (2273,5341)  seen 2 of 19 runs
    first tick  blob          (2258,5330)  seen 1 of 19 runs
    first tick  meleer        (2262,5335)  seen 3 of 19 runs
    first tick  meleer        (2273,5341)  seen 3 of 19 runs
    first tick  meleer        (2260,5347)  seen 3 of 19 runs
    first tick  meleer        (2258,5330)  seen 3 of 19 runs
    first tick  meleer        (2258,5353)  seen 2 of 19 runs
    first tick  meleer        (2280,5333)  seen 2 of 19 runs
    first tick  meleer        (2272,5330)  seen 1 of 19 runs
    first tick  meleer        (2279,5353)  seen 1 of 19 runs
    first tick  meleer        (2280,5346)  seen 1 of 19 runs
    first tick  nibbler       (2265,5345)  seen 10 of 19 runs
    first tick  nibbler       (2267,5346)  seen 8 of 19 runs
    first tick  nibbler       (2267,5347)  seen 7 of 19 runs
    first tick  nibbler       (2266,5346)  seen 7 of 19 runs
    first tick  nibbler       (2266,5347)  seen 7 of 19 runs
    first tick  nibbler       (2267,5345)  seen 7 of 19 runs
    first tick  nibbler       (2266,5345)  seen 5 of 19 runs
    first tick  nibbler       (2265,5347)  seen 3 of 19 runs
    first tick  nibbler       (2265,5346)  seen 3 of 19 runs
    first tick  pillar        (2257,5349)  seen 19 of 19 runs
    first tick  pillar        (2267,5335)  seen 19 of 19 runs
    first tick  pillar        (2274,5351)  seen 19 of 19 runs
    first tick  ranger        (2280,5346)  seen 4 of 19 runs
    first tick  ranger        (2258,5330)  seen 3 of 19 runs
    first tick  ranger        (2260,5347)  seen 2 of 19 runs
    first tick  ranger        (2280,5333)  seen 2 of 19 runs
    first tick  ranger        (2272,5330)  seen 2 of 19 runs
    first tick  ranger        (2262,5335)  seen 2 of 19 runs
    first tick  ranger        (2258,5353)  seen 2 of 19 runs
    first tick  ranger        (2279,5353)  seen 1 of 19 runs
    first tick  ranger        (2273,5341)  seen 1 of 19 runs
    later       bloblet_mage  (2279,5355)  seen 4
    later       bloblet_mage  (2274,5350)  seen 4
    later       bloblet_mage  (2260,5355)  seen 2
    later       bloblet_mage  (2275,5343)  seen 1
    later       bloblet_mage  (2278,5342)  seen 1
    later       bloblet_mage  (2268,5341)  seen 1
    later       bloblet_mage  (2279,5354)  seen 1
    later       bloblet_mage  (2279,5353)  seen 1
    later       bloblet_mage  (2273,5351)  seen 1
    later       bloblet_mage  (2276,5346)  seen 1
    later       bloblet_mage  (2261,5348)  seen 1
    later       bloblet_mage  (2279,5352)  seen 1
wave 29  (runs 19)
    first tick  bat           (2272,5330)  seen 5 of 19 runs
    first tick  bat           (2280,5333)  seen 3 of 19 runs
    first tick  bat           (2262,5335)  seen 3 of 19 runs
    first tick  bat           (2273,5341)  seen 3 of 19 runs
    first tick  bat           (2279,5353)  seen 2 of 19 runs
    first tick  bat           (2280,5346)  seen 1 of 19 runs
    first tick  bat           (2258,5353)  seen 1 of 19 runs
    first tick  bat           (2258,5330)  seen 1 of 19 runs
    first tick  blob          (2260,5347)  seen 4 of 19 runs
    first tick  blob          (2262,5335)  seen 3 of 19 runs
    first tick  blob          (2280,5333)  seen 3 of 19 runs
    first tick  blob          (2258,5353)  seen 3 of 19 runs
    first tick  blob          (2279,5353)  seen 2 of 19 runs
    first tick  blob          (2280,5346)  seen 2 of 19 runs
    first tick  blob          (2273,5341)  seen 1 of 19 runs
    first tick  blob          (2272,5330)  seen 1 of 19 runs
    first tick  meleer        (2280,5346)  seen 4 of 19 runs
    first tick  meleer        (2272,5330)  seen 3 of 19 runs
    first tick  meleer        (2262,5335)  seen 3 of 19 runs
    first tick  meleer        (2260,5347)  seen 3 of 19 runs
    first tick  meleer        (2258,5353)  seen 2 of 19 runs
    first tick  meleer        (2258,5330)  seen 1 of 19 runs
    first tick  meleer        (2280,5333)  seen 1 of 19 runs
    first tick  meleer        (2273,5341)  seen 1 of 19 runs
    first tick  meleer        (2279,5353)  seen 1 of 19 runs
    first tick  nibbler       (2267,5346)  seen 9 of 19 runs
    first tick  nibbler       (2267,5345)  seen 8 of 19 runs
    first tick  nibbler       (2266,5345)  seen 8 of 19 runs
    first tick  nibbler       (2265,5345)  seen 8 of 19 runs
    first tick  nibbler       (2267,5347)  seen 6 of 19 runs
    first tick  nibbler       (2266,5346)  seen 5 of 19 runs
    first tick  nibbler       (2266,5347)  seen 5 of 19 runs
    first tick  nibbler       (2265,5346)  seen 4 of 19 runs
    first tick  nibbler       (2265,5347)  seen 4 of 19 runs
    first tick  pillar        (2257,5349)  seen 19 of 19 runs
    first tick  pillar        (2267,5335)  seen 19 of 19 runs
    first tick  pillar        (2274,5351)  seen 19 of 19 runs
    first tick  ranger        (2260,5347)  seen 5 of 19 runs
    first tick  ranger        (2280,5333)  seen 3 of 19 runs
    first tick  ranger        (2272,5330)  seen 2 of 19 runs
    first tick  ranger        (2280,5346)  seen 2 of 19 runs
    first tick  ranger        (2273,5341)  seen 2 of 19 runs
    first tick  ranger        (2258,5330)  seen 2 of 19 runs
    first tick  ranger        (2279,5353)  seen 2 of 19 runs
    first tick  ranger        (2262,5335)  seen 1 of 19 runs
    later       bloblet_mage  (2274,5350)  seen 3
    later       bloblet_mage  (2262,5349)  seen 2
    later       bloblet_mage  (2273,5354)  seen 2
    later       bloblet_mage  (2264,5337)  seen 1
    later       bloblet_mage  (2278,5350)  seen 1
    later       bloblet_mage  (2278,5348)  seen 1
    later       bloblet_mage  (2276,5347)  seen 1
    later       bloblet_mage  (2279,5355)  seen 1
    later       bloblet_mage  (2279,5352)  seen 1
    later       bloblet_mage  (2273,5351)  seen 1
    later       bloblet_mage  (2282,5348)  seen 1
    later       bloblet_mage  (2274,5348)  seen 1
wave 30  (runs 19)
    first tick  bat           (2280,5333)  seen 7 of 19 runs
    first tick  bat           (2258,5330)  seen 6 of 19 runs
    first tick  bat           (2272,5330)  seen 5 of 19 runs
    first tick  bat           (2258,5353)  seen 5 of 19 runs
    first tick  bat           (2273,5341)  seen 4 of 19 runs
    first tick  bat           (2262,5335)  seen 4 of 19 runs
    first tick  bat           (2280,5346)  seen 4 of 19 runs
    first tick  bat           (2279,5353)  seen 2 of 19 runs
    first tick  bat           (2260,5347)  seen 1 of 19 runs
    first tick  blob          (2272,5330)  seen 6 of 19 runs
    first tick  blob          (2279,5353)  seen 4 of 19 runs
    first tick  blob          (2280,5346)  seen 3 of 19 runs
    first tick  blob          (2260,5347)  seen 2 of 19 runs
    first tick  blob          (2262,5335)  seen 2 of 19 runs
    first tick  blob          (2258,5330)  seen 1 of 19 runs
    first tick  blob          (2280,5333)  seen 1 of 19 runs
    first tick  meleer        (2280,5346)  seen 4 of 19 runs
    first tick  meleer        (2258,5353)  seen 4 of 19 runs
    first tick  meleer        (2272,5330)  seen 3 of 19 runs
    first tick  meleer        (2262,5335)  seen 2 of 19 runs
    first tick  meleer        (2260,5347)  seen 2 of 19 runs
    first tick  meleer        (2280,5333)  seen 2 of 19 runs
    first tick  meleer        (2279,5353)  seen 1 of 19 runs
    first tick  meleer        (2258,5330)  seen 1 of 19 runs
    first tick  nibbler       (2266,5347)  seen 10 of 19 runs
    first tick  nibbler       (2266,5345)  seen 9 of 19 runs
    first tick  nibbler       (2267,5346)  seen 7 of 19 runs
    first tick  nibbler       (2267,5347)  seen 7 of 19 runs
    first tick  nibbler       (2267,5345)  seen 7 of 19 runs
    first tick  nibbler       (2266,5346)  seen 6 of 19 runs
    first tick  nibbler       (2265,5345)  seen 4 of 19 runs
    first tick  nibbler       (2265,5347)  seen 4 of 19 runs
    first tick  nibbler       (2265,5346)  seen 3 of 19 runs
    first tick  pillar        (2257,5349)  seen 19 of 19 runs
    first tick  pillar        (2267,5335)  seen 19 of 19 runs
    first tick  pillar        (2274,5351)  seen 19 of 19 runs
    first tick  ranger        (2260,5347)  seen 6 of 19 runs
    first tick  ranger        (2280,5346)  seen 3 of 19 runs
    first tick  ranger        (2258,5353)  seen 2 of 19 runs
    first tick  ranger        (2262,5335)  seen 2 of 19 runs
    first tick  ranger        (2280,5333)  seen 2 of 19 runs
    first tick  ranger        (2273,5341)  seen 2 of 19 runs
    first tick  ranger        (2279,5353)  seen 2 of 19 runs
    later       bloblet_mage  (2279,5355)  seen 3
    later       bloblet_mage  (2279,5354)  seen 2
    later       bloblet_mage  (2278,5345)  seen 2
    later       bloblet_mage  (2274,5350)  seen 2
    later       bloblet_mage  (2282,5348)  seen 1
    later       bloblet_mage  (2277,5344)  seen 1
    later       bloblet_mage  (2273,5352)  seen 1
    later       bloblet_mage  (2276,5346)  seen 1
    later       bloblet_mage  (2262,5350)  seen 1
    later       bloblet_mage  (2274,5349)  seen 1
    later       bloblet_mage  (2274,5334)  seen 1
    later       bloblet_mage  (2279,5353)  seen 1
wave 31  (runs 19)
    first tick  blob          (2273,5341)  seen 7 of 19 runs
    first tick  blob          (2280,5346)  seen 7 of 19 runs
    first tick  blob          (2258,5353)  seen 6 of 19 runs
    first tick  blob          (2260,5347)  seen 5 of 19 runs
    first tick  blob          (2258,5330)  seen 4 of 19 runs
    first tick  blob          (2279,5353)  seen 3 of 19 runs
    first tick  blob          (2262,5335)  seen 3 of 19 runs
    first tick  blob          (2272,5330)  seen 2 of 19 runs
    first tick  blob          (2280,5333)  seen 1 of 19 runs
    first tick  meleer        (2260,5347)  seen 5 of 19 runs
    first tick  meleer        (2279,5353)  seen 4 of 19 runs
    first tick  meleer        (2280,5333)  seen 3 of 19 runs
    first tick  meleer        (2273,5341)  seen 2 of 19 runs
    first tick  meleer        (2280,5346)  seen 1 of 19 runs
    first tick  meleer        (2258,5353)  seen 1 of 19 runs
    first tick  meleer        (2262,5335)  seen 1 of 19 runs
    first tick  meleer        (2272,5330)  seen 1 of 19 runs
    first tick  meleer        (2258,5330)  seen 1 of 19 runs
    first tick  nibbler       (2266,5345)  seen 10 of 19 runs
    first tick  nibbler       (2267,5346)  seen 9 of 19 runs
    first tick  nibbler       (2266,5346)  seen 8 of 19 runs
    first tick  nibbler       (2265,5345)  seen 8 of 19 runs
    first tick  nibbler       (2265,5347)  seen 6 of 19 runs
    first tick  nibbler       (2266,5347)  seen 6 of 19 runs
    first tick  nibbler       (2267,5347)  seen 5 of 19 runs
    first tick  nibbler       (2265,5346)  seen 3 of 19 runs
    first tick  nibbler       (2267,5345)  seen 2 of 19 runs
    first tick  pillar        (2257,5349)  seen 19 of 19 runs
    first tick  pillar        (2267,5335)  seen 19 of 19 runs
    first tick  pillar        (2274,5351)  seen 19 of 19 runs
    first tick  ranger        (2262,5335)  seen 4 of 19 runs
    first tick  ranger        (2272,5330)  seen 3 of 19 runs
    first tick  ranger        (2273,5341)  seen 3 of 19 runs
    first tick  ranger        (2279,5353)  seen 2 of 19 runs
    first tick  ranger        (2258,5353)  seen 2 of 19 runs
    first tick  ranger        (2280,5346)  seen 2 of 19 runs
    first tick  ranger        (2280,5333)  seen 1 of 19 runs
    first tick  ranger        (2260,5347)  seen 1 of 19 runs
    first tick  ranger        (2258,5330)  seen 1 of 19 runs
    later       bloblet_mage  (2275,5343)  seen 4
    later       bloblet_mage  (2274,5350)  seen 4
    later       bloblet_mage  (2273,5351)  seen 3
    later       bloblet_mage  (2279,5354)  seen 3
    later       bloblet_mage  (2282,5348)  seen 2
    later       bloblet_mage  (2273,5355)  seen 2
    later       bloblet_mage  (2279,5355)  seen 2
    later       bloblet_mage  (2260,5355)  seen 2
    later       bloblet_mage  (2279,5353)  seen 2
    later       bloblet_mage  (2277,5350)  seen 2
    later       bloblet_mage  (2262,5349)  seen 1
    later       bloblet_mage  (2273,5354)  seen 1
wave 32  (runs 18)
    first tick  meleer        (2260,5347)  seen 7 of 18 runs
    first tick  meleer        (2280,5346)  seen 6 of 18 runs
    first tick  meleer        (2262,5335)  seen 5 of 18 runs
    first tick  meleer        (2272,5330)  seen 4 of 18 runs
    first tick  meleer        (2258,5330)  seen 4 of 18 runs
    first tick  meleer        (2280,5333)  seen 3 of 18 runs
    first tick  meleer        (2258,5353)  seen 3 of 18 runs
    first tick  meleer        (2279,5353)  seen 3 of 18 runs
    first tick  meleer        (2273,5341)  seen 1 of 18 runs
    first tick  nibbler       (2267,5346)  seen 8 of 18 runs
    first tick  nibbler       (2266,5346)  seen 8 of 18 runs
    first tick  nibbler       (2266,5347)  seen 7 of 18 runs
    first tick  nibbler       (2267,5345)  seen 6 of 18 runs
    first tick  nibbler       (2266,5345)  seen 6 of 18 runs
    first tick  nibbler       (2265,5345)  seen 6 of 18 runs
    first tick  nibbler       (2267,5347)  seen 5 of 18 runs
    first tick  nibbler       (2265,5346)  seen 5 of 18 runs
    first tick  nibbler       (2265,5347)  seen 3 of 18 runs
    first tick  pillar        (2257,5349)  seen 18 of 18 runs
    first tick  pillar        (2267,5335)  seen 18 of 18 runs
    first tick  pillar        (2274,5351)  seen 18 of 18 runs
    first tick  ranger        (2273,5341)  seen 5 of 18 runs
    first tick  ranger        (2258,5353)  seen 4 of 18 runs
    first tick  ranger        (2262,5335)  seen 3 of 18 runs
    first tick  ranger        (2272,5330)  seen 2 of 18 runs
    first tick  ranger        (2258,5330)  seen 2 of 18 runs
    first tick  ranger        (2279,5353)  seen 1 of 18 runs
    first tick  ranger        (2280,5346)  seen 1 of 18 runs
wave 33  (runs 18)
    first tick  nibbler       (2267,5347)  seen 13 of 18 runs
    first tick  nibbler       (2265,5346)  seen 7 of 18 runs
    first tick  nibbler       (2266,5346)  seen 7 of 18 runs
    first tick  nibbler       (2265,5347)  seen 6 of 18 runs
    first tick  nibbler       (2267,5345)  seen 6 of 18 runs
    first tick  nibbler       (2266,5345)  seen 5 of 18 runs
    first tick  nibbler       (2267,5346)  seen 4 of 18 runs
    first tick  nibbler       (2265,5345)  seen 3 of 18 runs
    first tick  nibbler       (2266,5347)  seen 3 of 18 runs
    first tick  pillar        (2257,5349)  seen 18 of 18 runs
    first tick  pillar        (2267,5335)  seen 18 of 18 runs
    first tick  pillar        (2274,5351)  seen 18 of 18 runs
    first tick  ranger        (2280,5346)  seen 7 of 18 runs
    first tick  ranger        (2262,5335)  seen 5 of 18 runs
    first tick  ranger        (2258,5353)  seen 5 of 18 runs
    first tick  ranger        (2280,5333)  seen 5 of 18 runs
    first tick  ranger        (2272,5330)  seen 4 of 18 runs
    first tick  ranger        (2279,5353)  seen 3 of 18 runs
    first tick  ranger        (2260,5347)  seen 3 of 18 runs
    first tick  ranger        (2258,5330)  seen 2 of 18 runs
    first tick  ranger        (2273,5341)  seen 2 of 18 runs
wave 34  (runs 18)
    first tick  nibbler       (2265,5346)  seen 17 of 18 runs
    first tick  nibbler       (2266,5345)  seen 14 of 18 runs
    first tick  nibbler       (2266,5346)  seen 14 of 18 runs
    first tick  nibbler       (2267,5346)  seen 13 of 18 runs
    first tick  nibbler       (2267,5345)  seen 12 of 18 runs
    first tick  nibbler       (2266,5347)  seen 12 of 18 runs
    first tick  nibbler       (2265,5345)  seen 9 of 18 runs
    first tick  nibbler       (2265,5347)  seen 9 of 18 runs
    first tick  nibbler       (2267,5347)  seen 8 of 18 runs
    first tick  pillar        (2257,5349)  seen 18 of 18 runs
    first tick  pillar        (2267,5335)  seen 18 of 18 runs
    first tick  pillar        (2274,5351)  seen 18 of 18 runs
wave 35  (runs 18)
    first tick  mager         (2262,5335)  seen 5 of 18 runs
    first tick  mager         (2279,5353)  seen 5 of 18 runs
    first tick  mager         (2258,5330)  seen 4 of 18 runs
    first tick  mager         (2258,5353)  seen 1 of 18 runs
    first tick  mager         (2280,5333)  seen 1 of 18 runs
    first tick  mager         (2273,5341)  seen 1 of 18 runs
    first tick  mager         (2272,5330)  seen 1 of 18 runs
    first tick  nibbler       (2266,5346)  seen 9 of 18 runs
    first tick  nibbler       (2267,5347)  seen 9 of 18 runs
    first tick  nibbler       (2267,5346)  seen 8 of 18 runs
    first tick  nibbler       (2266,5345)  seen 7 of 18 runs
    first tick  nibbler       (2265,5345)  seen 6 of 18 runs
    first tick  nibbler       (2265,5347)  seen 6 of 18 runs
    first tick  nibbler       (2267,5345)  seen 5 of 18 runs
    first tick  nibbler       (2266,5347)  seen 3 of 18 runs
    first tick  nibbler       (2265,5346)  seen 1 of 18 runs
    first tick  pillar        (2257,5349)  seen 18 of 18 runs
    first tick  pillar        (2267,5335)  seen 18 of 18 runs
    first tick  pillar        (2274,5351)  seen 18 of 18 runs
wave 36  (runs 18)
    first tick  bat           (2273,5341)  seen 4 of 18 runs
    first tick  bat           (2280,5346)  seen 3 of 18 runs
    first tick  bat           (2258,5353)  seen 3 of 18 runs
    first tick  bat           (2272,5330)  seen 2 of 18 runs
    first tick  bat           (2262,5335)  seen 2 of 18 runs
    first tick  bat           (2280,5333)  seen 1 of 18 runs
    first tick  bat           (2258,5330)  seen 1 of 18 runs
    first tick  bat           (2260,5347)  seen 1 of 18 runs
    first tick  bat           (2279,5353)  seen 1 of 18 runs
    first tick  mager         (2280,5333)  seen 5 of 18 runs
    first tick  mager         (2272,5330)  seen 4 of 18 runs
    first tick  mager         (2280,5346)  seen 3 of 18 runs
    first tick  mager         (2258,5330)  seen 3 of 18 runs
    first tick  mager         (2273,5341)  seen 1 of 18 runs
    first tick  mager         (2258,5353)  seen 1 of 18 runs
    first tick  mager         (2262,5335)  seen 1 of 18 runs
    first tick  nibbler       (2267,5347)  seen 10 of 18 runs
    first tick  nibbler       (2265,5347)  seen 9 of 18 runs
    first tick  nibbler       (2266,5345)  seen 7 of 18 runs
    first tick  nibbler       (2266,5346)  seen 6 of 18 runs
    first tick  nibbler       (2266,5347)  seen 6 of 18 runs
    first tick  nibbler       (2265,5346)  seen 5 of 18 runs
    first tick  nibbler       (2265,5345)  seen 5 of 18 runs
    first tick  nibbler       (2267,5346)  seen 3 of 18 runs
    first tick  nibbler       (2267,5345)  seen 3 of 18 runs
    first tick  pillar        (2257,5349)  seen 18 of 18 runs
    first tick  pillar        (2267,5335)  seen 18 of 18 runs
    first tick  pillar        (2274,5351)  seen 18 of 18 runs
    later       bat           (2275,5345)  seen 2
    later       bat           (2272,5341)  seen 1
    later       bat           (2276,5342)  seen 1
    later       bat           (2270,5343)  seen 1
    later       bat           (2274,5344)  seen 1
    later       bat           (2277,5344)  seen 1
wave 37  (runs 18)
    first tick  bat           (2280,5346)  seen 6 of 18 runs
    first tick  bat           (2258,5330)  seen 5 of 18 runs
    first tick  bat           (2258,5353)  seen 5 of 18 runs
    first tick  bat           (2279,5353)  seen 4 of 18 runs
    first tick  bat           (2272,5330)  seen 4 of 18 runs
    first tick  bat           (2262,5335)  seen 4 of 18 runs
    first tick  bat           (2260,5347)  seen 3 of 18 runs
    first tick  bat           (2273,5341)  seen 3 of 18 runs
    first tick  bat           (2280,5333)  seen 2 of 18 runs
    first tick  mager         (2280,5333)  seen 4 of 18 runs
    first tick  mager         (2273,5341)  seen 4 of 18 runs
    first tick  mager         (2279,5353)  seen 2 of 18 runs
    first tick  mager         (2258,5353)  seen 2 of 18 runs
    first tick  mager         (2258,5330)  seen 2 of 18 runs
    first tick  mager         (2262,5335)  seen 2 of 18 runs
    first tick  mager         (2260,5347)  seen 1 of 18 runs
    first tick  mager         (2272,5330)  seen 1 of 18 runs
    first tick  nibbler       (2266,5346)  seen 9 of 18 runs
    first tick  nibbler       (2265,5345)  seen 9 of 18 runs
    first tick  nibbler       (2265,5347)  seen 8 of 18 runs
    first tick  nibbler       (2267,5347)  seen 7 of 18 runs
    first tick  nibbler       (2265,5346)  seen 6 of 18 runs
    first tick  nibbler       (2267,5346)  seen 5 of 18 runs
    first tick  nibbler       (2266,5345)  seen 4 of 18 runs
    first tick  nibbler       (2267,5345)  seen 4 of 18 runs
    first tick  nibbler       (2266,5347)  seen 2 of 18 runs
    first tick  pillar        (2257,5349)  seen 18 of 18 runs
    first tick  pillar        (2267,5335)  seen 18 of 18 runs
    first tick  pillar        (2274,5351)  seen 18 of 18 runs
    later       bat           (2274,5344)  seen 2
    later       bat           (2272,5345)  seen 1
    later       bat           (2272,5343)  seen 1
    later       bat           (2277,5342)  seen 1
    later       bat           (2273,5341)  seen 1
    later       bat           (2276,5343)  seen 1
wave 38  (runs 18)
    first tick  blob          (2258,5353)  seen 3 of 18 runs
    first tick  blob          (2280,5333)  seen 3 of 18 runs
    first tick  blob          (2260,5347)  seen 3 of 18 runs
    first tick  blob          (2272,5330)  seen 3 of 18 runs
    first tick  blob          (2280,5346)  seen 2 of 18 runs
    first tick  blob          (2262,5335)  seen 2 of 18 runs
    first tick  blob          (2273,5341)  seen 1 of 18 runs
    first tick  blob          (2279,5353)  seen 1 of 18 runs
    first tick  mager         (2258,5330)  seen 4 of 18 runs
    first tick  mager         (2279,5353)  seen 4 of 18 runs
    first tick  mager         (2280,5346)  seen 3 of 18 runs
    first tick  mager         (2272,5330)  seen 2 of 18 runs
    first tick  mager         (2258,5353)  seen 2 of 18 runs
    first tick  mager         (2262,5335)  seen 1 of 18 runs
    first tick  mager         (2273,5341)  seen 1 of 18 runs
    first tick  mager         (2260,5347)  seen 1 of 18 runs
    first tick  nibbler       (2265,5346)  seen 8 of 18 runs
    first tick  nibbler       (2266,5345)  seen 7 of 18 runs
    first tick  nibbler       (2266,5347)  seen 7 of 18 runs
    first tick  nibbler       (2265,5345)  seen 7 of 18 runs
    first tick  nibbler       (2267,5346)  seen 6 of 18 runs
    first tick  nibbler       (2266,5346)  seen 6 of 18 runs
    first tick  nibbler       (2265,5347)  seen 6 of 18 runs
    first tick  nibbler       (2267,5345)  seen 4 of 18 runs
    first tick  nibbler       (2267,5347)  seen 3 of 18 runs
    first tick  pillar        (2257,5349)  seen 18 of 18 runs
    first tick  pillar        (2267,5335)  seen 18 of 18 runs
    first tick  pillar        (2274,5351)  seen 18 of 18 runs
    later       bloblet_mage  (2279,5355)  seen 3
    later       bloblet_mage  (2274,5350)  seen 3
    later       bloblet_mage  (2271,5340)  seen 2
    later       bloblet_mage  (2277,5350)  seen 2
    later       bloblet_mage  (2273,5355)  seen 1
    later       bloblet_mage  (2267,5339)  seen 1
    later       bloblet_mage  (2281,5349)  seen 1
    later       bloblet_mage  (2275,5350)  seen 1
    later       bloblet_mage  (2269,5353)  seen 1
    later       bloblet_mage  (2270,5354)  seen 1
    later       bloblet_mage  (2271,5334)  seen 1
    later       bloblet_mage  (2266,5345)  seen 1
wave 39  (runs 18)
    first tick  bat           (2280,5346)  seen 4 of 18 runs
    first tick  bat           (2258,5353)  seen 3 of 18 runs
    first tick  bat           (2280,5333)  seen 3 of 18 runs
    first tick  bat           (2262,5335)  seen 3 of 18 runs
    first tick  bat           (2260,5347)  seen 2 of 18 runs
    first tick  bat           (2279,5353)  seen 2 of 18 runs
    first tick  bat           (2273,5341)  seen 1 of 18 runs
    first tick  blob          (2280,5333)  seen 4 of 18 runs
    first tick  blob          (2260,5347)  seen 3 of 18 runs
    first tick  blob          (2258,5353)  seen 2 of 18 runs
    first tick  blob          (2279,5353)  seen 2 of 18 runs
    first tick  blob          (2262,5335)  seen 2 of 18 runs
    first tick  blob          (2272,5330)  seen 2 of 18 runs
    first tick  blob          (2280,5346)  seen 1 of 18 runs
    first tick  blob          (2273,5341)  seen 1 of 18 runs
    first tick  blob          (2258,5330)  seen 1 of 18 runs
    first tick  mager         (2272,5330)  seen 4 of 18 runs
    first tick  mager         (2258,5330)  seen 4 of 18 runs
    first tick  mager         (2273,5341)  seen 3 of 18 runs
    first tick  mager         (2280,5333)  seen 2 of 18 runs
    first tick  mager         (2260,5347)  seen 2 of 18 runs
    first tick  mager         (2279,5353)  seen 1 of 18 runs
    first tick  mager         (2258,5353)  seen 1 of 18 runs
    first tick  mager         (2280,5346)  seen 1 of 18 runs
    first tick  nibbler       (2266,5345)  seen 9 of 18 runs
    first tick  nibbler       (2265,5346)  seen 8 of 18 runs
    first tick  nibbler       (2265,5347)  seen 7 of 18 runs
    first tick  nibbler       (2267,5346)  seen 7 of 18 runs
    first tick  nibbler       (2266,5346)  seen 6 of 18 runs
    first tick  nibbler       (2267,5345)  seen 6 of 18 runs
    first tick  nibbler       (2267,5347)  seen 5 of 18 runs
    first tick  nibbler       (2265,5345)  seen 4 of 18 runs
    first tick  nibbler       (2266,5347)  seen 2 of 18 runs
    first tick  pillar        (2257,5349)  seen 18 of 18 runs
    first tick  pillar        (2267,5335)  seen 18 of 18 runs
    first tick  pillar        (2274,5351)  seen 18 of 18 runs
    later       bat           (2274,5346)  seen 1
    later       bat           (2272,5344)  seen 1
    later       bat           (2274,5340)  seen 1
    later       bat           (2274,5343)  seen 1
    later       bat           (2273,5343)  seen 1
    later       bat           (2277,5342)  seen 1
    later       bat           (2276,5342)  seen 1
    later       bloblet_mage  (2274,5350)  seen 5
    later       bloblet_mage  (2273,5355)  seen 2
    later       bloblet_mage  (2282,5348)  seen 1
    later       bloblet_mage  (2273,5351)  seen 1
    later       bloblet_mage  (2276,5344)  seen 1
wave 40  (runs 18)
    first tick  bat           (2260,5347)  seen 7 of 18 runs
    first tick  bat           (2262,5335)  seen 5 of 18 runs
    first tick  bat           (2258,5330)  seen 5 of 18 runs
    first tick  bat           (2280,5333)  seen 5 of 18 runs
    first tick  bat           (2279,5353)  seen 4 of 18 runs
    first tick  bat           (2272,5330)  seen 4 of 18 runs
    first tick  bat           (2273,5341)  seen 3 of 18 runs
    first tick  bat           (2258,5353)  seen 2 of 18 runs
    first tick  bat           (2280,5346)  seen 1 of 18 runs
    first tick  blob          (2272,5330)  seen 4 of 18 runs
    first tick  blob          (2258,5353)  seen 3 of 18 runs
    first tick  blob          (2260,5347)  seen 3 of 18 runs
    first tick  blob          (2273,5341)  seen 3 of 18 runs
    first tick  blob          (2258,5330)  seen 2 of 18 runs
    first tick  blob          (2262,5335)  seen 1 of 18 runs
    first tick  blob          (2280,5333)  seen 1 of 18 runs
    first tick  blob          (2280,5346)  seen 1 of 18 runs
    first tick  mager         (2258,5353)  seen 4 of 18 runs
    first tick  mager         (2273,5341)  seen 3 of 18 runs
    first tick  mager         (2258,5330)  seen 3 of 18 runs
    first tick  mager         (2272,5330)  seen 2 of 18 runs
    first tick  mager         (2279,5353)  seen 2 of 18 runs
    first tick  mager         (2280,5333)  seen 1 of 18 runs
    first tick  mager         (2262,5335)  seen 1 of 18 runs
    first tick  mager         (2260,5347)  seen 1 of 18 runs
    first tick  mager         (2280,5346)  seen 1 of 18 runs
    first tick  nibbler       (2267,5345)  seen 9 of 18 runs
    first tick  nibbler       (2267,5346)  seen 7 of 18 runs
    first tick  nibbler       (2265,5347)  seen 7 of 18 runs
    first tick  nibbler       (2265,5346)  seen 6 of 18 runs
    first tick  nibbler       (2266,5347)  seen 6 of 18 runs
    first tick  nibbler       (2265,5345)  seen 6 of 18 runs
    first tick  nibbler       (2266,5345)  seen 5 of 18 runs
    first tick  nibbler       (2266,5346)  seen 4 of 18 runs
    first tick  nibbler       (2267,5347)  seen 4 of 18 runs
    first tick  pillar        (2257,5349)  seen 18 of 18 runs
    first tick  pillar        (2267,5335)  seen 18 of 18 runs
    first tick  pillar        (2274,5351)  seen 18 of 18 runs
    later       bat           (2277,5344)  seen 1
    later       bat           (2272,5344)  seen 1
    later       bat           (2276,5344)  seen 1
    later       bat           (2275,5342)  seen 1
    later       blob          (2277,5340)  seen 1
    later       bloblet_mage  (2271,5342)  seen 2
    later       bloblet_mage  (2275,5350)  seen 2
    later       bloblet_mage  (2273,5355)  seen 1
    later       bloblet_mage  (2278,5350)  seen 1
    later       bloblet_mage  (2273,5352)  seen 1
    later       bloblet_mage  (2268,5354)  seen 1
    later       bloblet_mage  (2274,5350)  seen 1
wave 41  (runs 18)
    first tick  blob          (2258,5330)  seen 7 of 18 runs
    first tick  blob          (2273,5341)  seen 7 of 18 runs
    first tick  blob          (2280,5346)  seen 6 of 18 runs
    first tick  blob          (2260,5347)  seen 5 of 18 runs
    first tick  blob          (2279,5353)  seen 3 of 18 runs
    first tick  blob          (2258,5353)  seen 3 of 18 runs
    first tick  blob          (2272,5330)  seen 2 of 18 runs
    first tick  blob          (2280,5333)  seen 2 of 18 runs
    first tick  blob          (2262,5335)  seen 1 of 18 runs
    first tick  mager         (2280,5346)  seen 4 of 18 runs
    first tick  mager         (2280,5333)  seen 3 of 18 runs
    first tick  mager         (2273,5341)  seen 3 of 18 runs
    first tick  mager         (2260,5347)  seen 2 of 18 runs
    first tick  mager         (2258,5330)  seen 2 of 18 runs
    first tick  mager         (2279,5353)  seen 2 of 18 runs
    first tick  mager         (2258,5353)  seen 1 of 18 runs
    first tick  mager         (2262,5335)  seen 1 of 18 runs
    first tick  nibbler       (2266,5345)  seen 11 of 18 runs
    first tick  nibbler       (2267,5347)  seen 8 of 18 runs
    first tick  nibbler       (2267,5346)  seen 6 of 18 runs
    first tick  nibbler       (2266,5346)  seen 6 of 18 runs
    first tick  nibbler       (2266,5347)  seen 6 of 18 runs
    first tick  nibbler       (2265,5346)  seen 5 of 18 runs
    first tick  nibbler       (2265,5345)  seen 4 of 18 runs
    first tick  nibbler       (2265,5347)  seen 4 of 18 runs
    first tick  nibbler       (2267,5345)  seen 4 of 18 runs
    first tick  pillar        (2257,5349)  seen 18 of 18 runs
    first tick  pillar        (2267,5335)  seen 18 of 18 runs
    first tick  pillar        (2274,5351)  seen 18 of 18 runs
    later       blob          (2273,5347)  seen 1
    later       blob          (2272,5341)  seen 1
    later       blob          (2272,5345)  seen 1
    later       bloblet_mage  (2279,5354)  seen 5
    later       bloblet_mage  (2274,5350)  seen 4
    later       bloblet_mage  (2275,5350)  seen 3
    later       bloblet_mage  (2262,5349)  seen 3
    later       bloblet_mage  (2273,5355)  seen 2
    later       bloblet_mage  (2275,5347)  seen 2
    later       bloblet_mage  (2278,5350)  seen 2
    later       bloblet_mage  (2275,5343)  seen 2
    later       bloblet_mage  (2260,5348)  seen 1
wave 42  (runs 18)
    first tick  mager         (2272,5330)  seen 4 of 18 runs
    first tick  mager         (2262,5335)  seen 4 of 18 runs
    first tick  mager         (2280,5333)  seen 3 of 18 runs
    first tick  mager         (2273,5341)  seen 2 of 18 runs
    first tick  mager         (2258,5330)  seen 2 of 18 runs
    first tick  mager         (2279,5353)  seen 2 of 18 runs
    first tick  mager         (2260,5347)  seen 1 of 18 runs
    first tick  meleer        (2260,5347)  seen 4 of 18 runs
    first tick  meleer        (2258,5353)  seen 3 of 18 runs
    first tick  meleer        (2280,5346)  seen 3 of 18 runs
    first tick  meleer        (2279,5353)  seen 2 of 18 runs
    first tick  meleer        (2272,5330)  seen 2 of 18 runs
    first tick  meleer        (2262,5335)  seen 1 of 18 runs
    first tick  meleer        (2280,5333)  seen 1 of 18 runs
    first tick  meleer        (2258,5330)  seen 1 of 18 runs
    first tick  meleer        (2273,5341)  seen 1 of 18 runs
    first tick  nibbler       (2266,5346)  seen 9 of 18 runs
    first tick  nibbler       (2265,5347)  seen 8 of 18 runs
    first tick  nibbler       (2266,5345)  seen 7 of 18 runs
    first tick  nibbler       (2267,5346)  seen 7 of 18 runs
    first tick  nibbler       (2266,5347)  seen 7 of 18 runs
    first tick  nibbler       (2265,5346)  seen 6 of 18 runs
    first tick  nibbler       (2267,5347)  seen 4 of 18 runs
    first tick  nibbler       (2267,5345)  seen 3 of 18 runs
    first tick  nibbler       (2265,5345)  seen 3 of 18 runs
    first tick  pillar        (2257,5349)  seen 18 of 18 runs
    first tick  pillar        (2267,5335)  seen 18 of 18 runs
    first tick  pillar        (2274,5351)  seen 18 of 18 runs
    later       meleer        (2273,5341)  seen 1
    later       meleer        (2272,5346)  seen 1
    later       meleer        (2275,5342)  seen 1
    later       meleer        (2277,5342)  seen 1
wave 43  (runs 18)
    first tick  bat           (2280,5333)  seen 4 of 18 runs
    first tick  bat           (2258,5330)  seen 4 of 18 runs
    first tick  bat           (2279,5353)  seen 3 of 18 runs
    first tick  bat           (2260,5347)  seen 3 of 18 runs
    first tick  bat           (2273,5341)  seen 2 of 18 runs
    first tick  bat           (2258,5353)  seen 1 of 18 runs
    first tick  bat           (2262,5335)  seen 1 of 18 runs
    first tick  mager         (2258,5330)  seen 4 of 18 runs
    first tick  mager         (2280,5346)  seen 3 of 18 runs
    first tick  mager         (2272,5330)  seen 3 of 18 runs
    first tick  mager         (2280,5333)  seen 2 of 18 runs
    first tick  mager         (2262,5335)  seen 2 of 18 runs
    first tick  mager         (2258,5353)  seen 2 of 18 runs
    first tick  mager         (2260,5347)  seen 1 of 18 runs
    first tick  mager         (2279,5353)  seen 1 of 18 runs
    first tick  meleer        (2272,5330)  seen 3 of 18 runs
    first tick  meleer        (2273,5341)  seen 3 of 18 runs
    first tick  meleer        (2280,5346)  seen 3 of 18 runs
    first tick  meleer        (2258,5330)  seen 3 of 18 runs
    first tick  meleer        (2279,5353)  seen 2 of 18 runs
    first tick  meleer        (2258,5353)  seen 2 of 18 runs
    first tick  meleer        (2262,5335)  seen 1 of 18 runs
    first tick  meleer        (2280,5333)  seen 1 of 18 runs
    first tick  nibbler       (2265,5345)  seen 9 of 18 runs
    first tick  nibbler       (2265,5346)  seen 9 of 18 runs
    first tick  nibbler       (2267,5347)  seen 8 of 18 runs
    first tick  nibbler       (2267,5346)  seen 8 of 18 runs
    first tick  nibbler       (2266,5346)  seen 6 of 18 runs
    first tick  nibbler       (2266,5347)  seen 5 of 18 runs
    first tick  nibbler       (2266,5345)  seen 5 of 18 runs
    first tick  nibbler       (2265,5347)  seen 2 of 18 runs
    first tick  nibbler       (2267,5345)  seen 2 of 18 runs
    first tick  pillar        (2257,5349)  seen 18 of 18 runs
    first tick  pillar        (2267,5335)  seen 18 of 18 runs
    first tick  pillar        (2274,5351)  seen 18 of 18 runs
    later       bat           (2272,5345)  seen 1
    later       bat           (2274,5343)  seen 1
    later       bat           (2274,5345)  seen 1
    later       bat           (2273,5345)  seen 1
    later       meleer        (2276,5343)  seen 1
    later       meleer        (2273,5342)  seen 1
    later       meleer        (2275,5341)  seen 1
wave 44  (runs 18)
    first tick  bat           (2272,5330)  seen 7 of 18 runs
    first tick  bat           (2262,5335)  seen 5 of 18 runs
    first tick  bat           (2258,5353)  seen 4 of 18 runs
    first tick  bat           (2258,5330)  seen 4 of 18 runs
    first tick  bat           (2279,5353)  seen 4 of 18 runs
    first tick  bat           (2260,5347)  seen 4 of 18 runs
    first tick  bat           (2273,5341)  seen 3 of 18 runs
    first tick  bat           (2280,5333)  seen 3 of 18 runs
    first tick  bat           (2280,5346)  seen 2 of 18 runs
    first tick  mager         (2279,5353)  seen 5 of 18 runs
    first tick  mager         (2262,5335)  seen 4 of 18 runs
    first tick  mager         (2280,5346)  seen 4 of 18 runs
    first tick  mager         (2258,5330)  seen 3 of 18 runs
    first tick  mager         (2273,5341)  seen 2 of 18 runs
    first tick  meleer        (2258,5330)  seen 4 of 18 runs
    first tick  meleer        (2280,5346)  seen 3 of 18 runs
    first tick  meleer        (2260,5347)  seen 3 of 18 runs
    first tick  meleer        (2272,5330)  seen 3 of 18 runs
    first tick  meleer        (2258,5353)  seen 3 of 18 runs
    first tick  meleer        (2273,5341)  seen 2 of 18 runs
    first tick  nibbler       (2266,5346)  seen 10 of 18 runs
    first tick  nibbler       (2265,5346)  seen 9 of 18 runs
    first tick  nibbler       (2267,5346)  seen 8 of 18 runs
    first tick  nibbler       (2266,5345)  seen 6 of 18 runs
    first tick  nibbler       (2265,5345)  seen 5 of 18 runs
    first tick  nibbler       (2266,5347)  seen 5 of 18 runs
    first tick  nibbler       (2265,5347)  seen 4 of 18 runs
    first tick  nibbler       (2267,5347)  seen 4 of 18 runs
    first tick  nibbler       (2267,5345)  seen 3 of 18 runs
    first tick  pillar        (2257,5349)  seen 18 of 18 runs
    first tick  pillar        (2267,5335)  seen 18 of 18 runs
    first tick  pillar        (2274,5351)  seen 18 of 18 runs
    later       bat           (2272,5341)  seen 1
    later       bat           (2274,5345)  seen 1
    later       bat           (2271,5344)  seen 1
    later       bat           (2274,5340)  seen 1
    later       bat           (2274,5341)  seen 1
    later       meleer        (2273,5340)  seen 1
    later       meleer        (2276,5345)  seen 1
    later       meleer        (2273,5341)  seen 1
wave 45  (runs 17)
    first tick  blob          (2260,5347)  seen 7 of 17 runs
    first tick  blob          (2258,5330)  seen 3 of 17 runs
    first tick  blob          (2279,5353)  seen 2 of 17 runs
    first tick  blob          (2262,5335)  seen 2 of 17 runs
    first tick  blob          (2258,5353)  seen 1 of 17 runs
    first tick  blob          (2280,5333)  seen 1 of 17 runs
    first tick  blob          (2273,5341)  seen 1 of 17 runs
    first tick  mager         (2280,5333)  seen 5 of 17 runs
    first tick  mager         (2273,5341)  seen 4 of 17 runs
    first tick  mager         (2258,5353)  seen 3 of 17 runs
    first tick  mager         (2262,5335)  seen 1 of 17 runs
    first tick  mager         (2280,5346)  seen 1 of 17 runs
    first tick  mager         (2279,5353)  seen 1 of 17 runs
    first tick  mager         (2272,5330)  seen 1 of 17 runs
    first tick  mager         (2258,5330)  seen 1 of 17 runs
    first tick  meleer        (2260,5347)  seen 3 of 17 runs
    first tick  meleer        (2280,5333)  seen 2 of 17 runs
    first tick  meleer        (2258,5330)  seen 2 of 17 runs
    first tick  meleer        (2279,5353)  seen 2 of 17 runs
    first tick  meleer        (2273,5341)  seen 2 of 17 runs
    first tick  meleer        (2272,5330)  seen 2 of 17 runs
    first tick  meleer        (2262,5335)  seen 2 of 17 runs
    first tick  meleer        (2258,5353)  seen 1 of 17 runs
    first tick  meleer        (2280,5346)  seen 1 of 17 runs
    first tick  nibbler       (2266,5347)  seen 9 of 17 runs
    first tick  nibbler       (2265,5345)  seen 7 of 17 runs
    first tick  nibbler       (2266,5346)  seen 7 of 17 runs
    first tick  nibbler       (2265,5346)  seen 6 of 17 runs
    first tick  nibbler       (2265,5347)  seen 6 of 17 runs
    first tick  nibbler       (2266,5345)  seen 6 of 17 runs
    first tick  nibbler       (2267,5345)  seen 5 of 17 runs
    first tick  nibbler       (2267,5346)  seen 4 of 17 runs
    first tick  nibbler       (2267,5347)  seen 1 of 17 runs
    first tick  pillar        (2257,5349)  seen 17 of 17 runs
    first tick  pillar        (2267,5335)  seen 17 of 17 runs
    first tick  pillar        (2274,5351)  seen 17 of 17 runs
    later       blob          (2274,5344)  seen 1
    later       bloblet_mage  (2273,5354)  seen 3
    later       bloblet_mage  (2274,5350)  seen 2
    later       bloblet_mage  (2262,5349)  seen 1
    later       bloblet_mage  (2276,5346)  seen 1
    later       bloblet_mage  (2273,5355)  seen 1
    later       bloblet_mage  (2269,5344)  seen 1
    later       bloblet_mage  (2279,5355)  seen 1
    later       bloblet_mage  (2273,5352)  seen 1
    later       bloblet_mage  (2266,5338)  seen 1
    later       bloblet_mage  (2275,5346)  seen 1
    later       bloblet_mage  (2279,5352)  seen 1
wave 46  (runs 17)
    first tick  bat           (2258,5353)  seen 4 of 17 runs
    first tick  bat           (2262,5335)  seen 4 of 17 runs
    first tick  bat           (2258,5330)  seen 3 of 17 runs
    first tick  bat           (2260,5347)  seen 2 of 17 runs
    first tick  bat           (2280,5346)  seen 1 of 17 runs
    first tick  bat           (2273,5341)  seen 1 of 17 runs
    first tick  bat           (2279,5353)  seen 1 of 17 runs
    first tick  bat           (2272,5330)  seen 1 of 17 runs
    first tick  blob          (2260,5347)  seen 4 of 17 runs
    first tick  blob          (2272,5330)  seen 4 of 17 runs
    first tick  blob          (2279,5353)  seen 3 of 17 runs
    first tick  blob          (2262,5335)  seen 2 of 17 runs
    first tick  blob          (2280,5333)  seen 1 of 17 runs
    first tick  blob          (2273,5341)  seen 1 of 17 runs
    first tick  blob          (2258,5330)  seen 1 of 17 runs
    first tick  blob          (2280,5346)  seen 1 of 17 runs
    first tick  mager         (2280,5346)  seen 5 of 17 runs
    first tick  mager         (2258,5353)  seen 3 of 17 runs
    first tick  mager         (2260,5347)  seen 3 of 17 runs
    first tick  mager         (2272,5330)  seen 2 of 17 runs
    first tick  mager         (2273,5341)  seen 2 of 17 runs
    first tick  mager         (2262,5335)  seen 1 of 17 runs
    first tick  mager         (2280,5333)  seen 1 of 17 runs
    first tick  meleer        (2262,5335)  seen 4 of 17 runs
    first tick  meleer        (2279,5353)  seen 3 of 17 runs
    first tick  meleer        (2272,5330)  seen 3 of 17 runs
    first tick  meleer        (2280,5333)  seen 3 of 17 runs
    first tick  meleer        (2260,5347)  seen 2 of 17 runs
    first tick  meleer        (2258,5330)  seen 1 of 17 runs
    first tick  meleer        (2258,5353)  seen 1 of 17 runs
    first tick  nibbler       (2266,5346)  seen 9 of 17 runs
    first tick  nibbler       (2267,5347)  seen 7 of 17 runs
    first tick  nibbler       (2267,5346)  seen 6 of 17 runs
    first tick  nibbler       (2266,5347)  seen 6 of 17 runs
    first tick  nibbler       (2265,5345)  seen 6 of 17 runs
    first tick  nibbler       (2267,5345)  seen 5 of 17 runs
    first tick  nibbler       (2265,5346)  seen 4 of 17 runs
    first tick  nibbler       (2265,5347)  seen 4 of 17 runs
    first tick  nibbler       (2266,5345)  seen 4 of 17 runs
    first tick  pillar        (2257,5349)  seen 17 of 17 runs
    first tick  pillar        (2267,5335)  seen 17 of 17 runs
    first tick  pillar        (2274,5351)  seen 17 of 17 runs
    later       bat           (2276,5346)  seen 1
    later       bat           (2275,5345)  seen 1
    later       bat           (2275,5341)  seen 1
    later       bat           (2276,5343)  seen 1
    later       bat           (2272,5346)  seen 1
    later       blob          (2274,5343)  seen 1
    later       bloblet_mage  (2274,5350)  seen 4
    later       bloblet_mage  (2273,5355)  seen 2
    later       bloblet_mage  (2279,5356)  seen 1
    later       bloblet_mage  (2278,5348)  seen 1
    later       bloblet_mage  (2278,5350)  seen 1
    later       bloblet_mage  (2279,5355)  seen 1
wave 47  (runs 17)
    first tick  bat           (2273,5341)  seen 7 of 17 runs
    first tick  bat           (2279,5353)  seen 6 of 17 runs
    first tick  bat           (2272,5330)  seen 6 of 17 runs
    first tick  bat           (2280,5346)  seen 5 of 17 runs
    first tick  bat           (2262,5335)  seen 4 of 17 runs
    first tick  bat           (2258,5353)  seen 2 of 17 runs
    first tick  bat           (2258,5330)  seen 2 of 17 runs
    first tick  bat           (2260,5347)  seen 1 of 17 runs
    first tick  bat           (2280,5333)  seen 1 of 17 runs
    first tick  blob          (2258,5330)  seen 5 of 17 runs
    first tick  blob          (2272,5330)  seen 3 of 17 runs
    first tick  blob          (2280,5346)  seen 3 of 17 runs
    first tick  blob          (2279,5353)  seen 2 of 17 runs
    first tick  blob          (2273,5341)  seen 2 of 17 runs
    first tick  blob          (2260,5347)  seen 1 of 17 runs
    first tick  blob          (2280,5333)  seen 1 of 17 runs
    first tick  mager         (2280,5333)  seen 4 of 17 runs
    first tick  mager         (2279,5353)  seen 3 of 17 runs
    first tick  mager         (2258,5330)  seen 3 of 17 runs
    first tick  mager         (2260,5347)  seen 2 of 17 runs
    first tick  mager         (2280,5346)  seen 2 of 17 runs
    first tick  mager         (2258,5353)  seen 1 of 17 runs
    first tick  mager         (2273,5341)  seen 1 of 17 runs
    first tick  mager         (2272,5330)  seen 1 of 17 runs
    first tick  meleer        (2280,5346)  seen 4 of 17 runs
    first tick  meleer        (2262,5335)  seen 3 of 17 runs
    first tick  meleer        (2258,5330)  seen 2 of 17 runs
    first tick  meleer        (2273,5341)  seen 2 of 17 runs
    first tick  meleer        (2280,5333)  seen 2 of 17 runs
    first tick  meleer        (2258,5353)  seen 2 of 17 runs
    first tick  meleer        (2272,5330)  seen 1 of 17 runs
    first tick  meleer        (2279,5353)  seen 1 of 17 runs
    first tick  nibbler       (2267,5346)  seen 9 of 17 runs
    first tick  nibbler       (2266,5345)  seen 8 of 17 runs
    first tick  nibbler       (2265,5345)  seen 6 of 17 runs
    first tick  nibbler       (2267,5345)  seen 6 of 17 runs
    first tick  nibbler       (2267,5347)  seen 6 of 17 runs
    first tick  nibbler       (2266,5347)  seen 5 of 17 runs
    first tick  nibbler       (2266,5346)  seen 4 of 17 runs
    first tick  nibbler       (2265,5347)  seen 4 of 17 runs
    first tick  nibbler       (2265,5346)  seen 3 of 17 runs
    first tick  pillar        (2257,5349)  seen 17 of 17 runs
    first tick  pillar        (2267,5335)  seen 17 of 17 runs
    first tick  pillar        (2274,5351)  seen 17 of 17 runs
    later       bat           (2274,5347)  seen 1
    later       bat           (2275,5345)  seen 1
    later       bat           (2276,5341)  seen 1
    later       bat           (2276,5345)  seen 1
    later       bat           (2272,5344)  seen 1
    later       bloblet_mage  (2274,5350)  seen 4
    later       bloblet_mage  (2279,5355)  seen 2
    later       bloblet_mage  (2276,5348)  seen 1
    later       bloblet_mage  (2275,5350)  seen 1
    later       bloblet_mage  (2270,5346)  seen 1
    later       bloblet_mage  (2273,5353)  seen 1
    later       bloblet_mage  (2276,5344)  seen 1
wave 48  (runs 17)
    first tick  blob          (2273,5341)  seen 5 of 17 runs
    first tick  blob          (2272,5330)  seen 5 of 17 runs
    first tick  blob          (2262,5335)  seen 5 of 17 runs
    first tick  blob          (2260,5347)  seen 4 of 17 runs
    first tick  blob          (2279,5353)  seen 4 of 17 runs
    first tick  blob          (2258,5330)  seen 3 of 17 runs
    first tick  blob          (2280,5333)  seen 3 of 17 runs
    first tick  blob          (2258,5353)  seen 3 of 17 runs
    first tick  blob          (2280,5346)  seen 2 of 17 runs
    first tick  mager         (2258,5330)  seen 4 of 17 runs
    first tick  mager         (2260,5347)  seen 3 of 17 runs
    first tick  mager         (2262,5335)  seen 3 of 17 runs
    first tick  mager         (2280,5333)  seen 2 of 17 runs
    first tick  mager         (2279,5353)  seen 2 of 17 runs
    first tick  mager         (2280,5346)  seen 1 of 17 runs
    first tick  mager         (2272,5330)  seen 1 of 17 runs
    first tick  mager         (2273,5341)  seen 1 of 17 runs
    first tick  meleer        (2272,5330)  seen 4 of 17 runs
    first tick  meleer        (2279,5353)  seen 4 of 17 runs
    first tick  meleer        (2262,5335)  seen 2 of 17 runs
    first tick  meleer        (2258,5330)  seen 2 of 17 runs
    first tick  meleer        (2280,5346)  seen 2 of 17 runs
    first tick  meleer        (2273,5341)  seen 1 of 17 runs
    first tick  meleer        (2280,5333)  seen 1 of 17 runs
    first tick  meleer        (2258,5353)  seen 1 of 17 runs
    first tick  nibbler       (2265,5345)  seen 9 of 17 runs
    first tick  nibbler       (2265,5347)  seen 9 of 17 runs
    first tick  nibbler       (2266,5346)  seen 6 of 17 runs
    first tick  nibbler       (2266,5345)  seen 6 of 17 runs
    first tick  nibbler       (2266,5347)  seen 5 of 17 runs
    first tick  nibbler       (2267,5345)  seen 5 of 17 runs
    first tick  nibbler       (2267,5347)  seen 4 of 17 runs
    first tick  nibbler       (2267,5346)  seen 4 of 17 runs
    first tick  nibbler       (2265,5346)  seen 3 of 17 runs
    first tick  pillar        (2257,5349)  seen 17 of 17 runs
    first tick  pillar        (2267,5335)  seen 17 of 17 runs
    first tick  pillar        (2274,5351)  seen 17 of 17 runs
    later       blob          (2273,5343)  seen 1
    later       blob          (2277,5344)  seen 1
    later       bloblet_mage  (2274,5350)  seen 5
    later       bloblet_mage  (2275,5350)  seen 4
    later       bloblet_mage  (2277,5350)  seen 3
    later       bloblet_mage  (2273,5351)  seen 2
    later       bloblet_mage  (2279,5354)  seen 2
    later       bloblet_mage  (2272,5338)  seen 2
    later       bloblet_mage  (2273,5354)  seen 1
    later       bloblet_mage  (2278,5350)  seen 1
    later       bloblet_mage  (2276,5350)  seen 1
    later       bloblet_mage  (2269,5355)  seen 1
wave 49  (runs 16)
    first tick  mager         (2260,5347)  seen 5 of 16 runs
    first tick  mager         (2262,5335)  seen 3 of 16 runs
    first tick  mager         (2273,5341)  seen 3 of 16 runs
    first tick  mager         (2258,5330)  seen 2 of 16 runs
    first tick  mager         (2280,5346)  seen 1 of 16 runs
    first tick  mager         (2258,5353)  seen 1 of 16 runs
    first tick  mager         (2279,5353)  seen 1 of 16 runs
    first tick  meleer        (2258,5330)  seen 6 of 16 runs
    first tick  meleer        (2280,5333)  seen 5 of 16 runs
    first tick  meleer        (2272,5330)  seen 4 of 16 runs
    first tick  meleer        (2262,5335)  seen 4 of 16 runs
    first tick  meleer        (2280,5346)  seen 4 of 16 runs
    first tick  meleer        (2273,5341)  seen 3 of 16 runs
    first tick  meleer        (2258,5353)  seen 3 of 16 runs
    first tick  meleer        (2279,5353)  seen 2 of 16 runs
    first tick  meleer        (2260,5347)  seen 1 of 16 runs
    first tick  nibbler       (2266,5345)  seen 9 of 16 runs
    first tick  nibbler       (2267,5346)  seen 7 of 16 runs
    first tick  nibbler       (2265,5345)  seen 7 of 16 runs
    first tick  nibbler       (2266,5347)  seen 7 of 16 runs
    first tick  nibbler       (2265,5346)  seen 7 of 16 runs
    first tick  nibbler       (2265,5347)  seen 4 of 16 runs
    first tick  nibbler       (2266,5346)  seen 3 of 16 runs
    first tick  nibbler       (2267,5347)  seen 2 of 16 runs
    first tick  nibbler       (2267,5345)  seen 2 of 16 runs
    first tick  pillar        (2257,5349)  seen 16 of 16 runs
    first tick  pillar        (2267,5335)  seen 16 of 16 runs
    first tick  pillar        (2274,5351)  seen 16 of 16 runs
    later       meleer        (2271,5343)  seen 1
    later       meleer        (2274,5345)  seen 1
    later       meleer        (2272,5347)  seen 1
    later       meleer        (2276,5341)  seen 1
    later       meleer        (2272,5342)  seen 1
    later       meleer        (2274,5340)  seen 1
wave 50  (runs 16)
    first tick  mager         (2280,5333)  seen 3 of 16 runs
    first tick  mager         (2272,5330)  seen 3 of 16 runs
    first tick  mager         (2273,5341)  seen 3 of 16 runs
    first tick  mager         (2279,5353)  seen 2 of 16 runs
    first tick  mager         (2258,5353)  seen 2 of 16 runs
    first tick  mager         (2258,5330)  seen 2 of 16 runs
    first tick  mager         (2260,5347)  seen 1 of 16 runs
    first tick  nibbler       (2267,5345)  seen 10 of 16 runs
    first tick  nibbler       (2265,5346)  seen 9 of 16 runs
    first tick  nibbler       (2266,5345)  seen 7 of 16 runs
    first tick  nibbler       (2266,5347)  seen 6 of 16 runs
    first tick  nibbler       (2265,5347)  seen 5 of 16 runs
    first tick  nibbler       (2266,5346)  seen 4 of 16 runs
    first tick  nibbler       (2267,5347)  seen 3 of 16 runs
    first tick  nibbler       (2265,5345)  seen 2 of 16 runs
    first tick  nibbler       (2267,5346)  seen 2 of 16 runs
    first tick  pillar        (2257,5349)  seen 16 of 16 runs
    first tick  pillar        (2267,5335)  seen 16 of 16 runs
    first tick  pillar        (2274,5351)  seen 16 of 16 runs
    first tick  ranger        (2279,5353)  seen 3 of 16 runs
    first tick  ranger        (2262,5335)  seen 3 of 16 runs
    first tick  ranger        (2258,5330)  seen 2 of 16 runs
    first tick  ranger        (2280,5346)  seen 2 of 16 runs
    first tick  ranger        (2260,5347)  seen 2 of 16 runs
    first tick  ranger        (2273,5341)  seen 2 of 16 runs
    first tick  ranger        (2272,5330)  seen 1 of 16 runs
    first tick  ranger        (2280,5333)  seen 1 of 16 runs
wave 51  (runs 16)
    first tick  bat           (2280,5346)  seen 6 of 16 runs
    first tick  bat           (2280,5333)  seen 3 of 16 runs
    first tick  bat           (2279,5353)  seen 2 of 16 runs
    first tick  bat           (2260,5347)  seen 2 of 16 runs
    first tick  bat           (2273,5341)  seen 1 of 16 runs
    first tick  bat           (2272,5330)  seen 1 of 16 runs
    first tick  bat           (2258,5353)  seen 1 of 16 runs
    first tick  mager         (2280,5346)  seen 3 of 16 runs
    first tick  mager         (2273,5341)  seen 3 of 16 runs
    first tick  mager         (2258,5353)  seen 2 of 16 runs
    first tick  mager         (2262,5335)  seen 2 of 16 runs
    first tick  mager         (2272,5330)  seen 2 of 16 runs
    first tick  mager         (2260,5347)  seen 2 of 16 runs
    first tick  mager         (2280,5333)  seen 1 of 16 runs
    first tick  mager         (2258,5330)  seen 1 of 16 runs
    first tick  nibbler       (2265,5345)  seen 8 of 16 runs
    first tick  nibbler       (2266,5346)  seen 7 of 16 runs
    first tick  nibbler       (2267,5347)  seen 6 of 16 runs
    first tick  nibbler       (2267,5345)  seen 6 of 16 runs
    first tick  nibbler       (2265,5347)  seen 6 of 16 runs
    first tick  nibbler       (2266,5345)  seen 6 of 16 runs
    first tick  nibbler       (2265,5346)  seen 4 of 16 runs
    first tick  nibbler       (2267,5346)  seen 3 of 16 runs
    first tick  nibbler       (2266,5347)  seen 2 of 16 runs
    first tick  pillar        (2257,5349)  seen 16 of 16 runs
    first tick  pillar        (2267,5335)  seen 16 of 16 runs
    first tick  pillar        (2274,5351)  seen 16 of 16 runs
    first tick  ranger        (2258,5353)  seen 4 of 16 runs
    first tick  ranger        (2258,5330)  seen 3 of 16 runs
    first tick  ranger        (2260,5347)  seen 2 of 16 runs
    first tick  ranger        (2273,5341)  seen 2 of 16 runs
    first tick  ranger        (2279,5353)  seen 1 of 16 runs
    first tick  ranger        (2280,5346)  seen 1 of 16 runs
    first tick  ranger        (2272,5330)  seen 1 of 16 runs
    first tick  ranger        (2280,5333)  seen 1 of 16 runs
    first tick  ranger        (2262,5335)  seen 1 of 16 runs
    later       bat           (2272,5346)  seen 1
    later       bat           (2276,5346)  seen 1
    later       bat           (2272,5343)  seen 1
    later       bat           (2275,5346)  seen 1
    later       bat           (2274,5344)  seen 1
    later       bat           (2273,5340)  seen 1
    later       bat           (2274,5342)  seen 1
wave 52  (runs 16)
    first tick  bat           (2272,5330)  seen 6 of 16 runs
    first tick  bat           (2258,5330)  seen 6 of 16 runs
    first tick  bat           (2279,5353)  seen 5 of 16 runs
    first tick  bat           (2273,5341)  seen 4 of 16 runs
    first tick  bat           (2280,5346)  seen 3 of 16 runs
    first tick  bat           (2258,5353)  seen 3 of 16 runs
    first tick  bat           (2262,5335)  seen 2 of 16 runs
    first tick  bat           (2260,5347)  seen 2 of 16 runs
    first tick  bat           (2280,5333)  seen 1 of 16 runs
    first tick  mager         (2260,5347)  seen 5 of 16 runs
    first tick  mager         (2262,5335)  seen 4 of 16 runs
    first tick  mager         (2258,5330)  seen 3 of 16 runs
    first tick  mager         (2258,5353)  seen 2 of 16 runs
    first tick  mager         (2272,5330)  seen 1 of 16 runs
    first tick  mager         (2273,5341)  seen 1 of 16 runs
    first tick  nibbler       (2266,5345)  seen 8 of 16 runs
    first tick  nibbler       (2265,5347)  seen 8 of 16 runs
    first tick  nibbler       (2266,5346)  seen 7 of 16 runs
    first tick  nibbler       (2267,5347)  seen 6 of 16 runs
    first tick  nibbler       (2267,5346)  seen 5 of 16 runs
    first tick  nibbler       (2265,5345)  seen 4 of 16 runs
    first tick  nibbler       (2265,5346)  seen 4 of 16 runs
    first tick  nibbler       (2266,5347)  seen 3 of 16 runs
    first tick  nibbler       (2267,5345)  seen 3 of 16 runs
    first tick  pillar        (2257,5349)  seen 16 of 16 runs
    first tick  pillar        (2267,5335)  seen 16 of 16 runs
    first tick  pillar        (2274,5351)  seen 16 of 16 runs
    first tick  ranger        (2279,5353)  seen 4 of 16 runs
    first tick  ranger        (2260,5347)  seen 2 of 16 runs
    first tick  ranger        (2273,5341)  seen 2 of 16 runs
    first tick  ranger        (2258,5353)  seen 2 of 16 runs
    first tick  ranger        (2280,5333)  seen 2 of 16 runs
    first tick  ranger        (2272,5330)  seen 2 of 16 runs
    first tick  ranger        (2262,5335)  seen 1 of 16 runs
    first tick  ranger        (2258,5330)  seen 1 of 16 runs
    later       bat           (2276,5345)  seen 1
    later       bat           (2273,5344)  seen 1
    later       bat           (2276,5346)  seen 1
    later       bat           (2273,5343)  seen 1
    later       bat           (2275,5345)  seen 1
    later       bat           (2275,5341)  seen 1
    later       bat           (2273,5345)  seen 1
wave 53  (runs 16)
    first tick  blob          (2272,5330)  seen 3 of 16 runs
    first tick  blob          (2258,5330)  seen 2 of 16 runs
    first tick  blob          (2280,5346)  seen 2 of 16 runs
    first tick  blob          (2260,5347)  seen 2 of 16 runs
    first tick  blob          (2280,5333)  seen 2 of 16 runs
    first tick  blob          (2262,5335)  seen 2 of 16 runs
    first tick  blob          (2258,5353)  seen 1 of 16 runs
    first tick  blob          (2273,5341)  seen 1 of 16 runs
    first tick  blob          (2279,5353)  seen 1 of 16 runs
    first tick  mager         (2280,5346)  seen 3 of 16 runs
    first tick  mager         (2262,5335)  seen 3 of 16 runs
    first tick  mager         (2273,5341)  seen 3 of 16 runs
    first tick  mager         (2272,5330)  seen 2 of 16 runs
    first tick  mager         (2260,5347)  seen 2 of 16 runs
    first tick  mager         (2280,5333)  seen 1 of 16 runs
    first tick  mager         (2258,5330)  seen 1 of 16 runs
    first tick  mager         (2258,5353)  seen 1 of 16 runs
    first tick  nibbler       (2266,5347)  seen 8 of 16 runs
    first tick  nibbler       (2265,5345)  seen 8 of 16 runs
    first tick  nibbler       (2266,5345)  seen 8 of 16 runs
    first tick  nibbler       (2265,5347)  seen 6 of 16 runs
    first tick  nibbler       (2266,5346)  seen 5 of 16 runs
    first tick  nibbler       (2267,5346)  seen 5 of 16 runs
    first tick  nibbler       (2267,5347)  seen 4 of 16 runs
    first tick  nibbler       (2267,5345)  seen 3 of 16 runs
    first tick  nibbler       (2265,5346)  seen 1 of 16 runs
    first tick  pillar        (2257,5349)  seen 16 of 16 runs
    first tick  pillar        (2267,5335)  seen 16 of 16 runs
    first tick  pillar        (2274,5351)  seen 16 of 16 runs
    first tick  ranger        (2279,5353)  seen 4 of 16 runs
    first tick  ranger        (2260,5347)  seen 3 of 16 runs
    first tick  ranger        (2280,5346)  seen 2 of 16 runs
    first tick  ranger        (2273,5341)  seen 2 of 16 runs
    first tick  ranger        (2272,5330)  seen 1 of 16 runs
    first tick  ranger        (2280,5333)  seen 1 of 16 runs
    first tick  ranger        (2258,5353)  seen 1 of 16 runs
    first tick  ranger        (2258,5330)  seen 1 of 16 runs
    first tick  ranger        (2262,5335)  seen 1 of 16 runs
    later       blob          (2271,5342)  seen 1
    later       blob          (2275,5346)  seen 1
    later       bloblet_mage  (2274,5350)  seen 7
    later       bloblet_mage  (2275,5350)  seen 2
    later       bloblet_mage  (2260,5355)  seen 1
    later       bloblet_mage  (2279,5352)  seen 1
    later       bloblet_mage  (2273,5352)  seen 1
    later       bloblet_mage  (2279,5354)  seen 1
    later       bloblet_mage  (2262,5349)  seen 1
    later       bloblet_mage  (2278,5350)  seen 1
    later       bloblet_mage  (2277,5348)  seen 1
    later       bloblet_mage  (2276,5347)  seen 1
wave 54  (runs 16)
    first tick  bat           (2280,5346)  seen 4 of 16 runs
    first tick  bat           (2258,5353)  seen 3 of 16 runs
    first tick  bat           (2260,5347)  seen 2 of 16 runs
    first tick  bat           (2273,5341)  seen 2 of 16 runs
    first tick  bat           (2280,5333)  seen 2 of 16 runs
    first tick  bat           (2258,5330)  seen 1 of 16 runs
    first tick  bat           (2279,5353)  seen 1 of 16 runs
    first tick  bat           (2272,5330)  seen 1 of 16 runs
    first tick  blob          (2260,5347)  seen 5 of 16 runs
    first tick  blob          (2258,5330)  seen 4 of 16 runs
    first tick  blob          (2262,5335)  seen 4 of 16 runs
    first tick  blob          (2280,5346)  seen 1 of 16 runs
    first tick  blob          (2258,5353)  seen 1 of 16 runs
    first tick  blob          (2280,5333)  seen 1 of 16 runs
    first tick  mager         (2279,5353)  seen 5 of 16 runs
    first tick  mager         (2280,5333)  seen 3 of 16 runs
    first tick  mager         (2272,5330)  seen 3 of 16 runs
    first tick  mager         (2273,5341)  seen 2 of 16 runs
    first tick  mager         (2260,5347)  seen 1 of 16 runs
    first tick  mager         (2258,5330)  seen 1 of 16 runs
    first tick  mager         (2258,5353)  seen 1 of 16 runs
    first tick  nibbler       (2266,5346)  seen 10 of 16 runs
    first tick  nibbler       (2267,5345)  seen 8 of 16 runs
    first tick  nibbler       (2266,5347)  seen 8 of 16 runs
    first tick  nibbler       (2265,5345)  seen 5 of 16 runs
    first tick  nibbler       (2266,5345)  seen 5 of 16 runs
    first tick  nibbler       (2265,5346)  seen 4 of 16 runs
    first tick  nibbler       (2267,5346)  seen 3 of 16 runs
    first tick  nibbler       (2265,5347)  seen 3 of 16 runs
    first tick  nibbler       (2267,5347)  seen 2 of 16 runs
    first tick  pillar        (2257,5349)  seen 16 of 16 runs
    first tick  pillar        (2267,5335)  seen 16 of 16 runs
    first tick  pillar        (2274,5351)  seen 16 of 16 runs
    first tick  ranger        (2258,5353)  seen 4 of 16 runs
    first tick  ranger        (2273,5341)  seen 3 of 16 runs
    first tick  ranger        (2280,5333)  seen 3 of 16 runs
    first tick  ranger        (2262,5335)  seen 2 of 16 runs
    first tick  ranger        (2280,5346)  seen 1 of 16 runs
    first tick  ranger        (2260,5347)  seen 1 of 16 runs
    first tick  ranger        (2279,5353)  seen 1 of 16 runs
    first tick  ranger        (2272,5330)  seen 1 of 16 runs
    later       bat           (2275,5343)  seen 1
    later       bat           (2276,5347)  seen 1
    later       bat           (2274,5346)  seen 1
    later       bat           (2273,5344)  seen 1
    later       blob          (2274,5341)  seen 1
    later       bloblet_mage  (2274,5350)  seen 3
    later       bloblet_mage  (2265,5352)  seen 3
    later       bloblet_mage  (2275,5350)  seen 2
    later       bloblet_mage  (2273,5351)  seen 1
    later       bloblet_mage  (2276,5349)  seen 1
    later       bloblet_mage  (2279,5355)  seen 1
    later       bloblet_mage  (2273,5355)  seen 1
wave 55  (runs 16)
    first tick  bat           (2258,5353)  seen 5 of 16 runs
    first tick  bat           (2260,5347)  seen 5 of 16 runs
    first tick  bat           (2272,5330)  seen 4 of 16 runs
    first tick  bat           (2280,5333)  seen 4 of 16 runs
    first tick  bat           (2280,5346)  seen 4 of 16 runs
    first tick  bat           (2279,5353)  seen 4 of 16 runs
    first tick  bat           (2258,5330)  seen 2 of 16 runs
    first tick  bat           (2273,5341)  seen 2 of 16 runs
    first tick  bat           (2262,5335)  seen 2 of 16 runs
    first tick  blob          (2273,5341)  seen 5 of 16 runs
    first tick  blob          (2262,5335)  seen 3 of 16 runs
    first tick  blob          (2258,5353)  seen 2 of 16 runs
    first tick  blob          (2280,5346)  seen 2 of 16 runs
    first tick  blob          (2280,5333)  seen 2 of 16 runs
    first tick  blob          (2272,5330)  seen 1 of 16 runs
    first tick  blob          (2260,5347)  seen 1 of 16 runs
    first tick  mager         (2262,5335)  seen 5 of 16 runs
    first tick  mager         (2280,5346)  seen 3 of 16 runs
    first tick  mager         (2258,5353)  seen 2 of 16 runs
    first tick  mager         (2258,5330)  seen 2 of 16 runs
    first tick  mager         (2273,5341)  seen 2 of 16 runs
    first tick  mager         (2260,5347)  seen 1 of 16 runs
    first tick  mager         (2272,5330)  seen 1 of 16 runs
    first tick  nibbler       (2267,5347)  seen 9 of 16 runs
    first tick  nibbler       (2265,5346)  seen 7 of 16 runs
    first tick  nibbler       (2266,5346)  seen 6 of 16 runs
    first tick  nibbler       (2267,5346)  seen 6 of 16 runs
    first tick  nibbler       (2265,5345)  seen 6 of 16 runs
    first tick  nibbler       (2267,5345)  seen 5 of 16 runs
    first tick  nibbler       (2265,5347)  seen 5 of 16 runs
    first tick  nibbler       (2266,5345)  seen 3 of 16 runs
    first tick  nibbler       (2266,5347)  seen 1 of 16 runs
    first tick  pillar        (2257,5349)  seen 16 of 16 runs
    first tick  pillar        (2267,5335)  seen 16 of 16 runs
    first tick  pillar        (2274,5351)  seen 16 of 16 runs
    first tick  ranger        (2258,5330)  seen 4 of 16 runs
    first tick  ranger        (2262,5335)  seen 3 of 16 runs
    first tick  ranger        (2280,5333)  seen 2 of 16 runs
    first tick  ranger        (2273,5341)  seen 2 of 16 runs
    first tick  ranger        (2272,5330)  seen 1 of 16 runs
    first tick  ranger        (2279,5353)  seen 1 of 16 runs
    first tick  ranger        (2280,5346)  seen 1 of 16 runs
    first tick  ranger        (2258,5353)  seen 1 of 16 runs
    first tick  ranger        (2260,5347)  seen 1 of 16 runs
    later       bat           (2274,5346)  seen 1
    later       bat           (2272,5342)  seen 1
    later       bat           (2272,5345)  seen 1
    later       bat           (2274,5344)  seen 1
    later       bat           (2276,5346)  seen 1
    later       bat           (2275,5343)  seen 1
    later       bat           (2273,5343)  seen 1
    later       bat           (2275,5345)  seen 1
    later       bat           (2271,5344)  seen 1
    later       bat           (2276,5342)  seen 1
    later       blob          (2274,5343)  seen 1
    later       bloblet_mage  (2274,5350)  seen 4
wave 56  (runs 15)
    first tick  blob          (2279,5353)  seen 6 of 15 runs
    first tick  blob          (2260,5347)  seen 6 of 15 runs
    first tick  blob          (2280,5333)  seen 4 of 15 runs
    first tick  blob          (2258,5353)  seen 4 of 15 runs
    first tick  blob          (2258,5330)  seen 3 of 15 runs
    first tick  blob          (2262,5335)  seen 2 of 15 runs
    first tick  blob          (2280,5346)  seen 2 of 15 runs
    first tick  blob          (2273,5341)  seen 2 of 15 runs
    first tick  blob          (2272,5330)  seen 1 of 15 runs
    first tick  mager         (2272,5330)  seen 5 of 15 runs
    first tick  mager         (2258,5353)  seen 3 of 15 runs
    first tick  mager         (2280,5346)  seen 2 of 15 runs
    first tick  mager         (2262,5335)  seen 2 of 15 runs
    first tick  mager         (2273,5341)  seen 1 of 15 runs
    first tick  mager         (2258,5330)  seen 1 of 15 runs
    first tick  mager         (2279,5353)  seen 1 of 15 runs
    first tick  nibbler       (2265,5346)  seen 9 of 15 runs
    first tick  nibbler       (2266,5346)  seen 8 of 15 runs
    first tick  nibbler       (2267,5346)  seen 6 of 15 runs
    first tick  nibbler       (2265,5347)  seen 6 of 15 runs
    first tick  nibbler       (2267,5345)  seen 5 of 15 runs
    first tick  nibbler       (2266,5347)  seen 5 of 15 runs
    first tick  nibbler       (2267,5347)  seen 2 of 15 runs
    first tick  nibbler       (2266,5345)  seen 2 of 15 runs
    first tick  nibbler       (2265,5345)  seen 2 of 15 runs
    first tick  pillar        (2257,5349)  seen 15 of 15 runs
    first tick  pillar        (2267,5335)  seen 15 of 15 runs
    first tick  pillar        (2274,5351)  seen 15 of 15 runs
    first tick  ranger        (2273,5341)  seen 4 of 15 runs
    first tick  ranger        (2280,5333)  seen 4 of 15 runs
    first tick  ranger        (2279,5353)  seen 2 of 15 runs
    first tick  ranger        (2262,5335)  seen 2 of 15 runs
    first tick  ranger        (2260,5347)  seen 1 of 15 runs
    first tick  ranger        (2258,5330)  seen 1 of 15 runs
    first tick  ranger        (2272,5330)  seen 1 of 15 runs
    later       blob          (2275,5345)  seen 1
    later       blob          (2274,5345)  seen 1
    later       blob          (2276,5343)  seen 1
    later       blob          (2275,5346)  seen 1
    later       bloblet_mage  (2274,5350)  seen 6
    later       bloblet_mage  (2281,5355)  seen 5
    later       bloblet_mage  (2273,5355)  seen 4
    later       bloblet_mage  (2275,5350)  seen 2
    later       bloblet_mage  (2265,5352)  seen 2
    later       bloblet_mage  (2275,5347)  seen 2
    later       bloblet_mage  (2273,5352)  seen 2
    later       bloblet_mage  (2279,5355)  seen 1
wave 57  (runs 15)
    first tick  mager         (2273,5341)  seen 3 of 15 runs
    first tick  mager         (2280,5346)  seen 3 of 15 runs
    first tick  mager         (2258,5330)  seen 3 of 15 runs
    first tick  mager         (2272,5330)  seen 2 of 15 runs
    first tick  mager         (2262,5335)  seen 1 of 15 runs
    first tick  mager         (2279,5353)  seen 1 of 15 runs
    first tick  mager         (2280,5333)  seen 1 of 15 runs
    first tick  mager         (2258,5353)  seen 1 of 15 runs
    first tick  meleer        (2262,5335)  seen 3 of 15 runs
    first tick  meleer        (2272,5330)  seen 3 of 15 runs
    first tick  meleer        (2258,5353)  seen 3 of 15 runs
    first tick  meleer        (2279,5353)  seen 2 of 15 runs
    first tick  meleer        (2258,5330)  seen 1 of 15 runs
    first tick  meleer        (2280,5333)  seen 1 of 15 runs
    first tick  meleer        (2260,5347)  seen 1 of 15 runs
    first tick  meleer        (2280,5346)  seen 1 of 15 runs
    first tick  nibbler       (2265,5345)  seen 7 of 15 runs
    first tick  nibbler       (2266,5345)  seen 7 of 15 runs
    first tick  nibbler       (2266,5346)  seen 6 of 15 runs
    first tick  nibbler       (2265,5346)  seen 6 of 15 runs
    first tick  nibbler       (2265,5347)  seen 5 of 15 runs
    first tick  nibbler       (2266,5347)  seen 5 of 15 runs
    first tick  nibbler       (2267,5347)  seen 3 of 15 runs
    first tick  nibbler       (2267,5345)  seen 3 of 15 runs
    first tick  nibbler       (2267,5346)  seen 3 of 15 runs
    first tick  pillar        (2257,5349)  seen 15 of 15 runs
    first tick  pillar        (2267,5335)  seen 15 of 15 runs
    first tick  pillar        (2274,5351)  seen 15 of 15 runs
    first tick  ranger        (2258,5330)  seen 5 of 15 runs
    first tick  ranger        (2272,5330)  seen 3 of 15 runs
    first tick  ranger        (2280,5333)  seen 2 of 15 runs
    first tick  ranger        (2279,5353)  seen 2 of 15 runs
    first tick  ranger        (2258,5353)  seen 1 of 15 runs
    first tick  ranger        (2260,5347)  seen 1 of 15 runs
    first tick  ranger        (2273,5341)  seen 1 of 15 runs
    later       meleer        (2275,5341)  seen 1
    later       meleer        (2277,5341)  seen 1
    later       meleer        (2276,5345)  seen 1
    later       meleer        (2273,5347)  seen 1
wave 58  (runs 15)
    first tick  bat           (2280,5346)  seen 4 of 15 runs
    first tick  bat           (2273,5341)  seen 4 of 15 runs
    first tick  bat           (2262,5335)  seen 2 of 15 runs
    first tick  bat           (2280,5333)  seen 2 of 15 runs
    first tick  bat           (2260,5347)  seen 2 of 15 runs
    first tick  bat           (2258,5353)  seen 1 of 15 runs
    first tick  mager         (2280,5333)  seen 3 of 15 runs
    first tick  mager         (2258,5353)  seen 2 of 15 runs
    first tick  mager         (2273,5341)  seen 2 of 15 runs
    first tick  mager         (2260,5347)  seen 2 of 15 runs
    first tick  mager         (2258,5330)  seen 2 of 15 runs
    first tick  mager         (2272,5330)  seen 2 of 15 runs
    first tick  mager         (2279,5353)  seen 1 of 15 runs
    first tick  mager         (2280,5346)  seen 1 of 15 runs
    first tick  meleer        (2272,5330)  seen 3 of 15 runs
    first tick  meleer        (2262,5335)  seen 3 of 15 runs
    first tick  meleer        (2280,5346)  seen 3 of 15 runs
    first tick  meleer        (2258,5353)  seen 2 of 15 runs
    first tick  meleer        (2260,5347)  seen 2 of 15 runs
    first tick  meleer        (2279,5353)  seen 1 of 15 runs
    first tick  meleer        (2273,5341)  seen 1 of 15 runs
    first tick  nibbler       (2267,5347)  seen 7 of 15 runs
    first tick  nibbler       (2266,5345)  seen 7 of 15 runs
    first tick  nibbler       (2265,5346)  seen 6 of 15 runs
    first tick  nibbler       (2265,5345)  seen 6 of 15 runs
    first tick  nibbler       (2267,5346)  seen 5 of 15 runs
    first tick  nibbler       (2265,5347)  seen 5 of 15 runs
    first tick  nibbler       (2266,5346)  seen 5 of 15 runs
    first tick  nibbler       (2266,5347)  seen 3 of 15 runs
    first tick  nibbler       (2267,5345)  seen 1 of 15 runs
    first tick  pillar        (2257,5349)  seen 15 of 15 runs
    first tick  pillar        (2274,5351)  seen 15 of 15 runs
    first tick  pillar        (2267,5335)  seen 14 of 15 runs
    first tick  ranger        (2258,5330)  seen 4 of 15 runs
    first tick  ranger        (2280,5333)  seen 2 of 15 runs
    first tick  ranger        (2258,5353)  seen 2 of 15 runs
    first tick  ranger        (2272,5330)  seen 2 of 15 runs
    first tick  ranger        (2279,5353)  seen 2 of 15 runs
    first tick  ranger        (2280,5346)  seen 1 of 15 runs
    first tick  ranger        (2260,5347)  seen 1 of 15 runs
    first tick  ranger        (2273,5341)  seen 1 of 15 runs
    later       bat           (2275,5346)  seen 1
    later       bat           (2276,5345)  seen 1
    later       bat           (2274,5345)  seen 1
    later       bat           (2274,5342)  seen 1
    later       bat           (2275,5344)  seen 1
    later       bat           (2273,5343)  seen 1
    later       bat           (2270,5345)  seen 1
    later       bat           (2271,5341)  seen 1
    later       meleer        (2272,5343)  seen 2
    later       meleer        (2274,5341)  seen 1
    later       meleer        (2276,5343)  seen 1
    later       meleer        (2272,5346)  seen 1
wave 59  (runs 15)
    first tick  bat           (2258,5330)  seen 6 of 15 runs
    first tick  bat           (2280,5333)  seen 5 of 15 runs
    first tick  bat           (2279,5353)  seen 4 of 15 runs
    first tick  bat           (2258,5353)  seen 3 of 15 runs
    first tick  bat           (2260,5347)  seen 3 of 15 runs
    first tick  bat           (2280,5346)  seen 3 of 15 runs
    first tick  bat           (2262,5335)  seen 3 of 15 runs
    first tick  bat           (2273,5341)  seen 2 of 15 runs
    first tick  bat           (2272,5330)  seen 1 of 15 runs
    first tick  mager         (2260,5347)  seen 3 of 15 runs
    first tick  mager         (2273,5341)  seen 2 of 15 runs
    first tick  mager         (2280,5346)  seen 2 of 15 runs
    first tick  mager         (2279,5353)  seen 2 of 15 runs
    first tick  mager         (2262,5335)  seen 2 of 15 runs
    first tick  mager         (2280,5333)  seen 1 of 15 runs
    first tick  mager         (2272,5330)  seen 1 of 15 runs
    first tick  mager         (2258,5353)  seen 1 of 15 runs
    first tick  mager         (2258,5330)  seen 1 of 15 runs
    first tick  meleer        (2272,5330)  seen 4 of 15 runs
    first tick  meleer        (2280,5333)  seen 3 of 15 runs
    first tick  meleer        (2258,5353)  seen 3 of 15 runs
    first tick  meleer        (2280,5346)  seen 2 of 15 runs
    first tick  meleer        (2273,5341)  seen 1 of 15 runs
    first tick  meleer        (2262,5335)  seen 1 of 15 runs
    first tick  meleer        (2279,5353)  seen 1 of 15 runs
    first tick  nibbler       (2267,5346)  seen 10 of 15 runs
    first tick  nibbler       (2267,5347)  seen 9 of 15 runs
    first tick  nibbler       (2265,5347)  seen 5 of 15 runs
    first tick  nibbler       (2267,5345)  seen 5 of 15 runs
    first tick  nibbler       (2265,5345)  seen 4 of 15 runs
    first tick  nibbler       (2265,5346)  seen 4 of 15 runs
    first tick  nibbler       (2266,5346)  seen 3 of 15 runs
    first tick  nibbler       (2266,5345)  seen 3 of 15 runs
    first tick  nibbler       (2266,5347)  seen 2 of 15 runs
    first tick  pillar        (2274,5351)  seen 15 of 15 runs
    first tick  pillar        (2257,5349)  seen 14 of 15 runs
    first tick  pillar        (2267,5335)  seen 14 of 15 runs
    first tick  ranger        (2262,5335)  seen 4 of 15 runs
    first tick  ranger        (2280,5333)  seen 3 of 15 runs
    first tick  ranger        (2260,5347)  seen 2 of 15 runs
    first tick  ranger        (2279,5353)  seen 2 of 15 runs
    first tick  ranger        (2258,5330)  seen 2 of 15 runs
    first tick  ranger        (2273,5341)  seen 1 of 15 runs
    first tick  ranger        (2272,5330)  seen 1 of 15 runs
    later       bat           (2277,5343)  seen 1
    later       bat           (2273,5346)  seen 1
    later       bat           (2272,5342)  seen 1
    later       bat           (2277,5342)  seen 1
    later       bat           (2277,5345)  seen 1
    later       bat           (2275,5346)  seen 1
    later       bat           (2272,5346)  seen 1
    later       bat           (2273,5342)  seen 1
    later       meleer        (2275,5340)  seen 1
    later       meleer        (2275,5343)  seen 1
wave 60  (runs 15)
    first tick  blob          (2279,5353)  seen 3 of 15 runs
    first tick  blob          (2272,5330)  seen 3 of 15 runs
    first tick  blob          (2273,5341)  seen 3 of 15 runs
    first tick  blob          (2262,5335)  seen 3 of 15 runs
    first tick  blob          (2280,5333)  seen 2 of 15 runs
    first tick  blob          (2258,5353)  seen 1 of 15 runs
    first tick  mager         (2280,5333)  seen 6 of 15 runs
    first tick  mager         (2280,5346)  seen 3 of 15 runs
    first tick  mager         (2279,5353)  seen 2 of 15 runs
    first tick  mager         (2258,5330)  seen 1 of 15 runs
    first tick  mager         (2258,5353)  seen 1 of 15 runs
    first tick  mager         (2262,5335)  seen 1 of 15 runs
    first tick  mager         (2272,5330)  seen 1 of 15 runs
    first tick  meleer        (2279,5353)  seen 5 of 15 runs
    first tick  meleer        (2260,5347)  seen 3 of 15 runs
    first tick  meleer        (2258,5330)  seen 2 of 15 runs
    first tick  meleer        (2280,5346)  seen 2 of 15 runs
    first tick  meleer        (2262,5335)  seen 1 of 15 runs
    first tick  meleer        (2273,5341)  seen 1 of 15 runs
    first tick  meleer        (2280,5333)  seen 1 of 15 runs
    first tick  nibbler       (2266,5345)  seen 7 of 15 runs
    first tick  nibbler       (2265,5345)  seen 6 of 15 runs
    first tick  nibbler       (2267,5347)  seen 6 of 15 runs
    first tick  nibbler       (2265,5346)  seen 5 of 15 runs
    first tick  nibbler       (2266,5347)  seen 5 of 15 runs
    first tick  nibbler       (2267,5345)  seen 5 of 15 runs
    first tick  nibbler       (2265,5347)  seen 5 of 15 runs
    first tick  nibbler       (2266,5346)  seen 4 of 15 runs
    first tick  nibbler       (2267,5346)  seen 2 of 15 runs
    first tick  pillar        (2274,5351)  seen 15 of 15 runs
    first tick  pillar        (2257,5349)  seen 14 of 15 runs
    first tick  pillar        (2267,5335)  seen 14 of 15 runs
    first tick  ranger        (2279,5353)  seen 3 of 15 runs
    first tick  ranger        (2272,5330)  seen 2 of 15 runs
    first tick  ranger        (2280,5346)  seen 2 of 15 runs
    first tick  ranger        (2273,5341)  seen 2 of 15 runs
    first tick  ranger        (2260,5347)  seen 2 of 15 runs
    first tick  ranger        (2258,5353)  seen 1 of 15 runs
    first tick  ranger        (2280,5333)  seen 1 of 15 runs
    first tick  ranger        (2258,5330)  seen 1 of 15 runs
    first tick  ranger        (2262,5335)  seen 1 of 15 runs
    later       bloblet_mage  (2274,5350)  seen 5
    later       bloblet_mage  (2278,5350)  seen 2
    later       bloblet_mage  (2279,5355)  seen 1
    later       bloblet_mage  (2280,5355)  seen 1
    later       bloblet_mage  (2275,5350)  seen 1
    later       bloblet_mage  (2276,5345)  seen 1
    later       bloblet_mage  (2277,5347)  seen 1
    later       bloblet_mage  (2264,5354)  seen 1
    later       bloblet_mage  (2275,5348)  seen 1
    later       bloblet_mage  (2279,5354)  seen 1
    later       bloblet_melee (2273,5349)  seen 5
    later       bloblet_melee (2277,5354)  seen 2
wave 61  (runs 15)
    first tick  bat           (2280,5346)  seen 4 of 15 runs
    first tick  bat           (2262,5335)  seen 3 of 15 runs
    first tick  bat           (2272,5330)  seen 3 of 15 runs
    first tick  bat           (2273,5341)  seen 1 of 15 runs
    first tick  bat           (2258,5330)  seen 1 of 15 runs
    first tick  bat           (2260,5347)  seen 1 of 15 runs
    first tick  bat           (2279,5353)  seen 1 of 15 runs
    first tick  bat           (2280,5333)  seen 1 of 15 runs
    first tick  blob          (2258,5353)  seen 4 of 15 runs
    first tick  blob          (2260,5347)  seen 3 of 15 runs
    first tick  blob          (2280,5333)  seen 2 of 15 runs
    first tick  blob          (2262,5335)  seen 2 of 15 runs
    first tick  blob          (2280,5346)  seen 1 of 15 runs
    first tick  blob          (2272,5330)  seen 1 of 15 runs
    first tick  blob          (2273,5341)  seen 1 of 15 runs
    first tick  blob          (2279,5353)  seen 1 of 15 runs
    first tick  mager         (2279,5353)  seen 3 of 15 runs
    first tick  mager         (2273,5341)  seen 3 of 15 runs
    first tick  mager         (2260,5347)  seen 2 of 15 runs
    first tick  mager         (2262,5335)  seen 2 of 15 runs
    first tick  mager         (2272,5330)  seen 2 of 15 runs
    first tick  mager         (2280,5346)  seen 1 of 15 runs
    first tick  mager         (2280,5333)  seen 1 of 15 runs
    first tick  mager         (2258,5330)  seen 1 of 15 runs
    first tick  meleer        (2272,5330)  seen 3 of 15 runs
    first tick  meleer        (2273,5341)  seen 2 of 15 runs
    first tick  meleer        (2279,5353)  seen 2 of 15 runs
    first tick  meleer        (2280,5346)  seen 2 of 15 runs
    first tick  meleer        (2258,5330)  seen 2 of 15 runs
    first tick  meleer        (2260,5347)  seen 2 of 15 runs
    first tick  meleer        (2280,5333)  seen 1 of 15 runs
    first tick  meleer        (2258,5353)  seen 1 of 15 runs
    first tick  nibbler       (2266,5345)  seen 7 of 15 runs
    first tick  nibbler       (2267,5347)  seen 6 of 15 runs
    first tick  nibbler       (2265,5346)  seen 6 of 15 runs
    first tick  nibbler       (2265,5347)  seen 6 of 15 runs
    first tick  nibbler       (2266,5346)  seen 5 of 15 runs
    first tick  nibbler       (2267,5346)  seen 4 of 15 runs
    first tick  nibbler       (2267,5345)  seen 4 of 15 runs
    first tick  nibbler       (2265,5345)  seen 4 of 15 runs
    first tick  nibbler       (2266,5347)  seen 3 of 15 runs
    first tick  pillar        (2274,5351)  seen 15 of 15 runs
    first tick  pillar        (2257,5349)  seen 14 of 15 runs
    first tick  pillar        (2267,5335)  seen 13 of 15 runs
    first tick  ranger        (2273,5341)  seen 4 of 15 runs
    first tick  ranger        (2258,5330)  seen 2 of 15 runs
    first tick  ranger        (2280,5333)  seen 2 of 15 runs
    first tick  ranger        (2262,5335)  seen 2 of 15 runs
    first tick  ranger        (2258,5353)  seen 2 of 15 runs
    first tick  ranger        (2280,5346)  seen 1 of 15 runs
    first tick  ranger        (2260,5347)  seen 1 of 15 runs
    first tick  ranger        (2279,5353)  seen 1 of 15 runs
    later       bat           (2275,5340)  seen 1
    later       bat           (2275,5345)  seen 1
    later       bat           (2273,5341)  seen 1
    later       bat           (2273,5347)  seen 1
    later       blob          (2275,5341)  seen 1
    later       bloblet_mage  (2274,5350)  seen 3
    later       bloblet_mage  (2273,5355)  seen 2
    later       bloblet_mage  (2278,5352)  seen 1
    later       bloblet_mage  (2274,5348)  seen 1
    later       bloblet_mage  (2270,5344)  seen 1
    later       bloblet_mage  (2273,5352)  seen 1
    later       bloblet_mage  (2262,5349)  seen 1
wave 62  (runs 15)
    first tick  bat           (2260,5347)  seen 6 of 15 runs
    first tick  bat           (2280,5333)  seen 5 of 15 runs
    first tick  bat           (2280,5346)  seen 4 of 15 runs
    first tick  bat           (2279,5353)  seen 4 of 15 runs
    first tick  bat           (2272,5330)  seen 4 of 15 runs
    first tick  bat           (2258,5330)  seen 2 of 15 runs
    first tick  bat           (2258,5353)  seen 2 of 15 runs
    first tick  bat           (2262,5335)  seen 2 of 15 runs
    first tick  bat           (2273,5341)  seen 1 of 15 runs
    first tick  blob          (2258,5353)  seen 3 of 15 runs
    first tick  blob          (2280,5346)  seen 2 of 15 runs
    first tick  blob          (2272,5330)  seen 2 of 15 runs
    first tick  blob          (2260,5347)  seen 2 of 15 runs
    first tick  blob          (2258,5330)  seen 2 of 15 runs
    first tick  blob          (2279,5353)  seen 1 of 15 runs
    first tick  blob          (2262,5335)  seen 1 of 15 runs
    first tick  blob          (2273,5341)  seen 1 of 15 runs
    first tick  blob          (2280,5333)  seen 1 of 15 runs
    first tick  mager         (2273,5341)  seen 2 of 15 runs
    first tick  mager         (2280,5333)  seen 2 of 15 runs
    first tick  mager         (2262,5335)  seen 2 of 15 runs
    first tick  mager         (2280,5346)  seen 2 of 15 runs
    first tick  mager         (2258,5353)  seen 2 of 15 runs
    first tick  mager         (2272,5330)  seen 2 of 15 runs
    first tick  mager         (2258,5330)  seen 1 of 15 runs
    first tick  mager         (2279,5353)  seen 1 of 15 runs
    first tick  mager         (2260,5347)  seen 1 of 15 runs
    first tick  meleer        (2262,5335)  seen 3 of 15 runs
    first tick  meleer        (2258,5330)  seen 3 of 15 runs
    first tick  meleer        (2279,5353)  seen 3 of 15 runs
    first tick  meleer        (2272,5330)  seen 2 of 15 runs
    first tick  meleer        (2280,5346)  seen 2 of 15 runs
    first tick  meleer        (2280,5333)  seen 1 of 15 runs
    first tick  meleer        (2258,5353)  seen 1 of 15 runs
    first tick  nibbler       (2265,5347)  seen 9 of 15 runs
    first tick  nibbler       (2265,5345)  seen 9 of 15 runs
    first tick  nibbler       (2267,5347)  seen 7 of 15 runs
    first tick  nibbler       (2266,5346)  seen 5 of 15 runs
    first tick  nibbler       (2265,5346)  seen 5 of 15 runs
    first tick  nibbler       (2267,5346)  seen 3 of 15 runs
    first tick  nibbler       (2266,5345)  seen 3 of 15 runs
    first tick  nibbler       (2266,5347)  seen 2 of 15 runs
    first tick  nibbler       (2267,5345)  seen 2 of 15 runs
    first tick  pillar        (2274,5351)  seen 15 of 15 runs
    first tick  pillar        (2257,5349)  seen 14 of 15 runs
    first tick  pillar        (2267,5335)  seen 12 of 15 runs
    first tick  ranger        (2273,5341)  seen 5 of 15 runs
    first tick  ranger        (2280,5346)  seen 3 of 15 runs
    first tick  ranger        (2258,5330)  seen 3 of 15 runs
    first tick  ranger        (2280,5333)  seen 2 of 15 runs
    first tick  ranger        (2279,5353)  seen 1 of 15 runs
    first tick  ranger        (2272,5330)  seen 1 of 15 runs
    later       bat           (2273,5346)  seen 1
    later       bat           (2275,5342)  seen 1
    later       bat           (2272,5347)  seen 1
    later       blob          (2272,5343)  seen 1
    later       blob          (2276,5341)  seen 1
    later       bloblet_mage  (2274,5350)  seen 3
    later       bloblet_mage  (2273,5355)  seen 2
    later       bloblet_mage  (2279,5353)  seen 2
    later       bloblet_mage  (2281,5355)  seen 1
    later       bloblet_mage  (2260,5355)  seen 1
    later       bloblet_mage  (2275,5350)  seen 1
    later       bloblet_mage  (2279,5352)  seen 1
wave 63  (runs 15)
    first tick  blob          (2262,5335)  seen 4 of 15 runs
    first tick  blob          (2280,5333)  seen 4 of 15 runs
    first tick  blob          (2273,5341)  seen 4 of 15 runs
    first tick  blob          (2279,5353)  seen 4 of 15 runs
    first tick  blob          (2258,5330)  seen 4 of 15 runs
    first tick  blob          (2258,5353)  seen 3 of 15 runs
    first tick  blob          (2272,5330)  seen 3 of 15 runs
    first tick  blob          (2260,5347)  seen 2 of 15 runs
    first tick  blob          (2280,5346)  seen 2 of 15 runs
    first tick  mager         (2258,5353)  seen 4 of 15 runs
    first tick  mager         (2279,5353)  seen 4 of 15 runs
    first tick  mager         (2280,5333)  seen 3 of 15 runs
    first tick  mager         (2260,5347)  seen 2 of 15 runs
    first tick  mager         (2273,5341)  seen 1 of 15 runs
    first tick  mager         (2272,5330)  seen 1 of 15 runs
    first tick  meleer        (2262,5335)  seen 4 of 15 runs
    first tick  meleer        (2280,5346)  seen 2 of 15 runs
    first tick  meleer        (2272,5330)  seen 2 of 15 runs
    first tick  meleer        (2260,5347)  seen 2 of 15 runs
    first tick  meleer        (2273,5341)  seen 2 of 15 runs
    first tick  meleer        (2258,5330)  seen 1 of 15 runs
    first tick  meleer        (2258,5353)  seen 1 of 15 runs
    first tick  meleer        (2280,5333)  seen 1 of 15 runs
    first tick  nibbler       (2267,5345)  seen 7 of 15 runs
    first tick  nibbler       (2266,5345)  seen 6 of 15 runs
    first tick  nibbler       (2267,5347)  seen 6 of 15 runs
    first tick  nibbler       (2267,5346)  seen 6 of 15 runs
    first tick  nibbler       (2265,5345)  seen 6 of 15 runs
    first tick  nibbler       (2266,5347)  seen 5 of 15 runs
    first tick  nibbler       (2265,5346)  seen 3 of 15 runs
    first tick  nibbler       (2266,5346)  seen 3 of 15 runs
    first tick  nibbler       (2265,5347)  seen 3 of 15 runs
    first tick  pillar        (2274,5351)  seen 14 of 15 runs
    first tick  pillar        (2257,5349)  seen 13 of 15 runs
    first tick  pillar        (2267,5335)  seen 11 of 15 runs
    first tick  pillar        (2265,5357)  seen 1 of 15 runs
    first tick  pillar        (2282,5359)  seen 1 of 15 runs
    first tick  ranger        (2272,5330)  seen 4 of 15 runs
    first tick  ranger        (2280,5333)  seen 4 of 15 runs
    first tick  ranger        (2258,5330)  seen 3 of 15 runs
    first tick  ranger        (2280,5346)  seen 2 of 15 runs
    first tick  ranger        (2258,5353)  seen 1 of 15 runs
    first tick  ranger        (2260,5347)  seen 1 of 15 runs
    later       blob          (2276,5340)  seen 1
    later       blob          (2275,5342)  seen 1
    later       blob          (2273,5340)  seen 1
    later       bloblet_mage  (2274,5350)  seen 8
    later       bloblet_mage  (2275,5350)  seen 5
    later       bloblet_mage  (2281,5355)  seen 3
    later       bloblet_mage  (2265,5352)  seen 2
    later       bloblet_mage  (2273,5351)  seen 2
    later       bloblet_mage  (2279,5355)  seen 2
    later       bloblet_mage  (2275,5347)  seen 1
    later       bloblet_mage  (2279,5353)  seen 1
    later       bloblet_mage  (2279,5352)  seen 1
wave 64  (runs 15)
    first tick  mager         (2272,5330)  seen 4 of 15 runs
    first tick  mager         (2280,5333)  seen 3 of 15 runs
    first tick  mager         (2279,5353)  seen 3 of 15 runs
    first tick  mager         (2273,5341)  seen 2 of 15 runs
    first tick  mager         (2262,5335)  seen 1 of 15 runs
    first tick  mager         (2260,5347)  seen 1 of 15 runs
    first tick  mager         (2258,5353)  seen 1 of 15 runs
    first tick  meleer        (2280,5346)  seen 7 of 15 runs
    first tick  meleer        (2262,5335)  seen 5 of 15 runs
    first tick  meleer        (2273,5341)  seen 4 of 15 runs
    first tick  meleer        (2279,5353)  seen 4 of 15 runs
    first tick  meleer        (2280,5333)  seen 4 of 15 runs
    first tick  meleer        (2258,5353)  seen 3 of 15 runs
    first tick  meleer        (2258,5330)  seen 2 of 15 runs
    first tick  meleer        (2272,5330)  seen 1 of 15 runs
    first tick  nibbler       (2266,5347)  seen 8 of 15 runs
    first tick  nibbler       (2267,5345)  seen 6 of 15 runs
    first tick  nibbler       (2267,5346)  seen 5 of 15 runs
    first tick  nibbler       (2265,5346)  seen 5 of 15 runs
    first tick  nibbler       (2266,5346)  seen 5 of 15 runs
    first tick  nibbler       (2265,5345)  seen 4 of 15 runs
    first tick  nibbler       (2266,5345)  seen 4 of 15 runs
    first tick  nibbler       (2265,5347)  seen 4 of 15 runs
    first tick  nibbler       (2267,5347)  seen 4 of 15 runs
    first tick  pillar        (2274,5351)  seen 15 of 15 runs
    first tick  pillar        (2257,5349)  seen 13 of 15 runs
    first tick  pillar        (2267,5335)  seen 9 of 15 runs
    first tick  ranger        (2272,5330)  seen 3 of 15 runs
    first tick  ranger        (2280,5333)  seen 3 of 15 runs
    first tick  ranger        (2260,5347)  seen 2 of 15 runs
    first tick  ranger        (2279,5353)  seen 2 of 15 runs
    first tick  ranger        (2258,5330)  seen 2 of 15 runs
    first tick  ranger        (2258,5353)  seen 2 of 15 runs
    first tick  ranger        (2262,5335)  seen 1 of 15 runs
    later       meleer        (2273,5340)  seen 1
    later       meleer        (2272,5344)  seen 1
    later       meleer        (2277,5341)  seen 1
    later       meleer        (2274,5346)  seen 1
    later       meleer        (2277,5346)  seen 1
    later       meleer        (2273,5341)  seen 1
wave 65  (runs 15)
    first tick  mager         (2262,5335)  seen 4 of 15 runs
    first tick  mager         (2280,5346)  seen 3 of 15 runs
    first tick  mager         (2258,5353)  seen 2 of 15 runs
    first tick  mager         (2260,5347)  seen 2 of 15 runs
    first tick  mager         (2272,5330)  seen 1 of 15 runs
    first tick  mager         (2280,5333)  seen 1 of 15 runs
    first tick  mager         (2273,5341)  seen 1 of 15 runs
    first tick  mager         (2258,5330)  seen 1 of 15 runs
    first tick  nibbler       (2266,5346)  seen 8 of 15 runs
    first tick  nibbler       (2265,5345)  seen 8 of 15 runs
    first tick  nibbler       (2267,5346)  seen 5 of 15 runs
    first tick  nibbler       (2265,5346)  seen 5 of 15 runs
    first tick  nibbler       (2267,5345)  seen 4 of 15 runs
    first tick  nibbler       (2266,5347)  seen 4 of 15 runs
    first tick  nibbler       (2265,5347)  seen 4 of 15 runs
    first tick  nibbler       (2266,5345)  seen 4 of 15 runs
    first tick  nibbler       (2267,5347)  seen 3 of 15 runs
    first tick  pillar        (2274,5351)  seen 15 of 15 runs
    first tick  pillar        (2257,5349)  seen 13 of 15 runs
    first tick  pillar        (2267,5335)  seen 6 of 15 runs
    first tick  ranger        (2280,5346)  seen 6 of 15 runs
    first tick  ranger        (2262,5335)  seen 5 of 15 runs
    first tick  ranger        (2280,5333)  seen 5 of 15 runs
    first tick  ranger        (2258,5330)  seen 4 of 15 runs
    first tick  ranger        (2258,5353)  seen 3 of 15 runs
    first tick  ranger        (2272,5330)  seen 2 of 15 runs
    first tick  ranger        (2260,5347)  seen 2 of 15 runs
    first tick  ranger        (2273,5341)  seen 2 of 15 runs
    first tick  ranger        (2279,5353)  seen 1 of 15 runs
    later       ranger        (2276,5343)  seen 1
wave 66  (runs 14)
    first tick  mager         (2279,5353)  seen 7 of 14 runs
    first tick  mager         (2280,5333)  seen 4 of 14 runs
    first tick  mager         (2280,5346)  seen 3 of 14 runs
    first tick  mager         (2272,5330)  seen 3 of 14 runs
    first tick  mager         (2262,5335)  seen 3 of 14 runs
    first tick  mager         (2260,5347)  seen 2 of 14 runs
    first tick  mager         (2273,5341)  seen 2 of 14 runs
    first tick  mager         (2258,5330)  seen 2 of 14 runs
    first tick  mager         (2258,5353)  seen 2 of 14 runs
    first tick  nibbler       (2266,5346)  seen 6 of 14 runs
    first tick  nibbler       (2267,5346)  seen 6 of 14 runs
    first tick  nibbler       (2267,5345)  seen 6 of 14 runs
    first tick  nibbler       (2266,5347)  seen 6 of 14 runs
    first tick  nibbler       (2266,5345)  seen 5 of 14 runs
    first tick  nibbler       (2265,5345)  seen 4 of 14 runs
    first tick  nibbler       (2265,5346)  seen 4 of 14 runs
    first tick  nibbler       (2267,5347)  seen 3 of 14 runs
    first tick  nibbler       (2265,5347)  seen 2 of 14 runs
    first tick  pillar        (2274,5351)  seen 13 of 14 runs
    first tick  pillar        (2257,5349)  seen 9 of 14 runs
    first tick  pillar        (2267,5335)  seen 4 of 14 runs
    first tick  pillar        (2275,5343)  seen 1 of 14 runs
    first tick  pillar        (2282,5359)  seen 1 of 14 runs
    first tick  pillar        (2265,5357)  seen 1 of 14 runs
    later       mager         (2275,5347)  seen 1
    later       mager         (2272,5345)  seen 1
    later       mager         (2274,5344)  seen 1
wave 67  (runs 14)
    first tick  jad           (2265,5347)  seen 14 of 14 runs
    later       jad_healer    (2265,5355)  seen 6
    later       jad_healer    (2263,5348)  seen 4
    later       jad_healer    (2262,5347)  seen 3
    later       jad_healer    (2263,5346)  seen 2
    later       jad_healer    (2264,5345)  seen 2
    later       jad_healer    (2262,5349)  seen 2
    later       jad_healer    (2265,5343)  seen 2
    later       jad_healer    (2267,5355)  seen 2
    later       jad_healer    (2264,5354)  seen 2
    later       jad_healer    (2264,5355)  seen 2
    later       jad_healer    (2269,5355)  seen 2
    later       jad_healer    (2262,5350)  seen 2
wave 68  (runs 14)
    first tick  jad           (2268,5335)  seen 14 of 14 runs
    first tick  jad           (2265,5347)  seen 14 of 14 runs
    first tick  jad           (2274,5346)  seen 14 of 14 runs
    later       jad_healer    (2265,5335)  seen 3
    later       jad_healer    (2268,5331)  seen 3
    later       jad_healer    (2274,5343)  seen 3
    later       jad_healer    (2270,5343)  seen 3
    later       jad_healer    (2267,5331)  seen 2
    later       jad_healer    (2262,5354)  seen 2
    later       jad_healer    (2266,5337)  seen 2
    later       jad_healer    (2272,5333)  seen 2
    later       jad_healer    (2270,5346)  seen 2
    later       jad_healer    (2268,5342)  seen 2
    later       jad_healer    (2266,5332)  seen 2
    later       jad_healer    (2261,5347)  seen 2
wave 69  (runs 13)
    first tick  zuk           (2268,5364)  seen 13 of 13 runs
    first tick  zuk_shield    (2270,5360)  seen 13 of 13 runs
    later       zuk_healer    (2262,5363)  seen 13
    later       zuk_healer    (2266,5363)  seen 13
    later       zuk_healer    (2276,5363)  seen 13
    later       zuk_healer    (2280,5363)  seen 13
    later       zuk_jad       (2270,5347)  seen 13
    later       zuk_jad_healer (2273,5352)  seen 5
    later       zuk_jad_healer (2271,5354)  seen 4
    later       zuk_jad_healer (2270,5352)  seen 4
    later       zuk_jad_healer (2272,5354)  seen 3
    later       zuk_jad_healer (2270,5353)  seen 3
    later       zuk_jad_healer (2270,5354)  seen 3
    later       zuk_jad_healer (2271,5352)  seen 2

## Attack gaps per npc type and attack
Event: NPC_ATTACK(10), anchored to the npc's animation id. Gap = ticks between
consecutive NPC_ATTACK events of the SAME npc (room id) in one wave. A gap that is a
multiple of the cadence can be an attack the recorder did not see (an animation that
restarted without changing), a pause (freeze, dig) or a retarget.

bat            bat_auto         n=1872  3:1451  4:82  5:23  6:186  7:14  8:10  9:9  10:6  11:6  12:5  13:4  14:6  15:3  16:4  17:3  18:4  19:3  20:5  21:3  22:1  23:2  24:2  25:3  26:1  27:2  30:2  32:1  33:1  34:1  37:3  38:1  40:1  42:1  44:3  46:1  50:2  51:1  52:2  54:2  55:1  57:1  60:1  61:1  63:1  64:1  70:1  76:1  82:1  83:1  141:1  187:1
blob           blob_ranged      n=1263  6:1039  7:10  8:10  9:18  10:7  11:8  12:34  13:7  14:7  15:6  16:5  17:6  18:8  19:5  20:1  21:1  22:5  23:4  24:2  25:3  27:2  28:6  29:9  30:3  31:1  33:1  34:4  35:4  36:1  37:2  38:2  39:1  40:3  41:1  42:1  43:1  44:4  46:1  47:1  48:1  49:3  51:1  52:2  54:1  55:2  56:1  57:2  58:2  61:2  62:1  67:1  76:1  80:1  81:1  82:1  93:1  94:1  107:1  114:1  118:1  136:1
blob           blob_mage        n=1115  6:802  7:5  8:13  9:19  10:21  11:13  12:41  13:14  14:15  15:4  16:6  17:8  18:10  19:4  20:8  21:11  22:4  23:5  24:4  25:8  26:3  27:2  28:4  29:1  30:5  31:2  32:2  33:1  34:2  35:2  36:3  37:4  38:2  39:4  40:4  41:3  42:3  43:2  45:2  47:3  48:1  49:2  50:1  51:4  52:2  53:3  54:2  55:3  57:2  59:3  60:1  62:1  63:2  65:1  70:1  71:1  72:1  74:1  75:1  76:1  84:2  88:2  89:1  103:1  109:1  125:1  140:2  153:1  280:1
blob           blob_melee       n=39  3:23  4:1  6:3  8:1  9:9  20:1  34:1
bloblet_mage   bloblet_mage     n=527  4:454  5:2  7:2  8:54  9:1  10:1  11:3  13:1  17:1  19:1  20:1  23:3  26:1  45:1  51:1
bloblet_melee  bloblet_melee    n=285  4:233  5:15  6:3  7:2  8:20  9:3  10:3  11:1  12:1  13:1  17:1  27:1  56:1
bloblet_range  bloblet_ranged   n=473  4:419  6:2  8:41  11:3  12:3  13:1  15:1  18:1  20:1  45:1
jad            jad_ranged       n=426  4:12  8:58  9:356
jad            jad_mage         n=438  4:14  8:53  9:371
jad            jad_melee        n=43  4:14  8:14  9:14  11:1
jad_healer     jad_healer_auto  n=705  4:690  5:6  6:2  7:2  8:1  13:1  28:1  43:2
mager          mager_auto       n=5274  4:4786  5:28  6:17  7:15  8:144  9:19  10:24  11:17  12:18  13:8  14:13  15:11  16:12  17:7  18:12  19:2  20:9  21:5  22:10  23:6  24:7  25:9  26:7  27:3  28:3  29:6  30:6  31:4  32:1  33:3  34:3  35:4  36:4  37:3  38:1  39:1  40:3  41:4  42:1  43:1  44:2  45:2  46:1  47:2  48:4  49:1  50:2  51:3  53:1  54:1  55:2  56:1  58:1  60:1  62:2  66:2  67:1  69:1  95:1  118:1  123:1  129:1  135:1  137:1  152:1
mager          mager_melee      n=24  4:18  5:1  6:2  8:2  10:1
mager          mager_resurrect  n=184  4:163  5:2  6:1  8:3  14:1  18:1  19:1  22:1  24:2  25:1  29:1  30:1  36:1  37:2  62:1  78:1  81:1
meleer         meleer_auto      n=1430  4:1196  5:56  6:21  7:17  8:11  9:12  10:8  11:4  12:55  13:7  14:5  15:7  16:3  17:2  18:2  19:2  20:1  22:3  24:4  28:1  29:1  30:2  31:1  32:2  36:4  40:2  53:1
meleer         meleer_dig       n=25  20:1  23:1  24:1  25:1  28:1  30:1  31:1  32:2  33:1  35:1  38:2  41:1  42:1  45:1  47:1  49:2  51:1  54:1  56:1  61:1  62:1  66:1
ranger         ranger_auto      n=5179  4:4671  5:35  6:30  7:27  8:15  9:27  10:28  11:23  12:19  13:12  14:15  15:14  16:7  17:8  18:11  19:16  20:8  21:11  22:6  23:10  24:3  25:3  26:4  27:5  28:10  29:3  30:5  31:4  32:6  33:7  34:6  35:4  36:2  37:3  38:3  39:4  40:3  41:7  42:2  43:1  44:5  45:3  46:6  47:4  48:3  49:2  50:9  51:1  52:4  53:4  54:4  55:2  56:1  57:6  58:4  59:2  60:3  61:2  62:2  63:1  64:2  65:1  66:1  67:4  69:1  74:1  75:1  76:1  77:2  78:1  81:2  83:1  89:1  93:1  94:1  105:1  108:1  109:1  125:1  141:1  146:1  162:1  174:1  194:1  200:1  261:1  705:1
ranger         ranger_melee     n=122  4:115  5:3  6:1  25:1  47:1  89:1
zuk            zuk_auto         n=666  7:283  8:1  9:1  10:381
zuk_jad        jad_ranged       n=75  8:75
zuk_jad        jad_mage         n=66  8:66
zuk_jad_healer jad_healer_auto  n=96  4:61  5:2  6:8  7:2  8:2  9:1  11:4  12:1  13:3  14:1  16:1  17:3  25:1  32:1  33:2  34:2  35:1
zuk_mager      mager_auto       n=358  4:356  5:1  6:1
zuk_ranger     ranger_auto      n=64  4:63  6:1

## First attack after a real (post tick 0) spawn
Events: NPC_SPAWN(7) then NPC_ATTACK(10), same npc. Ticks from spawn to first attack.

bat            bat_auto         n=74  4:21  5:4  6:1  7:2  10:2  12:1  13:2  14:2  16:3  17:1  21:1  22:2  24:2  25:1  26:2  28:2  29:2  30:1  31:1  33:1  34:2  35:1  36:1  38:1  39:2  40:1  41:1  42:1  48:1  49:1  52:1  56:2  57:1  70:1  72:1  77:1  161:1
blob           blob_ranged      n=10  7:8  27:1  32:1
blob           blob_mage        n=8  7:4  23:1  38:1  44:1  121:1
bloblet_mage   bloblet_mage     n=615  3:519  4:6  5:6  6:1  7:34  8:4  9:6  10:1  11:5  12:6  13:3  14:6  15:3  16:2  18:3  20:1  21:1  30:3  36:1  38:1  51:1  52:1  70:1
bloblet_melee  bloblet_melee    n=478  3:388  4:15  5:11  6:2  7:23  8:11  9:4  10:7  11:1  13:2  14:2  15:4  17:1  18:1  21:1  25:1  28:1  40:1  49:1  93:1
bloblet_range  bloblet_ranged   n=677  3:605  4:3  5:3  6:5  7:45  8:1  9:2  11:1  15:1  16:1  17:1  23:2  24:1  25:1  26:1  28:1  29:1  36:1  84:1
jad_healer     jad_healer_auto  n=105  7:6  8:7  9:8  10:5  11:10  12:7  13:3  14:5  15:1  16:5  17:6  18:5  19:2  20:1  21:1  22:1  24:1  28:1  29:1  30:1  32:1  35:1  36:1  37:3  38:1  39:2  40:4  43:2  46:1  53:4  54:1  55:1  69:2  70:1  72:1  73:1  135:1
mager          mager_auto       n=3  4:3
meleer         meleer_auto      n=18  7:3  9:2  11:1  14:1  15:1  17:2  21:1  22:1  23:1  31:1  32:2  36:1  39:1
meleer         meleer_dig       n=15  49:15
ranger         ranger_auto      n=7  4:2  11:1  22:1  26:1  49:1  103:1
zuk_jad        jad_ranged       n=7  6:2  7:5
zuk_jad        jad_mage         n=6  6:1  7:2  8:3
zuk_jad_healer jad_healer_auto  n=29  12:1  14:1  15:1  17:3  18:3  19:1  20:2  21:2  23:1  24:1  25:2  26:2  27:2  28:1  36:2  38:1  41:1  47:1  50:1
zuk_mager      mager_auto       n=12  6:11  8:1
zuk_ranger     ranger_auto      n=12  8:12

## Despawn after a real spawn
Event: NPC_DEATH(9), which the plugin emits on NpcDespawned, NOT when hitpoints reach 0.

bat            lifetime n=95  8:1  9:3  10:2  11:5  12:5  13:5  14:4  15:7  16:1  18:1  19:1  23:4  25:3  26:1  27:1  28:1  30:2  31:1  33:2  34:3  38:1  39:3  40:1  41:1  42:4  44:1  45:3  46:2  47:1  50:3  53:1  54:2  57:1  58:1  59:1  60:3  62:1  64:1  66:1  68:1  74:1  75:1  76:1  81:2  85:1  97:1  102:1  231:1
blob           lifetime n=22  7:1  12:1  14:1  17:1  31:1  40:1  42:1  52:1  77:1  79:1  87:1  88:1  92:1  110:1  116:1  117:1  122:2  133:1  198:1  278:1  921:1
bloblet_mage   lifetime n=741  5:2  6:96  7:114  8:55  9:62  10:41  11:57  12:45  13:30  14:27  15:21  16:33  17:19  18:16  19:12  20:14  21:9  22:7  23:8  24:10  25:4  26:12  27:3  28:3  29:5  30:6  32:1  33:2  34:1  35:2  36:1  37:1  38:3  39:3  41:3  42:1  44:1  47:1  49:1  50:1  52:1  66:1  72:1  75:1  86:1  110:1  195:1  568:1
bloblet_melee  lifetime n=741  6:9  7:64  8:125  9:70  10:71  11:52  12:64  13:42  14:30  15:33  16:21  17:26  18:18  19:23  20:8  21:12  22:17  23:6  24:11  25:9  26:1  27:3  28:4  29:3  30:1  32:1  33:1  34:1  35:2  36:1  40:1  44:2  47:2  56:1  57:1  61:1  62:1  100:1  120:1  580:1
bloblet_range  lifetime n=741  5:16  6:82  7:143  8:68  9:58  10:48  11:49  12:64  13:32  14:24  15:20  16:17  17:19  18:15  19:11  20:11  21:9  22:10  23:4  24:2  25:2  26:1  27:2  28:4  29:3  30:2  31:1  32:2  33:3  34:2  35:1  38:2  39:1  40:2  41:1  42:1  43:2  48:1  55:1  67:1  74:1  76:1  91:1  103:1
jad_healer     lifetime n=196  28:1  31:3  32:3  34:3  36:3  37:16  38:3  39:5  40:3  41:3  42:8  44:8  46:1  47:9  48:9  49:6  52:9  55:8  56:5  57:9  58:3  63:6  66:5  67:11  68:3  71:5  73:11  78:5  80:3  90:3  93:4  98:5  104:3  105:3  109:3  135:1  159:2  205:2  260:1  264:2
mager          lifetime n=3  24:1  25:1  28:1
meleer         lifetime n=62  12:1  14:1  15:4  16:1  17:1  18:1  19:2  20:4  21:2  22:1  24:2  25:4  26:2  27:1  31:1  32:1  33:1  34:1  35:1  37:1  38:1  39:1  42:3  44:1  48:1  49:2  50:1  55:1  56:1  67:1  69:2  72:2  73:1  78:1  79:1  81:1  83:1  88:2  91:2  95:1  99:1  108:1
ranger         lifetime n=7  36:1  45:1  64:1  103:1  125:1  188:1  322:1
zuk_healer     lifetime n=44  19:2  28:2  32:1  33:1  35:1  38:1  39:3  40:2  42:1  47:1  48:2  50:1  52:3  53:1  54:3  60:1  61:1  62:2  65:1  69:1  70:1  71:1  75:4  76:2  78:1  102:1  117:1  134:1  151:1
zuk_jad        lifetime n=11  62:1  67:1  80:1  86:1  94:1  99:2  107:1  118:1  132:2
zuk_jad_healer lifetime n=30  37:3  39:3  47:3  50:3  57:3  64:3  74:3  82:3  87:3  90:3
zuk_mager      lifetime n=10  71:1  79:1  87:1  100:1  109:1  117:1  127:1  138:1  157:1  180:1
zuk_ranger     lifetime n=12  14:1  19:1  20:1  27:1  31:1  34:2  38:1  39:1  41:1  48:1  60:1

## Wave end per run
Events: the last NPC_SPAWN/ATTACK/DEATH tick of the wave in each run (a lower bound on the
wave's end tick; the stage-end tick is a STAGE_UPDATE the API does not serve).

wave 1  n=20 min 8    median 15   max 30
wave 2  n=20 min 11   median 21   max 41
wave 3  n=20 min 8    median 12   max 25
wave 4  n=20 min 20   median 31   max 60
wave 5  n=20 min 22   median 38   max 68
wave 6  n=20 min 26   median 45   max 89
wave 7  n=20 min 32   median 52   max 93
wave 8  n=20 min 7    median 14   max 36
wave 9  n=20 min 16   median 25   max 44
wave 10 n=20 min 17   median 30   max 56
wave 11 n=20 min 23   median 36   max 84
wave 12 n=20 min 27   median 47   max 79
wave 13 n=20 min 30   median 50   max 105
wave 14 n=20 min 28   median 60   max 148
wave 15 n=20 min 38   median 78   max 199
wave 16 n=20 min 20   median 40   max 61
wave 17 n=20 min 8    median 13   max 28
wave 18 n=20 min 17   median 31   max 51
wave 19 n=20 min 23   median 35   max 53
wave 20 n=20 min 25   median 40   max 79
wave 21 n=20 min 35   median 59   max 97
wave 22 n=20 min 41   median 56   max 141
wave 23 n=20 min 39   median 63   max 105
wave 24 n=20 min 44   median 78   max 201
wave 25 n=20 min 27   median 50   max 337
wave 26 n=20 min 28   median 49   max 183
wave 27 n=20 min 34   median 55   max 84
wave 28 n=19 min 44   median 71   max 151
wave 29 n=19 min 47   median 65   max 146
wave 30 n=19 min 48   median 75   max 169
wave 31 n=19 min 28   median 85   max 180
wave 32 n=18 min 50   median 66   max 133
wave 33 n=18 min 34   median 47   max 78
wave 34 n=18 min 7    median 11   max 30
wave 35 n=18 min 21   median 36   max 89
wave 36 n=18 min 27   median 50   max 105
wave 37 n=18 min 37   median 52   max 111
wave 38 n=18 min 45   median 70   max 125
wave 39 n=18 min 52   median 81   max 140
wave 40 n=18 min 43   median 79   max 189
wave 41 n=18 min 61   median 104  max 166
wave 42 n=18 min 36   median 67   max 103
wave 43 n=18 min 37   median 79   max 214
wave 44 n=18 min 20   median 83   max 226
wave 45 n=17 min 45   median 83   max 157
wave 46 n=17 min 55   median 105  max 201
wave 47 n=17 min 81   median 115  max 172
wave 48 n=17 min 24   median 138  max 369
wave 49 n=16 min 42   median 77   max 150
wave 50 n=16 min 49   median 67   max 264
wave 51 n=16 min 53   median 83   max 113
wave 52 n=16 min 52   median 101  max 125
wave 53 n=16 min 58   median 98   max 392
wave 54 n=16 min 60   median 116  max 226
wave 55 n=16 min 67   median 115  max 256
wave 56 n=15 min 66   median 140  max 251
wave 57 n=15 min 69   median 93   max 178
wave 58 n=15 min 66   median 108  max 218
wave 59 n=15 min 74   median 110  max 367
wave 60 n=15 min 65   median 124  max 216
wave 61 n=15 min 81   median 161  max 271
wave 62 n=15 min 85   median 184  max 1544
wave 63 n=15 min 91   median 177  max 538
wave 64 n=15 min 72   median 120  max 900
wave 65 n=15 min 27   median 102  max 233
wave 66 n=14 min 62   median 85   max 116
wave 67 n=14 min 61   median 91   max 143
wave 68 n=14 min 174  median 263  max 404
wave 69 n=13 min 252  median 510  max 622

# Per-wave record (GET /challenges/inferno/<uuid>), 20 challenges

CAUTION: a run whose plugin sent no INFERNO_WAVE_START (a logout mid-run sets `hasLogged`;
an older plugin) has every wave start RECONSTRUCTED by the server: start of wave 1 = 10 and
+6 between waves (asserted constants, `PROVENANCE.md`), so its gaps are 6 by construction.
The API has no flag for it. A run with a gap other than 6, or whose last wave end equals the
game's own `Duration`, carries real start events. Per run:

7dca5546 status=3 waves=44 last wave end 1868  game Duration 1868  gaps {6: 43}
c8d56ab9 status=3 waves=31 last wave end 1207  game Duration 1207  gaps {6: 30}
1b92d513 status=1 waves=69 last wave end 7307  game Duration 7372  gaps {6: 68}
41c5c054 status=1 waves=69 last wave end 6477  game Duration 6477  gaps {6: 67, 22: 1}
447a4094 status=1 waves=69 last wave end 5748  game Duration 5748  gaps {6: 67, 22: 1}
6214a4e9 status=1 waves=69 last wave end 5763  game Duration 5796  gaps {14: 1, 6: 67}
68174070 status=3 waves=48 last wave end 2783  game Duration 2783  gaps {6: 47}
6a865311 status=1 waves=69 last wave end 8242  game Duration 8282  gaps {6: 68}
754bc5aa status=1 waves=69 last wave end 4496  game Duration 4496  gaps {6: 67, 22: 1}
7d6a3542 status=1 waves=69 last wave end 6274  game Duration 6309  gaps {6: 68}
8a86bacb status=3 waves=69 last wave end 4091  game Duration 4091  gaps {6: 67, 22: 1}
936b38c9 status=3 waves=55 last wave end 2831  game Duration 2831  gaps {6: 54}
b1e47c9b status=1 waves=69 last wave end 7689  game Duration 7745  gaps {6: 68}
b338bcb1 status=1 waves=69 last wave end 8341  game Duration 8452  gaps {6: 68}
bf07a3ce status=3 waves=65 last wave end 3505  game Duration 3505  gaps {6: 64}
d66dfff5 status=1 waves=69 last wave end 7915  game Duration 7915  gaps {6: 67, 22: 1}
dd5b1591 status=3 waves=61 last wave end 3275  game Duration 3275  gaps {6: 60}
dda7541d status=3 waves=68 last wave end 9054  game Duration 9054  gaps {6: 67}
e018d2b6 status=1 waves=69 last wave end 4889  game Duration 4889  gaps {6: 67, 22: 1}
fdbf9985 status=1 waves=69 last wave end 8981  game Duration 8981  gaps {6: 67, 22: 1}

## Wave length in ticks (start message to end message), per wave

wave 1  n=20 min 8    median 15   max 30
wave 2  n=20 min 11   median 21   max 41
wave 3  n=20 min 8    median 12   max 25
wave 4  n=20 min 20   median 31   max 60
wave 5  n=20 min 22   median 38   max 68
wave 6  n=20 min 26   median 45   max 89
wave 7  n=20 min 32   median 52   max 93
wave 8  n=20 min 7    median 14   max 36
wave 9  n=20 min 16   median 25   max 44
wave 10 n=20 min 17   median 30   max 56
wave 11 n=20 min 23   median 36   max 84
wave 12 n=20 min 27   median 47   max 79
wave 13 n=20 min 30   median 50   max 105
wave 14 n=20 min 28   median 60   max 148
wave 15 n=20 min 38   median 78   max 199
wave 16 n=20 min 20   median 40   max 61
wave 17 n=20 min 8    median 13   max 28
wave 18 n=20 min 17   median 31   max 51
wave 19 n=20 min 23   median 35   max 53
wave 20 n=20 min 25   median 40   max 79
wave 21 n=20 min 35   median 59   max 97
wave 22 n=20 min 41   median 56   max 141
wave 23 n=20 min 39   median 63   max 105
wave 24 n=20 min 44   median 78   max 201
wave 25 n=20 min 27   median 50   max 337
wave 26 n=20 min 28   median 49   max 183
wave 27 n=20 min 34   median 55   max 84
wave 28 n=20 min 44   median 71   max 151
wave 29 n=20 min 45   median 65   max 146
wave 30 n=20 min 48   median 75   max 169
wave 31 n=20 min 28   median 85   max 180
wave 32 n=19 min 44   median 61   max 133
wave 33 n=19 min 34   median 47   max 78
wave 34 n=19 min 7    median 11   max 30
wave 35 n=19 min 21   median 35   max 89
wave 36 n=19 min 27   median 49   max 105
wave 37 n=19 min 37   median 52   max 111
wave 38 n=19 min 45   median 70   max 125
wave 39 n=19 min 52   median 81   max 140
wave 40 n=19 min 43   median 78   max 189
wave 41 n=19 min 61   median 95   max 166
wave 42 n=19 min 36   median 66   max 103
wave 43 n=19 min 37   median 69   max 214
wave 44 n=19 min 20   median 83   max 226
wave 45 n=18 min 45   median 83   max 157
wave 46 n=18 min 55   median 105  max 201
wave 47 n=18 min 73   median 115  max 172
wave 48 n=18 min 24   median 138  max 369
wave 49 n=17 min 42   median 77   max 150
wave 50 n=17 min 49   median 65   max 264
wave 51 n=17 min 53   median 82   max 113
wave 52 n=17 min 52   median 100  max 125
wave 53 n=17 min 58   median 97   max 392
wave 54 n=17 min 60   median 97   max 226
wave 55 n=17 min 67   median 102  max 256
wave 56 n=16 min 66   median 140  max 251
wave 57 n=16 min 63   median 93   max 178
wave 58 n=16 min 56   median 108  max 218
wave 59 n=16 min 72   median 110  max 367
wave 60 n=16 min 65   median 124  max 216
wave 61 n=16 min 16   median 161  max 271
wave 62 n=15 min 85   median 184  max 1544
wave 63 n=15 min 91   median 177  max 538
wave 64 n=15 min 72   median 120  max 900
wave 65 n=15 min 27   median 102  max 233
wave 66 n=14 min 62   median 85   max 116
wave 67 n=14 min 61   median 91   max 143
wave 68 n=14 min 174  median 263  max 404
wave 69 n=13 min 252  median 514  max 627

## Gap between a wave's end and the next wave's start message (ticks), all waves
(waves with ticksLost > 0 excluded; ALL runs, so runs with reconstructed starts count as 6)

n=1249  6:1241  14:1  22:7

## Spawn sets (the server's spawn index: npc id, x, y of the tick-0 batch), per wave

wave 1
    seen 5  bat@2280,5346
    seen 4  bat@2272,5330
    seen 3  bat@2258,5330
    seen 2  bat@2280,5333
    seen 2  bat@2262,5335
    seen 2  bat@2260,5347
    seen 1  bat@2279,5353
    seen 1  bat@2273,5341
wave 2
    seen 2  bat@2279,5353  bat@2280,5346
    seen 2  bat@2258,5330  bat@2262,5335
    seen 2  bat@2272,5330  bat@2273,5341
    seen 2  bat@2262,5335  bat@2280,5333
    seen 1  bat@2260,5347  bat@2280,5346
    seen 1  bat@2273,5341  bat@2279,5353
    seen 1  bat@2262,5335  bat@2279,5353
    seen 1  bat@2260,5347  bat@2262,5335
    seen 1  bat@2258,5330  bat@2258,5353
    seen 1  bat@2258,5330  bat@2272,5330
    seen 1  bat@2258,5330  bat@2273,5341
    seen 1  bat@2279,5353  bat@2280,5333
    seen 1  bat@2258,5330  bat@2280,5333
    seen 1  bat@2262,5335  bat@2272,5330
    seen 1  bat@2260,5347  bat@2280,5333
    seen 1  bat@2258,5353  bat@2279,5353
wave 4
    seen 4  blob@2258,5330
    seen 4  blob@2280,5333
    seen 3  blob@2273,5341
    seen 3  blob@2260,5347
    seen 2  blob@2280,5346
    seen 1  blob@2262,5335
    seen 1  blob@2279,5353
    seen 1  blob@2258,5353
    seen 1  blob@2272,5330
wave 5
    seen 2  bat@2260,5347  blob@2258,5353
    seen 2  bat@2260,5347  blob@2280,5346
    seen 2  bat@2258,5330  blob@2280,5333
    seen 1  bat@2258,5353  blob@2280,5333
    seen 1  bat@2258,5330  blob@2279,5353
    seen 1  bat@2272,5330  blob@2280,5346
    seen 1  bat@2260,5347  blob@2273,5341
    seen 1  bat@2280,5333  blob@2262,5335
    seen 1  bat@2280,5333  blob@2280,5346
    seen 1  bat@2258,5330  blob@2273,5341
    seen 1  bat@2280,5333  blob@2258,5330
    seen 1  bat@2280,5333  blob@2260,5347
    seen 1  bat@2258,5330  blob@2280,5346
    seen 1  bat@2272,5330  blob@2258,5353
    seen 1  bat@2272,5330  blob@2273,5341
    seen 1  bat@2273,5341  blob@2260,5347
    seen 1  bat@2262,5335  blob@2279,5353
wave 6
    seen 2  bat@2280,5333  bat@2280,5346  blob@2279,5353
    seen 1  bat@2262,5335  bat@2273,5341  blob@2258,5353
    seen 1  bat@2262,5335  bat@2280,5333  blob@2258,5353
    seen 1  bat@2279,5353  bat@2280,5333  blob@2273,5341
    seen 1  bat@2258,5330  bat@2280,5346  blob@2273,5341
    seen 1  bat@2260,5347  bat@2279,5353  blob@2262,5335
    seen 1  bat@2272,5330  bat@2279,5353  blob@2260,5347
    seen 1  bat@2272,5330  bat@2273,5341  blob@2260,5347
    seen 1  bat@2258,5330  bat@2273,5341  blob@2280,5333
    seen 1  bat@2258,5330  bat@2262,5335  blob@2280,5346
    seen 1  bat@2280,5333  bat@2280,5346  blob@2272,5330
    seen 1  bat@2273,5341  bat@2280,5333  blob@2272,5330
    seen 1  bat@2273,5341  bat@2279,5353  blob@2260,5347
    seen 1  bat@2279,5353  bat@2280,5346  blob@2272,5330
    seen 1  bat@2258,5353  bat@2272,5330  blob@2280,5346
    seen 1  bat@2260,5347  bat@2273,5341  blob@2258,5330
    seen 1  bat@2258,5353  bat@2260,5347  blob@2280,5333
    seen 1  bat@2260,5347  bat@2273,5341  blob@2279,5353
    seen 1  bat@2260,5347  bat@2280,5333  blob@2258,5330
wave 7
    seen 2  blob@2273,5341  blob@2279,5353
    seen 2  blob@2260,5347  blob@2280,5333
    seen 2  blob@2273,5341  blob@2280,5333
    seen 1  blob@2272,5330  blob@2279,5353
    seen 1  blob@2258,5330  blob@2279,5353
    seen 1  blob@2260,5347  blob@2273,5341
    seen 1  blob@2280,5333  blob@2280,5346
    seen 1  blob@2260,5347  blob@2280,5346
    seen 1  blob@2273,5341  blob@2280,5346
    seen 1  blob@2272,5330  blob@2280,5333
    seen 1  blob@2260,5347  blob@2279,5353
    seen 1  blob@2258,5353  blob@2272,5330
    seen 1  blob@2272,5330  blob@2273,5341
    seen 1  blob@2258,5330  blob@2258,5353
    seen 1  blob@2262,5335  blob@2280,5333
    seen 1  blob@2258,5330  blob@2262,5335
    seen 1  blob@2258,5353  blob@2262,5335
wave 9
    seen 5  meleer@2273,5341
    seen 4  meleer@2279,5353
    seen 2  meleer@2262,5335
    seen 2  meleer@2258,5353
    seen 2  meleer@2272,5330
    seen 2  meleer@2260,5347
    seen 2  meleer@2258,5330
    seen 1  meleer@2280,5333
wave 10
    seen 3  bat@2280,5333  meleer@2258,5353
    seen 2  bat@2260,5347  meleer@2262,5335
    seen 2  bat@2280,5333  meleer@2272,5330
    seen 1  bat@2280,5333  meleer@2273,5341
    seen 1  bat@2273,5341  meleer@2258,5330
    seen 1  bat@2279,5353  meleer@2258,5330
    seen 1  bat@2273,5341  meleer@2258,5353
    seen 1  bat@2260,5347  meleer@2280,5346
    seen 1  bat@2258,5353  meleer@2262,5335
    seen 1  bat@2273,5341  meleer@2280,5346
    seen 1  bat@2272,5330  meleer@2273,5341
    seen 1  bat@2279,5353  meleer@2280,5346
    seen 1  bat@2260,5347  meleer@2273,5341
    seen 1  bat@2262,5335  meleer@2279,5353
    seen 1  bat@2258,5330  meleer@2273,5341
    seen 1  bat@2262,5335  meleer@2280,5346
wave 11
    seen 2  bat@2258,5353  bat@2280,5346  meleer@2272,5330
    seen 1  bat@2260,5347  bat@2262,5335  meleer@2258,5330
    seen 1  bat@2260,5347  bat@2273,5341  meleer@2272,5330
    seen 1  bat@2258,5330  bat@2258,5353  meleer@2279,5353
    seen 1  bat@2272,5330  bat@2279,5353  meleer@2260,5347
    seen 1  bat@2262,5335  bat@2273,5341  meleer@2272,5330
    seen 1  bat@2279,5353  bat@2280,5333  meleer@2273,5341
    seen 1  bat@2272,5330  bat@2279,5353  meleer@2280,5333
    seen 1  bat@2272,5330  bat@2280,5333  meleer@2262,5335
    seen 1  bat@2258,5330  bat@2260,5347  meleer@2273,5341
    seen 1  bat@2260,5347  bat@2272,5330  meleer@2280,5346
    seen 1  bat@2260,5347  bat@2262,5335  meleer@2280,5346
    seen 1  bat@2272,5330  bat@2273,5341  meleer@2280,5346
    seen 1  bat@2258,5330  bat@2280,5333  meleer@2272,5330
    seen 1  bat@2258,5330  bat@2258,5353  meleer@2272,5330
    seen 1  bat@2258,5330  bat@2260,5347  meleer@2262,5335
    seen 1  bat@2280,5333  bat@2280,5346  meleer@2272,5330
    seen 1  bat@2258,5353  bat@2280,5333  meleer@2273,5341
    seen 1  bat@2272,5330  bat@2279,5353  meleer@2258,5353
wave 12
    seen 2  blob@2272,5330  meleer@2258,5330
    seen 2  blob@2272,5330  meleer@2260,5347
    seen 1  blob@2258,5353  meleer@2279,5353
    seen 1  blob@2273,5341  meleer@2258,5353
    seen 1  blob@2280,5333  meleer@2258,5330
    seen 1  blob@2279,5353  meleer@2262,5335
    seen 1  blob@2279,5353  meleer@2280,5333
    seen 1  blob@2258,5353  meleer@2258,5330
    seen 1  blob@2280,5333  meleer@2280,5346
    seen 1  blob@2260,5347  meleer@2272,5330
    seen 1  blob@2280,5333  meleer@2262,5335
    seen 1  blob@2273,5341  meleer@2280,5333
    seen 1  blob@2262,5335  meleer@2279,5353
    seen 1  blob@2279,5353  meleer@2258,5353
    seen 1  blob@2279,5353  meleer@2272,5330
    seen 1  blob@2262,5335  meleer@2258,5330
    seen 1  blob@2279,5353  meleer@2280,5346
    seen 1  blob@2272,5330  meleer@2279,5353
wave 13
    seen 2  bat@2258,5330  blob@2279,5353  meleer@2280,5346
    seen 1  bat@2258,5330  blob@2280,5333  meleer@2273,5341
    seen 1  bat@2260,5347  blob@2273,5341  meleer@2262,5335
    seen 1  bat@2260,5347  blob@2258,5330  meleer@2280,5333
    seen 1  bat@2272,5330  blob@2260,5347  meleer@2273,5341
    seen 1  bat@2258,5353  blob@2258,5330  meleer@2280,5333
    seen 1  bat@2258,5330  blob@2279,5353  meleer@2280,5333
    seen 1  bat@2258,5330  blob@2258,5353  meleer@2273,5341
    seen 1  bat@2258,5353  blob@2258,5330  meleer@2272,5330
    seen 1  bat@2273,5341  blob@2258,5330  meleer@2258,5353
    seen 1  bat@2258,5353  blob@2260,5347  meleer@2262,5335
    seen 1  bat@2279,5353  blob@2258,5330  meleer@2262,5335
    seen 1  bat@2272,5330  blob@2262,5335  meleer@2258,5353
    seen 1  bat@2279,5353  blob@2280,5346  meleer@2262,5335
    seen 1  bat@2280,5333  blob@2258,5330  meleer@2260,5347
    seen 1  bat@2260,5347  blob@2258,5330  meleer@2262,5335
    seen 1  bat@2262,5335  blob@2280,5346  meleer@2273,5341
    seen 1  bat@2258,5330  blob@2258,5353  meleer@2280,5346
    seen 1  bat@2273,5341  blob@2279,5353  meleer@2280,5333
wave 14
    seen 1  bat@2258,5330  bat@2280,5333  blob@2272,5330  meleer@2279,5353
    seen 1  bat@2258,5353  bat@2272,5330  blob@2258,5330  meleer@2262,5335
    seen 1  bat@2279,5353  bat@2280,5333  blob@2272,5330  meleer@2280,5346
    seen 1  bat@2258,5330  bat@2280,5346  blob@2258,5353  meleer@2272,5330
    seen 1  bat@2258,5330  bat@2280,5333  blob@2262,5335  meleer@2273,5341
    seen 1  bat@2258,5353  bat@2272,5330  blob@2279,5353  meleer@2280,5346
    seen 1  bat@2262,5335  bat@2272,5330  blob@2273,5341  meleer@2258,5330
    seen 1  bat@2273,5341  bat@2279,5353  blob@2272,5330  meleer@2258,5330
    seen 1  bat@2258,5330  bat@2280,5333  blob@2279,5353  meleer@2262,5335
    seen 1  bat@2258,5330  bat@2258,5353  blob@2273,5341  meleer@2272,5330
    seen 1  bat@2272,5330  bat@2280,5346  blob@2280,5333  meleer@2258,5330
    seen 1  bat@2273,5341  bat@2280,5333  blob@2279,5353  meleer@2280,5346
    seen 1  bat@2258,5330  bat@2260,5347  blob@2279,5353  meleer@2272,5330
    seen 1  bat@2258,5330  bat@2280,5333  blob@2273,5341  meleer@2279,5353
    seen 1  bat@2258,5330  bat@2272,5330  blob@2260,5347  meleer@2280,5333
    seen 1  bat@2262,5335  bat@2279,5353  blob@2272,5330  meleer@2280,5346
    seen 1  bat@2258,5330  bat@2280,5346  blob@2273,5341  meleer@2272,5330
    seen 1  bat@2258,5353  bat@2272,5330  blob@2260,5347  meleer@2280,5346
    seen 1  bat@2260,5347  bat@2280,5333  blob@2258,5330  meleer@2279,5353
    seen 1  bat@2273,5341  bat@2280,5333  blob@2280,5346  meleer@2279,5353
wave 15
    seen 1  blob@2273,5341  blob@2279,5353  meleer@2272,5330
    seen 1  blob@2272,5330  blob@2280,5346  meleer@2262,5335
    seen 1  blob@2272,5330  blob@2280,5346  meleer@2273,5341
    seen 1  blob@2258,5353  blob@2280,5333  meleer@2280,5346
    seen 1  blob@2258,5330  blob@2273,5341  meleer@2279,5353
    seen 1  blob@2258,5353  blob@2280,5333  meleer@2258,5330
    seen 1  blob@2258,5330  blob@2280,5346  meleer@2258,5353
    seen 1  blob@2258,5330  blob@2279,5353  meleer@2258,5353
    seen 1  blob@2258,5330  blob@2260,5347  meleer@2279,5353
    seen 1  blob@2258,5353  blob@2279,5353  meleer@2258,5330
    seen 1  blob@2258,5330  blob@2273,5341  meleer@2262,5335
    seen 1  blob@2273,5341  blob@2280,5333  meleer@2272,5330
    seen 1  blob@2258,5330  blob@2258,5353  meleer@2260,5347
    seen 1  blob@2260,5347  blob@2272,5330  meleer@2258,5353
    seen 1  blob@2258,5353  blob@2260,5347  meleer@2258,5330
    seen 1  blob@2262,5335  blob@2279,5353  meleer@2273,5341
    seen 1  blob@2258,5330  blob@2258,5353  meleer@2280,5333
    seen 1  blob@2258,5330  blob@2273,5341  meleer@2280,5333
    seen 1  blob@2260,5347  blob@2262,5335  meleer@2279,5353
    seen 1  blob@2258,5353  blob@2280,5333  meleer@2272,5330
wave 16
    seen 3  meleer@2262,5335  meleer@2280,5333
    seen 2  meleer@2258,5353  meleer@2262,5335
    seen 2  meleer@2258,5330  meleer@2280,5346
    seen 2  meleer@2262,5335  meleer@2273,5341
    seen 1  meleer@2258,5353  meleer@2260,5347
    seen 1  meleer@2258,5330  meleer@2280,5333
    seen 1  meleer@2258,5353  meleer@2279,5353
    seen 1  meleer@2258,5330  meleer@2260,5347
    seen 1  meleer@2258,5330  meleer@2258,5353
    seen 1  meleer@2258,5330  meleer@2273,5341
    seen 1  meleer@2273,5341  meleer@2279,5353
    seen 1  meleer@2258,5330  meleer@2262,5335
    seen 1  meleer@2273,5341  meleer@2280,5333
    seen 1  meleer@2260,5347  meleer@2280,5346
    seen 1  meleer@2260,5347  meleer@2279,5353
wave 18
    seen 4  ranger@2272,5330
    seen 4  ranger@2260,5347
    seen 3  ranger@2262,5335
    seen 3  ranger@2280,5333
    seen 3  ranger@2279,5353
    seen 1  ranger@2258,5353
    seen 1  ranger@2280,5346
    seen 1  ranger@2258,5330
wave 19
    seen 2  bat@2260,5347  ranger@2258,5330
    seen 2  bat@2262,5335  ranger@2258,5353
    seen 1  bat@2258,5353  ranger@2262,5335
    seen 1  bat@2279,5353  ranger@2258,5330
    seen 1  bat@2262,5335  ranger@2272,5330
    seen 1  bat@2279,5353  ranger@2262,5335
    seen 1  bat@2279,5353  ranger@2280,5333
    seen 1  bat@2260,5347  ranger@2258,5353
    seen 1  bat@2280,5333  ranger@2280,5346
    seen 1  bat@2273,5341  ranger@2280,5346
    seen 1  bat@2262,5335  ranger@2260,5347
    seen 1  bat@2262,5335  ranger@2279,5353
    seen 1  bat@2262,5335  ranger@2273,5341
    seen 1  bat@2258,5353  ranger@2279,5353
    seen 1  bat@2258,5330  ranger@2272,5330
    seen 1  bat@2279,5353  ranger@2273,5341
    seen 1  bat@2280,5346  ranger@2258,5330
    seen 1  bat@2273,5341  ranger@2260,5347
wave 20
    seen 2  bat@2273,5341  bat@2280,5333  ranger@2272,5330
    seen 2  bat@2279,5353  bat@2280,5333  ranger@2260,5347
    seen 1  bat@2260,5347  bat@2262,5335  ranger@2258,5330
    seen 1  bat@2279,5353  bat@2280,5346  ranger@2272,5330
    seen 1  bat@2272,5330  bat@2280,5346  ranger@2280,5333
    seen 1  bat@2258,5330  bat@2272,5330  ranger@2279,5353
    seen 1  bat@2262,5335  bat@2272,5330  ranger@2280,5333
    seen 1  bat@2258,5353  bat@2260,5347  ranger@2258,5330
    seen 1  bat@2258,5353  bat@2272,5330  ranger@2279,5353
    seen 1  bat@2262,5335  bat@2272,5330  ranger@2258,5330
    seen 1  bat@2273,5341  bat@2280,5346  ranger@2272,5330
    seen 1  bat@2262,5335  bat@2280,5333  ranger@2279,5353
    seen 1  bat@2272,5330  bat@2273,5341  ranger@2262,5335
    seen 1  bat@2272,5330  bat@2279,5353  ranger@2260,5347
    seen 1  bat@2258,5353  bat@2272,5330  ranger@2273,5341
    seen 1  bat@2272,5330  bat@2280,5346  ranger@2258,5330
    seen 1  bat@2260,5347  bat@2279,5353  ranger@2262,5335
    seen 1  bat@2262,5335  bat@2280,5346  ranger@2273,5341
wave 21
    seen 2  blob@2262,5335  ranger@2273,5341
    seen 2  blob@2273,5341  ranger@2279,5353
    seen 1  blob@2258,5353  ranger@2273,5341
    seen 1  blob@2258,5330  ranger@2262,5335
    seen 1  blob@2260,5347  ranger@2262,5335
    seen 1  blob@2280,5333  ranger@2279,5353
    seen 1  blob@2280,5333  ranger@2262,5335
    seen 1  blob@2272,5330  ranger@2280,5346
    seen 1  blob@2279,5353  ranger@2258,5330
    seen 1  blob@2272,5330  ranger@2280,5333
    seen 1  blob@2258,5330  ranger@2260,5347
    seen 1  blob@2258,5353  ranger@2280,5346
    seen 1  blob@2262,5335  ranger@2280,5346
    seen 1  blob@2262,5335  ranger@2280,5333
    seen 1  blob@2272,5330  ranger@2258,5353
    seen 1  blob@2273,5341  ranger@2262,5335
    seen 1  blob@2273,5341  ranger@2280,5333
    seen 1  blob@2260,5347  ranger@2280,5346
wave 22
    seen 1  bat@2273,5341  blob@2279,5353  ranger@2258,5330
    seen 1  bat@2279,5353  blob@2272,5330  ranger@2280,5333
    seen 1  bat@2260,5347  blob@2280,5333  ranger@2279,5353
    seen 1  bat@2272,5330  blob@2280,5333  ranger@2258,5353
    seen 1  bat@2280,5346  blob@2273,5341  ranger@2279,5353
    seen 1  bat@2280,5333  blob@2260,5347  ranger@2262,5335
    seen 1  bat@2280,5333  blob@2272,5330  ranger@2273,5341
    seen 1  bat@2258,5353  blob@2272,5330  ranger@2273,5341
    seen 1  bat@2258,5330  blob@2272,5330  ranger@2260,5347
    seen 1  bat@2280,5333  blob@2262,5335  ranger@2258,5353
    seen 1  bat@2273,5341  blob@2260,5347  ranger@2258,5353
    seen 1  bat@2272,5330  blob@2258,5330  ranger@2258,5353
    seen 1  bat@2258,5353  blob@2272,5330  ranger@2262,5335
    seen 1  bat@2262,5335  blob@2280,5333  ranger@2258,5353
    seen 1  bat@2280,5346  blob@2280,5333  ranger@2273,5341
    seen 1  bat@2258,5330  blob@2260,5347  ranger@2273,5341
    seen 1  bat@2279,5353  blob@2280,5346  ranger@2260,5347
    seen 1  bat@2279,5353  blob@2273,5341  ranger@2258,5330
    seen 1  bat@2280,5346  blob@2279,5353  ranger@2280,5333
    seen 1  bat@2258,5330  blob@2260,5347  ranger@2280,5333
wave 23
    seen 1  bat@2258,5330  bat@2279,5353  blob@2272,5330  ranger@2280,5333
    seen 1  bat@2260,5347  bat@2280,5333  blob@2262,5335  ranger@2258,5330
    seen 1  bat@2260,5347  bat@2272,5330  blob@2258,5353  ranger@2280,5333
    seen 1  bat@2260,5347  bat@2279,5353  blob@2280,5346  ranger@2280,5333
    seen 1  bat@2262,5335  bat@2280,5333  blob@2258,5330  ranger@2272,5330
    seen 1  bat@2272,5330  bat@2280,5333  blob@2258,5353  ranger@2258,5330
    seen 1  bat@2258,5330  bat@2260,5347  blob@2279,5353  ranger@2262,5335
    seen 1  bat@2262,5335  bat@2280,5333  blob@2258,5330  ranger@2258,5353
    seen 1  bat@2272,5330  bat@2273,5341  blob@2260,5347  ranger@2279,5353
    seen 1  bat@2260,5347  bat@2272,5330  blob@2258,5353  ranger@2280,5346
    seen 1  bat@2260,5347  bat@2273,5341  blob@2272,5330  ranger@2280,5333
    seen 1  bat@2272,5330  bat@2273,5341  blob@2262,5335  ranger@2280,5333
    seen 1  bat@2273,5341  bat@2279,5353  blob@2260,5347  ranger@2258,5353
    seen 1  bat@2260,5347  bat@2279,5353  blob@2280,5333  ranger@2262,5335
    seen 1  bat@2258,5353  bat@2273,5341  blob@2262,5335  ranger@2272,5330
    seen 1  bat@2258,5330  bat@2262,5335  blob@2272,5330  ranger@2280,5346
    seen 1  bat@2258,5330  bat@2280,5346  blob@2272,5330  ranger@2280,5333
    seen 1  bat@2258,5330  bat@2272,5330  blob@2280,5346  ranger@2258,5353
    seen 1  bat@2258,5353  bat@2260,5347  blob@2279,5353  ranger@2262,5335
    seen 1  bat@2258,5353  bat@2262,5335  blob@2272,5330  ranger@2279,5353
wave 24
    seen 1  blob@2262,5335  blob@2273,5341  ranger@2279,5353
    seen 1  blob@2272,5330  blob@2280,5333  ranger@2260,5347
    seen 1  blob@2279,5353  blob@2280,5346  ranger@2260,5347
    seen 1  blob@2258,5330  blob@2260,5347  ranger@2273,5341
    seen 1  blob@2258,5330  blob@2272,5330  ranger@2280,5346
    seen 1  blob@2258,5353  blob@2272,5330  ranger@2273,5341
    seen 1  blob@2260,5347  blob@2280,5346  ranger@2258,5330
    seen 1  blob@2258,5330  blob@2272,5330  ranger@2258,5353
    seen 1  blob@2258,5330  blob@2262,5335  ranger@2258,5353
    seen 1  blob@2272,5330  blob@2279,5353  ranger@2262,5335
    seen 1  blob@2258,5330  blob@2262,5335  ranger@2260,5347
    seen 1  blob@2258,5330  blob@2262,5335  ranger@2280,5333
    seen 1  blob@2260,5347  blob@2262,5335  ranger@2258,5330
    seen 1  blob@2260,5347  blob@2272,5330  ranger@2262,5335
    seen 1  blob@2258,5330  blob@2260,5347  ranger@2280,5346
    seen 1  blob@2258,5353  blob@2272,5330  ranger@2279,5353
    seen 1  blob@2272,5330  blob@2280,5346  ranger@2279,5353
    seen 1  blob@2258,5353  blob@2262,5335  ranger@2272,5330
    seen 1  blob@2258,5330  blob@2272,5330  ranger@2260,5347
    seen 1  blob@2262,5335  blob@2280,5346  ranger@2258,5353
wave 25
    seen 2  meleer@2258,5353  ranger@2279,5353
    seen 2  meleer@2260,5347  ranger@2258,5330
    seen 1  meleer@2262,5335  ranger@2258,5353
    seen 1  meleer@2260,5347  ranger@2280,5346
    seen 1  meleer@2279,5353  ranger@2272,5330
    seen 1  meleer@2273,5341  ranger@2280,5346
    seen 1  meleer@2262,5335  ranger@2280,5346
    seen 1  meleer@2279,5353  ranger@2258,5330
    seen 1  meleer@2258,5330  ranger@2262,5335
    seen 1  meleer@2280,5346  ranger@2280,5333
    seen 1  meleer@2279,5353  ranger@2260,5347
    seen 1  meleer@2262,5335  ranger@2279,5353
    seen 1  meleer@2280,5346  ranger@2260,5347
    seen 1  meleer@2280,5346  ranger@2262,5335
    seen 1  meleer@2258,5353  ranger@2280,5333
    seen 1  meleer@2258,5330  ranger@2260,5347
    seen 1  meleer@2262,5335  ranger@2258,5330
    seen 1  meleer@2272,5330  ranger@2258,5330
wave 26
    seen 2  bat@2280,5333  meleer@2258,5353  ranger@2280,5346
    seen 1  bat@2280,5346  meleer@2279,5353  ranger@2258,5330
    seen 1  bat@2272,5330  meleer@2258,5330  ranger@2273,5341
    seen 1  bat@2260,5347  meleer@2258,5330  ranger@2273,5341
    seen 1  bat@2279,5353  meleer@2262,5335  ranger@2273,5341
    seen 1  bat@2280,5333  meleer@2273,5341  ranger@2260,5347
    seen 1  bat@2280,5333  meleer@2258,5353  ranger@2258,5330
    seen 1  bat@2273,5341  meleer@2280,5333  ranger@2262,5335
    seen 1  bat@2258,5330  meleer@2262,5335  ranger@2273,5341
    seen 1  bat@2272,5330  meleer@2260,5347  ranger@2258,5353
    seen 1  bat@2258,5330  meleer@2273,5341  ranger@2262,5335
    seen 1  bat@2272,5330  meleer@2280,5333  ranger@2279,5353
    seen 1  bat@2262,5335  meleer@2273,5341  ranger@2280,5346
    seen 1  bat@2280,5346  meleer@2279,5353  ranger@2260,5347
    seen 1  bat@2260,5347  meleer@2273,5341  ranger@2272,5330
    seen 1  bat@2258,5353  meleer@2280,5346  ranger@2258,5330
    seen 1  bat@2280,5346  meleer@2258,5330  ranger@2272,5330
    seen 1  bat@2279,5353  meleer@2262,5335  ranger@2280,5346
    seen 1  bat@2260,5347  meleer@2279,5353  ranger@2280,5333
wave 27
    seen 1  bat@2272,5330  bat@2279,5353  meleer@2280,5333  ranger@2258,5353
    seen 1  bat@2258,5353  bat@2262,5335  meleer@2280,5333  ranger@2279,5353
    seen 1  bat@2260,5347  bat@2272,5330  meleer@2258,5353  ranger@2258,5330
    seen 1  bat@2258,5330  bat@2280,5346  meleer@2280,5333  ranger@2262,5335
    seen 1  bat@2258,5353  bat@2280,5346  meleer@2279,5353  ranger@2280,5333
    seen 1  bat@2273,5341  bat@2280,5346  meleer@2258,5353  ranger@2258,5330
    seen 1  bat@2258,5353  bat@2260,5347  meleer@2273,5341  ranger@2280,5346
    seen 1  bat@2280,5333  bat@2280,5346  meleer@2260,5347  ranger@2262,5335
    seen 1  bat@2260,5347  bat@2279,5353  meleer@2280,5333  ranger@2262,5335
    seen 1  bat@2279,5353  bat@2280,5333  meleer@2260,5347  ranger@2273,5341
    seen 1  bat@2273,5341  bat@2280,5346  meleer@2280,5333  ranger@2279,5353
    seen 1  bat@2260,5347  bat@2280,5346  meleer@2272,5330  ranger@2262,5335
    seen 1  bat@2272,5330  bat@2280,5333  meleer@2262,5335  ranger@2258,5330
    seen 1  bat@2260,5347  bat@2273,5341  meleer@2262,5335  ranger@2279,5353
    seen 1  bat@2272,5330  bat@2280,5333  meleer@2280,5346  ranger@2279,5353
    seen 1  bat@2272,5330  bat@2273,5341  meleer@2262,5335  ranger@2260,5347
    seen 1  bat@2272,5330  bat@2280,5346  meleer@2273,5341  ranger@2280,5333
    seen 1  bat@2258,5330  bat@2280,5333  meleer@2262,5335  ranger@2279,5353
    seen 1  bat@2279,5353  bat@2280,5346  meleer@2258,5330  ranger@2262,5335
    seen 1  bat@2260,5347  bat@2262,5335  meleer@2258,5330  ranger@2279,5353
wave 28
    seen 2  blob@2272,5330  meleer@2258,5330  ranger@2258,5353
    seen 1  blob@2273,5341  meleer@2258,5353  ranger@2258,5330
    seen 1  blob@2258,5353  meleer@2262,5335  ranger@2258,5330
    seen 1  blob@2279,5353  meleer@2272,5330  ranger@2280,5346
    seen 1  blob@2280,5346  meleer@2273,5341  ranger@2260,5347
    seen 1  blob@2262,5335  meleer@2260,5347  ranger@2280,5346
    seen 1  blob@2272,5330  meleer@2258,5353  ranger@2280,5333
    seen 1  blob@2262,5335  meleer@2258,5330  ranger@2272,5330
    seen 1  blob@2279,5353  meleer@2258,5330  ranger@2280,5333
    seen 1  blob@2279,5353  meleer@2280,5333  ranger@2262,5335
    seen 1  blob@2258,5330  meleer@2280,5333  ranger@2272,5330
    seen 1  blob@2258,5353  meleer@2279,5353  ranger@2258,5330
    seen 1  blob@2280,5346  meleer@2262,5335  ranger@2260,5347
    seen 1  blob@2258,5353  meleer@2260,5347  ranger@2280,5346
    seen 1  blob@2272,5330  meleer@2273,5341  ranger@2280,5346
    seen 1  blob@2273,5341  meleer@2280,5346  ranger@2262,5335
    seen 1  blob@2280,5346  meleer@2262,5335  ranger@2258,5353
    seen 1  blob@2280,5346  meleer@2273,5341  ranger@2279,5353
    seen 1  blob@2262,5335  meleer@2260,5347  ranger@2273,5341
wave 29
    seen 1  bat@2279,5353  blob@2260,5347  meleer@2258,5353  ranger@2272,5330
    seen 1  bat@2280,5333  blob@2262,5335  meleer@2280,5346  ranger@2272,5330
    seen 1  bat@2262,5335  blob@2273,5341  meleer@2272,5330  ranger@2260,5347
    seen 1  bat@2280,5346  blob@2280,5333  meleer@2258,5330  ranger@2260,5347
    seen 1  bat@2262,5335  blob@2280,5333  meleer@2280,5346  ranger@2260,5347
    seen 1  bat@2272,5330  blob@2262,5335  meleer@2280,5333  ranger@2260,5347
    seen 1  bat@2279,5353  blob@2272,5330  meleer@2258,5353  ranger@2280,5333
    seen 1  bat@2272,5330  blob@2279,5353  meleer@2273,5341  ranger@2280,5346
    seen 1  bat@2280,5333  blob@2279,5353  meleer@2262,5335  ranger@2273,5341
    seen 1  bat@2258,5353  blob@2260,5347  meleer@2262,5335  ranger@2258,5330
    seen 1  bat@2273,5341  blob@2280,5346  meleer@2279,5353  ranger@2262,5335
    seen 1  bat@2258,5330  blob@2280,5333  meleer@2272,5330  ranger@2279,5353
    seen 1  bat@2272,5330  blob@2258,5353  meleer@2280,5346  ranger@2260,5347
    seen 1  bat@2280,5333  blob@2260,5347  meleer@2272,5330  ranger@2273,5341
    seen 1  bat@2272,5330  blob@2260,5347  meleer@2280,5346  ranger@2279,5353
    seen 1  bat@2273,5341  blob@2262,5335  meleer@2260,5347  ranger@2280,5346
    seen 1  bat@2258,5353  blob@2273,5341  meleer@2280,5346  ranger@2272,5330
    seen 1  bat@2272,5330  blob@2258,5353  meleer@2260,5347  ranger@2280,5333
    seen 1  bat@2273,5341  blob@2280,5346  meleer@2262,5335  ranger@2280,5333
    seen 1  bat@2262,5335  blob@2258,5353  meleer@2260,5347  ranger@2258,5330
wave 30
    seen 1  bat@2272,5330  bat@2280,5333  blob@2280,5346  meleer@2262,5335  ranger@2260,5347
    seen 1  bat@2262,5335  bat@2273,5341  blob@2279,5353  meleer@2280,5346  ranger@2258,5353
    seen 1  bat@2258,5353  bat@2273,5341  blob@2280,5346  meleer@2272,5330  ranger@2262,5335
    seen 1  bat@2258,5330  bat@2273,5341  blob@2272,5330  meleer@2258,5353  ranger@2280,5333
    seen 1  bat@2258,5330  bat@2280,5333  blob@2272,5330  meleer@2262,5335  ranger@2260,5347
    seen 1  bat@2258,5330  bat@2280,5333  blob@2272,5330  meleer@2258,5353  ranger@2273,5341
    seen 1  bat@2258,5353  bat@2262,5335  blob@2258,5330  meleer@2272,5330  ranger@2280,5346
    seen 1  bat@2280,5333  bat@2280,5346  blob@2272,5330  meleer@2258,5353  ranger@2262,5335
    seen 1  bat@2262,5335  bat@2273,5341  blob@2260,5347  meleer@2280,5346  ranger@2279,5353
    seen 1  bat@2258,5330  bat@2279,5353  blob@2272,5330  meleer@2280,5346  ranger@2280,5333
    seen 1  bat@2258,5353  bat@2280,5333  blob@2272,5330  meleer@2260,5347  ranger@2280,5346
    seen 1  bat@2258,5330  bat@2272,5330  blob@2279,5353  meleer@2280,5346  ranger@2260,5347
    seen 1  bat@2272,5330  bat@2279,5353  blob@2280,5346  meleer@2258,5353  ranger@2260,5347
    seen 1  bat@2258,5330  bat@2258,5353  blob@2262,5335  meleer@2279,5353  ranger@2260,5347
    seen 1  bat@2272,5330  bat@2280,5346  blob@2279,5353  meleer@2280,5333  ranger@2260,5347
    seen 1  bat@2260,5347  bat@2262,5335  blob@2280,5333  meleer@2272,5330  ranger@2273,5341
    seen 1  bat@2258,5330  bat@2280,5333  blob@2262,5335  meleer@2273,5341  ranger@2260,5347
    seen 1  bat@2272,5330  bat@2280,5346  blob@2260,5347  meleer@2280,5333  ranger@2279,5353
    seen 1  bat@2280,5333  bat@2280,5346  blob@2279,5353  meleer@2260,5347  ranger@2258,5353
    seen 1  bat@2258,5353  bat@2280,5333  blob@2262,5335  meleer@2258,5330  ranger@2280,5346
wave 31
    seen 2  blob@2258,5330  blob@2258,5353  meleer@2260,5347  ranger@2262,5335
    seen 1  blob@2273,5341  blob@2280,5346  meleer@2280,5333  ranger@2272,5330
    seen 1  blob@2258,5330  blob@2273,5341  meleer@2260,5347  ranger@2280,5333
    seen 1  blob@2258,5353  blob@2272,5330  meleer@2273,5341  ranger@2262,5335
    seen 1  blob@2258,5353  blob@2260,5347  meleer@2279,5353  ranger@2272,5330
    seen 1  blob@2260,5347  blob@2279,5353  meleer@2273,5341  ranger@2272,5330
    seen 1  blob@2258,5353  blob@2260,5347  meleer@2280,5346  ranger@2279,5353
    seen 1  blob@2258,5330  blob@2280,5346  meleer@2279,5353  ranger@2260,5347
    seen 1  blob@2262,5335  blob@2280,5333  meleer@2258,5353  ranger@2273,5341
    seen 1  blob@2260,5347  blob@2280,5346  meleer@2262,5335  ranger@2273,5341
    seen 1  blob@2273,5341  blob@2280,5346  meleer@2260,5347  ranger@2258,5353
    seen 1  blob@2273,5341  blob@2279,5353  meleer@2260,5347  ranger@2262,5335
    seen 1  blob@2260,5347  blob@2280,5346  meleer@2272,5330  ranger@2258,5353
    seen 1  blob@2262,5335  blob@2273,5341  meleer@2280,5333  ranger@2280,5346
    seen 1  blob@2279,5353  blob@2280,5346  meleer@2280,5333  ranger@2273,5341
    seen 1  blob@2260,5347  blob@2279,5353  meleer@2258,5353  ranger@2262,5335
    seen 1  blob@2273,5341  blob@2280,5346  meleer@2258,5330  ranger@2279,5353
    seen 1  blob@2262,5335  blob@2273,5341  meleer@2279,5353  ranger@2258,5330
    seen 1  blob@2258,5353  blob@2272,5330  meleer@2279,5353  ranger@2280,5346
wave 32
    seen 2  meleer@2280,5333  meleer@2280,5346  ranger@2258,5353
    seen 2  meleer@2258,5353  meleer@2280,5346  ranger@2273,5341
    seen 1  meleer@2272,5330  meleer@2280,5346  ranger@2262,5335
    seen 1  meleer@2262,5335  meleer@2272,5330  ranger@2273,5341
    seen 1  meleer@2260,5347  meleer@2262,5335  ranger@2279,5353
    seen 1  meleer@2260,5347  meleer@2262,5335  ranger@2258,5353
    seen 1  meleer@2260,5347  meleer@2262,5335  ranger@2280,5346
    seen 1  meleer@2260,5347  meleer@2279,5353  ranger@2262,5335
    seen 1  meleer@2258,5330  meleer@2260,5347  ranger@2272,5330
    seen 1  meleer@2273,5341  meleer@2280,5346  ranger@2258,5330
    seen 1  meleer@2260,5347  meleer@2272,5330  ranger@2258,5330
    seen 1  meleer@2258,5330  meleer@2272,5330  ranger@2273,5341
    seen 1  meleer@2258,5330  meleer@2279,5353  ranger@2273,5341
    seen 1  meleer@2258,5353  meleer@2279,5353  ranger@2262,5335
    seen 1  meleer@2258,5330  meleer@2280,5346  ranger@2272,5330
    seen 1  meleer@2258,5330  meleer@2280,5333  ranger@2258,5353
    seen 1  meleer@2260,5347  meleer@2262,5335  ranger@2272,5330
wave 33
    seen 3  ranger@2280,5333  ranger@2280,5346
    seen 2  ranger@2258,5353  ranger@2262,5335
    seen 2  ranger@2262,5335  ranger@2272,5330
    seen 1  ranger@2258,5330  ranger@2279,5353
    seen 1  ranger@2262,5335  ranger@2280,5333
    seen 1  ranger@2279,5353  ranger@2280,5346
    seen 1  ranger@2273,5341  ranger@2280,5346
    seen 1  ranger@2258,5330  ranger@2280,5346
    seen 1  ranger@2258,5353  ranger@2260,5347
    seen 1  ranger@2260,5347  ranger@2280,5346
    seen 1  ranger@2272,5330  ranger@2279,5353
    seen 1  ranger@2260,5347  ranger@2280,5333
    seen 1  ranger@2260,5347  ranger@2273,5341
    seen 1  ranger@2258,5353  ranger@2280,5333
    seen 1  ranger@2258,5353  ranger@2272,5330
wave 35
    seen 5  mager@2262,5335
    seen 5  mager@2279,5353
    seen 4  mager@2258,5330
    seen 2  mager@2272,5330
    seen 1  mager@2258,5353
    seen 1  mager@2280,5333
    seen 1  mager@2273,5341
wave 36
    seen 2  bat@2262,5335  mager@2280,5333
    seen 2  bat@2258,5353  mager@2258,5330
    seen 1  bat@2280,5346  mager@2272,5330
    seen 1  bat@2272,5330  mager@2280,5346
    seen 1  bat@2280,5333  mager@2280,5346
    seen 1  bat@2272,5330  mager@2258,5330
    seen 1  bat@2273,5341  mager@2272,5330
    seen 1  bat@2280,5346  mager@2280,5333
    seen 1  bat@2280,5346  mager@2273,5341
    seen 1  bat@2273,5341  mager@2280,5333
    seen 1  bat@2258,5353  mager@2280,5333
    seen 1  bat@2258,5330  mager@2280,5346
    seen 1  bat@2273,5341  mager@2258,5353
    seen 1  bat@2260,5347  mager@2272,5330
    seen 1  bat@2280,5333  mager@2258,5353
    seen 1  bat@2279,5353  mager@2272,5330
    seen 1  bat@2273,5341  mager@2262,5335
wave 37
    seen 2  bat@2258,5353  bat@2280,5346  mager@2258,5330
    seen 1  bat@2279,5353  bat@2280,5346  mager@2260,5347
    seen 1  bat@2258,5330  bat@2260,5347  mager@2280,5333
    seen 1  bat@2258,5330  bat@2273,5341  mager@2279,5353
    seen 1  bat@2280,5333  bat@2280,5346  mager@2258,5353
    seen 1  bat@2258,5353  bat@2273,5341  mager@2272,5330
    seen 1  bat@2258,5353  bat@2279,5353  mager@2280,5333
    seen 1  bat@2272,5330  bat@2280,5346  mager@2262,5335
    seen 1  bat@2258,5330  bat@2262,5335  mager@2258,5353
    seen 1  bat@2272,5330  bat@2280,5333  mager@2273,5341
    seen 1  bat@2258,5330  bat@2280,5346  mager@2279,5353
    seen 1  bat@2258,5353  bat@2262,5335  mager@2273,5341
    seen 1  bat@2258,5330  bat@2272,5330  mager@2262,5335
    seen 1  bat@2260,5347  bat@2279,5353  mager@2280,5333
    seen 1  bat@2260,5347  bat@2262,5335  mager@2273,5341
    seen 1  bat@2258,5330  bat@2280,5346  mager@2260,5347
    seen 1  bat@2272,5330  bat@2279,5353  mager@2273,5341
    seen 1  bat@2262,5335  bat@2273,5341  mager@2280,5333
wave 38
    seen 2  blob@2260,5347  mager@2279,5353
    seen 1  blob@2258,5353  mager@2272,5330
    seen 1  blob@2280,5346  mager@2258,5353
    seen 1  blob@2258,5353  mager@2262,5335
    seen 1  blob@2262,5335  mager@2280,5346
    seen 1  blob@2262,5335  mager@2258,5330
    seen 1  blob@2280,5346  mager@2273,5341
    seen 1  blob@2258,5353  mager@2279,5353
    seen 1  blob@2273,5341  mager@2279,5353
    seen 1  blob@2280,5333  mager@2272,5330
    seen 1  blob@2280,5333  mager@2260,5347
    seen 1  blob@2280,5333  mager@2258,5330
    seen 1  blob@2272,5330  mager@2258,5330
    seen 1  blob@2279,5353  mager@2280,5346
    seen 1  blob@2279,5353  mager@2262,5335
    seen 1  blob@2272,5330  mager@2258,5353
    seen 1  blob@2260,5347  mager@2258,5330
    seen 1  blob@2272,5330  mager@2280,5346
wave 39
    seen 1  bat@2258,5353  blob@2280,5346  mager@2279,5353
    seen 1  bat@2280,5333  blob@2273,5341  mager@2272,5330
    seen 1  bat@2262,5335  blob@2258,5353  mager@2272,5330
    seen 1  bat@2273,5341  blob@2260,5347  mager@2258,5353
    seen 1  bat@2280,5346  blob@2280,5333  mager@2273,5341
    seen 1  bat@2280,5346  blob@2280,5333  mager@2258,5330
    seen 1  bat@2260,5347  blob@2258,5353  mager@2280,5333
    seen 1  bat@2262,5335  blob@2279,5353  mager@2260,5347
    seen 1  bat@2258,5353  blob@2262,5335  mager@2273,5341
    seen 1  bat@2280,5346  blob@2272,5330  mager@2260,5347
    seen 1  bat@2262,5335  blob@2260,5347  mager@2280,5333
    seen 1  bat@2258,5353  blob@2280,5333  mager@2258,5330
    seen 1  bat@2279,5353  blob@2258,5330  mager@2272,5330
    seen 1  bat@2279,5353  blob@2260,5347  mager@2258,5330
    seen 1  bat@2280,5346  blob@2279,5353  mager@2272,5330
    seen 1  bat@2280,5333  blob@2272,5330  mager@2279,5353
    seen 1  bat@2260,5347  blob@2280,5333  mager@2258,5330
    seen 1  bat@2280,5333  blob@2262,5335  mager@2273,5341
    seen 1  bat@2280,5333  blob@2272,5330  mager@2280,5346
wave 40
    seen 2  bat@2273,5341  bat@2280,5333  blob@2260,5347  mager@2258,5330
    seen 1  bat@2262,5335  bat@2279,5353  blob@2258,5353  mager@2272,5330
    seen 1  bat@2258,5353  bat@2279,5353  blob@2258,5330  mager@2272,5330
    seen 1  bat@2262,5335  bat@2279,5353  blob@2258,5353  mager@2280,5333
    seen 1  bat@2260,5347  bat@2272,5330  blob@2262,5335  mager@2258,5353
    seen 1  bat@2258,5330  bat@2272,5330  blob@2258,5353  mager@2273,5341
    seen 1  bat@2258,5330  bat@2272,5330  blob@2260,5347  mager@2273,5341
    seen 1  bat@2273,5341  bat@2280,5333  blob@2258,5330  mager@2279,5353
    seen 1  bat@2260,5347  bat@2280,5333  blob@2272,5330  mager@2262,5335
    seen 1  bat@2272,5330  bat@2280,5346  blob@2273,5341  mager@2260,5347
    seen 1  bat@2258,5330  bat@2279,5353  blob@2273,5341  mager@2258,5353
    seen 1  bat@2258,5353  bat@2260,5347  blob@2272,5330  mager@2280,5346
    seen 1  bat@2262,5335  bat@2280,5333  blob@2272,5330  mager@2258,5330
    seen 1  bat@2258,5330  bat@2260,5347  blob@2273,5341  mager@2279,5353
    seen 1  bat@2258,5330  bat@2260,5347  blob@2272,5330  mager@2273,5341
    seen 1  bat@2258,5353  bat@2279,5353  blob@2272,5330  mager@2273,5341
    seen 1  bat@2260,5347  bat@2262,5335  blob@2280,5333  mager@2258,5353
    seen 1  bat@2260,5347  bat@2262,5335  blob@2280,5346  mager@2258,5353
wave 41
    seen 1  blob@2258,5330  blob@2280,5346  mager@2260,5347
    seen 1  blob@2260,5347  blob@2279,5353  mager@2280,5346
    seen 1  blob@2258,5330  blob@2273,5341  mager@2260,5347
    seen 1  blob@2272,5330  blob@2280,5346  mager@2258,5330
    seen 1  blob@2262,5335  blob@2272,5330  mager@2280,5333
    seen 1  blob@2260,5347  blob@2279,5353  mager@2273,5341
    seen 1  blob@2258,5330  blob@2280,5346  mager@2273,5341
    seen 1  blob@2258,5330  blob@2273,5341  mager@2280,5333
    seen 1  blob@2279,5353  blob@2280,5346  mager@2258,5353
    seen 1  blob@2258,5353  blob@2260,5347  mager@2262,5335
    seen 1  blob@2273,5341  blob@2280,5346  mager@2279,5353
    seen 1  blob@2258,5330  blob@2258,5353  mager@2280,5346
    seen 1  blob@2258,5330  blob@2280,5346  mager@2280,5333
    seen 1  blob@2258,5330  blob@2273,5341  mager@2280,5346
    seen 1  blob@2260,5347  blob@2273,5341  mager@2279,5353
    seen 1  blob@2262,5335  blob@2280,5333  mager@2258,5330
    seen 1  blob@2273,5341  blob@2280,5333  mager@2280,5346
    seen 1  blob@2260,5347  blob@2280,5333  mager@2273,5341
    seen 1  blob@2258,5353  blob@2273,5341  mager@2258,5330
wave 42
    seen 2  meleer@2258,5353  mager@2272,5330
    seen 2  meleer@2280,5346  mager@2273,5341
    seen 2  meleer@2260,5347  mager@2262,5335
    seen 1  meleer@2262,5335  mager@2272,5330
    seen 1  meleer@2279,5353  mager@2262,5335
    seen 1  meleer@2280,5346  mager@2260,5347
    seen 1  meleer@2280,5333  mager@2258,5330
    seen 1  meleer@2258,5330  mager@2279,5353
    seen 1  meleer@2258,5353  mager@2262,5335
    seen 1  meleer@2260,5347  mager@2280,5333
    seen 1  meleer@2273,5341  mager@2280,5333
    seen 1  meleer@2279,5353  mager@2258,5330
    seen 1  meleer@2272,5330  mager@2280,5333
    seen 1  meleer@2280,5333  mager@2262,5335
    seen 1  meleer@2260,5347  mager@2272,5330
    seen 1  meleer@2272,5330  mager@2279,5353
wave 43
    seen 1  bat@2279,5353  meleer@2272,5330  mager@2280,5333
    seen 1  bat@2280,5333  meleer@2262,5335  mager@2280,5346
    seen 1  bat@2258,5330  meleer@2272,5330  mager@2260,5347
    seen 1  bat@2258,5330  meleer@2273,5341  mager@2272,5330
    seen 1  bat@2273,5341  meleer@2280,5346  mager@2258,5330
    seen 1  bat@2273,5341  meleer@2272,5330  mager@2258,5330
    seen 1  bat@2258,5353  meleer@2279,5353  mager@2258,5330
    seen 1  bat@2260,5347  meleer@2273,5341  mager@2280,5346
    seen 1  bat@2258,5330  meleer@2280,5346  mager@2262,5335
    seen 1  bat@2279,5353  meleer@2258,5330  mager@2258,5353
    seen 1  bat@2280,5333  meleer@2273,5341  mager@2272,5330
    seen 1  bat@2260,5347  meleer@2280,5333  mager@2279,5353
    seen 1  bat@2262,5335  meleer@2258,5330  mager@2280,5333
    seen 1  bat@2279,5353  meleer@2280,5346  mager@2258,5353
    seen 1  bat@2280,5333  meleer@2258,5353  mager@2280,5346
    seen 1  bat@2280,5346  meleer@2279,5353  mager@2272,5330
    seen 1  bat@2280,5333  meleer@2279,5353  mager@2258,5330
    seen 1  bat@2258,5330  meleer@2258,5353  mager@2262,5335
    seen 1  bat@2260,5347  meleer@2258,5330  mager@2272,5330
wave 44
    seen 1  bat@2258,5353  bat@2272,5330  meleer@2280,5346  mager@2258,5330
    seen 1  bat@2262,5335  bat@2272,5330  meleer@2280,5346  mager@2279,5353
    seen 1  bat@2262,5335  bat@2272,5330  meleer@2260,5347  mager@2273,5341
    seen 1  bat@2258,5330  bat@2258,5353  meleer@2280,5346  mager@2262,5335
    seen 1  bat@2262,5335  bat@2272,5330  meleer@2273,5341  mager@2258,5330
    seen 1  bat@2258,5330  bat@2273,5341  meleer@2260,5347  mager@2279,5353
    seen 1  bat@2258,5330  bat@2279,5353  meleer@2272,5330  mager@2280,5346
    seen 1  bat@2262,5335  bat@2279,5353  meleer@2272,5330  mager@2280,5346
    seen 1  bat@2258,5353  bat@2262,5335  meleer@2260,5347  mager@2279,5353
    seen 1  bat@2279,5353  bat@2280,5333  meleer@2258,5353  mager@2258,5330
    seen 1  bat@2260,5347  bat@2273,5341  meleer@2258,5353  mager@2262,5335
    seen 1  bat@2258,5330  bat@2260,5347  meleer@2273,5341  mager@2279,5353
    seen 1  bat@2272,5330  bat@2273,5341  meleer@2258,5330  mager@2279,5353
    seen 1  bat@2260,5347  bat@2272,5330  meleer@2258,5330  mager@2280,5346
    seen 1  bat@2280,5333  bat@2280,5346  meleer@2258,5330  mager@2262,5335
    seen 1  bat@2272,5330  bat@2279,5353  meleer@2258,5353  mager@2273,5341
    seen 1  bat@2280,5333  bat@2280,5346  meleer@2258,5353  mager@2273,5341
    seen 1  bat@2258,5353  bat@2260,5347  meleer@2272,5330  mager@2280,5346
    seen 1  bat@2272,5330  bat@2279,5353  meleer@2258,5330  mager@2262,5335
wave 45
    seen 1  blob@2260,5347  meleer@2280,5333  mager@2258,5353
    seen 1  blob@2260,5347  meleer@2258,5330  mager@2262,5335
    seen 1  blob@2260,5347  meleer@2258,5353  mager@2273,5341
    seen 1  blob@2279,5353  meleer@2260,5347  mager@2258,5353
    seen 1  blob@2258,5353  meleer@2279,5353  mager@2280,5333
    seen 1  blob@2260,5347  meleer@2280,5333  mager@2273,5341
    seen 1  blob@2258,5330  meleer@2260,5347  mager@2280,5333
    seen 1  blob@2260,5347  meleer@2258,5330  mager@2280,5333
    seen 1  blob@2280,5333  meleer@2273,5341  mager@2280,5346
    seen 1  blob@2279,5353  meleer@2272,5330  mager@2273,5341
    seen 1  blob@2258,5330  meleer@2262,5335  mager@2279,5353
    seen 1  blob@2262,5335  meleer@2272,5330  mager@2258,5353
    seen 1  blob@2258,5330  meleer@2262,5335  mager@2280,5333
    seen 1  blob@2260,5347  meleer@2280,5346  mager@2272,5330
    seen 1  blob@2273,5341  meleer@2280,5333  mager@2262,5335
    seen 1  blob@2260,5347  meleer@2279,5353  mager@2273,5341
    seen 1  blob@2273,5341  meleer@2260,5347  mager@2280,5333
    seen 1  blob@2262,5335  meleer@2273,5341  mager@2258,5330
wave 46
    seen 1  bat@2258,5353  blob@2260,5347  meleer@2279,5353  mager@2262,5335
    seen 1  bat@2280,5346  blob@2279,5353  meleer@2272,5330  mager@2258,5353
    seen 1  bat@2262,5335  blob@2272,5330  meleer@2258,5330  mager@2260,5347
    seen 1  bat@2258,5353  blob@2260,5347  meleer@2272,5330  mager@2280,5333
    seen 1  bat@2273,5341  blob@2272,5330  meleer@2258,5353  mager@2260,5347
    seen 1  bat@2258,5353  blob@2279,5353  meleer@2262,5335  mager@2280,5346
    seen 1  bat@2260,5347  blob@2272,5330  meleer@2262,5335  mager@2258,5353
    seen 1  bat@2258,5330  blob@2262,5335  meleer@2260,5347  mager@2272,5330
    seen 1  bat@2258,5330  blob@2260,5347  meleer@2280,5333  mager@2280,5346
    seen 1  bat@2262,5335  blob@2280,5333  meleer@2272,5330  mager@2280,5346
    seen 1  bat@2258,5330  blob@2260,5347  meleer@2262,5335  mager@2280,5346
    seen 1  bat@2258,5353  blob@2273,5341  meleer@2279,5353  mager@2260,5347
    seen 1  bat@2279,5353  blob@2258,5330  meleer@2262,5335  mager@2273,5341
    seen 1  bat@2272,5330  blob@2279,5353  meleer@2280,5333  mager@2258,5353
    seen 1  bat@2272,5330  blob@2258,5353  meleer@2280,5333  mager@2260,5347
    seen 1  bat@2262,5335  blob@2280,5346  meleer@2279,5353  mager@2272,5330
    seen 1  bat@2262,5335  blob@2272,5330  meleer@2260,5347  mager@2273,5341
    seen 1  bat@2260,5347  blob@2262,5335  meleer@2280,5333  mager@2280,5346
wave 47
    seen 1  bat@2273,5341  bat@2279,5353  blob@2258,5330  meleer@2280,5346  mager@2258,5353
    seen 1  bat@2260,5347  bat@2262,5335  blob@2272,5330  meleer@2258,5330  mager@2273,5341
    seen 1  bat@2272,5330  bat@2273,5341  blob@2260,5347  meleer@2280,5346  mager@2279,5353
    seen 1  bat@2258,5353  bat@2280,5346  blob@2280,5333  meleer@2272,5330  mager@2260,5347
    seen 1  bat@2272,5330  bat@2273,5341  blob@2258,5330  meleer@2262,5335  mager@2279,5353
    seen 1  bat@2272,5330  bat@2280,5346  blob@2279,5353  meleer@2273,5341  mager@2280,5333
    seen 1  bat@2258,5330  bat@2279,5353  blob@2273,5341  meleer@2262,5335  mager@2280,5346
    seen 1  bat@2273,5341  bat@2279,5353  blob@2280,5346  meleer@2258,5330  mager@2280,5333
    seen 1  bat@2262,5335  bat@2273,5341  blob@2280,5346  meleer@2280,5333  mager@2279,5353
    seen 1  bat@2272,5330  bat@2279,5353  blob@2258,5330  meleer@2280,5346  mager@2280,5333
    seen 1  bat@2258,5330  bat@2280,5346  blob@2273,5341  meleer@2262,5335  mager@2272,5330
    seen 1  bat@2273,5341  bat@2279,5353  blob@2258,5330  meleer@2280,5333  mager@2260,5347
    seen 1  bat@2279,5353  bat@2280,5346  blob@2272,5330  meleer@2258,5353  mager@2258,5330
    seen 1  bat@2258,5353  bat@2272,5330  blob@2280,5346  meleer@2273,5341  mager@2258,5330
    seen 1  bat@2260,5347  bat@2272,5330  blob@2279,5353  meleer@2258,5330  mager@2280,5346
    seen 1  bat@2272,5330  bat@2273,5341  blob@2279,5353  meleer@2280,5346  mager@2280,5333
    seen 1  bat@2262,5335  bat@2280,5333  blob@2258,5330  meleer@2279,5353  mager@2280,5346
    seen 1  bat@2262,5335  bat@2280,5346  blob@2272,5330  meleer@2258,5353  mager@2258,5330
wave 48
    seen 1  blob@2258,5330  blob@2273,5341  meleer@2272,5330  mager@2260,5347
    seen 1  blob@2262,5335  blob@2272,5330  meleer@2273,5341  mager@2280,5346
    seen 1  blob@2260,5347  blob@2272,5330  meleer@2262,5335  mager@2280,5333
    seen 1  blob@2262,5335  blob@2273,5341  meleer@2258,5330  mager@2260,5347
    seen 1  blob@2273,5341  blob@2280,5333  meleer@2279,5353  mager@2258,5330
    seen 1  blob@2272,5330  blob@2280,5346  meleer@2279,5353  mager@2280,5333
    seen 1  blob@2262,5335  blob@2273,5341  meleer@2272,5330  mager@2279,5353
    seen 1  blob@2260,5347  blob@2280,5333  meleer@2279,5353  mager@2262,5335
    seen 1  blob@2260,5347  blob@2273,5341  meleer@2280,5333  mager@2258,5330
    seen 1  blob@2260,5347  blob@2279,5353  meleer@2258,5353  mager@2262,5335
    seen 1  blob@2258,5353  blob@2262,5335  meleer@2280,5346  mager@2279,5353
    seen 1  blob@2258,5330  blob@2262,5335  meleer@2279,5353  mager@2272,5330
    seen 1  blob@2280,5333  blob@2280,5346  meleer@2262,5335  mager@2258,5330
    seen 1  blob@2258,5353  blob@2279,5353  meleer@2272,5330  mager@2258,5330
    seen 1  blob@2273,5341  blob@2280,5333  meleer@2258,5353  mager@2258,5330
    seen 1  blob@2258,5330  blob@2279,5353  meleer@2272,5330  mager@2260,5347
    seen 1  blob@2258,5353  blob@2272,5330  meleer@2280,5346  mager@2262,5335
    seen 1  blob@2272,5330  blob@2279,5353  meleer@2258,5330  mager@2273,5341
wave 49
    seen 1  meleer@2279,5353  meleer@2280,5333  mager@2280,5346
    seen 1  meleer@2260,5347  meleer@2280,5333  mager@2262,5335
    seen 1  meleer@2258,5330  meleer@2273,5341  mager@2258,5353
    seen 1  meleer@2273,5341  meleer@2279,5353  mager@2260,5347
    seen 1  meleer@2272,5330  meleer@2280,5333  mager@2273,5341
    seen 1  meleer@2258,5330  meleer@2272,5330  mager@2260,5347
    seen 1  meleer@2262,5335  meleer@2273,5341  mager@2258,5330
    seen 1  meleer@2258,5330  meleer@2262,5335  mager@2279,5353
    seen 1  meleer@2280,5333  meleer@2280,5346  mager@2262,5335
    seen 1  meleer@2258,5330  meleer@2258,5353  mager@2260,5347
    seen 1  meleer@2258,5330  meleer@2262,5335  mager@2273,5341
    seen 1  meleer@2258,5353  meleer@2272,5330  mager@2260,5347
    seen 1  meleer@2272,5330  meleer@2280,5346  mager@2273,5341
    seen 1  meleer@2273,5341  meleer@2280,5333  mager@2258,5353
    seen 1  meleer@2258,5330  meleer@2280,5346  mager@2262,5335
    seen 1  meleer@2258,5353  meleer@2262,5335  mager@2260,5347
    seen 1  meleer@2280,5333  meleer@2280,5346  mager@2258,5330
wave 50
    seen 2  ranger@2279,5353  mager@2272,5330
    seen 1  ranger@2279,5353  mager@2280,5333
    seen 1  ranger@2258,5330  mager@2279,5353
    seen 1  ranger@2272,5330  mager@2273,5341
    seen 1  ranger@2280,5346  mager@2279,5353
    seen 1  ranger@2280,5346  mager@2273,5341
    seen 1  ranger@2262,5335  mager@2273,5341
    seen 1  ranger@2280,5333  mager@2258,5353
    seen 1  ranger@2260,5347  mager@2272,5330
    seen 1  ranger@2262,5335  mager@2258,5330
    seen 1  ranger@2260,5347  mager@2258,5330
    seen 1  ranger@2258,5330  mager@2280,5333
    seen 1  ranger@2258,5353  mager@2262,5335
    seen 1  ranger@2262,5335  mager@2258,5353
    seen 1  ranger@2273,5341  mager@2260,5347
    seen 1  ranger@2273,5341  mager@2280,5333
wave 51
    seen 1  bat@2280,5346  ranger@2260,5347  mager@2280,5333
    seen 1  bat@2280,5346  ranger@2258,5330  mager@2258,5353
    seen 1  bat@2273,5341  ranger@2279,5353  mager@2258,5353
    seen 1  bat@2280,5346  ranger@2258,5353  mager@2262,5335
    seen 1  bat@2280,5333  ranger@2280,5346  mager@2272,5330
    seen 1  bat@2280,5346  ranger@2258,5353  mager@2258,5330
    seen 1  bat@2272,5330  ranger@2260,5347  mager@2280,5346
    seen 1  bat@2280,5333  ranger@2258,5330  mager@2280,5346
    seen 1  bat@2279,5353  ranger@2273,5341  mager@2262,5335
    seen 1  bat@2260,5347  ranger@2258,5330  mager@2273,5341
    seen 1  bat@2280,5346  ranger@2258,5353  mager@2272,5330
    seen 1  bat@2258,5353  ranger@2272,5330  mager@2260,5347
    seen 1  bat@2279,5353  ranger@2280,5333  mager@2273,5341
    seen 1  bat@2272,5330  ranger@2258,5330  mager@2279,5353
    seen 1  bat@2260,5347  ranger@2262,5335  mager@2273,5341
    seen 1  bat@2280,5333  ranger@2258,5353  mager@2280,5346
    seen 1  bat@2280,5346  ranger@2273,5341  mager@2260,5347
wave 52
    seen 1  bat@2272,5330  bat@2280,5333  ranger@2260,5347  mager@2258,5330
    seen 1  bat@2262,5335  bat@2279,5353  ranger@2273,5341  mager@2258,5330
    seen 1  bat@2258,5330  bat@2279,5353  ranger@2258,5353  mager@2260,5347
    seen 1  bat@2279,5353  bat@2280,5346  ranger@2280,5333  mager@2260,5347
    seen 1  bat@2258,5330  bat@2280,5346  ranger@2258,5353  mager@2260,5347
    seen 1  bat@2273,5341  bat@2280,5346  ranger@2272,5330  mager@2262,5335
    seen 1  bat@2260,5347  bat@2272,5330  ranger@2279,5353  mager@2258,5353
    seen 1  bat@2260,5347  bat@2272,5330  ranger@2279,5353  mager@2262,5335
    seen 1  bat@2258,5330  bat@2273,5341  ranger@2260,5347  mager@2272,5330
    seen 1  bat@2258,5330  bat@2273,5341  ranger@2279,5353  mager@2262,5335
    seen 1  bat@2262,5335  bat@2272,5330  ranger@2279,5353  mager@2258,5353
    seen 1  bat@2258,5353  bat@2279,5353  ranger@2262,5335  mager@2260,5347
    seen 1  bat@2258,5353  bat@2279,5353  ranger@2258,5330  mager@2273,5341
    seen 1  bat@2260,5347  bat@2273,5341  ranger@2262,5335  mager@2280,5333
    seen 1  bat@2258,5330  bat@2272,5330  ranger@2273,5341  mager@2260,5347
    seen 1  bat@2258,5353  bat@2273,5341  ranger@2272,5330  mager@2258,5330
    seen 1  bat@2258,5330  bat@2272,5330  ranger@2280,5333  mager@2262,5335
wave 53
    seen 1  blob@2258,5330  ranger@2272,5330  mager@2280,5346
    seen 1  blob@2258,5330  ranger@2280,5333  mager@2262,5335
    seen 1  blob@2258,5353  ranger@2280,5346  mager@2273,5341
    seen 1  blob@2272,5330  ranger@2258,5353  mager@2273,5341
    seen 1  blob@2280,5346  ranger@2279,5353  mager@2262,5335
    seen 1  blob@2260,5347  ranger@2279,5353  mager@2272,5330
    seen 1  blob@2280,5333  ranger@2279,5353  mager@2273,5341
    seen 1  blob@2262,5335  ranger@2260,5347  mager@2280,5346
    seen 1  blob@2280,5346  ranger@2273,5341  mager@2260,5347
    seen 1  blob@2262,5335  ranger@2258,5330  mager@2260,5347
    seen 1  blob@2273,5341  ranger@2279,5353  mager@2272,5330
    seen 1  blob@2260,5347  ranger@2280,5346  mager@2280,5333
    seen 1  blob@2272,5330  ranger@2262,5335  mager@2258,5330
    seen 1  blob@2279,5353  ranger@2258,5353  mager@2273,5341
    seen 1  blob@2280,5333  ranger@2273,5341  mager@2280,5346
    seen 1  blob@2279,5353  ranger@2260,5347  mager@2262,5335
    seen 1  blob@2272,5330  ranger@2260,5347  mager@2258,5353
wave 54
    seen 1  bat@2258,5353  blob@2258,5330  ranger@2273,5341  mager@2260,5347
    seen 1  bat@2258,5353  blob@2260,5347  ranger@2280,5346  mager@2279,5353
    seen 1  bat@2260,5347  blob@2262,5335  ranger@2258,5353  mager@2279,5353
    seen 1  bat@2273,5341  blob@2280,5346  ranger@2280,5333  mager@2258,5330
    seen 1  bat@2273,5341  blob@2258,5353  ranger@2262,5335  mager@2280,5333
    seen 1  bat@2258,5330  blob@2262,5335  ranger@2260,5347  mager@2280,5333
    seen 1  bat@2279,5353  blob@2262,5335  ranger@2258,5353  mager@2272,5330
    seen 1  bat@2260,5347  blob@2258,5330  ranger@2280,5333  mager@2273,5341
    seen 1  bat@2280,5333  blob@2258,5330  ranger@2279,5353  mager@2272,5330
    seen 1  bat@2280,5346  blob@2280,5333  ranger@2273,5341  mager@2258,5353
    seen 1  bat@2280,5346  blob@2258,5330  ranger@2273,5341  mager@2279,5353
    seen 1  bat@2272,5330  blob@2262,5335  ranger@2258,5353  mager@2279,5353
    seen 1  bat@2258,5353  blob@2260,5347  ranger@2272,5330  mager@2273,5341
    seen 1  bat@2258,5330  blob@2260,5347  ranger@2273,5341  mager@2272,5330
    seen 1  bat@2280,5333  blob@2260,5347  ranger@2258,5353  mager@2272,5330
    seen 1  bat@2280,5346  blob@2260,5347  ranger@2280,5333  mager@2279,5353
    seen 1  bat@2280,5346  blob@2260,5347  ranger@2262,5335  mager@2280,5333
wave 55
    seen 1  bat@2258,5330  bat@2258,5353  blob@2273,5341  ranger@2262,5335  mager@2260,5347
    seen 1  bat@2272,5330  bat@2273,5341  blob@2258,5353  ranger@2280,5333  mager@2262,5335
    seen 1  bat@2260,5347  bat@2280,5333  blob@2258,5353  ranger@2272,5330  mager@2280,5346
    seen 1  bat@2260,5347  bat@2280,5333  blob@2262,5335  ranger@2279,5353  mager@2258,5353
    seen 1  bat@2272,5330  bat@2280,5346  blob@2273,5341  ranger@2280,5333  mager@2262,5335
    seen 1  bat@2258,5353  bat@2279,5353  blob@2262,5335  ranger@2258,5330  mager@2280,5346
    seen 1  bat@2260,5347  bat@2262,5335  blob@2273,5341  ranger@2258,5330  mager@2280,5346
    seen 1  bat@2262,5335  bat@2280,5333  blob@2280,5346  ranger@2258,5330  mager@2258,5353
    seen 1  bat@2273,5341  bat@2280,5333  blob@2272,5330  ranger@2262,5335  mager@2258,5330
    seen 1  bat@2258,5330  bat@2279,5353  blob@2280,5333  ranger@2273,5341  mager@2262,5335
    seen 1  bat@2258,5353  bat@2279,5353  blob@2280,5333  ranger@2262,5335  mager@2273,5341
    seen 1  bat@2258,5353  bat@2260,5347  blob@2262,5335  ranger@2280,5346  mager@2273,5341
    seen 1  bat@2258,5353  bat@2260,5347  blob@2280,5346  ranger@2273,5341  mager@2272,5330
    seen 1  bat@2272,5330  bat@2273,5341  blob@2258,5330  ranger@2262,5335  mager@2280,5346
    seen 1  bat@2272,5330  bat@2280,5346  blob@2273,5341  ranger@2258,5353  mager@2262,5335
    seen 1  bat@2272,5330  bat@2280,5346  blob@2260,5347  ranger@2258,5330  mager@2262,5335
    seen 1  bat@2279,5353  bat@2280,5346  blob@2273,5341  ranger@2260,5347  mager@2258,5330
wave 56
    seen 1  blob@2279,5353  blob@2280,5333  ranger@2273,5341  mager@2258,5353
    seen 1  blob@2262,5335  blob@2280,5346  ranger@2273,5341  mager@2258,5353
    seen 1  blob@2272,5330  blob@2279,5353  ranger@2260,5347  mager@2280,5346
    seen 1  blob@2260,5347  blob@2273,5341  ranger@2279,5353  mager@2272,5330
    seen 1  blob@2260,5347  blob@2280,5346  ranger@2279,5353  mager@2272,5330
    seen 1  blob@2260,5347  blob@2280,5333  ranger@2273,5341  mager@2272,5330
    seen 1  blob@2262,5335  blob@2279,5353  ranger@2280,5333  mager@2272,5330
    seen 1  blob@2258,5330  blob@2258,5353  ranger@2262,5335  mager@2273,5341
    seen 1  blob@2258,5353  blob@2260,5347  ranger@2280,5333  mager@2262,5335
    seen 1  blob@2258,5353  blob@2280,5333  ranger@2258,5330  mager@2272,5330
    seen 1  blob@2258,5353  blob@2279,5353  ranger@2273,5341  mager@2258,5330
    seen 1  blob@2258,5330  blob@2260,5347  ranger@2280,5333  mager@2279,5353
    seen 1  blob@2272,5330  blob@2279,5353  ranger@2280,5333  mager@2260,5347
    seen 1  blob@2273,5341  blob@2280,5333  ranger@2262,5335  mager@2280,5346
    seen 1  blob@2260,5347  blob@2279,5353  ranger@2280,5333  mager@2258,5353
    seen 1  blob@2258,5330  blob@2279,5353  ranger@2272,5330  mager@2262,5335
wave 57
    seen 1  meleer@2262,5335  ranger@2258,5353  mager@2273,5341
    seen 1  meleer@2279,5353  ranger@2280,5333  mager@2262,5335
    seen 1  meleer@2272,5330  ranger@2258,5330  mager@2279,5353
    seen 1  meleer@2272,5330  ranger@2260,5347  mager@2280,5346
    seen 1  meleer@2258,5353  ranger@2258,5330  mager@2280,5346
    seen 1  meleer@2258,5353  ranger@2258,5330  mager@2280,5333
    seen 1  meleer@2262,5335  ranger@2272,5330  mager@2258,5330
    seen 1  meleer@2279,5353  ranger@2272,5330  mager@2280,5346
    seen 1  meleer@2262,5335  ranger@2258,5330  mager@2273,5341
    seen 1  meleer@2258,5330  ranger@2280,5333  mager@2272,5330
    seen 1  meleer@2280,5333  ranger@2272,5330  mager@2258,5330
    seen 1  meleer@2260,5347  ranger@2258,5330  mager@2258,5353
    seen 1  meleer@2260,5347  ranger@2280,5346  mager@2279,5353
    seen 1  meleer@2280,5346  ranger@2279,5353  mager@2272,5330
    seen 1  meleer@2272,5330  ranger@2279,5353  mager@2273,5341
    seen 1  meleer@2258,5353  ranger@2273,5341  mager@2258,5330
wave 58
    seen 1  bat@2262,5335  meleer@2272,5330  ranger@2280,5333  mager@2258,5353
    seen 1  bat@2280,5346  meleer@2262,5335  ranger@2258,5330  mager@2273,5341
    seen 1  bat@2273,5341  meleer@2279,5353  ranger@2258,5330  mager@2258,5353
    seen 1  bat@2273,5341  meleer@2280,5346  ranger@2258,5353  mager@2280,5333
    seen 1  bat@2273,5341  meleer@2262,5335  ranger@2258,5330  mager@2260,5347
    seen 1  bat@2280,5333  meleer@2280,5346  ranger@2258,5353  mager@2279,5353
    seen 1  bat@2258,5353  meleer@2272,5330  ranger@2280,5333  mager@2280,5346
    seen 1  bat@2260,5347  meleer@2262,5335  ranger@2280,5346  mager@2280,5333
    seen 1  bat@2280,5346  meleer@2258,5353  ranger@2272,5330  mager@2273,5341
    seen 1  bat@2280,5346  meleer@2273,5341  ranger@2260,5347  mager@2280,5333
    seen 1  bat@2262,5335  meleer@2258,5353  ranger@2279,5353  mager@2260,5347
    seen 1  bat@2260,5347  meleer@2272,5330  ranger@2279,5353  mager@2258,5330
    seen 1  bat@2262,5335  meleer@2273,5341  ranger@2258,5353  mager@2272,5330
    seen 1  bat@2273,5341  meleer@2260,5347  ranger@2272,5330  mager@2258,5330
    seen 1  bat@2280,5333  meleer@2280,5346  ranger@2258,5330  mager@2272,5330
    seen 1  bat@2280,5346  meleer@2260,5347  ranger@2273,5341  mager@2272,5330
wave 59
    seen 1  bat@2258,5330  bat@2279,5353  meleer@2272,5330  ranger@2262,5335  mager@2260,5347
    seen 1  bat@2258,5353  bat@2279,5353  meleer@2280,5333  ranger@2260,5347  mager@2273,5341
    seen 1  bat@2260,5347  bat@2273,5341  meleer@2272,5330  ranger@2279,5353  mager@2280,5333
    seen 1  bat@2280,5333  bat@2280,5346  meleer@2258,5353  ranger@2273,5341  mager@2272,5330
    seen 1  bat@2262,5335  bat@2280,5333  meleer@2273,5341  ranger@2279,5353  mager@2280,5346
    seen 1  bat@2258,5353  bat@2279,5353  meleer@2280,5333  ranger@2258,5330  mager@2260,5347
    seen 1  bat@2258,5330  bat@2260,5347  meleer@2272,5330  ranger@2280,5333  mager@2280,5346
    seen 1  bat@2258,5330  bat@2280,5333  meleer@2258,5353  ranger@2262,5335  mager@2279,5353
    seen 1  bat@2258,5330  bat@2262,5335  meleer@2272,5330  ranger@2280,5333  mager@2258,5353
    seen 1  bat@2258,5330  bat@2279,5353  meleer@2262,5335  ranger@2280,5333  mager@2273,5341
    seen 1  bat@2258,5330  bat@2280,5346  meleer@2279,5353  ranger@2260,5347  mager@2262,5335
    seen 1  bat@2262,5335  bat@2272,5330  meleer@2280,5346  ranger@2258,5330  mager@2260,5347
    seen 1  bat@2279,5353  bat@2280,5333  meleer@2260,5347  ranger@2280,5346  mager@2258,5330
    seen 1  bat@2280,5333  bat@2280,5346  meleer@2258,5353  ranger@2262,5335  mager@2258,5330
    seen 1  bat@2258,5353  bat@2260,5347  meleer@2280,5333  ranger@2272,5330  mager@2262,5335
    seen 1  bat@2273,5341  bat@2280,5333  meleer@2280,5346  ranger@2262,5335  mager@2279,5353
wave 60
    seen 1  blob@2279,5353  meleer@2258,5330  ranger@2272,5330  mager@2280,5333
    seen 1  blob@2272,5330  meleer@2279,5353  ranger@2258,5353  mager@2280,5333
    seen 1  blob@2279,5353  meleer@2262,5335  ranger@2272,5330  mager@2280,5333
    seen 1  blob@2272,5330  meleer@2260,5347  ranger@2279,5353  mager@2280,5333
    seen 1  blob@2273,5341  meleer@2279,5353  ranger@2280,5333  mager@2280,5346
    seen 1  blob@2280,5333  meleer@2279,5353  ranger@2280,5346  mager@2258,5330
    seen 1  blob@2273,5341  meleer@2260,5347  ranger@2280,5346  mager@2279,5353
    seen 1  blob@2272,5330  meleer@2258,5330  ranger@2273,5341  mager@2280,5346
    seen 1  blob@2262,5335  meleer@2280,5346  ranger@2279,5353  mager@2258,5353
    seen 1  blob@2262,5335  meleer@2273,5341  ranger@2260,5347  mager@2279,5353
    seen 1  blob@2258,5353  meleer@2279,5353  ranger@2258,5330  mager@2280,5333
    seen 1  blob@2280,5333  meleer@2279,5353  ranger@2273,5341  mager@2262,5335
    seen 1  blob@2273,5341  meleer@2272,5330  ranger@2260,5347  mager@2258,5330
    seen 1  blob@2279,5353  meleer@2280,5333  ranger@2260,5347  mager@2280,5346
    seen 1  blob@2262,5335  meleer@2280,5346  ranger@2279,5353  mager@2280,5333
    seen 1  blob@2273,5341  meleer@2260,5347  ranger@2262,5335  mager@2272,5330
wave 61
    seen 1  bat@2262,5335  blob@2280,5346  meleer@2273,5341  ranger@2258,5330  mager@2279,5353
    seen 1  bat@2273,5341  blob@2280,5333  meleer@2279,5353  ranger@2280,5346  mager@2260,5347
    seen 1  bat@2262,5335  blob@2272,5330  meleer@2280,5346  ranger@2280,5333  mager@2273,5341
    seen 1  bat@2258,5330  blob@2273,5341  meleer@2272,5330  ranger@2262,5335  mager@2279,5353
    seen 1  bat@2272,5330  blob@2258,5353  meleer@2258,5330  ranger@2260,5347  mager@2262,5335
    seen 1  bat@2272,5330  blob@2260,5347  meleer@2279,5353  ranger@2262,5335  mager@2280,5346
    seen 1  bat@2280,5346  blob@2262,5335  meleer@2260,5347  ranger@2273,5341  mager@2280,5333
    seen 1  bat@2260,5347  blob@2279,5353  meleer@2280,5346  ranger@2273,5341  mager@2272,5330
    seen 1  bat@2279,5353  blob@2260,5347  meleer@2272,5330  ranger@2258,5353  mager@2258,5330
    seen 1  bat@2280,5346  blob@2262,5335  meleer@2280,5333  ranger@2279,5353  mager@2260,5347
    seen 1  bat@2280,5346  blob@2260,5347  meleer@2258,5353  ranger@2273,5341  mager@2279,5353
    seen 1  bat@2262,5335  blob@2258,5353  meleer@2272,5330  ranger@2280,5333  mager@2273,5341
    seen 1  bat@2262,5335  blob@2258,5353  meleer@2260,5347  ranger@2279,5353  mager@2280,5333
    seen 1  bat@2280,5333  blob@2258,5353  meleer@2260,5347  ranger@2273,5341  mager@2272,5330
    seen 1  bat@2272,5330  blob@2280,5333  meleer@2258,5330  ranger@2258,5353  mager@2273,5341
    seen 1  bat@2280,5346  blob@2258,5353  meleer@2273,5341  ranger@2258,5330  mager@2262,5335
wave 62
    seen 1  bat@2258,5330  bat@2280,5333  blob@2258,5353  meleer@2262,5335  ranger@2280,5346  mager@2273,5341
    seen 1  bat@2258,5353  bat@2280,5346  blob@2279,5353  meleer@2262,5335  ranger@2273,5341  mager@2280,5333
    seen 1  bat@2272,5330  bat@2279,5353  blob@2280,5346  meleer@2258,5330  ranger@2280,5333  mager@2262,5335
    seen 1  bat@2272,5330  bat@2279,5353  blob@2262,5335  meleer@2258,5330  ranger@2280,5346  mager@2280,5333
    seen 1  bat@2258,5330  bat@2279,5353  blob@2258,5353  meleer@2272,5330  ranger@2273,5341  mager@2280,5346
    seen 1  bat@2260,5347  bat@2280,5333  blob@2280,5346  meleer@2258,5330  ranger@2279,5353  mager@2262,5335
    seen 1  bat@2260,5347  bat@2280,5333  blob@2272,5330  meleer@2262,5335  ranger@2280,5346  mager@2258,5330
    seen 1  bat@2272,5330  bat@2273,5341  blob@2260,5347  meleer@2280,5333  ranger@2258,5330  mager@2279,5353
    seen 1  bat@2272,5330  bat@2280,5333  blob@2258,5353  meleer@2280,5346  ranger@2273,5341  mager@2260,5347
    seen 1  bat@2280,5333  bat@2280,5346  blob@2260,5347  meleer@2272,5330  ranger@2258,5330  mager@2258,5353
    seen 1  bat@2260,5347  bat@2279,5353  blob@2272,5330  meleer@2280,5346  ranger@2273,5341  mager@2258,5353
    seen 1  bat@2258,5353  bat@2262,5335  blob@2258,5330  meleer@2279,5353  ranger@2280,5333  mager@2280,5346
    seen 1  bat@2260,5347  bat@2262,5335  blob@2258,5330  meleer@2279,5353  ranger@2272,5330  mager@2273,5341
    seen 1  bat@2260,5347  bat@2280,5346  blob@2273,5341  meleer@2258,5353  ranger@2258,5330  mager@2272,5330
    seen 1  bat@2260,5347  bat@2280,5346  blob@2280,5333  meleer@2279,5353  ranger@2273,5341  mager@2272,5330
wave 63
    seen 1  blob@2260,5347  blob@2262,5335  meleer@2280,5346  ranger@2258,5330  mager@2280,5333
    seen 1  blob@2273,5341  blob@2280,5333  meleer@2280,5346  ranger@2272,5330  mager@2258,5353
    seen 1  blob@2262,5335  blob@2280,5346  meleer@2258,5330  ranger@2272,5330  mager@2279,5353
    seen 1  blob@2258,5353  blob@2280,5346  meleer@2262,5335  ranger@2258,5330  mager@2279,5353
    seen 1  blob@2279,5353  blob@2280,5333  meleer@2258,5353  ranger@2280,5346  mager@2260,5347
    seen 1  blob@2273,5341  blob@2280,5333  meleer@2272,5330  ranger@2258,5353  mager@2279,5353
    seen 1  blob@2258,5330  blob@2273,5341  meleer@2260,5347  ranger@2280,5333  mager@2279,5353
    seen 1  blob@2258,5330  blob@2262,5335  meleer@2272,5330  ranger@2280,5333  mager@2258,5353
    seen 1  blob@2262,5335  blob@2279,5353  meleer@2280,5333  ranger@2272,5330  mager@2273,5341
    seen 1  blob@2258,5330  blob@2273,5341  meleer@2262,5335  ranger@2272,5330  mager@2280,5333
    seen 1  blob@2258,5353  blob@2280,5333  meleer@2262,5335  ranger@2260,5347  mager@2272,5330
    seen 1  blob@2272,5330  blob@2279,5353  meleer@2260,5347  ranger@2280,5333  mager@2258,5353
    seen 1  blob@2258,5330  blob@2279,5353  meleer@2262,5335  ranger@2280,5333  mager@2258,5353
    seen 1  blob@2258,5353  blob@2272,5330  meleer@2273,5341  ranger@2258,5330  mager@2260,5347
    seen 1  blob@2260,5347  blob@2272,5330  meleer@2273,5341  ranger@2280,5346  mager@2280,5333
wave 64
    seen 2  meleer@2273,5341  meleer@2279,5353  ranger@2280,5333  mager@2272,5330
    seen 1  meleer@2273,5341  meleer@2280,5346  ranger@2272,5330  mager@2262,5335
    seen 1  meleer@2262,5335  meleer@2279,5353  ranger@2260,5347  mager@2280,5333
    seen 1  meleer@2280,5333  meleer@2280,5346  ranger@2279,5353  mager@2273,5341
    seen 1  meleer@2258,5353  meleer@2279,5353  ranger@2262,5335  mager@2280,5333
    seen 1  meleer@2258,5353  meleer@2280,5346  ranger@2260,5347  mager@2280,5333
    seen 1  meleer@2262,5335  meleer@2280,5346  ranger@2258,5330  mager@2272,5330
    seen 1  meleer@2258,5330  meleer@2280,5333  ranger@2272,5330  mager@2279,5353
    seen 1  meleer@2280,5333  meleer@2280,5346  ranger@2258,5353  mager@2273,5341
    seen 1  meleer@2258,5353  meleer@2280,5346  ranger@2258,5330  mager@2279,5353
    seen 1  meleer@2262,5335  meleer@2272,5330  ranger@2258,5353  mager@2260,5347
    seen 1  meleer@2262,5335  meleer@2280,5346  ranger@2272,5330  mager@2279,5353
    seen 1  meleer@2262,5335  meleer@2273,5341  ranger@2280,5333  mager@2272,5330
    seen 1  meleer@2258,5330  meleer@2280,5333  ranger@2279,5353  mager@2258,5353
wave 65
    seen 1  ranger@2262,5335  ranger@2280,5346  mager@2258,5353
    seen 1  ranger@2260,5347  ranger@2272,5330  mager@2280,5346
    seen 1  ranger@2272,5330  ranger@2280,5346  mager@2262,5335
    seen 1  ranger@2258,5330  ranger@2273,5341  mager@2272,5330
    seen 1  ranger@2262,5335  ranger@2280,5346  mager@2260,5347
    seen 1  ranger@2258,5330  ranger@2262,5335  mager@2258,5353
    seen 1  ranger@2262,5335  ranger@2280,5333  mager@2260,5347
    seen 1  ranger@2258,5353  ranger@2280,5333  mager@2262,5335
    seen 1  ranger@2258,5353  ranger@2262,5335  mager@2280,5333
    seen 1  ranger@2260,5347  ranger@2280,5333  mager@2280,5346
    seen 1  ranger@2258,5353  ranger@2280,5346  mager@2262,5335
    seen 1  ranger@2279,5353  ranger@2280,5346  mager@2273,5341
    seen 1  ranger@2258,5330  ranger@2273,5341  mager@2280,5346
    seen 1  ranger@2258,5330  ranger@2280,5333  mager@2262,5335
    seen 1  ranger@2280,5333  ranger@2280,5346  mager@2258,5330
wave 66
    seen 3  mager@2279,5353  mager@2280,5333
    seen 2  mager@2262,5335  mager@2280,5346
    seen 1  mager@2280,5333  mager@2280,5346
    seen 1  mager@2260,5347  mager@2272,5330
    seen 1  mager@2260,5347  mager@2273,5341
    seen 1  mager@2258,5330  mager@2273,5341
    seen 1  mager@2258,5353  mager@2272,5330
    seen 1  mager@2258,5353  mager@2279,5353
    seen 1  mager@2258,5330  mager@2279,5353
    seen 1  mager@2272,5330  mager@2279,5353
    seen 1  mager@2262,5335  mager@2279,5353
