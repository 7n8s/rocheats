package android.graphics;

public class LinearGradient extends Shader {
    public LinearGradient(float x0, float y0, float x1, float y1, int c0, int c1, TileMode m) {
        awt = new java.awt.GradientPaint(x0, y0, Color.awt(c0),
                (x0 == x1 && y0 == y1) ? x1 + 1 : x1, (x1 == x0 && y0 == y1) ? y1 + 1 : y1, Color.awt(c1));
    }
}
