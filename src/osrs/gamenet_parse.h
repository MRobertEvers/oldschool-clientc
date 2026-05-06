#ifndef OSRS_GAMENET_PARSE_H
#define OSRS_GAMENET_PARSE_H

/* gamenet_parse — inbound listener layer.
 *
 * Pulls one fully-buffered packet from PacketBuffer (ISAAC-decrypted opcode +
 * accumulated payload) and dispatches to the appropriate revision-specific
 * parser (serverprot_netrevX_parse), which deserialises bytes into a typed
 * RevPacket struct and enqueues it for Lua/exec processing.
 *
 * Replaces the inline net_process_packets() block in tori_rs_net.u.c.
 */

struct GGame;

void
gamenet_parse(struct GGame* game);

#endif /* OSRS_GAMENET_PARSE_H */
