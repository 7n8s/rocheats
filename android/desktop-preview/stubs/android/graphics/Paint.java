package android.graphics;

public class Paint {
    public static final int ANTI_ALIAS_FLAG = 1, SUBPIXEL_TEXT_FLAG = 128;
    public enum Style { FILL, STROKE, FILL_AND_STROKE }
    public enum Cap { BUTT, ROUND, SQUARE }
    public enum Align { LEFT, CENTER, RIGHT }

    public float textSize = 12;
    public int color = 0xFF000000;
    public int alpha = 255;
    public Typeface typeface = Typeface.create("sans-serif", 0);
    public float letterSpacing = 0;
    public Style style = Style.FILL;
    public float strokeWidth = 1;
    public Cap cap = Cap.BUTT;
    public Shader shader;
    public BlurMaskFilter maskFilter;
    public boolean filterBitmap;

    public Paint() {}
    public Paint(int flags) {}

    public void setColor(int c) { color = c; }
    public void setAlpha(int a) { alpha = a; }
    public void setTextSize(float s) { textSize = s; }
    public void setTypeface(Typeface t) { typeface = t; }
    public void setLetterSpacing(float s) { letterSpacing = s; }
    public void setStyle(Style s) { style = s; }
    public void setStrokeWidth(float w) { strokeWidth = w; }
    public void setStrokeCap(Cap c) { cap = c; }
    public void setShader(Shader s) { shader = s; }
    public void setMaskFilter(BlurMaskFilter f) { maskFilter = f; }
    public void setFilterBitmap(boolean b) { filterBitmap = b; }
    public void setAntiAlias(boolean b) {}
    public void setTextAlign(Align a) {}
    public void setSubpixelText(boolean b) {}

    public java.awt.Font font() { return typeface.font.deriveFont(textSize); }
    public float measureText(String s) {
        if (s == null) return 0;
        java.awt.FontMetrics fm = new java.awt.Canvas().getFontMetrics(font());
        float w = 0;
        for (int i = 0; i < s.length(); i++) {
            w += fm.charWidth(s.charAt(i));
            if (i < s.length() - 1) w += letterSpacing * textSize;
        }
        return w;
    }
}
