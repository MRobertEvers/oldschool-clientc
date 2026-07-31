#ifndef RSCACHE_TOOLS_CACHEPACK_CP_FIELDS_H
#define RSCACHE_TOOLS_CACHEPACK_CP_FIELDS_H

/*
 * The tree's field register, `fields/<type>.ini` — the writer's half.
 *
 * `src/net/mock/mock230_servercodec.c` is the *reader*: it holds a C table of
 * `(opcode, wire, struct offset)` and turns a server-band stream back into a
 * `Mock230NpcDef`. This is the other end of that stream. Both are driven by the
 * same declarations —
 *
 *     [npc.hitpoints]   server = opcode:77:u2
 *
 * — and neither may hold a number the other does not. A field written under one
 * opcode and read under another produces no error anywhere: both streams are
 * well-formed opcode streams, the value simply lands in the wrong field or in
 * none. So the register is the single source and each side is checked against it
 * (`cp_fields_check` here, `mock230_servercodec_test`'s cross-check there).
 *
 * ## Why a second reader rather than shared code
 *
 * The same reason `cp_register.h:6-10` gives for `content.ini`: cachepack
 * deliberately links nothing from the server repo's `src/`, because the vendored
 * library and its tools are meant to be usable apart from the client. The `.ini`
 * is the *contract* between them, not a header. `src/content/content_fields.c` is
 * therefore the model for this file and not its supplier, and the two answer
 * different questions:
 *
 *   content_fields.c   what does a field mean? — scope, and how it reaches the
 *                      client (`native` / `param:<n>` / `drop` / `error`). It
 *                      carries built-in defaults so a tree that ships no register
 *                      still behaves as it did before.
 *   cp_fields.c        where does a field land in the server pack? — opcode, wire
 *                      width, and the namespace its value is spelled in.
 *
 * **This one has no built-in defaults, deliberately.** A writer that invents an
 * opcode the tree did not declare writes bytes nothing agreed to read. A tree with
 * no `fields/<type>.ini` therefore emits no server band at all, which is the safe
 * answer rather than a silently different one.
 *
 * ## `ref` — the namespace a value is spelled in
 *
 * The band is integers. Content is not: `param=death_drop,bones` names an obj and
 * `param=attack_anim,cow_attack` names a sequence, and turning either into an id
 * needs to know *which* pack file to look in. Nothing else in the register states
 * it — `client = param:<name>` names the param, not the value's type — so this
 * adds one optional row:
 *
 *     [npc.death_drop]  server = opcode:151:u4   ref = obj
 *
 * A field with no `ref` accepts only a decimal literal. That is the right default:
 * `huntmode = aggressive` is an engine enum with no cache namespace at all, and
 * guessing one would bake a number nobody agreed on. The unresolved value is
 * reported per field and not written, which is a visible gap rather than a wrong
 * record.
 *
 * The reader never sees `ref` and does not need to — it reads ids, not names — so
 * `content_fields.c` ignoring the key is correct rather than drift.
 */

#include <stdint.h>

/**
 * Payload width, spelled as the byte count so it reads the same as the reader's
 * `enum ServerWire`.
 *
 * `string` is parsed and then refused by `cp_fields_check`: the reader's
 * `Mock230_ServerNpcDecode` has no string case, so a tree declaring one would get
 * a stream that stops at that opcode and a record silently missing every field
 * after it. Refusing at the register is the only place that failure is legible.
 */
enum CP_FieldWire
{
    CP_FIELD_WIRE_NONE = 0,
    CP_FIELD_WIRE_U1 = 1,
    CP_FIELD_WIRE_U2 = 2,
    CP_FIELD_WIRE_U4 = 4,
    CP_FIELD_WIRE_STRING = 5,
};

/**
 * How a field reaches the client, mirroring `content_fields.h`'s enum.
 *
 * The writer needs this as well as the reader does, and for a sharper reason: it
 * is what decides whether a merged key may be handed to the client encoder at
 * all. `hitpoints` reaching `cp_npc.c` is an unknown key; `hitpoints` reaching
 * the record's param table is the projection working.
 */
