# Changelog

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

---

## v2.0.0 (2026-10-30)

### 🎉 大版本：只保留模块 + WebUI，一步到位

用户决策：「计划 2.0，要一步到位」+ A~E 五项全做、不分阶段。

---

### A. 彻底移除 App

**删除**：
- `app/` 整个目录 —— **86 MB / 2101 个文件 / 106 个 .kt / 11099 行**
- `.github/workflows/build-app.yml`
- `customize.sh` 里的 APK 安装逻辑（-2596 字节）
- `.gitignore` 里的 app/ 规则

**理由**（用户 2026-10-28 确认）：
- App 里 **42%（4612 行）** 是 HDR 夜光（2807 行）+ 视觉特效（1805 行）+ 悬浮窗（451 行）
- 这三块用户明确**不要**
- WebUI 改 PWA 后可完全替代，维护两套不划算

**效果**：仓库 86 MB → **2.8 MB**

---

### B. WebUI 功能对齐

App 的 19 个卡片里，WebUI 已有对应内容。**补上缺的 2 个**：

| 字段 | 说明 |
|---|---|
| **电流倍率** | 双电芯机型显示 `双电芯 ×2` / `单电芯 ×1` |
| **省电模式** | 读 `oem_power_save` |

**WebUI 反而比 App 多的**：手动充满 / 手动暂停 / 高级控制 / 电池校准 / 运行日志

---

### C. UI 优化（补齐并实测）

之前 v1.19.2 加的四项，这次**在浏览器里逐项验证**：

| 项 | 实测结果 |
|---|---|
| 卡片拖拽排序 | 4 卡片 + 调整布局栏 ✅ |
| 仪表盘折叠详情 | `toggleFold` false→true ✅ |
| 统计页二级 tab | 曲线组 3 panel / 事件组 3 panel ✅ |
| 设置页滑块大字 | 4 处 22px ✅ |

---

### D. 数据可视化升级（三项新功能）

#### D1. SOH 健康趋势曲线
- Canvas 自绘，按天数画衰减
- 80% 警戒线（健康分界）
- 曲线颜色随健康度变化（≥90 绿 / ≥80 琥珀 / <80 红）
- 底部统计：首记 / 当前 / 衰减 / 循环

**实测**：canvas 600×300，绘制 7097 像素，文案 `7 天 · 55 循环` ✅

#### D2. 充电热力分布
- 10 格（每格 10% 电量），显示该段**最高温**
- 颜色映射：28°C 蓝 → 45°C 红
- hover 显示「最高 X°C / 平均 Y°C / N 点」

**实测**：10 个格子正确渲染 ✅

#### D3. 今日停充时间线
- 24 小时横轴，色块标出每次停充的**位置和时长**
- 顶部汇总：按原因分组（次数 + 总时长）
- 颜色与事件列表统一（temp 红 / limit 蓝 / paused 紫…）

**实测**：panel 存在，无数据时显示「今日暂无停充记录」✅

---

### E. 文档 + 稳定性

#### README 重写
- 去掉所有 App 相关内容
- 补齐：特性清单 / 安装步骤 / 首次使用建议 / 数据存放 / API 速查 / 已知限制
- 明确写清「为什么不用 CC/CV 判阶段」

#### docs/API.md 补充
新增 v1.17~v2.0 的端点与字段：
- `/api/events` / `/api/event?id=` / `DELETE /api/events`
- `/api/charge_profile` / `/api/eta?to=N`
- status 的 7 个 suspend 字段
- 静态文件 MIME 表

#### 稳定性检查（7/7 通过）
```
✅ strdup/malloc 判空
✅ fopen 判空
✅ write_str 返回值检查
✅ 慢客户端超时 (SO_RCVTIMEO)
✅ 事件 id 路径穿越防护
✅ JSON 转义
✅ 环形缓冲边界
```

#### update.json
之前还停留在 **v1.16.3**（漏更新），现已到 v2.0.0。

---

### 🔴 顺便根治了一个老 bug

**`RangeError: Maximum call stack size exceeded`**

这个错误从 v1.17.0 就存在，一直没定位到。本轮终于查明：

**根因**：`chart: null` / `curveChart: null` 声明在 Alpine 的 `x-data` 返回对象里。
Alpine 3 会对该对象做**深度 reactive 代理**，而 Chart.js 实例内部有大量循环引用
（canvas↔ctx↔chart↔scale、options 回调互相引用…），代理遍历时无限递归。

