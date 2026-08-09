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
    struct Isaac* random230 = NULL;
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

    /* Rev 239's field is the fixed/resizable window class, not the three-way
     * Display layout index.  Classic and Modern must both encode resizable. */
    for( int mode = 0; mode <= 2; mode++ )
    {
        int expected_wire_mode = mode == 0 ? 1 : 2;

        memset(packet, 0, sizeof(packet));
        length = net_out_window_status(
            GameProtoRev_OSRS239(), random, packet, (int)sizeof(packet), mode, 765, 503);
        if( length != 6 || packet[1] != expected_wire_mode || packet[2] != 0x02 ||
            packet[3] != 0xfd || packet[4] != 0x01 || packet[5] != 0xf7 )
        {
            fprintf(stderr, "WINDOW_STATUS encoded rev239 layout %d incorrectly\n", mode);
            isaac_free(random);
            return 1;
        }
    }

    /* Rev 230 uses the mock-local three-way convention. */
    random230 = isaac_new(NULL, 0);
    for( int mode = 0; mode <= 2; mode++ )
    {
        memset(packet, 0, sizeof(packet));
        length = net_out_window_status(
            GameProtoRev_OSRS230(), random230, packet, (int)sizeof(packet), mode, 765, 503);
        if( length != 6 || packet[1] != mode )
        {
            fprintf(stderr, "WINDOW_STATUS changed rev230 layout %d encoding\n", mode);
            isaac_free(random230);
            isaac_free(random);
            return 1;
        }
    }
    isaac_free(random230);
    isaac_free(random);
    puts("net-out-resume: rev239 buttons and revision-specific window status bytes passed");
    return 0;
}
