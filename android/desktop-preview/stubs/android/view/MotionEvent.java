package android.view;

public class MotionEvent {
    public static final int ACTION_DOWN = 0, ACTION_UP = 1, ACTION_MOVE = 2, ACTION_CANCEL = 3;
    public float x, y;
    public int action;
    public int getActionMasked() { return action; }
    public float getX() { return x; }
    public float getY() { return y; }
}
