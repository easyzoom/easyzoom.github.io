---
title: FUSE 性能优化与内核新特性
author: EASYZOOM
date: 2026/04/22 23:00
categories:
 - Linux内核开发
tags:
 - Linux
 - 内核
 - FUSE
 - 性能
---

# FUSE 性能优化与内核新特性

## 前言

**C：** "FUSE 慢"是老生常谈。它确实慢——每条 I/O 都要一次用户-内核往返和两次拷贝，起步延迟就比内核 FS 高一个数量级。但这几年主线内核围绕 FUSE 做了大量改进：writeback cache、splice、multi-queue、io_uring、passthrough、BPF 加速——这些东西叠起来，在很多场景下 FUSE 的性能已经能打到原生 FS 的 70–95%。这一章我们把优化手段按"越用越厉害"的顺序排列，每一层都讲清楚它解决了什么、代价是什么。

<!-- more -->

## 先搞清楚瓶颈在哪

在动手之前必须量化。常用工具：

```bash
# 元数据 benchmark
fio --name=metadata --directory=/mnt/fuse --nrfiles=10000 \
    --filesize=4k --openfiles=100 --rw=randread --bs=4k \
    --size=4k --runtime=30 --time_based

# 顺序读
fio --name=seq-read --directory=/mnt/fuse --filename=big.bin \
    --size=1G --rw=read --bs=1M --direct=0 --runtime=30

# 随机小 I/O
fio --name=rand-read --directory=/mnt/fuse --filename=big.bin \
    --size=1G --rw=randread --bs=4k --direct=0 --runtime=30 --iodepth=32

# 看 FUSE 每条请求的延迟
sudo bpftrace -e '
tracepoint:fuse:fuse_request_send { @start[args->unique] = nsecs; }
tracepoint:fuse:fuse_request_end
  /@start[args->unique]/
  { @lat = hist(nsecs - @start[args->unique]); delete(@start[args->unique]); }'
```

一般你会看到几种模式：

- **每条 I/O 延迟都偏高**：用户态处理慢或开销大，调整缓存和 splice；
- **小 I/O 慢，大 I/O 还行**：元数据 / 请求头开销占比高，调大 `max_write/max_read`；
- **CPU 大量在 copy**：换 splice、passthrough；
- **单线程饱和**：开 clone fd + 多 worker；
- **fsync 慢**：后端本身慢，或 writeback 没开。

优化也按这个顺序对症下药。

## 第一层：缓存调优

### 属性/目录项缓存

默认 libfuse 高层 API 里 `entry_timeout = 1.0`、`attr_timeout = 1.0`。对"后端不变或者可变通知"的场景，调大很划算：

```c
// LL API
struct fuse_entry_param e = {
    .ino = ino, .generation = 1,
    .attr_timeout = 60.0,       // 属性缓存 60s
    .entry_timeout = 60.0,
};
```

对 `getattr` 加上长 timeout 之后：

- `ls -l` 一次之后再敲不用过用户态；
- `stat` 风暴（build 工具最爱）被挡在内核；
- 代价：后端外部改动时用户会看到陈旧属性，解决办法是 `fuse_lowlevel_notify_inval_*` 主动推失效。

### 页缓存

`FOPEN_KEEP_CACHE` 让内核在 `open` 时**不丢弃**现有页缓存。适合：

- 顺序大文件读，同一个文件被多次打开；
- 只读挂载。

如果你是一个**可能被其它节点改**的分布式 FS，要么别开它，要么开 `EXPLICIT_INVAL_DATA` 手动失效。

### writeback cache

这是最戏剧性的一个优化。默认 FUSE 的 write 是"**写透**"——每次 `write(2)` 都要同步发到用户态，用户态写完才返回。开了 `FUSE_WRITEBACK_CACHE`（在 INIT 时 opt-in）之后：

- 用户 write 先进内核页缓存，**立即返回**；
- 内核攒够一页或达到阈值再后台刷到用户态；
- fsync 时一次性下去。

效果：

- 顺序写吞吐可以提升数倍到一个数量级；
- 小 write 合并成大 write，用户态逻辑也更高效；
- 代价：崩溃时丢数据；mmap 的语义更复杂；需要 `FUSE_OPEN` 返回合适的 cache 策略。

