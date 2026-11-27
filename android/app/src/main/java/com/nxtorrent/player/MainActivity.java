package com.nxtorrent.player;

import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.KeyEvent;
import android.view.WindowManager;
import android.window.OnBackInvokedCallback;
import android.window.OnBackInvokedDispatcher;

import org.libsdl.app.BorealisHandler;
import org.libsdl.app.PlatformUtils;
import org.libsdl.app.SDLActivity;

public class MainActivity extends SDLActivity {

    private static volatile String pendingIntentUri = null;
    private long lastBackKeyMs = 0L;
    private Object backInvokedCallback = null;

    private void dispatchNativeBack() {
        if (SDLActivity.mBrokenLibraries || SDLActivity.mSDLThread == null) {
            return;
        }
        long now = SystemClock.uptimeMillis();
        if (now - lastBackKeyMs < 150L) {
            return;
        }
        lastBackKeyMs = now;
        SDLActivity.onNativeKeyDown(KeyEvent.KEYCODE_BACK);
        SDLActivity.onNativeKeyUp(KeyEvent.KEYCODE_BACK);
    }

    public static synchronized String consumePendingIntentUri() {
        String uri = pendingIntentUri;
        pendingIntentUri = null;
        return uri;
    }

    private void recordOrDispatchIntent(Intent intent, boolean coldStart) {
        if (intent == null) {
            return;
        }
        Uri data = intent.getData();
        if (data == null) {
            return;
        }
        String uri = "magnet".equalsIgnoreCase(data.getScheme())
                ? intent.getDataString()
                : data.getPath();
        if (uri == null || uri.isEmpty()) {
            uri = intent.getDataString();
        }
        if (uri != null && !uri.isEmpty()) {
            if (!coldStart && mSDLThread != null && !mBrokenLibraries) {
                SDLActivity.onNativeDropFile(uri);
            } else {
                pendingIntentUri = uri;
            }
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // Keep screen awake during video playback
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        // Allow rendering into display cutout area (notches/punch-holes)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            WindowManager.LayoutParams lp = getWindow().getAttributes();
            lp.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            getWindow().setAttributes(lp);
        }

        // Connect Borealis brightness and platform handler
        PlatformUtils.borealisHandler = new BorealisHandler();

        // Capture initial magnet/file intent so native code can consume it after SDL_Init
        recordOrDispatchIntent(getIntent(), true);

        if (Build.VERSION.SDK_INT >= 33) {
            OnBackInvokedCallback cb = new OnBackInvokedCallback() {
                @Override
                public void onBackInvoked() {
                    if (SDLActivity.nativeGetHintBoolean("SDL_ANDROID_TRAP_BACK_BUTTON", false)) {
                        dispatchNativeBack();
                    }
                }
            };
            backInvokedCallback = cb;
            getOnBackInvokedDispatcher().registerOnBackInvokedCallback(
                    OnBackInvokedDispatcher.PRIORITY_DEFAULT, cb);
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (getWindow() != null) {
            getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        }
    }

    @Override
    public void onBackPressed() {
        if (SDLActivity.nativeGetHintBoolean("SDL_ANDROID_TRAP_BACK_BUTTON", false)) {
            dispatchNativeBack();
            return;
        }
        super.onBackPressed();
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (event != null && event.getKeyCode() == KeyEvent.KEYCODE_BACK
                && event.getAction() == KeyEvent.ACTION_UP) {
            lastBackKeyMs = SystemClock.uptimeMillis();
        }
        return super.dispatchKeyEvent(event);
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        recordOrDispatchIntent(intent, false);
    }

    @Override
    public void setOrientationBis(int w, int h, boolean resizable, String hint) {
        // Respect user/system auto-rotate toggle everywhere (UI and player)
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_USER);
    }

    @Override
    protected void onDestroy() {
        if (Build.VERSION.SDK_INT >= 33 && backInvokedCallback != null) {
            try {
                getOnBackInvokedDispatcher().unregisterOnBackInvokedCallback(
                        (OnBackInvokedCallback) backInvokedCallback);
            } catch (Exception ignored) {
            }
            backInvokedCallback = null;
        }
        super.onDestroy();
    }

    @Override
    protected String[] getLibraries() {
        return new String[] {
            "c++_shared",
            "SDL2",
            "avutil",
            "swresample",
            "swscale",
            "avcodec",
            "avformat",
            "avfilter",
            "avdevice",
            "mpv",
            "NX-torrent-player"
        };
    }
}
