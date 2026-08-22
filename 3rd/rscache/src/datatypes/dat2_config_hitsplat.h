#ifndef RSCACHE_DATATYPES_DAT2_CONFIG_HITSPLAT_H
#define RSCACHE_DATATYPES_DAT2_CONFIG_HITSPLAT_H

#include "../rsbuffer.h"

struct RSCache;

/** Most opcodes one hitsplat record carries. Observed maximum is 5. */
#define RSCACHE_HITSPLAT_MAX_OPCODES 16
/** Cap on opcode 8's string, including its terminator. Longest observed is 3. */
#define RSCACHE_HITSPLAT_MAX_TEXT 64
/** Cap on the opcode 17/18 id array. Observed length is 2 (`count` 1). */
#define RSCACHE_HITSPLAT_MAX_VARIANTS 32

/**
 * A hitsplat type (config group 32): how a damage splat is drawn.
 *
 * ## The format is the rev-239 client's, read from the deob
 *
 * This was previously reverse-engineered by brute-forcing operand widths, and the
 * result was **wrong in a way that no amount of that method could catch**. The
 * record below is now transcribed from the reference's own reader —
 * `class420(class617)` in `src_osrs239_rl1_12_33/deob/class420.java` — and then
 * verified against real bytes.
 *
 * ### What the brute force got wrong, and why it looked right
 *
 * Opcode 8 is a **string**, not a u16, and it is preceded by a version-marker
 * byte. The old decoder read it as a u16 and then invented an "opcode 49" out of
 * the string's own bytes. On the commonest record the stream is:
 *
 *     08  00 25 31 00  05 08 de  09 00 32  00
 *     ^   ^^^^^^^^^^^  ^^^^^^^^  ^^^^^^^^  ^
 *     |   marker+"%1"  sprite    duration  end
 *
 * `0x25 0x31` is ASCII `"%1"`. The old reading took `08` + `g2(00 25)`, then saw
 * `0x31` = 49 and consumed the NUL as its operand — landing on **exactly the same
 * byte count**. That is why the old header could claim "verified at 243/243
 * records consuming exactly" and still have the format wrong: consuming the right
 * total is not the same as parsing the right fields, and a width search that only
 * scores "does the record consume exactly" cannot tell them apart.
 *
 * **There is no opcode 49.** Nor an opcode 37 — that was `'%'`.
 *
 * ### Verified, not assumed
 *
 * Both this decoder and a direct transcription of `class420` were run over every
 * OSRS-lineage cache in the tree and agree, consuming every record exactly:
 *
 *   | cache      | records | exact |
 *   |------------|---------|-------|
 *   | osrs239    |      83 |    83 |
 *   | osrs230    |      78 |    78 |
 *   | osrs184    |      14 |    14 |
 *   | kronos     |      14 |    14 |
 *
 * Only opcodes **5, 8, 9, 11, 13, 18** actually occur in those caches, so only
 * those widths are established by measurement. The rest are taken from the
 * reference's own reader, and each shares its read helper with a measured one,
 * which is what pins its width:
 *
 *   | opcode(s)   | reference reader | width  | pinned by |
 *   |-------------|------------------|--------|-----------|
 *   | 1, 3, 4, 6  | method13234      | u16    | 5 (measured) |
 *   | 7, 10       | method13132      | u16    | 13 (measured) |
 *   | 14          | method13235      | u16    | 9 (measured) |
 *   | 12          | method13128      | u8     | the opcode byte itself |
 *   | 2           | method13237      | 3 bytes | NOT pinned — colour, by convention |
 *
 * Opcode 2 is the one field here with no measured sibling. It is a colour
 * (`field5313`, default `0xFFFFFF`), and 3 bytes is what a colour is in every
 * other dat2 type; if a cache ever carries one, that is the assumption to check
 * first.
 *
 * ### Opcode 18's payload was structured all along, and it is a multi-var selector
 *
 * The old header carried its 11 bytes raw and guessed `u16, i16, u16, u8, u16,
 * u16`, calling it "suggestive but not established". The reference confirms it
 * exactly: `u16, u16, u16, u8 count, u16[count+1]`, with 65535 meaning -1 — which
 * is why bytes 2-3 read as `ff ff` on every record. Opcode 17 is the same minus
 * the third u16.
 *
 * What those three fields ARE is the whole of settings 5 ("Hitsplat tinting")
 * and 279 ("Max hit hitsplats"), and it went unnamed here for as long as the
 * fields were called `variant_a/b/c`. They are a **varbit id, a varp id and a
 * fallback**, and the type resolves exactly like `LocType::GetMultiLoc`
 * (`HitmarkType::GetMultiHitmark`, NXT):
 *
 *     if      (varbit != -1) v = GetVarbit(varbit);
 *     else if (varp   != -1) v = varp[varp];
 *     else                   v = -1;
 *     id = (v >= 0 && v < size - 1) ? array[v] : array[size - 1];
 *     return id == -1 ? nullptr : HitmarkType::List(id);
 *
 * where `array` is the `count + 1` ids read from the stream **followed by** the
 * opcode-18 fallback — the reference's own `count + 2` layout. Opcode 17 reads
 * no fallback and the reference leaves that slot at **-1**, so an opcode-17
 * record whose var reads out of range draws no splat at all. That is a real
 * outcome and not a decode failure.
 *
 * `cache.osrs239` uses nothing else: of its 34 selector records, 25 are keyed on
 * varbit 10236 (`hitsplat_tint_disabled`) and 9 on 14196
 * (`hitsplat_maxhit_disabled`). The records are wrappers over leaf appearances
 * and they pair — 16 "damage you dealt" resolves to leaf 28 either way, 17
 * "damage someone else dealt" resolves to the tinted leaf 29 when the setting is
 * on and to 28 when it is off. So a client that sends or stores the LEAF id has
 * silently opted out of both settings; the wrapper is the id that carries the
 * question.
 *
 * ## What the record draws, and therefore what each opcode is
 *
 * Every field below used to be called `opcode_<n>` here, and the number is not
 * the meaning: the meaning is in `Statics.method10685` (`sf.al`), the reference's
 * own splat renderer, which is the only place the fields are read. It lays four
 * sprites out in a row and centres the text over the middle one:
 *
 *     [ icon_sprite ][ left_sprite ][ sprite ][ sprite ]...[ right_sprite ]
 *                                   \___ tiled until it covers the text ___/
 *
 * `sprite` (opcode 5) is the one that repeats — `count = textwidth / width + 1`
 * — so it is the body of the bar, and the other three are drawn once each, in
 * that left-to-right order, at their own widths. A record that states only
 * `sprite` is the ordinary splat; the icon and the two caps are how the
 * decorated ones (poison, max hit) are built.
 *
 * The rest of the fields are what the renderer does with that row over the
 * splat's lifetime, where `remaining` counts down from `duration` to 0:
 *
 * - `duration` (opcode 9, `field5309`, **default 70**) — how long the splat stays
 *   up, in client cycles. The reference computes `cycle = field5309 + now + delay`.
 * - `drift_x` (opcode 7, `field5319`) — `x += drift_x - drift_x * remaining /
 *   duration`, so the splat starts in place and has slid `drift_x` pixels right
 *   by the time it expires.
 * - `drift_up` (opcode 10, `field5320`) — the same ramp on y with the sign
 *   flipped (`y += drift_up * remaining / duration - drift_up`), so a positive
 *   value makes the splat *rise* by that many pixels. Named for the direction it
 *   moves rather than for the axis, because the axis alone gets the sign wrong.
 * - `text_offset_y` (opcode 13, `field5304`) — extra pixels down for the text
 *   baseline, on top of the fixed `+15` the renderer applies to every splat.
 * - `fade_after` (opcodes 11 and 14, `field5311`, **default -1** = never fades) —
 *   `alpha = (remaining << 8) / (duration - fade_after)`, which is 255 or more
 *   until `fade_after` cycles have elapsed and then ramps to 0 at expiry. Opcode
 *   14 states the cycle count; **opcode 11 is the same field set to 0** with no
 *   operand, i.e. fade across the whole lifetime, and the two are kept apart
 *   (`has_fade_flag` vs `has_fade_after`) only so the encoder can reproduce the
 *   byte the record carried.
 * - `text_colour` (opcode 2, `field5313`, **default 0xFFFFFF**) and `font_id`
 *   (opcode 1, `field5312`) — the colour the number is drawn in and the font it
 *   is drawn with. A record with no font takes the client's default.
 * - `slot_policy` (opcode 12, `field5318`, **default -1**) — what to do when the
 *   actor's hitmark list is already full. -1 discards the incoming hit, 0
 *   overwrites the splat with the lowest remaining cycle, 1 overwrites the
 *   lowest-valued splat and discards the incoming hit when that value is already
 *   at least as large.
 *
 * Every default is the reference's own, set before its opcode loop runs, and
 * `duration`/`slot_policy` are what `World_EntityAddHitmark` had hardcoded.
 *
 * ## Era gating
 *
 * Group 32 is a hitsplat config **only in the OldSchool lineage**. Decoding it out
 * of a pre-EoC RS2 cache is meaningless — `cache.rs643` yields 2008 "records" and
 * `cache.rs727_preeoc` 2574, none of which parse under either reader. So the
 * opcode set is gated behind `RSCACHE_CONFIG_HITSPLAT_DECODE_OSRS`; with no flags
 * every opcode comes back unclaimed and `_consumed` stays short, which is the
 * loud failure every caller already checks for.
 */
