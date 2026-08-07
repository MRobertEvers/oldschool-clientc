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
    isaac_free(random);
    puts("net-out-resume: rev239 uid + sub-id bytes passed");
    return 0;
}