开启方式：

```c
static void ll_init(void *userdata, struct fuse_conn_info *conn) {
    if (conn->capable & FUSE_CAP_WRITEBACK_CACHE)
        conn->want |= FUSE_CAP_WRITEBACK_CACHE;
    conn->max_write = 1 << 20;  // 1MB
}
```

## 第二层：更大的 I/O 块

协议里 `max_read` / `max_write` 决定**单次 request 能带多少数据**。默认是 128KB，对现代硬件太小：

```c
conn->max_read  = 1 << 20;   // 1MB
conn->max_write = 1 << 20;
```

配合内核侧 `FUSE_MAX_PAGES`（协议 7.28+）：支持最多 256 页（1MB）单次请求。挂载时如果看到：

```
fuse: requested kernel page limit N >= MAX_READ_AHEAD
```

就要同步调：

```bash
mount -o max_read=1048576 ...
```

收益：大 I/O 的请求数线性下降，整体吞吐提升明显。

## 第三层：splice 零拷贝

前面讲过 FUSE 天生有**两次拷贝**。内核 4.x 起支持 splice：

- 内核 → `/dev/fuse`：用 `pipe` 中转，不拷贝到用户态缓冲区；
- `/dev/fuse` → 内核：同样 `splice`。

需要两端都 opt-in：

```c
conn->want |= FUSE_CAP_SPLICE_READ | FUSE_CAP_SPLICE_WRITE;
// libfuse 接收方调用 fuse_session_receive_buf_int 时会走 splice
```

libfuse 3.x 默认能走 splice，只要你 `fuse_reply_data(req, bufv, FUSE_BUF_SPLICE_MOVE)` 而不是 `fuse_reply_buf()`。

对大 I/O 场景，splice 一开，拷贝大头就消掉了，CPU 使用率通常下降 20–40%。

## 第四层：多线程 + clone fd

### 基本多线程

libfuse 的 `fuse_session_loop_mt` 早就支持每请求一个 worker：

```c
struct fuse_loop_config cfg = {
    .clone_fd = 1,           // 关键: 让每线程独占 fd
    .max_idle_threads = 10,
    .max_threads = 64,
};
fuse_session_loop_mt(se, &cfg);
```

- `clone_fd = 1` 启用 `FUSE_DEV_IOC_CLONE`，每个 worker 有独立 `/dev/fuse` fd；
- 内核那头也会给每个 fd 独立处理队列，**无锁分发**；
- 多核下线性扩展。

### 绑核 / NUMA 亲和

```c
cpu_set_t set; CPU_ZERO(&set); CPU_SET(core, &set);
pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
```

把 worker 绑到固定核，再让客户端请求也尽量走同一核——能降 cache miss。virtiofsd 有整套绑核 + NUMA 的策略。

### FUSE 队列并发（async, parallel）

INIT 时请求能力：

```c
conn->want |= FUSE_CAP_ASYNC_READ
           |  FUSE_CAP_PARALLEL_DIROPS
           |  FUSE_CAP_ATOMIC_O_TRUNC;
```

- `ASYNC_READ`：同一个文件允许多个并行 read（默认串行）；
- `PARALLEL_DIROPS`：同一目录下 lookup/create 并行；
- `HANDLE_KILLPRIV_V2`：让内核处理 suid 位清除，省往返。

## 第五层：io_uring 直通

2024 年起主线开始合 `fuse-over-io_uring`：

- 用户态直接用 `io_uring` 处理 FUSE 消息，省掉 `read(/dev/fuse)` 的切换；
- 多核友好、尾延迟更稳；
- 需要内核 `CONFIG_FUSE_IO_URING` 与 libfuse 较新版本。

启用方式（以较新 libfuse 为例）：

```c
conn->want |= FUSE_CAP_IO_URING;
/* loop 函数名在内核/libfuse 迭代中可能变化，
 * 截至 2025 年部分 patchset 使用 fuse_session_loop_uring()，
 * 请以你实际 libfuse 版本头文件为准。 */
```

实测看一些 benchmark，对 4k 随机读可把 IOPS 提升 30–80%。

