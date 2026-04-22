---
title: FUSE 协议与请求流程详解
author: EASYZOOM
date: 2026/04/22 21:00
categories:
 - Linux内核开发
tags:
 - Linux
 - 内核
 - FUSE
 - 协议
---

# FUSE 协议与请求流程详解

## 前言

**C：** 上一章我们摆好了 FUSE 的"参与者"。这一章我们就拿放大镜看**他们之间说的话**——也就是 FUSE 协议。协议的定义在 `include/uapi/linux/fuse.h`（大约一千多行），看起来很长，但你掌握了请求头、几个关键 opcode 和 lookup/forget 的 inode 生命周期，就能看懂 90% 的 FUSE 行为。

::: tip 阅读建议
本章是"参考手册"性质，第一次读不必逐字看完。如果你更偏好先动手，可以**跳到第四章先写代码**，写完 hello.c 和 memfs 后再回来，那时候你对 LOOKUP、READ、WRITE 这些 opcode 会有直觉，读起来快得多。
:::

<!-- more -->

## 协议的基本约定

### 字节序与对齐

- **主机字节序**：FUSE 是"本机协议"，不跨网络（virtiofs 那种跨 VM 也保证同字节序），所以字段**不做转换**；
- **64 位对齐**：所有结构体都按 8 字节对齐，字符串以 `\0` 结尾；
- **版本**：目前主线是 `FUSE_KERNEL_VERSION = 7`、minor 在 30+，每加一个 feature minor 就涨。

### 每条消息都有固定头

```c
struct fuse_in_header {
    uint32_t len;        // 整条消息长度(含 header + payload)
    uint32_t opcode;     // FUSE_LOOKUP / FUSE_READ / ...
    uint64_t unique;     // 请求唯一 id, 回复必须带回
    uint64_t nodeid;     // 操作目标 inode (根是 1)
    uint32_t uid;
    uint32_t gid;
    uint32_t pid;
    uint32_t total_extlen;
    uint32_t padding;
};

struct fuse_out_header {
    uint32_t len;
    int32_t  error;      // 0 成功, 负值是 -errno
    uint64_t unique;
};
```

几个看似琐碎但很重要的点：

- **`unique`**：内核分配、用户态必须回传，FUSE 协议靠它匹配请求/回复；
- **`nodeid`**：FUSE 协议里的 inode 标识，内核和用户态必须保持一致——后面会专门讲生命周期；
- **`uid/gid/pid`**：触发这个操作的**调用方**信息，用来做权限判定（`default_permissions` 模式下内核会帮你做，否则你自己判）；
- **`error`**：0 表示成功，否则是 `-errno`（负数）。

### 请求流

一次完整的往返：

```
内核                           用户态
──────                        ──────
构造 fuse_in_header + args
放入 pending 队列
                  ──read──►   fuse_in_header + args
                              (解析、处理)
                  ◄─write──   fuse_out_header + args
按 unique 匹配请求
唤醒挂起的 syscall
```

用户态可以 **乱序回复** —— 只要 unique 能对上，内核就能找到对应的等待者。这是 FUSE 支持多线程的基础。

## opcode 全景

按功能分组看，opcode 分下面几大类。下面列出的是最常见、最需要实现的子集。

### 生命周期

| opcode | 方向 | 触发 |
|--------|------|------|
| FUSE_INIT | K→U | 挂载时首个消息，协商版本/feature |
| FUSE_DESTROY | K→U | 卸载时 |
| FUSE_INTERRUPT | K→U | 调用者按 Ctrl-C 等，希望中止某个请求 |

### Inode 管理

| opcode | 触发 |
|--------|------|
| FUSE_LOOKUP | 解析路径组件（`stat("a/b/c")` 的每一段都会来一次） |
| FUSE_FORGET | 内核不再引用某 nodeid，用户态可以回收 |
| FUSE_BATCH_FORGET | 批量 forget（减少切换） |
| FUSE_GETATTR | `stat()` |
| FUSE_SETATTR | `chmod/chown/utimes/truncate` |

### 目录

| opcode | 触发 |
|--------|------|
| FUSE_OPENDIR | `opendir()` |
| FUSE_READDIR / FUSE_READDIRPLUS | `getdents()`，PLUS 版本带属性 |
| FUSE_RELEASEDIR | `closedir()` |
| FUSE_MKDIR / FUSE_RMDIR / FUSE_RENAME(2) | 对应 syscall |

