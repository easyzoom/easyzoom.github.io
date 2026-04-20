---
title: rpmsg 内核驱动编写——通道名、端点与收发
author: EASYZOOM
date: 2026/04/08 14:10
categories:
 - Linux驱动开发
tags:
 - RPMSG
 - 内核驱动
 - virtio
 - 异构多核
---

## 前言

**C：** 本篇承接 [remoteproc 与 RPMSG 衔接](/courses/linuxdev/06-总线与典型子系统/rpmsg/02-remoteproc与RPMSG衔接-资源表与设备出现时机)，专门讲 **Linux 内核里 `rpmsg_driver` 怎么写**：通道如何匹配、**`callback` 里能做什么**、**发送 API 的差异与失败语义**，以及和 **并发 / 电源** 相关的常见坑。

<!-- more -->

::: tip 关于 API 版本
不同内核版本在 `rpmsg_create_ept`、`rpmsg_send_offchannel` 等符号上可能有增减；以下以 **主流主线思路** 为准，对接具体树时请 `rg` 本树 `include/linux/rpmsg.h` 与 `drivers/rpmsg/`。
:::

## 1. 匹配：名字对不上 = 永远不 probe

`rpmsg_device` 携带 **逻辑通道名**（`rpdev->id.name` 等）。你的驱动通过 **`rpmsg_device_id.name`** 或 **设备树 compatible**（若平台用 OF 匹配）与之绑定。

**最常见量产问题：** 固件侧创建的通道叫 `rpmsg-foo`，驱动里写 `rpmsg-bar`，则内核侧一切「正常」但你的 `probe` 永远不会被调用。

```c
static struct rpmsg_device_id foo_id_table[] = {
    { .name = "rpmsg-foo" },  /* 必须与对端创建的通道名一致 */
    { }
};
```

模块别名便于 `modprobe`：`MODULE_ALIAS("rpmsg:rpmsg-foo");`

## 2. 接收路径：`callback` 的上下文与纪律

```c
static int foo_rpmsg_cb(struct rpmsg_device *rpdev, void *data, int len,
            void *priv, u32 src)
```

- **`data` / `len`**：对端发来的 payload（具体是否含头部由你们协议定义）。  
- **`src`**：对端端点地址，**回包**时可能要原路带回或配合 `rpmsg_sendto`。  
- **上下文**：实现上常在 **原子或不可睡眠上下文** 调用（具体因传输后端而异）。**默认假设：不能睡眠**——不要 `mutex_lock` 可能睡眠的路径、不要分配大块可能触发直接回收的内存；重活 **拷贝必要数据后 `queue_work`**。

```mermaid
flowchart LR
  rx["callback 收到"] --> copy["快速校验/拷贝"]
  copy --> wq["queue_work：workqueue 里解析与回包"]
```

## 3. 发送：`rpmsg_send` 族与背压

常用入口：

- **`rpmsg_send(rpdev, buf, len)`**：发往 **绑定在该 `rpmsg_device` 上的默认对端**（具体语义由通道建立方式决定）。  
- **`rpmsg_sendto(rpdev, buf, len, dst)`**：显式指定 **目的地址**。  
- **`rpmsg_trysend` / `rpmsg_trysendto`**：**非阻塞**尝试；队列满时返回错误而不是等待（适合在原子上下文或避免死锁的场景）。

**失败时**可能表示：对端未就绪、缓冲区满、远端掉线、virtio 层背压等。生产代码需要 **退避重试、限流、或与业务状态机联动**，而不是简单打日志。

若需 **离通道发送**（off-channel），部分场景会用到 `rpmsg_send_offchannel` 一类 API——**务必阅读你内核版本的注释与调用约束**，避免与地址规划冲突。

## 4. 端点（endpoint）与多路复用

简单场景：**一个 `rpmsg_device` 对应一条逻辑通道**，收发都通过该设备上的 API 即可。

复杂场景：需要在同一条 virtio rpmsg 链路上 **动态创建多个本地端点** 时，才会涉及 **`rpmsg_create_ept`**（名称与是否导出随版本变化）。此时要搞清楚：

- **本地地址** 与 **远端地址** 的分配规则；  
- **`ept->callback`** 与 **设备级 callback** 的分工；  
- **`remove` / 固件重启** 时是否 **销毁 ept**、避免野指针。

若你尚处在「先跑通一条通道」阶段，可以 **先不碰动态 ept**，减少变量。

## 5. `probe` / `remove` 与生命周期

- **`probe`**：保存 `rpdev`、初始化协议状态、如需可向对端发 **握手包**（注意失败重试）。  
- **`remove`**：取消 work、刷新队列；若创建了额外 ept，在此释放。  
- **远端复位**：可能表现为 virtio 层错误、发送失败激增；不要假设 `rpdev` 指针永久有效，**以返回值和总线通知为准**。

## 6. 最小骨架（教学向）

与入门篇类似，强调 **`id_table.name`** 与 **`callback` 轻量**：

```c
#include <linux/module.h>
#include <linux/rpmsg.h>

static int foo_cb(struct rpmsg_device *rpdev, void *data, int len,
          void *priv, u32 src)
{
    dev_dbg(&rpdev->dev, "rx %d from 0x%x\n", len, src);
    return rpmsg_send(rpdev, data, len); /* 示例回显；生产需协议与错误处理 */
}

static int foo_probe(struct rpmsg_device *rpdev)
{
    dev_info(&rpdev->dev, "channel %s up\n", rpdev->id.name);
    return 0;
}

static void foo_remove(struct rpmsg_device *rpdev) { }

static struct rpmsg_device_id foo_id_table[] = {
    { .name = "rpmsg-foo" },
    { }
};

static struct rpmsg_driver foo_rpmsg_driver = {
    .drv.name = KBUILD_MODNAME,
    .id_table = foo_id_table,
    .callback = foo_cb,
    .probe = foo_probe,
    .remove = foo_remove,
};
module_rpmsg_driver(foo_rpmsg_driver);

MODULE_LICENSE("GPL");
MODULE_ALIAS("rpmsg:rpmsg-foo");
```

## 7. 小结

| 主题 | 建议 |
| --- | --- |
| 匹配 | 通道名字符串与固件 **逐字对齐** |
| 接收 | `callback` **快速返回**，重活进 workqueue |
| 发送 | 区分阻塞/非阻塞 API，**处理背压与远端不在线** |
| 生命周期 | `remove` 与远端复位路径都要 **收口资源** |

::: tip 同组文章
[RPMSG 异构核通信入门](/courses/linuxdev/06-总线与典型子系统/rpmsg/01-RPMSG异构核通信入门) · [RPMSG 用户态与调试实践](/courses/linuxdev/06-总线与典型子系统/rpmsg/04-RPMSG用户态与调试实践)
:::
