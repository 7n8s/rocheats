/* RoCheats Monitor — web edition.
 *
 * Same nine products, same WEAO feed and the same Roblox build watch as the
 * desktop and Android builds, with no server behind it: the site is static and
 * both APIs send `access-control-allow-origin: *`, so the browser talks to them
 * directly.
 */
(() => {
  'use strict';

  const WEAO = 'https://weao.xyz/api/status/exploits';
  const ROBLOX = 'https://clientsettingscdn.roblox.com/v2/client-version/WindowsPlayer';

  const PRODUCTS = [
    { key: 'volt', name: 'VOLT', hue: 250 },
    { key: 'wave', name: 'WAVE', hue: 206 },
    { key: 'potassium', name: 'POTASSIUM', hue: 258 },
    { key: 'isaeva', name: 'ISAEVA', hue: 230 },
    { key: 'matcha', name: 'MATCHA', hue: 214 },
    { key: 'serotonin', name: 'SEROTONIN', hue: 244 },
    { key: 'codex', name: 'CODEX', hue: 254 },
    { key: 'lucid', name: 'LUCID', hue: 238 },
    { key: 'axis', name: 'AXIS', hue: 210 },
  ];

  const SCENES = [
    { id: 'aurora', label: 'Aurora', a: 'rgba(125,238,255,.17)', b: 'rgba(91,157,255,.10)' },
    { id: 'midnight', label: 'Midnight', a: 'rgba(70,110,220,.22)', b: 'rgba(40,60,150,.14)' },
    { id: 'ember', label: 'Ember', a: 'rgba(255,122,92,.18)', b: 'rgba(160,60,40,.12)' },
    { id: 'forest', label: 'Forest', a: 'rgba(80,200,150,.16)', b: 'rgba(30,90,70,.12)' },
    { id: 'carbon', label: 'Carbon', a: 'rgba(180,190,195,.09)', b: 'rgba(60,66,70,.10)' },
    { id: 'ocean', label: 'Ocean', a: 'rgba(60,160,200,.18)', b: 'rgba(20,60,90,.14)' },
    { id: 'ice', label: 'Ice', a: 'rgba(150,200,215,.16)', b: 'rgba(60,90,100,.12)' },
  ];

  const ACCENTS = ['#7deeff', '#5b9dff', '#8a6bff', '#6fe7c4', '#f5c46c', '#ff8fa3'];
  const INTERVALS = [1, 2, 5, 10, 15, 30];
  const ROBLOX_TICK = 20000;   // Roblox ships builds without warning

  // ------------------------------------------------------------------ state
  const state = {
    filter: 0,
    items: new Map(),
    roblox: { version: '', upload: '', checkedAt: 0 },
    checking: false,
    checkedAt: 0,
    nextAt: 0,
    error: '',
    demo: new URLSearchParams(location.search).has('demo'),
    flash: new Set(),
    newBuild: false,
  };

  const settings = (() => {
    const fallback = {
      scene: 'aurora', accent: '#7deeff', theme: 'glass',
      interval: 5, notifications: false,
    };
    try {
      return Object.assign(fallback, JSON.parse(localStorage.getItem('rcm.web') || '{}'));
    } catch (e) {
      return fallback;
    }
  })();

  const saveSettings = () => {
    try { localStorage.setItem('rcm.web', JSON.stringify(settings)); } catch (e) { /* private mode */ }
  };

  const $ = (sel) => document.querySelector(sel);
  const el = (tag, cls, text) => {
    const n = document.createElement(tag);
    if (cls) n.className = cls;
    if (text != null) n.textContent = text;
    return n;
  };

  // ------------------------------------------------------------------- time
  const ago = (ms) => {
    const d = Math.max(0, (Date.now() - ms) / 1000);
    if (d < 60) return 'just now';
    if (d < 3600) return Math.floor(d / 60) + 'm ago';
    if (d < 86400) return Math.floor(d / 3600) + 'h ago';
    if (d < 86400 * 30) return Math.floor(d / 86400) + 'd ago';
    if (d < 86400 * 365) return Math.floor(d / (86400 * 30)) + 'mo ago';
    return Math.floor(d / (86400 * 365)) + 'y ago';
  };

  const clock = (ms) => new Date(ms).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });

  /** "11/11/2026 at 11:56 PM UTC" -> epoch ms (0 when unreadable). */
  const parseWeaoDate = (s) => {
    const m = /(\d{1,2})\/(\d{1,2})\/(\d{4})\s*(?:at\s*)?(\d{1,2}):(\d{2})\s*([AaPp][Mm])?/.exec(s || '');
    if (!m) return 0;
    let [, mo, d, y, h, mi, ap] = m;
    mo = +mo; d = +d; y = +y; h = +h; mi = +mi;
    if (ap) {
      const pm = /p/i.test(ap);
      if (pm && h < 12) h += 12;
      if (!pm && h === 12) h = 0;
    }
    if (mo < 1 || mo > 12 || d < 1 || d > 31) return 0;
    return Date.UTC(y, mo - 1, d, h, mi);
  };

  // ------------------------------------------------------------------- feed
  async function fetchJson(url, timeout = 12000) {
    const ctrl = new AbortController();
    const timer = setTimeout(() => ctrl.abort(), timeout);
    try {
      const res = await fetch(url, { signal: ctrl.signal, cache: 'no-store', headers: { Accept: 'application/json' } });
      if (!res.ok) throw new Error('HTTP ' + res.status);
      return await res.json();
    } finally {
      clearTimeout(timer);
    }
  }

  function absorb(list) {
    const seen = new Set();
    for (const raw of list) {
      const key = String(raw.title || '').toLowerCase();
      const product = PRODUCTS.find((p) => p.key === key);
      if (!product) continue;
      const prev = state.items.get(key);
      const item = {
        live: true,
        updated: !!raw.updateStatus,
        detected: !!raw.detected,
        version: typeof raw.version === 'string' && raw.version.toLowerCase() !== 'unknown' ? raw.version : '',
        rbx: typeof raw.rbxversion === 'string' ? raw.rbxversion : '',
        at: parseWeaoDate(raw.updatedDate),
      };
      item.onLatest = !!item.rbx && !!state.roblox.upload && item.rbx === state.roblox.upload;
      if (prev && (prev.updated !== item.updated || prev.detected !== item.detected)) state.flash.add(key);
      state.items.set(key, item);
      seen.add(key);
    }
    // A product the feed stopped listing keeps its last known state but is not live.
    for (const p of PRODUCTS) {
      if (!seen.has(p.key)) {
        const prev = state.items.get(p.key);
        if (prev) state.items.set(p.key, { ...prev, live: false });
      }
    }
  }

  async function pollFeed({ silent = false } = {}) {
    if (state.checking) return;
    state.checking = true;
    state.error = '';
    if (!silent) render();
    try {
      const list = state.demo ? demoFeed() : await fetchJson(WEAO);
      if (!Array.isArray(list) || !list.length) throw new Error('empty feed');
      absorb(list);
      state.checkedAt = Date.now();
      scheduleNext();
    } catch (err) {
      state.error = err.name === 'AbortError'
        ? 'The feed took too long to answer.'
        : (err.message === 'HTTP 429'
          ? 'The feed is rate limiting us. Try again in a minute.'
          : 'Can’t reach the WEAO feed.');
    } finally {
      state.checking = false;
      render();
    }
  }

  async function pollRoblox() {
    if (state.demo) { render(); return; }
    try {
      const data = await fetchJson(ROBLOX, 8000);
      const upload = String(data.clientVersionUpload || '');
      if (!upload) return;
      const before = state.roblox.upload;
      state.roblox = { version: String(data.version || ''), upload, checkedAt: Date.now() };
      for (const item of state.items.values()) item.onLatest = !!item.rbx && item.rbx === upload;
      if (before && before !== upload) {
        state.newBuild = true;
        notify('Roblox updated',
          (state.roblox.version ? 'Now on ' + state.roblox.version + '. ' : '') +
          'Executors may need to catch up.');
        pollFeed({ silent: true });   // every currency flag just changed
      }
      render();
    } catch (e) {
      /* the strip simply keeps its last known build */
    }
  }

  function demoFeed() {
    const rows = [
      ['Volt', '1.3.22.2', true, true], ['Wave', 'NEW-1.6.2', true, true],
      ['Potassium', '2.6.2', false, true], ['Isaeva', '2.6.9', true, true],
      ['Matcha', 'v2', true, false], ['Serotonin', 'v2', false, false],
      ['Lucid', '1.0.3', true, true], ['Axis', '0.5.5-1a3bc73d0c6ed9a9', true, false],
    ];
    state.roblox = { version: '0.747.0.7471481', upload: 'version-4ea8e85481e34f46', checkedAt: Date.now() };
    return rows.map(([title, version, updated, detected], i) => ({
      title,
      version,
      updateStatus: updated,
      detected,
      // one product deliberately lags the live build, so the amber path is visible
      rbxversion: i === 1 ? 'version-0000000000000000' : 'version-4ea8e85481e34f46',
      updatedDate: new Date(Date.now() - (i + 1) * 6 * 3600e3).toUTCString(),
    }));
  }

  function scheduleNext() {
    state.nextAt = Date.now() + settings.interval * 60000;
  }

  // The meta line is the only thing that changes between checks, so it gets its
  // own update path: no rows are touched and nothing can reflow.
  function paintMeta() {
    const meta = $('#meta');
    if (state.error) {
      meta.textContent = state.error;
      meta.classList.add('is-error');
      return;
    }
    meta.classList.remove('is-error');
    const parts = [];
    if (state.checkedAt) parts.push('Checked ' + clock(state.checkedAt));
    if (state.nextAt) {
      const left = Math.max(0, state.nextAt - Date.now());
      parts.push('Next check in <span id="countdown">' + Math.floor(left / 60000) + ':' + String(Math.floor(left / 1000) % 60).padStart(2, '0') + '</span>');
    }
    if (state.demo) parts.push('sample data');
    if (!parts.length) { meta.textContent = 'Reading the feed\u2026'; return; }
    meta.innerHTML = parts.join('   \u00B7   ');
  }

  // ----------------------------------------------------------------- render
  let lastRows = '';

  function render() {
    const items = PRODUCTS.map((p) => ({ product: p, item: state.items.get(p.key) }));
    const live = items.filter((x) => x.item && x.item.live);
    const up = live.filter((x) => x.item.updated).length;
    const det = live.filter((x) => x.item.detected).length;
    const behind = live.filter((x) => x.item.rbx && state.roblox.upload && x.item.rbx !== state.roblox.upload).length;

    $('#headline').textContent = live.length ? `${up} of ${live.length} updated` : (state.checking ? 'Checking…' : 'No data yet');

    const stats = $('#stats').children;
    [up, live.length - up, det, behind].forEach((n, i) => {
      stats[i].querySelector('b').textContent = live.length ? String(n) : '—';
    });

    paintMeta();

    // Roblox strip
    const strip = $('#rbx');
    if (state.roblox.upload) {
      strip.hidden = false;
      $('#rbxVersion').textContent = state.roblox.version || state.roblox.upload.replace('version-', '');
      const cur = $('#rbxCurrent');
      cur.textContent = behind === 0
        ? `${live.length} of ${live.length} current`
        : `${behind} behind the live build`;
      cur.classList.toggle('is-behind', behind > 0);
      const dot = $('#rbxDot');
      dot.classList.toggle('is-behind', behind > 0);
      dot.classList.toggle('is-new', state.newBuild);
    } else {
      strip.hidden = true;
    }

    // pill + buttons
    $('#livePill').classList.toggle('is-stale', !!state.error);
    $('#checkBtn').classList.toggle('is-busy', state.checking);

    // rows
    const shown = items.filter((x) => {
      if (!x.item) return state.filter === 0;
      if (state.filter === 1) return x.item.live && x.item.updated;
      if (state.filter === 2) return x.item.live && !x.item.updated;
      return true;
    });

    $('#count').textContent = `${shown.length} of ${PRODUCTS.length} products`;

    const sig = JSON.stringify([
      state.filter,
      [...state.flash],
      shown.map((x) => (x.item
        ? [x.item.live, x.item.updated, x.item.detected, x.item.version, x.item.at, x.item.rbx]
        : 0)),
    ]);
    if (sig !== lastRows) {
      lastRows = sig;
      const list = $('#rows');
      list.textContent = '';
      for (const x of shown) list.appendChild(row(x.product, x.item));
    }

    $('#empty').hidden = !(state.error && !live.length);
    $('#emptyTitle').textContent = 'Can’t reach the feed';
    $('#emptyBody').textContent = state.error;

    document.documentElement.style.setProperty('--accent', settings.accent);
    // release the flash animation after one pass
    if (state.flash.size) {
      setTimeout(() => { state.flash.clear(); }, 1500);
    }
    if (state.newBuild) setTimeout(() => { state.newBuild = false; }, 3600);
  }

  function row(product, item) {
    const li = el('li', 'row');
    if (state.flash.has(product.key)) li.classList.add('is-flash');

    // tile
    const tile = el('div', 'tile');
    const img = el('img');
    img.src = `assets/logo-${product.key}.jpg`;
    img.alt = '';
    img.loading = 'lazy';
    img.addEventListener('error', () => {
      img.remove();
      const mono = el('span', 'mono', product.name.slice(0, 1));
      const h1 = (product.hue + 8) % 360;
      mono.style.setProperty('--h1', `hsl(${product.hue} 85% 66%)`);
      mono.style.setProperty('--h2', `hsl(${h1} 70% 46%)`);
      tile.appendChild(mono);
    }, { once: true });
    tile.appendChild(img);

    // name + version
    const mid = el('div');
    const nameLine = el('div', 'name-line');
    nameLine.appendChild(el('span', 'name', product.name));
    if (item && item.live && item.rbx && state.roblox.upload && item.rbx !== state.roblox.upload) {
      const flag = el('span', 'name-flag', 'BEHIND');
      flag.title = 'Supports an older Roblox build than the one live now';
      nameLine.appendChild(flag);
    }
    mid.appendChild(nameLine);

    const sub = el('div', 'sub');
    if (item && item.live) {
      sub.appendChild(el('span', 'ver', item.version || '—'));
      if (item.rbx && state.roblox.upload && item.rbx !== state.roblox.upload) {
        const lag = el('span', 'lag', '▲');
        lag.title = 'Behind the current Roblox build';
        sub.appendChild(lag);
      }
    } else if (item) {
      sub.appendChild(el('span', 'ver', 'not listed by the feed right now'));
    } else {
      sub.appendChild(el('span', 'ver', '—'));
    }
    mid.appendChild(sub);

    // state
    const stateCell = el('div', 'state');
    if (!item || !item.live) {
      stateCell.classList.add('is-none');
      stateCell.textContent = item ? 'No live source' : '—';
    } else {
      stateCell.classList.add(item.updated ? 'is-up' : 'is-down');
      stateCell.textContent = item.updated ? 'Updated' : 'Not updated';
      if (item.at) stateCell.appendChild(el('span', 'when', ago(item.at)));
    }

    // detection: a small exclamation mark on the tile's corner; hovering says "Detected"
    if (item && item.live && item.detected) {
      const badge = el('span', 'det-badge');
      badge.setAttribute('role', 'img');
      badge.setAttribute('aria-label', product.name + ' is detected');
      badge.tabIndex = 0;
      badge.appendChild(el('span', 'tip', 'Detected'));
      li.classList.add('has-badge');
      tile.appendChild(badge);
    }

    li.append(tile, mid, stateCell);
    return li;
  }

  // --------------------------------------------------------------- settings
  function applySettings() {
    document.documentElement.style.setProperty('--accent', settings.accent);
    const scene = SCENES.find((s) => s.id === settings.scene) || SCENES[0];
    const root = document.documentElement.style;
    root.setProperty('--scene-a', scene.a);
    root.setProperty('--scene-b', scene.b);
    document.body.dataset.theme = settings.theme;
    document.documentElement.dataset.theme = settings.theme;
    const meta = document.querySelector('meta[name=theme-color]');
    if (meta) meta.setAttribute('content', '#0a0c0d');
  }

  function buildSettings() {
    const scenes = $('#scenes');
    scenes.textContent = '';
    for (const s of SCENES) {
      const b = el('button', 'swatch');
      b.style.background = `radial-gradient(120% 80% at 30% 10%, ${s.a}, #101314 70%)`;
      b.appendChild(el('span', '', s.label));
      b.type = 'button';
      b.addEventListener('click', () => {
        settings.scene = s.id;
        saveSettings();
        applySettings();
        syncSettings();
      });
      b.dataset.scene = s.id;
      scenes.appendChild(b);
    }

    const accents = $('#accents');
    accents.textContent = '';
    for (const a of ACCENTS) {
      const b = el('button');
      b.type = 'button';
      b.style.background = a;
      b.style.color = a;
      b.dataset.accent = a;
      b.setAttribute('aria-label', 'Accent ' + a);
      b.addEventListener('click', () => {
        settings.accent = a;
        saveSettings();
        applySettings();
        syncSettings();
        render();
      });
      accents.appendChild(b);
    }

    const intervals = $('#intervalSeg');
    intervals.textContent = '';
    for (const m of INTERVALS) {
      const b = el('button', '', m + 'm');
      b.type = 'button';
      b.dataset.interval = String(m);
      b.addEventListener('click', () => {
        settings.interval = m;
        saveSettings();
        scheduleNext();
        syncSettings();
        render();
      });
      intervals.appendChild(b);
    }

    $('#themeSeg').addEventListener('click', (e) => {
      const b = e.target.closest('button[data-theme]');
      if (!b) return;
      settings.theme = b.dataset.theme;
      saveSettings();
      applySettings();
      syncSettings();
    });

    const toggle = $('#notifToggle');
    toggle.checked = settings.notifications;
    toggle.addEventListener('change', async () => {
      if (toggle.checked) {
        if (!('Notification' in window)) {
          toggle.checked = false;
          toast('This browser has no notification support.');
          return;
        }
        const perm = await Notification.requestPermission();
        if (perm !== 'granted') {
          toggle.checked = false;
          toast(perm === 'denied'
            ? 'Notifications are blocked for this site in your browser settings.'
            : 'Permission dismissed — tap the switch again.');
          return;
        }
        settings.notifications = true;
      } else {
        settings.notifications = false;
      }
      saveSettings();
      toast(settings.notifications ? 'Notifications on' : 'Notifications off');
    });

    $('#testNotifBtn').addEventListener('click', () => {
      if (!notify('RoCheats Monitor', 'This is a test. Real alerts look like this.')) {
        toast('Turn notifications on and allow permission first.');
      }
    });

    $('#settingsBtn').addEventListener('click', openSheet);
    $('#closeBtn').addEventListener('click', closeSheet);
    $('#scrim').addEventListener('click', closeSheet);
    document.addEventListener('keydown', (e) => {
      if (e.key === 'Escape') closeSheet();
    });
    syncSettings();
  }

  function syncSettings() {
    for (const b of document.querySelectorAll('.swatch')) {
      b.classList.toggle('is-on', b.dataset.scene === settings.scene);
    }
    for (const b of document.querySelectorAll('#accents button')) {
      b.classList.toggle('is-on', b.dataset.accent === settings.accent);
    }
    for (const b of document.querySelectorAll('#themeSeg button')) {
      b.classList.toggle('is-on', b.dataset.theme === settings.theme);
    }
    for (const b of document.querySelectorAll('#intervalSeg button')) {
      b.classList.toggle('is-on', +b.dataset.interval === settings.interval);
    }
  }

  function openSheet() {
    const sheet = $('#sheet');
    const scrim = $('#scrim');
    sheet.hidden = false;
    scrim.hidden = false;
    requestAnimationFrame(() => {
      sheet.classList.add('is-open');
      scrim.classList.add('is-open');
    });
    $('#settingsBtn').setAttribute('aria-expanded', 'true');
    document.body.classList.add('sheet-open');
  }

  function closeSheet() {
    const sheet = $('#sheet');
    const scrim = $('#scrim');
    if (sheet.hidden) return;
    sheet.classList.remove('is-open');
    scrim.classList.remove('is-open');
    $('#settingsBtn').setAttribute('aria-expanded', 'false');
    document.body.classList.remove('sheet-open');
    setTimeout(() => {
      if (!sheet.classList.contains('is-open')) {
        sheet.hidden = true;
        scrim.hidden = true;
      }
    }, 320);
  }

  // ----------------------------------------------------------- notifications
  function notify(title, body) {
    if (!settings.notifications || !('Notification' in window) || Notification.permission !== 'granted') return false;
    try {
      new Notification(title, { body, icon: 'assets/mark.png', badge: 'assets/mark.png', tag: title });
      return true;
    } catch (e) {
      return false;
    }
  }

  let toastTimer = 0;
  function toast(message) {
    const t = $('#toast');
    t.textContent = message;
    t.classList.add('is-open');
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => t.classList.remove('is-open'), 3200);
  }

  // ------------------------------------------------------------------- wiring
  $('#checkBtn').addEventListener('click', () => pollFeed());
  $('#retryBtn').addEventListener('click', () => pollFeed());
  for (const b of document.querySelectorAll('.chip')) {
    b.addEventListener('click', () => {
      state.filter = +b.dataset.filter;
      for (const o of document.querySelectorAll('.chip')) {
        const on = o === b;
        o.classList.toggle('is-on', on);
        o.setAttribute('aria-selected', String(on));
      }
      render();
    });
  }

  // ------------------------------------------------------------------- start
  applySettings();
  buildSettings();
  render();
  pollRoblox();
  pollFeed();

  setInterval(() => {
    if (document.hidden) return;
    if (state.nextAt && Date.now() >= state.nextAt) pollFeed();
    else if (state.checkedAt) paintMeta();   // tick the countdown only
  }, 1000);

  setInterval(() => {
    if (!document.hidden) pollRoblox();
  }, ROBLOX_TICK);

  document.addEventListener('visibilitychange', () => {
    if (document.hidden) return;
    if (!state.checkedAt || Date.now() - state.checkedAt > settings.interval * 60000) pollFeed();
    pollRoblox();
  });
})();