### 文件

| opcode | 触发 |
|--------|------|
| FUSE_CREATE | `open(O_CREAT)`（等价 LOOKUP + MKNOD + OPEN 的原子合并） |
| FUSE_OPEN | `open()` |
| FUSE_READ / FUSE_WRITE | I/O |
| FUSE_FLUSH | 每次 `close()` 都会有，**不是** fsync |
| FUSE_RELEASE | 最后一次 close |
| FUSE_FSYNC / FUSE_FSYNCDIR | 持久化 |

### 扩展属性与权限

| opcode | 触发 |
|--------|------|
| FUSE_GETXATTR / FUSE_SETXATTR / FUSE_LISTXATTR / FUSE_REMOVEXATTR | xattr |
| FUSE_ACCESS | `access(2)`（仅当关了 `default_permissions`） |

### 锁

| opcode | 触发 |
|--------|------|
| FUSE_GETLK / FUSE_SETLK / FUSE_SETLKW | POSIX / OFD 锁 |
| FUSE_FLOCK | BSD flock |

### 杂项 / 新特性

| opcode | 触发 |
|--------|------|
| FUSE_FALLOCATE | `fallocate()` |
| FUSE_COPY_FILE_RANGE | `copy_file_range()` |
| FUSE_LSEEK | `SEEK_HOLE/SEEK_DATA` |
| FUSE_POLL | 设备节点（FUSE 也支持字符设备行为） |
| FUSE_IOCTL | 自定义 ioctl |
| FUSE_NOTIFY_REPLY | 异步通知的回执 |

### 反向通知（U→K）

用户态也能主动给内核发消息，做缓存失效等：

| 通知 | 作用 |
|------|------|
| FUSE_NOTIFY_INVAL_INODE | "这个 inode 的缓存请丢掉" |
| FUSE_NOTIFY_INVAL_ENTRY | "这条目录项请丢掉" |
| FUSE_NOTIFY_STORE | 主动把数据塞进内核页缓存 |
| FUSE_NOTIFY_RETRIEVE | 从内核页缓存拉数据回来 |
| FUSE_NOTIFY_POLL | 唤醒等在 poll 上的进程 |

这些通知对"远程后端数据变了"的场景非常关键。

## 每个 opcode 的消息结构：读一次 README

`fuse.h` 里每条 opcode 都有输入/输出结构体，名字都遵循 `fuse_<op>_in` / `fuse_<op>_out`。挑几条常见的来看。

### LOOKUP：解析路径组件

**输入**：`nodeid`（父目录）+ 字符串名字（紧跟头之后，以 `\0` 结尾）。

**输出**：`fuse_entry_out`：

```c
struct fuse_entry_out {
    uint64_t nodeid;          // 新 nodeid
    uint64_t generation;      // 对抗 nodeid 复用
    uint64_t entry_valid;     // 目录项缓存秒
    uint64_t attr_valid;      // 属性缓存秒
    uint32_t entry_valid_nsec;
    uint32_t attr_valid_nsec;
    struct fuse_attr attr;    // stat 结果
};
```

`entry_valid` / `attr_valid` 告诉内核"这条信息能缓存多久"——这是 FUSE 决定性能的旋钮之一，下一章讲。

### READ

**输入**：`fuse_read_in`：

```c
struct fuse_read_in {
    uint64_t fh;        // FUSE_OPEN 回复里给的文件句柄
    uint64_t offset;
    uint32_t size;
    uint32_t read_flags;
    uint64_t lock_owner;
    uint32_t flags;     // open 时的 O_ 标志
    uint32_t padding;
};
```

**输出**：`fuse_out_header` 后面**直接**是数据字节，没有中间结构。长度由 out_header.len 减去 header 大小得到。

### WRITE

**输入**：`fuse_write_in` + 紧跟的数据：

```c
struct fuse_write_in {
    uint64_t fh;
    uint64_t offset;
    uint32_t size;
    uint32_t write_flags;
    uint64_t lock_owner;
    uint32_t flags;
    uint32_t padding;
};
```

