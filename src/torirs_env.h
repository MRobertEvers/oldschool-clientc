#ifndef TORIRS_ENV_H
#define TORIRS_ENV_H

#include <stdlib.h>

/*
 * THE CLIENT'S RUNTIME KNOBS, READ ONCE.
 *
 * `getenv` is a linear scan of the whole environment -- with this client's
 * env it measured ~1.4k cycles a call on the phone -- and the frame path was
 * asking 74 of them every frame: 0.59 % of the draw thread's cycles and 3.5 %
 * of its branch mispredicts, spent re-answering questions whose answers
 * cannot change. See KRAIT300_KERNEL_ANALYSIS.md § step 3 for the measurement
 * and for the per-call-site shim that found them, which is the only way to
 * attribute a caller on a phone that gives no call graphs.
 *
 * ONCE IS SAFE HERE, and not by luck: nothing in the tree calls `setenv` on
 * any of these names, Android's env.txt is applied before `main()`
 * (platform_android_jni.c), and every other host sets them in the shell that
 * launches the client. A knob that a running client could change would not
 * belong in this file.
 *
 * Two threads racing the first call both read the environment and store the
 * same pointer, so the race is benign and the cache needs no atomics.
 *
 * The knobs are declared HERE rather than beside their uses so that the
 * client's whole runtime-toggle surface is one list. A knob read in two
 * translation units gets a cache in each, which costs one extra `getenv` per
 * process and keeps the header free of definitions.
 *
 * -DTORIRS_NO_ENV_GATES compiles every knob to "unset". The gated blocks then
 * fold away entirely -- the branch, the log call, and the argument setup
 * feeding it -- which is what a build that will never have one set wants. It
 * is deliberately NOT the default: this tree's measurement discipline is one
 * binary that contains every arm (§10, "Rule for every step"), and a knob
 * compiled out cannot be an arm.
 */
#if defined(TORIRS_NO_ENV_GATES)
#define TORIRS_ENV_KNOB(fn_, name_)                                                                \
    static inline char const* fn_(void)                                                            \
    {                                                                                              \
        return NULL;                                                                               \
    }
#else
#define TORIRS_ENV_KNOB(fn_, name_)                                                                \
    static inline char const* fn_(void)                                                            \
    {                                                                                              \
        static char const* value_;                                                                 \
        static int read_;                                                                          \
        if( !read_ )                                                                               \
        {                                                                                          \
            value_ = getenv(name_);                                                                \
            read_ = 1;                                                                             \
        }                                                                                          \
        return value_;                                                                             \
    }
#endif

/* Network tracing and the packet cheat hook. */
TORIRS_ENV_KNOB(torirs_env_net_debug, "TORIRS_NET_DEBUG")
TORIRS_ENV_KNOB(torirs_env_net_cheat, "TORIRS_NET_CHEAT")

/* The world/camera traces, in the order app.c runs them. */
TORIRS_ENV_KNOB(torirs_env_pos_debug, "TORIRS_POS_DEBUG")
TORIRS_ENV_KNOB(torirs_env_hprof, "TORIRS_HPROF")
TORIRS_ENV_KNOB(torirs_env_tflags, "TORIRS_TFLAGS")
TORIRS_ENV_KNOB(torirs_env_tproj, "TORIRS_TPROJ")
TORIRS_ENV_KNOB(torirs_env_bridge_debug, "TORIRS_BRIDGE_DEBUG")
TORIRS_ENV_KNOB(torirs_env_world_view_debug, "TORIRS_WORLD_VIEW_DEBUG")
TORIRS_ENV_KNOB(torirs_env_wedge_zoom, "TORIRS_WEDGE_ZOOM")
TORIRS_ENV_KNOB(torirs_env_wedge_fov_debug, "TORIRS_WEDGE_FOV_DEBUG")
TORIRS_ENV_KNOB(torirs_env_orbit_debug, "TORIRS_ORBIT_DEBUG")

/* The painter's knobs: these change what is drawn, not just what is logged. */
TORIRS_ENV_KNOB(torirs_env_draw_distance, "TORIRS_DRAW_DISTANCE")
TORIRS_ENV_KNOB(torirs_env_painter_nocull, "TORIRS_PAINTER_NOCULL")
TORIRS_ENV_KNOB(torirs_env_painter_cull, "TORIRS_PAINTER_CULL")
TORIRS_ENV_KNOB(torirs_env_painter_w3d, "TORIRS_PAINTER_W3D")
TORIRS_ENV_KNOB(torirs_env_paint_debug, "TORIRS_PAINT_DEBUG")
TORIRS_ENV_KNOB(torirs_env_occluders, "TORIRS_OCCLUDERS")
TORIRS_ENV_KNOB(torirs_env_occluders_debug, "TORIRS_OCCLUDERS_DEBUG")
TORIRS_ENV_KNOB(torirs_env_wedge_drawcenter, "TORIRS_WEDGE_DRAWCENTER")
TORIRS_ENV_KNOB(torirs_env_tiletable, "TORIRS_TILETABLE")
TORIRS_ENV_KNOB(torirs_env_tiletable_at, "TORIRS_TILETABLE_AT")

/* Interface, script host and input traces. */
TORIRS_ENV_KNOB(torirs_env_clientop_debug, "TORIRS_CLIENTOP_DEBUG")
TORIRS_ENV_KNOB(torirs_env_var_hook_debug, "TORIRS_VAR_HOOK_DEBUG")
TORIRS_ENV_KNOB(torirs_env_element_alias_check, "TORIRS_ELEMENT_ALIAS_CHECK")
TORIRS_ENV_KNOB(torirs_env_force_show_slot, "TORIRS_FORCE_SHOW_SLOT")
TORIRS_ENV_KNOB(torirs_env_key_debug, "TORIRS_KEY_DEBUG")
TORIRS_ENV_KNOB(torirs_env_overlay_debug, "TORIRS_OVERLAY_DEBUG")
TORIRS_ENV_KNOB(torirs_env_overlay_script_debug, "TORIRS_OVERLAY_SCRIPT_DEBUG")

/* Host-level knobs main.c asks about every frame. */
TORIRS_ENV_KNOB(torirs_env_frame_debug, "TORIRS_FRAME_DEBUG")
TORIRS_ENV_KNOB(torirs_env_screenshot, "TORIRS_SCREENSHOT")
TORIRS_ENV_KNOB(torirs_env_cs2_harness, "TORIRS_CS2_HARNESS")
TORIRS_ENV_KNOB(torirs_env_touch_ui, "TORIRS_TOUCH_UI")
TORIRS_ENV_KNOB(torirs_env_chrome_scale, "TORIRS_CHROME_SCALE")

#endif /* TORIRS_ENV_H */
