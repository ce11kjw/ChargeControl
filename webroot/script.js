/* ==========================================================
   充电控制 – 前端脚本
   原生 JS，无框架依赖。
   ========================================================== */

'use strict';

/* ── API 基地址 ────────────────────────────────────────────
   默认指向本机 127.0.0.1:8080。
   局域网访问时可通过以下方式覆盖：
     - URL 参数：http://<设备IP>:8080/?api=http://<设备IP>:8080
     - 浏览器控制台：localStorage.setItem('cc-api-base', 'http://<设备IP>:8080')
   ──────────────────────────────────────────────────────── */
const API_BASE = (() => {
  // Validate that a string is a safe http/https origin; return null if invalid.
  function sanitizeOrigin(val) {
    if (!val) return null;
    try {
      const u = new URL(val);
      if (u.protocol !== 'http:' && u.protocol !== 'https:') return null;
      return u.origin; // strips path, query, hash and normalises
    } catch { return null; }
  }
  const params = new URLSearchParams(window.location.search);
  const fromQuery = sanitizeOrigin(params.get('api'));
  if (fromQuery) {
    localStorage.setItem('cc-api-base', fromQuery);
    return fromQuery;
  }
  const stored = sanitizeOrigin(localStorage.getItem('cc-api-base'));
  if (stored) return stored;
  return 'http://127.0.0.1:8080';
})();

// 兼容 CanvasRenderingContext2D.roundRect（Chrome < 99 及旧版 WebView）
if (!CanvasRenderingContext2D.prototype.roundRect) {
  CanvasRenderingContext2D.prototype.roundRect = function (x, y, w, h, radii) {
    const r = Array.isArray(radii) ? radii[0] ?? 0 : (radii ?? 0);
    this.beginPath();
    this.moveTo(x + r, y);
    this.lineTo(x + w - r, y);
    this.quadraticCurveTo(x + w, y, x + w, y + r);
    this.lineTo(x + w, y + h - r);
    this.quadraticCurveTo(x + w, y + h, x + w - r, y + h);
    this.lineTo(x + r, y + h);
    this.quadraticCurveTo(x, y + h, x, y + h - r);
    this.lineTo(x, y + r);
    this.quadraticCurveTo(x, y, x + r, y);
    this.closePath();
    return this;
  };
}

const API = {
  status:      '/api/status',
  settings:    '/api/settings',
  config:      '/api/config',
  modes:       '/api/modes',
  charging: {
    enable:  '/api/charging/enable',
    limit:   '/api/charging/limit',
    mode:    '/api/charging/mode',
    tempChk: '/api/charging/temperature-check',
  },
  stats: {
    daily:     '/api/stats/daily',
    weekly:    '/api/stats/weekly',
    monthly:   '/api/stats/monthly',
    snapshots: '/api/stats/snapshots',
    health:    '/api/stats/health',
  },
  export: {
    csv:  '/api/export/csv',
    json: '/api/export/json',
  },
};

/* ── 工具函数 ──────────────────────────────────────────────── */

async function apiFetch(url, opts = {}) {
  const fullUrl = url.startsWith('http') ? url : API_BASE + url;
  try {
    const res = await fetch(fullUrl, {
      headers: { 'Content-Type': 'application/json' },
      ...opts,
    });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    return await res.json();
  } catch (err) {
    console.error('[API错误]', fullUrl, err.message);
    throw err;
  }
}

function post(url, body) {
  return apiFetch(url, { method: 'POST', body: JSON.stringify(body) });
}

function $(id) { return document.getElementById(id); }

function setText(id, text) {
  const el = $(id);
  if (el) el.textContent = text;
}

function showToast(msg, duration = 3000) {
  const toast = $('toast');
  toast.textContent = msg;
  toast.classList.remove('hidden');
  clearTimeout(toast._timer);
  toast._timer = setTimeout(() => toast.classList.add('hidden'), duration);
}

function showResult(id, data, isError = false) {
  const el = $(id);
  el.textContent = typeof data === 'string' ? data : JSON.stringify(data, null, 2);
  el.className = 'result-box ' + (isError ? 'error' : 'success');
  el.classList.remove('hidden');
}

/* ── 主题切换 ─────────────────────────────────────────── */

function initTheme() {
  const saved = localStorage.getItem('cc-theme') || 'dark';
  document.documentElement.setAttribute('data-theme', saved);
  $('themeToggle').textContent = saved === 'dark' ? '☀️' : '🌙';
}

