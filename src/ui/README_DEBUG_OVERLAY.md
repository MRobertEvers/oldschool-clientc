# Debug overlay (`ToriRSChrome`)

A developer overlay for the C client: bordered windows, minimenu-styled menus,
checkboxes, text inputs, labels and separators, drawn with two fonts baked out
of the cache into the binary.

It is **client chrome, not content** — the same category as
[uitree_minimenu.c](uitree_minimenu.c) and [uitree_hovertext.c](uitree_hovertext.c).
It carries no game-facing strings, no item/npc/interface ids and no
config-shaped constants: every string is caller-supplied, every widget is
addressed by a handle the module hands back, and the two fonts travel as *slots*
that the host maps onto whatever scene ids it registered.

| | |
| --- | --- |
| Model + display list | [uitree_debug_overlay.h](uitree_debug_overlay.h) / [uitree_debug_overlay.c](uitree_debug_overlay.c) |
| Baked glyph advances | [uitree_debug_font_metrics.h](uitree_debug_font_metrics.h) *(generated)* |
| Baked glyph bitmaps | [../engine/torirs_debug_font_baked.h](../engine/torirs_debug_font_baked.h) *(generated)* |
| UITree element | `UIELEM_BUILTIN_DEBUG_OVERLAY` in [uitree.h](uitree.h) |
| Emit pass | `emit_debug_overlay_pass` in [uitree_emit.c](uitree_emit.c) |
| Render translation | `case UITREE_EMIT_DEBUG_OVERLAY` in [../render/torirs_frame.c](../render/torirs_frame.c) |
| Model tests | [test/uitree_test_debug_overlay.c](test/uitree_test_debug_overlay.c) |
| Visual tests | [test/uitree_debug_overlay_visual.c](test/uitree_debug_overlay_visual.c) |

---

## 1. No dependencies

The module is one header, one `.c`, and a generated table of `static const int`.
That is the whole thing. It does not link a font implementation, a renderer, a
scene, a cache, or anything else in `ui/`. Measured, not asserted:

```console
$ gcc -std=c11 -O2 -Wall -Wextra -Iui -c -o dbg.o ui/uitree_debug_overlay.c
$ nm -u dbg.o
         U _memcmp
         U _memmove
         U _memset
         U _strcmp
         U _strlen
```

Five libc symbols and nothing else. That is what baking the fonts bought:
layout needs glyph advances, and advances that are compiled in need no cache, no
decoder and no init step. You can bring the overlay up before a cache is open.

No allocation either — `struct ToriRSChrome` is a fixed-size POD. On the i686 lane
it measures **54272 bytes**, of which 30720 is the `prims` array. Heap or
static, not a stack local.

## 2. Retained, not immediate

The overlay's cost is dominated by text measurement and layout, and both change
only when the app changes a widget — not once per frame. So:

* the model persists across frames;
* every mutator compares before it sets, so `SetText` with the same string is
  free and does not dirty anything;
* `ToriRSChrome_Build` returns `0` and does no work at all on a frame where
  nothing moved;
* the display list is handed to the emit layer **by pointer**. On a steady
  frame the entire overlay costs one host call and one pointer copy.

An immediate-mode overlay would re-measure every label every frame. That is
exactly the work this design removes, which is why it is retained.

## 3. Damage rectangles

The XP-era half of the design. Every mutation marks its panel dirty; `Build`
unions the panel's **old** and **new** bounds into the damage rect. That union
is the invalid region in the classic `WM_PAINT` sense — the smallest box that
has to be repainted for the frame to be correct, and it is exactly why the old
bounds are kept: moving a panel invalidates where it *was* as much as where it
*is*.

```c
struct ToriRSChromeRect dirty;

ToriRSChrome_PanelMove(&ui, panel, 40, 40);   /* was at 8,8 */
ToriRSChrome_Build(&ui);
if( ToriRSChrome_Damage(&ui, &dirty) )
{
    /* dirty covers both 8,8 and 40,40 — repaint that box, present, then: */
    ToriRSChrome_DamageClear(&ui);
}
```

Callers that can present a partial frame read it. Callers that always repaint
the whole canvas can ignore it entirely and still get the layout skip from §2.

---

## 4. Quick start

Four steps: register the fonts once, build the model once, push one UITree node,
answer one host request per frame.

### 4.1 Register the baked fonts with the scene (once, at startup)

```c
#include "engine/torirs_debug_font_baked.h"

int const font_id_small = 494; /* any free local scene handle */
int const font_id_menu  = 496;

ToriDraw_SceneFontAdd(scene, font_id_small, ToriRSChromeFont_Small());
ToriDraw_SceneFontAdd(scene, font_id_menu,  ToriRSChromeFont_Menu());
```

These are **local host handles, not cache ids** — the overlay never names a font
id, it names slot 0 or slot 1. Using the source archive numbers is just a
convenience.

