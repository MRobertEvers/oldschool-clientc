package com.torirs.client;

import android.content.Context;
import android.content.SharedPreferences;
import android.os.Build;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.IOException;
import java.security.GeneralSecurityException;

/**
 * The prod app's two sign-ins, kept across launches.
 *
 * <h2>Two walls, two credentials</h2>
 *
 *   1. The IaP (auth.mrobertevers.com, the mrobertevers-proxy repo) stands in
 *      front of torirs.mrobertevers.com. Signing in with its email and
 *      password (POST /api/device/token) gives a REFRESH TOKEN good for 30
 *      days and a SESSION good for a day. The session is what every request
 *      carries, as the __Secure-iap cookie; the refresh token is traded for a
 *      new pair whenever the session is close to running out, and each trade
 *      starts its 30 days again.
 *   2. The torirs gate (io_server --gate, src/ioserver/io_gate.c) behind it.
 *      Its web login's name and password (POST /device/token) give the same:
 *      a REFRESH TOKEN good for 30 days and a torirs_gate TICKET good for a
 *      day, which io_server checks on every route and torirsserver on the
 *      game's WebSocket. The ticket is renewed from the refresh token exactly
 *      as the IaP session is.
 *
 * So a phone in use never signs in again: each renewal starts both 30-day
 * windows over. No password is kept: only the two refresh tokens, the
 * current session and the ticket, in this app's private preferences (allowBackup is off, so they never
 * leave the phone). Every request the app or its relay makes gets its Cookie
 * header from {@link #cookieHeader}.
 */
final class Session
{
    /** Thrown when a credential is gone for good: the person signs in again. */
    static final class SignInRequired extends Exception
    {
        SignInRequired(String message)
        {
            super(message);
        }
    }

    static final String IAP_COOKIE = "__Secure-iap";
    static final String GATE_COOKIE = "torirs_gate";

    /** A session with less than this left is traded before it is used, so a
     *  connection opened now does not die of an expiry mid-play. */
    private static final long SESSION_MARGIN_MS = 2 * 3600 * 1000L;

    private static final String PREFS = "torirs_session";
    private static Session instance;

    private final Context app;
    private final SharedPreferences prefs;