**修复**：把 Chart 实例挪到模块级变量 `CH`（在 `x-data` 之外），
并删掉 x-data 里的两个声明。共转换 **18 处** `this.chart` / `this.curveChart`。

之前我加过 `CH` 外部持有者，但**只改了一部分引用**，且声明还留在 x-data 里，
所以 Alpine 依然给那两个键建了响应式槽位 —— 这次彻底解决了。

---

### 验证

| 项 | 结果 |
|---|---|
| aarch64 编译 | ✅ 778 KB，零新增警告 |
| 冒烟测试 | ✅ 全绿（240+ 字段 / 6 端点 / 无内存泄漏） |
| 浏览器实测 | ✅ **零 JS 错误** |
| HTML 结构 | ✅ div 207/207 / template 4/4 / script 5/5 |
| 包大小 | 约 400 KB（不含 App） |

---

### 升级说明

从 1.19.x 升级：
1. 装 `ChargeControl.zip`（约 400 KB）
2. 装完约 10~20 秒自动生效，**无需重启**
3. WebUI 刷新即见新界面

⚠️ **App 不再随模块安装**。需要旧版 App 请从 v1.19.4 及更早 Release 下载。
但建议直接用 PWA：浏览器打开 `http://127.0.0.1:8800` → 添加到主屏幕。


## v1.19.5 (2026-10-29)

### 📦 包体瘦身：13MB → 约 1MB（去掉 App APK）

用户反馈：「是不是又把 app 打包在一起了，14mb 的包」

**实测包内构成**：
| 文件 | 大小 | 占比 |
|---|---|---|
| **ChargeControl.apk** | **14,459,363 B** | **94%** |
| bin/battd | 796,024 B | 5% |
| webroot/index.html | 75,992 B | 0.5% |
| 其他（脚本+图标+js） | ~50,000 B | 0.3% |

**真模块本体只有约 930KB，APK 占了 14.4MB。**

而 App 里 42%（4612 行）是 HDR 夜光 + 视觉特效 + 悬浮窗 —— 用户已明确不要。
WebUI 也早已改 PWA 可替代。

### 改动
**build-module.yml**：
- 删掉 `Set up JDK 17` / `Set up Android SDK` / `Gradle cache` / `Build App APK`（33 行）
- `Build module zip` 不再 `cp APK`，改为 `rm -f module/ChargeControl.apk`
- `Verify` 增加：确认**不含 APK**、**包体 < 2MB**（防止回归）
- `Create Release` 只上传 zip + sha256

`build` job 从 14 步 → 9 步，CI 也快了（不用跑 Gradle）。

**App 仍然可用**：`build-app.yml` 独立构建，需要 App 的从那里下载。
`customize.sh` 找不到 APK 时打印「zip 内未找到 APK，跳过」，不报错。

### 预期结果
| | 旧包 | 新包 |
|---|---|---|
| 大小 | 12.99 MB | **约 1 MB** |
| 下载耗时（10Mbps） | ~10s | **~1s** |
| 热更新耗时 | 长 | 短 |


## v1.19.4 (2026-10-29)

### 🎯 真正的热更新（参考 FileManager 的实现方式）

用户提示：「/sdcard/下载/压缩包/FileManagerV7.3.1.zip 就是支持热更新的」

#### 拆解 FileManager 的做法
```
customize.sh 末尾:
  nohup sh "$MODPATH/update.sh" "$MODPATH" >/dev/null 2>&1 &
  ui_print "等待安装完成，无需重启"

update.sh:
  PARENT_PID=$PPID
  while [ -d "/proc/$PARENT_PID" ]; do sleep 0.5; done   # 等安装器退出
  kill 旧进程 → rm -rf 旧目录 → cp -a $MODPATH → 启动新的
```

#### ⚠️ 实测发现 FileManager 的 $PPID 方案不可靠
本机实测（三种启动方式都一样）：
```
nohup &        → PPID=1
setsid &       → PPID=1
nohup setsid & → PPID=1
```
`$PPID` 被 init 收养成 1，而 `/proc/1` 永远存在 ⇒ **死循环**。
（FileManager 能用是因为它的 customize.sh 被 source，$PPID 恰是安装器；
  不同管理器的 source 行为不同，不可依赖。）

