#ifndef GAME_RS_IDK_DESIGN_H
#define GAME_RS_IDK_DESIGN_H

/*
 * Player-design (character creation) store — reference Client `idkDesignGender`
 * / `idkDesignPart` / `idkDesignColour` / `idkDesignRedraw`.
 *
 * The tutorial's design interface (dat1 iface 3559) is driven entirely by
 * client codes: 300-313 cycle the identity kit of a body part, 314-323 cycle a
 * design colour, 324/325 switch gender, 326 accepts (IDK_SAVEDESIGN), and 327
 * is the live preview widget. Nothing about it is server-driven until Accept.
 *
 * This module owns only the *state* and the reference's cycling rules; the
 * composite rebuild and the packet live in the app layer (they need the
 * provider, scene bridge and network).
 */

#include <stdint.h>

#define RS_IDK_DESIGN_PARTS 7   /* head, jaw, torso, arms, hands, legs, feet */
#define RS_IDK_DESIGN_COLOURS 5 /* hair, torso, legs, feet, skin */
/* Model ids whose load is already queued for the pending rebuild. Seven kits
 * of a handful of models each; the cap only bounds the dedup window. */
#define RS_IDK_DESIGN_LOAD_TRACK_MAX 64

struct CacheProvider;

struct RS_IdkDesign
{
    /** 0 = male, 1 = female. Reference `idkDesignGender` is a bool where true
     *  is male; the body-part id of a female kit is `part + 7`. */
    int gender;
    /** Identity kit id per design part, -1 = none resolved. */
    int parts[RS_IDK_DESIGN_PARTS];
    /** Palette index per design colour; 0 is the identity colour. */
    int colours[RS_IDK_DESIGN_COLOURS];
    /** IdkType.numDefinitions — the cycling wrap bound. 0 until resolved. */
    int kit_count;
    /** Composite is stale and must be rebuilt (reference idkDesignRedraw). */
    uint8_t redraw;

    /** Model ids already requested for the pending rebuild (dedup, so a
     *  rebuild that waits on IO does not re-queue the same loads every tick). */
    int load_requested[RS_IDK_DESIGN_LOAD_TRACK_MAX];
    int load_requested_count;

    /** Gender-button sprites captured from the first 324/325 widget seen
     *  (reference idkDesignButton1/idkDesignButton2). */
    int button_scene_id[2];
    uint8_t button_scene_valid;
};

/** Default state: male, no kits resolved, all colours 0, rebuild pending. */
void
RS_IdkDesign_Init(struct RS_IdkDesign* design);

/**
 * Resolve IdkType.numDefinitions from the provider's loaded identity kits
 * (highest present id + 1). Identity kit ids are contiguous from 0, so this
 * equals the reference's count once CreateTask_PlayerAppearanceLoad has walked
 * them. Returns the count and caches it on the store.
 */
int
RS_IdkDesign_ResolveKitCount(
    struct RS_IdkDesign* design,
    struct CacheProvider* provider);

/**
 * Reference validateIdkDesign(): for each design part pick the first
 * selectable kit whose body part matches the current gender, and mark the
 * composite for rebuild.
 */
void
RS_IdkDesign_Validate(
    struct RS_IdkDesign* design,
    struct CacheProvider* provider);

/**
 * Resolve the kit table and pick the default parts if that has not happened
 * yet, so callers do not depend on which of them runs first after the design
 * interface mounts. Returns nonzero once `parts` are usable.
 */
int
RS_IdkDesign_EnsureResolved(
    struct RS_IdkDesign* design,
    struct CacheProvider* provider);

/**
 * Reference clientButton() client codes 300-325: part cycling, colour cycling
 * and the gender switch. Returns nonzero when `client_code` was a design
 * button (handled), zero when it belongs to some other subsystem.
 */
int
RS_IdkDesign_Button(
    struct RS_IdkDesign* design,
    struct CacheProvider* provider,
    int client_code);

/** Palette size for a design colour slot (reference recol1d[part].length). */
int
RS_IdkDesign_ColourCount(int part);

/** Note `model_id` as requested; returns nonzero when it was not already in
 *  the dedup window (i.e. the caller should queue the load). */
int
RS_IdkDesign_LoadRequestAdd(
    struct RS_IdkDesign* design,
    int model_id);

#endif
