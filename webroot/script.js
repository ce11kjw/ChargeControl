/* ============================================
   ChargeControl - 主脚本
   SSE + 智能帧率 + 数据获取
   ============================================ */

// API配置
const API_BASE = window.location.origin;

// 充电模式定义
const MODES = {
    turbo:    { icon: '🚀', name: '涡轮加速', power: '240W' },
    normal:   { icon: '🤖', name: '智能模式', power: 'AI' },
    standard: { icon: '🔋', name: '标准充电', power: '20W' },
    trickle:  { icon: '💧', name: '涓流充电', power: '5W' },
    protect:  { icon: '🛡️', name: '电池保护', power: '3W' }
};

// ============================================
// 智能帧率系统
// ============================================
class SmartFrameRate {
    constructor() {
        this.currentFps = 0;
        this.targetFps = 0;
        this.maxFps = 60;
        this.isAnimating = false;
        this.userActive = false;
        this.idleTimeout = null;
        this.lastTime = 0;
        
        this.acceleration = 0.05;
        this.deceleration = 0.02;
        
        this.setupInputListeners();
        this.detectMaxFps();
    }
    
    async detectMaxFps() {
        return new Promise(resolve => {
            let frames = 0;
            const startTime = performance.now();
            
            const count = () => {
                frames++;
                if (performance.now() - startTime < 1000) {
                    requestAnimationFrame(count);
                } else {
                    this.maxFps = Math.min(frames, 165);
                    resolve(this.maxFps);
                }
            };
            
            requestAnimationFrame(count);
        });
    }
    
    setupInputListeners() {
        const events = ['mousedown', 'mousemove', 'touchstart', 'touchmove', 'scroll', 'keydown'];
        
        events.forEach(event => {
            document.addEventListener(event, () => this.onUserActivity(), { passive: true });
        });
    }
    
    onUserActivity() {
        this.userActive = true;
        this.targetFps = this.maxFps;
        
        clearTimeout(this.idleTimeout);
        this.idleTimeout = setTimeout(() => {
            this.onUserIdle();
        }, 500);
    }
    
    onUserIdle() {
        this.userActive = false;
        this.targetFps = 0;
    }
    
    updateFrameRate() {
        if (this.currentFps < this.targetFps) {
            this.currentFps += (this.targetFps - this.currentFps) * this.acceleration;
            if (this.targetFps - this.currentFps < 1) {
                this.currentFps = this.targetFps;
            }
        } else if (this.currentFps > this.targetFps) {
            this.currentFps += (this.targetFps - this.currentFps) * this.deceleration;
            if (this.currentFps < 0.5) {
                this.currentFps = 0;
            }
        }
        
        return Math.round(this.currentFps);
    }
}

// ============================================
// SSE客户端
// ============================================
class SSEClient {
    constructor() {
        this.eventSource = null;
        this.isConnected = false;
        this.reconnectAttempts = 0;
        this.maxReconnectAttempts = 10;
        this.onDataCallback = null;
    }
    
    connect() {
        if (this.isConnected) return;
        
        try {
            this.eventSource = new EventSource(`${API_BASE}/api/events`);
            
            this.eventSource.onopen = () => {
                console.log('SSE连接已建立');
                this.isConnected = true;
                this.reconnectAttempts = 0;
                this.updateConnectionStatus(true);
            };
            
            this.eventSource.onmessage = (event) => {
                try {
                    const data = JSON.parse(event.data);
                    if (this.onDataCallback) {
                        this.onDataCallback(data);
                    }
                } catch (e) {
                    console.error('解析SSE数据失败:', e);
                }
            };
            
            this.eventSource.onerror = () => {
                console.log('SSE连接断开');
                this.isConnected = false;
                this.updateConnectionStatus(false);
                this.reconnect();
            };
        } catch (e) {
            console.error('创建SSE连接失败:', e);
            this.reconnect();
        }
    }
    
    reconnect() {
        if (this.reconnectAttempts >= this.maxReconnectAttempts) {
            console.log('SSE重连次数超限，切换到轮询模式');
            this.startPolling();
            return;
        }
        
        this.reconnectAttempts++;
        const delay = Math.min(1000 * Math.pow(2, this.reconnectAttempts), 30000);
        
        setTimeout(() => {
            if (!this.isConnected) {
                this.connect();
            }
        }, delay);
    }
    
