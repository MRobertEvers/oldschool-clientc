# Plugin engine handoff — 2026-09-06

> **Second session, 2026-09-07 (Claude, concurrent with the first session in the same worktree).** Batches accepted and committed on `codex/plugin-engine`, each with a receipt under `/private/tmp/plugin-engine-evidence/`: hidden native features (`nxt-hidden-features-checkpoint.json`, cbc3b5ee3, with a bridge loc-highlight resolver fix), essential panel plugins (`essential-panel-plugins-checkpoint.json`, 65fa172ff), panels and data views (`panels-and-data-views-checkpoint.json`, 9f0173795), Lua demos and probes (`lua-demos-checkpoint.json`, 9e99d977f, which raises the plugin server tick on fence-less 2004 lanes), and the Lua product acceptance of the tile indicators, entity highlighter, ground items and loot beam (`lua-product-acceptance-checkpoint.json`, 421f71743), and the loot tracker's rs289lc attribution with a real kill (`loot-attribution-checkpoint.json`, 9e1f2f335; recipe: `~addxp strength`, one dialog click, `~npc man`, menu Attack). A clean-checkout build of 9e1f2f335 in `/private/tmp/3draster-plugin-engine-verify` passed thirteen plugin test targets (`clean-checkout-verify-9e1f2f335.json`); the settings-driven hovered-tile highlight group was captured with varbit 12977 plus clientscript 5198 run headlessly (`nxt-highlight-settings-group-checkpoint.json`, 7eed67452). Open small gaps in the second session's rows: tile-marker overlap on one tile, ground-item timers and value tiers, the current/destination tile highlight rows, Screenshot's hover highlight. On 2026-09-07 the user asked for workflows: five multi-agent runs reviewed the gameframe port (seven defects, all confirmed and fixed by the frame session), captured the OSRS239 gameframe scenarios (`gfw-*`), removed the superseded execution APIs in an isolated worktree (dabf74abe, review fixes e0075db2e/f96846d72, sweep cdba5620d), added the platform-safe rect on the gameframe event (a1a4bf9ff), `find_all` member numbering (9b01e1b7f) and the owner-filtered clip release (9e3c40ae8), each stage checked by independent read-only verifiers and a clean-worktree gate (19 test targets; receipt `superseded-api-removal-checkpoint.json`). The chain was fast-forwarded onto `codex/plugin-engine`; the frame session switched both frame plugins onto `event->safe` (1b095499b). Every settings page was captured on both revisions, which found and fixed a painter clip defect (28fc7dd5d); the final clean-worktree gate on 28fc7dd5d passed nineteen test targets (`final-gate-28fc7dd5d.json`). Still nothing pushed; the push is the owner's call. New headless knobs: `TORIRS_SIM_MOVE_AT`, `TORIRS_SIM_PANEL_PICK`, `TORIRS_SIM_CLICK_NPC`, `TORIRS_SIM_MENU_ROW`; exit-dump lines `NATIVE_HIGHLIGHT`, `PLUGIN_PANEL*`, `PLUGIN_NOTIFY`, `PLUGIN_FEATURE_SET`, `PLUGIN_CANVAS_IMAGE`, `NATIVE_PLAYER`, `PLUGIN_SCENE_OBJECTS`. Coordination lives in the "Concurrent work claims" section at the top of `docs/plugin-engine/PLUGIN_PORTS.md`; the frame products and the superseded execution API removal are the first session's claim. A read-only map of the host frame pipeline is at `/private/tmp/plugin-engine-evidence/frame-pipeline-map.md`. Nothing has been pushed; the clean-checkout build gate still applies before any push.

