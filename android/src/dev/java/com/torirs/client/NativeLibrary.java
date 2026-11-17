package com.torirs.client;

import android.app.Activity;

/**
 * Which native library the DEV app runs: the one packaged in the APK, which
 * `make -C src PLATFORM=android` wrote into src/main/jniLibs.
 *
 * The prod flavor has its own NativeLibrary with the same two members; it
 * loads the downloaded game pack instead. ClientActivity calls only these.
 */
final class NativeLibrary
{
    /** Where a refused boot goes: the boot menu, which shows the reason. */
    static final Class<? extends Activity> BOOT_SCREEN = BootActivity.class;

    private NativeLibrary()
    {
    }

    /** Load the library (once per process; loadLibrary is idempotent).
     *  Always true here: a missing .so is an UnsatisfiedLinkError, which is
     *  the loud failure a broken dev build should be. */
    static boolean prepare(Activity activity)
    {
        System.loadLibrary("torirs");
        return true;
    }
}
