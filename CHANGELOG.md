# Changelog

## v1.4.0 (2026-10-14)

### 🔴 修复（都是本地假 sysfs 实测出来的真 bug）
- **`write_str` 返回值语义错误（影响面最大）**：原 `return n`（写入字节数），
  但 20 个调用点里 5 处按 `== 0` 判成功、11 处按 `< 0` 判失败 ⇒
  **所有写入都被判定为失败**：真旁路写完即自我放弃、温度保护/暂停/夜间
  的失败日志全是误报。现统一为成功返 0 / 失败返 -1，并补 `O_TRUNC`。
- **`handle_charging_state()` 从未被调用**：导致 `sess_min`/`sess_mah`/
  `stats_count` 恒为 0 —— v1.3.1 引入的 `charge_counter` 精确统计**从未生效**，
  `stats.json` 也从未落盘。已在主循环接入。
- **恒压点原值备份时机错误**：`x_orig_cv` 仅在"要写之前"才读取，那时节点
  已被上一次写脏；且未校验读失败（-1 也照存）⇒ 关闭功能时还原不回去，
  电池被**永久锁在 4200mV**。改为启动时（任何写入前）抓取一次原厂值。
- **`cv_applied`/`cc_applied` 用函数内 static**：进程重启即归 0，
  "关掉恒压点"的还原分支永远进不去。改为文件级持久 + 启动按 conf 重建。
- **限流功能空转**：原实现把 `current_cmd` 硬编码写成 `"0 0"`，
  与实际限流值无关；且整块被 `access(current_cmd)` 门控 ——
  没有该 proc 节点的机型会**静默失效**。现直接作用于
  `input_current_limit`（三个候选路径依次探测），并在关闭时交还内核。
- **API 键名与 conf 键名不一致**：API 只认 `x_set_cv_en` 等长名，
  conf 存 `xcv` 等短名 ⇒ 重启后 `load_config` 用短名把刚设的长名覆盖成 0，
  高级控制在重启后静默失效。现在两边都接受。
- **高级控制开关未纳入 batt.conf**：原先只存 `extras.json`，
  该文件损坏/丢失即全部丢失。现同时写入主配置。

### ✨ 新增
- `snapshot_orig_cv()`：启动时记录原厂恒压点（带范围校验，读失败不写脏值）
- `release.sh`：版本自增 + 同步 update.json/CHANGELOG + commit + tag + push 一条命令
- CI 新增 tag↔module.prop 一致性校验；产物固定命名 `ChargeControl.zip`

### 🧹 清理
- 仓库根目录 10 个历史打包产物（v1.2.54~v1.3.1，约 4.8MB）移出版本控制
- `.gitignore` 补 `*.zip` / `*.zip.sha256` / `module/bin/battd` / `*.bak-*`
- 修复 3 个编译警告（`-Wmisleading-indentation` ×2、未使用函数 ×1），
  现 `-Wall -Wextra` 零警告

### 🧪 测试（假 sysfs 仿真 K60 Ultra，x86 实测）
- 温度：soc 52 / gpu 58 / chg 47 / case 43，vtskin -274°C 被过滤 ✓
- `sess_mah` 增量：`charge_counter` +11000uAh → 11mAh，停充结算并落盘 `stats.json` ✓
- 高级控制三段：开启 → 重启保持 → 关闭还原（恒压点/限流/旁路 各 3/3）✓

## v1.3.1 (2026-10-13)

### 🔴 修复
- **OTA 改用 APatch/KernelSU 官方更新渠道**：`module.prop` 加 `updateJson`，移除自研下载器（不再依赖 curl，Android 原生无 curl 导致 OTA 永久失效）
- **配置文件范围校验**：防止 `charge_limit=0` 等非法值导致永不充电
- **webhook 改用 busybox wget**：Android 无 curl，告警功能原先静默失效
- **HTTP 循环读**：修复 POST body > 4096 被截断
- **JSON 字符串转义**：sysfs 值含引号/反斜杠时不再破坏 JSON
- **会话电量统计改用硬件 `charge_counter`**：比容量估算精确

### ✨ 新增（全部 opt-in，默认关闭）
- **真旁路（MTK `en_power_path`）**：系统由充电器直供，电池不充不放，带最低电量护栏
- **恒压点调整（`set_cv`）**：4430→4200mV 延长电池寿命，自动备份/还原原值
- **充电电流限流**：降低充电功率降温
- **系统原生夜间充电**：复用固件层，比自研轮询省电
- **系统充电日志展示**：读取 `charge_logger` CSV（含 SOH/内阻/循环）
- **前端「高级控制」面板**：可视化开关

### 🎨 前端
- 检查更新改为提示走管理器渠道
- 新增高级控制面板

## v1.3.0 (2026-10-13)
- MTK 平台适配大修：温度扫描 20→96 + 值域过滤、充电IC 过温保护、PD 协议真值、
  supervisor 守护、健康度直读 `bms/soh`、OTA 链修复、`bypass_soft` off-by-one
