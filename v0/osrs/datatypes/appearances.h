#ifndef APPEARANCES_H
#define APPEARANCES_H

#include <stdint.h>

enum AppearanceKind
{
    APPEARANCE_KIND_NONE = 0,
    APPEARANCE_KIND_OBJ,
    APPEARANCE_KIND_IDK,
};

struct AppearanceOp
{
    enum AppearanceKind kind;
    uint16_t id;
};

void
appearances_decode(
    struct AppearanceOp* op,
    uint16_t* appearances,
    int slot);

#endif