---
title: Linux驱动开发
author: EASYZOOM
date: 2026/04/07 12:00
categories:
 - Linux驱动开发
tags:
 - Linux
 - 驱动开发
 - 嵌入式
---

# Linux驱动开发

## 前言

**C：** 本系列聚焦 Linux 设备驱动：字符设备、平台总线、设备树、中断与 DMA、常见子系统（input、I2C、SPI 等）的入门与实践笔记，配合 ARM 等常见嵌入式场景。

<!-- more -->

## 本册内容范围

计划围绕下列方向展开（随写作进度持续调整）：

- 内核模块与用户空间接口（ioctl、sysfs 等）
- 设备树与平台驱动匹配
- 中断处理与下半部（tasklet、workqueue）
- 同步与并发在驱动中的典型用法
- 调试与常见崩溃分析思路

## 学习路线图

```mermaid
flowchart LR
  A[模块与日志基础] --> B[字符设备模型]
  B --> C[读写与ioctl]
  C --> D[平台驱动与设备树]
  D --> E[中断处理]
  E --> F[并发控制与排错]
  F --> G[DMA与内存管理进阶]
  G --> J[总线与典型子系统]
  J --> K[电源管理与时钟复位]
  G --> H[锁与实时性进阶]
  H --> I[trace与稳定性排障]
  I --> L[通用框架与内核抽象]
  L --> M[工程化与长期维护]
```

## 章节导航

### 第一组：驱动基础入门

1. [Linux 驱动开发总览与学习路径](/courses/linuxdev/01-驱动基础入门/01-Linux驱动开发总览与学习路径)
2. [第一个内核模块与 Makefile](/courses/linuxdev/01-驱动基础入门/02-第一个内核模块与Makefile)
3. [内核日志调试与常用工具](/courses/linuxdev/01-驱动基础入门/03-内核日志调试与常用工具)

### 第二组：字符设备与用户空间接口

1. [字符设备驱动模型与设备节点](/courses/linuxdev/02-字符设备与用户空间接口/01-字符设备驱动模型与设备节点)
2. [open、read、write、release 的最小实现](/courses/linuxdev/02-字符设备与用户空间接口/02-open-read-write-release的最小实现)
3. [ioctl 设计与用户内核数据交互](/courses/linuxdev/02-字符设备与用户空间接口/03-ioctl设计与用户内核数据交互)

### 第三组：平台驱动与设备树

1. [平台总线 platform_driver 与 probe 流程](/courses/linuxdev/03-平台驱动与设备树/01-平台总线platform_driver与probe流程)
2. [设备树基础与驱动匹配实战](/courses/linuxdev/03-平台驱动与设备树/02-设备树基础与驱动匹配实战)

### 第四组：中断并发与调试

1. [中断处理上下半部与工作队列](/courses/linuxdev/04-中断并发与调试/01-中断处理上下半部与工作队列)
2. [驱动中的并发控制与问题排查](/courses/linuxdev/04-中断并发与调试/02-驱动中的并发控制与问题排查)

## 高级专题导航

### 第五组：内存管理与 DMA

1. [DMA基础、物理地址、虚拟地址与总线地址](/courses/linuxdev/05-内存管理与DMA/01-DMA基础、物理地址、虚拟地址与总线地址)
2. [cache一致性、streamingDMA与coherentDMA](/courses/linuxdev/05-内存管理与DMA/02-cache一致性、streamingDMA与coherentDMA)
3. [IOMMU、ScatterGather与高性能数据通路设计](/courses/linuxdev/05-内存管理与DMA/03-IOMMU、ScatterGather与高性能数据通路设计)

### 第六组：总线与典型子系统

1. [I2C与SPI驱动设计对比](/courses/linuxdev/06-总线与典型子系统/01-I2C与SPI驱动设计对比)
2. [PCIe驱动基础、BAR、中断与资源映射](/courses/linuxdev/06-总线与典型子系统/02-PCIe驱动基础、BAR、中断与资源映射)
3. [USB驱动匹配、枚举与热插拔处理](/courses/linuxdev/06-总线与典型子系统/03-USB驱动匹配、枚举与热插拔处理)

#### I2C 专题（子目录 `i2c/`）

1. [I2C子系统与设备驱动要点](/courses/linuxdev/06-总线与典型子系统/i2c/01-I2C子系统与设备驱动要点)
2. [I2C适配器与控制器驱动主线](/courses/linuxdev/06-总线与典型子系统/i2c/02-I2C适配器与控制器驱动主线)
3. [I2C传输时序与错误处理-SMBus与总线恢复](/courses/linuxdev/06-总线与典型子系统/i2c/03-I2C传输时序与错误处理-SMBus与总线恢复)
4. [I2C设备树与调试实践](/courses/linuxdev/06-总线与典型子系统/i2c/04-I2C设备树与调试实践)

#### SPI 专题（子目录 `spi/`）

1. [SPI子系统与设备驱动要点](/courses/linuxdev/06-总线与典型子系统/spi/01-SPI子系统与设备驱动要点)
2. [SPI控制器与主机驱动主线](/courses/linuxdev/06-总线与典型子系统/spi/02-SPI控制器与主机驱动主线)
3. [spi_message与传输语义-全双工片选与性能](/courses/linuxdev/06-总线与典型子系统/spi/03-spi_message与传输语义-全双工片选与性能)
4. [SPI设备树与调试实践](/courses/linuxdev/06-总线与典型子系统/spi/04-SPI设备树与调试实践)

