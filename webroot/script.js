/* ==========================================================
   充电控制 – 前端脚本 v2
   KernelSU 原生 WebUI 版本，通过 exec() 直接执行 shell 命令。
   后端逻辑完全保留，仅升级 UI 渲染部分。
   ========================================================== */

import { exec } from 'kernelsu';

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

/* ── 路径常量 ────────────────────────────────────────────── */

const MOD_DIR     = '/data/adb/modules/ChargeControl';
const CONFIG_PATH = `${MOD_DIR}/config.json`;
const DB_PATH     = `${MOD_DIR}/chargecontrol.db`;

/* ── 充电模式定义 ─────────────────────────────────────────── */

const MODES = {
  normal:       { max_current_ma: 2000, description: '标准充电速度，适合日常使用' },
  fast:         { max_current_ma: 4000, description: '最大速度充电，适合快速补电' },
  trickle:      { max_current_ma: 500,  description: '低速涓流充电，保护电池健康' },
  power_saving: { max_current_ma: 1000, description: '中速充电，兼顾速度与保护' },
  super_saver:  { max_current_ma: 300,  description: '超低功率充电，最大保护电池' },
};

/* ── sysfs 辅助 ────────────────────────────────────────────── */

async function readSysfs(path) {
  const { errno, stdout } = await exec(`cat "${path}" 2>/dev/null`);
  return errno === 0 ? stdout.trim() : null;
}

/* ── 配置文件读写 ─────────────────────────────────────────── */

async function loadConfig() {
  const { errno, stdout } = await exec(`cat "${CONFIG_PATH}"`);
  if (errno !== 0) return {};
  try { return JSON.parse(stdout); } catch { return {}; }
}

