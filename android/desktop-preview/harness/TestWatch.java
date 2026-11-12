package com.rocheats.monitor;

import java.lang.reflect.Method;
import java.util.List;

/**
 * Proves the alerting logic on a plain JVM: live feed in, simulated changes out.
 * Run from android/desktop-preview: java -cp out com.rocheats.monitor.TestWatch
 */
public final class TestWatch {

    private static int fails;

    public static void main(String[] a) throws Exception {
        liveFeed();
        simulatedChanges();
        simulatedRobloxBuild();
        System.out.println(fails == 0 ? "ALL CHECKS PASSED" : (fails + " CHECK(S) FAILED"));
        System.exit(fails == 0 ? 0 : 1);
    }

    /** The real endpoint, parsed by the real code. */
    private static void liveFeed() throws Exception {
        Method dl = Feed.class.getDeclaredMethod("download");
        dl.setAccessible(true);
        Feed.Result r = (Feed.Result) dl.invoke(null);
        ok(r.ok, "live feed downloads");
        ok(r.byKey.size() >= 6, "live feed matched products (" + r.byKey.size() + ")");
        ok(!r.robloxVersion.isEmpty(), "roblox version read (" + r.robloxVersion + ")");
        ok(r.robloxUpload.startsWith("version-"), "roblox upload hash read");
        System.out.printf("live: %d/%d updated, %d detected, %d of %d on the current build (%s)%n",
                r.updatedCount, r.liveCount, r.detectedCount, r.onLatestCount, r.liveCount,
                r.robloxVersion);
        for (String k : Feed.KEYS) {
            Feed.Item it = r.byKey.get(k);
            if (it == null) continue;
            System.out.printf("   %-10s v%-14s %-12s %-10s %s%n", k.toUpperCase(),
                    it.hasVersion ? it.version : "-",
                    it.updated ? "updated" : "stale",
                    it.detected ? "detected" : "clear",
                    it.rbx.isEmpty() ? "" : (it.onLatest ? "current build" : "behind Roblox"));
        }

        // a first poll must never spam: no "before" means no events
        ok(Watch.diff(null, r, "").isEmpty(), "first poll emits no alerts");
        // an unchanged poll must stay silent too
        ok(Watch.diff(r, r, r.robloxUpload).isEmpty(), "unchanged poll emits no alerts");
    }

    /** Volt goes stale, Matcha gets detected, Isaeva ships a new version. */
    private static void simulatedChanges() throws Exception {
        Feed.Result a = sample();
        Feed.Result b = sample();
        b.byKey.get("volt").updated = false;
        b.byKey.get("matcha").detected = true;
        b.byKey.get("isaeva").version = "2.7.0";
        List<Watch.Event> ev = Watch.diff(a, b, a.robloxUpload);
        System.out.println("simulated changes -> " + ev);
        ok(has(ev, Watch.EXEC_STALE, "VOLT"), "stale executor alert");
        ok(has(ev, Watch.DETECT_ON, "MATCHA"), "detection alert");
        ok(has(ev, Watch.EXEC_NEW_VERSION, "ISAEVA"), "new version alert");

        // muting a product silences exactly that product
        boolean[] muted = {true, true, true, true, false, true, true, true, true};
        int kept = 0, dropped = 0;
        for (Watch.Event e : ev) {
            if (Watch.muted(muted, e)) dropped++;
            else kept++;
        }
        ok(dropped == 1, "mute silences the muted product only (" + kept + " kept)");
    }

    /** A new Roblox build must fire once, then go quiet. */
    private static void simulatedRobloxBuild() throws Exception {
        Feed.Result a = sample();
        Feed.Result b = sample();
        b.robloxVersion = "0.748.0.7482000";
        b.robloxUpload = "version-aaaaaaaaaaaaaaaa";
        b.byKey.get("volt").rbx = "version-bbbbbbbbbbbbbbbb";
        Feed.markLatest(b);
        List<Watch.Event> ev = Watch.diff(a, b, a.robloxUpload);
        System.out.println("simulated roblox update -> " + ev);
        boolean roblox = false;
        for (Watch.Event e : ev) if (e.kind == Watch.ROBLOX_BUILD) roblox = true;
        ok(roblox, "roblox update alert");
        ok(b.onLatestCount == 0, "every executor marked behind the new build");
        ok(Watch.diff(b, b, b.robloxUpload).isEmpty(), "same build does not re-alert");
    }

    private static boolean has(List<Watch.Event> ev, int kind, String title) {
        for (Watch.Event e : ev) if (e.kind == kind && e.title.equals(title)) return true;
        return false;
    }

    private static Feed.Result sample() {
        Feed.Result r = new Feed.Result();
        r.ok = true;
        r.robloxVersion = "0.747.0.7471481";
        r.robloxUpload = "version-4ea8e85481e34f46";
        for (int i = 0; i < Feed.KEYS.length; i++) {
            Feed.Item it = new Feed.Item();
            it.live = !Feed.KEYS[i].equals("codex");
            it.hasVersion = it.live;
            it.version = "1.0." + i;
            it.updated = it.live;
            it.detected = false;
            it.rbx = r.robloxUpload;
            r.byKey.put(Feed.KEYS[i], it);
        }
        Feed.markLatest(r);
        return r;
    }

    private static void ok(boolean cond, String what) {
        System.out.println((cond ? "  ok   " : "  FAIL ") + what);
        if (!cond) fails++;
    }
}
