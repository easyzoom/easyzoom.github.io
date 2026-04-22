---
title: CFS完全公平调度器深度解析
author: EASYZOOM
date: 2026/04/22 12:00
categories:
 - Linux内核开发
tags:
 - Linux
 - 内核
 - 调度
 - CFS
 - EEVDF
---

# CFS 完全公平调度器深度解析

## 前言

**C：** CFS（Completely Fair Scheduler）在 2007 年的 2.6.23 进入主线，一用就是十多年；到 2023 年 6.6 内核时又被 EEVDF（Earliest Eligible Virtual Deadline First）算法接管了它的"灵魂"，但代码仍然生活在 `kernel/sched/fair.c` 里，对外接口也没变。这一章我们按源码的节奏讲清楚 CFS 的核心模型：`vruntime`、权重、红黑树、sched_slice，再看看 EEVDF 替换了其中哪些部分，以及它为何更合理。

<!-- more -->

## CFS 的"哲学"：理想多任务机器

CFS 的起点是一个思想实验：**假设我们有一台无限精细的多任务机器**，N 个任务同时以 1/N 的速度跑。那么公平性可以被定义为：每个任务获得的 CPU 时间都等于墙上时间除以 N。

现实当然做不到——一颗 CPU 同一时刻只能跑一个任务。CFS 的目标就是**让每个任务"看起来"像是在那台理想机器上跑**，即它累计获得的 CPU 时间不会长期偏离应有份额。

为此 CFS 引入了两个核心概念：

- **vruntime（虚拟运行时间）**：每个任务自己的"公平时钟"；
- **红黑树**：按 vruntime 排序的 runnable 任务集合，每次选最左边（vruntime 最小）的跑。

## vruntime 的数学本质

### 基本公式

任务跑 `delta_exec` 纳秒的物理时间后，vruntime 的增量是：

```
delta_vruntime = delta_exec * NICE_0_LOAD / task_load_weight
```

其中：

- `NICE_0_LOAD = 1024`，代表 nice=0 的基准权重；
- `task_load_weight` 来自 `sched_prio_to_weight[]` 表，nice 每下降 1，权重约乘以 1.25。

```c
const int sched_prio_to_weight[40] = {
    /* nice -20 */ 88761, 71755, 56483, 46273, 36291,
    /* nice -15 */ 29154, 23254, 18705, 14949, 11916,
    /* nice -10 */  9548,  7620,  6100,  4904,  3906,
    /* nice  -5 */  3121,  2501,  1991,  1586,  1277,
    /* nice   0 */  1024,   820,   655,   526,   423,
    /* nice   5 */   335,   272,   215,   172,   137,
    /* nice  10 */   110,    87,    70,    56,    45,
    /* nice  15 */    36,    29,    23,    18,    15,
};
```

几个直观的结论：

- **nice=0 的任务**：`delta_vruntime == delta_exec`，vruntime 按 1:1 增长；
- **nice=-5 的任务**：`weight ≈ 3121`，vruntime 增长只有物理时间的 ~33%，跑同样长的墙上时间但 vruntime 涨得慢→更容易被选中→跑得更多；
- **nice=+5 的任务**：权重降到 335，vruntime 涨得飞快，很快就被别人赶超。

### 红黑树里的 min_vruntime

一个 cfs_rq 里维护着：

```c
struct cfs_rq {
    struct load_weight load;
    unsigned int       nr_running;
    u64                min_vruntime;
    struct rb_root_cached tasks_timeline;
    struct sched_entity  *curr;
    // ...
};
```

`min_vruntime` 是一个**单调不减**的时间戳，近似等于红黑树最左节点的 vruntime。它存在的两个目的：

1. **新任务/唤醒任务**进入 rq 时不能 vruntime 为 0（否则会严重不公平地"跑很久"），会以 `min_vruntime` 为基准微调；
2. **跨 CPU 迁移**时 vruntime 需要归一化——源 rq 的 min_vruntime 不等于目标 rq 的。

### sched_vslice / sched_slice

CFS 并不希望任务"跑一小段就被切"，因此引入了时间片概念：

- `sched_period`：一次"公平调度周期"，大约 `sysctl_sched_latency`（默认 6ms）乘以任务数的上限；
- `sched_slice(cfs_rq, se)`：在当前周期内，该任务应得的物理时间；
- `sched_vslice`：把 `sched_slice` 换算成 vruntime 增量（按权重归一化）。

简化公式：

```
sched_period   = max(sched_latency, sched_min_granularity * nr_running)
sched_slice(p) = sched_period * p.weight / total_weight
sched_vslice(p)= sched_slice(p) * NICE_0_LOAD / p.weight
                = sched_period * NICE_0_LOAD / total_weight
```

