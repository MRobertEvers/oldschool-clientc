#ifndef SRC_CMD_CMDRING_H
#define SRC_CMD_CMDRING_H

#include <stdint.h>

/* 128KB: must hold a frame's worth of input commands plus several socket
 * recv chunks (<= TORIRS_CMD_MAX_PAYLOAD each) between drains. */
#define TORIRS_CMDRING_SIZE (128 * 1024)

/* Single-message payload cap. Socket producers read at most 4096 bytes per
 * recv, so this leaves headroom without letting one message starve the ring. */
#define TORIRS_CMD_MAX_PAYLOAD 8192

#pragma pack(push, 1)
struct ToriRS_CmdHeader
{
    uint32_t type;
    uint16_t length;
};
#pragma pack(pop)

/* SPSC ring of [header][payload] frames. The byte layout of the ring content
 * is also the on-disk record format (see cmdbus.h), so anything pushed here
 * is serializable by construction. */
struct ToriRS_CmdRing
{
    uint8_t data[TORIRS_CMDRING_SIZE];
    uint32_t head;
    uint32_t tail;
};

void
CmdRing_Init(struct ToriRS_CmdRing* ring);

/* Returns 1 on success, 0 when the message would not fit (never partial). */
int
CmdRing_Push(
    struct ToriRS_CmdRing* ring,
    uint32_t type,
    const uint8_t* payload,
    uint16_t length);

/* out_payload must hold TORIRS_CMD_MAX_PAYLOAD bytes. Returns 1 when a
 * message was popped, 0 when the ring is empty. */
int
CmdRing_Pop(
    struct ToriRS_CmdRing* ring,
    struct ToriRS_CmdHeader* out_header,
    uint8_t* out_payload);

int
CmdRing_IsEmpty(const struct ToriRS_CmdRing* ring);

/* Bytes the ring can still accept, headers included. */
uint32_t
CmdRing_FreeBytes(const struct ToriRS_CmdRing* ring);

/* Would a message of `length` bytes fit right now?
 *
 * A producer that cannot ask this can only discover a full ring by having its
 * push refused, and a socket producer refused mid-stream has already consumed
 * the bytes from the kernel: the drop is silent and the byte stream behind it
 * is truncated mid-packet. Asking first turns "drop" into "leave it in the
 * socket until the next drain", which is the only form back-pressure can take
 * when the ring is fixed. */
int
CmdRing_CanPush(
    const struct ToriRS_CmdRing* ring,
    uint16_t length);

#endif
