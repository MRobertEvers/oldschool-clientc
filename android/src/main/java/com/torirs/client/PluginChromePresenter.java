package com.torirs.client;

import android.app.AlertDialog;
import android.content.Context;
import android.content.DialogInterface;
import android.graphics.Color;
import android.text.InputType;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.Spinner;
import android.widget.TextView;
import android.util.SparseArray;

import java.nio.charset.Charset;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;

/**
 * Mirrors ToriRSChrome commands into framework Views.
 *
 * <p>There is one instance for the Activity, one rail entry and one page. The
 * command side may describe more than one model panel (older developer chrome
 * does), but only the most recently opened panel is materialised as Views. A
 * previous panel's controls are destroyed before another panel is mounted, so
 * plugins cannot accumulate hidden native UI.</p>
 */
public final class PluginChromePresenter
{
    public interface IntentSink
    {
        void send(int kind, int panel, int widget, int value, String text);
    }

    /* ToriRSChromeCmdKind. Keep in lockstep with torirs_chrome_exec.h. */
    private static final int CMD_PANEL_OPEN = 3;
    private static final int CMD_PANEL_CLOSE = 4;
    private static final int CMD_PANEL_TITLE = 5;
    private static final int CMD_PANEL_RECT = 6;
    private static final int CMD_PANEL_TAB = 7;
    private static final int CMD_WIDGET_ADD = 8;
    private static final int CMD_WIDGET_REMOVE = 9;
    private static final int CMD_WIDGET_LABEL = 10;
    private static final int CMD_WIDGET_TEXT = 11;
    private static final int CMD_WIDGET_CHECKED = 12;
    private static final int CMD_WIDGET_HIDDEN = 13;
    private static final int CMD_WIDGET_COLOR = 14;
    private static final int CMD_WIDGET_SELECTED = 15;
    private static final int CMD_WIDGET_FOCUS = 16;
    private static final int CMD_WIDGET_OPTIONS = 17;
    private static final int CMD_WIDGET_OPTION = 18;
    private static final int CMD_CHECK_STYLE = 19;

    /* ToriRSChromeIntentKind. */
    private static final int INTENT_ACTIVATE = 1;
    private static final int INTENT_ACTION = 2;
    private static final int INTENT_TOGGLE = 3;
    private static final int INTENT_TEXT = 4;
    private static final int INTENT_PICK = 5;
    private static final int INTENT_TAB = 6;
    private static final int INTENT_CLOSE = 7;

    /* ToriRSChromeWidgetKind. */
    private static final int W_LABEL = 0;
    private static final int W_CHECKBOX = 1;
    private static final int W_TEXTINPUT = 2;
    private static final int W_SEPARATOR = 3;
    private static final int W_MENUITEM = 4;
    private static final int W_DROPDOWN = 5;
    private static final int W_MODELVIEW = 6;
    private static final int W_BUTTON = 7;
    private static final int W_TABSTRIP = 8;
    private static final int W_LISTROW = 9;
    private static final int W_COLORPICK = 10;
    private static final int W_TEXTAREA = 11;

    private static final int ROW_ACTION = 0x1;
    private static final int ROW_LOCKED = 0x2;

    /** Ten scalar words and two fixed UTF-8 slots per command. */
    private static final int WORDS_PER_COMMAND = 10;
    private static final int LABEL_BYTES = 64;
    private static final int TEXT_BYTES = 192;
    private static final int STRING_BYTES = LABEL_BYTES + TEXT_BYTES;
    private static final int MAX_OPTIONS = 4096;

    private static final int I_KIND = 0;
    private static final int I_PANEL = 1;
    private static final int I_WIDGET = 2;
    private static final int I_TAB = 3;
    private static final int I_VALUE = 4;
    private static final int I_COLOR = 5;
    private static final int I_X = 6;
    private static final int I_Y = 7;
    private static final int I_W = 8;
    private static final int I_H = 9;

    private static final Charset UTF8 = Charset.forName("UTF-8");
    private static final int PAGE_BACKGROUND = Color.rgb(39, 39, 39);
    private static final int RAIL_BACKGROUND = Color.rgb(25, 25, 25);
    private static final int TEXT_COLOR = Color.rgb(235, 235, 235);
    private static final int LABEL_COLOR = Color.rgb(190, 190, 190);

