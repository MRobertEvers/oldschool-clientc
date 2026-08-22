#ifndef REVCONFIG_PROFILE_H
#define REVCONFIG_PROFILE_H

#include "revconfig.h"

/*
 * The `[features]` and `[camera]` sections of one boot, merged across every
 * RevConfig source and kept for the life of the client.
 *
 * The sibling of RevConfigRefs, and here for the same reason: the UI manifest
 * is built inside Task_UITreeBuild and freed with it, but these two answers are
 * read all session — the era table decides how every click routes, and the
 * camera policy is consulted on every wheel notch and every middle-button drag.
 * So the App parses the same sources once into this struct and holds it.
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

    /** `[camera]`, fully resolved: RevConfigProfile_Init seeds the defaults and
     *  a stated key replaces one. Read directly — has_zoom / has_controls are
     *  spent by the merge and mean nothing afterwards. */
    struct RevConfigCameraItem camera;
};

/** Seed the defaults: no feature stated, and the camera this tree shipped with
 *  (wheel zoom over REVCONFIG_CAMERA_ZOOM_DEFAULT_MIN..MAX, middle button and
 *  arrow keys both live). A profile that says nothing therefore changes
 *  nothing. */
void
RevConfigProfile_Init(struct RevConfigProfile* profile);

/**
 * Merge every `[features]` / `[camera]` item in `items` into `profile`.
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

/** Clamp `height` into the camera's zoom band. A `fixed:` camera has a band of
 *  one, so this is also what pins it. */
int
RevConfigProfile_CameraClampHeight(
    struct RevConfigProfile const* profile,
    int height);

#endif
