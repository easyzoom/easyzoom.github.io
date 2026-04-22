---
title: cgroup v2 与 CPU 控制器
author: EASYZOOM
date: 2026/04/22 17:00
categories:
 - Linux内核开发
tags:
 - Linux
 - 内核
 - 调度
 - cgroup
---

# cgroup v2 与 CPU 控制器

## 前言

**C：** 一个 Linux 系统上的 CPU 资源怎么分？只靠 `nice` 和 `chrt` 远远不够——我们需要"把一组进程作为整体"来分配 CPU，还要能限流、能做 SLA、能让容器之间隔离。这就是 cgroup 的 CPU 控制器要解决的。这一章我们把它和调度器的对接讲清楚：`cpu.weight` 是怎么传到 CFS 的、`cpu.max` 是怎么做 CFS bandwidth throttling 的、`cpu.idle` 为什么是个"逃生通道"，以及 cpuset、cpuacct 这些兄弟子系统。

<!-- more -->

## 两个 cgroup

Linux 上并存两套 cgroup：

- **v1**：每个控制器（cpu、cpuacct、cpuset、memory、blkio…）各自挂一棵层级树；
- **v2**：所有控制器共用一棵树，通过 `cgroup.subtree_control` 管理。

本章以 **cgroup v2** 为主（当前主流发行版默认）。v2 把 `cpu` + `cpuacct` 合成一个控制器，叫 `cpu`。

查看当前挂载：

```bash
mount | grep cgroup
# cgroup2 on /sys/fs/cgroup type cgroup2 (rw,...)
cat /sys/fs/cgroup/cgroup.controllers
# cpu cpuset io memory pids ...
```

## CPU 控制器的三个维度

cgroup v2 的 CPU 控制器把"分配 CPU"拆成三件事：

1. **权重**（`cpu.weight` / `cpu.weight.nice`）—— 按比例分配，只在竞争时体现；
2. **上限**（`cpu.max`）—— 硬限流，达到就停；
3. **策略**（`cpu.idle`、`cpu.uclamp.min/max`）—— 调整调度行为偏好。

三个维度可以独立使用，也可以叠加。

## cpu.weight：按比例分蛋糕

`cpu.weight` 是一个 1~10000 的整数（默认 100），**它直接映射成 CFS 里的 task_group 权重**。

假设你有两个 cgroup：

```
/sys/fs/cgroup/A  cpu.weight = 100
/sys/fs/cgroup/B  cpu.weight = 300
```

那么在竞争 CPU 时，A 和 B 拿到的比例是 `100 : 300 = 1 : 3`。

### 在 CFS 内部的实现

还记得 CFS 章节里讲过的"嵌套 cfs_rq"吗？每个 cgroup 在每颗 CPU 上都有一个 `cfs_rq`，挂在父 cgroup 的 `cfs_rq` 下面。`cpu.weight` 决定的就是**这个 cfs_rq 在父 rq 里的 sched_entity 权重**。

```
cpu.weight 1 ~ 10000
  └── 转换为 sched_group 的 shares (通过 sched_weight_to_cgroup 映射)
        └── 影响 se.load.weight
              └── 影响每层 vruntime 计算
```

和 `nice` 值的关系：v2 还提供了 `cpu.weight.nice`（-20 ~ 19），它其实是 `cpu.weight` 的另一种写法——内核会按 1.25 的幂次做换算，跟传统 `nice` 的行为保持一致。

### 关键性质

- `cpu.weight` 是**相对的**：如果一棵 cgroup 树里只有一个子节点，它的 weight 是 10000 还是 1 都没区别；
- `cpu.weight` 只在**饱和时**体现：系统 CPU 过剩时，每个 cgroup 想跑多少跑多少；
- 权重在每个父节点**独立归一**：`/A/A1` 和 `/B/B1` 的比例是 `A:B × A1:A2 × ...`。

## cpu.max：硬上限

`cpu.max` 的格式是 `max period`：

```bash
# A 最多每 100ms 吃 50ms (即 0.5 CPU)
echo "50000 100000" > /sys/fs/cgroup/A/cpu.max

# 取消限制
echo "max 100000" > /sys/fs/cgroup/A/cpu.max
```

