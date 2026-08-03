# Model lighting — two regimes, era divergences, manifest overrides

> Written 2026-08-03. Owns the actor/scene light split and the two places
> xrsps-typescript diverges from Client-TS. Call sites live under
> `3rd/toridraw/toridraw_light_model.*` and the build paths in `src/app.c`,
> `src/engine/entity_model_build.c`, `src/engine/uitree_scene_bridge.c`,
> and `src/engine/world_builder/`.

## Two regimes

Both Client-TS and xrsps-typescript light models with
`Model.calculateNormals` / `ModelData.light` using two constant sets:

| Regime | ambient | attenuation | light (X,Y,Z) | Used for |
|--------|---------|-------------|---------------|----------|
| **Actor** | 64 | 850 | (-30, -50, -30) | world players/NPCs, spotanims, projectiles; xrsps chatheads |
| **Scene** | 64 | 768 | (-50, -10, -50) | locs, ground objs, widget models, sharelight, Client-TS IF chatheads |

torirs encodes them as process-wide profiles in
`ToriDraw_LightModelActor` / `ToriDraw_LightModelScene`
(`3rd/toridraw/toridraw_light_model.c`). Signed per-type ambient/contrast
offsets are added onto the regime base; contrast arrives already pre-scaled
from the config decoder (npc/obj ×5, loc ×5 dat1 / ×25 dat2).

Before this split, every path called the scene regime — including world NPCs
and players — so actor models were shaded with the wrong direction and
attenuation. The character-design preview was the only site that already
passed `(64, 850, -30, -50, -30)`.

## Interface chatheads

Client-TS builds NPC/player head composites unlit (`NpcType.getHead` /
`ClientPlayer.getHeadModel`), then `IfType.getTempModel` lights every IF model
with the **scene** regime. torirs has no draw-time re-light, so
`UITreeSceneBridge_EnsureNpcHead` / `EnsurePlayerHead` bake at build time.

xrsps disagrees: `ChatheadFactory` uses actor + NpcType ambient/contrast;
`PlayerChatheadFactory` uses absolute ambient 128 with actor dir/atten.

## Where xrsps and Client-TS disagree

Only two things; both are era-table fields whose **zero is Client-TS**:

| Field | 0 (Client-TS / lostcity / osrs) | non-zero (xrsps / `server_routed`) |
|-------|----------------------------------|-------------------------------------|
| `npc_light_uses_type_ambient_contrast` | world NPC bodies ignore opcodes 100/101; IF NPC heads use Scene | ambient += npctype.ambient, contrast += npctype.contrast (bodies + IF heads use Actor) |
| `player_head_light_ambient` | IF player head uses Scene | absolute ambient when lighting the head with actor dir (128) |

Defined on `struct ToriRS_FeatureTable` (`src/features/features.h`).
`App_Init` copies them onto `App` / `UITreeSceneBridge` so a
`[render:light]` override can win without mutating the const era table.

## Manifest `[render:light]`

Optional escape hatch on any `manifest_*.ini`. Absent keys keep the
compiled-in / era defaults. Keys:

```
[render:light]
actor_ambient=64
actor_attenuation=850
actor_light=-30,-50,-30
scene_ambient=64
scene_attenuation=768
scene_light=-50,-10,-50
npc_type_ambient_contrast=0   ; or 1
player_head_ambient=0         ; or 128
```

Parsed by `src/bootmanifest/bootmanifest.c`, applied in `App_Init` via
`ToriDraw_LightSetProfiles` and the effective behaviour ints on `App`.

## Call-site map

| Path | Regime |
|------|--------|
| `app_world_build_model` (NPC spawn / CHANGE_TYPE) | Actor (+ type offsets when enabled) |
| `app_world_build_model` (projectile) | Actor |
| `app_world_build_model` (ground obj) | Scene (+ obj ambient/contrast) |
| `app_world_build_spotanim_model` | Actor (+ spot ambient/contrast) |
| `PlayerModel_BuildFromAppearance` | Actor |
| `PlayerHeadModel_BuildFromAppearance` | unlit; bridge always lights |
| `UITreeSceneBridge_EnsureNpcHead` | Scene (Client-TS); Actor + type offsets when era flag on |
| `UITreeSceneBridge_EnsurePlayerHead` | Scene when `player_head_light_ambient==0`; else absolute ambient + actor dir |
| Widget cache models / obj icons / obj 3D | Scene |
| Loc defaultlight / sharelight / runtime spawn | Scene |

## Tests

- `make -C src test-bootmanifest` — parses `[render:light]` including a
  negative light component.
- `make -C src test-light-model` — actor and scene bake different
  `face_colors_a`; `LightSetProfiles` changes the bake.
