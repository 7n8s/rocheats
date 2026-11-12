package com.rocheats.monitor;

import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.view.MotionEvent;


import java.awt.Graphics2D;
import java.awt.image.BufferedImage;
import java.io.File;
import java.lang.reflect.Field;
import java.lang.reflect.Method;

/**
 * Renders the real Android classes on the desktop, so the pixel output can be
 * checked without a device: java -cp out Render <outdir> <widthDp> <scale>
 */
public final class Render {

    public static void main(String[] a) throws Exception {
        String out = a.length > 0 ? a[0] : "shots";
        int wDp = a.length > 1 ? Integer.parseInt(a[1]) : 412;
        float scale = a.length > 2 ? Float.parseFloat(a[2]) : 2.75f;
        new File(out).mkdirs();

        // feed parsing checks, straight off the live payload shape
        java.lang.reflect.Method pd = Feed.class.getDeclaredMethod("parseDate", String.class);
        pd.setAccessible(true);
        long t = (Long) pd.invoke(null, "11/06/2026 at 1:24 PM UTC");
        System.out.println("parseDate check: 11/06/2026 at 1:24 PM UTC -> "
                + java.time.Instant.ofEpochMilli(t) + " (want 2026-11-06T13:24:00Z)");
        System.out.println("parseDate junk -> " + pd.invoke(null, "whenever") + " (want 0)");

        Context ctx = new Context();
        ctx.res.metrics.density = scale;
        ctx.res.metrics.widthPixels = Math.round(wDp * scale);

        int markId = 1001, smallId = 1002;
        BitmapFactory.REGISTRY.put(markId, "res/drawable-nodpi/mark.png");
        BitmapFactory.REGISTRY.put(smallId, "res/drawable-nodpi/mark_small.png");
        ctx.res.register("mark", "drawable", markId);
        ctx.res.register("mark_small", "drawable", smallId);
        int id = 2000;
        for (String k : Feed.KEYS) {
            BitmapFactory.REGISTRY.put(id, "res/drawable-nodpi/logo_" + k + ".jpg");
            ctx.res.register("logo_" + k, "drawable", id++);
        }

        int w = Math.round(wDp * scale);
        int h = Math.round(915 * scale);

        // real feed data, so the shot matches what the app shows on a phone
        Feed.Result r = liveOrSample();
        setStatic("last", r);

        MainView v = new MainView(ctx);
        v.bind(new Host(), ctx.getSharedPreferences("rcm", 0));
        v.layout(w, h);
        v.setResult(r, "");
        paint(v, w, h, out + "/android-dashboard.png");

        SharedPreferences sp = prefsOf(v, ctx);
        sp.edit().putString("scene", "midnight");
        v.invalidate();
        paint(v, w, h, out + "/android-midnight.png");

        sp.edit().putString("scene", "aurora");
        Pal.accent = Pal.ACCENTS[1];
        sp.edit().putInt("accent", Pal.ACCENTS[1]);

        callDraw(v, w, h);
        checkIgnored(v, w);
        v.openSheet();
        for (int i = 0; i < 6; i++) { stepSheet(v); try { Thread.sleep(60); } catch (Exception e) {} }
        paint(v, w, h, out + "/android-settings.png");
        // splash, drawn by the real class
        SplashView sv = new SplashView(ctx);
        sv.layout(w, h);
        BufferedImage si = new BufferedImage(w, h, BufferedImage.TYPE_INT_RGB);
        Graphics2D sg = si.createGraphics();
        sg.setColor(java.awt.Color.BLACK);
        sg.fillRect(0, 0, w, h);
        java.lang.reflect.Method sm = SplashView.class.getDeclaredMethod("onDraw", Canvas.class);
        sm.setAccessible(true);
        sm.invoke(sv, new Canvas(sg));
        sg.dispose();
        javax.imageio.ImageIO.write(si, "png", new File(out + "/android-splash.png"));

        // tablet / landscape width, to make sure the wide layout holds up
        if (wDp < 700) {
            String[] args2 = {out, "800", "1.6"};
            Main2(args2);
        }

        System.out.println("wrote " + out + "/android-*.png at " + w + "x" + h);
        System.exit(0);
    }

