#include "cmdring.h"

#include <string.h>

void
CmdRing_Init(struct ToriRS_CmdRing* ring)
{
    ring->head = 0;
    ring->tail = 0;
}

static uint32_t
free_space(const struct ToriRS_CmdRing* ring)
{
    if( ring->head >= ring->tail )
        return TORIRS_CMDRING_SIZE - (ring->head - ring->tail) - 1;
    return ring->tail - ring->head - 1;
}

static void
ring_write(
    struct ToriRS_CmdRing* ring,
    const uint8_t* src,
    uint32_t len)
{
    uint32_t space_to_end = TORIRS_CMDRING_SIZE - ring->head;
    if( len <= space_to_end )
    {
        memcpy(&ring->data[ring->head], src, len);
    }
    else
    {
        memcpy(&ring->data[ring->head], src, space_to_end);
        memcpy(&ring->data[0], src + space_to_end, len - space_to_end);
    }
    ring->head = (ring->head + len) % TORIRS_CMDRING_SIZE;
}

static void
ring_read(
    struct ToriRS_CmdRing* ring,
    uint8_t* dest,
    uint32_t len)
{
    uint32_t data_to_end = TORIRS_CMDRING_SIZE - ring->tail;
    if( len <= data_to_end )
    {
        memcpy(dest, &ring->data[ring->tail], len);
    }
    else
    {
        memcpy(dest, &ring->data[ring->tail], data_to_end);
        memcpy(dest + data_to_end, &ring->data[0], len - data_to_end);
    }
    ring->tail = (ring->tail + len) % TORIRS_CMDRING_SIZE;
}

int
CmdRing_Push(
    struct ToriRS_CmdRing* ring,
    uint32_t type,
    const uint8_t* payload,
    uint16_t length)
{
    uint32_t total = sizeof(struct ToriRS_CmdHeader) + length;

    if( length > TORIRS_CMD_MAX_PAYLOAD )
        return 0;
    if( free_space(ring) < total )
        return 0;

    struct ToriRS_CmdHeader header = { type, length };
    ring_write(ring, (const uint8_t*)&header, sizeof(header));
    if( length > 0 && payload )
        ring_write(ring, payload, length);
    return 1;
}

int
CmdRing_Pop(
    struct ToriRS_CmdRing* ring,
    struct ToriRS_CmdHeader* out_header,
    uint8_t* out_payload)
{
    if( ring->head == ring->tail )
        return 0;

    ring_read(ring, (uint8_t*)out_header, sizeof(*out_header));
    if( out_header->length > 0 )
        ring_read(ring, out_payload, out_header->length);
    return 1;
}

int
CmdRing_IsEmpty(const struct ToriRS_CmdRing* ring)
{
    return ring->head == ring->tail;
}
