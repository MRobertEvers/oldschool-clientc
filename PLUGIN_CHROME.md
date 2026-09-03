# Plugin chrome: claiming the frame, its parts, and the things in the world

## Definitions

| Term | Meaning |
| --- | --- |
| **lane** | One booted revision: the cache, its revconfig profile, and the client behaviour that goes with them. `rs289lc`, `osrs239` and the 2004 Classic frame are three lanes. |
| **frame** / **gameframe** | The whole arrangement of the screen: where the scene, the minimap, the chatbox and the sidebar go, and the art around them. |
| **arranger** | The one plugin holding the frame (`layout_claim`). It places the regions and declares the parts it dresses. Tier 1. |
| **region** / **slot** | One of the placeable live surfaces — `viewport`, `minimap`, `compass`, `chat`, `sidebar`, `main_modal`, `chat_buttons` — plus the derived `canvas` and `safe_gamechrome`. Addressed by `enum ToriRS_PluginLayoutSlot`. |
| **member** | One node of a region that has several: chat button 3 (Report abuse), sidebar tab 10. The region's OWN numbering, never a list position. |
| **role** | A semantic NAME for a node, resolved per lane by the revconfig profile's `[role:…]` chain (`src/ui/uitree_role.c`). `report_button` is `slot(chat_buttons, report)` on a 2004 frame and `iface(chat, 31)` on OldSchool. |
| **part** | Anything a plugin can claim: a chrome part (a role name — a button, an orb) or an entity part (`npc:12`, `player:7`, `loc:x,z,l,id`, `obj:x,z,l,id`). |
| **dresser** / **claimant** | A plugin holding one or more scopes of a part. Tier 2. Needs no frame ownership. |
| **scope** | Which aspect of a part a claim takes: `POSITION` (the box), `APPEARANCE` (the pictures / the hull), `HITBOX` (the click). Exclusive per (part, scope); bits, so one claim may take several. |
| **claim** | An exclusive, persistent hold on (part, scope) by one plugin; survives rebuilds and absence; released at teardown. |
| **source** | Who provides a part underneath every claim: `LANE` (the cache / revconfig draws it), `FRAME` (an arranger declared it), `ADDED` (a plugin introduced it), `NONE`. |
| **added part** | A part no lane has a node for, introduced by `chrome_add` and hung off an anchor role. The minimap orbs on a 2004 cache. |
| **anchor** | The role an added part's box is relative to, and whose subtree it is painted inside. |
| **declaration** | What a holder states about the scopes it holds: `chrome_paint` / `chrome_ops` inside `EV_CHROME` for chrome parts; `entity_look` / `entity_ops` at any time for entities. Rebuilt whole each pass — an undeclared part is hidden. |
| **borrow** | A read-only second reference to another plugin's image, lent by `chrome_part` so a dresser can draw or read the arranger's plate. Stale borrows read as pending. |
| **degrade** | What a plugin does with a scope it asked for and did not get: it does not draw that aspect, and it does not complain — the screen is right, another plugin drew it. |
| **tombstone** | Where a replaced native widget WAS: the point in the tree a claimant's painting is anchored to so it inherits the widget's clip and paint order. |

## Purpose

One arbitration mechanism for everything a plugin might want to own on the screen, so that any two plugins that touch the same thing settle it at the moment the user flips a switch, rather than by drawing over each other every frame.

The rules, stated once:

> Arranging the frame is exclusive per FRAME. Dressing is exclusive per (PART, SCOPE). The arranger declares its parts; it does not blit them. Everything a dresser needs is something the host holds.

> A plugin claims everything it wants at `EV_START`, degrades per scope when it loses one, and tells the player only about a part NOBODY ends up providing.

> An entity is a part. Where it is belongs to the server; its outline and its right-click are claimable like a button's.

## Status

Implemented and green on all six plugin suites (`test-gameframe`, `test-plugin-host`, `test-xp-orbs`, `test-nxt-plugins`, `test-feature-flags`, `test-mobile-gameframe`), ABI 18:

