package android.util;

public class TypedValue {
    public static final int COMPLEX_UNIT_DIP = 1, COMPLEX_UNIT_SP = 2;
    public static float applyDimension(int unit, float value, android.util.DisplayMetrics m) {
        return value * m.density;
    }
}
