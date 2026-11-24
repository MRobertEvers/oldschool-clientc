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
TORIRS_ENV_KNOB(torirs_env_loc_lane_trace, "TORIRS_LOC_LANE_TRACE")
/* How a server REBUILD is done: "blocking" (default) or "background", and a
 * background rebuild's deadline in ms (App_WorldRebuildModeInit). */
TORIRS_ENV_KNOB(torirs_env_world_rebuild, "TORIRS_WORLD_REBUILD")
TORIRS_ENV_KNOB(torirs_env_world_job_deadline_ms, "TORIRS_WORLD_JOB_DEADLINE_MS")
/* Where a rebuild's milliseconds go; the renderers read it here (the world
 * builder keeps its own reading, WorldBuilder_TimingOn). */
TORIRS_ENV_KNOB(torirs_env_rebuild_timing, "TORIRS_REBUILD_TIMING")
/* "1": a device that cannot afford two world builds (a tablet: the web page
 * decides, src/web/torirs_host.js). The retained GPU renderers then never
 * prebake a stage beside the drawn build and give the retired build's GPU
 * buffer back at the swap, so one build's storage is resident, not two. */
TORIRS_ENV_KNOB(torirs_env_low_memory, "TORIRS_LOW_MEMORY")
/* What the web page's device check (src/web/torirs_devicecheck.js) chose and
 * the player confirmed with OK, set for that one boot only. FPS is the Limit
 * Framerate value (5/15/20/30/60, 999 = Unlimited); RENDERER is the Client
 * Settings value (enum ToriRS_RendererKind + 1). app_boot.c writes both into
 * the option store after preferences load, as picking them in game would. */
TORIRS_ENV_KNOB(torirs_env_device_check_fps, "TORIRS_DEVICE_CHECK_FPS")
TORIRS_ENV_KNOB(torirs_env_device_check_renderer, "TORIRS_DEVICE_CHECK_RENDERER")
/* The device check's CPU score in tenths, set on every boot the page knows
 * one: the device lock (app/app_device_lock.h) reports it to the server. */
TORIRS_ENV_KNOB(torirs_env_device_score, "TORIRS_DEVICE_SCORE")
/* A path: write the loaded scene's geometry there once it has settled, for
 * the web device bench (app/app_scene_export.c). */
TORIRS_ENV_KNOB(torirs_env_scene_export, "TORIRS_SCENE_EXPORT")
/* The LOC_ADD/NPC_ADD trigger trace. Asked once per loc of a rebuilt scene:
 * a getenv there was ~3 ms of every rebuild's finish. */
TORIRS_ENV_KNOB(torirs_env_trigger_debug, "TORIRS_TRIGGER_DEBUG")
/* Name every task whose single run took at least this many ms, on the packet
 * FIFO, the frame queue and the background queue (task_slow:). The companion
 * of TORIRS_PKT_SLOW_MS, for the work a packet leaves behind it. */
TORIRS_ENV_KNOB(torirs_env_task_slow_ms, "TORIRS_TASK_SLOW_MS")
/* The paced catch-up after a background rebuild's commit (App_PaceArm): the
 * packet and script work one frame may do, in microseconds (default 4000; 0
 * turns pacing off), and how long after the commit it stays paced, in ms
 * (default 1000: the loads a new scene asks for, its music among them, arrive up to a
 * server tick behind the commit). */
TORIRS_ENV_KNOB(torirs_env_pace_budget_us, "TORIRS_PACE_BUDGET_US")
TORIRS_ENV_KNOB(torirs_env_pace_window_ms, "TORIRS_PACE_WINDOW_MS")
/* A retained renderer's world-build prefill: the frame work it fills up to,
 * and its largest slice, in microseconds (default 6000 and 4000). One pair
 * for every lane that prefills. */
TORIRS_ENV_KNOB(torirs_env_prep_target_us, "TORIRS_PREP_TARGET_US")
TORIRS_ENV_KNOB(torirs_env_prep_slice_us, "TORIRS_PREP_SLICE_US")

/* The painter's knobs: these change what is drawn, not just what is logged. */
TORIRS_ENV_KNOB(torirs_env_draw_distance, "TORIRS_DRAW_DISTANCE")
TORIRS_ENV_KNOB(torirs_env_painter_nocull, "TORIRS_PAINTER_NOCULL")
TORIRS_ENV_KNOB(torirs_env_painter_cull, "TORIRS_PAINTER_CULL")
TORIRS_ENV_KNOB(torirs_env_painter_w3d, "TORIRS_PAINTER_W3D")
TORIRS_ENV_KNOB(torirs_env_wev_flat, "TORIRS_WEV_FLAT")
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

/* The CS2 load-time jit (docs/CS2_JIT.md 4.11). TORIRS_CS2_JIT=0 builds no
 * overlay at all, so one binary is both arms of an A/B. Read by the HOST,
 * once, where it decides whether a loaded script gets an overlay
 * (CacheProvider_SetCS2Jit); the VM reads no environment. */
TORIRS_ENV_KNOB(torirs_env_cs2_jit, "TORIRS_CS2_JIT")
/* Its platform cache (docs/CS2_JIT.md 4.11), read once at boot by the App
 * (app_cs2jit_init): TORIRS_CS2_JIT_CACHE=0 builds every overlay in memory
 * and neither reads nor writes the pack; TORIRS_CS2_JIT_STATS=1 turns the
 * cache's timers on and prints its `cs2jit:` line after each store and at
 * shutdown; TORIRS_CS2_JIT_STORE_MS=<ms> is the write-behind delay after the
 * first new overlay (default 10000). TORIRS_DERIVED_DIR=<root> moves the
 * desktop's derived records (the pack) from beside the cache directory to
 * <root>/<cache dir's basename>/; platform_x_io.c reads it with getenv at
 * PlatformX_IO_InitCacheId, once per call, so it is not a knob here. */
TORIRS_ENV_KNOB(torirs_env_cs2_jit_cache, "TORIRS_CS2_JIT_CACHE")
TORIRS_ENV_KNOB(torirs_env_cs2_jit_stats, "TORIRS_CS2_JIT_STATS")
TORIRS_ENV_KNOB(torirs_env_cs2_jit_store_ms, "TORIRS_CS2_JIT_STORE_MS")

/* Host-level knobs main.c asks about every frame. */
TORIRS_ENV_KNOB(torirs_env_frame_debug, "TORIRS_FRAME_DEBUG")
TORIRS_ENV_KNOB(torirs_env_screenshot, "TORIRS_SCREENSHOT")
TORIRS_ENV_KNOB(torirs_env_cs2_harness, "TORIRS_CS2_HARNESS")
TORIRS_ENV_KNOB(torirs_env_touch_ui, "TORIRS_TOUCH_UI")
TORIRS_ENV_KNOB(torirs_env_chrome_scale, "TORIRS_CHROME_SCALE")

/* Debug traces that sat in per-node loops of an interface open and in the
 * per-frame animation walk, each a `getenv` per node: on Windows, whose C
 * runtime scans the environment with string compares, 0.15 ms of a bank
 * open. */
TORIRS_ENV_KNOB(torirs_env_anim_debug, "TORIRS_ANIM_DEBUG")
TORIRS_ENV_KNOB(torirs_env_onsubchange_debug, "TORIRS_ONSUBCHANGE_DEBUG")
TORIRS_ENV_KNOB(torirs_env_spillover_debug, "TORIRS_SPILLOVER_DEBUG")
TORIRS_ENV_KNOB(torirs_env_frame_role_audit, "TORIRS_FRAME_ROLE_AUDIT")

#endif /* TORIRS_ENV_H */
