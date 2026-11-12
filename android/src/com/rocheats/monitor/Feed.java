package com.rocheats.monitor;

import android.os.Handler;
import android.os.Looper;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.TimeZone;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * The live WEAO status feed, read exactly the way the desktop build reads it:
 *
 *   title        "Potassium"        matched case-insensitively
 *   updateStatus true/false         -> updated / not updated
 *   detected     true/false         -> the detection chip
 *   version      "2.6.1"            (40 chars max)
 *   updatedDate  "11/06/2026 at 1:24 PM UTC"  -> "3m ago"
 */
final class Feed {

    static final String[] KEYS = {"volt", "wave", "potassium", "isaeva", "matcha",
            "serotonin", "codex", "lucid", "axis"};
    static final String[] NAMES = {"VOLT", "WAVE", "POTASSIUM", "ISAEVA", "MATCHA",
            "SEROTONIN", "CODEX", "LUCID", "AXIS"};
    static final float[] HUES = {250, 206, 258, 230, 214, 244, 254, 238, 210};

    private static final String API = "https://weao.xyz/api/status/exploits";
    // Roblox publishes the live client build here; the feed's rbxversion is that hash.
    private static final String ROBLOX = "https://clientsettingscdn.roblox.com/v2/client-version/WindowsPlayer";
    private static final Pattern DATE = Pattern.compile(
            "(\\d{1,2})/(\\d{1,2})/(\\d{4})\\s*(?:at\\s*)?(\\d{1,2}):(\\d{2})\\s*([AaPp][Mm])?");

    static final class Item {
        boolean live;            // the feed listed this product
        boolean hasVersion, updated, detected, hasUpdatedAt;
        boolean banwave, unknownDetect;   // WEAO's extra detection flags, shown when set
        String rbx = "";                  // the Roblox build this product supports
        boolean onLatest;                 // ...and whether that is the build Roblox ships now
        String version = "";
        long updatedAt;          // epoch millis of the developer's own update
        String updatedText = "";
    }

    static final class Result {
        boolean ok, rateLimited;
        String error = "";
        int updatedCount, liveCount, onLatestCount, detectedCount, banwaveCount;
        String robloxVersion = "";   // "0.747.0.7471481"
        String robloxUpload = "";    // "version-4ea8e85481e34f46"
        long robloxCheckedAt;
        final Map<String, Item> byKey = new LinkedHashMap<>();
    }

    interface Callback {
        void onResult(Result r);
    }

    private static volatile Result last;

    /** Just the Roblox build, so a client update is noticed within seconds, not minutes. */
    interface RobloxCallback {
        void onRoblox(String version, String upload);
    }

    static void pollRoblox(final RobloxCallback cb) {
        final Handler main = new Handler(Looper.getMainLooper());
        new Thread(() -> {
            final String[] out = readRobloxVersion();
            main.post(() -> cb.onRoblox(out[0], out[1]));
        }, "roblox").start();
    }

    private static String[] readRobloxVersion() {
        HttpURLConnection c = null;
        try {
            c = (HttpURLConnection) new URL(ROBLOX).openConnection();
            c.setRequestProperty("User-Agent", "RoCheatsMonitor");
            c.setConnectTimeout(6000);
            c.setReadTimeout(6000);
            if (c.getResponseCode() != 200) return new String[]{"", ""};
            InputStream in = c.getInputStream();
            ByteArrayOutputStream bo = new ByteArrayOutputStream();
            byte[] buf = new byte[4096];
            int n;
            while ((n = in.read(buf)) > 0) bo.write(buf, 0, n);
            in.close();
            JSONObject o = new JSONObject(bo.toString("UTF-8"));
            return new String[]{o.optString("version", ""), o.optString("clientVersionUpload", "")};
        } catch (Exception e) {
            return new String[]{"", ""};
        } finally {
            if (c != null) c.disconnect();
        }
    }

    static Result last() {
        return last;
    }

    static void fetch(final Callback cb) {
        final Handler main = new Handler(Looper.getMainLooper());
        new Thread(() -> {
            final Result r = download();
            if (r.ok) last = r;
            main.post(() -> cb.onResult(r));
        }, "weao").start();
    }

    private static Result download() {
        Result r = new Result();
        HttpURLConnection c = null;
        try {
            c = (HttpURLConnection) new URL(API).openConnection();
            c.setRequestProperty("User-Agent", "WEAO-3PService");
            c.setRequestProperty("Accept", "application/json");
            c.setConnectTimeout(12000);
            c.setReadTimeout(15000);
            int code = c.getResponseCode();
            if (code == 429) {
                r.rateLimited = true;
                r.error = "The status feed is rate limiting us. Try again in a minute.";
                return r;
            }
            if (code != 200) {
                r.error = "The status feed answered " + code + ".";
                return r;
            }
            InputStream in = c.getInputStream();
            ByteArrayOutputStream bo = new ByteArrayOutputStream();
            byte[] buf = new byte[16384];
            int n;
            while ((n = in.read(buf)) > 0) bo.write(buf, 0, n);
            in.close();
            String json = bo.toString("UTF-8");
            parse(json, r);
            readRoblox(r);
            if (r.byKey.isEmpty()) r.error = "The status feed listed nothing we track.";
            else r.ok = true;
        } catch (Exception e) {
            r.error = "No connection to the status feed.";
        } finally {
            if (c != null) c.disconnect();
        }
        return r;
    }

