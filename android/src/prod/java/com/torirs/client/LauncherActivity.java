package com.torirs.client;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.system.ErrnoException;
import android.system.Os;
import android.text.format.DateFormat;
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.security.GeneralSecurityException;

/**
 * The prod app's front door: sign in if needed, bring the game pack up to
 * date, and boot the server's latest manifest through the relay.
 *
 *   1. Both sign-ins held? (Session) If not, SignInActivity.
 *   2. latest.json, signature checked (GamePack.fetch). A JNI number other than
 *      this APK's stops here: that pack needs the new APK.
 *   3. The library for this ABI and the data zip, each fetched only when its
 *      hash changed.
 *   4. A newer library than the one this process already loaded restarts the
 *      process (RestartActivity), because a loaded .so stays loaded.
 *   5. Load it, start the relay, write manifest_android.ini, point the
 *      account requests at the relay (TORIRS_AUTH_URL), start ClientActivity.
 *
 * A boot the client refused comes back here with its reason
 * (ClientActivity.bootFailed) and waits for Retry rather than looping.
 */
public final class LauncherActivity extends Activity
{
    private static final String TAG = "torirs";
    private static final int REQ_SIGN_IN = 1;

    private TextView status;
    private TextView detail;
    private TextView account;
    private ProgressBar progress;
    private Button retry;
    private Button signOut;
    private Button getApp;
    private boolean working;

