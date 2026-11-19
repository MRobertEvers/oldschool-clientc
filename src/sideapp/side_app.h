#ifndef SRC_SIDEAPP_SIDE_APP_H
#define SRC_SIDEAPP_SIDE_APP_H

/*
 * The side-app registry: the client's debug tools, each one registered once
 * with its state, its hooks and the action names it answers.
 *
 * A side-app is a tool the client can run beside the game (the projectile
 * source-tile tool, the loc editor, the frame-time panel). The frame loop, the
 * hotkey code and the rest of the client never name a tool: they ask the
 * registry, and the registry calls whichever side-apps registered, in
 * registration order. The list of registrations is in one place
 * (app/app_tools.c).
 *
 * The registry knows nothing about the client. The host is an opaque
 * SideAppHost it only forwards to the hooks, and it reads no input: delivering
 * the key actions of a frame-loop phase takes a predicate from the caller that
 * says whether one binding's key fires under the gate and mode its action
 * declared. The gate rules are the caller's.
 *
 * Bindings come from the launch configuration (sideapp/side_app_binding.h),
 * by name. Each binding is resolved to (side-app, action) when the side-app
 * that answers its name registers; a name nobody registered is listed by
 * SideApps_UnresolvedCount / SideApps_UnresolvedAt, and the boot refuses it.
 */

#include "sideapp/side_app_binding.h"

/** The client a side-app runs in. Opaque here; the registry only forwards it. */
struct SideAppHost;

struct SideApps;

/** The frame-loop point a key action is delivered at. */
enum SideAppPhase
{
    /** The dev chrome's tick, before the BOOTING early-out: no text input
     *  focused. Its frame hooks run right after (SideApps_Frame), before the
     *  chrome is built. */
    SIDE_APP_PHASE_EARLY_CHROME,
    /** The world camera's flight, inside the camera keys (once per view the
     *  keys steer): past their gates and with no text input focused, before
     *  the map editor's own flight and the clamp that holds the camera over
     *  the scene. */
    SIDE_APP_PHASE_CAMERA_FLIGHT,
    /** The world camera keys, after the logic: past their gates, once per
     *  view the keys steer, after the arrows have turned the camera. */
    SIDE_APP_PHASE_CAMERA_KEYS,
    /** The world hotkeys, last in the frame's input: a hovered tile, no text
     *  input focused. */
    SIDE_APP_PHASE_WORLD_HOTKEYS,
    /** The loc editor's tick, before the BOOTING early-out: after its own
     *  toggle and before the map editor's, no text input focused. */
    SIDE_APP_PHASE_LOC_EDITOR,
    /** The plugin window's tick, before the BOOTING early-out: once a plugin
     *  host exists, after its headless drives and before the window is laid
     *  out. Delivered whatever has the keyboard; the side-app asks. */
    SIDE_APP_PHASE_PLUGIN_WINDOW,
    /** Never delivered: the owner polls its own bindings. Only the App
     *  layer's bridge for the tools not moved onto the registry yet. */
    SIDE_APP_PHASE_POLLED,
    SIDE_APP_PHASE_COUNT
};

/** The key rule an action's binding is read under. What each one means is the
 *  caller's predicate; the registry only hands it over. */
enum SideAppGate
{
    /** CTRL held; a key the map editor claimed this frame does not fire. */
    SIDE_APP_GATE_CTRL,
    /** No CTRL; a key a revconfig hotkey or the map editor claimed this
     *  frame does not fire. */
    SIDE_APP_GATE_WORLD
};

enum SideAppKeyMode
{
    /** On the frame the key goes down. */
    SIDE_APP_KEY_PRESS,
    /** Every frame the key is down. */
    SIDE_APP_KEY_HELD,
    /** The frame the key goes down and every frame it is down: a press that
     *  is let go within one frame counts, as a held key does. */
    SIDE_APP_KEY_PRESS_OR_HELD
};

/** One action a side-app answers. */
struct SideAppAction
{
    char const* name;
    enum SideAppPhase phase;
    enum SideAppGate gate;
    enum SideAppKeyMode mode;
    /** Rank within the phase: a phase delivers its actions by ascending
     *  `order`, equal ranks in registration order and then table order. 0
     *  unless the actions of a phase must interleave across side-apps. */
    int order;
};

