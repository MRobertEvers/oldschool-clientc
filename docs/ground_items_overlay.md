# The ground-items overlay, and its settings

The rev-239 cache draws the names of the items lying on a tile over that tile,
coloured by what they are worth, with two optional countdowns and a pair of
highlight/ignore buttons. All of it is a cache script; none of it worked,
because the eight opcodes it reads its input from were not implemented and the
five settings that decide what it draws could not be typed into.

This is what landed, layer by layer, and where each part lives.

---

## 1. What the cache already had

`[clientscript,torirs_ground_items_overlay]` (script **7227**) is the whole
renderer. Given a packed absolute coord it:

1. asks how many objs are on the tile, and destroys its overlay if that is zero;
2. walks them newest-first, merging runs of the same obj id;
3. drops any the settings say to hide -- an ignore-list entry, an untradeable
   with "hide untradeables" on, someone else's loot, anything under every price
   threshold;
4. sorts what is left into six colour bands by value;
5. lays each survivor out as a row on a **coord-anchored entity overlay**
   (`overlay_coord_create`, the `_7200` family in `game/rs_entity_overlay.c`),
   with an optional despawn countdown, an optional visibility countdown, and --
   in edit mode -- a highlight and an ignore button per row.

`[clientscript,torirs_ground_items_overlay_hook]` (**7226**) is the no-argument
form: it reads the tile from `_6950` and calls 7227. That is the one the client
fires.

The settings it reads:

| what | var | where the player sets it |
|---|---|---|
| overlay on/off | `ground_items_enabled` (varbit 14869) | All Settings row 321, and the hotkey bar |
| price source | `ground_items_price_type` (14870) | row 333 |
| edit mode | `ground_items_edit_mode_enabled_desktop/mobile` (14871/14872) | rows 338 / 339 |
| despawn timer | `ground_items_despawn_time_enabled_*` (14873/14874) | rows 334 / 335 |
| visibility timer | `ground_items_visibility_time_enabled_*` (14875/14876) | rows 336 / 337 |
| modifier key | `ground_items_modifier_key` (14877) | row 340 |
| hide the price | `ground_items_display_price_disabled` (14885) | row 346 |
| hide untradeables | `ground_items_untradeable_items_disabled` (9606) | row 366 |
| rows to draw | `ground_items_max_lines` (15021) | row 350 |
| five price tiers | `ground_items_t1..t5_threshold` (varps 3804..3800) | rows 332..328 |
| six colours | `ground_items_t1..t5_colour`, `ground_items_custom_colour` (varps 3809..3805, 3810) | rows 326..322, 327 |

The varbits ride two carriers, both already declared server-side:
`settings_varp_ehc_4` (`interface_hiscores/configs/hiscores.varp`) and
`settings_varp_ehc_5` (`interface_loottools/configs/loottools.varp`).

---

## 2. The eight opcodes (the input)

All eight used to fall through to `CS2VM2_Op_StackMetaStub`, which balances the
stack from the generated table and fakes a zero. A faked zero for
`OBJSTACK_COUNT` is "the tile is empty", so the overlay was not visibly broken:
it was invisibly absent.

Semantics were read off the rev-216 native client
(`~/Documents/git_repos/osclient_decompile/osclient-216-mac.c`,
`ScriptRunnerImpl_6800To6899.cpp` and `_7100To7199.cpp`) rather than guessed
from the call sites. See the memory note `osrs-cpp-client-decompile`.

| opcode | name | shape | answer |
|---|---|---|---|
| 7120 | `OBJSTACK_COUNT` | `(coord) -> int` | `Client::GetObjectsOnTile(coord).size()`, 0 outside the build area |
| 7121 | `OBJSTACK_ID` | `(coord, index) -> obj` | the entry's obj id, `-1` past the end |
| 7122 | `OBJSTACK_QUANTITY` | `(coord, index) -> int` | its count, `-1` past the end |
| 6859 | `OBJ_FIND` | `(coord, index) -> bool` | selects the entry (`SetActiveObj`) and says whether it found one |
| 6860 | `OBJ_DESPAWNTIME` | `() -> int` | game ticks until the **selected** entry vanishes |
| 6861 | `OBJ_VISIBLETIME` | `() -> int` | game ticks until everyone can see it |
| 6862 | `OBJ_ISPUBLIC` | `() -> bool` | whether that has happened (always no when `neverBecomesPublic`) |
| 6863 | `OBJ_OWNER` | `() -> int` | `ownershipType`: 0/1 public-or-mine, 2 someone else's, 3 a group ironman's |