    disconnect() {
        if (this.eventSource) {
            this.eventSource.close();
            this.eventSource = null;
        }
        this.isConnected = false;
    }
    
    updateConnectionStatus(connected) {
        const statusEl = document.getElementById('connectionStatus');
        if (statusEl) {
            statusEl.textContent = connected ? '已连接' : '离线';
            statusEl.className = `status-badge ${connected ? 'online' : 'offline'}`;
        }
    }
    
    // 轮询备用方案
    startPolling() {
        setInterval(async () => {
            if (document.hidden || this.isConnected) return;
            
            try {
                const response = await fetch(`${API_BASE}/api/battery`);
                const data = await response.json();
                if (this.onDataCallback) {
                    this.onDataCallback(data);
                }
            } catch (e) {
                console.error('轮询失败:', e);
            }
        }, 5000);
    }
}

// ============================================
// 数据管理器
// ============================================
class DataManager {
    constructor() {
        this.lastData = null;
        this.history = [];
        this.maxHistory = 100;
    }
    
    updateData(newData) {
        // 检测数据变化
        const changed = this.detectChanges(newData);
        
        // 更新历史
        if (changed) {
            this.history.push({
                timestamp: Date.now(),
                data: { ...newData }
            });
            
            if (this.history.length > this.maxHistory) {
                this.history.shift();
            }
        }
        
        this.lastData = newData;
        return changed;
    }
    
    detectChanges(newData) {
        if (!this.lastData) return true;
        
        const keyFields = ['capacity', 'status', 'temperature', 'voltage_mv', 'current_ma'];
        return keyFields.some(field => this.lastData[field] !== newData[field]);
    }
    
    getPower(voltage_mv, current_ma) {
        if (voltage_mv == null || current_ma == null) return null;
        return (voltage_mv * current_ma) / 1000000;
    }
    
    getChargeSpeed(current_ma) {
        if (current_ma == null) return '--';
        if (current_ma > 3000) return '极速';
        if (current_ma > 1500) return '快速';
        if (current_ma > 500) return '标准';
        return '慢速';
    }
}

// ============================================
// UI更新器
// ============================================
class UIUpdater {
    constructor() {
        this.elements = {};
        this.cacheElements();
    }
    
    cacheElements() {
        const ids = [
            'batteryPercent', 'batteryLiquid', 'chargingBolt', 'chargePower',
            'batteryTemp', 'chargeStatus', 'statusIcon', 'voltage', 'current',
            'power', 'tempValue', 'voltageBar', 'currentBar', 'powerBar', 'tempBar',
            'designCapacity', 'currentCapacity', 'cycleCount', 'healthStatus',
            'manufacturer', 'modelInfo', 'techType', 'cellTemp',
            'chipModel', 'cpuCores', 'cpuFreq', 'chipTemp',
            'chargerType', 'fastCharge', 'maxPower', 'timeLeft',
            'batteryTempValue', 'cpuTempValue', 'gpuTempValue', 'boardTempValue',
            'todayCharges', 'todayUsage', 'avgPower', 'batteryWear'
        ];
        
        ids.forEach(id => {
            this.elements[id] = document.getElementById(id);
        });
    }
    
    updateBatteryStatus(data) {
        // 电量
        if (data.capacity != null) {
            this.setText('batteryPercent', data.capacity);
            if (this.elements.batteryLiquid) {
                this.elements.batteryLiquid.style.height = `${data.capacity}%`;
            }
        }
        
        // 充电状态
        const isCharging = data.status === 'Charging';
        if (this.elements.chargingBolt) {
            this.elements.chargingBolt.classList.toggle('active', isCharging);
        }
        if (this.elements.statusIcon) {
            this.elements.statusIcon.style.color = isCharging ? '#00ff88' : '#ff6b35';
        }
        this.setText('chargeStatus', this.formatStatus(data.status));
        
        // 功率
        const power = window.dataManager.getPower(data.voltage_mv, data.current_ma);
        this.setText('chargePower', power != null ? `${power.toFixed(1)} W` : '-- W');
        
        // 温度
        this.setText('batteryTemp', data.temperature != null ? `${data.temperature}°C` : '-- °C');
        
        // 电压
        if (data.voltage_mv != null) {
            this.setText('voltage', (data.voltage_mv / 1000).toFixed(3));
            this.setBarWidth('voltageBar', (data.voltage_mv / 5000) * 100);
        }
        
        // 电流
        if (data.current_ma != null) {
            this.setText('current', Math.abs(data.current_ma).toFixed(0));
            this.setBarWidth('currentBar', (Math.abs(data.current_ma) / 5000) * 100);
        }
        
        // 功率值
        if (power != null) {
            this.setText('power', power.toFixed(2));
            this.setBarWidth('powerBar', (power / 100) * 100);
        }
        
        // 温度值
        if (data.temperature != null) {
            this.setText('tempValue', data.temperature);
            this.setBarWidth('tempBar', (data.temperature / 60) * 100);
        }
    }
    
