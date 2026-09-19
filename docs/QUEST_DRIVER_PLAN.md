# quest-driver: integrated implementation plan

One Lua coroutine per quest test, resumed by a C scheduler from the content-test pump.
`api.drive` (test-only) exposes engine seams; the Lua half composes verbs. Every verb
returns `(result, detail)`, `result` in
`{ok, timeout, not_found, refused, covered, no_row, not_visible, closed, unsupported}`.

Deadlines are in **server ticks**, counted off the `server_tick` drive event.

## 0. Decisions that changed during integration

Everything below was re-opened in the tree before it was applied. Citations are the
lines I read, not the lines a group plan claimed.

| # | Decision | Why | Evidence |
|---|---|---|---|
| D1 | A dialog/CLICK-armed row's real action is `RESUME_PAUSEBUTTON` with `action_index -1`, **not** a numbered `IF_BUTTON`. Dialog clicks (`chat.continue_`, `chat.choose`) drive `app->host.resume_pausebutton_component_id`, never a fabricated op click. | `app_plugin_click_node` forces `IF_BUTTON` for `op>=1` and the button-type default (`IF_BUTTON`) otherwise; neither reaches the row a real click builds. | `src/game/rs_minimenu_build.c:1073-1082`, `:702-719`; `src/plugin/torirs_plugin_bridge.u.c:3959-4048`; `src/app/app_cs2_flush.c:209-220` |
| D2 | `ui.invoke` on a **cache-authored numbered-op** button uses `app_plugin_if_click(app, component_id, op)`, not `app->host.trigger_op`. | `trigger_op` is the CS2 host's ring (`RS_CS2Host`) and has no IF1 counterpart; `app_plugin_if_click` → `app_plugin_click_node` already splits IF1/IF3 in one path. | `src/plugin/torirs_plugin_bridge.u.c:4105-4121`, `:3959-4048`; `src/game/rs_if1_buttons.c` |
| D3 | Mount liveness is `UITree_GroupPresent(tree, group_id)`. `interface_parents` is **CS2-only**. | The only non-test writer of `interface_parents` is `task_interface_open.c` (the IF_OPENSUB path). The dat1 lane mounts through `CreateTask_SlotMount` and never writes it. | `src/engine/uitree_builder/task_interface_open.c:404,641`; `src/app/app_boot.c:1181`; `src/game/rs_ui_slots.c:236`; `src/ui/uitree.h:3213` |
| D4 | Never test `app->modal_host_uid == 0`, and never treat non-zero as "a modal is open". | Initialised to `-1`; set on open; **never cleared on close**. | `src/app.c:80`; `src/app/app_boot.c:1268`; `App_CloseSubInterface` takes only `(app, target_uid)` and enqueues `(-1, 0)` |
| D5 | A dialogue page advance cannot be detected by a `group_id` diff. Paging remounts the **same** interface and the tree has no per-mount generation. Page advance is an EDGE event (`sub_mounted`) plus a content diff. | `UITree_InterfaceParentSet` stores only `{container_uid, group_id, type}`. | `src/ui/uitree.c:6679-6703`; `src/app/app_boot.c:1230-1232` |
| D6 | `pause_pending` must be compared **by component id**, not by presence. | `UITree_SetPausePending(tree, -1)` is called unconditionally on every sub-interface mount and on every slot mount, by anything in the app. | set: `src/app/app_cs2_flush.c:220`, `src/game/rs_if1_buttons.c:105`; cleared: `src/app/app_boot.c:1235`, `src/game/rs_ui_slots.c:267` |
| D7 | `TORIRS_WIDGET_LOADED/CLOSED` are **not** a blocker. `BOUND`/`UNBOUND` are already raised per watch per publication fence and carry role bind/unbind. `LOADED/CLOSED` stay on the list only because they distinguish *interface changed* from *rebound*. | The contract comment says so, and the dispatch loop exists. | `src/plugin/torirs_plugin_contract.h:222-232`; `src/plugin/torirs_plugin_host.c:7264-7288` |
| D8 | The scheduler pumps **twice** per frame: at `FRAME_START` and again after `App_PluginLayoutTick`. | Within one `app_frame`, `PluginHost_FrameStart` (:569) precedes the packet pump (:891) which precedes `App_PluginLayoutTick` (:940). A single FRAME_START pump can never see this frame's own events. | `src/app/app_frame.c:569, 891, 940, 2166` |
| D9 | `iface(<name>, <child>)`'s second argument is a **numeric expression only**. Symbolic child names do not parse. | `revconfig_role_parse_ref`'s iface branch calls `revconfig_role_parse_int`, which requires the whole trimmed value to parse as an int expression. | `src/revconfig/revconfig.c:2679-2694, 2724-2748` |
| D10 | Rung-level alternation already exists as **repeated `match=` lines**. `any(...)` is needed only *inside* one rung's numeric argument (because `|` is bitwise-or); `derive=` rides the existing, unused `UITreeRoleTable.fallback` hook. | `[role:sidetab_3]` ships four `match=` lines today; the fallback pointer exists and nothing sets it. | `revconfig/osrs239/osrs239_dat2_cache.ini:1311-1315`; `src/ui/uitree_role.h:146-154`; `src/revconfig/revconfig.c:626-636` |
| D11 | Component content symbols are always `"<iface>:<child>"`. | The pack stores the qualified string. | `src/torirsserver/torirs_server_content.c:428,438` |
| D12 | dat1 child index comes from the `com_N` **name**, not section order. | `multi2.if`'s sections appear in order `com_0,1,2,5,3,4,8,6,7`. osrs239 `.if` files are the opposite: order **is** the index (verified against `[role:report_button] match=iface(chat, 31)` → `chatbox.if` section 31 = `reportabuse`). | `LostCity_Content2/scripts/interface_chat/interfaces/multi2.if`; `revconfig/osrs239/osrs239_dat2_cache.ini:818-819` |

### Corrections applied (confirmed by opening the file)