> The returned fonts are statically allocated and live for the process. Never
> pass one to `ToriDraw_FontFree`: it frees every `glyph_alpha` pointer, and
> these point into a `const` blob.

### 4.2 Build the model (once, or whenever the app decides to)

```c
#include "ui/uitree_debug_overlay.h"

static struct ToriRSChrome g_dbg;   /* ~53 KB — static or heap, not a local */
static int g_wireframe, g_pos, g_cmd;

void
debug_ui_init(void)
{
    int panel;

    ToriRSChrome_Init(&g_dbg);

    panel = ToriRSChrome_PanelAdd(&g_dbg, TORIRS_CHROME_PANEL_WINDOW, 8, 8, 0, "Debug");
    ToriRSChrome_Label(&g_dbg, panel, "fps 60");
    ToriRSChrome_Separator(&g_dbg, panel);
    g_wireframe = ToriRSChrome_Checkbox(&g_dbg, panel, "wireframe", 0);
    g_pos       = ToriRSChrome_Checkbox(&g_dbg, panel, "show pos", 1);
    g_cmd       = ToriRSChrome_TextInput(&g_dbg, panel, "cmd", "");
}
```

`fixed_w` of `0` sizes the panel to its widest row. Pass a width to pin it.

### 4.3 Declare the node (once, in the boot manifest)

```ini
[revconfig:component:gameframe]
type=rs_iface

[revconfig:component:overlay]
type=debug_overlay

[revconfig:layout:root]
c=gameframe
=
c=overlay
```

That is the whole integration. `debug_overlay` takes no config: the bake
resolves both baked fonts through the scene bridge itself and marks the node
`always_dirty`, so §4.1 is only needed by a host that builds its tree by hand.
Declared *after* the frame means painted after it, and the ordering survives
everything the CS2 scripts do to the frame — see
[`docs/debug_overlay.md`](../../docs/debug_overlay.md) §3 for why, and for the
`TORIRS_DUMP_ROOTS` check. Every manifest with a root layout carries this block;
what the client then puts in the model — a ten-frame frame-time average behind
the `P` key — is `app_debug_overlay_*` in `src/app.c`, and §5 of that document.

By hand, for a host with no manifest (this is what the tests do):

```c
struct UITreeComponentSpec spec;

memset(&spec, 0, sizeof(spec));
spec.type = UIELEM_BUILTIN_DEBUG_OVERLAY;
spec.component_id = /* your id */;
spec.u.debug_overlay.font_id_small = font_id_small;
spec.u.debug_overlay.font_id_menu  = font_id_menu;
UITree_Push(tree, -1, &spec);
```

The node has no children and no layout of its own — the prims carry absolute
screen pixels and their own scissor boxes. `emit_debug_overlay_pass` runs
**after** every other walk pass, so the overlay lands above ordinary widgets
*and* above drag ghosts. It descends the tree rather than scanning root
siblings, so an overlay parented under a container with `p=` still draws.

### 4.4 Answer the host request (once per frame)

```c
case UITREE_HOST_GET_DEBUG_OVERLAY:
{
    int count = 0;
    struct ToriRSChromePrim const* prims;
    if( !req->u.get_debug_overlay.out_prims )
        return 0;
    prims = ToriRSChrome_Prims(&g_dbg, &count);
    *req->u.get_debug_overlay.out_prims = prims;
    return count;
}
```

Returning `0` is the normal, cheap case when there is no overlay: the pass costs
one host call and emits nothing at all.

### 4.5 Per-frame

```c
ToriRSChrome_SetText(&g_dbg, g_fps_label, fps_string);  /* no-op if unchanged */
ToriRSChrome_SetCaretVisible(&g_dbg, (ticks / 15) & 1); /* the app owns the clock */
ToriRSChrome_Build(&g_dbg);                             /* returns 0 if nothing moved */
```

`ui/` owns no clock, so the caret blink is app-driven — same reason the module
owns no keymap (see §6).

---

## 5. Features

### 5.1 Bordered backgrounds — `TORIRS_CHROME_PANEL_WINDOW`

A body fill, the **minimenu's chrome**, and a title bar in the menu face.
Content is clipped to the panel's inner rect, so an over-long label is cut at
the border rather than spilling onto the scene.

```c
int p = ToriRSChrome_PanelAdd(&ui, TORIRS_CHROME_PANEL_WINDOW, 8, 8, 120, "Stats");
ToriRSChrome_Label(&ui, p, "fps 60");
ToriRSChrome_LabelColored(&ui, p, "draws 812", 0x50FF50);
```

"Chrome" here is literal, not a family resemblance: the header bar, the
separator under it, the bottom rule and the two side rails come off the same
`dbg_menu_layout()` §5.2 uses, at the same offsets, and the title draws in the
same bold menu face at the same `x + 3`. A window panel and a real game
minimenu side by side differ only in what is inside them. There is no outer
outline — the minimenu has none, and it was the last thing that gave a panel
away.

