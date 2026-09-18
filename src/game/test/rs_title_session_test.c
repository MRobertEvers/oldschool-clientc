/*
 * Getting from the title screen to the world, once.
 *
 * Every rule here fails as something other than what it is. An automatic
 * submit that fires twice reads as a server that keeps dropping you. A connect
 * that happens in the same tick as its submit reads as a client that ignored
 * the click. A "not dialled yet" read as "refused" shows a login failure on the
 * way to a login that succeeds. None of them leaves anything in a log.
 */

#include "game/rs_title_session.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define TEST_ASSERT(cond, what)                                                                    \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, (what));                                \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

/** A session started with --user/--pass, as a scripted or headless run is. */
static void
with_credentials(struct RS_TitleSession* session)
{
    RS_TitleSession_Reset(session);
    RS_TitleSession_SetCredentials(session, "tester", "hunter2");
    RS_TitleSession_SetConnectTarget(session, "localhost");
}

static void
test_a_fresh_session_asks_for_nothing(void)
{
    struct RS_TitleSession session;

    memset(&session, 0x7c, sizeof(session));
    RS_TitleSession_Reset(&session);
    TEST_ASSERT(!RS_TitleSession_HasCredentials(&session), "nothing to log in with");
    TEST_ASSERT(!session.connect_pending, "nothing waiting to dial");
    TEST_ASSERT(!session.autologin_spent, "the automatic submit is unspent");
    TEST_ASSERT(!session.resume_pending, "and no session to come back into");
    TEST_ASSERT(!session.resume_dial, "so nothing dials round the form");
    TEST_ASSERT(!session.pending_after_boot, "no title screen queued behind a bake");
}

static void
test_an_interactive_session_never_submits_itself(void)
{
    struct RS_TitleSession session;

    /* No credentials: the player is going to type. Prefilling an empty name
     * and submitting it puts a rejection on the screen before anyone has
     * touched the keyboard. */
    RS_TitleSession_Reset(&session);
    TEST_ASSERT(
        !RS_TitleSession_TakePrefill(&session, true, RS_TITLE_PHASE_TITLE),
        "an interactive session prefills nothing");
    TEST_ASSERT(
        !RS_TitleSession_TakeHeadlessLogin(&session, true, RS_TITLE_PHASE_GAME),
        "and dials nothing by itself");
}

static void
test_the_prefill_fires_once(void)
{
    struct RS_TitleSession session;

    with_credentials(&session);
    TEST_ASSERT(
        RS_TitleSession_TakePrefill(&session, true, RS_TITLE_PHASE_TITLE),
        "the form is filled in and submitted");
    /* Twice is the failure that matters: a rejected login lands back on the
     * form, and a second prefill dials again, is rejected again, and dials
     * again -- a client hammering a server that has told it to go away, and a
     * title screen nobody can type into. */
    TEST_ASSERT(
        !RS_TitleSession_TakePrefill(&session, true, RS_TITLE_PHASE_TITLE),
        "and never again in this session");
    TEST_ASSERT(
        RS_TitleSession_HasCredentials(&session),
        "the credentials themselves are still there for the dial");
}

static void
test_the_prefill_waits_for_a_form_to_fill(void)
{
    struct RS_TitleSession session;

    /* Spendable once, so spending it on a screen that has not been built yet
     * loses it: the form comes up blank and the automatic login never
     * happens at all. */
    with_credentials(&session);
    TEST_ASSERT(
        !RS_TitleSession_TakePrefill(&session, false, RS_TITLE_PHASE_TITLE),
        "not before the title tree is up");
    TEST_ASSERT(
        !RS_TitleSession_TakePrefill(&session, true, RS_TITLE_PHASE_CONNECTING),
        "not while a submit is already in flight");
    TEST_ASSERT(
        !RS_TitleSession_TakePrefill(&session, true, RS_TITLE_PHASE_OTHER),
        "not during the boot");
    TEST_ASSERT(
        RS_TitleSession_TakePrefill(&session, true, RS_TITLE_PHASE_TITLE),
        "still unspent when the form finally arrives");
}

