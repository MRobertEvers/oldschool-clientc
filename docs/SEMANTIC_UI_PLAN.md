# Semantic UI tagging — design, and what shipped

> **Status: implemented.** Phases 1–6 below are done and green. §8 records what
> the survey actually measured, and the three things the plan got wrong.


The plugin system needs to talk about interface elements by what they ARE —
the report button, the logout screen, the minimap box, the safe part of the
viewport — rather than by which component id a particular revision happened to
give them. The geometry half of this already exists: the frame-slot roles
(`slot_rect`, `api.layout`). This plan generalizes that vocabulary into a
data-driven **role registry**, configurable per revision in revconfig, that can
also name *elements* (clickable controls, screens) and can reach the dynamic
components a CS2 script builds at runtime.

Vocabulary note: this doc uses **role** as the one name for a semantic tag,
because that is the word `uitree_frame.h` already uses for the placeable
regions ("Everything here is addressed by ROLE and never by id",
`src/ui/uitree_frame.h:21-24`). A role that is a region keeps living in the
layout system; this plan adds roles that are elements.

---

## 1. What exists today (survey)

Four semi-connected identification layers already exist. The design's job is
to join them under one lookup, not to add a fifth stranger.

**A. Frame-slot roles** — `enum UITreeFrameSlot` (`src/ui/uitree_frame.h:40`):
`VIEWPORT, MINIMAP, COMPASS, CHAT, SIDEBAR, MAIN_MODAL, CHAT_BUTTONS`, plus
derived `CANVAS` and `SAFE` on the plugin side. Recognition is a hardcoded C
switch, `frame_node_is_slot` (`src/ui/uitree_frame.c:151-176`), over two
channels:

- **builtin type** — a revconfig frame declares `type=world` / `type=minimap`,
  baked to `UIELEM_BUILTIN_*`;
- **slot tag** — a cache frame's mount nodes carry `slot=main_modal` etc.
  (`enum UITreeSlotTag`, `src/ui/uitree.h:172-180`).

Sub-identity within a role is the role's own numbering, never list position:
chat buttons answer to their *filter*, sidebar mounts to their *tabno*
(`UITree_FrameSlotIndex`, `src/ui/uitree_frame.c:178-194`). The bridge caches
role→node per `tree->generation` (`app_plugin_slot_node_cached`,
`src/plugin/torirs_plugin_bridge.u.c:2427`).

**B. `[iface:<name>]` cacherefs** — `RevConfigRefs` maps `(kind, name)` → id,
session lifetime, −1 means "this revision does not have that thing"
(`src/revconfig/revconfig_refs.h`). Exposed to plugins as
`cache_id(ctx, "iface", name)`. This names *interface groups*, not live nodes.

**C. `[component:<name>]` names** — consumed by the uitree builder and
**discarded**: `struct UITreeComponent` has no name field, and there is no
`UITree_FindByName`. Runtime lookups are by uid
(`UITree_FindByComponentId`) or by cc sub-id (`UITree_FindChildBySubid`).

**D. clientCode** — the cache's own semantic tagging. Decoded for dat1 and
dat2 alike, carried to `UITreeComponent.behavior.client_code`. Two uses:

- content-slot codes retype a node into a builtin surface (1337→WORLD,
  1338→MINIMAP, 1339→COMPASS, 1400/1401→WORLDMAP,
  `src/ui/uitree_build.c:200-241`) — this is the join that makes frame slots
  work on CS2 lanes;
- behaviour codes drive the CS1-era per-tick handlers (`RS_CC_LOGOUT = 205`
  etc., `src/game/rs_clientcode.h:20-61`), walked via the `tree->client_code`
  live set.

**Identity facts for CS2 dynamics** (the part that forces the design):

- A `cc_create` node's `component_id` is synthesized from a rotating tree-wide
  counter over child ids `0x8000..0xFFFF`
  (`UITree_AllocateDynamicComponentId`, `src/ui/uitree.c:970`). A
  `CC_DELETEALL` + rebuild recycles the same uids. **A dynamic uid is a
  per-rebuild handle, never an identity.**
