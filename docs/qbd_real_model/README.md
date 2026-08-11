# Why the QBD went grey, and what fixed it

[grey_fix.png](grey_fix.png) — before and after, same camera. **36.1% of pixels.**

## It was not the material kernels

The first thing worth stating, because I spent a long time assuming otherwise:

```
model: 4816 vertices, 6863 faces, textured 0, alpha 166
```

`textured 0`. The Queen Black Dragon has **no textured faces at all** — both
`rs2012_model_70260` and `rs2012_model_69766`, in every version of the lane
checked. Her entire appearance is flat and gouraud face colour.

So none of the HD/SD material work ([`../HD_KERNELS.md`](../HD_KERNELS.md)) can
affect how she looks, in either direction. Every "31% changed" figure in that
document is measured on a model that renders the same with the kernels off,
because the faces those kernels touch are on other lane models. That is worth
knowing before any more effort goes into her via the material route.

Ruled out along the way, each by a render:

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

**The plain lane models are still grey.** `rs2012_model_70260.ob3` as the bake
writes it today renders the same grey the `_authored` copies did. So the next
person to re-author priorities from the lane will reintroduce this. Something in
the bake's model rewrite changes face colours between the state that produced
`run/` and today's, and I did not isolate which step — that is the real bug and
it is still open.

**`rs2012_material_bake` skips `*_authored` models.** I added that skip
(`parse_model_pack`) so the bake would tolerate authored rows in the pack. It
means the bake re-materials the copies nobody draws and never touches the ones
the npc uses. Harmless for the QBD, since she has no textured faces — but wrong
for any authored model that does.
