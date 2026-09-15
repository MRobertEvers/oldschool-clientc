# The world camera, and every knob that moves it

The follow camera is one line of arithmetic. Every `[camera]` key is one term
in it, and nothing else moves the follow camera:

```
        pitch * pitch_distance                  <- pitch_distance=, over pitch_flattest=..pitch_steepest=
      +  live zoom                              <- rest=, moved by the wheel inside the band
      x viewport term / 256                     <- viewport_zoom=
      x distance_scale / 100                    <- distance_scale=
      = distance, in fine units, from the player to the eye
```

That expression is `app_world_camera_follow` in `src/app.c`.

## 1. The units, because three different ones are mixed in one line

| Quantity | Unit | Range |
|---|---|---|
| `pitch` | angle, 2048 to a full circle (~0.176 deg each) | `pitch_flattest=`..`pitch_steepest=`, by default 128..383, i.e. 22.5 deg..67.3 deg above horizontal |
| `yaw` | same angle unit | 0..2047 |
| `rest`, `zoom_closest`, `zoom_furthest`, `wheel_step`, the distance itself | **fine world units, 128 per tile** | |
| viewport term, `distance_scale` | 256ths and percent | multipliers |

`pitch * pitch_distance` multiplies an angle by a length-per-angle. That is not
a physical relation; it is Client-TS `camFollow` verbatim (`pitch * 3 + 600`), a
linear "pull the eye back as the camera tips over" term. At the reference's
coefficient of 3 it is **384 fine units at the flattest pitch and 1149 at the
steepest**, and it is the single most important number on this page: at overhead
angles it is nine tenths of the distance, and no key that *adds* to it can
compete with it. It used to be a bare `3` in `app.c`; it is `pitch_distance=`
now, and every profile states it.

"Height" was the reference's word for the distance and it was a misnomer. The
distance is the *slant* distance along the pitch/yaw ray -- `ToriRS_OrbitCameraEye`
rotates a `(0, 0, distance)` offset -- not a vertical Y. Nothing in this section
is called a height any more.

## 2. The six `[camera]` keys