> **Resumed 2026-09-06 (frame products session, later).** Branch `codex/plugin-engine` head `318dbe9f7`. Three contract slices landed with tree pins (each observed red under a stub): widget anchors (`widgets.set_anchor`, node-anchored OVER/BEHIND/REPLACE through the common `UITree_FrameReorder` pass), native re-skin (`set_image` on a native sprite/graphic/compass with size 0,0; `set_mask`), and provided gameframes (`on_gameframe` + `UITree_FrameProvide`: chrome suppressed, roles bound, nothing placed; PENDING re-asked each fence; containers above moved widgets released). `gameframe.c` (698f742c4..92a6fcbd3) and `mobile_gameframe.c` (318dbe9f7) are ported to provided frames; test-gameframe 92, test-mobile-gameframe 65. Live: all three desktop layouts on rs289lc and OSRS239 548 pass every pixel rule and were inspected at 2x (`/private/tmp/plugin-engine-evidence/gf-*`, receipt `gameframe-port-checkpoint.json`); Stone Drawer captures in flight (`gf-stone-*`). Open: six-piece scrollbar skin (no widget to re-skin), the OSRS popout safe area (`placement.primary` superseded), resize/tab/remount scenarios and the 40-run matrix. The superseded execution API removal is handed to session 3draster-15 (register claim section). OSRS captures MUST use `MANIFEST=/private/tmp/plugin-engine-prepared-actions.ini TORIRSSERVER_CONTENT=/private/tmp/plugin-engine-content/osrs239-content`; the shared repo's manifest is refused by the freshness gate while `OSRS-Content` carries live edits.
> **Resumed 2026-09-07 (frame products session, review).** Branch `codex/plugin-engine` head `147fff5d8` (+ register commit). Stone Drawer captured on both revisions; the 601 `orb_column_four_discs` miss was the fixture, not the frame (native 601 with every plugin off paints the special orb's plate over the run rim; the rule now skips rim points under a later orb's box, red on a 3px-shifted box). Seven review claims from session 3draster-15 checked and pinned (`test-gameframe` 104, `test-mobile-gameframe` 67): stone faces are separate owned images at natural size; stones/faces/icons chained OVER from the last piece with the surfaces OVER the last of them; `frame_reset_surfaces` before every plan; `frame_clear`/`mobile_clear` on `active` false. Found: the popout strip was never subtracted (nothing sets `TORIRS_UI_NODE_BLOCKS_FRAME`), only the phone keyboard band was; the provided event lacks it. 3draster-15 takes the host side (safe rect on `ToriRS_GameframeEvent`, `find_all` keeping member numbering) after the API removal; hold off `torirs_plugin_bridge.u.c` and host/types files until their hashes arrive, then switch both frame plugins from `event->height` to the safe rect. Recaptures after the fixes, all rules passing and 2x inspected: `gf-review-osrs-v1` (m03/m05/m08/m10/m39) and `gf-review-rs289-{classic,modern,resizable,stone}-v1`; receipt `gameframe-port-checkpoint.json` (`review_2026_09_07`). Later the same day: the 40-run matrix (`gf-review-matrix40-v1`) is 40/40 under the orb-rim rule's second revision (a rim point under any later draw command is not sampled; `7e2395af9`), and the popout strip turned out to be the profile role `lane_chrome_0` the builder's FRAME_BUILD had subtracted -- both frame plugins now subtract it themselves (`frame_usable_canvas`; pinned, red under a stub; `gf-review-strip-osrs-v1` m07 map at native 161's 607,8). 3draster-15 then landed the superseded-API removal, the event safe rect (`a1a4bf9ff`), `find_all` member numbering (`9b01e1b7f`) and the owner-filtered clip release (`9e3c40ae8`) as a fast-forward over `2b9ec4198`; both frame plugins now read `event->safe` (desktop: intersected before the strip; Stone Drawer: block hung from the safe bottom), pinned with a fake band and regression-recaptured on both revisions. Still open: scrollbar skin; clean-checkout build before any push (nothing pushed); the keyboard band itself has no headless capture.


> **Resumed 2026-09-06 (later session).** The unfinished owned-control patch in section 4 is finished and committed on `codex/plugin-engine` as `789995b6a` ("Arm owned plugin controls with native operations"). The compile error was a layer boundary: the public header now defines `TORIRS_WIDGET_OP_LABEL_MAX` and the bridge static-asserts it against the native menu option length. Review corrections: allocation asserted, teardown removal is an accepted no-op, stale-slot reclamation never evicts a live registration, host dispatch checks the exact owner/widget/serial. Lua `widget:set_on_op` plus metadata added with a 128-slot budget matching C. A native fix was needed: the app dropped clicks on owned controls because it admitted UI clicks by component id only; the interact result now also names the clicked node. Both the C demo and the Lua probe arm a control that invokes the native public-chat Friends action; clicked natively on OSRS239 and rs289lc (new accounts), inspected at 2x, harness rule `GF_MATRIX_WIDGET_OP=1`. Receipt: `/private/tmp/plugin-engine-evidence/owned-op-checkpoint.json`; register: `docs/plugin-engine/PLUGIN_PORTS.md` "Owned-control operations checkpoint". Section 10's diff is superseded by that commit. Second commit of the resumed session: owned image controls (`create_image`/`set_image`/`set_opacity`, C and Lua) and the Screenshot port off every superseded execution API; both camera modes clicked natively on OSRS239 and rs289lc with saved PNGs verified. Receipt: `/private/tmp/plugin-engine-evidence/owned-image-checkpoint.json`; register section "Owned image controls and the Screenshot port". Third commit `6fc330b33`: retained right-click menu proven natively on both revisions (row fires; re-armed listener retires the row), `TORIRS_MINIMENU_DEBUG` reports row bands, `GF_MATRIX_FORBID_LOG`. Fourth commit `b7f85778c`: minimap orbs ported onto owned image controls (covers interface 160 roots on OSRS, hangs beside the 2004 minimap on rs289lc; run orb pressed natively on both), with a native menu-sort fix so an owned control over a native button is the left-click default; receipts `owned-menu-checkpoint.json`, `orbs-checkpoint.json`. Commits `e52e719f0`, `fcd368721`, `d238b190b` (+ this one): XP drop orbs ported onto owned image controls, with the never-captured start-time skill-table freeze fixed; globes appeared and flipped natively on both revisions (receipt `xporbs-checkpoint.json`). A second session works in the same worktree on the panels/data-views batch (claims section at the top of `PLUGIN_PORTS.md`): stage only your own hunks of shared tool files. Next in order: gameframe/mobile_gameframe (frame products: decoration behind native surfaces needs an insert-behind owned image, minimap mask), remaining probe captures, then removal of the superseded execution APIs and their tests.

Recorded at approximately 22:32 UTC (17:32 America/Chicago). The user explicitly interrupted implementation and requested this single handoff document plus a resume prompt. Implementation is paused for that request. No cleanup, stashing, pushing, or server shutdown was performed. The full plugin migration goal remains unfinished; do not mark it complete or blocked on the strength of this handoff.

## 1. Workers and live processes

| Worker/process | Observed state | Ownership and next action |
|---|---|---|
| `/root` (primary assistant) | Sole agent in the current collaboration tree; writing this handoff | Resume the owned-control implementation described below. Last goal turn before the interruption made code changes; it was progress, not a wait. |
| Sub-workers | None. `collaboration.list_agents` returned only `/root` | No delegated patches or worker mailboxes to recover. Current instructions prohibit spawning sub-agents unless explicitly requested. Other repository worktrees are not evidence of my workers and must not be swept. |
| Build exec session `90268` | Re-polled after interruption: terminal, exit 2 | `make -C src test-plugin-host test-plugin-api`; log `/private/tmp/plugin-engine-owned-op-host-build.log`. First compiler error is the undefined `UITREE_MENU_OPTION_LEN` described below. Nothing is still compiling under this handle. |
| Pinned Lost City fixture | Live Python supervisor PID **62889**, Node child **63571** | Command: `tools/gameframe_lostcity_fixture.py serve --out /private/tmp/plugin-engine-lc-pinned`. Node is listening on TCP **43894** and HTTP **8982**, confirmed with `lsof`. This is the fixture used by current accepted rs289lc captures. Keep it unless a verified reason requires restart. |
| Older Lost City replay fixture | Live Python supervisor PID **58563**, Node child **58572** | Command: `tools/gameframe_lostcity_fixture.py serve --out /private/tmp/plugin-engine-lc-replay`. Not the current acceptance fixture. Leave it alone unless deliberately investigating it. |
| Recent native captures and verification jobs | Terminal; no current client/build job observed in the process audit | Latest accepted evidence is in the receipts below. Do not restart a job merely because a prior observation timed out. Recheck process handles/PIDs when resuming; these observations can age. |

The process audit also saw unrelated clangd/editor processes. Do not terminate them. No screenshots or native acceptance runs were performed for the new uncommitted owned-operation code.

## 2. Goal, decisions, and non-negotiable constraints

Full user goal: **port every plugin to the new system, ensure correctness, and capture screenshots of every plugin**. This includes C products, hidden native features, Lua runtime and shipped products, probes/demos, manifests, metadata, examples, tests, and useful user data. Registration under major 3 alone does not count as a completed port.

The user explicitly approved replacing the property-claim/bundle proposal with **RuneLite-style live widgets, events/listeners, native operations and explicit revalidation**, while keeping identity, lifetime and native-hide checks inside the host. Do not revive claims, bundles, whole-bundle conflict suspension, or preserve the old execution ABI. A new revision needs an adapter and native conformance evidence; no honest bug-free or automatic all-generation guarantee exists.

Read the complete implementation plan in the migration worktree:
`/private/tmp/3draster-plugin-engine/docs/PLUGIN_ENGINE_IMPLEMENTATION_PLAN.md`.
The originally supplied locally uncommitted plan is in the shared repository at the corresponding `docs/` path. The revised plan in the migration branch is the implementation contract reflecting the user's RuneLite decision.

Working constraints:

- Shared original workspace is very dirty. Inspect git first; never stash, reset, or sweep unrelated files. Latest audit counted **219** status entries before creating this handoff; that number is informational and can change through other work.
- Continue implementation in the isolated migration worktree. Do not conflate other repository worktrees with this task.
- **Use a different Lost City account for every connection, including retries.** The existing gameframe harness allocates new accounts; use it rather than reconnecting an earlier fixture account.
- Read mechanisms and substantiate them with instrumented native runs. Both actual rs289lc and actual OSRS239 paths are required, not synthetic substitutes alone.
- Extend `tools/gameframe_matrix.sh` and `tools/gameframe_pixels.py`; no competing capture harness.
- Inspect enlarged screenshots at 2× and count visible controls. A trace saying an element exists is not proof it is visible or operational.
- Prove relevant regressions fail with the mechanism broken. Compiler failures are not negative-control evidence.
- Preserve native hiding, including RS2 server hiding, and native content/actions through edits and reset.
- Reproducible launch/content freshness is an acceptance gate. No unexplained bypasses. Exact final commits must build in clean checkouts before pushing.
- Routine implementation decisions are already authorized. Do not ask for confirmation of the accepted architecture.
- Keep progress reports concise and distinguish verified behavior, intended behavior, and remaining gaps.
- Use the failure ledger internally; never append “LNN” bookkeeping to user messages.

Prior memory files already read earlier in this thread:
`~/.claude/projects/-Users-matthewevers-Documents-git-repos-3draster/memory/gameframe-cs2-layout-measured.md`
and `gameframe-design-failure-ledger.md` in the same directory.

## 3. Authoritative workspace and commit state

| Location | State/purpose |
|---|---|
| `/Users/matthewevers/Documents/git_repos/3draster` | Shared original; branch `gameframe-2004-osrs239`, HEAD `e3d110a7a` at audit. Numerous unrelated Krait/sailing/UI/content edits. This handoff is a new uncommitted file here for durability/discoverability. |
| `/private/tmp/3draster-plugin-engine` | Active implementation worktree, branch **`codex/plugin-engine`**, HEAD **`c0f08afe46cca056dc845c67491ae20a123a652f`**. Contains the unfinished owned-operation patch below. |
| `/private/tmp/3draster-plugin-engine-verify` | Detached at the same **`c0f08afe4`**, verified clean at audit. It does **not** contain the new uncommitted owned-operation patch. |
| `/private/tmp/plugin-engine-content` | Prepared content worktree, previously pinned to `26aba4540c0f87014e851969f4714148e16f65de`; revalidate before changing it. |
| `/private/tmp/plugin-engine-evidence` | Captures, fixture receipts, source snapshots, logs, enlarged inspections, negative controls. Preserve this directory. |

Nothing from this migration has been pushed. No completion claim or final release gate has been made. Do not treat PR #82's earlier 40-case matrix and 13 native scenarios as comprehensive mutation correctness.

Recent committed checkpoints, newest first:

- `c0f08afe4`: run pending role-probe startup actions on native binding/config events; fixes the observed slow-login race.
- `10566b3d7`: checked native widget action queries/invocation in C and Lua; role probe port; native chat modes survive tree remount.
- `5253eeb6e`: Cannon Ammo tracks native coordinate and null; no false empty notification after pickup/replacement.
- `920ede30d`: native Ground Items caption height, row spacing, and text outline.
- `5edbc681e`: checked synchronous native CS2 callbacks for C and Lua.
- `bf227b7b4`: live widget hiding and tree publications for native captions.
- `da73937a4`: world overlays use live scene origin; retained Ground Items action intent/data preserved.
- `1d7e1bbd8`: Performance Display owned widgets and scoped menus.
- `60f946515`: owned live text widgets.
- `e9f009b0c`: safe binding subscriptions and correct sidebar group movement.
- `9862b1215`: production major-3 live geometry path.
- `31be89c58`: abandoned unshipped claim API removed.

Do not stage the pre-existing task-worktree generated/binary dirt:

```
3rd/rscache/tools/cachepack/cachepack
3rd/rscache/tools/cs2/cs2
3rd/rscache/tools/cs2/__pycache__/local_commands.cpython-313.pyc
tools/cs2_gen_opcodes/__pycache__/local_opcodes.cpython-313.pyc
tools/cs2_gen_opcodes/__pycache__/opcode_docs.cpython-313.pyc
```

The `/private/tmp` worktrees/evidence have persisted through this session but are temporary paths. The branch commits remain in the shared repository's git database. An exact copy of the unfinished source diff is embedded at the end of this document, so it can be recovered if the temporary worktree disappears. Do not apply it twice if those edits are already present.

## 4. Exact interruption point: unfinished owned controls

**Current uncommitted code does not compile yet.** The first and only observed build error is:

```
src/plugin/torirs_plugin_runtime.inc:3682:58:
error: use of undeclared identifier 'UITREE_MENU_OPTION_LEN'
```

That constant belongs to the UI tree header and is currently not visible in the plugin runtime include context. Resolve the layer boundary deliberately—e.g. define a public label limit with a native static assertion, or use an appropriate existing shared constant. Do not just assume the rest builds after fixing this. The failed host/API test command did not validate the implementation.

The uncommitted patch touches exactly these 10 source files (122 additions, 4 removals at audit):

```
src/app.c
src/game/rs_minimenu_build.c
src/game/rs_minimenu_build.h
src/plugin/torirs_plugin_bridge.u.c
src/plugin/torirs_plugin_contract.h
src/plugin/torirs_plugin_host.c
src/plugin/torirs_plugin_host.h
src/plugin/torirs_plugin_runtime.inc
src/ui/uitree.c
src/ui/uitree.h
```

What was just written, **intended but not verified**:

- Public `widgets.set_on_op(context, widget, label, listener, user)` for an owned widget; null listener removes its operation.
- `UITreeComponent.plugin_op_serial` and `UITree_WidgetSetOperation` store a host listener version and owned menu label. The serial participates in native action signatures, so replacing a listener should retire retained menu entries.
- A native `RS_MINIMENU_ACTION_PLUGIN_WIDGET` (`CLIENT_BASE + 8`) is default-click eligible. `add_component_rows` creates this row for owned widgets, stamps exact node identity, and avoids sending owned controls through native packet operations.
- Host `PluginWidgetOp` registration table, capacity 128 per owner; callback dispatch by owner + checked widget + serial through `PluginHost_WidgetOperation`.
- Host teardown frees operation registrations before stop callbacks. Registration is forbidden during paint/shutdown. Full-table registration tries to reuse a stale widget reference.
- App dispatch routes the new menu action to the owner listener after the ordinary retained native-pick checks. No separate hit-testing system was introduced.

What has **not** been done for this slice:

1. Fix compiler error and inspect subsequent errors.
2. Review correctness of registration replacement/removal, reentry, allocation failures and cleanup. No tests yet cover any of these additions.
3. Add the Lua `widget:set_on_op(label, callback)` wrapper and metadata. The current C/Lua API inventory will fail until they agree.
4. Add tests covering real native hit/default/menu routing, stale listeners/menu entries, native hiding, foreign ownership, self-disable/reload during callbacks, node removal/reuse, and bounded registration cleanup.
5. Add a visible owned control to the C/Lua widget examples. Existing `create_text` is the proposed first control surface; native text widgets become interactive when the owned operation is armed.
6. Run actual native click captures on both revisions and observe broken-mechanism failures.
7. Add owned image/style/resource handling needed to port Screenshot without losing its camera artwork and placement modes.
8. Actually port `script/plugins/screenshot.lua` off its old `ui_contributions`, `api.ui.*`, `on_ui_node_draw`, and `on_ui_node_action`. It has not been edited in the current slice.

Potential review points, not established fixes: the new host operation dispatcher assumes App supplied a current widget; the app path does the native checks first. Examine whether every entry point enforces that assumption. Removal of stale registrations should not leak Lua closures after repeated native remounts. Ensure copying native content never copies `plugin_op_serial`. Preserve identity during mouse press/release and retained menus. Do not create a second public execution layer to get the Screenshot port compiling.

Mechanisms read immediately before interruption:

- `UITree_WidgetCreateText` creates an owner/key-scoped native text child, component ID -1, with a checked incarnation. It currently supplies geometry/text/color/align, not an image or a button appearance.
- `UITree_MenuOptionsMut` allocates and can return null; the new setter checks that before writing.
- Native hit testing already treats menu option text as interactive. The new custom action must travel through that same path.
- Existing binding callbacks establish/restore host dispatch context. The operation callback implementation followed that pattern but still needs teardown/reentry tests.
- Lua has `LuaWidgetWatch` with a registry function reference and 32 semantic subscriptions. An operation registry must preserve C/Lua authority/lifetime parity; do not silently limit Lua to a smaller incompatible control budget.
- Plugin image resources already publish through `app_plugin_image_publish[_argb]`; scene IDs use `UITREE_SCENE_PLUGIN_IMAGE_BASE + image_slot`. `plugin_v2_runtime_image_slot` validates public image tokens. Avoid unchecked resource IDs in a future widget-image setter.
- Host teardown resets owned widgets before dropping plugin image resources. Resource replacement/release while a widget remains live still needs explicit verification.

## 5. Verified architecture at the last committed checkpoint

Public API major 3 is `ToriRS_Api` / `ToriRS_PluginDef` / `PluginHost_Register`. No old public V2 registration header/ABI is accepted. Many private v2 identifiers and old public frame/panel/named-UI execution builders remain and must be removed after their consumers are ported. Do not count those consumers complete merely because their aggregate type says major 3.

Committed widgets:

- Opaque `(tree instance, index, incarnation)` references, checked every retained use.
- Shared profile lookup, current children/parent, text and geometry reads; native-parent positioning/sizing, explicit revalidation, reset.
- Owner-scoped narrow presentation edits; last setter per property; reset reveals remaining owner/current native state, not a startup snapshot.
- Additional plugin hiding cannot reveal native/server-hidden content. Copying native content does not transfer edits.
- Owned text children, text/color/align/remove. Full interactive owned controls are the unfinished slice above.
- Semantic `watch` bound/unbound and structural `watch_tree`; owner lifetime and dispatch snapshots prevent newly enabled/restarted subscriptions joining an in-progress dispatch. Pure geometry/hiding does not spuriously publish tree changes.
- C/Lua `visible`, `actions`, `invoke`. Native component action queries regenerate native rows; retained refs include current native action signature, row contents/targets and effective server mask. Normal native dispatch handles IF1 semantics and IF3 operations. Paint, shutdown and synchronous script callbacks cannot invoke actions.
- Native component actions are not a complete inventory-grid/chat-line selection API. Keep those remaining requirements open.

Committed CS2 integration:

- Opcode 6599 `RUNELITE_CALLBACK`; synchronous, non-yielding, non-reentrant.
- Scoped typed script argument/result access in C/Lua. No raw VM memory exposed.
- Approved `groundItemCaption` hook in script 7232; original script bytes/helper 7225 pinned, prepared cache patch fingerprint checked at runtime. Unknown/mismatched integrations are refused. rs289lc reports CS2 callbacks unavailable.
- Eleven integer slots from top: ignore 0, highlight 1, row offset 2, row count 3, row index 4, edit mode 5, color 6, native value 7, quantity 8, object ID 9, packed coordinate 10; one caption string. Only row offset/color/string are writable. Event includes the checked current caption widget.
- `scripts.invalidate` coalesces native producer refresh and works during shutdown; it is not arbitrary script scheduling. General native script execution/pre/post events and client-thread scheduling remain incomplete.
- Ground Items custom projection height, spacing and outline now use native caption construction/layout. Native auxiliary controls measure the modified caption. Full filtering/order/overflow/timer combinations remain open.

Other verified fixes:

- Native root remount refires ground-item overlays.
- LootStore auxiliary-list revisions refresh native Ignore/Highlight feedback immediately.
- Live scene origin fixes mid-session startup for world overlays.
- Performance Display uses four owned text widgets; settings and lifecycle captured on both revisions.
- Cannon Ammo tracks coordinate, treats zero/null as no cannon, and seeds new identity without inventing an ammo-loss event. Native pickup before/after screenshots prove removal of its false empty message.
- `RS_UISlots_RebindTree` preserves current chat privacy modes while resetting tree-owned slot identities. Later server/native updates remain authoritative.
- The role probe uses live widgets/actions and reacts to BOUND/config events. A regression reproduces binding after 200 ticks; completed startup actions do not replay after remount; enable starts a new request.

## 6. Evidence and limitations

Latest full receipt:
`/private/tmp/plugin-engine-evidence/widget-actions-clean-checkpoint.json`.
It records clean commit `c0f08afe46cca056dc845c67491ae20a123a652f`, binary SHA-256
`a840d5da52e30fa2c2696f915ff686eb1bdaaf9abda9f86fcb68d3bec9a2f192`, build/test logs, per-capture source revisions and image hashes. The C remount capture in that receipt was made at `10566b3d7`; the subsequent fix changed Lua startup behavior, not the C binary logic.

Accepted enlarged captures most relevant to resumption:

| Capture directory (under evidence root) | Inspected result |
|---|---|
| `widget-actions-clean-lua-osrs/m01` | Five cyan probe boundaries; Public Friends yellow; hidden action rejected; 8 chat controls and 14 sidebar buttons. |
| `widget-actions-clean-rs289-v2/r01` | Same live Lua source; Public Friends; hidden action rejected; 4 chat controls and 13 sidebar buttons; fresh account. |
| `widget-actions-clean-osrs/m01` | C owned Strength label; Public Friends retained through root 164 remount; prior action rejected with result 2/stale reference; 8 chat controls, 13 sidebar buttons plus top logout X. |
| `widget-actions-c-rs289/r01` | C native action changes Public Friends; one Strength label; 4 chat/13 sidebar. |
| `layout-clean-osrs/m01` | Two raised outlined native Ground Items captions, 30-pixel row spacing, two aligned native controls; 8 chat/14 sidebar. |
| `layout-clean-rs289/r01` | One complete raised outlined Chocolate cake caption, unrelated arrow label clipped at left edge; 4/13. |
| `cannon-native-empty/m01` | Native Empty unloads 15 balls; one plugin out-of-ammo line plus a separate native content message. |
| `cannon-native-pickup/m01` | Native pickup returns parts/ammo without the old false plugin empty notification. |

Each directory contains `out.bmp`, `log.txt`, `pixels.txt`, an `inspection-2x.png`, and associated fixture records higher in the tree. Earlier receipts: `layout-clean-checkpoint.json`, `cannon-checkpoint.json`, `script-clean-checkpoint.json` under the same evidence root.

Latest verification logs:

```
/private/tmp/plugin-engine-widget-actions-clean-final-build.log
/private/tmp/plugin-engine-widget-actions-clean-final-tests.log
/private/tmp/plugin-engine-widget-actions-clean-build.log
/private/tmp/plugin-engine-widget-actions-asan.log
/private/tmp/plugin-engine-widget-action-negative.log
/private/tmp/plugin-engine-role-binding-negative.log
```

397 host checks passed at the action checkpoint; C API compilation, Lua API inventory/runtime/product tests, native menu tests passed. ASan host/Lua passed before the Lua-only slow-binding correction; normal Lua tests verified that correction. The new uncommitted owned-operation code has none of this verification.

Observed negative evidence:

- `widget-action-negative`: native action signature and native availability deliberately broken; named assertions fail.
- `role-binding-negative`: removing BOUND execution fails the late-native-binding assertion.
- `widget-actions-clean-rs289` (without v2): failed clean launch timing case; both old diagnostic ticks elapsed before binding. Keep as failure evidence, not acceptance.
- `widget-actions-c-remount-v2`: native privacy mode reset to On on root rebuild; v3 passes after the native rebind fix.
- `roleprobe-actions-osrs` (without v2): missing Public mapping; native state/color checks reject it.
- `cannon-product-negative`: coordinate and null guards broken separately; intended assertions fail.
- `cannon-native-pickup-before`: old implementation visibly reports empty after pickup; fixed capture removes it.
- Caption projection, outline, spacing, callback timing/lifetime and script fingerprint mismatch negatives are recorded in the earlier receipts/register.

Known gaps/failures that must survive the handoff:

- **Full migration is still substantially incomplete.** Panels, frame products, orbs, screenshot UI and many hidden-feature acceptance rows remain.
- The Report probe returns accepted native dispatch on OSRS239 but the fixture shows no report dialog. Do not call that end-to-end success. rs289lc's current builtin has no report operation; do not fake one.
- Native cannon model remains visible after pickup in both old and fixed notification builds, despite returned parts. Native loc/render removal is still an open issue.
- Root 164 small-canvas native orbs overlap the sidebar; it is visible in accepted targeted captures and remains a native baseline limitation.
- Ground Items full filtering/order/overflow/auxiliary timer behavior remains open; two-item captures do not prove arbitrary piles. Attempted third whip drop remained in inventory; never describe those images as three-item tests.
- General line alpha and the legacy zero-alpha rectangle/outline convention remain to reconcile.
- D3D9 outline source changed but Windows compile/runtime proof is still missing. GLES2 UI compiled with emcc; that does not prove all GPU text rendering. Software outline pixels and native/GL3 build were verified.
- Allocation failures, focus/drag/retained-menu combinations, queued lifecycle/resource changes, all roots/adapters/frontends/performance and data migration gates are not closed.

## 7. Consumer status snapshot

This table is copied from the current port register for a single-document handoff. It is deliberately not a “done” list. Details and later checkpoint sections in `docs/plugin-engine/PLUGIN_PORTS.md` and `m0-consumers.json` refine the older table cells. In particular, the C widget demo now also has the verified native action/remount results above, and the role-probe cold-start fix is committed.

| Consumer | Current state | Captures |
|---|---|---|
| `script/plugins/_beamprobe.lua` | common logic tick and live settings port; both-revision column captures inspected; full release gate pending | `beamprobe-port-osrs/m01`, `beamprobe-port-lc/r01` |
| `script/plugins/_drawprobe.lua` | live-world coordinates and rectangle opacity fixed; both-revision captures inspected | `drawprobe-alpha-osrs/m01`, `drawprobe-alpha-lc/r01` |
| `script/plugins/_gicount.lua` | live scene origin; labels no longer restricted to log frames; OSRS capture produced, LC pending | `gicount-port-osrs/m01` |
| `script/plugins/_giprobe.lua` | live scene origin; both revisions survive disable/re-enable with two labels and one tile marker | `giprobe-origin-osrs/m01`, `giprobe-text-lc/r01` |
| `script/plugins/_hoverprobe.lua` | major-3 graphics; OSRS hover marker inspected; LC pending | `hoverprobe-port-osrs/m01` |
| `script/plugins/_hullprobe.lua` | graphics probe; both-revision two-shape captures inspected; full release gate pending | `hullprobe-port-osrs/m01`, `hullprobe-port-lc/r01` |
| `script/plugins/_paneldemo.lua` | pending product acceptance; major-3 registration alone is insufficient | Pending |
| `script/plugins/_probe.lua` | pending product acceptance; major-3 registration alone is insufficient | Pending |
| `script/plugins/_roleprobe.lua` | live widget/watch/action port; both native revisions inspected; Report endpoint and full release gates remain open | `roleprobe-actions-osrs-v2/m01`, `roleprobe-actions-rs289/r01`, `roleprobe-report-osrs/m01` |
| `script/plugins/_windemo.lua` | pending product acceptance; major-3 registration alone is insufficient | Pending |
| `script/plugins/entity_highlighter.lua` | menu port fixes retained species/action; OSRS highlight checked; LC spatial/occlusion and native menu acceptance pending | `entity-visible-osrs/m01`, `entity-visible-lc/r01` |
| `script/plugins/ground_items.lua` | native caption controls and custom height/gap/outline implemented; full filtering/order, timer combinations and release acceptance remain open | `ground-text-osrs/m01`, `ground-partyhat-lc/r01`, `ground-caption-controls/m01`, `ground-caption-live-native/m01`, `ground-caption-remount/m01`, `ground-hide-rs289-stats/r01`, `callback-caption-controls-v2/m01`, `callback-caption-disable-v2/m01`, `callback-ignore-refreshed/m01` |
| `script/plugins/loot_beam.lua` | major-3 scene API; real dropped-item beams inspected on both revisions; full lifecycle/composition acceptance pending | `lootbeam-low-osrs/m01`, `lootbeam-drop-lc/r01` |
| `script/plugins/performance_display.lua` | owned-widget port; settings, remount and disable/re-enable checked; full release gate pending | `performance-port-osrs/m01`, `performance-port-lc/r01`, `performance-settings-osrs/m01`, `performance-disabled-lc/r01`, `performance-reenabled-remount-osrs/m01` |
| `script/plugins/screenshot.lua` | pending product acceptance; major-3 registration alone is insufficient | Pending |
| `script/plugins/tile_indicator.lua` | graphics port; stationary marker checked; native walking/overlap/lifecycle acceptance pending | `tile-port-lua-osrs/m01`, `tile-port-lua-lc/r01` |
| `src/plugin/plugins/client_settings.c` | pending product acceptance; major-3 registration alone is insufficient | Pending |
| `src/plugin/plugins/feature_flags.c` | pending product acceptance; major-3 registration alone is insufficient | Pending |
| `src/plugin/plugins/gameframe.c` | pending product acceptance; major-3 registration alone is insufficient | Pending |
| `src/plugin/plugins/item_stats.c` | pending product acceptance; major-3 registration alone is insufficient | Pending |
| `src/plugin/plugins/loot_tracker.c` | pending product acceptance; major-3 registration alone is insufficient | Pending |
| `src/plugin/plugins/minimap_orbs.c` | pending product acceptance; major-3 registration alone is insufficient | Pending |
| `src/plugin/plugins/mobile_gameframe.c` | pending product acceptance; major-3 registration alone is insufficient | Pending |
| `src/plugin/plugins/nxt_bird_nest.c` | pending product acceptance; major-3 registration alone is insufficient | Pending |
| `src/plugin/plugins/nxt_cannon_ammo.c` | major-3 native tick/cache/notification API; coordinate/null/lifecycle fixes tested; OSRS empty/pickup and rs289 unavailable captures inspected; full release gate pending | `cannon-native-empty/m01`, `cannon-native-pickup/m01`, `cannon-native-rs289/r01` |
| `src/plugin/plugins/nxt_highlight.c` | pending product acceptance; major-3 registration alone is insufficient | Pending |
| `src/plugin/plugins/tileind.c` | graphics port; stationary marker checked; native walking/overlap/lifecycle acceptance pending | `tile-port-c-osrs/m01`, `tile-port-c-lc/r01` |
| `src/plugin/plugins/xp_orbs.c` | pending product acceptance; major-3 registration alone is insufficient | Pending |
| `src/plugin/plugins/xp_tracker.c` | pending product acceptance; major-3 registration alone is insufficient | Pending |
| `src/plugin/torirs_plugin_lua.c` | pending product acceptance; major-3 registration alone is insufficient | Pending |
| `script/plugins/_widgetprobe.lua` | owned text and geometry probe; native actions/style extensions remain pending | `owned-widget-lua-osrs/m01`, `owned-widget-lua-lc/r01` |
| `src/plugin/plugins/widget_demo.c` | live widgets and native C script callback demonstrated; CS1 reports script capability unavailable and updates live label | `owned-widget-c-osrs/m01`, `owned-widget-c-lc/r01`, `callback-c-native/m01`, `callback-c-rs289/r01` |


## 8. Reproducible build, tests, content and launch

Run from the task worktree unless a clean-checkout command is stated. Do not use the existing task binary as evidence for uncommitted C edits: it was built before the owned-operation patch.

```sh
make -C src OPT=1 EMBED_SERVER=1 \
  PLATFORM_OBJ_BASE=build_plugin_engine_perf \
  PLATFORM_TARGET=torirs_plugin_engine_perf torirs_plugin_engine_perf -j8

make -C src test-uitree test-plugin-host test-plugin-api test-plugin-lua \
  test-plugin-native-hooks test-font-markup

make -C src OPT=1 EMBED_SERVER=1 \
  PLATFORM_OBJ_BASE=build_plugin_engine_perf \
  PLATFORM_TARGET=torirs_plugin_engine_perf test-minimenu-world
```

For the verify worktree use `build_plugin_engine_verify` and `torirs_plugin_engine_verify`. Inspect its git status before changing its detached HEAD. Native source checks relevant to prior work also include `test-cs2-host-request-kinds`, `test-cs2-host-request-producers`, and the actual object-linked `test-cs2-transmit-pump`. Existing product targets include `test-gameframe`, `test-mobile-gameframe`, `test-minimap-orbs-v2`, `test-xp-orbs`, `test-trackers`, `test-nxt-plugins`.

ASan flag is **`ENABLE_ASAN=1`**, not `ASAN=1`. macOS dyld can stall before main; previous samples demonstrated this. Relevant host/Lua/UI/font/VM targets use the existing ASan shim. If a run hangs, inspect its live process/sample first rather than repeatedly restarting it or treating an initialization hang as a product failure.

Current OSRS prepared manifests/cache:

- Task profile: `/private/tmp/plugin-engine-prepared-actions.ini`.
- Clean profile: `/private/tmp/plugin-engine-prepared-actions-clean.ini`.
- Both consume `/private/tmp/plugin-engine-cache-layout-v1`; the clean variant points at the verify worktree's OSRS profile.
- Previous `/private/tmp/plugin-engine-prepared-layout.ini` is the caption-layout checkpoint; previous callback-v3 cache is incompatible with the current eleven-slot hook.
- Content: `/private/tmp/plugin-engine-content/osrs239-content`.
- Original base/curses/cache/server-script derivations are still required; do not bypass their freshness checks.
- Hook tool: `tools/plugin_engine_script_hooks.py --build --base VERIFIED_BASE --cache NEW_OUTPUT`; refuses existing output. `--check` uses **1=fresh, 0=stale, 2=error**, matching launcher convention. Normal build exit 0 means success.
- Hook spec: `tools/testdata/plugin-engine/script-hooks.json`; original script 7232 SHA `eb1a2f9c94e5e7adbfb52dfe0dcb756749fbda4a62e2a4f526fc16944897e4d0`; helper 7225 SHA `cd3464d4de037cdfe32940a400d0b56727f64a69a59587d2fb90d73f5d43f3e8`. Current generated fingerprint `4a94a2161b6571a6`, 11 ints, writable mask 68.

Native capture examples (always choose a new output directory):

```sh
# OSRS Lua live-action probe; reverse configuration order deliberately so
# verify_hide is enabled BEFORE the action can run on binding/config change.
env PATH=/private/tmp/plugin-engine-python/bin:$PATH \
 REPO=/Users/matthewevers/Documents/git_repos/3draster \
 BIN=/private/tmp/3draster-plugin-engine/src/torirs_plugin_engine_perf \
 MANIFEST=/private/tmp/plugin-engine-prepared-actions.ini \
 TORIRSSERVER_CONTENT=/private/tmp/plugin-engine-content/osrs239-content \
 GF_MATRIX_BASELINE=1 GF_MATRIX_TAGS=m01 GF_MATRIX_PLUGIN=lua:roleprobe \
 GF_MATRIX_PUBLIC_CHAT_MODE=friends GF_MATRIX_MAX_FRAMES=1000 TORIRS_PLUGIN_LOG=1 \
 TORIRS_SIM_PLUGIN_CONFIG='1,roleprobe,verify_hide,true;2,roleprobe,public_friends,true' \
 tools/gameframe_matrix.sh /private/tmp/plugin-engine-evidence/NEW_UNIQUE_OUTPUT

# rs289lc: harness reserves a NEW account on EVERY invocation.
env PATH=/private/tmp/plugin-engine-python/bin:$PATH \
 REPO=/Users/matthewevers/Documents/git_repos/3draster \
 BIN=/private/tmp/3draster-plugin-engine/src/torirs_plugin_engine_perf \
 MANIFEST=/private/tmp/plugin-engine-lc-pinned/manifest.ini \
 GF_MATRIX_LC_SERVER=/private/tmp/plugin-engine-lc-pinned \
 GF_MATRIX_LC_SAVE=/private/tmp/plugin-engine-evidence/rs289-seed.sav \
 GF_MATRIX_REVISION=rs289lc GF_MATRIX_BASELINE=1 GF_MATRIX_PLUGIN=lua:roleprobe \
 GF_MATRIX_PUBLIC_CHAT_MODE=friends GF_MATRIX_MAX_FRAMES=1000 TORIRS_PLUGIN_LOG=1 \
 TORIRS_SIM_PLUGIN_CONFIG='1,roleprobe,verify_hide,true;2,roleprobe,public_friends,true' \
 tools/gameframe_matrix.sh /private/tmp/plugin-engine-evidence/ANOTHER_UNIQUE_OUTPUT
```

For clean-checkout captures, run the clean checkout's harness, use its binary, and use `/private/tmp/plugin-engine-lc-pinned/manifest-actions-clean.ini` or the OSRS clean manifest.

Useful inputs already traced to native handlers:

- C demo: `GF_MATRIX_WIDGET_DEMO=1`; `TORIRS_SIM_PLUGIN_CONFIG='1,widget-demo,public_friends,true'`.
- OSRS remount: `TORIRS_SIM_CMD='500,layout 2'`, `GF_MATRIX_EXPECT_ROOT=164`; C/Lua moving probes need `GF_MATRIX_WIDGET_MOVES=2` for two native bindings.
- Real OSRS inventory platebody drop: `TORIRS_SIM_CLICK_AT='500,618,235,1;550,610,280'` (C demo moved sidebar uses approximately 606/235 then 590/280).
- Ground native controls: `TORIRS_SIM_VARBIT='450,14871,1'`.
- Ground custom layout: config `height=160`, `text_outline=true`, `line_gap=30`; `GF_MATRIX_GROUND_ROW_GAP=30` requires a multi-row pile.
- Two-item pile: commands `450,item 4151 1;451,item 1079 1`, clicks `500,618,235,1;550,610,280;650,700,235,1;700,610,280;800,578,270,1;850,578,315`. Whip remains; platebody and platelegs drop.
- Disable: `TORIRS_SIM_PLUGIN_TOGGLE='850,ground-items,0'`, `GF_MATRIX_PLUGIN_ENABLED=0`.
- Real rs289 stats/CS1: `GF_MATRIX_RS289_SCENARIO=stats`, `TORIRS_SIM_CLICK_AT='500,585,184'`, `TORIRS_SIM_CMD='480,setstat strength 1;650,setstat strength 20'`.
- Lost City debugproc prefix is **`~`**: `500,~addobj2 1` creates a random public stack at the player. Plain addobj/varrock requests previously did not apply. Public drops can affect another simultaneous fixture account; do not assert unrelated pile counts.
- Cannon: command `500,cannon`; native setting writes 14175=1,14176=10,14177=1 at frames 450–452; `TORIRS_SIM_OPLOC='1400,2,3210,3424,6'` for pickup, operation 3 for empty, max frames 1700–1800. Existing debugproc bypasses placement-space checks but runs actual timed assembly. `TORIRS_SIM_OPLOC` is recorded by the fixture.

## 9. Next work, in order

1. Reinspect git, this document, the complete plan, current port/mutation registers and active processes. Keep the dirty original workspace intact.
2. Finish/review the exact uncommitted owned-control patch; fix the compile error first. Add Lua/metadata, native operation tests and meaningful negative controls, then actual native clicks in both revisions. Treat all current owned-operation behavior as unproven.
3. Use the owned-control API in a small C/Lua example bound to changing native state and invoking a checked native operation. Check stale press/release/menu callbacks and teardown; extend the existing harness.
4. Finish owned image/style/resource support and port Screenshot preserving camera artwork, all placement modes, report replacement, hotkey, delayed event captures, folders and useful settings. Do not merely rename old contribution builders.
5. Continue representative frame/orb/panel ports, then every inventory row. Remove all superseded execution APIs/loaders and their obsolete tests once their consumers are genuinely ported.
6. Close all remaining native mutation/lifecycle/composition/frontend/data/performance gates from the plan, with real both-revision evidence and screenshots for every plugin. Exact final commits must build in clean checkouts before pushing.

Do not substitute a new general design document for implementation, or trim the goal to the subset already passing. The user has requested a resumable pause, not completion or abandonment of the migration.

## 10. Exact unfinished source diff (recovery copy)

This diff is relative to `c0f08afe46cca056dc845c67491ae20a123a652f` and excludes generated binaries/pycs. It is already applied in `/private/tmp/3draster-plugin-engine` at handoff. It is **incomplete and known not to compile**; preserve/review it rather than treating it as verified. SHA-256 of the patch text below: `afae7ccae04f7796d967ec530a92a63c9bdd2091b16078307b07e84c242ae2a6`.

```diff
diff --git a/src/app.c b/src/app.c
index e81426a3d..753dc22f6 100644
--- a/src/app.c
+++ b/src/app.c
@@ -28952,6 +28952,17 @@ app_minimenu_run_option(
      * on a later one, and in between a plugin can have been switched off and
      * its regions cleared.
      */
+    if( opt.action == RS_MINIMENU_ACTION_PLUGIN_WIDGET )
+    {
+        int32_t node=opt.pick.node_index;
+        if( opt.pick.has_node_identity && node>=0 && (uint32_t)node<app->tree->component_count )
+        {
+            struct UITreeComponent const* c=&app->tree->components[node];
+            PluginHost_WidgetOperation(app->plugins,c->plugin_owner,app_widget_ref(app->tree,node),c->plugin_op_serial);
+        }
+        return 0;
+    }
+
     if( opt.action == RS_MINIMENU_ACTION_PLUGIN_REGION )
     {
         int const at = opt.action_index / TORIRS_PLUGIN_REGION_OPS_MAX;
diff --git a/src/game/rs_minimenu_build.c b/src/game/rs_minimenu_build.c
index c033cb6b6..dc109be9c 100644
--- a/src/game/rs_minimenu_build.c
+++ b/src/game/rs_minimenu_build.c
@@ -878,6 +878,17 @@ add_component_rows(
     if( ((node->component_id >> 16) & 0xFFFF) == TORIRS_CHROME_GROUP )
         return 0;
 
+    if( node->plugin_owner )
+    {
+        if( node->plugin_op_serial && opts->option[0] )
+        {
+            struct UIMinimenuPick owned=pick;
+            UITree_StampMenuPick(ctx->tree,(int32_t)(node-ctx->tree->components),&owned);
+            UIMinimenu_AddOption(menu,opts->option,RS_MINIMENU_ACTION_PLUGIN_WIDGET,0,owned);
+        }
+        return menu->option_count-before;
+    }
+
     if( add_social_rows(node, menu) )
         return menu->option_count - before;
 
diff --git a/src/game/rs_minimenu_build.h b/src/game/rs_minimenu_build.h
index 835cdd19b..a9eb509dd 100644
--- a/src/game/rs_minimenu_build.h
+++ b/src/game/rs_minimenu_build.h
@@ -269,6 +269,7 @@ _Static_assert(
  * is default-eligible, and having two ids is how that difference is stated.
  */
 #define RS_MINIMENU_ACTION_PLUGIN_REGION (UITREE_MINIMENU_ACTION_CLIENT_BASE + 6)
+#define RS_MINIMENU_ACTION_PLUGIN_WIDGET (UITREE_MINIMENU_ACTION_CLIENT_BASE + 8)
 
 /**
  * May this row be the LEFT-click default?
@@ -287,7 +288,7 @@ static inline int
 RS_Minimenu_ActionIsDefaultable(int action)
 {
     return action < 1000 || action == RS_MINIMENU_ACTION_PLUGIN_PANEL ||
-           action == RS_MINIMENU_ACTION_PLUGIN_REGION;
+           action == RS_MINIMENU_ACTION_PLUGIN_REGION || action == RS_MINIMENU_ACTION_PLUGIN_WIDGET;
 }
 
 /** Pack a client op's (kind, slot) into a minimenu option's action_index, and
diff --git a/src/plugin/torirs_plugin_bridge.u.c b/src/plugin/torirs_plugin_bridge.u.c
index 214a6105d..cf6bfc920 100644
--- a/src/plugin/torirs_plugin_bridge.u.c
+++ b/src/plugin/torirs_plugin_bridge.u.c
@@ -4259,6 +4259,10 @@ app_plugin_widget_request(void* user, uint64_t owner, struct PluginWidgetRequest
         return TORIRS_CONTRACT_NATIVE_BLOCKED;
     switch( r->kind )
     {
+    case PLUGIN_WIDGET_SET_ON_OP:
+        if( c->plugin_owner!=owner ) return TORIRS_CONTRACT_NATIVE_BLOCKED;
+        if( !UITree_WidgetSetOperation(tree,ref,owner,r->registration,r->name) ) return TORIRS_CONTRACT_FAILED;
+        break;
     case PLUGIN_WIDGET_VISIBLE:
         *r->flag=!UITree_NodeOrAncestorDisplayHidden(tree,idx) &&
             UITree_NodeNativeVisible(tree,&app->ui_host,idx,app->hover_com_id);
diff --git a/src/plugin/torirs_plugin_contract.h b/src/plugin/torirs_plugin_contract.h
index 3016ecd6f..5b414e14f 100644
--- a/src/plugin/torirs_plugin_contract.h
+++ b/src/plugin/torirs_plugin_contract.h
@@ -168,6 +168,8 @@ struct ToriRS_WidgetApi
     enum ToriRS_ContractResult (*set_text_color)(void*, struct ToriRS_WidgetRef, uint32_t rgb);
     enum ToriRS_ContractResult (*set_text_align)(void*, struct ToriRS_WidgetRef, int horizontal, int vertical);
     enum ToriRS_ContractResult (*remove)(void*, struct ToriRS_WidgetRef);
+    /* Owned controls only. NULL listener removes the operation. */
+    enum ToriRS_ContractResult (*set_on_op)(void*,struct ToriRS_WidgetRef,char const* label,ToriRS_WidgetListener,void* user);
 
     /* Follow a semantic binding at native publication boundaries. A new
      * subscription receives BOUND when available; replacement sends UNBOUND
diff --git a/src/plugin/torirs_plugin_host.c b/src/plugin/torirs_plugin_host.c
index 6e53ae731..826794144 100644
--- a/src/plugin/torirs_plugin_host.c
+++ b/src/plugin/torirs_plugin_host.c
@@ -160,6 +160,15 @@ struct PluginWidgetWatch
     void* user;
 };
 
+#define PLUGIN_WIDGET_OP_MAX 128
+struct PluginWidgetOp
+{
+    struct ToriRS_WidgetRef widget;
+    uint64_t serial;
+    ToriRS_WidgetListener listener;
+    void* user;
+};
+
 struct PluginContext
 {
     struct ToriRS_PluginHost* host;
@@ -221,6 +230,7 @@ struct PluginContext
     int ui_contribution_count;
     struct PluginV2Instance* v2;
     struct PluginWidgetWatch* widget_watches;
+    struct PluginWidgetOp* widget_ops;
     void (*reload_handler)(
         struct ToriRS_PluginHost* host,
         int plugin_index,
@@ -8269,6 +8279,8 @@ plugin_teardown(
     if( ctx->tearing_down )
         return;
     ctx->tearing_down = true;
+    free(ctx->widget_ops);
+    ctx->widget_ops=NULL;
     free(ctx->widget_watches);
     ctx->widget_watches = NULL;
 
@@ -10342,6 +10354,28 @@ PluginHost_FrameStart(
     plugin_dispatch(host, PLUGIN_CALLBACK_FRAME_START, &ev);
 }
 
+bool PluginHost_WidgetOperation(struct ToriRS_PluginHost* host,uint64_t owner,
+                                struct ToriRS_WidgetRef widget,uint64_t registration)
+{
+    if( !host || !owner || owner>(uint64_t)host->plugin_count || !registration ) return false;
+    int index=(int)owner-1;
+    struct PluginContext* ctx=&host->plugins[index];
+    if( !ctx->enabled || !ctx->running || ctx->tearing_down || !ctx->widget_ops ) return false;
+    for( int i=0;i<PLUGIN_WIDGET_OP_MAX;++i )
+    {
+        struct PluginWidgetOp op=ctx->widget_ops[i];
+        if( op.serial!=registration || !ToriRS_WidgetRefEqual(op.widget,widget) ) continue;
+        struct ToriRS_WidgetEvent event={.type=TORIRS_WIDGET_OPERATION,.widget=widget,
+            .operation=1,.native_revision=registration};
+        int previous_owner=host->dispatching,previous_event=host->dispatch_event;
+        host->dispatching=index;host->dispatch_event=PLUGIN_CALLBACK_WIDGET_BINDING;
+        op.listener(&ctx->v2->runtime.api,op.user,&event);
+        host->dispatching=previous_owner;host->dispatch_event=previous_event;
+        return true;
+    }
+    return false;
+}
+
 static struct PluginWidgetWatch*
 plugin_widget_watch_current(struct ToriRS_PluginHost* host, int owner, int slot, uint64_t serial)
 {
diff --git a/src/plugin/torirs_plugin_host.h b/src/plugin/torirs_plugin_host.h
index 8eb79ae26..e2f60cdba 100644
--- a/src/plugin/torirs_plugin_host.h
+++ b/src/plugin/torirs_plugin_host.h
@@ -140,7 +140,7 @@ enum PluginWidgetRequestKind
     PLUGIN_WIDGET_POSITION, PLUGIN_WIDGET_SIZE,
     PLUGIN_WIDGET_REVALIDATE, PLUGIN_WIDGET_RESET, PLUGIN_WIDGET_RESET_OWNER,
     PLUGIN_WIDGET_CREATE_TEXT, PLUGIN_WIDGET_SET_TEXT, PLUGIN_WIDGET_TEXT_COLOR, PLUGIN_WIDGET_TEXT_ALIGN, PLUGIN_WIDGET_REMOVE,
-    PLUGIN_WIDGET_HIDDEN, PLUGIN_WIDGET_PROJECTION_HEIGHT, PLUGIN_WIDGET_TEXT_OUTLINE, PLUGIN_WIDGET_INVOKE
+    PLUGIN_WIDGET_HIDDEN, PLUGIN_WIDGET_PROJECTION_HEIGHT, PLUGIN_WIDGET_TEXT_OUTLINE, PLUGIN_WIDGET_INVOKE, PLUGIN_WIDGET_SET_ON_OP
 };
 struct PluginWidgetRequest
 {
@@ -153,6 +153,7 @@ struct PluginWidgetRequest
     bool* flag;
     struct ToriRS_WidgetAction* actions;
     struct ToriRS_WidgetActionRef action;
+    uint64_t registration;
     char* text;
     size_t capacity;
     size_t* count;
@@ -160,6 +161,7 @@ struct PluginWidgetRequest
 
 /* Called after native frame bindings are available at the pre-input/paint
  * publication fence. A zero instance means no ready native tree. */
+bool PluginHost_WidgetOperation(struct ToriRS_PluginHost*,uint64_t owner,struct ToriRS_WidgetRef,uint64_t registration);
 void PluginHost_WidgetsChanged(struct ToriRS_PluginHost*, uint64_t instance, uint64_t generation);
 
 /* Internal native adapter. No VM pointer or borrowed stack slot reaches a
diff --git a/src/plugin/torirs_plugin_runtime.inc b/src/plugin/torirs_plugin_runtime.inc
index cf819ca84..87e6c89f6 100644
--- a/src/plugin/torirs_plugin_runtime.inc
+++ b/src/plugin/torirs_plugin_runtime.inc
@@ -3672,6 +3672,42 @@ static enum ToriRS_ContractResult widget_text_align(void* u, struct ToriRS_Widge
     struct PluginWidgetRequest r={.kind=PLUGIN_WIDGET_TEXT_ALIGN,.ref=ref,.a=h,.b=v};
     return plugin_widget_request(u,&r);
 }
+static enum ToriRS_ContractResult widget_set_on_op(void* u,struct ToriRS_WidgetRef ref,
+    char const* label,ToriRS_WidgetListener listener,void* user)
+{
+    struct PluginV2Runtime* runtime=u;
+    struct PluginContext* ctx=runtime ? runtime->context : NULL;
+    if( !ctx || !ctx->running || ctx->tearing_down || ctx->host->dispatching!=ctx->index ||
+        plugin_ev_is_draw(ctx->host->dispatch_event) ) return TORIRS_CONTRACT_WRONG_CONTEXT;
+    if( listener && (!label || !*label || strlen(label)>=UITREE_MENU_OPTION_LEN) ) return TORIRS_CONTRACT_INVALID_ARGUMENT;
+    if( ctx->host->widget_watch_serial==UINT64_MAX ) return TORIRS_CONTRACT_BUDGET_EXCEEDED;
+    if( !ctx->widget_ops )
+    {
+        if( !listener ) return TORIRS_CONTRACT_OK;
+        ctx->widget_ops=calloc(PLUGIN_WIDGET_OP_MAX,sizeof(*ctx->widget_ops));
+        if( !ctx->widget_ops ) return TORIRS_CONTRACT_FAILED;
+    }
+    int slot=-1;
+    for( int i=0;i<PLUGIN_WIDGET_OP_MAX;++i )
+    {
+        if( ctx->widget_ops[i].serial && ToriRS_WidgetRefEqual(ctx->widget_ops[i].widget,ref) ) { slot=i;break; }
+        if( slot<0 && !ctx->widget_ops[i].serial ) slot=i;
+    }
+    if( slot<0 )
+        for( int i=0;i<PLUGIN_WIDGET_OP_MAX;++i )
+        {
+            bool visible;
+            if( widget_visible(u,ctx->widget_ops[i].widget,&visible)==TORIRS_CONTRACT_STALE_REFERENCE ) { slot=i;break; }
+        }
+    if( slot<0 ) return TORIRS_CONTRACT_BUDGET_EXCEEDED;
+    uint64_t serial=listener ? ++ctx->host->widget_watch_serial : 0;
+    struct PluginWidgetRequest request={.kind=PLUGIN_WIDGET_SET_ON_OP,.ref=ref,
+        .name=listener ? label : "",.registration=serial};
+    enum ToriRS_ContractResult result=plugin_widget_request(u,&request);
+    if( result!=TORIRS_CONTRACT_OK ) return result;
+    ctx->widget_ops[slot]=(struct PluginWidgetOp){ref,serial,listener,user};
+    return TORIRS_CONTRACT_OK;
+}
 static enum ToriRS_ContractResult widget_remove(void* u, struct ToriRS_WidgetRef ref)
 {
     struct PluginWidgetRequest r={.kind=PLUGIN_WIDGET_REMOVE,.ref=ref};
@@ -3793,7 +3829,7 @@ v2_runtime_init(
         .widgets = {
             .context = runtime, .find = widget_find, .get_widget = widget_get,
             .find_all = widget_find_all, .set_hidden = widget_set_hidden, .watch_tree = widget_watch_tree,
-            .visible=widget_visible,.actions=widget_actions,.invoke=widget_invoke,.parent=widget_parent,.set_projection_height=widget_projection_height,.set_text_outline=widget_text_outline,
+            .set_on_op=widget_set_on_op,.visible=widget_visible,.actions=widget_actions,.invoke=widget_invoke,.parent=widget_parent,.set_projection_height=widget_projection_height,.set_text_outline=widget_text_outline,
             .children = widget_children, .bounds = widget_bounds, .get_text = widget_text,
             .position = widget_local_bounds,
             .set_position = widget_position, .set_size = widget_size,
diff --git a/src/ui/uitree.c b/src/ui/uitree.c
index d29327ebd..bdfe76964 100644
--- a/src/ui/uitree.c
+++ b/src/ui/uitree.c
@@ -3947,7 +3947,7 @@ UITree_ActionSignatureAt(struct UITree const* tree, int32_t idx)
     ACTION_FIELD(behavior.button_type); ACTION_FIELD(behavior.client_code);
     ACTION_FIELD(behavior.click_mask); ACTION_FIELD(behavior.target_mask);
     ACTION_FIELD(target_priority); ACTION_FIELD(force_left_click);
-    ACTION_FIELD(item_id); ACTION_FIELD(item_count);
+    ACTION_FIELD(item_id); ACTION_FIELD(item_count); ACTION_FIELD(plugin_op_serial);
 #undef ACTION_FIELD
     hash = action_hash_text(hash, c->data_text);
     /* Native social rows and server-armed continue prompts derive their
@@ -4185,6 +4185,21 @@ int32_t UITree_WidgetCreateText(struct UITree* tree, struct UITreeNodeRef parent
     return index;
 }
 
+bool UITree_WidgetSetOperation(struct UITree* tree,struct UITreeNodeRef ref,uint64_t owner,
+                                uint64_t serial,char const* label)
+{
+    int32_t idx=UITree_ResolveRef(tree,ref);
+    if( idx<0 || !owner || tree->components[idx].plugin_owner!=owner ||
+        !label || strlen(label)>=UITREE_MENU_OPTION_LEN || (serial && !*label) ) return false;
+    struct UITreeComponent* c=&tree->components[idx];
+    struct UITreeMenuOptions* options=UITree_MenuOptionsMut(c);
+    if( !options ) return false;
+    snprintf(options->option,sizeof(options->option),"%s",serial ? label : "");
+    c->plugin_op_serial=serial;
+    uitree_note_mutation(tree,idx,UITREE_IMPACT_EMIT_SELF|UITREE_IMPACT_REACHABILITY);
+    return true;
+}
+
 bool UITree_WidgetRemove(struct UITree* tree, struct UITreeNodeRef ref, uint64_t owner)
 {
     int32_t idx=UITree_ResolveRef(tree,ref);
diff --git a/src/ui/uitree.h b/src/ui/uitree.h
index 90d384e72..81d84ac37 100644
--- a/src/ui/uitree.h
+++ b/src/ui/uitree.h
@@ -792,6 +792,7 @@ struct UITreeComponent
     struct UITreeWidgetGeometry* widget_geometry;
     uint64_t plugin_owner;
     char* plugin_key;
+    uint64_t plugin_op_serial; /* Host listener version; never copied with native content. */
     /**
      * A layer the plugin frame declared NOT to clip.
      *
@@ -2125,6 +2126,7 @@ bool UITree_WidgetReset(struct UITree*, struct UITreeNodeRef, uint64_t owner);
 void UITree_WidgetResetOwner(struct UITree*, uint64_t owner);
 int32_t UITree_WidgetCreateText(struct UITree*, struct UITreeNodeRef parent, uint64_t owner,
                                char const* key, int font_id);
+bool UITree_WidgetSetOperation(struct UITree*,struct UITreeNodeRef,uint64_t owner,uint64_t serial,char const* label);
 bool UITree_WidgetRemove(struct UITree*, struct UITreeNodeRef, uint64_t owner);
 
 int UITree_WidgetPositionOverride(struct UITree const*, int32_t, struct UITreeElemPosition*);
```
