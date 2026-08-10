# RS2012 QBD arena and model repair log

This directory records the visual and runtime validation of the 2012 Queen
Black Dragon and Tormented Demon cache port. It is intentionally a running log:
each material change is paired with a client capture and the command used to
produce it.

## 2026-08-10 — broken baseline

The composed `osrs239` manifest starts the QBD encounter through the
QA-only `rs2012qbdmanifest` cheat, but the first client frame is visibly wrong:

- the arena floor is a flat teal colour;
- the central platform contains a black rectangular void;
- a large white malformed mesh appears at the west edge;
- the OpenGL client subsequently exits with signal 11 while the encounter is
  waking, while the software renderer survives long enough to capture a frame;
- the same imported-material path is shared by the Tormented Demon model, so
  both encounters require visual validation rather than binary-only checks.

![Broken arena baseline](images/01_before_qbd_arena.png)

The software-renderer reproduction reported missing destination underlays 244,
245, and 246. The map importer had allocated RS2012 underlay configs at
500–511, but a terrain tile carries the underlay identity in one byte. Values
such as 501 were therefore truncated to 245 before lookup. This is a concrete
map-encoding defect, not missing source terrain.

The model audit found that QBD and Tormented Demon vertex/face geometry matches
the source 727 records, but nearly every face uses a procedurally baked 727
material. Passing binary model round-trip tests therefore did not prove that
the models render correctly in the OSRS239 client. Material and in-client
screenshots remain the acceptance criterion.

## 2026-08-10 — wire and renderer crash repaired

The high-ID NPC transformation now installs sleeping QBD type 25003 and models
110000/110001 at the expected five-tile footprint. The malformed west-edge
mesh did not move when the NPC type changed, proving it was the left-claw
location rather than the Queen.

The working macOS ASan flavor then found the immediate crash: a full ToriDraw
scene allocated projection and face-order buffers for 4,096 vertices/faces,
while the merged sleeping QBD contains 6,223 vertices and 9,012 faces. Priority
4 alone contains 6,484 faces, also exceeding the hidden 2,000-face priority
stride. Full-scene capacities and stride-aware indexing are now explicit, with
a pre-projection capacity guard. The composed encounter ran for 900 client
frames under the dylib-backed sanitizer and exited normally.

This capture is intentionally not called “fixed”: the NPC/wire crash is gone,
but the left claw is still white, the platform is black, and the default camera
does not frame QBD. It records the boundary between protocol/allocator repair
and the remaining material/camera work.

![After NPC and scene-capacity repair](images/03_after_npc_and_capacity_fix.png)

The material cause is now concrete. RS727 material `valid` is the SD-selection
bit, and all three left-claw materials plus all nine Tormented Demon materials
are HD-only (`valid=0`). The first bridge forced their procedural shader inputs
into OSRS diffuse sprites; material 285, for example, is pale normal/noise data,
which is why the claw looked like a white malformed mass. The revised bridge
retains every baked asset but drops HD-only selectors from destination model
faces so OSRS239 lights their underlying HSL colours, matching the source 2012
SD client.

A second defect was in the server's revision-239 NPC encoder, not in the chosen
25000–25009 allocation. The per-client NPC index is 16 bits, but an
initial add contains only a 14-bit type field. For a high definition such as
25000, the add's update flag must be set and that same `NPC_INFO` packet must
carry update-mask bit `0x1`: its replacement type is the transformed unsigned
16-bit `p2Alt3` / `UShortLEAdd` value. The 16-bit replacement path—not a
16-bit add type—is what makes 25000–25009 valid; 14 bits is not a global NPC
definition ceiling. The encoder already decoded that replacement block but did
not force it for a newly added high-ID NPC. The first attempted repair moved
the definitions under 16384; review against the 239 deob showed that was
unnecessary and it has been reverted. The permanent repair retains 25000–25009
and emits the same-packet type replacement, with a wire decoder regression for
QBD type 25003.

The intermediate capture below is still useful evidence: it removed the false
human NPC, but the unchanged white west-edge mesh proved that mesh belongs to
the imported left-claw location rather than QBD. The lower-ID allocation shown
in this image was only a diagnostic and is no longer the implementation.

![Intermediate NPC-wire diagnostic](images/02_npc_wire_diagnostic.png)

### Reproduction

```sh
make -C src mock230-cache-rs2012
./run-live.sh manifest_osrs239_rs2012.ini qbdrepro test --opengl3
```

The manifest's startup cheat is deliberately separate from the production
portal gate. It bypasses only the 60 Summoning requirement and does not change
the account's skills or quest state.

### Repair checklist

- [x] Capture the broken arena baseline.
- [x] Identify the underlay-ID truncation.
- [x] Verify source/destination QBD and TD geometry records structurally.
- [x] Preserve high NPC IDs through the revision-239 transformation update.
- [x] Reallocate and regenerate all RS2012 terrain underlays within the byte
  domain.
- [x] Restore the macOS ASan dylib/static-SDL build path; plain sanitizer flags
  alone hang during dyld/allocator initialisation on macOS 26.
- [ ] Isolate the OpenGL crash under an instrumented build.
- [x] Identify and repair the QBD projection/face-order buffer overflow.
- [x] Apply the source SD material-selection rule to imported model faces.
- [ ] Validate QBD, claws, platforms, and TD with destination materials enabled.
- [ ] Capture corrected QBD arena and QBD/TD model images.
- [ ] Run the composed-cache, map, UI, combat, and client-launch regressions.
