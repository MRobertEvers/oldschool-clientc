package com.torirs.client;

import android.annotation.SuppressLint;
import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.graphics.Color;
import android.util.Base64;
import android.webkit.JavascriptInterface;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;

import java.io.ByteArrayInputStream;
import java.nio.charset.Charset;
import java.util.ArrayDeque;
import java.util.concurrent.ConcurrentHashMap;

import org.json.JSONException;
import org.json.JSONObject;

/**
 * One locked-down WebView presenting the canonical ToriRSChrome bundle.
 *
 * <p>The bundle belongs to the application, not to a plugin. Plugins provide
 * only semantic model records through C; no plugin HTML, script, URL or native
 * callback enters this view. The WebView persists across collapse and page
 * selection, so all plugins share one rail and exactly one mounted page.</p>
 */
public final class PluginChromePresenter
{
    public interface IntentSink
    {
        void send(
                int kind, int panel, int widget, int value, String text,
                int x, int y, int selectionGeneration, int widgetSerial);
    }

    public interface ShellSink
    {
        void select(int pluginIndex, int selectionGeneration);
        void layout(
                int selectionGeneration, int width, int height, int scaleMilli,
                int sizeClass, boolean visible, boolean gameVisible);
    }

    private static final String BUNDLE_ROOT = "file:///android_asset/plugin_chrome/";
    /** Explicit compatibility choice: the API22 device embeds Chrome 39. */
    private static final String BUNDLE_URL = BUNDLE_ROOT + "legacy-ie8.html";
    private static final Charset UTF8 = Charset.forName("UTF-8");
    private static final int WORDS_PER_COMMAND = 11;
    private static final int LABEL_BYTES = 64;
    private static final int TEXT_BYTES = 192;
    private static final int STRING_BYTES = LABEL_BYTES + TEXT_BYTES;
    private static final int RAIL_HEADER_WORDS = 8;
    private static final int RAIL_ENTRY_WORDS = 4;
    private static final int RAIL_TITLE_BYTES = 64;
    private static final int RAIL_ICON_BYTES = 64;
    private static final int RAIL_BADGE_BYTES = 24;
    private static final int RAIL_ENTRY_BYTES =
            RAIL_TITLE_BYTES + RAIL_ICON_BYTES + RAIL_BADGE_BYTES;
    private static final int SCRIPT_QUEUE_MAX = 128;
    private static final int CUSTOM_ACTIVATE = 8;

    private final PluginChromeLayout layout;
    private final IntentSink intentSink;
    private final ShellSink shellSink;
    private final WebView webView;
    private final ArrayDeque<String> pendingScripts = new ArrayDeque<>();
    private final ConcurrentHashMap<Integer, Integer> widgetSerials =
            new ConcurrentHashMap<>();
    private boolean ready;
    private volatile boolean destroyed;
    private boolean expanded;
    private volatile int activePanel = -1;
    private volatile int railSelectionGeneration;
    private volatile int railPageGeneration;
    private int presentedPageGeneration;
    private int customRevision;
    private String pageTitle = "Plugins";
    private int currentCheckStyle;