    private static SharedPreferences prefsOf(Object v, Context ctx) throws Exception {
        Field f = MainView.class.getDeclaredField("prefs");
        f.setAccessible(true);
        Object p = f.get(v);
        return (SharedPreferences) p;
    }

    private static void set(MainView v, Feed.Result r) throws Exception {
        Field f = MainView.class.getDeclaredField("prefs");
        f.setAccessible(true);
        f.set(v, new SharedPreferences());
    }

    private static void setStatic(String name, Object val) throws Exception {
        Field f = Feed.class.getDeclaredField(name);
        f.setAccessible(true);
        f.set(null, val);
    }

    private static Feed.Result liveOrSample() throws Exception {
        try {
            java.lang.reflect.Method dl = Feed.class.getDeclaredMethod("download");
            dl.setAccessible(true);
            Feed.Result live = (Feed.Result) dl.invoke(null);
            if (live != null && live.ok && live.byKey.size() > 3) {
                System.out.println("render: using live feed (" + live.updatedCount + "/"
                        + live.liveCount + " updated, roblox " + live.robloxVersion + ")");
                return live;
            }
        } catch (Exception ignored) {
        }
        System.out.println("render: using sample feed");
        return sampleFeed();
    }

    private static Feed.Result sampleFeed() throws Exception {
        Feed.Result r = new Feed.Result();
        r.ok = true;
        String[][] rows = {
                {"volt", "1.3.21.3", "1", "1"},
                {"wave", "NEW-1.6.1", "0", "1"},
                {"potassium", "2.6.1", "0", "1"},
                {"isaeva", "2.6.9", "1", "1"},
                {"matcha", "v2", "1", "0"},
                {"serotonin", "v2", "1", "0"},
                {"lucid", "1.0.3", "1", "1"},
                {"axis", "0.5.5-1a3bc73d0c6ed9a9", "1", "0"},
        };
        for (String[] row : rows) {
            Feed.Item it = new Feed.Item();
            it.live = true;
            it.hasVersion = true;
            it.version = row[1];
            it.updated = "1".equals(row[2]);
            it.detected = "1".equals(row[3]);
            it.hasUpdatedAt = true;
            it.updatedAt = System.currentTimeMillis() - 3L * 60000;
            it.updatedText = Feed.relative(it.updatedAt);
            it.rbx = "version-4ea8e85481e34f46";
            r.byKey.put(row[0], it);
        }
        r.liveCount = 8;
        r.updatedCount = 5;
        r.detectedCount = 5;
        r.robloxVersion = "0.747.0.7471481";
        r.robloxUpload = "version-4ea8e85481e34f46";
        r.byKey.get("wave").rbx = "version-stale0000000000";
        Feed.markLatest(r);
        return r;
    }

    private static void callDraw(MainView v, int w, int h) throws Exception {
        BufferedImage img = new BufferedImage(w, h, BufferedImage.TYPE_INT_RGB);
        Graphics2D g = img.createGraphics();
        g.setColor(java.awt.Color.BLACK);
        g.fillRect(0, 0, w, h);
        Method m = MainView.class.getDeclaredMethod("onDraw", Canvas.class);
        m.setAccessible(true);
        m.invoke(v, new Canvas(g));
        g.dispose();
    }

    private static void paint(MainView v, int w, int h, String path) throws Exception {
        BufferedImage img = new BufferedImage(w, h, BufferedImage.TYPE_INT_RGB);
        Graphics2D g = img.createGraphics();
        g.setColor(java.awt.Color.BLACK);
        g.fillRect(0, 0, w, h);
        Canvas c = new Canvas(g);
        Method m = MainView.class.getDeclaredMethod("onDraw", Canvas.class);
        m.setAccessible(true);
        m.invoke(v, c);
        g.dispose();
        javax.imageio.ImageIO.write(img, "png", new File(path));
    }

