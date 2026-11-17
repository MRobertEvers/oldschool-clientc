package com.torirs.client;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.os.Process;

/**
 * Starts the app again in a NEW process: how a newer game pack takes over
 * from one this process already loaded (a loaded .so cannot be unloaded).
 *
 * It runs in a process of its own (android:process=":restart"), so it outlives
 * the one it kills: the old process asks for it and exits, and this one kills
 * whatever is left of the old one, opens the launcher -- in a fresh main
 * process -- and exits too.
 */
public final class RestartActivity extends Activity
{
    private static final String EXTRA_PID = "com.torirs.client.RESTART_PID";

    /** Called from the main process; does not return. */
    static void restart(Context context)
    {
        Intent intent = new Intent(context, RestartActivity.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        intent.putExtra(EXTRA_PID, Process.myPid());
        context.startActivity(intent);
        Runtime.getRuntime().exit(0);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);
        int pid = getIntent().getIntExtra(EXTRA_PID, -1);
        if( pid > 0 && pid != Process.myPid() )
            Process.killProcess(pid);
        Intent launcher = new Intent(this, LauncherActivity.class);
        launcher.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
        startActivity(launcher);
        finish();
        Runtime.getRuntime().exit(0);
    }
}