    @SuppressLint({ "SetJavaScriptEnabled", "JavascriptInterface" })
    public PluginChromePresenter(
            Context context,
            PluginChromeLayout layout,
            IntentSink intentSink,
            ShellSink shellSink)
    {
        this.layout = layout;
        this.intentSink = intentSink;
        this.shellSink = shellSink;

        if( (context.getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) != 0 )
            WebView.setWebContentsDebuggingEnabled(true);
        webView = new WebView(context);
        webView.setBackgroundColor(Color.rgb(14, 14, 12));
        webView.setFocusable(true);
        webView.setFocusableInTouchMode(true);
        webView.setOverScrollMode(WebView.OVER_SCROLL_NEVER);
        webView.setHorizontalScrollBarEnabled(false);
        webView.setVerticalScrollBarEnabled(false);

        WebSettings settings = webView.getSettings();
        settings.setJavaScriptEnabled(true);
        settings.setDomStorageEnabled(false);
        settings.setDatabaseEnabled(false);
        settings.setAllowContentAccess(false);
        settings.setAllowFileAccess(true); /* packaged android_asset only */
        settings.setAllowFileAccessFromFileURLs(false);
        settings.setAllowUniversalAccessFromFileURLs(false);
        settings.setBlockNetworkLoads(true);
        settings.setSupportMultipleWindows(false);
        settings.setJavaScriptCanOpenWindowsAutomatically(false);
        settings.setMixedContentMode(WebSettings.MIXED_CONTENT_NEVER_ALLOW);
        settings.setCacheMode(WebSettings.LOAD_NO_CACHE);

        webView.removeJavascriptInterface("searchBoxJavaBridge_");
        webView.removeJavascriptInterface("accessibility");
        webView.removeJavascriptInterface("accessibilityTraversal");
        webView.addJavascriptInterface(new Bridge(), "ToriRSAndroid");
        webView.setWebViewClient(new LocalOnlyClient());
        layout.attachChrome(webView);
        layout.setChromeOpen(false);
        webView.loadUrl(BUNDLE_URL);
    }

    /** One complete atomic model transaction. */
    public void applyBatch(int[] words, byte[] strings)
    {
        if( destroyed || words == null || strings == null ||
                words.length % WORDS_PER_COMMAND != 0 )
            return;
        int count = words.length / WORDS_PER_COMMAND;
        if( strings.length != count * STRING_BYTES )
            return;
        StringBuilder commands = new StringBuilder(Math.max(32, count * 96));
        boolean opened = false;
        boolean closed = false;
        int panel = activePanel;
        String title = pageTitle;
        int checkStyle = currentCheckStyle;
        commands.append('[');
        for( int i = 0; i < count; i++ )
        {
            int at = i * WORDS_PER_COMMAND;
            int textAt = i * STRING_BYTES;
            int kind = words[at];
            int commandPanel = words[at + 1];
            if( kind == 3 )
            {
                widgetSerials.clear();
                panel = commandPanel;
                activePanel = commandPanel;
                expanded = true;
                opened = true;
                title = decode(strings, textAt + LABEL_BYTES, TEXT_BYTES);
                pageTitle = title;
                layout.setChromeOpen(true);
            }
            else if( kind == 4 && commandPanel == activePanel )
            {
                closed = true;
                activePanel = -1;
                widgetSerials.clear();
            }
            else if( kind == 19 )
            {
                checkStyle = words[at + 4];
                currentCheckStyle = checkStyle;
            }
            else if( kind == 5 && commandPanel == activePanel )
            {
                title = decode(strings, textAt + LABEL_BYTES, TEXT_BYTES);
                pageTitle = title;
            }
            if( kind == 8 && words[at + 2] >= 0 )
                widgetSerials.put(words[at + 2], words[at + 10]);
            else if( kind == 9 )
                widgetSerials.remove(words[at + 2]);
            if( i > 0 )
                commands.append(',');
            commands.append("{\"k\":").append(kind)
                    .append(",\"p\":").append(commandPanel)
                    .append(",\"w\":").append(words[at + 2])
                    .append(",\"tab\":").append(words[at + 3])
                    .append(",\"v\":").append(words[at + 4])
                    .append(",\"c\":").append(words[at + 5] & 0xffffffffL)
                    .append(",\"x\":").append(words[at + 6])
                    .append(",\"y\":").append(words[at + 7])
                    .append(",\"cw\":").append(words[at + 8])
                    .append(",\"ch\":").append(words[at + 9])
                    .append(",\"s\":").append(words[at + 10] & 0xffffffffL)
                    .append(",\"label\":\"");
            appendJson(commands, decode(strings, textAt, LABEL_BYTES));
            commands.append("\",\"text\":\"");
            appendJson(commands, decode(strings, textAt + LABEL_BYTES, TEXT_BYTES));
            commands.append("\"}");
        }
        commands.append(']');
        if( !opened && activePanel >= 0 && presentedPageGeneration != railPageGeneration )
            opened = true;
        if( closed && !opened )
        {
            receive("{\"protocol\":1,\"type\":\"page.close\",\"pageGeneration\":" +
                    (railPageGeneration & 0xffffffffL) + "}");
            return;
        }
        StringBuilder envelope = new StringBuilder(commands.length() + 180);
        envelope.append("{\"protocol\":1,\"type\":\"")
                .append(opened ? "page.snapshot" : "page.delta")
                .append("\",\"pageGeneration\":")
                .append(railPageGeneration & 0xffffffffL);
        if( opened )
        {
            envelope.append(",\"panel\":").append(panel)
                    .append(",\"title\":\"");
            appendJson(envelope, title);
            envelope.append("\",\"checkStyle\":").append(checkStyle);
        }
        envelope.append(",\"commands\":").append(commands).append('}');
        receive(envelope.toString());
        if( opened )
            presentedPageGeneration = railPageGeneration;
    }

