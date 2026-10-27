/* ═══════════════════════════════════════════════════════════════════
   ChargeControl v1.19.0 —— 高级温度分析视图
   产品级 UI，非 Chart.js，纯 Canvas 自绘
   ═══════════════════════════════════════════════════════════════════

   设计目标（用户原话："一看就是高级UI工程师写出来"）：
   1. 横轴用充电进度（SOC%），不用时间轴 —— 能直接看「哪个电量段最烫」
   2. Catmull-Rom 样条平滑，不用直线连接
   3. 温度超过 temp_limit 画红色渐变警戒带
   4. 事件（温度超限）标红点，点击跳详情
   5. 电池温度 + 充电IC温度双线对比
   6. 悬停十字准星 + 精确值浮层
   7. 底部统计徽章：当前/峰值/平均/充电时平均/放电时平均
   8. devicePixelRatio 适配，高分屏不糊
   ═══════════════════════════════════════════════════════════════════ */

class TempCurveView {
  constructor(canvas, opts = {}) {
    this.cv = canvas;
    this.ctx = canvas.getContext('2d');
    this.dpr = Math.min(window.devicePixelRatio || 1, 3);
    this.opts = Object.assign({
      tempLimit: 45,           // 警戒线
      maxPoints: 200,          // 最多取多少个点
      showIC: true,            // 是否显示充电IC温度
      showBattery: true,
      highlightEvents: true,
      yMin: 25,                // Y 轴范围（°C）
      yMax: 55,
    }, opts);

    // 颜色（从 CSS 变量读主题）
    const cs = getComputedStyle(document.documentElement);
    this.colors = {
      bg:        cs.getPropertyValue('--card').trim()    || '#161b22',
      grid:      cs.getPropertyValue('--line').trim()    || '#30363d',
      txt:       cs.getPropertyValue('--txt').trim()     || '#e6edf3',
      txt2:      cs.getPropertyValue('--txt2').trim()    || '#8b949e',
      battery:   '#fb923c',   // 电池温度（橙）
      ic:        '#38bdf8',   // 充电IC温度（蓝）
      soc:       '#4ade80',   // 电量（绿）
      warn:      '#f87171',   // 警戒（红）
      warnFill:  'rgba(248,113,113,0.08)',
      evtDot:    '#fbbf24',   // 事件点（琥珀）
      hoverLine: 'rgba(139,148,158,0.4)',
    };

    this.data = [];           // 数据点 [{t, soc, tmp, chg_tmp, status, evt}]
    this.hoverIdx = -1;
    this.animFrame = null;

    // 交互
    canvas.addEventListener('mousemove', e => this.onMove(e));
    canvas.addEventListener('mouseleave', () => { this.hoverIdx = -1; this.scheduleRender(); });
    canvas.addEventListener('click', e => this.onClick(e));
    new ResizeObserver(() => this.scheduleRender()).observe(canvas);

    this.resize();
  }

  /* ── 数据接入 ─────────────────────────────────────────────── */
  setData(points) {
    // points: [{t, c, tmp, chg_tmp(可选), status}]
    // 转换 + 按电量聚合（取每个 SOC 的最高温）
    const bySoc = new Map();
    for (const p of points) {
      if (p.tmp == null || p.tmp <= 0) continue;
      const soc = p.c;
      if (!bySoc.has(soc)) {
        bySoc.set(soc, {
          t: p.t, soc, tmp: p.tmp / 10, chg_tmp: (p.chg_tmp || p.tmp) / 10,
          status: p.s, evt: null
        });
      } else {
        const old = bySoc.get(soc);
        if (p.tmp > old.tmp * 10) {
          old.tmp = p.tmp / 10;
          old.chg_tmp = (p.chg_tmp || p.tmp) / 10;
          old.t = p.t;
        }
      }
    }
    this.data = [...bySoc.values()].sort((a, b) => a.soc - b.soc);
    this.scheduleRender();
  }

  setEvents(events) {
    // 标记异常事件到最近的数据点
    if (!this.opts.highlightEvents) return;
    for (const e of events) {
      if (e.type !== 'temp_exceed' && e.type !== 'chg_temp_exceed') continue;
      const targetTemp = e.type === 'temp_exceed' ? (e.detail.temp || 0) : (e.detail.chg_temp || 0);
      // 找最接近该温度的点
      let best = null, bestDiff = 999;
      for (const p of this.data) {
        const d = Math.abs(p.tmp - targetTemp) + Math.abs(p.chg_tmp - targetTemp);
        if (d < bestDiff) { bestDiff = d; best = p; }
      }
      if (best) best.evt = e;
    }
    this.scheduleRender();
  }