任务一次连续执行至少 `sched_min_granularity`（默认 0.75ms），避免切换过于频繁；至多跑到 `sched_slice`，防止长期占用。

## 红黑树与 pick_next

### 为什么是红黑树

CFS 的需求是"快速拿到 vruntime 最小的 runnable 任务"。候选数据结构：

| 结构 | insert | pick-min | 缺点 |
|------|--------|----------|------|
| 排序链表 | O(n) | O(1) | 插入慢 |
| 最小堆 | O(log n) | O(1) | 任意位置删除麻烦（enqueue 之后 vruntime 会被外部改写） |
| **红黑树** | O(log n) | O(1)（缓存 leftmost） | 稍微复杂，但无短板 |

内核用的是 `rb_root_cached`，额外缓存了 leftmost 指针：

```c
struct rb_root_cached tasks_timeline;

// pick_next 本质就是:
se = rb_entry(cfs_rq->tasks_timeline.rb_leftmost, struct sched_entity, run_node);
```

### pick_next_task_fair 的骨架

```c
static struct task_struct *
pick_next_task_fair(struct rq *rq, struct task_struct *prev, struct rq_flags *rf)
{
    struct cfs_rq *cfs_rq = &rq->cfs;
    struct sched_entity *se;

    if (!cfs_rq->nr_running)
        return NULL;

    put_prev_task(rq, prev);          // 前任下台

    do {
        se = pick_next_entity(cfs_rq, NULL);
        set_next_entity(cfs_rq, se);
        cfs_rq = group_cfs_rq(se);    // 任务组嵌套: 继续下钻
    } while (cfs_rq);

    return task_of(se);
}
```

`pick_next_entity()` 就是取红黑树最左节点，特殊情况是 **last buddy / next buddy**——为了减少不必要的切换，CFS 允许缓存"刚唤醒的同伴"或"刚 yield 的同伴"，这段逻辑在 EEVDF 之后已经被大幅简化。

### 任务组带来的"嵌套调度"

当启用 `CONFIG_FAIR_GROUP_SCHED`（几乎所有发行版都启用）时，`sched_entity` 可能代表一个**任务组**而不是一个任务。内核在每个 CPU 上给每个 cgroup 建一个子 `cfs_rq`，形成一棵树：

```
rq->cfs (root)
  ├── se(group A) → cfs_rq(A)
  │     ├── se(task a1)
  │     └── se(task a2)
  └── se(group B) → cfs_rq(B)
        └── se(task b1)
```

`pick_next_task_fair` 的循环就是沿着这棵树一路下钻，直到拿到叶子（真正的任务）。vruntime 也是逐层累计的，每一层都在做"公平分配"。这就是 cgroup v2 里 `cpu.weight` 能工作的原理。

## 唤醒路径：wake_affine 与 select_task_rq_fair

CFS 在任务被唤醒时会做一次重要决策：**放回旧 CPU（提升缓存命中），还是挪到 waker 附近（提升数据局部性）**。

```c
int select_task_rq_fair(struct task_struct *p, int prev_cpu, int wake_flags)
{
    // 1. 没有其它 runnable 的 CPU 可选? 直接用 prev_cpu
    // 2. wake_affine: 如果 waker 和 wakee 在同一个 LLC, 且 waker 负载不过重,
    //    把 wakee 放到 waker 的 CPU (cache 近)
    // 3. 否则在 prev_cpu 的 LLC 域内找 idle sibling
    // 4. 都不合适就选负载最轻的 CPU
}
```

这一步的启发式规则长期是 CFS 的"争议地带"——对数据库/web server 的 pipelined 唤醒很有效，但对某些 HPC 工作负载可能把任务聚在同一个 CCX 导致瓶颈。`sched_feat(WAKE_WIDE, WA_WEIGHT, WA_BIAS)` 等 feature 开关就是留给调优的。

## 唤醒后的抢占：check_preempt_wakeup

新任务 enqueue 后：

```c
static void check_preempt_wakeup(struct rq *rq, struct task_struct *p, int wake_flags)
{
    struct sched_entity *se = &rq->curr->se;
    struct sched_entity *pse = &p->se;
    s64 delta = se->vruntime - pse->vruntime;

    // 至少要跑过 sched_wakeup_granularity (默认 ~1ms) 才允许被抢
    if (delta < sysctl_sched_wakeup_granularity)
        return;

    if (wakeup_preempt_entity(se, pse) == 1)
        resched_curr(rq);   // 置 NEED_RESCHED
}
```

这里的"最小保护间隔"很重要：如果没有它，N 个任务相互唤醒会触发**乒乓切换**，cache 被彻底打烂。

## 负载追踪：PELT

CFS 不只关心"现在这一刻谁的 vruntime 最小"，还需要回答：

