package com.torirs.client;

import android.app.Activity;
import android.graphics.Color;
import android.os.Bundle;
import android.text.InputType;
import android.util.TypedValue;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.inputmethod.EditorInfo;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

/**
 * The prod app's sign-in, natively: the two walls in front of the game, one
 * after the other (Session.java says what each one is).
 *
 *   step 1  mrobertevers.com  -- the IaP's email and password. Kept as a
 *                                30-day refresh token. An account the IaP
 *                                requires to verify its email does that here
 *                                (step 1b), with the code it mails.
 *   step 2  ToriRS            -- the torirs web login's name and password.
 *                                Kept as the gate's own 30-day refresh
 *                                token (io_gate.c /device/token).
 *
 * A step already held is skipped, so a gate ticket that ran out asks only for
 * step 2. Passwords are sent and dropped; neither is stored.
 */
public final class SignInActivity extends Activity
{
    /** A sentence to show above the form: why sign-in was asked for again. */
    static final String EXTRA_MESSAGE = "com.torirs.client.SIGN_IN_MESSAGE";

    private static final int STEP_IAP = 1;
    private static final int STEP_VERIFY = 2;
    private static final int STEP_GATE = 3;

    private LinearLayout form;
    private TextView heading;
    private TextView note;
    private TextView error;
    private EditText first;
    private EditText second;
    private Button submit;
    private Button secondary;
    private int step;
    private boolean busy;

