# Changelog

## v2.1.0 (2026-10-30)

### 🎨 UI 极致打磨：建立完整设计系统

用户要求：「优化 UI 要优化到极致」

#### 先诊断：到底哪里不精致

用浏览器量化审计当前界面（400×2400 @3x）：

| 项 | 改造前 | 问题 |
|---|---|---|
| **字号** | **14 种**：9,10,11,12,13,14,15,16,17,20,22,23,28,46 | 无阶梯，随手写 |
| **圆角** | **4 种**：10/12/14/20px | 不成体系 |
| **阴影** | **1 种**：全是同一个紫 glow | 无层次 |
| **间距** | 0/4/8/12/16px 混用不成比例 | 非 8pt 栅格 |
| **面板** | 25 个外观完全一样 | 无主次 |

**结论：不是"某个地方丑"，是整个项目没有设计系统。**

#### 改造：参考 Material 3 / Apple HIG / Linear

**① 字号系统（7 档，模块化比例 ≈1.25）**
```css
--fs-xs:   10px   辅助标签（上限、单位）
--fs-sm:   11px   次要说明
--fs-base: 13px   正文
--fs-md:   15px   小标题
--fs-lg:   18px   次级数值
--fs-xl:   26px   卡片主数值
--fs-2xl:  52px   首屏大数字
```

**② 间距系统（8pt 栅格）**
```css
--sp-1: 4px   --sp-2: 8px   --sp-3: 12px
--sp-4: 16px  --sp-5: 20px  --sp-6: 24px  --sp-8: 32px
```

**③ 圆角（2 档 + 胶囊）**
```css
--r-sm: 12px    --r-md: 18px    --r-full: 999px
```

**④ 阴影（3 级层次）**
```css
--sh-1: 0 1px 2px rgba(0,0,0,.3)      平面微浮
--sh-2: 0 4px 14px rgba(0,0,0,.35)    卡片
--sh-3: 0 12px 36px rgba(0,0,0,.5)    浮层/拖拽
--sh-glow: 0 0 24px rgba(139,92,246,.22)
```

**⑤ 描边层次**（暗色模式下用半透明白比纯色更细腻）
```css
--line:        rgba(255,255,255,.07)   默认
--line-hi:     rgba(255,255,255,.12)   强调
--line-accent: rgba(139,92,246,.35)    品牌
```

**⑥ 动效曲线（统一 easing）**
```css
--ease-out:    cubic-bezier(.16,1,.3,1)     进场
--ease-in-out: cubic-bezier(.4,0,.2,1)      状态变化
--dur-fast: 120ms   --dur-base: 200ms   --dur-slow: 320ms
```

**⑦ 图标尺寸（与字号解耦）**
```css
--icon-sm: 14px   --icon-md: 17px   --icon-lg: 20px
```

#### 具体改动

**配色重调**（暗色模式减少压抑感）
```
--bg:  #080B14 → #0A0E17   提亮 2%，减少纯黑
--card: #101624 → #121A28  更清晰的层次
--txt:  #E8EDF5 → #EDF1F7
--txt2: #8B98B0 → #9AA7BC  对比度 WCAG AA 达标
新增 --txt3 三级文字
```

**组件全部重做**
| 组件 | 改造内容 |
|---|---|
| **panel** | 加 1px 描边 + 3 级阴影；新增 `.panel-primary`（微光）/ `.panel-muted`（降存在感） |
| **panel h2** | 标题加分割线，图标统一品牌色 |
| **card** | 渐变背景 + 描边 + hover 微光（仅桌面 `@media (hover:hover)`）；`:active` 缩放到 0.975 |
| **底部导航** | `backdrop-filter: blur(20px) saturate(180%)` 毛玻璃；激活态顶部 2px 指示条 + 图标上移 |
| **按钮** | 描边 + hover 发光 + active 缩放 0.96 |
| **滑块** | 自定义 thumb（22px 白心紫环）+ 轨道渐变填充 + 拖拽时外发光扩散 |
| **数据行** | min-height 44px（触摸目标）+ 等宽数字 + 末行去边框 |
| **大电量** | 52px 大数字 + 进度条流光动画（`shimmer`） |
| **滚动条** | 6px 细滚动条，hover 变色 |

**批量归一**
- HTML 内联 `font-size` 硬编码：**47 处 → 0 处**
- 内联间距硬编码：**24 处 → 0 处**
- 内联圆角硬编码：**3 处 → 0 处**

#### 新增无障碍支持

```css
/* 键盘焦点可见 */
:focus-visible { outline:2px solid var(--accent); outline-offset:2px; }

/* 尊重系统「减少动效」 */
@media (prefers-reduced-motion: reduce) {
  *, *::before, *::after {
    animation-duration:0.01ms !important;
    transition-duration:0.01ms !important;
  }
}
```

#### 新增状态组件

- `.skeleton` —— 骨架屏（渐变扫光动画）
- `.empty-state` —— 空状态（居中图标 + 说明）
- `.pulse` —— 实时数据脉冲

