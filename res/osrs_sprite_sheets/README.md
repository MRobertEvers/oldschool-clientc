# OSRS-Content icon contact sheets

Contact sheets of every icon-sized sprite in
`OSRS-Content/osrs239-content/sprites/` — 5714 sprites whose first frame
(`0.bmp`) is between 10x10 and 64x64. Each cell shows the sprite over black
(black is the transparent colour in these BMPs) with its directory name below.

- 20 columns x 13 rows per sheet, 260 sprites per sheet, 22 sheets.
- `index.tsv` — `sheet, row, col, sprite, width, height` for every cell.
- Only frame 0 of each sprite directory is shown; multi-frame packs have one
  directory per frame (`foo_0`, `foo_1`, ...).

To reference a sprite from CS2, drop the trailing frame number and use it as an
index: directory `options_icons_21` is `cc_setgraphic("options_icons,21")`.

Camera icon: `options_icons_21` (sheet17, row 0, col 11) — grey movie camera
with a rotate arrow, 28x32, sprite pack 1073.