    @Override
    protected void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);
        setContentView(buildUi());
        String why = getIntent().getStringExtra(EXTRA_MESSAGE);
        if( why != null )
            error.setText(why);
        Session s = Session.get(this);
        showStep(s.hasIap() ? STEP_GATE : STEP_IAP);
    }

    /* ---- steps ----------------------------------------------------------- */

    private void showStep(int which)
    {
        step = which;
        busy = false;
        submit.setEnabled(true);
        secondary.setVisibility(View.GONE);
        first.setText("");
        second.setText("");
        second.setVisibility(View.VISIBLE);
        Session s = Session.get(this);
        if( which == STEP_IAP )
        {
            heading.setText("Sign in to mrobertevers.com");
            note.setText("Step 1 of 2. Your mrobertevers.com email and password: the sign-in "
                    + "in front of every site there. You stay signed in on this phone for 30 "
                    + "days, renewed each time you play.");
            first.setHint("Email");
            first.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_EMAIL_ADDRESS);
            second.setHint("Password");
            second.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
            if( !s.iapEmail().isEmpty() )
                first.setText(s.iapEmail());
            submit.setText("Sign in");
        }
        else if( which == STEP_VERIFY )
        {
            heading.setText("Verify your email");
            note.setText("mrobertevers.com needs to know " + s.iapEmail()
                    + " is yours. Send a code to it, then enter the 6 digits.");
            first.setHint("6-digit code");
            first.setInputType(InputType.TYPE_CLASS_NUMBER);
            second.setVisibility(View.GONE);
            submit.setText("Verify");
            secondary.setText("Send a code");
            secondary.setVisibility(View.VISIBLE);
        }
        else
        {
            heading.setText("Sign in to ToriRS");
            note.setText("Step 2 of 2. Your ToriRS web login (the one the torirs.mrobertevers.com "
                    + "sign-in page asks for), not a player name. Signed in as " + s.iapEmail()
                    + " on mrobertevers.com.");
            first.setHint("Web login name");
            first.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
            second.setHint("Password");
            second.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
            if( !s.gateName().isEmpty() )
                first.setText(s.gateName());
            submit.setText("Sign in");
            secondary.setText("Use another mrobertevers.com account");
            secondary.setVisibility(View.VISIBLE);
        }
        (first.getText().length() == 0 || second.getVisibility() != View.VISIBLE ? first : second)
                .requestFocus();
    }

    private void onSubmit()
    {
        if( busy )
            return;
        final String a = first.getText().toString().trim();
        final String b = second.getText().toString();
        if( a.isEmpty() || (second.getVisibility() == View.VISIBLE && b.isEmpty()) )
        {
            error.setText(step == STEP_VERIFY ? "Enter the code." : "Fill in both.");
            return;
        }
        busy = true;
        submit.setEnabled(false);
        error.setText("");
        note.setTextColor(Color.rgb(150, 150, 150));
        final int at = step;
        new Thread(new Runnable()
        {
            @Override
            public void run()
            {
                Session s = Session.get(SignInActivity.this);
                try
                {
                    if( at == STEP_IAP )
                    {
                        Session.IapAccount acct = s.signInIap(a, b);
                        next(acct.mustVerify ? STEP_VERIFY : STEP_GATE);
                    }
                    else if( at == STEP_VERIFY )
                    {
                        s.checkVerifyCode(a);
                        next(STEP_GATE);
                    }
                    else
                    {
                        s.signInGate(a, b);
                        runOnUiThread(new Runnable()
                        {
                            @Override
                            public void run()
                            {
                                setResult(RESULT_OK);
                                finish();
                            }
                        });
                    }
                }
                catch( Session.SignInRequired e )
                {
                    /* The gate step finding the IaP's session gone sends the
                     * person back to step 1; anything else stays put. */
                    fail(e.getMessage(), at == STEP_GATE && !s.hasIap() ? STEP_IAP : at);
                }
                catch( Exception e )
                {
                    fail("Could not reach the server: " + e.getMessage(), at);
                }
            }
        }).start();
    }

    private void onSecondary()
    {
        if( busy )
            return;
        if( step == STEP_GATE )
        {
            busy = true;
            new Thread(new Runnable()
            {
                @Override
                public void run()
                {
                    Session.get(SignInActivity.this).signOut();
                    next(STEP_IAP);
                }
            }).start();
            return;
        }
        busy = true;
        error.setText("");
        new Thread(new Runnable()
        {
            @Override
            public void run()
            {
                try
                {
                    Session.get(SignInActivity.this).sendVerifyCode();
                    runOnUiThread(new Runnable()
                    {
                        @Override
                        public void run()
                        {
                            busy = false;
                            note.setText("A code is on its way to " + Session.get(SignInActivity.this).iapEmail()
                                    + ". It lasts 15 minutes.");
                        }
                    });
                }
                catch( Exception e )
                {
                    fail(e.getMessage(), STEP_VERIFY);
                }
            }
        }).start();
    }

    private void next(final int which)
    {
        runOnUiThread(new Runnable()
        {
            @Override
            public void run()
            {
                error.setText("");
                showStep(which);
            }
        });
    }

    private void fail(final String message, final int backTo)
    {
        runOnUiThread(new Runnable()
        {
            @Override
            public void run()
            {
                if( backTo != step )
                    showStep(backTo);
                busy = false;
                submit.setEnabled(true);
                error.setText(message);
            }
        });
    }

    /* ---- UI -------------------------------------------------------------- */

    private View buildUi()
    {
        ScrollView scroller = new ScrollView(this);
        scroller.setBackgroundColor(Color.BLACK);
        scroller.setFillViewport(true);

        form = new LinearLayout(this);
        form.setOrientation(LinearLayout.VERTICAL);
        form.setPadding(dp(24), dp(20), dp(24), dp(20));
        scroller.addView(form);

        form.addView(label("ToriRS", 26, Color.WHITE));
        heading = label("", 20, Color.WHITE);
        heading.setPadding(0, dp(18), 0, dp(6));
        form.addView(heading);
        note = label("", 13, Color.rgb(150, 150, 150));
        note.setPadding(0, 0, 0, dp(12));
        form.addView(note);

        first = field();
        second = field();
        second.setOnEditorActionListener(new TextView.OnEditorActionListener()
        {
            @Override
            public boolean onEditorAction(TextView v, int actionId, KeyEvent event)
            {
                /* The soft keyboard's Done, or Enter on a hardware keyboard
                 * (IME_NULL with the key's own event, once per press). */
                boolean enter = actionId == EditorInfo.IME_NULL && event != null
                        && event.getAction() == KeyEvent.ACTION_DOWN;
                if( actionId == EditorInfo.IME_ACTION_DONE || actionId == EditorInfo.IME_ACTION_GO || enter )
                {
                    onSubmit();
                    return true;
                }
                return false;
            }
        });
        form.addView(first);
        form.addView(second);

        error = label("", 14, Color.rgb(255, 140, 120));
        error.setPadding(0, dp(8), 0, dp(8));
        form.addView(error);

        submit = new Button(this);
        submit.setOnClickListener(new View.OnClickListener()
        {
            @Override
            public void onClick(View v)
            {
                onSubmit();
            }
        });
        form.addView(submit, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        secondary = new Button(this);
        secondary.setOnClickListener(new View.OnClickListener()
        {
            @Override
            public void onClick(View v)
            {
                onSecondary();
            }
        });
        form.addView(secondary, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        return scroller;
    }

    private EditText field()
    {
        EditText e = new EditText(this);
        e.setSingleLine(true);
        /* Dark on the platform field's light fill (Theme.Black still draws
         * EditText light on these devices); white text vanished into it. */
        e.setTextColor(Color.rgb(20, 20, 20));
        e.setHintTextColor(Color.rgb(120, 120, 120));
        e.setTextSize(TypedValue.COMPLEX_UNIT_SP, 17);
        e.setImeOptions(EditorInfo.IME_ACTION_DONE | EditorInfo.IME_FLAG_NO_EXTRACT_UI);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.bottomMargin = dp(6);
        e.setLayoutParams(lp);
        return e;
    }

    private TextView label(String text, int sp, int color)
    {
        TextView t = new TextView(this);
        t.setText(text);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        t.setTextColor(color);
        return t;
    }

    private int dp(int v)
    {
        return (int)(v * getResources().getDisplayMetrics().density + 0.5f);
    }
}