> 注意：fuse-over-io_uring 在 6.10–6.12 间经历了多次 API 调整，属于"积极开发中"的特性。生产环境建议锁定一个已验证的内核版本。

## 第六层：passthrough

这是 2024–2025 的重头戏。思路很简单：**如果 FUSE 文件最终就是"代理到另一个 fd"（passthrough_hp 干的事），为什么要每次 read/write 都绕一趟用户态？**

于是内核提供了 `FUSE_PASSTHROUGH`：

- 用户态在 `open` 回调里**把一个真实 fd 注册给内核**；
- 之后内核对这个 FUSE 文件的 read/write 直接转发到那个 fd；
- 用户态完全不参与 I/O 路径。

```c
/* 伪代码——展示思路，实际 API 因内核版本而异（6.9+ 初步合入）
 * 请参考最新 fs/fuse/passthrough.c 和 example/passthrough_hp.cc */
// 在 open 回调里, 已经 open 了底层文件 real_fd
struct fuse_backing_map map = { .fd = real_fd, .flags = 0 };
int backing_id = ioctl(fuse_fd, FUSE_DEV_IOC_BACKING_OPEN, &map);
// 把 backing_id 放到 open reply 里，内核就会对后续 read/write 直通
out_arg->backing_id = backing_id;
```

> 注意：上面只是简化示意。passthrough API 在 6.9 ~ 6.12 间仍有变化，生产使用请对照你实际跑的内核版本以及 `example/passthrough_hp.cc` 源码。

效果：对 passthrough 文件系统（所有的 "代理式" FUSE，比如 bindfs、权限翻译、rootless container rootfs），I/O 性能**几乎与原生 FS 相同**。元数据仍然走常规 FUSE，但那本来就快。

## 第七层：BPF 加速

5.10 之后几个 RFC/patchset 在推 **FUSE + BPF**：在内核里挂 BPF 程序，对某些 opcode**直接在内核处理**，连用户态都不进。典型用法：

- 大量 stat 风暴时，BPF 查一个事先灌好的哈希表直接返回；
- 只读文件的 read 走预先注册的 backing file；
- 元数据缓存命中时零成本。

这个方向还在演化，但趋势是清晰的：**把用户态必须参与的部分降到最小，其他都在内核闭环**。

## virtiofs：另一种优化方向

前面章节介绍过，virtiofs 把 FUSE 协议搬到 virtio 上。在 VM 共享场景里：

- 消息走 virtio-queue，不经过 host 的 `/dev/fuse`；
- 开 DAX 时，host 页缓存直接映射进 guest —— 读热页不走 daemon；
- 比 9p、NFS 快一个数量级。

如果你在写"宿主机和 VM 之间的大文件共享"，不要再考虑 9p，直接上 virtiofs。

## 一张对照表：不同优化的收益粗估

以"相对原生 ext4 的吞吐/延迟"为基准（经验值，实际看具体工作负载）：

| 配置 | 顺序读 | 顺序写 | 小随机 I/O | 元数据 (stat) |
|------|--------|--------|-----------|---------------|
| 裸 FUSE HL API | 20–40% | 10–30% | 5–15% | 10% |
| + attr/entry_timeout | 20–40% | 10–30% | 5–15% | **60–90%** |
| + max_read/write=1M | 40–60% | 30–50% | 5–15% | 同上 |
| + writeback cache | 40–60% | **60–90%** | 10–30% | 同上 |
| + splice | 60–80% | 70–90% | 10–30% | 同上 |
| + multi-thread/clone | 60–80% | 70–90% | 30–60% | 同上 |
| + io_uring | 70–90% | 80–95% | 50–80% | 同上 |
| + passthrough（适用时） | **95%+** | **95%+** | **90%+** | 同上 |

如果你对性能很在意，至少要把前 4 层（缓存、大 I/O、writeback、splice）全部打开——这只是几十行代码的事。再往后视需求上 io_uring、多线程和 passthrough。

## 实战 checklist

一个生产级 FUSE 实现，启动时至少应该配置：

