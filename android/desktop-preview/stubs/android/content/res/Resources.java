package android.content.res;

import android.util.DisplayMetrics;
import java.util.HashMap;
import java.util.Map;

public class Resources {
    public final Map<String, Integer> ids = new HashMap<>();
    public final DisplayMetrics metrics = new DisplayMetrics();
    public int getIdentifier(String name, String type, String pkg) { return ids.getOrDefault(type + "/" + name, 0); }
    public void register(String name, String type, int id) { ids.put(type + "/" + name, id); }
    public DisplayMetrics getDisplayMetrics() { return metrics; }
    public final Configuration config = new Configuration();
    public Configuration getConfiguration() { return config; }
}
