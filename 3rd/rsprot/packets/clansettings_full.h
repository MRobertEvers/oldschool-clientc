#ifndef RSPROT_PACKET_CLANSETTINGS_FULL_H
#define RSPROT_PACKET_CLANSETTINGS_FULL_H

/*
 * CLANSETTINGS_FULL -- server -> client.
 *
 * The whole clan settings, or (nothing after the clan type) leaving them.
 *
 * `flags`: 0x1 members by name hash, 0x2 by display name -- exactly one.
 * By version: 2 adds each member's extra info, 3 the settings list, 4 four
 * skipped bytes after the clan name, 5 each member's join day, 6 muted.
 * A setting's header holds its id (low 30 bits) and type (top 2): 0 an
 * int, 1 a long, 2 a string.
 *
 * Hand-written from RSProx's decoder (protocol/osrs-<N>/.../decoder/codec);
 * the C here is the source -- no generator writes this file.
 */

#include "../include/rsprot_exec.h"

#define MSG_CLANSETTINGS_FULL_MAX_MEMBERS 2048
#define MSG_CLANSETTINGS_FULL_MAX_BANNED 255
#define MSG_CLANSETTINGS_FULL_MAX_SETTINGS 2048

typedef struct MsgClansettingsFullMember {
	int64_t hash;
	const char *display_name;
	int32_t rank;
	int32_t extra_info;
	int32_t join_rune_day;
	int32_t muted;
} MsgClansettingsFullMember;

typedef struct MsgClansettingsFullBanned {
	int64_t hash;
	const char *display_name;
} MsgClansettingsFullBanned;

typedef struct MsgClansettingsFullSetting {
	int32_t header;
	int32_t ival;
	int64_t lval;
	const char *sval;
} MsgClansettingsFullSetting;

typedef struct MsgClansettingsFull {
	int32_t clan_type;
	/** 0: the clan type alone -- the settings were left. */
	int32_t joined;
	int32_t version;
	int32_t flags;
	int32_t update_num;
	int32_t creation_time;
	int32_t affined_member_count;
	int32_t banned_member_count;
	const char *clan_name;
	int32_t skip4;
	int32_t allow_unaffined;
	int32_t talk_rank;
	int32_t kick_rank;
	int32_t lootshare_rank;
	int32_t coinshare_rank;
	MsgClansettingsFullMember members[MSG_CLANSETTINGS_FULL_MAX_MEMBERS];
	MsgClansettingsFullBanned banned[MSG_CLANSETTINGS_FULL_MAX_BANNED];
	int32_t setting_count;
	MsgClansettingsFullSetting settings[MSG_CLANSETTINGS_FULL_MAX_SETTINGS];
} MsgClansettingsFull;

/* v1 -- revs 222-241. */
void packet_clansettings_full_v1_out(RsprotExec *x, MsgClansettingsFull *m);

#define packet_clansettings_full_osrs222_out packet_clansettings_full_v1_out
#define packet_clansettings_full_osrs223_out packet_clansettings_full_v1_out
#define packet_clansettings_full_osrs224_out packet_clansettings_full_v1_out
#define packet_clansettings_full_osrs225_out packet_clansettings_full_v1_out
#define packet_clansettings_full_osrs226_out packet_clansettings_full_v1_out
#define packet_clansettings_full_osrs227_out packet_clansettings_full_v1_out
#define packet_clansettings_full_osrs228_out packet_clansettings_full_v1_out
#define packet_clansettings_full_osrs229_out packet_clansettings_full_v1_out
#define packet_clansettings_full_osrs230_out packet_clansettings_full_v1_out
#define packet_clansettings_full_osrs231_out packet_clansettings_full_v1_out
#define packet_clansettings_full_osrs232_out packet_clansettings_full_v1_out
#define packet_clansettings_full_osrs233_out packet_clansettings_full_v1_out
#define packet_clansettings_full_osrs234_out packet_clansettings_full_v1_out
#define packet_clansettings_full_osrs235_out packet_clansettings_full_v1_out
#define packet_clansettings_full_osrs236_out packet_clansettings_full_v1_out
#define packet_clansettings_full_osrs237_out packet_clansettings_full_v1_out
#define packet_clansettings_full_osrs238_out packet_clansettings_full_v1_out
#define packet_clansettings_full_osrs239_out packet_clansettings_full_v1_out
#define packet_clansettings_full_osrs240_out packet_clansettings_full_v1_out
#define packet_clansettings_full_osrs241_out packet_clansettings_full_v1_out

/** Revision -> layout. See the .c for the rows. */
extern const RsprotVersionRange rsprot_clansettings_full_out[];
extern const int rsprot_clansettings_full_out_count;

#endif
