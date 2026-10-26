/* See proto_out.h -- client packets, framed around the revision's encode. */
#include "proto_out.h"

#include <assert.h>

int
proto_out_speaks(struct GameProtoRevTable const* rev)
{
    assert(rev);
    return rev->encode != NULL;
}

int
proto_out_has(struct GameProtoRevTable const* rev, int out_name)
{
    assert(rev);
    assert(rev->encodes);
    return rev->packetout_code(out_name) >= 0 && rev->encodes(out_name);
}

int
proto_encode(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int out_name,
    struct ProtoOutArgs const* args)
{
    int wire;
    int size;
    int head;
    int len;

    assert(rev);
    assert(random_out);
    assert(buf);
    assert(args);
    assert(rev->encode);
    wire = rev->packetout_code(out_name);
    if( wire < 0 )
        return -1;
    size = rev->packetout_size(wire);
    head = size == PKTIN_LENGTH_VARU8 ? 1 : size == PKTIN_LENGTH_VARU16 ? 2 : 0;
    if( cap < 1 + head )
        return -1;
    /* The revision's executor runs the prot's codec (src/net/rev/<rev>/). */
    len = rev->encode(out_name, args, buf + 1 + head, cap - 1 - head);
    if( len < 0 )
        return -1;
    if( head == 0 )
        assert(len == size);
    else if( head == 1 )
    {
        if( len > 0xFF )
            return -1;
        buf[1] = (uint8_t)len;
    }
    else
    {
        if( len > 0xFFFF )
            return -1;
        buf[1] = (uint8_t)(len >> 8);
        buf[2] = (uint8_t)len;
    }
    buf[0] = (uint8_t)((wire + isaac_next(random_out)) & 0xff);
    return 1 + head + len;
}
