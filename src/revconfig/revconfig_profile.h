#ifndef REVCONFIG_PROFILE_H
#define REVCONFIG_PROFILE_H

#include "revconfig.h"

/*
 * The `[features]`, `[camera]` and `[chrome]` sections of one boot, merged
 * across every RevConfig source and kept for the life of the client.
 *
 * The sibling of RevConfigRefs, and here for the same reason: the UI manifest
 * is built inside Task_UITreeBuild and freed with it, but these answers are
 * read all session — the era table decides how every click routes, the camera
 * policy is consulted on every wheel notch and every middle-button drag, and
 * the plugin button is rebuilt after every gameframe rebuild. So the App parses
 * the same sources once into this struct and holds it.
 *
 * Where RevConfigRefs answers "which id is that, on THIS cache", this answers
 * "how does THIS revision's client behave". Both are facts about the revision,
 * which is why they are stated in the revision profile rather than in each of
 * the boot manifests that share it — rs245_2lc's file serves 254, 289 and 377.
 *
 * Nothing here is resolved to an enum: `era`, `mover` and
 * `ground_click_nearest` are carried as the names the INI spelled, because
 * this module is a leaf and src/features/features.h owns the spellings. The
 * App resolves them, with the manifest's `[features:boot]` and the
 * TORIRS_FEATURES_* env vars layered on top.
 */
struct RevConfigProfile
{
    /** `[features]`. Every member carries its own "not stated" sentinel; see
     *  struct RevConfigFeaturesItem. */
    struct RevConfigFeaturesItem features;

    /** `[camera]`, fully resolved: RevConfigProfile_Init seeds one default per
     *  key and a stated key replaces exactly that one. Read directly — the
     *  has_* flags are the merge's own bookkeeping (they also say whether a
     *  band end is still derived from `rest`) and mean nothing to a reader. */
    struct RevConfigCameraItem camera;

    /** `[frame]`, fully resolved: no cap and revconfig as its owner until a
     *  profile says otherwise. @see struct RevConfigFrameItem */
    struct RevConfigFrameItem frame;

    /** `[chrome]` — where this revision mounts the client's own plugin button.
     *  Unlike the camera there are NO defaults: every number keeps its -1 and
     *  every name its empty string until a profile states it, because a strip
     *  that does not exist has no geometry to guess at. */
    struct RevConfigChromeItem chrome;
};

/** Seed the defaults: no feature stated, no chrome mount stated, and the camera
 *  this tree shipped with (wheel zoom over REVCONFIG_CAMERA_ZOOM_DEFAULT_MIN..MAX,
 *  middle button and arrow keys both live). A profile that says nothing
 *  therefore changes nothing. */
void
RevConfigProfile_Init(struct RevConfigProfile* profile);

/**
 * Merge every `[features]` / `[camera]` / `[chrome]` item in `items` into
 * `profile`.
 *
 * Per KEY, later wins — the same rule RevConfigRefs uses for ids, so a boot
 * manifest's inline `[revconfig:camera]` can move the zoom band without
 * restating the controls the shared profile already chose.
 */
void
RevConfigProfile_AddItems(
    struct RevConfigProfile* profile,
    struct RevConfigItemBuffer const* items);

/**
 * Parse the three RevConfig sources a boot manifest can name, in load order.
 * Any of them may be NULL or "". `inline_ini` is read under the `revconfig`
 * section prefix, i.e. it is the boot manifest itself.
 */
void
RevConfigProfile_LoadSources(
    struct RevConfigProfile* profile,
    char const* ui_ini,
    char const* cache_ini,
    char const* inline_ini);

/** Clamp a live zoom into `zoom_closest..zoom_furthest`. The band is always
 *  real, so this narrows a value the wheel moved; it never pins the camera.
 *  Pinning is the player's REVCONFIG_CAMERA_WHEEL_PINNED, not a band of one. */
int
RevConfigProfile_CameraClampZoom(
    struct RevConfigProfile const* profile,
    int height);

#endif