- Chrome tier: scopes, claims, `chrome_add`, borrows, `EV_CHROME`, host painting on the right surface per source, `layout_slot_art` / `layout_slot_claimed` / `layout_slot_state` for arrangers.
- Entity tier: `entity_part` / `entity_look` / `entity_ops`, the `draw_hull` gate, menu-row drop/append through a new `menu_drop` engine verb.
- Lua: `api.chrome.*`, `api.entity.*`, `on_chrome`.
- Ports: `gameframe-layout` and `mobile-gameframe` declare their chat plates; `minimap-orbs` claims-then-adds per orb and is on by default; `xp-drop-orbs` claims `xp_drops`; `nxt-highlight` yields through the gate.
- Profiles: `report_button` bound on the dat1 lane; `orb_hitpoints` / `orb_prayer` / `orb_run` / `orb_spec` bound to interface 160's roots on osrs239.

Not done, and said so: moving or tinting a native node (engine work — `POSITION` on a LANE part and on any entity is refused out loud); rasterising a lane widget's sprite into a borrowable image (`SOURCE_LANE` reports a box and `art[]` of -1).

## Cache gameframes (ABI 25, 2026-09-02)

Both arrangers run on an OldSchool lane now, over whichever CS2 toplevel the server opened (548 fixed, 161/164 resizable, 601 mobile). The engine states a RULE and the profile states every number:

- **Frame binder.** A profile role `frame_<slot>` (or `frame_<slot>_<member>`, slot spelling from `uitree_role.c`) names the cache node that IS that region; `app_plugin_frame_bind` stamps it with the slot tag and member before every frame collection (`UITree_FrameSetBinder`). `revconfig/osrs239/osrs239_dat2_cache.ini` binds chat, main_modal, orbs, sidebar and sidebar_0..13 per toplevel with `id(if(<top>, <n>))`.
- **`orbs` region** (`TORIRS_PLUGIN_SLOT_ORBS`): the orb pack's block beside the map, placed by the arranger at the offset the lane's toplevel uses. Absent on a 2004 frame.
- **`api->frame_root`**: the live toplevel's group id, compared against `cache_id("iface", "toplevel_fixed")` and siblings — `gameframe-layout`'s `Auto` layout follows it.
- **Tab verbs on a cache frame**: `tab_active` reads which bound side panel is unhidden; `tab_select` runs `[script:sidebar_switch]`.
- **Chrome rule**: root-group LAYERS with an op or `noclickthrough` are hidden with the graphics; containers and ancestors of placed surfaces stay.
- On that lane the chat and the orbs are PACKS: placed whole, dressed with nothing.

## The problem this replaced

1. `EV_LAYOUT` and `EV_DRAW_FRAME` went to exactly one plugin. A plugin replacing one button had to become the frame owner — mutually exclusive with `gameframe-layout`.
2. The arranger's plate was a plugin-local image handle and a blit; nothing else could see it, measure it, or replace it. Painting the role's rect instead overhung the plate by three rows.
3. `minimap-orbs` drew pixels nobody else could find, and its only defence against the OldSchool cache drawing a second set was `disabled_by_default` and a comment.
4. Two plugins outlining the same npc had nothing to arbitrate.

## The two tiers

**Tier 1 — arrange.** `layout_claim`, one plugin, the whole frame. Unchanged.

**Tier 2 — dress.** `chrome_claim(part, scopes, enabled)`, exclusive per (part, scope), any plugin.

```
engine.layout_begin()      slot + part tables emptied
  EV_LAYOUT   -> the arranger: layout_slot / layout_slot_at / layout_slot_art
  POSITION holders' boxes re-applied over the arranger's placement
engine.layout_end()
  EV_CHROME   -> each claimant whose declaration went stale, in claim order:
                 chrome_part (read), chrome_paint / chrome_ops (declare)
  EV_LAYOUT_CHANGED -> everyone
```

