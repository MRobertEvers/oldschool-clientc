#include "test_harness.h"

#include <string.h>

void
test_parse(void)
{
    printf("TEST: parse\n");

    TEST_ASSERT(revconfig_parse_button_type("") == 0, "button empty");
    TEST_ASSERT(revconfig_parse_button_type("ok") == REVCONFIG_BUTTON_TYPE_OK, "ok");
    TEST_ASSERT(revconfig_parse_button_type("TARGET") == REVCONFIG_BUTTON_TYPE_TARGET, "target");
    TEST_ASSERT(revconfig_parse_button_type("close") == REVCONFIG_BUTTON_TYPE_CLOSE, "close");
    TEST_ASSERT(revconfig_parse_button_type("toggle") == REVCONFIG_BUTTON_TYPE_TOGGLE, "toggle");
    TEST_ASSERT(revconfig_parse_button_type("select") == REVCONFIG_BUTTON_TYPE_SELECT, "select");
    TEST_ASSERT(
        revconfig_parse_button_type("continue") == REVCONFIG_BUTTON_TYPE_CONTINUE, "continue");
    TEST_ASSERT(revconfig_parse_button_type("4") == 4, "button int");

    TEST_ASSERT(revconfig_parse_minimenu_action("") == 0, "action empty");
    TEST_ASSERT(
        revconfig_parse_minimenu_action("CANCEL") == REVCONFIG_MINIMENU_CANCEL, "CANCEL");
    TEST_ASSERT(
        revconfig_parse_minimenu_action("INV_BUTTON1") == REVCONFIG_MINIMENU_INV_BUTTON1,
        "INV_BUTTON1");
    TEST_ASSERT(
        revconfig_parse_minimenu_action("CLOSE_BUTTON") == REVCONFIG_MINIMENU_CLOSE_MODAL,
        "CLOSE_BUTTON alias");
    TEST_ASSERT(
        revconfig_parse_minimenu_action("TOGGLE_BUTTON") == REVCONFIG_MINIMENU_IF_BUTTON_TOGGLE,
        "TOGGLE_BUTTON alias");
    TEST_ASSERT(
        revconfig_parse_minimenu_action("ACCEPT_TRADEREQ") == REVCONFIG_MINIMENU_OPPLAYER_TRADEREQ,
        "ACCEPT_TRADEREQ alias");
    TEST_ASSERT(
        revconfig_parse_minimenu_action("737") == REVCONFIG_MINIMENU_CLOSE_MODAL, "numeric");

    /*
     * The client's own action, and the BAND it has to be in.
     *
     * The second assertion is the one worth having: every id in the reference's
     * range picks up the +2000 priority bias and has it stripped again at
     * dispatch, so a client-invented id parked among them arrives somewhere
     * else and its handler -- an equality test against the constant -- quietly
     * stops matching. Being >= the client base is what exempts it, and the
     * profiles that author this row have no way to notice if it stops being
     * true.
     */
    TEST_ASSERT(
        revconfig_parse_minimenu_action("PLUGIN_PANEL") == REVCONFIG_MINIMENU_PLUGIN_PANEL,
        "PLUGIN_PANEL");
    TEST_ASSERT(
        REVCONFIG_MINIMENU_PLUGIN_PANEL >= 500000,
        "PLUGIN_PANEL is in the client action band");
}

/**
 * The number grammar.
 *
 * The forms are what a profile actually wants to write -- a hex id, a packed
 * uid, a colour -- and the failures matter as much as the successes: the whole
 * point of the parser is that a typo is REPORTED and comes back 0 instead of
 * being read as the prefix atoi() could make sense of.
 */
