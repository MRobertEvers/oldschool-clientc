# QBD Encounter Research — sources and findings

Distilled from a four-agent research pass (Aug 2026): 2012 wiki revisions and
fansite archives, OSRS moving-wall mechanics, an engine survey of this
codebase's zone/projectile plumbing, and a byte-level asset audit of the wall
graphics. This file is the provenance for every timing and mechanic number in
`ENCOUNTER.md`.

## 1. Sources

| Label | Source | Era |
|---|---|---|
| S1/S3 | Fandom *Queen Black Dragon* revs 5861300 (29 Jun 2012) / 6053174 (6 Aug 2012 — combat text identical to S1) | era |
| S2 | Fandom *QBD/Strategies* rev 5818963 (24 Jun 2012) | era |
| S4 | Fandom *QBD/Strategies* rev 6579443 (18 Nov 2012, last pre-EoC; includes 7-Aug patch facts) | era |
| S5 | Fandom *Tortured soul (QBD)* rev 6079892 (12 Aug 2012) | era |
| S6 | Tip.It QBD guide (Wayback, Dec 2012 snapshot, pre-EoC scale) | era |
| S7 | RuneHQ QBD Special Report (Wayback, Aug 2012) | era |
| S8 | open727 (CSS-Lletya) QBD combat scripts — RSPS reconstruction, machine-readable tick values | reconstruction |
| S9 | Modern runescape.wiki — post-EoC ×10 scale; used only where marked | modern |
| OSRS | oldschool.runescape.wiki: Galvek/Tsunami, Judge of Yama Fire Wave, Great Olm, Vorkath; OpenOSRS/RuneLite id tables | modern (mechanics reference) |

Fandom page fetches are Cloudflare-blocked; the MediaWiki API
(`api.php?action=query&prop=revisions&revids=…`) serves archived revision
wikitext. The 7 Aug 2012 rebalance is the "Some Like it Cold" update (added
the 10-s dragonfire cooldown, ~30-s siphon cooldown, soul-summon separation).

## 2. QBD fight findings adopted into the guide

- Stats (S1/S3 infobox): level 2100; 75,000 LP as 4×18,750; **attack speed
  4 ticks**; max hits 970 extreme fire / 525 ranged / 475 melee; player hits
  capped at 1,000. (open727 halves her LP — rejected.)
- Wake: ~30 s of sleep before the fight (S2/S4); open727: wake anim tick 5,
  attackable tick 30, **first attack tick 40**.
- Selection is **random-with-cooldowns**, explicitly not a fixed rotation
  (S6: "some of these attacks have cooldowns to prevent the QBD from using
  multiple powerful attacks at once"). open727 recovery ranges: melee/range/
  fire/summon 4–15, armour 4–10, time stop 5–10, extreme 8–15, wall
  8+2×waves; cooldowns: wall (7w+5)–60, summon 41–100, armour 41–100, siphon
  50–90, time stop 40–90; post-restoration first attack +20.
- Fire wall (S1/S3/S4 + S8): warning "takes a huge breath" in orange;
  1/2/3/3 waves; **1 tile/tick** southward; **7 ticks between waves** (S8);
  full-width with **one gap**; gap squares **5, 9 (one west of the centre
  artefact), 15** of the 19-square platform → columns 28/32/38;
  **"She will cycle between these waves so a player may predict where the
  next gap will be"** — deterministic gap cycling (order itself
  undocumented; guide adopts type 1→2→3 = squares 15→9→5). Stationary = two
  hits ~200 LP each protected; running through = one or zero; unprotected
  contact "is the same as being hit by her Dragonfire attack" (S6) → the
  dragonfire ladder applies. open727's east gap x=37 (square 14) contradicts
  the era wiki's square 15 (x=38); the wiki wins.
- Ordinary dragonfire: no warning line; ~1-tick delivery; low-200s LP
  through shield+super antifire; 700–900+ LP naked; **no cooldown at launch,
  10 s after 7 Aug** (S4).
- Souls (S1/S5/S6): level 147, 500 LP, slow (half walk speed); first spawn
  **one square west of the player**; teleport → **exactly 1.2 s (2 ticks)**
  → cast; shadow spawns opposite adjacent tile, homes **1 tile/tick**,
  ~200–260 LP per contact, prayer-proof, one-shots a soul it touches (the
  lure counterplay); after its cast the soul melees weakly. Phase counts
  1/2/4, and **phase 4 opens with 4 at once** (S1/S2/S6). Six voiced lament
  lines (S5).