#### RPMSG 专题（子目录 `rpmsg/`）

1. [RPMSG异构核通信入门](/courses/linuxdev/06-总线与典型子系统/rpmsg/01-RPMSG异构核通信入门)
2. [remoteproc与RPMSG衔接-资源表与设备出现时机](/courses/linuxdev/06-总线与典型子系统/rpmsg/02-remoteproc与RPMSG衔接-资源表与设备出现时机)
3. [rpmsg内核驱动编写-通道名-端点与收发](/courses/linuxdev/06-总线与典型子系统/rpmsg/03-rpmsg内核驱动编写-通道名-端点与收发)
4. [RPMSG用户态与调试实践](/courses/linuxdev/06-总线与典型子系统/rpmsg/04-RPMSG用户态与调试实践)

### 第七组：电源管理与时钟复位

1. [runtimePM与设备空闲管理](/courses/linuxdev/07-电源管理与时钟复位/01-runtimePM与设备空闲管理)
2. [suspend与resume流程及常见陷阱](/courses/linuxdev/07-电源管理与时钟复位/02-suspend与resume流程及常见陷阱)
3. [clock、reset、regulator协同设计](/courses/linuxdev/07-电源管理与时钟复位/03-clock、reset、regulator协同设计)

### 第八组：并发、锁与实时性

1. [自旋锁、互斥锁、原子变量与RCU的取舍](/courses/linuxdev/08-并发、锁与实时性/01-自旋锁、互斥锁、原子变量与RCU的取舍)
2. [中断上下文、软中断、workqueue与线程化IRQ](/courses/linuxdev/08-并发、锁与实时性/02-中断上下文、软中断、workqueue与线程化IRQ)
3. [PREEMPT_RT下驱动设计注意事项](/courses/linuxdev/08-并发、锁与实时性/03-PREEMPT_RT下驱动设计注意事项)

### 第九组：调试、trace 与稳定性排障

1. [ftrace、tracepoints与函数级时序分析](/courses/linuxdev/09-调试、trace与稳定性排障/01-ftrace、tracepoints与函数级时序分析)
2. [perf、lockdep、kmemleak、KASAN常用排障手段](/courses/linuxdev/09-调试、trace与稳定性排障/02-perf、lockdep、kmemleak、KASAN常用排障手段)
3. [死锁、内存踩踏、偶发超时与崩溃现场分析](/courses/linuxdev/09-调试、trace与稳定性排障/03-死锁、内存踩踏、偶发超时与崩溃现场分析)

### 第十组：通用框架与内核抽象

1. [regmap、pinctrl与寄存器访问抽象](/courses/linuxdev/10-通用框架与内核抽象/01-regmap、pinctrl与寄存器访问抽象)
2. [input子系统与简单输入设备驱动](/courses/linuxdev/10-通用框架与内核抽象/02-input子系统与简单输入设备驱动)
3. [netdev、DRM、V4L2框架入门方法论](/courses/linuxdev/10-通用框架与内核抽象/03-netdev、DRM、V4L2框架入门方法论)

### 第十一组：工程化与长期维护

1. [驱动分层、平台差异隔离与BSP适配策略](/courses/linuxdev/11-工程化与长期维护/01-驱动分层、平台差异隔离与BSP适配策略)
2. [版本兼容、回移植与Upstream思维](/courses/linuxdev/11-工程化与长期维护/02-版本兼容、回移植与Upstream思维)
3. [日志、诊断接口、可测试性与长期维护](/courses/linuxdev/11-工程化与长期维护/03-日志、诊断接口、可测试性与长期维护)

## 配套实验源码

当前已经补了入门与高级两类实验目录：

- `examples/linuxdev/01-hello-lkm/`：最小内核模块与 Makefile
- `examples/linuxdev/02-ezpipe/`：字符设备 `open/read/write/release`
- `examples/linuxdev/03-dma-mapping-skeleton/`：DMA 映射、描述符和 SG 生命周期骨架
- `examples/linuxdev/04-threaded-irq-workqueue/`：hardirq、线程化 IRQ、workqueue 分工骨架
- `examples/linuxdev/05-tracing-debug-playbook/`：trace/debug 常用脚本与排障清单
- `examples/linuxdev/06-runtime-pm-skeleton/`：runtime PM 忙闲切换与 autosuspend 骨架
- `examples/linuxdev/07-regmap-pinctrl-skeleton/`：regmap 与 pinctrl 状态切换骨架

后续若继续扩展，我会再把 `PCIe/MSI-X`、`USB/URB`、`input`、`V4L2` 等方向的示例骨架逐步补齐。

## 学习建议

- 用户空间能解决的尽量不要进内核；进内核后调试成本陡增。
- 与具体 SoC/BSP 强相关的内容会以「某平台示例」形式标注，避免泛化成通用真理。

::: tip 持续更新中

章节与示例会陆续补充；若你发现疏漏或与所用内核版本不符之处，欢迎评论交流。

:::
