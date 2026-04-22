---
title: SMP负载均衡与调度域
author: EASYZOOM
date: 2026/04/22 15:00
categories:
 - Linux内核开发
tags:
 - Linux
 - 内核
 - 调度
 - SMP
 - NUMA
---

# SMP 负载均衡与调度域

## 前言

**C：** 到目前为止，我们讲的都是"单颗 CPU 怎么选任务"。现实中的机器从手机 8 核到服务器 192 核不等，还要考虑 SMT（超线程）、big.LITTLE、NUMA。Linux 的做法是：**每颗 CPU 一个 rq，独立选任务**；而**负载均衡是另一条独立的流水线**，负责在 rq 之间搬任务。这一章我们把负载均衡讲清楚：调度域（sched_domain）是什么、主动/被动均衡分别什么时机发生、wake_affine 在其中扮演什么角色，以及 NUMA balancing 和 EAS 又额外做了什么。

<!-- more -->

## 为什么需要"调度域"这么复杂的结构

最朴素的多核均衡是"哪颗 CPU 空就把任务挪过去"——但这会带来一堆问题：

- **迁移成本不是 0**：L1/L2 cache 白扔了，TLB 要重建；
- **CPU 之间并不对称**：SMT sibling 共享执行单元，NUMA 远节点内存访问慢 5–10x，big core 和 little core 算力相差数倍；
- **锁争用**：频繁跨 CPU 挪动任务会让 rq 锁、cgroup 锁成为热点。

于是 Linux 引入了 **调度域（sched domain）** 的分层抽象，把"均衡"这个操作按**拓扑层级**进行：

```
            NUMA domain        ← 节点之间，最贵
                │
           DIE / MC domain    ← 同 socket 内
                │
             LLC domain        ← 共享 LLC (L3)
                │
              SMT domain        ← 同物理核的超线程兄弟
```

每个 domain 都有一份配置：多久均衡一次、允许多大的不均衡、迁移代价多少。越靠下的 domain 越便宜越频繁，越靠上的 domain 越贵越稀疏。

## 调度域与调度组

### sched_domain 与 sched_group

```c
struct sched_domain {
    struct sched_domain __rcu *parent;
    struct sched_domain __rcu *child;

    struct sched_group *groups;      // 同级的 CPU 分组
    unsigned long       min_interval;
    unsigned long       max_interval;
    unsigned int        busy_factor;
    unsigned int        imbalance_pct;
    int                 flags;       // SD_SHARE_PKG_RESOURCES / SD_NUMA 等
    // ...
};

struct sched_group {
    struct sched_group *next;
    atomic_t            ref;
    unsigned int        group_weight;
    unsigned long       cpumask[];    // 这个组覆盖的 CPU
    // ...
};
```

- **sched_domain** 是一棵树，每颗 CPU 的 rq 都指向这棵树的叶子（它自己所在的最小域），然后沿 parent 往上走；
- **sched_group** 是 domain 里的"兄弟组"——负载均衡比较的是组与组之间的负载，而不是 CPU 与 CPU。

举例：一颗 8 核（4 物理核 × 2 SMT）的 CPU：

- SMT domain：group 只有 1 个 CPU（sibling），组数 = 2；
- MC domain：group = 2 个 SMT sibling，组数 = 4；
- DIE domain：group = 整颗 socket 的 8 个 CPU，组数 = 1（单 socket）或 多（多 socket）。

### domain 的关键 flags

- `SD_LOAD_BALANCE`：是否允许在这一层做均衡；
- `SD_SHARE_PKG_RESOURCES`：该域内 CPU 共享 L2/L3（影响 wake_affine）；
- `SD_SHARE_CPUCAPACITY`：共享算力（SMT）；
- `SD_ASYM_CPUCAPACITY`：域内 CPU 算力不对称（big.LITTLE）；
- `SD_NUMA`：跨 NUMA 节点。

启动后可以在 `/proc/sys/kernel/sched_domain/cpu<N>/domain<M>/` 看到所有配置，也可以查 `/sys/kernel/debug/sched/domains/`（较新内核）。

## 什么时候会做负载均衡

### 周期性均衡：rebalance_domains

每个 tick 会调用 `trigger_load_balance(rq)`：

```c
void trigger_load_balance(struct rq *rq)
{
    // 本 CPU 过了 next_balance? 唤醒 SCHED_SOFTIRQ
    if (time_after_eq(jiffies, rq->next_balance))
        raise_softirq(SCHED_SOFTIRQ);
    // nohz idle balance: 选一个 CPU 代表所有 idle 做均衡
    nohz_balancer_kick(rq);
}
```

真正的工作在软中断里 `run_rebalance_domains()` → `rebalance_domains()`：

