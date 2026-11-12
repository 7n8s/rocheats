package com.rocheats.monitor;

import android.content.Context;
import android.content.res.Configuration;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RadialGradient;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.util.TypedValue;

/** The same tokens the desktop build uses, in one place. */
final class Pal {
    static final int BG = 0xFF0A0C0D;
    static final int CARD = 0xFF141617;
    static final int ROW = 0xFF15181A;
    static final int LINE = 0xFF24282A;
    static final int LINE_HOT = 0xFF3A4042;
    static final int PANEL = 0xFF101314;
    static final int RAIL = 0xFF0D1011;
    static final int TEXT = 0xFFEDEAE4;
    static final int TEXT2 = 0xFFA3A6A1;
    static final int TEXT3 = 0xFF808680;
    static final int TEXT4 = 0xFF5A615C;
    static final int GREEN = 0xFF3DD68C;
    static final int RED = 0xFFF2616D;
    static final int AMBER = 0xFFF0B243;
    static final int VIOLET = 0xFF7C5CFF;

    static int accent = 0xFF7DEEFF;

    static final int[] ACCENTS = {0xFF7DEEFF, 0xFF5B9DFF, 0xFF8A6BFF,
            0xFF6FE7C4, 0xFFF5C46C, 0xFFFF8FA3};

    static final String[] SCENES = {"aurora", "midnight", "ember", "forest",
            "carbon", "ocean", "ice"};

    static final Typeface UI = Typeface.create("sans-serif", Typeface.NORMAL);
    static final Typeface UI_MED = Typeface.create("sans-serif-medium", Typeface.NORMAL);
    static final Typeface UI_BOLD = Typeface.create("sans-serif", Typeface.BOLD);
    static final Typeface MONO = Typeface.create("monospace", Typeface.NORMAL);

    private Pal() {}

    // ------------------------------------------------------------------ paint

    static Paint fill(int color) {
        Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        p.setColor(color);
        p.setStyle(Paint.Style.FILL);
        return p;
    }

