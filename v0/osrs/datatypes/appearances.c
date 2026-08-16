#include "appearances.h"

void
appearances_decode(
    struct AppearanceOp* op,
    uint16_t* appearances,
    int slot)
{
    op->kind = APPEARANCE_KIND_NONE;

    uint16_t appearance = appearances[slot];
    if( appearance >= 0x100 && appearance < 0x200 )
    {
        op->kind = APPEARANCE_KIND_IDK;
        op->id = appearance - 0x100;
    }
    else if( appearance >= 0x200 )
    {
        op->kind = APPEARANCE_KIND_OBJ;
        op->id = appearance - 0x200;
    }
}