enum CP_FieldClient
{
    /** Server-only. The client encoder never sees the key. Default. */
    CP_FIELD_CLIENT_DROP = 0,
    /** The record's own field; the client encoder handles the key itself. */
    CP_FIELD_CLIENT_NATIVE,
    /** Folded into the record's param table, under `param_name`. */
    CP_FIELD_CLIENT_PARAM,
    /** The encoder must refuse. No silent loss allowed. */
    CP_FIELD_CLIENT_ERROR,
};

struct CP_Field
{
    /** `hitpoints` — the key as a config spells it, without the `<type>.` prefix. */
    char name[48];
    /** 64..255, or 0 when the field has no server band. Client npc opcodes run
     *  1..147, so the reserved band is what keeps a server record from being
     *  mistaken for a client one. */
    int opcode;
    /** `CP_FIELD_WIRE_NONE` for a field the register declares but gives no
     *  server home — `[npc.name]` is a real declaration with nothing to encode. */
    enum CP_FieldWire wire;
    enum CP_FieldClient client;
    /** For `CP_FIELD_CLIENT_PARAM`: the param's *name*, resolved through the param
     *  pack at bake time. A name and not a number, because which number
     *  `hitpoints` is is the pack file's business and it has been renumbered once
     *  already. */
    char param_name[48];
    /**
     * Namespace a symbolic value is resolved through, or "" for decimal only.
     *
     * A cachepack config type name — `obj`, `seq`, `loc` — validated against
     * `cp_type_by_name` by the caller, not here: this file stays free of
     * `cachepack.h` so it links into a test without the cache library behind it.
     */
    char ref[32];
};

enum
{
    CP_FIELDS_MAX = 128,
    /** Every field at once — opcode byte plus widest payload — plus the
     *  terminator. A record cannot state more fields than the register declares. */
    CP_SERVER_BAND_MAX = (CP_FIELDS_MAX * 5) + 1,
};

struct CP_Fields
{
    char type[32];
    /**
     * Band fields first, ascending by opcode, then everything else.
     *
     * Two things read this array and they want different halves. The band writer
     * walks `[0, band_count)` and needs ascending opcodes, so two packs of the
     * same content come out byte-identical. The client-side filter walks all
     * `count` of them, because `[npc.name]` and `[npc.magic]` have no opcode and
     * are still real declarations — dropping them, as an earlier cut of this file
     * did, left the filter unable to tell a client-native key from an unknown one.
     */
    struct CP_Field entries[CP_FIELDS_MAX];
    int count;
    /** How many of `entries` carry a server opcode. */
    int band_count;
    /**
     * Whether *records the tree adds* belong in the client cache.
     *
     * Spelled `records = client` in a bare `[<type>]` section. The default is no,
     * and the default is the interesting one: a block that exists only in
     * `server/scripts` is usually a server table wearing a config type's grammar.
     * The seven enums this tree authors are exactly that — `bank_tabs` and
     * `worn_slots` are read by `mock230_content_enum`, and no client script has
     * ever heard of them — so writing them into the cache would add records with
     * no reader, in a grammar cachepack cannot fully resolve.
     *
     * `param` opts in, because it must: `fields/npc.ini` projects `hitpoints`
     * into param 2643, and a param id with no record behind it is a reference the
     * client cannot interpret.
     *
     * A record that exists at rank 0 is written either way — it is already in the
     * cache, and this is only about *new* ones.
     */
    int records_client;
    /** 1 when a `fields/<type>.ini` was actually read. A tree that meant to
     *  declare a projection and misspelled the file otherwise emits nothing and
     *  looks like a tree that declared nothing. */
    int from_file;
    /** Rows the parser refused — an out-of-band opcode, an unreadable spec.
     *  Counted rather than dropped, so `cp_fields_check` can fail on them. */
    int rejected;
};

/**
 * Read `<srcdir>/fields/<type>.ini`.
 *
 * Never fails: a missing file yields an empty register and `from_file == 0`.
 * Returns the number of `server = opcode:...` fields found.
 */
int
cp_fields_load(
    struct CP_Fields* fields,
    const char* srcdir,
    const char* type);

/** By name, or NULL. */
const struct CP_Field*
cp_fields_find(
    const struct CP_Fields* fields,
    const char* name);

