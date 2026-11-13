# RoCheats Monitor — website

A static RoCheats status monitor with live WEAO status and Roblox client updates. No server or build step is required.

- `index.html` — page markup
- `styles.css` — responsive appearance and animations
- `app.js` — status polling, filters, settings and music control
- `assets/` — logos and background audio
- `_headers` — optional headers for compatible hosts

## Run locally

From the repository root:

```sh
python3 -m http.server 8099
```

Open `http://localhost:8099`. Add `?demo=1` to preview with sample data. Checks and notifications run while the browser tab is open; notification permissions depend on the browser.

## Deploy

The website files live at the repository root. Serve them as static files, for example with GitHub Pages, Cloudflare Pages, Netlify or Vercel. The GitHub Actions workflow packages a web-only ZIP.

Status information comes from WEAO and Roblox. RoCheats store links carry a referral code; pricing and checkout are handled by RoCheats.