async function saveConfig(cfg) {
  const json = JSON.stringify(cfg, null, 2).replace(/'/g, "'\\''");
  const { errno } = await exec(`printf '%s' '${json}' > "${CONFIG_PATH}"`);
  return errno === 0;
}

/* ── 电池状态读取（替换 GET /api/settings） ─────────────────── */

async function getBatteryStatus() {
  const capacityPaths = [
    '/sys/class/power_supply/battery/capacity',
    '/sys/class/power_supply/BAT0/capacity',
  ];
  const statusPaths = [
    '/sys/class/power_supply/battery/status',
    '/sys/class/power_supply/BAT0/status',
  ];
  const tempPaths = [
    '/sys/class/power_supply/battery/temp',
    '/sys/class/power_supply/BAT0/temp',
  ];
  const voltagePaths = [
    '/sys/class/power_supply/battery/voltage_now',
    '/sys/class/power_supply/BAT0/voltage_now',
  ];
  const currentPaths = [
    '/sys/class/power_supply/battery/current_now',
    '/sys/class/power_supply/BAT0/current_now',
  ];
  const chargingEnabledPaths = [
    '/sys/class/power_supply/battery/charging_enabled',
    '/sys/kernel/debug/charger/charging_enable',
    '/proc/driver/mmi_battery/charging',
  ];

  async function readFirst(paths) {
    for (const p of paths) {
      const val = await readSysfs(p);
      if (val !== null) return val;
    }
    return null;
  }

  const capacityRaw = await readFirst(capacityPaths);
  const statusRaw   = await readFirst(statusPaths);
  const tempRaw     = await readFirst(tempPaths);
  const voltageRaw  = await readFirst(voltagePaths);
  const currentRaw  = await readFirst(currentPaths);
  const chargingRaw = await readFirst(chargingEnabledPaths);

  let temperature = null;
  if (tempRaw !== null) {
    const val = parseInt(tempRaw, 10);
    if (!isNaN(val)) {
      temperature = Math.abs(val) > 100 ? val / 10 : val;
    }
  }

  let voltage_mv = null;
  if (voltageRaw !== null) {
    const uv = parseInt(voltageRaw, 10);
    if (!isNaN(uv)) voltage_mv = uv / 1000;
  }

  let current_ma = null;
  if (currentRaw !== null) {
    const ua = parseInt(currentRaw, 10);
    if (!isNaN(ua)) current_ma = ua / 1000;
  }

  let charging_enabled = true;
  if (chargingRaw !== null) {
    charging_enabled = !['0', 'false', 'disabled'].includes(chargingRaw.trim());
  }

  return {
    capacity:         capacityRaw !== null ? parseInt(capacityRaw, 10) : null,
    status:           statusRaw ?? '未知',
    temperature,
    voltage_mv,
    current_ma,
    charging_enabled,
    timestamp:        new Date().toISOString(),
  };
}

/* ── 设置充电开关（替换 POST /api/charging/enable） ─────────── */

async function setChargingEnabled(enabled) {
  const val = enabled ? '1' : '0';
  const paths = [
    '/sys/class/power_supply/battery/charging_enabled',
    '/sys/kernel/debug/charger/charging_enable',
    '/proc/driver/mmi_battery/charging',
  ];
  for (const p of paths) {
    const { errno } = await exec(`echo ${val} > "${p}" 2>/dev/null`);
    if (errno === 0) return true;
  }
  return false;
}

/* ── 设置充电上限（替换 POST /api/charging/limit） ──────────── */

async function setChargeLimit(limit) {
  const paths = [
    '/sys/class/power_supply/battery/charge_control_limit',
    '/sys/devices/platform/soc/soc:qti_battery_charger/charge_limit',
  ];
  let ok = false;
  for (const p of paths) {
    const { errno } = await exec(`echo ${limit} > "${p}" 2>/dev/null`);
    if (errno === 0) { ok = true; break; }
  }
  const cfg = await loadConfig();
  cfg.charging = cfg.charging || {};
  cfg.charging.max_limit = limit;
  await saveConfig(cfg);
  return ok;
}

/* ── 设置充电模式（替换 POST /api/charging/mode） ───────────── */

async function setChargingMode(mode) {
  const cfg = await loadConfig();
  const modeCfg = (cfg.modes || {})[mode] || MODES[mode];
  if (!modeCfg) return false;
  const ma = modeCfg.max_current_ma;
  if (ma) {
    const ua = ma * 1000;
    const inputPaths = [
      '/sys/class/power_supply/battery/input_current_limit',
      '/sys/class/power_supply/usb/input_current_limit',
    ];
    const chargePaths = [
      '/sys/class/power_supply/battery/constant_charge_current',
      '/sys/class/power_supply/battery/constant_charge_current_max',
    ];
    for (const p of inputPaths) {
      const { errno } = await exec(`echo ${ua} > "${p}" 2>/dev/null`);
      if (errno === 0) break;
    }
    for (const p of chargePaths) {
      const { errno } = await exec(`echo ${ua} > "${p}" 2>/dev/null`);
      if (errno === 0) break;
    }
  }
  cfg.charging = cfg.charging || {};
  cfg.charging.mode = mode;
  await saveConfig(cfg);
  return true;
}

/* ── 温度保护（替换 POST /api/charging/temperature-check） ───── */

async function checkTemperatureProtection() {
  const cfg = await loadConfig();
  const threshold = (cfg.charging || {}).temperature_threshold ?? 40;
  const critical  = (cfg.charging || {}).temperature_critical  ?? 45;
  const battery   = await getBatteryStatus();
  const temp      = battery.temperature ?? 0;

  let action = 'none';
  if (temp >= critical) {
    await setChargingEnabled(false);
    action = 'charging_stopped';
  } else if (temp >= threshold) {
    await setChargingMode('trickle');
    action = 'throttled_to_trickle';
  } else if (!battery.charging_enabled) {
    await setChargingEnabled(true);
    action = 'charging_resumed';
  }

  return { temperature: temp, threshold, critical, action };
}

/* ── SQLite 查询辅助（替换 /api/stats/*） ────────────────────── */

async function queryDb(sql) {
  const { errno, stdout } = await exec(`sqlite3 -json "${DB_PATH}" "${sql}" 2>/dev/null`);
  if (errno !== 0 || !stdout.trim()) return [];
  try { return JSON.parse(stdout); } catch { return []; }
}

/* ==========================================================
   UI 工具函数
   ========================================================== */

function $(id) { return document.getElementById(id); }

function setText(id, text) {
  const el = $(id);
  if (el) el.textContent = text;
}

/* ── 通知堆叠（替换旧版 showToast） ── */

function showToast(msg, duration = 3000) {
  // 兼容旧调用
  _pushNotification(msg, duration);
}

function _pushNotification(msg, duration = 3000) {
  const stack = $('notificationStack');
  if (!stack) return;

  const note = document.createElement('div');
  note.className = 'notification';
  if (msg.startsWith('✅') || msg.startsWith('✓')) note.classList.add('notification--success');
  else if (msg.startsWith('❌') || msg.startsWith('✗')) note.classList.add('notification--error');
  else if (msg.startsWith('⚠')) note.classList.add('notification--warning');
  note.textContent = msg;
  stack.appendChild(note);

  setTimeout(() => {
    note.classList.add('dismissing');
    note.addEventListener('animationend', () => note.remove(), { once: true });
  }, duration);
}

function showResult(id, data, isError = false) {
  const el = $(id);
  el.textContent = typeof data === 'string' ? data : JSON.stringify(data, null, 2);
  el.className = 'result-box ' + (isError ? 'error' : 'success');
  el.classList.remove('hidden');
}

/* ── 带动画的确认按钮 ── */

function setButtonState(btn, state) {
  // state: 'idle' | 'loading' | 'success' | 'error'
  btn.classList.remove('btn--loading', 'btn--success', 'btn--error');
  const icon = btn.querySelector('.btn__icon');
  const text = btn.querySelector('.btn__text');
  if (state === 'loading') {
    btn.classList.add('btn--loading');
    if (icon) icon.textContent = '↻';
    btn.disabled = true;
  } else if (state === 'success') {
    btn.classList.add('btn--success');
    if (icon) icon.textContent = '✓';
    btn.disabled = false;
    setTimeout(() => setButtonState(btn, 'idle'), 2000);
  } else if (state === 'error') {
    btn.classList.add('btn--error');
    if (icon) icon.textContent = '✗';
    btn.disabled = false;
    setTimeout(() => setButtonState(btn, 'idle'), 2000);
  } else {
    // idle – restore original icon from data-icon attribute
    if (icon) icon.textContent = btn.dataset.defaultIcon || '✓';
    if (text && btn.dataset.defaultText) text.textContent = btn.dataset.defaultText;
    btn.disabled = false;
  }
}

/* ── 主题切换 ── */

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

/* ── 汉堡菜单（移动端） ── */

$('hamburgerBtn').addEventListener('click', () => {
  const btn  = $('hamburgerBtn');
  const tabs = $('navbarTabs');
  btn.classList.toggle('open');
  tabs.classList.toggle('open');
});

/* ── 顶部标签页导航 ── */

document.querySelectorAll('.navbar__tab').forEach(tab => {
  tab.addEventListener('click', () => {
    const target = tab.dataset.tab;
    document.querySelectorAll('.navbar__tab').forEach(t => t.classList.remove('active'));
    document.querySelectorAll('.tab-panel').forEach(p => p.classList.remove('active'));
    tab.classList.add('active');
    $('tab-' + target).classList.add('active');
    if (target === 'stats')  loadStats('daily');
    if (target === 'config') loadConfigEditor();
    if (target === 'modes')  loadModes();
    // 关闭移动端菜单
    $('hamburgerBtn').classList.remove('open');
    $('navbarTabs').classList.remove('open');
  });
});

/* ==========================================================
   仪表盘
   ========================================================== */

/* ── 环形电量仪表 ── */

let _gaugeAnim = null;
let _gaugeCurrentValue = 0;

function drawDonutGauge(targetPct) {
  const canvas = $('donutGauge');
  if (!canvas) return;
  const ctx    = canvas.getContext('2d');
  const W = canvas.width;
  const H = canvas.height;
  const cx = W / 2, cy = H / 2;
  const r  = Math.min(W, H) / 2 - 18;
  const lineW = 18;

  if (_gaugeAnim) cancelAnimationFrame(_gaugeAnim);
  const start = _gaugeCurrentValue;
  const diff  = targetPct - start;
  const dur   = 600; // ms
  const t0    = performance.now();

  function frame(now) {
    const prog = Math.min((now - t0) / dur, 1);
    // ease-out-cubic
    const ease = 1 - Math.pow(1 - prog, 3);
    const pct  = start + diff * ease;
    _gaugeCurrentValue = pct;

    ctx.clearRect(0, 0, W, H);

    // 背景圆环
    ctx.beginPath();
    ctx.arc(cx, cy, r, 0, Math.PI * 2);
    ctx.strokeStyle = getComputedStyle(document.documentElement).getPropertyValue('--surface2').trim();
    ctx.lineWidth = lineW;
    ctx.stroke();

    // 彩色前景弧
    const startAngle = -Math.PI / 2;
    const endAngle   = startAngle + (pct / 100) * Math.PI * 2;
    const gaugeColor = pct > 50 ? '#22c55e' : pct > 20 ? '#f59e0b' : '#ef4444';

    const grad = ctx.createLinearGradient(cx - r, cy, cx + r, cy);
    grad.addColorStop(0, getComputedStyle(document.documentElement).getPropertyValue('--accent').trim() || '#38bdf8');
    grad.addColorStop(1, gaugeColor);

    ctx.beginPath();
    ctx.arc(cx, cy, r, startAngle, endAngle);
    ctx.strokeStyle = grad;
    ctx.lineWidth = lineW;
    ctx.lineCap = 'round';
    ctx.stroke();

    if (prog < 1) _gaugeAnim = requestAnimationFrame(frame);
  }
  _gaugeAnim = requestAnimationFrame(frame);
}

/* ── 仪表盘状态更新 ── */

function updateDashboard(battery) {
  const cap = battery.capacity ?? null;

  // 环形仪表 + 中心数字
  if (cap !== null) {
    drawDonutGauge(cap);
    setText('batteryLevel', String(cap));
  } else {
    setText('batteryLevel', '—');
  }

  // Stat Chips
  setText('chargingStatus', battery.status ?? '—');
  setText('batteryTemp',    battery.temperature != null ? battery.temperature + '°C' : '—');
  setText('batteryVoltage', battery.voltage_mv  != null ? (battery.voltage_mv / 1000).toFixed(2) + ' V' : '—');
  setText('batteryCurrent', battery.current_ma  != null ? battery.current_ma.toFixed(0) + ' mA' : '—');

  // 充电动画图标
  const icon = $('chargingIcon');
  if (icon) {
    const isCharging = battery.status && battery.status !== '未知' && battery.status.toLowerCase() !== 'discharging';
    icon.classList.toggle('charging-anim', isCharging);
    icon.textContent = isCharging ? '⚡' : '🔋';
  }
}

function updateMode(config) {
  const mode = config?.charging?.mode ?? '—';
  const meta = MODE_META[mode];
  setText('currentMode', meta ? meta.label : mode.replace('_', ' ').replace(/\b\w/g, c => c.toUpperCase()));
}

async function refreshStatus() {
  try {
    const [battery, cfg] = await Promise.all([getBatteryStatus(), loadConfig()]);
    updateDashboard(battery);
    updateMode(cfg);

    const charging = cfg.charging ?? {};

    // 电源开关状态同步
    const isEnabled = battery.charging_enabled !== false;
    const toggle = $('chargingToggle');
    if (toggle) toggle.checked = isEnabled;
    _syncPowerSwitch(isEnabled);

    const limitSlider = $('chargeLimitSlider');
    if (limitSlider && charging.max_limit != null) {
      limitSlider.value = charging.max_limit;
      setText('chargeLimitValue', charging.max_limit + '%');
      _updateBubble('chargeLimitBubble', limitSlider, charging.max_limit + '%');
    }

    const tempSlider = $('tempThresholdSlider');
    if (tempSlider && charging.temperature_threshold != null) {
      tempSlider.value = charging.temperature_threshold;
      setText('tempThresholdValue', charging.temperature_threshold + '°C');
      _updateBubble('tempThresholdBubble', tempSlider, charging.temperature_threshold + '°C');
    }

    const critSlider = $('tempCriticalSlider');
    if (critSlider && charging.temperature_critical != null) {
      critSlider.value = charging.temperature_critical;
      setText('tempCriticalValue', charging.temperature_critical + '°C');
      _updateBubble('tempCriticalBubble', critSlider, charging.temperature_critical + '°C');
    }

    $('connectionStatus').textContent = '已连接';
    $('connectionStatus').className = 'badge badge--success';
  } catch {
    $('connectionStatus').textContent = '离线';
    $('connectionStatus').className = 'badge badge--danger';
  }
}

/* ── 温度折线图（保留原 drawLineChart 逻辑，增加平滑动画） ── */

function drawLineChart(canvasId, labels, dataset, color = '#38bdf8', label = '') {
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

  // 缓存 CSS 变量颜色值（避免在循环中重复调用 getComputedStyle）
  const cs = getComputedStyle(document.documentElement);
  const borderColor   = cs.getPropertyValue('--border').trim()     || '#21262d';
  const mutedColor    = cs.getPropertyValue('--text-muted').trim() || '#7d8590';
  ctx.strokeStyle = borderColor;
  ctx.lineWidth = 1;
  for (let i = 0; i <= 4; i++) {
    const y = pad.top + (h / 4) * i;
    ctx.beginPath(); ctx.moveTo(pad.left, y); ctx.lineTo(pad.left + w, y); ctx.stroke();
    const val = (max - (range / 4) * i).toFixed(1);
    ctx.fillStyle = mutedColor;
    ctx.font = '10px sans-serif';
    ctx.textAlign = 'right';
    ctx.fillText(val, pad.left - 4, y + 3);
  }

  // X 轴标签
  ctx.fillStyle = mutedColor;
  ctx.font = '10px sans-serif';
  ctx.textAlign = 'center';
  const step = Math.max(1, Math.floor(labels.length / 6));
  labels.forEach((lbl, i) => {
    if (i % step === 0) ctx.fillText(lbl, xOf(i), H - 6);
  });

  // 平滑动画过渡 – 用 requestAnimationFrame 逐步绘制
  const totalPoints = dataset.length;
  let drawn = 0;
  const pointsPerFrame = Math.max(1, Math.ceil(totalPoints / 30));

  function drawSegment() {
    const end = Math.min(drawn + pointsPerFrame, totalPoints);

    // 重新绘制渐变填充区域（当前已绘区段）
    if (end >= 2) {
      const grad = ctx.createLinearGradient(0, pad.top, 0, pad.top + h);
      grad.addColorStop(0, color + '55');
      grad.addColorStop(1, color + '00');
      ctx.beginPath();
      ctx.moveTo(xOf(0), yOf(dataset[0]));
      for (let i = 1; i < end; i++) ctx.lineTo(xOf(i), yOf(dataset[i]));
      ctx.lineTo(xOf(end - 1), pad.top + h);
      ctx.lineTo(xOf(0), pad.top + h);
      ctx.closePath();
      ctx.fillStyle = grad;
      ctx.fill();
    }

    // 折线
    ctx.beginPath();
    ctx.strokeStyle = color;
    ctx.lineWidth = 2;
    ctx.lineJoin = 'round';
    ctx.moveTo(xOf(0), yOf(dataset[0]));
    for (let i = 1; i < end; i++) ctx.lineTo(xOf(i), yOf(dataset[i]));
    ctx.stroke();

    drawn = end;
    if (drawn < totalPoints) requestAnimationFrame(drawSegment);
  }
  requestAnimationFrame(drawSegment);
}

async function refreshTempChart() {
  try {
    const snaps = await queryDb(
      'SELECT * FROM battery_snapshots ORDER BY timestamp DESC LIMIT 60'
    );
    const ordered = snaps.slice().reverse();
    const labels = ordered.map(s => {
      const d = new Date(s.timestamp);
      return d.getHours() + ':' + String(d.getMinutes()).padStart(2, '0');
    });
    const temps = ordered.map(s => s.temperature ?? 0);
    drawLineChart('tempChart', labels, temps, '#f59e0b', '温度 °C');
  } catch { /* ignore */ }
}

/* ── 电池健康环（圆形百分比） ── */

function drawHealthRing(score) {
  const canvas = $('healthRing');
  if (!canvas) return;
  const ctx = canvas.getContext('2d');
  const W = canvas.width, H = canvas.height;
  const cx = W / 2, cy = H / 2;
  const r = Math.min(W, H) / 2 - 10;
  const lineW = 10;

  ctx.clearRect(0, 0, W, H);

  // 背景圆
  ctx.beginPath();
  ctx.arc(cx, cy, r, 0, Math.PI * 2);
  ctx.strokeStyle = getComputedStyle(document.documentElement).getPropertyValue('--surface2').trim();
  ctx.lineWidth = lineW;
  ctx.stroke();

  // 彩色弧
  const ringColor = score >= 80 ? '#22c55e' : score >= 50 ? '#f59e0b' : '#ef4444';
  ctx.beginPath();
  ctx.arc(cx, cy, r, -Math.PI / 2, -Math.PI / 2 + (score / 100) * Math.PI * 2);
  ctx.strokeStyle = ringColor;
  ctx.lineWidth = lineW;
  ctx.lineCap = 'round';
  ctx.stroke();
}

async function refreshHealthCard() {
  try {
    const rows = await queryDb(
      'SELECT AVG(temperature) AS avg_temp, MAX(temperature) AS max_temp, COUNT(*) AS total_snapshots FROM battery_snapshots'
    );
    const sessRows = await queryDb(
      "SELECT COUNT(*) AS cnt FROM charging_sessions WHERE end_time IS NOT NULL"
    );
    const r = rows[0] || {};
    const avg_temp = r.avg_temp ?? 0;
    const max_temp = r.max_temp ?? 0;
    let health_score = 100;
    if (avg_temp > 38) health_score -= 10;
    if (max_temp > 44) health_score -= 15;
    health_score = Math.max(0, health_score);

    setText('healthScore', health_score + '%');
    drawHealthRing(health_score);
    setText('healthDetail',
      `平均温度: ${(+avg_temp).toFixed(1)}°C  |  最高温度: ${(+max_temp).toFixed(1)}°C  |  充电次数: ${(sessRows[0] || {}).cnt ?? 0}`);
  } catch { /* ignore */ }
}

$('refreshChart').addEventListener('click', refreshTempChart);

/* ==========================================================
   控制标签页
   ========================================================== */

/* ── 圆形电源开关 ── */

function _syncPowerSwitch(enabled) {
  const btn   = $('powerSwitchBtn');
  const label = $('powerSwitchLabel');
  if (!btn) return;
  btn.setAttribute('aria-pressed', enabled ? 'true' : 'false');
  if (label) label.textContent = enabled ? '充电中' : '已暂停';
}

$('powerSwitchBtn').addEventListener('click', async () => {
  const toggle  = $('chargingToggle');
  const newState = toggle.checked ? false : true; // 点击切换
  toggle.checked = newState;
  _syncPowerSwitch(newState);
  try {
    const ok = await setChargingEnabled(newState);
    showToast(ok ? '✅ 充电已' + (newState ? '启用' : '禁用') : '❌ 操作失败');
  } catch { showToast('❌ 切换充电状态出错'); }
});

// 保留旧 change 监听（chargingToggle checkbox，以防直接操作）
$('chargingToggle').addEventListener('change', async e => {
  _syncPowerSwitch(e.target.checked);
  try {
    const ok = await setChargingEnabled(e.target.checked);
    showToast(ok ? '✅ 充电已' + (e.target.checked ? '启用' : '禁用') : '❌ 操作失败');
  } catch { showToast('❌ 切换充电状态出错'); }
});

/* ── 气泡标签滑块辅助 ── */

function _updateBubble(bubbleId, rangeEl, text) {
  const bubble = $(bubbleId);
  if (!bubble || !rangeEl) return;
  const min = parseFloat(rangeEl.min);
  const max = parseFloat(rangeEl.max);
  const val = parseFloat(rangeEl.value);
  const pct = (val - min) / (max - min);
  // 计算滑块轨道宽度偏移（thumb宽度 9px 补偿）
  const thumbW = 9;
  const trackW = rangeEl.offsetWidth - thumbW * 2;
  const leftPx = thumbW + pct * trackW;
  bubble.style.left = leftPx + 'px';
  bubble.textContent = text;
}

$('chargeLimitSlider').addEventListener('input', e => {
  setText('chargeLimitValue', e.target.value + '%');
  _updateBubble('chargeLimitBubble', e.target, e.target.value + '%');
});

$('applyLimitBtn').dataset.defaultIcon = '✓';
$('applyLimitBtn').addEventListener('click', async () => {
  const btn   = $('applyLimitBtn');
  const limit = parseInt($('chargeLimitSlider').value);
  setButtonState(btn, 'loading');
  try {
    const ok = await setChargeLimit(limit);
    setButtonState(btn, ok ? 'success' : 'error');
    showToast(ok ? `✅ 上限已设为 ${limit}%` : '❌ 操作失败');
  } catch { setButtonState(btn, 'error'); showToast('❌ 设置上限出错'); }
});

$('tempThresholdSlider').addEventListener('input', e => {
  setText('tempThresholdValue', e.target.value + '°C');
  _updateBubble('tempThresholdBubble', e.target, e.target.value + '°C');
});

$('tempCriticalSlider').addEventListener('input', e => {
  setText('tempCriticalValue', e.target.value + '°C');
  _updateBubble('tempCriticalBubble', e.target, e.target.value + '°C');
});

$('applyTempBtn').dataset.defaultIcon = '✓';
$('applyTempBtn').addEventListener('click', async () => {
  const btn       = $('applyTempBtn');
  const threshold = parseInt($('tempThresholdSlider').value);
  const critical  = parseInt($('tempCriticalSlider').value);
  setButtonState(btn, 'loading');
  try {
    const cfg = await loadConfig();
    cfg.charging = cfg.charging || {};
    cfg.charging.temperature_threshold = threshold;
    cfg.charging.temperature_critical  = critical;
    const ok = await saveConfig(cfg);
    setButtonState(btn, ok ? 'success' : 'error');
    showToast(ok ? '✅ 温度阈值已更新' : '❌ 操作失败');
  } catch { setButtonState(btn, 'error'); showToast('❌ 更新温度阈值出错'); }
});

$('tempCheckBtn').dataset.defaultIcon = '🌡️';
$('tempCheckBtn').addEventListener('click', async () => {
  const btn = $('tempCheckBtn');
  setButtonState(btn, 'loading');
  try {
    const res = await checkTemperatureProtection();
    setButtonState(btn, 'success');
    showResult('tempCheckResult', res, false);
  } catch (e) {
    setButtonState(btn, 'error');
    showResult('tempCheckResult', e.message, true);
  }
});

/* ==========================================================
   模式标签页
   ========================================================== */

const MODE_META = {
  normal:       { icon: '🔋', label: '普通' },
  fast:         { icon: '⚡', label: '快充' },
  trickle:      { icon: '💧', label: '涓流' },
  power_saving: { icon: '🌿', label: '省电' },
  super_saver:  { icon: '🌱', label: '超级省电' },
};

async function loadModes() {
  try {
    const cfg     = await loadConfig();
    const current = cfg.charging?.mode || 'normal';
    const modes   = cfg.modes || MODES;
    const rail    = $('modesGrid');
    rail.innerHTML = '';

    Object.entries(modes).forEach(([key, modeCfg]) => {
      const meta = MODE_META[key] || { icon: '⚙️', label: key };

      // 翻转卡片外壳
      const flipCard = document.createElement('div');
      flipCard.className = 'flip-card' + (key === current ? ' selected' : '');
      flipCard.dataset.key = key;

      flipCard.innerHTML = `
        <div class="flip-card__inner">
          <div class="flip-card__front">
            <div class="flip-card__ribbon">当前</div>
            <div class="mode-icon">${meta.icon}</div>
            <div class="mode-name">${meta.label}</div>
            <div class="mode-desc">${modeCfg.description || ''}</div>
            ${modeCfg.max_current_ma ? `<div class="mode-current-ma">${modeCfg.max_current_ma} mA</div>` : ''}
          </div>
          <div class="flip-card__back">
            <div class="flip-back__title">${meta.label}</div>
            <div class="flip-back__desc">${modeCfg.description || ''}</div>
            <button class="flip-confirm-btn" data-key="${key}">确认切换</button>
          </div>
        </div>`;

      // 正面点击 → 翻转
      flipCard.querySelector('.flip-card__front').addEventListener('click', () => {
        flipCard.classList.toggle('flipped');
      });

      // 背面确认按钮
      flipCard.querySelector('.flip-confirm-btn').addEventListener('click', async e => {
        e.stopPropagation();
        try {
          const ok = await setChargingMode(key);
          if (ok) {
            rail.querySelectorAll('.flip-card').forEach(c => c.classList.remove('selected'));
            flipCard.classList.add('selected');
            flipCard.classList.remove('flipped');
            showToast(`✅ 已切换模式: ${meta.label}`);
            showResult('modeSetResult', { success: true, mode: key }, false);
          } else {
            showResult('modeSetResult', { success: false }, true);
          }
        } catch (err) { showResult('modeSetResult', err.message, true); }
      });

      rail.appendChild(flipCard);
    });
  } catch (e) { $('modesGrid').textContent = '加载模式失败: ' + e.message; }
}

/* ==========================================================
   统计标签页
   ========================================================== */

let currentPeriod = 'daily';

// 分段控件绑定
document.querySelectorAll('.segmented-control__btn').forEach(btn => {
  btn.addEventListener('click', () => {
    document.querySelectorAll('.segmented-control__btn').forEach(b => b.classList.remove('active'));
    btn.classList.add('active');
    currentPeriod = btn.dataset.period;
    loadStats(currentPeriod);
  });
});

// 旧版 data-period 兼容（防止 JS 代码中有直接调用）
document.querySelectorAll('[data-period]').forEach(btn => {
  if (!btn.classList.contains('segmented-control__btn')) {
    btn.addEventListener('click', () => {
      currentPeriod = btn.dataset.period;
      loadStats(currentPeriod);
    });
  }
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

function isoSince(ms) {
  return new Date(Date.now() - ms).toISOString().replace('T', ' ').replace(/\.\d+Z$/, '');
}

async function loadStats(period) {
  try {
    let sql;
    if (period === 'daily') {
      const since = isoSince(7 * 24 * 3600 * 1000);
      sql = `SELECT DATE(start_time) AS day, COUNT(*) AS sessions, AVG(efficiency) AS avg_efficiency, SUM(duration_s) AS total_duration_s, MAX(max_temp) AS max_temp FROM charging_sessions WHERE start_time >= '${since}' AND end_time IS NOT NULL GROUP BY DATE(start_time) ORDER BY day ASC`;
    } else if (period === 'weekly') {
      const since = isoSince(12 * 7 * 24 * 3600 * 1000);
      sql = `SELECT STRFTIME('%Y-W%W', start_time) AS week, COUNT(*) AS sessions, AVG(efficiency) AS avg_efficiency, SUM(duration_s) AS total_duration_s, MAX(max_temp) AS max_temp FROM charging_sessions WHERE start_time >= '${since}' AND end_time IS NOT NULL GROUP BY week ORDER BY week ASC`;
    } else {
      const since = isoSince(365 * 24 * 3600 * 1000);
      sql = `SELECT STRFTIME('%Y-%m', start_time) AS month, COUNT(*) AS sessions, AVG(efficiency) AS avg_efficiency, SUM(duration_s) AS total_duration_s, MAX(max_temp) AS max_temp FROM charging_sessions WHERE start_time >= '${since}' AND end_time IS NOT NULL GROUP BY month ORDER BY month ASC`;
    }

    const rows = await queryDb(sql);
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

    const labels   = rows.map(r => r[cols[0]]);
    const sessions = rows.map(r => r.sessions ?? 0);
    drawBarChart('statsChart', labels, sessions);
  } catch (e) { $('statsTableBody').innerHTML = `<tr><td colspan="5">${e.message}</td></tr>`; }
}

/* ── 柱状图（颜色渐变 + hover tooltip） ── */

let _barData = { labels: [], dataset: [] };

function drawBarChart(canvasId, labels, dataset) {
  const canvas = $(canvasId);
  if (!canvas) return;
  const ctx = canvas.getContext('2d');
  const W = canvas.offsetWidth;
  const H = canvas.offsetHeight || 180;
  canvas.width = W;
  canvas.height = H;
  _barData = { labels, dataset, W, H };

  _renderBars(ctx, W, H, labels, dataset, -1);
}

function _barColor(ratio) {
  // 从蓝 #38bdf8 渐变到紫 #818cf8
  const r1 = 0x38, g1 = 0xbd, b1 = 0xf8;
  const r2 = 0x81, g2 = 0x8c, b2 = 0xf8;
  const r = Math.round(r1 + (r2 - r1) * ratio);
  const g = Math.round(g1 + (g2 - g1) * ratio);
  const b = Math.round(b1 + (b2 - b1) * ratio);
  return `rgb(${r},${g},${b})`;
}

function _renderBars(ctx, W, H, labels, dataset, hoverIdx) {
  const pad = { top: 20, right: 16, bottom: 30, left: 40 };
  const w = W - pad.left - pad.right;
  const h = H - pad.top - pad.bottom;

  ctx.clearRect(0, 0, W, H);
  if (!dataset.length) return;

  const max   = Math.max(...dataset, 1);
  const barW  = (w / dataset.length) * 0.6;
  const gapW  = (w / dataset.length) * 0.4;
  // 缓存 CSS 变量颜色值
  const mutedColor = getComputedStyle(document.documentElement).getPropertyValue('--text-muted').trim() || '#7d8590';

  dataset.forEach((v, i) => {
    const bh  = (v / max) * h;
    const x   = pad.left + i * (w / dataset.length) + gapW / 2;
    const y   = pad.top + h - bh;
    const ratio = dataset.length > 1 ? i / (dataset.length - 1) : 0;
    const alpha = i === hoverIdx ? 'ff' : 'cc';

    ctx.fillStyle = _barColor(ratio) + alpha;
    ctx.beginPath();
    ctx.roundRect(x, y, barW, bh, [4, 4, 0, 0]);
    ctx.fill();

    ctx.fillStyle = mutedColor;
    ctx.font = '10px sans-serif';
    ctx.textAlign = 'center';
    ctx.fillText(labels[i], x + barW / 2, H - 6);
  });
}

// 柱状图 hover tooltip
const statsCanvas = $('statsChart');
const chartTooltip = $('chartTooltip');
if (statsCanvas && chartTooltip) {
  statsCanvas.addEventListener('mousemove', e => {
    const { labels, dataset, W, H } = _barData;
    if (!dataset || !dataset.length) return;
    const rect = statsCanvas.getBoundingClientRect();
    const mx = e.clientX - rect.left;
    const my = e.clientY - rect.top;
    const pad = { top: 20, right: 16, bottom: 30, left: 40 };
    const w   = W - pad.left - pad.right;
    const barW = (w / dataset.length) * 0.6;
    const gapW = (w / dataset.length) * 0.4;

    let hoveredIdx = -1;
    dataset.forEach((v, i) => {
      const x  = pad.left + i * (w / dataset.length) + gapW / 2;
      const bh = (v / Math.max(...dataset, 1)) * (H - pad.top - pad.bottom);
      const y  = pad.top + (H - pad.top - pad.bottom) - bh;
      if (mx >= x && mx <= x + barW && my >= y && my <= H - pad.bottom) hoveredIdx = i;
    });

    if (hoveredIdx >= 0) {
      chartTooltip.textContent = `${labels[hoveredIdx]}: ${dataset[hoveredIdx]}`;
      chartTooltip.style.left = (e.offsetX + 12) + 'px';
      chartTooltip.style.top  = (e.offsetY - 28) + 'px';
      chartTooltip.classList.add('visible');
      // 重绘高亮
      const ctx = statsCanvas.getContext('2d');
      _renderBars(ctx, W, H, labels, dataset, hoveredIdx);
    } else {
      chartTooltip.classList.remove('visible');
      if (_barData.W) {
        const ctx = statsCanvas.getContext('2d');
        _renderBars(ctx, _barData.W, _barData.H, labels, dataset, -1);
      }
    }
  });
  statsCanvas.addEventListener('mouseleave', () => {
    chartTooltip.classList.remove('visible');
    if (_barData.W) {
      const ctx = statsCanvas.getContext('2d');
      _renderBars(ctx, _barData.W, _barData.H, _barData.labels, _barData.dataset, -1);
    }
  });
}

/* ── 导出 ── */

$('exportCsv').addEventListener('click', async () => {
  try {
    const rows = await queryDb('SELECT * FROM charging_sessions ORDER BY start_time');
    if (!rows.length) { showToast('暂无数据'); return; }
    const keys = Object.keys(rows[0]);
    const csv  = [keys.join(',')]
      .concat(rows.map(r => keys.map(k => JSON.stringify(r[k] ?? '')).join(',')))
      .join('\n');
    const blob = new Blob([csv], { type: 'text/csv' });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = 'charging_data.csv';
    a.click();
  } catch (e) { showToast('❌ 导出失败: ' + e.message); }
});

$('exportJson').addEventListener('click', async () => {
  try {
    const data = await queryDb('SELECT * FROM charging_sessions ORDER BY start_time');
    const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = 'charging_data.json';
    a.click();
  } catch (e) { showToast('❌ 导出失败: ' + e.message); }
});

/* ==========================================================
   设置标签页
   ========================================================== */

/* ── 代码编辑器行号 + 语法检测 ── */

function _syncGutter(textarea, gutter) {
  const lines = textarea.value.split('\n').length;
  const existing = gutter.childElementCount;
  if (lines > existing) {
    for (let i = existing + 1; i <= lines; i++) {
      const span = document.createElement('div');
      span.textContent = i;
      gutter.appendChild(span);
    }
  } else if (lines < existing) {
    while (gutter.childElementCount > lines) gutter.lastChild.remove();
  }
  // 同步滚动
  gutter.scrollTop = textarea.scrollTop;
}

function _validateJson(textarea) {
  const errEl = $('configSyntaxError');
  if (!errEl) return;
  try {
    JSON.parse(textarea.value);
    errEl.textContent = '';
    errEl.classList.add('hidden');
  } catch (e) {
    errEl.textContent = '⚠ JSON 语法错误: ' + e.message;
    errEl.classList.remove('hidden');
  }
}

const configEditor = $('configEditor');
const editorGutter = $('editorGutter');

if (configEditor && editorGutter) {
  configEditor.addEventListener('input', () => {
    _syncGutter(configEditor, editorGutter);
    _validateJson(configEditor);
  });
  configEditor.addEventListener('scroll', () => {
    editorGutter.scrollTop = configEditor.scrollTop;
  });
}

async function loadConfigEditor() {
  try {
    const cfg = await loadConfig();
    configEditor.value = JSON.stringify(cfg, null, 2);
    _syncGutter(configEditor, editorGutter);
    _validateJson(configEditor);
  } catch (e) { configEditor.value = '// 加载配置出错: ' + e.message; }
}

$('loadConfigBtn').addEventListener('click', loadConfigEditor);

$('saveConfigBtn').addEventListener('click', async () => {
  try {
    const cfg = JSON.parse(configEditor.value);
    const ok  = await saveConfig(cfg);
    showResult('configResult', ok ? '配置保存成功！' : '保存失败。', !ok);
    showToast(ok ? '✅ 配置已保存' : '❌ 保存失败');
  } catch (e) { showResult('configResult', 'JSON 解析错误: ' + e.message, true); }
});

/* ── 预设卡片 ── */

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

    // Inline confirm: first click highlights, second click within 3s applies
    if (!btn._pendingConfirm) {
      btn._pendingConfirm = true;
      btn.style.borderColor = 'var(--accent)';
      btn.style.boxShadow   = 'var(--shadow-glow)';
      _pushNotification(`⚠ 再次点击「${btn.querySelector('.preset-card__name')?.textContent || btn.dataset.preset}」确认应用`, 3000);
      btn._confirmTimer = setTimeout(() => {
        btn._pendingConfirm = false;
        btn.style.borderColor = '';
        btn.style.boxShadow   = '';
      }, 3000);
      return;
    }

    // Second click – apply preset
    clearTimeout(btn._confirmTimer);
    btn._pendingConfirm = false;
    btn.style.borderColor = '';
    btn.style.boxShadow   = '';

    try {
      const cfg = await loadConfig();
      cfg.charging = { ...cfg.charging, ...p };
      const ok = await saveConfig(cfg);
      if (ok) {
        await setChargingMode(p.mode);
        await setChargeLimit(p.max_limit);
        showToast('✅ 已应用预设: ' + btn.dataset.preset);
        loadConfigEditor();
      }
    } catch (e) { showToast('❌ ' + e.message); }
  });
});

/* ==========================================================
   初始化与轮询
   ========================================================== */

initTheme();
refreshStatus();
refreshTempChart();
refreshHealthCard();

// 初始化气泡位置（渲染后）
requestAnimationFrame(() => {
  [
    ['chargeLimitSlider',    'chargeLimitBubble',    '%'],
    ['tempThresholdSlider',  'tempThresholdBubble',  '°C'],
    ['tempCriticalSlider',   'tempCriticalBubble',   '°C'],
  ].forEach(([sliderId, bubbleId, suffix]) => {
    const slider = $(sliderId);
    if (slider) _updateBubble(bubbleId, slider, slider.value + suffix);
  });
});

setInterval(refreshStatus,     10000);
setInterval(refreshTempChart,  30000);
setInterval(refreshHealthCard, 60000);