    /** Complete host-owned rail state; latest snapshot replaces older state. */
    public void applyRailSnapshot(int[] words, byte[] strings)
    {
        if( destroyed || words == null || strings == null ||
                words.length < RAIL_HEADER_WORDS )
            return;
        int count = words[7];
        if( count < 0 || count > 33 ||
                words.length != RAIL_HEADER_WORDS + count * RAIL_ENTRY_WORDS ||
                strings.length != count * RAIL_ENTRY_BYTES )
            return;
        railSelectionGeneration = words[1];
        if( railPageGeneration != words[2] )
        {
            widgetSerials.clear();
            presentedPageGeneration = 0;
        }
        railPageGeneration = words[2];
        expanded = words[6] != 0;
        if( !expanded )
        {
            activePanel = -1;
            presentedPageGeneration = 0;
            widgetSerials.clear();
        }
        for( int i = 0; i < count; i++ )
        {
            int wa = RAIL_HEADER_WORDS + i * RAIL_ENTRY_WORDS;
            if( words[wa + 1] == words[5] )
            {
                layout.setPanelWidthDp(words[wa + 2]);
                break;
            }
        }
        layout.setChromeOpen(expanded);

        StringBuilder json = new StringBuilder(256 + count * 180);
        json.append("{\"protocol\":1,\"type\":\"rail.snapshot\"")
                .append(",\"registryRevision\":").append(words[0] & 0xffffffffL)
                .append(",\"selectionGeneration\":").append(words[1] & 0xffffffffL)
                .append(",\"pageGeneration\":").append(words[2] & 0xffffffffL)
                .append(",\"activePlugin\":").append(words[3])
                .append(",\"lastSelectedPlugin\":").append(words[4])
                .append(",\"selectedEntry\":").append(words[5])
                .append(",\"expanded\":").append(words[6] != 0 ? "true" : "false")
                .append(",\"entries\":[");
        for( int i = 0; i < count; i++ )
        {
            int wa = RAIL_HEADER_WORDS + i * RAIL_ENTRY_WORDS;
            int sa = i * RAIL_ENTRY_BYTES;
            if( i > 0 )
                json.append(',');
            json.append("{\"kind\":").append(words[wa])
                    .append(",\"pluginIndex\":").append(words[wa + 1])
                    .append(",\"preferredWidth\":").append(words[wa + 2])
                    .append(",\"attention\":").append(words[wa + 3] != 0 ? 1 : 0)
                    .append(",\"title\":\"");
            appendJson(json, decode(strings, sa, RAIL_TITLE_BYTES));
            json.append("\",\"iconAsset\":\"");
            appendJson(json, decode(strings, sa + RAIL_TITLE_BYTES, RAIL_ICON_BYTES));
            json.append("\",\"badge\":\"");
            appendJson(json, decode(
                    strings, sa + RAIL_TITLE_BYTES + RAIL_ICON_BYTES, RAIL_BADGE_BYTES));
            json.append("\"}");
        }
        json.append("]}");
        receive(json.toString());
    }