    @Override
    protected void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);
        setContentView(buildUi());
        String bootError = getIntent().getStringExtra(BootActivity.EXTRA_BOOT_ERROR);
        if( bootError != null )
        {
            showError("The game stopped: " + bootError);
            return;
        }
        begin();
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data)
    {
        super.onActivityResult(requestCode, resultCode, data);
        if( requestCode != REQ_SIGN_IN )
            return;
        if( resultCode == RESULT_OK )
            begin();
        else
            finish();
    }

    /* ---- the steps ------------------------------------------------------- */

    private void begin()
    {
        if( working )
            return;
        Session session = Session.get(this);
        if( !session.hasIap() || !session.hasGate() )
        {
            startActivityForResult(new Intent(this, SignInActivity.class), REQ_SIGN_IN);
            return;
        }
        working = true;
        refreshAccountLine();
        retry.setVisibility(View.GONE);
        getApp.setVisibility(View.GONE);
        status.setTextColor(Color.rgb(120, 200, 255));
        setStatus("Checking for updates…", null);
        new Thread(new Runnable()
        {
            @Override
            public void run()
            {
                runSteps();
            }
        }, "launcher").start();
    }

    private void runSteps()
    {
        try
        {
            Session session = Session.get(this);
            String cookie = session.cookieHeader();
            final GamePack.Release r = GamePack.fetch(this, cookie);
            final String version = "game " + r.commit + (r.built.isEmpty() ? "" : ", built " + r.built);
            post(null, version);

            if( r.jniAbi != GamePack.JNI_ABI )
            {
                runOnUiThread(new Runnable()
                {
                    @Override
                    public void run()
                    {
                        showError("The game has moved on from this version of the app. "
                                + "Install the new app to keep playing.");
                        if( !r.apk.isEmpty() )
                        {
                            getApp.setTag(GamePack.baseUrl() + r.apk);
                            getApp.setVisibility(View.VISIBLE);
                        }
                    }
                });
                return;
            }
            if( r.libPath == null )
                throw new IOException("the server has no game pack for this phone (" + r.abi + ")");

            File dataRoot = BootProfile.dataRoot(this);
            if( !GamePack.haveLib(this, r) )
                post("Downloading the game…", version);
            File lib = GamePack.ensureLib(this, r, cookie, bar("Downloading the game"));
            post("Updating game data…", version);
            GamePack.ensureData(this, dataRoot, r, cookie, bar("Updating game data"));
            firstPluginPrefs(dataRoot);

            String loaded = GamePack.loadedPath();
            if( loaded != null && !loaded.equals(lib.getAbsolutePath()) )
            {
                post("Restarting for the new version…", version);
                RestartActivity.restart(this);
                return;
            }
            post("Starting…", version);
            GamePack.load(lib);
            IapTunnel tunnel = IapTunnel.start(this);
            final File manifest = ProdManifest.write(new File(dataRoot, r.manifest), tunnel.port);
            /* The account flow's requests (task_accounts_io.h: /oauth/token,
             * /game/play, /account/*) go where this says; the relay adds the
             * cookies, so TORIRS_GATE_COOKIE stays unset. */
            Os.setenv("TORIRS_AUTH_URL", "http://127.0.0.1:" + tunnel.port, true);

            runOnUiThread(new Runnable()
            {
                @Override
                public void run()
                {
                    working = false;
                    Intent intent = new Intent(LauncherActivity.this, ClientActivity.class);
                    intent.putExtra(ClientActivity.EXTRA_MANIFEST, manifest.getAbsolutePath());
                    startActivity(intent);
                    finish();
                }
            });
        }
        catch( final Session.SignInRequired e )
        {
            runOnUiThread(new Runnable()
            {
                @Override
                public void run()
                {
                    working = false;
                    startActivityForResult(
                            new Intent(LauncherActivity.this, SignInActivity.class)
                                    .putExtra(SignInActivity.EXTRA_MESSAGE, e.getMessage()),
                            REQ_SIGN_IN);
                }
            });
        }
        catch( Http.HttpStatusException e )
        {
            Log.w(TAG, "launcher", e);
            /* 302/401 here is a wall turning the request away: a session or
             * ticket the server no longer honours. Renew both from their
             * refresh tokens on Retry; only a refused refresh asks to sign in. */
            if( e.status == 302 || e.status == 303 || e.status == 401 || e.status == 403 )
            {
                Session.get(this).dropSessions();
                fail("The server turned the request away (" + e.status + "). Tap Retry.");
            }
            else
                fail("The server answered " + e.status + ". Tap Retry in a moment.");
        }
        catch( IOException e )
        {
            Log.w(TAG, "launcher", e);
            fail("Could not reach the server: " + e.getMessage());
        }
        catch( GeneralSecurityException e )
        {
            Log.e(TAG, "launcher", e);
            fail("The game pack failed its security check: " + e.getMessage());
        }
        catch( ErrnoException | RuntimeException | UnsatisfiedLinkError e )
        {
            Log.e(TAG, "launcher", e);
            fail("The game could not start: " + e);
        }
    }

    /** plugin_prefs.mobile.ini (in the data zip) seeds plugin_prefs.ini once;
     *  after that the file is the player's, and an update never resets it. */
    private static void firstPluginPrefs(File dataRoot) throws IOException
    {
        File prefs = new File(dataRoot, "plugin_prefs.ini");
        File seed = new File(dataRoot, "plugin_prefs.mobile.ini");
        if( prefs.exists() || !seed.isFile() )
            return;
        try( InputStream in = new FileInputStream(seed); OutputStream out = new FileOutputStream(prefs) )
        {
            out.write(Http.readAll(in));
        }
    }

    /* ---- UI -------------------------------------------------------------- */

    private View buildUi()
    {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(Color.BLACK);
        root.setPadding(dp(24), dp(20), dp(24), dp(20));

        root.addView(label("ToriRS", 26, Color.WHITE));
        account = label("", 13, Color.rgb(150, 150, 150));
        root.addView(account);

        status = label("", 16, Color.rgb(120, 200, 255));
        status.setPadding(0, dp(24), 0, dp(8));
        root.addView(status);

        progress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progress.setMax(1000);
        progress.setVisibility(View.INVISIBLE);
        root.addView(progress, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        detail = label("", 12, Color.rgb(130, 130, 130));
        detail.setPadding(0, dp(8), 0, dp(16));
        root.addView(detail);

        LinearLayout buttons = new LinearLayout(this);
        buttons.setOrientation(LinearLayout.HORIZONTAL);
        retry = button("Retry", new View.OnClickListener()
        {
            @Override
            public void onClick(View v)
            {
                begin();
            }
        });
        retry.setVisibility(View.GONE);
        getApp = button("Get the new app", new View.OnClickListener()
        {
            @Override
            public void onClick(View v)
            {
                startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse((String)v.getTag())));
            }
        });
        getApp.setVisibility(View.GONE);
        signOut = button("Sign out", new View.OnClickListener()
        {
            @Override
            public void onClick(View v)
            {
                if( working )
                    return;
                setStatus("Signing out…", null);
                new Thread(new Runnable()
                {
                    @Override
                    public void run()
                    {
                        Session.get(LauncherActivity.this).signOut();
                        runOnUiThread(new Runnable()
                        {
                            @Override
                            public void run()
                            {
                                refreshAccountLine();
                                startActivityForResult(
                                        new Intent(LauncherActivity.this, SignInActivity.class),
                                        REQ_SIGN_IN);
                            }
                        });
                    }
                }).start();
            }
        });
        buttons.addView(retry);
        buttons.addView(getApp);
        buttons.addView(signOut);
        root.addView(buttons);
        refreshAccountLine();
        return root;
    }

    private void refreshAccountLine()
    {
        Session s = Session.get(this);
        if( !s.hasIap() )
        {
            account.setText("not signed in");
            return;
        }
        String line = s.iapEmail();
        if( s.hasGate() )
            line += "  ·  ToriRS: " + s.gateName();
        long until = s.hasGate() ? Math.min(s.iapRefreshExpires(), s.gateRefreshExpires())
                : s.iapRefreshExpires();
        line += "\nsigned in until " + DateFormat.format("d MMM yyyy", until)
                + " (renewed each time you play)";
        account.setText(line);
    }

    private void showError(String message)
    {
        working = false;
        progress.setVisibility(View.INVISIBLE);
        status.setTextColor(Color.rgb(255, 140, 120));
        status.setText(message);
        retry.setVisibility(View.VISIBLE);
    }

    private void fail(final String message)
    {
        runOnUiThread(new Runnable()
        {
            @Override
            public void run()
            {
                showError(message);
            }
        });
    }

    private void setStatus(String text, String detailText)
    {
        if( text != null )
            status.setText(text);
        if( detailText != null )
            detail.setText(detailText);
        progress.setVisibility(View.INVISIBLE);
    }

    private void post(final String text, final String detailText)
    {
        runOnUiThread(new Runnable()
        {
            @Override
            public void run()
            {
                setStatus(text, detailText);
            }
        });
    }

    private Http.Progress bar(final String what)
    {
        return new Http.Progress()
        {
            @Override
            public void onBytes(final long done, final long total)
            {
                runOnUiThread(new Runnable()
                {
                    @Override
                    public void run()
                    {
                        progress.setVisibility(View.VISIBLE);
                        if( total > 0 )
                        {
                            progress.setIndeterminate(false);
                            progress.setProgress((int)(1000 * done / total));
                            status.setText(String.format("%s… %.1f / %.1f MB", what,
                                    done / 1e6, total / 1e6));
                        }
                        else
                        {
                            progress.setIndeterminate(true);
                            status.setText(String.format("%s… %.1f MB", what, done / 1e6));
                        }
                    }
                });
            }
        };
    }

    private TextView label(String text, int sp, int color)
    {
        TextView t = new TextView(this);
        t.setText(text);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        t.setTextColor(color);
        t.setGravity(Gravity.START);
        return t;
    }

    private Button button(String text, View.OnClickListener onClick)
    {
        Button b = new Button(this);
        b.setText(text);
        b.setOnClickListener(onClick);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.rightMargin = dp(8);
        b.setLayoutParams(lp);
        return b;
    }

    private int dp(int v)
    {
        return (int)(v * getResources().getDisplayMetrics().density + 0.5f);
    }
}
