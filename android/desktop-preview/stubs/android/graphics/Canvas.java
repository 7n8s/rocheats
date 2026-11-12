package android.graphics;

public class Canvas {
    public java.awt.Graphics2D g;
    private final java.util.ArrayDeque<java.awt.Shape> clips = new java.util.ArrayDeque<>();
    private final java.util.ArrayList<java.awt.Composite> comps = new java.util.ArrayList<>();

    public Canvas(java.awt.Graphics2D g) {
        this.g = g;
        g.setRenderingHint(java.awt.RenderingHints.KEY_ANTIALIASING,
                java.awt.RenderingHints.VALUE_ANTIALIAS_ON);
        g.setRenderingHint(java.awt.RenderingHints.KEY_STROKE_CONTROL,
                java.awt.RenderingHints.VALUE_STROKE_PURE);
        g.setRenderingHint(java.awt.RenderingHints.KEY_TEXT_ANTIALIASING,
                java.awt.RenderingHints.VALUE_TEXT_ANTIALIAS_ON);
    }

    private java.awt.Composite comp(Paint p) {
        return java.awt.AlphaComposite.getInstance(java.awt.AlphaComposite.SRC_OVER, p.alpha / 255f);
    }

    private java.awt.Paint fillFor(Paint p) {
        if (p.shader != null && p.shader.awt != null) return p.shader.awt;
        return Color.awt(p.color);
    }

    private java.awt.Stroke strokeFor(Paint p) {
        float w = Math.max(1f, p.strokeWidth);
        if (p.cap == Paint.Cap.ROUND)
            return new java.awt.BasicStroke(w, java.awt.BasicStroke.CAP_ROUND, java.awt.BasicStroke.JOIN_ROUND);
        return new java.awt.BasicStroke(w);
    }

    public void drawColor(int color) {
        g.setComposite(java.awt.AlphaComposite.Src);
        g.setColor(Color.awt(color));
        g.fillRect(0, 0, 100000, 100000);
        g.setComposite(java.awt.AlphaComposite.SrcOver);
    }

    public Canvas(Bitmap b) { this(b.img.createGraphics()); }

    public void drawBitmap(Bitmap b, float x, float y, Paint p) {
        if (b == null || b.img == null) return;
        g.setComposite(comp(p));
        g.drawImage(b.img, (int) x, (int) y, null);
    }

    public void drawRect(RectF r, Paint p) { drawRect(r.left, r.top, r.right, r.bottom, p); }

    public void drawRect(float l, float t, float r, float b, Paint p) {
        g.setComposite(comp(p));
        g.setPaint(fillFor(p));
        java.awt.geom.Rectangle2D.Float sh = new java.awt.geom.Rectangle2D.Float(l, t, r - l, b - t);
        if (p.style == Paint.Style.STROKE) { g.setStroke(strokeFor(p)); g.draw(sh); } else { g.fill(sh); }
    }

    public void drawRoundRect(float l, float t, float r, float b, float rad, float rad2, Paint p) {
        g.setComposite(comp(p));
        g.setPaint(fillFor(p));
        java.awt.geom.RoundRectangle2D.Float sh =
                new java.awt.geom.RoundRectangle2D.Float(l, t, r - l, b - t, rad * 2, rad * 2);
        if (p.style == Paint.Style.STROKE) { g.setStroke(strokeFor(p)); g.draw(sh); } else { g.fill(sh); }
    }

    public void drawRoundRect(RectF r, float rad, float rad2, Paint p) {
        drawRoundRect(r.left, r.top, r.right, r.bottom, rad, rad2, p);
    }

    public void drawCircle(float cx, float cy, float rad, Paint p) {
        g.setComposite(comp(p));
        g.setPaint(fillFor(p));
        java.awt.geom.Ellipse2D.Float sh = new java.awt.geom.Ellipse2D.Float(cx - rad, cy - rad, rad * 2, rad * 2);
        if (p.style == Paint.Style.STROKE) { g.setStroke(strokeFor(p)); g.draw(sh); } else { g.fill(sh); }
    }

    public void drawLine(float x1, float y1, float x2, float y2, Paint p) {
        g.setComposite(comp(p));
        g.setColor(Color.awt(p.color));
        g.setStroke(strokeFor(p));
        g.draw(new java.awt.geom.Line2D.Float(x1, y1, x2, y2));
    }