The palette keys stay separate (`panel_*` vs `menu_*`) so the flat developer
theme can keep a legible grey title; it is the osrs theme that points them at
the same colours.

Pass a resizable panel a grip:

```c
ToriRSChrome_PanelSetResizable(&ui, p, 1);
```

Three carets in the bottom-right corner, in a strip the layout reserves for
them so they never sit under a row. Dragging takes both edges with the cursor
and writes what it lands on into `fixed_w` / `fixed_h`; the origin never moves,
which is why the grip is in that corner and not another.

There is no scrolling here, so a panel dragged shorter than its content
**drops** the rows that no longer fit — undrawn *and* unclickable, because
`dbg_build_window` zeroes their hit boxes. A row that is invisible but still
toggles when clicked is the worse failure, and the one worth ruling out by
construction. Grow the panel back and the rows return. If these panels ever get
a scroll offset, this is the behaviour it replaces.

Visual: `build/debug_overlay_01_bordered_background.bmp`,
`build/debug_overlay_08_clipping.bmp`, `build/debug_overlay_14_grip.bmp`,
`build/debug_overlay_16_grip_clamped.bmp`.

### 5.2 Menus — `TORIRS_CHROME_PANEL_MENU`

The minimenu's chrome: body fill, black title bar, black separator and
side/bottom border strips, shadowed rows that go accent-coloured on hover.

```c
int m = ToriRSChrome_PanelAdd(&ui, TORIRS_CHROME_PANEL_MENU, 100, 40, 0, "Choose Option");
ToriRSChrome_MenuItem(&ui, m, "Teleport");
ToriRSChrome_MenuItem(&ui, m, "Toggle roofs");
ToriRSChrome_MenuItem(&ui, m, "Cancel");
```

Geometry is not re-invented: `dbg_menu_layout()` recomputes
`UIMinimenu_LayoutFromLineBox` verbatim, and
`test_debug_overlay_menu_geometry` asserts the two agree for every line box in
8..24 plus the derived height, row pitch and hit-box quantities. If one drifts,
a test fails.

Two deliberate differences from the game minimenu:

* Rows read **top-to-bottom**. The reference draws bottom-to-top because its
  option list is built in reverse; a debug menu is authored in the order it is
  read.
* Hit bands overlap by exactly one pixel, because `hover_above + hover_below ==
  box` while `row_stride == box - 1`. `UIMinimenu_HitOption` returns the *first*
  match, so the shared seam belongs to the row **above** — and the overlay's
  half-open boxes plus first-match iteration reproduce that exactly. Asserted,
  so the two cannot diverge.

Visual: `build/debug_overlay_02_menu.bmp`.

### 5.3 Checkboxes

```c
int wf = ToriRSChrome_Checkbox(&ui, p, "wireframe", 0);
...
if( ToriRSChrome_TakeActivated(&ui) == wf )
    renderer_set_wireframe(ToriRSChrome_Checked(&ui, wf));
```

A click toggles on mouse-up and latches the widget as activated. The mark is a
filled inset square in `theme.check_mark`.

Visual: `build/debug_overlay_03_checkbox.bmp`, `..._04_checkbox_toggled.bmp`.

### 5.4 Text inputs

```c
int cmd = ToriRSChrome_TextInput(&ui, p, "cmd", "");
...
if( ToriRSChrome_TakeActivated(&ui) == cmd )       /* Enter committed it */
    console_run(ToriRSChrome_Text(&ui, cmd));
```

A labelled box with a caret. Focus follows mouse-down; the border switches to
`theme.input_border_focus`. Content is clipped to the box's inner rect, and the
caret is a 1px rule drawn in `theme.input_text` at the caret column. Editing
keys are `BACKSPACE`, `DELETE`, `LEFT`, `RIGHT`, `HOME`, `END`, `ENTER`,
`ESCAPE`; printable bytes come in separately (see §6).

Visual: `build/debug_overlay_05_textinput_caret_on.bmp`,
`..._06_textinput_caret_off.bmp`.

### 5.5 Dropdowns, menus and the scrollbar

```c
int tool = ToriRSChrome_Dropdown(&ui, p, "Tool", tool_names, TOOL_COUNT, 0);
int file = ToriRSChrome_MenuDrop(&ui, bar, "File", file_items, 4);
...
if( ToriRSChrome_TakeActivated(&ui) == tool )
    editor_set_tool(ToriRSChrome_DropdownSelected(&ui, tool));
```

`options` is **borrowed**, not copied: the array and every string in it must
outlive the widget. One popup list serves the whole overlay (see
`dropdown_open`), so only one can ever be open, and it is built after every
panel and therefore never buried under one.

**Two looks, because the game has two widgets.** A value dropdown is the
cache's CS2 dropdown and a `MenuDrop` is the minimenu, and no palette turns one
into the other, so the split is in the code rather than in the theme:

