#include "osrs241_clientprot.h"

#include "rsprot_buf.h"

#include <assert.h>
#include <stddef.h>

/*
 * The byte transforms are rsprot_buf's, not restated here: `G2_ALT3` is
 * rsprot_p2_alt3 / rsprot_g2_alt3, the same functions every generated codec
 * runs, so a 241 client prot and a 241 server prot mean the same thing by
 * "Alt3".
 */

typedef void (*cp_put_fn)(RsprotBuf*, int32_t);
typedef int32_t (*cp_get_fn)(RsprotBuf*);

static const struct
{
    int width;
    cp_put_fn put;
    cp_get_fn get;
} k_access[] = {
    [OSRS241_CP_G1] = { 1, rsprot_p1, rsprot_g1 },
    [OSRS241_CP_G1_ALT1] = { 1, rsprot_p1_alt1, rsprot_g1_alt1 },
    [OSRS241_CP_G1_ALT2] = { 1, rsprot_p1_alt2, rsprot_g1_alt2 },
    [OSRS241_CP_G1_ALT3] = { 1, rsprot_p1_alt3, rsprot_g1_alt3 },
    [OSRS241_CP_G2] = { 2, rsprot_p2, rsprot_g2 },
    [OSRS241_CP_G2_ALT1] = { 2, rsprot_p2_alt1, rsprot_g2_alt1 },
    [OSRS241_CP_G2_ALT2] = { 2, rsprot_p2_alt2, rsprot_g2_alt2 },
    [OSRS241_CP_G2_ALT3] = { 2, rsprot_p2_alt3, rsprot_g2_alt3 },
    [OSRS241_CP_G3] = { 3, rsprot_p3, rsprot_g3 },
    [OSRS241_CP_G3_ALT1] = { 3, rsprot_p3_alt1, rsprot_g3_alt1 },
    [OSRS241_CP_G3_ALT2] = { 3, rsprot_p3_alt2, rsprot_g3_alt2 },
    [OSRS241_CP_G3_ALT3] = { 3, rsprot_p3_alt3, rsprot_g3_alt3 },
    [OSRS241_CP_G4] = { 4, rsprot_p4, rsprot_g4 },
    [OSRS241_CP_G4_ALT1] = { 4, rsprot_p4_alt1, rsprot_g4_alt1 },
    [OSRS241_CP_G4_ALT2] = { 4, rsprot_p4_alt2, rsprot_g4_alt2 },
    [OSRS241_CP_G4_ALT3] = { 4, rsprot_p4_alt3, rsprot_g4_alt3 },
};

int
osrs241_cp_bytes(struct Osrs241CpLayout const* layout)
{
    int n = 0;

    assert(layout);
    for( int i = 0; i < layout->field_count; i++ )
    {
        assert(layout->fields[i].access < sizeof(k_access) / sizeof(k_access[0]));
        n += k_access[layout->fields[i].access].width;
    }
    return n;
}

int
osrs241_cp_write(
    struct Osrs241CpLayout const* layout,
    int32_t const vals[OSRS241_CP_ROLE_COUNT],
    uint8_t* out,
    int cap)
{
    RsprotBuf b;

    assert(layout);
    assert(vals);
    assert(out);
    assert(osrs241_cp_bytes(layout) <= cap);
    rsprot_buf_wrap(&b, out, cap);
    for( int i = 0; i < layout->field_count; i++ )
    {
        struct Osrs241CpField f = layout->fields[i];
        assert(f.role < OSRS241_CP_ROLE_COUNT);
        k_access[f.access].put(&b, vals[f.role]);
    }
    assert(!b.err);
    return b.wpos;
}

int
osrs241_cp_read(
    struct Osrs241CpLayout const* layout,
    uint8_t const* in,
    int len,
    int32_t vals[OSRS241_CP_ROLE_COUNT])
{
    RsprotBuf b;

    assert(layout);
    assert(in || len == 0);
    assert(vals);
    if( len < osrs241_cp_bytes(layout) )
        return -1;
    rsprot_buf_wrap_read(&b, in, len);
    for( int i = 0; i < layout->field_count; i++ )
    {
        struct Osrs241CpField f = layout->fields[i];
        assert(f.role < OSRS241_CP_ROLE_COUNT);
        vals[f.role] = k_access[f.access].get(&b);
    }
    return b.err ? -1 : b.rpos;
}
