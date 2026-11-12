package android.content;

import java.util.HashMap;
import java.util.Map;

public class SharedPreferences {
    public final Map<String, Object> map = new HashMap<>();
    private final Editor _e = new Editor(this);
    public Editor edit() { return _e; }
    public int getInt(String k, int d) { Object v = map.get(k); return v instanceof Integer ? (Integer) v : d; }
    public String getString(String k, String d) { Object v = map.get(k); return v instanceof String ? (String) v : d; }
    public boolean getBoolean(String k, boolean d) { Object v = map.get(k); return v instanceof Boolean ? (Boolean) v : d; }

    public static class Editor {
        private final SharedPreferences sp;
        Editor(SharedPreferences sp) { this.sp = sp; }
        public Editor putInt(String k, int v) { sp.map.put(k, v); return this; }
        public Editor putString(String k, String v) { sp.map.put(k, v); return this; }
        public Editor putBoolean(String k, boolean v) { sp.map.put(k, v); return this; }
        public void apply() {}
    }
}
