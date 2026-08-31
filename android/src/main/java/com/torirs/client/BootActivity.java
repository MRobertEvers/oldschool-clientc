package com.torirs.client;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.util.List;

/**
 * The boot menu.
 *
 * <h2>What it is for</h2>
 *
 * A phone has no command line, and `--manifest <path>` is how this client is
 * told which world to boot. Without a menu, choosing a profile would mean
 * rebuilding the APK with a different string in it. So this screen is the
 * device's stand-in for the argument: it lists the manifests actually present,
 * says which of them can boot, and hands the chosen one to the client as argv.
 *
 * <h2>Why it times out instead of waiting</h2>
 *
 * The common case is booting the same profile again -- an edit-build-run loop
 * where the menu is something to get past, not something to read. So it behaves
 * like a boot loader: the default is preselected, a countdown runs, and doing
 * nothing boots it. Touching anything cancels the countdown, because a user who
 * has started choosing must not have the machine choose for them mid-tap.
 *
 * <p>The default is the LAST profile booted, remembered here. That makes the
 * zero-interaction path the one the developer was already on.
 */
public final class BootActivity extends Activity
{
    /**
     * How long the countdown runs.
     *
     * Long enough to read the list and reach for it; short enough that it is
     * not felt on a rebuild-and-run cycle. It is also the reason nothing here
     * needs an explicit "boot" button for the default case.
     */
    private static final int COUNTDOWN_MS = 4000;
    private static final int TICK_MS = 100;

    private static final String PREFS = "torirs_boot";
    private static final String KEY_DEFAULT = "default_manifest";

    private final Handler handler = new Handler(Looper.getMainLooper());

    private List<BootProfile> profiles;
    private BootProfile selected;
    private TextView status;
    private long deadline;
    private boolean countdownRunning;
    private boolean launched;
    /** Set once onCreate has built the UI, so onResume can tell a first
     *  show from a return out of the editor. */
    private boolean uiBuilt;

