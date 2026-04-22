---
title: 实时与DEADLINE调度类
author: EASYZOOM
date: 2026/04/22 14:00
categories:
 - Linux内核开发
tags:
 - Linux
 - 内核
 - 调度
 - 实时
 - SCHED_DEADLINE
---

# 实时与 DEADLINE 调度类

## 前言

**C：** CFS 关心的是"长期公平"，实时调度关心的是"短期可靠"。你不在乎一段音频进程整个小时里得到了多少 CPU，你只在乎它**这一帧 5 ms 内有没有跑完**。Linux 为这类需求提供了两套调度类：`rt_sched_class`（传统 POSIX 实时，FIFO/RR）与 `dl_sched_class`（基于最早截止时间的 DEADLINE）。这一章把它们讲清楚，也顺便聊一下 Linux 的"实时"到底能到什么程度。

<!-- more -->

## 先厘清两个容易混的概念

Linux 里的实时有两个层级，不要弄混：

1. **SCHED_FIFO / SCHED_RR / SCHED_DEADLINE** —— 这是**调度策略**，提升的是"优先级高于普通任务"，但仍然跑在主线内核上，不保证微秒级延迟；
2. **PREEMPT_RT（Real-Time patch）** —— 这是**内核特性**，把绝大多数自旋锁转为可抢占的 rtmutex、中断线程化，用它 + SCHED_FIFO 才能谈"硬实时"。

本章讲的是第 1 类。PREEMPT_RT 在主线已经逐步合并，但真正的硬实时部署一般还是会用专门的 RT 发行版。

## POSIX 实时：SCHED_FIFO / SCHED_RR

### 数据结构：100 条优先级队列

```c
struct rt_prio_array {
    DECLARE_BITMAP(bitmap, MAX_RT_PRIO + 1);
    struct list_head queue[MAX_RT_PRIO];
};

struct rt_rq {
    struct rt_prio_array active;
    unsigned int         rt_nr_running;
    int                  rt_throttled;
    u64                  rt_time;
    u64                  rt_runtime;
    // ...
};
```

- `MAX_RT_PRIO = 100`，对应用户态看到的 RT 优先级 1–99（0 是内核内部用的）；
- `active.queue[k]` 是一条链表，挂着所有优先级为 k 的 runnable RT 任务；
- `active.bitmap` 用一位标记某优先级是否非空，`find_first_bit()` 一次就能找到最高优先级；
- **整体复杂度是 O(1)**，无论队列里有多少任务。

### pick 的逻辑

```c
static struct task_struct *pick_next_task_rt(struct rq *rq)
{
    struct rt_rq *rt_rq = &rq->rt;
    int idx = sched_find_first_bit(rt_rq->active.bitmap);
    struct list_head *queue = rt_rq->active.queue + idx;
    return list_entry(queue->next, struct task_struct, rt.run_list);
}
```

- 找到最高优先级的那条非空队列；
- 取队首任务；
- 整个过程是位运算 + 链表操作，完全常数时间。

### FIFO 与 RR 的区别

两者都走 `rt_sched_class`，差别只在 `task_tick_rt()`：

- `SCHED_FIFO`：只要任务不主动阻塞/让出，就一直跑，**不会被同优先级任务抢**；
- `SCHED_RR`：每个任务有一个时间片（`sched_rr_timeslice`，默认 100ms），用完后移到队尾。

```c
static void task_tick_rt(struct rq *rq, struct task_struct *p, int queued)
{
    if (p->policy != SCHED_RR)
        return;                // FIFO 啥也不做
    if (--p->rt.time_slice)
        return;
    p->rt.time_slice = sched_rr_timeslice;
    // 挪到同优先级队尾
    if (p->rt.run_list.prev != p->rt.run_list.next) {
        requeue_task_rt(rq, p, 0);
        resched_curr(rq);
    }
}
```

### RT 抢占规则

- **不同优先级**：高优先级一上来立刻抢低优先级的（`check_preempt_curr_rt`）；
- **同优先级 FIFO**：不抢；
- **同优先级 RR**：等 `task_tick_rt` 切换；
- **RT 抢 CFS**：永远抢；
- **DL 抢 RT**：永远抢。

这是一条严格的优先级层级：`DL > RT(99) > RT(98) > ... > RT(1) > CFS`。