```c
for_each_domain(cpu, sd) {
    if (time_after_eq(jiffies, sd->last_balance + interval))
        load_balance(cpu, rq, sd, ...);
}
```

**从叶子到根逐层尝试**，每层间隔不同（由 `balance_interval` 和 `busy_factor` 决定）。

### idle balance：即将 idle 前的救援

当一颗 CPU 上没任务了，`schedule()` 会在选 idle 之前调 `newidle_balance()`：

```c
// 简化伪代码
if (rq->nr_running == 0 && !rq->idle_stamp) {
    for_each_domain(cpu, sd)
        pulled = load_balance_newidle(cpu, rq, sd);
    if (pulled) goto again;      // 拉到任务了, 重选
}
```

这是非常关键的一步：**不让 CPU 空闲等下一个 tick**，能拉就拉。

### nohz_idle_balance：tickless 下的集体均衡

启用 `nohz` 的机器上，idle 的 CPU 本身不会收到 tick，也就不会主动做均衡。Linux 的办法是选一个"代理 CPU"（往往是 `nohz.idle_cpus_mask` 里第一个）在它的软中断里替所有 idle CPU 做一次均衡——就是 `nohz_idle_balance()`。

### wake-time balance：select_task_rq_fair

唤醒一个任务时调用 `select_task_rq_fair()`，这是**最频繁的一种"均衡"**，决定把一个 runnable 任务挂到哪颗 CPU 的 rq 上。逻辑层次：

1. **wake_affine**：如果 waker 和 wakee 共享 LLC 且负载不重，放到 waker 附近；
2. **find_idlest_cpu**：否则在 prev_cpu 的调度域里找空闲 CPU；
3. **find_idlest_group**：再不行就沿 domain 向上走。

## load_balance：真正搬任务的那函数

`load_balance()` 是内核里最长最绕的函数之一（千行量级），但主干就三步：

```c
int load_balance(int this_cpu, struct rq *this_rq,
                 struct sched_domain *sd, enum cpu_idle_type idle, ...)
{
    // 1. find_busiest_group: 在 sd 里找最忙的 sched_group
    group = find_busiest_group(sd, ...);
    if (!group) return 0;

    // 2. find_busiest_queue: 在该 group 里找最忙的 rq
    busiest = find_busiest_queue(sd, group, ...);
    if (!busiest) return 0;

    // 3. 从 busiest 抢任务到 this_rq
    pulled = detach_tasks(this_cpu, busiest, ...);
    attach_tasks(this_rq, pulled);
    return pulled;
}
```

抢任务的核心是 `detach_tasks()` + `can_migrate_task()`：

- **cpus_mask 允许么？** 任务被 taskset 限制就跳过；
- **cache hot？** 刚刚在 busiest 上跑过的任务代价高（用 `sched_migration_cost` 判定），除非负载严重不均才挪；
- **running？** 正在跑的任务不能直接搬（得用 push/migration_thread）；
- **任务组带宽？** CFS bandwidth throttle 的任务也不搬。

### 不均衡的度量：imbalance_pct 与 group_type

`find_busiest_group()` 需要量化"两个 group 差多少才算不均衡"。Linux 把 sched_group 分类：

| group_type | 含义 |
|------------|------|
| `group_has_spare` | 还有余力，不紧迫 |
| `group_fully_busy` | 满载，大家都在跑 |
| `group_misfit_task` | 有不该在 little core 上的 big 任务（EAS） |
| `group_asym_packing` | SMT/能效打包不均 |
| `group_imbalanced` | 上一轮没搬成功留的标志 |
| `group_overloaded` | 真的超了 |

只有当 busiest 和 local 组类别差到一定程度，且负载比例超过 `sd->imbalance_pct`（默认 117–125，意味着 17%~25% 的差距），才触发真正迁移。

## wake_affine 的启发式

```c
static int wake_affine(struct sched_domain *sd, struct task_struct *p,
                       int this_cpu, int prev_cpu, int sync)
{
    // sync 唤醒: waker 马上要睡, 别把 wakee 塞到它 CPU
    // 比较 this_cpu 和 prev_cpu 的 load_avg, 偏向哪边 "更合算"
}
```

这段逻辑长期是 CFS 调优的"战区"，相关 feature：

- `WAKE_AFFINE`：总开关；
- `WA_WEIGHT`：考虑负载权重；
- `WA_BIAS`：偏爱 waker CPU；
- `WAKE_WIDE`：当 waker 唤醒数量多到成为"生产者"时放弃 affine，扩散到更宽的域。

这些 feature 都可以通过 `/sys/kernel/debug/sched/features` 动态切换来排查问题：

```bash
# 查看
sudo cat /sys/kernel/debug/sched/features
# 关闭 WAKE_AFFINE 调试
echo NO_WAKE_AFFINE | sudo tee /sys/kernel/debug/sched/features
```

