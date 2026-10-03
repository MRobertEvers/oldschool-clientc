# osrs_sdk: OldSchoolSDK/osrs-sdk (the engine InfernoTrainer runs on)

Pinned 2026-10-03 by the waves loop corpus worker (`corpus.code`). Entry in `../LEDGER_code.md`; every constant below is a row of `../CODE_CONSTANTS.md` (the row id is in the first column).

* **Repository**: https://github.com/OldSchoolSDK/osrs-sdk
* **Commit**: 04fdaee3d155238e54cf16c1ac259f6c2b210078 (committed 2026-08-29, package version 0.1.8). The trainer pins `osrs-sdk` 0.1.4 (version bump commit 9b80ab4, 2025-10-24). `git diff 9b80ab4 HEAD` shows no change in LineOfSight.ts, Pathing.ts, Mob.ts, Unit.ts, Collision.ts, Projectile.ts: the copied files are the 0.1.4 files.
* **Licence**: GPL-3.0 (`LICENSE`); package.json says ISC.
* **What it is for**: the trainer's engine.
* **Files copied** (byte for byte, licence header intact): `src/sdk/` LineOfSight.ts (the line-of-sight algorithm and masks), Collision.ts, Pathing.ts, Mob.ts (NPC movement, attack gating, melee-if-close), Unit.ts (retaliation / flinch delay, spawnDelay age), `weapons/` MagicWeapon.ts, RangedWeapon.ts, MeleeWeapon.ts (hit delay formulas), `gear/Weapon.ts` (max hit and accuracy formulas).
* **What it credits as its own sources**: LineOfSight.ts says: "this entire file is lifted and modified ... This algorithm makes no sense and is copy pasta'd between basically every trainer ... Woox made a video that helps people understand it" (https://www.youtube.com/watch?v=vnyNLXTwjCE). No other credits.

## Constants this source states (name, value, file and line)

The value column is the whole row of `../CODE_CONSTANTS.md`; a source often states only part of it (the notes below and the row's last column say which part).

| row | quantity | value | file:line (relative to this directory) |
|---|---|---|---|
| C037 | Jal-Ak: melee when the player is adjacent | crush, 50% of attacks while within melee distance (diagonals count) | `src/sdk/Mob.ts:340` |
| C048 | Jal-Xil: projectile hit delay | trainer: SDK ranged formula floor((3+d)/6)+1 then +2 (reduceDelay -2); AUTOZUK calibrated table by distance from the 3x3 centre: d1-5 3, d6-9 4, d10-12 5, d13+ 6 (hit tick, attack tick = 1) | `src/sdk/weapons/RangedWeapon.ts:25` |
| C059 | JalTok-Jad: style choice | 50% ranged / 50% magic; melee (stab) 50% of attacks while adjacent | `src/sdk/Mob.ts:340` |
| C086 | line of sight: algorithm and masks | Bresenham-style, 16.16 fixed point; masks NORTH 0x400, EAST 0x1000, SOUTH 0x4000, WEST 0x10000, FULL 0x20000; an NPC's LoS is tested from the player to the closest footprint tile; range 1 = melee adjacency | `src/sdk/LineOfSight.ts:19`, `src/sdk/LineOfSight.ts:68` |
| C087 | movement: NPC step rule | one step a tick toward the player by sign(dx), sign(dy); if the destination footprint overlaps the player keep y (corner safespot); under the player, a random cardinal sidestep; no move while attackDelay > attackSpeed (after a dig); frozen or stunned block movement | `src/sdk/Mob.ts:110`, `src/sdk/Mob.ts:144`, `src/sdk/Mob.ts:149` |
| C088 | combat: hit delay formulas (engine; NPC projectiles add their own delay) | magic floor((1+d)/3)+1; ranged floor((3+d)/6)+1 | `src/sdk/weapons/MagicWeapon.ts:22`, `src/sdk/weapons/RangedWeapon.ts:25` |
| C089 | combat: retaliation delay | a retaliating NPC waits floor(speed/2)+1 ticks (Jad flinchDelay 2) | `src/sdk/Unit.ts:363` |
| C090 | combat: NPC max hit and defence roll | max = floor((effective * (bonus + 64) + 320) / 640), effective = level + 9; defence roll (def + 9) * (bonus + 64) | `src/sdk/gear/Weapon.ts:204` |

## Notes

* States no Inferno constant; it is cited for line of sight, NPC movement and the hit-delay / max-hit formulas the trainer relies on.
