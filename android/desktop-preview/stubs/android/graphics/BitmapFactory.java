package android.graphics;

public final class BitmapFactory {
    public static final java.util.Map<Integer, String> REGISTRY = new java.util.HashMap<>();
    public static Bitmap decodeResource(android.content.res.Resources r, int id) {
        String path = REGISTRY.get(id);
        return path == null ? null : decodeFile(path);
    }
    public static Bitmap decodeFile(String path) {
        try {
            return new Bitmap(javax.imageio.ImageIO.read(new java.io.File(path)));
        } catch (Exception e) {
            return null;
        }
    }
}
