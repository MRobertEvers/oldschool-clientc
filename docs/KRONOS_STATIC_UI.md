# Kronos static UI reference

Reference for the OSRS HUD as documented in [Kronos184-Client](https://github.com/Kronos184/Kronos184-Client) (RuneLite fork). Pixel coordinates live in the dat2 interface cache; this doc records widget IDs, sprite archive IDs, and tab indices used to build [revconfig](../src/osrs/revconfig/) INI files.

## Viewport shells

| Mode | Canvas | Interface group | Viewport child | Selection |
|------|--------|-----------------|----------------|-----------|
| Fixed | 765×503 | **548** | child **17** | `!isResized()` |
| Resizable OSRS box | stretches | **161** | child **12** | varbit `SIDE_PANELS` (4607) != 1 |
| Resizable bottom line | stretches | **164** | child **12** | varbit `SIDE_PANELS` (4607) == 1 |

Sources: `WidgetID.java`, `Constants.GAME_FIXED_SIZE`, `Client.getViewportWidget()`.

## Builtin regions → revconfig `type=`

| Region | Widget type | revconfig | Fixed (548) | Resizable |
|--------|-------------|-----------|-------------|-----------|
| Game world | VIEWPORT (1337) | `world` | 17 | 161:12 / 164:12 |
| Minimap | MINIMAP (1338) | `minimap` | 3, draw 8 | 17–28 |
| Compass | COMPASS (1339) | `compass` | in minimap frame | sprite 169 |
| Sidebar host | container | `sidebar` | 65 | 161:65 / 164:71 |
| Chat | group 162 | `chat` | separate overlay at `(0,0)` | separate overlay at `(0,0)` |
| Minimap orbs | group 160 | (future) | on minimap | on minimap |

## 14-tab sidebar (`InterfaceTab` 0–13)

**Kronos client (`--kronos`):** Sidebar `componentno` values in `rev_kronos_ui.ini` match Kronos server `DisplayHandler.sendDisplay()` (screen 165 children 8–21). Use `--kronos` (implies `--dat2` format); cache resolves to `<repo-root>/cache.kronos/` by walking up from the current working directory. Generic dat2 uses `rev_245_2_dat2_ui.ini` and `cache/`.

**LostCity cache:** When using the LostCity `interface.pack` (rev_245_2 dat1), sidebar `componentno` values in `rev_245_2_dat1_ui.ini` use LostCity flat wire IDs from `if_settab()` in `login.rs2` (e.g. stats=3917, questlist=638, inventory=3213).

| tabno | Name | Panel iface | Fixed tab / icon | RS2 sideicon sprite |
|-------|------|-------------|------------------|----------------------|
| 0 | Combat | 593 | 48 / 55 | 774 |
| 1 | Stats | 320 | 49 / 56 | 775 |
| 2 | Quest | 720 | 50 / 57 | 776 |
| 3 | Inventory | 149 | 51 / 58 | 777 |
| 4 | Equipment | 387 | 52 / 59 | 778 |
| 5 | Prayer | 541 | 53 / 60 | 779 |
| 6 | Spellbook | 218 | 54 / 61 | 780 |
| 7 | Clan | 7 | 31 / 38 | 781 |
| 8 | Account | 720 | — | dynamic |
| 9 | Friends | 429 | 33 / 40 | 782 |
| 10 | Logout | 182 | 34 / 41 | 784 |
| 11 | Options | 261 | 35 / 42 | 785 |
| 12 | Emotes | 216 | 36 / 43 | 786 |
| 13 | Music | 239 | 37 / 44 | 787 |

### Resizable box (161) tab / icon children

| tabno | Tab child | Icon child |
|-------|-----------|------------|
| 0–6 | 51–57 | 58–64 |
| 7 | 35 | 42 |
| 8 | — | — |
| 9 | 37 | 44 |
| 10 | 38 | 45 |
| 11 | 39 | 46 |
| 12 | 40 | 47 |
| 13 | 41 | 48 |

### Resizable bottom line (164) tab / icon children

| tabno | Tab child | Icon child |
|-------|-----------|------------|
| 0 | 50 | 57 |
| 1 | 51 | 58 |
| 2 | 52 | 59 |
| 3 | 53 | 60 |
| 4 | 54 | 61 |
| 5 | 55 | 62 |
| 6 | 56 | 63 |
| 7 | 35 | 41 |
| 8 | — | — |
| 9 | 37 | 43 |
| 10 | — | — |
| 11 | 38 | 44 |
| 12 | 39 | 45 |
| 13 | 40 | 46 |

## Tab highlight stones (modern)

| Role | SpriteID | WidgetOverride (fixed) |
|------|----------|------------------------|
| Top-left | 1026 | combat tab |
| Top-right | 1027 | magic tab |
| Bottom-left | 1028 | clan tab |
| Bottom-right | 1029 | music tab |
| Middle | 1030 | stats/quests/inv/equip/prayer, friends/ignores/logout/options/emotes |
| Resizable middle | 1180 / 1181 | resizable modes |

## Panel sprites

| Logical | Fixed SpriteID | Resizable SpriteID |
|---------|----------------|-------------------|
| Side panel BG | 1031 | 897 |
| Top tab row | 1036 | 1173 |
| Bottom tab row | 1032 | 1174 |
| Minimap frame | 1182 | 1177 |
| Minimap alpha mask | 1183 | 1178 |
| Compass alpha mask | 1184 | 1179 |
| Chat background | 1017 | 1017 |
| Inventory backing | 297 (tradebacking) | 297 |
| Compass texture | 169 | 169 |

## Related interface groups

| Group | Role |
|-------|------|
| 160 | Minimap orbs (HP, prayer, run, spec, world map) |
| 162 | Chatbox — mount filter buttons via `chat_region` `componentno=10616833` (`(162<<16)|1`, `WidgetID.Chatbox.BUTTONS`) at `(0,0)`; full iface 162 includes script-driven modal/message layers that must not be baked for static HUD |
| 593, 320, 399, 149, 387, 541, 218, 7, 429, 182, 216, 239 | Sidebar panel content |

## Revconfig mapping

- Components are shared across modes; layout groups select coordinates:
  - `[layout:fixed]` — 765×503 shell
  - `[layout:resizable_box]` — group 161 layout
  - `[layout:resizable_bottom]` — group 164 layout
- Sprites use symbolic names + `archive_id=` in `rev_kronos_ui_cache.ini`
- Coordinates are extracted with `tools/dump_interface_layout` from the local dat2 cache

## Map XTEA keys (`xteas.json`)

XTEA keys decrypt **map scenery** archives (`l*_*` in dat2 table 5) only. They are **not** used for interfaces, sprites, fonts, or the static Kronos HUD.

`cache.kronos/` does not ship `xteas.json`. Kronos stores region keys in `kronos-server/data/region_keys.json` as `{ "<regionId>": [k0,k1,k2,k3], ... }`. The client expects an array keyed by map archive index (`group`):

```json
[{ "archive": 5, "group": 1738, "name_hash": -1156005173, "name": "l18_54", "mapsquare": 4662, "key": [...] }]
```

`group` indices are **cache-specific** — generate from `cache.kronos`, not generic `cache/xteas.json`.

### Generate `cache.kronos/xteas.json`

```bash
cmake --build build --target dump_map_index

python3 tools/gen_kronos_xteas.py \
  --region-keys /path/to/kronos-server/data/region_keys.json \
  --cache cache.kronos \
  --out cache.kronos/xteas.json
```

If `xteas.json` is missing at startup, dat2 init warns and continues (UI works; encrypted map scenery will not decrypt).
