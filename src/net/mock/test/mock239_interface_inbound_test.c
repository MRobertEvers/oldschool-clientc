/*
 * Focused golden-byte tests for revision-239 interface callbacks.
 *
 * These arrays are the bytes written by the authoritative deob's class617
 * primitives, not round trips through a second implementation. In particular,
 * IF_SCRIPT_TRIGGER's frame and fixed fields come from Statics.method9637:
 *
 *   p2(VAR_SHORT length), p2Alt3(child), p2(object), p4LE(component),
 *   p4LE(crc), then ZigZag/LEB128 ints or NUL strings selected by crc's
 *   signature.
 *
 * The live session consumes that first p2 before translation. Fixtures retain
 * it as `*_frame`, assert it, then hand only `frame + 2` to the decoder. This
 * prevents a direct-decoder test from accidentally exercising a boundary the
 * real server can never present.
 */

#include "net/mock/mock239_inbound.h"
#include "net/mock/mock239_interface_inbound.h"
#include "net/rev/osrs239/packetout.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int checks;
static int failures;

#define CHECK(cond, ...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        checks++;                                                                                  \
        if( !(cond) )                                                                              \
        {                                                                                          \
            failures++;                                                                            \
            fprintf(stderr, "FAIL: ");                                                             \
            fprintf(stderr, __VA_ARGS__);                                                          \
            fprintf(stderr, "\n");                                                                 \
        }                                                                                          \
    } while( 0 )

static const struct Osrs239PacketOutDef*
packet_for_code(int code)
{
    size_t count =
        sizeof(g_packet_out_definitions_osrs239) / sizeof(g_packet_out_definitions_osrs239[0]);

    for( size_t i = 0; i < count; i++ )
        if( g_packet_out_definitions_osrs239[i].code == code )
            return &g_packet_out_definitions_osrs239[i];
    return NULL;
}

static void
check_table(void)
{
    struct Expected
    {
        int code;
        int name;
        int length;
    } expected[] = {
        { 56, PKTOUT_NAME_IF_SCRIPT_TRIGGER,         PKTOUT_LENGTH_VARU16 },
        { 26, PKTOUT_NAME_RESUME_P_NAMEDIALOG,       PKTOUT_LENGTH_VARU8  },
        { 64, PKTOUT_NAME_RESUME_P_STRINGDIALOG,     PKTOUT_LENGTH_VARU8  },
        { 63, PKTOUT_NAME_RESUME_P_COUNTDIALOG_LONG, 8                    },
        { 32, PKTOUT_NAME_RESUME_P_OBJDIALOG,        2                    },
    };

    for( size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++ )
    {
        const struct Osrs239PacketOutDef* def = packet_for_code(expected[i].code);

        CHECK(def != NULL, "opcode %d has a table row", expected[i].code);
        if( def )
        {
            CHECK(
                def->name == expected[i].name,
                "opcode %d canonical name is %d (got %d)",
                expected[i].code,
                expected[i].name,
                def->name);
            CHECK(
                def->length == expected[i].length,
                "opcode %d length is %d (got %d)",
                expected[i].code,
                expected[i].length,
                def->length);
        }
    }
}

