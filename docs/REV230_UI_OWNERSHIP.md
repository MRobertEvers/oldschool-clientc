# Four rev-230 UI bugs, and the one question behind them

> **What this is.** A rev-230 gameframe that draws correctly and behaves wrong:
> the chat input line never showed what you typed, a selected inventory item
> never got its white outline, the bank clipped the top off every stack number
> and coloured all of them yellow, and the world could be hovered, clicked and
> wheel-zoomed straight through an open interface.
>
> Sibling case file to [`REV230_UI_BLANK_PANELS.md`](REV230_UI_BLANK_PANELS.md),
> which is about panels that drew *nothing*. This one is about panels that drew
> the wrong thing, and it has a different unifying cause.

---

## 0. The one-paragraph version

Every one of the four is an **ownership** mistake, in one of two directions.

Two of them are the client doing a job that is not its own: it wrote
`chatbox:input` on top of the clientscript that owns it, and it decided a stack
number's colour that the reference encodes in the string. Two are the client
*not* doing a job that is: the "selected for Use" outline and the stack count
were both implemented against `TYPE_INV`, a widget rev 230 does not have, and
`noClickThrough` — the cache's own "input stops here" flag — was decoded by
`3rd/rscache` and then dropped on the floor by the bridge that builds the tree.

So the triage question that would have found all four is not "what is missing",
it is:

> **At this revision, who owns this pixel — the client, the clientscript, or the
> cache record?** Then check that exactly one of them writes it.

Three of the four had *two* writers or *zero*. None had one.

---

## 1. The chatbox showed `name: *` no matter what you typed

**Symptom.** Typing in the chatbox produced no visible text. The name and the
`*` caret drew; the characters between them never did. And the input was
plainly *working* — typing `::setlevel 2 40` blind and pressing Enter printed
`Set stat 2 to 40.` in the scrollback.

**Who owns the input line at rev 230.** The clientscript does, completely.
Interface 162's root carries an onKey hook — **script 73** — which is dispatched
on every key press (confirmed live with `TORIRS_KEY_DEBUG=1`:
`key_dispatch: com=0x00a20000 script=73`). It edits `%varcstring335` one
character at a time via `~script74`, routes `::` lines to `docheat` and public
lines to `~script5517` on Enter, and ends **every** invocation with `~script223`.

Script 223 is the input line's renderer, and it does all of it:

```
[clientscript,script223]
def_string $string0 = "<col=0000ff>";
...
$string2 = "<enum(int, string, enum_1894, %varbit1777)><chat_playername>";
$string2 = append($string2, ": <$string0><$string1></col>");   // $string1 = escape(%varcstring335)
if ($length2 < 80) { $string2 = append($string2, "<$string0>*</col>"); }
...
if_settext($string2, interface_162:57);
```

including the colour, the shadow, the alignment, the per-state ops, and the
"You must set a name before you can chat" branch when varbit 8119 is clear.

**Root cause — two writers, and the wrong one won.** `RS_ChatWidgets_Apply`
runs immediately before every emit, and the manifest's `[ui:chatbox]` section
told it `chatbox_input=57`. So every frame, after script 223 had composed the
real line, the client overwrote component 162:57 with its own:

```c
snprintf(line, sizeof(line), "<col=000000>%s:</col> <col=0000ff>%s</col>…",
         chat->username, chat->input);
UITree_ApplyText(tree, uid, line);
```

`chat->input` is always empty at this revision. The C key path that fills it is
gated on `app->slots.chat_index >= 0` (`app.c`), and `chat_index` is only ever
set from a node tagged `slot=chat` — a tag that comes from
`uitree_builder_bake.c`'s `slot_tag_from_string`, i.e. from a **revconfig INI**.
A rev-230 tree is the cache's own IF3 gameframe and carries no slot tags at all,
so the whole block is dead there and `chat->input` can never be anything but
`""`.

The result: a live input line with a dead one painted over it, once per frame.

**Fix.** The manifests stop declaring the child. `chatbox_input` is documented
as **-1 at rev 230, and that being the answer rather than an omission** — the
scrollback (500 line components) is ours because nothing else writes it; the
input line is not.

