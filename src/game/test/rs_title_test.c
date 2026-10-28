/*
 * Title-screen model: typing, focus, submit, and the per-revision caps.
 *
 * The point of a leaf model is that this file needs no client, no cache and no
 * server to ask whether the login form behaves -- so every question the form
 * raises gets asked here rather than by launching the game and squinting.
 */

#include "game/rs_title.h"
#include "input/torirs_keymap.h"

#include <stdio.h>
#include <string.h>

static int g_failures;
static int g_checks;

#define TEST_ASSERT(cond, msg)                                                                     \
    do                                                                                             \
    {                                                                                              \
        g_checks++;                                                                                \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__);                        \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

/* A plain character: key_typed -1, character in key_pressed (see rs_chat.c). */
static int
type_char(
    struct RS_Title* title,
    int ch)
{
    return RS_Title_HandleKey(title, -1, ch);
}

static int
press(
    struct RS_Title* title,
    int osrs_key)
{
    return RS_Title_HandleKey(title, osrs_key, 0);
}

static void
type_string(
    struct RS_Title* title,
    char const* text)
{
    for( char const* p = text; *p; p++ )
        type_char(title, (unsigned char)*p);
}

/* The old lane's configuration: the reference CHARSET, 12/20 caps. */
static void
configure_old_lane(struct RS_Title* title)
{
    static char const k_charset[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!\"$%^&*()-_=+[{]};:'@#~,<.>/"
        "?\\| ";

    title->field_cfg[RS_TITLE_FIELD_USERNAME].maxlen = 12;
    snprintf(
        title->field_cfg[RS_TITLE_FIELD_USERNAME].charset,
        sizeof(title->field_cfg[RS_TITLE_FIELD_USERNAME].charset),
        "%s",
        k_charset);
    title->field_cfg[RS_TITLE_FIELD_PASSWORD].maxlen = 20;
    snprintf(
        title->field_cfg[RS_TITLE_FIELD_PASSWORD].charset,
        sizeof(title->field_cfg[RS_TITLE_FIELD_PASSWORD].charset),
        "%s",
        k_charset);
}

static void
test_init(void)
{
    struct RS_Title title;

    RS_Title_Init(&title);
    TEST_ASSERT(title.screen == RS_TITLE_MAIN_MENU, "starts on the front menu");
    TEST_ASSERT(title.focus == RS_TITLE_FIELD_USERNAME, "starts focused on the username");
    TEST_ASSERT(title.progress_percent == -1, "starts with no progress bar");
    TEST_ASSERT(!title.submit_requested, "starts with nothing submitted");
    TEST_ASSERT(
        RS_Title_FieldText(&title, RS_TITLE_FIELD_USERNAME)[0] == '\0', "username starts empty");
}

/* The menu takes no typing: a keystroke on it must not silently fill a field
 * the player cannot see. */
static void
test_menu_ignores_typing(void)
{
    struct RS_Title title;

    RS_Title_Init(&title);
    configure_old_lane(&title);
    TEST_ASSERT(type_char(&title, 'a') == 0, "menu ignores a typed character");
    TEST_ASSERT(
        RS_Title_FieldText(&title, RS_TITLE_FIELD_USERNAME)[0] == '\0',
        "menu typing left the username empty");
}

static void
test_typing_and_focus(void)
{
    struct RS_Title title;

    RS_Title_Init(&title);
    configure_old_lane(&title);
    RS_Title_HandleAction(&title, RS_TITLE_ACTION_EXISTING_USER);
    TEST_ASSERT(title.screen == RS_TITLE_LOGIN_FORM, "existing user opens the form");

    type_string(&title, "bob");
    TEST_ASSERT(
        strcmp(RS_Title_FieldText(&title, RS_TITLE_FIELD_USERNAME), "bob") == 0, "username types");

    press(&title, TORIRS_OSRSKEY_TAB);
    TEST_ASSERT(title.focus == RS_TITLE_FIELD_PASSWORD, "tab moves to the password");
    type_string(&title, "hunter2");
    TEST_ASSERT(
        strcmp(RS_Title_FieldText(&title, RS_TITLE_FIELD_PASSWORD), "hunter2") == 0,
        "password types");
    TEST_ASSERT(
        strcmp(RS_Title_FieldText(&title, RS_TITLE_FIELD_USERNAME), "bob") == 0,
        "typing the password left the username alone");

    press(&title, TORIRS_OSRSKEY_TAB);
    TEST_ASSERT(title.focus == RS_TITLE_FIELD_USERNAME, "tab wraps back to the username");

    press(&title, TORIRS_OSRSKEY_BACKSPACE);
    TEST_ASSERT(
        strcmp(RS_Title_FieldText(&title, RS_TITLE_FIELD_USERNAME), "bo") == 0,
        "backspace deletes one character");
}

/* Backspace on an empty field reports "nothing changed" rather than churning a
 * redraw and an epoch bump every time a player leans on the key. */
static void
test_backspace_on_empty(void)
{
    struct RS_Title title;

    RS_Title_Init(&title);
    configure_old_lane(&title);
    RS_Title_SetScreen(&title, RS_TITLE_LOGIN_FORM);
    TEST_ASSERT(press(&title, TORIRS_OSRSKEY_BACKSPACE) == 0, "backspace on empty changes nothing");
}

static void
test_caps_are_per_field_and_per_revision(void)
{
    struct RS_Title title;

    RS_Title_Init(&title);
    configure_old_lane(&title);
    RS_Title_SetScreen(&title, RS_TITLE_LOGIN_FORM);

    type_string(&title, "abcdefghijklmnopqrst");
    TEST_ASSERT(
        strlen(RS_Title_FieldText(&title, RS_TITLE_FIELD_USERNAME)) == 12,
        "old-lane username caps at 12");

    press(&title, TORIRS_OSRSKEY_TAB);
    type_string(&title, "abcdefghijklmnopqrstuvwxyz");
    TEST_ASSERT(
        strlen(RS_Title_FieldText(&title, RS_TITLE_FIELD_PASSWORD)) == 20,
        "old-lane password caps at 20");

    /* The modern lane's username is 320 characters, which is why the cap is
     * configuration and not a constant. */
    RS_Title_Init(&title);
    title.field_cfg[RS_TITLE_FIELD_USERNAME].maxlen = 320;
    RS_Title_SetScreen(&title, RS_TITLE_LOGIN_FORM);
    for( int i = 0; i < 400; i++ )
        type_char(&title, 'a');
    TEST_ASSERT(
        strlen(RS_Title_FieldText(&title, RS_TITLE_FIELD_USERNAME)) == 320,
        "modern username caps at its own 320");
}

/* A character the revision's font cannot draw is a character the server would
 * be told about and the player could not see. */
static void
test_charset_filters(void)
{
    struct RS_Title title;

    RS_Title_Init(&title);
    configure_old_lane(&title);
    RS_Title_SetScreen(&title, RS_TITLE_LOGIN_FORM);

    TEST_ASSERT(type_char(&title, '`') == 0, "a character outside the charset is refused");
    TEST_ASSERT(
        RS_Title_FieldText(&title, RS_TITLE_FIELD_USERNAME)[0] == '\0',
        "the refused character was not appended");
    TEST_ASSERT(type_char(&title, 'a') == 1, "a character inside the charset is taken");

    /* An unset charset means "anything printable", so a revision that declares
     * none is not silently restricted to the 254 set. */
    RS_Title_Init(&title);
    RS_Title_SetScreen(&title, RS_TITLE_LOGIN_FORM);
    TEST_ASSERT(type_char(&title, '`') == 1, "no declared charset accepts any printable");
}

static void
test_enter_advances_then_submits(void)
{
    struct RS_Title title;

    RS_Title_Init(&title);
    configure_old_lane(&title);
    RS_Title_SetScreen(&title, RS_TITLE_LOGIN_FORM);

    type_string(&title, "bob");
    press(&title, TORIRS_OSRSKEY_ENTER);
    TEST_ASSERT(title.focus == RS_TITLE_FIELD_PASSWORD, "enter on the username advances");
    TEST_ASSERT(!title.submit_requested, "enter on the username did not submit");

    type_string(&title, "pw");
    press(&title, TORIRS_OSRSKEY_ENTER);
    TEST_ASSERT(title.submit_requested, "enter on the password submits");
}

static void
test_escape_returns_to_menu(void)
{
    struct RS_Title title;

    RS_Title_Init(&title);
    configure_old_lane(&title);
    RS_Title_SetScreen(&title, RS_TITLE_LOGIN_FORM);
    press(&title, TORIRS_OSRSKEY_ESCAPE);
    TEST_ASSERT(title.screen == RS_TITLE_MAIN_MENU, "escape returns to the menu");
}

/* Cancel must not leave the password sitting in a buffer after the player
 * explicitly backed out. */
static void
test_cancel_clears_credentials(void)
{
    struct RS_Title title;

    RS_Title_Init(&title);
    configure_old_lane(&title);
    RS_Title_SetScreen(&title, RS_TITLE_LOGIN_FORM);
    type_string(&title, "bob");
    press(&title, TORIRS_OSRSKEY_TAB);
    type_string(&title, "hunter2");

    RS_Title_HandleAction(&title, RS_TITLE_ACTION_CANCEL);
    TEST_ASSERT(title.screen == RS_TITLE_MAIN_MENU, "cancel returns to the menu");
    TEST_ASSERT(
        RS_Title_FieldText(&title, RS_TITLE_FIELD_USERNAME)[0] == '\0', "cancel cleared the username");
    TEST_ASSERT(
        RS_Title_FieldText(&title, RS_TITLE_FIELD_PASSWORD)[0] == '\0', "cancel cleared the password");
}

/* Opening the form with a username already in hand (remembered, or from
 * --user) puts the caret where the player's next keystroke belongs. */
static void
test_prefilled_username_focuses_password(void)
{
    struct RS_Title title;

    RS_Title_Init(&title);
    configure_old_lane(&title);
    RS_Title_SetFieldText(&title, RS_TITLE_FIELD_USERNAME, "bob");
    RS_Title_SetScreen(&title, RS_TITLE_LOGIN_FORM);
    TEST_ASSERT(title.focus == RS_TITLE_FIELD_PASSWORD, "a prefilled username focuses the password");

    RS_Title_Init(&title);
    configure_old_lane(&title);
    RS_Title_SetScreen(&title, RS_TITLE_LOGIN_FORM);
    TEST_ASSERT(title.focus == RS_TITLE_FIELD_USERNAME, "an empty form focuses the username");
}

/* SetFieldText is how autologin and a remembered username get in, so it has to
 * respect the same cap typing does. */
static void
test_set_field_text_truncates(void)
{
    struct RS_Title title;

    RS_Title_Init(&title);
    configure_old_lane(&title);
    RS_Title_SetFieldText(&title, RS_TITLE_FIELD_USERNAME, "averylongusernameindeed");
    TEST_ASSERT(
        strlen(RS_Title_FieldText(&title, RS_TITLE_FIELD_USERNAME)) == 12,
        "prefilled text truncates to the cap");
}

static void
test_messages_and_progress(void)
{
    struct RS_Title title;

    RS_Title_Init(&title);
    RS_Title_SetMessages(&title, "Line one.", "Line two.", NULL);
    TEST_ASSERT(strcmp(title.messages[0], "Line one.") == 0, "message line 1");
    TEST_ASSERT(strcmp(title.messages[1], "Line two.") == 0, "message line 2");
    TEST_ASSERT(title.messages[2][0] == '\0', "a NULL message line clears it");

    RS_Title_SetProgress(&title, 60, "Loading fonts");
    TEST_ASSERT(title.progress_percent == 60, "progress percent");
    TEST_ASSERT(strcmp(title.progress_text, "Loading fonts") == 0, "progress text");
}

static void
test_action_names(void)
{
    TEST_ASSERT(
        RS_Title_ActionFromName("login") == RS_TITLE_ACTION_LOGIN, "login action resolves");
    TEST_ASSERT(
        RS_Title_ActionFromName("existing_user") == RS_TITLE_ACTION_EXISTING_USER,
        "existing_user action resolves");
    TEST_ASSERT(
        RS_Title_ActionFromName("cancel") == RS_TITLE_ACTION_CANCEL, "cancel action resolves");
    /* An unknown action= is a typo in an INI, and a button that does nothing is
     * how it announces itself. */
    TEST_ASSERT(
        RS_Title_ActionFromName("nonsense") == RS_TITLE_ACTION_NONE, "an unknown action is none");
}

/* The accounts screen: home once the app says there are accounts, Play Now
 * with its index, "another account" to an empty form, the icon back. */
static void
test_accounts_screen(void)
{
    struct RS_Title title;

    RS_Title_Init(&title);
    TEST_ASSERT(title.home == RS_TITLE_MAIN_MENU, "home starts as the front menu");
    TEST_ASSERT(title.play_requested == -1, "no Play Now pending");
    TEST_ASSERT(!RS_Title_PlayAccount(&title, 0), "Play Now off the accounts screen is ignored");

    RS_Title_SetHome(&title, RS_TITLE_ACCOUNTS);
    TEST_ASSERT(title.screen == RS_TITLE_ACCOUNTS, "a new home moves a screen showing the old one");
    TEST_ASSERT(RS_Title_PlayAccount(&title, 2) && title.play_requested == 2,
                "Play Now asks for its account");
    TEST_ASSERT(!RS_Title_PlayAccount(&title, 1) && title.play_requested == 2,
                "a second press while one is pending is ignored");
    title.play_requested = -1;

    RS_Title_SetFieldText(&title, RS_TITLE_FIELD_USERNAME, "someone");
    TEST_ASSERT(RS_Title_HandleAction(&title, RS_TITLE_ACTION_OTHER_ACCOUNT) &&
                    title.screen == RS_TITLE_LOGIN_FORM,
                "another account opens the form");
    TEST_ASSERT(RS_Title_FieldText(&title, RS_TITLE_FIELD_USERNAME)[0] == '\0',
                "empty: it is a different account");
    type_string(&title, "bob");
    press(&title, TORIRS_OSRSKEY_ENTER);
    type_string(&title, "pw");
    TEST_ASSERT(RS_Title_HandleAction(&title, RS_TITLE_ACTION_SHOW_ACCOUNTS) &&
                    title.screen == RS_TITLE_ACCOUNTS,
                "the account icon goes back to the accounts");
    TEST_ASSERT(RS_Title_FieldText(&title, RS_TITLE_FIELD_PASSWORD)[0] == '\0',
                "and drops the half-typed password");

    RS_Title_HandleAction(&title, RS_TITLE_ACTION_OTHER_ACCOUNT);
    press(&title, TORIRS_OSRSKEY_ESCAPE);
    TEST_ASSERT(title.screen == RS_TITLE_ACCOUNTS, "escape returns home, the accounts");
    RS_Title_HandleAction(&title, RS_TITLE_ACTION_OTHER_ACCOUNT);
    RS_Title_HandleAction(&title, RS_TITLE_ACTION_CANCEL);
    TEST_ASSERT(title.screen == RS_TITLE_ACCOUNTS, "so does cancel");

    RS_Title_SetHome(&title, RS_TITLE_MAIN_MENU);
    TEST_ASSERT(title.screen == RS_TITLE_MAIN_MENU,
                "with the last account forgotten, home is the front menu again");
    TEST_ASSERT(RS_Title_ActionFromName("play_account") == RS_TITLE_ACTION_PLAY_ACCOUNT &&
                    RS_Title_ActionFromName("other_account") == RS_TITLE_ACTION_OTHER_ACCOUNT &&
                    RS_Title_ActionFromName("show_accounts") == RS_TITLE_ACTION_SHOW_ACCOUNTS,
                "the account actions resolve");
}

static void
test_link_prompt(void)
{
    struct RS_Title title;

    RS_Title_Init(&title);
    TEST_ASSERT(!RS_Title_HandleAction(&title, RS_TITLE_ACTION_LINK_ACCOUNT) &&
                    title.link_choice == RS_TITLE_LINK_NONE,
                "link: off the prompt, its buttons do nothing");
    RS_Title_SetScreen(&title, RS_TITLE_LOGIN_FORM);
    type_string(&title, "bob");
    press(&title, TORIRS_OSRSKEY_ENTER);
    type_string(&title, "pw");
    RS_Title_SetScreen(&title, RS_TITLE_LINK_PROMPT);
    TEST_ASSERT(!type_char(&title, 'x') && strcmp(RS_Title_FieldText(&title, RS_TITLE_FIELD_PASSWORD),
                                              "pw") == 0,
                "link: the prompt takes no typing and keeps the form's fields");
    TEST_ASSERT(RS_Title_HandleAction(&title, RS_TITLE_ACTION_LINK_ACCOUNT) &&
                    title.link_choice == RS_TITLE_LINK_LINK,
                "link: Link account is taken");
    TEST_ASSERT(!RS_Title_HandleAction(&title, RS_TITLE_ACTION_PLAIN_LOGIN) &&
                    title.link_choice == RS_TITLE_LINK_LINK,
                "link: and answers once");
    title.link_choice = RS_TITLE_LINK_NONE;
    TEST_ASSERT(RS_Title_HandleAction(&title, RS_TITLE_ACTION_PLAIN_LOGIN) &&
                    title.link_choice == RS_TITLE_LINK_LOG_IN,
                "link: Log in is the other answer");
    title.link_choice = RS_TITLE_LINK_NONE;
    RS_Title_HandleAction(&title, RS_TITLE_ACTION_CANCEL);
    TEST_ASSERT(title.screen == RS_TITLE_MAIN_MENU &&
                    RS_Title_FieldText(&title, RS_TITLE_FIELD_PASSWORD)[0] == '\0',
                "link: cancel goes home and drops the password");
    TEST_ASSERT(RS_Title_ActionFromName("link_account") == RS_TITLE_ACTION_LINK_ACCOUNT &&
                    RS_Title_ActionFromName("plain_login") == RS_TITLE_ACTION_PLAIN_LOGIN,
                "link: the prompt's actions resolve");
}

static void
test_switch_character(void)
{
    struct RS_Title title;

    RS_Title_Init(&title);
    TEST_ASSERT(title.chosen_account == 0, "the first account is chosen until another is");
    TEST_ASSERT(!RS_Title_HandleAction(&title, RS_TITLE_ACTION_SWITCH_CHARACTER) &&
                    title.screen == RS_TITLE_MAIN_MENU,
                "switch character is the accounts screen's");
    RS_Title_SetHome(&title, RS_TITLE_ACCOUNTS);
    TEST_ASSERT(!RS_Title_ChooseAccount(&title, 1) && title.chosen_account == 0,
                "a card's Select only answers on the characters screen");
    TEST_ASSERT(RS_Title_HandleAction(&title, RS_TITLE_ACTION_SWITCH_CHARACTER) &&
                    title.screen == RS_TITLE_CHARACTERS,
                "switch character shows the cards");
    TEST_ASSERT(RS_Title_ChooseAccount(&title, 1) && title.chosen_account == 1 &&
                    title.screen == RS_TITLE_ACCOUNTS,
                "Select chooses that account and goes back to its Play Now");
    RS_Title_HandleAction(&title, RS_TITLE_ACTION_SWITCH_CHARACTER);
    RS_Title_HandleAction(&title, RS_TITLE_ACTION_CANCEL);
    TEST_ASSERT(title.screen == RS_TITLE_ACCOUNTS && title.chosen_account == 1,
                "Back goes home and keeps the choice");
    TEST_ASSERT(!RS_Title_HandleAction(&title, RS_TITLE_ACTION_SIGN_OUT) &&
                    !RS_Title_HandleAction(&title, RS_TITLE_ACTION_PLAY_CHOSEN),
                "sign out and the chosen Play Now are the app's");
    TEST_ASSERT(RS_Title_ActionFromName("play_chosen") == RS_TITLE_ACTION_PLAY_CHOSEN &&
                    RS_Title_ActionFromName("switch_character") ==
                        RS_TITLE_ACTION_SWITCH_CHARACTER &&
                    RS_Title_ActionFromName("choose_account") == RS_TITLE_ACTION_CHOOSE_ACCOUNT &&
                    RS_Title_ActionFromName("sign_out") == RS_TITLE_ACTION_SIGN_OUT,
                "the accounts screen's actions resolve");
}

int
main(void)
{
    g_failures = 0;
    g_checks = 0;

    test_init();
    test_menu_ignores_typing();
    test_typing_and_focus();
    test_backspace_on_empty();
    test_caps_are_per_field_and_per_revision();
    test_charset_filters();
    test_enter_advances_then_submits();
    test_escape_returns_to_menu();
    test_cancel_clears_credentials();
    test_prefilled_username_focuses_password();
    test_set_field_text_truncates();
    test_messages_and_progress();
    test_action_names();
    test_accounts_screen();
    test_link_prompt();
    test_switch_character();

    if( g_failures )
    {
        fprintf(stderr, "%d failure(s) of %d check(s)\n", g_failures, g_checks);
        return 1;
    }
    printf("rs_title_test: ok (%d checks)\n", g_checks);
    return 0;
}
