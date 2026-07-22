#include "net_out.h"

#include "rev/lc245_2/packetout.h"
#include "wordpack.h"

#include <rsbuffer.h>
#include <string.h>

int
net_out_opcode(
    struct Isaac* random_out,
    uint8_t* buf,
    int wire_opcode)
{
    buf[0] = (uint8_t)((wire_opcode + isaac_next(random_out)) & 0xff);
    return 1;
}

int
net_out_move_gameclick(
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int abs_x,
    int abs_z,
    int ctrl_held)
{
    /* Single-step walk: reference MOVE_GAMECLICK with one waypoint. Payload
     * length = 2*count + 3; count 1 -> 5 body bytes. */
    struct RSCache_Buffer b;
    if( cap < 8 )
        return -1;
    net_out_opcode(random_out, buf, PKTOUT_LC245_2_MOVE_GAMECLICK);
    RSCache_BufferInit(&b, buf + 1, (uint32_t)(cap - 1));
    p1(&b, 2 * 1 + 3);       /* length byte */
    p2(&b, abs_x);           /* base tile x */
    p2(&b, abs_z);           /* base tile z */
    p1(&b, ctrl_held ? 1 : 0);
    p1(&b, 0);               /* dx of waypoint 0 (this tile) */
    p1(&b, 0);               /* dz of waypoint 0 */
    return 1 + (int)b.position;
}

int
net_out_if_button(
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int component_id)
{
    struct RSCache_Buffer b;
    if( cap < 3 )
        return -1;
    net_out_opcode(random_out, buf, PKTOUT_LC245_2_IF_BUTTON);
    RSCache_BufferInit(&b, buf + 1, (uint32_t)(cap - 1));
    p2(&b, component_id);
    return 1 + (int)b.position;
}

int
net_out_resume_pausebutton(
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int component_id)
{
    struct RSCache_Buffer b;
    if( cap < 3 )
        return -1;
    net_out_opcode(random_out, buf, PKTOUT_LC245_2_RESUME_PAUSEBUTTON);
    RSCache_BufferInit(&b, buf + 1, (uint32_t)(cap - 1));
    p2(&b, component_id);
    return 1 + (int)b.position;
}

int
net_out_message_public(
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    char const* text)
{
    /* MESSAGE_PUBLIC: var-u8 length, body = colour(0) effect(0) wordpacked. */
    struct RSCache_Buffer b;
    int len_pos;
    int start;
    if( cap < 8 || !text )
        return -1;
    net_out_opcode(random_out, buf, PKTOUT_LC245_2_MESSAGE_PUBLIC);
    RSCache_BufferInit(&b, buf + 1, (uint32_t)(cap - 1));
    p1(&b, 0); /* length placeholder */
    len_pos = (int)b.position - 1;
    start = (int)b.position;
    p1(&b, 0); /* colour */
    p1(&b, 0); /* effect */
    wordpack_pack(&b, text);
    buf[1 + len_pos] = (uint8_t)((int)b.position - start);
    return 1 + (int)b.position;
}

int
net_out_chat_setmode(
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int public_mode,
    int private_mode,
    int trade_mode)
{
    struct RSCache_Buffer b;
    if( cap < 4 )
        return -1;
    net_out_opcode(random_out, buf, PKTOUT_LC245_2_CHAT_SETMODE);
    RSCache_BufferInit(&b, buf + 1, (uint32_t)(cap - 1));
    p1(&b, public_mode);
    p1(&b, private_mode);
    p1(&b, trade_mode);
    return 1 + (int)b.position;
}

int
net_out_no_timeout(
    struct Isaac* random_out,
    uint8_t* buf,
    int cap)
{
    if( cap < 1 )
        return -1;
    net_out_opcode(random_out, buf, PKTOUT_LC245_2_NO_TIMEOUT);
    return 1;
}