static void
test_a_profile_with_no_title_screen_dials_anyway(void)
{
    struct RS_TitleSession session;

    /* A revconfig that predates a declared title layout boots straight to the
     * gameframe. There is no screen to show progress on, and the credentials
     * still have to reach the server. */
    with_credentials(&session);
    TEST_ASSERT(
        RS_TitleSession_TakeHeadlessLogin(&session, true, RS_TITLE_PHASE_GAME),
        "a screenless profile logs in from the world");
    TEST_ASSERT(
        !RS_TitleSession_TakeHeadlessLogin(&session, true, RS_TITLE_PHASE_GAME),
        "once");

    /* And only from the world. A profile that DOES have a title screen must
     * go through it: the form is the state the dial reads, and dialling round
     * it sends the stored credentials rather than whatever the player has
     * since typed into the boxes in front of them. */
    with_credentials(&session);
    TEST_ASSERT(
        !RS_TitleSession_TakeHeadlessLogin(&session, true, RS_TITLE_PHASE_TITLE),
        "a profile with a form does not dial round it");
    TEST_ASSERT(
        !RS_TitleSession_TakeHeadlessLogin(&session, true, RS_TITLE_PHASE_CONNECTING),
        "nor while a submit is already in flight");
    TEST_ASSERT(
        !RS_TitleSession_TakeHeadlessLogin(&session, true, RS_TITLE_PHASE_OTHER),
        "nor mid-boot");
    TEST_ASSERT(
        RS_TitleSession_TakePrefill(&session, true, RS_TITLE_PHASE_TITLE),
        "and the automatic login is still there for the form");
}

/** A boot a reloaded page made: the name and the key it was given, no
 *  password, and the link has taken the token. */
static void
resumed(struct RS_TitleSession* session)
{
    RS_TitleSession_Reset(session);
    RS_TitleSession_SetCredentials(session, "tester", "");
    RS_TitleSession_SetConnectTarget(session, "localhost");
    RS_TitleSession_ArmResume(session);
}

static void
test_a_reload_reconnects_instead_of_filling_in_the_form(void)
{
    struct RS_TitleSession session;

    /* The whole point. A prefill types somebody's credentials into the form
     * and submits them where the player can watch it happen; a reload was
     * already in the world and is owed it back, not a login screen with their
     * own account typed into it. So the prefill must not fire on this boot --
     * and it would, because a resumed boot carries a name just like a
     * scripted one does. */
    resumed(&session);
    TEST_ASSERT(
        !RS_TitleSession_TakePrefill(&session, true, RS_TITLE_PHASE_TITLE),
        "a reload filled in the login form");
    TEST_ASSERT(
        RS_TitleSession_TakeResume(&session, true, RS_TITLE_PHASE_TITLE),
        "and did not reconnect either");
    TEST_ASSERT(session.resume_dial, "the armed dial is the reconnect's, not the form's");
    /* Spent on being made, not on being answered. The caller's loading bar is
     * held up by this flag, and every later bake settles through the same
     * check -- the gameframe this handshake opens, and the title screen a
     * logout returns to. Left up, both come back saying "reconnecting". */
    TEST_ASSERT(!session.resume_pending, "a reconnect that has been made is still owed");

    /* Once, by the shared latch: a reconnect that re-arms itself after a
     * refusal is the same server-hammering loop the prefill's latch prevents. */
    TEST_ASSERT(
        !RS_TitleSession_TakeResume(&session, true, RS_TITLE_PHASE_TITLE),
        "the reconnect fired twice");
    TEST_ASSERT(
        !RS_TitleSession_TakePrefill(&session, true, RS_TITLE_PHASE_TITLE),
        "and left the prefill to fire after it");
}

static void
test_the_reconnect_waits_for_the_screen_it_shows_through(void)
{
    struct RS_TitleSession session;

    /* The bar that says "reconnecting" is a widget of the title tree. Dialled
     * before that tree exists, the reconnect runs behind a blank screen and
     * the player watches nothing at all -- and the latch is spent, so the
     * caption never arrives. */
    resumed(&session);
    TEST_ASSERT(
        !RS_TitleSession_TakeResume(&session, false, RS_TITLE_PHASE_TITLE),
        "not before the title tree is up");
    TEST_ASSERT(
        !RS_TitleSession_TakeResume(&session, true, RS_TITLE_PHASE_OTHER),
        "not during the boot");
    TEST_ASSERT(
        !RS_TitleSession_TakeResume(&session, true, RS_TITLE_PHASE_CONNECTING),
        "not while a dial is already in flight");
    TEST_ASSERT(
        RS_TitleSession_TakeResume(&session, true, RS_TITLE_PHASE_TITLE),
        "still unspent when the screen finally arrives");
}