static void
check_buttons(void)
{
    /* 860:17, slot 27, item 314, component op 10. This is the case the old
     * OPHELD1..5 fan-out discarded solely because an object was present. */
    static const uint8_t item_op10[] = {
        0x03, 0x5c, 0x00, 0x11, 0x00, 0x1b, 0x01, 0x3a, 0x0a,
    };
    /* The same endpoint selecting submenu entry 9 of op 3. */
    static const uint8_t subop[] = {
        0x03, 0x5c, 0x00, 0x11, 0x00, 0x07, 0x10, 0x37, 0x03, 0x09,
    };
    struct Mock239IfButton got;
    uint8_t translated[32];
    int translated_len = -1;
    int name;

    CHECK(
        mock239_if_button_decode(item_op10, sizeof(item_op10), 0, &got),
        "IF_BUTTONX golden bytes decode");
    CHECK(
        got.component_id == 0x035c0011 && got.sub == 27 && got.object_id == 314 && got.op == 10 &&
            got.subop == -1,
        "IF_BUTTONX retains uid/sub/object/op and no synthetic subop");
    CHECK(
        mock239_if_button_route(&got) == MOCK239_IF_ROUTE_COMPONENT,
        "item-backed op 10 routes as IF_BUTTON10, not nonexistent OPHELD10");
    name = mock239_inbound_translate(
        47,
        PKTOUT_NAME_IF_BUTTONX,
        item_op10,
        sizeof(item_op10),
        translated,
        sizeof(translated),
        &translated_len);
    CHECK(name == PKTOUT_NAME_IF_BUTTONX, "item-backed op 10 routes through the unified callback");
    CHECK(
        translated_len == (int)sizeof(item_op10) &&
            memcmp(translated, item_op10, sizeof(item_op10)) == 0,
        "item-backed op 10 remains lossless");

    CHECK(mock239_if_button_decode(subop, sizeof(subop), 1, &got), "IF_SUBOP golden bytes decode");
    CHECK(
        got.object_id == 4151 && got.op == 3 && got.subop == 9,
        "IF_SUBOP preserves its trailing submenu index");
    CHECK(
        mock239_if_button_route(&got) == MOCK239_IF_ROUTE_OPHELD,
        "item submenu op 3 retains OPHELD3 semantics");
    name = mock239_inbound_translate(
        40,
        PKTOUT_NAME_IF_SUBOP,
        subop,
        sizeof(subop),
        translated,
        sizeof(translated),
        &translated_len);
    CHECK(
        name == PKTOUT_NAME_IF_SUBOP && translated_len == (int)sizeof(subop) &&
            memcmp(translated, subop, sizeof(subop)) == 0,
        "IF_SUBOP routes without dropping byte 10");

    {
        uint8_t bad[sizeof(item_op10)];
        memcpy(bad, item_op10, sizeof(bad));
        bad[8] = 11;
        CHECK(
            mock239_inbound_translate(
                47,
                PKTOUT_NAME_IF_BUTTONX,
                bad,
                sizeof(bad),
                translated,
                sizeof(translated),
                &translated_len) == PKTOUT_NAME_NONE,
            "out-of-range component op is rejected");
    }
}