/**
 * Refuse a register the writer and the reader cannot both honour.
 *
 * Three ways a well-formed file still describes an unwritable band, each of which
 * fails silently if allowed through:
 *
 *   - **two fields on one opcode** — the writer emits both, the reader assigns
 *     whichever it matches first and the other value is simply gone.
 *   - **an opcode outside 64..255** — the client band starts at 1, and a server
 *     record overlapping it stops being distinguishable from a client one.
 *   - **`wire = string`** — declared by the register, unimplemented by the
 *     reader's decoder, so the stream would terminate mid-record.
 *
 * Reports each violation naming the field and returns the count.
 */
int
cp_fields_check(const struct CP_Fields* fields);

/* ---- the band ----------------------------------------------------------- */

/*
 * One record's server-only fields, as bytes.
 *
 * **Sparse, and by presence rather than by value.** A field is written when the
 * tree states it and omitted when the tree does not — there is no comparison
 * against a default, and there must never be one against zero. `death_drop`
 * defaults to -1 and 0 is a real obj; `idk.type` 0 is a body part; sprite 0 is a
 * sprite. The reader already treats "absent" and "present and zero" as different
 * states (`mock230_servercodec.h`, and the seed-then-override rule), and the
 * writer's job is to preserve that distinction, not to re-derive it from values it
 * has no defaults for.
 *
 * Ascending by opcode, which here is a *choice* and not a constraint: the reader
 * dispatches per opcode, so any order decodes identically. Ascending is what makes
 * two packs of the same content byte-identical and a pack diff readable. Worth
 * separating from the cache's own config records, where order is load-bearing and
 * ascending would be wrong — dat1 records are not written in opcode order, and
 * reproducing one means recording the decoded order and replaying it.
 */
struct CP_ServerBand
{
    uint8_t bytes[CP_SERVER_BAND_MAX];
    int at;
    /** Fields written. Zero means the record states nothing and needs no archive. */
    int stated;
};

void
cp_server_band_init(struct CP_ServerBand* band);

/**
 * Append one field. Returns 0 when `value` does not survive `field->wire`.
 *
 * The check is against what the reader decodes back, not against a sign
 * convention: `u1` and `u2` are zero-extended there, so only `u4` can carry a
 * negative — which is exactly what `death_drop`'s -1 needs and why it is declared
 * `u4`. A value that would truncate is refused rather than masked, because a
 * masked id is a valid id for some other record.
 */
int
cp_server_band_put(
    struct CP_ServerBand* band,
    const struct CP_Field* field,
    int value);

/** Append the terminator. Returns the band's size in bytes. */
int
cp_server_band_finish(struct CP_ServerBand* band);

/* ---- the archive -------------------------------------------------------- */

/*
 * `server/pack/` is a dat2 with no reference table.
 *
 * `RSCache_Dat2DiskWriteArchive` will create `main_file_cache.dat2` and an
 * `idx<n>` from nothing, which is what makes the server pack a real cache
 * directory rather than a bespoke file format — but it writes no `idx255`, so
 * there is no per-archive CRC, version or child list the way a client cache has.
 * Nothing would notice an archive left over from a tree two edits ago.
 *
 * Hence a header on every archive, carrying the three facts the missing reference
 * table would have carried:
 *
 *     u8  'S'
 *     u8  'P'
 *     u8  version        — the payload grammar's revision, so a reader can refuse
 *                          a stream it predates instead of decoding it as if it
 *                          understood
 *     u8  kind           — which grammar: an opcode band, or a name table
 *     u32 crc32(payload) — over the bytes that follow, so truncation and a stale
 *                          sector chain are detectable
 *     ..  payload
 *
 * Eight bytes on a payload that is typically under forty. The magic is not
 * paranoia: these archives sit in a directory that looks exactly like a client
 * cache, and reading one as the other should fail loudly on the first byte. The
 * `kind` byte is what keeps the container self-describing now that two different
 * payloads live in it — inferring the grammar from the group id would work today
 * and stop working the moment a server-only type has both.
 */
#define CP_SERVER_PACK_VERSION 1

/** What an archive's payload is. */
enum CP_ServerPayload
{
    /** `<opcode:u8> <payload>` until a zero opcode — one record's server band. */
    CP_SERVER_PAYLOAD_BAND = 1,
    /** `u2 count`, then `u4 id` + NUL-terminated name per entry. */
    CP_SERVER_PAYLOAD_NAMES = 2,
};

