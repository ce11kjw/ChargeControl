/* ChargeControl - 主脚本 */

const API_BASE = window.location.origin;

// ============================================
// 页面切换功能
// ============================================
function initTabSwitching() {
    const navItems = document.querySelectorAll('.nav-item');
    const tabPanels = document.querySelectorAll('.tab-panel');
    
    navItems.forEach(item => {
        item.addEventListener('click', () => {
            const tabName = item.dataset.tab;
            
            // 更新导航状态
            navItems.forEach(n => n.classList.remove('active'));
            item.classList.add('active');
            
            // 更新面板显示
            tabPanels.forEach(panel => {
                panel.classList.remove('active');
                if (panel.id === `tab-${tabName}`) {
                    panel.classList.add('active');
                }
            });
            
            console.log('切换到标签页:', tabName);
        });
    });
}

// ============================================
// SSE 客户端
// ============================================
let eventSource = null;

function connectSSE() {
    if (eventSource) {
        eventSource.close();
    }
    
    eventSource = new EventSource(`${API_BASE}/api/events`);
    
    eventSource.onopen = () => {
        console.log('SSE 连接成功');
        document.getElementById('connectionStatus').textContent = '● 已连接';
        document.getElementById('connectionStatus').classList.add('online');
    };
    
    eventSource.onmessage = (event) => {
        try {
            const data = JSON.parse(event.data);
            updateUI(data);
        } catch (e) {
            console.error('解析数据失败:', e);
        }
    };
    
    eventSource.onerror = () => {
        console.log('SSE 连接断开');
        document.getElementById('connectionStatus').textContent = '● 离线';
        document.getElementById('connectionStatus').classList.remove('online');
        setTimeout(connectSSE, 3000);
    };
}

// ============================================
// UI 更新
// ============================================
function updateUI(data) {
    // 电量
    if (data.capacity != null) {
        document.getElementById('batteryPercent').textContent = data.capacity;
        document.getElementById('batteryBar').style.width = `${data.capacity}%`;
    }
    
    // 状态
    if (data.status) {
        const statusMap = {
            'Charging': '充电中',
            'Discharging': '放电中',
            'Full': '已充满',
            'Not charging': '未充电',
            'Unknown': '未知'
        };
        document.getElementById('chargeStatus').textContent = statusMap[data.status] || data.status;
    }
    
    // 功率计算
    if (data.voltage_mv && data.current_ma) {
        const power = (data.voltage_mv * data.current_ma) / 1000000;
        document.getElementById('chargePower').textContent = `${power.toFixed(1)}W`;
        document.getElementById('power').textContent = power.toFixed(1);
    }
    
    // 温度
    if (data.temperature != null) {
        document.getElementById('batteryTemp').textContent = `${data.temperature}°C`;
    }
    
    // 健康
    if (data.health) {
        document.getElementById('healthStatus').textContent = data.health;
    }
    
    // 电压
    if (data.voltage_mv != null) {
        document.getElementById('voltage').textContent = (data.voltage_mv / 1000).toFixed(3);
        document.getElementById('voltageBar').style.width = `${(data.voltage_mv / 5000) * 100}%`;
    }
    
    // 电流
    if (data.current_ma != null) {
        document.getElementById('current').textContent = Math.abs(data.current_ma).toFixed(0);
        document.getElementById('currentBar').style.width = `${(Math.abs(data.current_ma) / 5000) * 100}%`;
    }
    
    // 芯片温度
    if (data.chip_temp != null) {
        document.getElementById('chipTemp').textContent = data.chip_temp.toFixed(0);
        document.getElementById('chipTempBar').style.width = `${(data.chip_temp / 80) * 100}%`;
    }
}

// ============================================
// 充电模式切换
// ============================================
function initModeSwitching() {
    const modeBtns = document.querySelectorAll('.mode-btn');
    
    modeBtns.forEach(btn => {
        btn.addEventListener('click', async () => {
            const mode = btn.dataset.mode;
            
            // 更新UI
            modeBtns.forEach(b => b.classList.remove('active'));
            btn.classList.add('active');
            
            // 发送请求
            try {
                const response = await fetch(`${API_BASE}/api/charging/mode`, {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ mode })
                });
                
                if (response.ok) {
                    showToast(`已切换到${btn.querySelector('.mode-name').textContent}模式`);
                }
            } catch (e) {
                console.error('切换模式失败:', e);
            }
        });
    });
}

// ============================================
// Toast 提示
// ============================================
function showToast(message) {
    const toast = document.getElementById('toast');
    toast.textContent = message;
    toast.classList.add('show');
    setTimeout(() => toast.classList.remove('show'), 2000);
}

// ============================================
// 主题切换
// ============================================
function initThemeToggle() {
    const themeBtn = document.getElementById('themeToggle');
    themeBtn.addEventListener('click', () => {
        const isDark = document.body.style.getPropertyValue('--bg-primary') !== '#f0f0f0';
        if (isDark) {
            document.documentElement.style.setProperty('--bg-primary', '#f0f0f0');
            document.documentElement.style.setProperty('--text-primary', '#1a1a1a');
            document.documentElement.style.setProperty('--text-secondary', '#666');
            document.documentElement.style.setProperty('--glass-bg', 'rgba(255, 255, 255, 0.8)');
            document.documentElement.style.setProperty('--glass-border', 'rgba(0, 0, 0, 0.1)');
            themeBtn.textContent = '☀️';
        } else {
            document.documentElement.style.setProperty('--bg-primary', '#0a0a1a');
            document.documentElement.style.setProperty('--text-primary', '#ffffff');
            document.documentElement.style.setProperty('--text-secondary', '#a0a0b0');
            document.documentElement.style.setProperty('--glass-bg', 'rgba(255, 255, 255, 0.05)');
            document.documentElement.style.setProperty('--glass-border', 'rgba(255, 255, 255, 0.1)');
            themeBtn.textContent = '🌙';
        }
    });
}

// ============================================
// 初始化
// ============================================
document.addEventListener('DOMContentLoaded', () => {
    console.log('ChargeControl 初始化中...');
    
    initTabSwitching();
    initModeSwitching();
    initThemeToggle();
    connectSSE();
    
    console.log('ChargeControl 初始化完成');
});
