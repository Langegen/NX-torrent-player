package org.libsdl.app;

import android.app.Activity;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkInfo;
import android.net.Uri;
import android.net.wifi.WifiInfo;
import android.net.wifi.WifiManager;
import android.os.BatteryManager;
import android.os.Message;
import android.provider.Settings;
import android.view.Window;
import android.view.WindowManager;

public class PlatformUtils {
    public static boolean isBatterySupported() {
        try {
            Context context = SDLActivity.getContext();
            if (context == null) return false;
            Intent batteryIntent = context.registerReceiver(null, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
            if (batteryIntent == null) return false;
            int status = batteryIntent.getIntExtra(BatteryManager.EXTRA_STATUS, -1);
            int level = batteryIntent.getIntExtra(BatteryManager.EXTRA_LEVEL, -1);
            return status != -1 && level >= 0;
        } catch (Exception ignored) {
            return false;
        }
    }

    public static int getBatteryLevel() {
        try {
            Context context = SDLActivity.getContext();
            if (context == null) return 100;

            Intent batteryIntent = context.registerReceiver(null, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
            if (batteryIntent == null) {
                return 100;
            }
            int level = batteryIntent.getIntExtra(BatteryManager.EXTRA_LEVEL, -1);
            int scale = batteryIntent.getIntExtra(BatteryManager.EXTRA_SCALE, -1);

            if (level >= 0 && scale > 0) {
                return (level * 100) / scale;
            }
        } catch (Exception ignored) {
        }
        return 100;
    }

    public static boolean isBatteryCharging() {
        try {
            Context context = SDLActivity.getContext();
            if (context == null) return false;

            IntentFilter filter = new IntentFilter(Intent.ACTION_BATTERY_CHANGED);
            Intent batteryStatus = context.registerReceiver(null, filter);
            if (batteryStatus == null) return false;

            int status = batteryStatus.getIntExtra(BatteryManager.EXTRA_STATUS, -1);
            return status == BatteryManager.BATTERY_STATUS_CHARGING ||
                    status == BatteryManager.BATTERY_STATUS_FULL;
        } catch (Exception ignored) {
            return false;
        }
    }

    public static boolean isEthernetConnected() {
        try {
            Context context = SDLActivity.getContext();
            if (context == null) return false;

            ConnectivityManager connectivityManager = (ConnectivityManager) context.getSystemService(Context.CONNECTIVITY_SERVICE);
            if (connectivityManager == null) return false;
            Network[] networks = connectivityManager.getAllNetworks();
            if (networks == null) return false;
            for (Network network : networks) {
                NetworkCapabilities capabilities = connectivityManager.getNetworkCapabilities(network);
                if (capabilities != null && capabilities.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET)) {
                    return true;
                }
            }
        } catch (Exception ignored) {
        }
        return false;
    }

    public static boolean isWifiSupported() {
        try {
            Context context = SDLActivity.getContext();
            if (context == null) return false;

            WifiManager wifiManager = (WifiManager) context.getApplicationContext().getSystemService(Context.WIFI_SERVICE);
            return wifiManager != null && wifiManager.isWifiEnabled();
        } catch (Exception ignored) {
            return false;
        }
    }

    public static boolean isWifiConnected() {
        try {
            Context context = SDLActivity.getContext();
            if (context == null) return false;

            ConnectivityManager connectivityManager = (ConnectivityManager) context.getSystemService(Context.CONNECTIVITY_SERVICE);
            if (connectivityManager == null) return false;
            Network[] networks = connectivityManager.getAllNetworks();
            if (networks != null) {
                for (Network network : networks) {
                    NetworkCapabilities capabilities = connectivityManager.getNetworkCapabilities(network);
                    if (capabilities != null && capabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)) {
                        return true;
                    }
                }
            }
            @SuppressWarnings("deprecation")
            NetworkInfo wifiInfo = connectivityManager.getNetworkInfo(ConnectivityManager.TYPE_WIFI);
            return wifiInfo != null && wifiInfo.isConnected();
        } catch (Exception ignored) {
            return false;
        }
    }

