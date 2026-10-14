#!/usr/bin/env bash
# ChargeControl 发布脚本
#   1. 从 module.prop 读当前版本，按参数 bump
#   2. 同步 update.json / CHANGELOG 头部
#   3. commit + tag + push（tag 触发 CI 自动编译并建 Release）
#
# 用法: ./release.sh <patch|minor|major> "更新说明"
set -euo pipefail
BUMP="${1:-patch}"
NOTE="${2:-}"
cd "$(dirname "$0")"

PROP=module/module.prop
CUR_V=$(grep '^version=' "$PROP" | cut -d= -f2 | sed 's/^v//')
CUR_C=$(grep '^versionCode=' "$PROP" | cut -d= -f2)

IFS=. read -r MA MI PA <<< "$CUR_V"
case "$BUMP" in
  major) MA=$((MA+1)); MI=0; PA=0 ;;
  minor) MI=$((MI+1)); PA=0 ;;
  patch) PA=$((PA+1)) ;;
  *) echo "用法: $0 <patch|minor|major> \"说明\""; exit 1 ;;
esac
NEW_V="v$MA.$MI.$PA"
NEW_C=$((CUR_C+1))

echo "→ $CUR_V ($CUR_C)  ⇒  $NEW_V ($NEW_C)"

sed -i "s/^version=.*/version=$NEW_V/"               "$PROP"
sed -i "s/^versionCode=.*/versionCode=$NEW_C/"       "$PROP"

python3 - "$NEW_V" "$NEW_C" <<'PY'
import json, sys
v, c = sys.argv[1], int(sys.argv[2])
for p in ("update.json", "module/update.json"):
    d = json.load(open(p))
    d["version"] = v
    d["versionCode"] = c
    json.dump(d, open(p, "w"), indent=2, ensure_ascii=False)
    open(p, "a").write("\n")
print("update.json 已同步")
PY

# CHANGELOG 头部插入
if [ -n "$NOTE" ]; then
  TMP=$(mktemp)
  { head -1 CHANGELOG.md
    echo
    echo "## $NEW_V ($(date +%F))"
    echo
    echo "- $NOTE"
    tail -n +2 CHANGELOG.md
  } > "$TMP"
  mv "$TMP" CHANGELOG.md
fi

git add -A
git commit -m "release: $NEW_V" -m "${NOTE:-版本更新}" || { echo "无改动"; exit 0; }
git tag -a "$NEW_V" -m "$NEW_V${NOTE:+ — $NOTE}"
git push origin main
git push origin "$NEW_V"
echo "✅ 已推送 $NEW_V，CI 将自动编译并发布 Release"
