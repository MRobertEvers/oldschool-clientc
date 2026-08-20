#include "torirsserver/mock239_interface_inbound.h"
#include <assert.h>

#include <string.h>

#include "rsprot_buffer.h"

/*
 * These used to be two private pointer-peeks — `g2(const uint8_t*)` and
 * `g4_le(const uint8_t*)` — reading a fixed offset with no cursor and no bounds
 * check. They are RSProt_Buffer's, and reading through a cursor is what makes
 * the length check below load-bearing rather than advisory: a short payload
 * latches an error instead of reading past the end.
 *
 * `g4_le` already carried the byte order in its name; that convention is now
 * the whole library's. See `docs/BUFFER_ACCESSOR_AUDIT.md`.
 */

int
mock239_if_button_decode(
    const uint8_t* payload,
    int payload_len,
    int has_subop,
    struct Mock239IfButton* out)
{
    int expected = has_subop ? 10 : 9;
    RSProt_Buffer b;

    if( payload_len != expected )
        return 0;
    assert(payload);
    assert(out);
    memset(out, 0, sizeof(*out));
    RSProt_BufferWrapRead(&b, payload, payload_len);
    out->component_id = RSProt_BufferG4Be(&b);
    out->sub = (int16_t)RSProt_BufferG2Be(&b);
    out->object_id = (uint16_t)RSProt_BufferG2Be(&b);
    out->op = RSProt_BufferG1(&b);
    out->subop = has_subop ? RSProt_BufferG1(&b) : -1;
    if( !RSProt_BufferOk(&b) )
        return 0;
    return out->op >= 1 && out->op <= 10;
}

enum Mock239IfButtonRoute
mock239_if_button_route(const struct Mock239IfButton* button)
{
    if( button && button->object_id != MOCK239_IF_OBJ_NONE && button->op <= 5 )
        return MOCK239_IF_ROUTE_OPHELD;
    return MOCK239_IF_ROUTE_COMPONENT;
}

int
mock239_if_button_backpack_op(const struct Mock239IfButton* button)
{
    assert(button);
    if( button->object_id == MOCK239_IF_OBJ_NONE )
        return 0;
    /* The revision-239 backpack's script_7779 uses enum_4303, rather than
     * consecutive IF_BUTTONX operations: generic Use owns op 1, then its
     * ObjType iop rows use 2, 3, 4, 6, and 7.  In particular, op 5 remains a
     * component action and must not turn into OPHELD4. */
    switch( button->op )
    {
    case 2:
        return 1;
    case 3:
        return 2;
    case 4:
        return 3;
    case 6:
        return 4;
    case 7:
        return 5;
    default:
        return 0;
    }
}

/** The exact inverse of class617.method13127 (signed ZigZag + unsigned LEB128). */
static int
decode_zigzag(
    const uint8_t* bytes,
    int len,
    int* pos,
    int32_t* out)
{
    uint32_t encoded = 0;

    for( int shift = 0; shift <= 28; shift += 7 )
    {
        uint8_t byte;

        if( *pos >= len )
            return 0;
        byte = bytes[(*pos)++];
        if( shift == 28 && (byte & 0xf0) != 0 )
            return 0; /* more than the 32 bits the Java int writer can emit */
        encoded |= (uint32_t)(byte & 0x7f) << shift;
        if( !(byte & 0x80) )
        {
            *out = (int32_t)((encoded >> 1) ^ (uint32_t)-(int32_t)(encoded & 1));
            return 1;
        }
    }
    return 0;
}

int
mock239_if_script_trigger_decode(
    const uint8_t* payload,
    int payload_len,
    const char* signature,
    struct Mock239IfScriptTrigger* out)
{
    uint16_t child;
    int typed_pos = 0;

    if( payload_len < 12 )
        return 0;
    assert(payload);
    assert(out);
    memset(out, 0, sizeof(*out));