  /* ── 尺寸与渲染 ───────────────────────────────────────────── */
  resize() {
    const r = this.cv.getBoundingClientRect();
    if (r.width === 0) return;
    this.cv.width = r.width * this.dpr;
    this.cv.height = r.height * this.dpr;
    this.cv.style.width = r.width + 'px';
    this.cv.style.height = r.height + 'px';
    this.ctx.setTransform(this.dpr, 0, 0, this.dpr, 0, 0);
    this.W = r.width; this.H = r.height;
    this.scheduleRender();
  }

  scheduleRender() {
    if (this.animFrame) return;
    this.animFrame = requestAnimationFrame(() => { this.animFrame = null; this.render(); });
  }

  /* ── 坐标换算 ─────────────────────────────────────────────── */
  // 布局：左轴（温度）+ 底部（电量轨迹）+ 边距
  layout() {
    const padL = 44, padR = 12, padT = 16, padB = 44;
    const w = this.W - padL - padR;
    const h = this.H - padT - padB;
    return { padL, padR, padT, padB, w, h };
  }
  // 温度 → Y
  yOf(t, L) { return L.padT + (this.opts.yMax - t) / (this.opts.yMax - this.opts.yMin) * L.h; }
  // 电量 → X（0-100% 映射到绘图区）
  xOf(soc, L) { return L.padL + (soc / 100) * L.w; }
  // 反算 X → 电量
  socOf(x, L) { return Math.max(0, Math.min(100, (x - L.padL) / L.w * 100)); }

  /* ── 平滑路径（Catmull-Rom → Bezier） ─────────────────────── */
  smoothPath(points, getX, getY) {
    if (points.length < 2) return null;
    const path = new Path2D();
    path.moveTo(getX(points[0]), getY(points[0]));
    const n = points.length;
    for (let i = 0; i < n - 1; i++) {
      const p0 = points[Math.max(0, i - 1)];
      const p1 = points[i];
      const p2 = points[i + 1];
      const p3 = points[Math.min(n - 1, i + 2)];
      const x0 = getX(p0), y0 = getY(p0);
      const x1 = getX(p1), y1 = getY(p1);
      const x2 = getX(p2), y2 = getY(p2);
      const x3 = getX(p3), y3 = getY(p3);
      // Catmull-Rom → Bezier 控制点
      const cp1x = x1 + (x2 - x0) / 6;
      const cp1y = y1 + (y2 - y0) / 6;
      const cp2x = x2 - (x3 - x1) / 6;
      const cp2y = y2 - (y3 - y1) / 6;
      path.bezierCurveTo(cp1x, cp1y, cp2x, cp2y, x2, y2);
    }
    return path;
  }

