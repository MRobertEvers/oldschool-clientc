#ifndef RSPROT_PACKET_FRIENDCHAT_JOIN_LEAVE_H
#define RSPROT_PACKET_FRIENDCHAT_JOIN_LEAVE_H

/*
 * FRIENDCHAT_JOIN_LEAVE -- client -> server.
 *
 * Join the named friends chat, or (an empty payload) leave it.
 *
 * Hand-written from RSProx's decoder (protocol/osrs-<N>/.../decoder/codec);
 * the C here is the source -- no generator writes this file.
 */

#include "../include/rsprot_exec.h"

typedef struct MsgFriendchatJoinLeave {
	int32_t has_name;
	const char *name;
} MsgFriendchatJoinLeave;

/* v1 -- revs 222-241. */
void packet_friendchat_join_leave_v1_in(RsprotExec *x, MsgFriendchatJoinLeave *m);

/* osrsm241 -- the Android 241.4 client. */
void packet_friendchat_join_leave_osrsm241_in(RsprotExec *x, MsgFriendchatJoinLeave *m);

#define packet_friendchat_join_leave_osrs222_in packet_friendchat_join_leave_v1_in
#define packet_friendchat_join_leave_osrs223_in packet_friendchat_join_leave_v1_in
#define packet_friendchat_join_leave_osrs224_in packet_friendchat_join_leave_v1_in
#define packet_friendchat_join_leave_osrs225_in packet_friendchat_join_leave_v1_in
#define packet_friendchat_join_leave_osrs226_in packet_friendchat_join_leave_v1_in
#define packet_friendchat_join_leave_osrs227_in packet_friendchat_join_leave_v1_in
#define packet_friendchat_join_leave_osrs228_in packet_friendchat_join_leave_v1_in
#define packet_friendchat_join_leave_osrs229_in packet_friendchat_join_leave_v1_in
#define packet_friendchat_join_leave_osrs230_in packet_friendchat_join_leave_v1_in
#define packet_friendchat_join_leave_osrs231_in packet_friendchat_join_leave_v1_in
#define packet_friendchat_join_leave_osrs232_in packet_friendchat_join_leave_v1_in
#define packet_friendchat_join_leave_osrs233_in packet_friendchat_join_leave_v1_in
#define packet_friendchat_join_leave_osrs234_in packet_friendchat_join_leave_v1_in
#define packet_friendchat_join_leave_osrs235_in packet_friendchat_join_leave_v1_in
#define packet_friendchat_join_leave_osrs236_in packet_friendchat_join_leave_v1_in
#define packet_friendchat_join_leave_osrs237_in packet_friendchat_join_leave_v1_in
#define packet_friendchat_join_leave_osrs238_in packet_friendchat_join_leave_v1_in
#define packet_friendchat_join_leave_osrs239_in packet_friendchat_join_leave_v1_in
#define packet_friendchat_join_leave_osrs240_in packet_friendchat_join_leave_v1_in
#define packet_friendchat_join_leave_osrs241_in packet_friendchat_join_leave_v1_in
#define packet_friendchat_join_leave_osrsm241_in packet_friendchat_join_leave_osrsm241_in

/** Revision -> layout. See the .c for the rows. */
extern const RsprotVersionRange rsprot_friendchat_join_leave_in[];
extern const int rsprot_friendchat_join_leave_in_count;

#endif