    /* Statics.method9637's initial p2 is the outer VAR_SHORT length. The live
     * session has consumed it by this boundary. method13196 then writes child
     * as p2Alt3 -- low+128 first, high second -- which is `G2Le_add128`. That
     * order used to be spelled out by hand here, a third private copy of an
     * encoding the buffer library already had. */
    {
        RSProt_Buffer b;

        RSProt_BufferWrapRead(&b, payload, payload_len);
        child = (uint16_t)RSProt_BufferG2Le_add128(&b);
        out->child = (int16_t)child;
        out->object_id = (uint16_t)RSProt_BufferG2Be(&b);
        out->component_id = RSProt_BufferG4Le(&b);
        out->crc = RSProt_BufferG4Le(&b);
        if( !RSProt_BufferOk(&b) )
            return 0;
    }
    /* The typed tail stays a borrowed pointer: it is handed to the caller as a
     * range, not decoded here. */
    out->typed_bytes = payload + 12;
    out->typed_len = payload_len - 12;

    if( !signature )
        return 1;
    for( int i = 0; signature[i]; i++ )
    {
        struct Mock239IfScriptValue* value;

        if( i >= MOCK239_IF_SCRIPT_VALUE_MAX )
            return 0;
        value = &out->values[i];
        if( signature[i] == 'i' )
        {
            value->kind = MOCK239_IF_SCRIPT_INT;
            if( !decode_zigzag(out->typed_bytes, out->typed_len, &typed_pos, &value->as.integer) )
                return 0;
        }
        else
        {
            int start = typed_pos;

            value->kind = MOCK239_IF_SCRIPT_STRING;
            while( typed_pos < out->typed_len && out->typed_bytes[typed_pos] != 0 )
                typed_pos++;
            if( typed_pos >= out->typed_len )
                return 0;
            value->as.string.bytes = out->typed_bytes + start;
            value->as.string.len = typed_pos - start;
            typed_pos++; /* NUL */
        }
        out->value_count = i + 1;
    }
    return typed_pos == out->typed_len;
}

int
mock239_if_script_trigger_skill_guide_sub(
    const uint8_t* payload,
    int payload_len,
    int32_t expected_component,
    int32_t* out_sub,
    int32_t* out_object_id)
{
    struct Mock239IfScriptTrigger trigger;

    if( !out_sub || !out_object_id ||
        !mock239_if_script_trigger_decode(payload, payload_len, "i", &trigger) ||
        trigger.component_id != expected_component ||
        trigger.crc != MOCK239_SKILL_GUIDE_TRIGGER_CRC || trigger.value_count != 1 ||
        trigger.values[0].kind != MOCK239_IF_SCRIPT_INT )
        return 0;
    *out_sub = trigger.child != -1 ? trigger.child : trigger.values[0].as.integer;
    *out_object_id = trigger.object_id;
    return 1;
}

int
mock239_resume_text_decode(
    const uint8_t* payload,
    int payload_len,
    struct Mock239ByteString* out)
{
    assert(payload);
    if( payload_len < 1 || payload[payload_len - 1] != 0 )
        return 0;
    assert(out);
    /* The golden gjstr writer rejects embedded NULs. */
    if( payload_len > 1 && memchr(payload, 0, (size_t)payload_len - 1) )
        return 0;
    out->bytes = payload;
    out->len = payload_len - 1;
    return 1;
}

int
mock239_resume_count_long_decode(
    const uint8_t* payload,
    int payload_len,
    int64_t* out)
{
    RSProt_Buffer b;

    if( payload_len != 8 )
        return 0;
    assert(payload);
    assert(out);
    RSProt_BufferWrapRead(&b, payload, payload_len);
    *out = RSProt_BufferG8Be(&b);
    return RSProt_BufferOk(&b);
}

int
mock239_resume_object_decode(
    const uint8_t* payload,
    int payload_len,
    int32_t* out)
{
    RSProt_Buffer b;

    if( payload_len != 2 )
        return 0;
    assert(payload);
    assert(out);
    RSProt_BufferWrapRead(&b, payload, payload_len);
    *out = RSProt_BufferG2Be(&b);
    return RSProt_BufferOk(&b);
}
