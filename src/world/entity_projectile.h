#ifndef WORLD_ENTITY_PROJECTILE_H
#define WORLD_ENTITY_PROJECTILE_H

#include "entity_facets.h"

#include <stdbool.h>

struct WorldEntity_Projectile
{
    int element_id;
    int level;
    int dst_level;

    /* Immutable params (world units / ticks). */
    int src_x;
    int src_z;
    int h1;
    int end_height;
    int t1;
    int t2;
    int angle;
    int startpos;
    int dst_x;
    int dst_z;

    /* Dynamic state. */
    int cycle;
    bool launched;
    double x;
    double y;
    double z;
    double vx;
    double vy;
    double vz;
    double velocity;
    double ay;
    struct WorldEntityFacet_OrientationPYR orientation;
};

#endif
