# 05 tracing debug playbook

这个目录不是驱动代码，而是一套排障操作骨架。

## 包含内容

- `capture-trace.sh`：使用 `tracefs` 采集 IRQ、调度和函数图 trace 的最小脚本

## 使用前提

- 已挂载 `debugfs`
- 内核启用了对应 trace 能力
- 当前环境允许使用 `sudo`

## 建议用途

适合在下面这些问题里快速抓第一份证据：

- 某个 IRQ 路径偶发很慢
- workqueue 是否被及时调度
- 某条驱动函数链是否明显超长

## 适合对应的文章

- 《ftrace、tracepoints与函数级时序分析》
- 《perf、lockdep、kmemleak、KASAN常用排障手段》
- 《死锁、内存踩踏、偶发超时与崩溃现场分析》