$('themeToggle').addEventListener('click', () => {
  const html = document.documentElement;
  const next = html.getAttribute('data-theme') === 'dark' ? 'light' : 'dark';
  html.setAttribute('data-theme', next);
  $('themeToggle').textContent = next === 'dark' ? '☀️' : '🌙';
  localStorage.setItem('cc-theme', next);
});

/* ── 侧边栏 / 标签页导航 ─────────────────────────────── */

document.querySelectorAll('.sidebar-link').forEach(link => {
  link.addEventListener('click', e => {
    e.preventDefault();
    const tab = link.dataset.tab;
    document.querySelectorAll('.sidebar-link').forEach(l => l.classList.remove('active'));
    document.querySelectorAll('.tab-panel').forEach(p => p.classList.remove('active'));
    link.classList.add('active');
    $('tab-' + tab).classList.add('active');
    if (tab === 'stats') loadStats('daily');
    if (tab === 'config') loadConfigEditor();
    if (tab === 'modes') loadModes();
  });
});

/* ── 仪表盘 ────────────────────────────────────────────── */

let tempChartCtx, tempChartData;

function updateDashboard(battery) {
  const cap = battery.capacity ?? '—';
  setText('batteryLevel', cap + (cap !== '—' ? '%' : ''));
  setText('chargingStatus', battery.status ?? '—');
  setText('batteryTemp', battery.temperature != null ? battery.temperature + '°C' : '—');
  setText('batteryVoltage', battery.voltage_mv != null ? (battery.voltage_mv / 1000).toFixed(2) + ' V' : '—');
  setText('batteryCurrent', battery.current_ma != null ? battery.current_ma.toFixed(0) + ' mA' : '—');

  const bar = $('batteryBar');
  if (bar) bar.style.width = (cap ?? 0) + '%';
}

function updateMode(config) {
  const mode = config?.charging?.mode ?? '—';
  const meta = MODE_META[mode];
  setText('currentMode', meta ? meta.label : mode.replace('_', ' ').replace(/\b\w/g, c => c.toUpperCase()));
}

async function refreshStatus() {
  try {
    const data = await apiFetch(API.settings);
    updateDashboard(data.battery ?? {});
    updateMode(data.config ?? {});

    // 同步控制界面
    const charging = data.config?.charging ?? {};
    const toggle = $('chargingToggle');
    if (toggle) toggle.checked = data.battery?.charging_enabled !== false;

    const limitSlider = $('chargeLimitSlider');
    if (limitSlider && charging.max_limit != null) {
      limitSlider.value = charging.max_limit;
      setText('chargeLimitValue', charging.max_limit + '%');
    }

    const tempSlider = $('tempThresholdSlider');
    if (tempSlider && charging.temperature_threshold != null) {
      tempSlider.value = charging.temperature_threshold;
      setText('tempThresholdValue', charging.temperature_threshold + '°C');
    }

    const critSlider = $('tempCriticalSlider');
    if (critSlider && charging.temperature_critical != null) {
      critSlider.value = charging.temperature_critical;
      setText('tempCriticalValue', charging.temperature_critical + '°C');
    }

    $('connectionStatus').textContent = '已连接';
    $('connectionStatus').className = 'badge badge-success';
  } catch {
    $('connectionStatus').textContent = '离线';
    $('connectionStatus').className = 'badge badge-danger';
  }
}

/* ── 迷你图表（温度历史） ────────────────────────────────── */