    public void applyRailIcon(
            int pluginIndex, int revision, int width, int height, int[] argb)
    {
        if( destroyed || pluginIndex < 0 || width < 0 || height < 0 ||
                width > 64 || height > 64 || (width * height > 0 &&
                (argb == null || argb.length != width * height)) )
            return;
        String json = "{\"protocol\":1,\"type\":\"rail.icon\",\"pluginIndex\":" +
                pluginIndex + ",\"revision\":" + (revision & 0xffffffffL) +
                ",\"width\":" + width + ",\"height\":" + height +
                ",\"rgbaBase64\":\"" + rgbaBase64(argb, width, height) + "\"}";
        receive(json);
    }

    public void applyCustomFrame(
            int panel, int widget, int selectionGeneration, int widgetSerial,
            int scaleMilli, int width, int height, int[] argb)
    {
        if( destroyed || selectionGeneration == 0 || widgetSerial == 0 ||
                scaleMilli <= 0 || width <= 0 || height <= 0 || width > 4096 ||
                height > 4096 || (long)width * height > 2_000_000L ||
                argb == null || argb.length != width * height ||
                selectionGeneration != railPageGeneration || panel != activePanel ||
                !serialMatches(widget, widgetSerial) )
            return;
        String json = "{\"protocol\":1,\"type\":\"custom.bitmap\",\"pageGeneration\":" +
                (selectionGeneration & 0xffffffffL) + ",\"panel\":" + panel +
                ",\"widget\":" + widget + ",\"widgetSerial\":" +
                (widgetSerial & 0xffffffffL) + ",\"revision\":" +
                nextCustomRevision() + ",\"scaleMilli\":" + scaleMilli +
                ",\"width\":" + width + ",\"height\":" + height +
                ",\"rgbaBase64\":\"" + rgbaBase64(argb, width, height) + "\"}";
        receive(json);
    }

    public void collapse()
    {
        if( destroyed )
            return;
        expanded = false;
        activePanel = -1;
        presentedPageGeneration = 0;
        widgetSerials.clear();
        layout.setChromeOpen(false);
        receive("{\"protocol\":1,\"type\":\"page.close\",\"pageGeneration\":" +
                (railPageGeneration & 0xffffffffL) + "}");
    }

    public boolean handleBack()
    {
        if( destroyed || !expanded )
            return false;
        postScript("(function(){var b=document.getElementById('tpc-close');if(b)b.click();})();");
        return true;
    }

    public boolean isOpen() { return !destroyed && expanded; }

    public void shutdown()
    {
        if( destroyed )
            return;
        destroyed = true;
        pendingScripts.clear();
        webView.removeJavascriptInterface("ToriRSAndroid");
        webView.stopLoading();
        webView.loadUrl("about:blank");
        webView.destroy();
    }

    private void postScript(String script)
    {
        if( destroyed )
            return;
        if( !ready )
        {
            if( pendingScripts.size() >= SCRIPT_QUEUE_MAX )
                pendingScripts.removeFirst();
            pendingScripts.addLast(script);
            return;
        }
        webView.evaluateJavascript(script, null);
    }

    private void receive(String json)
    {
        postScript("window.ToriRSPluginChrome&&window.ToriRSPluginChrome.receive(" + json + ");");
    }

