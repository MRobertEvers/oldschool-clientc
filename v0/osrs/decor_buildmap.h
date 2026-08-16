#ifndef DECOR_BUILDMAP_H
#define DECOR_BUILDMAP_H

#include "heightmap.h"

enum DecorDisplacementKind
{
    DECOR_DISPLACEMENT_KIND_NONE = 0,
    DECOR_DISPLACEMENT_KIND_STRAIGHT,
    DECOR_DISPLACEMENT_KIND_DIAGONAL,
    DECOR_DISPLACEMENT_KIND_STRAIGHT_ONWALL_OFFSET,
    DECOR_DISPLACEMENT_KIND_DIAGONAL_ONWALL_OFFSET,
};

struct DecorElementsOnWall
{
    int element_id[2];
    int8_t displacement_kind[2];
    int8_t orientation[2];
    int8_t count;
};

struct DecorBuildMap
{
    struct DecorElementsOnWall* elements;
    int16_t* offsets;

    int size_x;
    int size_z;
    int levels;
};

struct DecorBuildMap*
decor_buildmap_new(
    int size_x,
    int size_z,
    int levels);

void
decor_buildmap_free(struct DecorBuildMap* build_map);

int
decor_buildmap_get_wall_offset(
    struct DecorBuildMap* build_map,
    int x,
    int z,
    int level);

void
decor_buildmap_set_wall_offset(
    struct DecorBuildMap* build_map,
    int x,
    int z,
    int level,
    int wall_offset);

void
decor_buildmap_add_element(
    struct DecorBuildMap* build_map,
    int x,
    int z,
    int level,
    int element_id,
    int orientation,
    enum DecorDisplacementKind displacement_kind);

struct DecorElementsOnWall*
decor_buildmap_get_elements(
    struct DecorBuildMap* build_map,
    int x,
    int z,
    int level);

#endif