void
test_parse_numbers(void)
{
    char const* end = NULL;
    int value = 0;
    struct RevConfigCameraItem camera;

    printf("TEST: parse numbers\n");

    /* Decimal, as before. */
    TEST_ASSERT(revconfig_parse_int("0") == 0, "zero");
    TEST_ASSERT(revconfig_parse_int("1088") == 1088, "decimal");
    TEST_ASSERT(revconfig_parse_int("-42") == -42, "negative decimal");
    TEST_ASSERT(revconfig_parse_int("  17  ") == 17, "surrounding space");
    TEST_ASSERT(revconfig_parse_int("") == 0, "empty is unstated");

    /* Hex, both spellings, and binary. */
    TEST_ASSERT(revconfig_parse_int("0x1088") == 0x1088, "0x hex");
    TEST_ASSERT(revconfig_parse_int("0XdeadBEE") == 0xdeadBEE, "0X hex, mixed case");
    TEST_ASSERT(revconfig_parse_int("8000h") == 0x8000, "h-suffixed hex");
    TEST_ASSERT(revconfig_parse_int("0FFh") == 0xFF, "h-suffixed hex, leading zero");
    TEST_ASSERT(revconfig_parse_int("0b1010_1010") == 0xAA, "binary");

    /* Digit grouping, in every base. */
    TEST_ASSERT(revconfig_parse_int("0x1000_0000") == 0x10000000, "grouped hex");
    TEST_ASSERT(revconfig_parse_int("1_000_000") == 1000000, "grouped decimal");

    /* Arithmetic, at C's precedence. */
    TEST_ASSERT(revconfig_parse_int("(1088 << 16) | 0xFF") == ((1088 << 16) | 0xFF), "packed uid");
    TEST_ASSERT(revconfig_parse_int("1088 << 16 | 0xFF") == ((1088 << 16) | 0xFF), "shift binds tighter than or");
    TEST_ASSERT(revconfig_parse_int("1 + 2 * 3") == 7, "* binds tighter than +");
    TEST_ASSERT(revconfig_parse_int("0xF0 & 0x3C ^ 0x03") == ((0xF0 & 0x3C) ^ 0x03), "& binds tighter than ^");
    TEST_ASSERT(revconfig_parse_int("~0") == -1, "unary ~");
    TEST_ASSERT(revconfig_parse_int("-(2 + 3)") == -5, "unary - on a group");
    TEST_ASSERT(revconfig_parse_int("0x8000_0000 >> 16") == 0x8000, "shift right is a bit move, not a sign smear");
    TEST_ASSERT(revconfig_parse_int("100 / 7 % 5") == ((100 / 7) % 5), "/ and % at one level");

    /* Colours. rgba() packs ARGB, which is the word the client blits -- so a
     * fully opaque white is the -1 an int field can hold, not a 33rd bit. */
    TEST_ASSERT(revconfig_parse_int("rgb(255, 0, 0)") == 0xFF0000, "rgb");
    TEST_ASSERT(revconfig_parse_int("RGB(0,128,255)") == 0x0080FF, "rgb is case-insensitive");
    TEST_ASSERT(revconfig_parse_int("rgba(16, 32, 48, 64)") == 0x40102030, "rgba is ARGB");
    TEST_ASSERT(revconfig_parse_int("rgba(255,255,255,255)") == -1, "opaque white is the 32-bit pattern");
    TEST_ASSERT(revconfig_parse_int("rgb(255, 0, 0) | 0x00FF00") == 0xFFFF00, "a colour is an operand like any other");

    /* `#` is a hex marker, not a colour type: the width is the field's. */
    TEST_ASSERT(revconfig_parse_int("#FF0000") == 0xFF0000, "#RRGGBB");
    TEST_ASSERT(revconfig_parse_int("#0f9") == 0x0F9, "# takes any digit count");
    TEST_ASSERT(revconfig_parse_int("#80FF0000") == (int)0x80FF0000u, "# carries the top bit");
    TEST_ASSERT(revconfig_parse_int("#FF0000 | 0x0000FF") == 0xFF00FF, "# is an operand");
    TEST_ASSERT(revconfig_parse_int("#") == 0, "# with no digits");
    TEST_ASSERT(revconfig_parse_int("#FFGG00") == 0, "# with a non-hex digit");

    /* hsl16(), the client's own palette index -- hue, saturation, lightness
     * packed as 6/3/7 bits. */
    TEST_ASSERT(revconfig_parse_int("hsl16(0, 0, 0)") == 0, "hsl16 black");
    TEST_ASSERT(revconfig_parse_int("hsl16(63, 7, 127)") == 0xFFFF, "hsl16 packs 6/3/7");
    TEST_ASSERT(revconfig_parse_int("hsl16(0, 7, 64)") == ((0 << 10) | (7 << 7) | 64), "hsl16 red");
    TEST_ASSERT(revconfig_parse_int("HSL16(1, 2, 3)") == ((1 << 10) | (2 << 7) | 3), "hsl16 is case-insensitive");
    TEST_ASSERT(revconfig_parse_int("hsl16(64, 0, 0)") == 0, "hue out of range");
    TEST_ASSERT(revconfig_parse_int("hsl16(0, 8, 0)") == 0, "saturation out of range");
    TEST_ASSERT(revconfig_parse_int("hsl16(0, 0, 128)") == 0, "lightness out of range");
    TEST_ASSERT(revconfig_parse_int("hsl16(0, 0)") == 0, "wrong argument count");

    /* Uids. */
    TEST_ASSERT(revconfig_parse_int("if(1088, 255)") == ((1088 << 16) | 255), "if() packs a uid");
    TEST_ASSERT(
        revconfig_parse_int("if(1088, -1)") == ((1088 << 16) | 0xFFFF),
        "if() takes -1 for no component");
    TEST_ASSERT(
        revconfig_parse_int("if(0x440, 0b1111)") == ((0x440 << 16) | 15),
        "if() halves are expressions");

    /* Typos are reported and come back 0, not read as a prefix. */
    TEST_ASSERT(revconfig_parse_int("12abc") == 0, "trailing letters");
    TEST_ASSERT(revconfig_parse_int("12 34") == 0, "two numbers");
    TEST_ASSERT(revconfig_parse_int("0x") == 0, "no digits after 0x");
    TEST_ASSERT(revconfig_parse_int("0b12") == 0, "2 is not a binary digit");
    TEST_ASSERT(revconfig_parse_int("none") == 0, "a bare word");
    TEST_ASSERT(revconfig_parse_int("(1 << 4") == 0, "unclosed group");
    TEST_ASSERT(revconfig_parse_int("1 / 0") == 0, "division by zero");
    TEST_ASSERT(revconfig_parse_int("rgb(300, 0, 0)") == 0, "channel out of range");
    TEST_ASSERT(revconfig_parse_int("rgb(1, 2)") == 0, "wrong argument count");
    TEST_ASSERT(revconfig_parse_int("hsv(1, 2, 3)") == 0, "unknown function");
    TEST_ASSERT(revconfig_parse_int("0x1_0000_0000") == 0, "does not fit 32 bits");

    /* The partial form, for a value with more than a number in it. */
    end = NULL;
    value = 0;
    TEST_ASSERT(revconfig_parse_int_expr("0x40 , 60]", &end, &value), "expr parses a prefix");
    TEST_ASSERT(value == 0x40, "expr value");
    /* Trailing space is skipped looking for an operator, so what is left is the
     * first thing that is not part of the expression. */
    TEST_ASSERT(end && *end == ',', "expr stops where the number ends");
    TEST_ASSERT(!revconfig_parse_int_expr("]", &end, &value), "expr refuses a non-number");

    /* A zoom band's bounds are expressions too. */
    memset(&camera, 0, sizeof(camera));
    TEST_ASSERT(revconfig_parse_camera_zoom("clamped:[0x100, 8 * 100]", &camera), "zoom expressions");
    TEST_ASSERT(camera.zoom_min == 0x100, "zoom min");
    TEST_ASSERT(camera.zoom_max == 800, "zoom max");
    memset(&camera, 0, sizeof(camera));
    TEST_ASSERT(revconfig_parse_camera_zoom("fixed:0x200", &camera), "fixed zoom hex");
    TEST_ASSERT(camera.zoom_height == 0x200, "fixed zoom height");
    memset(&camera, 0, sizeof(camera));
    TEST_ASSERT(!revconfig_parse_camera_zoom("clamped:[100 200]", &camera), "zoom band needs its comma");
}
