#include "game/rs_title.h"

#include "input/torirs_keymap.h"
#include "ui/uitree.h"

#include <assert.h>
#include <string.h>

/*
 * ui/ restates these because it is a leaf of game/ (see UITreeTitleAction).
 * These are what stop the two copies from drifting: add a value to one and
 * forget the other, and the build says so.
 */
_Static_assert(
    (int)RS_TITLE_ACTION_NONE == (int)UITREE_TITLE_ACTION_NONE, "title action none");
_Static_assert(
    (int)RS_TITLE_ACTION_EXISTING_USER == (int)UITREE_TITLE_ACTION_EXISTING_USER,
    "title action existing_user");
_Static_assert(
    (int)RS_TITLE_ACTION_NEW_USER == (int)UITREE_TITLE_ACTION_NEW_USER, "title action new_user");
_Static_assert(
    (int)RS_TITLE_ACTION_LOGIN == (int)UITREE_TITLE_ACTION_LOGIN, "title action login");
_Static_assert(
    (int)RS_TITLE_ACTION_CANCEL == (int)UITREE_TITLE_ACTION_CANCEL, "title action cancel");
_Static_assert(
    (int)RS_TITLE_ACTION_FOCUS_USERNAME == (int)UITREE_TITLE_ACTION_FOCUS_USERNAME,
    "title action focus_username");
_Static_assert(
    (int)RS_TITLE_ACTION_FOCUS_PASSWORD == (int)UITREE_TITLE_ACTION_FOCUS_PASSWORD,
    "title action focus_password");
_Static_assert(
    (int)RS_TITLE_ACTION_TOGGLE_REMEMBER == (int)UITREE_TITLE_ACTION_TOGGLE_REMEMBER,
    "title action toggle_remember");
_Static_assert(
    (int)RS_TITLE_ACTION_TOGGLE_HIDE == (int)UITREE_TITLE_ACTION_TOGGLE_HIDE,
    "title action toggle_hide");

void
RS_Title_Init(struct RS_Title* title)
{
    assert(title);
    memset(title, 0, sizeof(*title));
    title->screen = RS_TITLE_MAIN_MENU;
    title->focus = RS_TITLE_FIELD_USERNAME;
    title->progress_percent = -1;
}