struct SideAppHooks
{
    /** A key bound to action `action` (an index into the side-app's action
     *  table) fired. `args` is the binding's `a=`, "" when it has none. */
    void (*key)(
        void* self,
        struct SideAppHost* host,
        int action,
        char const* args);
    /** Release `self`. Called once, by SideApps_Free. */
    void (*free)(void* self);
    /** The keys of `phase` have all been delivered (SideApps_DeliverKeys),
     *  whether or not one of this side-app's fired. Called for a side-app
     *  with an action of that phase only. */
    void (*keys_done)(
        void* self,
        struct SideAppHost* host,
        enum SideAppPhase phase);
    /** Where `phase` is read, before the phase's own gates
     *  (SideApps_ProbeKeys): `any_down` says whether a binding of one of this
     *  side-app's actions of that phase goes down this frame, under the
     *  action's gate, read as a press whatever the action's mode. Called for
     *  a side-app with an action of that phase only. */
    void (*keys_probe)(
        void* self,
        struct SideAppHost* host,
        enum SideAppPhase phase,
        int any_down);
    /** A frame-loop point the host runs (SideApps_Frame) came round. Called
     *  at every such point, told which; a side-app does its work at the
     *  points it names and returns at the others. */
    void (*frame)(
        void* self,
        struct SideAppHost* host,
        enum SideAppPhase phase);
    /** Make the side-app's panels in the host's shared dev chrome, once,
     *  where the host makes that chrome's panels (SideApps_BuildChrome): a
     *  chrome numbers its panels in the order they are made. */
    void (*build_chrome)(
        void* self,
        struct SideAppHost* host);
    /** Push the side-app's marks into the host's world overlay, at the point
     *  of the overlay pass the host asks (SideApps_DrawWorldOverlay). */
    void (*world_overlay)(
        void* self,
        struct SideAppHost* host);
    /** Does the side-app need the pick to see every loc this frame -- walls,
     *  fences and decor with no ops, which the pick drops by default? */
    int (*inspects_scenery)(void const* self);
};

/** Does this binding's key fire, under this gate and mode, this frame? */
typedef int (*SideAppKeyFires)(
    void* ctx,
    struct SideAppBinding const* binding,
    enum SideAppGate gate,
    enum SideAppKeyMode mode);

#define SIDE_APP_MAX 32

/** A registry holding a copy of `bindings`, nothing registered yet. */
struct SideApps*
SideApps_New(struct SideAppBindingTable const* bindings);

/** Free each side-app through its free hook, the last registered first, then
 *  the registry. NULL is accepted. */
void
SideApps_Free(struct SideApps* apps);

/**
 * Register a side-app. `self`, `hooks` and `actions` must outlive the
 * registry; `self` is released through hooks->free (when set). No two
 * side-apps may answer one action name. Resolves every binding that names one
 * of `actions`. Returns the side-app's index.
 */
int
SideApps_Register(
    struct SideApps* apps,
    char const* name,
    void* self,
    struct SideAppHooks const* hooks,
    struct SideAppAction const* actions,
    int action_count);

/** The index of the side-app registered as `name`, -1 when none is. */
int
SideApps_IndexOf(
    struct SideApps const* apps,
    char const* name);

/**
 * Deliver the key actions of `phase`. Its actions by ascending `order`, equal
 * ranks for each side-app in registration order and each of its actions in
 * its own order: the first binding, in binding order, that names the action
 * and for which `fires` answers yes calls the key hook once with that
 * binding's args. An action fires at most once per delivery however many of
 * its keys fire -- the reading every hotkey had when the first matching
 * binding answered. Then each side-app with an action of the phase and a
 * keys_done hook, in registration order, is told the phase is done.
 */
void
SideApps_DeliverKeys(
    struct SideApps* apps,
    enum SideAppPhase phase,
    struct SideAppHost* host,
    SideAppKeyFires fires,
    void* ctx);

/**
 * Probe the keys of `phase` where it is read, before its gates: each
 * side-app with an action of that phase and a keys_probe hook, in
 * registration order, is told whether one of its bindings of the phase goes
 * down this frame (`fires` asked under the action's gate and
 * SIDE_APP_KEY_PRESS). Fires nothing.
 */
void
SideApps_ProbeKeys(
    struct SideApps* apps,
    enum SideAppPhase phase,
    struct SideAppHost* host,
    SideAppKeyFires fires,
    void* ctx);

/**
 * Run the frame hooks at frame-loop point `phase`: each side-app with a frame
 * hook, in registration order, is told the point came round.
 */
void
SideApps_Frame(
    struct SideApps* apps,
    enum SideAppPhase phase,
    struct SideAppHost* host);

/**
 * Make the side-apps' panels in the host's shared dev chrome: each side-app
 * with a build_chrome hook, in registration order. The host calls it once,
 * at the place in its chrome's panel order the side-apps' panels belong.
 */
void
SideApps_BuildChrome(
    struct SideApps* apps,
    struct SideAppHost* host);

/** The side-apps' world overlay marks: each world_overlay hook, in
 *  registration order. */
void
SideApps_DrawWorldOverlay(
    struct SideApps* apps,
    struct SideAppHost* host);

/** Does any side-app need every loc pickable this frame (its
 *  inspects_scenery hook says yes)? */
int
SideApps_AnyInspectsScenery(struct SideApps const* apps);

int
SideApps_BindingCount(struct SideApps const* apps);

struct SideAppBinding const*
SideApps_BindingAt(
    struct SideApps const* apps,
    int index);

/** Is binding `index` resolved to action `action` of side-app `app`? */
int
SideApps_BindingAnswers(
    struct SideApps const* apps,
    int index,
    int app,
    int action);

/** One action name no registered side-app answers. `source` is the
 *  manifest's `[action:<source>]` that declared it, "" for a binding that no
 *  declaration names. */
struct SideAppUnresolved
{
    char const* action;
    char const* source;
};

/** How many names nobody answers: each declared action whose name no
 *  side-app registered, then each binding whose name is neither registered
 *  nor declared. */
int
SideApps_UnresolvedCount(struct SideApps const* apps);

struct SideAppUnresolved
SideApps_UnresolvedAt(
    struct SideApps const* apps,
    int index);

#endif
