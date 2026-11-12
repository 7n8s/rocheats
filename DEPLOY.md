# Putting the site online yourself

Three free routes, easiest first. The site is plain static files — no build step, no
server, no account needed for the app itself.

---

## 1. Cloudflare Pages — dashboard upload (no terminal, ~3 minutes)

1. Sign up at <https://dash.cloudflare.com/sign-up> (free, no card).
2. Left sidebar → **Workers & Pages** → **Create** → **Pages** → **Upload assets**.
3. Project name: `rocheats-monitor` (this becomes the URL:
   `rocheats-monitor.pages.dev`).
4. Drag the whole **`web`** folder into the uploader. Upload.
5. You get `https://rocheats-monitor.pages.dev` in about 30 seconds.

To update later: same project → **Create new deployment** → drag the folder again.
Old versions stay available and roll back in one click.

> Drag the folder itself, not the zip. The uploader wants `index.html`,
> `styles.css`, `app.js` and `assets/` at the top level.

---

## 2. Cloudflare Pages — from the command line

```bash
npx wrangler login                       # opens the browser once
npx wrangler pages deploy web --project-name rocheats-monitor
```

Every later deploy is the same one-liner. `_headers` is picked up automatically.

---

## 3. GitHub Pages — free forever, and it doubles as your repo

This is the route that also gives you the source repo you wanted.

```bash
# from the folder above the project
cd /path/to && ls RoCheatsMonitor
gh repo create rocheats-monitor --public --source=RoCheatsMonitor --push
```

Then on GitHub: **Settings → Pages → Build and deployment**

- Source: **Deploy from a branch**
- Branch: `main` · Folder: **`/web`**
- Save.

Live at `https://<your-username>.github.io/rocheats-monitor/` in ~1 minute. Every push
to `main` redeploys.

> GitHub Pages ignores `_headers` (it is a Cloudflare/Netlify file). It also serves
> from a subfolder, so all paths in this site are relative — which is why they work.

---

## Also fine, if you prefer

- **Netlify**: drag `web` onto <https://app.netlify.com/drop>. Instant URL, `_headers`
  respected.
- **Vercel**: `npx vercel --prod web`.

---

## Making it *yours*

Everything you would want to change is near the top of `web/app.js`:

| What | Where |
|---|---|
| Product list, order, monogram hues | `PRODUCTS` array |
| Background scenes | `SCENES` array (add your own gradient) |
| Accent presets | `ACCENTS` array |
| Default check interval, notification default | `settings` fallback object |
| Site title, description | `index.html` `<title>` / `<meta name="description">` |
| Colours and type | `web/styles.css` `:root` block — the same tokens the desktop app uses |
| Logo and product art | `web/assets/` — same filenames, any image |

To rename the app: `index.html` (`.brand h1`, `<title>`) and `assets/mark.png`.

---

## Custom domain

1. Buy a domain anywhere (~£8/year) or use a subdomain you own.
2. **Cloudflare Pages** → your project → **Custom domains** → **Set up a domain**.
3. If the domain's DNS is on Cloudflare: it adds the record for you. Otherwise add a
   `CNAME` at your registrar pointing to `rocheats-monitor.pages.dev`.
4. Certificate is issued automatically; HTTPS is on within a couple of minutes.

For GitHub Pages: repo **Settings → Pages → Custom domain**, then a `CNAME` record at
your registrar pointing to `<username>.github.io`.

---

## Checklist before you share the link

- [ ] Open it on a phone — the live URL, not localhost.
- [ ] Tap **Check now**; the hero counts and the Roblox strip should fill in.
- [ ] Open settings, switch accent and scene, reload — choices should survive.
- [ ] Turn notifications on and hit **Send a test notification**.
- [ ] Check it on mobile data (not just wifi) — that is what catches a network problem.

## If something breaks

| Symptom | Cause |
|---|---|
| Hero stuck on "Can't reach the WEAO feed" | WEAO rate limit for that IP, or the API is down. Open <https://weao.xyz/api/status/exploits> directly to check. |
| Product logos missing | `assets/` did not upload. They must sit next to `index.html`. |
| Notifications switch bounces back off | Browser blocked permission, or the page is in a cross-origin iframe. Open the site directly. |
| Blank page | Open the browser console (F12). A 404 on `app.js` means the folder uploaded one level too deep. |
