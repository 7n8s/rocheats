package com.rocheats.monitor;

import java.util.ArrayList;
import java.util.List;

/**
 * Turns two feed snapshots into the handful of events worth a notification.
 * Deliberately free of Android types so it can be tested on a plain JVM.
 */
final class Watch {

    static final int ROBLOX_BUILD = 0;   // Roblox shipped a new client
    static final int EXEC_UPDATED = 1;   // a product went not-updated -> updated
    static final int EXEC_STALE = 2;     // ...or the other way round
    static final int DETECT_ON = 3;
    static final int DETECT_OFF = 4;
    static final int EXEC_NEW_VERSION = 5;  // same status, new version string

    static final class Event {
        final int kind;
        final int index;        // product index, -1 for Roblox itself
        final String title;
        final String body;

        Event(int kind, int index, String title, String body) {
            this.kind = kind;
            this.index = index;
            this.title = title;
            this.body = body;
        }

        @Override
        public String toString() {
            return kind + ":" + title + " / " + body;
        }
    }

    private Watch() {}

    /**
     * @param before previous snapshot, may be null on the very first poll
     * @param after  the snapshot just fetched
     * @param robloxBefore last Roblox upload we knew about, may be empty
     */
    static List<Event> diff(Feed.Result before, Feed.Result after, String robloxBefore) {
        List<Event> out = new ArrayList<>();
        if (after == null) return out;

        // Roblox itself: the fastest-moving, most useful alert.
        if (!after.robloxUpload.isEmpty()
                && !robloxBefore.isEmpty()
                && !after.robloxUpload.equals(robloxBefore)) {
            out.add(new Event(ROBLOX_BUILD, -1,
                    "Roblox updated",
                    after.robloxVersion.isEmpty() ? "A new client build is out."
                            : "Now on " + after.robloxVersion + " \u2014 executors may need to catch up."));
        }

        if (before == null) return out;
        List<Feed.Item> a = Feed.list(before), b = Feed.list(after);
        for (int i = 0; i < b.size(); i++) {
            Feed.Item o = a.get(i), n = b.get(i);
            if (!n.live) continue;
            String name = Feed.NAMES[i];

            if (o.live && o.updated != n.updated) {
                out.add(new Event(n.updated ? EXEC_UPDATED : EXEC_STALE, i, name,
                        n.updated ? (n.hasVersion ? "Updated to " + n.version : "Back in sync with Roblox")
                                  : "Not updated for the current build"));
            } else if (o.live && n.hasVersion && o.hasVersion && !o.version.equals(n.version)) {
                out.add(new Event(EXEC_NEW_VERSION, i, name, "New version " + n.version));
            }

            if (o.live && o.detected != n.detected) {
                out.add(new Event(n.detected ? DETECT_ON : DETECT_OFF, i, name,
                        n.detected ? "WEAO reports it as detected" : "No longer reported as detected"));
            }
        }
        return out;
    }

    /** True when the event is about something the user muted. */
    static boolean muted(boolean[] perProduct, Event e) {
        return e.index >= 0 && e.index < perProduct.length && !perProduct[e.index];
    }
}
