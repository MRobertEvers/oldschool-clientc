#ifndef ENTITY_POS_H
#define ENTITY_POS_H

struct EntityPos
{
    int x;
    int y;
    int z;

    int pitch_r2pi2048;
    int yaw_r2pi2048;
    int roll_r2pi2048;
};

#endif