`manifest_osrs230*.ini`, `manifest_osrs239*.ini`, `src/game/rs_chat_widgets.h`
(the `input_child` field comment carries the reasoning),
`src/game/rs_chat_widgets.c`.

> **Superseded (2026-08-22).** The scrollback went the same way as the input
> line: `rs_chat_widgets.{c,h}` and the whole `[ui:chatbox]` section are
> deleted, and the cache's `[proc,rebuildchatbox]` writes all 500 line
> components off an `onchattransmit` hook, reading the client's message store
> through the `CHAT_GETHISTORY*` opcodes. "Two writers, and the wrong one won"
> is now "one writer, and it is the cache's" — which is the ownership this
> document was arguing towards.

**And one thing that only became visible once the overlay was gone.** With
script 223 finally reaching the screen the line read `: hello*` — no name. CS2
opcode **5015 `chat_playername`** had a metadata row
(`cs2_opcode_meta.c: CS2_HANDLER_HOST`) and no handler anywhere, so it pushed
nothing and `<chat_playername>` interpolated empty. Implemented: it answers
`RS_CS2Host.local_player_name`, which `app.c` writes from the same
PLAYER_APPEARANCE that sets `RS_Chat.username` — one source, so a public line
and the input line above it cannot spell the player differently.

`src/cs2vm2/cs2vm2.c`, `src/game/rs_cs2_host.{c,h}`, `src/app.c`.

**Verified.** Headless, `TORIRS_SIM_TYPE` typing `::setlevel 2 40`: the line
renders `testc: ::setlevel 2 40*` mid-type, and Enter clears it and prints
`Set stat 2 to 40.` in the scrollback.

---

## 2. A selected item got no white outline

**Symptom.** Right-click an inventory item → "Use" and nothing changed. The
selection was armed (`app->objsel`), the next click did use the item — only the
white pixel-perfect outline that says which item is armed never drew.

**Where the outline comes from.** The reference bakes it into the item sprite as
an outline **state**, not as a separate draw
(`rt4-client/.../rt4/Inv.java:231`):

```java
if (state >= 1) {
    canvas.drawOutline(1);              // value-1 edge, the normal inventory look
    if (state >= 2) {
        canvas.drawOutline(16777215);   // the white "selected" ring on top
    }
}
```

and the IF3 draw picks the state per widget: `component.outlineThickness`
normally, `max(2, borderType)` when the widget is the selected cell
(`Cs1ScriptRunner.java:789`; xrsps `ui/gl/widgets-gl.ts:4180`, which spells it
out: *"selected items render with outline=2 (white)"*). The bank's own onload
sets state 1 on every cell it creates — `cc_setoutline(1)` inside script 274's
`cc_create($component7, 5, …)` loop, right after `cc_setsize(36, 32, 0, 0)`.

**Root cause — the era's dead widget, again.** This client already bakes both
variants (`UITreeSceneBridge_EnsureObjIcon` / `…Selected`, i.e.
`BRIDGE_ICON_OUTLINE_SHADOW` and `BRIDGE_ICON_OUTLINE_WHITE`), and already has
the host query that answers "is this cell the armed one"
(`UITREE_HOST_GET_INV_SELECT_ICON`). The swap that uses them lived in exactly
one place: `emit_rs_inv_slots`, the `TYPE_INV` grid expander.

**Rev 230 has no `TYPE_INV` inventory.** The gameframe `cc_create`s one type-5
graphic per slot and hangs the obj on it with `SETOBJECT`; those draw through
the generic path, which never asked. This is bug #4 of
`REV230_UI_BLANK_PANELS.md` (missing stack counts) wearing different clothes —
same widget, same wrong era, second feature.

