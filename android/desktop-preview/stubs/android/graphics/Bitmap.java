package android.graphics;

public class Bitmap {
    public enum Config { ARGB_8888, RGB_565 }
    public java.awt.image.BufferedImage img;
    public Bitmap(java.awt.image.BufferedImage i) { img = i; }
    public int getWidth() { return img.getWidth(); }
    public int getHeight() { return img.getHeight(); }
    public static Bitmap createScaledBitmap(Bitmap src, int w, int h, boolean filter) {
        java.awt.image.BufferedImage out = new java.awt.image.BufferedImage(Math.max(1, w), Math.max(1, h),
                java.awt.image.BufferedImage.TYPE_INT_ARGB);
        java.awt.Graphics2D g = out.createGraphics();
        g.setRenderingHint(java.awt.RenderingHints.KEY_INTERPOLATION,
                java.awt.RenderingHints.VALUE_INTERPOLATION_BILINEAR);
        g.drawImage(src.img, 0, 0, Math.max(1, w), Math.max(1, h), null);
        g.dispose();
        return new Bitmap(out);
    }
    public static Bitmap createBitmap(int w, int h, Config c) {
        return new Bitmap(new java.awt.image.BufferedImage(Math.max(1, w), Math.max(1, h),
                java.awt.image.BufferedImage.TYPE_INT_ARGB));
    }
}
