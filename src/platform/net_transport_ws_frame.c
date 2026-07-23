#include "net_transport_ws_frame.h"

/* Cap a single inbound frame at 16 MiB — larger is treated as malformed rather
 * than trusted (guards against a bogus 64-bit length). */
#define WS_FRAME_MAX_PAYLOAD (16 * 1024 * 1024)

int
ws_frame_encode(
    int opcode,
    uint8_t const* payload,
    int payload_len,
    uint8_t const mask[4],
    uint8_t* out,
    int out_cap)
{
    int header;
    int i;

    if( payload_len < 0 )
        return -1;

    /* byte0: FIN + opcode. byte1: MASK + length code. */
    if( payload_len < 126 )
        header = 2;
    else if( payload_len < 65536 )
        header = 4;
    else
        header = 10;

    int total = header + 4 /* mask key */ + payload_len;
    if( total > out_cap )
        return -1;

    out[0] = (uint8_t)(0x80 | (opcode & 0x0f));
    if( payload_len < 126 )
    {
        out[1] = (uint8_t)(0x80 | payload_len);
    }
    else if( payload_len < 65536 )
    {
        out[1] = (uint8_t)(0x80 | 126);
        out[2] = (uint8_t)((payload_len >> 8) & 0xff);
        out[3] = (uint8_t)(payload_len & 0xff);
    }
    else
    {
        out[1] = (uint8_t)(0x80 | 127);
        /* 64-bit BE length; our cap keeps the high 32 bits zero. */
        out[2] = out[3] = out[4] = out[5] = 0;
        out[6] = (uint8_t)((payload_len >> 24) & 0xff);
        out[7] = (uint8_t)((payload_len >> 16) & 0xff);
        out[8] = (uint8_t)((payload_len >> 8) & 0xff);
        out[9] = (uint8_t)(payload_len & 0xff);
    }

    for( i = 0; i < 4; i++ )
        out[header + i] = mask[i];

    for( i = 0; i < payload_len; i++ )
        out[header + 4 + i] = (uint8_t)(payload[i] ^ mask[i & 3]);

    return total;
}

enum WsDecodeStatus
ws_frame_decode(uint8_t* buf, int len, struct WsFrame* frame, int* consumed)
{
    int header = 2;
    int masked;
    int64_t payload_len;
    uint8_t const* mask;
    int i;

    *consumed = 0;
    if( len < 2 )
        return WS_DECODE_INCOMPLETE;

    int len7 = buf[1] & 0x7f;
    masked = (buf[1] & 0x80) != 0;

    if( len7 < 126 )
    {
        payload_len = len7;
        header = 2;
    }
    else if( len7 == 126 )
    {
        if( len < 4 )
            return WS_DECODE_INCOMPLETE;
        payload_len = ((int64_t)buf[2] << 8) | buf[3];
        header = 4;
    }
    else /* 127 */
    {
        if( len < 10 )
            return WS_DECODE_INCOMPLETE;
        payload_len = 0;
        for( i = 0; i < 8; i++ )
            payload_len = (payload_len << 8) | buf[2 + i];
        header = 10;
    }

    if( payload_len < 0 || payload_len > WS_FRAME_MAX_PAYLOAD )
        return WS_DECODE_ERROR;

    int mask_bytes = masked ? 4 : 0;
    int64_t total = (int64_t)header + mask_bytes + payload_len;
    if( (int64_t)len < total )
        return WS_DECODE_INCOMPLETE;

    if( masked )
    {
        mask = buf + header;
        uint8_t* p = buf + header + 4;
        for( i = 0; i < payload_len; i++ )
            p[i] ^= mask[i & 3];
        frame->payload = p;
    }
    else
    {
        frame->payload = buf + header;
    }

    frame->fin = (buf[0] & 0x80) != 0;
    frame->opcode = buf[0] & 0x0f;
    frame->payload_len = (int)payload_len;
    *consumed = (int)total;
    return WS_DECODE_OK;
}