`PluginHost_ChromeTick` runs every frame after `PluginHost_Layout` (`src/app.c`), not only on layout passes, because a borrowed image lands off the IO queue with no layout in flight and the holder has to be re-asked once it does.

## Addressing: a part is a role name

`chrome_claim("report_button", …)` on every lane. The per-lane difference is the profile's:

```ini
; revconfig/rs245_2lc/rs245_2lc_dat1_ui.ini (rs254 / rs289 / rs377 share it)
[role:report_button]
match=slot(chat_buttons, report)

; revconfig/osrs239/osrs239_dat2_cache.ini
[role:report_button]
match=iface(chat, 31)
```

`UITREE_ROLE_MATCH_SLOT` already resolves through `UITree_FrameSlotMemberNode`; one new engine verb, `role_slot`, answers the reverse (`report_button` → `(chat_buttons, 3)`) so an arranger's `layout_slot_art(slot, member)` and a dresser's `chrome_claim(name)` meet.

The arranger stays in slot terms — it places members and should not know their names — and has twins for the two things it must say about a part: `layout_slot_claimed(slot, member, scopes)` before installing a region of its own, and `layout_slot_state(slot, member, state)` to select the plate the host paints.

## Scopes

| Scope | Chrome part | Entity part |
| --- | --- | --- |
| `POSITION` | the box; a FRAME member is re-placed under it on the next layout pass; refused on LANE parts | refused — the server's |
| `APPEARANCE` | `art[]` per state, `label_x/y`, `chrome_state`; on a LANE part takes a `role_replace` so the native pixels and click go | the hull (`entity_look`); `draw_hull` by anyone else is refused for that element |
| `HITBOX` | `chrome_ops`: the region the host installs and the click it routes | `entity_ops`: rows appended to / replacing / removing the game's own |

A resolve COMPOSES: the box from the POSITION holder, the pictures from the APPEARANCE holder, the click from the HITBOX holder, and the source underneath for each scope nobody took. Three plugins can hold the three scopes of one button.

`chrome_claim` returns the mask GRANTED, or `-1` for a part no revision has. `0` means every scope asked for is another plugin's. `chrome_owner(part, scope)` says whose.

## Where things paint

| Source | Surface | Anchored to |
| --- | --- | --- |
| FRAME | `EV_DRAW_FRAME`, after the owner's own drawing | nothing — beside the stones the arranger declared |
| LANE | canvas pass, before the plugins' `EV_DRAW_CANVAS` | the role, `replace=1` — the tombstone of the hidden widget |
| ADDED | canvas pass, before `EV_DRAW_CANVAS` | the anchor role — inside its subtree, under its clip |

The "before" is what lets a holder's own per-tick drawing (an orb's fill and number) land on top of the plate the host put down.

## Added parts

```c
api->chrome_add(ctx, "orb_hitpoints", "minimap", &initial);
```

Anchored, not positioned, so the frame moving the map moves the orb. Interned as a role with no matcher, so anyone can find it by name. Introduced with every scope; the introducer may release some. Named for what it IS (`orb_hitpoints`, never `minimap_orbs_orb_0`) so a profile can later bind the same name to a native node — at which point `chrome_add` on that lane silently becomes a claim, and the plugin's code does not change. That is exactly what happened to `minimap-orbs` on osrs239.

`chrome_add` answers `-1` when the anchor has no box YET (the first layout has not run). That is "not now", not "no": the orbs retry from `EV_LAYOUT_CHANGED` until a frame can take the part, and report once when every claim is settled.

## The enable-time protocol

Claim everything at `EV_START`, before drawing anything. Then per part:

| answer | meaning | do |
| --- | --- | --- |
| mask with the bits you wanted | you provide it | declare it in `EV_CHROME` |
| mask missing bits | another plugin provides those aspects | degrade per scope; `api->log` naming `chrome_owner` |
| `-1` | no revision has it | `chrome_add`, or do without |