    private static void parse(String body, Result r) throws Exception {
        JSONArray arr = new JSONArray(body);
        for (int i = 0; i < arr.length(); i++) {
            JSONObject o = arr.optJSONObject(i);
            if (o == null) continue;
            String key = o.optString("title", "").toLowerCase(Locale.US);
            if (!isTracked(key)) continue;

            Item it = new Item();
            it.live = true;
            it.updated = o.optBoolean("updateStatus", false);
            if (o.has("detected")) {
                it.detected = o.optBoolean("detected", false);
            }
            String ver = o.optString("version", "").trim();
            if (!ver.isEmpty() && !"unknown".equalsIgnoreCase(ver)) {
                it.hasVersion = true;
                it.version = ver.length() > 40 ? ver.substring(0, 40) : ver;
            }
            it.rbx = o.optString("rbxversion", "");
            it.banwave = o.optBoolean("possibleBanwave", false);
            it.unknownDetect = o.optBoolean("unknownDetection", false) || o.optBoolean("unknown", false);
            long at = parseDate(o.optString("updatedDate", ""));
            if (at > 0) {
                it.hasUpdatedAt = true;
                it.updatedAt = at;
                it.updatedText = relative(at);
            }
            r.byKey.put(key, it);
        }
        for (String k : KEYS) if (r.byKey.containsKey(k)) r.liveCount++;
        for (Item it : r.byKey.values()) {
            if (it.updated) r.updatedCount++;
            if (it.detected) r.detectedCount++;
            if (it.banwave) r.banwaveCount++;
        }
    }

    /** The current Roblox client build, so each product can be marked current or behind. */
    private static void readRoblox(Result r) {
        String[] v = readRobloxVersion();
        r.robloxVersion = v[0];
        r.robloxUpload = v[1];
        r.robloxCheckedAt = System.currentTimeMillis();
        markLatest(r);
    }

    /** Recompute which products support the current Roblox build. */
    static void markLatest(Result r) {
        if (r == null || r.robloxUpload.isEmpty()) return;
        r.onLatestCount = 0;
        for (Item it : r.byKey.values()) {
            it.onLatest = !it.rbx.isEmpty() && it.rbx.equals(r.robloxUpload);
            if (it.onLatest) r.onLatestCount++;
        }
    }

    private static boolean isTracked(String key) {
        for (String k : KEYS) if (k.equals(key)) return true;
        return false;
    }

    /** "11/06/2026 at 1:24 PM UTC" -> epoch millis, 0 when unreadable. */
    static long parseDate(String s) {
        if (s == null || s.isEmpty()) return 0;
        Matcher m = DATE.matcher(s);
        if (!m.find()) return 0;
        try {
            int mo = Integer.parseInt(m.group(1));
            int day = Integer.parseInt(m.group(2));
            int year = Integer.parseInt(m.group(3));
            int hour = Integer.parseInt(m.group(4));
            int min = Integer.parseInt(m.group(5));
            String ap = m.group(6);
            if (ap != null) {
                boolean pm = ap.toLowerCase(Locale.US).startsWith("p");
                if (pm && hour < 12) hour += 12;
                if (!pm && hour == 12) hour = 0;
            }
            if (mo < 1 || mo > 12 || day < 1 || day > 31 || year < 2000 || year > 2200) return 0;
            SimpleDateFormat f = new SimpleDateFormat("yyyy-MM-dd HH:mm", Locale.US);
            f.setTimeZone(TimeZone.getTimeZone("UTC"));
            Date d = f.parse(String.format(Locale.US, "%04d-%02d-%02d %02d:%02d", year, mo, day, hour, min));
            return d == null ? 0 : d.getTime();
        } catch (Exception e) {
            return 0;
        }
    }

    /** "3m ago", matching the desktop app's Ago(). */
    static String relative(long epochMillis) {
        long d = Math.max(0, (System.currentTimeMillis() - epochMillis) / 1000L);
        if (d < 60) return "just now";
        if (d < 3600) return (d / 60) + "m ago";
        if (d < 86400) return (d / 3600) + "h ago";
        if (d < 86400L * 30) return (d / 86400) + "d ago";
        if (d < 86400L * 365) return (d / (86400L * 30)) + "mo ago";
        return (d / (86400L * 365)) + "y ago";
    }

    static String clock(long millis) {
        SimpleDateFormat f = new SimpleDateFormat("HH:mm", Locale.US);
        f.setTimeZone(TimeZone.getDefault());
        return f.format(new Date(millis));
    }

    static List<Item> list(Result r) {
        List<Item> out = new ArrayList<>();
        for (String k : KEYS) {
            Item it = r == null ? null : r.byKey.get(k);
            out.add(it == null ? new Item() : it);
        }
        return out;
    }
}
