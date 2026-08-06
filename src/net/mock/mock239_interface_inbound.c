#include "net/mock/mock239_interface_inbound.h"

#include <string.h>

static uint16_t
g2(const uint8_t* p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static int32_t
g4_le(const uint8_t* p)
{
    return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
                     ((uint32_t)p[3] << 24));
}

int
mock239_if_button_decode(
    const uint8_t* payload,
    int payload_len,
    int has_subop,
    struct Mock239IfButton* out)
{
    int expected = has_subop ? 10 : 9;

    if( !payload || !out || payload_len != expected )
        return 0;
    memset(out, 0, sizeof(*out));
    out->component_id = (int32_t)(((uint32_t)payload[0] << 24) | ((uint32_t)payload[1] << 16) |
                                  ((uint32_t)payload[2] << 8) | payload[3]);
    out->sub = (int16_t)g2(payload + 4);
    out->object_id = g2(payload + 6);
    out->op = payload[8];
    out->subop = has_subop ? payload[9] : -1;
    return out->op >= 1 && out->op <= 10;
}

enum Mock239IfButtonRoute
mock239_if_button_route(const struct Mock239IfButton* button)
{
    if( button && button->object_id != MOCK239_IF_OBJ_NONE && button->op <= 5 )
        return MOCK239_IF_ROUTE_OPHELD;
    return MOCK239_IF_ROUTE_COMPONENT;
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

    if( !payload || !out || payload_len < 12 )
        return 0;
    memset(out, 0, sizeof(*out));

    /* Statics.method9637's initial p2 is the outer VAR_SHORT length. The live
     * session has consumed it by this boundary. method13196 then writes child
     * as p2_alt3: low+128 first, high second. */
    child = (uint16_t)(((uint16_t)payload[1] << 8) | ((payload[0] - 128) & 0xff));
    out->child = (int16_t)child;
    out->object_id = g2(payload + 2);
    out->component_id = g4_le(payload + 4);
    out->crc = g4_le(payload + 8);
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
    if( !payload || !out || payload_len < 1 || payload[payload_len - 1] != 0 )
        return 0;
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
    uint64_t value = 0;

    if( !payload || !out || payload_len != 8 )
        return 0;
    for( int i = 0; i < 8; i++ )
        value = (value << 8) | payload[i];
    *out = (int64_t)value;
    return 1;
}

int
mock239_resume_object_decode(
    const uint8_t* payload,
    int payload_len,
    int32_t* out)
{
    if( !payload || !out || payload_len != 2 )
        return 0;
    *out = g2(payload);
    return 1;
}