    private final Context context;
    private final PluginChromeLayout layout;
    private final IntentSink sink;
    private final LinearLayout rail;
    private final Button railButton;
    private final LinearLayout pane;
    private final TextView title;
    private final Button close;
    private final ScrollView scroll;
    private final LinearLayout content;
    private final SparseArray<PanelRecord> panels = new SparseArray<>();
    private final SparseArray<WidgetRecord> widgets = new SparseArray<>();
    private long panelSequence;
    private long widgetSequence;
    private int activePanel = -1;
    private boolean applying;
    private AlertDialog colorDialog;
    private WidgetRecord colorDialogOwner;
    private boolean suppressDialogIntent;

    public PluginChromePresenter(
            Context context, PluginChromeLayout layout, IntentSink sink)
    {
        this.context = context;
        this.layout = layout;
        this.sink = sink;

        rail = new LinearLayout(context);
        rail.setOrientation(LinearLayout.VERTICAL);
        rail.setGravity(Gravity.TOP | Gravity.CENTER_HORIZONTAL);
        rail.setBackgroundColor(RAIL_BACKGROUND);

        railButton = new Button(context);
        railButton.setAllCaps(false);
        railButton.setText("P");
        railButton.setContentDescription("Plugin panel");
        railButton.setOnClickListener(new View.OnClickListener()
        {
            @Override
            public void onClick(View view)
            {
                requestClose();
            }
        });
        rail.addView(railButton, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        pane = new LinearLayout(context);
        pane.setOrientation(LinearLayout.VERTICAL);
        pane.setBackgroundColor(PAGE_BACKGROUND);

        LinearLayout header = new LinearLayout(context);
        header.setOrientation(LinearLayout.HORIZONTAL);
        header.setGravity(Gravity.CENTER_VERTICAL);
        int pad = dp(8);
        header.setPadding(pad, dp(4), dp(4), dp(4));

        title = makeText(18, TEXT_COLOR);
        title.setSingleLine(true);
        header.addView(title, new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1.0f));

        close = new Button(context);
        close.setText("\u00d7");
        close.setContentDescription("Close plugin panel");
        close.setOnClickListener(new View.OnClickListener()
        {
            @Override
            public void onClick(View view)
            {
                requestClose();
            }
        });
        header.addView(close, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        pane.addView(header, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        content = new LinearLayout(context);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setPadding(pad, pad, pad, pad);
        scroll = new ScrollView(context);
        scroll.setFillViewport(true);
        scroll.addView(content, new ScrollView.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        pane.addView(scroll, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1.0f));

        layout.attachChrome(rail, pane);
    }

    /** Apply exactly one native SYNC transaction. Called only on Android's UI
     * thread by ClientActivity's one posted Runnable. */
    public void applyBatch(int[] words, byte[] strings)
    {
        if( words == null || strings == null || words.length % WORDS_PER_COMMAND != 0 )
            return;
        int count = words.length / WORDS_PER_COMMAND;
        if( strings.length != count * STRING_BYTES )
            return;

        applying = true;
        layout.suppressLayout(true);
        try
        {
            for( int i = 0; i < count; i++ )
            {
                int at = i * WORDS_PER_COMMAND;
                String label = decode(strings, i * STRING_BYTES, LABEL_BYTES);
                String text = decode(strings, i * STRING_BYTES + LABEL_BYTES, TEXT_BYTES);
                applyOne(words, at, label, text);
            }
            reconcilePage();
        }
        finally
        {
            layout.suppressLayout(false);
            applying = false;
        }
    }

    public void shutdown()
    {
        applying = true;
        suppressDialogIntent = true;
        try
        {
            if( colorDialog != null )
                colorDialog.dismiss();
            colorDialog = null;
            colorDialogOwner = null;
            content.removeAllViews();
            for( int i = 0; i < widgets.size(); i++ )
                disposeView(widgets.valueAt(i));
            widgets.clear();
            panels.clear();
            activePanel = -1;
            title.setText("");
            layout.setPluginEditorFocused(false);
            layout.setChromeOpen(false);
        }
        finally
        {
            suppressDialogIntent = false;
            applying = false;
        }
    }

    /** Back/Escape closes a chrome editor first and the page second. Native
     * dropdowns and dialogs receive Back before the Activity does. */
    public boolean handleBack()
    {
        if( activePanel < 0 )
            return false;
        if( colorDialog != null )
        {
            colorDialog.dismiss();
            return true;
        }

        View focused = pane.findFocus();
        if( focused instanceof EditText )
        {
            WidgetRecord record = (WidgetRecord)focused.getTag();
            if( record != null )
                commitEditor(record);
            focused.clearFocus();
            hideIme(focused);
            layout.setPluginEditorFocused(false);
            return true;
        }

        requestClose();
        return true;
    }

    public boolean isOpen()
    {
        return activePanel >= 0;
    }

    private void applyOne(int[] a, int at, String labelValue, String textValue)
    {
        int kind = a[at + I_KIND];
        int panel = a[at + I_PANEL];
        int widget = a[at + I_WIDGET];

        switch( kind )
        {
        case CMD_PANEL_OPEN:
        {
            closePanel(panel);
            PanelRecord record = new PanelRecord();
            record.handle = panel;
            record.style = a[at + I_VALUE];
            record.title = textValue;
            record.activeTab = 0;
            record.sequence = ++panelSequence;
            panels.put(panel, record);
            break;
        }
        case CMD_PANEL_CLOSE:
            closePanel(panel);
            break;
        case CMD_PANEL_TITLE:
        {
            PanelRecord record = panels.get(panel);
            if( record != null )
                record.title = textValue;
            break;
        }
        case CMD_PANEL_TAB:
        {
            PanelRecord record = panels.get(panel);
            if( record != null )
                record.activeTab = a[at + I_VALUE];
            break;
        }
        case CMD_PANEL_RECT:
        case CMD_CHECK_STYLE:
            /* Android owns native layout and checkbox art. */
            break;
        case CMD_WIDGET_ADD:
        {
            removeWidget(widget);
            WidgetRecord record = new WidgetRecord();
            record.handle = widget;
            record.panel = panel;
            record.kind = a[at + I_VALUE];
            record.tab = a[at + I_TAB];
            record.rowShape = a[at + I_W];
            record.rows = a[at + I_H];
            record.label = labelValue;
            record.text = textValue;
            record.color = a[at + I_COLOR];
            record.selected = -1;
            record.order = ++widgetSequence;
            widgets.put(widget, record);
            break;
        }
        case CMD_WIDGET_REMOVE:
            removeWidget(widget);
            break;
        case CMD_WIDGET_LABEL:
        {
            WidgetRecord record = widgets.get(widget);
            if( record != null )
                record.label = labelValue;
            break;
        }
        case CMD_WIDGET_TEXT:
        {
            WidgetRecord record = widgets.get(widget);
            if( record != null )
                record.text = textValue;
            break;
        }
        case CMD_WIDGET_CHECKED:
        {
            WidgetRecord record = widgets.get(widget);
            if( record != null )
                record.checked = a[at + I_VALUE] != 0;
            break;
        }
        case CMD_WIDGET_HIDDEN:
        {
            WidgetRecord record = widgets.get(widget);
            if( record != null )
                record.hidden = a[at + I_VALUE] != 0;
            break;
        }
        case CMD_WIDGET_COLOR:
        {
            WidgetRecord record = widgets.get(widget);
            if( record != null )
                record.color = a[at + I_COLOR];
            break;
        }
        case CMD_WIDGET_SELECTED:
        {
            WidgetRecord record = widgets.get(widget);
            if( record != null )
                record.selected = a[at + I_VALUE];
            break;
        }
        case CMD_WIDGET_FOCUS:
        {
            WidgetRecord record = widgets.get(widget);
            if( record != null )
                record.focused = a[at + I_VALUE] != 0;
            break;
        }
        case CMD_WIDGET_OPTIONS:
        {
            WidgetRecord record = widgets.get(widget);
            if( record != null )
            {
                int optionCount = Math.max(0, Math.min(MAX_OPTIONS, a[at + I_VALUE]));
                record.options.clear();
                for( int i = 0; i < optionCount; i++ )
                    record.options.add("");
                record.optionsRevision++;
            }
            break;
        }
        case CMD_WIDGET_OPTION:
        {
            WidgetRecord record = widgets.get(widget);
            int index = a[at + I_VALUE];
            if( record != null && index >= 0 && index < record.options.size() )
            {
                record.options.set(index, textValue);
                record.optionsRevision++;
            }
            break;
        }
        default:
            /* Future optional command: ignored by this protocol generation. */
            break;
        }
    }

    private void closePanel(int panel)
    {
        if( panel < 0 )
            return;
        panels.remove(panel);
        for( int i = widgets.size() - 1; i >= 0; i-- )
        {
            WidgetRecord record = widgets.valueAt(i);
            if( record.panel == panel )
            {
                disposeView(record);
                widgets.removeAt(i);
            }
        }
    }

    private void removeWidget(int handle)
    {
        WidgetRecord record = widgets.get(handle);
        if( record == null )
            return;
        disposeView(record);
        widgets.remove(handle);
    }

    private void disposeView(WidgetRecord record)
    {
        if( colorDialogOwner == record && colorDialog != null )
        {
            boolean prior = suppressDialogIntent;
            suppressDialogIntent = true;
            colorDialog.dismiss();
            suppressDialogIntent = prior;
            colorDialog = null;
            colorDialogOwner = null;
        }
        if( record.root != null && record.root.getParent() instanceof ViewGroup )
            ((ViewGroup)record.root.getParent()).removeView(record.root);
        record.root = null;
        record.caption = null;
        record.editor = null;
        record.check = null;
        record.spinner = null;
        record.tabBar = null;
        record.swatch = null;
        record.viewOptionsRevision = -1;
    }

    private void reconcilePage()
    {
        int nextPanel = -1;
        long newest = Long.MIN_VALUE;
        for( int i = 0; i < panels.size(); i++ )
        {
            PanelRecord panel = panels.valueAt(i);
            if( panel.sequence > newest )
            {
                newest = panel.sequence;
                nextPanel = panel.handle;
            }
        }

        if( nextPanel != activePanel )
        {
            for( int i = 0; i < widgets.size(); i++ )
                disposeView(widgets.valueAt(i));
            content.removeAllViews();
            activePanel = nextPanel;
        }

        PanelRecord panel = panels.get(activePanel);
        if( panel == null )
        {
            layout.setPluginEditorFocused(false);
            layout.setChromeOpen(false);
            return;
        }

        title.setText(panel.title);
        railButton.setText(railCaption(panel.title));
        railButton.setContentDescription(panel.title.length() == 0 ? "Plugin panel" : panel.title);

        List<WidgetRecord> wanted = new ArrayList<>();
        for( int i = 0; i < widgets.size(); i++ )
        {
            WidgetRecord record = widgets.valueAt(i);
            if( record.panel != activePanel || record.hidden )
                continue;
            if( record.tab >= 0 && record.tab != panel.activeTab )
                continue;
            wanted.add(record);
        }
        Collections.sort(wanted, new Comparator<WidgetRecord>()
        {
            @Override
            public int compare(WidgetRecord left, WidgetRecord right)
            {
                return left.order < right.order ? -1 : (left.order == right.order ? 0 : 1);
            }
        });

        /* Controls from every nonselected panel are presentation resources,
         * not retained model state. Dispose them before mounting this page. */
        for( int i = 0; i < widgets.size(); i++ )
        {
            WidgetRecord record = widgets.valueAt(i);
            if( record.panel != activePanel || !wanted.contains(record) )
                disposeView(record);
        }

        for( int i = content.getChildCount() - 1; i >= 0; i-- )
        {
            Object tag = content.getChildAt(i).getTag();
            if( !(tag instanceof WidgetRecord) || !wanted.contains(tag) )
                content.removeViewAt(i);
        }

        for( int i = 0; i < wanted.size(); i++ )
        {
            WidgetRecord record = wanted.get(i);
            View view = ensureView(record);
            if( view.getParent() == null )
            {
                LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
                lp.bottomMargin = dp(6);
                int insertAt = Math.min(i, content.getChildCount());
                content.addView(view, insertAt, lp);
            }
            render(record);
        }
        layout.setChromeOpen(true);
    }

    private View ensureView(final WidgetRecord record)
    {
        if( record.root != null )
            return record.root;

        View root;
        switch( record.kind )
        {
        case W_LABEL:
        {
            TextView value = makeText(14, TEXT_COLOR);
            record.caption = value;
            root = value;
            break;
        }
        case W_CHECKBOX:
        {
            CheckBox checkBox = new CheckBox(context);
            checkBox.setTextColor(TEXT_COLOR);
            checkBox.setOnCheckedChangeListener((button, checked) ->
            {
                if( applying || widgets.get(record.handle) != record || checked == record.checked )
                    return;
                record.checked = checked;
                send(INTENT_TOGGLE, record, checked ? 1 : 0, "");
            });
            record.check = checkBox;
            root = checkBox;
            break;
        }
        case W_TEXTINPUT:
            root = makeEditor(record, false, false);
            break;
        case W_TEXTAREA:
            root = makeEditor(record, true, false);
            break;
        case W_SEPARATOR:
        {
            View separator = new View(context);
            separator.setBackgroundColor(Color.rgb(90, 90, 90));
            separator.setMinimumHeight(dp(1));
            root = separator;
            break;
        }
        case W_MENUITEM:
        case W_BUTTON:
        {
            Button button = new Button(context);
            button.setAllCaps(false);
            button.setOnClickListener(view -> send(INTENT_ACTIVATE, record, 0, ""));
            record.caption = button;
            root = button;
            break;
        }
        case W_DROPDOWN:
            root = makeDropdown(record);
            break;
        case W_MODELVIEW:
        {
            TextView placeholder = makeText(14, LABEL_COLOR);
            placeholder.setGravity(Gravity.CENTER);
            placeholder.setBackgroundColor(Color.rgb(52, 52, 52));
            placeholder.setMinHeight(dp(96));
            record.caption = placeholder;
            root = placeholder;
            break;
        }
        case W_TABSTRIP:
        {
            HorizontalScrollView strip = new HorizontalScrollView(context);
            strip.setHorizontalScrollBarEnabled(true);
            LinearLayout tabs = new LinearLayout(context);
            tabs.setOrientation(LinearLayout.HORIZONTAL);
            strip.addView(tabs, new FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));
            record.tabBar = tabs;
            root = strip;
            break;
        }
        case W_LISTROW:
            root = makeListRow(record);
            break;
        case W_COLORPICK:
            root = makeColorPicker(record);
            break;
        default:
        {
            TextView unsupported = makeText(14, LABEL_COLOR);
            unsupported.setText("Unsupported plugin control");
            root = unsupported;
            break;
        }
        }
        root.setTag(record);
        root.setContentDescription(record.label);
        record.root = root;
        return root;
    }

    private View makeEditor(final WidgetRecord record, boolean multiline, boolean colorField)
    {
        LinearLayout row = new LinearLayout(context);
        row.setOrientation(LinearLayout.VERTICAL);
        TextView caption = makeText(13, LABEL_COLOR);
        row.addView(caption, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        EditText editor = new EditText(context);
        editor.setTextColor(TEXT_COLOR);
        editor.setHintTextColor(LABEL_COLOR);
        if( multiline )
        {
            editor.setSingleLine(false);
            editor.setGravity(Gravity.TOP | Gravity.START);
            editor.setInputType(InputType.TYPE_CLASS_TEXT
                    | InputType.TYPE_TEXT_FLAG_MULTI_LINE
                    | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
            editor.setImeOptions(EditorInfo.IME_FLAG_NO_EXTRACT_UI);
            int rows = record.rows > 0 ? Math.min(record.rows, 12) : 4;
            editor.setMinLines(rows);
            editor.setMaxLines(Math.max(rows, 12));
        }
        else
        {
            editor.setSingleLine(true);
            editor.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
            editor.setImeOptions(EditorInfo.IME_ACTION_DONE | EditorInfo.IME_FLAG_NO_EXTRACT_UI);
        }
        editor.setTag(record);
        editor.setOnFocusChangeListener((view, focused) ->
        {
            if( applying || widgets.get(record.handle) != record )
                return;
            if( focused )
            {
                layout.setPluginEditorFocused(true);
                send(colorField ? INTENT_ACTION : INTENT_ACTIVATE, record, 0, "");
            }
            else
            {
                commitEditor(record);
                layout.post(new Runnable()
                {
                    @Override
                    public void run()
                    {
                        layout.setPluginEditorFocused(pane.findFocus() instanceof EditText);
                    }
                });
            }
        });
        editor.setOnEditorActionListener((view, actionId, event) ->
        {
            if( multiline && event != null && event.getKeyCode() == KeyEvent.KEYCODE_ENTER )
                return false;
            if( actionId == EditorInfo.IME_ACTION_DONE ||
                    (event != null && event.getKeyCode() == KeyEvent.KEYCODE_ENTER) )
            {
                commitEditor(record);
                return !multiline;
            }
            return false;
        });
        row.addView(editor, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        record.caption = caption;
        record.editor = editor;
        return row;
    }

    private View makeDropdown(final WidgetRecord record)
    {
        LinearLayout row = new LinearLayout(context);
        row.setOrientation(LinearLayout.VERTICAL);
        TextView caption = makeText(13, LABEL_COLOR);
        Spinner spinner = new Spinner(context);
        spinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener()
        {
            @Override
            public void onItemSelected(AdapterView<?> parent, View view, int position, long id)
            {
                if( applying || widgets.get(record.handle) != record || position == record.selected )
                    return;
                record.selected = position;
                String picked = position >= 0 && position < record.options.size()
                        ? record.options.get(position) : "";
                send(INTENT_PICK, record, position, picked);
            }

            @Override
            public void onNothingSelected(AdapterView<?> parent)
            {
                /* The model uses -1 for none, but Android reports this during
                 * adapter replacement too; the next SELECTED command decides. */
            }
        });
        row.addView(caption, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        row.addView(spinner, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        record.caption = caption;
        record.spinner = spinner;
        return row;
    }

    private View makeListRow(final WidgetRecord record)
    {
        LinearLayout row = new LinearLayout(context);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        boolean action = (record.rowShape & ROW_ACTION) != 0;
        boolean locked = (record.rowShape & ROW_LOCKED) != 0;

        TextView name;
        if( action )
        {
            Button button = new Button(context);
            button.setAllCaps(false);
            button.setGravity(Gravity.START | Gravity.CENTER_VERTICAL);
            button.setOnClickListener(view -> send(INTENT_ACTION, record, 0, ""));
            name = button;
        }
        else
            name = makeText(14, TEXT_COLOR);
        row.addView(name, new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1.0f));
        record.caption = name;

        if( !locked )
        {
            CheckBox toggle = new CheckBox(context);
            toggle.setContentDescription("Enable " + record.label);
            toggle.setOnCheckedChangeListener((button, checked) ->
            {
                if( applying || widgets.get(record.handle) != record || checked == record.checked )
                    return;
                record.checked = checked;
                send(INTENT_TOGGLE, record, checked ? 1 : 0, "");
            });
            row.addView(toggle, new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));
            record.check = toggle;
        }
        return row;
    }

    private View makeColorPicker(final WidgetRecord record)
    {
        LinearLayout outer = new LinearLayout(context);
        outer.setOrientation(LinearLayout.VERTICAL);
        TextView caption = makeText(13, LABEL_COLOR);
        outer.addView(caption, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        LinearLayout row = new LinearLayout(context);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        Button swatch = new Button(context);
        swatch.setText("Choose");
        swatch.setAllCaps(false);
        swatch.setOnClickListener(view -> showColorDialog(record));
        row.addView(swatch, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        /* Reuse the normal native editor behavior; ACTION rather than
         * ACTIVATE identifies the colour row's text-field zone. */
        EditText editor = new EditText(context);
        editor.setSingleLine(true);
        editor.setTextColor(TEXT_COLOR);
        editor.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        editor.setImeOptions(EditorInfo.IME_ACTION_DONE | EditorInfo.IME_FLAG_NO_EXTRACT_UI);
        editor.setTag(record);
        editor.setOnFocusChangeListener((view, focused) ->
        {
            if( applying || widgets.get(record.handle) != record )
                return;
            if( focused )
            {
                layout.setPluginEditorFocused(true);
                send(INTENT_ACTION, record, 0, "");
            }
            else
            {
                commitEditor(record);
                layout.post(() -> layout.setPluginEditorFocused(pane.findFocus() instanceof EditText));
            }
        });
        editor.setOnEditorActionListener((view, actionId, event) ->
        {
            if( actionId == EditorInfo.IME_ACTION_DONE ||
                    (event != null && event.getKeyCode() == KeyEvent.KEYCODE_ENTER) )
            {
                commitEditor(record);
                return true;
            }
            return false;
        });
        row.addView(editor, new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1.0f));
        outer.addView(row, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        record.caption = caption;
        record.editor = editor;
        record.swatch = swatch;
        return outer;
    }

    private void render(final WidgetRecord record)
    {
        if( record.root == null )
            return;
        record.root.setContentDescription(record.label);
        int textColor = record.color == 0 ? TEXT_COLOR : (0xff000000 | record.color);

        switch( record.kind )
        {
        case W_LABEL:
            record.caption.setText(joinLabelAndValue(record.label, record.text));
            record.caption.setTextColor(textColor);
            break;
        case W_CHECKBOX:
            record.check.setText(firstNonempty(record.label, record.text));
            record.check.setChecked(record.checked);
            record.check.setTextColor(textColor);
            break;
        case W_TEXTINPUT:
        case W_TEXTAREA:
            record.caption.setText(record.label);
            setEditorText(record);
            if( record.focused && !record.editor.hasFocus() )
                record.editor.requestFocus();
            else if( !record.focused && record.editor.hasFocus() )
                record.editor.clearFocus();
            break;
        case W_MENUITEM:
        case W_BUTTON:
            record.caption.setText(firstNonempty(record.text, record.label));
            record.caption.setTextColor(textColor);
            break;
        case W_DROPDOWN:
            record.caption.setText(record.label);
            if( record.viewOptionsRevision != record.optionsRevision )
            {
                ArrayAdapter<String> adapter = new ArrayAdapter<>(
                        context, android.R.layout.simple_spinner_item, record.options);
                adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
                record.spinner.setAdapter(adapter);
                record.viewOptionsRevision = record.optionsRevision;
            }
            if( record.selected >= 0 && record.selected < record.options.size() &&
                    record.spinner.getSelectedItemPosition() != record.selected )
                record.spinner.setSelection(record.selected, false);
            break;
        case W_MODELVIEW:
            record.caption.setText(firstNonempty(record.label, "Model preview"));
            record.caption.setTextColor(textColor);
            break;
        case W_TABSTRIP:
            renderTabs(record);
            break;
        case W_LISTROW:
            record.caption.setText(firstNonempty(record.label, record.text));
            record.caption.setTextColor(textColor);
            if( record.check != null )
                record.check.setChecked(record.checked);
            break;
        case W_COLORPICK:
            record.caption.setText(record.label);
            setEditorText(record);
            try
            {
                record.swatch.setBackgroundColor(Color.parseColor(record.text));
            }
            catch( IllegalArgumentException ignored )
            {
                record.swatch.setBackgroundColor(Color.DKGRAY);
            }
            if( record.focused && !record.editor.hasFocus() )
                record.editor.requestFocus();
            else if( !record.focused && record.editor.hasFocus() )
                record.editor.clearFocus();
            break;
        default:
            break;
        }
    }

    private void renderTabs(final WidgetRecord record)
    {
        if( record.tabBar == null || record.viewOptionsRevision == record.optionsRevision )
            return;
        record.tabBar.removeAllViews();
        for( int i = 0; i < record.options.size(); i++ )
        {
            final int index = i;
            Button tab = new Button(context);
            tab.setAllCaps(false);
            tab.setText(record.options.get(i));
            tab.setSelected(i == record.selected);
            tab.setEnabled(i != record.selected);
            tab.setOnClickListener(view ->
            {
                if( widgets.get(record.handle) != record || index == record.selected )
                    return;
                record.selected = index;
                send(INTENT_TAB, record, index, "");
            });
            record.tabBar.addView(tab, new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        }
        record.viewOptionsRevision = record.optionsRevision;
    }

    private void setEditorText(WidgetRecord record)
    {
        if( record.editor == null || record.editor.getText().toString().equals(record.text) )
            return;
        int selection = record.editor.getSelectionStart();
        record.editor.setText(record.text);
        record.editor.setSelection(Math.max(0, Math.min(selection, record.text.length())));
    }

    private void commitEditor(WidgetRecord record)
    {
        if( record.editor == null || widgets.get(record.handle) != record )
            return;
        String value = record.editor.getText().toString();
        if( value.equals(record.text) )
            return;
        /* Optimistic only to coalesce IME-action plus focus-loss. The model is
         * authoritative and its next WIDGET_TEXT can replace this value. */
        record.text = value;
        send(INTENT_TEXT, record, 0, value);
    }

    private void showColorDialog(final WidgetRecord record)
    {
        if( colorDialog != null || widgets.get(record.handle) != record || record.panel != activePanel )
            return;

        send(INTENT_ACTIVATE, record, 0, "");
        LinearLayout sliders = new LinearLayout(context);
        sliders.setOrientation(LinearLayout.VERTICAL);
        int pad = dp(16);
        sliders.setPadding(pad, pad, pad, pad);
        final SeekBar hue = addSlider(sliders, "Hue", 63, (record.selected >> 10) & 63);
        final SeekBar saturation = addSlider(
                sliders, "Saturation", 7, (record.selected >> 7) & 7);
        final SeekBar lightness = addSlider(sliders, "Lightness", 127, record.selected & 127);

        colorDialogOwner = record;
        colorDialog = new AlertDialog.Builder(context)
                .setTitle(firstNonempty(record.label, "Choose colour"))
                .setView(sliders)
                .setNegativeButton("Cancel", null)
                .setPositiveButton("Use", new DialogInterface.OnClickListener()
                {
                    @Override
                    public void onClick(DialogInterface dialog, int which)
                    {
                        int value = (hue.getProgress() << 10)
                                | (saturation.getProgress() << 7)
                                | lightness.getProgress();
                        record.selected = value;
                        send(INTENT_PICK, record, value, "");
                    }
                })
                .create();
        colorDialog.setOnDismissListener(dialog ->
        {
            boolean report = !suppressDialogIntent && widgets.get(record.handle) == record;
            colorDialog = null;
            colorDialogOwner = null;
            if( report )
                send(INTENT_ACTIVATE, record, 0, "");
        });
        colorDialog.show();
    }

    private SeekBar addSlider(LinearLayout parent, String label, int max, int progress)
    {
        TextView caption = makeText(13, LABEL_COLOR);
        caption.setText(label);
        parent.addView(caption, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        SeekBar bar = new SeekBar(context);
        bar.setMax(max);
        bar.setProgress(Math.max(0, Math.min(max, progress)));
        parent.addView(bar, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        return bar;
    }

    private void requestClose()
    {
        if( activePanel < 0 )
            return;
        View focused = pane.findFocus();
        if( focused instanceof EditText )
            commitEditor((WidgetRecord)focused.getTag());
        sink.send(INTENT_CLOSE, activePanel, -1, 0, "");
    }

    private void send(int kind, WidgetRecord record, int value, String textValue)
    {
        sink.send(kind, record.panel, record.handle, value, textValue == null ? "" : textValue);
    }

    private void hideIme(View view)
    {
        InputMethodManager imm =
                (InputMethodManager)context.getSystemService(Context.INPUT_METHOD_SERVICE);
        if( imm != null )
            imm.hideSoftInputFromWindow(view.getWindowToken(), 0);
    }

    private TextView makeText(float sp, int color)
    {
        TextView view = new TextView(context);
        view.setTextSize(sp);
        view.setTextColor(color);
        view.setGravity(Gravity.CENTER_VERTICAL);
        return view;
    }

    private int dp(int value)
    {
        return Math.round(value * context.getResources().getDisplayMetrics().density);
    }

    private static String decode(byte[] bytes, int offset, int capacity)
    {
        int length = 0;
        while( length < capacity && bytes[offset + length] != 0 )
            length++;
        return new String(bytes, offset, length, UTF8);
    }

    private static String firstNonempty(String first, String fallback)
    {
        return first != null && first.length() != 0 ? first : (fallback == null ? "" : fallback);
    }

    private static String joinLabelAndValue(String label, String value)
    {
        if( label == null || label.length() == 0 )
            return value == null ? "" : value;
        if( value == null || value.length() == 0 )
            return label;
        return label + ": " + value;
    }

    private static String railCaption(String value)
    {
        if( value == null || value.length() == 0 )
            return "P";
        int end = value.offsetByCodePoints(0, 1);
        return value.substring(0, end).toUpperCase();
    }

    private static final class PanelRecord
    {
        int handle;
        int style;
        int activeTab;
        long sequence;
        String title = "";
    }

    private static final class WidgetRecord
    {
        int handle;
        int panel;
        int kind;
        int tab;
        int rowShape;
        int rows;
        int selected;
        int color;
        long order;
        boolean checked;
        boolean hidden;
        boolean focused;
        String label = "";
        String text = "";
        final List<String> options = new ArrayList<>();
        int optionsRevision;
        int viewOptionsRevision = -1;

        View root;
        TextView caption;
        EditText editor;
        CheckBox check;
        Spinner spinner;
        LinearLayout tabBar;
        Button swatch;
    }
}