static void
test_a_boot_with_no_token_is_an_ordinary_one(void)
{
    struct RS_TitleSession session;

    /* Nothing armed the resume -- an ordinary first visit, or a revision whose
     * login has no seed reconnect and turned the token down. Holding a
     * "reconnecting" bar over a handshake that is really a passwordless
     * GAMELOGIN is a bar that waits for a reply nothing will send. */
    with_credentials(&session);
    TEST_ASSERT(
        !RS_TitleSession_TakeResume(&session, true, RS_TITLE_PHASE_TITLE),
        "a boot with no token reconnected anyway");
    TEST_ASSERT(
        RS_TitleSession_TakePrefill(&session, true, RS_TITLE_PHASE_TITLE),
        "and the ordinary prefill is untouched by it");

    /* A token with no name beside it names no save for the server to hand
     * back: GAMERECONNECT's seed replaces the PASSWORD, not the identity. */
    RS_TitleSession_Reset(&session);
    RS_TitleSession_ArmResume(&session);
    TEST_ASSERT(
        !RS_TitleSession_TakeResume(&session, true, RS_TITLE_PHASE_TITLE),
        "a nameless token was presented");
}

static void
test_a_refused_reconnect_leaves_nothing_to_log_in_with(void)
{
    struct RS_TitleSession session;

    /* The server no longer has that session. There is no password kept
     * anywhere -- the page stores a name and a key and nothing else -- so the
     * player is about to be shown the login form, and the name must not be
     * sitting in it. Leaving it there is the auto-filled form the reload
     * exists to avoid, arriving one step later. */
    resumed(&session);
    RS_TitleSession_TakeResume(&session, true, RS_TITLE_PHASE_TITLE);
    RS_TitleSession_ResumeRefused(&session);

    TEST_ASSERT(!RS_TitleSession_HasCredentials(&session), "the form would open holding a name");
    TEST_ASSERT(!session.resume_dial, "and would still dial the dead session");
    TEST_ASSERT(
        !RS_TitleSession_TakeResume(&session, true, RS_TITLE_PHASE_TITLE),
        "a refused reconnect asked again");
    TEST_ASSERT(
        !RS_TitleSession_TakePrefill(&session, true, RS_TITLE_PHASE_TITLE),
        "nor submitted itself as a login");
}

static void
test_leaving_spends_the_reconnect_too(void)
{
    struct RS_TitleSession session;

    /* A logout ends the session the reconnect would ask for. Left armed, the
     * next title tick walks the player straight back into the world they just
     * left -- and does it behind a loading bar, with no form to cancel from. */
    resumed(&session);
    RS_TitleSession_Abandon(&session);
    TEST_ASSERT(
        !RS_TitleSession_TakeResume(&session, true, RS_TITLE_PHASE_TITLE),
        "logging out left the reconnect armed");
}

static void
test_the_two_automatic_paths_share_one_latch(void)
{
    struct RS_TitleSession session;

    /* One session gets one automatic login, by whichever route. Two latches
     * would let a profile that boots to the world and then shows a title
     * screen log itself in twice. */
    with_credentials(&session);
    TEST_ASSERT(
        RS_TitleSession_TakeHeadlessLogin(&session, true, RS_TITLE_PHASE_GAME),
        "the screenless route went first");
    TEST_ASSERT(
        !RS_TitleSession_TakePrefill(&session, true, RS_TITLE_PHASE_TITLE),
        "so the form route is already spent");

    with_credentials(&session);
    TEST_ASSERT(
        RS_TitleSession_TakePrefill(&session, true, RS_TITLE_PHASE_TITLE),
        "the form route went first");
    TEST_ASSERT(
        !RS_TitleSession_TakeHeadlessLogin(&session, true, RS_TITLE_PHASE_GAME),
        "so the screenless route is already spent");
}