#### 改进：三重判据
```sh
# 1. 固定等待 3 秒（安装器通常立即结束）
sleep 3
# 2. 轮询安装器进程是否还在（最多 30s）
#    匹配 --install-module / module install / apd install / pm install
# 3. 文件稳定性：连续 2 次采样 size+mtime 的 md5 不变
#    → 确认安装器不再写 $MODPATH
```

#### 完整流程（新增 module/update.sh）
```
① 管理器解压 zip 到 $MODPATH
② 执行 customize.sh（节点探测 + APK 安装 + 设权限）
③ customize.sh 末尾 nohup 拉起 update.sh（脱离安装器）
④ 安装器退出 → 管理器显示"安装成功"
⑤ update.sh 等文件稳定
⑥ 停 supervisor → 停 battd
⑦ 备份旧模块目录 → cp -a 新目录 → 原子重命名
⑧ 同步运行目录（battd / webroot / supervisor.sh）
⑨ 启动 supervisor → 验证新进程 → 失败则回滚
```

**对比 v1.19.3**：
| | v1.19.3 | v1.19.4 |
|---|---|---|
| 做法 | customize.sh 里直接 cp + kill | 独立 update.sh 后台执行 |
| 竞态 | ⚠️ 安装器还在写 $MODPATH | ✅ 等文件稳定后 |
| 更新范围 | 只换 battd + webroot | ✅ **整个模块目录** |
| 回滚 | ❌ 无 | ✅ 失败自动回滚 |
| 日志 | ❌ 无 | ✅ hot-update.log |

#### 实测（模拟完整安装流程）
```
旧: v1.0
  ↓
✅ 8s 后新进程起来 (PID 1523293 → 1525414)

模块版本:    v2.0        ✅
模块 webroot: v2 NEW     ✅
运行 webroot: v2 NEW     ✅
manifest:    新增文件也同步 ✅
```
日志：`等待安装器收尾 → 文件已稳定 → 停 supervisor → 停 battd → 备份 → 替换 → 同步 → 启动`


## v1.19.3 (2026-10-29)

### 🔴 修复：装模块必须重启手机才生效（用户明确抱怨）

**根因（Linux 进程语义）**：
`customize.sh` 里**已经**在复制新文件了：
```sh
cp "$MODPATH/bin/battd" /data/adb/battery-manager/battd
```
但**没有杀掉正在运行的旧 battd**。而 Linux 进程运行的是**内存里的代码**，
磁盘文件被替换不影响运行中的进程 ⇒ 装了新模块，旧进程还在跑旧代码。

之前 `customize.sh` 末尾那句 `ui_print "模块已就绪（battd 自动启动，无需重启）"`
是**假的** —— 只是复制了文件，没让新代码生效。

### 调研过的方案
| 方案 | 免重启 | 说明 |
|---|---|---|
| LSPosed 的 customize.sh | ❌ | 依赖 Zygisk 注入，必须重启 |
| aplm123.root.mcp 的 `auto` 模式 | ❌ | 「已在跑就跳过」，同样要重启 |
| **杀旧进程让 supervisor 自动拉起** | ✅ | **采用** |

关键发现：`supervisor.sh` 主循环是
```sh
while true; do battd_running || start_battd; sleep 15; done
```
⇒ **杀掉旧 battd 后它 15 秒内自动拉起新的**（从磁盘读），不用碰 supervisor。

### 实现（customize.sh）
```sh
# 1. 遍历 /proc/*/cmdline 找出 battery-manager/battd 的进程
#    （校验 cmdline 防 PID 复用误杀）
# 2. SIGTERM 优雅退出（让它写日志/保存 stats），2 秒后 SIGKILL
# 3. rm -f battd.pid（避免 supervisor 误判「还在跑」）
# 4. 轮询最多 20 秒确认新进程起来
# 5. 起不来则兜底手动 nohup 拉起
# 6. 用 sha256 确认磁盘上的 battd 已是新版
```

### 实测（模拟环境，用假 battd 验证）
```
初始:  旧 PID 1194422
替换:  battd 换成 v2
杀旧:  kill 1194422
结果:  ✅ 新 PID 1194487，只用了 1 秒
日志:  started battd pid=1194487
```
（1 秒是因为 supervisor 正好在循环中；最坏 15 秒）