static void
copy_line(
    char* dst,
    size_t dst_size,
    char const* src)
{
    assert(dst);
    if( !src )
    {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

void
RS_Title_SetMessages(
    struct RS_Title* title,
    char const* line1,
    char const* line2,
    char const* line3)
{
    assert(title);
    copy_line(title->messages[0], sizeof(title->messages[0]), line1);
    copy_line(title->messages[1], sizeof(title->messages[1]), line2);
    copy_line(title->messages[2], sizeof(title->messages[2]), line3);
}

void
RS_Title_SetProgress(
    struct RS_Title* title,
    int percent,
    char const* text)
{
    assert(title);
    title->progress_percent = percent;
    copy_line(title->progress_text, sizeof(title->progress_text), text);
}

void
RS_Title_SetScreen(
    struct RS_Title* title,
    enum RS_TitleScreen screen)
{
    assert(title);
    title->screen = screen;
    /* Entering the form always starts on the username, except that a
     * prefilled one (remembered, or handed in on the command line) means the
     * player's next keystroke is meant for the password -- the deob does the
     * same in method4556. */
    if( screen == RS_TITLE_LOGIN_FORM )
    {
        title->focus = title->fields[RS_TITLE_FIELD_USERNAME][0] != '\0'
                           ? RS_TITLE_FIELD_PASSWORD
                           : RS_TITLE_FIELD_USERNAME;
    }
}

char const*
RS_Title_FieldText(
    struct RS_Title const* title,
    enum RS_TitleField field)
{
    assert(title);
    assert(field >= 0);
    assert(field < RS_TITLE_FIELD_COUNT);
    return title->fields[field];
}

/* The cap a field actually enforces: its configured maxlen, clamped to what
 * the buffer holds. An unset (0) maxlen means the buffer is the only limit. */
static int
field_cap(
    struct RS_Title const* title,
    enum RS_TitleField field)
{
    int configured = title->field_cfg[field].maxlen;
    int buffer_max = RS_TITLE_FIELD_LEN - 1;

    if( configured <= 0 || configured > buffer_max )
        return buffer_max;
    return configured;
}

void
RS_Title_SetFieldText(
    struct RS_Title* title,
    enum RS_TitleField field,
    char const* text)
{
    int cap;

    assert(title);
    assert(field >= 0);
    assert(field < RS_TITLE_FIELD_COUNT);

    cap = field_cap(title, field);
    if( !text )
    {
        title->fields[field][0] = '\0';
        return;
    }
    strncpy(title->fields[field], text, (size_t)cap);
    title->fields[field][cap] = '\0';
}

static int
charset_accepts(
    struct RS_TitleFieldCfg const* cfg,
    int ch)
{
    assert(cfg);
    /* No charset declared: any printable character. A revision whose font can
     * draw more than the 254 CHARSET says so in its INI. */
    if( cfg->charset[0] == '\0' )
        return ch >= 32 && ch < 127;
    return strchr(cfg->charset, ch) != NULL;
}

static int
append_char(
    struct RS_Title* title,
    enum RS_TitleField field,
    int ch)
{
    char* text = title->fields[field];
    int len = (int)strlen(text);
    int cap = field_cap(title, field);

    if( len >= cap )
        return 0;
    text[len] = (char)ch;
    text[len + 1] = '\0';
    return 1;
}

static int
backspace(
    struct RS_Title* title,
    enum RS_TitleField field)
{
    char* text = title->fields[field];
    int len = (int)strlen(text);

    if( len == 0 )
        return 0;
    text[len - 1] = '\0';
    return 1;
}

int
RS_Title_HandleKey(
    struct RS_Title* title,
    int key_typed,
    int key_pressed)
{
    enum RS_TitleField field;

    assert(title);

    /* Only the credential form takes typing. The menu and the info pages have
     * buttons, and Enter on them is the button's business (see
     * RS_Title_HandleAction). */
    if( title->screen != RS_TITLE_LOGIN_FORM )
        return 0;

    assert(title->focus >= 0);
    assert(title->focus < RS_TITLE_FIELD_COUNT);
    field = (enum RS_TitleField)title->focus;

    /* key_typed carries OSRS internal key codes, not VK or ASCII: Enter is 84,
     * Backspace 85, Tab 80, Escape 13. A plain character arrives as
     * key_typed == -1 with the character in key_pressed. @see rs_chat.c. */
    if( key_typed == TORIRS_OSRSKEY_BACKSPACE )
        return backspace(title, field);

    if( key_typed == TORIRS_OSRSKEY_TAB )
    {
        title->focus = (field == RS_TITLE_FIELD_USERNAME) ? RS_TITLE_FIELD_PASSWORD
                                                          : RS_TITLE_FIELD_USERNAME;
        return 1;
    }

    if( key_typed == TORIRS_OSRSKEY_ENTER )
    {
        /* Enter on the username advances; Enter on the password submits. The
         * modern reference does this (class424 ENTER -> login); Client-TS only
         * cycles focus, which leaves a keyboard-only player on a filled-in
         * form with no way to send it. */
        if( field == RS_TITLE_FIELD_USERNAME )
        {
            title->focus = RS_TITLE_FIELD_PASSWORD;
            return 1;
        }
        title->submit_requested = 1;
        return 1;
    }

    if( key_typed == TORIRS_OSRSKEY_ESCAPE )
    {
        RS_Title_SetScreen(title, RS_TITLE_MAIN_MENU);
        return 1;
    }

    if( key_typed == -1 && key_pressed >= 32 && key_pressed < 127 )
    {
        if( !charset_accepts(&title->field_cfg[field], key_pressed) )
            return 0;
        return append_char(title, field, key_pressed);
    }

    return 0;
}

int
RS_Title_HandleAction(
    struct RS_Title* title,
    enum RS_TitleAction action)
{
    assert(title);

    switch( action )
    {
    case RS_TITLE_ACTION_EXISTING_USER:
        RS_Title_SetScreen(title, RS_TITLE_LOGIN_FORM);
        return 1;

    case RS_TITLE_ACTION_NEW_USER:
        /* The reference sends the player to a web page to sign up; this client
         * has no browser to hand them to, so the INI's info page says so. */
        RS_Title_SetScreen(title, RS_TITLE_INFO);
        return 1;

    case RS_TITLE_ACTION_LOGIN:
        title->submit_requested = 1;
        return 1;

    case RS_TITLE_ACTION_CANCEL:
        /* Cancel clears the credentials as well as the screen: the reference
         * does (Client-TS titleScreenLoop), and a password left in a buffer
         * after the player backed out is a password nobody asked us to keep. */
        title->fields[RS_TITLE_FIELD_USERNAME][0] = '\0';
        title->fields[RS_TITLE_FIELD_PASSWORD][0] = '\0';
        RS_Title_SetScreen(title, RS_TITLE_MAIN_MENU);
        return 1;

    case RS_TITLE_ACTION_FOCUS_USERNAME:
        if( title->focus == RS_TITLE_FIELD_USERNAME )
            return 0;
        title->focus = RS_TITLE_FIELD_USERNAME;
        return 1;

    case RS_TITLE_ACTION_FOCUS_PASSWORD:
        if( title->focus == RS_TITLE_FIELD_PASSWORD )
            return 0;
        title->focus = RS_TITLE_FIELD_PASSWORD;
        return 1;

    case RS_TITLE_ACTION_TOGGLE_REMEMBER:
        title->toggles[RS_TITLE_TOGGLE_REMEMBER] =
            !title->toggles[RS_TITLE_TOGGLE_REMEMBER];
        return 1;

    case RS_TITLE_ACTION_TOGGLE_HIDE:
        title->toggles[RS_TITLE_TOGGLE_HIDE] = !title->toggles[RS_TITLE_TOGGLE_HIDE];
        return 1;

    case RS_TITLE_ACTION_NONE:
        return 0;
    }

    return 0;
}

enum RS_TitleAction
RS_Title_ActionFromName(char const* name)
{
    assert(name);

    if( strcmp(name, "existing_user") == 0 )
        return RS_TITLE_ACTION_EXISTING_USER;
    if( strcmp(name, "new_user") == 0 )
        return RS_TITLE_ACTION_NEW_USER;
    if( strcmp(name, "login") == 0 )
        return RS_TITLE_ACTION_LOGIN;
    if( strcmp(name, "cancel") == 0 )
        return RS_TITLE_ACTION_CANCEL;
    if( strcmp(name, "focus_username") == 0 )
        return RS_TITLE_ACTION_FOCUS_USERNAME;
    if( strcmp(name, "focus_password") == 0 )
        return RS_TITLE_ACTION_FOCUS_PASSWORD;
    if( strcmp(name, "toggle_remember") == 0 )
        return RS_TITLE_ACTION_TOGGLE_REMEMBER;
    if( strcmp(name, "toggle_hide") == 0 )
        return RS_TITLE_ACTION_TOGGLE_HIDE;
    return RS_TITLE_ACTION_NONE;
}