function drawLineChart(canvasId, labels, dataset, color = '#4f8ef7', label = '') {
  const canvas = $(canvasId);
  if (!canvas) return;
  const ctx = canvas.getContext('2d');
  const W = canvas.offsetWidth;
  const H = canvas.offsetHeight || 140;
  canvas.width = W;
  canvas.height = H;

  const pad = { top: 20, right: 16, bottom: 30, left: 44 };
  const w = W - pad.left - pad.right;
  const h = H - pad.top - pad.bottom;

  ctx.clearRect(0, 0, W, H);

  if (!dataset.length) return;

  const min = Math.min(...dataset) - 2;
  const max = Math.max(...dataset) + 2;
  const range = max - min || 1;

  const xStep = w / Math.max(dataset.length - 1, 1);

  function xOf(i) { return pad.left + i * xStep; }
  function yOf(v) { return pad.top + h - ((v - min) / range) * h; }

  // 网格线
  ctx.strokeStyle = getComputedStyle(document.documentElement).getPropertyValue('--border').trim() || '#2e3350';
  ctx.lineWidth = 1;
  for (let i = 0; i <= 4; i++) {
    const y = pad.top + (h / 4) * i;
    ctx.beginPath(); ctx.moveTo(pad.left, y); ctx.lineTo(pad.left + w, y); ctx.stroke();
    const val = (max - (range / 4) * i).toFixed(1);
    ctx.fillStyle = getComputedStyle(document.documentElement).getPropertyValue('--text-muted').trim() || '#8892a4';
    ctx.font = '10px sans-serif';
    ctx.textAlign = 'right';
    ctx.fillText(val, pad.left - 4, y + 3);
  }

  // X轴标签（每约10个点一个）
  ctx.fillStyle = getComputedStyle(document.documentElement).getPropertyValue('--text-muted').trim() || '#8892a4';
  ctx.font = '10px sans-serif';
  ctx.textAlign = 'center';
  const step = Math.max(1, Math.floor(labels.length / 6));
  labels.forEach((lbl, i) => {
    if (i % step === 0) ctx.fillText(lbl, xOf(i), H - 6);
  });

  // 渐变填充
  const grad = ctx.createLinearGradient(0, pad.top, 0, pad.top + h);
  grad.addColorStop(0, color + '55');
  grad.addColorStop(1, color + '00');
  ctx.beginPath();
  ctx.moveTo(xOf(0), yOf(dataset[0]));
  dataset.forEach((v, i) => i > 0 && ctx.lineTo(xOf(i), yOf(v)));
  ctx.lineTo(xOf(dataset.length - 1), pad.top + h);
  ctx.lineTo(xOf(0), pad.top + h);
  ctx.closePath();
  ctx.fillStyle = grad;
  ctx.fill();

  // 折线
  ctx.beginPath();
  ctx.strokeStyle = color;
  ctx.lineWidth = 2;
  ctx.lineJoin = 'round';
  ctx.moveTo(xOf(0), yOf(dataset[0]));
  dataset.forEach((v, i) => i > 0 && ctx.lineTo(xOf(i), yOf(v)));
  ctx.stroke();
}

async function refreshTempChart() {
  try {
    const snaps = await apiFetch(API.stats.snapshots + '?limit=60');
    const labels = snaps.map(s => {
      const d = new Date(s.timestamp);
      return d.getHours() + ':' + String(d.getMinutes()).padStart(2, '0');
    });
    const temps = snaps.map(s => s.temperature ?? 0);
    drawLineChart('tempChart', labels, temps, '#f59e0b', '温度 °C');
  } catch { /* ignore */ }
}

async function refreshHealthCard() {
  try {
    const h = await apiFetch(API.stats.health);
    setText('healthScore', (h.estimated_health ?? '—') + (h.estimated_health != null ? '%' : ''));
    setText('healthDetail',
      `平均温度: ${h.avg_temp}°C  |  最高温度: ${h.max_temp}°C  |  充电次数: ${h.total_sessions}`);
  } catch { /* ignore */ }
}

$('refreshChart').addEventListener('click', refreshTempChart);

/* ── 控制标签页 ──────────────────────────────────────────── */

$('chargingToggle').addEventListener('change', async e => {
  try {
    const res = await post(API.charging.enable, { enabled: e.target.checked });
    showToast(res.success ? '✅ 充电已' + (e.target.checked ? '启用' : '禁用') : '❌ 操作失败');
  } catch { showToast('❌ 切换充电状态出错'); }
});

$('chargeLimitSlider').addEventListener('input', e => {
  setText('chargeLimitValue', e.target.value + '%');
});

$('applyLimitBtn').addEventListener('click', async () => {
  const limit = parseInt($('chargeLimitSlider').value);
  try {
    const res = await post(API.charging.limit, { limit });
    showToast(res.success ? `✅ 上限已设为 ${limit}%` : '❌ 操作失败');
  } catch { showToast('❌ 设置上限出错'); }
});

$('tempThresholdSlider').addEventListener('input', e => {
  setText('tempThresholdValue', e.target.value + '°C');
});

$('tempCriticalSlider').addEventListener('input', e => {
  setText('tempCriticalValue', e.target.value + '°C');
});

