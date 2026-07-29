/* ============================================
   粒子背景系统 - 科技感
   ============================================ */

class ParticleSystem {
    constructor(canvasId) {
        this.canvas = document.getElementById(canvasId);
        this.ctx = this.canvas.getContext('2d');
        this.particles = [];
        this.mouse = { x: 0, y: 0 };
        this.isRunning = false;
        this.frameRate = 60;
        
        this.resize();
        this.init();
        this.setupEvents();
    }
    
    resize() {
        this.canvas.width = window.innerWidth;
        this.canvas.height = window.innerHeight;
    }
    
    init() {
        const count = Math.min(50, Math.floor((this.canvas.width * this.canvas.height) / 20000));
        this.particles = [];
        
        for (let i = 0; i < count; i++) {
            this.particles.push(this.createParticle());
        }
    }
    
    createParticle() {
        return {
            x: Math.random() * this.canvas.width,
            y: Math.random() * this.canvas.height,
            vx: (Math.random() - 0.5) * 0.5,
            vy: (Math.random() - 0.5) * 0.5,
            radius: Math.random() * 2 + 1,
            color: `rgba(0, 212, 255, ${Math.random() * 0.5 + 0.2})`,
            pulse: Math.random() * Math.PI * 2
        };
    }
    
    setupEvents() {
        window.addEventListener('resize', () => {
            this.resize();
            this.init();
        });
        
        document.addEventListener('mousemove', (e) => {
            this.mouse.x = e.clientX;
            this.mouse.y = e.clientY;
        });
        
        document.addEventListener('touchmove', (e) => {
            if (e.touches.length > 0) {
                this.mouse.x = e.touches[0].clientX;
                this.mouse.y = e.touches[0].clientY;
            }
        }, { passive: true });
    }
    
    update() {
        this.particles.forEach(p => {
            // 更新位置
            p.x += p.vx;
            p.y += p.vy;
            
            // 边界反弹
            if (p.x < 0 || p.x > this.canvas.width) p.vx *= -1;
            if (p.y < 0 || p.y > this.canvas.height) p.vy *= -1;
            
            // 鼠标交互
            const dx = this.mouse.x - p.x;
            const dy = this.mouse.y - p.y;
            const dist = Math.sqrt(dx * dx + dy * dy);
            
            if (dist < 150) {
                const force = (150 - dist) / 150;
                p.x -= dx * force * 0.02;
                p.y -= dy * force * 0.02;
            }
            
            // 脉冲动画
            p.pulse += 0.02;
        });
    }
    
    draw() {
        this.ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);
        
        // 绘制连接线
        for (let i = 0; i < this.particles.length; i++) {
            for (let j = i + 1; j < this.particles.length; j++) {
                const dx = this.particles[i].x - this.particles[j].x;
                const dy = this.particles[i].y - this.particles[j].y;
                const dist = Math.sqrt(dx * dx + dy * dy);
                
                if (dist < 120) {
                    const alpha = (1 - dist / 120) * 0.3;
                    this.ctx.beginPath();
                    this.ctx.moveTo(this.particles[i].x, this.particles[i].y);
                    this.ctx.lineTo(this.particles[j].x, this.particles[j].y);
                    this.ctx.strokeStyle = `rgba(0, 212, 255, ${alpha})`;
                    this.ctx.lineWidth = 1;
                    this.ctx.stroke();
                }
            }
        }
        
        // 绘制粒子
        this.particles.forEach(p => {
            const pulseSize = p.radius + Math.sin(p.pulse) * 0.5;
            
            // 发光效果
            this.ctx.shadowBlur = 15;
            this.ctx.shadowColor = 'rgba(0, 212, 255, 0.8)';
            
            this.ctx.beginPath();
            this.ctx.arc(p.x, p.y, pulseSize, 0, Math.PI * 2);
            this.ctx.fillStyle = p.color;
            this.ctx.fill();
            
            // 重置阴影
            this.ctx.shadowBlur = 0;
        });
    }
    
    animate() {
        if (!this.isRunning) return;
        
        if (!document.hidden) {
            this.update();
            this.draw();
        }
        
        requestAnimationFrame(() => this.animate());
    }
    
    start() {
        if (this.isRunning) return;
        this.isRunning = true;
        this.animate();
    }
    
    stop() {
        this.isRunning = false;
    }
}

// 页面可见性控制
document.addEventListener('visibilitychange', () => {
    if (window.particleSystem) {
        if (document.hidden) {
            window.particleSystem.stop();
        } else {
            window.particleSystem.start();
        }
    }
});

// 初始化
window.addEventListener('DOMContentLoaded', () => {
    window.particleSystem = new ParticleSystem('particles');
    window.particleSystem.start();
});