struct RSCache_Dat2ConfigHitsplat
{
    int id;

    /** Opcode 5 (`field5316`). The sprite the bar is tiled from — the body of the
     *  splat, repeated until it covers the text. -1 when absent. */
    int sprite_id;

    /** Opcode 3 (`field5315`). The sprite drawn first, left of everything else —
     *  the poison drop, the max-hit star. -1 when absent. */
    int icon_sprite_id;
    /** Opcode 4 (`field5323`). Drawn between the icon and the tiled body. -1 absent. */
    int left_sprite_id;
    /** Opcode 6 (`field5324`). Drawn last, right of the tiled body. -1 absent. */
    int right_sprite_id;

    /** Opcode 1 (`field5312`). The font the number is drawn in. -1 = the client's
     *  default font. */
    int font_id;
    /** Opcode 2 (`field5313`). The colour the number is drawn in; 0xFFFFFF absent. */
    int text_colour;
    /** Opcode 13 (`field5304`). Extra pixels down for the text baseline. 0 absent. */
    int text_offset_y;

    /** Opcode 7 (`field5319`). Pixels the splat slides right over its lifetime. 0
     *  when absent. */
    int drift_x;
    /** Opcode 10 (`field5320`). Pixels the splat rises over its lifetime — the
     *  renderer negates it, so positive is up. 0 when absent. */
    int drift_up;
    /** Opcodes 11 (sets 0, no operand) and 14 (u16), both `field5311`: the cycle
     *  the fade to transparent starts at. -1 when absent, meaning never. */
    int fade_after;