- The only stable identity a dynamic component has is
  **(parent uid, `dynamic_child_index`)** — the sub-id the script itself chose.
  `CC_GETID` agrees: it answers the sub-id, not the uid
  (`src/game/rs_cs2_host.c:8751`).
- Child lookup precedence is *dynamic wins, static is the fallback*
  (`uitree_child_key`, `src/ui/uitree.c:624`; comment at `:2298-2306`).
- One revision ships several toplevels (osrs239: 161, 164, 548, 601, 80, 165),
  and the same logical element sits at different addresses in each.

**The gap**, stated as the four requested semantics:

| semantic | today |
|---|---|
| world safe viewport | ✅ served — `TORIRS_PLUGIN_SLOT_SAFE_GAMECHROME`, derived live (`plugin_safe_gamechrome_rect`, `src/plugin/torirs_plugin_host.c:913`) |
| minimap box | ✅ served — `TORIRS_PLUGIN_SLOT_MINIMAP` (dat1 `type=minimap`; dat2 clientCode 1338) |
| report button | ⚠️ dat1 only, and only as a chat-button *member*; no element-level verb; nothing on CS2 lanes |
| logout screen | ❌ no role at all; dat1 knows it as sidebar tab 10's `componentno=2449`, dat2 as an interface group; clientCode 205 marks only the button inside it |

And structurally: role recognition is a closed C switch; `[component:]` names
die at bake; nothing can address a CS2 dynamic node; plugins have no
element-level verbs (only `slot_rect` for regions and raw-id `if_click`).

---

## 2. Design

### 2.1 One registry, two channels, open vocabulary

A **role registry**: `role name (string) → matcher chain`, loaded per revision
from revconfig, resolved lazily against the live uitree. Two declaration
channels, mirroring the existing builtin-type / slot-tag split:

1. **Baked tag** — for revconfig-authored nodes (dat1 chrome, builtins,
   profile-authored controls): a `role=<name>` key on a `[component:<name>]`
   section, carried through the builder and stamped on the live node. This is
   the dat1 channel and costs nothing at lookup time.

2. **Matcher chain** — for cache-owned and dynamic nodes (CS2 lanes): a
   `[role:<name>]` section, usually in the cache half of the profile, holding
   ordered `match=` lines. First matcher that resolves on the live tree wins —
   the same fallback-chain idiom `slot_rect`'s doc already teaches.

A role a revision does not declare is an **answer, not a fault** — identical
to the `RevConfigRefs_Get` and `slot_rect` contracts. Plugins address roles by
string; the registry is open vocabulary so profiles and plugins can grow it
without touching C. The client's own well-known roles get documented name
constants (meta.lua + `#define`s), not a closed enum.

Region roles stay where they are: `safe_gamechrome`, `minimap`, `viewport` etc. continue
to answer through the layout system, and the new element verbs **delegate** to
the frame-slot resolver for those names rather than growing a second lookup —
one vocabulary, one resolver underneath, per the collapse doctrine recorded at
`src/plugin/torirs_plugin_bridge.u.c:2403-2411`.

### 2.2 revconfig grammar

```ini
; ---- element channel 1: baked tag on an authored component (ui half) ----
[component:chat_button_report]
type=chat_button
filter=report
role=report_button          ; NEW — stamped on the live node at bake

; ---- element channel 2: matcher chain (cache half) ----
[role:logout_screen]
; Ordered; first line that resolves on the LIVE tree wins.
match=iface(logout)         ; the [iface:logout] group's root node, if mounted
match=id(2449)              ; dat1 flat uid (LostCity logout tab pack)

[role:report_button]
match=slot(chat_buttons, report)  ; delegate to the frame-slot member
match=clientcode(601)             ; or the cache's own tag, where it ships one
match=id(if(553, 0))              ; or a static uid, stated per profile

[role:xp_counter]                 ; example of a DYNAMIC target
match=cc(iface(xpdrop), 4)        ; dynamic child sub-id 4 under group xpdrop
```

Matcher forms (each a tagged union at parse time):

