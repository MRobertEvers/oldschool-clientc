#include "net/isaac.h"
#include "net/net_out.h"
#include "net/rev/gameproto_revisions.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

int
main(void)
{
    static const uint8_t expected_payload[] = { 0x34, 0x12, 0x78, 0x56, 0x00, 0x07 };
    static const uint8_t expected_obj_op[] = {
        0x00, 0x95, 0x00, 0x00, 0x00, 0x03, 0x02, 0x35, 0x07
    };
    uint8_t packet[16] = { 0 };
    struct Isaac* random = isaac_new(NULL, 0);
    int length = net_out_resume_pausebutton(
        GameProtoRev_OSRS239(), random, packet, (int)sizeof(packet), 0x12345678, 7);

    if( length != 7 || memcmp(packet + 1, expected_payload, sizeof(expected_payload)) != 0 )
    {
        fprintf(stderr, "RESUME_PAUSEBUTTON did not preserve its dynamic sub-id\n");
        isaac_free(random);
        return 1;
    }
    memset(packet, 0, sizeof(packet));
    length = net_out_if_button_obj_op(
        GameProtoRev_OSRS239(), random, packet, (int)sizeof(packet),
        7, 149 << 16, 3, 565);
    if( length != 10 || memcmp(packet + 1, expected_obj_op, sizeof(expected_obj_op)) != 0 )
    {
        fprintf(stderr, "IF_BUTTONX did not preserve its inventory object id\n");
        isaac_free(random);
        return 1;
    }
    isaac_free(random);
    puts("net-out-resume: rev239 resume and object-backed IF_BUTTONX bytes passed");
    return 0;
}
