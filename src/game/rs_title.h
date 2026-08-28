#ifndef SRC_GAME_RS_TITLE_H
#define SRC_GAME_RS_TITLE_H

/*
 * Title-screen model: which pre-game screen is showing, what is typed into its
 * two fields, and the three message lines the server's login reply fills in.
 *
 * A leaf, deliberately: no App, no UITree, no cache. Everything here is
 * reachable from a test, which is the point -- the login form is the one part
 * of the client a user touches before anything else works, and "does backspace
 * on an empty password do the right thing" should not need a running client
 * and a server to answer.
 *
 * The screens mirror the references' own numbering (Client-TS `loginscreen`,
 * deob `loginIndex`) so a coordinate lifted from either reads across without a
 * translation table. Everything about how these are DRAWN -- position, font,
 * colour, the caret string, the mask character, the charset, the length caps --
 * lives in revconfig and reaches this model through RS_TitleFieldCfg.
 */

#define RS_TITLE_FIELD_LEN 321
#define RS_TITLE_MESSAGE_LEN 256
#define RS_TITLE_MESSAGE_LINES 3
#define RS_TITLE_CHARSET_LEN 160
#define RS_TITLE_PROGRESS_TEXT_LEN 128

/**
 * Which pre-game screen is up.
 *
 * Values are the references' own: Client-TS `loginscreen` 0/2/3 and the deob's
 * `loginIndex` agree on 0 = the front menu and 2 = the credential form. The
 * gaps are real screens this client does not implement yet (deob 1 = world-type
 * warning, 4 = authenticator, 7 = date of birth); leaving the numbering alone
 * is what lets those be added later without renumbering what works.
 */
enum RS_TitleScreen
{
    RS_TITLE_MAIN_MENU = 0,
    RS_TITLE_LOGIN_FORM = 2,
    /** Static prose plus one dismiss button: the reference's "create a free
     *  account" page, and where a rejected login's explanation lands. */
    RS_TITLE_INFO = 3,
};

enum RS_TitleField
{
    RS_TITLE_FIELD_USERNAME = 0,
    RS_TITLE_FIELD_PASSWORD,
    RS_TITLE_FIELD_COUNT
};

/**
 * What a login_button does when clicked, resolved from revconfig `action=` at
 * bake time so the INI names an intent and never a screen number.
 */
enum RS_TitleAction
{
    RS_TITLE_ACTION_NONE = 0,
    RS_TITLE_ACTION_EXISTING_USER,
    RS_TITLE_ACTION_NEW_USER,
    RS_TITLE_ACTION_LOGIN,
    RS_TITLE_ACTION_CANCEL,
    RS_TITLE_ACTION_FOCUS_USERNAME,
    RS_TITLE_ACTION_FOCUS_PASSWORD,
};

/** Per-field limits, read off the baked login_input node. */
struct RS_TitleFieldCfg
{
    /**
     * INI maxlen=. The two lanes disagree by a lot -- 12 for a 254 username,
     * 320 for a modern one -- which is exactly why it is not a constant here.
     * 0 means "unset", and the field then caps at RS_TITLE_FIELD_LEN - 1.
     */
    int maxlen;
    /**
     * INI charset=. Characters the field accepts; empty accepts anything
     * printable. The old lane ships the reference's 94-character CHARSET,
     * because a character the bitmap font has no glyph for is a character the
     * server will be told about and the player cannot see.
     */
    char charset[RS_TITLE_CHARSET_LEN];
};

struct RS_Title
{
    enum RS_TitleScreen screen;
    char fields[RS_TITLE_FIELD_COUNT][RS_TITLE_FIELD_LEN];
    struct RS_TitleFieldCfg field_cfg[RS_TITLE_FIELD_COUNT];
    /** enum RS_TitleField; which field the caret is in. */
    int focus;
    /** Reference loginMes1/2/3. The old lane fills two, the modern three. */
    char messages[RS_TITLE_MESSAGE_LINES][RS_TITLE_MESSAGE_LEN];

    /**
     * Credentials arrived on the command line or in the manifest, and the
     * first title tick should submit them. Autologin runs through the SAME
     * submit path a clicked Login button takes, so the scripted lanes exercise
     * the screen rather than bypassing it.
     */
    int autologin_pending;
    /** Set by a submit; the app clears it when it starts connecting. */
    int submit_requested;

    /** 0-100, or -1 for "no bar showing". */
    int progress_percent;
    char progress_text[RS_TITLE_PROGRESS_TEXT_LEN];
};

/** Zero the model and put it on the front menu with no bar. */
void
RS_Title_Init(struct RS_Title* title);

/** Replace the three message lines; a NULL line clears that line. */
void
RS_Title_SetMessages(
    struct RS_Title* title,
    char const* line1,
    char const* line2,
    char const* line3);

void
RS_Title_SetProgress(
    struct RS_Title* title,
    int percent,
    char const* text);

/** Move to `screen`, clearing per-screen transient state. */
void
RS_Title_SetScreen(
    struct RS_Title* title,
    enum RS_TitleScreen screen);

/** Text of one field. Never NULL. */
char const*
RS_Title_FieldText(
    struct RS_Title const* title,
    enum RS_TitleField field);

/** Replace a field's text, truncating to its configured cap. */
void
RS_Title_SetFieldText(
    struct RS_Title* title,
    enum RS_TitleField field,
    char const* text);

/**
 * One keyboard event. `key_typed` is the character (0 when the event carries
 * none), `key_pressed` the key code for the editing keys.
 *
 * Printable characters append when the focused field's charset accepts them
 * and it is not already full; backspace deletes; tab moves to the next field;
 * return advances from the username and submits from the password (the modern
 * reference's behaviour -- Client-TS only ever cycles focus, which strands a
 * keyboard-only player on a filled-in form); escape returns to the front menu.
 *
 * Returns nonzero when the model changed, which is the caller's cue to bump
 * the host's client-state epoch.
 */
int
RS_Title_HandleKey(
    struct RS_Title* title,
    int key_typed,
    int key_pressed);

/**
 * One resolved button action. Returns nonzero when the model changed.
 *
 * RS_TITLE_ACTION_LOGIN sets submit_requested rather than connecting: what a
 * login attempt costs, and what it does on failure, is the app's business.
 */
int
RS_Title_HandleAction(
    struct RS_Title* title,
    enum RS_TitleAction action);

/** Resolve a revconfig `action=` name. RS_TITLE_ACTION_NONE when unknown. */
enum RS_TitleAction
RS_Title_ActionFromName(char const* name);

#endif /* SRC_GAME_RS_TITLE_H */
