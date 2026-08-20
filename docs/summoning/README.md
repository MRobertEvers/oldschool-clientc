# Summoning rendering failure and fix

This document records why the ported Summoning familiars initially rendered as
only a beak, claw, or other small untextured fragment, why an early interface
fix did not repair the familiar in the 3D world, and what must remain intact for
the models to render correctly.

For the separate investigation of the missing pouch/call visual, including
period wiki captures and the exact revision-530 graphic IDs, see
[`SPAWN_ANIMATION_RESEARCH.md`](SPAWN_ANIMATION_RESEARCH.md).

## Symptoms

The imported familiar models were valid. Independent model review renders
showed complete bodies, and the animation frames could drive the imported
vertex groups. In the live client, however:

- the Spirit terrorbird often rendered only its beak or another small fragment;
- the familiar interface could show a partial body while the summoned NPC was
  still completely absent from the world;
- animation ticks advanced even when the body was invisible; and
- repacking the cache alone did not repair the problem.

The visible fragment was the important clue. Most of each familiar body uses a
texture, while small extremities can use ordinary untextured HSL faces. The
untextured faces followed the Gouraud/flat-colour path and survived. The
textured faces followed a different path and were being discarded or mapped
incorrectly.

## Failure chain

There were two independent texture failures and one widget projection issue.

### 1. Interface models did not request their textures

World models report their referenced texture IDs to the asynchronous texture
pump after model transforms and SD-material filtering. Models loaded through
`UITreeSceneBridge_EnsureModel` did not do that. The interface model therefore
reached the scene, but textures such as the terrorbird's mapped materials `5`
and `27` never became resident in the scene texture map.

The rasterizer intentionally skips a textured face while its texture is absent.
It cannot safely fall back to the face's colour fields because a textured face
stores lighting values there rather than a normal HSL colour. This behavior is
why the result was a clean-looking beak instead of an obviously corrupted full
model.

The bridge now calls `ToriDraw_ModelNoteTextureWants` after
`ToriDraw_ModelDropNonSdTextures`. That ordering matters: HD-only materials are
removed first, and only the surviving SD texture IDs are requested.

### 2. Complex texture render types were lost during model conversion

Fixing texture residency was necessary but not sufficient. Revision-530
familiar models use complex texture mappings. The confirmed examples include:

- Spirit terrorbird faces using texture render type `2`; and
- Dreadfowl faces using texture render types `1` and `2`.

The cache decoder retained `RSCache_Model.texture_render_types`, but the next
two representations did not have a corresponding field:

```text
RSCache_Model
    -> ToriRS_Model
    -> ToriDraw_Model
    -> raster context
```

Consequently, every textured face was treated as render type `0`, the ordinary
triangle-projector mapping. Types `1` and `2` do not have the same mapping
semantics. Sending those faces through the type-0 perspective texture kernel
did not merely select the wrong-looking UVs in this case; it produced no useful
pixels for most of the familiar body.

The fix preserves `texture_render_types` through the entire pipeline:

- cache-to-engine conversion transfers and owns the array;
- model size accounting and destruction include it;
- engine-to-renderer conversion copies it;
- model copy, steal, animation, and merge paths retain it; and
- the raster context receives it alongside the P/M/N texture coordinates.

At raster time, type `0` faces keep the existing perspective texture path.
Only faces whose preserved mapping type is nonzero are routed through the
compatible affine face kernel. This is deliberately face-specific. Globally
forcing affine texturing made the familiar visible during diagnosis, but it
also changed unrelated world rendering and was not an acceptable fix.

`TORIRS_RASTER_TEX_MODE_DEBUG=1` instruments this decision. A corrected
terrorbird run contains entries like:

```text
raster_tex_mode: face=714 coord=25 type=2 affine=1
```

The Dreadfowl capture records both type `1` and type `2` faces taking this
route.

### 3. The widget path had separate projection state

Interface models are projected directly and share a scene object with the
world renderer. The widget draw inherited the previous world model's
`near_clipped` state even though widget screen depth is model-relative, as in
the reference client's `Model.objRender`. That could clip ordinary animated
widget faces against a world-camera near plane.

The widget path now clears that stale near-clip state before projecting. It
also explicitly selects affine texturing for its already-projected interface
triangles. The familiar component uses a fitted camera distance and offset so
the complete animated body stays below the title and above the points/buttons.

## Why the first apparent fix was incomplete

During diagnosis, enabling affine texturing only for the interface widget made
the familiar body appear in the Equipment-side panel. That screenshot looked
better, but the summoned NPC in the 3D world was still missing. The workaround
had bypassed the lost render-type metadata only in one renderer entry point.

The decisive test was to render the same active familiar twice in one frame:

1. as the summoned NPC in the world; and
2. as the model component in the familiar tab.

When the complex mapping type was preserved and handled per face, both copies
appeared simultaneously. This is now the required acceptance test; an
interface-only screenshot is not proof that familiar rendering works.