    updateExtendedInfo(data) {
        // 基本信息
        this.setTextOrHide('designCapacity', data.charge_full_design ? `${data.charge_full_design} mAh` : null);
        this.setTextOrHide('currentCapacity', data.charge_full ? `${data.charge_full} mAh` : null);
        this.setTextOrHide('cycleCount', data.cycle_count ? `${data.cycle_count} 次` : null);
        this.setTextOrHide('healthStatus', data.health || null);
        
        // 电芯信息
        this.setTextOrHide('manufacturer', data.manufacturer || null);
        this.setTextOrHide('modelInfo', data.model_name || null);
        this.setTextOrHide('techType', data.technology || null);
        this.setTextOrHide('cellTemp', data.cell_temperature ? `${data.cell_temperature}°C` : null);
        
        // 芯片信息
        this.setTextOrHide('chipModel', data.chip_model || null);
        this.setTextOrHide('cpuCores', data.cpu_cores ? `${data.cpu_cores} 核` : null);
        this.setTextOrHide('cpuFreq', data.cpu_freq ? `${data.cpu_freq} MHz` : null);
        this.setTextOrHide('chipTemp', data.chip_temp ? `${data.chip_temp}°C` : null);
        
        // 充电器信息
        this.setTextOrHide('chargerType', data.charger_type || null);
        this.setTextOrHide('fastCharge', data.fast_charge_protocol || null);
        this.setTextOrHide('maxPower', data.max_charger_power ? `${data.max_charger_power}W` : null);
        this.setTextOrHide('timeLeft', data.estimated_time || null);
    }
    
    updateTemperature(data) {
        this.setTextOrHide('batteryTempValue', data.temperature ? `${data.temperature}°C` : '--');
        this.setTextOrHide('cpuTempValue', data.cpu_temp ? `${data.cpu_temp}°C` : '--');
        this.setTextOrHide('gpuTempValue', data.gpu_temp ? `${data.gpu_temp}°C` : '--');
        this.setTextOrHide('boardTempValue', data.board_temp ? `${data.board_temp}°C` : '--');
    }
    
    updateHistory(data) {
        this.setTextOrHide('todayCharges', data.today_charges ? `${data.today_charges}次 / ${data.today_duration || '--'}` : null);
        this.setTextOrHide('todayUsage', data.today_usage ? `${data.today_usage}%` : null);
        this.setTextOrHide('avgPower', data.avg_power ? `${data.avg_power}W` : null);
        this.setTextOrHide('batteryWear', data.battery_wear ? `${data.battery_wear}%` : null);
    }
    
    setText(id, text) {
        if (this.elements[id]) {
            this.elements[id].textContent = text;
        }
    }
    
    setTextOrHide(id, text) {
        if (this.elements[id]) {
            if (text) {
                this.elements[id].textContent = text;
                this.elements[id].classList.remove('unavailable');
            } else {
                this.elements[id].textContent = 'N/A';
                this.elements[id].classList.add('unavailable');
            }
        }
    }
    
    setBarWidth(id, percent) {
        if (this.elements[id]) {
            this.elements[id].style.width = `${Math.min(100, Math.max(0, percent))}%`;
        }
    }
    
    formatStatus(status) {
        const statusMap = {
            'Charging': '充电中',
            'Discharging': '放电中',
            'Full': '已充满',
            'Not charging': '未充电',
            'Unknown': '未知'
        };
        return statusMap[status] || status || '未知';
    }
}