| | Value dropdown | Menu (`MenuDrop`) |
| --- | --- | --- |
| Closed state | tiled button, framed and inset, arrow on the **right** | the bare title |
| List body | the list's own tile (`TORIRS_CHROME_SKIN_DROPDOWN_BODY`) | flat `menu_body` |
| List edge | the button's own frame: `dropdown_border` with `dropdown_border_inner` a pixel inside it | one `menu_chrome` rule |
| Rows | centred, `dropdown_text`, alternating black bands | left-aligned, `menu_text` |
| Hover | the row's band thins out, lightening it | the row's **text** goes `menu_hover_text` |

Every number in the first column is read off the scripts that build the real
thing — `script_3850` for the button, `script_9114` for the list — down to the
`cc_settrans` values of the two bands (220 and 200) and the thinner veil under
the cursor (240). The arrow is the cache's own sprite, and it is the *same*
sprite the scrollbar's ends wear: down while the list is shut, up while it is
open.

**The chosen row is not marked.** It used to draw in the accent, which is a
highlight the reference's list does not have: the row under the pointer is the
only one it picks out, and it picks it out by its BAND. Two highlights in two
colours read as two cursors, and the chosen option is already stated by the
button above the list.

The list wearing the button's own frame rather than a single flat rule is the
same rule from the other side: in the reference the two are one control seen
open, and a list edged in one line reads as a tooltip that happened to appear
under a field.

**The scrollbar** appears on a list that overflows (`TORIRS_CHROME_DROPDOWN_ROWS`
rows are shown at once), 16 chrome pixels wide, *inside* the list — so the rows
lose that width rather than running under the bar. It is the client's bar in
both of the forms the client draws it:

- with the baked skin, the six sprites `~script31` assembles — two arrow
  buttons, a track, and a grip whose middle stretches between two 5px caps;
- without it, the flat IF1 form: `scroll_track` under a `scroll_grip` with a
  `scroll_grip_hi` highlight down its top and left and a `scroll_grip_lo`
  shadow down its bottom and right (the same four values as
  `UITREE_SCROLLBAR_*_ARGB`, restated here because this module has no
  dependencies).

It is a control, not a picture: an arrow steps a row, the track pages by the
window, and the grip drags. The wheel still works over the list either way.

Visual: `build/debug_overlay_17_dropdown_closed.bmp`,
`..._18_dropdown_open_short.bmp`, `..._19_dropdown_open_long.bmp`,
`..._20_dropdown_scrolled_to_end.bmp`, `..._22_dropdown_no_skin.bmp`,
`..._23_dropdown_3x.bmp`, `..._21_menubar_dropdown.bmp`.

### 5.6 Labels and separators

