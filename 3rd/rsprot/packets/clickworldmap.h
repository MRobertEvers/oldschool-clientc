#ifndef RSPROT_PACKET_CLICKWORLDMAP_H
#define RSPROT_PACKET_CLICKWORLDMAP_H

/*
 * CLICKWORLDMAP -- client -> server.
 *
 * A world map click: the packed coord (level << 28 | x << 14 | z).
 *
 * Hand-written from RSProx's decoder (protocol/osrs-<N>/.../decoder/codec);
 * the C here is the source -- no generator writes this file.
 */

#include "../include/rsprot_exec.h"

typedef struct MsgClickworldmap {
	int32_t packed;
} MsgClickworldmap;

/* v1 -- revs 223, 227-229, 231, 241. */
void packet_clickworldmap_v1_in(RsprotExec *x, MsgClickworldmap *m);
/* v2 -- revs 224, 226, 230, 235, 238. */
void packet_clickworldmap_v2_in(RsprotExec *x, MsgClickworldmap *m);
/* v3 -- revs 222, 225, 233, 236-237. */
void packet_clickworldmap_v3_in(RsprotExec *x, MsgClickworldmap *m);
/* v4 -- revs 232, 234, 239-240. */
void packet_clickworldmap_v4_in(RsprotExec *x, MsgClickworldmap *m);

/* osrsm241 -- the Android 241.4 client. */
void packet_clickworldmap_osrsm241_in(RsprotExec *x, MsgClickworldmap *m);

#define packet_clickworldmap_osrs222_in packet_clickworldmap_v3_in
#define packet_clickworldmap_osrs223_in packet_clickworldmap_v1_in
#define packet_clickworldmap_osrs224_in packet_clickworldmap_v2_in
#define packet_clickworldmap_osrs225_in packet_clickworldmap_v3_in
#define packet_clickworldmap_osrs226_in packet_clickworldmap_v2_in
#define packet_clickworldmap_osrs227_in packet_clickworldmap_v1_in
#define packet_clickworldmap_osrs228_in packet_clickworldmap_v1_in
#define packet_clickworldmap_osrs229_in packet_clickworldmap_v1_in
#define packet_clickworldmap_osrs230_in packet_clickworldmap_v2_in
#define packet_clickworldmap_osrs231_in packet_clickworldmap_v1_in
#define packet_clickworldmap_osrs232_in packet_clickworldmap_v4_in
#define packet_clickworldmap_osrs233_in packet_clickworldmap_v3_in
#define packet_clickworldmap_osrs234_in packet_clickworldmap_v4_in
#define packet_clickworldmap_osrs235_in packet_clickworldmap_v2_in
#define packet_clickworldmap_osrs236_in packet_clickworldmap_v3_in
#define packet_clickworldmap_osrs237_in packet_clickworldmap_v3_in
#define packet_clickworldmap_osrs238_in packet_clickworldmap_v2_in
#define packet_clickworldmap_osrs239_in packet_clickworldmap_v4_in
#define packet_clickworldmap_osrs240_in packet_clickworldmap_v4_in
#define packet_clickworldmap_osrs241_in packet_clickworldmap_v1_in
#define packet_clickworldmap_osrsm241_in packet_clickworldmap_osrsm241_in

/** Revision -> layout. See the .c for the rows. */
extern const RsprotVersionRange rsprot_clickworldmap_in[];
extern const int rsprot_clickworldmap_in_count;

#endif
