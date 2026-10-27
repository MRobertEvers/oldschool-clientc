#ifndef SRC_PLATFORM_PLATFORM_CREDENTIAL_FORM_H
#define SRC_PLATFORM_PLATFORM_CREDENTIAL_FORM_H

/*
 * The title's login form, as a password manager needs to see it.
 *
 * The form is drawn on the canvas, and a browser's password manager fills and
 * saves only real <input>s. So the browser lays a real form over the two drawn
 * fields (src/web/torirs_credentials.js) and keeps the client's fields in step
 * with it; this is what the client tells it, whenever it changes
 * (App_TakeCredentialForm -> PlatformWindow_SetCredentialForm). A backend with
 * no password manager ignores it.
 *
 * Its own header, with no dependencies, because both sides of that hand-off
 * name it and neither may include the other's.
 */

/* RS_TITLE_FIELD_LEN: the longest a field can hold. */
#define PLATFORM_CREDENTIAL_FIELD_LEN 321

struct PlatformCredentialForm
{
    /** The credential form is the screen: fields to lay inputs over. */
    int showing;
    /** A submitted login is dialling (or signing in at the account server). */
    int connecting;
    /** The login went through: the moment a password manager offers to save. */
    int in_game;
    /** The two fields' drawn text lines in layout pixels, [0] username and
     *  [1] password: from one ascent above the baseline the row is drawn on,
     *  as tall as the font's line box, as wide as the field. The platform maps
     *  them through its present (scale, stretch, letterbox). */
    int x[2];
    int y[2];
    int w[2];
    int h[2];
    /** Which field the client's caret is in (0 username, 1 password). */
    int focus;
    /** The client's password length, never the password: 0 says the client
     *  cleared it (a cancel, a submit), so the page should clear its copy. */
    int password_len;
    /** The client's username, so a page input it has not touched can follow
     *  a prefill or a cleared form. */
    char username[PLATFORM_CREDENTIAL_FIELD_LEN];
};

#endif