| form | resolves to | notes |
|---|---|---|
| `slot(<name>[, <member>])` | `UITree_FrameSlotNode` / `MemberNode` | member uses the role's own numbering (chat filter names `public/private/trade/report`, sidebar tab numbers) |
| `id(<expr>)` | `UITree_FindByComponentId` | `<expr>` is the existing integer expression grammar, so `if(g, c)` and flat dat1 ids both work |
| `iface(<name>[, <child>])` | `[iface:<name>]` via `RevConfigRefs`, packed `(id<<16)\|child` (child defaults 0), then `FindByComponentId` | unmounted group → no match, chain falls through — this is what makes per-toplevel alternates work |
| `clientcode(<n>)` | first live node with `behavior.client_code == n`, via the `tree->client_code` set | the cache's own semantic channel; robust within a lineage |
| `cc(<anchor>, <sub_id>)` | `UITree_FindChildBySubid` under the anchor | anchor is `iface(name)` or `id(expr)`; inherits the dynamic-wins precedence; **the** CS2-dynamic channel |

Repeated `match=` keys ride the existing field stream the same way repeated
`hotkey=` lines do. Matcher strings must fit `RevConfigField.value[64]` —
comfortable for every form above.

### 2.3 Resolution semantics (the CS2 considerations)

- **Live, never bound.** Resolution happens at query time against the current
  tree. A resolved answer is memoized per role, keyed on
  `(tree->generation, tree->id_generation)` — `generation` catches rebuilds,
  `id_generation` catches `cc_create`/`cc_delete`/id churn *within* a
  generation (it bumps on every id assign/clear, `src/ui/uitree.h:835-837`).
  This is the same pattern as `plugin_slot_node_gen`, tightened for dynamics.
- **Never store a dynamic uid.** The memo holds `(generation pair, node
  index)`; consumers get answers (rect, visibility, a click dispatched now),
  not a uid to keep. `role_id()` exists for interop but its doc states the
  handle dies with the next `id_generation`.
- **Precedence**: baked tag first (it is the profile author speaking
  directly), then the matcher chain in declaration order. Within one matcher,
  existing tree rules apply: dynamic beats static on uid collision
  (`src/ui/uitree.c:1480`), first tree-order match wins for scans
  (`clientcode()`), matching `UITree_FrameSlotNode`'s first-match rule.
- **Toplevel variance**: a chain lists one `iface()`/`id()` line per toplevel
  family; whichever group is actually mounted resolves, the rest fall through.
  No toplevel conditionals needed in the grammar.
- **What this deliberately does not do**: no matcher can address "whatever
  script 909 last created" by script id, and no hook fires when a role
  appears. Both are plausible extensions (§7) but the four target semantics
  don't need them, and (parent, sub-id) is the identity the reference engine
  itself uses.

### 2.4 Plugin API — the element verbs

Per the requirement, these are **dedicated functions, separate from the
generic UI verbs** (`if_click`'s raw ids, `slot_rect`'s region enum). New in
`struct ToriRS_PluginApi` (ABI 15 → 16):

```c
/* All answer 0 / -1 for "this revision has no such role" — an answer,
 * not a fault, same contract as slot_rect. `role` is the registry name. */
int (*role_rect)(struct ToriRS_PluginCtx* ctx, char const* role,
                 int* out_x, int* out_y, int* out_w, int* out_h);
int (*role_visible)(struct ToriRS_PluginCtx* ctx, char const* role);
int (*role_click)(struct ToriRS_PluginCtx* ctx, char const* role, int op);
int (*role_id)(struct ToriRS_PluginCtx* ctx, char const* role); /* transient */
```

- `role_rect` on a region-role name (`safe_gamechrome`, `minimap`, `viewport`, …)
  answers **exactly** what `slot_rect` answers — same resolver underneath.
- `role_visible` walks ancestor visibility including `frame_hidden` (the
  layout-suppression flag deliberately separate from `behavior.hide`,
  `src/ui/uitree.h:501-517`). "Is the logout screen up?" is this verb.
- `role_click` resolves and dispatches through the same minimenu path
  `if_click` uses. Requires refactoring `app_plugin_if_click`
  (`src/plugin/torirs_plugin_bridge.u.c:2238`) so its node-index core is
  callable without a uid — a revconfig-authored control can carry
  `component_id == -1` and must still be pressable by role.