- **这个 CPU 有多忙？**（负载均衡要用）
- **这个任务平均需要多少 CPU？**（调频要用）

PELT（Per-Entity Load Tracking）给出了答案。它的核心想法是指数衰减平均：

```
new_load = old_load * y + Δ
其中 y = 0.5 ^ (1/32)  →  约 32ms 半衰期
```

每个 `sched_entity` 和 `cfs_rq` 都维护三个量：

- `util_avg`：平均利用率（0 ~ SCHED_CAPACITY_SCALE，一般 1024）；
- `load_avg`：平均负载（加权后的）；
- `runnable_avg`：runnable 但未必在跑的时间占比。

这些数字会被 `schedutil` governor 用来选 CPU 频率，被 `EAS`（Energy Aware Scheduling）用来在 big.LITTLE 上选 CPU 簇。PELT 的精妙之处在于**完全可计算**，不需要定期全局扫描，只在 enqueue/dequeue/tick 时增量更新。

## EEVDF：6.6 之后的 CFS 新内核

2023 年末 6.6 合并了 Peter Zijlstra 的 EEVDF patchset，**fair_sched_class 内部的挑选算法从"最小 vruntime"换成了"最早可行虚拟 deadline"**。对外接口没变，sysctl 也兼容。核心差异：

| 项 | 旧 CFS (vruntime) | EEVDF |
|----|-------------------|-------|
| 公平度量 | 累计 vruntime 差 | lag（应得 - 实得） |
| 选择规则 | 最小 vruntime | eligible 集合中最小 virtual deadline |
| 时间片 | sched_slice 保证最小连续执行 | `request`（默认 1ms，可由任务指定） |
| 延迟敏感 | 通过 nice 和 sched_latency 粗调 | 提供 `SCHED_FLAG_LATENCY_NICE`，细粒度控制延迟 vs. 吞吐 |

EEVDF 的主要优势：

- **延迟更可控**：虚拟 deadline 直接给出"最晚什么时候该跑"，不再只是"比它小就先跑"；
- **更少的抖动**：原 CFS 在重负载下会反复把相近 vruntime 的任务来回切，EEVDF 的 deadline 更稳定；
- **面向未来的 API**：`SCHED_FLAG_LATENCY_NICE`（`latency_nice` 字段在 task_struct 里）让用户态可以声明"我是个延迟敏感任务"。

代码层面它复用了大量 CFS 的基础设施——红黑树、PELT、任务组、wake_affine 都没动，只是 `pick_next_entity` 和入队时的"可行性判定"换了一套。

## 一些容易被问到的数字

这些 sysctl 是调 CFS 行为最常见的旋钮：

```bash
# 调度周期最小值(单核时的期望 sched_period)
cat /proc/sys/kernel/sched_latency_ns           # 6000000 (6ms)

# 每个任务最少连续执行时间
cat /proc/sys/kernel/sched_min_granularity_ns   # 750000  (0.75ms)

# 唤醒抢占的最小保护间隔
cat /proc/sys/kernel/sched_wakeup_granularity_ns # 1000000 (1ms)

# 迁移代价(均衡时用)
cat /proc/sys/kernel/sched_migration_cost_ns    # 500000  (0.5ms)
```

注意：**从 6.6 EEVDF 起，这些旧 sysctl 有些不再生效**，被 `sched_base_slice`、`latency_nice` 等新参数取代。具体以你用的内核版本为准，可以看 `/sys/kernel/debug/sched/features`。

## 实战片段：观察 vruntime 变化

```bash
# 查看某进程当前的 CFS 统计
sudo cat /proc/<pid>/sched

# 重点字段:
#   se.sum_exec_runtime   累计物理运行时间(ns)
#   se.vruntime           虚拟运行时间
#   nr_voluntary_switches 主动让出次数
#   nr_involuntary_switches 被抢占次数
```

配合 `perf sched`：

```bash
# 记录 30 秒调度事件
sudo perf sched record -- sleep 30
# 分析延迟
sudo perf sched latency --sort max
# 看上下文切换热点
sudo perf sched timehist
```

## 本章小结

- CFS 把"公平"定义为 vruntime 对齐，用红黑树维护 runnable 集合；
- 权重表把 nice 值映射成 1.25 的幂次，nice 差 1 → CPU 份额约差 25%；
- 任务组让调度变成了一棵树，cgroup 的 `cpu.weight` 靠的就是它；
- PELT 是负载追踪和 CPU 调频的共同基础；
- 6.6 起 EEVDF 在不改接口的前提下替换了 CFS 的挑选算法，换来更好的延迟控制。

下一篇我们离开"公平"，看硬实时世界：RT 与 DEADLINE 调度类是怎么保证"必须在时限前完成"的。
