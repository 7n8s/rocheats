# RoCheats Monitor — web

The same checker as a static site: no server, no build step, no dependencies.

```
index.html   markup
styles.css   the desktop app's tokens as CSS custom properties
app.js       feed + Roblox polling, rendering, settings, notifications
assets/      RC mark and the nine product logos
_headers     Cloudflare Pages / Netlify security + cache headers
```

## Why it needs no backend

Both APIs send `access-control-allow-origin: *`, so the browser can call them
directly:

- `https://weao.xyz/api/status/exploits` — the nine products
- `https://clientsettingscdn.roblox.com/v2/client-version/WindowsPlayer` — the
  live Roblox build, polled every 20 s (same cadence as the desktop and APK)

The one thing a page cannot do is set the `WEAO-3PService` user agent that WEAO
asks tools to send. Verified that the feed still answers `200` with a normal
browser UA, so no proxy is needed. If WEAO ever starts rejecting browser UAs, add
a single Cloudflare Function proxy — that is the only piece of server this would
ever need.

## Deploy

**Cloudflare Pages (free, no card):**

```
npx wrangler pages deploy web --project-name rocheats-monitor
```

or drag the `web` folder onto the Cloudflare dashboard's Pages uploader.
Netlify, Vercel and GitHub Pages all serve it unchanged — it is plain static files,
so drop the `_headers` file if the host does not understand it.

**Locally:**

```
cd web && python3 -m http.server 8099     # then open http://localhost:8099
```

`?demo=1` renders sample data, including a product deliberately lagging the Roblox
build, so the amber states are visible without waiting for real changes.

## What came across from the desktop build

Rows are inert (nothing opens on click), the four hero counts including
*behind Roblox*, the `ROBLOX 0.747.0.7471481 · 8 of 8 current` strip, the amber `▲`
on a lagging row, filter chips, and the settings sheet with seven scenes, six accent
presets, glass/flat, check interval, and notifications. Settings persist in
`localStorage`.

## Honest limits of the web version

- **It only checks while the tab is open.** A browser page cannot poll in the
  background; the Android app and the Windows tray build exist for that. Alerts
  arrive while the tab is open and hidden at most.
- Notifications need permission, and browsers refuse it inside cross-origin
  iframes — so the notification switch only works when the page is opened directly
  or served from its own origin.
- Rate limits are WEAO's, shared per IP: the site polls no more often than the
  chosen interval (default 5 minutes) plus one Roblox request every 20 s.