### 现在的完整链路
| 时机 | 做什么 |
|---|---|
| **装模块时** | `customize.sh`：复制新文件 + 杀旧进程 → 1~15 秒生效 |
| **开机时** | `service.sh`：sha256 对比同步（v1.19.1） |

两者互补，装完不用重启手机。


## v1.19.2 (2026-10-29)

### 🎨 WebUI 四项 UI 优化（砍 App 后补齐体验）

#### 1️⃣ 卡片拖拽排序（对齐 App 的 sh.calvin.reorderable）
App 用的是 Compose 专用库，无法直接搬到 WebUI。改用 **pointer events 自己实现**
（移动端 HTML5 drag&drop 的 touch 支持极差，dragstart 根本不触发）。

- **长按 400ms** 才进入拖拽（防误触，滚动页面不会误拖）
- 拖拽中：卡片浮起 `scale(1.03) rotate(1deg)` + 紫色描边 + 阴影
- 拖动经过其他卡片时**实时换位**（DOM 重排，实时反馈）
- 松手存 `localStorage.cc_card_order`，刷新后保持
- 「调整布局」按钮切换编辑态，编辑态才显示 ⠿ 手柄
- 触屏 + 鼠标统一用 pointer events

#### 2️⃣ 仪表盘折叠详情（41 字段不再平铺）
「充电详情」+「系统信息」两个 panel 改成点击标题展开/收起，默认收起。
- 标题右侧显示 `展开 ▾ / 收起 ▴`
- 状态存 `localStorage.cc_fold`
- 首屏可视字段从 41 项减到 **19 项**

#### 3️⃣ 统计页拆二级 tab（6 panel → 3+3）
```
[曲线] 充电统计 + 温度分析 + 充电曲线
[事件] 停充状态卡 + 异常事件 + 电池校准
```
- 顶部胶囊式切换器，选中态紫色高亮 + 阴影
- 「事件」tab 带**红点角标**显示事件数
- 停充卡用 `s.suspend_active && subTab==='events'` 双重条件

#### 4️⃣ 设置页滑块大字档位
4 个滑动条的当前值改为 **22px 加粗紫色** + `tabular-nums`（数字等宽，
拖动时不抖动）。原来是 13px 普通字重，拖的时候看不清。

### 验证（puppeteer 实测）
```
拖拽:   4 个卡片 + 调整布局按钮  ✅
折叠:   toggleFold false→true     ✅
二级tab: 2 个 tab，curve 显示 3 panel，events 显示 2 panel ✅
大字档位: 4 处，22px              ✅
JS 错误: 0                        ✅
div/template/script: 185/185, 2/2, 5/5  ✅
```


## v1.19.1 (2026-10-29)

### 🎯 战略转型：砍 App，WebUI 改 PWA

用户决策：
- **HDR 夜光不要了**（App 独有 2807 行）
- **悬浮窗不要了**（App 独有 451 行）
- **WebUI 改造成 PWA**（可加到主屏幕，看起来像 App）

背景：App 共 11099 行，其中 42%（4612 行）是 HDR 夜光 + 视觉特效 + 悬浮窗，
跟电池监控无关。而模块 WebUI 的 17 个 panel 已覆盖 App 的全部功能，
还多了手动充满/手动暂停/高级控制/电池校准。⇒ 维护两套不划算。

### ✨ PWA 支持
新增文件：
- `manifest.json` —— name/display=standalone/icons/shortcuts
- `sw.js` —— Service Worker（Cache First 静态 + Network Only API）
- `icon-192.png` / `icon-512.png` / `icon-maskable-512.png` —— 纯 Python 生成，零依赖

index.html 新增：
- `<link rel="manifest">` + `theme-color`（深/浅两套）
- `apple-mobile-web-app-*`（iOS 全屏）
- `apple-touch-icon` + `icon` 多尺寸
- Service Worker 注册（失败静默，不影响功能）
- 支持 `manifest.shortcuts` 的 `?tab=stats` 直达

**缓存策略**：
| 类型 | 策略 |
|---|---|
| `/api/*` | **Network Only**（绝不缓存，否则看到过期数据） |
| 同源静态 | Cache First + 后台静默更新 |
| 跨域 CDN | Cache First |