    static Paint text(float size, int color, Typeface tf) {
        Paint p = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.SUBPIXEL_TEXT_FLAG);
        p.setColor(color);
        p.setTextSize(size);
        p.setTypeface(tf);
        p.setStyle(Paint.Style.FILL);
        return p;
    }

    /** The eyebrow face: tracked-out caps, as on the desktop. */
    static Paint caps(Context c, int dp, int color, boolean bold) {
        Paint p = text(dp2(c, dp), color, bold ? UI_BOLD : UI_MED);
        p.setLetterSpacing(0.16f);
        p.setTextAlign(Paint.Align.LEFT);
        return p;
    }

    static float dp2(Context c, float dp) {
        return TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, dp,
                c.getResources().getDisplayMetrics());
    }

    static float dp(Context c, float dp) {
        return dp * c.getResources().getDisplayMetrics().density;
    }

    static boolean wide(Context c) {
        return c.getResources().getConfiguration().orientation
                == Configuration.ORIENTATION_LANDSCAPE
                && c.getResources().getConfiguration().screenWidthDp >= 600;
    }

    // ------------------------------------------------------------- primitives

    static void roundRect(Canvas c, float l, float t, float r, float b, float rad, Paint p) {
        c.drawRoundRect(l, t, r, b, rad, rad, p);
    }

    /** Dark glassy card: a faint top highlight over a near-black fill. */
    static void glass(Canvas c, RectF r, float rad, int tint, float glassy) {
        Paint base = fill(mix(Pal.CARD, tint, glassy));
        roundRect(c, r.left, r.top, r.right, r.bottom, rad, base);
        if (glassy > 0.01f) {
            Paint hi = fill(Color.argb((int) (255 * 0.055f * glassy), 255, 255, 255));
            Path p = new Path();
            p.addRoundRect(r, rad, rad, Path.Direction.CW);
            c.save();
            c.clipPath(p);
            c.drawRect(r.left, r.top, r.right, r.top + r.height() * 0.55f, hi);
            c.restore();
            Paint edge = fill(0);
            edge.setStyle(Paint.Style.STROKE);
            edge.setStrokeWidth(Math.max(1f, 1f));
            edge.setColor(Color.argb((int) (255 * 0.10f * glassy), 228, 229, 219));
            roundRect(c, r.left + 0.5f, r.top + 0.5f, r.right - 0.5f, r.bottom - 0.5f, rad, edge);
        }
    }

    static void hairline(Canvas c, float x1, float y1, float x2, float y2, int color) {
        Paint p = fill(color);
        p.setStrokeWidth(1f);
        p.setStyle(Paint.Style.STROKE);
        c.drawLine(x1, y1, x2, y2, p);
    }

    static void dot(Canvas c, float cx, float cy, float rad, int color) {
        c.drawCircle(cx, cy, rad, fill(color));
    }

    /** Detection chip: a soft pill with a glowing dot when something is live. */
    static void chip(Canvas c, RectF r, boolean on, boolean live, float alpha) {
        int col = on ? (live ? Pal.accent : Pal.TEXT) : Pal.TEXT3;
        Paint bg = fill(withAlpha(on ? (live ? Color.argb(38, Color.red(Pal.accent),
                Color.green(Pal.accent), Color.blue(Pal.accent)) : 0x1FFFFFFF) : 0x0DFFFFFF, alpha));
        roundRect(c, r.left, r.top, r.right, r.bottom, r.height() / 2f, bg);
        Paint edge = fill(0);
        edge.setStyle(Paint.Style.STROKE);
        edge.setStrokeWidth(1f);
        edge.setColor(withAlpha(on ? (live ? 0x8C7DEEFF : 0x3DFFFFFF) : 0x1FFFFFFF, alpha));
        if (on && live) edge.setColor(withAlpha(Color.argb(140, Color.red(Pal.accent),
                Color.green(Pal.accent), Color.blue(Pal.accent)), alpha));
        roundRect(c, r.left + 0.5f, r.top + 0.5f, r.right - 0.5f, r.bottom - 0.5f,
                r.height() / 2f, edge);
        float dr = r.height() * 0.16f;
        float dx = r.left + r.height() * 0.55f;
        if (on && live) {
            Paint glow = fill(withAlpha(Color.argb(60, Color.red(col),
                    Color.green(col), Color.blue(col)), alpha));
            c.drawCircle(dx, r.centerY(), dr * 2.6f, glow);
        }
        dot(c, dx, r.centerY(), dr, withAlpha(col, alpha));
    }

    static int mix(int a, int b, float t) {
        t = clamp01(t);
        int ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255;
        int br = (b >> 16) & 255, bg = (b >> 8) & 255, bb = b & 255;
        return Color.rgb((int) (ar + (br - ar) * t), (int) (ag + (bg - ag) * t),
                (int) (ab + (bb - ab) * t));
    }

    static int withAlpha(int color, float a) {
        return Color.argb((int) (Color.alpha(color) * clamp01(a)), Color.red(color),
                Color.green(color), Color.blue(color));
    }

    static float clamp01(float v) {
        return v < 0 ? 0 : (v > 1 ? 1 : v);
    }

    static float ease(float t) {
        t = clamp01(t);
        return t * t * (3 - 2 * t);
    }

    static int hsl(float h, float s, float l) {
        h = ((h % 360f) + 360f) % 360f;
        float c = (1 - Math.abs(2 * l - 1)) * s;
        float x = c * (1 - Math.abs(((h / 60f) % 2) - 1));
        float m = l - c / 2f, r, g, b;
        if (h < 60) { r = c; g = x; b = 0; }
        else if (h < 120) { r = x; g = c; b = 0; }
        else if (h < 180) { r = 0; g = c; b = x; }
        else if (h < 240) { r = 0; g = x; b = c; }
        else if (h < 300) { r = x; g = 0; b = c; }
        else { r = c; g = 0; b = x; }
        return Color.rgb((int) ((r + m) * 255), (int) ((g + m) * 255), (int) ((b + m) * 255));
    }

    /** Full-screen backdrop: near-black, lifted by one soft radial tint. */
    static void scene(Canvas c, String name, int w, int h) {
        Paint p = fill(Pal.BG);
        c.drawRect(0, 0, w, h, p);
        int tint;
        switch (name) {
            case "midnight": tint = 0xFF2A3E7A; break;
            case "ember": tint = 0xFF7A3A22; break;
            case "forest": tint = 0xFF1E4A38; break;
            case "carbon": tint = 0xFF2A2C2E; break;
            case "ocean": tint = 0xFF143A52; break;
            case "ice": tint = 0xFF35505C; break;
            default: tint = Pal.accent; break;
        }
        Paint g = new Paint(Paint.ANTI_ALIAS_FLAG);
        int a = "carbon".equals(name) ? 0x22 : 0x30;
        int tinted = Color.argb(a, Color.red(tint), Color.green(tint), Color.blue(tint));
        g.setShader(new RadialGradient(w * 0.78f, h * 0.10f, Math.max(w, h) * 0.95f,
                tinted, 0x00000000, Shader.TileMode.CLAMP));
        c.drawRect(0, 0, w, h, g);
        Paint g2 = new Paint(Paint.ANTI_ALIAS_FLAG);
        g2.setShader(new RadialGradient(w * 0.05f, h * 0.95f, Math.max(w, h) * 0.7f,
                Color.argb(0x14, 91, 157, 255), 0x00000000, Shader.TileMode.CLAMP));
        c.drawRect(0, 0, w, h, g2);
        Paint v = new Paint(Paint.ANTI_ALIAS_FLAG);
        v.setShader(new LinearGradient(0, 0, 0, h, Color.argb(0x30, 0, 0, 0), 0x00000000,
                Shader.TileMode.CLAMP));
        c.drawRect(0, 0, w, h * 0.35f, v);
    }
}