    @Override
    protected void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);

        profiles = BootProfile.discover(this);
        setContentView(buildUi());

        if( selected != null )
            startCountdown();
    }

    /**
     * Coming back from the editor re-reads the manifests, so an edit is visible
     * immediately -- and does NOT restart the countdown.
     *
     * A user who has just been editing a profile is mid-decision; having the
     * machine boot something four seconds after they hit Back is the one
     * behaviour this screen must not have.
     */
    @Override
    protected void onResume()
    {
        super.onResume();
        if( uiBuilt )
        {
            profiles = BootProfile.discover(this);
            selected = null;
            setContentView(buildUi());
            cancelCountdown();
            if( status != null )
                status.setText("tap a profile to boot it");
        }
        uiBuilt = true;
    }

    @Override
    protected void onDestroy()
    {
        super.onDestroy();
        handler.removeCallbacksAndMessages(null);
    }

    /* ---- the screen ------------------------------------------------------ */

    private View buildUi()
    {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(Color.BLACK);
        root.setPadding(dp(24), dp(20), dp(24), dp(20));

        /*
         * Title on the left, gear on the right.
         *
         * The gear edits where a profile's SERVER is -- the one field that goes
         * stale without anyone touching it, because it is a DHCP lease. Before
         * this the only way to correct it was a desktop, a cable and an adb
         * push to change one word.
         */
        {
            LinearLayout header = new LinearLayout(this);
            header.setOrientation(LinearLayout.HORIZONTAL);
            header.setGravity(Gravity.CENTER_VERTICAL);

            TextView title = label("ToriRS", 26, Color.WHITE, true);
            header.addView(title, new LinearLayout.LayoutParams(
                    0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));

            /* Drawn, not typed: U+2699 has no glyph in Android 5.1's font and
             * comes out as a tofu box. @see GearView. */
            GearView gear = new GearView(this, dp(34));
            gear.setOnClickListener(new View.OnClickListener()
            {
                @Override
                public void onClick(View v)
                {
                    /* Stop the clock first: opening the editor must not be
                     * followed a second later by the countdown booting a world
                     * underneath it. */
                    cancelCountdown();
                    if( status != null )
                        status.setText("tap a profile to boot it");
                    startActivity(new Intent(BootActivity.this, ProfileEditorActivity.class));
                }
            });
            header.addView(gear);
            root.addView(header);
        }

        if( profiles.isEmpty() )
        {
            /*
             * The one failure this screen must explain rather than merely
             * report: an empty list on a fresh device means the data was never
             * pushed, and the fix is a command the user is unlikely to guess.
             */
            root.addView(label(
                    "No manifests found.\n\n"
                            + "Push the client's data to this device first:\n\n"
                            + "    tools/android_push_data.sh\n\n"
                            + "It expects them under:\n"
                            + BootProfile.manifestDir(this).getAbsolutePath(),
                    14, Color.rgb(255, 140, 140), false));
            return root;
        }

        root.addView(label("select a profile", 13, Color.rgb(150, 150, 150), false));

        status = label("", 14, Color.rgb(120, 200, 255), false);
        status.setPadding(0, dp(6), 0, dp(10));
        root.addView(status);

        ScrollView scroller = new ScrollView(this);
        LinearLayout list = new LinearLayout(this);
        list.setOrientation(LinearLayout.VERTICAL);

        String defaultName = defaultManifestName();
        for( BootProfile p : profiles )
        {
            list.addView(row(p));
            if( p.isBootable() && selected == null && p.manifest.getName().equals(defaultName) )
                selected = p;
        }
        /*
         * No remembered default, or it is no longer bootable. Fall back to the
         * first SELF-CONTAINED profile -- one that needs nothing outside the
         * device -- before considering one that needs a server.
         *
         * The distinction is the whole point of the fallback. A profile that
         * streams its cache from a server aborts on the first archive when that
         * server is not there, and having the countdown pick one by itself
         * means the user watches a failure they never chose. An explicit tap on
         * such a profile is a different matter: that is someone who knows their
         * server is up.
         */
        if( selected == null )
        {
            for( BootProfile p : profiles )
            {
                if( p.isSelfContained() )
                {
                    selected = p;
                    break;
                }
            }
        }
        /* Only then one that needs a server -- better than offering nothing. */
        if( selected == null )
        {
            for( BootProfile p : profiles )
            {
                if( p.isBootable() )
                {
                    selected = p;
                    break;
                }
            }
        }

        scroller.addView(list);
        root.addView(scroller, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

        if( selected == null )
            status.setText("no profile on this device can boot - see the reasons above");

        return root;
    }

    private View row(final BootProfile p)
    {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.VERTICAL);
        row.setPadding(dp(12), dp(10), dp(12), dp(10));

        TextView title = label(p.name, 18, p.isBootable() ? Color.WHITE : Color.rgb(110, 110, 110), false);
        row.addView(title);

        String subtitle;
        int subtitleColor;
        if( !p.isBootable() )
        {
            subtitle = p.problem;
            subtitleColor = Color.rgb(200, 110, 110);
        }
        else if( p.needsServer != null )
        {
            /* Amber, not red: this profile is offered and may well work. The
             * colour says "depends on something not on this device", which is
             * also why the countdown will not pick it on its own. */
            subtitle = p.needsServer;
            subtitleColor = Color.rgb(220, 180, 90);
        }
        else
        {
            subtitle = p.cacheDir != null ? p.cacheDir.getName() : "no cache directory stated";
            subtitleColor = Color.rgb(130, 130, 130);
        }
        row.addView(label(subtitle, 12, subtitleColor, false));

        if( p.isBootable() )
        {
            row.setOnClickListener(new View.OnClickListener()
            {
                @Override
                public void onClick(View v)
                {
                    /* A tap is a decision, so it both stops the countdown and
                     * boots -- there is no second confirmation step, because
                     * the countdown was already the confirmation. */
                    cancelCountdown();
                    launch(p);
                }
            });
            row.setBackgroundColor(Color.rgb(22, 22, 26));
        }
        else
        {
            row.setBackgroundColor(Color.rgb(16, 16, 16));
        }

        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.bottomMargin = dp(6);
        row.setLayoutParams(lp);
        return row;
    }

    /* ---- the countdown --------------------------------------------------- */

    private void startCountdown()
    {
        deadline = System.currentTimeMillis() + COUNTDOWN_MS;
        countdownRunning = true;
        handler.post(tick);
    }

    private void cancelCountdown()
    {
        countdownRunning = false;
        handler.removeCallbacks(tick);
    }

    private final Runnable tick = new Runnable()
    {
        @Override
        public void run()
        {
            if( !countdownRunning )
                return;

            long remain = deadline - System.currentTimeMillis();
            if( remain <= 0 )
            {
                countdownRunning = false;
                launch(selected);
                return;
            }
            status.setText(String.format(
                    "booting %s in %.1fs   -   tap a profile to choose",
                    selected.name, remain / 1000.0));
            handler.postDelayed(this, TICK_MS);
        }
    };

    /**
     * Any touch anywhere stops the clock.
     *
     * Wider than the rows on purpose: a user who has begun interacting with the
     * screen at all has taken over, and having the machine boot something under
     * their finger a moment later is the one behaviour a boot menu must not
     * have.
     */
    @Override
    public void onUserInteraction()
    {
        super.onUserInteraction();
        if( countdownRunning )
        {
            cancelCountdown();
            status.setText("tap a profile to boot it");
        }
    }

    /* ---- launching ------------------------------------------------------- */

    private void launch(BootProfile profile)
    {
        if( profile == null || launched )
            return;
        /* Guarded because the countdown firing and a tap landing in the same
         * frame would otherwise start the client twice. */
        launched = true;

        rememberDefault(profile);

        Intent intent = new Intent(this, ClientActivity.class);
        intent.putExtra(ClientActivity.EXTRA_MANIFEST, profile.manifest.getAbsolutePath());
        startActivity(intent);
        /* The boot menu does not stay on the back stack: coming "back" from the
         * client means leaving the app, not being asked to choose again. */
        finish();
    }

    private String defaultManifestName()
    {
        SharedPreferences prefs = getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        return prefs.getString(KEY_DEFAULT, null);
    }

    private void rememberDefault(BootProfile profile)
    {
        getSharedPreferences(PREFS, Context.MODE_PRIVATE)
                .edit()
                .putString(KEY_DEFAULT, profile.manifest.getName())
                .apply();
    }

    /* ---- small helpers --------------------------------------------------- */

    private TextView label(String text, int sp, int color, boolean bold)
    {
        TextView tv = new TextView(this);
        tv.setText(text);
        tv.setTextColor(color);
        tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        tv.setGravity(Gravity.START);
        if( bold )
            tv.setTypeface(tv.getTypeface(), android.graphics.Typeface.BOLD);
        return tv;
    }

    private int dp(int value)
    {
        return (int)(value * getResources().getDisplayMetrics().density + 0.5f);
    }
}
