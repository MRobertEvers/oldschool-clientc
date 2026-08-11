# Two ports of the same dragon

The lane has been through two independent pieces of porting work. They fix
different things and neither folder shows the other's result, which makes it
easy to assume one subsumes the other. This harness renders the same geometry,
from the same textures, at the same camera, through each.

| port | what it changes | written up in |
|---|---|---|
| **priorities** | the ORDER faces are painted in (re-authored face render priorities) | [`../rs2012_qbd_priorities/`](../rs2012_qbd_priorities/) |
| **materials** | WHAT each face is filled with (per-texel alpha, modulate by face colour, detail maps, OB_TORI per-face kernel routing) | [`../HD_KERNELS.md`](../HD_KERNELS.md) |

They are orthogonal: one is the sort, one is the fill.

```sh
tools/qbd_port_compare.sh                 # every form both ports have
FORMS=default tools/qbd_port_compare.sh
```

Its own harness on purpose — it reads the priorities `run/` directory and the
lane side by side, writes only under this folder, and never writes the lane,
packs a cache or touches the client.

## The pictures

- [images/default_sheet.png](images/default_sheet.png) — lane, priorities port, materials port
- [images/default_diff.png](images/default_diff.png) — what each changed against the lane, and how far apart the two are

Measured on the QBD head, three yaws:

| | changed vs the lane |
|---|---:|
| priorities port | 36.0% |
| materials port | 30.6% |
| **the two ports against each other** | **36.2%** |

## What the sheet shows, including the awkward part

The lane as shipped is a white and grey metal mess — that is the untinted
greyscale masks being drawn as if they were surfaces.

Both ports fix that, and **the priorities port currently reads closer to the
reference art than the materials port does**: its head is a deeper red-brown,
its horns a cleaner tan, and the materials port is lighter and noisier beside
it.

That is not the sort doing colour work. The likely explanation is that the
priorities models were authored from a *different bake state*, in which more
materials were still being flattened to their face colour — so more of that
model is flat colour, and flat colour is exactly what the reference's broad
areas look like. The materials port keeps those faces textured and modulates
them, which adds the HD program's noise back on top.

If that is right, the interesting question is not which port wins but **how much
texture detail actually belongs on those faces** — the materials route can be
dialled toward flat by tightening which materials classify as masks, and the
right amount is a judgement about the art, not something either harness can
settle. The 36.2% between-ports figure is the size of that disagreement.

## Rebuilding the inputs

The priorities models come from `tools/rs2012_qbd_prio.sh` (writes `run/`). The
materials models are built by this harness itself, into `build/qbd_obtori/`,
from whatever the lane's current texture records say — so a re-bake changes them
and the sheet should be regenerated after one.