### RT 限流：sched_rt_runtime_us

**"实时任务卡死所有普通任务"是 Linux 历史上真实发生过的事故**（比如写出 `while(1);` 的 FIFO 任务，它甚至不让 init 跑）。为了兜底，内核提供了全局限流：

```bash
cat /proc/sys/kernel/sched_rt_period_us    # 1000000 (1s)
cat /proc/sys/kernel/sched_rt_runtime_us   # 950000  (0.95s)
```

含义是：**每 1s 的周期里，所有 RT 任务加起来最多跑 0.95s**，剩下的 50ms 强制留给 CFS/idle。超了就把 RT 任务整体 throttle（`rt_throttled = 1`），直到新周期开始。

调试建议：开发阶段**不要**把 `sched_rt_runtime_us` 设成 `-1`（无限），不然写错一个 FIFO 会把整机打死。生产上如果你确信自己的 RT 负载可控，才考虑解除限流。

### cgroup 里的 RT 带宽

开启 `CONFIG_RT_GROUP_SCHED` 后，可以给 cgroup v1 的 cpu 子系统分配 RT 带宽：

```bash
echo 500000 > /sys/fs/cgroup/cpu/myrt/cpu.rt_period_us
echo 100000 > /sys/fs/cgroup/cpu/myrt/cpu.rt_runtime_us   # 这个组最多吃 20%
```

cgroup v2 目前主流发行版**没有**开启 RT group（它默认 off，需要启动参数或重编内核），这也是为什么一般建议 RT 任务留在 root cgroup 并用 `sched_rt_runtime_us` 全局兜底。

## SCHED_DEADLINE：最早截止时间优先

### 模型：(runtime, deadline, period)

`SCHED_DEADLINE` 把每个任务描述成一个周期性任务：

- **runtime**：每个周期内，任务需要的 CPU 时间上限；
- **deadline**：从周期开始到任务必须完成的时间；
- **period**：周期长度。

一般要求 `runtime ≤ deadline ≤ period`（隐式 deadline 模型时 `deadline == period`）。

数学上，只要 `runtime / period` 之和不超过系统可用带宽（粗略看是 `#CPU`），就能保证所有任务都能按时完成——这是经典的 **EDF（Earliest Deadline First）可调度性定理**。

### 使用方法

```c
struct sched_attr attr = {
    .size            = sizeof(attr),
    .sched_policy    = SCHED_DEADLINE,
    .sched_flags     = 0,
    .sched_runtime   = 10 * 1000 * 1000,   // 10ms
    .sched_deadline  = 30 * 1000 * 1000,   // 30ms
    .sched_period    = 30 * 1000 * 1000,   // 30ms
};
sched_setattr(0, &attr, 0);
```

或者用 `chrt`：

```bash
sudo chrt -d -T 10000000 -D 30000000 -P 30000000 0 /my/realtime/prog
```

内核会检查"当前系统还有没有足够带宽接纳这个任务"——通过 `dl_bw_alloc()` / `__dl_overflow()`。如果会超额，`sched_setattr` 直接返回 `EBUSY`。这是 `SCHED_DEADLINE` 和 RT 的一个关键区别：**DL 是准入控制（admission control），RT 则没有**。

### 运行时跟踪：runtime 削减与补充

```c
struct sched_dl_entity {
    u64 dl_runtime;      // 用户给的 runtime
    u64 dl_deadline;
    u64 dl_period;

    s64 runtime;         // 当前周期剩余可用 runtime
    u64 deadline;        // 当前周期的 absolute deadline
    // ...
};
```

每次 `update_curr_dl()` 被调用（通常是 tick 或切出 CPU 时），都会从 `runtime` 里扣掉刚刚跑了多少。当 `runtime` 归零：

- 任务被 dequeue，设置 `dl_throttled = 1`；
- 一个 hrtimer 在下一个 period 开始时"补血"——恢复 `runtime = dl_runtime`，`deadline += dl_period`，重新 enqueue。

这套机制叫 **CBS（Constant Bandwidth Server）**，保证"一个恶意/bug DL 任务不能吃掉它声明之外的 CPU"。

### pick：最小 deadline 的红黑树

DL 的运行队列也是一棵红黑树，按 `deadline` 排序：