Lua mirrors the layout system's closure-upvalue doctrine
(`src/plugin/torirs_plugin_lua.c:1613-1626` — bind once, misspelling becomes
a nil index at the point of the mistake):

```lua
local report = api.role("report_button")   -- constructor, open vocabulary
if report.rect() then ... end
report.click()
report.visible()
```

`plugin_api.meta.lua` gains `torirs.Role` and `api.role` together with the
binding (the two files change together, per the meta.lua header contract).
Well-known names are documented there: `"safe_gamechrome"`, `"viewport"`, `"minimap"`,
`"report_button"`, `"logout_screen"`.

### 2.5 The four target semantics, bound per profile

| role | rs245_2lc / rs289lc (dat1) | osrs239 (dat2/CS2) |
|---|---|---|
| `safe_gamechrome` | delegate to `SLOT_SAFE_GAMECHROME` (derived; nothing to declare) | same |
| `minimap` | delegate to `SLOT_MINIMAP` (`type=minimap`) | same (clientCode 1338 retype already feeds the slot) |
| `report_button` | `role=report_button` on `[component:chat_button_report]` — or nothing at all, since `match=slot(chat_buttons, report)` in a shared default works | `[role:report_button]` with the surveyed uid/clientcode for the chat-button strip (survey task, §5 phase 5 — do **not** guess ids) |
| `logout_screen` | `[role:logout_screen] match=id(2449)` (tab 10's pack root); optionally `match=clientcode(205)` names the button inside it as `logout_button` later | `[role:logout_screen] match=iface(logout)` plus a `[iface:logout]` ref with the surveyed group id |

rev-634 and the other lanes bind lazily as need arises; an unbound role
answers "not here", which is correct until someone states otherwise.

---

## 3. Layering

revconfig stays a leaf (it hands strings/ints up, resolves nothing itself —
the `RevConfigFeaturesItem` doctrine, `src/revconfig/revconfig.h:742-765`),
and `src/ui/` stays ignorant of revconfig. So:

```
revconfig parse        src/revconfig/revconfig_roles.{h,c}   [role:] items, matcher parse
        │                                                     (string/int payloads only)
        ▼
App_Init translate     src/app.c                              resolve iface() names via
                                                              RevConfigRefs once; build the
        ▼                                                     ui-domain table
runtime resolve        src/ui/uitree_role.{h,c}               struct UITreeRoleTable;
                                                              UITree_RoleNode(tree, table, name)
        ▼                                                     memoized (generation, id_gen)
plugin surface         torirs_plugin_host.c / _bridge.u.c /   role_rect/visible/click/id;
                       _lua.c / plugin_api.meta.lua           api.role(name)
```

The baked-tag channel rides the existing builder path:
`RevConfigUIComponentItem` gains `role[64]`; `UIBuilderTreeOp` carries it;
bake interns the name in the role table and stamps a small
`uint16_t role_id` (0 = none) on `UITreeComponent` — the one new field the
tree pays for.

---

## 4. Contracts (project conventions applied)

- Registry/table pointers handed to resolvers: `assert()`, per CLAUDE.md — a
  NULL table is a caller bug, not a state.
- "Role not declared", "matcher didn't resolve", "group not mounted": all
  **legitimate runtime states** → guarded returns, never asserts.
- Matcher parse errors (unknown form, bad member name) fail loudly at load
  with the file/section named, following
  `revconfig: unknown slot tag '%s'` precedent — a typo must not become a
  role that silently never resolves.
- Allocations in table build: `assert()` the result.
- No tests that pin NULL-tolerance.

---

## 5. Implementation phases

**Phase 1 — revconfig: `[role:]` sections + `role=` key.**
- `revconfig_load.c`: accept section type `role` (name required); `match=` →
  new `RCFIELD_ROLE_MATCH`; accept `role=` under `component` sections →
  `RCFIELD_UICOMPONENT_ROLE`.
- `revconfig.h/.c`: `RCITEM_ROLE`; `struct RevConfigRoleItem { char name[64];
  struct RevConfigRoleMatcher matchers[8]; int matcher_count; }`; matcher
  parser (tagged union: SLOT/ID/IFACE/CLIENTCODE/CC) reusing
  `revconfig_parse_int` for `<expr>` payloads; slot + member symbol tables
  (member names: chat filter spellings from
  `revconfig_parse_chat_button_filter`, integers for tabno).
- `revconfig.h`: add `char role[64]` to `RevConfigUIComponentItem`.
- Tests: `src/revconfig/test/revconfig_test_roles.c` — parse, ordering,
  bad-form loudness, 64-char bounds.

**Phase 2 — ui: role table + resolver.**
- New `src/ui/uitree_role.{h,c}`: `struct UITreeRoleTable` (interned names,
  per-role matcher array in ui-domain terms, per-role memo slot);
  `UITree_RoleNode(tree, table, name)`; `UITree_RoleIntern(table, name)`.
- `uitree.h`: `uint16_t role_id` on `UITreeComponent` (+ spec); baked-tag
  scan path (linear over components is fine at these counts; the memo
  amortizes it).
- Region-role delegation: names matching the frame-slot vocabulary route to
  `UITree_FrameSlotNode` / `MemberNode`.
- Builder: `UIBuilderTreeOp.role`, stamped in `uitree_builder_bake.c`.
- Tests: mirror `uitree_frame` tests — baked tag wins over matcher; dynamic
  rebuild (delete-all + recreate) re-resolves via `cc()`; memo invalidation
  on `id_generation` bump; unmounted `iface()` falls through the chain.

**Phase 3 — App translate + plugin C surface.**
- `src/app.c` init: build the `UITreeRoleTable` from revconfig items +
  `RevConfigRefs` (after refs load, `src/app.c:7683-7698` neighborhood);
  free with the tree's owners.
- Refactor `app_plugin_if_click`'s core to take a node index; keep the uid
  wrapper.
- `torirs_plugin.h`: ABI 15→16; four verbs with full doc comments (answer-
  not-fault contract; `role_id` transience warning).
- `torirs_plugin_bridge.u.c` + `torirs_plugin_host.c`: implementations,
  legality gates (no `role_click` from draw handlers, matching `if_click`'s
  gates), engine-completeness asserts extended.
- Tests: `src/plugin/test/` — resolve/click/visible against a built tree,
  both channels, plus the ABI assert sweep.

**Phase 4 — Lua + meta.**
- `torirs_plugin_lua.c`: `api.role(name)` constructor returning the closure
  table (`rect`, `visible`, `click`, `id`), role name bound as upvalue.
- `plugin_api.meta.lua`: `torirs.Role`, `api.role`, well-known-name docs —
  same commit as the binding.

**Phase 5 — profile authoring + surveys.**
- rs245_2lc/rs289lc: `[role:logout_screen] match=id(2449)`; `role=` tag on
  `chat_button_report` (belt-and-braces beside the shared `slot()` default).
- osrs239: survey the actual uids/clientcodes for the report button and the
  logout group per toplevel (headless boot + uitree debug overlay /
  `UITreeIfaceStats`, the same method every prior id survey used) — **ids go
  in the INI only after being read off the live tree, never from memory**.
- Ship shared defaults: `report_button → slot(chat_buttons, report)` and the
  region delegations can live as compiled-in fallback rungs so a profile only
  writes what differs.

**Phase 6 — proof.**
- Convert one consumer end-to-end as the acceptance test: a probe plugin (or
  a `screenshot.lua` extension) that draws the four roles' rects and clicks
  the report button, run headless on a dat1 lane and on osrs239
  (env-gated input sim + BMP screenshot recipe).
- `make test-revconfig`, plugin test suite, and the uitree tests all green.

---

## 5a. What shipped, by file

| layer | file | what it holds |
|---|---|---|
| revconfig | [revconfig.h](../src/revconfig/revconfig.h), [revconfig.c](../src/revconfig/revconfig.c), [revconfig_load.c](../src/revconfig/revconfig_load.c) | `RCITEM_ROLE`, `[role:]` + `match=` + component `role=`, `revconfig_parse_role_matcher` |
| ui (leaf) | [uitree_role.h](../src/ui/uitree_role.h), [uitree_role.c](../src/ui/uitree_role.c) | the table, the resolver, the memo, the slot/member vocabulary |
| ui | [uitree.h](../src/ui/uitree.h) | `UITreeComponent::role_id`, `UITreeNodeSpec::role_id` |
| engine | [uitree_role_load.h](../src/engine/uitree_role_load.h)/[.c](../src/engine/uitree_role_load.c) | revconfig → ui translation; the only place that knows both spellings |
| engine | [uitree_builder_bake.c](../src/engine/uitree_builder/uitree_builder_bake.c) | stamps `role=` onto the live node |
| app | [app.h](../src/app.h), [app.c](../src/app.c) | `App::ui_roles`, loaded after the refs, freed with them |
| plugin | [torirs_plugin.h](../src/plugin/torirs_plugin.h) | ABI 16; `role_rect` / `role_visible` / `role_click` / `role_id` |
| plugin | [torirs_plugin_bridge.u.c](../src/plugin/torirs_plugin_bridge.u.c) | the four engine verbs + `app_plugin_click_node` |
| plugin | [torirs_plugin_lua.c](../src/plugin/torirs_plugin_lua.c) | `api.role(name)` → a bound verb table |
| diagnostics | [main.c](../src/main.c) | `TORIRS_DUMP_ROLES`, `TORIRS_DUMP_CLIENTCODES` |
| profiles | [rs245_2lc_dat1_ui.ini](../revconfig/rs245_2lc/rs245_2lc_dat1_ui.ini), [osrs239_dat2_cache.ini](../revconfig/osrs239/osrs239_dat2_cache.ini) | the two roles, bound per lane |
| proof | [_roleprobe.lua](../script/plugins/_roleprobe.lua) | one id-free script, both lanes |

Tests: `revconfig_test_roles.c` (grammar, nesting, loud rejection),
`uitree_test_roles.c` (resolution, precedence, the delete-all/rebuild case),
plus role coverage in `torirs_plugin_host_test.c`. `make -C src test-revconfig
test-uitree test-plugin-host test-gameframe test-xp-orbs test-feature-flags`
all green.

## 5b. The plugin migration, and where it stops

Every plugin that was reaching for a semantically-named element by hand now
asks for it by role — and one that looked like it should was deliberately left
alone, because it is asking a different question.

**`screenshot.lua` — migrated.** It carried a `report_button` config key
holding `"<interface>:<component>"`, parsed it with a pattern match, shifted it
into a uid by hand, and shipped `"162:31"` as the *default* — a correct id on
exactly one cache, and a button drawn over whatever component 31 of interface
162 happens to be on every other. All of that is one `api.role("report_button")`
now, and the id moved to the profile where it can be right per lane. The config
key is gone.

**`minimap_orbs.c` — partly migrated.** Its `minimap_rect` call became
`slot_rect(SLOT_MINIMAP)`: the same rectangle, reached through the vocabulary
the layout *write* path uses, so a plugin that reads the map and a layout that
places it cannot come to disagree about where it is.

Its five orb *click targets* stay on `cache_id("iface", …)`, and that is not an
oversight. `cache_id` answers **"what id does this revision declare"** — a
static profile lookup. A role answers **"which node is that right now"** — a
live tree resolution. They differ exactly when the target is not mounted, and
that is the orbs' normal case: interface 160 is not in the osrs239 tree at all
(the mounted groups are 161, 163, 728, 896), so a role-bound run orb would
offer no verb where today it offers one and reports honestly if the press
misses. Migrating would have been a silent regression dressed as a cleanup.

The boundary is worth stating once: **use a role to find or measure an element
that is on screen; use `cache_id` to name an id you intend to press blind.**

**`gameframe.c` — unchanged.** It is the layout *writer* and already speaks
roles (`layout_slot(SLOT_VIEWPORT, …)`); its tab art is its own, and its tab
identity is the tabno, which is the role's own numbering rather than a magic
number.

## 5c. The role vocabulary the profiles now carry

| profile | lanes | roles |
|---|---|---|
| `rs245_2lc_dat1_ui.ini` | rev 254 / 289 / 377 | 28 — 13 `tab_*` (baked `role=` on the sideicons), 13 `panel_*` via `slot(sidebar, N)`, `logout_screen`, `report_button` |
| `osrs239_dat2_cache.ini` | the seven rev-239 manifests | 16 — 14 `panel_*` via `id(if(<iface>, 0))`, `report_button`, `logout_screen` |
| `osrs_static_ui.ini`, `osrs_kronos_ui.ini` | (no manifest boots these) | 15 each — 14 `tab_*` + 14 `panel_*` + `logout_screen`; authored, not boot-verified |

`tab_<name>` is the stone you click, `panel_<name>` the interface behind it.
The point of the split is that both are things a player calls "the inventory",
and the tab NUMBER — the fact that actually moves between revisions — is stated
once in the profile and never learned by a plugin.

The dat1 lane resolves all 28, and every `panel_*` uid matches the tab table
that file has carried in its header comment since long before roles existed
(5855 combat, 3917 stats, 638 quests, 3213 inventory, …). All 13 `tab_*` stones
resolve with `component_id` = −1, so the role API is the only thing that can
reach them.

### `sidetab_<n>` — the one role reached by arithmetic

`tab_<name>` and `panel_<name>` are both spelled by NAME, which is right for
every caller that has one. A gameframe does not: it holds a tab NUMBER and
needs the node, and no amount of naming gets you from 3 to `panel_inventory`.

So `[role:sidetab_<n>]` is numbered, in the cache's own `side0..side13`
ordering, and it names the node whose state answers *"has the server given this
player tab n"* — the question the client's own chrome asks before it draws a
tab icon, and the one a plugin gameframe that replaced that chrome inherits
(`ToriRS_PluginApi::tab_enabled` → `RS_UISlots_TabGiven`).

`osrs239_dat2_cache.ini` declares fourteen of them, each a three-rung chain —
`iconN` on Classic (161), Fixed (548) and Modern (164), since `if_opentop`
swaps the whole root and only the live one is in the tree. The ICON and not the
`sideN` mount: `sideN` is `hidden=yes` in the cache and unhidden for the
SELECTED tab, so reading its hide would call thirteen tabs out of fourteen
taken away at every moment.

The dat1 profiles declare none and do not need to. Their gameframe carries its
own sidebar mounts, so IF_SETTAB answers there and the engine reads
`sideOverlayId` directly — which is also why this role could not simply be the
existing `tab_<name>`: those name the client's own tab-icon builtins, and a
gameframe plugin suppresses every one of them (`frame_is_lane_chrome`) the
moment it claims the frame. A verb reading their visibility would answer
"hidden" to the only caller that has any use for the answer.

The osrs239 panel ids were each read off that cache before being written down:
218 carries "Spell Filters", 541 "Prayer Filters", 239 "Playing:", 216 is the
62-graphic emote grid, 593 the combat tab's styles, 387 the 42-graphic
equipment sheet, 149 the bare container the server fills. They agree with the
tab table `revconfig/osrs_static` has always carried, which is the
cross-check — not the source.

## 6. Non-goals

- **Retype/behaviour by role.** The registry identifies; it never changes what
  a node does. clientCode's retype table stays where it is.
- **Migrating `[iface:orb_*]`.** Deliberately NOT done, and §5b says why: those
  are ids to press blind, not elements to find. The config-string escape hatch
  stays.
- **A general tree-query language.** Matchers are a closed, small set on
  purpose; anything smarter belongs in a plugin.

## 7. Future extensions (recorded, not planned)

- `role_changed` event (role resolved/unresolved edge) for plugins that cache
  drawings against an element — today `EV_LAYOUT_CHANGED` + cheap re-query
  covers it.
- Multi-level `cc()` paths (`cc(cc(iface(x), 4), 2)`) if a target ever sits
  two dynamic levels deep.
- Member sub-addressing on element roles (e.g. `logout_button` inside
  `logout_screen`) via a `within(<role>, ...)` anchor form.
- A CS2-host annotation channel (script id → role hint) if a lane ever ships
  an element that neither (parent, sub-id) nor clientCode can pin.

## 8. What the survey measured, and what the plan got wrong

### The bindings, read off the live tree

Never from memory — `TORIRS_DUMP_ROLES` and `TORIRS_DUMP_CLIENTCODES` print
what actually resolved, and every id below came from one of them.

| role | rs245_2lc (dat1, rev 254/289/377) | osrs239 |
|---|---|---|
| `report_button` | `role=` on `[component:chat_button_report]`; resolves to a `UIELEM_BUILTIN_CHAT_BUTTON` at 408,467 100×32 with **`component_id` = −1** | `iface(chat, 31)` → 162:31, at 437,480 79×23, ops `["", "Report abuse", "", "Report game bug", ""]` — so *Report abuse* is **op 2** |
| `logout_screen` | `slot(sidebar, 10)` → the tab-10 mount, uid 2449, at 553,205 190×261 | `iface(logout)` → 182:0, at 0,0 190×261 ("Use the buttons below to logout or switch worlds safely") |
| regions | nothing declared — resolve through the layout vocabulary | same, via the cache's clientCodes: 1337 world, 1338 minimap, 1339 compass, 1336 chat, 1354 xp-drops, all on toplevel 161 |

The dat1 report button having **no component id at all** is the finding that
justifies the whole `role_click` design: `if_click` takes a uid, and there is
no uid to give it. That element was unreachable by any pre-existing verb.

`cc()` was proven on a real lane too, against interface 162 component 58 — the
chat log's row container, whose children are `cc_create`d text rows. A role
bound to `cc(iface(chat, 58), 3)` resolves to a `dynamic` node at uid
0x00a28003, i.e. the 0x8000+ recycled range the design refuses to store.

### Three things the plan got wrong

**1. `role_visible` needed the host, not just the flags.** The plan said walk
the ancestors checking `hide` and `frame_hidden`. That is not enough: a sidebar
mount's visibility is not a flag on the node at all — it is
`UITREE_HOST_GET_SELECTED_TAB` compared against its `tabno`, read at emit time.
Without that test `logout_screen` reported *visible* from the moment the frame
was built, on every tab, which is precisely the question the role exists to
answer. The walk now consults `UITree_ComponentVisibleHost` for the four
host-gated builtin types. Measured: pressing F10 on the dat1 lane flips
`logout_screen` from `visible=no` to `visible=yes`.

**2. Memoising on `(generation, id_generation)` for every role was wrong.**
`id_generation` bumps on *every component pushed anywhere in the tree*, so a
region role keyed on it would re-walk the tree every frame to reach the same
answer. Roles are now split: one built only from `slot()` rungs keys on
`generation` alone (a frame slot is matched on builtin type and slot tag, and
`CcCreate` produces neither), and only a role that names a uid, a clientCode or
a cc sub id pays the strict key. Same reasoning added an `authored` flag, so a
lookup for a role nothing authored never walks the tree hunting for one.

**3. Roles must not be resolved in `on_start`.** The plan never said when a
plugin should ask. `on_start` runs before the gameframe is built, so every role
answers "not here" and a plugin that latched the answer is wrong for the rest of
the session. Binding the name (`api.role("x")`) is free and belongs in
`on_start`; *resolving* it has to happen on the frame. `_roleprobe.lua` is
written to demonstrate the split, and `torirs.Role`'s doc says so.

### Diagnostics added

- `TORIRS_DUMP_ROLES=1` — every declared role, what it resolved to (node, uid,
  type, dynamic/hidden, box), and a census of mounted interface groups.
  "Declared but unresolved" and "not declared at all" print differently,
  because they are different bugs with the same symptom.
- `TORIRS_DUMP_CLIENTCODES=1` — every live node carrying a clientCode, which is
  where a `clientcode()` rung's number comes from.

### Coverage still open

Only `rs245_2lc` (serving rev 254/289/377) and `osrs239` have role bindings.
`osrs_static`, `osrs_kronos`, `rs634void` and `rs377lc` declare none, so every
element role answers "not here" there — correct behaviour, unfinished coverage.
Each needs the same survey before anything is written into its profile.