    private void sendTheme()
    {
        receive("{\"protocol\":1,\"type\":\"theme\",\"revision\":1,\"assets\":{" +
                "\"panelBody\":\"skin/PanelBody.png\"," +
                "\"pluginIcon\":\"skin/PluginIcon.png\"," +
                "\"buttonLeft\":\"skin/ButtonLeft.png\"," +
                "\"buttonMiddle\":\"skin/ButtonMid.png\"," +
                "\"buttonRight\":\"skin/ButtonRight.png\"," +
                "\"checkOn\":\"skin/CheckOn.png\"," +
                "\"checkOff\":\"skin/CheckOff.png\"," +
                "\"checkBoxOn\":\"skin/CheckBoxOn.png\"," +
                "\"checkBoxOff\":\"skin/CheckBoxOff.png\"," +
                "\"dropdownBody\":\"skin/DropdownBody.png\"," +
                "\"close\":\"skin/CloseButton.png\"," +
                "\"scrollUp\":\"skin/ScrollUp.png\"," +
                "\"scrollDown\":\"skin/ScrollDown.png\"," +
                "\"scrollTrack\":\"skin/ScrollTrack.png\"," +
                "\"scrollGripTop\":\"skin/ScrollGripTop.png\"," +
                "\"scrollGripMiddle\":\"skin/ScrollGripMid.png\"," +
                "\"scrollGripBottom\":\"skin/ScrollGripBottom.png\"," +
                "\"frameTopLeft\":\"skin/FrameTopLeft.png\"," +
                "\"frameTop\":\"skin/FrameTop.png\"," +
                "\"frameTopRight\":\"skin/FrameTopRight.png\"," +
                "\"frameLeft\":\"skin/FrameLeft.png\"," +
                "\"frameRight\":\"skin/FrameRight.png\"," +
                "\"frameBottomLeft\":\"skin/FrameBottomLeft.png\"," +
                "\"frameBottom\":\"skin/FrameBottom.png\"," +
                "\"frameBottomRight\":\"skin/FrameBottomRight.png\"}}");
    }

    private static String decode(byte[] bytes, int offset, int capacity)
    {
        int length = 0;
        while( length < capacity && bytes[offset + length] != 0 )
            length++;
        return new String(bytes, offset, length, UTF8);
    }

    private boolean serialMatches(int widget, int serial)
    {
        Integer expected = widgetSerials.get(widget);
        return expected != null && expected == serial;
    }

    private int nextCustomRevision()
    {
        customRevision++;
        if( customRevision <= 0 )
            customRevision = 1;
        return customRevision;
    }

    private static void appendJson(StringBuilder out, String value)
    {
        for( int i = 0; i < value.length(); i++ )
        {
            char ch = value.charAt(i);
            switch( ch )
            {
            case '\\': out.append("\\\\"); break;
            case '"': out.append("\\\""); break;
            case '\n': out.append("\\n"); break;
            case '\r': out.append("\\r"); break;
            case '\t': out.append("\\t"); break;
            default:
                /* JSON permits these two Unicode separators unescaped, but a
                 * pre-ES2019 JavaScript parser treats them as source line
                 * endings. This JSON is embedded in evaluateJavascript, so
                 * escaping them keeps plugin-owned text data, never code. */
                if( ch < 32 || ch == '\u2028' || ch == '\u2029' )
                    out.append(String.format("\\u%04x", (int)ch));
                else
                    out.append(ch);
                break;
            }
        }
    }

    private static String rgbaBase64(int[] argb, int width, int height)
    {
        if( argb == null || width <= 0 || height <= 0 )
            return "";
        byte[] bytes = new byte[width * height * 4];
        for( int i = 0, b = 0; i < argb.length; i++, b += 4 )
        {
            int p = argb[i];
            bytes[b] = (byte)(p >>> 16);
            bytes[b + 1] = (byte)(p >>> 8);
            bytes[b + 2] = (byte)p;
            bytes[b + 3] = (byte)(p >>> 24);
        }
        return Base64.encodeToString(bytes, Base64.NO_WRAP);
    }

