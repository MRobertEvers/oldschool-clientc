package com.torirs.client;

import android.app.Activity;
import android.content.Context;
import android.os.Bundle;
import android.util.DisplayMetrics;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowManager;
import android.view.inputmethod.InputMethodManager;

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

    static
    {
        System.loadLibrary("torirs");
    }

    private SurfaceView surfaceView;
    private boolean started;

    /* ---- native ---------------------------------------------------------- */

    private native void nativeStart(String[] args, String dataRoot);
    private native void nativeSurfaceChanged(Surface surface, int width, int height);
    private native void nativeSetDensity(int density);
    private native void nativeTouch(int action, int pointerId, int x, int y);
    private native void nativeKey(int keycode, int down, int unicode);
    private native void nativeStop();

    /* ---- lifecycle ------------------------------------------------------- */

    @Override
    protected void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        surfaceView = new SurfaceView(this);
        surfaceView.getHolder().addCallback(this);
        /* The view has to be focusable in touch mode or it never receives key
         * events from a hardware or soft keyboard. */
        surfaceView.setFocusable(true);
        surfaceView.setFocusableInTouchMode(true);
        setContentView(surfaceView);
        surfaceView.requestFocus();

        nativeSetDensity(displayDensityBucket());
    }

    @Override
    protected void onDestroy()
    {
        super.onDestroy();
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
             * could be started earlier -- PlatformSDL2_Init waits for the
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
         * An extra_args file lets a profile be tried with a flag -- --webgl1 to
         * take the GLES2 path, --offline, a --connect override -- without
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

    @Override
    public boolean onTouchEvent(MotionEvent event)
    {
        final int action = event.getActionMasked();

        switch( action )
        {
        case MotionEvent.ACTION_DOWN:
        case MotionEvent.ACTION_POINTER_DOWN:
        {
            int i = event.getActionIndex();
            nativeTouch(TOUCH_DOWN, event.getPointerId(i), (int)event.getX(i), (int)event.getY(i));
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
                    nativeTouch(TOUCH_MOVE, event.getPointerId(i),
                            (int)event.getHistoricalX(i, h), (int)event.getHistoricalY(i, h));
            }
            for( int i = 0; i < pointers; i++ )
                nativeTouch(TOUCH_MOVE, event.getPointerId(i), (int)event.getX(i), (int)event.getY(i));
            return true;
        }
        case MotionEvent.ACTION_UP:
        case MotionEvent.ACTION_POINTER_UP:
        {
            int i = event.getActionIndex();
            nativeTouch(TOUCH_UP, event.getPointerId(i), (int)event.getX(i), (int)event.getY(i));
            return true;
        }
        case MotionEvent.ACTION_CANCEL:
        {
            /* The gesture was taken over by something else (a system gesture,
             * a notification pull). Every finger must be ended, or the C side
             * keeps tracking contacts that will never be lifted. */
            for( int i = 0; i < event.getPointerCount(); i++ )
                nativeTouch(TOUCH_UP, event.getPointerId(i), (int)event.getX(i), (int)event.getY(i));
            return true;
        }
        default:
            return super.onTouchEvent(event);
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

        nativeKey(keyCode, 1, event.getUnicodeChar(event.getMetaState()));
        return true;
    }

    @Override
    public boolean onKeyUp(int keyCode, KeyEvent event)
    {
        if( keyCode == KeyEvent.KEYCODE_VOLUME_UP || keyCode == KeyEvent.KEYCODE_VOLUME_DOWN )
            return super.onKeyUp(keyCode, event);

        nativeKey(keyCode, 0, 0);
        return true;
    }

    /**
     * Called from the native frame thread. @see PlatformSDL2_SetTextInput.
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
                    surfaceView.requestFocus();
                    imm.showSoftInput(surfaceView, InputMethodManager.SHOW_IMPLICIT);
                }
                else
                {
                    imm.hideSoftInputFromWindow(surfaceView.getWindowToken(), 0);
                }
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
