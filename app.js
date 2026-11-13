/* RoCheats Monitor — static website.
 * Live WEAO status and Roblox build checks run in the browser.
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

  // Two independent colours are rendered in both the preview and backdrop.
  const SCENES = [
    { id: 'aurora', label: 'Aurora', a: '#245f64', b: '#202b60' },
    { id: 'midnight', label: 'Midnight', a: '#202e67', b: '#241b50' },
    { id: 'ember', label: 'Ember', a: '#803b27', b: '#451c29' },
    { id: 'forest', label: 'Forest', a: '#246143', b: '#1b3834' },
    { id: 'carbon', label: 'Carbon', a: '#414f4b', b: '#1b2527' },
    { id: 'ocean', label: 'Ocean', a: '#1b627a', b: '#1d315f' },
    { id: 'ice', label: 'Ice', a: '#47808c', b: '#2a4962' },
    { id: 'orchid', label: 'Orchid', a: '#663e78', b: '#342455' },
    { id: 'rose', label: 'Rose', a: '#833b5b', b: '#49283d' },
    { id: 'sunset', label: 'Sunset', a: '#9b5029', b: '#652c4c' },
    { id: 'lagoon', label: 'Lagoon', a: '#137c77', b: '#174b5a' },
    { id: 'moss', label: 'Moss', a: '#596c32', b: '#234632' },
    { id: 'violet', label: 'Violet', a: '#4d3d87', b: '#38295e' },
    { id: 'copper', label: 'Copper', a: '#775039', b: '#422d2b' },
    { id: 'slate', label: 'Slate', a: '#385d69', b: '#2a354f' },
    { id: 'noir', label: 'Noir', a: '#303c37', b: '#151e1d' },
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
      density: 'comfortable', strength: 100, reducedMotion: false, sceneColor: '#258d91',
      alerts: { updates: true, versions: true, detection: true, roblox: true },
    };
    try {
      const saved = JSON.parse(localStorage.getItem('rcm.web') || '{}');
      return { ...fallback, ...saved, alerts: { ...fallback.alerts, ...(saved.alerts || {}) } };
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

  // Reversible viewport fades. Content stays visible when the browser does
  // not support IntersectionObserver or reduced motion is enabled.
  let revealObserver = null, rowRevealObserver = null, headerRevealObserver = null;
  function observeReveal(node, key, delay = 0, observer = revealObserver) {
    if (!node || !observer) return;
    node.dataset.revealKey = key;
    node.style.setProperty('--reveal-delay', `${delay}ms`);
    node.classList.add('reveal-pending');
    observer.observe(node);
  }
  function setupReveals() {
    if (!('IntersectionObserver' in window)) return;
    const revealVisible = (entries, observer) => {
      for (const entry of entries) {
        const visible = entry.isIntersecting && entry.intersectionRatio >= observer.thresholds[0];
        // Exiting above moves up slightly; below enters from below.
        const above = !visible && entry.boundingClientRect.bottom <= (entry.rootBounds?.top ?? 0) + entry.boundingClientRect.height * observer.thresholds[0];
        entry.target.classList.toggle('is-above', above);
        entry.target.classList.toggle('is-revealed', visible);
      }
    };
    // Use viewport-height pixels: percentage root margins resolve against
    // width, which would hide content too early on wide desktop screens.
    const createViewportObservers = () => {
      if (revealObserver) revealObserver.disconnect();
      if (rowRevealObserver) rowRevealObserver.disconnect();
      const h = window.innerHeight;
      revealObserver = new IntersectionObserver(revealVisible,
        { threshold: 0.06, rootMargin: `-${Math.round(h * .20)}px 0px -${Math.round(h * .05)}px 0px` });
      rowRevealObserver = new IntersectionObserver(revealVisible,
        { threshold: 0.18, rootMargin: `-${Math.round(h * .28)}px 0px -${Math.round(h * .09)}px 0px` });
    };
    createViewportObservers();
    // The header starts at the very top and must remain visible on page load.
    headerRevealObserver = new IntersectionObserver(revealVisible,
      { threshold: 0.06, rootMargin: '0px' });
    window.addEventListener('resize', () => {
      createViewportObservers();
      document.querySelectorAll('[data-reveal-key]').forEach((node) => {
        const key = node.dataset.revealKey;
        if (key === 'header' || key === 'intro') return;
        (key.startsWith('row:') || key.startsWith('faq:') ? rowRevealObserver : revealObserver).observe(node);
      });
    }, { passive: true });
    observeReveal($('.titlebar'), 'header', 0, headerRevealObserver);
    // The introductory title must also be fully visible at the top of the page.
    observeReveal($('.page-intro'), 'intro', 70, headerRevealObserver);
    [
      ['.hero', 'overview', 140], ['.list-heading', 'list-heading', 190],
      ['.toolbar', 'filters', 240], ['.shop-heading', 'shop-heading', 0],
      ['.shop-all', 'shop-all', 0], ['.shop-disclaimer', 'shop-note', 0],
      ['.faq-heading', 'faq-heading', 0], ['.faq-important', 'faq-important', 0],
      ['.foot', 'footer', 0],
    ].forEach(([selector, key, delay]) => observeReveal($(selector), key, delay));
    document.querySelectorAll('.stats li').forEach((stat, i) =>
      observeReveal(stat, `stat:${i}`, 180 + i * 60));
    document.querySelectorAll('.chip').forEach((chip, i) =>
      observeReveal(chip, `filter:${i}`, 90 + i * 60));
    observeReveal($('#rbx'), 'roblox-build', 160);
    document.querySelectorAll('.shop-card').forEach((card, i) =>
      observeReveal(card, `shop:${card.dataset.shop}`, i * 80));
    document.querySelectorAll('.faq-card').forEach((card, i) =>
      observeReveal(card, `faq:${i}`, (i % 2) * 75, rowRevealObserver));
  }

  // Keep the section currently occupying the middle of the screen intact.
  // Items below the viewport still reveal individually as they come into view.
  function setupActiveSection() {
    const sections = [...document.querySelectorAll('.checker-section, .shop-section, .faq-section')];
    let active = null, frame = 0;
    const update = () => {
      frame = 0;
      const middle = innerHeight / 2;
      let next = sections.find((section) => {
        const r = section.getBoundingClientRect();
        return r.top <= middle && r.bottom > middle;
      });
      if (!next) {
        next = sections.reduce((best, section) => {
          const r = section.getBoundingClientRect();
          const visible = Math.max(0, Math.min(innerHeight, r.bottom) - Math.max(0, r.top));
          return visible > best.visible ? { section, visible } : best;
        }, { section: null, visible: 0 }).section;
      }
      if (next === active) return;
      if (active) active.classList.remove('is-active-section');
      active = next;
      if (active) active.classList.add('is-active-section');
    };
    const schedule = () => { if (!frame) frame = requestAnimationFrame(update); };
    window.addEventListener('scroll', schedule, { passive: true });
    window.addEventListener('resize', schedule, { passive: true });
    update();
  }

  // Wheel smoothing is used only for fine-pointer desktop input. Touch,
  // keyboard, zoom, modal scrolling and reduced-motion remain native.
  let scrollFrame = 0, scrollTarget = 0, scrollPosition = 0, scrollTime = 0;
  const motionPreference = window.matchMedia('(prefers-reduced-motion: reduce)');
  const finePointer = window.matchMedia('(pointer: fine)');
  function stopSmoothScroll() {
    if (scrollFrame) cancelAnimationFrame(scrollFrame);
    scrollFrame = 0;
    scrollTime = 0;
    document.documentElement.classList.remove('wheel-smoothing');
  }
  function smoothScrollFrame(time) {
    if (settings.reducedMotion || motionPreference.matches || !finePointer.matches) {
      stopSmoothScroll(); return;
    }
    const limit = Math.max(0, document.documentElement.scrollHeight - window.innerHeight);
    scrollTarget = Math.max(0, Math.min(limit, scrollTarget));
    const elapsed = scrollTime ? Math.min(40, Math.max(1, time - scrollTime)) : 16;
    scrollTime = time;
    scrollPosition += (scrollTarget - scrollPosition) * (1 - Math.exp(-elapsed / 105));
    if (Math.abs(scrollTarget - scrollPosition) < 0.5) {
      window.scrollTo(0, scrollTarget);
      stopSmoothScroll(); return;
    }
    window.scrollTo(0, scrollPosition);
    scrollFrame = requestAnimationFrame(smoothScrollFrame);
  }
  function nestedCanScroll(node, delta) {
    for (let n = node instanceof Element ? node : null; n && n !== document.body; n = n.parentElement) {
      if (n.closest('.sheet')) return true;
      const style = getComputedStyle(n);
      if (/(auto|scroll)/.test(style.overflowY) && n.scrollHeight > n.clientHeight + 1 &&
          ((delta < 0 && n.scrollTop > 0) || (delta > 0 && n.scrollTop + n.clientHeight < n.scrollHeight - 1))) return true;
    }
    return false;
  }
  window.addEventListener('wheel', (event) => {
    if (!event.cancelable || event.ctrlKey || event.metaKey || event.shiftKey || event.altKey ||
        !finePointer.matches || motionPreference.matches || settings.reducedMotion ||
        Math.abs(event.deltaX) > Math.abs(event.deltaY) || !event.deltaY ||
        nestedCanScroll(event.target, event.deltaY)) return;
    const limit = Math.max(0, document.documentElement.scrollHeight - window.innerHeight);
    if (!limit) return;
    const delta = event.deltaY * (event.deltaMode === 1 ? 16 : event.deltaMode === 2 ? window.innerHeight : 1);
    if (!scrollFrame) { scrollPosition = window.scrollY; scrollTarget = scrollPosition; }
    const next = Math.max(0, Math.min(limit, scrollTarget + delta));
    if (next === scrollTarget) return;
    event.preventDefault();
    scrollTarget = next;
    if (!scrollFrame) {
      document.documentElement.classList.add('wheel-smoothing');
      scrollFrame = requestAnimationFrame(smoothScrollFrame);
    }
  }, { passive: false });
  window.addEventListener('keydown', stopSmoothScroll, { passive: true });
  window.addEventListener('touchstart', stopSmoothScroll, { passive: true });
  window.addEventListener('pointerdown', stopSmoothScroll, { passive: true });

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
      // per-event alerts: only on a real transition, never on the first poll
      if (prev) {
        if (settings.alerts.updates && prev.updated !== item.updated) {
          notify(product.name + (item.updated ? ' updated' : ' broke'),
            item.updated ? 'Back on the current build.' : 'Marked as not updated by the feed.');
        }
        if (settings.alerts.versions && item.version && prev.version !== item.version) {
          notify(product.name + ' ' + item.version,
            'New version, previously ' + (prev.version || 'unknown') + '.');
        }
        if (settings.alerts.detection && prev.detected !== item.detected) {
          notify(product.name + (item.detected ? ' detected' : ' no longer detected'),
            item.detected ? 'Hyperion is flagging it right now.' : 'The flag has cleared.');
        }
      }
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
        if (settings.alerts.roblox) notify('Roblox updated',
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
    const livePill = $('#livePill');
    livePill.classList.toggle('is-stale', !!state.error);
    livePill.lastChild.textContent = state.demo ? 'DEMO' : (state.error ? 'OFFLINE' : (state.checkedAt ? 'LIVE' : 'CHECKING'));
    $('#checkBtn').classList.toggle('is-busy', state.checking);

    renderShop();

    // rows
    const shown = items.filter((x) => {
      if (!x.item) return state.filter === 0 || (state.filter === 1 && x.product.key === 'codex');
      if (state.filter === 1) return (x.item.live && x.item.updated) || (x.product.key === 'codex' && !x.item.live);
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
      // Carry the visual state across feed refreshes so a polling update does
      // not make an already-visible row flash or replay its entrance.
      const previous = new Map([...list.querySelectorAll('[data-reveal-key]')].map((n) =>
        [n.dataset.revealKey, { visible: n.classList.contains('is-revealed'), above: n.classList.contains('is-above') }]));
      if (rowRevealObserver) list.querySelectorAll('[data-reveal-key]').forEach((n) => rowRevealObserver.unobserve(n));
      list.textContent = '';
      for (const [i, x] of shown.entries()) {
        const item = row(x.product, x.item);
        const key = `row:${x.product.key}`;
        const prior = previous.get(key);
        if (prior && prior.visible) item.classList.add('is-revealed');
        if (prior && prior.above) item.classList.add('is-above');
        list.appendChild(item);
        observeReveal(item, key, Math.min(i % 3, 2) * 45, rowRevealObserver);
      }
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

  function renderShop() {
    for (const card of document.querySelectorAll('[data-shop]')) {
      const item = state.items.get(card.dataset.shop);
      const live = !!(item && item.live);
      const status = card.querySelector('.shop-status');
      status.textContent = live ? (item.updated ? 'Updated' : 'Not updated') : (state.checking ? 'Checking status' : 'No live data');
      status.classList.toggle('is-up', live && item.updated);
      status.classList.toggle('is-down', live && !item.updated);
    }
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
    } else if (product.key === 'codex') {
      // WEAO does not list Codex and codex.lol publishes no feed or API
      sub.appendChild(el('span', 'ver', 'Unverified · no public version feed'));
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
      if (product.key === 'codex') {
        stateCell.textContent = 'Updated';
        stateCell.classList.add('is-unverified');
        stateCell.appendChild(el('span', 'when', 'Unverified'));
      } else {
        stateCell.textContent = item ? 'No live source' : '—';
      }
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
    const customColour = /^#[0-9a-f]{6}$/i.test(settings.sceneColor || '') ? settings.sceneColor : '#258d91';
    root.setProperty('--scene-a', settings.scene === 'custom-color' ? `color-mix(in srgb, ${customColour} 45%, #0a1211)` : scene.a);
    root.setProperty('--scene-b', settings.scene === 'custom-color' ? `color-mix(in srgb, ${customColour} 27%, #11141e)` : scene.b);
    root.setProperty('--strength', String(Math.max(20, Math.min(100, Number(settings.strength) || 100)) / 100));
    let image = '';
    try { image = localStorage.getItem('rcm.web.image') || ''; } catch (_) {}
    // Only images we read from the local file picker are used as CSS URLs.
    const validImage = /^data:image\/(png|jpeg|webp);base64,[A-Za-z0-9+/=]+$/.test(image);
    root.setProperty('--scene-image', settings.scene === 'custom' && validImage
      ? `linear-gradient(rgba(5,12,11,.38), rgba(5,12,11,.6)), url("${image}")` : 'none');
    document.documentElement.dataset.motion = settings.reducedMotion ? 'reduced' : 'normal';
    document.body.dataset.theme = settings.theme;
    document.documentElement.dataset.theme = settings.theme;
    document.documentElement.dataset.density = settings.density || 'comfortable';
    const meta = document.querySelector('meta[name=theme-color]');
    if (meta) meta.setAttribute('content', '#0a0c0d');
  }

  function buildSettings() {
    const scenes = $('#scenes');
    scenes.textContent = '';
    for (const s of SCENES) {
      const b = el('button', 'swatch');
      b.style.background = `linear-gradient(135deg, ${s.a}, ${s.b})`;
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
    const custom = el('button', 'swatch', '');
    custom.type = 'button';
    custom.dataset.scene = 'custom';
    custom.appendChild(el('span', '', 'Your image'));
    custom.addEventListener('click', () => {
      let image = '';
      try { image = localStorage.getItem('rcm.web.image') || ''; } catch (_) {}
      if (!image) { $('#imageInput').click(); return; }
      settings.scene = 'custom'; saveSettings(); applySettings(); syncSettings();
    });
    scenes.appendChild(custom);

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
    // Deliberately do not close on outside click; use X or Escape.
    document.addEventListener('keydown', (e) => {
      if ($('#sheet').hidden) return;
      if (e.key === 'Escape') { closeSheet(); return; }
      if (e.key !== 'Tab') return;
      const focusable = [...$('#sheet').querySelectorAll('button:not([disabled]), input:not([disabled]):not([type=file]), a[href]')]
        .filter((node) => node.getClientRects().length);
      if (!focusable.length) return;
      const first = focusable[0], last = focusable[focusable.length - 1];
      if (e.shiftKey && (document.activeElement === first || !$('#sheet').contains(document.activeElement))) {
        e.preventDefault(); last.focus();
      } else if (!e.shiftKey && document.activeElement === last) {
        e.preventDefault(); first.focus();
      }
    });
    syncSettings();
  }

  function syncSettings() {
    for (const b of document.querySelectorAll('.swatch')) {
      const on = b.dataset.scene === settings.scene;
      b.classList.toggle('is-on', on);
      b.setAttribute('aria-pressed', String(on));
      if (b.dataset.scene === 'custom') {
        let image = '';
        try { image = localStorage.getItem('rcm.web.image') || ''; } catch (_) {}
        b.style.background = /^data:image\/(png|jpeg|webp);base64,[A-Za-z0-9+/=]+$/.test(image)
          ? `center / cover url("${image}")` : 'linear-gradient(135deg,#334b43,#151f1d)';
        $('#removeImageBtn').hidden = !image;
      }
    }
    for (const b of document.querySelectorAll('#accents button')) {
      b.classList.toggle('is-on', b.dataset.accent === settings.accent);
      b.setAttribute('aria-pressed', String(b.dataset.accent === settings.accent));
    }
    for (const b of document.querySelectorAll('#themeSeg button')) {
      b.classList.toggle('is-on', b.dataset.theme === settings.theme);
      b.setAttribute('aria-pressed', String(b.dataset.theme === settings.theme));
    }
    for (const b of document.querySelectorAll('#intervalSeg button')) {
      b.classList.toggle('is-on', +b.dataset.interval === settings.interval);
    }
    for (const b of document.querySelectorAll('#densitySeg button')) {
      b.classList.toggle('is-on', b.dataset.density === (settings.density || 'comfortable'));
      b.setAttribute('aria-pressed', String(b.dataset.density === (settings.density || 'comfortable')));
    }
    $('#customSceneColor').value = /^#[0-9a-f]{6}$/i.test(settings.sceneColor || '') ? settings.sceneColor : '#258d91';
    $('#sceneStrength').value = settings.strength || 100;
    $('#sceneStrengthValue').textContent = (settings.strength || 100) + '%';
    $('#motionToggle').checked = !!settings.reducedMotion;
    $('#customAccent').value = /^#[0-9a-f]{6}$/i.test(settings.accent) ? settings.accent : '#7deeff';
    for (const b of document.querySelectorAll('[data-alert]')) {
      b.checked = !!settings.alerts[b.dataset.alert];
    }
  }

  $('#sceneStrength').addEventListener('input', (e) => {
    settings.strength = Number(e.target.value); saveSettings(); applySettings(); syncSettings();
  });
  $('#customSceneColor').addEventListener('input', (e) => {
    settings.sceneColor = e.target.value; settings.scene = 'custom-color';
    saveSettings(); applySettings(); syncSettings();
  });
  $('#customAccent').addEventListener('input', (e) => {
    settings.accent = e.target.value; saveSettings(); applySettings(); syncSettings();
  });
  $('#motionToggle').addEventListener('change', (e) => {
    settings.reducedMotion = e.target.checked; if (settings.reducedMotion) stopSmoothScroll(); saveSettings(); applySettings();
  });
  $('#resetAppearance').addEventListener('click', () => {
    Object.assign(settings, { scene: 'aurora', accent: '#7deeff', theme: 'glass',
      density: 'comfortable', strength: 100, reducedMotion: false });
    saveSettings(); applySettings(); syncSettings(); toast('Appearance reset');
  });
  $('#imageInput').addEventListener('change', (e) => {
    const file = e.target.files && e.target.files[0];
    if (!file) return;
    if (!['image/png', 'image/jpeg', 'image/webp'].includes(file.type) || file.size > 1024 * 1024) {
      toast('Choose a PNG, JPG or WebP under 1 MB.'); e.target.value = ''; return;
    }
    const reader = new FileReader();
    reader.onload = () => {
      try {
        localStorage.setItem('rcm.web.image', String(reader.result));
        settings.scene = 'custom'; saveSettings(); applySettings(); syncSettings();
        toast('Background saved in this browser');
      } catch (_) { toast('Not enough browser storage to save this image.'); }
      e.target.value = '';
    };
    reader.onerror = () => { toast('Could not read that image.'); e.target.value = ''; };
    reader.readAsDataURL(file);
  });
  $('#removeImageBtn').addEventListener('click', () => {
    try { localStorage.removeItem('rcm.web.image'); } catch (_) {}
    if (settings.scene === 'custom') settings.scene = 'aurora';
    saveSettings(); applySettings(); syncSettings(); toast('Custom image removed');
  });

  for (const b of document.querySelectorAll('#densitySeg button')) {
    b.addEventListener('click', () => {
      settings.density = b.dataset.density;
      saveSettings();
      applySettings();
      syncSettings();
    });
  }

  for (const i of document.querySelectorAll('[data-alert]')) {
    i.addEventListener('change', () => {
      settings.alerts[i.dataset.alert] = i.checked;
      saveSettings();
    });
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
    $('#closeBtn').focus();
  }

  function closeSheet() {
    const sheet = $('#sheet');
    const scrim = $('#scrim');
    if (sheet.hidden) return;
    sheet.classList.remove('is-open');
    scrim.classList.remove('is-open');
    $('#settingsBtn').setAttribute('aria-expanded', 'false');
    document.body.classList.remove('sheet-open');
    $('#settingsBtn').focus();
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

  // Optional local background music. Browsers require a user gesture to play;
  // the track is not downloaded until the listener asks for it.
  const backgroundMusic = $('#backgroundMusic');
  const musicControl = $('#musicControl');
  backgroundMusic.volume = 0.18;
  function syncMusicControl() {
    const playing = !backgroundMusic.paused && !backgroundMusic.ended;
    musicControl.setAttribute('aria-pressed', String(playing));
    musicControl.setAttribute('aria-label', `${playing ? 'Pause' : 'Play'} background music: Whole Team Straight by ovine hall`);
    $('#musicLabel').textContent = playing ? 'Music on' : 'Music off';
  }
  backgroundMusic.addEventListener('play', syncMusicControl);
  backgroundMusic.addEventListener('pause', syncMusicControl);
  musicControl.addEventListener('click', async () => {
    if (!backgroundMusic.paused) { backgroundMusic.pause(); syncMusicControl(); return; }
    if (!backgroundMusic.hasAttribute('src')) backgroundMusic.src = backgroundMusic.dataset.src;
    try { await backgroundMusic.play(); syncMusicControl(); }
    catch (_) { syncMusicControl(); toast('Could not play the music in this browser.'); }
  });

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
  setupReveals();
  setupActiveSection();
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