### CFS Bandwidth

这背后是 CFS Bandwidth Control（`CONFIG_CFS_BANDWIDTH`）。每个 task_group 有：

```c
struct cfs_bandwidth {
    ktime_t        period;        // 周期 (e.g. 100ms)
    u64            quota;         // 每周期的配额 (e.g. 50ms)
    u64            runtime;       // 当前周期剩余配额
    struct hrtimer period_timer;  // 周期重置
    struct hrtimer slack_timer;
    int            idle;
    int            throttled_count;
    // ...
};
```

每颗 CPU 上，这个 task_group 的 cfs_rq 会"**申请**"一小段配额（slack 一般是 5ms）用于本地消费；跑完了再申请。全局 `runtime` 归零时：

1. 所有属于该 cgroup 的任务被 dequeue（throttle）；
2. rq 上该 task_group 的 cfs_rq 被标记为 throttled；
3. `period_timer` 到期时补血、重新 enqueue。

### 常见坑：throttle 带来的尾延迟

在容器场景里，`cpu.max` 很容易造成**周期性卡顿**：

- 你的 Web 服务 burst 一下用完了 quota；
- 接下来到周期末，即便整机空闲，这个 cgroup 的线程也得停；
- 等待期间进来的请求全部排队，尾延迟炸了。

应对方式：

1. 放大 period（从 100ms 加到 200ms），让 burst 能被摊平；
2. 改用 `cpu.weight` 而不是 `cpu.max`——软限制不会 throttle；
3. 升级到 **BPF 版本的 CPU 控制器** 或使用"可抢占的 bandwidth 预留"；
4. 关闭 HT / 提升 CPU 实际可用算力。

### 观察 throttle

```bash
cat /sys/fs/cgroup/A/cpu.stat
# usage_usec 12345
# user_usec ...
# nr_periods 1234
# nr_throttled 56
# throttled_usec 7890
```

`nr_throttled` 和 `throttled_usec` 是最重要的两个指标，一旦非零且在增长，你就该怀疑 quota 设得太紧。

## cpu.idle：降到最低优先级

cgroup v2 提供了 `cpu.idle`（较新的内核才有），把整个 cgroup 的 CPU 优先级映射到 `SCHED_IDLE`：

```bash
echo 1 > /sys/fs/cgroup/batch/cpu.idle
```

效果：

- 该 cgroup 里所有 CFS 任务**等同于 SCHED_IDLE**，在有普通任务时几乎不会被调度；
- 典型场景：容器里的 GC、数据备份、索引重建这种"有空再跑"的后台任务；
- 比 `cpu.weight = 1` 更强：后者依然是 CFS 正常任务，只是权重低；`cpu.idle = 1` 直接降到 idle class 级别。

## cpu.uclamp：性能下限/上限（EAS/schedutil）

`cpu.uclamp.min` 和 `cpu.uclamp.max` 是 ARM 移动端带进来的特性，用来**约束 schedutil 调频决策**：

- `cpu.uclamp.min = 30`：这个 cgroup 里的任务在调度时，CPU 利用率"至少按 30% 算"——会让 CPU 频率抬起来；
- `cpu.uclamp.max = 60`：反之，任务跑多猛最多按 60% 算——控制峰值功耗。

桌面/服务器 CPU 上意义有限，在手机 / Android 的前台/后台应用差异化里用得最多。

## cpuset：CPU 和 NUMA 节点的亲和

cpuset 控制的是"**允许跑在哪些 CPU**"和"**允许从哪些 NUMA 节点分配内存**"：

```bash
# 只允许这个 cgroup 的进程跑在 0-3 号 CPU
echo "0-3" > /sys/fs/cgroup/A/cpuset.cpus

# 只允许从节点 0 分配内存
echo "0" > /sys/fs/cgroup/A/cpuset.mems

# 独占：0-3 号 CPU 将不被系统其它 cgroup 使用
echo "root" > /sys/fs/cgroup/A/cpuset.cpus.partition
```

