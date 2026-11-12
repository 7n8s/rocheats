package com.rocheats.monitor;

import android.Manifest;
import android.app.Activity;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.TextView;
import android.widget.Toast;

import java.util.List;

public final class MainActivity extends Activity implements MainView.Host {

    private MainView view;
    private SplashView splash;
    private SharedPreferences prefs;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private Feed.Result previous;
    private String lastRobloxUpload = "";
    private String lastRobloxVersion = "";

    private final Runnable tick = new Runnable() {
        @Override
        public void run() {
            if (view != null) {
                view.invalidate();
                if (view.dueNow()) doCheck();
            }
            ui.postDelayed(this, 1000);
        }
    };

    /** Roblox ships builds without warning, so this runs on its own fast clock. */
    private final Runnable robloxTick = new Runnable() {
        @Override
        public void run() {
            Feed.pollRoblox((version, upload) -> {
                if (!upload.isEmpty()) onRobloxSeen(version, upload);
            });
            ui.postDelayed(this, 20000);
        }
    };

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        prefs = getSharedPreferences("rcm", MODE_PRIVATE);
        Pal.accent = prefs.getInt("accent", Pal.ACCENTS[0]);

        Window win = getWindow();
        win.addFlags(WindowManager.LayoutParams.FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS);
        win.setStatusBarColor(Pal.BG);
        win.setNavigationBarColor(Pal.BG);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            WindowManager.LayoutParams lp = win.getAttributes();
            lp.layoutInDisplayCutoutMode =
                    WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            win.setAttributes(lp);
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            win.setDecorFitsSystemWindows(false);
        } else {
            win.getDecorView().setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                            | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN);
        }

        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Pal.BG);
        view = new MainView(this);
        view.bind(this, prefs);
        root.addView(view, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        splash = new SplashView(this);
        root.addView(splash, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        splash.setOnDone(() -> {
            root.removeView(splash);
            if (Feed.last() != null) view.setResult(Feed.last(), "");
        });
        setContentView(root);

        applyInsets(root);

        createChannel();
        maybeAskForNotifications();
        lastRobloxUpload = prefs.getString("robloxUpload", "");
        lastRobloxVersion = prefs.getString("robloxVersion", "");

        if (Feed.last() != null) {
            view.setResult(Feed.last(), "");
            previous = Feed.last();
        }
        view.setChecking(true);
        doCheck();
        ui.postDelayed(tick, 700);
        ui.postDelayed(robloxTick, 2500);
    }

    private void applyInsets(final FrameLayout root) {
        root.setOnApplyWindowInsetsListener((v, insets) -> {
            int top, bottom, left, right;
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                android.graphics.Insets i = insets.getInsets(
                        android.view.WindowInsets.Type.systemBars()
                                | android.view.WindowInsets.Type.displayCutout());
                top = i.top; bottom = i.bottom; left = i.left; right = i.right;
            } else {
                top = insets.getSystemWindowInsetTop();
                bottom = insets.getSystemWindowInsetBottom();
                left = insets.getSystemWindowInsetLeft();
                right = insets.getSystemWindowInsetRight();
            }
            v.setPadding(left, top, right, bottom);
            return insets;
        });
    }

    private static final String CHANNEL_EXEC = "status";
    private static final String CHANNEL_ROBLOX = "roblox";

    private void createChannel() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) return;
        NotificationManager nm = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
        NotificationChannel exec = new NotificationChannel(CHANNEL_EXEC, "Executor changes",
                NotificationManager.IMPORTANCE_DEFAULT);
        exec.setDescription("When a product you track updates, goes stale or is detected.");
        exec.enableLights(true);
        exec.setLightColor(Pal.accent);
        exec.setShowBadge(true);
        nm.createNotificationChannel(exec);

        NotificationChannel roblox = new NotificationChannel(CHANNEL_ROBLOX, "Roblox updates",
                NotificationManager.IMPORTANCE_HIGH);
        roblox.setDescription("The moment Roblox ships a new client build.");
        roblox.enableLights(true);
        roblox.setLightColor(Pal.accent);
        roblox.enableVibration(true);
        roblox.setShowBadge(true);
        nm.createNotificationChannel(roblox);
    }

    private void maybeAskForNotifications() {
        if (Build.VERSION.SDK_INT >= 33
                && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)
                != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS}, 3);
        }
    }

    /** A Roblox build was seen (polled or from a full refresh). */
    private void onRobloxSeen(String version, String upload) {
        if (upload.equals(lastRobloxUpload)) return;
        boolean had = !lastRobloxUpload.isEmpty();
        lastRobloxUpload = upload;
        lastRobloxVersion = version;
        prefs.edit().putString("robloxUpload", upload)
                .putString("robloxVersion", version).apply();
        if (view != null) {
            view.pulseNewBuild();
            if (had) doCheck();   // executors' "current build" flags all just changed
        }
        if (had && prefs.getInt("notifications", 1) == 1) {
            post("Roblox updated",
                    (version.isEmpty() ? "A new client build is out." : "Now on " + version)
                            + " Executors may need to catch up.", CHANNEL_ROBLOX);
        }
    }

    void doCheck() {
        view.setChecking(true);
        Feed.fetch(r -> {
            if (isFinishing() || isDestroyed()) return;
            view.setResult(r.ok ? r : null, r.ok ? "" : r.error);
            if (r.ok) {
                maybeNotify(previous, r);
                previous = r;
            }
        });
    }

    private void maybeNotify(Feed.Result before, Feed.Result after) {
        if (prefs.getInt("notifications", 1) == 0) return;
        boolean[] allowed = new boolean[Feed.KEYS.length];
        for (int i = 0; i < allowed.length; i++) {
            allowed[i] = prefs.getInt("notify_" + Feed.KEYS[i], 1) != 0;
        }
        // robloxBefore is empty here on purpose: MainActivity already announced the
        // build in onRobloxSeen, so Watch only reports executor changes.
        for (Watch.Event e : Watch.diff(before, after, "")) {
            if (Watch.muted(allowed, e)) continue;
            boolean roblox = e.kind == Watch.ROBLOX_BUILD;
            post(e.title, e.body, roblox ? CHANNEL_ROBLOX : CHANNEL_EXEC);
        }
    }

    private void post(String title, String body, String channel) {
        NotificationManager nm = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
        Intent i = new Intent(this, MainActivity.class);
        int flags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= 23) flags |= PendingIntent.FLAG_IMMUTABLE;
        PendingIntent pi = PendingIntent.getActivity(this, 0, i, flags);

        Notification.Builder nb;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            nb = new Notification.Builder(this, channel);
        } else {
            nb = new Notification.Builder(this);
        }
        nb.setSmallIcon(channel.equals(CHANNEL_ROBLOX)
                ? android.R.drawable.stat_notify_sync
                : android.R.drawable.stat_sys_download_done)
                .setContentTitle(title)
                .setContentText(body)
                .setColor(Pal.accent)
                .setAutoCancel(true)
                .setContentIntent(pi);
        nm.notify((int) (System.currentTimeMillis() % 10000), nb.build());
        if (prefs.getInt("sound", 1) == 1) {
            try {
                android.net.Uri u = android.media.RingtoneManager.getActualDefaultRingtoneUri(
                        this, android.media.RingtoneManager.TYPE_NOTIFICATION);
                if (u != null) android.media.RingtoneManager.getRingtone(this, u).play();
            } catch (Exception ignored) {
            }
        }
    }

    @Override
    public void onCheckNow() {
        doCheck();
    }

    @Override
    public void onPrefsChanged() {
        Pal.accent = prefs.getInt("accent", Pal.ACCENTS[0]);
        view.invalidate();
    }

    @Override
    public void onToast(String s) {
        Toast.makeText(this, s, Toast.LENGTH_SHORT).show();
    }

    @Override
    protected void onDestroy() {
        ui.removeCallbacksAndMessages(null);
        super.onDestroy();
    }
}
