// ChargeControl UI 静态 + 逻辑测试（适配 v2.4.0 黑+lime 新界面）
// 老版测试检查 #chart / Chart.js 守卫 / loadHistory，已随 UI 重写而失效。
// 这里改为检查当前实现的关键结构与契约。
import fs from 'fs';

const F = process.env.UI_FILE || 'module/webroot/index.html';
const html = fs.readFileSync(F, 'utf8');
let pass = 0, fail = 0;
const ok = (n) => { console.log('  OK ' + n); pass++; };
const no = (n, why) => { console.log('  XX ' + n + (why ? '  — ' + why : '')); fail++; };
const has = (re, n) => { re.test(html) ? ok(n) : no(n); };

console.log('文件: ' + F + ' (' + html.length + ' 字符)');

console.log('\n-- 1. 页面结构 --');
has(/id="v-over"/, '总览页 v-over');
has(/id="v-curve"/, '曲线页 v-curve');
has(/id="v-ev"/, '事件页 v-ev');
has(/id="v-set"/, '设置页 v-set');
has(/VIEWS\s*=\s*\[/, '导航配置 VIEWS 数组');

console.log('\n-- 2. 关键卡片 --');
const cards = {
  'id="vBat"': '功率路径-电池',
  'id="kpis"': 'KPI 条',
  'id="thermo"': '温度分布',
  'id="hwList"': '硬件耗电列表',
  'id="etaBig"': '充满预测',
  'id="segList"': '分段耗时',
  'id="svgRate"': '充电速率图',
  'id="svgLearn"': '学习曲线',
  'id="lifeLine"': '电池寿命',
  'id="diagPanel"': '系统诊断',
  'id="sl_cap"': '充电上限滑块',
  'id="sl_tmp"': '温度上限滑块',
  'id="tpBtn"': '温度降档开关',
  'id="calStart"': '校准按钮',
  'id="expLoad"': '导出按钮',
};
for (const [k, v] of Object.entries(cards)) has(new RegExp(k.replace(/"/g, '"')), v);

console.log('\n-- 3. 渲染函数 --');
const fns = ['renderThermo','renderProfile','renderStats','renderRate','renderLife',
             'renderDiag','renderPower','renderLearn','renderSessions','renderEvents','renderCal','diagnose'];
for (const f of fns) has(new RegExp('function\\s+' + f + '\\s*\\('), f + '()');

console.log('\n-- 4. 数据源契约 --');
const feeds = ['/api/status','/api/history','/api/events','/api/sessions','/api/curve',
               '/api/calibrate','/api/export','/api/limit',
               '/power.json','/charge_profile.json','/stats.json','/baseline.json','/cycling.json'];
for (const u of feeds) has(new RegExp(u.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')), 'fetch ' + u);

console.log('\n-- 5. 标签配对 --');
const sc = (html.match(/<script/g) || []).length;
const scc = (html.match(/<\/script>/g) || []).length;
sc === scc ? ok(`<script> 配对 (${sc})`) : no(`<script> 不配对 ${sc} vs ${scc}`);
const st = (html.match(/<style/g) || []).length;
const stc = (html.match(/<\/style>/g) || []).length;
st === stc ? ok(`<style> 配对 (${st})`) : no(`<style> 不配对 ${st} vs ${stc}`);

console.log('\n-- 6. 假数据防线（与 lint-fake.sh 呼应）--');
!/还能约/.test(html) ? ok('无「还能约」文案') : no('仍有「还能约」');
!/Math\.round\([^)]*\/\s*(300|365|250|400)\)/.test(html) ? ok('无魔数推年份') : no('仍有魔数推年份');

console.log('\n-- 7. 安全性 --');
// 只查字符串型字段：数字字段拼接无害（s.temp+'<small>°C</small>' 不是 XSS）
const STR_FIELDS = ['suspend_reason','control_state','gpu_model','proto_name','cpu_model','oem_os_name','health_rating','device_model'];
const unescaped = STR_FIELDS.filter(f => new RegExp('innerHTML\\s*=\\s*[^;]*\\bs\\.' + f + '\\s*\\+').test(html));
unescaped.length ? no('字符串字段未转义: ' + unescaped.join(',')) : ok('字符串字段均已转义（数字字段不适用）');
has(/cache:'no-store'|cache: *'no-store'/, 'fetch 带 no-store（避免缓存旧数据）');

console.log('\n结果: ' + pass + ' 通过 / ' + fail + ' 失败');
process.exit(fail ? 1 : 0);
