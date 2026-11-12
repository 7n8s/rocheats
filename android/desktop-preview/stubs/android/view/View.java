package android.view;

public class View {
    protected final android.content.Context ctx;
    private int w, h;
    public View(android.content.Context c) { ctx = c; }
    public android.content.Context getContext() { return ctx; }
    public int getWidth() { return w; }
    public int getHeight() { return h; }
    public void layout(int width, int height) { w = width; h = height; }
    protected void setMeasuredDimension(int width, int height) { w = width; h = height; }
    protected void onDraw(android.graphics.Canvas c) {}
    protected void onMeasure(int wSpec, int hSpec) { setMeasuredDimension(w, h); }
    public static class MeasureSpec {
        public static int getSize(int spec) { return spec; }
    }
    public boolean onTouchEvent(MotionEvent e) { return false; }
    public void invalidate() {}
    public void postInvalidateOnAnimation() {}
    public boolean canScrollVertically(int dir) { return false; }
    public int getSuggestedMinimumWidth() { return w; }
    public int getSuggestedMinimumHeight() { return h; }
}