- **Losing a scope is not a fault.** The player's screen is correct. Log only.
- **A part nobody ends up providing** — an add refused for good, a full table — is the one thing the chatbox hears, in ONE line after every claim has been tried.
- **Nothing provided at all** → `disable_self`.
- **Degradation is informed.** `chrome_part` answers for a part someone else holds, so a plugin that lost two orbs keeps its other two where they were rather than packing them into the slots the winner is using.

`minimap_orbs.c:orbs_claim_all` is the reference implementation.

## Entities

Same table, same scopes, same answers, same teardown. What differs is resolution: a name resolves to a scene ELEMENT once per world frame (`plugin_entity_resolve_all`, through the same snapshot walks a plugin uses), and the element is what `draw_hull` and the menu speak in.

- `entity_part(kind, a, b, c, d, buf)` spells the name; a plugin never formats one by hand.
- `entity_look(part, {hull, rgb, fill_alpha, shape})` — the APPEARANCE holder's standing hull, painted by the host after `EV_DRAW_WORLD`.
- `entity_ops(part, mode, ops, count, tag)` — the HITBOX holder's rows, applied by the host after `EV_MENU_BUILD` on every build the entity is the subject of: `APPEND` keeps the game's rows, `REPLACE` drops them (new `menu_drop` engine verb), `NONE` drops them and adds nothing. Selection comes back through `EV_MENU_SELECT` with `plugin_tag`.
- A claim on an npc slot that has not spawned stands and binds to whatever spawns. Slots are reused: a plugin that means "this goblin" watches `base_npc_id`.
- `nxt-highlight` — the cache's own highlight groups — claims nothing and yields through the `draw_hull` gate; a claim is for a plugin overriding the baseline.

## Edge cases

### Ownership
- **A1** Second claimant on a held scope gets that bit cleared from the returned mask; nothing changes. First-come, per scope.
- **A2** Arranger claiming its own part: allowed; `EV_CHROME` wins over `EV_LAYOUT` for that scope.
- **A3/A4** Frame owner changes or releases: claims key on the name, survive, and re-resolve against the new source next pass. A LANE replacement is RECONCILED each tick, not taken once — a claim that was hiding a cache widget stops hiding it the moment an arranger starts placing that member, or the arranger's button would vanish.
- **A5** Claim table full: `-1` from the internal set, reported as unprovided; logged with the table size.
- **A6** `role_replace` and `chrome_claim(APPEARANCE)` on a LANE part share one exclusion: the host takes the replacement on the claimant's behalf and releases it with the claim.

### Lifetime
- **B1** Dresser stops: claims, borrows both ways, and its LANE replacements go in `plugin_teardown`; the arranger's declaration was retained while suppressed, so restoring it is a flag flip.
- **B2** Introducer of an added part stops: the part goes with it. A plugin that degraded does NOT reacquire; it picks the part up at its next enable.
- **B3** Arranger stops under a live borrow: borrowed handles go stale, dresser re-reads next pass with `source = LANE`.
- **B5** Stale borrow: `PluginImage.generation` bumps on every drop and survives the wipe; a borrow whose generation moved reads exactly as PENDING (`image_size` 0, `draw_image` nothing). Deliberate reuse rather than a third state.
- **B6** Borrow table: fixed at claims × states; no failure path.

### Absence and timing
- **C1** Claim before any frame: stands; `chrome_part` 0; `EV_CHROME` not raised. Claiming at `EV_START` is correct, not premature.
- **C2** Claim on a part this revision lacks: `-1`. Add or do without.
- **C3** Add with an anchor that has no box: `-1`; retried from `EV_LAYOUT_CHANGED`.
- **C4** Claimed and never painted: hidden. Releasing gives the source's part back. Two sentences, both sayable.
- **C5** Borrowed image lands with no layout pass: `plugin_image_publish` marks the borrower's claims `needs_declare`; the per-frame tick re-asks.

### Coordinates
- **D1** A replacement bigger than the part: clip is the holder's declared box under the anchor's PARENT clip — `layout_slot_overlay`'s rule, reused.
- **D2** `w <= 0 || h <= 0` from a POSITION holder: refused and logged. Other holders pass 0.
- **D3** Anchor not placed this pass: `chrome_paint` returns 0.
- **D4** In: anchor-relative (added) or canvas (everything else). Out of `chrome_part`: canvas, always.

