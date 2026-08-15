#include "test_harness.h"

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
    TEST_ASSERT(revconfig_parse_minimenu_action(NULL) == 0, "action null");
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
}
