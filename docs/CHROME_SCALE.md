# Chrome at every scale: the skin and font tiers

Workbench step 6.0b (docs: the workbench design, section 2 "R1 in full"), part
2 of 3: the skin and the fonts baked at 1x, 2x and 3x, reproducibly. Which
tier the native chrome draws at which scale (the rational scale, the
once-rounded metrics, area-averaging down from the tier at or above the
scale) is the draw path's business and another slice's; this part gives it the
art and one accessor for each kind.

Since the 6.0b integration (2026-11-05) the bake's skin cache
(`ToriRSChromeBake_Skin`, src/ui/torirs_chrome_bake.c) reads these tiers
through `ToriRSChromeSkin_SpriteTier`: at a fractional scale it area-averages
down from the tier at or above it, at 2x and 3x it uses the tier itself. The
integer draw path the native client runs today (the scene bridge's fonts and
skin) still draws 1x art scaled, and only at whole scales: until the bridge
draws the bake's faces and skin, main.c hands the native chrome
`ToriRSChromeScaleQ_Whole(q)` (125 % draws 1x, 150 % and 175 % 2x, 250 % 3x)
and only the DOM the fractional scale. docs/MAPED_STATUS.md "Workbench steps
6.0b and 6.1" lists what is left.

## What is baked

| Art | 1x | 2x and 3x | Tool |
|---|---|---|---|
| Skin, 58 slots | `src/engine/torirs_chrome_skin_baked.c` (spritebake, from the cache) | `src/engine/torirs_chrome_skin_tiers.c` | `tools/chrome_skin_tiers.py` |
| Web skin | `res/plugin_chrome/skin/` | `res/plugin_chrome/skin@2x/`, `skin@3x/` | the same run |
| Fonts Small p11, Body p12, Menu b12 | `src/engine/torirs_debug_font_baked.c` | the same file, derived (fontbake `@2`, `@3`) | checked by `tools/chrome_font_tiers.py` |

Neither tool reads the cache. Both derive from the checked-in 1x C, the way
`tools/torirs_chrome_fonts.py` derives the web fonts: the bake is the source.

## The two kinds of skin slot

`src/engine/torirs_chrome_skin_class.h` classifies every slot in one table
(`ROW(Name, CLASS, WRAP)`), read by the C (`ToriRSChromeSkin_SlotClass`,
`ToriRSChromeSkin_SlotWrap`) and by the tool.

- **GLYPH** (37): the tool icons, CheckOn/Off, CheckBoxOn/Off, RadioOn/Off,
  ScrollUp/Down, TreeClosed/Open, Close/Popout/Dock and their Over states,
  Run, Stop, Save, Build, WarningGlyph, ErrorGlyph, SplitterGrip, SliderKnob,
  SearchGlass.
- **TEXTURED** (21): PanelBody, DropdownBody, the eight Frame pieces,
  ButtonLeft/Mid/Right, ScrollTrack, ScrollGripTop/Mid/Bottom, SliderTrack,
  SliderTrackLeft/Right, PluginIcon.

WRAP names the axes the drawer tiles a slot along (PanelBody and DropdownBody
both, the frame rails, ButtonMid, SliderTrack along X or Y), so the scaler
reads across the seam instead of clamping it.

## The recipe

The generated header `src/engine/torirs_chrome_skin_tiers.h` carries it in
full. In short:

- **GLYPH, the mask route.** Alpha >= 128 is the 1-bit mask. The ink inside it
  is split into two tones by k-means in RGB (seeded with the most frequent
  colour, then the colour farthest from it, so a red cross on a dark disk
  stays a red cross); a slot with no transparent plate gets three tones, the
  plate being one. Each tone is the rounded mean of its pixels. The label
  image is scaled by scale2x (2x) or scale3x (3x), which copies labels and
  never drops a 1-pixel feature, and painted back. A GLYPH tier holds at most
  three colours: the plate and two inks. Every GLYPH tier is DERIVED: an
  algorithmic scale2x / scale3x of a two-ink quantisation of the 1x art.
  Nothing is hand-drawn and no outline is baked, so this is not yet the
  design's "1-bit mask plus an outline, 2x and 3x drawn as pixel art, not
  resampled" -- it is the stand-in for it, with the mask kept exact.
