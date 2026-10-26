#ifndef SRC_NET_PROTO_PROTO_OUT_H
#define SRC_NET_PROTO_PROTO_OUT_H

/*
 * Client -> server packets, written by RSProt's codecs.
 *
 * A packet is described by what it means, in RSProt's own field names
 * (struct ProtoOutArgs): the tile and id an op targets, the component triple a
 * use-on carries, the text a chat line sends. proto_encode asks the revision's
 * executor (src/net/rev/<rev>/<rev>_exec.c, its `encode` slot) to fill the
 * prot's message from the arguments (w_<file>, proto_out_writers.h) and run the
 * `_in` codec that revision carries, then frames it: the ISAAC-scrambled
 * opcode, then the length the prot table's size says. Field order, byte
 * transforms and which version a revision sends are all RSProt's; nothing here
 * knows a revision.
 *
 * The one thing the arguments do not decide is what a packet's text or tail
 * bytes are: a chat line's packed text, a script trigger's arguments. The
 * caller builds those (`huffman`, `copy`) as the revision encodes them.
 */

#include "net/isaac.h"
#include "net/rev/gameproto_revisions.h"

#include <stdint.h>

struct ProtoOutArgs
{
    /* A tile, and what is on it. */
    int32_t x;
    int32_t z;
    int32_t id;
    int32_t index; /* an npc's or player's slot */
    int32_t control_key;
    int32_t subop;
    /* A component click: the component, its sub (slot), the obj in it. */
    int32_t combined_id;
    int32_t sub;
    int32_t obj;
    int32_t op;
    /* The second component of a use-on, a spell, a drag. */
    int32_t selected_combined_id;
    int32_t selected_sub;
    int32_t selected_obj;
    int32_t target_combined_id;
    int32_t target_sub;
    int32_t target_obj;
    int32_t crc;
    /* Dialogs. */
    int32_t count;
    int64_t count_long;
    /* Social. */
    int32_t clan_id;
    int32_t member_index;
    int32_t muted;
    int32_t rank;
    int32_t rule_id;
    int32_t mute;
    int32_t type;
    int32_t colour;
    int32_t effect;
    int32_t pattern[8];
    int32_t clan_type;
    int32_t public_chat_filter;
    int32_t private_chat_filter;
    int32_t trade_chat_filter;
    /* Movement. */
    int32_t key_combination;
    int32_t minimap_width;
    int32_t minimap_height;
    int32_t camera_angle_y;
    int32_t checkpoint1;
    int32_t checkpoint2;
    int32_t checkpoint3;
    int32_t checkpoint4;
    int32_t checkpoint5;
    int32_t fine_x;
    int32_t fine_z;
    /* The rest. */
    int32_t packed;
    int32_t window_mode;
    int32_t frame_width;
    int32_t frame_height;
    int32_t heading;
    int32_t has_name;
    int32_t has_autocomplete;
    int32_t autocomplete;
    char const* name;
    char const* string;
    char const* description;
    char const* instructions;
    char const* command;
    /* LostCity's own: a waypoint walk (the start tile, then up to 24 signed
     * deltas toward the destination), a name as base-37, the tutorial tab, a drag's mode, a
     * report's rule, the character design. */
    int32_t route_x;
    int32_t route_z;
    int32_t route_count;
    int32_t route_dx[24];
    int32_t route_dz[24];
    int64_t name37;
    int32_t tab;
    int32_t mode;
    int32_t reason;
    int32_t gender;
    int32_t idkit[7];
    int32_t colours[5];
    /* Tails the caller encodes: packed chat text, a script trigger's args. */
    uint8_t const* huffman;
    int32_t huffman_length;
    uint8_t const* copy;
    int32_t copy_length;
};

/** 1 when `rev` writes its client packets through an executor (proto_encode). */
int
proto_out_speaks(struct GameProtoRevTable const* rev);

/** 1 when `rev` can write `out_name`: an opcode for it, and a codec. */
int
proto_out_has(struct GameProtoRevTable const* rev, int out_name);

/**
 * Write `out_name` at `rev` from `args` into `buf`: the scrambled opcode, the
 * length its prot table states, the payload. Returns the bytes written, or -1
 * when the revision has no prot for `out_name` or `cap` cannot hold it (ISAAC
 * is not stepped then). `rev` must speak RSProt (proto_out_speaks).
 */
int
proto_encode(
    struct GameProtoRevTable const* rev,
    struct Isaac* random_out,
    uint8_t* buf,
    int cap,
    int out_name,
    struct ProtoOutArgs const* args);

#endif