    private Session(Context context)
    {
        app = context.getApplicationContext();
        prefs = app.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    static synchronized Session get(Context context)
    {
        if( instance == null )
            instance = new Session(context);
        return instance;
    }

    /* ---- what is held ---------------------------------------------------- */

    synchronized boolean hasIap()
    {
        return !prefs.getString("iap_refresh", "").isEmpty()
                && prefs.getLong("iap_refresh_expires", 0) > System.currentTimeMillis();
    }

    synchronized boolean hasGate()
    {
        return !prefs.getString("gate_refresh", "").isEmpty()
                && prefs.getLong("gate_refresh_expires", 0) > System.currentTimeMillis();
    }

    synchronized String iapEmail()
    {
        return prefs.getString("iap_email", "");
    }

    synchronized String gateName()
    {
        return prefs.getString("gate_name", "");
    }

    synchronized long iapRefreshExpires()
    {
        return prefs.getLong("iap_refresh_expires", 0);
    }

    synchronized long gateRefreshExpires()
    {
        return prefs.getLong("gate_refresh_expires", 0);
    }

    /* ---- the IaP --------------------------------------------------------- */

    /** What the IaP said about the account, for the sign-in screen. */
    static final class IapAccount
    {
        String email;
        boolean mustVerify;
        boolean verified;
    }

    /** Sign in with the IaP's email and password: a new 30-day refresh token. */
    IapAccount signInIap(String email, String password)
            throws IOException, GeneralSecurityException, SignInRequired
    {
        JSONObject body = new JSONObject();
        try
        {
            body.put("grant_type", "password");
            body.put("email", email);
            body.put("password", password);
            body.put("device", Build.MANUFACTURER + " " + Build.MODEL);
        }
        catch( JSONException e )
        {
            throw new IllegalStateException(e);
        }
        Http.Response r = Http.postJson(app, BuildConfig.IAP_ORIGIN + "/api/device/token", null,
                body.toString());
        if( r.status != 200 )
            throw new SignInRequired(errorOf(r, "Sign-in failed"));
        return store(r);
    }

    /**
     * The current IaP session, traded for a new one first when it is close to
     * running out. Blocking (it may make a request); called by the launcher
     * and by every relay connection.
     */
    synchronized String iapSession()
            throws IOException, GeneralSecurityException, SignInRequired
    {
        long now = System.currentTimeMillis();
        String session = prefs.getString("iap_session", "");
        if( !session.isEmpty() && prefs.getLong("iap_session_expires", 0) - now > SESSION_MARGIN_MS )
            return session;
        if( !hasIap() )
            throw new SignInRequired("Sign in to mrobertevers.com.");

        JSONObject body = new JSONObject();
        try
        {
            body.put("grant_type", "refresh_token");
            body.put("refresh_token", prefs.getString("iap_refresh", ""));
        }
        catch( JSONException e )
        {
            throw new IllegalStateException(e);
        }
        Http.Response r = Http.postJson(app, BuildConfig.IAP_ORIGIN + "/api/device/token", null,
                body.toString());
        if( r.status == 200 )
        {
            store(r);
            return prefs.getString("iap_session", "");
        }
        if( r.status == 400 && "invalid_grant".equals(jsonField(r, "error_code")) )
        {
            clearIap();
            throw new SignInRequired("Your mrobertevers.com sign-in has ended. Sign in again.");
        }
        /* Anything else (a 429, a 5xx, the proxy restarting) is not the end of
         * the sign-in: keep it and say so. */
        throw new IOException(errorOf(r, "The sign-in server answered " + r.status));
    }

    /** Mail a verification code to the account (the IaP's /api/verify/send). */
    void sendVerifyCode() throws IOException, GeneralSecurityException, SignInRequired
    {
        Http.Response r = Http.postJson(app, BuildConfig.IAP_ORIGIN + "/api/verify/send",
                IAP_COOKIE + "=" + iapSession(), "{}");
        if( r.status != 200 )
            throw new IOException(errorOf(r, "Could not send a code"));
    }

    /** Check a mailed code. */
    void checkVerifyCode(String code) throws IOException, GeneralSecurityException, SignInRequired
    {
        JSONObject body = new JSONObject();
        try
        {
            body.put("code", code);
        }
        catch( JSONException e )
        {
            throw new IllegalStateException(e);
        }
        Http.Response r = Http.postJson(app, BuildConfig.IAP_ORIGIN + "/api/verify/check",
                IAP_COOKIE + "=" + iapSession(), body.toString());
        if( r.status != 200 )
            throw new IOException(errorOf(r, "That code did not work"));
    }

    private synchronized IapAccount store(Http.Response r) throws IOException
    {
        try
        {
            JSONObject j = new JSONObject(r.text());
            long now = System.currentTimeMillis();
            prefs.edit()
                    .putString("iap_email", j.getString("email"))
                    .putString("iap_session", j.getString("session"))
                    .putLong("iap_session_expires", now + j.getLong("session_expires_in") * 1000L)
                    .putString("iap_refresh", j.getString("refresh_token"))
                    .putLong("iap_refresh_expires", now + j.getLong("refresh_expires_in") * 1000L)
                    .commit();
            IapAccount a = new IapAccount();
            a.email = j.getString("email");
            a.mustVerify = j.optBoolean("mustVerify", false);
            a.verified = j.optBoolean("verified", false);
            return a;
        }
        catch( JSONException e )
        {
            throw new IOException("the sign-in server's answer made no sense: " + e.getMessage());
        }
    }

    private synchronized void clearIap()
    {
        prefs.edit()
                .remove("iap_session").remove("iap_session_expires")
                .remove("iap_refresh").remove("iap_refresh_expires")
                .commit();
    }

    /* ---- the torirs gate ------------------------------------------------- */

    private static final String GATE_TOKEN_PATH = "/device/token";

    /** Sign in to the torirs gate with its web login: a 30-day refresh token
     *  and a ticket (io_gate.c POST /device/token). */
    void signInGate(String name, String password)
            throws IOException, GeneralSecurityException, SignInRequired
    {
        Http.Response r = Http.postForm(app, "https://" + BuildConfig.SERVER_HOST + GATE_TOKEN_PATH,
                IAP_COOKIE + "=" + iapSession(),
                "grant_type", "password", "name", name, "password", password);
        if( r.status == 200 )
        {
            storeGate(r);
            return;
        }
        refusedByIap(r);
        throw new SignInRequired(gateErrorOf(r, "ToriRS sign-in failed"));
    }

    /**
     * The current gate ticket, traded for a new one (and a rotated refresh
     * token) first when it is close to running out -- the gate's half of what
     * {@link #iapSession} does. Blocking.
     */
    String gateTicket() throws IOException, GeneralSecurityException, SignInRequired
    {
        String iap = iapSession();
        String refresh;
        synchronized( this )
        {
            String ticket = prefs.getString("gate_ticket", "");
            if( !ticket.isEmpty()
                    && prefs.getLong("gate_ticket_expires", 0) - System.currentTimeMillis() > SESSION_MARGIN_MS )
                return ticket;
            if( !hasGate() )
                throw new SignInRequired("Sign in to ToriRS.");
            refresh = prefs.getString("gate_refresh", "");
        }
        Http.Response r = Http.postForm(app, "https://" + BuildConfig.SERVER_HOST + GATE_TOKEN_PATH,
                IAP_COOKIE + "=" + iap, "grant_type", "refresh_token", "refresh_token", refresh);
        if( r.status == 200 )
        {
            storeGate(r);
            synchronized( this )
            {
                return prefs.getString("gate_ticket", "");
            }
        }
        refusedByIap(r);
        if( r.status == 400 && "invalid_grant".equals(jsonField(r, "error")) )
        {
            clearGate();
            throw new SignInRequired("Your ToriRS sign-in has ended. Sign in again.");
        }
        throw new IOException(gateErrorOf(r, "The ToriRS server answered " + r.status));
    }

    private synchronized void storeGate(Http.Response r) throws IOException
    {
        try
        {
            JSONObject j = new JSONObject(r.text());
            long now = System.currentTimeMillis();
            prefs.edit()
                    .putString("gate_name", j.getString("name"))
                    .putString("gate_ticket", j.getString("ticket"))
                    .putLong("gate_ticket_expires", now + j.getLong("ticket_expires_in") * 1000L)
                    .putString("gate_refresh", j.getString("refresh_token"))
                    .putLong("gate_refresh_expires", now + j.getLong("refresh_expires_in") * 1000L)
                    .commit();
        }
        catch( JSONException e )
        {
            throw new IOException("the ToriRS server's answer made no sense: " + e.getMessage());
        }
    }

    /* The IaP turning a request to torirs away -- its session ended, or the
     * account must verify its email -- is a 401/403 in plain text or a 302 to
     * auth.: the FIRST sign-in's fault, whatever was being asked. */
    private void refusedByIap(Http.Response r) throws SignInRequired
    {
        boolean json = r.text().trim().startsWith("{");
        if( r.status == 302 || ((r.status == 401 || r.status == 403) && !json) )
        {
            clearIap();
            throw new SignInRequired("Your mrobertevers.com sign-in has ended. Sign in again.");
        }
    }

    /** Forget the short-lived IaP session and gate ticket, keeping both
     *  refresh tokens: the next request renews both. What a server turning a
     *  request away calls for, short of signing in again. */
    synchronized void dropSessions()
    {
        prefs.edit()
                .remove("iap_session").remove("iap_session_expires")
                .remove("gate_ticket").remove("gate_ticket_expires")
                .commit();
    }

    synchronized void clearGate()
    {
        prefs.edit()
                .remove("gate_ticket").remove("gate_ticket_expires")
                .remove("gate_refresh").remove("gate_refresh_expires")
                .commit();
    }

    /* ---- what every request carries -------------------------------------- */

    /** The Cookie header for torirs.mrobertevers.com: both sign-ins, each
     *  renewed first if it is close to running out. */
    String cookieHeader() throws IOException, GeneralSecurityException, SignInRequired
    {
        String ticket = gateTicket();
        return IAP_COOKIE + "=" + iapSession() + "; " + GATE_COOKIE + "=" + ticket;
    }

    /** Forget both sign-ins, and tell both servers to end this device's
     *  (best effort: signing out must work offline too). Blocking. */
    void signOut()
    {
        String iapRefresh;
        String gateRefresh;
        String iap = "";
        try
        {
            iap = iapSession();
        }
        catch( Exception ignored )
        {
            /* No session: the gate's token just runs out. */
        }
        synchronized( this )
        {
            iapRefresh = prefs.getString("iap_refresh", "");
            gateRefresh = prefs.getString("gate_refresh", "");
            prefs.edit().clear().commit();
        }
        try
        {
            if( !gateRefresh.isEmpty() && !iap.isEmpty() )
                Http.postForm(app, "https://" + BuildConfig.SERVER_HOST + "/device/revoke",
                        IAP_COOKIE + "=" + iap, "refresh_token", gateRefresh);
        }
        catch( Exception ignored )
        {
            /* Offline: the token simply runs out in 30 days. */
        }
        try
        {
            if( !iapRefresh.isEmpty() )
            {
                JSONObject body = new JSONObject();
                body.put("refresh_token", iapRefresh);
                Http.postJson(app, BuildConfig.IAP_ORIGIN + "/api/device/revoke", null, body.toString());
            }
        }
        catch( Exception ignored )
        {
            /* Likewise. */
        }
    }

    /* ---- helpers --------------------------------------------------------- */

    private static String jsonField(Http.Response r, String key)
    {
        try
        {
            return new JSONObject(r.text()).optString(key, "");
        }
        catch( JSONException e )
        {
            return "";
        }
    }

    private static String errorOf(Http.Response r, String fallback)
    {
        String e = jsonField(r, "error");
        return e.isEmpty() ? fallback + " (" + r.status + ")." : e;
    }

    /* RFC 6749's error body, as io_gate.c answers: error_description first. */
    private static String gateErrorOf(Http.Response r, String fallback)
    {
        String e = jsonField(r, "error_description");
        return e.isEmpty() ? fallback + " (" + r.status + ")." : e;
    }
}