    /**
     * Opcode 9 (`field5309`): how long the splat stays up, in client cycles.
     * **Defaults to 70** — the reference's own pre-loop value, and what the
     * client hardcoded before this field existed.
     */
    int duration;

    /**
     * Opcode 12 (`field5318`): what to do when the target's hitmark list is full.
     * **Defaults to -1** (discard the incoming hit). 0 = overwrite the lowest
     * remaining cycle, 1 = overwrite the lowest value.
     */
    int slot_policy;

    /** Opcode 8 (`field5322`), NUL-terminated. Empty when absent. The template the
     *  splat prints: every `%1` in it is replaced with the damage (`method9601`),
     *  which is why almost every record's is exactly `%1`. */
    char text[RSCACHE_HITSPLAT_MAX_TEXT];
    /** The version-marker byte that precedes the string; re-emitted verbatim. */
    uint8_t text_marker;
    bool has_text;

    /* --- opcode 17 / 18 (`field5325`), the multi-variant selector ----------- */

    /** 17 or 18, or 0 when neither was present. */
    int variant_opcode;
    /** The varbit whose value indexes `variants`. -1 when absent (65535 on the
     *  wire), in which case `variant_varp` is consulted instead. */
    int variant_varbit;
    /** The varp whose value indexes `variants`, used only when
     *  `variant_varbit` is -1. -1 when absent. */
    int variant_varp;
    /** The id used when neither var is set or the value is out of range.
     *  Opcode 18 only. **-1 for opcode 17**, which reads no fallback — and -1
     *  means "draw no splat", so that is the outcome, not a missing value. */
    int variant_fallback;
    /** The ids read from the stream, in var-value order. -1 is a real entry and
     *  means "draw no splat at all", not "absent". */
    int variants[RSCACHE_HITSPLAT_MAX_VARIANTS];
    int variant_count;

