/* ═══════════════════════════════════════════════════════════════
   ChargeControl PWA Service Worker
   ═══════════════════════════════════════════════════════════════

   目标（用户 2026-10-28 决策：砍 App，WebUI 改 PWA）：
   · 加到主屏幕后无地址栏，看起来像 App
   · 断网/模块重启时仍能打开界面（显示上次数据）
   · 不缓存 /api/*（实时数据必须每次拉新的）

   缓存策略：
   - 静态资源（html/js/css/png）→ Cache First，版本号变了才更新
   - /api/*              → Network Only（绝不缓存，否则看到过期数据）
   - 其他                → Network First，失败回退缓存
   ═══════════════════════════════════════════════════════════════ */

const VERSION = 'cc-v1.19.1';
const CACHE = `${VERSION}-static`;

/* 预缓存：这几个文件断网时必须能开 */
const PRECACHE = [
  './',
  './index.html',
  './TempCurveView.js',
  './manifest.json',
];

/* 安装：预缓存核心文件 */
self.addEventListener('install', (e) => {
  e.waitUntil(
    caches.open(CACHE).then(async (c) => {
      // 逐个加，单个失败不影响整体（比如图标可能还没生成）
      for (const url of PRECACHE) {
        try { await c.add(url); } catch (_) { /* 忽略 */ }
      }
    }).then(() => self.skipWaiting())
  );
});

/* 激活：清掉旧版本缓存 */
self.addEventListener('activate', (e) => {
  e.waitUntil(
    caches.keys().then((keys) =>
      Promise.all(keys.filter((k) => !k.startsWith(VERSION)).map((k) => caches.delete(k)))
    ).then(() => self.clients.claim())
  );
});

self.addEventListener('fetch', (e) => {
  const { request } = e;
  if (request.method !== 'GET') return;

  const url = new URL(request.url);

  /* ── API：永不缓存，直接走网络 ── */
  if (url.pathname.startsWith('/api/')) {
    e.respondWith(
      fetch(request).catch(() => new Response(
        JSON.stringify({ ok: false, err: 'offline' }),
        { status: 503, headers: { 'Content-Type': 'application/json' } }
      ))
    );
    return;
  }

  /* ── 跨域 CDN（Chart.js / Alpine / FontAwesome）：Cache First ── */
  if (url.origin !== self.location.origin) {
    e.respondWith(
      caches.open(CACHE).then(async (c) => {
        const hit = await c.match(request);
        if (hit) return hit;
        try {
          const resp = await fetch(request);
          if (resp.ok && resp.type === 'basic' || resp.type === 'cors') {
            c.put(request, resp.clone()).catch(() => {});
          }
          return resp;
        } catch (err) {
          return new Response('', { status: 504 });
        }
      })
    );
    return;
  }

  /* ── 同源静态资源：Cache First，后台静默更新 ── */
  e.respondWith(
    caches.open(CACHE).then(async (c) => {
      const hit = await c.match(request);
      const network = fetch(request).then((resp) => {
        if (resp.ok) c.put(request, resp.clone()).catch(() => {});
        return resp;
      }).catch(() => null);

      if (hit) {
        // 有缓存先返回，同时后台拉新（stale-while-revalidate）
        e.waitUntil(network);
        return hit;
      }
      const resp = await network;
      if (resp) return resp;
      // 都失败：主页回退到缓存的 index.html
      if (request.mode === 'navigate') {
        const fallback = await c.match('./index.html');
        if (fallback) return fallback;
      }
      return new Response('离线且无缓存', { status: 503, headers: { 'Content-Type': 'text/plain; charset=utf-8' } });
    })
  );
});

/* 允许页面主动更新 */
self.addEventListener('message', (e) => {
  if (e.data === 'SKIP_WAITING') self.skipWaiting();
});
