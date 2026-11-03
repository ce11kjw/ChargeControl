#!/usr/bin/env bash
# 检测前端里的「编造数据」代码模式
# 背景：2026-11-01 发现寿命卡用 Math.round(est_cycles_left/300) 得出「还能约 8 年」，
#       而 est_cycles_left 本身是用 58 次循环掉 1%（取整噪声）拟合出来的。两头都是假的。
# 用法：bash scripts/lint-fake.sh [file]   默认检查 module/webroot/index.html

set -u
F="${1:-module/webroot/index.html}"
FAIL=0

[ -f "$F" ] || { echo "找不到 $F"; exit 2; }
echo "扫描: $F ($(wc -c < "$F") 字节)"
echo

hit() {  # hit <正则> <说明>
  local out
  out=$(grep -nE "$1" "$F" 2>/dev/null | head -3)
  if [ -n "$out" ]; then
    echo "  [FAIL] $2"
    echo "$out" | sed 's/^/         /'
    FAIL=$((FAIL+1))
  else
    echo "  [ OK ] $2"
  fi
}

echo "── 1. 禁止用魔数把循环数换算成年份 ──"
hit 'Math\.round\([^)]*/[[:space:]]*(300|365|250|400)\)' "魔数除法推算年份/月数（应改为按 cycling.jsonl 实测速率）"

echo "── 2. 禁止无依据的寿命文案 ──"
hit '还能约' "出现「还能约」文案（数据不足时必须显示「积累中」）"

echo "── 3. 禁止直接对 est_cycles_left 做除法 ──"
hit 'est_cycles_left[^;]{0,40}/[[:space:]]*[0-9]' "est_cycles_left 被常数相除"

echo "── 4. 设计容量必须来自 API，不能硬编码 ──"
HARD=$(grep -nE '\b(5261|5380)\b' "$F" 2>/dev/null | grep -vE 'fallback|//|/\*|\* ' | head -3)
if [ -n "$HARD" ]; then
  echo "  [WARN] 硬编码容量数值（应读 s.charge_full_mah / s.design_mah）"
  echo "$HARD" | sed 's/^/         /'
else
  echo "  [ OK ] 无硬编码容量"
fi

echo "── 5. 空值/无效值不能直接渲染成数字 ──"
# 判据：这些字段可能是 -1（无效），同行没有比较运算就直接拼进字符串 = 会显示 -1
BAD5=""
for k in health_degrade gpu_load est_cycles_left est_full_min; do
  L=$(grep -n "$k" "$F" 2>/dev/null | grep -vE "$k[[:space:]]*(>=|>|<|===)" | grep -vE '/\*|^[0-9]+:[[:space:]]*//' | head -2)
  [ -n "$L" ] && BAD5="$BAD5$L
"
done
if [ -n "$BAD5" ]; then
  echo "  [FAIL] 可能把无效值(-1)直接显示（同行应有 >=0 判断）"
  echo "$BAD5" | sed 's/^/         /' | head -6
  FAIL=$((FAIL+1))
else
  echo "  [ OK ] 无效值都有判空"
fi

echo
if [ "$FAIL" -eq 0 ]; then echo "✅ 通过"; else echo "❌ $FAIL 项失败"; fi
exit $FAIL
