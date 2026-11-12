package android.content;

public class Context {
    public final android.content.res.Resources res = new android.content.res.Resources();
    public android.content.res.Resources getResources() { return res; }
    public String getPackageName() { return "com.rocheats.monitor"; }
    public SharedPreferences getSharedPreferences(String n, int mode) { return new SharedPreferences(); }
}