```c
static void fs_init(void *userdata, struct fuse_conn_info *conn) {
    /* I/O 块 */
    conn->max_read  = 1 << 20;
    conn->max_write = 1 << 20;

    /* 缓存 */
    if (conn->capable & FUSE_CAP_WRITEBACK_CACHE)
        conn->want |= FUSE_CAP_WRITEBACK_CACHE;
    if (conn->capable & FUSE_CAP_READDIRPLUS)
        conn->want |= FUSE_CAP_READDIRPLUS;
    if (conn->capable & FUSE_CAP_READDIRPLUS_AUTO)
        conn->want |= FUSE_CAP_READDIRPLUS_AUTO;

    /* 零拷贝 */
    if (conn->capable & FUSE_CAP_SPLICE_WRITE)
        conn->want |= FUSE_CAP_SPLICE_WRITE;
    if (conn->capable & FUSE_CAP_SPLICE_READ)
        conn->want |= FUSE_CAP_SPLICE_READ;
    if (conn->capable & FUSE_CAP_SPLICE_MOVE)
        conn->want |= FUSE_CAP_SPLICE_MOVE;

    /* 并发 */
    if (conn->capable & FUSE_CAP_ASYNC_READ)
        conn->want |= FUSE_CAP_ASYNC_READ;
    if (conn->capable & FUSE_CAP_PARALLEL_DIROPS)
        conn->want |= FUSE_CAP_PARALLEL_DIROPS;
    if (conn->capable & FUSE_CAP_ATOMIC_O_TRUNC)
        conn->want |= FUSE_CAP_ATOMIC_O_TRUNC;

    /* 权限 */
    if (conn->capable & FUSE_CAP_HANDLE_KILLPRIV_V2)
        conn->want |= FUSE_CAP_HANDLE_KILLPRIV_V2;

    /* io_uring / passthrough —— 仅在合适场景启用 */
    // conn->want |= FUSE_CAP_IO_URING;
    // conn->want |= FUSE_CAP_PASSTHROUGH;
}
```

loop 循环：

```c
struct fuse_loop_config cfg = {
    .clone_fd = 1,
    .max_idle_threads = 4,
    .max_threads = sysconf(_SC_NPROCESSORS_ONLN),
};
fuse_session_loop_mt(se, &cfg);
```

这一套 + 合理的 attr/entry_timeout + 正确的 readdirplus 实现，大多数负载都能达到"够用"的性能。

## 案例：从 20MB/s 到 800MB/s

一个真实案例：某团队 FUSE 挂载 S3 做大文件分发，起步吞吐 20MB/s，CPU 全在 FUSE 进程里。优化路径：

1. 打开 `writeback_cache` + `max_write=1M`：→ 80MB/s；
2. 开 splice + 避免一次内存拷贝：→ 200MB/s；
3. clone_fd + 8 线程：→ 450MB/s；
4. S3 并发 GET + 内部预读：→ 800MB/s；
5. 大文件走 passthrough 到本地 cache 文件：→ 1.2GB/s（接近本地 SSD 上限）。

每一步都有可量化收益，次序也符合"先消 CPU 开销，再提并行度，最后消路径"的通用优化哲学。

## 本章小结

- FUSE 性能优化按层递进：缓存 → 大 I/O → 零拷贝 → 多线程 → io_uring → passthrough；
- 前 4 层基本没坏处，应该**默认打开**；
- io_uring、passthrough、BPF 代表新方向，合适场景能把 FUSE 拉到原生水准；
- 再好的参数也抵不过设计：该缓存就缓存，该 readdirplus 就 readdirplus，别让小 I/O 风暴形成。

## 练习

1. **量化缓存收益**：用前一章的 memfs，分别 `mount -o writeback_cache` 和不加的情况下跑 `dd if=/dev/zero of=/tmp/mnt/big bs=1M count=64`，对比耗时。
2. **看 splice 效果**：在 `init` 里加 `FUSE_CAP_SPLICE_READ | FUSE_CAP_SPLICE_WRITE`，用 `perf stat` 对比 `copy_user` 次数变化。
3. **画延迟对比图**：用 `fio --lat_percentiles=1` 分别在 single-thread 和 clone_fd + 4 线程下跑 4k randread，对比 p99 延迟。

下一章我们讲最容易被忽略的那一面——**FUSE 的坑**：死锁、cred 检查、mount 挂死、kill 悬挂、权限穿透、xattr 兼容、大页/mmap 语义等生产环境真实问题。
