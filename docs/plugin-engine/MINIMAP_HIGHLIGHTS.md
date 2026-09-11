# Native minimap highlights

The native highlight snapshot already carried flag 64, but the renderer only
had world drawing verbs. A script-author workaround would have needed to guess
the minimap's projection, clip, mask and layering, and a world draw could be
clipped before any map mark was produced.

`on_draw_minimap` now receives a scoped builder with `minimap_tile`. Its inputs
are absolute world tile coordinates, level, fill/outline colors, opacity and
outline width. The host projects them using the normal minimap camera and
local-player anchor. The shared frame applies the minimap widget's actual clip,
mask crop and mask polarity, then emits ordinary colored spans. Native icons,
actor dots and the local-player marker remain above these marks. No backend
specific drawing path or per-frame texture upload is required.

The tile occupies the normal four map pixels per tile, rotated with the map.
Outline width is an inward width in canvas pixels; zero omits the border.
Fill opacity remains 0..255. A different level, distant tile or empty style is
a successful no-op. Capacity refusal returns BUDGET. The map has 512 plugin slots
in addition to its original 256 native dot slots, and the per-plugin drawing
budget is shared with that plugin's other drawing passes.

This is a preparation callback, like the existing world/canvas draw callbacks,
not a notification that a physical presentation completed. Its result is cached
within one App_RunOnce so a retained-UI refresh followed by a full-walk retry
cannot invoke the callback or consume its budget twice. The next loop rebuilds
from active callbacks; disabling the plugin or removing a native mark therefore
leaves no stale map records. Physical FPS uses FrameEvent.drawn_frames.

The callback and graphics verb are appended to the existing source API. The
MINIMAP_SIZE guard protects an older graphics prefix. Lua receives only the
minimap verb in this callback; wrong-surface calls are refused before engine
execution. Graphics reports the existing INVALID result for a scope error.

Focused checks cover native flag 64 on/off, a complete 2x3 footprint, disable,
512 complete requests and the capacity refusal, off-map no-op after the budget,
world/minimap scope separation, Lua forwarding and prefix checks, projection,
mask polarity/crop, independent alpha, distinct one/two-pixel borders, repeated
frames and native-dot layering. Live acceptance must still use the native tile
marker setup/clientop and inspect the map at 2x. The hovered-tile group is a live
world-only control with different style values; exact isolation of flag 64 is
covered by the focused test and must not be claimed for that different-group
live comparison.
