---
title: FUSE 的坑与生产实践
author: EASYZOOM
date: 2026/04/23 09:00
categories:
 - Linux内核开发
tags:
 - Linux
 - 内核
 - FUSE
 - 生产
---

# FUSE 的坑与生产实践

## 前言

**C：** 前几章 FUSE 看起来挺美好——架构清晰、开发快、性能也在变好。但真把它上到生产，就会遇到各种"设计之外"的问题：死锁、mount 挂死、kill 进程后整个挂载点僵住、权限被穿透、页缓存语义、非特权挂载的系统配置……这一章把我们在实际项目里踩过的坑整理出来，分故障模式讲清楚。把这些清单过一遍，能帮你少掉很多头发。

<!-- more -->

## 经典死锁：FUSE 进程访问自己挂载点

**症状**：FUSE 进程卡死，挂载点所有访问都 D 状态。

**原因**：FUSE 进程本身去 `open/read/write` 它自己挂载的目录 → VFS 把请求派给 `fuse.ko` → `fuse.ko` 往 `/dev/fuse` 塞消息等回复 → 谁来处理？就是被卡的那个进程。完美循环。

常见触发：

- 日志文件写到挂载点里；
- core dump 路径在挂载点里；
- Go/Python 运行时去读 `/proc/self/maps` 触发 mmap 里的 FUSE 页；
- 用户态实现里手滑 `fopen("/mnt/fuse/...")`。

**对策**：

- 日志和临时文件**严格不能**放在自己挂载的目录下；
- 设置 `core_pattern` 到一个绝对不受自己控制的路径；
- 尽量给 FUSE 进程单独的 mount namespace，只能看到自己需要的文件系统；
- 在 libfuse 代码里审视所有 IO 调用，配 lint 规则禁止挂载点前缀。

多线程版本：如果**所有 worker 线程都陷在处理请求里、且某个请求又依赖其它请求完成**，照样会饿死。经验上：

- 尽量让请求处理是"纯函数"式的，不跨请求等待；
- 如果必须跨请求，开单独的线程池，不要占满 FUSE worker。

## mount 挂死：fuse_conn 未初始化

**症状**：`mount -t fuse` 返回成功，但后续 `ls` 立即卡住，进程不能 kill（`D` 状态）。

**原因**：用户态 `fuse_main` / `fuse_session_loop` 还没起来，或起来了没处理 `FUSE_INIT`，内核一直在等协议握手。

**对策**：

- **先起循环，再退出 main 线程 daemon 化**：libfuse 默认这么做，但有些魔改版本顺序反了；
- 测试时加 `-f` 前台跑，能看到错误；
- 在 `init` 回调里**尽快**返回，别在里面做慢操作。

如果挂死了怎么恢复：

```bash
# 1. 先尝试 fusermount3 -u
fusermount3 -u /mnt/fuse

# 2. 懒卸载
sudo umount -l /mnt/fuse

# 3. 仍不行, 通过 sysfs 强制 abort
ls /sys/fs/fuse/connections/
# 每个数字目录对应一条 fuse_conn
echo 1 | sudo tee /sys/fs/fuse/connections/<N>/abort
```

`abort` 会让内核把所有 pending 请求直接以 `-ENOTCONN` 返回，`D` 状态进程立刻被踢出去。

## Kill 悬挂：进程退出但挂载点僵住

**症状**：`kill -9` 了 FUSE 进程，访问挂载点报 "Transport endpoint is not connected"，但 `umount` 说 "device is busy"。

**原因**：

- 挂载点上还有文件句柄；
- 或者有进程 `cwd` 指在这里；
- 或者有 mount namespace 没回收。

**对策**：

```bash
# 查谁还在用
sudo lsof /mnt/fuse | head
sudo fuser -vm /mnt/fuse

# 查 cwd
sudo ls -l /proc/*/cwd 2>/dev/null | grep /mnt/fuse

# 懒卸载(立即脱开，等引用归零自动清理)
sudo umount -l /mnt/fuse
```

生产上最好用 `-o auto_unmount` 挂载：

```bash
sshfs user@host: /mnt -o auto_unmount
```

进程退出时 libfuse 会主动 `fusermount3 -u`，减少僵尸挂载的几率。

## 权限陷阱

### allow_other 的安全语义

默认 FUSE 挂载**只有挂载者自己能访问**——这是内核为了防止普通用户用 FUSE 做权限攻击。打开 `allow_other` 后其它用户才能进。

但这里有个坑：你的 FUSE 实现如果不**自己做权限检查**，别人进来就是 root（因为 FS 的文件属性是你 `getattr` 返回的，你说啥就是啥）。解决方案二选一：

- **挂载时加 `default_permissions`**：让内核用你返回的 mode/uid/gid 做标准 POSIX 检查；
- **在每个回调里自己查 `fuse_get_context()`**。

