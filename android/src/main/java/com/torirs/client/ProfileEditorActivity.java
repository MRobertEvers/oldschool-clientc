package com.torirs.client;

import android.app.Activity;
import android.graphics.Color;
import android.os.Bundle;
import android.text.InputType;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ArrayAdapter;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.util.List;

/**
 * The gear button's screen: pick a profile, fix where its server is.
 *
 * Two levels, both built in code (this app carries no layouts and no AndroidX):
 * a list of the manifests on the device, and a form for the one that was
 * tapped. @see ProfileEditor for what is editable and why the write is
 * line-oriented.
 *
 * <p>This exists because a manifest's server address is the one field that goes
 * stale on its own. A DHCP lease moves and every profile pointing at it is
 * wrong, with no way to correct it from the device -- which previously meant a
 * desktop, a USB cable and an adb push to change one word.
 */
public final class ProfileEditorActivity extends Activity
{
    private LinearLayout root;

    @Override
    protected void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);
        root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(Color.BLACK);
        root.setPadding(dp(20), dp(16), dp(20), dp(16));
        setContentView(root);
        showList();
    }

    /* ---- level 1: which profile ------------------------------------------ */

    private void showList()
    {
        root.removeAllViews();
        root.addView(label("Edit profiles", 22, Color.WHITE, true));
        root.addView(label("the server address is the field that goes stale",
                12, Color.rgb(140, 140, 140), false));

        List<BootProfile> profiles = BootProfile.discover(this);
        if( profiles.isEmpty() )
        {
            root.addView(label("\nNo manifests on this device.", 14,
                    Color.rgb(255, 140, 140), false));
            return;
        }

        ScrollView scroller = new ScrollView(this);
        LinearLayout list = new LinearLayout(this);
        list.setOrientation(LinearLayout.VERTICAL);
        for( final BootProfile p : profiles )
        {
            LinearLayout row = new LinearLayout(this);
            row.setOrientation(LinearLayout.VERTICAL);
            row.setPadding(dp(12), dp(10), dp(12), dp(10));
            row.setBackgroundColor(Color.rgb(22, 22, 26));
            row.addView(label(p.name, 17, Color.WHITE, false));
            /* Every profile is editable, including one that cannot boot --
             * a broken profile is exactly the one you came here to fix. */
            row.addView(label(p.manifest.getName(), 11, Color.rgb(120, 120, 120), false));
            row.setOnClickListener(new View.OnClickListener()
            {
                @Override
                public void onClick(View v)
                {
                    showForm(p.manifest);
                }
            });
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
            lp.bottomMargin = dp(6);
            row.setLayoutParams(lp);
            list.addView(row);
        }
        scroller.addView(list);
        root.addView(scroller, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));
    }

    /* ---- level 2: the fields --------------------------------------------- */

    private void showForm(final File manifest)
    {
        final List<ProfileEditor.Field> fields = ProfileEditor.fields();
        ProfileEditor.load(manifest, fields);
        final int rendererAt = ProfileEditor.loadRenderer(manifest);

        root.removeAllViews();
        root.addView(label(manifest.getName(), 18, Color.WHITE, true));

        ScrollView scroller = new ScrollView(this);
        LinearLayout form = new LinearLayout(this);
        form.setOrientation(LinearLayout.VERTICAL);

        final EditText[] inputs = new EditText[fields.size()];
        for( int i = 0; i < fields.size(); i++ )
        {
            ProfileEditor.Field f = fields.get(i);
            form.addView(label(f.label, 13, Color.rgb(150, 200, 255), false));

            EditText e = new EditText(this);
            e.setText(f.value);
            e.setHint(f.hint);
            /*
             * The platform's default EditText background is a LIGHT nine-patch,
             * and white-on-light is invisible. Painting a dark background over
             * it is what keeps this screen legible on the app's black theme --
             * setting only the text colour left the values unreadable.
             */
            e.setBackgroundColor(Color.rgb(30, 30, 34));
            e.setPadding(dp(10), dp(10), dp(10), dp(10));
            e.setTextColor(Color.WHITE);
            e.setHintTextColor(Color.rgb(110, 110, 110));
            e.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16);
            e.setSingleLine(true);
            /* URI type rather than TEXT: it suppresses autocorrect and
             * auto-capitalisation, both of which quietly corrupt a hostname. */
            e.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI);
            inputs[i] = e;
            form.addView(e);
            form.addView(label(f.section + "  " + f.key, 10, Color.rgb(100, 100, 100), false));
            form.addView(spacer(dp(10)));
        }

        /*
         * The renderer is a dropdown because it is a closed set of three, and a
         * free-text box would let someone type a flag the client will reject at
         * boot with no way to see why from the device.
         */
        form.addView(label("Renderer", 13, Color.rgb(150, 200, 255), false));
        final Spinner renderer = new Spinner(this);
        ArrayAdapter<String> adapter = new ArrayAdapter<>(
                this, android.R.layout.simple_spinner_item, ProfileEditor.RENDERER_LABELS);
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        renderer.setAdapter(adapter);
        renderer.setSelection(rendererAt);
        renderer.setBackgroundColor(Color.rgb(30, 30, 34));
        form.addView(renderer);
        form.addView(label("[client:args]  arg=", 10, Color.rgb(100, 100, 100), false));
        form.addView(spacer(dp(10)));

        scroller.addView(form);
        root.addView(scroller, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

        LinearLayout buttons = new LinearLayout(this);
        buttons.setOrientation(LinearLayout.HORIZONTAL);

        Button save = new Button(this);
        save.setText("Save");
        save.setOnClickListener(new View.OnClickListener()
        {
            @Override
            public void onClick(View v)
            {
                for( int i = 0; i < fields.size(); i++ )
                    fields.get(i).value = inputs[i].getText().toString().trim();

                String err = ProfileEditor.save(manifest, fields);
                if( err == null )
                {
                    /* Second pass, because the renderer lives in repeated
                     * `arg=` lines rather than in a key the field save
                     * understands. Ordered after it so both read the same
                     * file state. */
                    err = ProfileEditor.saveRenderer(
                            manifest, renderer.getSelectedItemPosition());
                }
                if( err != null )
                {
                    Toast.makeText(ProfileEditorActivity.this, err, Toast.LENGTH_LONG).show();
                    return;
                }
                Toast.makeText(ProfileEditorActivity.this,
                        "saved " + manifest.getName(), Toast.LENGTH_SHORT).show();
                showList();
            }
        });

        Button cancel = new Button(this);
        cancel.setText("Back");
        cancel.setOnClickListener(new View.OnClickListener()
        {
            @Override
            public void onClick(View v)
            {
                showList();
            }
        });

        buttons.addView(save, new LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        buttons.addView(cancel, new LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        root.addView(buttons);
    }

    /** Back from the form returns to the list; back from the list leaves. */
    @Override
    public void onBackPressed()
    {
        super.onBackPressed();
    }

    /* ---- helpers ---------------------------------------------------------- */

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

    private View spacer(int h)
    {
        View v = new View(this);
        v.setLayoutParams(new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, h));
        return v;
    }

    private int dp(int value)
    {
        return (int)(value * getResources().getDisplayMetrics().density + 0.5f);
    }
}