    bool has_font;
    bool has_text_colour;
    bool has_icon_sprite;
    bool has_left_sprite;
    bool has_right_sprite;
    bool has_drift_x;
    bool has_drift_up;
    bool has_text_offset_y;
    bool has_duration;
    bool has_slot_policy;
    /** True for opcode 11 specifically (the bare flag, `fade_after` = 0), so the
     *  encoder can tell it from opcode 14, which sets the same field with an
     *  operand. */
    bool has_fade_flag;
    bool has_fade_after;

    /**
     * The opcodes in the order the record carried them, which differs per record
     * (`5,8,9` / `5,8,11,9` / `8,5,9` / `8,5,9,13` / `9,18` are all real).
     * Replayed by the encoder to reproduce the bytes.
     */
    uint8_t opcodes[RSCACHE_HITSPLAT_MAX_OPCODES];
    int opcode_count;

    /** Bytes consumed. Equal to the record size for a fully understood record. */
    int _consumed;
};

/** The OldSchool opcode set. Without it every opcode comes back unclaimed. */
#define RSCACHE_CONFIG_HITSPLAT_DECODE_OSRS 1

/**
 * Era payload flags for this cache — the `flags_for` hook of `opcode_codec.h`.
 *
 * Group 32 carries hitsplat records only in the OldSchool lineage, so this is a
 * lineage test rather than a revision comparison: asking whether an RS2 archive
 * revision is "at least" some OSRS number is the D16 trap `dat2_config_npc.c`
 * documents. No intra-OSRS variation has been observed — 184, 230 and 239 all
 * carry the same opcode set — so there is deliberately no revision threshold
 * here yet; add one the same way npc does if a cache ever disagrees.
 */
int
RSCache_Dat2ConfigHitsplatFlags(const struct RSCache* cache);

/** Decode from a cursor, so back-to-back records in one buffer can be walked. */
void
RSCache_Dat2ConfigHitsplatDecode(
    struct RSCache_Dat2ConfigHitsplat* entry,
    struct RSCache_Buffer* buffer,
    unsigned flags);

/**
 * Set the type's non-zero defaults on an already-zeroed record.
 *
 * Must run before any `DecodeOp` call. `sprite_id` defaults to -1 because sprite
 * 0 is a real sprite and cannot double as "absent"; a record decoded without this
 * draws splat sprite 0 instead of none. `duration` (70) and `slot_policy` (-1)
 * matter for the same reason — a zeroed record would claim a splat that vanishes
 * instantly and an eviction policy that overwrites.
 */
void
RSCache_Dat2ConfigHitsplatInit(struct RSCache_Dat2ConfigHitsplat* entry);

/**
 * Handle one opcode, advancing `buffer`. True when consumed, false when unknown.
 *
 * The extension point: a server-side record that embeds this one calls this first
 * and handles only what comes back false. See `opcode_codec.h`.
 */
bool
RSCache_Dat2ConfigHitsplatDecodeOp(
    struct RSCache_Dat2ConfigHitsplat* entry,
    int opcode,
    struct RSCache_Buffer* buffer,
    unsigned flags);

/** Decode one dat2 record and record `_consumed`. Stops on an unknown opcode rather
 *  than guessing its width, leaving `_consumed` short — the harness asserts on that. */
void
RSCache_Dat2ConfigHitsplatDecodeInplace(
    struct RSCache_Dat2ConfigHitsplat* entry,
    const void* data,
    int data_size,
    unsigned flags);

/** Byte-exact when `opcodes` holds the source order. Returns bytes written, or 0. */
uint32_t
RSCache_Dat2ConfigHitsplatEncode(
    const struct RSCache_Dat2ConfigHitsplat* entry,
    uint8_t* out,
    uint32_t out_capacity);

uint32_t
RSCache_Dat2ConfigHitsplatEncodeBound(const struct RSCache_Dat2ConfigHitsplat* entry);

#endif // RSCACHE_DATATYPES_DAT2_CONFIG_HITSPLAT_H