### 🔴 修复：service.sh 导致模块升级后 battd 不更新（严重）
```sh
# 原实现
if [ ! -x "$DIR/battd" ]; then cp ... fi
```
只在「文件不存在」时复制。升级模块后 `$DIR/battd` 已存在（旧版），
**永远不会被替换** ⇒ 用户装了新模块，实际跑的仍是旧二进制。

实测踩过：模块 `bin/battd` = 796024 字节(10-28)，
数据目录还是 730424 字节(10-17)，进程跑旧的，所有新功能都不生效。

修复：改用 **sha256 对比**，不一致就同步（幂等）。webroot 同样逐文件对比。

### 🔴 修复：manifest.json 被误判成 application/javascript
`strstr(path, ".js")` 会匹配 `manifest.json` 里的 `.js`。
⇒ 把 manifest 判断提到 `.js`/`.json` 之前，返回 `application/manifest+json`。
（Chrome 严格要求此 MIME，否则拒绝加载 manifest）

### 验证
```
/manifest.json   → application/manifest+json  ✅
/sw.js           → application/javascript      ✅
/icon-192.png    → image/png                   ✅
/                → text/html                   ✅
浏览器：manifest 解析 OK / SW activated / themeColor #8B5CF6
```


## v1.19.0 (2026-10-28)

### 🎯 充电预估学习（方案 B）+ 高级温度分析

#### 🔬 调研结论：本机不是标准 CC/CV
用 6586 条历史记录（1723 个充电点）实测：
| 电量 | 平均电流 | 说明 |
|---|---|---|
| 50-59% | **200mA**（最低） | |
| 70-79% | **358mA**（又涨回） | 非单调 |
| 100% | **298mA** | 比 50% 时还高，不符合 CV 衰减 |

原因：MTK 智能充电策略（Basic/Pulse + JEITA + 充电到 80% 后来电）动态调流。
⇒ **任何按电流/电压判阶段的做法在本机都不成立**。

#### ✨ 方案 B：从历史数据学
放弃 CC/CV 判定，直接记录「每跨 10% 实际耗时」：
- 充电会话中每跨一个 10% 边界 → 记录该区间耗时
- 拔电/充满 → 结算入库 `charge_profile.json`
- 预估时逐段累加中位数 + 速率修正（当前实测 vs 历史，夹在 0.5~2 倍）
- 样本 <3 → confidence=low，回退线性外推

**实测验证**：
```
75→80   预期 11 分   实际 11 分  ✅
75→100  预期 45 分   实际 45 分  ✅
75→90   预期 26 分   实际 26 分  ✅
20→80   预期 ~45 分  实际 56 分  ✅（含缺失段插值）
```

新 API：
```
GET /api/charge_profile   学习数据
GET /api/eta?to=N         预估剩余时间
  → {"ok":true,"to":80,"from":65,"eta_min":23,"eta_txt":"23分",
     "scale":1.12,"confidence":"high","samples":8}
```

#### 🎨 高级温度分析视图（纯 Canvas 自绘）
- **横轴 = 充电进度（SOC%）**，不用时间轴 —— 直接看「哪个电量段最烫」
- Catmull-Rom 样条平滑（非直线连接）
- 45°C 警戒线 + 红色渐变警戒带
- 事件（温度超限）琥珀色光晕点，点击跳事件详情
- 电池温度 + 充电IC温度双线对比（实线/虚线）
- 悬停十字准星 + 精确值浮层
- 底部统计徽章：当前/峰值/最低/平均/充电时平均/放电时平均
- devicePixelRatio 适配高分屏

#### 🔴 修复：cc_peak 被 0 污染导致永远判不出 CV
实测序列：
```
9~24%   869mA 恒定   st=2  ✅ 真 CC
25%     0mA          st=2  ❌ cc_peak 被污染
91%     139mA        st=2  ❌ 只剩峰值 16%，仍判 CC
```
三处根因（`if (bat_curr > cc_peak)` / `if (cc_peak <= 0)` / `bat_curr > 0` 门槛）已修。
新增 `CUR_VALID_MA=50` 过滤无效采样，`cc_peak` 只在 CC 阶段累积。

#### 🧪 CI 增强
冒烟测试新增覆盖：
- `/api/events` 状态码
- `/api/charge_profile`、`/api/eta`（允许 200/404）
- status 的 7 个 suspend 字段完整性
- `/api/events` 响应结构


## v1.17.0 (2026-10-24)