cpuset 和负载均衡、调度域是深度集成的——`cpuset.cpus.partition = root` 会**动态重建 sched_domain 树**，把划出去的 CPU 从均衡域里摘出去。这是实现"独立 CPU 池"的标准手段，比 `isolcpus=` 灵活得多（因为可以运行时修改）。

## cpu.stat：观测点

v2 统一了统计接口：

```bash
cat /sys/fs/cgroup/A/cpu.stat
# usage_usec      总使用时间
# user_usec       用户态
# system_usec     系统态
# nr_periods      bandwidth 周期数
# nr_throttled    被 throttle 过的周期数
# throttled_usec  累计被 throttle 时间
# nr_bursts       (6.x 新增) burst 使用次数
# burst_usec      burst 累计时间
```

监控系统通常采集这些指标做容器 CPU 使用率、throttle 告警。

## 实战：一个完整的 cgroup 分配示例

假设一台 16 核机器，我们要分成三组：

- **latency 组**：在线服务，保证快速响应；
- **batch 组**：离线计算，吞吐优先；
- **maintenance 组**：有空再跑的 GC / backup。

目录结构：

```bash
cd /sys/fs/cgroup
mkdir -p latency batch maintenance

# 使能 cpu / cpuset 控制器
echo "+cpu +cpuset" > cgroup.subtree_control

# latency: 高权重 + 绑定 0-7 CPU (不允许跨 CCX)
echo 800   > latency/cpu.weight
echo "0-7" > latency/cpuset.cpus

# batch: 中等权重 + 全核共享 + 限制最多 10 核
echo 200    > batch/cpu.weight
echo "0-15" > batch/cpuset.cpus
echo "10000000 10000000" > batch/cpu.max   # 1 秒 10 核秒

# maintenance: idle 类, 只跑在 12-15
echo 1       > maintenance/cpu.idle
echo "12-15" > maintenance/cpuset.cpus
```

然后把各自的进程 pid 写进 `cgroup.procs` 即可。

## 旧 cgroup v1 的对应字段

如果你还在维护旧系统，对照表如下：

| v2 | v1 等价 |
|----|---------|
| `cpu.weight` | `cpu.shares`（1024 为基准） |
| `cpu.max` | `cpu.cfs_quota_us` + `cpu.cfs_period_us` |
| `cpu.max.burst` | `cpu.cfs_burst_us`（5.14+） |
| `cpu.idle` | 无直接对应，可用 `sched_idle` 策略 |
| `cpuset.cpus` | `cpuset.cpus`（同名） |
| `cpu.stat` | `cpu.stat` + `cpuacct.usage*` |

## 和 systemd 的关系

systemd 是 cgroup v2 的主要用户，常见操作：

```bash
# 查看 service 的 cgroup 配置
systemctl show my.service | grep -E 'CPUWeight|CPUQuota'

# 运行时调整
sudo systemctl set-property my.service CPUWeight=500
sudo systemctl set-property my.service CPUQuota=200%

# 持久化到 drop-in
sudo systemctl edit my.service
# [Service]
# CPUWeight=500
# CPUQuota=200%
```

`CPUWeight` 直接映射到 `cpu.weight`；`CPUQuota=200%` 相当于"2 颗 CPU 的上限"，systemd 会换算成 `cpu.max`。

## 本章小结

- cgroup v2 的 CPU 控制器 = 权重（cpu.weight）+ 上限（cpu.max）+ 策略（cpu.idle / uclamp）；
- `cpu.weight` 最终变成 task_group 在 CFS 红黑树里的 sched_entity 权重；
- `cpu.max` 用 CFS bandwidth 做 throttle，易用但有尾延迟风险；
- cpuset 能动态重建调度域，是容器化时代 CPU 隔离的标准手段；
- 监控一定要看 `cpu.stat`，特别是 `nr_throttled` 与 `throttled_usec`。

最后一篇我们把前面所有知识串起来，看一个真实的调度延迟问题是怎么被 `trace-cmd`、`perf sched`、`bpftrace` 和 `cyclictest` 一步步定位的。