**Fix.** `emit_obj_selected_icon` on the generic path, beside
`emit_obj_stack_count` for the same reason. Matching a cell to the armed
selection needed the protocol's own addressing: a CS2 cell's identity is its
static **parent's** uid plus its index within it (`uitree_obj_cell.h`), never
the runtime child's own id — so a node matches either directly or as child
`slot` of the armed component. That is the identical two-form test the drag path
makes a few lines above, and testing only the first form is why *that* one did
nothing on its first attempt. A new host request,
`UITREE_HOST_GET_INV_SELECTION`, reports the armed pair (mirroring
`GET_INV_DRAG`); the grid path does not need it, because a grid already knows
the (component, slot) of every slot it draws.

`src/ui/uitree_emit.c`, `src/ui/uitree_host.{c,h}`, `src/app.c`.

---

## 3. The bank clipped its stack numbers, and coloured them all yellow

Two independent bugs in the same three lines.

### 3a. Clipped

**Symptom.** In the bank, the top row of items lost the top ~3 pixels of every
stack number; the second row was fine. Measured on the framebuffer, the digits
occupied rows 81..86 with a hard cut at 81 — and 81 is exactly the top of the
item container's clip rect (`TORIRS_DUMP_BOUNDS=12`: `(12|16) abs=-2,81`).

**Why the reference never has this problem.** Over there the count is not a
sibling draw at all — it is rasterised **into the 36x32 item sprite**:

```java
if (drawText && (objType.stackable == 1 || stack != 1) && stack != -1) {
    ObjTypeList.font.renderLeft(formatObjAmount(stack), 0, 9, 16776960, 1);
}
```
(`Inv.java:253`, inside `renderObjectSprite`, on a `SoftwareSprite(36, 32)`.)

So it is clipped by exactly whatever clips the icon and by nothing else, and its
baseline is at y=9 **within the sprite** — inside the icon's own rows by
construction.

**Root cause.** Here the count is an ordinary text desc sharing the icon's clip
rect, which is equivalent *only while it stays inside the icon's rows*. It did
not. The y was written as

```c
count_desc.y = icon->y + 9 - 12;   /* "our text box top sits one line up" */
```

against a renderer whose box path puts the **glyph top** at the box y, not the
baseline one line below it (`gl3_draw_font_box` resolves
`draw_y = y + max_ascent - font_ascent`, and `ToriDraw2D_DrawStringBox` the
same). Three pixels above the icon, every time — invisible everywhere the icon
had headroom, and a third of a digit missing wherever the icon's box top *is*
the clip top, which in a scrolling item grid is the whole first row.

**Fix.** State the reference's number instead of guessing an ascent. A new
`text_baseline` flag on `UITreeEmitDesc` selects the renderer's existing
baseline path — `ToriDraw2D_DrawString`, which does `y -= font->line_height`,
exactly the reference `PixFont.drawString` convention (`Client-TS
graphics/PixFont.ts:161`) — and the count is emitted at `icon->y + 9`. Both back
ends already implemented the flag for hitsplat numbers; only the plumbing from
the emit desc was missing. Both count paths (CS2 cell and `TYPE_INV` grid) were
changed together so they cannot drift.

Measured after: rows 81..88, nothing above the clip.

### 3b. All yellow

**Symptom.** 250,000 coins drew "250K" in yellow; the reference draws it white.

**Root cause.** `count_desc.color = 0xFFFF00`, hardcoded, on both paths. The
formatter next to it (`uitree_emit_inv_number`) had the reference's *band
arithmetic* — raw below 100K, K below 10M, M above — copied from the 254
client's `invNumber` (`Client.ts:10502`), which really does draw one colour.
The modern client does not: `Inv.formatObjAmount` returns each band **wrapped in
its own colour tag** (`Inv.java:264`, constants at `:14/:16/:18`):

| amount | tag | looks like |
|---|---|---|
| `< 100000` | `<col=ffff00>` | yellow |
| `< 10000000` | `<col=ffffff>` | white |
| `>= 10000000` | `<col=00ff80>` | green |

and the `16776960` it passes to `renderLeft` is only the fallback for a string
carrying no tag.

