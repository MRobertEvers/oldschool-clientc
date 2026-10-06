# Research notes

Facts gathered 2026-09-04 that the implementation depends on. Keep this current when something is re-verified.

## Reference target: scim.gg

- React SPA, custom WebGL2 renderer, loads the real OSRS cache in the browser (OpenRS2 cache 2671, build 240, trimmed to 9.85 MB). Only Yama P3 is live; TzKal-Zuk and Sol Heredit were withdrawn on 19 Aug 2026 and their logic is not in the shipped bundle.
- Plays fully without its backend. Backend (Express) only provides accounts, profile sync, replay sharing and a hiscores proxy.
- UI: RuneLite-style plugins, resource packs (RuneLite Resource Packs), Fixed / Resizable-Classic / Resizable-Modern layouts, replays with cursor, training courses, encounter configure screen (loadout presets, paper-doll gear editor, levels / hiscores import, mechanics toggles).

## OpenRS2 archive

- API doc: https://archive.openrs2.org/api. All endpoints send `Access-Control-Allow-Origin: *`. No range requests.
- List: `GET https://archive.openrs2.org/caches.json`
- Cache: `GET /caches/runescape/<id>/disk.zip` (dat2 + idx files under `cache/`), `.../flat-file.tar.gz`, `.../keys.json`
- Single group: `GET /caches/runescape/<id>/archives/<index>/groups/<group>.dat`
- Cache 2671: build 240, 2026-08-26, 25 indexes, 117,214 groups, 190,433,165 bytes. Newest at time of writing: 2686 (2026-09-02).
- **XTEA is gone**: every OSRS live cache from build 237 (cache 2504, 2026-03-25) has zero encrypted groups. Map groups in index 5 are addressed numerically as `(mapX << 8) | mapY`; file 0 = terrain, file 1 = locs. Group `(98 << 8) | 199` is a special world-area group to skip. Older caches use named groups `m{X}_{Y}` / `l{X}_{Y}` with XTEA on the `l` groups.

## Cache layout (verified against RuneLite IndexType / ConfigType)

Indexes: 0 frames, 1 frame maps (bases), 2 configs, 3 interfaces, 4 sound effects, 5 maps, 6 music tracks, 7 models, 8 sprites, 9 textures, 10 binary, 11 jingles, 12 clientscripts, 13 fonts, 14 music samples, 15 music patches, 17 graphic defaults, 18 world map geography, 19 world map, 20 world map ground, 21 dbtable index, 22 animaya (skeletal keyframes, OSRS 229+), 24 gamevals.

Configs (index 2) archives: 1 underlay, 3 identkit, 4 overlay, 5 inv, 6 object (loc), 8 enum, 9 npc, 10 item, 11 params, 12 sequence, 13 spotanim, 14 varbit, 15 varcstring, 16 varplayer, 19 varclient, 32 hitsplat, 33 healthbar, 34 struct, 35 area, 38 dbrow, 39 dbtable.

Frame ids are `(frameMapId << 16) | frameId`; sequences reference them.

## Open-source code we can lean on

| Project | Licence | Gives us |
| --- | --- | --- |
| dennisdev/rs-map-viewer (osrs.world) | BSD-2-Clause | TypeScript decoders for models, sequences, frames, skeletal animation, npc/loc/obj/underlay/overlay configs, maps (terrain + locs), sprites, textures; WebGL2 scene builder, pathfinder. Closest thing to what we build. |
| Dezinater/osrscachereader | BSD-2-Clause | JS loaders incl. Model/Frames/Framemap/Animaya, GLTF export. Handy for verifying our decoders. |
| abextm/cache2 | BSD-2-Clause | Config loaders (npc, item, obj, sequence...), own bzip2 port. No model/map decoders. |
| RuneLite `cache` module | BSD-2-Clause | Canonical Java loaders; the ground truth when formats disagree. |
| OldSchoolSDK/osrs-sdk, InfernoTrainer | GPL-3.0 | Behavioural reference for Inferno/Zuk only. Do not copy code. |

BSD-2 requires keeping the copyright notice: see `NOTICE.md` when any code is ported.

## Inferno identifiers

- Region 9043 = `(35 << 8) | 83`, tiles x 2240-2303, y 5312-5375.
- NPC ids: Jal-Nib 7691, Jal-MejRah 7692, Jal-Ak 7693, Jal-AkRek-Mej 7694, Jal-AkRek-Xil 7695, Jal-AkRek-Ket 7696, Jal-ImKot 7697, Jal-Xil 7698 (final wave 7702), Jal-Zek 7699 (final wave 7703), JalTok-Jad 7700 (final wave 7704), Yt-HurKot 7701 (final wave 7705), TzKal-Zuk 7706, Ancestral Glyph 7707, Jal-MejJak 7708, Rocky support 7709 / 7710 (dying), TzHaar-Ket-Keh 7690.
- Zuk / arena sequences: ZUK_DEATH 7562, ZUK_SPAWN 7563, ZUK_IDLE 7564, ZUK_DEFEND 7565, ZUK_ATTACK 7566, ZUK_PROJ 7571, glyph READY/HIT/DEATH 7567/7568/7569, pillar collapse 7561, collapsing wall right/left 7559/7560, Jal-Nib walk/ready 7572/7573.
- Player sequences: blowpipe attack 5061 (spec 876), tbow / crossbow PvM fire 7552 (4230 non-PvM), ZCB attack 9168 (9166), bow 426, stance ready 808, walk F/B/L/R 819/820/821/822, turn 823, run 824, death 836; crossbow stance 4591, walk 4226/4227, run 4228. Weapon stances otherwise come from item config params.
