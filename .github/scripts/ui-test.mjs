import fs from 'fs';
const html = fs.readFileSync('module/webroot/index.html','utf8');
let pass=0, fail=0;
const chk=(name,cond,extra='')=>{ cond?pass++:fail++; console.log(`  ${cond?'OK':'XX'} ${name}${cond?'':'  '+extra}`); };

console.log('-- 结构完整性 --');
chk('#chart 元素存在',      /<canvas id="chart">/.test(html));
chk('#chartWrap 容器存在',  /id="chartWrap"/.test(html));
chk('趋势面板标题回来',      /fa-chart-line/.test(html) && html.includes('趋势'));
chk('<script> 配对',        (html.match(/<script/g)||[]).length === (html.match(/<\/script>/g)||[]).length);
chk('<style> 配对',         (html.match(/<style/g)||[]).length === (html.match(/<\/style>/g)||[]).length);

console.log('\n-- P0 回归: Chart 不拿到 null --');
chk('不再直接 new Chart(getElementById)', !/new Chart\(document\.getElementById\('chart'\)/.test(html));
chk('改为 new Chart(chartEl)',            /new Chart\(chartEl,/.test(html));
const guardIdx = html.indexOf("const chartEl = document.getElementById('chart')");
const chartIdx = html.indexOf('new Chart(chartEl');
chk('守卫在 new Chart 之前', guardIdx>0 && guardIdx < chartIdx);
chk('守卫有 return', /if \(!chartEl[^)]*\) return;/.test(html));
chk('守卫覆盖 Chart 未加载(CDN离线)', /typeof Chart === 'undefined'/.test(html));

console.log('\n-- 口径检查 --');
chk('无残留「需校准」',      !html.includes('需校准'));
chk('无残留「已超时」',      !html.includes('已超时'));
chk('calibrate 阈值 3800',   html.includes('calibrate < 3800') && !html.includes('calibrate < 3900'));

console.log('\n-- 逻辑模拟: loadHistory 在有无 canvas 时的行为 --');
const m = html.match(/async loadHistory\(\) \{[\s\S]*?\n    \},/);
if(!m){ console.log('  XX 提取 loadHistory 失败'); fail++; }
else {
  global.__chartCtor=0;
  class Chart { constructor(){ global.__chartCtor++; } update(){} }
  const build = (hasChart) => {
    const CH={};
    const document={ getElementById:(id)=> id==='chart' ? (hasChart?{tagName:'CANVAS'}:null) : null,
      documentElement:{}, createElement:()=>({getContext:()=>({createLinearGradient:()=>({addColorStop(){}})})}) };
    const getComputedStyle=()=>({getPropertyValue:()=>'#000'});
    let src = m[0]
      .replace('async loadHistory() {','return async function(){')
      .replace(/\n    \},$/,'\n    };');
    const fn = new Function('CH','document','getComputedStyle','Chart','API_BASE','fetchJson', src)
      (CH, document, getComputedStyle, Chart, '', async()=>[{t:1,c:50,tmp:35}]);
    // loadHistory 内部用 this.fetchJson / this.API_BASE，需绑定一个 app 对象
    const app = { fetchJson: async()=>[{t:1,c:50,tmp:35}], API_BASE: '' };
    return fn.bind(app);
  };
  global.__chartCtor=0;
  await build(true)();
  chk('有 canvas => Chart 创建一次', global.__chartCtor===1, 'actual '+global.__chartCtor);
  global.__chartCtor=0;
  let threw=null;
  try { await build(false)(); } catch(e){ threw=e; }
  chk('无 canvas => 不抛异常', threw===null, threw && threw.message);
  chk('无 canvas => 不创建 Chart', global.__chartCtor===0, 'actual '+global.__chartCtor);
}
console.log('\n结果: '+pass+' 通过 / '+fail+' 失败');
process.exit(fail?1:0);