**输出**：`fuse_write_out { uint32_t size; }` —— 实际写入字节数。

### READDIRPLUS

**输入**：`fuse_read_in`（同上）。

**输出**：一串连续的 `fuse_direntplus` 条目：

```c
struct fuse_direntplus {
    struct fuse_entry_out entry_out;   // LOOKUP 等价信息
    struct fuse_dirent    dirent;
};

struct fuse_dirent {
    uint64_t ino;
    uint64_t off;
    uint32_t namelen;
    uint32_t type;
    char     name[];
};
```

READDIRPLUS 的好处：一次 readdir 就把每个条目的 stat 带回去，少一轮 N 次 LOOKUP，在远程 FS 上效果立竿见影。

## inode 生命周期：lookup / forget

FUSE inode 的生命周期是**引用计数**的，这一点很多人踩过坑。

### 引用计数规则

- 每一次 `FUSE_LOOKUP` 返回一个 `nodeid` 时，**等于** 内核 dcache 里对这个 nodeid 的引用 +1；
- 也就是说**同一个 nodeid 可能被 lookup 多次**，每次 +1；
- 内核发 `FUSE_FORGET { nodeid, nlookup }` 时，意味着**一次性扣掉 nlookup 次引用**；
- 当引用归零，用户态才可以真正回收这个 nodeid 对应的资源。

### 一个时序例子

```
内核侧状态           协议消息               用户态状态
─────────────      ─────────────          ─────────────
ino=1 (root)                                 nodeid=1 existing
                   LOOKUP "foo"   →
                                           nodeid=42, ref+=1, reply
ino=42 ref=1
                   LOOKUP "foo"   →          ref+=1 (可能是另一个 dentry)
ino=42 ref=2
(dcache 回收)
                   ← FORGET {42, 2}
                                           ref-=2 → 0, 回收资源
```

### 常见 bug

- **用户态把 nodeid=inode 直接用 map 存，忘记计数**：用户一 ls，lookup 来两次，一 forget 你就删了——再访问就是野指针。libfuse HL API 帮你管这个，LL API 必须自己做；
- **把 nodeid 直接暴露为后端真实 ID**：后端删了、重建，nodeid 还在用户态活着——要用 `generation` 字段对抗这种"别名"问题；
- **没处理 FUSE_DESTROY**：卸载时要一次性 forget 所有 inode，且进程**不要立刻退**，否则 pending 请求拿不到回复。

## 目录项缓存：entry_timeout / attr_timeout

FUSE 允许你声明缓存时长：

- `entry_timeout`：这条目录项（name → nodeid）在内核 dcache 里存多久；
- `attr_timeout`：这个 inode 的属性（stat 结果）在内核 attr cache 里存多久；
- 0：不缓存，每次都问用户态；
- 很大值：尽量缓存，直到被反向通知失效。

对小文件高频 stat 的场景，`attr_timeout` 从 0 调到 1s 常能带来几倍性能提升——代价是如果后端被别人改了，用户会看到过期数据。

如果你的后端（比如 S3、分布式 FS）有能力通知变更，正确做法是**设置较大的 timeout + 主动发 `FUSE_NOTIFY_INVAL_*`**。

## 页缓存与 open flags

FUSE_OPEN 的回复里有两个关键 flag：

```c
struct fuse_open_out {
    uint64_t fh;
    uint32_t open_flags;   // FOPEN_* bits
    uint32_t padding;
};
```

| FOPEN_* | 含义 |
|---------|------|
| DIRECT_IO | 绕过页缓存（每次 read/write 都下到用户态） |
| KEEP_CACHE | open 时不丢弃已有页缓存（默认会丢） |
| NONSEEKABLE | 这个文件句柄不支持 lseek（管道类） |
| CACHE_DIR | readdir 结果可缓存（需要 reactor 支持） |
| STREAM | 类流文件，不支持随机访问 |
| NOFLUSH | release 时不发 flush |
| PARALLEL_DIRECT_WRITES | 允许对同文件多写者并发（绕过 FUSE 自带串行化） |

这些 flag 对性能行为影响极大：

- **DIRECT_IO**：适合数据会经常变动的后端；
- **KEEP_CACHE**：大文件顺序读，reopen 时很省；
- **PARALLEL_DIRECT_WRITES**：对象存储后端常用，避免了 FUSE 默认"同一个 inode 的写串行化"。