$('applyTempBtn').addEventListener('click', async () => {
  const threshold = parseInt($('tempThresholdSlider').value);
  const critical  = parseInt($('tempCriticalSlider').value);
  try {
    const cfg = await apiFetch(API.config);
    cfg.charging = cfg.charging || {};
    cfg.charging.temperature_threshold = threshold;
    cfg.charging.temperature_critical  = critical;
    const res = await post(API.config, cfg);
    showToast(res.success ? `✅ 温度阈值已更新` : '❌ 操作失败');
  } catch { showToast('❌ 更新温度阈值出错'); }
});

$('tempCheckBtn').addEventListener('click', async () => {
  try {
    const res = await post(API.charging.tempChk, {});
    showResult('tempCheckResult', res, false);
  } catch (e) { showResult('tempCheckResult', e.message, true); }
});

/* ── 模式标签页 ────────────────────────────────────────────── */

const MODE_META = {
  normal:       { icon: '🔋', label: '普通' },
  fast:         { icon: '⚡', label: '快充' },
  trickle:      { icon: '💧', label: '涓流' },
  power_saving: { icon: '🌿', label: '省电' },
  super_saver:  { icon: '🌱', label: '超级省电' },
};

async function loadModes() {
  try {
    const modes = await apiFetch(API.modes);
    const settings = await apiFetch(API.settings);
    const current = settings.config?.charging?.mode || 'normal';
    const grid = $('modesGrid');
    grid.innerHTML = '';
    Object.entries(modes).forEach(([key, cfg]) => {
      const meta = MODE_META[key] || { icon: '⚙️', label: key };
      const card = document.createElement('div');
      card.className = 'mode-card' + (key === current ? ' selected' : '');
      card.innerHTML = `
        <div class="mode-icon">${meta.icon}</div>
        <div class="mode-name">${meta.label}</div>
        <div class="mode-desc">${cfg.description || ''}</div>
        <div class="card-sub">${cfg.max_current_ma ? cfg.max_current_ma + ' mA' : ''}</div>`;
      card.addEventListener('click', async () => {
        try {
          const res = await post(API.charging.mode, { mode: key });
          if (res.success) {
            grid.querySelectorAll('.mode-card').forEach(c => c.classList.remove('selected'));
            card.classList.add('selected');
            showToast(`✅ 已切换模式: ${meta.label}`);
            showResult('modeSetResult', res, false);
          } else {
            showResult('modeSetResult', res, true);
          }
        } catch (e) { showResult('modeSetResult', e.message, true); }
      });
      grid.appendChild(card);
    });
  } catch (e) { $('modesGrid').textContent = '加载模式失败: ' + e.message; }
}

/* ── 统计标签页 ───────────────────────────────────────────── */

let currentPeriod = 'daily';

document.querySelectorAll('[data-period]').forEach(btn => {
  btn.addEventListener('click', () => {
    document.querySelectorAll('[data-period]').forEach(b => b.classList.remove('active'));
    btn.classList.add('active');
    currentPeriod = btn.dataset.period;
    loadStats(currentPeriod);
  });
});

const PERIOD_COLS = {
  daily:   ['day', 'sessions', 'avg_efficiency', 'total_duration_s', 'max_temp'],
  weekly:  ['week', 'sessions', 'avg_efficiency', 'total_duration_s', 'max_temp'],
  monthly: ['month', 'sessions', 'avg_efficiency', 'total_duration_s', 'max_temp'],
};

function fmtDuration(s) {
  if (s == null) return '—';
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  return h ? `${h}h ${m}m` : `${m}m`;
}

async function loadStats(period) {
  try {
    const url = API.stats[period] + (period === 'daily' ? '?days=7' : '');
    const rows = await apiFetch(url);
    const cols = PERIOD_COLS[period];

    const head = $('statsTableHead');
    const body = $('statsTableBody');
    head.innerHTML = '<tr>' + cols.map(c => `<th>${c.replace(/_/g,' ')}</th>`).join('') + '</tr>';
    body.innerHTML = rows.map(r => '<tr>' + cols.map(c => {
      let val = r[c] ?? '—';
      if (c === 'total_duration_s') val = fmtDuration(val);
      if (c === 'avg_efficiency')   val = val !== '—' ? parseFloat(val).toFixed(2) : '—';
      if (c === 'max_temp')         val = val !== '—' ? val + '°C' : '—';
      return `<td>${val}</td>`;
    }).join('') + '</tr>').join('') || `<tr><td colspan="${cols.length}" style="color:var(--text-muted)">暂无数据</td></tr>`;

    // 绘制柱状图（每期充电次数）
    const labels   = rows.map(r => r[cols[0]]);
    const sessions = rows.map(r => r.sessions ?? 0);
    drawBarChart('statsChart', labels, sessions, '#4f8ef7');
  } catch (e) { $('statsTableBody').innerHTML = `<tr><td colspan="5">${e.message}</td></tr>`; }
}

