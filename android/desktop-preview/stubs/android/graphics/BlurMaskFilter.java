package android.graphics;

public class BlurMaskFilter {
    public enum Blur { NORMAL, SOLID, OUTER, INNER }
    public final float radius;
    public BlurMaskFilter(float radius, Blur style) { this.radius = radius; }
}