实战推荐：**一定要加 `default_permissions`**，除非你有特别明确的理由（比如要做 bindfs 风格的用户映射）。

### 非特权挂载 + rootless container

现代容器运行时（buildah、podman rootless）喜欢在 user namespace 里跑 FUSE。需要：

- 在 `/etc/fuse.conf` 里启用 `user_allow_other`（Linux 没有 `sysctl fs.fuse.allow_other`，这是个文件配置而非 sysctl）；
- 容器内部 `/dev/fuse` 得可见，通常要 `--device /dev/fuse`；
- 容器内部有合适的 uid_map / gid_map；
- libfuse 3.10+ 对 idmap 有支持。

### suid / sgid 保护

默认 `nosuid,nodev` 是 FUSE 的强制标志，即使你 mount 时没写，内核也会加上。这是为了防止用户做一个 FUSE 目录，里面放一个 suid=root 的假文件去骗内核。

## 缓存一致性坑

### 用户 A 改了，用户 B 看不到

典型场景：两台机器都挂了同一个远程，A 写了文件，B `cat` 出来是旧的。

原因：B 端的内核页缓存和 attr 缓存都还是旧的。

解决：

- 减小 `attr_timeout / entry_timeout`（但性能下降）；
- 让你的 FS 订阅后端变更，在变更时调：

```c
fuse_lowlevel_notify_inval_inode(se, ino, 0, 0);
fuse_lowlevel_notify_inval_entry(se, parent_ino, name, strlen(name));
```

- 或者在 open 时返回 `!FOPEN_KEEP_CACHE`，强制每次 open 重新拉。

### writeback cache 下的 mmap 语义

开了 writeback cache 之后，mmap 的页是内核自己管理的——用户态的 write 不会立即被写入 mmap 区，反之亦然。后果：

- `mmap` + `msync` 语义变复杂；
- 数据库类负载可能看到"写了之后再读到老版本"的现象；
- 某些进程（比如 Java / libxml）会 mmap 文件做哈希校验，容易误判损坏。

对策：**对需要 mmap 严格语义的文件，open 时返回 `FOPEN_DIRECT_IO`**，绕过页缓存。

## 大文件与 64 位偏移

- FUSE 协议本身用 64 位 offset/size，没问题；
- 但 `max_read/write` 默认 128KB——大文件读取如果没设置大的 max_read，会变成成百上千的 4K read；
- `stat.st_size` 要支持 > 4GB，确保你的 API/序列化都用 64 位。

## statfs 必须实现

看似不重要的 `statfs`，**很多应用（bash 补全、apt-get、fallocate）会调用它**。如果没实现：

- `df /mnt/fuse` 返回奇怪的值；
- 有些程序判断"剩余空间"时直接失败；
- 某些 K8s CSI 探测会报错。

最小实现：

```c
static int fs_statfs(const char *path, struct statvfs *st) {
    st->f_bsize  = 4096;
    st->f_blocks = 1 << 30;          // 假装 4TB
    st->f_bfree  = st->f_bavail = 1 << 29;
    st->f_files  = 1 << 20;
    st->f_ffree  = 1 << 19;
    st->f_namemax = 255;
    return 0;
}
```

## rename / unlink 的并发复杂度

FUSE 的 rename 不支持"原子多步"，实现者要注意：

- `RENAME_NOREPLACE / RENAME_EXCHANGE`（5.0+ 协议 flag）需要显式处理；
- 跨 parent 的 rename 要加两把锁（父 + 目标父），注意固定顺序防死锁；
- 删除正在被 open 的文件：按 POSIX 应该推迟释放，直到最后一个 fd close——你的实现有没有这么做？

常见做法：给 `node` 加引用计数，`unlink` 只断链、`release` 时如果孤立就真正释放。

## xattr / ACL 的兼容性

许多程序（Finder、NetworkManager、systemd）会狂查 xattr：

- `security.capability`、`security.selinux`；
- `user.*` 自定义键；
- 不支持时必须返回 `-ENOTSUP` 或 `-ENODATA`，不要 `-EINVAL`。

别忘了 `SELinux` / `AppArmor`：如果系统启用了 MAC，FUSE 上的文件默认标签可能不合预期，导致应用报 "Permission denied"。挂载时可以加：

```
-o context="system_u:object_r:usr_t:s0"
```

## 运维与监控

### 看当前有多少 FUSE 连接

```bash
ls /sys/fs/fuse/connections/
# 每个数字目录一个
cat /sys/fs/fuse/connections/<N>/waiting
# 有多少 pending 请求
```

### 观察请求吞吐

```bash
# 用 bpftrace 统计每秒请求数
sudo bpftrace -e '
tracepoint:fuse:fuse_request_end
{ @[str(args->in_opcode_str)] = count(); }
interval:s:1
{ print(@); clear(@); }'
```

### alert 点