  /* ── 主渲染 ───────────────────────────────────────────────── */
  render() {
    const { ctx } = this;
    const L = this.layout();
    ctx.clearRect(0, 0, this.W, this.H);
    if (!this.data.length) { this.renderEmpty(L); return; }

    // 1. 警戒带（45°C 以上）
    if (this.opts.tempLimit < this.opts.yMax) {
      const y = this.yOf(this.opts.tempLimit, L);
      const grad = ctx.createLinearGradient(0, y, 0, L.padT);
      grad.addColorStop(0, this.colors.warnFill);
      grad.addColorStop(1, 'rgba(248,113,113,0.22)');
      ctx.fillStyle = grad;
      ctx.fillRect(L.padL, L.padT, L.w, y - L.padT);
      // 警戒线
      ctx.strokeStyle = this.colors.warn;
      ctx.setLineDash([6, 4]);
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(L.padL, y); ctx.lineTo(L.padL + L.w, y); ctx.stroke();
      ctx.setLineDash([]);
      // 标签
      ctx.fillStyle = this.colors.warn;
      ctx.font = '600 10px system-ui';
      ctx.textAlign = 'right';
      ctx.fillText(this.opts.tempLimit + '°C 警戒', L.padL + L.w - 6, y - 5);
    }

    // 2. 网格 + Y 轴
    ctx.strokeStyle = this.colors.grid;
    ctx.lineWidth = 1;
    ctx.fillStyle = this.colors.txt2;
    ctx.font = '10px system-ui';
    ctx.textAlign = 'right';
    for (let t = this.opts.yMin; t <= this.opts.yMax; t += 5) {
      const y = this.yOf(t, L);
      ctx.beginPath(); ctx.moveTo(L.padL, y); ctx.lineTo(L.padL + L.w, y); ctx.stroke();
      ctx.fillText(t + '°', L.padL - 6, y + 3);
    }

    // 3. X 轴（电量刻度）
    ctx.textAlign = 'center';
    for (let s = 0; s <= 100; s += 20) {
      const x = this.xOf(s, L);
      ctx.fillStyle = this.colors.txt2;
      ctx.fillText(s + '%', x, L.padT + L.h + 16);
      ctx.strokeStyle = this.colors.grid;
      ctx.beginPath(); ctx.moveTo(x, L.padT + L.h); ctx.lineTo(x, L.padT + L.h + 4); ctx.stroke();
    }

    // 4. 电量轨迹（底部细线）
    const socPath = this.smoothPath(this.data, p => this.xOf(p.soc, L), p => L.padT + L.h + 28);
    if (socPath) {
      ctx.strokeStyle = this.colors.soc;
      ctx.lineWidth = 1.5;
      ctx.globalAlpha = 0.5;
      ctx.stroke(socPath);
      ctx.globalAlpha = 1;
    }

    // 5. 温度曲线（电池）
    if (this.opts.showBattery) {
      const path = this.smoothPath(this.data, p => this.xOf(p.soc, L), p => this.yOf(p.tmp, L));
      if (path) {
        // 渐变填充
        const yTop = this.yOf(this.opts.yMax, L);
        const yBot = this.yOf(this.opts.yMin, L);
        const fill = new Path2D(path);
        fill.lineTo(this.xOf(this.data[this.data.length-1].soc, L), yBot);
        fill.lineTo(this.xOf(this.data[0].soc, L), yBot);
        fill.closePath();
        const grad = ctx.createLinearGradient(0, yTop, 0, yBot);
        grad.addColorStop(0, 'rgba(251,146,60,0.25)');
        grad.addColorStop(1, 'rgba(251,146,60,0)');
        ctx.fillStyle = grad;
        ctx.fill(fill);

        ctx.strokeStyle = this.colors.battery;
        ctx.lineWidth = 2;
        ctx.stroke(path);
      }
    }

    // 6. 温度曲线（充电IC，虚线）
    if (this.opts.showIC && this.data.some(p => p.chg_tmp > 0)) {
      const path = this.smoothPath(this.data, p => this.xOf(p.soc, L), p => this.yOf(p.chg_tmp, L));
      if (path) {
        ctx.strokeStyle = this.colors.ic;
        ctx.lineWidth = 1.5;
        ctx.setLineDash([4, 3]);
        ctx.stroke(path);
        ctx.setLineDash([]);
      }
    }

    // 7. 事件标记
    for (const p of this.data) {
      if (!p.evt) continue;
      const x = this.xOf(p.soc, L), y = this.yOf(p.tmp, L);
      // 光晕
      const g = ctx.createRadialGradient(x, y, 0, x, y, 10);
      g.addColorStop(0, this.colors.evtDot + '60');
      g.addColorStop(1, this.colors.evtDot + '00');
      ctx.fillStyle = g;
      ctx.fillRect(x - 10, y - 10, 20, 20);
      // 实心点
      ctx.fillStyle = this.colors.evtDot;
      ctx.beginPath(); ctx.arc(x, y, 4, 0, Math.PI * 2); ctx.fill();
      ctx.strokeStyle = '#fff';
      ctx.lineWidth = 1.5;
      ctx.stroke();
    }

    // 8. 悬停十字准星
    if (this.hoverIdx >= 0 && this.hoverIdx < this.data.length) {
      const p = this.data[this.hoverIdx];
      const x = this.xOf(p.soc, L);
      const y = this.yOf(p.tmp, L);
      // 垂直线
      ctx.strokeStyle = this.colors.hoverLine;
      ctx.lineWidth = 1;
      ctx.setLineDash([3, 3]);
      ctx.beginPath(); ctx.moveTo(x, L.padT); ctx.lineTo(x, L.padT + L.h); ctx.stroke();
      ctx.setLineDash([]);
      // 当前点高亮
      ctx.fillStyle = this.colors.battery;
      ctx.beginPath(); ctx.arc(x, y, 5, 0, Math.PI * 2); ctx.fill();
      ctx.strokeStyle = '#fff'; ctx.lineWidth = 2; ctx.stroke();
      // 浮层
      this.renderTooltip(p, x, y, L);
    }

    // 9. 底部统计徽章
    this.renderStats(L);
  }

