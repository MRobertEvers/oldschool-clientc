#include "pokesprite_format.h"

#include <assert.h>
#include <string.h>

struct Reader
{
    const uint8_t* p;
    int left;
};

static bool
take(
    struct Reader* r,
    int n,
    const uint8_t** out)
{
    if( r->left < n )
        return false;
    *out = r->p;
    r->p += n;
    r->left -= n;
    return true;
}

static bool
u8(
    struct Reader* r,
    int* out)
{
    const uint8_t* b;
    if( !take(r, 1, &b) )
        return false;
    *out = b[0];
    return true;
}

static bool
u16(
    struct Reader* r,
    int* out)
{
    const uint8_t* b;
    if( !take(r, 2, &b) )
        return false;
    *out = b[0] | (b[1] << 8);
    return true;
}

static bool
i16(
    struct Reader* r,
    int* out)
{
    int v;
    if( !u16(r, &v) )
        return false;
    *out = v >= 0x8000 ? v - 0x10000 : v;
    return true;
}

static bool
u32(
    struct Reader* r,
    int* out)
{
    const uint8_t* b;
    if( !take(r, 4, &b) )
        return false;
    uint32_t v = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) |
                 ((uint32_t)b[3] << 24);
    if( v > 0x7FFFFFFFu )
        return false;
    *out = (int)v;
    return true;
}

bool
PokeSprite_ParseSheet(
    const uint8_t* data,
    int size,
    struct PokeSpriteSheet* out)
{
    assert(data);
    assert(out);
    memset(out, 0, sizeof(*out));

    struct Reader r = { data, size };
    const uint8_t* magic;
    int kind;
    if( !take(&r, 4, &magic) || memcmp(magic, POKESPRITE_MAGIC, 4) != 0 )
        return false;
    if( !u8(&r, &kind) || (kind != POKESPRITE_KIND_RGBA && kind != POKESPRITE_KIND_LAYER) )
        return false;
    out->kind = (enum PokeSpriteKind)kind;
    if( !u8(&r, &out->facings) || (out->facings != 1 && out->facings != 4) )
        return false;
    if( !u16(&r, &out->frames) || out->frames < 1 || out->frames > POKESPRITE_MAX_FRAMES )
        return false;
    if( !u16(&r, &out->cell_w) || !u16(&r, &out->cell_h) || out->cell_w < 1 || out->cell_h < 1 )
        return false;
    if( !i16(&r, &out->origin_x) || !i16(&r, &out->origin_y) || !i16(&r, &out->ground_x) ||
        !i16(&r, &out->ground_y) )
        return false;
    for( int i = 0; i < out->frames; i++ )
    {
        int d;
        if( !u16(&r, &d) )
            return false;
        out->duration_ms[i] = (uint16_t)d;
    }
    if( !u8(&r, &out->material_count) || out->material_count > POKESPRITE_MAX_MATERIALS )
        return false;
    if( out->kind == POKESPRITE_KIND_RGBA && out->material_count != 0 )
        return false;
    for( int m = 0; m < out->material_count; m++ )
    {
        struct PokeSpriteMaterial* mat = &out->materials[m];
        const uint8_t* rgb;
        int steps, flags, n;
        if( !take(&r, 3, &rgb) || !u8(&r, &steps) || !u8(&r, &flags) || !u8(&r, &n) )
            return false;
        if( steps < 1 || steps > 3 || n > POKESPRITE_MAX_MATERIAL_HSL )
            return false;
        memcpy(mat->base_rgb, rgb, 3);
        mat->steps = (uint8_t)steps;
        mat->flags = (uint8_t)flags;
        mat->hsl16_count = (uint8_t)n;
        for( int h = 0; h < n; h++ )
        {
            int v;
            if( !u16(&r, &v) )
                return false;
            mat->hsl16[h] = (uint16_t)v;
        }
    }
    if( !u32(&r, &out->png_size) || out->png_size < 8 || out->png_size != r.left )
        return false;
    out->png = r.p;
    return true;
}

bool
PokeSprite_KeyValid(const char* key)
{
    assert(key);
    int n = (int)strlen(key);
    if( n < 1 || n > 127 || key[0] == '/' || key[n - 1] == '/' )
        return false;
    for( int i = 0; i < n; i++ )
    {
        char c = key[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
                  c == '/';
        if( !ok )
            return false;
        if( c == '/' && key[i + 1] == '/' )
            return false;
    }
    /* Only [a-z0-9_/-] remain, so ".." cannot occur; '.' is not allowed at all. */
    return true;
}

int
PokeSprite_FramesPerRow(
    int cell_w,
    int frames)
{
    assert(cell_w > 0);
    assert(frames > 0);
    if( cell_w * frames <= POKESPRITE_MAX_ROW_PX )
        return frames;
    int per_row = POKESPRITE_MAX_ROW_PX / cell_w;
    return per_row > 0 ? per_row : 1;
}
