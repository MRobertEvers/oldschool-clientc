package com.torirs.client;

import android.app.Activity;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkInfo;
import android.os.BatteryManager;
import android.graphics.Rect;
import android.os.Build;
import android.os.Bundle;
import android.text.InputType;
import android.util.DisplayMetrics;
import android.view.KeyCharacterMap;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.ViewTreeObserver;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;

import java.io.File;
import java.util.ArrayList;
import java.util.List;

/**
 * The client itself: a Surface, a stream of input, and the native frame loop.
 *
 * <h2>What this class does and does not do</h2>
 *
 * It does four things: hand the native side a Surface, forward touches and
 * keys, tell it the display density, and stop it cleanly. It does NOT decide
 * anything about a frame, translate a gesture, or map a key -- all of that is
 * in C, where every other platform's version of it already lives
 * (src/platform/platform_android.c, src/input/torirs_touch.c). A gesture policy
 * written here would be a second one, and it would drift from the one the
 * desktop and web builds share.
 *
 * <h2>Why the surface is not a GLSurfaceView</h2>
 *
 * The client owns its own render loop and its own EGL context; a GLSurfaceView
 * would bring a second render thread and a second context with its own opinion
 * about when a frame starts. A plain SurfaceView hands over an ANativeWindow
 * and stays out of the way, which is what both the software presenter and the
 * GLES2 renderer want.
 */
public final class ClientActivity extends Activity implements SurfaceHolder.Callback
{
    /** Absolute path of the boot manifest to run. @see BootActivity. */
    public static final String EXTRA_MANIFEST = "com.torirs.client.MANIFEST";

    /*
     * Must match enum PlatformAndroid_TouchAction in
     * src/platform/platform_android.h. Three integers rather than a shared
     * header because there is no way to share one, and because the C side
     * restates Android's KeyEvent constants for the same reason.
     */
    private static final int TOUCH_DOWN = 0;
    private static final int TOUCH_MOVE = 1;
    private static final int TOUCH_UP = 2;

    /*
     * Must match enum ToriRS_CmdNetworkKind in src/cmd/cmdbus.h, for the same
     * reason the touch actions above restate theirs.
     */
    private static final int NETWORK_NONE = 0;
    private static final int NETWORK_WIFI = 1;
    private static final int NETWORK_CELLULAR = 2;

    static
    {
        System.loadLibrary("torirs");
    }

    private SurfaceView surfaceView;
    private Button hideKeyboardButton;
    private PluginChromeLayout rootLayout;
    private PluginChromePresenter pluginChrome;
    private boolean started;
    private int lastImeInset;
    private int lastChromeInsetLeft;
    private int lastChromeInsetTop;
    private int lastChromeInsetRight;
    private int lastChromeInsetBottom;
    private boolean chromeBackHeld;

    /** Latest battery reading, held so a network change can report both
     *  numbers together. @see #reportDeviceStatus. */
    private int batteryPercent = 100;
    private int batteryCharging = 1;
    private BroadcastReceiver batteryReceiver;
    private BroadcastReceiver connectivityReceiver;
    private ConnectivityManager.NetworkCallback networkCallback;

    /**
     * A SurfaceView the IME will agree to type into.
     *
     * A plain SurfaceView is not a text editor: onCreateInputConnection
     * returns null, so no input session ever binds, and InputMethodManager
     * quietly drops showSoftInput for a view it is not serving -- the request
     * reached Java (the Hide-keyboard button appeared) and the keyboard never
     * did. This is the same problem every game engine hits on Android, and
     * the fix is the one they all ship: declare a dummy editor.
     *
     * TYPE_NULL is the whole contract: it tells the IME "no text field --
     * send raw key events", which the dummy BaseInputConnection also enforces
     * by synthesising KeyEvents for anything the IME commits as text. Those
     * events run the normal dispatch, the SurfaceView handles none of them,
     * and they land in the activity's onKeyDown/onKeyUp exactly where the
     * hardware keys already do -- so the C side sees one key stream and never
     * learns which kind of keyboard produced it.
     *
     * NO_EXTRACT_UI / NO_FULLSCREEN keep the landscape IME from replacing the
     * whole screen with its own editor box, which is what a landscape phone
     * otherwise does to an app it cannot show inline text for.
     */
    private static final class ClientSurfaceView extends SurfaceView
    {
        ClientSurfaceView(Context context)
        {
            super(context);
        }