- **TEXTURED, xBR.** libavfilter vf_xbr's 2xBR / 3xBR rules (the 21-pixel
  neighbourhood, YUV distance, four rotated corner filters per pixel),
  extended to alpha: the distance adds |dA| and blends run in premultiplied
  alpha. Integer arithmetic throughout.
- Tier 1 is the bake itself. A rerun is byte-identical.

Review the result as crops: `make -C src chrome-skin` writes
`src/<objdir>/tmp/chrome_skin/tiers.png` (each slot at 1x nearest x3, 2x x1.5,
and 3x, side by side on the panel brown).

What the mask route costs, seen in that sheet: a glyph with more than two
inks loses the rest, and the quantisation keeps no outline. The arrows, the
crosses, Run, Error, Warning, Build, the tree and scroll arrows and the Close /
Popout / Dock buttons keep their shape. These flatten to two tones and change
the look: ToolPlace (logs), ToolDelete (red arrow over a grey bin),
ToolPaintUnderlay (grass), ToolSmooth, SliderKnob (the blue glass ball),
SaveButton, and the check and radio marks -- CheckOn / CheckOff,
CheckBoxOn / CheckBoxOff and RadioOn / RadioOff lose their dark ring (CheckOn
is 52 opaque colours at 1x and 2 in skin@2x). They are the skin's hand-drawing
queue, or candidates to reclassify as TEXTURED; either is a one-row change and
a rerun. Until then the web host keeps the tiers off (below).

## Reaching a tier

C: `ToriRSChromeSkin_SpriteTier(slot, tier)` (`src/ui/torirs_chrome_skin.h`),
tier 1..`TORIRS_CHROME_SKIN_TIER_MAX`. Tier 1 is the very sprite
`ToriRSChromeSkin_ForSlot` returns, so 1x stays pixel-identical. A slot the
tiers do not cover (spritebake gained it after the last tier bake, or its 1x
size changed) falls back to the 1x image. `ToriRSChromeSkin_ClassOf(slot)`
says GLYPH or TEXTURED. The arrays are `static const`; nothing allocates.

Web: a host that staged `skin@2x/` and `skin@3x/` beside `skin/` says
`skinTiers: 3` in its theme. The runtime then draws a packaged
`skin/<Slot>.png` from the smallest tier whose pixels cover the device pixels
it lands on -- its CSS size times `devicePixelRatio` times the chrome's own
zoom (TORIRS_CHROME_CMD_SCALE) -- the nearest tier at or above the scale, sizes
it back to the authored CSS size, and repaints when the zoom changes. The
stylesheet's `image-set()` rules, scoped under `.tpc-skin-tiers`, do the same
for the art it names, on `devicePixelRatio` alone (a browser does not count
CSS `zoom` in image-set). A host without tiers is never asked for one.
`src/web/torirs_chrome.js` says `skinTiers: 1` for now, though `make web`
still stages both: the derived GLYPH tiers above would change the marks' look
at an integer 2x, so the page draws the 1x art, pixelated, as it always did.

The page pins `image-rendering: pixelated` only at an integer device scale
(`devicePixelRatio` times the zoom). At a fractional one the runtime sets
`.tpc-dpr-fractional` on the root and the stylesheet lets the browser
resample instead (nearest-neighbour at 1.5 drops and doubles whole columns).

The DOM zoom itself is the chrome scale over the page's own density
(`ToriRSChrome_SetPageScale`): a HighDPI page's CSS pixel is already
`devicePixelRatio` device pixels, so at Auto the page is unzoomed on any
display, and View > Chrome scale 200 % on a 1x page zooms it 2x.

## Fonts: the hand-tuned tier hook