    public void drawArc(RectF r, float start, float sweep, boolean center, Paint p) {
        g.setComposite(comp(p));
        g.setColor(Color.awt(p.color));
        g.setStroke(strokeFor(p));
        g.draw(new java.awt.geom.Arc2D.Float(r.left, r.top, r.width(), r.height(), start, sweep,
                java.awt.geom.Arc2D.OPEN));
    }

    public void drawPath(Path path, Paint p) {
        g.setComposite(comp(p));
        g.setColor(Color.awt(p.color));
        if (p.style == Paint.Style.STROKE) { g.setStroke(strokeFor(p)); g.draw(path.p); } else { g.fill(path.p); }
    }

    public void drawText(String s, float x, float y, Paint p) {
        if (s == null || s.isEmpty()) return;
        g.setComposite(comp(p));
        g.setColor(Color.awt(p.color));
        g.setFont(p.font());
        java.awt.FontMetrics fm = g.getFontMetrics();
        if (p.letterSpacing == 0) { g.drawString(s, x, y); return; }
        float cx = x;
        for (int i = 0; i < s.length(); i++) {
            g.drawString(s.substring(i, i + 1), cx, y);
            cx += fm.charWidth(s.charAt(i)) + p.letterSpacing * p.textSize;
        }
    }

    private static final java.util.Map<String, java.awt.image.BufferedImage> BLUR = new java.util.HashMap<>();

    public void drawBitmap(Bitmap b, java.awt.geom.Rectangle2D src, RectF dst, Paint p) {
        if (b == null || b.img == null) return;
        g.setComposite(comp(p));
        java.awt.image.BufferedImage img = b.img;
        if (p.maskFilter != null) {
            final int r = Math.max(1, Math.round(p.maskFilter.radius / 2));
            String key = System.identityHashCode(b) + ":" + r + ":" + (int) dst.width();
            final java.awt.image.BufferedImage srcImg = img;
            img = BLUR.computeIfAbsent(key, k -> blur(srcImg, r));
        }
        g.setRenderingHint(java.awt.RenderingHints.KEY_INTERPOLATION,
                java.awt.RenderingHints.VALUE_INTERPOLATION_BILINEAR);
        g.drawImage(img, (int) dst.left, (int) dst.top, (int) dst.width(), (int) dst.height(), null);
    }

    private static java.awt.image.BufferedImage blur(java.awt.image.BufferedImage in, int r) {
        int w = in.getWidth(), h = in.getHeight();
        int[] px = in.getRGB(0, 0, w, h, null, 0, w);
        int[] tmp = new int[px.length];
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                long a = 0, rr = 0, gg = 0, bb = 0; int n = 0;
                for (int dx = -r; dx <= r; dx++) {
                    int xx = x + dx; if (xx < 0 || xx >= w) continue;
                    int v = px[y * w + xx];
                    a += (v >>> 24) & 255; rr += (v >> 16) & 255; gg += (v >> 8) & 255; bb += v & 255; n++;
                }
                tmp[y * w + x] = (((int) (a / n)) << 24) | (((int) (rr / n)) << 16) | (((int) (gg / n)) << 8) | (int) (bb / n);
            }
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                long a = 0, rr = 0, gg = 0, bb = 0; int n = 0;
                for (int dy = -r; dy <= r; dy++) {
                    int yy = y + dy; if (yy < 0 || yy >= h) continue;
                    int v = tmp[yy * w + x];
                    a += (v >>> 24) & 255; rr += (v >> 16) & 255; gg += (v >> 8) & 255; bb += v & 255; n++;
                }
                px[y * w + x] = (((int) (a / n)) << 24) | (((int) (rr / n)) << 16) | (((int) (gg / n)) << 8) | (int) (bb / n);
            }
        java.awt.image.BufferedImage out = new java.awt.image.BufferedImage(w, h,
                java.awt.image.BufferedImage.TYPE_INT_ARGB);
        out.setRGB(0, 0, w, h, px, 0, w);
        return out;
    }

    public void save() { java.awt.Shape c = g.getClip(); if (c == null) c = new java.awt.geom.Rectangle2D.Float(-1e6f, -1e6f, 2e6f, 2e6f); clips.push(c); comps.add(g.getComposite()); }
    public void restore() { if (!clips.isEmpty()) g.setClip(clips.pop()); if (!comps.isEmpty()) g.setComposite(comps.remove(comps.size() - 1)); }
    public void clipPath(Path p) { g.clip(p.p); }
}