**One key states one number.** A key a section does not spell is left to the
default; it is never inferred from a neighbour. A `@mobile` / `@desktop` tag on
the section header scopes the whole section to that platform
(`TORIRS_REVCONFIG_PLATFORM=mobile` forces the tag on a desktop, which is how
you look at the phone's camera without a phone).

| Key | Unit | Default | What it is |
|---|---|---|---|
| `rest=` | fine units | 600 | Where the eye sits before anyone touches it. The reference's own 600. |
| `zoom_closest=` | fine units | 40% of `rest` | Near end of the band the wheel and the pinch may reach. |
| `zoom_furthest=` | fine units | 360% of `rest` | Far end of the same band. |
| `wheel_step=` | fine units | 60 | How far one wheel notch, or one pinch step, moves the live zoom. |
| `distance_scale=` | percent, 10..400 | 100 | Multiplier on the **whole** distance, pitch term included. |
| `viewport_zoom=` | `yes` / `no` | `yes` | Does this revision have the later client's viewport zoom? |
| `pitch_distance=` | fine units per angle unit, 0..16 | 3 | How hard the eye is pulled back as the camera tips over. |
| `pitch_flattest=` | angle units, 0..511 | 128 | The most level the camera may sit. |
| `pitch_steepest=` | angle units, 0..511 | 383 | The most overhead it may tip. |
| `controls=` | `mmb`, `arrow_keys` | both | Which gestures orbit the camera. Whole list, not an addition. |

A malformed value is reported on stderr and ignored -- the default stands. So
is `zoom=`, the key this section used to have, which is named in the loader
purely so an old profile is told what to write instead.

### `rest=` and the band

The band ends default to percentages of `rest`, resolved *after* the merge, so
a lane whose ui.ini says `rest=600` and whose boot manifest says `rest=900` gets
the default band around 900. A band end that a section **did** state is left
exactly as stated: `zoom_closest=60` on a phone keeps its floor no matter what
a later source does to the rest.

`closest`/`furthest` rather than min/max because the number is a distance: the
smaller end is the *closer* view, and "min zoom" reads as the opposite of what
it does.

### Three levers move the camera closer, and they are not interchangeable

This is the one thing to know before tuning anything. Which lever is worth
anything depends entirely on where the camera is **pointed**, because the pitch
term is 384 at the flattest angle and 1149 at the steepest.

| Lever | What it multiplies | Level (pitch 128) | Overhead (pitch 383) |
|---|---|---|---|
| `zoom_closest=` 240 -> 60 | nothing; it is a constant | 624 -> 444, **-29%** | 1389 -> 1209, **-13%** |
| `distance_scale=` 100 -> 70 | the total | **-30%** | **-30%** |
| `pitch_distance=` 3 -> 2 | the pitch term | 624 -> 496, **-21%** | 1389 -> 1006, **-28%** |

- **The band** is an additive constant, so its authority collapses as the camera
  tips over. Answering "I want to zoom in further" with a closer band end
  answers it only for the angles that were already closest -- which is exactly
  how a phone ends up with an overhead view it cannot pull in.
- **`distance_scale=`** scales everything, so it is worth the same at every
  angle. It is the same shape as the later client's own zoom
  (`* viewportZoom / 256`, `client.method2068`) and composes with it rather than
  replacing it.
- **`pitch_distance=`** scales the pitch term only, so nearly all of its value
  lands overhead and little of it level. It is the lever for "the top-down view
  is too far out" specifically, and the only one that changes how the camera
  *behaves* as it tips rather than where it sits.

Use the band for **how far a gesture may travel** -- it is the player's. Use the
other two for **how much camera a screen wants** -- they are the device's, and
they stay put while the pinch runs.

### `pitch_flattest=` / `pitch_steepest=`

One statement of how far the camera may tip, read by everything that used to
spell it itself: the boot angle, the middle-button drag, `TORIRS_ORBIT_CAM`, the
arrow-key ease, and the terrain clamp -- which had the same two numbers a fifth
time as `32768`/`98048`, being 128 and 383 times
`REVCONFIG_CAMERA_PITCH_CLAMP_SCALE` (256, the fixed-point the clamp eases in).
Crossed ends are widened with a complaint, since a crossed pair would pin the
camera at one angle with the terrain clamp pushing it up and the drag pushing it
down.

Both stop below 512 -- a quarter turn -- because past it the eye is placed under
the anchor and the follow camera is upside down.

### `viewport_zoom=`

One key for one client-era fact, and it gates both halves of the later client's
zoom because in the reference they are one mechanism:

- the `* viewportZoom / 256` on the follow distance (`client.method2068`), and
- the viewport-recomputed projection scale (`class159.method5357`).

`no` is the 2004 camera: a flat `pitch * 3 + rest` and the bare `<< 9` of
`Model.project`. The 2004 profile (`revconfig/rs245_2lc/`) is the only lane that
states it.

It is emphatically **not** the same question as "does the wheel work" -- see
below. Those two shared one key once, and the settings row that appeared to turn
the wheel on instead turned this on and halved the picture.

## 3. The one camera value no INI may state

Whether the **wheel is live** (`enum RevConfigCameraWheel`) is the player's
switch, written by the settings page's "Zoom" row. It is the same answer on
every revision, so no profile states it and no merge carries it. A revision
describes its camera; it does not decide whether this client offers a gesture
the player wants.

## 4. The two terms nothing in `[camera]` states

**The viewport distance term** (`app_world_cam_dist_zoom`): the follow distance
is interpolated between `host.viewport_zoom` (default 256) and
`viewport_zoom_max` (default 320) over the world viewport height, 334 px to
434 px. A full-screen world view is past the far end and takes the whole
`320/256` -- **the eye sits 25% further out than the raw expression says.** CS2
writes the endpoints with `VIEWPORT_SETZOOM`. Skipped entirely under
`viewport_zoom=no`.

**The projection scale** (`class159.method5357`, in `app.c`): `vp_h * fov / 334`,
interpolated between the `VIEWPORT_SETFOV` endpoints over the same 100 px band.
That is magnification, not distance. On-screen size is
`projection scale / distance`, so the *fraction of the screen* a player occupies
works out as `fov / (334 * distance)` -- independent of how many pixels tall the
viewport is, and dependent on these two terms.

That identity is the answer to "why does it look further away on the phone": a
full-height world view takes the far end of the distance interpolation (1.25x
out) while the FOV endpoints a revision never writes both default to 256, so
nothing takes it back.

## 5. Where the same numbers are reachable at runtime

The plugin settings page ("Camera") writes the resolved profile live, which is
the fastest way to find a value on a real device before committing it to an ini:
Zoom (wheel on/off), Zoom range (band presets), Wheel step, **Camera distance**
(`distance_scale`), Arrow keys, Middle-button drag.

`TORIRS_CAM_DEBUG=1` prints yaw/pitch/zoom/eye on every gesture that moves the
camera. `TORIRS_ORBIT_CAM=<yaw>,<pitch>,<zoom%>,<spin>` pins the whole thing for
a reproducible frame.

## 6. What the profiles ship today

Every OSRS lane states nothing on the desktop -- the defaults above are the
camera -- and this on touch:

Every profile states its pitch behaviour, because that is a fact about the
revision's camera rather than a house default:

```ini
[camera]
pitch_distance=3     ; the reference's coefficient
pitch_flattest=128   ; and the reference's range
pitch_steepest=383
```

and every profile overrides two zoom keys and the coefficient on touch:

```ini
[camera@mobile]
zoom_closest=60      ; a tenth of the rest, and exactly one wheel_step
distance_scale=70    ; a phone wants a third less room than a monitor
pitch_distance=2     ; and the overhead view is the one people play from
```

The 2004 lane (`rs245_2lc`) states its camera model as well:

```ini
[camera]
rest=600
viewport_zoom=no
pitch_distance=3
pitch_flattest=128
pitch_steepest=383
controls=arrow_keys
```

Resolved (`osrs239`, with the full viewport term a full-screen phone takes):

| | desktop closest | mobile closest | mobile rest |
|---|---|---|---|
| level (pitch 128) | 780 (6.1 tiles) | 276 (2.2) | 749 (5.9) |
| overhead (pitch 383) | 1736 (13.6 tiles) | 722 (5.6) | 1194 (9.3) |

Overhead, the phone's closest view was 1623 fine units before any of this and is
722 now -- a picture 2.2x bigger -- while the desktop is untouched.