**Fix.** Emit the tags, keep the fallback. Resolving the colour in C instead
would be a second implementation of what `<col=…>` means — the same trap
`REV230_UI_BLANK_PANELS.md` §9 sprang on `parawidth`. Both back ends already
tokenise markup (`font_draw_string_range`, `ToriDraw_FontVisitGlyphsStyled`) and
both draw the shadow pass black regardless of the tag, which is the reference's
black-then-colour pair.

Verified on pixels at all three bands: `25M` green, `250K` white, `5000` /
`100` / `60` yellow, none clipped.

`src/ui/uitree_emit.{c,h}`, `src/render/torirs_frame.c`.

---

## 4. The world could be hovered, clicked and zoomed through an open interface

**Symptom.** With the bank open over the entire viewport: the mouseover text
read "Walk here", right-clicking the bank's background offered world rows, a
left click walked the player, and the wheel scrolled the bank's item pane **and**
zoomed the camera behind it.

### The design question, and the reference's answer

The user-facing question is *"how do you stop clicking and mousing through an
interface unless that interface intends it"*, and it has a real answer at this
revision, in the cache and in every reference client. Two things block input,
and nothing else does:

1. **`noClickThrough`** — a **cache field**, decoded on type-0 (layer) records
   right after `hidden`, beside `scrollWidth`/`scrollHeight`
   (`rt4 Component.java:1045`; `3rd/rscache/.../dat2_component.c:944`). CS2 can
   also raise it at runtime (`if_setnoclickthrough` 2005 /
   `cc_setnoclickthrough` 1005). Where the pointer is inside one, the reference
   *discards everything drawn underneath*: `Cs1ScriptRunner.java:542` resets the
   whole minimenu to Cancel-only — which throws away the world rows the scene
   pass had already added — and `InterfaceList.java:666` unlinks every pending
   `onScroll` hook request, i.e. eats the wheel notch.
2. **A modal sub-interface mount** — `IF_OPENSUB` with `type == 0`. xrsps'
   `findBlockingWidgetInHits` (`ui/widgets/menu/utils.ts:730`), the function
   behind its `isPointOverWidget`, treats an InterfaceParent of type 0 exactly
   like a `noClickThrough` layer.

Both are needed, and the bank proves it: `bankmain.if` declares
`noclickthrough=yes` on eight inner layers (`capacity_layer`,
`banktags_display_container`, `potionstore_container`, `storage_popup_tab`,
`incinerator_confirm`, `banktags_popup_stringexport_container`, `dropdown`,
`gim_storage`) and on **none** that covers the panel — nor does the gameframe's
`mainmodal` container. The flag alone would not have blocked it. What blocks it
is that the server opened it as a *modal*
(`torirs_server_bank.c`: `ToriRSServer_SendIfOpensub(…, mainmodal, bankmain, 0)`).

And the converse is why "block anything that is mounted" would be wrong: the
world map floater and the XP tracker mount as **overlays** (type 1 —
`torirs_server_worldmap.c` sends 1) and are click-through unless they raise
`noClickThrough` themselves — which both of them do. The XP tracker does it
conditionally (`script999`: `if_setnoclickthrough($boolean9, $layer19)` behind a
setting); the **world map does it in the cache record**, statically:
`worldmap.if [window]` (595:5) declares `noclickthrough=yes`, consumed by
`UITree_PointBlocksWorld` (`src/ui/uitree_input.c`).

