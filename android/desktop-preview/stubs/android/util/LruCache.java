package android.util;

public class LruCache<K, V> {
    private final java.util.LinkedHashMap<K, V> map = new java.util.LinkedHashMap<K, V>(16, 0.75f, true);
    public LruCache(int max) {}
    public V get(K k) { return map.get(k); }
    public V put(K k, V v) { map.put(k, v); return v; }
}