### Input
- **E1** Arranger's imperative region for a claimed member: gated by `layout_slot_claimed(…, HITBOX)` — the one line `gameframe.c` adds.
- **E2** LANE part APPEARANCE claim routes through `role_replace`, so the native ops leave with the pixels. HITBOX alone lays a region over the native one; paint order gives it precedence.
- **E3** Click after ownership changed: routed by who holds the claim at dispatch, via the route table.
- **E4** `op_count` 0 claims the pointer and offers nothing.
- **E5** Entity `REPLACE` drops rows from the highest index down so each drop leaves lower indices true; runs after the plugins' `EV_MENU_BUILD` so no handler's payload shifts under it.

### Ordering
- **F1** Claim order = table order; the table is never compacted.
- **F2** Host paints FRAME parts after the owner's `EV_DRAW_FRAME`; canvas parts before `EV_DRAW_CANVAS`.
- **F3** `chrome_paint` / `chrome_ops` outside `EV_CHROME`, `layout_slot_art` outside `EV_LAYOUT`: assert. Lua gets `luaL_error` instead.
- **F4** `chrome_claim` from inside a chrome pass: refused with a log (the table is being iterated), never asserted.

### Added parts
- **G1** Two plugins add the same name: second gets the held scopes cleared, exactly as losing a claim.
- **G2** A profile later binds the name natively: `chrome_add` becomes a claim; same code path.
- **G3** Anchor disappears mid-session: the part inherits its fate through the anchor's emit.
- **G4** Adding a name that already resolves: it is a claim, not a shadow.
- **G5** Name parts for what they are.

## Files

| File | What changed |
| --- | --- |
| `src/plugin/torirs_plugin.h` | ABI 18; `EV_CHROME`; scope/state/source/entity enums; `ChromePart`, `EntityLook`; the chrome, arranger and entity verbs |
| `src/plugin/torirs_plugin_host.[ch]` | claim/borrow/slot-art tables; resolve/compose; `PluginHost_ChromeTick`; surface-aware painting; entity resolve, hull gate, menu ops; `role_slot` and `menu_drop` engine verbs; image `generation` |
| `src/plugin/torirs_plugin_bridge.u.c` | `app_plugin_role_frame_slot`, `app_plugin_menu_drop` |
| `src/app.c` | `PluginHost_ChromeTick` after the layout tick |
| `src/plugin/torirs_plugin_lua.c` | `api.chrome.*`, `api.entity.*`, `on_chrome` |
| `src/plugin/plugins/gameframe.c`, `mobile_gameframe.c` | plates declared with `layout_slot_art`; regions gated; state via `layout_slot_state` |
| `src/plugin/plugins/minimap_orbs.c` | claim-then-add per orb, degrade table, host-painted plates, enabled by default |
| `src/plugin/plugins/xp_orbs.c` | claims `xp_drops`; degrades |
| `src/plugin/plugins/nxt_highlight.c` | documented as the yielding baseline |
| `src/plugin/test/*` | fake `role_slot`, `menu_drop`; slot-rect fakes answer from placement |
| `revconfig/rs245_2lc/rs245_2lc_dat1_ui.ini` | `[role:report_button]` |
| `revconfig/osrs239/osrs239_dat2_cache.ini` | `[role:orb_hitpoints]` … `[role:orb_spec]` |

## Verification

- All six plugin suites pass; `test-gameframe` is the A/B for phase 2 (the declared plate reproduces the blit count exactly).
- Headless boot of `manifest_osrs239.ini` runs 600 frames clean on the optimized binary. A `make OPT=0` binary (asserts on) is the one to soak the phase asserts against; `TORIRS_PLUGIN_PREFS` with `[plugin:minimap-orbs] enabled=1` exercises the claim path on both lanes.
