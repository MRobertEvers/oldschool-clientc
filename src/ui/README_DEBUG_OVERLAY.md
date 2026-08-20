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
struct ToriDbgRect dirty;

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

ToriDraw_SceneFontAdd(scene, font_id_small, ToriDbgFont_Small());
ToriDraw_SceneFontAdd(scene, font_id_menu,  ToriDbgFont_Menu());
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

    panel = ToriRSChrome_PanelAdd(&g_dbg, TORIDBG_PANEL_WINDOW, 8, 8, 0, "Debug");
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
    struct ToriDbgPrim const* prims;
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

### 5.1 Bordered backgrounds — `TORIDBG_PANEL_WINDOW`

A body fill, a 1px border, and a title bar in the menu face. Content is clipped
to the panel's inner rect, so an over-long label is cut at the border rather
than spilling onto the scene.

```c
int p = ToriRSChrome_PanelAdd(&ui, TORIDBG_PANEL_WINDOW, 8, 8, 120, "Stats");
ToriRSChrome_Label(&ui, p, "fps 60");
ToriRSChrome_LabelColored(&ui, p, "draws 812", 0x50FF50);
```

An outline needs no new render command: a `TORIDBG_PRIM_RECT` with `filled == 0`
becomes `ToriDraw2D_DrawRectOutline`.

Visual: `build/debug_overlay_01_bordered_background.bmp`,
`build/debug_overlay_08_clipping.bmp`.

### 5.2 Menus — `TORIDBG_PANEL_MENU`

The minimenu's chrome: body fill, black title bar, black separator and
side/bottom border strips, shadowed rows that go accent-coloured on hover.

```c
int m = ToriRSChrome_PanelAdd(&ui, TORIDBG_PANEL_MENU, 100, 40, 0, "Choose Option");
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

### 5.5 Labels and separators

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

if( ToriRSChrome_KeyEdit(&ui, TORIDBG_KEY_BACKSPACE) ) return;
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
struct ToriDbgPrim[]            flat POD display list, absolute pixels, per-prim clip
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

**`y` is a baseline, not a box top,** for `TORIDBG_PRIM_TEXT`. That is the
reference `PixFont.drawString` convention, which `ToriDraw2D_DrawString`
follows (`y -= font->line_height`).

---

## 8. Theme

18 `0xRRGGBB` fields. `ToriRSChrome_Init` installs `toridbg_theme_default`;
`ToriRSChrome_SetTheme` swaps it wholesale.

| Group | Fields |
| --- | --- |
| Window panel | `panel_body` `panel_border` `panel_title_bg` `panel_title_text` |
| Text | `text` `text_dim` `accent` `separator` |
| Text input | `input_bg` `input_border` `input_border_focus` `input_text` |
| Checkbox | `check_box` `check_mark` |
| Menu | `menu_body` `menu_chrome` `menu_text` `menu_hover_text` |

The menu group defaults to the minimenu's own palette, so a debug menu and a
game minimenu on screen at the same time read as the same widget.

---

## 9. Fonts

Both generated files come out of one `fontbake` run over one cache, so the
advances the overlay lays out with and the glyphs the renderer draws cannot
disagree.

| Slot | Archive | Ascent | Line box | Glyph bytes |
| --- | --- | --- | --- | --- |
| `TORIDBG_FONT_SMALL` | 494 | 10 | 12 | 2725 |
| `TORIDBG_FONT_MENU` | 496 | 12 | 16 | 5045 |

In a dat2 cache the metrics blob lives in the **fonts** table and the glyph
bitmaps live in the **sprites** table at the same archive id; `fontbake` reads
both.

To regenerate (the host-tool lane wants the mingw64 compiler, not the i686 one
the client builds with):

```bash
export PATH="/c/Users/mrobe/Documents/git_repos/oldschool-clientc/toolchain/mingw64/bin:$PATH"
mingw32-make -C 3rd/rscache/tools fontbake CC=gcc
./3rd/rscache/tools/fontbake/fontbake --rev osrs239 \
    /c/Users/mrobe/Documents/git_repos/oldschool-clientc/cache.osrs239 \
    --font 494=Small --font 496=Menu --prefix ToriDbgFont \
    --out     src/engine/torirs_debug_font_baked.c \
    --header  src/engine/torirs_debug_font_baked.h \
    --metrics src/ui/uitree_debug_font_metrics.h
```

`--metrics` is the advance-table half that keeps the module dependency-free;
`--out`/`--header` are the glyph half the renderer needs.

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
| `TORIDBG_MAX_PANELS` | 16 |
| `TORIDBG_MAX_WIDGETS` | 128 (across all panels) |
| `TORIDBG_MAX_PRIMS` | 512 |
| `TORIDBG_LABEL_MAX` | 64 bytes, with the terminator |
| `TORIDBG_INPUT_MAX` | 64 bytes, with the terminator |

`PanelAdd` and the widget constructors return `-1` when full. `Build` stops
early and sets `ui->overflow` rather than writing past the prim array — check it
if you are generating panels programmatically.

---

## 12. API summary

| | |
| --- | --- |
| **Lifecycle** | `Init` `Reset` `SetTheme` |
| **Panels** | `PanelAdd` `PanelMove` `PanelSetVisible` `PanelRect` |
| **Widgets** | `Label` `LabelColored` `Checkbox` `TextInput` `Separator` `MenuItem` |
| **Mutation** | `SetText` `SetLabel` `SetColor` `SetChecked` `SetCaretVisible` |
| **Query** | `Checked` `Text` `HitTest` `MeasureText` `FontLineHeight` `FontLineBox` |
| **Input** | `MouseMove` `MouseDown` `MouseUp` `KeyChar` `KeyEdit` `TakeActivated` |
| **Display list** | `Build` `Prims` `Damage` `DamageClear` |

All prefixed `ToriRSChrome_`. Full documentation is in
[uitree_debug_overlay.h](uitree_debug_overlay.h) — this file is the tour, the
header is the reference.