`ToriRSChrome_Label` / `ToriRSChrome_LabelColored` (colour `0` = the theme's `text`),
and `ToriRSChrome_Separator` — a 1px rule with air above and below. Neither is
hit-testable: `HitTest` skips both, so a click passes through to whatever is
underneath.

---

## 6. Input

The overlay does not read the keyboard; it is fed. `ui/` owning a keymap would
be a dependency, and printable-byte decoding is the platform layer's job
anyway.

```c
if( ToriRSChrome_MouseMove(&ui, mx, my) )  return; /* consumed: pointer over a panel */
if( ToriRSChrome_MouseDown(&ui, mx, my) )  return;
if( ToriRSChrome_MouseUp(&ui, mx, my) )    return;

if( ToriRSChrome_KeyEdit(&ui, TORIRS_CHROME_KEY_BACKSPACE) ) return;
if( ToriRSChrome_KeyChar(&ui, ch) )                    return;   /* printable byte */

{
    int w = ToriRSChrome_TakeActivated(&ui);   /* one latch, -1 when nothing fired */
    if( w >= 0 )
        handle_activation(w);
}
```

Each returns `1` when it consumed the event, so routing is "overlay first, then
the game". `ToriRSChrome_HitTest(&ui, x, y)` answers the same question without
mutating anything — useful for a cursor change or a "is the pointer over debug
chrome" check.

Panels hit-test back to front (a later panel wins over an earlier one);
widgets within a panel hit-test in insertion order.

Press and release must land on the same widget, so dragging off a checkbox
cancels the toggle instead of firing it. Mouse-down on a text input takes focus
and puts the caret at the end; mouse-down anywhere else drops focus.

---

## 7. The pipeline

```
ToriRSChrome (retained model)
  |  ToriRSChrome_Build            relayout + rebuild the prim array, only when dirty
  v
struct ToriRSChromePrim[]            flat POD display list, absolute pixels, per-prim clip
  |  UITREE_HOST_GET_DEBUG_OVERLAY     host hands back the pointer + count
  v
emit_debug_overlay_pass         ONE UITreeEmitDesc for the whole list
  |                             (debug_prims, debug_prim_count, debug_font_id[2])
  v
torirs_frame.c                  expands the desc into N render commands via sb_steps
  v
TORIRSRC_FILL_RECT / TORIRSRC_DRAW_STRING
```

Two things are worth knowing about the middle of that:

**One desc, N commands.** `ToriRS_FrameNextCommand` reuses the multi-step
mechanism the scrollbars use (`is_scrollbar = 1; sb_steps = debug_prim_count`),
so the whole display list travels as a pointer and a count. Nothing copies
per-prim state into the emit buffer. `test_debug_overlay_emit_pass` asserts
pointer identity, so a future "helpful" copy fails a test.

**The alpha byte belongs to the frame layer.** Prims carry `0xRRGGBB`, the same
convention the UITree colour fields use; `torirs_frame.c` supplies alpha with
`emit_color_argb(color, 0)`. A raw copy draws nothing at all —
`ToriDraw2D_FillRect` early-returns on alpha 0.

**`y` is a baseline, not a box top,** for `TORIRS_CHROME_PRIM_TEXT`. That is the
reference `PixFont.drawString` convention, which `ToriDraw2D_DrawString`
follows (`y -= font->line_height`).

---

## 8. Theme

`0xRRGGBB` colours plus a handful of non-colour switches.
`ToriRSChrome_Init` installs `torirs_chrome_theme_default`; `ToriRSChrome_SetTheme`
swaps it wholesale.

| Group | Fields |
| --- | --- |
| Window panel | `panel_body` `panel_border` `panel_title_bg` `panel_title_text` |
| Text | `text` `text_dim` `accent` `separator` |
| Text input | `input_bg` `input_border` `input_border_focus` `input_text` |
| Checkbox | `check_box` `check_mark` |
| Menu | `menu_body` `menu_chrome` `menu_text` `menu_hover_text` |
| Dropdown | `dropdown_border` `dropdown_border_inner` `dropdown_text` `dropdown_veil` |
| Dropdown (trans) | `dropdown_band_trans` `dropdown_band_trans_alt` `dropdown_row_trans_hover` `dropdown_hover_trans` |
| Scrollbar | `scroll_track` `scroll_grip` `scroll_grip_hi` `scroll_grip_lo` |
| Switches | `text_shadowed` `font_row` `skin_panel_body` `skin_dropdown` |

The `*_trans` fields are the **client's** transparency, `0` opaque to `255`
invisible — not an alpha. They are written that way round because the values
are lifted straight from the `cc_settrans` calls in the scripts that draw the
real widget, and a field that read the other way would have every one of them
inverted at the call site.

A checkbox is a SPRITE in this game -- there is no drawn checkbox anywhere in
the cache to imitate, so the flat box-with-a-mark is the *fallback* and the
tick/cross pair is the control. Same for a roster row's on/off, which was a
sliding switch until it was pointed out that this game has no such thing.

### 8.0 One table of metrics, two presentations

The geometry and the palette live in `ui/torirs_chrome_metrics.h`, and neither
this module nor the CS2 executor owns them. The plugin window is drawn twice —
here as prims, and in `ui/torirs_chrome_exec_cs2.c` as real interface
components — and for as long as each carried its own numbers the two slowly
stopped agreeing: a row 20 tall beside one 18 tall, a 12px settings well beside
a 14px one, a toggle at 22×11 beside one at 24×12. None of that is a bug either
file can see; it shows up only as "the panel looks different depending on which
executor is bound".

Everything in that header is a **1x chrome pixel**: this module multiplies by
`ui->scale` (`DBG_PX`), the CS2 executor uses them raw because the gameframe's
interface scaling applies over the top.

Two consequences worth knowing, because both were behaviour changes:

- **Every row is one height** (`TORIRS_CHROME_M_ROW_H`), whatever it holds.
  Widgets used to measure themselves, which gave a column whose controls did
  not line up with each other. Contents are centred inside the grid instead —
  `dbg_row_text_baseline` is ToriDraw's own `y_align == 1` arithmetic, which is
  what every CS2 `TEXT` component uses, and the two land on the same pixel row
  because the baked faces have `line_box == max_ascent + max_descent` exactly.
- **The label column is fixed** (`TORIRS_CHROME_M_LABEL_W`) rather than
  measured from the panel's widest label. Measured meant a plugin renaming a
  setting slid every field in the panel sideways. An *unlabelled* row still
  reserves nothing — there is nothing to line it up with.

### 8.0.1 The optional panel frame

`ToriRSChrome_PanelSetFramed` swaps the minimenu's rails for the interfaces'
own nine-slice border — the one the gameframe's popout strip draws around the
panels mounted in it, which is why the plugin window wore one under the CS2
executor and not in canvas.

Opt-in rather than implied by the window style, because both alternatives are
live in this tree at once: a floating developer panel wants the rails so it
reads as a menu beside a real minimenu, and a panel mounted inside something
that already draws a frame must not draw a second one inside the first.

The corners are **rounded** — the outer pixel of each 3×3 corner is
transparent — so the pieces are blitted rather than one sprite stretched over
the box, and the baked centre is never drawn: the panel's own tile is already
under it. `dbg_panel_is_framed` gates the LAYOUT as well as the draw, so a
build that baked no frame is not a panel indented past an edge it never paints.

The two `skin_*` switches are what a theme uses to ask for the baked cache art
(§8.1) instead of flat boxes. Off is the flat look *and* the automatic
fallback: each draw checks the slot it is about to use against `skin_avail`,
so a build with the skin module stubbed out still renders.

The menu group defaults to the minimenu's own palette, so a debug menu and a
game minimenu on screen at the same time read as the same widget. Under
`torirs_chrome_theme_osrs` the window group points at those same values —
`panel_title_text` is the minimenu's brown-on-black, not the interfaces'
heading orange — so a window panel matches too.

### 8.1 The baked skin

`engine/torirs_chrome_skin_baked.c` is a `spritebake` run over the cache: a
handful of cache sprites as compiled-in ARGB arrays, addressed by *semantic
slot* (`enum ToriRSChromeSkinSlot`) rather than by archive id, so a re-bake from a
different cache — or no bake at all — needs no change here.

| Slot | Cache sprite | Drawn as |
| --- | --- | --- |
| `PANEL_BODY` | `tradebacking` (297) | tiled behind a window panel and a closed dropdown |
| `DROPDOWN_BODY` | 1040 | tiled behind an open dropdown list |
| `SCROLL_UP` / `SCROLL_DOWN` | 773 / 788 | the bar's two arrow buttons, and the dropdown button's arrow |
| `SCROLL_TRACK` | 792 | stretched down the bar between the arrows |
| `SCROLL_GRIP_TOP/MID/BOTTOM` | 789 / 790 / 791 | the grip: middle stretched, then a cap on each end |
| `PLUGIN_ICON` | `sideicons_interface_11` (785) | the wrench that opens the plugin window from the gameframe's strip |
| `CHECK_ON` / `CHECK_OFF` | 8380 / 8379 | every on/off state: a checkbox, and a roster row's switch |
| `FRAME_TOP_LEFT` … `FRAME_BOTTOM_RIGHT` | 5814–5822 | the nine-slice border a framed panel wears |
| `CLOSE` / `CLOSE_OVER` | 831 / 832 | a closable panel's window X, resting and under the cursor |

**A closable panel has ONE title-bar button.** There was an Ok beside the close
box wearing `CHECK_ON`, which fired the panel's `confirm_widget` on the way out.
Both are gone, along with `TORIRS_CHROME_INTENT_CONFIRM` and the web page's
`ok` button — a green tick is the game's answer to a *question*, not a way out
of a window, and in a title bar it read as a second unlabelled copy of the Save
row a few pixels below. Closing discards; a page that stages edits carries Save
and Revert as labelled rows. A kind nothing can raise is worse than an absent
one, which is why the intent went with the button rather than staying behind.

**`CLOSE` is not `CHECK_OFF`.** The red cross is the game's *no* answer — the
other half of the tick, as it appears against every boolean setting — and using
it to shut a window read as rejecting whatever the panel was showing. 831/832
are the button the interfaces actually put in a title bar. The cache carries the
same button at 24x24 as well (799/800); the 16x16 pair is baked because the
title bar's button box is 14 at 1x chrome scale and the bake has no scale
variants, so the larger art would land there as a 0.58x downscale.

The pair is **one image lit from opposite corners** — the glyph pixels are
identical and only the bevel flips, so the resting button reads as raised and
the hovered one as pressed in. That is the whole hover effect, and it is why
`dbg_build_window` does *not* lay the accent outline over this one control: art
that already says "hovered" plus an outline reads as selected instead.

Because these buttons are panel chrome rather than widgets, nothing in the
widget hover path marks them dirty. `ToriRSChrome::hover_button_panel` /
`hover_button_which` are what `ToriRSChrome_MouseMove` compares to trigger the
repaint — without them the pressed art would appear only on a frame something
unrelated had already rebuilt. `visual_panel_close` asserts the `Build` return,
not just the pixels, for exactly that reason.

**`CHECK_ON` is 8380, not 8379**, which is the opposite of what the ids
suggest. `script3422` sets 8379 alongside `if_setop(1, "Show", ..)` -- the op is
what a click *will do*, so a row offering "Show" is currently hidden, and 8379
is the red cross. Baking them the other way round renders cleanly and is simply
backwards, so `visual_checkbox_skinned` asserts the hue directly: the pixel
comparisons cannot catch a swap, since a swapped pair blits just as faithfully.

The host uploads them as one multi-frame scene entry and sets one `skin_avail`
bit per slot it actually got; `skin_tile_w/h` carry the tile's size so the
tiling loop up here can step by it without holding any pixels.

Regenerate with the command in the generated file's header comment.

**The bake has a second consumer, and it does not draw prims.** The interface-
tree presentation of the plugin window (`ui/torirs_chrome_exec_cs2.c`) builds
real components, so it reaches these images through the scene rather than
through the display list: the host hands it the multi-frame sprite's scene id,
and a component names a slot with `UIBuildComponent::graphic_atlas_index`
beside `graphic_scene_id`. `PLUGIN_ICON` is baked for that consumer alone --
nothing up here emits it.

That is also why it is baked rather than resolved. Archive 785 is the wrench
only on the OSRS cache it was chosen from; asking a different cache for 785
gets a confidently wrong picture rather than a missing one, and asking any
cache for it costs a load round-trip during which the button cannot be built at
all.

---

## 9. Fonts

Both generated files come out of one `fontbake` run over one cache, so the
advances the overlay lays out with and the glyphs the renderer draws cannot
disagree.

Three faces, each baked at three sizes:

| Slot | Archive | Ascent | Line box | Glyph bytes |
| --- | --- | --- | --- | --- |
| `TORIRS_CHROME_FONT_SMALL` | 494 | 10 / 20 / 30 | 12 / 24 / 36 | 2725 / 10900 / 24525 |
| `TORIRS_CHROME_FONT_BODY` | 495 | 12 / 24 / 36 | 16 / 32 / 48 | 4237 / 16948 / 38133 |
| `TORIRS_CHROME_FONT_MENU` | 496 | 12 / 24 / 36 | 16 / 32 / 48 | 5045 / 20180 / 45405 |

In a dat2 cache the metrics blob lives in the **fonts** table and the glyph
bitmaps live in the **sprites** table at the same archive id; `fontbake` reads
both.

### Why the sizes are baked and not scaled

A HighDPI display gives the client a framebuffer with twice the pixels per
inch. Chrome authored for 1x pixels and drawn into it comes out half the
physical size, and the obvious fix — draw it small and stretch the result — is
not available here and should not be: `ToriDraw2D` blits a glyph by testing its
mask byte for non-zero and writing the colour (`toridraw_font.c`). There is no
coverage, no blend, and therefore nothing for a filter to interpolate. A
stretched pixel font is a stretched pixel font.

So the sizes are authored. `fontbake --font 496=Menu2x@2` block-scales the
glyph masks on an integer grid and multiplies every metric — advance, offsets,
ascent, line box — by the same integer. The result is not an approximation of
the face at twice the size: at scale N each source pixel is an N×N block, so
the 2x chrome is the 1x chrome with every coordinate doubled, exactly. The
visual test asserts that as an equality on drawn pixel COUNT (4x at 2x, 9x at
3x), which is the assertion that catches a resampled glyph or a rule left at
1px.

Integer only, for the same reason: a 1.5x glyph would land stems on half
pixels and the mask test would round them to uneven widths. `TORIRS_CHROME_SCALE_MAX`
is 3 because three sizes are baked — raising it means baking the size first, in
the one `fontbake` run that writes both generated files.

### Who sets the scale

`ToriRSChrome_SetScale` relayouts the chrome; `UITreeSceneBridge_EnsureDebugFont`
resolves the slot against the same scale so layout and glyphs cannot come from
different bakes. `App_SetChromeScale` is the one call that does both (and
re-points the tree's overlay components), and the desktop shell drives it from
`PlatformSDL2_PixelDensity` every frame — so a window dragged between a Retina
display and an ordinary one re-bakes its chrome size on arrival.

`TORIRS_CHROME_SCALE=N` pins it, which is how scaled chrome gets worked on from
an ordinary display. `TORIRS_HIDPI=0` gives up the HighDPI drawable entirely,
for a machine where the software rasteriser cannot afford 4x the pixels.

### Regenerating

```bash
make -C 3rd/rscache/tools fontbake
./3rd/rscache/tools/fontbake/fontbake --rev osrs239 "$PWD/cache.osrs239" \
    --font 494=Small   --font 495=Body   --font 496=Menu \
    --font 494=Small2x@2 --font 495=Body2x@2 --font 496=Menu2x@2 \
    --font 494=Small3x@3 --font 495=Body3x@3 --font 496=Menu3x@3 \
    --prefix ToriRSChromeFont \
    --out     src/engine/torirs_debug_font_baked.c \
    --header  src/engine/torirs_debug_font_baked.h \
    --metrics src/ui/uitree_debug_font_metrics.h
```

(On the Windows host-tool lane that is `mingw32-make ... CC=gcc` with the
mingw64 compiler on PATH, not the i686 one the client builds with.)

`--metrics` is the advance-table half that keeps the module dependency-free;
`--out`/`--header` are the glyph half the renderer needs. `@1` is the identity:
a 1x bake through the scaling path is byte-identical to one without it.

---

## 10. Tests

```bash
mingw32-make -C src test-uitree                 # model
mingw32-make -C src test-debug-overlay-visual   # rasters
```

**Model** — [test/uitree_test_debug_overlay.c](test/uitree_test_debug_overlay.c),
part of the UITree suite: measurement, menu geometry against
`UIMinimenu_LayoutFromLineBox`, the retained no-op path, damage unions, widget
behaviour, and the emit pass's pointer identity.

**Visual** — [test/uitree_debug_overlay_visual.c](test/uitree_debug_overlay_visual.c)
goes display list → emit desc → `ToriRS_Frame` → the software rasteriser with
the real baked fonts, asserts on the resulting pixels, and writes one BMP per
feature into `build/`:

| BMP | Prims | Covers |
| --- | --- | --- |
| `01_bordered_background` | 8 | body fill, border, title bar, labels |
| `02_menu` | 11 | minimenu chrome, hover row, shadowed text |
| `03_checkbox` / `04_checkbox_toggled` | 14 / 15 | box, mark, toggle |
| `05_textinput_caret_on` / `06_..._off` | 13 / 12 | focus border, caret phases |
| `07_damage` | 5 | old ∪ new bounds after a move |
| `08_clipping` | 5 | content cut at the panel's inner rect |
| `09_kitchen_sink` | 35 | everything at once |
| `10_skin_off` / `11_skin_on` | 9 / 13 | flat fallback vs the tiled parchment |
| `11_scale1x/2x/3x` | 12 | HighDPI relayout; ink area is exactly `scale²` |
| `12`/`13_chrome_*_drag` | 12 | minimenu chrome on a window, carried by its header |
| `14`/`15`/`16_grip*` | 19 / 19 / 13 | resize grip, drag, clamp |
| `17_dropdown_closed` | 29 | the CS2 button: tile, frame, inset, right arrow |
| `18_dropdown_open_short` | 39 | banded rows, centred, the hover veil |
| `19_dropdown_open_long` | 59 | the scrollbar column, mid-list |
| `20_dropdown_scrolled_to_end` | 58 | arrow / track / grip driven by the mouse |
| `21_menubar_dropdown` | 9 | a menu list did **not** become a settings dropdown |
| `22_dropdown_no_skin` | — | the flat IF1 bar, with the skin withheld |
| `23_dropdown_3x` | — | sprites blown up to the box, not left at 16px |

Assertions are written against theme colours and module-reported geometry, never
against hardcoded pixel coordinates, so moving a panel in the test does not mean
editing a table of magic numbers.

The visual target links the real frame translator and the real rasteriser but
**not** `world/world.c`: `torirs_frame.c` only reaches `World_*` on the
`UITREE_EMIT_WORLD` path, which an overlay-only emit buffer never takes, so the
test closes the link with seven stubs rather than dragging painters, collision,
heightmap and minimap into a rectangles-and-text test. Everything under
`src/render/` is the real code; only the world *model* is shimmed.

Note that `TORIRSRC_FONT_LOAD` is a no-op in the soft3d backend, which is why
the test registers the baked fonts with `ToriDraw_SceneFontAdd` directly.

---

## 11. Capacity

| | |
| --- | --- |
| `TORIRS_CHROME_MAX_PANELS` | 16 |
| `TORIRS_CHROME_MAX_WIDGETS` | 128 (across all panels) |
| `TORIRS_CHROME_MAX_PRIMS` | 512 |
| `TORIRS_CHROME_LABEL_MAX` | 64 bytes, with the terminator |
| `TORIRS_CHROME_INPUT_MAX` | 64 bytes, with the terminator |

`PanelAdd` and the widget constructors return `-1` when full. `Build` stops
early and sets `ui->overflow` rather than writing past the prim array — check it
if you are generating panels programmatically.

---

## 12. API summary

| | |
| --- | --- |
| **Lifecycle** | `Init` `Reset` `SetTheme` |
| **Panels** | `PanelAdd` `PanelMove` `PanelSetVisible` `PanelSetResizable` `PanelRect` |
| **Widgets** | `Label` `LabelColored` `Checkbox` `TextInput` `Separator` `MenuItem` |
| **Mutation** | `SetText` `SetLabel` `SetColor` `SetChecked` `SetCaretVisible` |
| **Query** | `Checked` `Text` `HitTest` `MeasureText` `FontLineHeight` `FontLineBox` |
| **Input** | `MouseMove` `MouseDown` `MouseUp` `KeyChar` `KeyEdit` `TakeActivated` |
| **Display list** | `Build` `Prims` `Damage` `DamageClear` |

All prefixed `ToriRSChrome_`. Full documentation is in
[uitree_debug_overlay.h](uitree_debug_overlay.h) — this file is the tour, the
header is the reference.
