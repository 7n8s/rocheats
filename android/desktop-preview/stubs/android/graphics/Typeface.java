package android.graphics;

public final class Typeface {
    public static final int NORMAL = 0, BOLD = 1;
    public final java.awt.Font font;
    private Typeface(java.awt.Font f) { font = f; }
    public static Typeface create(String family, int style) {
        String f = family == null ? "SansSerif" : family;
        if (f.contains("monospace")) f = "Monospaced";
        return new Typeface(new java.awt.Font(f, style == BOLD ? java.awt.Font.BOLD : java.awt.Font.PLAIN, 12));
    }
}
