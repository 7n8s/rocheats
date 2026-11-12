package com.rocheats.monitor;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.BlurMaskFilter;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.os.SystemClock;
import android.util.LruCache;
import android.view.View;

/** The opening: a blurred RC behind the one-line wordmark, then it dissolves. */
final class SplashView extends View {

    private static final long HOLD = 1050, LIFT = 3200;

    private final Bitmap mark;
    private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint tp = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final long t0 = SystemClock.uptimeMillis();
    private float shown;
    private Runnable done;
    private final LruCache<Integer, Bitmap> blurCache = new LruCache<>(8);

    SplashView(Context c) {
        super(c);
        int id = c.getResources().getIdentifier("mark", "drawable", c.getPackageName());
        mark = BitmapFactory.decodeResource(c.getResources(), id);
        p.setFilterBitmap(true);
        tp.setTypeface(Pal.UI_BOLD);
    }

    void setOnDone(Runnable r) {
        done = r;
    }

    @Override
    protected void onDraw(Canvas c) {
        final long elapsed = SystemClock.uptimeMillis() - t0;
        final float w = getWidth(), h = getHeight();
        c.drawColor(Pal.BG);

        // A soft accent haze and the blurred RC, both breathing out.
        float drift = Pal.ease(Pal.clamp01(elapsed / 700f));
        p.setShader(new android.graphics.RadialGradient(w * 0.5f, h * 0.42f, w * 0.75f,
                android.graphics.Color.argb(0x1E, 125, 238, 255), 0x00000000,
                android.graphics.Shader.TileMode.CLAMP));
        c.drawRect(0, 0, w, h, p);
        p.setShader(null);

        float base = Math.min(w, h) * 0.62f;
        // Two layers so it reads as haze, not a double image: a wide, faint halo
        // behind a tighter one, both breathing out with the wordmark.
        drawBlur(c, w * 0.5f, h * 0.44f, base * (1.30f + 0.10f * drift), 0.15f * (1 - 0.4f * drift));
        drawBlur(c, w * 0.5f, h * 0.44f, base * (0.86f + 0.14f * drift), 0.26f * (1 - 0.4f * drift));

        // The crisp mark above the wordmark.
        float m = Pal.dp(getContext(), 52);
        float my = h * 0.5f - Pal.dp(getContext(), 46);
        p.setAlpha(0xFF);
        c.drawBitmap(mark, null, new RectF(w / 2 - m / 2, my - m / 2, w / 2 + m / 2, my + m / 2), p);

        // One line: ROCHEATS STATUS CHECKER, shrinking until it fits.
        float room = w - Pal.dp(getContext(), 48);
        float size = Pal.dp(getContext(), 27);
        tp.setLetterSpacing(0.06f);
        String a = "ROCHEATS ", b = "STATUS CHECKER";
        while (size > Pal.dp(getContext(), 11)) {
            tp.setTextSize(size);
            if (tp.measureText(a) + Pal.dp(getContext(), 10) + tp.measureText(b) <= room) break;
            size -= Pal.dp(getContext(), 1);
        }
        float wa = tp.measureText(a), wb = tp.measureText(b), gap = Pal.dp(getContext(), 10);
        float x = w / 2 - (wa + gap + wb) / 2;
        float baseline = my + m / 2 + Pal.dp(getContext(), 46);
        tp.setColor(Pal.TEXT);
        c.drawText(a, x, baseline, tp);
        tp.setColor(Pal.accent);
        c.drawText(b, x + wa + gap, baseline, tp);

        // Caption.
        float captionY = baseline + Pal.dp(getContext(), 30);
        drawCaps(c, "WEAO  \u00B7  LIVE STATUS", w / 2, captionY, 9, Pal.TEXT3, 0.30f);

        // The hairline that fills while the first check runs.
        float ruleW = Math.min(Pal.dp(getContext(), 170), w * 0.4f);
        float ry = captionY + Pal.dp(getContext(), 40);
        p.setColor(0xFF1C2021);
        c.drawRect(w / 2 - ruleW / 2, ry, w / 2 + ruleW / 2, ry + Pal.dp(getContext(), 2), p);
        p.setColor(Pal.accent);
        float fill = Pal.clamp01(shown);
        c.drawRect(w / 2 - ruleW / 2, ry, w / 2 - ruleW / 2 + ruleW * fill, ry + Pal.dp(getContext(), 2), p);

        shown = Math.max(shown, Pal.clamp01(elapsed / 700f));

        if (elapsed > HOLD) {
            float k = Pal.ease(Pal.clamp01((elapsed - HOLD) / (float) (LIFT - HOLD)));
            p.setColor(Pal.withAlpha(Pal.BG, k * 0.92f));
            c.drawRect(0, 0, w, h, p);
            if (k >= 1f) {
                if (done != null) {
                    Runnable d = done;
                    done = null;
                    d.run();
                }
            } else {
                postInvalidateOnAnimation();
            }
        } else {
            postInvalidateOnAnimation();
        }
    }


    private void drawBlur(Canvas c, float cx, float cy, float size, float alpha) {
        int key = Math.round(size / 8f);
        Bitmap b = blurCache.get(key);
        if (b == null) {
            int px = Math.max(16, Math.round(size));
            Bitmap src = Bitmap.createScaledBitmap(mark, px, px, true);
            float r = Math.max(3f, px * 0.075f);
            Bitmap out = src;
            for (int i = 0; i < 3; i++) {
                Bitmap tmp = Bitmap.createBitmap(px, px, Bitmap.Config.ARGB_8888);
                Canvas tc = new Canvas(tmp);
                Paint bp = new Paint(Paint.ANTI_ALIAS_FLAG);
                bp.setMaskFilter(new BlurMaskFilter(r, BlurMaskFilter.Blur.NORMAL));
                tc.drawBitmap(out, 0, 0, bp);
                out = tmp;
            }
            b = out;
            blurCache.put(key, b);
        }
        float half = size / 2;
        p.setAlpha((int) (255 * Pal.clamp01(alpha)));
        c.drawBitmap(b, null, new RectF(cx - half, cy - half, cx + half, cy + half), p);
        p.setAlpha(0xFF);
    }

    private void drawCaps(Canvas c, String s, float cx, float y, int dpSize, int color, float alpha) {
        Paint q = Pal.text(Pal.dp(getContext(), dpSize), Pal.withAlpha(color, alpha), Pal.UI_MED);
        q.setLetterSpacing(0.30f);
        float w = q.measureText(s);
        c.drawText(s, cx - w / 2, y, q);
    }
}
