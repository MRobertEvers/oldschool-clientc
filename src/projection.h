#ifndef PROJECTION_H
#define PROJECTION_H

#define UNIT_SCALE (512)
#define SCALE_UNIT(x) ((((long long)x) << 9))
#define UNIT_SCALE_SHIFT (9)

struct ProjectedVertex
{
    int x;
    int y;

    // z is the distance from the camera to the point
    int z;

    // If the projection is too close or behind the screen, clipped is nonzero.
    int clipped;
};

#endif