        @Override
        public boolean onCheckIsTextEditor()
        {
            return true;
        }

        @Override
        public InputConnection onCreateInputConnection(EditorInfo outAttrs)
        {
            outAttrs.inputType = InputType.TYPE_NULL;
            outAttrs.imeOptions = EditorInfo.IME_ACTION_NONE
                    | EditorInfo.IME_FLAG_NO_EXTRACT_UI
                    | EditorInfo.IME_FLAG_NO_FULLSCREEN;
            return new BaseInputConnection(this, false);
        }
    }

    /* ---- native ---------------------------------------------------------- */

    private native void nativeStart(String[] args, String dataRoot);
    private native void nativeSurfaceChanged(Surface surface, int width, int height);
    private native void nativeSetDensity(int density);
    private native void nativeTouch(int action, int pointerId, int x, int y);
    private native void nativeKey(int keycode, int down, int unicode);
    private native void nativeKeyboardInset(int bottomPx);
    private native void nativeDeviceStatus(int batteryPercent, int batteryCharging, int networkKind);
    private native void nativePluginChromeIntent(
            int kind, int panel, int widget, int value, String text);
    private native void nativeStop();

    /* ---- lifecycle ------------------------------------------------------- */

    @Override
    protected void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        surfaceView = new ClientSurfaceView(this);
        surfaceView.getHolder().addCallback(this);
        /* The view has to be focusable in touch mode or it never receives key
         * events from a hardware or soft keyboard. */
        surfaceView.setFocusable(true);
        surfaceView.setFocusableInTouchMode(true);

        /*
         * The surface, with a "Hide keyboard" button floating over it.
         *
         * The button exists because Android's soft keyboard has no dismiss
         * affordance of its own that this app can rely on: Back closes it on
         * most devices, but Back is ALSO the client's Escape (see
         * android_keycode_to_torirsk), so a user pressing it to put the
         * keyboard away also closes whatever interface they were typing into.
         * An explicit button separates the two.
         *
         * It is visible only while the keyboard is up -- a permanent button
         * over the viewport would cover part of the world for a control that is
         * almost never wanted.
         */
        hideKeyboardButton = new Button(this);
        hideKeyboardButton.setText("Hide keyboard");
        hideKeyboardButton.setVisibility(View.GONE);
        hideKeyboardButton.setOnClickListener(new View.OnClickListener()
        {
            @Override
            public void onClick(View v)
            {
                showSoftKeyboard(false);
            }
        });
        rootLayout = new PluginChromeLayout(this, surfaceView, hideKeyboardButton);
        pluginChrome = new PluginChromePresenter(
                this,
                rootLayout,
                new PluginChromePresenter.IntentSink()
                {
                    @Override
                    public void send(int kind, int panel, int widget, int value, String text)
                    {
                        nativePluginChromeIntent(kind, panel, widget, value, text);
                    }
                });
        rootLayout.setEditorFocusListener(new PluginChromeLayout.EditorFocusListener()
        {
            @Override
            public void onPluginEditorFocusChanged(boolean focused)
            {
                /* The native keyboard inset belongs to the game editor. A
                 * framework EditText lays itself out through the pane's inset
                 * and must not also squash the game canvas behind it. */
                nativeKeyboardInset(focused ? 0 : lastImeInset);
                if( focused && hideKeyboardButton != null )
                    hideKeyboardButton.setVisibility(View.GONE);
            }
        });

        setContentView(rootLayout);
        surfaceView.requestFocus();