    public static int getWifiSignalStrength() {
        try {
            Context context = SDLActivity.getContext();
            if (context == null) return -100;

            WifiManager wifiManager = (WifiManager) context.getApplicationContext().getSystemService(Context.WIFI_SERVICE);
            if (wifiManager == null) return -100;
            WifiInfo wifiInfo = wifiManager.getConnectionInfo();
            if (wifiInfo == null) return -100;
            return wifiInfo.getRssi();
        } catch (Exception ignored) {
            return -100;
        }
    }

    public static void openBrowser(String url) {
        try {
            Context context = SDLActivity.getContext();
            if (context == null || url == null || url.isEmpty()) return;

            Uri webpage = Uri.parse(url);
            Intent intent = new Intent(Intent.ACTION_VIEW, webpage);
            if (!(context instanceof Activity)) {
                intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            }
            context.startActivity(intent);
        } catch (Exception ignored) {
        }
    }

    public static int[] getWebPSize(byte[] data) {
        if (data == null || data.length == 0) return null;
        try {
            BitmapFactory.Options opts = new BitmapFactory.Options();
            opts.inJustDecodeBounds = true;
            BitmapFactory.decodeByteArray(data, 0, data.length, opts);
            if (opts.outWidth <= 0 || opts.outHeight <= 0) return null;
            return new int[] { opts.outWidth, opts.outHeight };
        } catch (Exception ignored) {
            return null;
        }
    }

    public static byte[] decodeWebP(byte[] data) {
        if (data == null || data.length == 0) return null;
        Bitmap bmp = null;
        try {
            BitmapFactory.Options opts = new BitmapFactory.Options();
            opts.inPreferredConfig = Bitmap.Config.ARGB_8888;
            bmp = BitmapFactory.decodeByteArray(data, 0, data.length, opts);
            if (bmp == null) return null;
            if (bmp.getConfig() != Bitmap.Config.ARGB_8888) {
                Bitmap conv = bmp.copy(Bitmap.Config.ARGB_8888, false);
                bmp.recycle();
                bmp = conv;
                if (bmp == null) return null;
            }
            int w = bmp.getWidth();
            int h = bmp.getHeight();
            if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return null;

            int[] pixels = new int[w * h];
            bmp.getPixels(pixels, 0, w, 0, 0, w, h);

            // Pack 8-byte header (little-endian width, height) + RGBA bytes
            byte[] out = new byte[8 + w * h * 4];
            out[0] = (byte)(w & 0xff);
            out[1] = (byte)((w >> 8) & 0xff);
            out[2] = (byte)((w >> 16) & 0xff);
            out[3] = (byte)((w >> 24) & 0xff);
            out[4] = (byte)(h & 0xff);
            out[5] = (byte)((h >> 8) & 0xff);
            out[6] = (byte)((h >> 16) & 0xff);
            out[7] = (byte)((h >> 24) & 0xff);

            int idx = 8;
            for (int i = 0; i < pixels.length; i++) {
                int argb = pixels[i];
                out[idx++] = (byte)((argb >> 16) & 0xff); // R
                out[idx++] = (byte)((argb >> 8) & 0xff);  // G
                out[idx++] = (byte)(argb & 0xff);         // B
                out[idx++] = (byte)((argb >> 24) & 0xff); // A
            }
            return out;
        } catch (Throwable ignored) {
            return null;
        } finally {
            if (bmp != null) bmp.recycle();
        }
    }

    public static float getSystemScreenBrightness(Context context) {
        ContentResolver contentResolver = context.getContentResolver();
        return Settings.System.getInt(contentResolver,
                Settings.System.SCREEN_BRIGHTNESS, 125) * 1.0f / 255.0f;
    }

    public static BorealisHandler borealisHandler = null;

    public static void setAppScreenBrightness(Activity activity, float value) {
        Message message = Message.obtain();
        message.obj = activity;
        message.arg1 = (int)(value * 255);
        message.what = 0;
        if(borealisHandler != null) borealisHandler.sendMessage(message);
    }

    public static float getAppScreenBrightness(Activity activity) {
        Window window = activity.getWindow();
        WindowManager.LayoutParams lp = window.getAttributes();
        if (lp.screenBrightness < 0) return getSystemScreenBrightness(activity);
        return lp.screenBrightness;
    }
}