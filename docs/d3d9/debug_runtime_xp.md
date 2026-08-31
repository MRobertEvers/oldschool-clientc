# The D3D9 debug runtime on Windows XP

`lib/d3d9-debug-runtime-xp.zip` holds the two files, and its `README.txt`
carries the full provenance. This page is why you would want them and what to
watch out for.

## What it is for

One query, and it is the only way to get it: **`D3DQUERYTYPE_RESOURCEMANAGER`**.
The retail runtime answers `D3DERR_NOTAVAILABLE`; the debug runtime returns
`D3DRESOURCESTATS` per resource type —

| field | what it settles |
|---|---|
| `bThrashing` | is the MANAGED texture set being evicted and re-uploaded |
| `NumEvicts` / `NumVidCreates` | how often, over the session |
| `WorkingSetBytes` / `TotalBytes` | how much of it is actually resident |

`TORIRS_D3D9_VRAM=<frame>` prints it, alongside the client's own accounting and
`GetAvailableTextureMem`.

It buys **nothing else**. It is a runtime, not a driver: on the GeForce4 MX 440
the `TIMESTAMP`, `TIMESTAMPFREQ`, `TIMESTAMPDISJOINT` and `OCCLUSION` queries
are unsupported by the hardware and stay unsupported with it loaded. There is
no GPU timing to be had on that card by this route.

## Installing it

Put **both** DLLs beside `torirs.exe` and set

    HKLM\SOFTWARE\Microsoft\Direct3D\LoadDebugRuntime = 1   (REG_DWORD)

Not in `system32`. The application directory leads the DLL search order, so
`d3d9.dll` finds `d3d9d.dll` there and `d3d9d.dll` finds `d3dx9d_33.dll` beside
it. In `system32` both fail to load with `err 999`, and `d3d9.dll` swallows
that and falls back to retail — which reads as "this card cannot do D3D9"
rather than as a missing file. Uninstalling is deleting two files.

Confirm it actually took, because the fallback is silent:

    d3d modules in client: d3d9.dll, d3d8thk.dll, d3d9d.dll, d3dx9d_33.dll

If `d3d9d.dll` is absent from that list, the retail runtime is what ran.

## Two traps

**The SDK hands you the wrong DLL first.** The cabinet holds three x86 builds
of `d3d9d.dll` under one name, and `expand.exe` yields the first — which
imports `dwmapi.dll`, the Vista Desktop Window Manager, absent on XP. Tell the
three apart by their PE import tables, never by size or date. The README in the
zip has the table and the extraction recipe.

**Never benchmark with it loaded.** The debug runtime validates aggressively and
is slower. Numbers taken with those two files present are not comparable to
retail ones — delete or rename them before timing anything.

## What it measured

On box B (GeForce4 MX 440, 64 MiB — see [../xp_boxes.md](../xp_boxes.md)),
`rs289lc` in-world:

    type            thrash   evicts vidcreat   workingset        total
    texture             no        0        0     32.31 MB     32.38 MB
    vertexbuffer        no        0        0      0.00 MB      0.00 MB
    indexbuffer         no        0        0      0.00 MB      0.00 MB

No thrashing, no evictions, the 32 MB atlas fully resident. The client's total
GPU footprint is ~73 MB against a driver-offered pool of 113 MB — the card's
64 MiB of local VRAM plus the AGP aperture — so it is comfortably inside the
budget the driver actually enforces, and the 64 MiB figure was never the
binding constraint.

The `vertexbuffer` and `indexbuffer` rows read zero because those live in
`D3DPOOL_DEFAULT`: the resource manager only tracks MANAGED resources. They are
placed once and read across the bus, which at this geometry rate is a few
percent of AGP 8X.