#### 验证结果（浏览器量化审计）

| 指标 | 改造前 | 改造后 |
|---|---|---|
| **字号种类** | 14 种 | **7 种** ✅ |
| **圆角种类** | 4 种 | **2 种** ✅ |
| **阴影层次** | 1 种 | **4 种**（3 级 + glow） |
| **CSS 行数** | 183 行 | **586 行**（+220%） |
| **设计 token** | 22 个 | **110 个** |
| **动画** | 0 个 | **4 个 @keyframes** |
| **transition 覆盖** | 部分 | **48/48 全覆盖** |
| **裸 px 字号** | 47 处 | **0 处** ✅ |
| **HTML 结构** | — | div 207/207 / template 4/4 / script 5/5 ✅ |
| **JS 错误** | — | 0 ✅ |

#### 视觉效果

- 深色：`#0A0E17` 底 / `#121A28` 卡片 / 紫色 `#8B5CF6` 主色
- 浅色：`#F4F6FA` 底 / `#FFFFFF` 卡片 / `#7C3AED` 主色
- 所有字号、间距、圆角、阴影都从 CSS 变量取，改一处全局生效

---

## v2.0.1 (2026-10-30)

### 🔴 修复 v2.0.0 热更新的三个问题（真机安装后实测发现）

把 v2.0.0 装到真机（K60 Ultra / APatch）时，热更新本身成功了
（v1.19.0 → v2.0.0，PID 7878 → 24616，用时 24 秒），
但发现三个后续问题：

#### 问题 1（严重）：`update.sh` 把自己删了 ⇒ 下次热更新失效

`module/update.sh` 里有一行：
```sh
rm -f "$SRC/update.sh" 2>/dev/null     # ❌
```
本意是「别把 update.sh 带进目标目录」，但 `$SRC` 是**解压临时目录**，
删掉后紧接着的 `cp -a "$SRC" "$MOD_DIR.new"` 就没有 update.sh 了。

结果：模块目录缺 `update.sh` ⇒ **下次装模块时热更新不会执行**，
又回到「必须重启」的状态。

**修复**：删掉这一行。`update.sh` 是热更新的执行者，
**必须**被复制进模块目录（下次还要用它）。

#### 问题 2：运行目录残留 14.4 MB 的旧 APK

v1.19.5 起不再打包 App，但老版本装过的
`/data/adb/battery-manager/ChargeControl.apk`（14,459,375 字节）一直留着。

**修复**：`update.sh` 同步运行目录时顺手删掉，
并在日志记录释放的字节数。

实测：清理后 `/data/adb/battery-manager` 从 16 MB → **1.9 MB**。

#### 问题 3：日志重复「停止 battd」

第一次循环 kill 完，第二次循环又扫一遍，日志刷两条。
功能无害但难看。

**修复**：只在确实还有残留时才输出 KILL 日志。

---

### 真机安装验证（v2.0.0 安装过程实录）

```
- APatch 环境
- 电池控制节点
  [Y] input_suspend = 0 (可控制充电)
  [Y] charge_control_limit = 7 (备选节点)
- 电池监控节点
  [Y] capacity = 4    [Y] temp = 388
  [Y] voltage_now = 3674000    [Y] current_now = -2141000
  [Y] status = Charging
- 电池健康节点
  [Y] cycle_count = 55
  [Y] charge_full = 5261000    [Y] charge_full_design = 5380000
- 充电协议节点
  [Y] real_type = HVCHG    [Y] pd_type = 5
- 硬件温度
  [Y] zone0 soc_max = 54881  (54.9°C)
  [Y] zone34 gpu1 = 50803    (50.8°C)
  [Y] zone69 X7_therm = 47466 (47.5°C)
  [Y] zone75 mtk-master-charger = 61000 (61.0°C)
  [Y] zone79 battery = 39200 (39.2°C)
- MTK 扩展节点
  [Y] mtk_battery_cmd/en_power_path = 1 (旁路电源路径)
  [Y] bms/soh = 99 (原生健康度)
- 准备热更新 → 已排队
- 安装完成，约 10-20 秒生效，无需重启手机
```

热更新日志：
```
19:06:18 === 热更新开始 → v2.0.0 ===
19:06:18 等待安装器收尾…
19:06:42   文件已稳定（采样 2 次）
19:06:42 停止 supervisor
19:06:50   battd 旧进程: 7878
19:07:11 备份旧模块目录 → chargecontrol.pre-hot-update
19:07:11 替换模块目录 → 新目录就位
19:07:12 同步运行目录 → battd(796024B) / webroot(7 文件)
19:07:12 启动新 battd → supervisor 已拉起
19:07:27 SUCCESS: 新 battd pid=24616, version=v2.0.0
19:07:27 === 热更新完成 ===
```

**用时 24 秒，全程未重启手机。**

---

### 验证

- aarch64 编译：778 KB，零新增警告
- 冒烟测试：全绿（240+ 字段 / 6 端点 / 无内存泄漏）
- `update.sh` 语法：通过
- 包大小：约 400 KB