- Siphon (S5/S6): **continuous channel** — 20 damage per soul and 40 healed
  per soul *per drain*, "continues until all Tortured souls have been
  killed"; a full soul is worth 1,000 LP to her; ~30-s cooldown post-7-Aug.
- Time stop (S1/S5/S6/S8): caster teleports to the east/west edge; four
  overhead lines; **~15-tick kill window** (wiki "10 seconds"); on
  completion a **12-tick freeze** (green screen) with damage accumulated
  into **one hit** on release; killing the caster prevents it; cooldown
  40–90 from resolution.
- Extreme fire (S1/S2/S6): phase 4; yellow warning; multi-coloured/blue
  flame aimed at the **platform centre**; **three rounds** (first at
  T0+4, S8) scaled by distance from the epicentre (300+ LP close, ~60 at
  the edges, max 970); dodge to the east/west sides; forges the Royal
  crossbow.
- Armour forms: full era message texts (S6/S8, cyan/green); distinct NPC
  forms; **the two flanking arena crystals swap appearance with her state**
  (S8: 70818/70822 default, 70819/70823 crystal, 70820/70824 hardened);
  ~40-tick duration, 41–100-tick cooldown.
- Worm intermission (S1/S7/S8): she **coughs (16747) and lobs a worm
  projectile (gfx 3141) to a random mid-field tile**; the worm hatches
  there (landing gfx 3142) a few ticks later, aggressive; stop-cough 16748
  on artefact restore. Interval conflict: 1 s (S7) / 3 s (S6) / 6 s (S8) —
  guide adopts 5 ticks (3 s, the era-guide midpoint).
- Melee zone: north of one square above the centre artefact (z ≥ 33); three
  directional bite anims. Ranged sweep is animation-only, usable at melee
  range, ~high-300s LP typical.

Modern-wiki-only claims deliberately NOT adopted: worm cap of 11 + artefact
"leak" damage; ±25% armour-form damage transforms; 6-tick attack rate;
siphon 100/200 values; ×10 damage numbers.

## 3. Moving-wall mechanics survey (OSRS)

- Galvek's phase-3 **Tsunami**: full-width wall, one tile missing, sweeps
  the deck; ~1 tile/tick; 70–120 contact damage; nothing lingers behind the
  front. Implemented as a **non-attackable NPC row** (id 8099) — OSRS gets
  smooth motion from the client's NPC walk interpolation; the gap is a
  missing segment.
- Judge of Yama's **Fire Wave** (NPC 10939): the literal moving fire wall,
  same design, up to 20 damage.
- Great Olm's **flame wall**: the static contrast — projectile 1347 seeds
  two parallel fire lines that cage players (GameObjects, douse a segment to
  escape); Olm's **lightning** is the per-tile-per-tick spotanim exemplar
  (GraphicsObject 1338 strobing down rows).
- Rule of thumb: moves → NPC; flashes once → tile spotanim; persists/
  clickable → loc; flies point-to-point → projectile.
- Server model: track {front row, gap column, span, end row, per-player hit
  latch}; resolve player movement, advance front, then contact-check.

This engine diverges from OSRS's NPC trick deliberately: the authentic 2012
wall is **one huge model per gap pattern**, not per-tile segments, so the
guide specifies a first-class moving zone graphic (`flamewall_map`) instead
of fake NPCs — same server damage model, authentic visual.

## 4. Asset audit (byte-verified against the composed caches)

- Spotanims 3141–3165 map linearly to dest 10000–10024. Walls: 3158/59/60 →
  **10017/10018/10019**, models 69880/69878/69879 → **110099/110100/110101**,
  shared seq 16761 → **22043**. Flame variants 3155–57 → 10014–16 (model
  110098). Breath 3143→10002, tall flame column 3149→10008, extreme
  3152–54 → 10011–13, shadow 3146→10005, teleport 3147→10006, siphon
  3148/3150 → 10007/10009, worm expel/land 3141/3142 → 10000/10001.
- **Wall model geometry** (vertex-measured, source and dest agree): face
  span **9,760 units = 76.25 tiles** in X (−4880..+4880), ~10.1 tiles tall,
  ~9 deep; origin at geometry centre; base at y=0; scale 128/128, no
  rotation (verified in rev-727 source bytes — no resize exists). Gap
  holes (model-local X): 110099 centre −20 tiles (clear −21..−19); 110100
  centre +16 (clear +15..+17); 110101 centre ≈−3.8 (clear −5..−3). Hence
  spawn anchors 48/22/36 for gap columns 28/38/32. **No common anchor
  exists** — anchoring all three at x=33 (the old code) put the visual gaps
  at columns ~13/49/29.
