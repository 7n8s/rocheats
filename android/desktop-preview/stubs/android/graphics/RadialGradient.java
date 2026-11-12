package android.graphics;

public class RadialGradient extends Shader {
    public RadialGradient(float cx, float cy, float radius, int c0, int c1, TileMode m) {
        float r = Math.max(1f, radius);
        awt = new java.awt.RadialGradientPaint(new java.awt.geom.Point2D.Float(cx, cy), r,
                new float[]{0f, 1f}, new java.awt.Color[]{Color.awt(c0), Color.awt(c1)});
    }
}