    private final class Bridge
    {
        @JavascriptInterface
        public void intent(
                int kind, int panel, int widget, int value, String text,
                int x, int y, int generation, int serial)
        {
            if( destroyed || kind < 1 || kind > CUSTOM_ACTIVATE ||
                    panel != activePanel )
                return;
            if( generation != railPageGeneration ||
                    (widget >= 0 && (serial == 0 ||
                     !serialMatches(widget, serial))) )
                return;
            String copied = text == null ? "" : text;
            if( copied.length() > TEXT_BYTES - 1 )
                copied = copied.substring(0, TEXT_BYTES - 1);
            intentSink.send(
                    kind, panel, widget, value, copied, x, y, generation, serial);
        }

        /** Versioned outbound envelope for rail selection and layout. */
        @JavascriptInterface
        public void postMessage(String json)
        {
            if( destroyed || json == null || json.length() > 8192 )
                return;
            try
            {
                JSONObject message = new JSONObject(json);
                if( message.optInt("protocol", 0) != 1 )
                    return;
                String type = message.optString("type", "");
                if( "rail.select".equals(type) )
                {
                    int generation = message.optInt("selectionGeneration", 0);
                    int plugin = message.optInt("pluginIndex", -1);
                    if( generation == railSelectionGeneration && plugin != -1 )
                        shellSink.select(plugin, generation);
                }
                else if( "layout".equals(type) )
                {
                    int generation = message.optInt("selectionGeneration", 0);
                    if( generation != railSelectionGeneration ||
                            message.optInt("pageGeneration", 0) != railPageGeneration )
                        return;
                    shellSink.layout(
                            generation,
                            Math.max(0, message.optInt("width", 0)),
                            Math.max(0, message.optInt("height", 0)),
                            Math.max(1, message.optInt("scaleMilli", 1000)),
                            Math.max(0, Math.min(2, message.optInt("sizeClass", 0))),
                            message.optBoolean("visible", false),
                            layout.isGameVisible());
                }
                else if( "widget.intent".equals(type) )
                {
                    JSONObject intent = message.optJSONObject("intent");
                    if( intent != null )
                        intent(
                                intent.optInt("k", 0), intent.optInt("p", -1),
                                intent.optInt("w", -1), intent.optInt("v", 0),
                                intent.optString("text", ""), intent.optInt("x", 0),
                                intent.optInt("y", 0), intent.optInt("g", 0),
                                intent.optInt("s", 0));
                }
                else if( "editor.focus".equals(type) &&
                        message.optInt("pageGeneration", 0) == railPageGeneration )
                {
                    final boolean focused = message.optBoolean("focused", false);
                    webView.post(new Runnable()
                    {
                        @Override public void run()
                        {
                            if( !destroyed )
                                layout.setPluginEditorFocused(focused);
                        }
                    });
                }
            }
            catch( JSONException ignored )
            {
                /* Only the packaged runtime writes this boundary. */
            }
        }

    }

    private final class LocalOnlyClient extends WebViewClient
    {
        private boolean allowed(String url)
        {
            return url != null && url.startsWith(BUNDLE_ROOT);
        }

        @Override
        public boolean shouldOverrideUrlLoading(WebView view, String url)
        {
            return !allowed(url);
        }

        @Override
        public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request)
        {
            return request == null || !allowed(String.valueOf(request.getUrl()));
        }

        private WebResourceResponse blocked()
        {
            return new WebResourceResponse(
                    "text/plain", "UTF-8", new ByteArrayInputStream(new byte[0]));
        }

        @Override
        public WebResourceResponse shouldInterceptRequest(WebView view, String url)
        {
            return allowed(url) ? null : blocked();
        }

        @Override
        public WebResourceResponse shouldInterceptRequest(
                WebView view, WebResourceRequest request)
        {
            return request != null && allowed(String.valueOf(request.getUrl()))
                    ? null : blocked();
        }

        @Override
        public void onPageFinished(WebView view, String url)
        {
            if( destroyed || !BUNDLE_URL.equals(url) )
                return;
            ready = true;
            sendTheme();
            while( !pendingScripts.isEmpty() )
                view.evaluateJavascript(pendingScripts.removeFirst(), null);
        }
    }
}
