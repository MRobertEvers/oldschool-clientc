#ifndef SRC_NET_MOCK_MOCK239_INTERFACE_INBOUND_H
#define SRC_NET_MOCK_MOCK239_INTERFACE_INBOUND_H

#include <stddef.h>
#include <stdint.h>

/* The IF_BUTTONX object sentinel used by the golden revision-239 client. */
#define MOCK239_IF_OBJ_NONE 0xffff

/** Exact, lossless decode of IF_BUTTONX / IF_SUBOP. */
struct Mock239IfButton
{
    int32_t component_id;
    int32_t sub;
    int32_t object_id;
    int32_t op;
    /** -1 for IF_BUTTONX; the actual trailing byte for IF_SUBOP. */
    int32_t subop;
};

int
mock239_if_button_decode(
    const uint8_t* payload,
    int payload_len,
    int has_subop,
    struct Mock239IfButton* out);

enum Mock239IfButtonRoute
{
    MOCK239_IF_ROUTE_OPHELD,
    MOCK239_IF_ROUTE_COMPONENT,
};

/** Golden-family route after decoding: object ops stop at five; component ops
 * continue through ten even when the row itself contains an object. */
enum Mock239IfButtonRoute
mock239_if_button_route(const struct Mock239IfButton* button);

/**
 * Translate a revision-239 backpack row number back to the classic OPHELD
 * trigger number used by content.
 *
 * The IF3 inventory inserts its generic Use row before ObjType inventory
 * actions. script_7779's enum_4303 consequently maps classic OPHELD1..5 to
 * IF_BUTTONX ops 2, 3, 4, 6, and 7 respectively. Wire op 5 is intentionally
 * absent from that mapping: it remains a component operation, not OPHELD4.
 * Returns zero for a row that is not a backpack ObjType action.
 */
int
mock239_if_button_backpack_op(const struct Mock239IfButton* button);

enum Mock239IfScriptValueKind
{
    MOCK239_IF_SCRIPT_INT,
    MOCK239_IF_SCRIPT_STRING,
};

struct Mock239ByteString
{
    /** Borrowed from the packet body; not NUL terminated. */
    const uint8_t* bytes;
    int len;
};

struct Mock239IfScriptValue
{
    enum Mock239IfScriptValueKind kind;
    union
    {
        int32_t integer;
        struct Mock239ByteString string;
    } as;
};

#define MOCK239_IF_SCRIPT_VALUE_MAX 32

/**
 * Golden IF_SCRIPT_TRIGGER representation.
 *
 * `payload` starts at the child field. The packet's leading p2 written by
 * Statics.method9637 is the IF_SCRIPT_TRIGGER VAR_SHORT frame length, and
 * mock230_session consumes it before calling the inbound translator.
 *
 * The packet does not carry a type signature. The server is expected to look
 * it up from `crc`; consequently the raw tail is always retained. Passing a
 * non-NULL signature decodes that tail into `values` too: `i` is the client's
 * ZigZag/LEB128 integer and every other signature character is a CP-1252,
 * NUL-terminated string, exactly as CS2 opcode 2929 builds its Object[].
 *
 * This makes an unknown CRC lossless rather than guessed. A future script
 * signature registry can decode it later without changing the wire decoder.
 */
struct Mock239IfScriptTrigger
{
    int32_t crc;
    int32_t component_id;
    int32_t child;
    int32_t object_id;

    /** Borrowed byte-for-byte tail following the four fixed fields. */
    const uint8_t* typed_bytes;
    int typed_len;

    int value_count;
    struct Mock239IfScriptValue values[MOCK239_IF_SCRIPT_VALUE_MAX];
};

int
mock239_if_script_trigger_decode(
    const uint8_t* payload,
    int payload_len,
    const char* signature,
    struct Mock239IfScriptTrigger* out);

#define MOCK239_SKILL_GUIDE_TRIGGER_CRC INT32_C(1707091816)

/** Current typed consumer: script9189 declares signature `i`; its child of -1
 * means the typed quest id becomes IF_BUTTON1's sub. */
int
mock239_if_script_trigger_skill_guide_sub(
    const uint8_t* payload,
    int payload_len,
    int32_t expected_component,
    int32_t* out_sub,
    int32_t* out_object_id);

/** Exact scalar decoders for the four additional rev-239 resume packets. */
int
mock239_resume_text_decode(
    const uint8_t* payload,
    int payload_len,
    struct Mock239ByteString* out);

int
mock239_resume_count_long_decode(
    const uint8_t* payload,
    int payload_len,
    int64_t* out);

int
mock239_resume_object_decode(
    const uint8_t* payload,
    int payload_len,
    int32_t* out);

#endif /* SRC_NET_MOCK_MOCK239_INTERFACE_INBOUND_H */
