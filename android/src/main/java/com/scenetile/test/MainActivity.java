package com.scenetile.test;

import android.app.Activity;
import android.os.Bundle;
import android.view.WindowManager;
import android.widget.Toast;
import android.content.res.AssetManager;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.IOException;

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
        
        // Copy cache files from assets to files directory
        try {
            copyCacheFiles();
        } catch (IOException e) {
            Toast.makeText(this, "Failed to copy cache files: " + e.getMessage(), Toast.LENGTH_LONG).show();
        }
        
        // Let SDL handle initialization when surface is ready
        // Don't call nativeInit() here - it's too early!
    }

    @Override
    protected void onDestroy() {
        nativeCleanup();
        super.onDestroy();
    }

    @Override
    protected void onPause() {
        super.onPause();
        // SDL will handle pause/resume automatically
    }

    @Override
    protected void onResume() {
        super.onResume();
        // SDL will handle pause/resume automatically
    }

    private void copyCacheFiles() throws IOException {
        AssetManager assetManager = getAssets();
        
        // Create cache directory in app's files directory
        File cacheDir = new File(getFilesDir(), "cache");
        if (!cacheDir.exists()) {
            cacheDir.mkdirs();
        }
        
        // List of cache files to copy (based on your cache directory contents)
        String[] cacheFiles = {
            "main_file_cache.dat2",
            "main_file_cache.idx0", "main_file_cache.idx1", "main_file_cache.idx2",
            "main_file_cache.idx3", "main_file_cache.idx4", "main_file_cache.idx5",
            "main_file_cache.idx6", "main_file_cache.idx7", "main_file_cache.idx8",
            "main_file_cache.idx9", "main_file_cache.idx10", "main_file_cache.idx11",
            "main_file_cache.idx12", "main_file_cache.idx13", "main_file_cache.idx14",
            "main_file_cache.idx15", "main_file_cache.idx16", "main_file_cache.idx17",
            "main_file_cache.idx18", "main_file_cache.idx19", "main_file_cache.idx20",
            "main_file_cache.idx21", "main_file_cache.idx22", "main_file_cache.idx23",
            "main_file_cache.idx24", "main_file_cache.idx255",
            "xteas.json"
        };
        
        // Copy each file from assets to files/cache directory
        for (String fileName : cacheFiles) {
            try {
                InputStream inputStream = assetManager.open(fileName);
                File outputFile = new File(cacheDir, fileName);
                
                // Only copy if file doesn't exist or is different size
                if (!outputFile.exists() || outputFile.length() != inputStream.available()) {
                    FileOutputStream outputStream = new FileOutputStream(outputFile);
                    
                    byte[] buffer = new byte[8192];
                    int bytesRead;
                    while ((bytesRead = inputStream.read(buffer)) != -1) {
                        outputStream.write(buffer, 0, bytesRead);
                    }
                    
                    outputStream.close();
                }
                inputStream.close();
            } catch (IOException e) {
                // Log the error but continue with other files
                android.util.Log.w("MainActivity", "Failed to copy " + fileName + ": " + e.getMessage());
            }
        }
    }

    // Method to get cache directory path for native code
    public String getCachePath() {
        File cacheDir = new File(getFilesDir(), "cache");
        return cacheDir.getAbsolutePath();
    }

    // Native methods
    public native void nativeInit();
    public native void nativeCleanup();
//    public native void nativePause();
//    public native void nativeResume();
}