static void
check_script_trigger(void)
{
    /* script9189: _2929(1707091816, 860:17, -1, quest=300, "i").
     * inner=12 fixed + 2 typed; child -1 under p2Alt3 is 7f ff; component and
     * crc are little-endian; 300 -> ZigZag 600 -> d8 04. */
    static const uint8_t skill_guide_frame[] = {
        0x00, 0x0e, 0x7f, 0xff, 0xff, 0xff, 0x11, 0x00,
        0x5c, 0x03, 0x68, 0x27, 0xc0, 0x65, 0xd8, 0x04,
    };
    /* A second golden primitive fixture for typed ordering: child=42,
     * object=314, component=0x12345678, crc=0x89abcdef,
     * values (-65, "Rune", 42) under signature "isi". */
    static const uint8_t mixed_frame[] = {
        0x00, 0x14, 0xaa, 0x00, 0x01, 0x3a, 0x78, 0x56, 0x34, 0x12, 0xef,
        0xcd, 0xab, 0x89, 0x81, 0x01, 0x52, 0x75, 0x6e, 0x65, 0x00, 0x54,
    };
    const uint8_t* skill_guide = skill_guide_frame + 2;
    int skill_guide_len = (int)sizeof(skill_guide_frame) - 2;
    const uint8_t* mixed = mixed_frame + 2;
    int mixed_len = (int)sizeof(mixed_frame) - 2;
    struct Mock239IfScriptTrigger got;
    uint8_t translated[64];
    int translated_len = -1;

    CHECK(
        ((int)skill_guide_frame[0] << 8 | skill_guide_frame[1]) == skill_guide_len &&
            ((int)mixed_frame[0] << 8 | mixed_frame[1]) == mixed_len,
        "golden VAR_SHORT frames state the payload lengths consumed by the session");
    CHECK(
        mock239_if_script_trigger_decode(skill_guide, skill_guide_len, NULL, &got),
        "IF_SCRIPT_TRIGGER fixed fields decode without guessing a signature");
    CHECK(
        got.component_id == 56360977 && got.child == -1 && got.object_id == 65535 &&
            got.crc == 1707091816 && got.typed_len == 2 && got.typed_bytes[0] == 0xd8 &&
            got.typed_bytes[1] == 0x04,
        "unknown-signature representation retains all fixed fields and typed bytes");
    CHECK(
        mock239_if_script_trigger_decode(skill_guide, skill_guide_len, "i", &got) &&
            got.value_count == 1 && got.values[0].kind == MOCK239_IF_SCRIPT_INT &&
            got.values[0].as.integer == 300,
        "known skill-guide signature decodes quest id 300");
    {
        int32_t sub;
        int32_t object_id;

        CHECK(
            mock239_if_script_trigger_skill_guide_sub(
                skill_guide, skill_guide_len, 56360977, &sub, &object_id) &&
                sub == 300 && object_id == MOCK239_IF_OBJ_NONE,
            "script9189 routes typed quest id as IF_BUTTON1 sub");
    }
    CHECK(
        mock239_inbound_translate(
            56,
            PKTOUT_NAME_IF_SCRIPT_TRIGGER,
            skill_guide,
            skill_guide_len,
            translated,
            sizeof(translated),
            &translated_len) == PKTOUT_NAME_IF_SCRIPT_TRIGGER &&
            translated_len == skill_guide_len &&
            memcmp(translated, skill_guide, (size_t)skill_guide_len) == 0,
        "IF_SCRIPT_TRIGGER reaches its canonical router losslessly");

    CHECK(
        mock239_if_script_trigger_decode(mixed, mixed_len, "isi", &got),
        "mixed typed values decode under their crc-selected signature");
    CHECK(
        got.child == 42 && got.object_id == 314 && got.component_id == 0x12345678 &&
            (uint32_t)got.crc == 0x89abcdefu,
        "mixed fixture fixed fields preserve every golden byte order");
    CHECK(
        got.value_count == 3 && got.values[0].as.integer == -65 &&
            got.values[1].kind == MOCK239_IF_SCRIPT_STRING && got.values[1].as.string.len == 4 &&
            memcmp(got.values[1].as.string.bytes, "Rune", 4) == 0 && got.values[2].as.integer == 42,
        "ZigZag ints and NUL string retain typed order");

    {
        uint8_t truncated[sizeof(skill_guide_frame) - 3];
        memcpy(truncated, skill_guide, sizeof(truncated));
        CHECK(
            !mock239_if_script_trigger_decode(truncated, sizeof(truncated), "i", &got),
            "truncated typed integer is rejected after real framing");
    }
    CHECK(
        !mock239_if_script_trigger_decode(skill_guide, skill_guide_len, "s", &got),
        "wrong signature cannot silently consume an integer tail");
}