        /*
         * Tell the native side how much of the surface the soft keyboard is
         * covering, so the client can slide its chat line and login inputs up
         * from under it. Two listeners, because the signal has two eras:
         *
         *   API 30+ -- the window dispatches ime() insets to a fullscreen
         *   window as well, so the listener is the whole answer.
         *
         *   Older -- there is no ime inset type. The visible-display-frame
         *   comparison is the classic substitute; under this activity's
         *   fullscreen theme some devices never shrink the frame, and on those
         *   the report simply stays 0 -- the client keeps working with the
         *   keyboard over its chat, which is exactly what it did before this
         *   listener existed.
         *
         * Both report through one method so the native side has one number and
         * no opinion about which era produced it.
         */
        final View insetRoot = rootLayout;
        if( Build.VERSION.SDK_INT >= 30 )
        {
            insetRoot.setOnApplyWindowInsetsListener(new View.OnApplyWindowInsetsListener()
            {
                @Override
                public WindowInsets onApplyWindowInsets(View v, WindowInsets insets)
                {
                    android.graphics.Insets bars =
                            insets.getInsets(WindowInsets.Type.systemBars());
                    android.graphics.Insets cutout =
                            insets.getInsets(WindowInsets.Type.displayCutout());
                    android.graphics.Insets gestures =
                            insets.getInsets(WindowInsets.Type.systemGestures());
                    int ime = insets.getInsets(WindowInsets.Type.ime()).bottom;
                    setChromeInsets(
                            Math.max(bars.left, Math.max(cutout.left, gestures.left)),
                            Math.max(bars.top, Math.max(cutout.top, gestures.top)),
                            Math.max(bars.right, Math.max(cutout.right, gestures.right)),
                            Math.max(bars.bottom, Math.max(cutout.bottom, gestures.bottom)));
                    reportImeInset(ime);
                    return insets;
                }
            });
        }
        else
        {
            insetRoot.getViewTreeObserver().addOnGlobalLayoutListener(
                    new ViewTreeObserver.OnGlobalLayoutListener()
            {
                private final Rect visible = new Rect();

                @Override
                public void onGlobalLayout()
                {
                    insetRoot.getWindowVisibleDisplayFrame(visible);
                    int covered = insetRoot.getRootView().getHeight() - visible.bottom;
                    /* A sliver can be reported with no keyboard up (system
                     * bars settling); a keyboard is never that short. */
                    if( covered < 80 )
                        covered = 0;
                    reportImeInset(covered);
                }
            });

            /* API 21-29 has no per-type inset query, but its stable system
             * insets still keep the native rail/page clear of visible bars.
             * The global-layout listener above supplies the separate IME
             * estimate and the two are combined by reportImeInset(). */
            insetRoot.setOnApplyWindowInsetsListener(new View.OnApplyWindowInsetsListener()
            {
                @Override
                public WindowInsets onApplyWindowInsets(View v, WindowInsets insets)
                {
                    setChromeInsets(
                            insets.getSystemWindowInsetLeft(),
                            insets.getSystemWindowInsetTop(),
                            insets.getSystemWindowInsetRight(),
                            insets.getSystemWindowInsetBottom());
                    return insets;
                }
            });
        }