  renderTooltip(p, x, y, L) {
    const { ctx } = this;
    const lines = [
      { t: `${p.soc}%`, c: this.colors.soc, b: true },
      { t: `电池 ${p.tmp.toFixed(1)}°C`, c: this.colors.battery, b: false },
      ...(p.chg_tmp > 0 ? [{ t: `充IC ${p.chg_tmp.toFixed(1)}°C`, c: this.colors.ic, b: false }] : []),
      ...(p.evt ? [{ t: `⚠ ${this.evtTypeText(p.evt.type)}`, c: this.colors.evtDot, b: false }] : []),
    ];
    ctx.font = '11px system-ui';
    let maxW = 0;
    for (const l of lines) maxW = Math.max(maxW, ctx.measureText(l.t).width);
    const pad = 10, lh = 17, tw = maxW + pad * 2, th = lines.length * lh + pad * 2;
    let tx = x + 12, ty = y - th - 8;
    if (tx + tw > this.W - 4) tx = x - tw - 12;
    if (ty < 4) ty = y + 12;

    // 卡片背景
    ctx.fillStyle = 'rgba(22,27,34,0.96)';
    ctx.strokeStyle = this.colors.grid;
    ctx.lineWidth = 1;
    this.roundRect(tx, ty, tw, th, 8);
    ctx.fill(); ctx.stroke();

    // 文本
    ctx.textAlign = 'left';
    lines.forEach((l, i) => {
      ctx.fillStyle = l.c;
      ctx.font = l.b ? '700 12px system-ui' : '11px system-ui';
      ctx.fillText(l.t, tx + pad, ty + pad + i * lh + 12);
    });
  }

  renderStats(L) {
    if (!this.data.length) return;
    const { ctx } = this;
    const cur = this.data[this.data.length - 1];
    const temps = this.data.map(p => p.tmp).filter(t => t > 0);
    const peak = Math.max(...temps);
    const min = Math.min(...temps);
    const avg = temps.reduce((a, b) => a + b, 0) / temps.length;
    const chargeTemps = this.data.filter(p => p.status === 'Charging').map(p => p.tmp);
    const chargeAvg = chargeTemps.length ? chargeTemps.reduce((a,b)=>a+b,0)/chargeTemps.length : 0;
    const idleTemps = this.data.filter(p => p.status !== 'Charging').map(p => p.tmp);
    const idleAvg = idleTemps.length ? idleTemps.reduce((a,b)=>a+b,0)/idleTemps.length : 0;

    const items = [
      { label: '当前', val: cur.tmp.toFixed(1) + '°C', c: this.colors.battery },
      { label: '峰值', val: peak.toFixed(1) + '°C', c: peak >= this.opts.tempLimit ? this.colors.warn : this.colors.txt },
      { label: '最低', val: min.toFixed(1) + '°C', c: this.colors.ic },
      { label: '平均', val: avg.toFixed(1) + '°C', c: this.colors.txt2 },
      ...(chargeAvg > 0 ? [{ label: '充电时', val: chargeAvg.toFixed(1) + '°C', c: this.colors.battery }] : []),
      ...(idleAvg > 0 ? [{ label: '放电时', val: idleAvg.toFixed(1) + '°C', c: this.colors.txt2 }] : []),
    ];

    const y = L.padT + L.h + 38;
    ctx.font = '600 11px system-ui';
    let x = L.padL;
    for (const it of items) {
      ctx.fillStyle = this.colors.txt2;
      ctx.fillText(it.label, x, y);
      x += ctx.measureText(it.label).width + 6;
      ctx.fillStyle = it.c;
      ctx.font = '700 12px system-ui';
      ctx.fillText(it.val, x, y);
      x += ctx.measureText(it.val).width + 16;
      ctx.font = '600 11px system-ui';
    }
  }

  renderEmpty(L) {
    const { ctx } = this;
    ctx.fillStyle = this.colors.txt2;
    ctx.font = '12px system-ui';
    ctx.textAlign = 'center';
    ctx.fillText('暂无温度数据', this.W / 2, L.padT + L.h / 2);
  }

  roundRect(x, y, w, h, r) {
    const { ctx } = this;
    ctx.beginPath();
    ctx.moveTo(x + r, y);
    ctx.arcTo(x + w, y, x + w, y + h, r);
    ctx.arcTo(x + w, y + h, x, y + h, r);
    ctx.arcTo(x, y + h, x, y, r);
    ctx.arcTo(x, y, x + w, y, r);
    ctx.closePath();
  }

  evtTypeText(t) {
    return { temp_exceed: '电池过温', chg_temp_exceed: '充电IC过温',
             recovered: '已恢复充电', replug_reset: '重新插电' }[t] || t;
  }

  /* ── 交互 ─────────────────────────────────────────────────── */
  onMove(e) {
    const r = this.cv.getBoundingClientRect();
    const mx = e.clientX - r.left;
    const L = this.layout();
    const soc = this.socOf(mx, L);
    // 找最近点
    let best = -1, bestD = Infinity;
    for (let i = 0; i < this.data.length; i++) {
      const d = Math.abs(this.data[i].soc - soc);
      if (d < bestD) { bestD = d; best = i; }
    }
    if (best !== this.hoverIdx) {
      this.hoverIdx = best;
      this.scheduleRender();
    }
  }

  onClick(e) {
    if (this.hoverIdx >= 0 && this.hoverIdx < this.data.length) {
      const p = this.data[this.hoverIdx];
      if (p.evt && this.opts.onEventClick) this.opts.onEventClick(p.evt);
    }
  }
}
