#!/usr/bin/env bash
set -euo pipefail

OUT_DIR="${1:-trace-output}"
mkdir -p "${OUT_DIR}"

echo "[1/4] 关闭旧 trace"
echo 0 | sudo tee /sys/kernel/debug/tracing/tracing_on >/dev/null
echo nop | sudo tee /sys/kernel/debug/tracing/current_tracer >/dev/null

echo "[2/4] 清空旧缓冲"
echo | sudo tee /sys/kernel/debug/tracing/trace >/dev/null

echo "[3/4] 打开常见事件"
echo 1 | sudo tee /sys/kernel/debug/tracing/events/irq/enable >/dev/null
echo 1 | sudo tee /sys/kernel/debug/tracing/events/sched/enable >/dev/null
echo function_graph | sudo tee /sys/kernel/debug/tracing/current_tracer >/dev/null

echo "[4/4] 开始采集，5 秒后自动停止"
echo 1 | sudo tee /sys/kernel/debug/tracing/tracing_on >/dev/null
sleep 5
echo 0 | sudo tee /sys/kernel/debug/tracing/tracing_on >/dev/null

sudo cp /sys/kernel/debug/tracing/trace "${OUT_DIR}/trace.txt"
echo "trace saved to ${OUT_DIR}/trace.txt"
