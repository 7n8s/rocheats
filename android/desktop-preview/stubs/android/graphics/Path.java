package android.graphics;

public class Path {
    public enum Direction { CW, CCW }
    public java.awt.geom.Path2D.Float p = new java.awt.geom.Path2D.Float();
    public void addCircle(float cx, float cy, float r, Direction d) {
        p.append(new java.awt.geom.Ellipse2D.Float(cx - r, cy - r, r * 2, r * 2), false);
    }
    public void addRoundRect(RectF r, float rad, float rad2, Direction d) {
        p.append(new java.awt.geom.RoundRectangle2D.Float(r.left, r.top, r.width(), r.height(), rad * 2, rad * 2), false);
    }
}