enum
{
    CP_SERVER_PACK_HEADER = 8,
};

/** Wrap a finished band in the header. Returns bytes written, or 0. */
uint32_t
cp_server_archive_build(
    const struct CP_ServerBand* band,
    uint8_t* out,
    uint32_t out_capacity);

/** The same framing over any payload — one entry point, so the magic, version
 *  and CRC cannot be written two slightly different ways. */
uint32_t
cp_server_archive_build_payload(
    int kind,
    const uint8_t* payload,
    uint32_t payload_size,
    uint8_t* out,
    uint32_t out_capacity);

/**
 * Validate a header and point at the payload inside it.
 *
 * Returns 1 when the magic, the version and the CRC all hold, 0 otherwise. The
 * writer's own round-trip check runs through this, so the header is exercised by
 * the thing that writes it rather than only by a reader nobody has written yet.
 */
int
cp_server_archive_open(
    const uint8_t* data,
    int size,
    int* out_version,
    int* out_kind,
    const uint8_t** out_payload,
    int* out_payload_size);

/* ---- server-only namespaces ---------------------------------------------- */

/*
 * `stat` and `category` are not records the client ignores. They are types the
 * client has no concept of: no config kind, no gameval archive, nothing in the
 * cache to overlay. Today they exist only as `pack/<ns>.pack` text re-read at
 * every boot, which is the same "re-parsed out of text" problem the server band
 * removed for npc.
 *
 * ## The group-id space, and why 128
 *
 * The server pack addresses an archive by `(group, id)`, borrowing the cache's
 * config-kind numbering — `npc` is group 9 because that is its kind. A
 * server-only type has no kind, so it needs a number from a space that provably
 * cannot collide with one.
 *
 * **The whole space is 0..255, and that is a format limit rather than a
 * convention.** Every sector in a dat2 carries its table id in a single byte
 * (`dat2disk.c`, `data[7] = header->index_id & 0xFF` in both the small and large
 * sector headers), so a group id of 256 is not merely unwise — it is written as
 * 0 and silently aliases another table. Anything "well above the cache's range"
 * has to fit in a byte.
 *
 * Inside that byte: `dat2_configs.h` runs 1..39, with 39 (`dbtable`) the largest
 * kind this revision defines. **128 is the top half of the space**, leaving
 * 40..127 — more than double the current maximum — for OldSchool to grow into
 * before it reaches ours. That is the same argument the 64..255 opcode band
 * makes in `mock230_servercodec.h`, and it is the strongest one available: the
 * numbers below are Jagex's and may grow, the numbers above are ours alone, and
 * the gap between them is the margin.
 *
 * One group per namespace, with its name table at archive 0. A server-only type
 * that later needs per-record bands — `prayer` will — takes its own group and
 * says so here, rather than sharing one and reserving archive ids inside it.
 */
#define CP_SERVER_GROUP_BASE 128

struct CP_ServerGroup
{
    /** Namespace as `pack/<name>.pack` spells it. */
    const char* name;
    /** idx number inside `server/pack`, 128..255. */
    int group;
};

/** The server-only namespaces, for the packer and for the test that checks the
 *  space is respected. */
const struct CP_ServerGroup*
cp_server_groups(int* out_count);

/** `ns`'s group, or -1 when it is not a server-only namespace. */
int
cp_server_group_for(const char* ns);

/**
 * Encode an `id -> name` table.
 *
 * Sparse by construction: the id is written per entry rather than implied by
 * position. A dense form would be wrong for `category`, whose ids are the
 * cache's own and run to 131 across 24 names — and it is also why this is not a
 * `RSCache_FileList`, which indexes members 0..n-1 and takes their real ids from
 * a reference table this pack does not have.
 *
 * Returns bytes written, or 0 if `out_capacity` is too small.
 */
uint32_t
cp_server_names_encode(
    const int* ids,
    const char* const* names,
    int count,
    uint8_t* out,
    uint32_t out_capacity);

/**
 * Read one back.
 *
 * `out_ids` and `out_names` are filled up to `max`; the names point into `data`
 * and are NUL-terminated there. Returns the entry count, or -1 when the table is
 * malformed.
 */
int
cp_server_names_decode(
    const uint8_t* data,
    int size,
    int* out_ids,
    const char** out_names,
    int max);

#endif