static void
check_resumes(void)
{
    /* Existing callbacks included as control cases: 595:38 under p4Alt3 plus
     * sub -1, and normal p4 count 123456. */
    static const uint8_t pause_body[] = { 0x53, 0x02, 0x26, 0x00, 0xff, 0xff };
    static const uint8_t pause_normalized[] = { 0x02, 0x53, 0x00, 0x26, 0xff, 0xff };
    /* chatmenu:options (219:1), dynamic row 1. The uid is g4Alt3 on the
     * revision-239 wire; the row is the trailing plain g2. */
    static const uint8_t choice_body[] = { 0xdb, 0x00, 0x01, 0x00, 0x00, 0x01 };
    static const uint8_t choice_normalized[] = { 0x00, 0xdb, 0x00, 0x01, 0x00, 0x01 };
    static const uint8_t count_body[] = { 0x00, 0x01, 0xe2, 0x40 };
    static const uint8_t name_body[] = { 'Z', 'e', 'z', 'i', 'm', 'a', 0 };
    static const uint8_t string_body[] = { 'r', 'u', 'n', 'e', 0 };
    static const uint8_t long_body[] = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef };
    static const uint8_t object_body[] = { 0xa4, 0x2f };
    struct Mock239ByteString text;
    uint8_t translated[32];
    int translated_len;
    int64_t count;
    int32_t object_id;

    CHECK(
        mock239_inbound_translate(
            115,
            PKTOUT_NAME_RESUME_PAUSEBUTTON,
            pause_body,
            sizeof(pause_body),
            translated,
            sizeof(translated),
            &translated_len) == PKTOUT_NAME_RESUME_PAUSEBUTTON &&
            translated_len == (int)sizeof(pause_normalized) &&
            memcmp(translated, pause_normalized, sizeof(pause_normalized)) == 0,
        "RESUME_PAUSEBUTTON decodes p4Alt3 and preserves its dynamic-child sub-id");
    CHECK(
        mock239_inbound_translate(
            115,
            PKTOUT_NAME_RESUME_PAUSEBUTTON,
            choice_body,
            sizeof(choice_body),
            translated,
            sizeof(translated),
            &translated_len) == PKTOUT_NAME_RESUME_PAUSEBUTTON &&
            translated_len == (int)sizeof(choice_normalized) &&
            memcmp(translated, choice_normalized, sizeof(choice_normalized)) == 0,
        "chatmenu row 1 retains sub=1 after the golden six-byte decode");
    CHECK(
        mock239_inbound_translate(
            75,
            PKTOUT_NAME_RESUME_P_COUNTDIALOG,
            count_body,
            sizeof(count_body),
            translated,
            sizeof(translated),
            &translated_len) == PKTOUT_NAME_RESUME_P_COUNTDIALOG &&
            translated_len == (int)sizeof(count_body) &&
            memcmp(translated, count_body, sizeof(count_body)) == 0,
        "existing RESUME_P_COUNTDIALOG control stays big-endian p4");

    CHECK(
        mock239_resume_text_decode(name_body, sizeof(name_body), &text) && text.len == 6 &&
            memcmp(text.bytes, "Zezima", 6) == 0,
        "RESUME_P_NAMEDIALOG gjstr decodes");
    CHECK(
        mock239_resume_text_decode(string_body, sizeof(string_body), &text) && text.len == 4 &&
            memcmp(text.bytes, "rune", 4) == 0,
        "RESUME_P_STRINGDIALOG gjstr decodes");
    CHECK(
        mock239_resume_count_long_decode(long_body, sizeof(long_body), &count) &&
            (uint64_t)count == UINT64_C(0x0123456789abcdef),
        "RESUME_P_COUNTDIALOG_LONG is big-endian p8");
    CHECK(
        mock239_resume_object_decode(object_body, sizeof(object_body), &object_id) &&
            object_id == 42031,
        "RESUME_P_OBJDIALOG is big-endian p2");

#define CHECK_RESUME(opcode, canonical, bytes)                                                     \
    do                                                                                             \
    {                                                                                              \
        translated_len = -1;                                                                       \
        CHECK(                                                                                     \
            mock239_inbound_translate(                                                             \
                (opcode),                                                                          \
                (canonical),                                                                       \
                (bytes),                                                                           \
                sizeof(bytes),                                                                     \
                translated,                                                                        \
                sizeof(translated),                                                                \
                &translated_len) == (canonical) &&                                                 \
                translated_len == (int)sizeof(bytes) &&                                            \
                memcmp(translated, (bytes), sizeof(bytes)) == 0,                                   \
            "opcode %d routes exact resume body",                                                  \
            (opcode));                                                                             \
    } while( 0 )
    CHECK_RESUME(26, PKTOUT_NAME_RESUME_P_NAMEDIALOG, name_body);
    CHECK_RESUME(64, PKTOUT_NAME_RESUME_P_STRINGDIALOG, string_body);
    CHECK_RESUME(63, PKTOUT_NAME_RESUME_P_COUNTDIALOG_LONG, long_body);
    CHECK_RESUME(32, PKTOUT_NAME_RESUME_P_OBJDIALOG, object_body);
#undef CHECK_RESUME

    {
        static const uint8_t unterminated[] = { 'n', 'o' };
        CHECK(
            mock239_inbound_translate(
                26,
                PKTOUT_NAME_RESUME_P_NAMEDIALOG,
                unterminated,
                sizeof(unterminated),
                translated,
                sizeof(translated),
                &translated_len) == PKTOUT_NAME_NONE,
            "unterminated name callback is rejected");
    }
}

int
main(void)
{
    check_table();
    check_buttons();
    check_script_trigger();
    check_resumes();

    fprintf(stderr, "mock239-interface-inbound: %d checks, %d failure(s)\n", checks, failures);
    return failures ? 1 : 0;
}
