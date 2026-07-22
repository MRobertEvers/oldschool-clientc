#ifndef SRC_NET_REV_PKT_PLAYER_APPEARANCE_H
#define SRC_NET_REV_PKT_PLAYER_APPEARANCE_H

/*
 * PLAYER_INFO appearance-blob decoder (port of v0 player_appearance; layout
 * matches the authoritative server's Player.generateAppearance: gender g1,
 * headicons g1, 12 slots (g1 0 = empty, else (msb<<8)|g1: 0x100+idk part or
 * 0x200+worn obj id), 5 colour g1s, 7 movement anims g2 (65535 -> -1),
 * name g8 base37, combat level g1).
 */

#include <stdint.h>

struct PktPlayerAppearance
{
    int gender;
    int headicon;
    int slots[12]; /* 0 | 0x100+idk | 0x200+obj */
    int colors[5];
    int readyanim;
    int turnanim;
    int walkanim;
    int walkanim_b;
    int walkanim_l;
    int walkanim_r;
    int runanim;
    char name[16];
    int combat_level;
};

/** Returns 1 on success, 0 on a short/invalid blob. */
int
PktPlayerAppearance_Decode(
    struct PktPlayerAppearance* out,
    uint8_t const* buf,
    int len);

#endif
