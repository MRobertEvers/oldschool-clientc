#ifndef RSPROT_PACKET_OBJ_CUSTOMISE_SPECIFIC_V2_H
#define RSPROT_PACKET_OBJ_CUSTOMISE_SPECIFIC_V2_H

/*
 * OBJ_CUSTOMISE_SPECIFIC_V2 -- server -> client.
 *
 * Hand-written from RSProx (ObjCustomiseSpecificV2Decoder, protocol/osrs-<N>/.../decoder/codec/specific/ObjCustomiseSpecificV2Decoder.kt).
 * RSProt has no codec for this prot, and the RSProx decoder carries two counted lists and an optional colour, which
 * tools/rsprox_gen_codec.py does not translate -- it lists this file in HAND and
 * never overwrites it. Field names are RSProx's. Version numbers are local to
 * this file and start at 100, the RSProx numbering (gen/rsprox_versions.txt).
 */

#include "../include/rsprot_exec.h"

#define MSG_OBJ_CUSTOMISE_SPECIFIC_V2_MAX_REPLACEMENTS 255

typedef struct MsgObjReplacement {
	int32_t index;
	int32_t value;
} MsgObjReplacement;

typedef struct MsgObjCustomiseSpecificV2 {
	int32_t packed_coord;
	int32_t id;
	int32_t model_id; /* 65535 = none */
	int32_t retexture_count;
	MsgObjReplacement retextures[MSG_OBJ_CUSTOMISE_SPECIFIC_V2_MAX_REPLACEMENTS];
	int32_t quantity;
	int32_t recolour_count;
	MsgObjReplacement recolours[MSG_OBJ_CUSTOMISE_SPECIFIC_V2_MAX_REPLACEMENTS];
	int32_t has_colour;
	int32_t colour;
} MsgObjCustomiseSpecificV2;

/* v100 -- revs 241. */
void packet_obj_customise_specific_v2_v100_out(RsprotExec *x, MsgObjCustomiseSpecificV2 *m);

#define packet_obj_customise_specific_v2_rev241_out packet_obj_customise_specific_v2_v100_out

/** Revision -> layout. See the .c for the rows. */
extern const RsprotVersionRange rsprot_obj_customise_specific_v2_out[];
extern const int rsprot_obj_customise_specific_v2_out_count;

#endif
