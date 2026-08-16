# Why the QBD went grey, and what fixed it

[grey_fix.png](grey_fix.png) — before and after, same camera. **36.1% of pixels.**

## Correction: "she has no textured faces" was wrong

An earlier revision of this file claimed the QBD has no textured faces, from
this line:

```
model: 4816 vertices, 6863 faces, textured 0, alpha 166
```

**That `textured 0` is an artifact of the command that produced it.**
`rs2012_model_view` strips texture ids unless `--textures` or `--lane-textures`
is passed, and `--stats` reports the model *after* the strip. The same model
asked properly:

```
$ rs2012_model_view --model rs2012_model_70260.ob3 --textures --stats
model: 4816 vertices, 6863 faces, textured 6533, alpha 166
```

**6,533 of 6,863 faces are textured**, and their imported materials are
greyscale masks drawn as if they were surfaces — which is the whole of the white
and black appearance. Everything the earlier revision concluded from the
opposite is void. See
[`../qbd_port_compare/README.md`](../qbd_port_compare/README.md) for the
evidence, and for why the material-kernel route that was built to address this
has since been removed.

Ruled out along the way, each by a render. The two bake flags named below no
longer exist — they were removed with the imported-material kernels they fed:

| suspected cause | result |
|---|---|
| the `--alpha-textures` bake referencing masks | 2.3% — not it |
| RS727 face priorities sorting her inside-out | `--ignore-priorities` still 36% off — not it |
| re-authoring priorities against today's lane | 0.1% — not it |

## What it was

The npc does not draw the plain lane models:

```
[rs2012_qbd_default]  model1=110000  model2=110001
        ↓  pack/7_models.pack
110000 = rs2012_model_70260_authored
110001 = rs2012_model_69766_authored
```

Those `_authored` copies had been regenerated (timestamp 22:47) from the lane
while it was in the `--detail-textures` state — the bake configuration that also
turned the whole arena into green and white striping and was backed out. Their
**face colours** came out wrong, and since the model is nothing but face
colours, wrong colours is the entire visible result.

`docs/rs2012_qbd_priorities/run/` still held models from before that (21:37),
which render correctly. Installing those as the npc's `model1`/`model2` is the
fix, and is what the "after" panel shows: dark red-brown head, cream horns,
purple frill — the reference palette.

```sh
L=OSRS-Content/osrs239-content/models/ported/rs2012_qbd_td
for m in 70260 69766 70267 70268 70761 69765; do
  cp docs/rs2012_qbd_priorities/run/rs2012_model_$m.ob3 $L/rs2012_model_${m}_authored.ob3
done
```

The previous copies are backed up in `build/qbd_authored_backup/`.

## Two things still outstanding

**The plain lane models still render white and black in the client** — and that
one is now understood. It is not their face colours, which are correct: strip
the textures and the lane model draws the right dragon. It is that 6,533 of its
faces carry imported greyscale materials and the lane's texture records carry no
kernel flags, so every one of them draws through the stock opaque kernel with
the mask as the surface. Diagnosis, pictures and the two raster bugs it turned
up are in [`../qbd_port_compare/README.md`](../qbd_port_compare/README.md).

**`rs2012_material_bake` skips `*_authored` models.** I added that skip
(`parse_model_pack`) so the bake would tolerate authored rows in the pack. It
means the bake re-materials the copies nobody draws and never touches the ones
the npc uses. Harmless for the QBD, since she has no textured faces — but wrong
for any authored model that does.
