# ChargeControl API 参考文档

基础 URL：`http://127.0.0.1:8080`

---

## 电池与状态

### `GET /api/status`
返回实时电池读数。

**响应**
```json
{
  "capacity": 75,
  "status": "Charging",
  "health": "Good",
  "temperature": 32.5,
  "voltage_mv": 3940.0,
  "current_ma": 1850.0,
  "charging_enabled": true,
  "timestamp": "2025-01-01T12:00:00Z"
}
```

### `GET /api/settings`
返回实时电池数据与当前配置的合并结果。

---

## 充电控制

### `POST /api/charging/enable`
启用或禁用充电。

**请求体**
```json
{ "enabled": true }
```

### `POST /api/charging/limit`
设置最大充电百分比（0–100）。

**请求体**
```json
{ "limit": 80 }
```

### `POST /api/charging/mode`
设置当前充电模式。

**请求体**
```json
{ "mode": "normal" }
```

有效模式：`normal`, `fast`, `trickle`, `power_saving`, `super_saver`

### `POST /api/charging/temperature-check`
立即执行温度保护逻辑。

**响应**
```json
{
  "temperature": 38.0,
  "threshold": 40,
  "critical": 45,
  "action": "none"
}
```

`action` 可取值：`none`, `throttled_to_trickle`, `charging_stopped`, `charging_resumed`

---

## 配置

### `GET /api/config`
返回完整的 `config.json` 内容。

### `POST /api/config`
用提供的 JSON 对象替换完整配置。

---

## 模式

### `GET /api/modes`
返回所有已配置的充电模式及其参数。

---

## 统计

### `GET /api/stats/daily?days=7`
最近 N 天的每日统计数据。

### `GET /api/stats/weekly`
最近 12 周的每周统计数据。

### `GET /api/stats/monthly`
最近 12 个月的每月统计数据。

### `GET /api/stats/snapshots?limit=60`
最近的电池快照（最多 `limit` 条）。

### `GET /api/stats/health`
电池健康评分及温度统计信息。

---

## 数据导出

### `GET /api/export/csv`
以 CSV 文件形式下载所有充电会话记录。

### `GET /api/export/json`
以 JSON 格式下载所有充电会话记录。

---

## 会话

### `POST /api/sessions/start`
标记充电会话的开始。

**请求体**
```json
{ "level": 40, "mode": "normal" }
```

**响应**
```json
{ "session_id": 5 }
```

### `POST /api/sessions/end`
标记充电会话的结束。

**请求体**
```json
{ "session_id": 5, "level": 80, "max_temp": 36.2 }
```