        nativeSetDensity(displayDensityBucket());
        startDeviceStatusReports();
    }

    private void setChromeInsets(int left, int top, int right, int bottom)
    {
        lastChromeInsetLeft = Math.max(0, left);
        lastChromeInsetTop = Math.max(0, top);
        lastChromeInsetRight = Math.max(0, right);
        lastChromeInsetBottom = Math.max(0, bottom);
        if( rootLayout != null )
            rootLayout.setChromeInsets(
                    lastChromeInsetLeft,
                    lastChromeInsetTop,
                    lastChromeInsetRight,
                    Math.max(lastChromeInsetBottom, lastImeInset));
    }

    private void reportImeInset(int bottom)
    {
        lastImeInset = Math.max(0, bottom);
        if( rootLayout != null )
        {
            rootLayout.setChromeInsets(
                    lastChromeInsetLeft,
                    lastChromeInsetTop,
                    lastChromeInsetRight,
                    Math.max(lastChromeInsetBottom, lastImeInset));
            nativeKeyboardInset(rootLayout.isPluginEditorFocused() ? 0 : lastImeInset);
        }
        else
            nativeKeyboardInset(lastImeInset);
    }

    /* ---- battery and network --------------------------------------------- */

    /**
     * Watch the battery and the network link, and report both to the client.
     *
     * The CS2 scripts ask for all three numbers -- percentage, charging, wifi
     * -- and the answers are Android's to give. Level state, not events: each
     * source updates its half and then reports both, and the native side
     * coalesces to one command per change (src/platform/platform_android.c).
     *
     * ACTION_BATTERY_CHANGED is sticky, so registerReceiver hands back the
     * current reading and the first report happens here rather than whenever
     * the battery next moves a percent.
     */
    private void startDeviceStatusReports()
    {
        batteryReceiver = new BroadcastReceiver()
        {
            @Override
            public void onReceive(Context context, Intent intent)
            {
                applyBatteryIntent(intent);
            }
        };
        Intent sticky = registerReceiver(
                batteryReceiver, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
        applyBatteryIntent(sticky);

        /*
         * The link, which has two eras like the keyboard inset does. API 24+
         * has a default-network callback; below that the broadcast is the only
         * signal, and it is not optional -- a battery tick can be minutes away
         * (measured on an API 22 phone: wifi off, and the client still said
         * wifi), so without this the answer is simply wrong for that long.
         */
        if( Build.VERSION.SDK_INT >= 24 )
        {
            ConnectivityManager cm =
                    (ConnectivityManager)getSystemService(Context.CONNECTIVITY_SERVICE);
            if( cm != null )
            {
                networkCallback = new ConnectivityManager.NetworkCallback()
                {
                    @Override
                    public void onAvailable(Network network)
                    {
                        reportDeviceStatus();
                    }

                    @Override
                    public void onLost(Network network)
                    {
                        reportDeviceStatus();
                    }

                    @Override
                    public void onCapabilitiesChanged(Network network, NetworkCapabilities caps)
                    {
                        reportDeviceStatus();
                    }
                };
                try
                {
                    cm.registerDefaultNetworkCallback(networkCallback);
                }
                catch( RuntimeException e )
                {
                    /* Some devices refuse the registration (too many callbacks
                     * process-wide). The battery tick still refreshes the link,
                     * so this is a slower answer, not a missing one. */
                    networkCallback = null;
                }
            }
        }
        else
        {
            connectivityReceiver = new BroadcastReceiver()
            {
                @Override
                public void onReceive(Context context, Intent intent)
                {
                    reportDeviceStatus();
                }
            };
            registerReceiver(
                    connectivityReceiver,
                    new IntentFilter(ConnectivityManager.CONNECTIVITY_ACTION));
        }
    }

    private void stopDeviceStatusReports()
    {
        if( batteryReceiver != null )
        {
            unregisterReceiver(batteryReceiver);
            batteryReceiver = null;
        }
        if( connectivityReceiver != null )
        {
            unregisterReceiver(connectivityReceiver);
            connectivityReceiver = null;
        }
        if( networkCallback != null )
        {
            ConnectivityManager cm =
                    (ConnectivityManager)getSystemService(Context.CONNECTIVITY_SERVICE);
            if( cm != null )
                cm.unregisterNetworkCallback(networkCallback);
            networkCallback = null;
        }
    }

    /** Take the battery half from a BATTERY_CHANGED intent, then report. */
    private void applyBatteryIntent(Intent intent)
    {
        if( intent == null )
            return;

        int level = intent.getIntExtra(BatteryManager.EXTRA_LEVEL, -1);
        int scale = intent.getIntExtra(BatteryManager.EXTRA_SCALE, -1);
        int status = intent.getIntExtra(BatteryManager.EXTRA_STATUS, -1);

        if( level >= 0 && scale > 0 )
            batteryPercent = (level * 100) / scale;
        batteryCharging = (status == BatteryManager.BATTERY_STATUS_CHARGING
                || status == BatteryManager.BATTERY_STATUS_FULL) ? 1 : 0;
        reportDeviceStatus();
    }

    private void reportDeviceStatus()
    {
        nativeDeviceStatus(batteryPercent, batteryCharging, currentNetworkKind());
    }

    /**
     * Which link the device is on right now, as a NETWORK_* value.
     *
     * Anything that is not cellular and is connected counts as wifi: what the
     * scripts do with the answer is decide whether the connection is the
     * metered one, and ethernet or a dock is not.
     */
    private int currentNetworkKind()
    {
        ConnectivityManager cm =
                (ConnectivityManager)getSystemService(Context.CONNECTIVITY_SERVICE);
        if( cm == null )
            return NETWORK_NONE;

        if( Build.VERSION.SDK_INT >= 23 )
        {
            Network active = cm.getActiveNetwork();
            if( active == null )
                return NETWORK_NONE;
            NetworkCapabilities caps = cm.getNetworkCapabilities(active);
            if( caps == null )
                return NETWORK_NONE;
            if( caps.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR) )
                return NETWORK_CELLULAR;
            return NETWORK_WIFI;
        }

        NetworkInfo info = cm.getActiveNetworkInfo();
        if( info == null || !info.isConnected() )
            return NETWORK_NONE;
        return info.getType() == ConnectivityManager.TYPE_MOBILE
                ? NETWORK_CELLULAR
                : NETWORK_WIFI;
    }

    @Override
    protected void onDestroy()
    {
        super.onDestroy();
        stopDeviceStatusReports();
        if( started )
        {
            /*
             * Blocks until the frame loop has finished its frame and shut the
             * client down. Android allows this in onDestroy, and the
             * alternative -- letting the process be killed with the preference
             * file or the incremental cache half-written -- is how a save gets
             * corrupted.
             */
            nativeStop();
            started = false;
        }
        if( pluginChrome != null )
            pluginChrome.shutdown();
    }

    /* ---- the surface ----------------------------------------------------- */

    @Override
    public void surfaceCreated(SurfaceHolder holder)
    {
        /*
         * Deliberately empty. A surface without a size is not yet useful to the
         * renderer, and surfaceChanged is always called right after this with
         * one -- so publishing here would only mean publishing twice, the first
         * time with nothing the native side can size a canvas from.
         */
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height)
    {
        nativeSurfaceChanged(holder.getSurface(), width, height);

        if( !started )
        {
            /*
             * The client starts only once there is a surface to draw on. It
             * could be started earlier -- PlatformWindow_Init waits for the
             * window anyway -- but starting it here means the first thing the
             * frame loop ever sees is a real size, so nothing boots at a
             * placeholder resolution and relayouts a moment later.
             */
            nativeStart(buildArgs(), BootProfile.dataRoot(this).getAbsolutePath());
            started = true;
        }
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder)
    {
        /*
         * Android takes the Surface back whenever the activity stops, and gives
         * a DIFFERENT one back on resume. The frame loop keeps running across
         * this: the world ticks and the network drains, and Present simply has
         * nowhere to put a picture until a surface returns.
         *
         * Publishing the null BEFORE returning is what makes that safe -- the C
         * side must not still be holding this surface when this method returns,
         * because Android destroys it the moment it does.
         */
        nativeSurfaceChanged(null, 0, 0);
    }

    /* ---- argv ------------------------------------------------------------ */

    /**
     * The command line, exactly as a desktop invocation would spell it.
     *
     * This is the whole reason the Android app needs no configuration format of
     * its own: the client is configured by argv and a manifest on every host,
     * and this method is the only place that changes between them.
     */
    private String[] buildArgs()
    {
        List<String> args = new ArrayList<>();

        String manifest = getIntent().getStringExtra(EXTRA_MANIFEST);
        if( manifest != null )
        {
            args.add("--manifest");
            args.add(manifest);
        }

        /*
         * Where the client may WRITE -- preferences, saves, the incremental
         * cache -- is not an argument. It is the process's working directory
         * and $HOME, which the native side sets from the dataRoot passed to
         * nativeStart; see place_process() in platform_android_jni.c. The
         * client reads both the same way it does on any other host.
         */

        /*
         * An extra_args file lets a profile be tried with a flag -- --gles2 to
         * take the GPU path, --offline, a --connect override -- without
         * rebuilding the APK. One argument per line, blank lines and '#'
         * comments ignored.
         */
        args.addAll(readExtraArgs());

        return args.toArray(new String[0]);
    }

    private List<String> readExtraArgs()
    {
        List<String> extra = new ArrayList<>();
        File f = new File(BootProfile.dataRoot(this), "extra_args.txt");

        if( !f.isFile() )
            return extra;
        try( java.io.BufferedReader r = new java.io.BufferedReader(new java.io.FileReader(f)) )
        {
            String line;
            while( (line = r.readLine()) != null )
            {
                line = line.trim();
                if( line.isEmpty() || line.startsWith("#") )
                    continue;
                extra.add(line);
            }
        }
        catch( Exception ignored )
        {
            /* A missing or unreadable extras file is not a boot failure: it is
             * an optional override, and the profile is still perfectly
             * bootable without it. */
        }
        return extra;
    }

    /* ---- input ----------------------------------------------------------- */

    /*
     * Where the surface sits inside the window.
     *
     * Activity.onTouchEvent is handed the event the decor view could not
     * deliver to any child, and its coordinates are relative to the WINDOW.
     * The native side treats what it is given as a point on the SURFACE, and
     * the two are not the same origin: the surface is the content area, which
     * begins below the status bar and beside the navigation bar, and moves
     * again whenever the soft keyboard pans the window. Forwarding the window
     * point unchanged therefore offsets every touch by however far the surface
     * has been pushed -- which is a tap landing somewhere it was not made.
     *
     * Re-read per gesture rather than cached: the offset changes with rotation,
     * with the system bars, and with the IME, and none of those notify here.
     */
    private final int[] surfaceOrigin = new int[2];

    private void readSurfaceOrigin()
    {
        if( surfaceView == null )
        {
            surfaceOrigin[0] = 0;
            surfaceOrigin[1] = 0;
            return;
        }
        surfaceView.getLocationInWindow(surfaceOrigin);
    }

    private void touchToSurface(int action, int pointerId, float windowX, float windowY)
    {
        nativeTouch(
                action,
                pointerId,
                (int)(windowX - surfaceOrigin[0]),
                (int)(windowY - surfaceOrigin[1]));
    }

    @Override
    public boolean onTouchEvent(MotionEvent event)
    {
        final int action = event.getActionMasked();

        readSurfaceOrigin();

        switch( action )
        {
        case MotionEvent.ACTION_DOWN:
        case MotionEvent.ACTION_POINTER_DOWN:
        {
            int i = event.getActionIndex();
            if( action == MotionEvent.ACTION_DOWN )
                surfaceView.requestFocus();
            touchToSurface(TOUCH_DOWN, event.getPointerId(i), event.getX(i), event.getY(i));
            return true;
        }
        case MotionEvent.ACTION_MOVE:
        {
            /*
             * One MotionEvent can carry several fingers AND several historical
             * samples of each -- the digitiser reports faster than the app is
             * called. The history is forwarded too: dropping it turns a smooth
             * drag into a series of jumps, and the gesture policy in C measures
             * distance travelled to decide whether a touch is a tap or a drag.
             */
            final int pointers = event.getPointerCount();
            final int history = event.getHistorySize();
            for( int h = 0; h < history; h++ )
            {
                for( int i = 0; i < pointers; i++ )
                    touchToSurface(TOUCH_MOVE, event.getPointerId(i),
                            event.getHistoricalX(i, h), event.getHistoricalY(i, h));
            }
            for( int i = 0; i < pointers; i++ )
                touchToSurface(TOUCH_MOVE, event.getPointerId(i), event.getX(i), event.getY(i));
            return true;
        }
        case MotionEvent.ACTION_UP:
        case MotionEvent.ACTION_POINTER_UP:
        {
            int i = event.getActionIndex();
            touchToSurface(TOUCH_UP, event.getPointerId(i), event.getX(i), event.getY(i));
            return true;
        }
        case MotionEvent.ACTION_CANCEL:
        {
            /* The gesture was taken over by something else (a system gesture,
             * a notification pull). Every finger must be ended, or the C side
             * keeps tracking contacts that will never be lifted. */
            for( int i = 0; i < event.getPointerCount(); i++ )
                touchToSurface(TOUCH_UP, event.getPointerId(i), event.getX(i), event.getY(i));
            return true;
        }
        default:
            return super.onTouchEvent(event);
        }
    }

    /**
     * A modifier the soft keyboard made up.
     *
     * The dummy input connection turns what the IME commits into KeyEvents,
     * and an IME shifting for a capital sends SHIFT down, the letter, SHIFT up
     * -- or not the up, depending on the keyboard app. The client keeps
     * modifier state per key (the scripts ask keyheld(shift) when they
     * rebuild the inventory: shift held binds the shift-click DROP to every
     * slot), so one lost release turned every later tap on an item into a
     * drop. A virtual keyboard's modifiers mean nothing to the game -- the
     * character it commits already carries its case -- so they stop here.
     * A hardware keyboard (a real device id) keeps its modifiers.
     */
    private static boolean isVirtualModifier(KeyEvent event)
    {
        if( event.getDeviceId() != KeyCharacterMap.VIRTUAL_KEYBOARD )
            return false;
        switch( event.getKeyCode() )
        {
        case KeyEvent.KEYCODE_SHIFT_LEFT:
        case KeyEvent.KEYCODE_SHIFT_RIGHT:
        case KeyEvent.KEYCODE_CTRL_LEFT:
        case KeyEvent.KEYCODE_CTRL_RIGHT:
        case KeyEvent.KEYCODE_ALT_LEFT:
        case KeyEvent.KEYCODE_ALT_RIGHT:
            return true;
        default:
            return false;
        }
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event)
    {
        /*
         * Volume keys are the system's, always. Swallowing them would leave a
         * user unable to change the volume while the client is up, and the
         * client binds nothing to them.
         */
        if( keyCode == KeyEvent.KEYCODE_VOLUME_UP || keyCode == KeyEvent.KEYCODE_VOLUME_DOWN )
            return super.onKeyDown(keyCode, event);
        if( (keyCode == KeyEvent.KEYCODE_BACK || keyCode == KeyEvent.KEYCODE_ESCAPE) &&
                pluginChrome != null && pluginChrome.handleBack() )
        {
            chromeBackHeld = true;
            return true;
        }
        if( isVirtualModifier(event) )
            return true;

        nativeKey(keyCode, 1, event.getUnicodeChar(event.getMetaState()));
        return true;
    }

    @Override
    public boolean onKeyUp(int keyCode, KeyEvent event)
    {
        if( keyCode == KeyEvent.KEYCODE_VOLUME_UP || keyCode == KeyEvent.KEYCODE_VOLUME_DOWN )
            return super.onKeyUp(keyCode, event);
        if( chromeBackHeld &&
                (keyCode == KeyEvent.KEYCODE_BACK || keyCode == KeyEvent.KEYCODE_ESCAPE) )
        {
            chromeBackHeld = false;
            return true;
        }
        if( isVirtualModifier(event) )
            return true;

        nativeKey(keyCode, 0, 0);
        return true;
    }

    /**
     * Called from the native frame thread when the client refuses to boot.
     * @see PlatformAndroid_BootFailed, which is where the reason comes from.
     *
     * The native side has already ended its own thread; all that is left is to
     * put the person back where they can act on the message. That is the boot
     * menu, which shows it and does NOT run its countdown -- booting the same
     * profile again four seconds later would replay the same failure forever.
     *
     * Posted to the UI thread: the caller is the frame thread, and finishing an
     * activity is not its to do.
     */
    @SuppressWarnings("unused") /* invoked by JNI, by name */
    public void bootFailed(final String message)
    {
        runOnUiThread(new Runnable()
        {
            @Override
            public void run()
            {
                Intent intent = new Intent(ClientActivity.this, BootActivity.class);
                intent.putExtra(BootActivity.EXTRA_BOOT_ERROR, message);
                startActivity(intent);
                finish();
            }
        });
    }

    /**
     * One complete native chrome transaction. JNI calls this from the frame
     * thread only after SYNC_END; the single Runnable is the thread boundary,
     * and PluginChromePresenter applies all mutations under one suppressed
     * layout pass on Android's UI thread.
     */
    @SuppressWarnings("unused") /* invoked by JNI, by name */
    public void applyPluginChromeBatch(final int[] words, final byte[] strings)
    {
        runOnUiThread(new Runnable()
        {
            @Override
            public void run()
            {
                if( pluginChrome != null )
                    pluginChrome.applyBatch(words, strings);
            }
        });
    }

    /** Release native controls when the one shared executor is unbound. */
    @SuppressWarnings("unused") /* invoked by JNI, by name */
    public void endPluginChrome()
    {
        runOnUiThread(new Runnable()
        {
            @Override
            public void run()
            {
                if( pluginChrome != null )
                    pluginChrome.shutdown();
            }
        });
    }

    /**
     * Called from the native frame thread. @see PlatformWindow_SetTextInput.
     *
     * Posted to the UI thread rather than acted on directly: every
     * InputMethodManager call must happen there, and the caller is the frame
     * thread.
     */
    @SuppressWarnings("unused") /* invoked by JNI, by name */
    public void showSoftKeyboard(final boolean show)
    {
        runOnUiThread(new Runnable()
        {
            @Override
            public void run()
            {
                InputMethodManager imm =
                        (InputMethodManager)getSystemService(Context.INPUT_METHOD_SERVICE);
                if( imm == null || surfaceView == null )
                    return;
                if( show )
                {
                    if( rootLayout != null && rootLayout.isPluginEditorFocused() )
                        return;
                    /* Focus taken again from scratch, and the input session
                     * restarted: after the person hid the keyboard with the
                     * system's Back, InputMethodManager treats a plain show on
                     * the still-focused view as already satisfied and does
                     * nothing -- the dismiss button appeared, the keyboard did
                     * not. Dropping and retaking focus is what makes the next
                     * show a new request. */
                    surfaceView.requestFocus();
                    /* Flags 0, and NOT SHOW_FORCED. A forced show survives
                     * the system Back that the person uses to put the
                     * keyboard away: the IME goes but the window keeps the
                     * space reserved, so the client's safe-area inset never
                     * returned to zero and the whole frame stayed squashed
                     * into the top half of the screen. The focus dance above
                     * is what makes a plain show work again after a Back. */
                    imm.showSoftInput(surfaceView, 0);
                }
                else
                {
                    imm.hideSoftInputFromWindow(surfaceView.getWindowToken(), 0);
                }
                /* The dismiss button exists exactly as long as the thing it
                 * dismisses. */
                if( hideKeyboardButton != null )
                    hideKeyboardButton.setVisibility(show ? View.VISIBLE : View.GONE);
            }
        });
    }

    /* ---- display --------------------------------------------------------- */

    /**
     * Drawable pixels per 160dpi point, 1..4.
     *
     * Not used to scale the framebuffer -- an Android Surface is already device
     * pixels. It exists for the chrome, which picks which BAKED font size to
     * lay itself out with; a UI authored for 1x pixels on a 3x phone would
     * otherwise come out a third of its intended physical size.
     */
    private int displayDensityBucket()
    {
        DisplayMetrics m = getResources().getDisplayMetrics();
        int bucket = Math.round(m.density);
        if( bucket < 1 )
            bucket = 1;
        if( bucket > 4 )
            bucket = 4;
        return bucket;
    }
}
