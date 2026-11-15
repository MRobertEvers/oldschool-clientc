#ifndef SRC_TORIRSSERVER_WIRE_CORE_WIRE_IN_H
#define SRC_TORIRSSERVER_WIRE_CORE_WIRE_IN_H

/*
 * The inbound core: the canonical client -> server bodies the world handlers
 * (`torirs_server_world.c`) read, one writer per body.
 *
 * A revision's translator (wire/osrs<N>/osrs<N>_inbound.c, the `translate_in`
 * slot of its wire) decodes its own bytes with rsprot's codec for that
 * revision -- `packet_oploc1_v2_osrs241_in` and the rest, the same functions
 * the client writes with -- and hands the fields to a writer here. Which
 * revision sent the packet is the translator's knowledge; what the world reads
 * is this file's. The two cannot drift into a third transcription of a layout,
 * which is what the server's own hand-kept 241/242 tables were.
 *
 * Bodies that did not change between 239 and 242 (the dialogs, the social
 * names, CLIENT_CHEAT, the two chat messages, IF_BUTTONX/IF_SUBOP) are taken
 * from the raw payload by `wire_in_body`, the same at every revision.
 *
 * Every writer appends to `w`; the translator finishes with `wire_in_done`.
 */

#include "rsprot_buf.h"
#include "rsprot_exec.h"

#include "rsprot_buf.h"

#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Running one rsprot codec over an inbound payload                    */
/* ------------------------------------------------------------------ */

struct WireInReader
{
    RsprotBuf buf;
    RsprotExec exec;
    int len;
};

/** Begin decoding `in`; pass the result to one `packet_<prot>_osrs<N>_in`. */
RsprotExec*
wire_in_reader(struct WireInReader* r, uint8_t const* in, int in_len);

/** 1 when the codec read cleanly and consumed exactly the payload. */
int
wire_in_read_all(struct WireInReader const* r);

/** 1 when the codec read cleanly (a trailing remainder is the caller's). */
int
wire_in_read_ok(struct WireInReader const* r);

/** Finish a translation: `name` with `*out_len` set, or PKTOUT_NAME_NONE when
 *  the body did not fit. */
int
wire_in_done(struct RsprotBuf const* w, int name, int* out_len);

/* ------------------------------------------------------------------ */
/* Canonical bodies                                                    */
/* ------------------------------------------------------------------ */

/** OPNPC<n>, OPPLAYER<n>: `p2 index`. */
void
wire_in_index(struct RsprotBuf* w, int index);

/** OPLOC<n>, OPOBJ<n>: `p2 x, p2 z, p2 id`. */
void
wire_in_tile(struct RsprotBuf* w, int x, int z, int id);

/**
 * The T ("targeted") form of an op on an npc or player: the index, then either
 * a spell tail (when nothing is selected but a spell) or the use-on tail.
 * Returns `name_t` for a cast and `name_u` for a use-on.
 */
int
wire_in_index_target(
    struct RsprotBuf* w,
    int name_t,
    int name_u,
    int index,
    int selected_obj,
    int selected_sub,
    int selected_com);

/** The same for a loc or ground obj, after its tile. */
int
wire_in_tile_target(
    struct RsprotBuf* w,
    int name_t,
    int name_u,
    int x,
    int z,
    int id,
    int selected_obj,
    int selected_sub,
    int selected_com);

/** IF_BUTTONT: OPHELDT (a spell on an item) or OPHELDU (an item on an item). */
int
wire_in_if_buttont(
    struct RsprotBuf* w,
    int target_com,
    int target_obj,
    int target_sub,
    int selected_com,
    int selected_obj,
    int selected_sub);

/** IF_BUTTOND, in the 16-byte body handle_inv_buttond reads. */
void
wire_in_inv_buttond(
    struct RsprotBuf* w,
    int selected_com,
    int selected_obj,
    int selected_sub,
    int target_com,
    int target_obj,
    int target_sub);

/** MOVE_GAMECLICK / MOVE_MINIMAPCLICK, both into handle_move's `ctrl, x, z`. */
void
wire_in_move(struct RsprotBuf* w, int key_combination, int x, int z);

/** RESUME_PAUSEBUTTON: `p4 uid, p2 sub`. */
void
wire_in_resume_pausebutton(struct RsprotBuf* w, int combined_id, int sub);

/** CLICK_WORLD_MAP: `p4 packed`. */
void
wire_in_clickworldmap(struct RsprotBuf* w, int packed);

/** SET_HEADING: one plain byte. */
void
wire_in_set_heading(struct RsprotBuf* w, int heading);

/**
 * IF_SCRIPT_TRIGGER, in the body the interface router decodes
 * (wire_if_script_trigger_decode): its twelve-byte header, then the typed
 * arguments copied whole. Returns 0 when the result does not decode.
 */
int
wire_in_if_script_trigger(
    struct RsprotBuf* w,
    int sub,
    int obj,
    int combined_id,
    int crc,
    uint8_t const* args,
    int args_len);

/**
 * The packets whose body is the same at 239..242, taken from the raw payload:
 * IF_BUTTONX, IF_SUBOP, IF_BUTTON, the dialog resumes, the social names,
 * CLIENT_CHEAT, MESSAGE_PRIVATE, MESSAGE_PUBLIC, WINDOW_STATUS, CHAT_SETMODE
 * and the empty packets. Returns the canonical name to route, or
 * PKTOUT_NAME_NONE (also for a `name` this does not cover).
 */
int
wire_in_body(
    int name,
    uint8_t const* in,
    int in_len,
    uint8_t* out,
    int out_cap,
    int* out_len);

#endif