    private static void stepSheet(MainView v) throws Exception {
        Method m = MainView.class.getDeclaredMethod("stepSheet");
        m.setAccessible(true);
        m.invoke(v);
    }

    /** Rows are inert: a tap on a product row may not open anything or toggle a chip. */
    private static void checkIgnored(MainView v, int w) throws Exception {
        boolean before = (Boolean) get(v, "sheet");
        tap(v, w * 0.45f, Math.round(560 * v.getHeight() / 2516f));
        boolean after = (Boolean) get(v, "sheet");
        System.out.println("row tap opens sheet? " + after + " (was " + before + ")");
        if (after != before) throw new AssertionError("a product row opened the settings sheet");
        // a tap on the detection chip of the second row must toggle muted state
        Object chips = get(v, "chipRects");
        Object idx = get(v, "chipIndex");
        java.util.List<?> rects = (java.util.List<?>) chips;
        java.util.List<?> index = (java.util.List<?>) idx;
        int at = -1;
        for (int i = 0; i < index.size(); i++) if (((Integer) index.get(i)) == 1) at = i;
        android.graphics.RectF cr = (android.graphics.RectF) rects.get(at);
        Object notify = get(v, "notify");
        int beforeChip = ((int[]) notify)[1];
        tap(v, cr.centerX(), cr.centerY());
        int afterChip = ((int[]) notify)[1];
        System.out.println("chip tap toggled notify: " + (beforeChip != afterChip));
        if (beforeChip == afterChip) throw new AssertionError("the detection chip did not toggle");
        tap(v, cr.centerX(), cr.centerY());
        System.out.println("chip restored: " + (((int[]) get(v, "notify"))[1] == beforeChip));
    }

    private static void tap(MainView v, float x, float y) {
        android.view.MotionEvent e = new android.view.MotionEvent();
        e.action = android.view.MotionEvent.ACTION_DOWN;
        e.x = x;
        e.y = y;
        v.onTouchEvent(e);
        e.action = android.view.MotionEvent.ACTION_UP;
        v.onTouchEvent(e);
    }

    private static Object get(Object o, String field) throws Exception {
        Field f = o.getClass().getDeclaredField(field);
        f.setAccessible(true);
        return f.get(o);
    }

    private static void Main2(String[] a) throws Exception {
        String out = a[0];
        int wDp = Integer.parseInt(a[1]);
        float scale = Float.parseFloat(a[2]);
        Context ctx = new Context();
        ctx.res.metrics.density = scale;
        ctx.res.metrics.widthPixels = Math.round(wDp * scale);
        int markId = 1001, smallId = 1002;
        BitmapFactory.REGISTRY.put(markId, "res/drawable-nodpi/mark.png");
        BitmapFactory.REGISTRY.put(smallId, "res/drawable-nodpi/mark_small.png");
        ctx.res.register("mark", "drawable", markId);
        ctx.res.register("mark_small", "drawable", smallId);
        int id = 2000;
        for (String k : Feed.KEYS) {
            BitmapFactory.REGISTRY.put(id, "res/drawable-nodpi/logo_" + k + ".jpg");
            ctx.res.register("logo_" + k, "drawable", id++);
        }
        ctx.res.config.orientation = android.content.res.Configuration.ORIENTATION_LANDSCAPE;
        ctx.res.config.screenWidthDp = wDp;
        int w = Math.round(wDp * scale), h = Math.round(520 * scale);
        MainView v = new MainView(ctx);
        v.bind(new Host(), ctx.getSharedPreferences("rcm", 0));
        v.layout(w, h);
        v.setResult(sampleFeed(), "");
        paint(v, w, h, out + "/android-tablet.png");
    }

    static final class Host implements MainView.Host {
        public void onCheckNow() { System.out.println("host: check now"); }
        public void onPrefsChanged() { System.out.println("host: prefs changed"); }
        public void onToast(String s) { System.out.println("host: toast " + s); }
    }
}
