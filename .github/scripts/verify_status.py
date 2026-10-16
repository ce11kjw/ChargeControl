#!/usr/bin/env python3
"""校验 /api/status 的字段完整性、能力协商、关键值与类型。"""
import json
import sys

d = json.load(open('/tmp/status.json'))
n = len(d)
print(f"  字段数: {n}")
assert n >= 200, f"字段数异常: {n}"

must = [
    # 能力协商
    'api_version', 'features',
    # 核心电池
    'capacity', 'temp', 'voltage', 'current', 'status',
    'charge_limit', 'temp_limit', 'limit_enabled',
    # 数组
    'cores', 'parts',
    # CPU / 内存 / GPU
    'cpu_arch', 'cpu_cores', 'mem_total', 'gpu_model',
    # OEM / 设备
    'oem_brand', 'dev_soc_chipname',
    # 健康 / 统计
    'health', 'health_rating', 'sess_min', 'stats_count',
    # 高级控制
    'x_pp', 'x_pp_ok',
]
missing = [k for k in must if k not in d]
assert not missing, f"缺字段: {missing}"

assert d['api_version'] >= 1, f"api_version 异常: {d['api_version']}"
feats = d['features'].split(',')
assert len(feats) >= 10, f"features 太少: {len(feats)}"

assert d['capacity'] == 78, f"capacity={d['capacity']}（应为假节点值 78）"
assert 20 <= d['charge_limit'] <= 100, f"charge_limit 越界: {d['charge_limit']}"
assert d['health_rating'] in ('优秀', '良好', '一般', '较差', '需更换', '未知'), \
    f"health_rating 值异常: {d['health_rating']}"
# cpu_arch 曾被内核编号 "8" 污染（v1.10.1 修复）
assert d['cpu_arch'] not in ('8', '7', ''), f"cpu_arch 未映射: {d['cpu_arch']!r}"

cores = d['cores']
assert len(cores) == d['cpu_cores'], \
    f"cores 长度 {len(cores)} != cpu_cores {d['cpu_cores']}"
assert all(('f' in c and 'max' in c and 'min' in c) for c in cores), "cores 元素缺键"

print(f"  api_version = {d['api_version']}")
print(f"  features    = {len(feats)} 项")
print(f"  health      = {d['health']} ({d['health_rating']})")
print(f"  cpu_arch    = {d['cpu_arch']}")
print(f"  cores       = {len(cores)} 项")
print("  ✅ 状态接口校验通过")