- Seq 22043: 2 frames × 15 cycles = **30 cycles (exactly one tick) per
  loop**, framestep 2 — the client element must loop it explicitly
  (ClientProj wraps unconditionally; the C object-frame path may not).
- Wall-cast seq 16746 → 22007: 150 cycles (3.0 s), sounds at frame 0
  (sample 14984) and frame 23 (14896), both present as idx14 archives.
- **Nothing is missing from `cache.osrs239.rs2012`** (or `-summoning`) for
  the moving wall: all 25 spotanim configs, wall models, seqs, animation
  archives 22000/22005/22010, framemaps, and both samples verified present.
  No extraction from `cache.rs727_preeoc` required.
- Trap noted: the wall models carry 39–41 orphan rigged vertices out to
  ±90–129 tiles, inflating the bounds cylinder (cull/pick radius) — perf
  watch-item, not correctness.

## 5. Engine survey (this codebase)

- Wire delays/durations are **client cycles (20 ms), 30 per tick**; classic
  MAP_PROJANIM is 15 bytes, MAP_ANIM 6; projectile heights ×4 client-side.
- The old wall visual: `projanim_map(…, delay 0, duration 18, peak 46,
  arc 0)` — a 0.36-s arcing projectile racing an ~11.4-s damage front, at a
  shared anchor. Every part of that is wrong (§ENCOUNTER 6.4).
- Projectile arc math is a ClientProj line-port
  (`World_ProjectileSetTarget/Move`, src/world/world.c); spotanims are
  single-shot, one-tile-painter-registered, fixed-height.
- Painter: spotanims register one tile; projectiles ±60 fine units; draw
  box radius defaults to 25 tiles — a 76-tile model anchored at one tile
  pops and mis-sorts; locs solve this by registering their true footprint
  (max 255) with span flags. The flame-wall element registers its clamped
  multi-tile footprint the same way.
- Ground height: `app_world_height` (heightmap-interpolated, LINK_BELOW
  aware) is the sampler movers use per frame; the wall element re-samples
  it every cycle.
- **Constraint adopted: official packets only.** The official client renders
  the wall from MAP_PROJANIM, so the mock sends exactly that; no custom
  zone op. With `peak 0, arc 0, heights 0` the ported ClientProj math
  (World_ProjectileSetTarget/Move) degenerates to a flat ground glide with
  pitch 0 and, for southward travel, yaw 0 (model unrotated) — verified
  against the arc equations. Using the official packet also means the wall
  renders on the rev-239/RuneLite lane via MAP_PROJANIM_V2 for free.
- Seq looping: ToriDraw_AnimationAdvanceObjectFrame implements the official
  DynamicObject rule `frame -= framestep` at sequence end; wall seq 22043
  (2 frames, framestep 2) therefore wraps 2 → 0 and loops for the whole
  flight. No client change needed.
- Draw-circle behaviour: projectiles paint from their anchor position
  (±60 fine units); the anchor columns 22/36/48 stay inside the arena
  square and within the classic tile-anchored draw range, matching how the
  official 2012 client kept the wall on screen in this small arena.
- Test seams: `torirs_server_world.c` capture stanzas that byte-decode the wire,
  and the `::zukstill`-style settled-arena fixture pattern for counting
  zone packets server-side — a QBD wall stanza asserts MAP_PROJANIM
  payloads (spotanim id, duration 570, peak 0) at the pattern anchors.

## 6. Open questions carried forward

- Live wall-to-wall spacing (7 ticks is open727) and the true cycle order of
  the three gap types ("she will cycle" is stated; the order is not).
- Worm-eruption interval: 1 s vs 3 s vs 6 s across sources (guide: 3 s).
- Whether siphon/armour forms existed from 29 May and their exact phase
  gates (Tip.It: siphon P2/armour P3; open727: armour P2/siphon P3; era
  wiki: silent). Guide keeps the port's P3 gate for both.
- Exact special-attack scheduler on live (open727's random 4–15 is a
  reconstruction; the infobox 4-tick speed is the only era number).
- Frame-level wall visuals from 2012 videos (fetch-blocked); the era wiki
  screenshot `QBD firewall.png` and "licking walls of flames" prose are the
  best surviving descriptions.