## CS2 and interface placement

The rendering bug was separate from the Equipment-tab placement bug. The
Summoning content remains an overlay and does not edit the original Equipment
or gameframe content.

The overlay's CS2 layout initially ran during the login burst, before
`wornitems` had been mounted. Its component targets therefore did not yet
exist. The overlay now reapplies its layout from an overlay-owned
`[if_open,wornitems]` trigger. Opening the familiar view replaces the Equipment
contents inside the same side-panel mount, changes the Equipment redstone icon
to the Summoning icon, and Back restores the normal Equipment components and
redstone graphic.

This mount-timing fix must not be replaced with a root overlay. The familiar
interface belongs inside `wornitems:universe`.

## Build and run

From the repository root, build and launch the entire Summoning lane with one
command:

```sh
./run-live.sh manifests/manifest_osrs239_summoning.ini
```

The launcher reads the cache, compiled-script directory, credentials, renderer,
and embedded transport from that manifest. It rebuilds the Summoning cache and
Summoning-only script pack before compiling and starting the embedded client.

If those artifacts are already built, the equivalent direct client invocation
is:

```sh
./src/torirs --manifest manifests/manifest_osrs239_summoning.ini
```

`manifests/manifest_osrs239_summoning.ini` selects `cache.osrs239.summoning`, the
`build_summoning` server scripts, embedded transport, development credentials,
Soft3D, level 99 Summoning, the persisted Summoning unlock, all three playable
familiar pouches, the Clockwork cat, and Spirit wolf infusion supplies.
`TORIRSSERVER_CACHE` and `TORIRSSERVER_SCRIPTS` are no longer required. They remain
higher-priority overrides for diagnostics. The original `manifests/manifest_osrs239.ini`,
cache, and content trees are not modified by this selection.

Nothing is summoned automatically. Use a pouch's real Summon option, then open
Equipment → Summoning. Use the Clockwork cat's real item action to test the pet
path, or take the supplied charm, blank pouch, wolf bones, and shards to the
Summoning obelisk to test infusion.

Ordinary pouch summoning and **Call familiar** now play the revision-530
familiar-attached arrival animation and sound. Dreadfowl uses the small effect;
Spirit wolf and Spirit terrorbird use the 200%-scaled large effect. The player
correctly remains idle. The source evidence, target-ID mapping, procedural
material conversion, and verification details are in
[`SPAWN_ANIMATION_RESEARCH.md`](SPAWN_ANIMATION_RESEARCH.md).

## Cache repacking

The Summoning cache must be repacked when overlay interfaces, CS2, models, or
texture mappings change:

```sh
make -C src torirsserver-cache-summoning torirsserver-scripts-summoning
make -C src torirs EMBED_SERVER=1
```

Repacking alone could not fix this incident because the packed cache already
contained the models, textures, and complex render-type bytes. The runtime was
dropping part of that decoded metadata and the interface loader was not
requesting its textures.

## Verification evidence

Fresh client captures and their runtime logs are in
[`screenshots/`](screenshots/README.md):

- `spirit_wolf_familiar_tab.png` — full Spirit wolf in the world and tab;
- `terrorbird_world_and_familiar_tab.png` — full textured Spirit terrorbird in
  both rendering paths;
- `dreadfowl_world_and_familiar_tab.png` — full Dreadfowl in both paths; and
- `equipment_tab_restored.png` — normal Equipment contents after Back, with the
  familiar entry button retained in the top-right; and
- `familiar_spawn_animation.png` — full Spirit wolf with the translucent
  familiar-attached arrival animation visible around its feet.

The associated logs prove that the server summoned the expected familiar,
complex texture faces were routed with their preserved type, animation frames
advanced (`23000` for Dreadfowl and `23032` for Spirit terrorbird), and no CS2
or script abort occurred.

The renderer and interface regression checks used for the final verification
are recorded in `screenshots/verification_tests.log` and cover model lighting,
animation stepping, scene profiles, and interface setters.

## Regression checklist

If a future imported model again renders only extremities, check these in order:

1. Confirm the model is complete with a material/by-label review render.
2. Enable `TORIRS_TEX_DEBUG=1` and verify all referenced texture IDs are
   requested and published.
3. Enable `TORIRS_RASTER_TEX_DEBUG=1` and check whether resident textures are
   still being skipped.
4. Enable `TORIRS_RASTER_TEX_MODE_DEBUG=1` and confirm mapping types `1`-`3`
   survive to the rasterizer and select the complex-face route.
5. Capture the same familiar in the world and in the interface. Do not accept
   success in only one path.
6. Verify changing `anim_tick` frame numbers so a bind-pose screenshot cannot
   hide an animation regression.
7. Open and close the familiar tab and confirm the Equipment redstone graphic
   and components are restored.

The central invariant is that a textured model is more than geometry plus a
texture ID. Its per-texture-triangle mapping type is rendering data and must
survive every cache, engine, copy/merge, animation, and raster boundary.
