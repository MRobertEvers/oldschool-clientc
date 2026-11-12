#ifndef SRC_EDITOR_EDITOR_FLOORNAMES_H
#define SRC_EDITOR_EDITOR_FLOORNAMES_H

/**
 * What a mapper calls a floor record, headless.
 *
 * The cache's underlays and overlays have no names: configs/all.underlay is
 * `[underlay_47] colour=0x35720A`, 256 of them, and configs/all.overlay 659
 * more. So the Palette was two wells of unlabelled cells, and "which of
 * these is a dirt road, a stone floor, wood planks or water" had no answer
 * on the screen (2026-11-11). This module gives one, three ways:
 *
 *   - THE COMMON SET: the floors the content's own maps are made of, most
 *     used first, each with the name a person would say. Counted over every
 *     .jm2 of OSRS-Content/osrs-content/maps (2,949 squares): underlay 48 is
 *     on 988,458 tiles of 597 squares and is the grass; overlay 14 is the
 *     brown track of 269 squares; overlay 6 lays texture 1, the water. An
 *     entry also states what the record must BE (its colour, or the texture
 *     it lays): a cache whose record under that id is something else does
 *     not get the name, so the table can never mislabel another revision.
 *   - A TEXTURE'S NAME, for any overlay that lays one: texture 3 is the
 *     plank floor whatever overlay lays it.
 *   - A COLOUR'S WORDS, for everything else: "dark green (grass)", "light
 *     brown (dirt)", "grey (stone)" -- the tone, the hue, and the ground a
 *     map uses that colour for.
 *
 * And the search the Palette's Find box and its two pick fields run over
 * those names: "dirt", "water", "wood", an id, a "#3d2b0b".
 *
 * Ids here are FILE ids: the `.jm2` value, config id + 1 (as everywhere in
 * the Palette).
 */

#include <stddef.h>
#include <stdint.h>

/** A name's bytes, NUL included: every name this module writes fits. */
#define EDITOR_FLOOR_NAME_MAX 40

/** What a common floor is for: the path painter asks for one by role. */
enum Editor_FloorRole
{
    EDITOR_FLOOR_ROLE_NONE = 0,
    /** The brown track a dirt path lays. */
    EDITOR_FLOOR_ROLE_DIRT_TRACK,
    /** The grey floor a pavement lays. */
    EDITOR_FLOOR_ROLE_PAVING,
    /** The water a river lays. */
    EDITOR_FLOOR_ROLE_WATER,
};

/** One floor of the common set. */
struct Editor_FloorCommon
{
    int is_overlay;
    /** The file id. */
    int id;
    /** The record's colour, when `texture` is -1; else ignored. */
    uint32_t rgb;
    /** The texture the overlay lays, or -1 for a plain colour. */
    int texture;
    const char* name;
    enum Editor_FloorRole role;
};

/** The common set, underlays then overlays, each kind most used first. */
const struct Editor_FloorCommon*
Editor_FloorCommonList(int* out_count);

/**
 * The common entry for file id `id` of a kind, when the record IS what the
 * entry says: `rgb` its colour, `texture` the texture it lays (-1 none, -2
 * not known yet: the entry is then taken on its colour alone when it has
 * one, and on trust when it is a textured entry). NULL otherwise.
 */
const struct Editor_FloorCommon*
Editor_FloorCommonFind(
    int is_overlay,
    int id,
    uint32_t rgb,
    int texture);

/** What ground texture `texture` is ("water", "wood planks"), or NULL for
 *  one this module has no name for. */
const char*
Editor_FloorTextureName(int texture);

/**
 * A colour in a mapper's words: "dark green (grass)", "sand", "grey
 * (stone)", "black", "white (snow)". Never more than EDITOR_FLOOR_NAME_MAX
 * bytes; `size` must hold that.
 */
void
Editor_FloorColourWords(
    uint32_t rgb,
    char* out,
    size_t size);

/**
 * The name of a record: its common name, else its texture's, else its
 * colour's words (a texture this module cannot name is "texture N").
 * `texture` as Editor_FloorCommonFind takes it.
 */
void
Editor_FloorName(
    int is_overlay,
    int id,
    uint32_t rgb,
    int texture,
    char* out,
    size_t size);

/**
 * Does `query` find a record called `name` with file id `id` and colour
 * `rgb`? Every word of the query must: be the id; or be a "#" and the first
 * digits of the colour; or be part of the name, or a word that means one in
 * it ("road" finds a track, "mud" dirt, "rock" stone, "planks" wood, "pond"
 * water). Letter case aside. An empty query finds everything.
 */
int
Editor_FloorMatches(
    const char* query,
    int id,
    const char* name,
    uint32_t rgb);

#endif