function drawBarChart(canvasId, labels, dataset, color = '#4f8ef7') {
  const canvas = $(canvasId);
  if (!canvas) return;
  const ctx = canvas.getContext('2d');
  const W = canvas.offsetWidth;
  const H = canvas.offsetHeight || 180;
  canvas.width = W;
  canvas.height = H;

  const pad = { top: 20, right: 16, bottom: 30, left: 40 };
  const w = W - pad.left - pad.right;
  const h = H - pad.top - pad.bottom;

  ctx.clearRect(0, 0, W, H);
  if (!dataset.length) return;

  const max = Math.max(...dataset, 1);
  const barW = (w / dataset.length) * 0.6;
  const gapW = (w / dataset.length) * 0.4;

  dataset.forEach((v, i) => {
    const bh = (v / max) * h;
    const x = pad.left + i * (w / dataset.length) + gapW / 2;
    const y = pad.top + h - bh;

    ctx.fillStyle = color + 'cc';
    ctx.beginPath();
    ctx.roundRect(x, y, barW, bh, [4, 4, 0, 0]);
    ctx.fill();

    ctx.fillStyle = getComputedStyle(document.documentElement).getPropertyValue('--text-muted').trim() || '#8892a4';
    ctx.font = '10px sans-serif';
    ctx.textAlign = 'center';
    ctx.fillText(labels[i], x + barW / 2, H - 6);
  });
}

/* ── 导出 ───────────────────────────────────────────────── */

$('exportCsv').addEventListener('click', () => {
  try {
    const exportUrl = new URL(API.export.csv, API_BASE);
    if (exportUrl.protocol === 'http:' || exportUrl.protocol === 'https:') {
      window.location.href = exportUrl.toString();
    }
  } catch { /* ignore invalid URL */ }
});

$('exportJson').addEventListener('click', async () => {
  const data = await apiFetch(API.export.json);
  const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = 'charging_data.json';
  a.click();
});

/* ── 配置编辑器 ────────────────────────────────────────── */

async function loadConfigEditor() {
  try {
    const cfg = await apiFetch(API.config);
    $('configEditor').value = JSON.stringify(cfg, null, 2);
  } catch (e) { $('configEditor').value = '// 加载配置出错: ' + e.message; }
}

$('loadConfigBtn').addEventListener('click', loadConfigEditor);

$('saveConfigBtn').addEventListener('click', async () => {
  try {
    const cfg = JSON.parse($('configEditor').value);
    const res = await post(API.config, cfg);
    showResult('configResult', res.success ? '配置保存成功！' : '保存失败。', !res.success);
    showToast(res.success ? '✅ 配置已保存' : '❌ 保存失败');
  } catch (e) { showResult('configResult', 'JSON 解析错误: ' + e.message, true); }
});

/* ── 预设 ──────────────────────────────────────────────── */

const PRESETS = {
  night:  { mode: 'trickle', max_limit: 80,  temperature_threshold: 38, temperature_critical: 43 },
  work:   { mode: 'normal',  max_limit: 90,  temperature_threshold: 40, temperature_critical: 45 },
  travel: { mode: 'fast',    max_limit: 100, temperature_threshold: 42, temperature_critical: 47 },
  save:   { mode: 'trickle', max_limit: 60,  temperature_threshold: 36, temperature_critical: 42 },
};

document.querySelectorAll('.preset-btn').forEach(btn => {
  btn.addEventListener('click', async () => {
    const p = PRESETS[btn.dataset.preset];
    if (!p) return;
    try {
      const cfg = await apiFetch(API.config);
      cfg.charging = { ...cfg.charging, ...p };
      const res = await post(API.config, cfg);
      if (res.success) {
        await post(API.charging.mode, { mode: p.mode });
        await post(API.charging.limit, { limit: p.max_limit });
        showToast('✅ 已应用预设: ' + btn.dataset.preset);
        loadConfigEditor();
      }
    } catch (e) { showToast('❌ ' + e.message); }
  });
});

/* ── 初始化与轮询 ───────────────────────────────────────── */

initTheme();
refreshStatus();
refreshTempChart();
refreshHealthCard();

setInterval(refreshStatus, 10000);
setInterval(refreshTempChart, 30000);
setInterval(refreshHealthCard, 60000);