## NUMA Balancing：跨节点的局部性

NUMA 系统上，除了传统的"忙的搬给闲的"，还要考虑"任务和它的内存是不是在一个节点"。内核通过 `sched_numa_balancing` 提供了 **自动 NUMA 迁移**：

1. 周期性把进程的一部分页设成 `PROT_NONE`；
2. 访问触发 fault → `do_numa_page()` 收集"哪些页被哪颗 CPU 访问了"；
3. 根据统计决定：
   - 把任务迁到"它的页大多在的"节点；
   - 或把页迁到"任务正在跑的"节点。

相关可调：

```bash
# 总开关
cat /proc/sys/kernel/numa_balancing     # 1 表示启用

# 扫描间隔范围
cat /proc/sys/kernel/numa_balancing_scan_period_min_ms
cat /proc/sys/kernel/numa_balancing_scan_period_max_ms
cat /proc/sys/kernel/numa_balancing_scan_size_mb
```

NUMA balancing 是"**默默**帮你优化"的那种特性——绝大多数 workload 受益，但 JVM / 大页 / 对 fault 敏感的负载可能要关掉它，以 `numactl --cpunodebind` / `--membind` 做静态绑定。

## EAS：能耗感知调度

在 ARM big.LITTLE 和最近的 Intel Hybrid（P-core/E-core）上，调度器还要回答一个新问题：**把任务放 big 还是 little 能耗更低**。Linux 从 4.14 起逐步合入 EAS（Energy Aware Scheduling）。它的关键部件：

- **CPU capacity**：`rq->cpu_capacity`，由 DT / ACPI 里的能效模型决定；
- **能量模型（EM）**：每档频率下每颗 CPU 的功耗；
- **compute_energy()**：给"把任务放到候选 CPU"的方案算一份预估能耗；
- **find_energy_efficient_cpu()**：在唤醒时选能耗最小且能 meet 性能的 CPU。

EAS 只在 **`SD_ASYM_CPUCAPACITY` 域且关闭 SCHED_AUTOGROUP** 等条件下生效。对于服务器 CPU（对称算力）它是 no-op。

## NOHZ、isolcpus 与"不要被均衡打扰"

有些 workload（HPC、DPDK 用户态快速路径、实时控制）希望**固定某些核不要被均衡挪动**。Linux 提供了分层的隔离手段：

- `isolcpus=` 启动参数：从一开始就把这些核排除出均衡域；
- `nohz_full=`：这些核上不跑 tick（单任务独占时进入 dyntick）；
- `cgroup cpuset`：运行期限制某任务组只能在哪些 CPU 上跑；
- `SCHED_DEADLINE` + 亲和：准入控制直接认领带宽。

一个典型的 DPDK 隔离配置：

```
isolcpus=nohz,domain,2-7 nohz_full=2-7 rcu_nocbs=2-7 \
  irqaffinity=0-1 intel_pstate=disable
```

这组参数会告诉内核：2–7 号核不参与均衡、不收 tick、RCU 回调甩给别处、硬件中断绑在 0–1。用户态再用 `taskset -c 2-7` 绑定 DPDK 线程即可。

## 观察负载均衡

### /proc/schedstat

`/proc/schedstat` 记录了每颗 CPU 每层 domain 的均衡统计：拉到任务次数、失败次数、被 affinity 阻挡次数等。`schedstats` 需要通过内核参数 `schedstats=enable` 或运行时 sysctl 打开。

```bash
sudo sysctl kernel.sched_schedstats=1
cat /proc/schedstat
```

### tracepoint

```bash
sudo trace-cmd record -e sched:sched_migrate_task \
                      -e sched:sched_wakeup_new \
                      -e sched:sched_wakeup -- sleep 30
```

能看到每次任务迁移的 src/dst CPU。

### bcc / bpftrace 一把梭

```bash
# 迁移热图
sudo bpftrace -e '
  tracepoint:sched:sched_migrate_task
  { @[args->orig_cpu, args->dest_cpu] = count(); }'
```

## 本章小结

- 多核均衡通过**分层调度域**做，越底层越便宜越频繁；
- 触发点：tick 周期均衡、idle balance、nohz idle balance、唤醒时的 wake_affine；
- `load_balance` 的核心：找最忙 group → 找最忙 rq → 从中抢任务（尊重 affinity、cache hot、cgroup 约束）；
- NUMA balancing 在不对称内存系统上自动同步"任务-内存"位置；
- EAS 在异构算力系统上加一层能耗优化；
- 不想被均衡打扰？`isolcpus + nohz_full + taskset + SCHED_DEADLINE`。

下一篇我们回到"时间点"问题：抢占什么时候允许、上下文切换具体切了什么、返回用户态前又做了什么。
