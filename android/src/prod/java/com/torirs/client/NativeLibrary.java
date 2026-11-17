package com.torirs.client;

import android.app.Activity;
import android.content.Intent;

/**
 * Which native library the PROD app runs: the game pack LauncherActivity
 * downloaded, verified and loaded (GamePack.java). The APK carries none.
 */
final class NativeLibrary
{
    /** Where a refused boot goes: the launcher, which shows the reason and
     *  offers Retry and Sign out. */
    static final Class<? extends Activity> BOOT_SCREEN = LauncherActivity.class;

    private NativeLibrary()
    {
    }

    /**
     * True when this process already loaded a game pack and runs the relay
     * the client dials. False -- after sending the person to the launcher --
     * when Android restored ClientActivity straight into a new process (it
     * does that after killing a backgrounded app): nothing is loaded yet, and
     * only the launcher knows how to get it.
     */
    static boolean prepare(Activity activity)
    {
        if( GamePack.loadedPath() != null && IapTunnel.current() != null )
            return true;
        Intent intent = new Intent(activity, LauncherActivity.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
        activity.startActivity(intent);
        return false;
    }
}
