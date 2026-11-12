package com.rocheats.monitor;

import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.os.SystemClock;
import android.view.MotionEvent;
import android.view.View;
import android.view.animation.DecelerateInterpolator;

import java.util.ArrayList;
import java.util.List;

/**
 * The whole checker, drawn the way the desktop build draws it: the same cards,
 * the same tracked eyebrow caps, the same cyan accent, the same eight scenes.
 */
final class MainView extends View {

    interface Host {
        void onCheckNow();

        void onPrefsChanged();

        void onToast(String s);
    }

    static final int TONE_UPDATED = 0, TONE_MISSING = 1, TONE_UNKNOWN = 2;

    private Host host;
    private SharedPreferences prefs;

    private Feed.Result result;
    private boolean checking;
    private float spinner;
    private long checkedAt;
    private long nextAt;
    private String lastError = "";
    private int filter; // 0 all, 1 updated, 2 not updated

    private final Bitmap mark;
    private final Bitmap[] logos = new Bitmap[Feed.KEYS.length];

    private float scroll, scrollTarget, maxScroll;
    private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint tp = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.SUBPIXEL_TEXT_FLAG);

    // settings sheet
    private boolean sheet, sheetAnim;
    private float sheetT;
    private long sheetStarted;
    private float sheetFrom;
    private boolean sheetClosing;

    // hit rects, rebuilt every frame
    private final List<RectF> chipRects = new ArrayList<>();
    private final List<Integer> chipIndex = new ArrayList<>();
    private final List<float[]> swatchRects = new ArrayList<>();
    private final List<float[]> accentRects = new ArrayList<>();
    private RectF gearRect, checkRect, closeRect, pictureRect, sheetCheckRect, themeGlass, themeFlat, notifRect, soundRect;
    private RectF[] intervalRects = new RectF[6];

    private final int[] notify = new int[Feed.KEYS.length];

    private final List<float[]> rowRects = new ArrayList<>();

    MainView(Context c) {
        super(c);
        int mid = c.getResources().getIdentifier("mark_small", "drawable", c.getPackageName());
        mark = BitmapFactory.decodeResource(c.getResources(), mid);
        for (int i = 0; i < Feed.KEYS.length; i++) {
            int id = c.getResources().getIdentifier("logo_" + Feed.KEYS[i], "drawable",
                    c.getPackageName());
            if (id != 0) logos[i] = BitmapFactory.decodeResource(c.getResources(), id);
        }
    }

    void bind(Host h, SharedPreferences sp) {
        host = h;
        prefs = sp;
        for (int i = 0; i < notify.length; i++) {
            notify[i] = sp.getInt("notify_" + Feed.KEYS[i], 1);
        }
    }

    float dp(float v) {
        return Pal.dp(getContext(), v);
    }

    // ------------------------------------------------------------------ data

    void setResult(Feed.Result r, String error) {
        result = r;
        checking = false;
        lastError = error == null ? "" : error;
        if (r != null && r.ok) checkedAt = System.currentTimeMillis();
        scheduleNext();
        invalidate();
    }

    void setChecking(boolean v) {
        checking = v;
        if (v) lastError = "";
        invalidate();
    }

    void scheduleNext() {
        int min = prefs.getInt("interval", 5);
        nextAt = System.currentTimeMillis() + min * 60000L;
    }

    long nextIn() {
        return Math.max(0, nextAt - System.currentTimeMillis());
    }

    boolean dueNow() {
        return nextAt != 0 && System.currentTimeMillis() >= nextAt;
    }

    // ---------------------------------------------------------------- layout

    private float contentHeight;

    @Override
    protected void onMeasure(int wSpec, int hSpec) {
        // MeasureSpec is a nested class of View, so it resolves unqualified here.
        setMeasuredDimension(MeasureSpec.getSize(wSpec), MeasureSpec.getSize(hSpec));
    }


    private float headerH() {
        return dp(58);
    }

    private float heroHeight(float w) {
        return w >= dp(520) ? dp(140) : dp(132);
    }

    private float rowHeight(float w) {
        return w >= dp(520) ? dp(56) : dp(64);
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        switch (e.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                pointerX = e.getX();
                pointerY = e.getY();
                downX = e.getX();
                downY = e.getY();
                scrollStart = scroll;
                dragging = false;
                pressedAt = SystemClock.uptimeMillis();
                if (!sheet) {
                    // A chip or the gear is an immediate press; rows stay inert.
                    pressedChip = -1;
                    for (int i = 0; i < chipRects.size(); i++) {
                        if (chipRects.get(i).contains(e.getX(), e.getY() + scroll)) pressedChip = i;
                    }
                    pressedGear = gearRect != null && gearRect.contains(e.getX(), e.getY());
                    pressedCheck = checkRect != null && checkRect.contains(e.getX(), e.getY() + scroll);
                    pressedFilter = filterAt(e.getX(), e.getY() + scroll);
                }
                return true;
            case MotionEvent.ACTION_MOVE:
                pointerX = e.getX();
                pointerY = e.getY();
                if (Math.abs(e.getY() - downY) > dp(6)) dragging = true;
                if (sheet) {
                    if (e.getY() > sheetTop + dp(70) && Math.abs(e.getY() - downY) > dp(4)) {
                        dragging = true;
                    }
                    return true;
                }
                scroll = clampScroll(scrollStart - (e.getY() - downY));
                invalidate();
                return true;
            case MotionEvent.ACTION_UP:
                pointerX = dp(-999);
                pointerY = dp(-999);
                if (sheet) {
                    if (!dragging) tapInsideSheet(e.getX(), e.getY());
                    else if (e.getY() - downY > dp(90)) closeSheet();
                    return true;
                }
                if (!dragging) {
                    if (pressedFilter >= 0) {
                        filter = pressedFilter;
                        scroll = 0;
                        invalidate();
                    }
                    else if (pressedGear) openSheet();
                    else if (pressedCheck) host.onCheckNow();
                    else if (pressedChip >= 0) toggleChip(pressedChip);
                    // Anything else — including a product row — does nothing.
                }
                pressedChip = -1;
                pressedFilter = -1;
                pressedGear = pressedCheck = false;
                return true;
            case MotionEvent.ACTION_CANCEL:
                pressedChip = -1;
                pressedGear = pressedCheck = false;
                return true;
        }
        return super.onTouchEvent(e);
    }

    private String tipText = "";
    private float[] tipRect = new float[4];
    private float tipAlpha;

    private float downX, downY, scrollStart, sheetTop;
    private float sheetBottomRail;

    private float dpRounded(float v) { return Math.round(v); }
    private boolean dragging;
    private long pressedAt;
    private int pressedChip = -1;
    private float pointerX = -9999, pointerY = -9999;
    private int pressedFilter = -1;
    private boolean pressedGear, pressedCheck;

    /** Which filter chip, if any, sits under the given content-space point. */
    private int filterAt(float x, float y) {
        final float w = getWidth();
        float pad = w >= dp(520) ? dp(20) : dp(14);
        float top = headerH() + dp(6) + heroHeight(w) + dp(14) + dp(6);
        if (y < top || y > top + dp(30)) return -1;
        float cx = pad;
        Paint t = Pal.text(dp(12), Pal.TEXT2, Pal.UI_MED);
        String[] labels = {"All", "Updated", "Not updated"};
        for (int i = 0; i < 3; i++) {
            float tw = t.measureText(labels[i]) + dp(28);
            if (x >= cx && x <= cx + tw) return i;
            cx += tw + dp(8);
        }
        return -1;
    }

    @Override
    public boolean canScrollVertically(int dir) {
        return dir > 0 ? scroll < maxScroll - dp(1) : scroll > dp(1);
    }

    private float clampScroll(float v) {
        return Math.max(0, Math.min(maxScroll, v));
    }

    private List<Feed.Item> cachedItems;
    private Feed.Result cachedFor;

    private List<Feed.Item> items() {
        if (cachedItems == null || cachedFor != result) {
            cachedFor = result;
            cachedItems = Feed.list(result);
        }
        return cachedItems;
    }

    private void toggleChip(int i) {
        int idx = chipIndex.get(i);
        notify[idx] = notify[idx] == 0 ? 1 : 0;
        prefs.edit().putInt("notify_" + Feed.KEYS[idx], notify[idx]).apply();
        host.onPrefsChanged();
        invalidate();
    }

    // ------------------------------------------------------------ animations

    void openSheet() {
        sheet = true;
        sheetClosing = false;
        sheetT = 0;
        sheetStarted = SystemClock.uptimeMillis();
        invalidate();
    }

    void closeSheet() {
        sheetClosing = true;
        sheetStarted = SystemClock.uptimeMillis();
        sheetFrom = sheetT;
        invalidate();
    }

    private void stepSheet() {
        float k = Pal.clamp01((SystemClock.uptimeMillis() - sheetStarted) / 260f);
        float e = Pal.ease(k);
        if (sheetClosing) sheetT = sheetFrom * (1 - e);
        else sheetT = e;
        if (k >= 1f) {
            sheetT = sheetClosing ? 0 : 1;
            if (sheetClosing) sheet = false;
        }
        if (k < 1f) postInvalidateOnAnimation();
    }

    // ------------------------------------------------------------------ draw

    @Override
    protected void onDraw(Canvas c) {
        final float w = getWidth(), h = getHeight();
        final boolean wide = w >= dp(520);
        final float pad = wide ? dp(20) : dp(14);

        Pal.scene(c, prefs == null ? "aurora" : prefs.getString("scene", "aurora"), (int) w, (int) h);

        // header
        float headerH = headerH();
        if (scroll > 0) {
            p.setColor(0xE60A0C0D);
            c.drawRect(0, 0, w, headerH, p);
            Pal.hairline(c, 0, headerH, w, headerH, 0xFF1B1F20);
        }
        drawHeader(c, w, pad, wide);

        float y = headerH + dp(6) - scroll;

        // ---------------- hero
        float heroH = heroHeight(w);
        rowRects.clear();
        RectF hero = new RectF(pad, y, w - pad, y + heroH);
        Pal.glass(c, hero, dp(16), 0xFF11181A, 1f);
        float hx = hero.left + dp(16);
        float hy = hero.top + dp(16);

        Paint eyebrow = Pal.caps(getContext(), 9, Pal.accent, true);
        c.drawText("WEAO STATUS", hx, hy + dp(9), eyebrow);

        String big = bigLine();
        tp.setTypeface(Pal.UI_BOLD);
        tp.setTextSize(wide ? dp(28) : dp(26));
        tp.setLetterSpacing(-0.01f);
        tp.setColor(Pal.TEXT);
        c.drawText(big, hx, hy + (wide ? dp(46) : dp(42)), tp);

        // Check now, top right
        float bw = dp(wide ? 116 : 108), bh = dp(34);
        checkRect = new RectF(hero.right - dp(16) - bw, hero.top + dp(14), hero.right - dp(16),
                hero.top + dp(14) + bh);
        drawButton(c, checkRect, "Check now", checking, false);

        float sy = hy + (wide ? dp(70) : dp(64));
        drawStats(c, hx, sy);

        // Roblox build strip: the thing that moves first when a client ships.
        drawRobloxStrip(c, hero, wide, sy + dp(38));

        tp.setTextSize(dp(11.5f));
        tp.setTypeface(Pal.UI);
        tp.setColor(Pal.TEXT3);
        tp.setLetterSpacing(0f);
        String checked = checkedAt == 0 ? (checking ? "Checking now\u2026" : "Not checked yet")
                : "Checked " + Feed.clock(checkedAt) + "   \u00B7   Next check in " + countdown();
        if (!lastError.isEmpty()) {
            tp.setColor(Pal.AMBER);
            checked = lastError;
        }
        c.drawText(checked, hx, sy + dp(22), tp);

        // ---------------- toolbar
        y += heroH + dp(14);
        float toolbarH = dp(48);
        drawToolbar(c, pad, w, y, wide);
        y += toolbarH;

        // ---------------- rows
        float rh = rowHeight(w);
        chipRects.clear();
        chipIndex.clear();
        tipText = "";
        for (int i = 0; i < Feed.KEYS.length; i++) {
            if (!passesFilter(i)) continue;
            RectF r = new RectF(pad, y, w - pad, y + rh);
            drawRow(c, r, i, wide);
            y += rh + dp(8);
        }
        contentHeight = y + dp(92);  // clears the nav bar when scrolled to the end
        maxScroll = Math.max(0, contentHeight - h);
        if (scroll > maxScroll) scroll = maxScroll;

        // fade the top edge under the header while scrolling
        if (scroll > 1) {
            Paint g = new Paint(Paint.ANTI_ALIAS_FLAG);
            g.setShader(new android.graphics.LinearGradient(0, headerH, 0, headerH + dp(14),
                    0xFF0A0C0D, 0x000A0C0D, android.graphics.Shader.TileMode.CLAMP));
            c.drawRect(0, headerH, w, headerH + dp(14), g);
        }

        if (checking) spinner += 0.09f;

        if (!tipText.isEmpty() && !sheet) drawTip(c, w);

        if (sheet || sheetT > 0.001f) drawSheet(c, w, h, wide);
        if (sheet && sheetT < 1f) stepSheet();
        else if (sheetClosing) stepSheet();

        if (checking || sheetT > 0.001f) postInvalidateOnAnimation();
    }

    /** Fades a freshly detected Roblox build in and out of the strip dot. */
    private float newBuildPulse;

    void pulseNewBuild() {
        newBuildPulse = 1f;
        invalidate();
    }

    private String bigLine() {
        int updated = 0, live = 0;
        List<Feed.Item> items = items();
        for (Feed.Item it : items) {
            if (it.live) live++;
            if (it.live && it.updated) updated++;
        }
        int total = 0;
        for (Feed.Item it : items) if (it.live) total++;
        if (result == null) return "Checking\u2026";
        return updated + " of " + total + " updated";
    }

    private String countdown() {
        long ms = nextIn();
        if (ms <= 0) return "now";
        long s = ms / 1000;
        return (s / 60) + ":" + (s % 60 < 10 ? "0" : "") + (s % 60);
    }

    /** The same pill the Windows build draws above a detection badge. */
    private void drawTip(Canvas c, float w) {
        Paint tp2 = Pal.text(dp(10.5f), Pal.TEXT, Pal.UI_BOLD);
        float tw = tp2.measureText(tipText);
        float pw = tw + dp(22), ph = dp(26);
        float cx = tipRect[0], cy = tipRect[1] - tipRect[2] - dp(9) - ph / 2;
        float left = Math.max(dp(6), Math.min(cx - pw / 2, w - pw - dp(6)));
        RectF box = new RectF(left, cy - ph / 2, left + pw, cy + ph / 2);
        p.setColor(0x99000000);
        c.drawRoundRect(box, dp(7), dp(7), p);
        Pal.roundRect(c, box.left, box.top, box.right, box.bottom, dp(7), Pal.fill(0xFF141617));
        Paint edge = Pal.fill(0x24FFFFFF);
        edge.setStyle(Paint.Style.STROKE);
        edge.setStrokeWidth(1f);
        Pal.roundRect(c, box.left, box.top, box.right, box.bottom, dp(7), edge);
        c.drawText(tipText, box.centerX() - tw / 2, box.centerY() + dp(3.8f), tp2);
    }

    private void drawHeader(Canvas c, float w, float pad, boolean wide) {
        float headerH = headerH();
        float mx = pad + dp(2), my = headerH / 2;
        if (mark != null) {
            float m = dp(26);
            c.drawBitmap(mark, null, new RectF(mx, my - m / 2, mx + m, my + m / 2), p);
        }
        tp.setTypeface(Pal.UI_BOLD);
        tp.setTextSize(dp(15.5f));
        tp.setLetterSpacing(0f);
        tp.setColor(Pal.TEXT);
        float tx = mx + dp(34);
        c.drawText("RoCheats Monitor", tx, my + dp(5.5f), tp);
        float tw = tp.measureText("RoCheats Monitor");

        // LIVE pill
        float px = tx + tw + dp(10);
        float pw = dp(52), ph = dp(20);
        RectF pill = new RectF(px, my - ph / 2, px + pw, my + ph / 2);
        Pal.roundRect(c, pill.left, pill.top, pill.right, pill.bottom, ph / 2,
                Pal.fill(Pal.withAlpha(Pal.accent, 0.10f)));
        Paint edge = Pal.fill(Pal.withAlpha(Pal.accent, 0.30f));
        edge.setStyle(Paint.Style.STROKE);
        edge.setStrokeWidth(1f);
        Pal.roundRect(c, pill.left, pill.top, pill.right, pill.bottom, ph / 2, edge);
        Pal.dot(c, pill.left + dp(11), pill.centerY(), dp(2.6f), Pal.accent);
        Paint lp = Pal.text(dp(9), Pal.accent, Pal.UI_BOLD);
        lp.setLetterSpacing(0.12f);
        c.drawText("LIVE", pill.left + dp(18), pill.centerY() + dp(3.2f), lp);

        // gear
        float gs = dp(34);
        gearRect = new RectF(w - pad - gs, my - gs / 2, w - pad, my + gs / 2);
        drawGear(c, gearRect.centerX(), gearRect.centerY(), dp(9));
        Pal.hairline(c, 0, headerH, w, headerH, 0xFF191D1E);
    }

    private void drawGear(Canvas c, float cx, float cy, float r) {
        Paint g = Pal.fill(Pal.TEXT2);
        g.setStyle(Paint.Style.STROKE);
        g.setStrokeWidth(Math.max(1.4f, dp(1.4f)));
        Path path = new Path();
        path.addCircle(cx, cy, r * 0.52f, Path.Direction.CW);
        c.drawPath(path, g);
        for (int i = 0; i < 8; i++) {
            double a = Math.PI * 2 * i / 8;
            float x1 = (float) (cx + Math.cos(a) * r * 0.62f);
            float y1 = (float) (cy + Math.sin(a) * r * 0.62f);
            float x2 = (float) (cx + Math.cos(a) * r * 1.0f);
            float y2 = (float) (cy + Math.sin(a) * r * 1.0f);
            c.drawLine(x1, y1, x2, y2, g);
        }
    }

    private void drawButton(Canvas c, RectF r, String label, boolean busy, boolean primary) {
        if (primary) {
            Pal.roundRect(c, r.left, r.top, r.right, r.bottom, r.height() / 2, Pal.fill(Pal.accent));
        } else {
            Pal.roundRect(c, r.left, r.top, r.right, r.bottom, r.height() / 2, Pal.fill(0xFF1E2325));
            Paint edge = Pal.fill(0x33E4E5DB);
            edge.setStyle(Paint.Style.STROKE);
            edge.setStrokeWidth(1f);
            Pal.roundRect(c, r.left, r.top, r.right, r.bottom, r.height() / 2, edge);
        }
        Paint t = Pal.text(dp(12), primary ? 0xFF212723 : Pal.TEXT, Pal.UI_MED);
        float iconW = dp(18);
        float tw = t.measureText(label);
        float x = r.centerX() - (tw + iconW) / 2 + iconW;
        // spinner or refresh glyph
        float gx = r.centerX() - (tw + iconW) / 2 + dp(6);
        Paint gp = Pal.fill(primary ? 0xFF101416 : Pal.TEXT2);
        gp.setStyle(Paint.Style.STROKE);
        gp.setStrokeWidth(Math.max(1.5f, dp(1.6f)));
        gp.setStrokeCap(Paint.Cap.ROUND);
        float gr = dp(5.4f);
        RectF arc = new RectF(gx - gr, r.centerY() - gr, gx + gr, r.centerY() + gr);
        c.drawArc(arc, busy ? spinner * 57 : -70, 300, false, gp);
        c.drawText(label, x, r.centerY() + dp(4), t);
    }

    /** "ROBLOX 0.747.0 · 6 of 8 executors current", with a dot that pulses on a new build. */
    private void drawRobloxStrip(Canvas c, RectF hero, boolean wide, float y) {
        if (result == null || result.robloxVersion.isEmpty()) return;
        float x = hero.left + dp(16);
        float w = hero.right - dp(16) - x;

        Pal.roundRect(c, x, y - dp(11), x + w, y + dp(13), dp(12), Pal.fill(0x0EFFFFFF));
        float dotX = x + dp(12), dotY = y + dp(1);
        int liveNow = 0;
        for (int i = 0; i < Feed.KEYS.length; i++) if (items().get(i).live) liveNow++;
        int dotCol = result.onLatestCount >= liveNow ? Pal.GREEN : Pal.AMBER;
        if (newBuildPulse > 0) {
            Paint glow = Pal.fill(Pal.withAlpha(Pal.accent, 0.25f * newBuildPulse));
            c.drawCircle(dotX, dotY, dp(7) * (1.2f - 0.2f * newBuildPulse), glow);
            dotCol = Pal.accent;
        }
        Pal.dot(c, dotX, dotY, dp(2.8f), dotCol);

        Paint l = Pal.text(dp(9.5f), Pal.TEXT3, Pal.UI_MED);
        l.setLetterSpacing(0.14f);
        c.drawText("ROBLOX", dotX + dp(10), dotY + dp(3.4f), l);

        Paint v = Pal.text(dp(11.5f), Pal.TEXT, Pal.UI_MED);
        String ver = result.robloxVersion;
        c.drawText(ver, dotX + dp(10) + l.measureText("ROBLOX") + dp(8), dotY + dp(4), v);

        String tail = result.onLatestCount + " of " + liveNow + (wide ? " executors on this build" : " current");
        Paint t = Pal.text(dp(10.5f), result.onLatestCount >= liveNow ? Pal.TEXT3 : Pal.AMBER, Pal.UI);
        float tw = t.measureText(tail);
        float tx = x + w - dp(12) - tw;
        if (tx > dotX + dp(120)) c.drawText(tail, tx, dotY + dp(3.8f), t);
    }

    private void drawStats(Canvas c, float x, float y) {
        List<Feed.Item> items = items();
        int up = 0, down = 0, det = 0;
        for (Feed.Item it : items) {
            if (it.live && it.updated) up++;
            else if (it.live) down++;
            if (it.live && it.detected) det++;
        }
        Paint n = Pal.text(dp(13.5f), Pal.TEXT, Pal.UI_MED);
        Paint l = Pal.text(dp(10), Pal.TEXT3, Pal.UI);
        drawStat(c, x, y, String.valueOf(up), "updated", n, l);
        drawStat(c, x + dp(86), y, String.valueOf(down), "stale", n, l);
        drawStat(c, x + dp(168), y, String.valueOf(det), "detected", n, l);
        int lag = 0;
        for (Feed.Item it : items) if (it.live && !it.rbx.isEmpty() && !it.onLatest) lag++;
        if (result != null && !result.robloxVersion.isEmpty()) {
            drawStat(c, x + dp(262), y, String.valueOf(lag), "behind Roblox", n, l);
        }
    }

    private void drawStat(Canvas c, float x, float y, String value, String label, Paint n, Paint l) {
        c.drawText(value, x, y, n);
        float vw = n.measureText(value);
        c.drawText(label, x + vw + dp(5), y, l);
    }

    private void drawToolbar(Canvas c, float pad, float w, float y, boolean wide) {
        float ch = dp(30), cx = pad;
        String[] labels = {"All", "Updated", "Not updated"};
        Paint t = Pal.text(dp(12), Pal.TEXT2, Pal.UI_MED);
        for (int i = 0; i < 3; i++) {
            float tw = t.measureText(labels[i]) + dp(28);
            RectF r = new RectF(cx, y + dp(6), cx + tw, y + dp(6) + ch);
            boolean on = filter == i;
            Pal.roundRect(c, r.left, r.top, r.right, r.bottom, ch / 2,
                    Pal.fill(on ? 0xFF202A2C : 0xFF14181A));
            Paint edge = Pal.fill(on ? 0x59E4E5DB : 0x24E4E5DB);
            edge.setStyle(Paint.Style.STROKE);
            edge.setStrokeWidth(1f);
            Pal.roundRect(c, r.left, r.top, r.right, r.bottom, ch / 2, edge);
            t.setColor(on ? Pal.TEXT : Pal.TEXT2);
            c.drawText(labels[i], r.left + dp(14), r.centerY() + dp(4), t);
            if (!wide) {
            } else {
            }
            cx += tw + dp(8);
        }
        int shown = 0, total = Feed.KEYS.length;
        for (int i = 0; i < total; i++) if (passesFilter(i)) shown++;
        Paint c2 = Pal.text(dp(11), Pal.TEXT3, Pal.UI);
        String s = shown + " of " + total + " products";
        c.drawText(s, w - pad - c2.measureText(s), y + dp(6) + ch / 2 + dp(4), c2);
    }

    private boolean passesFilter(int i) {
        if (filter == 0) return true;
        Feed.Item it = items().get(i);
        if (filter == 1) return it.live && it.updated;
        return it.live && !it.updated;
    }

    private void drawRow(Canvas c, RectF r, int i, boolean wide) {
        boolean on = notify[i] != 0;
        Pal.glass(c, r, dp(12), 0xFF15181A, 1f);

        float tile = wide ? dp(40) : dp(38);
        float ty = r.centerY();
        if (wide) ty = r.centerY();
        else ty = r.top + dp(26);
        RectF tileRect = new RectF(r.left + dp(12), ty - tile / 2, r.left + dp(12) + tile,
                ty + tile / 2);
        Bitmap b = logos[i];
        if (b != null) {
            Path clip = new Path();
            clip.addRoundRect(tileRect, dp(10), dp(10), Path.Direction.CW);
            c.save();
            c.clipPath(clip);
            c.drawBitmap(b, null, tileRect, p);
            c.restore();
        } else {
            Pal.roundRect(c, tileRect.left, tileRect.top, tileRect.right, tileRect.bottom,
                    dp(10), Pal.fill(Pal.hsl(Feed.HUES[i], .5f, .5f)));
        }

        float nx = tileRect.right + dp(12);
        tp.setTypeface(Pal.UI_MED);
        tp.setTextSize(dp(13));
        tp.setLetterSpacing(0.045f);
        tp.setColor(Pal.TEXT);
        float nameY = wide ? r.centerY() + dp(4) : r.top + dp(22);
        c.drawText(Feed.NAMES[i], nx, nameY, tp);

        Feed.Item it = items().get(i);

        // the version line doubles as the Roblox-currency check
        if (!it.rbx.isEmpty() && !isOnLatest(it)) {
            // behind the live Roblox build: mark the version in amber
        }

        // status dot under the name
        int dotCol = !it.live ? Pal.TEXT4 : (it.detected ? Pal.accent : Pal.GREEN);
        if (!it.live) dotCol = Pal.TEXT4;
        float dotY = wide ? r.centerY() + dp(17) : nameY + dp(12);
        float dotX = nx + dp(2);
        if (on) {
            Paint glow = Pal.fill(Pal.withAlpha(dotCol, 0.22f));
            c.drawCircle(dotX, dotY, dp(5), glow);
        }
        Pal.dot(c, dotX, dotY, dp(2.6f), on ? dotCol : Pal.withAlpha(dotCol, 0.4f));

        // Detection: a small exclamation mark on the tile's corner. It is also the
        // tap target that mutes this product, and hovering it explains itself.
        float badR = dp(9);
        float bx = tileRect.left + dp(3), by = tileRect.top + dp(3);
        boolean flagged = it.live && it.detected;
        boolean hotBadge = flagged && pressedChip >= 0
                && chipRects.size() > pressedChip
                && chipRects.get(pressedChip).contains(bx, by);
        boolean hovered = flagged && pointerX >= bx - dp(18) && pointerX <= bx + dp(18)
                && pointerY + scroll >= by - dp(18) && pointerY + scroll <= by + dp(18);
        if (hovered) {
            tipText = Feed.NAMES[i] + " \u00B7 detected";
            tipRect = new float[]{bx, by, dp(9), 0f};
        }
        if (flagged) {
            float k = hovered ? dp(10) : badR;
            Pal.dot(c, bx, by, k + dp(1.5f), Pal.ROW);
            Pal.dot(c, bx, by, k, Pal.AMBER);

            Paint mark = Pal.fill(0xFF241A05);
            Pal.roundRect(c, bx - dp(0.9f), by - dp(4.4f), bx + dp(0.9f), by + dp(1.2f),
                    dp(0.9f), mark);
            Pal.dot(c, bx, by + dp(3.6f), dp(1.1f), 0xFF241A05);
        }

        // the badge keeps the old chip's behaviour: tap to mute this product
        RectF chip = new RectF(bx - dp(14), by - dp(14), bx + dp(14), by + dp(14));
        chipRects.add(chip);
        chipIndex.add(i);
        if (flagged && pressedChip == chipRects.size() - 1) {
            tipText = Feed.NAMES[i] + " \u00B7 detected";
            tipRect = new float[]{bx, by, dp(9), 1f};
        }

        // version + state
        tp.setTypeface(wide ? Pal.MONO : Pal.UI);
        float stx = r.right - dp(12);
        String stateText = !it.live ? (wide ? "No live source" : "")
                : (it.updated ? "Updated" : "Not updated");
        int stateCol = !it.live ? Pal.TEXT3 : (it.updated ? Pal.GREEN : Pal.RED);
        tp.setTextSize(dp(12));
        tp.setLetterSpacing(wide ? 0f : 0f);
        tp.setColor(stateCol);
        tp.setTypeface(wide ? Pal.UI_MED : Pal.UI_MED);
        if (wide) {
            c.drawText(stateText, stx - tp.measureText(stateText), r.centerY() + dp(4), tp);
            // version column, right aligned just left of the state
            if (it.hasVersion) {
                tp.setColor(Pal.TEXT2);
                tp.setTypeface(Pal.MONO);
                tp.setTextSize(dp(11.5f));
                float vx = r.left + r.width() * 0.46f;
                c.drawText(clip(it.version, 13), vx, r.centerY() + dp(4), tp);
            }
            // second line under the name: version · how the build compares · flags
            Paint sub = Pal.text(dp(10), Pal.TEXT3, Pal.UI);
            String subText = it.hasVersion ? clip(it.version, 18) : "";
            if (!it.rbx.isEmpty()) {
                subText += (subText.isEmpty() ? "" : "  \u00B7  ")
                        + (isOnLatest(it) ? "current build" : "behind Roblox");
                sub.setColor(isOnLatest(it) ? Pal.TEXT3 : Pal.AMBER);
            }
            if (it.banwave) subText += "  \u00B7  BANWAVE";
            else if (it.unknownDetect) subText += "  \u00B7  UNKNOWN";
            if (!subText.isEmpty()) c.drawText(subText, nx, nameY + dp(18), sub);
        } else {
            // second line: version on the left, state right-aligned above the chip row
            float lineY = r.bottom - dp(14);
            if (it.hasVersion) {
                tp.setColor(Pal.TEXT2);
                tp.setTypeface(Pal.UI);
                tp.setTextSize(dp(11.5f));
                String vs = clip(it.version, 14);
                c.drawText(vs, nx, lineY, tp);
                // amber "stale" tick right after the version when the build moved on
                if (!it.rbx.isEmpty() && !isOnLatest(it)) {
                    float vw = tp.measureText(vs);
                    Paint lag = Pal.text(dp(9), Pal.AMBER, Pal.UI_MED);
                    c.drawText("\u25B2", nx + vw + dp(5), lineY - dp(1), lag);
                }
            }
            tp.setTypeface(Pal.UI_MED);
            tp.setTextSize(dp(11.5f));
            tp.setColor(stateCol);
            float sw = tp.measureText(stateText);
            c.drawText(stateText, r.right - dp(12) - sw, r.centerY() + dp(4), tp);
            if (it.hasUpdatedAt && !stateText.isEmpty()) {
                tp.setTypeface(Pal.UI);
                tp.setTextSize(dp(10));
                tp.setColor(Pal.TEXT4);
                c.drawText(it.updatedText, r.right - dp(12) - tp.measureText(it.updatedText),
                        lineY, tp);
            }
        }
    }

    private boolean isOnLatest(Feed.Item it) {
        return it.onLatest || (result != null && it.rbx.equals(result.robloxUpload)
                && !result.robloxUpload.isEmpty());
    }

    private String clip(String s, int n) {
        return s.length() <= n ? s : s.substring(0, n - 1) + "\u2026";
    }

    // --------------------------------------------------------------- settings

    private void drawSheet(Canvas c, float w, float h, boolean wide) {
        float t = sheetT;
        // The sheet floats over the list; its rects are in screen space.
        p.setColor(Pal.withAlpha(0xFF000000, 0.55f * t));
        c.drawRect(0, 0, w, h, p);

        if (sheetBottomRail == 0) sheetBottomRail = dp(600);
        float sheetH = Math.min(h - dp(28), sheetBottomRail);
        float top = h - sheetH * t;
        sheetTop = top;
        RectF panel = new RectF(dp(8), top, w - dp(8), top + sheetH + dp(20));
        Pal.roundRect(c, panel.left, panel.top, panel.right, panel.bottom, dp(24),
                Pal.fill(Pal.PANEL));
        Paint edge = Pal.fill(0x26E4E5DB);
        edge.setStyle(Paint.Style.STROKE);
        edge.setStrokeWidth(1f);
        Pal.roundRect(c, panel.left, panel.top, panel.right, panel.bottom, dp(24), edge);
        // grabber
        Pal.roundRect(c, w / 2 - dp(18), top + dp(10), w / 2 + dp(18), top + dp(14), dp(2),
                Pal.fill(0x33E4E5DB));

        float x = panel.left + dp(20), y = top + dp(40);
        Paint title = Pal.text(dp(15), Pal.TEXT, Pal.UI_BOLD);
        c.drawText("Appearance", x, y, title);
        closeRect = new RectF(panel.right - dp(48), y - dp(22), panel.right - dp(16), y + dp(6));
        Paint cl = Pal.fill(Pal.TEXT3);
        cl.setStyle(Paint.Style.STROKE);
        cl.setStrokeWidth(Math.max(1.4f, dp(1.5f)));
        cl.setStrokeCap(Paint.Cap.ROUND);
        float ax = closeRect.centerX(), ay = closeRect.centerY(), q = dp(6);
        c.drawLine(ax - q, ay - q, ax + q, ay + q, cl);
        c.drawLine(ax + q, ay - q, ax - q, ay + q, cl);

        y += dp(26);
        Paint eyebrow = Pal.caps(getContext(), 9, Pal.TEXT3, true);
        c.drawText("BACKGROUND", x, y, eyebrow);
        y += dp(12);

        // eight swatches: seven scenes, then "your own picture"
        swatchRects.clear();
        String scene = prefs.getString("scene", "aurora");
        float sw = (w - dp(16) * 2 - dp(40) - dp(3) * dp(12)) / 4f;
        float gapx = dp(12);
        String[] scenes = Pal.SCENES;
        for (int i = 0; i < 8; i++) {
            int row = i / 4, col = i % 4;
            float sx = x + col * (sw + gapx);
            float sy = y + row * (sw + gapx + dp(6));
            RectF r = new RectF(sx, sy, sx + sw, sy + sw);
            boolean on = i < scenes.length ? scenes[i].equals(scene) : "picture".equals(scene);
            Pal.roundRect(c, r.left, r.top, r.right, r.bottom, dp(12),
                    Pal.fill(0xFF101314));
            if (i < scenes.length) {
                int preview = sceneColor(scenes[i]);
                Paint gp = new Paint(Paint.ANTI_ALIAS_FLAG);
                gp.setShader(new android.graphics.RadialGradient(r.centerX(), r.top + sw * 0.2f,
                        sw, Pal.withAlpha(preview, 0.85f), 0xFF141718,
                        android.graphics.Shader.TileMode.CLAMP));
                Path clip = new Path();
                clip.addRoundRect(r, dp(12), dp(12), Path.Direction.CW);
                c.save();
                c.clipPath(clip);
                c.drawRect(r, gp);
                c.restore();
            } else {
                Paint ic = Pal.fill(Pal.TEXT2);
                ic.setStyle(Paint.Style.STROKE);
                ic.setStrokeWidth(Math.max(1.4f, dp(1.6f)));
                RectF ir = new RectF(r.centerX() - dp(13), r.centerY() - dp(10),
                        r.centerX() + dp(13), r.centerY() + dp(10));
                c.drawRoundRect(ir, dp(4), dp(4), ic);
                c.drawCircle(ir.left + dp(9), ir.top + dp(7), dp(2.6f), ic);
            }
            if (on) {
                Paint ring = Pal.fill(Pal.accent);
                ring.setStyle(Paint.Style.STROKE);
                ring.setStrokeWidth(Math.max(1.6f, dp(1.8f)));
                Pal.roundRect(c, r.left - dp(1), r.top - dp(1), r.right + dp(1), r.bottom + dp(1),
                        dp(13), ring);
            }
            swatchRects.add(new float[]{r.left, r.top, r.right, r.bottom, i});
        }
        y += 2 * (sw + gapx + dp(6)) + dp(6);

        // accent
        c.drawText("ACCENT", x, y + dp(4), eyebrow);
        y += dp(18);
        accentRects.clear();
        float ar = dp(13), step = dp(40);
        for (int i = 0; i < Pal.ACCENTS.length; i++) {
            float cx = x + ar + i * step;
            boolean on = Pal.ACCENTS[i] == Pal.accent;
            if (on) {
                Paint ring = Pal.fill(Pal.withAlpha(Pal.ACCENTS[i], 0.5f));
                ring.setStyle(Paint.Style.STROKE);
                ring.setStrokeWidth(Math.max(1.5f, dp(1.6f)));
                c.drawCircle(cx, y + ar, ar + dp(4), ring);
            }
            c.drawCircle(cx, y + ar, ar * 0.72f, Pal.fill(Pal.ACCENTS[i]));
            accentRects.add(new float[]{cx, y + ar, ar + dp(8), i});
        }
        y += ar * 2 + dp(18);

        // theme segment
        String theme = prefs.getString("theme", "glass");
        float segW = dp(150), segH = dp(32);
        Paint segBg = Pal.fill(0xFF15191A);
        Pal.roundRect(c, x + dp(2), y, x + dp(2) + segW, y + segH, segH / 2, segBg);
        RectF half = new RectF(x, y, x + dp(2) + segW / 2 + dp(4), y + segH);
        themeGlass = new RectF(x, y, x + segW / 2, y + segH);
        themeFlat = new RectF(x + segW / 2, y, x + segW, y + segH);
        RectF sel = "glass".equals(theme) ? themeGlass : themeFlat;
        Pal.roundRect(c, sel.left + dp(3), sel.top + dp(3), sel.right - dp(3), sel.bottom - dp(3),
                (segH - dp(6)) / 2, Pal.fill(0xFF262C2E));
        Paint st = Pal.text(dp(11.5f), Pal.TEXT2, Pal.UI_MED);
        c.drawText("Glass", themeGlass.centerX() - st.measureText("Glass") / 2,
                themeGlass.centerY() + dp(4), "glass".equals(theme) ? Pal.text(dp(11.5f), Pal.TEXT, Pal.UI_MED) : st);
        c.drawText("Flat", themeFlat.centerX() - st.measureText("Flat") / 2,
                themeFlat.centerY() + dp(4), "flat".equals(theme) ? Pal.text(dp(11.5f), Pal.TEXT, Pal.UI_MED) : st);

        // check interval
        y += segH + dp(18);
        c.drawText("CHECK EVERY", x, y, eyebrow);
        y += dp(14);
        int interval = prefs.getInt("interval", 5);
        int[] opts = {1, 2, 5, 10, 15, 30};
        float iw = (w - dp(16) * 2 - dp(40) - dp(5) * dp(6)) / 6f;
        for (int i = 0; i < 6; i++) {
            RectF r = new RectF(x + i * (iw + dp(6)), y, x + i * (iw + dp(6)) + iw, y + dp(28));
            boolean on = opts[i] == interval;
            Pal.roundRect(c, r.left, r.top, r.right, r.bottom, dp(9),
                    Pal.fill(on ? Pal.withAlpha(Pal.accent, 0.16f) : 0xFF15191A));
            Paint t2 = Pal.text(dp(11), on ? Pal.accent : Pal.TEXT2, Pal.UI_MED);
            String s = opts[i] + "m";
            c.drawText(s, r.centerX() - t2.measureText(s) / 2, r.centerY() + dp(4), t2);
            intervalRects[i] = r;
        }
        y += dp(28) + dp(18);

        // switches
        notifRect = new RectF(x, y, w - dp(16) - dp(20), y + dp(36));
        y = drawSwitch(c, notifRect, "Notifications", "Notify me when a product changes",
                prefs.getInt("notifications", 1) == 1);
        soundRect = new RectF(x, y, notifRect.right, y + dp(36));
        y = drawSwitch(c, soundRect, "Sound", "A short chime with the alert",
                prefs.getInt("sound", 1) == 1);
        y += dp(8);

        // primary action
        RectF go = new RectF(x, y, w - dp(16) - dp(20), y + dp(42));
        drawButton(c, go, "Check now", checking, true);
        sheetCheckRect = go;
        Pal.hairline(c, x, panel.bottom - dp(22), panel.right - dp(20), panel.bottom - dp(22),
                0xFF1E2223);
    }

    private float drawSwitch(Canvas c, RectF r, String title, String sub, boolean on) {
        tp.setTypeface(Pal.UI_MED);
        tp.setTextSize(dp(12.5f));
        tp.setLetterSpacing(0f);
        tp.setColor(Pal.TEXT);
        c.drawText(title, r.left, r.top + dp(12), tp);
        tp.setTypeface(Pal.UI);
        tp.setTextSize(dp(10.5f));
        tp.setColor(Pal.TEXT3);
        c.drawText(sub, r.left, r.top + dp(27), tp);
        float tw = dp(38), th = dp(22);
        RectF t = new RectF(r.right - tw, r.centerY() - th / 2, r.right, r.centerY() + th / 2);
        Pal.roundRect(c, t.left, t.top, t.right, t.bottom, th / 2,
                Pal.fill(on ? Pal.withAlpha(Pal.accent, 0.30f) : 0xFF2A2E3A));
        if (on) {
            Paint ring = Pal.fill(Pal.withAlpha(Pal.accent, 0.55f));
            ring.setStyle(Paint.Style.STROKE);
            ring.setStrokeWidth(1f);
            Pal.roundRect(c, t.left, t.top, t.right, t.bottom, th / 2, ring);
        }
        float kx = on ? t.right - th / 2 : t.left + th / 2;
        c.drawCircle(kx, t.centerY(), th / 2 - dp(2.5f), Pal.fill(on ? Pal.accent : 0xFF6A7078));
        return r.top + dp(44);
    }

    private int sceneColor(String name) {
        switch (name) {
            case "midnight": return 0xFF2A3E7A;
            case "ember": return 0xFF7A3A22;
            case "forest": return 0xFF1E4A38;
            case "carbon": return 0xFF2A2C2E;
            case "ocean": return 0xFF143A52;
            case "ice": return 0xFF35505C;
            default: return Pal.accent;
        }
    }

    private void tapInsideSheet(float x, float y) {
        if (closeRect != null && closeRect.contains(x, y)) {
            closeSheet();
            return;
        }
        if (sheetCheckRect != null && sheetCheckRect.contains(x, y) && sheetT > 0.98f) {
            closeSheet();
            host.onCheckNow();
            return;
        }
        for (float[] r : swatchRects) {
            if (x >= r[0] && x <= r[2] && y >= r[1] && y <= r[3]) {
                int i = (int) r[4];
                if (i < Pal.SCENES.length) {
                    prefs.edit().putString("scene", Pal.SCENES[i]).apply();
                    host.onPrefsChanged();
                    invalidate();
                } else {
                    host.onToast("Custom pictures are Windows-only for now.");
                }
                return;
            }
        }
        for (float[] r : accentRects) {
            float dx = x - r[0], dy = y - r[1];
            if (dx * dx + dy * dy <= r[2] * r[2]) {
                Pal.accent = Pal.ACCENTS[(int) r[3]];
                prefs.edit().putInt("accent", Pal.accent).apply();
                host.onPrefsChanged();
                invalidate();
                return;
            }
        }
        if (themeGlass != null && themeGlass.contains(x, y)) {
            prefs.edit().putString("theme", "glass").apply();
            host.onPrefsChanged();
            invalidate();
            return;
        }
        if (themeFlat != null && themeFlat.contains(x, y)) {
            prefs.edit().putString("theme", "flat").apply();
            host.onPrefsChanged();
            invalidate();
            return;
        }
        for (int i = 0; i < intervalRects.length; i++) {
            if (intervalRects[i] != null && intervalRects[i].contains(x, y)) {
                int[] opts = {1, 2, 5, 10, 15, 30};
                prefs.edit().putInt("interval", opts[i]).apply();
                scheduleNext();
                host.onPrefsChanged();
                invalidate();
                return;
            }
        }
        if (notifRect != null && notifRect.contains(x, y)) {
            prefs.edit().putInt("notifications",
                    prefs.getInt("notifications", 1) == 1 ? 0 : 1).apply();
            invalidate();
            return;
        }
        if (soundRect != null && soundRect.contains(x, y)) {
            prefs.edit().putInt("sound", prefs.getInt("sound", 1) == 1 ? 0 : 1).apply();
            invalidate();
        }
    }

    /** Tap on a filter chip, called from the activity after a tap in the list area. */
    void tapFilter(float x, float y) {
        if (y > headerH() + dp(6) + heroHeight(getWidth()) + dp(14)
                && y < headerH() + dp(6) + heroHeight(getWidth()) + dp(14) + dp(48)) {
            float pad = getWidth() >= dp(520) ? dp(20) : dp(14);
            float cx = pad;
            Paint t = Pal.text(dp(12), Pal.TEXT2, Pal.UI_MED);
            String[] labels = {"All", "Updated", "Not updated"};
            for (int i = 0; i < 3; i++) {
                float tw = t.measureText(labels[i]) + dp(28);
                if (x >= cx && x <= cx + tw) {
                    filter = i;
                    scroll = 0;
                    invalidate();
                    return;
                }
                cx += tw + dp(8);
            }
        }
    }
}