`OBJ_FIND` is a **selector**, which is why the four getters beside it take no
arguments. That is the reference's own arrangement and it is the one thing
about this family that cannot be inferred from the call sites.

Where each part is:

- `tools/cs2_gen_opcodes/local_opcodes.py` names them; `cs2_opcode.h` and its
  siblings are generated from it (`python3 tools/cs2_gen_opcodes/gen_opcodes.py`).
- `cs2vm2/cs2vm2.c`, `CS2VM2_Op_GroundObj` -- pops, and forwards to the host.
- `game/rs_cs2_host.c`, `exec_ground_obj` -- answers, and holds `active_obj`.
- `app.c`, `app_cs2_objs_on_coord` -- walks the obj-stack pool for one tile.

### The two clocks

The wire counts a despawn in **game ticks**; `RS_CS2Host::client_clock` counts
**logic ticks** (20 ms); `~buff_bar_time_string` divides its argument by 50 to
get seconds, and the overlay script multiplies the opcode's answer by 30 before
handing it over. So the opcodes answer in GAME ticks, the world stores
DEADLINES in client-clock units, and `RS_CS2_HOST_CLOCKS_PER_TICK` is the only
place the factor is written down. A factor dropped anywhere in that chain shows
up as a timer thirty times wrong rather than as anything failing, which is why
`make -C src test-ground-items` pins it.

`-1` is "the server never said" and is not a deadline in the past: the
countdowns answer 0 for it, so a pre-ownership revision draws no timer rather
than "[0s]" beside every pile.

---

## 3. The wire (where the timers come from)

`OBJ_ADD` at rev 239 carries `timeUntilPublic`, `ownershipType`,
`neverBecomesPublic` and `timeUntilDespawn` beside the id and quantity. The
generated codec (`3rd/rsprot/packets/obj_add.c`, `packet_obj_add_v19_out`)
always decoded all four; `osrs239_parse.c` threw them away.

Now: `struct PktObjAdd` carries them, `rs_gameproto_exec.c` hands them to
`App_WorldObjStackSetOwnership`, which converts the two tick counts into
client-clock deadlines, and `struct WorldEntity_ObjStack` stores them. A
revision whose codec has no such fields leaves the stack on
`World_ObjStackAdd`'s `-1` defaults, and so does a hotkey spawn or a plugin's
pile.

ToriRSServer sends `despawn_ticks` and zeroes for the ownership half -- it has
no per-player loot ownership -- so the despawn countdown is live against the
embedded server and the visibility one is not.

---

## 4. Firing it

Nothing in the cache calls 7226: a pile changing is something only the client
knows. So the client fires it, exactly as it fires the three tile-highlight
refreshers beside it:

- `revconfig/osrs239/osrs239_dat2_cache.ini` names the script
  (`[script:ground_items_overlay]`) and the two settings carriers.
- Every obj-stack mutation in `app.c` (`App_WorldObjStackAdd`, `…Del`,
  `…SetCount`, `…ClearTile`) queues its tile.
- `app_ground_items_tick` drains the queue once per logic tick, sets the active
  tile and runs the script. Draining per tick rather than per packet matters:
  one OBJ_ADD burst touches the same tile several times.
- A rebuild shift, a settings-carrier change, or more than
  `APP_GROUND_ITEMS_DIRTY_MAX` tiles in one tick all fall back to walking every
  pile in the scene.

`TORIRS_GROUND_ITEMS_DEBUG=1` prints one line per rebuild.

---

## 5. The settings that could not be set

Two row kinds in the All Settings panel state everything about themselves
except the part only a client can do, because in the reference that part is the
engine's:

- **colour rows** (`param_1078 = 9`) -- the picker. Landed 2026-08-22; see the
  memory note `settings-colour-rows-have-no-apply`.