- **pointer**: `WorldEntity_ObjStack` *does* carry `draw_position` — project it, do not fall back to a tile centre (`src/world/entity_objstack.h:8-13`). Route-length arrival is consumed per tick in `world_cycle.c`, not `entity_pathing.c:235` (a teleport reset). CmdBus is drained once per loop iteration, and a plugin-driven push lands one frame later than the `TORIRS_SIM_*` main-loop precedent — budget one extra frame in every click cadence (`src/cmd/cmdbus.h:4-9`).
- **world-interact**: `"items"` → `"inventory:items"` (D11). `click_obj` default op `1` → `3`: the synthesized *Take* row is emitted only for slot index 2 (`rs_minimenu_world.c:645-652`). `modal_host_uid == 0` predicates deleted (D4). The `sidetab_3` match is four separate `match=` lines, never pipe-joined (D10). The OPHELD/OPHELDT/IF_BUTTON cases live in `app_minimenu_inv_action` (`src/app/app_minimenu.c:1160`), called from `app_minimenu_run_option`'s INV_SLOT case (`:2630`). `PKT_NAME_OBJ_ADD`/`OBJ_DEL` exist and are dispatched (`src/game/rs_gameproto_exec.c:457,475`) — the "no despawn notification" claim is withdrawn.
- **chat-continue**: every `dialog_continue` match expression re-expressed with numeric child indices (D9), re-counted from the `.if` files myself. Same-group remount cannot release the await (D5). `pause_pending` release scoped by com id (D6). The meslayer mode reader is a `struct VarCIds` field + `VarCManager_GetInt`, not a new VM hook (`src/game/varc_ids.h:20-43`, `src/varc/varc_manager.h:50`). `chat.kind()` gains an `other_input` bucket. `ToriRSServer_WorldCloseModalEx` is the function with the abort logic. `app_cs2_flush.c:126-165` closes the modal locally, same tick — that is the await's real mechanism.
- **chat-options**: `chat.choose` clicks through the resume path, not `app_plugin_click_node(op=1)` (D1). `UITree_PausePendingActive` is a live guard for this verb, not dead code.
- **chat-read**: `TORIRS_WIDGET_LOADED` wiring dropped from the critical path (D7). `objectbox_double` has a live caller (`tutorial_boxes.rs2`'s `~doubleobjbox` and its `::objbox2` debugproc), so `chat.item`'s two-slot path is testable against real content. `questscroll:close_button` carries a static `op1`, so it takes `add_menu_ops_rows`/`k_inv_button_action` → `IF_BUTTON`, not the `if_button_action_for_type` fallback. `PLUGIN_WIDGET_MODEL` also needs `src/plugin/torirs_plugin_host.h` (the request enum/struct live there).
- **state**: `RS_ChatMessage` has **no** ordering field — `msg.await` needs one added before it can be scoped to post-registration (`src/game/rs_chat.h:71-76`). `var_serv[]` is the client's record of the server-authoritative value and is the cheap source for `var.expect`'s server half (`src/varp/varp_manager.h:78-80`). `InvManager_Total` already exists (`src/inv/inv_manager.h:202`). `torirs_server_ids.c` is the wrong citation for quest-authored varp/stat symbols.
- **ui-misc**: `ui.invoke` seam changed (D2); mount liveness changed (D3); `app_plugin_tab_select` already branches CS2 vs dat1 internally, so `ui.tab` needs no lane handling and no `sidetab_N` precheck (`src/plugin/torirs_plugin_bridge.u.c:5768-5772`); `npc->name` can carry `<col=…>` and must be normalised (the field is sized 64 for exactly that reason).
- **events-core**: `App_CloseSubInterface` has no `interface_id`/`type` in scope — close must be stamped where the old group is still known. `mesbox`/`objbox` are **server proc names**; the interfaces are `messagebox`/`objectbox`. `objectbox` has no continue child (resume is armed on `objectbox:universe`). Two-pump ordering (D8). `multiobj2/3/4` is a button-driven *choice* family, not a `doubleobjbox` alternate.
- **rs289lc-lane**: `npcchat1` continue is `com_3` (four children); `npcchat4` and `chat4` continue is `com_6` (seven children) — both re-counted from the `.if` files. `questscroll` opens via `if_openmain` only. Inventory/worn content updates are `PKT_NAME_UPDATE_INV_FULL` / `UPDATE_INV_PARTIAL`. `multiobj2/3/4` moved out of the doubleobjbox role into its own choice family.

### Rejected

| Rejected | Why |
|---|---|
| "This group needs zero new revconfig roles" (world-interact, for the *inventory* verbs) | True for npc/loc/obj targets; the backpack verbs still need `panel_inventory`/`sidetab_3` to know the grid is mounted. Kept as stated, but the blanket phrasing is narrowed. |
| Raising `TORIRS_WIDGET_LOADED/CLOSED` as a **prerequisite** (chat-read, chat-continue, ui-misc) | `BOUND`/`UNBOUND` already fire (D7). Demoted to step 9. |
| A dedicated `MINIMENU_OPENED` drive event (pointer) | The menu opens synchronously inside the same `app_frame`; a level read of `interact.minimenu.visible` is sufficient. |
| `npc.nearest` as symbol-matched (ui-misc's own guess) | Split instead: `npc.nearest(sym, radius)` keyed on the content symbol, with `npc.by_name` kept separate. The parameter is renamed `sym` so the verb list stops being ambiguous. |
| `multiobj2/3/4` as a `dialog_doubleobjbox` binding | Every child is `buttontype=normal` with no pause row — it is a choice menu, filed with the options family. |
| `iface(chat_left, continue)` and every other symbolic-child match | Does not parse (D9). |
| Scraping stderr for the debugproc verdict (`t.cheat`) | `ToriRSServer_ScriptsRunDebugproc` returns the enum synchronously in-process; call it directly. |

---

## 1. osrs239 roles

`kind`: **static** = one component; **member** = a numbered/alternating family;
**derived** = answered by the engine at the publication fence through
`UITreeRoleTable.fallback`, no match expression.

Child indices are the 0-based section order of the interface's own `.if`
(convention verified against `[role:report_button] match=iface(chat, 31)` →
`chatbox.if` section 31 = `reportabuse`).

### 1a. Roles that exist today

| role | kind | match expression | source component symbol | exists | needed by |
|---|---|---|---|---|---|
| `panel_inventory` | static | `id(if(149, 0))` | `inventory:items`' interface root | yes | world-interact |
| `panel_equipment` | static | `id(if(387, 0))` | equipment interface root | yes | world-interact (worn `use_on`) |
| `sidetab_3` | member | `id(if(161, 69))` / `id(if(548, 74))` / `id(if(164, 62))` / `id(if(601, 104))` — four `match=` lines | — | yes | world-interact, ui-misc |
| `chat_input` | static | `iface(chat, 57)` | `chatbox:input` | yes | chat-continue |
| `frame_chat` | static | four `match=` lines | — | yes | chat-read, events-core |
| `frame_main_modal` | static | four `match=` lines | — | yes | chat-read (`scroll.*`) |
| `[script:sidebar_switch]` | script | `id=915` | — | yes | ui-misc (`ui.tab`) |
| `[iface:chat]` | iface | `id=162` | `chatbox` | yes | all chat verbs |

### 1b. New `[iface:]` sections

All new. `id=` from `OSRS-Content/osrs239-content/pack/3_interfaces.pack`.

| section | id | needed by |
|---|---|---|
| `[iface:chat_left]` | `id=231` | chat-read, chat-continue |
| `[iface:chat_right]` | `id=217` | chat-read, chat-continue |
| `[iface:messagebox]` | `id=229` | chat-read, chat-continue |
| `[iface:messagebox_titled]` | `id=923` | chat-read |
| `[iface:messagebox_url]` | `id=917` | chat-read |
| `[iface:objectbox]` | `id=193` | chat-read, chat-continue |
| `[iface:objectbox_double]` | `id=11` | chat-read, chat-continue |
| `[iface:chatmenu]` | `id=219` | chat-options |
| `[iface:levelup_display]` | `id=233` | chat-read |
| `[iface:questscroll]` | `id=153` | chat-read |

### 1c. New roles — dialog pages

| role | kind | match expression | source component symbol | exists | needed by |
|---|---|---|---|---|---|
| `chat_modal_host` | static | `iface(chat, 567)` | `chatbox:chatmodal` | no | chat-continue, chat-options, chat-read, events-core |
| `dialog_npc_universe` | static | `iface(chat_left, 0)` | `chat_left:universe` | no | chat.kind, chat.text |
| `dialog_npc_head` | static | `iface(chat_left, 2)` | `chat_left:head` | no | chat.head, chat.expect_head |
| `dialog_npc_name` | static | `iface(chat_left, 4)` | `chat_left:name` | no | chat.name |
| `dialog_npc_continue` | static | `iface(chat_left, 5)` | `chat_left:continue` | no | chat.continue_, chat.drain |
| `dialog_npc_text` | static | `iface(chat_left, 6)` | `chat_left:text` | no | chat.text, chat.expect_text |
| `dialog_player_universe` | static | `iface(chat_right, 0)` | `chat_right:universe` | no | chat.kind |
| `dialog_player_head` | static | `iface(chat_right, 2)` | `chat_right:head` | no | chat.head |
| `dialog_player_name` | static | `iface(chat_right, 4)` | `chat_right:name` | no | chat.name |
| `dialog_player_continue` | static | `iface(chat_right, 5)` | `chat_right:continue` | no | chat.continue_ |
| `dialog_player_text` | static | `iface(chat_right, 6)` | `chat_right:text` | no | chat.text |
| `dialog_mesbox_universe` | static | `iface(messagebox, 0)` | `messagebox:universe` | no | chat.kind |
| `dialog_mesbox_text` | static | `iface(messagebox, 3)` | `messagebox:text` | no | chat.text |
| `dialog_mesbox_continue` | static | `iface(messagebox, 4)` | `messagebox:continue` | no | chat.continue_ |
| `dialog_mesbox_titled_universe` | static | `iface(messagebox_titled, 0)` | `messagebox_titled:universe` | no | chat.kind |
| `dialog_mesbox_titled_title` | static | `iface(messagebox_titled, 3)` | `messagebox_titled:title` | no | chat.text (titled variant) |
| `dialog_mesbox_titled_continue` | static | `iface(messagebox_titled, 4)` | `messagebox_titled:continue` | no | chat.continue_ |
| `dialog_mesbox_titled_text` | static | `iface(messagebox_titled, 5)` | `messagebox_titled:text` | no | chat.text |
| `dialog_mesbox_url_universe` | static | `iface(messagebox_url, 0)` | `messagebox_url:universe` | no | chat.kind |
| `dialog_mesbox_url_text` | static | `iface(messagebox_url, 1)` | `messagebox_url:text` | no | chat.text |
| `dialog_mesbox_url_continue` | static | `iface(messagebox_url, 2)` | `messagebox_url:continue` | no | chat.continue_ |
| `dialog_objbox_universe` | static | `iface(objectbox, 0)` | `objectbox:universe` | no | chat.kind, **and the resume target** (this pack has no continue child; `if_addresumebutton(objectbox:universe)`) |
| `dialog_objbox_item` | static | `iface(objectbox, 1)` | `objectbox:item` | no | chat.item, chat.expect_item |
| `dialog_objbox_text` | static | `iface(objectbox, 2)` | `objectbox:text` | no | chat.text |
| `dialog_objbox_double_universe` | static | `iface(objectbox_double, 0)` | `objectbox_double:universe` | no | chat.kind |
| `dialog_objbox_double_item1` | static | `iface(objectbox_double, 1)` | `objectbox_double:model1` | no | chat.item |
| `dialog_objbox_double_text` | static | `iface(objectbox_double, 2)` | `objectbox_double:text` | no | chat.text |
| `dialog_objbox_double_item2` | static | `iface(objectbox_double, 3)` | `objectbox_double:model2` | no | chat.item |
| `dialog_objbox_double_continue` | static | `iface(objectbox_double, 4)` | `objectbox_double:pausebutton` | no | chat.continue_ |

### 1d. New roles — options menu

`chatmenu`'s title and rows are `cc_create`d at runtime by
`chatbox_multi_init` / `chatbox_multi_addoption` under `chatmenu:options`, so they
have no `.if` section and only a `cc()` role can name them.

| role | kind | match expression | source component symbol | exists | needed by |
|---|---|---|---|---|---|
| `dialog_options_universe` | static | `iface(chatmenu, 0)` | `chatmenu:universe` | no | chat.kind |
| `dialog_options` | static | `iface(chatmenu, 1)` | `chatmenu:options` | no | chat.options, chat.choose |
| `dialog_options_title` | static | `cc(iface(chatmenu, 1), 0)` | none — `cc_create`d | no | chat.options_title, chat.choose |
| `dialog_options_row_1` … `_row_5` | member | `cc(iface(chatmenu, 1), 1)` … `cc(iface(chatmenu, 1), 5)` | none — `cc_create`d | no | chat.options, chat.choose |

### 1e. New roles — questscroll and levelup

| role | kind | match expression | source component symbol | exists | needed by |
|---|---|---|---|---|---|
| `dialog_quest_scroll_universe` | static | `iface(questscroll, 0)` | `questscroll:universe` | no | scroll.title |
| `dialog_quest_scroll_title` | static | `iface(questscroll, 4)` | `questscroll:quest_title` | no | scroll.title |
| `dialog_quest_scroll_icon` | static | `iface(questscroll, 5)` | `questscroll:quest_model` | no | scroll.rewards |
| `dialog_quest_scroll_points` | static | `iface(questscroll, 6)` | `questscroll:quest_points` | no | scroll.title |
| `dialog_quest_scroll_award_text` | static | `iface(questscroll, 8)` | `questscroll:award_text` | no | scroll.rewards |
| `dialog_quest_scroll_reward_1` … `_7` | member | `iface(questscroll, 9)` … `iface(questscroll, 15)` | `questscroll:quest_reward1..7` | no | scroll.rewards |
| `dialog_quest_scroll_close` | static | `iface(questscroll, 16)` | `questscroll:close_button` | no | scroll.close |
| `dialog_levelup_universe` | static | `iface(levelup_display, 0)` | `levelup_display:universe` | no | levelup.skill |
| `dialog_levelup_text1` | static | `iface(levelup_display, 1)` | `levelup_display:text1` | no | levelup.skill |
| `dialog_levelup_text2` | static | `iface(levelup_display, 2)` | `levelup_display:text2` | no | levelup.skill |
| `dialog_levelup_continue` | static | `iface(levelup_display, 3)` | `levelup_display:continue` | no | levelup.continue |
| `dialog_levelup_skill_<name>` | member | `iface(levelup_display, N)` — agility 4, attack 6, construction 9, cooking 12, crafting 14, defence 17, farming 19, firemaking 21, fishing 23, fletching 25, herblore 28, hitpoints 30, hunter 32, magic 34, mining 36, prayer 38, ranged 40, runecraft 43, slayer 45, smithing 47, strength 49, thieving 51, woodcutting 53, combat 55, sailing 57 | `levelup_display:<name>` | no | levelup.skill |

### 1f. Derived roles (`derive=`, no match expression)

| role | kind | fact the fallback computes | exists | needed by |
|---|---|---|---|---|
| `dialog_continue` | derived | the live node whose effective IF_SETEVENTS carries `RS_MINIMENU_EVENT_CLICK` under `chat_modal_host` — the same test `rs_minimenu_build.c:1073` makes per row | no | chat.continue_, chat.drain, levelup.continue |
| `pause_pending` | derived | `UITree_PausePendingIndex(tree)` — already tracked, zero new logic, identical on both lanes | no (role), yes (fact) | chat.continue_, chat.choose |
| `chat_modal_host` (rs289lc only) | derived | `app->slots.chat_com_id`'s node — dat1 has no interface-parent mount | no | every chat verb on the dat1 lane |

---

## 2. rs289lc roles

`revconfig/rs289lc/*.ini` declares 2 roles; the shared dat1 UI half
(`revconfig/rs245_2lc/rs245_2lc_dat1_ui.ini`) declares 17 more. dat1 ids are flat
cache-global component ids — never `iface<<16|child`. **dat1 child index is the
`com_N` name's number, not section order** (D12). Ids cross-checked against
`LostCity_Content2/pack/interface.pack`; the family is `id(<flat id>)`.

| role | rs289lc binding |
|---|---|
| `panel_inventory` | exists — `match=slot(sidebar, 3)` (`rs245_2lc_dat1_ui.ini:112`) |
| `panel_equipment` | exists — `match=slot(sidebar, 4)` (`:115`) |
| `sidetab_3` | not needed — dat1 answers `IF_SETTAB` directly; `app_plugin_tab_select`'s non-CS2 branch handles it |
| `chat_input` | unknown: dat1's chat log + input are ONE builtin widget (`[component:chat_region]` type=chat, `rs245_2lc_dat1_ui.ini:592-605`). Likely no component to bind; see U7 |
| `chat_modal_host` | `derive=chat_modal_host` → `app->slots.chat_com_id` (`src/game/rs_ui_slots.c:325`) |
| `dialog_npc_universe/head/name/text/continue` | per line count, 4 interfaces: `npcchat1 id(4882)` (com_0 model, com_1 name, com_2 line, com_3 continue), `npcchat2 id(4887)`, `npcchat3 id(4893)`, `npcchat4 id(4900)` (com_0 model, com_1 name, com_2..com_5 lines, com_6 continue). Member set needs `any(...)` |
| `dialog_player_*` | `chat1 id(968)`, `chat2 id(973)`, `chat3 id(979)`, `chat4 id(986)` — same shape, portrait mirrored right. com counts confirmed for chat1 (4) and chat4 (7); chat2/chat3 unread |
| `dialog_mesbox_*` | `message1 id(356)`, `message2 id(359)`, `message3 id(363)` (com_0..com_2 lines, com_3 continue), `message4 id(368)`, `message5 id(374)` |
| `dialog_mesbox_titled_*` / `dialog_mesbox_url_*` | unknown: no dat1 equivalent found; likely absent on this era |
| `dialog_objbox_*` | `objbox1 id(306)` (com_0 model, com_1 line, com_2 continue), `objbox2 id(310)`, `objbox3 id(315)`, `objbox4 id(321)`. Also `objbox1_special 5706 / objbox2_special 5710 / objbox3_special 5842 / objbox4_special 5848` with no caller found — see U10 |
| `dialog_objbox_double_*` | `doubleobjbox id(4950)` — com_0 model1, com_1/com_2 lines, com_4/com_5 lines, com_6 model2. Continue row unconfirmed, see U11 |
| `dialog_options` / `dialog_options_row_N` / `dialog_options_title` | static, one interface per option count: `multi2 id(2459)`, `multi3 id(2469)`, `multi4 id(2480)`, `multi5 id(2492)`. Title is `com_0`; rows are `com_1..com_N` (all `buttontype=normal`) |
| `dialog_item_choice_2/3/4` (new family, dat1 only) | `multiobj2 id(139)`, `multiobj3 id(4657)`, `multiobj4 id(4667)` — named children `obj1/obj2/objtext1/objtext2/title`, all `buttontype=normal`, no pause row. Same await class as the options menu, not the objbox display |
| `dialog_continue` | `derive=button_type(6)` — no id anywhere. dat1 bakes `buttontype=pause` into the `.if`; `if_button_action_for_type` already maps it to `RESUME_PAUSEBUTTON` |
| `dialog_quest_scroll_*` | `questscroll id(297)` — `com_2` is *Close Window* (`buttontype=close`), not a pause row; opened via `if_openmain` only. Journal body is `questjournal_scroll id(8134)` with named children `ifquestname`, `close`, `qj1..qjN` |
| `dialog_levelup_*` | one interface per skill, named children `line1/line2/continue/icon/icon2`: attack 6247, defence 6253, strength 6206, hitpoints 6216, ranged 4443, prayer 6242, magic 6211, cooking 6226, woodcutting 4272, fletching 6231, fishing 6258, firemaking 4282, crafting 6263, smithing 6221, mining 4416, herblore 6237, agility 4277, thieving 4261, runecraft 4267. Member set needs `any(...)` |
| `inventory_grid` | `inventory id(3213)` root / `inventory:inv id(3214)` (type=inv, 4×7) |
| `worn_grid` | `wornitems id(1644)` root / `wornitems:wear id(1688)` (type=inv, option1=Remove) |
| minimap + orbs | exist — `[component:minimap]`, `action_frame_orb_run_enable id(153)`, `action_frame_orb_run_disable id(152)`, `varp:run_mode id(173)`, `varp:special_attack_energy id(300)`. **No spec-attack button on this cache** → any spec verb returns `unsupported` |
| count entry / name entry | not a cache interface at all — engine overlay state (`RS_Chat.dialog_input_open`, `RS_Chat.social_input_open`). No role |
| `[script:sidebar_switch]` | n/a — dat1 has no CS2 script layer |

---

## 3. Events

All drive events are appended to one fixed ring on `struct App`, drained once per
frame at `App_PluginLayoutTick`, read by cursor. Every await is **EDGE + LEVEL**:
the predicate is checked when the await registers (satisfied → return without
yielding), then re-checked on each matching event and on each `server_tick`.

| event kind | stamp site (file:line) | payload | awaited by | exists today |
|---|---|---|---|---|
| `sub_opened` | `src/app/app_boot.c:1249` (`App_OpenSubInterface`) | `target_uid`, `interface_id`, `type` | ui.await_open (edge only — queued, not mounted) | seam yes, stamp no |
| `sub_mounted` | `src/app/app_boot.c:1198` (end of `Task_OpenSubRefresh_Run`, after `App_RefreshAfterTreeMutation`, before `PT_END`) | `target_uid`, `interface_id`, `type` | chat.continue_, chat.drain, chat.choose, chat.kind, chat.text/name/head/item, scroll.*, ui.open, ui.await_open | seam yes, stamp no |
| `sub_closed` | `src/app/app_boot.c:1170-1181` (close branch, old `group_id` captured **before** `UITree_ReclaimInterfaceGroup`) | `target_uid`, old `group_id` | chat.close, scroll.close, ui.await_close | seam yes, stamp no |
| `chat_opened` (dat1) | `src/game/rs_ui_slots.c:325` (`RS_UISlots_OpenChat`), from `src/game/rs_gameproto_exec.c:1312` | `component_id` | every chat verb on rs289lc | seam yes, stamp no |
| `slot_mounted` (dat1) | `src/engine/uitree_builder/task_slot_mount.c` (at `PT_END`) | `owner_index`, `iface_id` | ui.await_open/close on rs289lc | seam yes, stamp no |
| `resume_answered` | `src/app/app_cs2_flush.c:220` — carry the `com_id` already in scope | `com_id` | chat.continue_, chat.choose, levelup.continue | seam yes, scoped stamp no |
| `varp_changed` | `src/app/app_varp_transforms.c:555` (`app_varp_server_update`) | `varp_id`, new value | var.await, click_loc, use_on, inv_op | seam yes, stamp no |
| `inv_changed` | `src/app/app_ui_host.c:397-407` — `container_id` is `(void)`-discarded today | `container_id` | inv.await, click_obj, equip, drop, use_on, inv_op | seam yes, payload dropped |
| `obj_added` | `src/game/rs_gameproto_exec.c:457` (`PKT_NAME_OBJ_ADD` → `App_WorldObjStackAdd`) | tile, level, `obj_id`, count | drop | yes |
| `obj_removed` | `src/game/rs_gameproto_exec.c:475` (`PKT_NAME_OBJ_DEL`) | tile, level, `obj_id` | click_obj | yes |
| `map_flag` | `src/game/rs_gameproto_exec.c:2098-2115` (`PKT_NAME_UNSET_MAP_FLAG`, both branches) | `-1` on clear, else packed tile | walk_to, walk_near, idle, talk_to, click_loc | yes (handler), stamp no |
| `chat_message` | `src/app/app_plugin_api.c:403` (`App_NotifyChatMessage`) **and** `src/game/rs_chat.c:104` (`RS_Chat_AddMessage`) | type, sender, text, serial | msg.await, msg.expect, talk_to, click_obj, equip, drop, use_on | `App_NotifyChatMessage` yes; it misses clan chat and the logout line, so the ring must stamp at `RS_Chat_AddMessage` |
| `server_tick` | `src/game/task_gameproto_exec.c:442` **and** `src/game/rs_gameproto_exec.c:2199` (`PluginHost_ServerTick`) | world cycle | every await — this is the deadline clock | yes |
| `npc_spawn` / `npc_despawn` / `npc_retype` | `src/app/app_world_spawn.c:813`, `src/app/app_world_frame.c:64`, `src/app/app_world_apply.c:295` | npc slot | npc.*, walk_near, talk_to | yes (callbacks), stamp no |
| `inv_packet` (dat1) | `src/game/rs_gameproto_exec.c:1103, 1106` (`UPDATE_INV_FULL` / `UPDATE_INV_PARTIAL`) | container | inv.* on rs289lc | yes |
| `screenshot_ready` | polled, not stamped — `capture_path[0]` as `content_test.c:698-701` does | path | t.shot | yes |
| `TORIRS_WIDGET_BOUND` / `UNBOUND` | `src/plugin/torirs_plugin_host.c:7264-7288`, driven from `src/app/app_canvas_layout.c:666` | widget ref, role | any role-level await that wants "this role resolves now" | **yes, already raised** |
| `TORIRS_WIDGET_LOADED` / `CLOSED` | raised at `App_PluginLayoutTick` from `sub_mounted`/`sub_closed` ring entries | widget ref, role | nothing in phase 1 — for real plugins | declared, never raised |

---

## 4. Engine additions (beyond names and events)

| what | file | size |
|---|---|---|
| `api.drive` module: registration gated on `ContentTest_Enabled()`, the Lua table, the arg marshalling | `src/plugin/torirs_plugin_drive.c/.h` (new) | 250 |
| Coroutine scheduler: `lua_newthread` + registry anchor, `lua_sethook` re-armed on the **coroutine's own** state every resume (`lua_newthread` copies the hook only at creation, `3rd/lua/lstate.c:280-284`), `lua_resume`, fault routed through `lua_script_fault`/`lua_script_defer_disable`/`lua_script_flush_disable` (`src/plugin/torirs_plugin_lua.c:418-453`) | `src/plugin/torirs_plugin_drive.c` | 200 |
| Drive event ring on `struct App` + `App_DriveEvent` append + cursor read; drained at `App_PluginLayoutTick`; second scheduler pump after the drain (D8) | `src/app/app_plugin_api.c`, `src/app/app_canvas_layout.c` | 120 |
| `App_LocScreenPosition` — nearest on-screen scenery of a loc type | `src/app/app_plugin_api.c` | 60 |
| `App_ObjScreenPosition` — same for a ground stack; projects `stack->draw_position` | `src/app/app_plugin_api.c` | 55 |
| `App_PlayerScreenPosition` — optional, only if a phase-1 test targets a remote player | `src/app/app_plugin_api.c` | 55 |
| `App_MinimenuRowFind` — match a row by `(action, pick.kind, pick identity)` with a **wildcard action** for the collapsed use-item row; replaces the text-prefix `App_MinimenuRowCenter` (`src/app/app_plugin_api.c:343`) | `src/app/app_plugin_api.c` | 55 |
| Expose `opnpc_action_for_slot` / `oploc_action_for_slot` / `opobj_action_for_slot` (static today) | `src/game/rs_minimenu_world.h` | 15 |
| Click state machine (move → settle → press → release) with one extra frame of CmdBus drain latency for a plugin-driven push | `src/plugin/torirs_plugin_drive.c` | 90 |
| `app_plugin_world_op` — the `app_plugin_click_node` scratch-menu trick generalised to NPC/SCENERY/OBJSTACK picks, `TORIRS_REPORT`ed unconditionally | `src/plugin/torirs_plugin_bridge.u.c` (beside `:3959`) | 70 |
| `PLUGIN_WIDGET_MODEL` — read `struct App::if_heads[]` by `component_id`, return `{kind, id}` (`src/app.h:1533-1543`, stored by `app_if_head_store`, `src/app/app_if_models.c:274-300`) | `torirs_plugin_host.h` (enum + request struct), `torirs_plugin_bridge.u.c` (case), `torirs_plugin_contract.h` (public struct), `plugin_api.meta.lua` | 75 |
| `VarCIds.meslayer_mode` + per-revision mapping, read via `VarCManager_GetInt` | `src/game/varc_ids.h`, one call site | 20 |
| Serial field on `struct RS_ChatMessage`, assigned in `RS_Chat_AddMessage` | `src/game/rs_chat.h`, `src/game/rs_chat.c:104` | 10 |
| `App_LocalPlayerIdle(app, bool* out_idle)` — route pending + flag pending | `src/app/app_plugin_api.c` | 20 |
| `App_SetCameraPose` — extract `content_test.c:511-520`'s validation + write so both callers share it | `src/app/app_plugin_api.c`, `src/game/content_test.c` | 25 |
| `ToriRSServer_RunDebugprocForTest` — returns `ToriRSServerTriggerResult` instead of discarding it (`torirs_server_world.c:7787-7792` throws it away after an fprintf) | `src/torirsserver/torirs_server_world.c` | 25 |
| Content-symbol trampolines (`npc`/`obj`/`loc`/`component`/`interface`/`varp`/`varbit`/`stat`/`inv` by name, and `obj` id→name reverse) | `src/plugin/torirs_plugin_drive.c` | 60 |
| `revconfig` grammar: widen `RevConfigRoleMatcher`/`UITreeRoleMatcher` numeric value to a small list, parse `any(v1,…)` (recurse `revconfig_role_arg_split`, which splits one comma per call), carry through `role_matcher_to_tree` and `role_resolve_matcher` | `src/revconfig/revconfig.{c,h}`, `src/ui/uitree_role.{c,h}`, `src/engine/uitree_role_load.c` | 120 |
| `revconfig` grammar: `RCFIELD_ROLE_DERIVE` + `derive_fact` carried to `UITreeRoleEntry`; `App_RoleDeriveFallback` wired into `UITreeRoleTable.fallback` (consulted before the matcher chain, `src/ui/uitree_role.c:376-380`) | same + `src/app/app_role_derive.c` (new) | 130 |
| `tools/revconfig_roles_from_pack.py` + manifest, and `make -C src check-revconfig-roles` (shaped like `check-pt-switch`, `src/makefile:3960-3963`) | `tools/`, `src/makefile` | 200 |
| `t.finish` exit fence: a static finished-flag/code beside `max_frames` (`src/main.c:1527, 2016, 7370`) + an atomic-rename results file | `src/main.c`, `src/plugin/torirs_plugin_drive.c` | 40 |
| Lane-agnostic mount liveness helper wrapping `UITree_GroupPresent` | `src/app/app_plugin_api.c` | 15 |

---

## 5. Verb groups

### 5.1 events-core

Infrastructure. No verbs a test calls directly.

**`drive_event_ring`** — `App_DriveEvent(app, kind, com_id, group_id, value, text)`
appends; `PluginHost_DriveEvents(host, after_serial, out, cap, *count)` reads by
cursor. Fixed array on `struct App`, never allocates. `*count` reports the true
count even when truncated so the caller knows to re-poll; a cursor older than the
oldest surviving entry answers `refused` rather than silently skipping. Drained at
`App_PluginLayoutTick` (`src/app/app_canvas_layout.c:666`). Deadline 0.
Stamps: §3, every row marked "stamp no".
**Gate**: open then close a sub-interface under `TORIRS_CONTENT_TEST`, assert
`sub_opened` then `sub_mounted` in order with the right `target_uid`, and that a
second read with the returned serial sees nothing. A wraparound case asserts the
stale-cursor `refused` path.

**`await_scheduler`** — one coroutine per test. `coroutine` is not in the sandbox's
library list and `pcall` is removed (`src/plugin/torirs_plugin_lua.c:3721-3736`), so
the thread is created and resumed entirely from C. Per resume: re-arm the budget hook
on the **coroutine's** `lua_State*`, `lua_resume`, branch — `LUA_YIELD` means the
script called `drive.await(desc, deadline)`; `LUA_OK` means the test returned its
verdict; an error status routes through `lua_script_fault` → plugin disabled, message
readable via `PluginHost_Error` (`src/plugin/torirs_plugin_host.h:946`).
Pumped twice per frame (D8). Deadline unit: `server_tick`.
**Gate**: a fixture chunk that yields twice on deterministic predicates; a busy-loop
chunk with no await must still be killed by the step hook (proves the per-resume
re-arm is on the coroutine, not the parent); an erroring chunk must disable the plugin.

**`widget_loaded_closed`** — raise `TORIRS_WIDGET_LOADED`/`CLOSED` from the
`sub_mounted`/`sub_closed` ring entries at the layout fence, filtered by the mounted
group, reusing `plugin_widget_watch_call`. Not on the critical path (D7).
**Gate**: a watch on a role inside the opened interface sees `LOADED, BOUND, UNBOUND,
CLOSED` in that order; a watch on an always-mounted role sees none of the four.

**`revconfig grammar`** — `any(...)` and `derive=` per §4 and D10.
**Gate**: `any(10,11)` parses to a two-entry list; `any()` with 0 or >4 args is
rejected; an unknown `derive=` fact is reported (as `revconfig_parse_role_matcher`
already reports a malformed line) and leaves the role with no matchers; a `derive=`
role resolves through the fallback and never runs the matcher chain.

**`generator` + `check-revconfig-roles`** — regenerate `[iface:]`/`[role:]` from
`pack/3_interfaces.pack` + each `.compack`, diff against the committed file, and
statically check that every `derive=` fact is one `App_RoleDeriveFallback` knows.
**Gate**: bootstrap by introducing a deliberate drift **in a throwaway worktree**
(never the shared tree) and confirming PASS flips to FAIL.

### 5.2 pointer

| verb | signature | deadline | result set |
|---|---|---|---|
| `drive.screen_position` | `(target) -> result, {x,y,element_id}` | 1 | ok / not_found / not_visible |
| `drive.click_minimenu` | `(target, option) -> result, {row_text,row_action}` | 4 | ok / covered / no_row / timeout / not_found / not_visible |
| `drive.op` | `(target, option) -> result, detail` | 1 | ok / not_found / refused |
| `player.walk_to` | `(x, z) -> result, detail` | 20 | ok / refused / timeout |
| `player.walk_near` | `(target) -> result, detail` | 20 | ok / not_found / timeout |
| `drive.camera` | `(yaw, pitch, zoom) -> result, detail` | 1 | ok / refused |
| `player.idle` | `() -> result, detail` | 10 | ok / timeout |

**`drive.screen_position`** — NPC reuses `App_NpcScreenPosition`
(`src/app/app_plugin_api.c:131-198`, nearest-to-viewport-centre with a 12px margin).
Loc/obj/player need the three new readers (§4); the obj reader projects
`stack->draw_position`, not a tile centre. No await.

**`drive.click_minimenu`** — THE primary action. (1) `screen_position`;
(2) `CmdBus_PushMouseMove`; (3) let at least one frame render, **then** read
`app->world_pickset` for an item whose `element_id` matches — else `covered` (the
pickset is stamped at the render-time hover point, so checking before the move is
meaningless); (4) right press; the menu opens synchronously in the same `app_frame`
via `app_minimenu_open` (`src/app/app_minimenu.c:881`, from `app_frame.c:1113`);
(5) `App_MinimenuRowFind` by `(action, pick.kind, pick.id)` where the expected action
is `op*_action_for_slot(option-1)`, or `*_OP*6` for `'examine'`; (6) move to the row
centre, left press → `app_minimenu_run_option` (`src/app/app_minimenu.c:1996`) runs
the real dispatcher. Cadence budgets one extra frame for CmdBus drain (a push from
inside `on_frame_start` is not drained until the next loop iteration,
`src/cmd/cmdbus.h:4-9`), which is why the default deadline is 4, not 3.
Events: `server_tick` (re-check), level reads of the pickset and `minimenu.visible`.
**Gate**: spawn a known npc via `::cheat`, drive op 1, assert the server-side effect;
cross-check that the action-id match selects the same row `TORIRS_MINIMENU_DEBUG`
prints. Mutation (worktree only): shift the action lookup by one and confirm the wrong
op fires.

**`drive.op`** — the logged bypass. Fabricates a one-row `UIMinimenu` with the pick
fields `add_npc_rows` fills (`src/game/rs_minimenu_world.c:171-182`), swaps it in,
calls `app_minimenu_run_option`, restores. `TORIRS_REPORT`s every call. Pass a real
projection for the cross paint when one is available; `0,0` only as a documented
fallback (a world WALK/INTERACT action does read it, unlike the UI case).
**Gate**: unit test asserting the same packet a real row click produces; mutation feeds
a mismatched action index and the test must see the wrong op.

**`player.walk_to`** — absolute scene tiles → scene-local via `world->_base_tile_*`,
then `app_try_move` (`src/app/app_world_click.c:251`). A `0` return is `refused`, with
no await registered. Await: player tile == requested tile, on `map_flag` and
`server_tick`. `route_length` reaching 0 is necessary but not sufficient — a route can
be replaced mid-walk. The per-tick decrement lives in
`src/world/world_cycle.c:270-281`, not in `entity_pathing.c`.
**Gate**: `/state`'s x,z. Mutation: stub the flag-clear branch and confirm the verb
still resolves via `server_tick` — proving the two triggers are redundant.

**`player.walk_near`** — `app_try_move_npc` / `app_try_move_loc`
(`src/app/app_world_click.c:547, 660`), **re-issued every server tick** while the
await is pending, because the target can walk. Await: Chebyshev distance to the
target's *current* tile within the approach adjacency and route settled.
**Gate**: a genuinely wandering npc (not a stationary one, or the mutation is
invisible). Mutation: re-issue only once and confirm the wandering fixture times out.

**`drive.camera`** — shared `App_SetCameraPose`: normalise yaw, pitch in [128,383],
zoom in [-1000,10000], write `orbit` **and** `world_camera`, zero the velocities, set
`need_redraw`. Synchronous. **Gate**: read back `/state`'s camera fields; pitch 999 →
`refused` with no state change.

**`player.idle`** — movement idleness only: `route_length == 0` and
`minimap.flag_tile_x < 0`. Both are checked because they can settle a tick apart.
**Gate**: walk then idle, assert it resolves only after arrival. Mutation: a
multi-waypoint route must not resolve early on an intermediate dip.

### 5.3 world-interact

Every world target is a server-content symbol, so **no revconfig role**. The four
backpack verbs use `panel_inventory` + `sidetab_3` only to confirm the grid is the
mounted panel before a cell rect means anything. Ops are identified by **number**,
never by row text — the server binds `[opheld2,_] ~equip` and `[opheld5,_] ~dropslot`
on the op number regardless of the cache's verb string.

| verb | signature | deadline | result set |
|---|---|---|---|
| `player.talk_to` | `(npc, op?=1)` | 20 | ok / covered / no_row / not_visible / not_found / timeout |
| `player.click_loc` | `(loc, op?=1)` | 20 | ok / covered / no_row / not_visible / refused / timeout |
| `player.click_obj` | `(obj, op?=3)` | 15 | ok / covered / no_row / not_visible / refused / timeout |
| `player.use_on` | `(item, target)` | 25 | ok / no_row / covered / not_found / not_visible / refused / timeout |
| `player.equip` | `(item)` | 6 | ok / no_row / not_found / refused / timeout |
| `player.drop` | `(item)` | 5 | ok / not_found / refused / timeout |
| `player.inv_op` | `(item, op)` | 10 | ok / no_row / timeout |

**`talk_to`** — `drive.click_minimenu({kind='npc'}, op)`. Awaits, in order of
preference: `sub_mounted` under `chat_modal_host` (dialogue opened) → `ok`;
`chat_message` of type GAME → `ok` or `refused` depending on content; `map_flag`
cleared with nothing mounted → `ok` (a mute npc is a valid outcome). **The
no-dialogue predicate must be `UITree_GroupPresent`-based**, never `modal_host_uid`
(D4). **Gate**: one scripted npc and one mute npc; an out-of-range op → `no_row`.

**`click_loc`** — same shape against `UI_MINIMENU_PICK_SCENERY`. `app_try_move_loc`
refusing (no reachable approach tile) → `refused`. Completion: `sub_mounted`, or
`varp_changed`, or `map_flag` clear with nothing mounted.
**Open**: which screen point on a rotated multi-tile footprint (U3).

**`click_obj`** — default op **3**, not 1: the synthesized *Take* row is emitted only
when slot index 2 is empty (`src/game/rs_minimenu_world.c:645-652`), and most ground
stacks have an empty slot 0. Completion: `inv_changed` on the backpack with the total
for `obj_id` up. Refusal: a GAME message with no container change. `obj_removed`
(`PKT_NAME_OBJ_DEL`) tells the verb the stack vanished — no polling needed.
**Gate**: drop one obj, assert the total rises; a second fixture pre-fills to 28/28
in a setup step (never relying on scenario defaults) and asserts `refused`.

**`use_on`** — phase 1: click the item's cell; the unarmed cell's row is
`OPHELDT_START`, which sets `app->objsel.*` (`src/app/app_minimenu.c:1291`, inside
`app_minimenu_inv_action`). Confirm `objsel.active && objsel.obj_id == item_id`.
Phase 2: with a select mode armed, `add_world_select_row` /
`add_inv_slot_select_row` collapse the menu to **one** row whose action is
`USEHELD_ON*` — so `App_MinimenuRowFind` is called with a **wildcard action** and
matches on pick identity alone. That wildcard is part of the function's signature,
not an aside. The cell rect is resolved through content symbol `"inventory:items"`
(D11) → `UITree_FindByComponentId` → `UITree_ObjCellDynamicAtSlot` →
`UITree_LayoutGetBounds`, after clicking `sidetab_3` if `panel_inventory` is not
mounted. **Gate**: tinderbox + logs; mutation flips the target-mask test from `&`
to `|`.

**`equip`** / **`drop`** — `inv_op(item, 2)` and `inv_op(item, 5)`, but building the
**real** menu first so a non-equippable item comes back `no_row` instead of sending a
packet the server answers with a refusal line. Dispatch is
`app_minimenu_inv_action`'s `OPHELD1..5` case (`src/app/app_minimenu.c:1224`).
`equip` awaits `inv_changed` on the worn container; `drop` awaits the backpack total
falling, with `obj_added` at the player's tile as corroboration.
**Gate**: `equip` — a level-gated item → `refused`, then `ok` after `::setlevel`, with
the assertion reading container 94 directly rather than the cheat's own `say()` text.
`drop` — inside vs outside a drop-blocked zone.

**`inv_op`** — the general form. Op 1..5 → OPHELD family; a server-armed
IF_SETEVENTS bit or op 6..10 → `IF_BUTTON` with `action_index = op-1`
(`src/app/app_minimenu.c:1259-1281`). The driver must branch on `opt.action`
**as built**, never assume the family from the op number.
**Gate**: covered by equip/drop plus one bank-ladder case that must take the
`IF_BUTTON` branch. **Open**: `ok` here means "accepted and dispatched", not
"something changed" (U5).

### 5.4 chat-continue

On osrs239 every dialogue page mounts into `chat_modal_host` via the generic
IF_OPENSUB path; `RS_UISlots_OpenChat` and `rs_chat.c`'s input overlay are the
**rs289lc** mechanism and are dead on this lane (osrs239's packet table binds
neither `IF_OPENCHAT` nor `P_COUNTDIALOG`).

| verb | signature | deadline | result set |
|---|---|---|---|
| `chat.continue_` | `() -> result, detail` | 6 | ok / timeout / unsupported / not_found / refused / not_visible / closed |
| `chat.drain` | `(opts) -> result, kind` | 6/page | ok / timeout / (propagates) |
| `chat.close` | `() -> result, detail` | 6 | ok / timeout |
| `chat.count` | `(n) -> result, detail` | 6 | ok / unsupported / timeout / refused |
| `chat.name_entry` | `(text) -> result, detail` | 6 | ok / unsupported / timeout / refused |
| `chat.kind` | `() -> kind` | 0 | returns a kind directly |

**`chat.continue_`** — (1) resolve `chat_modal_host`; (2) read the mounted group and
map it to the page's continue role (`dialog_*_continue`, or
`dialog_objbox_universe` for the objectbox, which has no continue child); (3) confirm
the row is actually armed via `UIIfEventTable_Effective` — the same `RS_MINIMENU_EVENT_CLICK`
test `rs_minimenu_build.c:1073` makes — else `not_visible`; (4) if
`UITree_PausePendingActive` is already set, `refused`; (5) set
`app->host.resume_pausebutton_component_id` (the seam `content_test.c:555-559` already
uses, drained at `src/app/app_cs2_flush.c:209-220`).
Await release: the `sub_mounted` **edge** for `chat_modal_host` (a same-group remount
is the normal paging case and changes nothing observable, D5) **or** a
`resume_answered` scoped to the com id this verb armed (D6) **or** `sub_closed` →
`closed`.
**Gate**: drive each continue-shaped page and assert it advances; two fast calls →
the second is `refused`; calling it on an options page → `unsupported`. Mutation in a
worktree: delete the `PausePendingActive` guard and confirm the double-continue test
fails.

**`chat.drain(opts)`** — pure Lua loop over `chat.kind` + the continue seam.
`opts = {max_pages=40, stop_at=nil, per_page_ticks=6}`. Stops *before* clicking
`stop_at`. `count`/`name`/`other_input` are already non-continue kinds, so the loop
halts there without an opt-in. **Gate**: a multi-page conversation ending in an
options page, drained with `stop_at='options'`, must stop without clicking; `max_pages=1`
on a 3-page conversation → `timeout` naming the page it stalled on.

**`chat.close`** — sets `app->host.close_modal_requested` (`content_test.c:562`).
`app_cs2_flush_notifications` closes every modal/sidemodal locally in the same tick
(`src/app/app_cs2_flush.c:142-165`); the server-side
`ToriRSServer_WorldCloseModalEx` (`src/torirsserver/torirs_server_world.c:9912-9966`)
aborts any outstanding PAUSEBUTTON/COUNTDIALOG/NAMEDIALOG wait, which is why this one
verb also cancels a count or name prompt. Idempotent: already-clear → `ok`
immediately, never a hang.
**Gate**: open each page kind plus a count and a name prompt and close each.

**`chat.count(n)` / `chat.name_entry(text)`** — the osrs239 "dialog" is not a separate
interface: it is `chat_input` (`chatbox:input`), disambiguated only by the meslayer
mode carried in a VarC int. Read it via the new `VarCIds.meslayer_mode` +
`VarCManager_GetInt`, following `src/app/app_chat_focus.c:144-146`. Refuse to type
blind — without the mode read, digits go into ordinary chat and become a public
message. Compose keys with `CmdBus_PushKeyEvent` (`content_test.c:566-576`), then
ENTER (`content_test.c:581`), which runs `meslayer_enter` client-side →
`resume_countdialog` / `resume_namedialog` → `CS2VM_HOST_REQUEST_RESUME_*` →
`PKTOUT_NAME_RESUME_P_*`. Await: mode clears or `chat_modal_host` gets a new mount.
**Gate**: a withdraw-X prompt for `count`; `godwars_private.rs2`'s `p_namedialog` for
`name_entry`; calling `count` while an npc page is open → `unsupported` with no packet.

**`chat.kind()`** — synchronous. Reads the mounted group under `chat_modal_host` and
maps it: `chat_left`→npc, `chat_right`→player,
`messagebox|messagebox_titled|messagebox_url`→mesbox,
`objectbox|objectbox_double`→objbox, `chatmenu`→options, `levelup_display`→levelup,
`questscroll`→scroll. Nothing mounted → read the meslayer mode: count, name,
**`other_input`** (the ≥6 other modes `meslayer_enter` dispatches — friend/ignore
add-delete, clan name, world search, autotyper, quantity callback — which must not
masquerade as `none`), else `none`.
**Gate**: table-driven, one mount per kind, plus forced modes.

### 5.5 chat-options

`chatmenu` ships only `universe` and `options`; the title and 2–5 rows are
`cc_create`d at runtime, so a `cc()` role is the only way to name them. `find_all`
cannot serve the family (its numbering is the frame-slot member mechanism, not an
arbitrary role family) — `find` each row and stop at the first miss.

| verb | signature | deadline | result set |
|---|---|---|---|
| `chat.options` | `() -> result, {texts}` | 2 | ok / not_visible |
| `chat.options_title` | `() -> result, {title}` | 2 | ok / not_visible |
| `chat.choose` | `(selector) -> result, detail` | 5 | ok / no_row / not_visible / refused / timeout / closed |

**`chat.options` / `chat.options_title`** — zero new C: `widgets.find(role)` +
`widget:text()`. `PLUGIN_WIDGET_TEXT` reads any `UIELEM_RS_TEXT` node's string
(`src/plugin/torirs_plugin_bridge.u.c:4987`), which covers both the title (`iftype_text`)
and the rows, despite the meta doc calling it "text input". The readiness predicate
requires rows **1 and 2** to resolve (two is the floor; waiting on row 1 alone returns
`ok` mid-rebuild). RUNCLIENTSCRIPT is held until the tick fence
(`src/app/app_cs2_flush.c:71`, drained at `src/game/rs_gameproto_exec.c:2194`), so the
container can be mounted and still empty — `sub_mounted` alone is not the gate;
`server_tick` + `TREE_CHANGED` are. Title often carries the shared default header
string, which is a legitimate `ok`.
**Gate**: a real `p_choice3` line, asserting the returned array equals the strings the
`.rs2` proc passed. Mutation in a worktree: point row 2's `cc()` sub at 3.

**`chat.choose(selector)`** — `selector` is a 1-based row index or an exact row text.
Resolve against the live rows (`no_row` / `not_visible`), snapshot title+rows, then
**click through the resume path** (D1), not a fabricated op click: the rows are
`cc_create`d text with no cache ops and no button type, so a real click builds
`RESUME_PAUSEBUTTON` with `action_index -1` (`src/game/rs_minimenu_build.c:1073-1082`)
and goes through the one-resume-per-pause guard. `api.drive` must check
`UITree_PausePendingActive` itself and answer `refused` when a resume is outstanding,
or the verb can double-submit in a way a real player cannot.
Classification after the await: modal gone → `ok`; title/rows differ → `ok`;
**identical** title+rows → `refused, 'stale reopen'` (the server replays the same
options when `last_slot` lands outside the armed range — `chat.rs2:240-263`); nothing
changed by the deadline → `timeout`. Retained widget ids cannot be compared across
the rebuild (`CC_DELETEALL` runs on every `p_choice_open`, so the same row gets a new
runtime id) — only text can detect a stale reopen.
**Gate**: choose the same row by index and by text, assert the same server-side branch.
Mutation: make the send-instrumentation always report "sent" and confirm the `refused`
test fails.

### 5.6 chat-read

Reads only. All awaits compose on `widgets.watch` BOUND/UNBOUND plus a level check —
no new event wiring (D7).

| verb | signature | deadline | result set |
|---|---|---|---|
| `chat.text` | `() -> result, text` | 10 | ok / timeout / no_row |
| `chat.head` | `() -> result, {kind,id}` | 10 | ok / timeout / unsupported |
| `chat.name` | `() -> result, name` | 10 | ok / timeout / unsupported |
| `chat.item` | `() -> result, {slot,kind,id}[]` | 10 | ok / timeout / unsupported |
| `chat.expect_text` | `(sub)` | 10 | ok / not_found / timeout |
| `chat.expect_head` | `(npc)` | 10 | ok / not_found / timeout / unsupported |
| `chat.expect_item` | `(obj)` | 10 | ok / not_found / timeout / unsupported |
| `scroll.title` | `() -> result, {name,points}` | 8 | ok / timeout / not_visible |
| `scroll.rewards` | `() -> result, {lines,icon}` | 8 | ok / timeout / not_visible |
| `scroll.close` | `()` | 5 | ok / covered / no_row / timeout |
| `levelup.skill` | `() -> result, {skill,level_text}` | 8 | ok / timeout / no_row |
| `levelup.continue` | `()` | 5 | ok / no_row / covered / timeout |

**`chat.text`** — poll the seven `dialog_*_text` roles in order, return the first whose
`state().presented` holds. No engine change.

**`chat.head` / `chat.item`** — both need `PLUGIN_WIDGET_MODEL` (§4), which reads
`struct App::if_heads[]` by `component_id`. That array carries the **raw identity the
server sent** (`npc_id`/`obj_id`/`model_id`), written synchronously by
`app_if_head_store` on IF_SETNPCHEAD/IF_SETOBJECT — long before the composite is
built, so no second await is needed. `c->u.rs_model.gamecache_model_id` cannot answer
"which npc is this" and must not be used. `chat.item` returns one entry for objectbox
and up to two for objectbox_double.
**Gate**: a synthetic tree with a stored head entry asserts the exact stored id; a
real `~chatnpc` call asserts the live value. `chat.item`'s two-slot path is driven by
`::objbox2 <obj> <obj>` (`tutorial_boxes.rs2`'s `~doubleobjbox` debugproc) — it is
**not** untestable.

**`chat.expect_text(sub)`** — strips `<col=…>`, `</col>`, `<br>`, `<str>`, `<u>`,
`<shad=…>` in Lua (no client-side tag parser exists) and substring-matches.
**Gate**: assert the negative case explicitly (`not_found`, never a silent `ok`).
Mutation: comment out the strip and a colour-wrapped line must fail.

**`chat.expect_head(npc)` / `chat.expect_item(obj)`** — compare against
`ToriRSServer_ContentSymbol(PACK_NPC|PACK_OBJ, name)`. Both read the **widget's** bound
identity, never the active-npc snapshot — `chatnpc_specific` can show a different
npc's head than the npc that opened the conversation.
**Open**: shell vs multi-resolved npc id (U4).

**`scroll.title` / `scroll.rewards`** — plain text reads plus one model read for the
reward icon. Blank reward rows come back as empty strings, not omitted.
**Gate**: complete a short quest end-to-end via its `_selftest.rs2`.

**`scroll.close`** — `questscroll:close_button` carries a static `op1`, so it is an
ordinary numbered-op button (`add_menu_ops_rows` → `k_inv_button_action` →
`IF_BUTTON`), **not** a resume. Drive it with `ui.invoke` /
`app_plugin_if_click(component_id, 1)`; the server's
`[if_button1,questscroll:close_button]` calls `if_closesub`.
**Gate**: assert the modal slot is empty afterwards and a following `scroll.title`
returns `no_row`. Mutation: leave `op1` unarmed and the verb must time out.

**`levelup.skill` / `levelup.continue`** — `levelup_display` has **no caller anywhere
in this content tree**: every `[advancestat,*]` routes to the account-summary side
panel. Phase-1 gate is a synthetic-tree unit test (scan `own_hidden` across the 25
skill containers, exercise the pause-pending latch); re-gate against a real mount only
if the product decision is to open it. `levelup.continue` is a resume
(`levelup_display:continue` ships the same pause-arming onload literal as the chat
pages), so it uses the `chat.continue_` seam.

### 5.7 state

No revconfig roles: every value is a server-declared content symbol resolved through
`ToriRSServer_ContentSymbol` or a raw engine struct.

| verb | signature | deadline | result set |
|---|---|---|---|
| `var.varp` / `var.varbit` | `(name) -> result, value` | 0 | ok / not_found |
| `var.server` | `(name) -> result, value` | 0 | ok / not_found |
| `var.await` | `(name, value)` | 10 | ok / timeout / not_found |
| `var.expect` | `(name, value)` | 0 | ok / refused / not_found |
| `inv.has` / `inv.count` | `(name)` | 0 | ok / not_found |
| `inv.slot` | `(i)` | 0 | ok / not_found |
| `inv.expect_has` | `(name, count?)` | 0 | ok / refused / not_found |
| `inv.expect_absent` | `(name)` | 0 | ok / refused / not_found |
| `inv.await` | `(name, count)` | 10 | ok / timeout / not_found |
| `skill` | `(name) -> result, snapshot` | 0 | ok / not_found |
| `msg.last` | `(n)` | 0 | ok |
| `msg.await` | `(sub)` | 10 | ok / timeout |
| `msg.expect` | `(sub)` | 0 | ok / refused |

**`var.*`** — `VarPManager_GetVarp` / `GetVarbit` for the client value. `var.server`
and `var.expect`'s server half read `varps.var_serv[]`, the client's record of the
server-authoritative value (`src/varp/varp_manager.h:78-80`) — no embedded-server
handle and no new plumbing. A live embedded read is the fallback only if `var_serv`
proves too stale (U6). `var.expect` requires client == server == value; a client that
shows the right number while the server disagrees is the desync this verb exists to
catch, so that is `refused`, with the detail naming which side disagreed.
`var.await` translates a varbit name to its base varp for event matching
(`VarPManager_VarbitBaseVar`) because var events carry a **varp** id, and wakes on
`varp_changed` (the server-confirmed seam) plus `server_tick`.
**Gate**: fire a synthetic VARP packet at staggered ticks; a fixture with client and
server deliberately disagreeing must come back `refused`.

**`inv.*`** — container from content symbol `"inv"` (→ the backpack id);
`InvManager_Total` already sums a stack across slots, so `inv.count` reuses it rather
than re-walking. `inv.slot(i)` reverse-resolves the obj id to a **name** via
`ToriRSServer_ContentSymbolName` so tests compare names, not ids; `i` is a legitimate
caller ordinal, so out-of-range is `not_found`, not an assert. `inv.await` wakes on
`inv_changed` filtered by container id — which means wiring the `container_id` that
`app_inv_ui_host_change` discards today.
**Gate**: stacked vs unstacked fixtures; `expect_has`'s detail must distinguish
"absent" from "present but short".

**`skill(name)`** — `PACK_STAT` name → index, then `app->stats.*`. `stated = last_seen_level[idx] != 0`:
the pre-login table is a fresh account's, not empty. Reading `app->stats` directly
avoids binding a test-only module to `ToriRS_SkillSnapshot`'s `struct_size` contract.

**`msg.*`** — read `app->chat.messages` directly. **Not** `App_NotifyChatMessage` /
`PluginHost_ChatMessage`: those miss clan chat and the synthetic logout line, while
`RS_Chat_AddMessage` (`src/game/rs_chat.c:104`) sees every line. `msg.await` must be
scoped to messages inserted **after** registration — a 100-line ring easily holds a
stale match from an earlier quest step — which requires the new serial field on
`struct RS_ChatMessage` (it has none today; `RS_ChatNode.uid` is on the per-type ring,
a different object). This is a deliberate, recorded deviation from the blanket
already-true-satisfies rule.
**Gate**: push game/private/clan/logout lines and assert `msg.last` returns them
newest-first including the clan line; assert a stale pre-existing match does not
satisfy `msg.await`.

### 5.8 ui-misc

| verb | signature | deadline | result set |
|---|---|---|---|
| `ui.open` | `(interface)` | 10 | ok / no_row / refused / timeout |
| `ui.await_open` / `ui.await_close` | `(interface, ticks?)` | 20 | ok / timeout / no_row |
| `ui.widget` | `(sym, sub?)` | 0 | ok / no_row / not_visible |
| `ui.invoke` | `(widget, op)` | 5 | ok / not_found / refused |
| `ui.tab` | `(name)` | 5 | ok / refused / no_row |
| `ui.is_modal` | `()` | 0 | ok(true/false) |
| `npc.by_name` | `(name)` | 0 | ok / not_found |
| `npc.by_symbol` | `(sym)` | 0 | ok / not_found / no_row |
| `npc.nearest` | `(sym, radius)` | 0 | ok / not_found / no_row |
| `world.loc_near` | `(sym, radius)` | 0 | ok / not_found / no_row |
| `world.tile` / `world.level` | `()` | 0 | ok / not_visible |
| `t.cheat` | `(text)` | 1 | ok / refused / no_row |
| `t.key` / `t.text` | `(name)` / `(str)` | 1 | ok / no_row / refused |
| `t.shot` | `(name)` | 3 | ok / refused |
| `t.ticks` | `(n)` | n+2 | ok |
| `t.settle` | `()` | 30 | ok / timeout |
| `t.finish` | `(code)` | 0 | ok |

**`ui.open` / `ui.await_open` / `ui.await_close`** — liveness is
`UITree_GroupPresent(tree, interface_id)` (D3), never `UITree_InterfaceParentFind`
(which is keyed by container uid and cannot be queried by interface name) and never
`interface_parents` (CS2-only). `ui.open` composes `t.cheat(<debugproc>)` +
`ui.await_open`; `TRIGGER_NONE` → `no_row`, `TRIGGER_FAILED` → `refused`.
The await must stamp at the mount **task's** completion, not at the enqueue, or a test
can see a `target_uid` it cannot yet query.
**Gate**: register the await one frame before the cheat fires. Mutation: stamp at the
enqueue instead and confirm an immediate widget query reads stale.

**`ui.widget`** — content symbol → `UITree_FindByComponentId` (+
`UITree_FindChildBySubid`), reusing `content_test.c:137-145` verbatim, wrapped as a
`ToriRS_WidgetRef` so `get_text`/`state` work on the result.

**`ui.invoke`** — `app_plugin_if_click(app, component_id, op)` (D2). Lane-agnostic;
handles IF1 button types and IF3 numbered ops in one path. **Not** `trigger_op`.
**Gate**: a known button op with a server-side effect. Mutation: skip the settle and
confirm the op never dispatches.

**`ui.tab`** — name → tab number from the numbering the ini already documents, then
`app_plugin_tab_select(app, tabno)`, which branches CS2 (runs
`[script:sidebar_switch]`) vs dat1 (`RS_UISlots_TabEnabled` + `SetSideTab`) internally
— so no lane handling and no `sidetab_N` precheck here. `0` → `refused`. Confirm with
`cache.tab_active()`, an ungated read. Calling the raw seam rather than the gated
`api_tab_select` is deliberate: that wrapper's `dispatch_event` allow-list has no
entry for a coroutine resume on a bare frame tick (U9).

**`ui.is_modal`** — read `modal_host_uid`, then **re-verify** it is still live
(D4). A bare non-zero test is wrong at boot and permanently wrong after the session's
first dialogue.
**Gate**: open a modal → true; close → false. A variant that only checks
`modal_host_uid != 0` must fail the same scenario.

**`npc.*` / `world.loc_near`** — pool walks in the shape of `content_test.c`'s
`npc_json` / `scenery_json`, nearest by tile distance from `App_LocalPlayerTiles`.
`npc.by_symbol` matches `npc_id` **or** `base_npc_id` so a multiNpc wrapper resolves.
`npc.by_name` must normalise `<col=…>` tags — `WorldEntity_NPC.name` is sized 64
precisely because it stores the tagged form, so this is settled, not open.
`world.loc_near` must decide on non-root worldviews (U12).

**`world.tile` / `world.level`** — `App_LocalPlayerTiles` verbatim; its `false` return
maps to `not_visible`.

**`t.cheat`** — calls `ToriRSServer_ScriptsRunDebugproc` directly against the
in-process embedded server and returns the enum: RAN → `ok`, FAILED → `refused`,
NONE → `no_row`. `handle_cheat` computes this today and throws it away after an
fprintf (`src/torirsserver/torirs_server_world.c:7787-7792`); `App_SendCommand` only
reports whether the packet queued. Socket-server runs fall back to the packet path
with `detail.verdict='unknown'`.
**Gate**: a nonsense command → `no_row` with no stderr scraping; an out-of-range
argument to a real debugproc → `refused`, proving FAILED is distinguishable from NONE.

**`t.key` / `t.text` / `t.shot` / `t.ticks` / `t.settle`** — `t.key` uses
`torirs_keymap.c`'s named table (wider than `content_test.c`'s four names) then
push/char/release. `t.text` validates 1..63 printable ASCII up front. `t.shot` calls
`App_RequestScreenshot` and awaits the file, which is fulfilled asynchronously out of
the renderer's read-back. `t.ticks(n)` is a level predicate on the world cycle with
deadline `n + 2`. `t.settle` reuses `!App_AsyncPending && App_FrameSettled &&
!world_load_inflight` (`content_test.c:132-135`).

**`t.finish(code)`** — a new static finished-flag/code checked in the same branch as
`max_frames` (`src/main.c:1527, 2016`), plus an atomic temp-file+rename results file.
No other exit path answers to anything but `TORIRS_MAX_FRAMES` today.
**Gate**: `t.finish(1)` after a failed assertion must exit within a frame with the
code visible, not hang to the frame cap.

### 5.9 rs289lc lane

Data only — no verb in any group may branch on a lane name. Two structural facts drive
everything:

1. **dat1 has no IF_SETEVENTS arming.** The literal continue row on every dialog
   template carries a cache-native `buttontype=pause`, which
   `if_button_action_for_type` (`src/game/rs_minimenu_build.c:702-719`) already maps
   to `RESUME_PAUSEBUTTON` with no revconfig entry at all — so `dialog_continue` is
   `derive=button_type(6)` here and needs no ids. Applied in
   `RS_IF1_ApplyButtonClick` (`src/game/rs_if1_buttons.c:94-111`), which carries the
   same `PausePendingActive` guard.
2. **Option rows are indistinguishable client-side.** `multiN`'s `com_1..com_N` are
   `buttontype=normal` → generic `IF_BUTTON`; the fact that one of them resumes the
   paused script is armed server-side (`SS_OP_IF_ADDRESUMEBUTTON` → `resume_buttons[]`)
   and never reaches the client. So `chat.choose` on this lane matches by **static
   child index or row text**, never by action id, and its click is an ordinary
   `IF_BUTTON` send. Same for the `multiobj2/3/4` item-choice family.

Other lane facts: no varbit table in a dat1 cache; every id is flat and cache-global;
child index is the `com_N` name's number, not section order (D12); `questscroll`'s
close is `buttontype=close` → `CLOSE_MODAL`, a third derived variant; count entry and
name entry are engine overlay state (`RS_Chat.dialog_input_open` /
`social_input_open`) with no component at all — the count path is already fully
implemented, including the digit filter; there is **no spec-attack button** on this
cache, so a spec verb answers `unsupported`, never `covered` or `timeout`.

Bindings: §2. Mount/await events on this lane are `chat_opened`
(`RS_UISlots_OpenChat`) and `slot_mounted` (`task_slot_mount`), not `sub_mounted`.

**Gate**: every dat1 binding is provisional until a `TORIRS_DUMP_TREE` against
`manifest_rs289lc.ini` confirms the ids the running cache actually ships (U13).

---

## 6. Sequencing

| step | build | unblocks |
|---|---|---|
| 1 | Drive event ring + `App_DriveEvent` + the cursor read at the layout fence; stamps for `server_tick`, `sub_mounted`, `sub_closed`, `varp_changed`, `inv_changed` (with `container_id`), `map_flag`, `chat_message` | every await in every group |
| 2 | Coroutine scheduler, the `await` primitive, two-pump ordering (D8), fault routing; `api.drive` registration gated on `ContentTest_Enabled()` | every verb |
| 3 | Content-symbol trampolines + `t.cheat`/`t.ticks`/`t.settle`/`t.finish` | a test can run, drive content and report a verdict end to end |
| 4 | `state` group (var/inv/skill/msg reads), plus the `RS_ChatMessage` serial | every other group's assertions and completion predicates |
| 5 | `revconfig` grammar (`any(...)`, `derive=`), `App_RoleDeriveFallback`, the generator and `check-revconfig-roles` | every role-named verb; landing role data without drift |
| 6 | osrs239 role data: `[iface:]` sections + the dialog/options/scroll/levelup roles (§1b–1f) | chat-continue, chat-options, chat-read, ui-misc |
| 7 | `chat-continue` + `chat-options` (resume seam, scoped pause release, meslayer mode reader) | a quest test can hold a conversation |
| 8 | `chat-read` (+ `PLUGIN_WIDGET_MODEL`) | identity assertions on heads, items, scrolls |
| 9 | `pointer` (+ the three projectors and `App_MinimenuRowFind`), then `world-interact` | real clicks on the world and the backpack |
| 10 | `ui-misc` remainder (`ui.open/await/widget/invoke/tab/is_modal`, npc/world queries, `t.key/text/shot`) | panel-driven quest steps |
| 11 | Raise `TORIRS_WIDGET_LOADED`/`CLOSED` from the ring | real plugins; not a phase-1 blocker |
| 12 | rs289lc data pass: dump the tree, confirm ids, write the bindings, add the dat1 stamps (`chat_opened`, `slot_mounted`) | the second lane, with no code change |

---

## 7. Unresolved

| # | question | where to look |
|---|---|---|
| U1 | Is a loc target identified by its config `loc_id` or by a placed `element_id`? There is no existing "first live scenery of type X" helper to mirror. | `src/app/app_world_click.c` scenery pool iteration; `App_SimulateLocOp` (`app_plugin_api.c:70-90`) |
| U2 | Should the new projectors hard-refuse (`unsupported`) a deck/world-entity-view target, or project through it? | `app_world_project_actor`'s `placement->view_id` branch (`src/app/app_world_project.c:99-144`); the sailing worktree's tests |
| U3 | Which screen point on a rotated, multi-tile loc footprint — one centroid, or per-cell enumeration mirroring `add_scenery_rows`? | `rotate_extent_by_yaw`, `src/game/rs_minimenu_world.c:331-380` |
| U4 | Does `chat.head().id` return the raw server-sent npc id or the multi-resolved one? `app_if_head_poll` resolves it every poll but `app_if_head_store` never writes the resolved value back. | `src/app/app_if_models.c:274,704`; `App_NpctypeResolveMultiId` |
| U5 | `inv_op`'s completion signal is generic — an op with no observable effect times out identically to a dropped click. Per-op expected-result table, or accept that `ok` means "dispatched"? | owner decision |
| U6 | Is `var_serv[]` ever stale enough relative to a live embedded-server read to change `var.expect`'s answer? | `src/varp/varp_manager.h:78-80`; the VARP packet handlers |
| U7 | Does the dat1 lane's multi-choice dialogue render as a distinct component tree at all, or as text inside the single builtin chat widget? If the latter, `dialog_options_row_N` has nothing to bind to there regardless of cache mining. | `revconfig/rs245_2lc/rs245_2lc_dat1_ui.ini:592-621`; the rs289-era dialogue-choice implementation |
| U8 | Is `TORIRS_EMBED_SERVER` guaranteed for every lane a quest test runs against, or must the socket-server `t.cheat` fallback be a first-class result? | owner decision |
| U9 | Is bypassing `api_tab_select`'s `dispatch_event` gate acceptable for a test-only module, or should a resume be tagged as one of the permitted callback kinds so the same gated path plugins use is exercised? | `src/plugin/torirs_plugin_host.c:1354-1420` |
| U10 | What are `objbox1_special`..`objbox4_special` for? No caller found in `chat.rs2`. | grep the whole LostCity server script tree, not just `interface_chat` |
| U11 | Does `doubleobjbox.if` carry a pause row? Only the calling proc was read, not the `.if`. | `LostCity_Content2/scripts/interface_chat/interfaces/doubleobjbox.if` |
| U12 | Should `world.loc_near` include non-root worldviews (sailing decks) by default? `scenery_json` includes them unconditionally. | `src/game/content_test.c:235-241` |
| U13 | Are `pack/interface.pack`'s ids byte-identical to what `cache254.lostcity` ships? Every dat1 id is cross-referenced, not dumped. | `TORIRS_DUMP_TREE` on `manifest_rs289lc.ini` with a `::talk` cheat |
| U14 | `chat2`/`chat3` (and `message1/2/4/5`, `objbox2/3/4`) child counts were not re-read; only the 1- and 4-line templates were verified. | the `.if` files, counted the way `npcchat1`/`npcchat4` were |
| U15 | Which packet opens `questjournal_scroll`, and is it the right binding for a per-quest `questlist` role, or is the sidebar quest list a different interface? | a recorded quest-completion session; the dat1 sidebar interfaces |
| U16 | Should `levelup_display` ever be opened on osrs239? Nothing in the content tree mounts it; every `[advancestat,*]` routes to the account-summary side panel. Until that is decided, `levelup.*` can only be gated synthetically. | `OSRS-Content/osrs239-content/server/scripts/levelup/`; product decision |
| U17 | `messagebox_titled` and `messagebox_url` have no caller in `chat.rs2`. Should they fold into the `mesbox` kind or get their own? | the wider OSRS-Content script tree |
| U18 | Does the renderer interpret `<col=…>`/`<br>` at all on this revision, or draw them as literal glyphs? Changes what "the text the player reads" means for `expect_text`. | `src/ui` font/markup path; a rendered crop |
| U19 | The generator's input manifest (which interface/component pairs to emit roles for) is undesigned, and whether generated roles land in the hand-edited ini or a separate included file. | `src/engine/uitree_role_load.c:206-221` (the existing ini chain) |
| U20 | Where the key-name dispatch table recognises `match=`/`type=` inside a section, to add `derive=` beside it. | `src/revconfig/revconfig.c` near `RCFIELD_ROLE_MATCH` |
| U21 | Is `PLUGIN_LUA_STEP_BUDGET` generous enough for a quest test's per-resume slice? | `src/plugin/torirs_plugin_lua.c` |
| U22 | Does an equip that bumps a conflicting worn item produce one container event or two? | a two-hander-over-shield fixture |
| U23 | `wornitems.if` declares 14 slots but only 12 are used (`slot7`/`slot9` absent) — content gap or a real EquipSlot mapping difference? | `LostCity_Content2/scripts/player/interfaces/wornitems.if`; the EquipSlot enum |
