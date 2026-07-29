/* ChargeControl - 主脚本 */

const API_BASE = window.location.origin;

// ============================================
// 页面切换
// ============================================
function initTabSwitching() {
    const navItems = document.querySelectorAll('.nav-item');
    
    navItems.forEach(item => {
        item.addEventListener('click', () => {
            navItems.forEach(n => n.classList.remove('active'));
            item.classList.add('active');
            console.log('切换到:', item.dataset.tab);
        });
    });
}

// ============================================
// SSE 连接
// ============================================
let eventSource = null;

function connectSSE() {
    if (eventSource) eventSource.close();
    
    eventSource = new EventSource(`${API_BASE}/api/events`);
    
    eventSource.onopen = () => {
        document.getElementById('connectionStatus').textContent = '● 已连接';
        document.getElementById('connectionStatus').classList.add('online');
    };
    
    eventSource.onmessage = (event) => {
        try {
            const data = JSON.parse(event.data);
            updateUI(data);
        } catch (e) {
            console.error('解析失败:', e);
        }
    };
    
    eventSource.onerror = () => {
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
        document.getElementById('batteryPercent').innerHTML = `${data.capacity}<span>%</span>`;
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
        document.getElementById('voltage').textContent = (data.voltage_mv / 1000).toFixed(2);
    }
    
    // 电流
    if (data.current_ma != null) {
        document.getElementById('current').textContent = Math.abs(data.current_ma).toFixed(0);
    }
    
    // 功率计算
    if (data.voltage_mv && data.current_ma) {
        const power = (data.voltage_mv * data.current_ma) / 1000000;
        document.getElementById('chargePower').textContent = `${power.toFixed(1)} W`;
        document.getElementById('power').textContent = power.toFixed(1);
    }
    
    // 芯片温度
    if (data.chip_temp != null) {
        document.getElementById('chipTemp').textContent = data.chip_temp.toFixed(0);
    }
}

// ============================================
// 充电模式
// ============================================
function initModeSwitching() {
    const modeBtns = document.querySelectorAll('.mode-btn');
    
    modeBtns.forEach(btn => {
        btn.addEventListener('click', async () => {
            const mode = btn.dataset.mode;
            
            modeBtns.forEach(b => b.classList.remove('active'));
            btn.classList.add('active');
            
            try {
                await fetch(`${API_BASE}/api/charging/mode`, {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ mode })
                });
                showToast(`已切换到${btn.querySelector('.mode-name').textContent}模式`);
            } catch (e) {
                console.error('切换失败:', e);
            }
        });
    });
}

// ============================================
// Toast
// ============================================
function showToast(message) {
    const toast = document.getElementById('toast');
    toast.textContent = message;
    toast.classList.add('show');
    setTimeout(() => toast.classList.remove('show'), 2000);
}

// ============================================
// 初始化
// ============================================
document.addEventListener('DOMContentLoaded', () => {
    initTabSwitching();
    initModeSwitching();
    connectSSE();
    console.log('ChargeControl 初始化完成');
});