### 🎯 提醒中心改造（用户要求：砍掉 TG，全走 App 通知）

#### 🔴 核心修复：停充后不自动恢复（根因是 5 个写者互相覆盖）
原 `apply_control()` 里有 **5 个独立写者**各写 `input_suspend`（温度/充IC/paused/full_once/
软旁路/容量上限），谁最后执行谁说了算，导致：
- 温度停充后被容量分支写 0 冲掉
- 温度恢复**没有迟滞、没有保持时间** → 44.9/45.1°C 反复抖
- 充IC 分支温度读不到（<0）时**永久卡在停充**

**改为单一裁决器**：先收集所有停充理由 → 按优先级取最严重的一个 → **只写一次**。
现在全项目只剩 **1 个** `input_suspend` 写者。

优先级：电池过温 > 充IC过温 > 手动暂停 > 手动充满 > 夜间保持 > 软旁路 > 容量上限

**恢复规则**：
- `temp` → `≤ 上限-5°C` **且保持 60s**（可配 `rec_temp_hyst` / `rec_temp_hold`）
- `chg_temp` → `< 上限-5°C`；**温度读不到时放行，不卡死**
- `limit` → `≤ 上限-5%`（`rec_soc_hyst` 可配）
- `paused` / `manual` → 不自动恢复

**⭐ 拔插豁免**：检测 `usb_online` 0→1 ⇒ 清空停充状态 + **10 分钟限充豁免**。
> 没有豁免的话，SOC 仍高于上限会立刻再次停充，用户体感是"插上也没用"。
> 这个坑是实测发现的。

#### ✨ 新增：异常事件 + 曲线快照
- 环形缓冲 `{t, soc, temp, cur}` × 30（10s 一点 = 5 分钟）
- 触发时**强制补一点**，保证快照含触发瞬间
- 落盘 `/data/adb/battery-manager/events/evt-<ts>.json`，滚动保留 20 条
- 事件类型：`temp_exceed` / `chg_temp_exceed` / `recovered` / `replug_reset`

#### ✨ 新增 API
```
GET    /api/events          列表（不含 snapshot，轻量）
GET    /api/event?id=evt-X  单条（含 snapshot）
DELETE /api/events          清空
```
（`id` 严格校验 `evt-<纯数字>`，防路径穿越）

#### ✨ status 新增 7 字段
`suspend_active` / `suspend_reason` / `suspend_since` / `suspend_recover_in` /
`rec_temp_hyst` / `rec_temp_hold` / `rec_soc_hyst`

#### ✨ WebUI
- **停充状态卡**：原因 + 已持续 + 预计恢复倒计时 + 拔插提示
- **异常事件列表**：类型/时间/详情，点击跳到曲线高亮
- **曲线预估虚线**：最近 10 分钟电量斜率外推，显示"预计 N 分钟到 X%"
- 保养上限滑动条改为 **60~100% 步进 5**

#### 🔴 修复：WebUI 结构 bug（原版详情页/设置页绑定全失效）
`index.html` 第 163 行 `</div></div>` 多闭合一个，导致 `x-data` 容器在 164 行就关闭，
**后续所有 tab 的 Alpine 指令都在作用域外**（Alpine 只初始化了 64/471 个元素）。
修复后 174/174 配平，所有 tab 深度正常。

#### 🗑️ 移除：TG/Webhook 推送
- 删 `webhook_url` 全局变量、`extras.json` 字段、`high_temp_alert()` 里的发送逻辑
- status 不再返回 `webhook` 字段
- 理由（用户）：App 有实时曲线 + 本地通知零依赖，TG 重复且多一条故障链路

#### 📱 App v1.17.0
- 新增 `BatteryAlertService`（前台服务，5s 轮询）
  - 4 个通知通道：`chrgc`保养 / `chrgw`异常 / `chrgp`阈值 / `chrgr`恢复
  - 恢复通知**同原因 10 分钟去重**
  - 点异常通知 → 带 `event_id` 跳曲线高亮
- 新增 `BatteryEvent` / `EventSnapshotPoint` 数据模型
- `ChargeControlApi` 新增 `fetchEvents()` / `fetchEvent(id)` / `clearEvents()`
- 设置页新增「充电提醒」：保养上限滑动条（60-100 步进 5）+ 4 开关 + 电量阈值
- 三语言文案（简体/繁体/英文）