```c
struct dl_rq {
    struct rb_root_cached root;          // 按 deadline 排序
    unsigned long  dl_nr_running;
    u64            running_bw;           // 当前正在跑的任务的总带宽
    u64            this_bw;               // 这个 rq 上所有 DL 任务的总带宽
    // ...
};
```

`pick_next_task_dl()` 同样是取 leftmost——也就是 deadline 最近的那个任务。

### Deadline 在多核上的 push/pull

DL 支持"全局 EDF + 分区"的混合模型。两条关键逻辑：

- **push**：本 rq 多出来的 DL 任务会被 push 到其它"当前没有更紧迫 DL 的"CPU；
- **pull**：本 rq 即将 idle 或只剩宽松任务时，主动从别处 pull 紧迫的 DL 任务。

这两个操作通过 `cpupri`-like 的 `cpudl` 结构加速（维护"每颗 CPU 当前运行的 DL deadline"），能 O(log n) 找到最合适的迁移目标。

## RT 和 DL 的对比

| 维度 | RT (FIFO/RR) | DEADLINE |
|------|--------------|----------|
| 准入控制 | 无 | 有（基于带宽） |
| 语义 | 固定优先级 | 最早 deadline |
| 时间片 | RR 有, FIFO 无 | runtime/period |
| 保护机制 | 全局 sched_rt_runtime_us | per-task CBS |
| 多核策略 | push/pull | push/pull + dl_bw |
| 典型用法 | 驱动线程、音频栈 | 视频帧、控制环路 |

经验之谈：

- 如果你的任务"**只要比其它任务先跑**"就够，用 RT；
- 如果你的任务"**每 N ms 必须有 M ms 的 CPU**"，用 DL；
- 如果你搞不清楚，先用 CFS + `nice=-20` 或 `SCHED_IDLE`，等真的测出延迟不达标再上实时。

## 常见坑与排查

### 问题 1：SCHED_FIFO 任务突然被"卡住"

多半是 `sched_rt_runtime_us` 限流生效了。查 `dmesg` 或 tracepoint：

```bash
sudo trace-cmd record -e sched:sched_rt_throttled -e sched:sched_switch -- sleep 10
```

### 问题 2：sched_setattr 返回 EBUSY

DL 准入控制不让你进。可能的原因：

- 系统里已经有 DL 任务吃了大部分带宽；
- CPU 亲和性太窄（`sched_setaffinity` 限制了只能跑在 1 颗 CPU 上，带宽分母变小）；
- 设置了 `SCHED_FLAG_SUGOV`（kernel 内部用）而你不该用。

解决：放宽亲和、减小 runtime 或加大 period、检查是不是被 `cgroup cpu.max` 压缩了。

### 问题 3：RT 任务跨核迁移造成 jitter

RT 的 push/pull 默认开启。如果你要**严格固定**在某颗隔离核上：

```bash
# 隔离核
# kernel cmdline: isolcpus=2,3 nohz_full=2,3 rcu_nocbs=2,3

# 把任务绑到核 2
taskset -pc 2 <pid>
chrt -f -p 80 <pid>
```

并考虑 PREEMPT_RT 内核以减少中断/软中断带来的 jitter。

### 问题 4：优先级反转

低优先级任务持有 mutex，高优先级 RT 任务等这把锁——经典的优先级反转。Linux 的解法：

- 使用 `pthread_mutexattr_setprotocol(PTHREAD_PRIO_INHERIT)` 开启 PI mutex；
- 或用 `futex` 的 `PI` 变种；
- 内核内部的 `rt_mutex` 自带 PI。

没开 PI 的话，RT 任务可能被 `SCHED_OTHER` 的持锁者"间接卡死"。

## 本章小结

- RT 调度类提供的是**静态优先级 + 可选时间片**的 POSIX 实时，100 条链表 O(1) 选择；
- `sched_rt_runtime_us` 是 RT 的**全局安全阀**，默认留 5% 给 CFS，必须保留；
- SCHED_DEADLINE 是**真正基于 deadline 的调度器**，带准入控制和 CBS，用红黑树 O(log n) 挑最紧迫任务；
- 只有 PREEMPT_RT 内核 + SCHED_FIFO/DL + 正确的中断处理 + PI mutex 才能谈"硬实时"。

下一篇我们离开"一颗 CPU 上怎么选"，看多核系统上 Linux 怎么决定"把谁挪到哪个 CPU"。