// ============================================
// Toast通知
// ============================================
function showToast(message, duration = 3000) {
    const toast = document.getElementById('toast');
    if (toast) {
        toast.textContent = message;
        toast.classList.add('show');
        setTimeout(() => {
            toast.classList.remove('show');
        }, duration);
    }
}

// ============================================
// 初始化
// ============================================
let smartFrameRate, sseClient, dataManager, uiUpdater;

document.addEventListener('DOMContentLoaded', async () => {
    console.log('ChargeControl 初始化中...');
    
    // 初始化组件
    smartFrameRate = new SmartFrameRate();
    sseClient = new SSEClient();
    dataManager = new DataManager();
    uiUpdater = new UIUpdater();
    
    // 暴露到全局
    window.smartFrameRate = smartFrameRate;
    window.sseClient = sseClient;
    window.dataManager = dataManager;
    window.uiUpdater = uiUpdater;
    
    // 设置SSE数据回调
    sseClient.onDataCallback = (data) => {
        if (dataManager.updateData(data)) {
            uiUpdater.updateBatteryStatus(data);
            uiUpdater.updateExtendedInfo(data);
            uiUpdater.updateTemperature(data);
            uiUpdater.updateHistory(data);
        }
    };
    
    // 连接SSE
    sseClient.connect();
    
    // 模式选择器
    setupModeSelector();
    
    // 主题切换
    setupThemeToggle();
    
    // 刷新按钮
    setupRefreshButton();
    
    // 页面可见性
    document.addEventListener('visibilitychange', () => {
        if (document.hidden) {
            sseClient.disconnect();
        } else {
            sseClient.connect();
        }
    });
    
    console.log('ChargeControl 初始化完成');
});

// ============================================
// 事件设置
// ============================================
function setupModeSelector() {
    const modeCards = document.querySelectorAll('.mode-card');
    
    modeCards.forEach(card => {
        card.addEventListener('click', async () => {
            const mode = card.dataset.mode;
            
            // 更新UI
            modeCards.forEach(c => c.classList.remove('active'));
            card.classList.add('active');
            
            // 发送请求
            try {
                const response = await fetch(`${API_BASE}/api/charging/mode`, {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ mode })
                });
                
                if (response.ok) {
                    showToast(`✅ 已切换到${MODES[mode]?.name || mode}模式`);
                } else {
                    showToast('❌ 切换失败');
                }
            } catch (e) {
                showToast('❌ 网络错误');
            }
        });
    });
}

function setupThemeToggle() {
    const themeBtn = document.getElementById('themeToggle');
    if (!themeBtn) return;
    
    // 加载保存的主题
    const savedTheme = localStorage.getItem('cc-theme') || 'dark';
    document.documentElement.setAttribute('data-theme', savedTheme);
    themeBtn.textContent = savedTheme === 'dark' ? '☀️' : '🌙';
    
    themeBtn.addEventListener('click', () => {
        const current = document.documentElement.getAttribute('data-theme');
        const next = current === 'dark' ? 'light' : 'dark';
        
        document.documentElement.setAttribute('data-theme', next);
        themeBtn.textContent = next === 'dark' ? '☀️' : '🌙';
        localStorage.setItem('cc-theme', next);
    });
}

function setupRefreshButton() {
    const refreshBtn = document.getElementById('refreshBtn');
    if (!refreshBtn) return;
    
    refreshBtn.addEventListener('click', async () => {
        refreshBtn.style.animation = 'spin 1s linear infinite';
        
        try {
            const response = await fetch(`${API_BASE}/api/battery`);
            const data = await response.json();
            dataManager.updateData(data);
            uiUpdater.updateBatteryStatus(data);
            uiUpdater.updateExtendedInfo(data);
            uiUpdater.updateTemperature(data);
            uiUpdater.updateHistory(data);
            showToast('✅ 数据已刷新');
        } catch (e) {
            showToast('❌ 刷新失败');
        }
        
        setTimeout(() => {
            refreshBtn.style.animation = '';
        }, 1000);
    });
}

// Spin动画
const style = document.createElement('style');
style.textContent = `@keyframes spin { from { transform: rotate(0deg); } to { transform: rotate(360deg); } }`;
document.head.appendChild(style);
