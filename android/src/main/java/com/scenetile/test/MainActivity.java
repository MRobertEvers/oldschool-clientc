package com.scenetile.test;

import android.app.Activity;
import android.os.Bundle;
import android.view.WindowManager;
import android.widget.Toast;

import org.libsdl.app.SDLActivity;

public class MainActivity extends SDLActivity {
    static {
        System.loadLibrary("scene_tile_test_android");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        
        // Keep screen on
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        
        // Set fullscreen
        getWindow().setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN,
                WindowManager.LayoutParams.FLAG_FULLSCREEN);
        
        // Initialize native code
        try {
            nativeInit();
        } catch (Exception e) {
            Toast.makeText(this, "Failed to initialize: " + e.getMessage(), Toast.LENGTH_LONG).show();
            finish();
        }
    }

    @Override
    protected void onDestroy() {
        nativeCleanup();
        super.onDestroy();
    }

    @Override
    protected void onPause() {
        nativePause();
        super.onPause();
    }

    @Override
    protected void onResume() {
        super.onResume();
        nativeResume();
    }

    // Native methods
    public native void nativeInit();
    public native void nativeCleanup();
//    public native void nativePause();
//    public native void nativeResume();
}