static void
test_an_offline_profile_does_not_dial(void)
{
    struct RS_TitleSession session;

    with_credentials(&session);
    TEST_ASSERT(
        !RS_TitleSession_TakeHeadlessLogin(&session, false, RS_TITLE_PHASE_GAME),
        "no link, no dial");
    /* And the latch is not spent on the attempt, or a profile whose network
     * comes up a tick later would never log in. */
    TEST_ASSERT(
        RS_TitleSession_TakeHeadlessLogin(&session, true, RS_TITLE_PHASE_GAME),
        "the automatic login survives a tick with no link");
}

static void
test_a_submit_needs_something_to_submit(void)
{
    struct RS_TitleSession session;

    RS_TitleSession_Reset(&session);
    /* An empty name is left on the form rather than sent: every server answers
     * one with a rejection the player then has to read as if it meant
     * something about their account. */
    TEST_ASSERT(!RS_TitleSession_Submit(&session, true, false), "an empty name is not submitted");
    TEST_ASSERT(!session.connect_pending, "and nothing is armed");
    TEST_ASSERT(!RS_TitleSession_Submit(&session, false, true), "no link, nothing to submit to");
    TEST_ASSERT(!session.connect_pending, "and nothing is armed");
    TEST_ASSERT(RS_TitleSession_Submit(&session, true, true), "a name and a link is a submit");
    TEST_ASSERT(session.connect_pending, "which arms the dial");
}

static void
test_the_dial_happens_a_tick_after_the_submit(void)
{
    struct RS_TitleSession session;

    RS_TitleSession_Reset(&session);
    RS_TitleSession_Submit(&session, true, true);
    /* The submit tick ends here. Dialling inside it would spend the rest of
     * that tick, and several after it, on a handshake and a world of assets
     * with the pre-click picture still on screen: the player clicks Login and
     * watches nothing happen. */
    TEST_ASSERT(RS_TitleSession_TakePendingConnect(&session), "the next tick dials");
    TEST_ASSERT(!RS_TitleSession_TakePendingConnect(&session), "and only that one");
    TEST_ASSERT(
        !RS_TitleSession_TakePendingConnect(&session),
        "a session with nothing armed dials nothing");
}

static void
test_a_dial_that_has_not_happened_is_not_a_failure(void)
{
    struct RS_TitleSession session;

    RS_TitleSession_Reset(&session);
    RS_TitleSession_Submit(&session, true, true);

    /* Between the arming tick and the dialling tick the screen already says
     * "connecting" and the link has not been touched -- which is the same
     * state a refused handshake leaves it in. Read as a failure, the player is
     * shown a login error on the way to a login that then succeeds. */
    TEST_ASSERT(
        !RS_TitleSession_LoginFailed(&session, RS_TITLE_PHASE_CONNECTING, RS_TITLE_LINK_DOWN),
        "the arming tick does not read its own un-started handshake as a refusal");

    RS_TitleSession_TakePendingConnect(&session);
    TEST_ASSERT(
        RS_TitleSession_LoginFailed(&session, RS_TITLE_PHASE_CONNECTING, RS_TITLE_LINK_DOWN),
        "once dialled, a link that is down really was refused");
}

static void
test_only_a_connecting_session_reads_the_link(void)
{
    struct RS_TitleSession session;

    RS_TitleSession_Reset(&session);
    /* A link that is down while the player is sitting on the form is just a
     * client that has not dialled yet. Reporting a failure there would put a
     * rejection message on a screen nobody has submitted from. */
    TEST_ASSERT(
        !RS_TitleSession_LoginFailed(&session, RS_TITLE_PHASE_TITLE, RS_TITLE_LINK_DOWN),
        "an untouched form is not a refused login");
    TEST_ASSERT(
        !RS_TitleSession_LoginFailed(&session, RS_TITLE_PHASE_CONNECTING, RS_TITLE_LINK_BUSY),
        "a handshake still running is not a refused login");
    TEST_ASSERT(
        !RS_TitleSession_LoginFailed(&session, RS_TITLE_PHASE_CONNECTING, RS_TITLE_LINK_ABSENT),
        "a profile with no link at all is not a refused login");
    TEST_ASSERT(
        RS_TitleSession_LoginFailed(&session, RS_TITLE_PHASE_CONNECTING, RS_TITLE_LINK_DOWN),
        "a dialled handshake that ended down is");
}

