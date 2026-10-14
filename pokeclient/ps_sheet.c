#include "ps_sheet.h"

#include "engine/png_decode.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct PS_Sheet*
PS_SheetDecode(
    const uint8_t* bytes,
    int size)
{
    assert(bytes);
    struct PokeSpriteSheet head;
    if( !PokeSprite_ParseSheet(bytes, size, &head) )
        return NULL;
    int w = 0, h = 0;
    uint32_t* px = NULL;
    if( !PngDecode_Argb(head.png, head.png_size, &w, &h, &px) )
        return NULL;
    if( w != head.cell_w * head.frames || h != head.cell_h * head.facings )
    {
        free(px);
        return NULL;
    }
    struct PS_Sheet* s = (struct PS_Sheet*)calloc(1, sizeof(*s));
    assert(s);
    s->head = head;
    s->head.png = NULL;
    s->head.png_size = 0;
    s->width = w;
    s->height = h;
    s->pixels = px;
    for( int i = 0; i < head.frames; i++ )
        s->total_ms += head.duration_ms[i] > 0 ? head.duration_ms[i] : 20;
    return s;
}

void
PS_SheetFree(struct PS_Sheet* s)
{
    if( !s )
        return;
    free(s->pixels);
    free(s);
}

const uint32_t*
PS_SheetCell(
    const struct PS_Sheet* s,
    int facing,
    int frame)
{
    assert(s);
    assert(facing >= 0);
    assert(frame >= 0 && frame < s->head.frames);
    if( s->head.facings == 1 )
        facing = 0;
    assert(facing < s->head.facings);
    return s->pixels + (size_t)facing * (size_t)s->head.cell_h * (size_t)s->width +
           (size_t)frame * (size_t)s->head.cell_w;
}

int
PS_SheetFrameAt(
    const struct PS_Sheet* s,
    int elapsed_ms,
    int loop)
{
    assert(s);
    if( elapsed_ms < 0 )
        elapsed_ms = 0;
    if( loop && s->total_ms > 0 )
        elapsed_ms %= s->total_ms;
    for( int i = 0; i < s->head.frames; i++ )
    {
        int d = s->head.duration_ms[i] > 0 ? s->head.duration_ms[i] : 20;
        if( elapsed_ms < d )
            return i;
        elapsed_ms -= d;
    }
    return s->head.frames - 1;
}

int
PS_FacingFromYaw(int yaw)
{
    /* Quarter turns, rounded: 0 down, 1 left, 2 up, 3 right. */
    static const int row[4] = { PS_FACING_DOWN, PS_FACING_LEFT, PS_FACING_UP, PS_FACING_RIGHT };
    return row[((yaw & 2047) + 256) >> 9 & 3];
}