The 2x and 3x font tiers are derived: the 1x masks multiplied. Where doubling
reads badly (the 1-pixel counters of p12's a, e, s), the fix is hand-tuned
pixel art, and the hook for it is in place; no glyph is hand-drawn yet.

- The metrics header (`src/ui/uitree_debug_font_metrics.h`) carries
  `ToriRSChromeFont_<Face><N>x_TUNED` per face per tier, 0 everywhere today.
  fontbake writes it from the recipe's `--tuned <Symbol>` arguments, so a
  re-bake keeps it.
- `ToriRSChromeFont_Tier(face, tier)` (`src/engine/torirs_chrome_font_tier.h`)
  is the one accessor the draw path reads (`uitree_scene_bridge.c` picks its
  fonts through it). For a tuned tier it returns
  `ToriRSChromeFont_<Face><N>xTuned()`; otherwise the derived tier.
  `ToriRSChromeFont_TierTuned(face, tier)` answers the flag.

To tune a tier, say Body 2x:

1. Write `src/engine/torirs_chrome_font_tuned.c` defining
   `struct ToriDraw_Font* ToriRSChromeFont_Body2xTuned(void)`: a copy of
   Body2x's struct with new glyph masks, and the SAME offsets, advances,
   draw widths, ascent and line box (the layout measures with the derived
   tables).
2. Add `--tuned Body2x` to the fontbake recipe in the header of
   `torirs_debug_font_baked.c` and set `ToriRSChromeFont_Body2x_TUNED 1` (a
   re-bake writes both).
3. Add the file beside `engine/torirs_debug_font_baked.c` in the makefile
   lists.
4. `make -C src chrome-skin`: `tools/chrome_font_tiers.py` checks a tuned
   tier still has the derived metrics, and every other tier is still exactly
   its derivation.

## Reproducible, not a comment

`make -C src chrome-skin` needs no cache. It runs both tools with `--check`
(failing when the committed tiers, PNGs or the queue below differ from what
the recipe bakes, or when a derived font tier is not the 1x masks multiplied)
and `test-chrome-skin-tiers` (every slot classified; tiers 1, 2, 3 of the
right size; tier 1 the bake; the tiers baked from this 1x image, by hash; a
GLYPH tier at most three colours with every mask pixel kept; a TEXTURED tier
of opaque art opaque; every font tier the accessor's and agreeing with the
metrics header). To refresh: run `python3 tools/chrome_skin_tiers.py` and
`python3 tools/chrome_font_tiers.py` and commit what they write.

`make -C src chrome-skin-rebake` (needs `cache.osrs241`) re-runs spritebake's
recipe and then rewrites the skin tiers.

## The hand-tuning queue

<!-- chrome_font_tiers.py: begin generated queue -->

Generated by `tools/chrome_font_tiers.py` from the 1x masks; do not edit by
hand. A glyph is listed when its scale2x (2x) or scale3x (3x) form differs
from the mask multiplied, with the number of tier pixels that differ: that
glyph has a diagonal, a corner or a 1-pixel counter that multiplication
turns into a staircase or a blot, so it is where a hand-tuned tier would
change what is read. The scaler's output is a pointer, not a bake.

The head of each queue is the glyphs that also have a 1-pixel counter (a
background pixel with ink on both sides along one axis: the 1-pixel
counters of p12's a, e and s), written `glyph counter-pixels/changed`; the
rest follow as `glyph changed`, most changed first.

- **Small (p11) 2x** (derived): 83 of 94 glyphs differ, 45 with a 1-pixel counter
  - counters first: `@` 10/20, `m` 9/2, `$` 6/12, `X` 6/8, `#` 6/8, `N` 6/6, `&` 5/16, `Q` 5/6, `a` 5/4, `%` 4/24, `x` 4/8, `e` 4/4, `s` 4/4, `w` 4/2, `V` 3/6, `j` 3/2, `*` 3/2, `8` 2/12, `K` 2/10, `R` 2/10, `W` 2/6, `k` 2/6, `v` 2/6, `^` 2/6, `<` 2/6, `>` 2/6, `M` 2/4, `z` 2/4, `4` 2/4, `6` 1/14, `B` 1/12, `2` 1/12, `A` 1/10, `Z` 1/10, `3` 1/10, `f` 1/8, `?` 1/8, `Y` 1/6, `7` 1/6, `{` 1/6, `}` 1/6, `U` 1/4, `r` 1/2, `'` 1/2, `,` 1/2
  - then: `G` 14, `S` 14, `C` 12, `g` 12, `D` 10, `P` 10, `5` 10, `9` 10, `£` 10, `O` 8, `b` 8, `d` 8, `o` 8, `p` 8, `q` 8, `y` 8, `0` 8, `E` 6, `J` 6, `~` 6, `F` 4, `H` 4, `c` 4, `h` 4, `n` 4, `t` 4, `u` 4, `1` 4, `(` 4, `)` 4, `+` 4, `[` 4, `]` 4, `/` 4, `\` 4, `L` 2, `T` 2, `;` 2
- **Small (p11) 3x** (derived): 83 of 94 glyphs differ, 45 with a 1-pixel counter
  - counters first: `@` 10/34, `m` 9/2, `#` 6/22, `$` 6/14, `X` 6/12, `N` 6/9, `&` 5/28, `Q` 5/12, `a` 5/6, `%` 4/46, `x` 4/8, `e` 4/4, `s` 4/4, `w` 4/2, `V` 3/12, `*` 3/4, `j` 3/2, `8` 2/28, `R` 2/21, `K` 2/14, `k` 2/10, `v` 2/10, `4` 2/10, `W` 2/8, `^` 2/8, `<` 2/8, `>` 2/8, `M` 2/6, `z` 2/6, `B` 1/32, `6` 1/25, `A` 1/20, `3` 1/20, `2` 1/18, `Z` 1/17, `f` 1/13, `7` 1/13, `?` 1/13, `Y` 1/10, `{` 1/10, `}` 1/10, `U` 1/8, `'` 1/4, `,` 1/4, `r` 1/3
  - then: `g` 25, `S` 24, `5` 23, `D` 22, `G` 22, `P` 22, `b` 20, `d` 20, `9` 20, `£` 20, `C` 18, `E` 18, `p` 18, `q` 18, `y` 17, `O` 16, `o` 16, `0` 16, `J` 13, `F` 12, `H` 12, `+` 12, `[` 12, `]` 12, `h` 10, `n` 10, `~` 10, `c` 8, `u` 8, `/` 8, `\` 8, `t` 7, `1` 7, `L` 6, `T` 6, `(` 6, `)` 6, `;` 3
- **Body (p12) 2x** (derived): 84 of 94 glyphs differ, 45 with a 1-pixel counter
  - counters first: `@` 12/28, `0` 8/14, `$` 6/16, `X` 6/8, `w` 6/2, `Q` 5/14, `N` 5/8, `x` 5/8, `8` 4/24, `#` 4/16, `a` 4/8, `%` 3/26, `K` 3/12, `e` 3/8, `M` 3/6, `V` 3/6, `k` 3/6, `s` 3/6, `*` 3/6, `W` 2/6, `v` 2/6, `z` 2/6, `&` 1/34, `6` 1/20, `S` 1/18, `2` 1/16, `R` 1/14, `B` 1/12, `?` 1/12, `Z` 1/10, `3` 1/10, `{` 1/10, `}` 1/10, `<` 1/10, `>` 1/10, `7` 1/8, `U` 1/6, `Y` 1/6, `m` 1/6, `h` 1/4, `j` 1/4, `n` 1/4, `1` 1/4, `;` 1/4, `r` 1/2
  - then: `O` 16, `G` 14, `A` 12, `C` 12, `P` 12, `g` 12, `q` 12, `5` 12, `£` 12, `D` 10, `b` 10, `d` 10, `f` 10, `y` 10, `9` 10, `~` 10, `o` 8, `p` 8, `^` 8, `(` 8, `)` 8, `E` 6, `J` 6, `4` 6, `/` 6, `\` 6, `F` 4, `H` 4, `I` 4, `c` 4, `t` 4, `u` 4, `+` 4, `[` 4, `]` 4, `'` 4, `,` 4, `L` 2, `T` 2
- **Body (p12) 3x** (derived): 84 of 94 glyphs differ, 45 with a 1-pixel counter
  - counters first: `@` 12/43, `0` 8/20, `$` 6/23, `X` 6/16, `w` 6/2, `Q` 5/20, `N` 5/13, `x` 5/10, `#` 4/40, `8` 4/36, `a` 4/18, `%` 3/42, `e` 3/16, `K` 3/12, `V` 3/12, `s` 3/10, `*` 3/10, `M` 3/8, `k` 3/6, `v` 2/12, `W` 2/8, `z` 2/8, `&` 1/68, `B` 1/32, `6` 1/31, `R` 1/29, `S` 1/24, `2` 1/22, `Z` 1/21, `3` 1/20, `7` 1/17, `?` 1/17, `{` 1/16, `}` 1/16, `m` 1/14, `<` 1/14, `>` 1/14, `U` 1/10, `Y` 1/10, `h` 1/8, `j` 1/8, `n` 1/8, `1` 1/8, `;` 1/6, `r` 1/3
  - then: `£` 27, `g` 25, `5` 25, `A` 24, `O` 24, `P` 24, `G` 23, `D` 22, `b` 22, `d` 22, `q` 22, `9` 20, `y` 19, `C` 18, `E` 18, `f` 18, `p` 18, `4` 18, `o` 16, `J` 14, `^` 14, `~` 14, `F` 12, `H` 12, `+` 12, `[` 12, `]` 12, `/` 12, `\` 12, `t` 10, `(` 10, `)` 10, `I` 8, `c` 8, `u` 8, `'` 8, `,` 8, `L` 6, `T` 6
- **Menu (b12) 2x** (derived): 88 of 94 glyphs differ, 59 with a 1-pixel counter
  - counters first: `@` 16/44, `%` 11/38, `m` 10/12, `8` 8/40, `W` 8/16, `$` 6/32, `0` 6/28, `Q` 5/24, `N` 5/16, `&` 4/46, `#` 4/32, `j` 4/10, `"` 4/8, `6` 3/34, `S` 3/32, `K` 3/26, `e` 3/16, `a` 3/14, `k` 3/14, `^` 3/14, `s` 3/12, `w` 3/12, `4` 3/10, `O` 2/28, `?` 2/26, `X` 2/24, `R` 2/22, `C` 2/20, `q` 2/20, `P` 2/18, `M` 2/16, `x` 2/16, `z` 2/12, `*` 2/12, `J` 2/10, `;` 2/10, `!` 2/8, `i` 2/4, `2` 1/32, `G` 1/28, `A` 1/22, `y` 1/20, `3` 1/20, `{` 1/20, `}` 1/20, `<` 1/20, `>` 1/20, `B` 1/18, `Z` 1/18, `7` 1/18, `~` 1/18, `b` 1/16, `d` 1/16, `D` 1/14, `U` 1/14, `f` 1/14, `h` 1/12, `n` 1/10, `'` 1/4
  - then: `g` 22, `5` 20, `9` 20, `£` 18, `V` 16, `Y` 16, `o` 16, `p` 16, `v` 16, `(` 16, `)` 16, `/` 16, `\` 16, `H` 12, `u` 12, `c` 8, `t` 8, `1` 8, `+` 8, `E` 6, `F` 6, `r` 6, `I` 4, `L` 4, `T` 4, `l` 4, `[` 4, `]` 4, `,` 4
- **Menu (b12) 3x** (derived): 88 of 94 glyphs differ, 59 with a 1-pixel counter
  - counters first: `@` 16/70, `%` 11/54, `m` 10/28, `8` 8/52, `W` 8/32, `$` 6/50, `0` 6/42, `Q` 5/36, `N` 5/34, `#` 4/76, `&` 4/75, `"` 4/24, `j` 4/22, `6` 3/51, `S` 3/42, `K` 3/36, `e` 3/32, `a` 3/30, `4` 3/30, `k` 3/24, `^` 3/24, `s` 3/22, `w` 3/22, `X` 2/60, `R` 2/44, `?` 2/41, `O` 2/40, `M` 2/38, `q` 2/34, `P` 2/32, `C` 2/30, `x` 2/28, `!` 2/24, `*` 2/24, `J` 2/22, `;` 2/17, `z` 2/15, `i` 2/12, `G` 1/47, `A` 1/44, `B` 1/44, `2` 1/44, `y` 1/41, `3` 1/40, `7` 1/39, `Z` 1/35, `b` 1/33, `d` 1/33, `{` 1/32, `}` 1/32, `<` 1/30, `>` 1/30, `h` 1/29, `U` 1/28, `D` 1/27, `~` 1/26, `f` 1/25, `n` 1/23, `'` 1/9
  - then: `g` 45, `V` 40, `v` 40, `9` 40, `5` 39, `£` 37, `H` 36, `Y` 36, `p` 36, `/` 36, `\` 36, `o` 32, `u` 28, `+` 24, `(` 22, `)` 22, `t` 20, `E` 18, `F` 18, `1` 17, `c` 16, `r` 13, `L` 12, `T` 12, `l` 12, `[` 12, `]` 12, `,` 12, `I` 8

<!-- chrome_font_tiers.py: end generated queue -->
