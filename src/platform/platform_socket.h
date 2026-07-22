#ifndef SRC_PLATFORM_PLATFORM_SOCKET_H
#define SRC_PLATFORM_PLATFORM_SOCKET_H

/*
 * Socket transport bridging ToriRS_Network to a real TCP connection (adapted
 * from v0 LibToriRSPlatformC_NetPoll): drains the subsystem's outbound ring
 * (CONNECT opens the socket, SEND_DATA writes it) and pushes received bytes
 * and status changes onto the command bus as TORIRS_CMD_NET_* commands — the
 * same shape the loopback mock produces, so the subsystem is transport-blind.
 */

#include <stdint.h>

struct PlatformSocket;
struct ToriRS_Network;
struct ToriRS_CmdBus;

struct PlatformSocket*
PlatformSocket_New(int default_port);

void
PlatformSocket_Free(struct PlatformSocket* sock);

/**
 * One poll step: drain net->out (connect/send), pump the pending connect,
 * read available bytes -> NET_RECV, and emit NET_STATUS on state changes.
 */
void
PlatformSocket_Poll(
    struct PlatformSocket* sock,
    struct ToriRS_Network* net,
    struct ToriRS_CmdBus* bus);

#endif