Measured 2026-08-02, because the earlier wording ("deliberately click-through
unless they raise it") read as if the map were transparent: hovering the bare
map surface shows no "Walk here", and hovering any of its controls returns that
control's component id. The map is opaque, by its own declaration, and no client
code names it.

> **The rule, stated once:** an interface is opaque to the world where a
> `noClickThrough` layer covers the point, or where a modal (type 0) mount
> covers it. Everything else is transparent. Both facts are declared — one by
> the cache record, one by the packet that opened the interface — so no client
> code names an interface, a component or a rectangle.

### Root causes — one per half, both "the mechanism was there"

**The flag was decoded and thrown away.** `3rd/rscache` has read
`noClickThrough` for as long as it has read IF3 records, and
`UITreeComponent.no_click_through` has existed with a correct consumer
(`hit_test_interactive_recursive`'s `blocks`, and the minimenu collector's
barrier) — but the bridge between them,
`torirs_component_from_rscache.c`, copied `scrollHeight` and `scrollWidth` out of
the type-0 branch and not the third field beside them. So the only source was
the CS2 opcodes, and the 133 interfaces in the tree that declare it statically
declared it to nobody.

**The world gate asked the wrong question.** `app_world_mouse_gate()` had two
tests, and neither could see the bank:

```c
if( app->slots.main_modal_id != -1 )        /* dead at rev 230: see below */
    return 0;
if( UITree_HitTestInteractive(...) >= 0 )   /* wrong question */
    return 0;
```

`slots.main_modal_id` is written by the IF1 packet path (`RS_UISlots_OpenMain`)
and seeded from a revconfig-baked tree — the same dat1-only slot state that
bug #1 turned out to depend on. At rev 230 it is permanently -1.

The second is a different question from the one being asked.
`UITree_HitTestInteractive` answers *"is there a widget that wants this click"*.
The world needs *"should the UI consume this click"*, and those genuinely
differ in both directions: a bank's dark background wants nothing and must still
swallow the click, the hover text and the wheel; a hovered chat line wants the
click and must not swallow the wheel.

The wheel had a third, separate reason, and it was a correct decision made
against a missing mechanism: `interact_wheel` deliberately does **not** set
`wheel_consumed` when it dispatches an IF3 `onScroll` hook, because rev 230's
gameframe root (161:1) carries one over the entire screen and treating that as a
claim would disable every app-level wheel gesture everywhere. With no way to ask
"is this point blocked", "the top-most handler took it" was the only signal
available, and it is the wrong one.

### Fix

- `noClickThrough` now flows cache → tree, through the four hops it needed:
  `ToriRS_Component.no_click_through` (`torirs_types.h`,
  `torirs_component_from_rscache.c`) → `UIBuildComponent`
  (`uitree_build.h`, `uitree_from_component.c`) → `UITreeNodeSpec`
  (`uitree_build.c`) → `UITreeComponent` (`uitree.h`, `uitree.c`).
- `UITree_ChildMountType()` (`uitree.c`/`uitree.h`) exposes a mount's
  `IF_OPENSUB` type. The tree already recorded it
  (`struct UITreeInterfaceParent.type`, "0 modal, 1 overlay, 3 tab/sidemodal");
  nothing had ever read it. `uitree_emit.c`'s
  `child_is_interface_parent_mount` now calls it instead of keeping its own
  copy of the lookup.
- `UITree_PointBlocksWorld()` (`uitree_input.{c,h}`) answers the reference's
  `isPointOverWidget`. It is computed by the **existing** hit-test walk as a
  second output beside `blocks`, so it inherits that walk's hide / clip /
  scroll / drag / sidebar-tab rules rather than growing a second set that could
  disagree with the drawn pixels.
- `app_world_mouse_gate()` consults it. Every world gesture is already behind
  that one function — hover text, minimenu world rows, left-click walk,
  right-click, and the wheel zoom — so one call fixes all of them, and the
  wheel needs no `wheel_consumed` change at all.

### Verified, headlessly, all three surfaces

| check | before | after |
|---|---|---|
| hover text over the bank (bright px in the mouseover band) | 224 (`Walk here`) | **0** |
| same pointer, bank closed | 224 | **224** (unchanged) |
| `TORIRS_SIM_WHEEL` over the bank, `TORIRS_CAM_DEBUG=1` | `cam_zoom` fires | **no `cam_zoom`** |
| same wheel, bank closed | `cam_zoom` fires | **fires** (100% → 70%) |

and the things the flag could plausibly have broken, all still working: the
chat filter tabs (which sit inside `chatbox:controls`, itself
`noclickthrough=yes`) still switch, the bank's quantity buttons still latch,
and right-click → "Use" on an inventory item still arms the selection.

---

## 5. What to take from this

- **`TYPE_INV` is a trap that keeps paying out.** Three separate item-draw
  features have now been found implemented only on the grid path
  (`REV230_UI_BLANK_PANELS.md` §4's stack counts, the drag ghost, and now the
  selection outline). Anything written against `emit_rs_inv_slots` should be
  assumed missing from the CS2 cell path until checked, because rev 230 has no
  grid at all and the omission is silent.
- **`slots.*` is dat1-only.** Two of these four bugs bottomed out in a
  `RS_UISlots` field that a cache-chrome boot never fills (`chat_index`,
  `main_modal_id`). Any `if (app->slots.X)` in a rev-230 path is dead code until
  proven otherwise — it is not a bug in the slot table, it is a boot that has no
  slot table.
- **A "-1 means unset" config field is a place for two writers to hide.**
  `chatbox_input=57` was not a wrong value in the sense of pointing at the wrong
  component; it pointed at exactly the right one, and being right is what made
  it silent.
- **Decoding a field is not reading it.** `noClickThrough` had a decoder, a
  storage field, and a correct consumer, and was still inert, because the one
  hop between decoder and storage was a hand-written struct copy. Grep for the
  *cache* spelling (`noClickThrough`) as well as the local one before concluding
  a field is unsupported.

## 6. Printable keys must deliver a code event *and* a character event

**Symptom.** Spacebar did not advance "Click here to continue" on NPC dialogue
(`chat_left:continue` / `231:5`), even though the cache already wires it:
`onload` → script **55** (`chatbox_keyinput_init`) → onKey script **57**
(`chatbox_keyinput_listener`) with `$int5 = 83` (`TORIRS_OSRSKEY_SPACE`) →
script **2153** (`chatbox_keyinput_matched`) → `if_resume_pausebutton`.

**Who owns what.** LostCity puts the dialogue interfaces and their resume
buttons in content (`interface_chat/scripts/chat.rs2` uses `if_addresumebutton`
+ `p_pausebutton`). The key-to-code delivery and the `RESUME_PAUSEBUTTON` write
are engine.

**Root cause — two client breaks.**

1. **Code event cancelled.** `PlatformWindow_PollCommands` held each
   `SDL_KEYDOWN`'s OSRS code and cleared it when a printable `SDL_TEXTINPUT`
   followed, so space only reached scripts as `event_key = -1,
   event_keychar = ' '`. Script 57's code branch needs `event_key = 83`; its
   character branch tests `$string0`/`$string1`, which are empty (`s:,s:`) for
   dialog continue buttons — so it returned without matching. The reference
   `KeyHandler` queues both: `(code, char = 0)` from `keyPressed` and
   `(-1, char)` from `keyTyped`.
2. **`if_resume_pausebutton` was a no-op.** The host arm accepted and dropped
   it; mouse continue worked only because the minimenu/`IF_BUTTON` path sends
   the packet. The opcode is now a pending host request drained through
   `app->button_sink.resume_pausebutton` → `net_out_resume_pausebutton`.

**Fix.** Push the code event on `SDL_KEYDOWN` and let `SDL_TEXTINPUT` add its
own character event (`src/platform/platform_sdl2.c`). No double-insert into the
chat line: `RS_Chat_HandleKey` inserts only on `key_typed == -1 &&
key_pressed >= 32`, and `chatdefault_onkey` (73) splits the same way — codes
for Enter/Backspace, character events for insertion. Headless
`TORIRS_SIM_TYPE` must use `k83` for space (a `c32` character event alone still
misses script 57's code branch).

**Verified.** Hans `::talk hans 1`, then `k83`: one `RESUME_PAUSEBUTTON 231:5`
per press (`TORIRSSERVER_VERBOSE=1`), interface 219 opens. Chat typing via character
events still inserts once.

## 6b. Chatmenu digits and clicks (script 57 + dynamic rows)

**Symptom.** "Select an Option" (`chatmenu` 219) rendered, but neither mouse
clicks nor number keys selected a row. Terminal spam:

`Task_CS2Run: script 57 failed at opcode 4120 pc 33` (`STRING_INDEXOF_CHAR`).

**Who owns what.** LostCity's engine `string_indexof_char(string, char)(int)` is
the CS2/SSVM surface; content's `~p_choice*` answers via `last_slot`. Digit
matching is script 57's character branch (`string_indexof_char($string0,
$char1)` with `$string0 = tostring(row)`).

**Root cause — two engine breaks.**

1. **`STRING_INDEXOF_CHAR` arity.** The handler popped a start index the
   bytecode never pushes (only char + string). Script 57 aborted before matching
   a digit. Fixed to match LostCity/`ssvm.c` / `cs2_command.gen.h` `(1,1,1,0)`.
2. **Dynamic-child addressing.** EVENT_CLICK and `cc_resume_pausebutton` sent
   the child's runtime uid. The server only resumes on registered
   `chatmenu:options`, and needs the row as `last_slot`. `app_send_if_button` /
   `app_send_resume_pausebutton` now remap via `app_if_button_target` to
   `IF_BUTTON1(parent, sub)`; `handle_if_button_op` latches `last_slot` then
   resumes (same as §3.11f in `osrs230_mockserver.md`).

**Verified.** Headless embed: `TORIRS_NET_CHEAT="talk hans 1"`, `k83` into the
choice menu, then `c49` (`'1'`): no `script 57 failed at opcode 4120`; Hans's
branch-1 line (`I'm looking for whoever is in charge of this place.`) is set on
the chatplayer. `ToriRSServer --selftest` "npc chat dialogue" (IF_BUTTON1 alone
resumes with `last_slot=3`) and `ToriRSServer_Pack --check-only` at 0 errors.
Re-checked on a fresh TCP mock (`TORIRSSERVER_VERBOSE=1`): `RESUME_PAUSEBUTTON 231:5`
then `IF_BUTTON1 219:1 sub=1`, no 4120 abort.

**Mouse click (follow-up).** Digits worked while left-clicks still did nothing:
choice rows are `cc_create`d TEXT with no cache `clickmask`/ops, so
`UITree_ComponentIsPassThrough` treated them as decorative and the hit never
reached the EVENT_CLICK → `IF_BUTTON1(parent,sub)` path. Continue prompts are
fine because `chat_left:continue` authors `clickmask=1`. Hit-test now asks the
host for the node's IF_SETEVENTS mask (including parent-range inheritance) via
`UITREE_HOST_GET_IF_EVENTS`. Verified: `TORIRS_SIM_CLICK_AT` on row 1 →
`IF_BUTTON1 219:1 sub=1`.

**Live note.** `run-live.sh` runs the in-process server for osrs230
(`EMBED_SERVER=1` + `TORIRS_TRANSPORT=embed`), so a client rebuild always
includes the matching server — there is no stale TCP `ToriRSServer` left on the
port. For a socket server under a debugger, start `src/build/torirsserver` by hand
against a TCP manifest.

## 6c. `chat_left` body text sits high — cache truth, not unpack loss

**Report.** Hans's short NPC line sits near the top of the parchment with a
large gap above "Click here to continue."

**Verdict.** Do **not** author `valign=1` into `chat_left.if`. Deob IF3 type-4
(`Widget.method6525`) stores y-text-alignment as a u8 defaulting to **0**
(top). Pristine `cache.osrs239` / content / baked omit `valign` on
`chat_left` `[text]` (= 0); `chat_right` / `chat_both` emit `valign=1`.
Unpack only writes `valign` when ≠ 0 (`cp_decode.c`). Engine top-align
(`ToriDraw2D_DrawStringBox` `y_align==0` → `base_y0 = max_ascent`) matches
deob `class439.method7680` `arg9==0` → `field4805 + arg2`. Short lines high
in the 67px body box are what Jagex encoded for 231.

## 6d. Rev239 effective flags, mouse resume, and object-backed menu ops

**Authority.** The rev239 deob is the reference here, not Client-TS:
`class545.method12093` chooses a server WidgetFlags override and otherwise falls
back to the widget's decoded flags; `method12078` permits a named operation when
its bit is set *or* the widget owns `on_op`; `method12079` exposes the target
verb when flags bits 11..16 are nonzero; `Statics.method5229` builds those rows.
LostCity places the particular labels/actions in interface content. The flag
resolution, CS2 widget mutators, menu construction, click dispatch and packets
are client engine.

Three previously separate symptoms were the same missing contract:

- The XP orb had cache-authored op bits but no server override. Treating a
  missing override as zero removed its Show/Setup rows and suppressed
  `IF_BUTTON1`. `App_IfEventsGetEffective` now distinguishes “not found” from
  an explicit zero override and falls back to `behavior.click_mask`. A headless
  click on `orbs:xp_drops` sends `IF_BUTTON1 160:6` and opens its subinterface.
- Mouse dialogue continue now builds action 30 from event bit 0 and sends
  rev239 `RESUME_PAUSEBUTTON` as the static parent uid plus dynamic sub-id.
  `talk hans 1` followed by a click on the prompt sends `231:5, sub=-1` and
  opens the choice interface; keyboard resume continues to use the same sink.
- Object-backed dynamic children use their script-installed operation ladder.
  `OC_IOP` consumes `(obj, one-based op)` from the integer stack (the C VM had
  incorrectly used the bytecode operand), and `CC/IF_SETTARGETVERB` now mutates
  the live widget instead of being discarded by the stack stub. The menu keeps
  named ops when `on_op` exists, adds the target verb as held-item selection,
  and supplies rev239's default op-7 Drop without duplicating ObjType rows.
  Object-backed `IF_BUTTONX` includes the object id and sends only for an armed
  op bit, while the local `on_op` still runs. Live Blood rune verification is
  exactly Use / Drop / Examine; clicking Drop emits `IF_BUTTONX` op 7 with
  object 565 and reaches classic `OPHELD5` normalization on the server.

Regression coverage: `test-net-exec`, `test-net-out-resume`,
`test-cs2-target-verb`, and `test-minimenu-world`.

## 7. Harness notes

Everything above was found and checked with knobs that already existed
(`REV230_UI_BLANK_PANELS.md` §3 lists more):

| knob | answered |
|---|---|
| `TORIRS_KEY_DEBUG=1` | which component's onKey script a keystroke reaches — how script 73 was found |
| `TORIRS_SIM_TYPE="frame,c<char>,…,k84"` | type and submit a chat line headlessly; use `k83` for space-as-code |
| `TORIRS_SIM_WHEEL="frame,x,y,notches"` | park the pointer and turn the wheel; pairs with `TORIRS_CAM_DEBUG=1` to see who got the notch |
| `TORIRS_NET_CHEAT="bank"` | open the bank without walking to a booth (body only — no leading `::`) |
| `TORIRS_DUMP_BOUNDS=<group>` | the clip rect a count was being cut against |
| `3rd/rscache/tools/cs2/cs2 decompile --rev osrs239 --cache cache.osrs239 --out DIR` | the whole 9,433-script corpus; grepping it is how `cc_setoutline(2)` and every `setnoclickthrough` call site were located |

Pixel measurements were taken by reading `TORIRS_EXIT_BMP` output directly
rather than by eye — the clipped-count bug is three pixels and the "before"
crop of it reads as a slightly odd font.

## 8. Not fixed here

- `ToriRSServer_Pack --check-only` is currently clean at 0 errors (15 warnings of
  the combat-block / door shape). Earlier drafts of this doc recorded 17
  category-lane errors from in-flight content work; those are gone on HEAD.
- `make -C src test-ui-slots` and `test-db` still fail on
  `[cache:boot] identity`, exactly as `REV230_UI_BLANK_PANELS.md` §4 recorded
  before any of that work either.
- The debug camera keys (`W`/`A`/`S`/`D`, spawn digits) are suppressed while
  `app->chat_input_active` is set, which at rev 230 is never — so typing a `w`
  into the chatbox also flies the camera. Same root as §1 (`slots.chat_index`);
  not part of the reported four, and the honest fix is a focus rule rather than
  a fifth gate.
