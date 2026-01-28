#ifndef PLAYER_APPEARANCE_H
#define PLAYER_APPEARANCE_H

#include <stdint.h>

struct PlayerAppearance
{
    uint16_t appearance[12];
    uint16_t color[5];
    char name[13];
    int combat_level;
    int readyanim;
    int turnanim;
    int walkanim;
    int walkanim_b;
    int walkanim_r;
    int walkanim_l;
    int runanim;
};

void
player_appearance_decode(
    struct PlayerAppearance* appearance,
    uint8_t* buf,
    int len);

#endif