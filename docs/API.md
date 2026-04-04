# ChargeControl API Reference

Base URL: `http://<device-ip>:8080`

---

## Battery & Status

### `GET /api/status`
Returns live battery readings.

**Response**
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
Returns live battery data merged with the current configuration.

---

## Charging Control

### `POST /api/charging/enable`
Enable or disable charging.

**Body**
```json
{ "enabled": true }
```

### `POST /api/charging/limit`
Set the maximum charge percentage (0–100).

**Body**
```json
{ "limit": 80 }
```

### `POST /api/charging/mode`
Set the active charging mode.

**Body**
```json
{ "mode": "normal" }
```

Valid modes: `normal`, `fast`, `trickle`, `power_saving`, `super_saver`

### `POST /api/charging/temperature-check`
Run temperature protection logic immediately.

**Response**
```json
{
  "temperature": 38.0,
  "threshold": 40,
  "critical": 45,
  "action": "none"
}
```

Possible `action` values: `none`, `throttled_to_trickle`, `charging_stopped`, `charging_resumed`

---

## Configuration

### `GET /api/config`
Returns the full `config.json` content.

### `POST /api/config`
Replace the full configuration with the supplied JSON object.

---

## Modes

### `GET /api/modes`
Returns all configured charging modes and their parameters.

---

## Statistics

### `GET /api/stats/daily?days=7`
Per-day stats for the last N days.

### `GET /api/stats/weekly`
Per-week stats for the last 12 weeks.

### `GET /api/stats/monthly`
Per-month stats for the last 12 months.

### `GET /api/stats/snapshots?limit=60`
Most recent battery snapshots (up to `limit`).

### `GET /api/stats/health`
Battery health score and temperature statistics.

---

## Data Export

### `GET /api/export/csv`
Download all charging sessions as a CSV file.

### `GET /api/export/json`
Download all charging sessions as JSON.

---

## Sessions

### `POST /api/sessions/start`
Mark the beginning of a charging session.

**Body**
```json
{ "level": 40, "mode": "normal" }
```

**Response**
```json
{ "session_id": 5 }
```

### `POST /api/sessions/end`
Mark the end of a charging session.

**Body**
```json
{ "session_id": 5, "level": 80, "max_temp": 36.2 }
```
