/* Auto-generated from contiguous_buffer.bin - do not edit */
#include "../src/osrs/isaac.c"
#include "../src/osrs/core/revision.h"
#include "../src/osrs/packetin.h"
#include "../src/osrs/core/packetbuffer.c"
#include "../src/osrs/rscache/rsbuf.c"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

/* Standalone tool: minimal revision dispatch for packetbuffer framing. */
static struct Revision g_contiguous_buf_rev = { REVISION_KIND_LC245_2, NULL };

const struct Revision*
revision_active(void)
{
    return &g_contiguous_buf_rev;
}

void
revision_set_active(struct Revision rev)
{
    g_contiguous_buf_rev = rev;
}

int
revision_packetin_size(const struct Revision* rev, int opcode)
{
    if( !rev )
        return 0;
    switch( rev->kind )
    {
    case REVISION_KIND_LC245_2:
        return packetin_size_lc245_2(opcode);
    case REVISION_KIND_LC254:
        return packetin_size_lc254(opcode);
    default:
        return 0;
    }
}

const unsigned char contiguous_buffer[] = {
    0x1c, 0x01, 0x82, 0x01, 0x84, 0xb4, 0x00, 0x00, 0x00, 0x50, 0x00, 0x00, 0xff, 0x1c, 0x00, 0x64,
    0x01, 0xe7, 0x10, 0x00, 0x12, 0x00, 0xdf, 0x00, 0x14, 0x00, 0xc8, 0x00, 0x15, 0x00, 0x58, 0x00,
    0x16, 0x00, 0xde, 0x00, 0x17, 0x00, 0x92, 0x00, 0x18, 0x00, 0x95, 0x00, 0x19, 0x00, 0x07, 0x00,
    0x2b, 0x00, 0x42, 0x00, 0x4b, 0x00, 0x54, 0x00, 0x4f, 0x00, 0x00, 0x00, 0x53, 0x00, 0x67, 0x00
};

const size_t contiguous_buffer_len = 64;

int
main(void)
{
    struct Isaac* isaac = NULL;
    FILE* f = fopen("login_isaac_state.bin", "rb");
    if( f )
    {
        size_t sz = isaac_state_size();
        void* buf = malloc(sz);
        if( buf && fread(buf, 1, sz, f) == sz )
            isaac = isaac_from_state(buf);
        free(buf);
        fclose(f);
    }
    uint8_t* data = contiguous_buffer;
    int offset = 0;

    struct PacketBuffer packet_buffer = { 0 };
    packetbuffer_init(&packet_buffer, isaac);
    int amnt_used =
        packetbuffer_read(&packet_buffer, data + offset, contiguous_buffer_len - offset);
    offset += amnt_used;
    if( amnt_used < contiguous_buffer_len )
    {
    }
    if( packetbuffer_ready(&packet_buffer) )
    {
        printf("packet_type: %d\n", packetbuffer_packet_type(&packet_buffer));
    }
}