- **number-input rows** (`param_1078 = 4`) -- the numeric entry. `[clientscript,
  settings_input_op]` (3857) is `~settings_op_checker` and a flag, and
  `%varbit16075` ("a settings row is being edited") is read by three
  clientscripts and written by none.

The ground-items feature is mostly number rows: five price tiers and the line
limit. At their zero default every pile draws in the top colour and, with the
line limit at zero, nothing draws at all -- so those rows are not a refinement,
they are the feature.

The numeric entry is the colour picker's twin, built the same way:

- `RS_CS2Host_ScriptStarted` catches script 3857 with its arguments intact
  (`rs_cs2_settings_row_struct` is the shared frame check) and resolves the row.
- The row's varp is **learned, not tabulated**: a varp read inside a frame of
  `settings_get_number_input` (3964) names that setting's varp, and the row
  builder calls the hub while laying the row out, so every visible row has
  answered before its field can be clicked.
- `app_settings_number_tick` opens a `dbg_ui` entry box beside the field and
  commits on Enter. Writing the varp is the whole apply -- the row installed its
  own var-transmit hook, so the field repaints itself.

Unlike a colour, the value is stored **plain**. A colour varp holds `colour + 1`
so that zero can mean "never chosen"; for a threshold, zero is a real answer.

`TORIRS_SETTINGS_DEBUG=1` prints one line when the entry box opens. It has to:
a `dbg_ui` panel reaches the platform renderer as `ToriRSChrome_Prims` and is
invisible to every BMP path this client has, so a screenshot cannot answer "did
the box open".

---

## 6. Opening values

`~ground_items_login` (`general/scripts/misc/settings_ground_items.rs2`), called
from `[login,_]`, seeds once per character behind `%ground_items_seeded`:
overlay on, six rows, and the price ladder `0 / 25k / 100k / 1M / 10M`. `t1`
doubles as the visibility floor -- the overlay hides a pile worth less than
every threshold -- so `t1 = 0` means "hide nothing", which is what a player who
never opens the panel should get.

Colours are deliberately not seeded:
`~torirs_settings_colour_or_default` falls back to the setting struct's own
`param_1230`, so the cache already states what each band looks like.

The eleven varps behind the thresholds and colours are declared in
`general/configs/settings_ground_items.varp` (`transmit=yes scope=perm`).
Undeclared, `ToriRSServer_WorldMarkVarp` drops the write and
`ToriRSServer_SaveWrite` skips it, so a threshold would not survive a logout --
see the memory note `settings-rows-need-a-declared-carrier`.

---

## 7. Verifying

```sh
make -C src test-ground-items
```

pins the eight opcodes (dispatch shape, pop order, the two clock conversions,
the `-1` answers) and the number row's learned varp.

End to end, against the embedded server:

```sh
# overlay + colour bands
SDL_VIDEODRIVER=dummy TORIRS_GROUND_ITEMS_DEBUG=1 TORIRS_MAX_FRAMES=1700 \
  TORIRS_SIM_CMD="600,dropobj coins 5000;620,dropobj rune_scimitar 1;700,dropobj abyssal_whip 1" \
  TORIRS_EXIT_BMP=/tmp/gi.bmp \
  ./src/torirs_embed --manifest manifests/manifest_osrs239.ini --user testc --pass test

# the despawn countdown
#   ...;900,setting 14873 1

# the numeric entry (3964 teaches the varp, 3956 warms the struct, 3857 clicks)
SDL_VIDEODRIVER=dummy TORIRS_STDERR_UNBUFFERED=1 TORIRS_SETTINGS_DEBUG=1 \
  TORIRS_MAX_FRAMES=2000 \
  TORIRS_SIM_RUNSCRIPT="1100,3964,328;1200,3956,4511,0;1600,3857,4511,1" \
  ./src/torirs_embed --manifest manifests/manifest_osrs239.ini --user testc --pass test
```

The manifest needs `transport=embed` and a `dir=` pointing at a whole cache;
`profiles/osrs239.ini` states both. And `TORIRS_STDERR_UNBUFFERED=1` is not
optional for the last one -- stderr is 64 KB buffered and the run's own output
never reaches the flush.
