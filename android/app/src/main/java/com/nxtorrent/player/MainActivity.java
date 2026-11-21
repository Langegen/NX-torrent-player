package com.nxtorrent.player;

import android.os.Bundle;
import android.view.WindowManager;

import org.libsdl.app.BorealisHandler;
import org.libsdl.app.PlatformUtils;
import org.libsdl.app.SDLActivity;

public class MainActivity extends SDLActivity {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // Keep screen awake during video playback
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        // Connect Borealis brightness and platform handler
        PlatformUtils.borealisHandler = new BorealisHandler();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        System.exit(0);
    }

    @Override
    protected String[] getLibraries() {
        return new String[] {
            "SDL2",
            "NX-torrent-player"
        };
    }
}