## 中断与并发

### INTERRUPT 消息

用户 Ctrl-C / 进程被 signal 时，内核会发 `FUSE_INTERRUPT { unique }`：

- 告诉用户态"这个 unique 对应的请求可以放弃了"；
- 用户态**仍然需要**给原请求一个回复（常见是 `-EINTR`）；
- 如果用户态忽略这条消息、继续完成原工作，内核会等——但应用层已经在催了。

libfuse 默认帮你处理 INTERRUPT。

### 请求并发

同一挂载点可以有许多 pending 请求并行：

- 内核侧按 nodeid 做一定串行化（如同文件写默认串行），除非 open 时带了 `PARALLEL_DIRECT_WRITES`；
- 用户态可以通过多线程 + clone fd 并发处理；
- 每条请求都有自己的 `unique`，乱序回复没问题。

## 用 wireshark-like 工具观察协议

FUSE 协议不走网络，但可以借助几种方法观察：

### 1. fuse_debug

```bash
# 大多数 FUSE 文件系统支持 -d 参数
sshfs -d -o debug user@host: /mnt/remote
```

会把每条 in/out 消息打印到 stderr，最直观。

### 2. 内核 tracepoint

较新内核提供 `fuse` tracepoint 子系统：

```bash
sudo trace-cmd record -e fuse -- ls /mnt/fuse
sudo trace-cmd report
```

可以看到每个 opcode、unique、nodeid 的走向。

### 3. bpftrace

```bash
# 使用 tracepoint（推荐，不受内核结构体变动影响）
sudo bpftrace -e '
tracepoint:fuse:fuse_request_send
{ printf("unique=%llu op=%s\n", args->unique, str(args->in_opcode_str)); }'
```

> 注意：不同内核版本的 FUSE tracepoint 字段名可能略有差异，如果报错可先 `sudo bpftrace -lv 'tracepoint:fuse:*'` 查看可用字段。

### 4. dynamic_debug

```bash
echo "file fs/fuse/*.c +p" | sudo tee /sys/kernel/debug/dynamic_debug/control
# dmesg 里会多出 FUSE 的日志
```

## 一个完整的 ls 例子

看一下 `ls /mnt/fuse/foo` 会产生多少协议消息：

```
1. LOOKUP(nodeid=1, name=foo)          → entry_out{ nodeid=42, attr }
2. OPENDIR(nodeid=42)                   → fh=X
3. READDIRPLUS(fh=X, off=0, size=4K)    → 多个 direntplus
4. READDIRPLUS(fh=X, off=N, size=4K)    → 0 字节 (EOF)
5. RELEASEDIR(fh=X)
6. (每个 ls 输出的条目已经靠 READDIRPLUS 带回 stat 了,
    所以不再需要单独 LOOKUP/GETATTR)
7. FORGET(nodeid=42, nlookup=1)  (退出时)
```

如果你的 FS 实现了 READDIRPLUS，`ls -l` 之后**几乎没有额外的 LOOKUP/GETATTR**；如果只实现了 READDIR，每个条目都会单独触发一次 LOOKUP——这在远程 FS 上差距可能是几十倍。

## 本章小结

- FUSE 协议由一个固定 header + 每 opcode 各自 in/out 组成，主机字节序、64 位对齐；
- 常用 opcode 分六类：生命周期、inode 管理、目录、文件、xattr/锁、特殊；
- inode 生命周期靠 LOOKUP/FORGET 的引用计数，**必须**在用户态实现正确的计数；
- 目录项/属性缓存由 `entry_timeout / attr_timeout` 控制，需要变更通知时用 `FUSE_NOTIFY_INVAL_*`；
- 实现 READDIRPLUS、挑对 FOPEN_* flag，是性能 tuning 的第一步；
- 调试靠 `-d`、tracepoint、bpftrace、dynamic_debug 四板斧。

::: tip 想先动手？
如果你觉得协议细节读起来有点枯燥，可以**先跳到下一章写代码**——把 hello.c 和 memfs 跑起来之后，再回来看协议会直观很多。
:::

下一章我们终于动手写代码——从一个 `hello.c` 出发，做一个能 ls/read 的内存文件系统。