- `/sys/fs/fuse/connections/<N>/waiting` 持续 > 100：后端慢或 worker 数不够；
- `D` 状态进程数升高：基本就是 FUSE 在卡；
- 挂载点 rw 挂载但反复出现 EIO：后端不稳定；
- FUSE 进程 RSS 无限增长：lookup 计数不对，inode 没被 forget。

## 生产级部署建议

把前面所有坑浓缩成一份"做之前检查一下"的清单：

### 进程层

- 单独进程运行，尽量不要混杂业务；
- 日志、core dump、临时文件**全部在自己挂载点外**；
- 用 `systemd` 服务化，加 `Restart=on-failure`；
- 限制 `RLIMIT_NOFILE`、`RLIMIT_NPROC`；
- 设置 `OOMScoreAdjust=-500` 提高存活几率；
- 用 `MemoryMax=` 防止内存失控拖垮整机。

### 挂载选项

```
-o allow_other,default_permissions,auto_unmount,\
   noatime,\
   max_read=1048576
```

如果你信任 user：加上 `-o user_allow_other` 在 `/etc/fuse.conf`。

### 监控

- Prometheus exporter 把 `cpu.stat`、`/sys/fs/fuse/connections/*/waiting`、FUSE 进程 RSS/CPU、请求延迟直方图都采集起来；
- bpftrace 脚本定期采样 slow request；
- 有业务协议的后端（S3/NFS）要单独监控它本身的错误率。

### 故障演练

不定期做以下测试：

1. `kill -9` 掉 FUSE 进程，观察客户端应用行为、挂载点恢复时间；
2. 断开后端网络 30 秒再恢复，看缓存是否最终一致；
3. 大并发 stat 风暴（`find /mnt -type f | xargs stat`）压测；
4. 写入过程中拔电源（或 KVM 强制重启），看用户态 writeback 数据有没有丢。

这四个场景过了，基本就能上生产。

## 什么时候别硬上 FUSE

FUSE 用错地方也会很痛：

- **数据库底层存储**：别用，哪怕你开了 `direct_io + writeback_cache` 也不稳；
- **高并发小文件写**（邮件系统、CI 缓存）：优先考虑专用 FS + 分层缓存；
- **需要 `fsync` 保证每秒几万次**：FUSE 的 fsync 走完整 request/reply 往返，达不到；
- **严格 POSIX ACL / NFSv4 ACL**：FUSE 的 ACL 支持还不完善，别指望。

遇到这些场景，考虑：ext4/xfs + 业务层适配、ceph-kernel 而非 ceph-fuse、直接用内核 NFSv4.2 等。

## 本章小结

- FUSE 最大的坑是**循环依赖**（FUSE 进程依赖自己挂载点）和**缓存一致性**；
- 挂载卡死看 `/sys/fs/fuse/connections/*/abort`；
- 非特权挂载要理清 `user_allow_other`、`default_permissions`、`nosuid`；
- writeback cache 打开后 mmap 语义变复杂，对 db 类应用要开 direct_io；
- 生产化清单：独立进程、日志外置、监控全、演练勤；
- 不合适的地方就别硬套 FUSE——它不是银弹。

::: tip 急救速查表
| 症状 | 快速处置 |
|------|----------|
| `ls` 挂起不返回 | `echo 1 > /sys/fs/fuse/connections/<N>/abort`，然后 `fusermount3 -u` |
| "Transport endpoint is not connected" | `fusermount3 -u /mnt/xxx`（或 `sudo umount -l`） |
| 进程 kill 不掉（D 状态） | 先 abort 上面的连接，再 kill |
| "Permission denied" 但 root 也不行 | 检查是否加了 `allow_other` 且 `/etc/fuse.conf` 里有 `user_allow_other` |
| 数据不一致 / 读到旧内容 | 检查 `entry_timeout` 和 `attr_timeout` 设置，或用 `direct_io` |
:::

## 练习

1. **制造并恢复一次挂载卡死**：启动 memfs，然后 `kill -9` 进程，观察挂载点状态，用 `fusermount3 -u` 恢复。
2. **测试权限隔离**：以普通用户挂载 hello.c，然后换另一个用户 `ls` 挂载点——观察报错，再加上 `allow_other` 看变化。
3. **看 abort 流程**：挂载后 `cat /sys/fs/fuse/connections/*/waiting` 看当前阻塞请求数，在 memfs 的 `read` 回调里加 `sleep(30)`，然后从另一个终端 `echo 1 > .../abort`，观察 `cat` 返回什么。

至此"FUSE 用户态文件系统"这一册就讲完了。建议按顺序动手把 hello 和 memfs 跑一遍，再去看 libfuse 的 `passthrough_hp` 和 `virtiofsd` 的源码，你会真正感到"FUSE 是一套很成熟的工程体系"，而不只是一个小内核模块。