static void
test_the_world_opens_only_on_a_finished_handshake(void)
{
    TEST_ASSERT(
        RS_TitleSession_LoginSucceeded(RS_TITLE_PHASE_CONNECTING, RS_TITLE_LINK_IN_GAME),
        "a finished handshake opens the world");
    TEST_ASSERT(
        !RS_TitleSession_LoginSucceeded(RS_TITLE_PHASE_CONNECTING, RS_TITLE_LINK_BUSY),
        "a running one does not");
    /* Already in the world. Opening the root interface again would tear down
     * and rebuild the whole gameframe under a player who is standing in it. */
    TEST_ASSERT(
        !RS_TitleSession_LoginSucceeded(RS_TITLE_PHASE_GAME, RS_TITLE_LINK_IN_GAME),
        "a session already in the world does not open it again");
}

static void
test_leaving_spends_the_automatic_login(void)
{
    struct RS_TitleSession session;

    with_credentials(&session);
    RS_TitleSession_Submit(&session, true, true);
    RS_TitleSession_Abandon(&session);

    /* Both halves. An unspent latch dials straight back into the world the
     * player just walked out of; an armed pending connect does the same thing
     * one tick later. */
    TEST_ASSERT(
        !RS_TitleSession_TakePrefill(&session, true, RS_TITLE_PHASE_TITLE),
        "logging out spends the automatic submit");
    TEST_ASSERT(
        !RS_TitleSession_TakePendingConnect(&session),
        "and disarms a dial that was already queued");
    TEST_ASSERT(
        !RS_TitleSession_TakeHeadlessLogin(&session, true, RS_TITLE_PHASE_GAME),
        "by either route");
}

static void
test_credentials_are_bounded_and_optional(void)
{
    struct RS_TitleSession session;
    char long_name[512];

    /* The password is what the dial is made with, and it is the one field
     * whose value nothing on screen ever shows -- so a copy that lands in the
     * wrong place presents as a login the server rejects for no stated reason.
     */
    with_credentials(&session);
    TEST_ASSERT(strcmp(session.user, "tester") == 0, "the name is the name");
    TEST_ASSERT(strcmp(session.password, "hunter2") == 0, "the password is the password");
    TEST_ASSERT(strcmp(session.connect_target, "localhost") == 0, "the address is the address");

    RS_TitleSession_Reset(&session);
    memset(long_name, 'a', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';
    RS_TitleSession_SetCredentials(&session, long_name, long_name);
    RS_TitleSession_SetConnectTarget(&session, long_name);
    TEST_ASSERT(strlen(session.user) == sizeof(session.user) - 1, "a long name is truncated");
    TEST_ASSERT(session.user[sizeof(session.user) - 1] == '\0', "and stays a string");
    TEST_ASSERT(
        strlen(session.connect_target) == sizeof(session.connect_target) - 1,
        "so is a long address");

    /* An absent flag is an interactive login, not a crash and not a literal
     * name of "(null)". */
    RS_TitleSession_SetCredentials(&session, NULL, NULL);
    TEST_ASSERT(!RS_TitleSession_HasCredentials(&session), "no --user means no automatic login");
    RS_TitleSession_SetConnectTarget(&session, NULL);
    TEST_ASSERT(session.connect_target[0] == '\0', "and no address to dial");
}

int
main(void)
{
    test_a_fresh_session_asks_for_nothing();
    test_an_interactive_session_never_submits_itself();
    test_the_prefill_fires_once();
    test_the_prefill_waits_for_a_form_to_fill();
    test_a_profile_with_no_title_screen_dials_anyway();
    test_a_reload_reconnects_instead_of_filling_in_the_form();
    test_the_reconnect_waits_for_the_screen_it_shows_through();
    test_a_boot_with_no_token_is_an_ordinary_one();
    test_a_refused_reconnect_leaves_nothing_to_log_in_with();
    test_leaving_spends_the_reconnect_too();
    test_the_two_automatic_paths_share_one_latch();
    test_an_offline_profile_does_not_dial();
    test_a_submit_needs_something_to_submit();
    test_the_dial_happens_a_tick_after_the_submit();
    test_a_dial_that_has_not_happened_is_not_a_failure();
    test_only_a_connecting_session_reads_the_link();
    test_the_world_opens_only_on_a_finished_handshake();
    test_leaving_spends_the_automatic_login();
    test_credentials_are_bounded_and_optional();

    if( g_failures )
    {
        printf("rs_title_session: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("rs_title_session: all checks passed\n");
    return 0;
}
