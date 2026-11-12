package android.view.animation;

public class DecelerateInterpolator implements Interpolator {
    public DecelerateInterpolator() {}
    public DecelerateInterpolator(float f) {}
    public float getInterpolation(float t) { return 1 - (1 - t) * (1 - t); }
}
