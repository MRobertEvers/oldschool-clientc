#ifndef SHADEDMAP_H
#define SHADEDMAP_H

#include "rscache/tables/maps.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

struct Shademap2
{
    int* map;
    int width;
    int height;
    int levels;
};

struct Shademap2*
shademap2_new(
    int tile_width,
    int tile_height,
    int levels);

void
shademap2_free(struct Shademap2* shademap);

bool
shademap2_in_bounds(
    struct Shademap2* shademap,
    int sx,
    int sz,
    int level);

void
shademap2_set_wall_corner(
    struct Shademap2* shademap,
    int sx,
    int sz,
    int slevel,
    int orientation,
    int shade);

void
shademap2_set_wall(
    struct Shademap2* shademap,
    int sx,
    int sz,
    int slevel,
    int orientation,
    int shade);

void
shademap2_set_sized(
    struct Shademap2* shademap,
    int sx,
    int sz,
    int slevel,
    int size_x,
    int size_z,
    int shade);

int
shademap2_get(
    struct Shademap2* shademap,
    int sx,
    int sz,
    int slevel);
#endif