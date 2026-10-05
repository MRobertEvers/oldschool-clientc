# openosrs_inferno: karankurbur/OpenOSRSPlugins, `inferno` plugin (the OpenOSRS Inferno plugin, 2020)

Pinned 2026-10-03 by the waves loop corpus worker (`corpus.code`). Entry in `../LEDGER_code.md`; every constant below is a row of `../CODE_CONSTANTS.md` (the row id is in the first column).

* **Repository**: https://github.com/karankurbur/OpenOSRSPlugins
* **Commit**: 904639cfdeaef33e724c872118e005e8967eadfd (committed 2020-08-18)
* **Licence**: GPL-3.0 repository (`LICENSE`); the Inferno files keep their BSD-2 upstream headers (Devin French 2017, Jacky 2019, Kyleeld 2019).
* **What it is for**: same as kotori (its ancestor).
* **Files copied** (byte for byte, licence header intact): `inferno/src/main/java/net/runelite/client/plugins/inferno/` InfernoNPC.java, InfernoPlugin.java, InfernoWaveMappings.java, InfernoSpawnTimerInfobox.java. Not copied: overlays, config. The official open-osrs/plugins repository (9e680b5e, 2022) no longer carries the plugin; this fork of it does. A second copy, Dirro/osrs-plugins (b13d786e, 2020-05-01), differs in the same files and was examined, not copied.
* **What it credits as its own sources**: Nothing is cited. Uses the RuneLite `AnimationID` / `NpcID` names, so its numeric ids come from the RuneLite constants of the day (the kotori copy states the numbers literally and is the one cited for ids).

## Constants this source states (name, value, file and line)

The value column is the whole row of `../CODE_CONSTANTS.md`; a source often states only part of it (the notes below and the row's last column say which part).

| row | quantity | value | file:line (relative to this directory) |
|---|---|---|---|
| C001 | arena: region id | 9043 (Inferno interior) | `inferno/src/main/java/net/runelite/client/plugins/inferno/InfernoPlugin.java:77` |
| C010 | wave table: waves 1-66: monsters per wave (nibbler, bat, blob, melee, ranger, mager) | w1 3,1,0,0,0,0 ... w66 3,0,0,0,0,2; nibblers 3 except 6 on waves 3, 8, 17, 34 | `inferno/src/main/java/net/runelite/client/plugins/inferno/InfernoWaveMappings.java:51` |
| C043 | Jal-ImKot: dig and resurface timing | 6 ticks frozen digging; on resurfacing attackDelay 6 and frozen 2; observers use 12 ticks from the burrow animation to the next attack | `inferno/src/main/java/net/runelite/client/plugins/inferno/InfernoNPC.java:261` |
| C057 | JalTok-Jad: attack speed | 8 on wave 67 and the Zuk-wave Jad; 9 on wave 68; kotori: 8 after the animation (6 with its sixTickJad option) | `inferno/src/main/java/net/runelite/client/plugins/inferno/InfernoNPC.java:221` |
| C070 | TzKal-Zuk: attack speed | 10; 7 when enraged (below 240 hp) | `inferno/src/main/java/net/runelite/client/plugins/inferno/InfernoNPC.java:208` |
| C082 | Ancestral Glyph (shield): pause at each end | trainer freeze(5) on reaching an end (x < 11 or x > 35); kotori ticksLeftInCorner 4 | `inferno/src/main/java/net/runelite/client/plugins/inferno/InfernoPlugin.java:759` |

## Notes

* Same constants as kotori at the lines cited in the rows; OpenOSRS names (`JALNIB`, `JALMEJRAH`, `JALAK`, `JALIMKOT`, `JALXIL(_7702)`, `JALZEK(_7703)`, `JALTOKJAD(_7704)`, `YTHURKOT(_7701/_7705)`, `TZKALZUK`, `JALMEJJAK`, `ANCESTRAL_GLYPH`) at `InfernoNPC.java:366-375` and `InfernoPlugin.java:313,390`.
