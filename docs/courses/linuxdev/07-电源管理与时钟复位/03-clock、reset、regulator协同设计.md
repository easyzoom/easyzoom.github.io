---
title: clock、reset、regulator协同设计
author: EASYZOOM
date: 2026/04/07 18:30
categories:
 - Linux驱动开发
tags:
 - clock
 - reset
 - regulator
 - 电源管理
---

# clock、reset、regulator协同设计

## 前言

**C：** 在很多 SoC 平台里，一个设备能否正常工作，并不只取决于“驱动 probe 了没有”，而取决于三件基础资源是否被正确协同：时钟、复位和供电。很多驱动明明寄存器代码没问题，但设备偶发起不来、resume 后状态错乱、某些板子能跑某些板子不能跑，根源经常就在这些基础资源的使能时序和依赖关系没有建好。本篇就讲这类资源为什么要被当成状态机，而不是零散函数调用。

<!-- more -->

## 三类基础资源的关系

```mermaid
flowchart LR
  regulator["regulator供电"] --> clock["clock时钟"]
  clock --> resetCtl["reset释放"]
  resetCtl --> deviceReady["设备寄存器与功能可访问"]
  deviceReady --> runtimePm["进入运行态或PM状态机"]
```

## 为什么这三类资源要一起看

很多问题并不是某一个 API 调错，而是顺序和依赖错了。  
例如：

- 电还没起来就访问寄存器
- 时钟没开就解除 reset
- 设备还在 reset 中就开始初始化队列

这类问题的表面症状可能非常随机：

- probe 偶发失败
- 某些板子稳定，某些板子不稳定
- 冷启动和热启动行为不同

所以高级工程师不会把 clock/reset/regulator 看成三个孤立子系统，而是把它们看成**设备上电状态机**的一部分。

## regulator 解决的是“设备是否真正有电”

regulator 不只是电压开关，它还涉及：

- 上电时序
- 依赖电源域
- 不同板级配置差异

如果驱动没把 regulator 视为一等资源，常见后果是：

- 某些板卡上根本没按期望供电
- PM 状态切换后供电没恢复
- 功耗优化时把核心供电关早了

## clock 影响的不只是“跑多快”

很多人提到 clock，第一反应只是频率配置。  
但驱动视角下，clock 更常见的意义是：

- 设备寄存器访问是否可用
- 某个内部状态机是否真正开始工作
- DMA/总线/功能模块是否获得时钟源

也就是说，clock 往往是“设备是否活着”的前提，而不仅是“设备跑得多快”。

## reset 控制的是设备是否处于已知初始状态

reset 常被低估，因为很多开发板默认 bootloader 已经帮你把设备放到了某种“看起来可用”的状态。  
但量产环境里，真正稳健的驱动必须明确：

- 什么时候 assert reset
- 什么时候 deassert reset
- reset 后哪些寄存器需要重新配置

否则常见后果是：

- 冷启动正常，异常恢复不正常
- 第一次 probe 正常，第二次 reset 恢复失败
- 某些硬件版本对 reset 顺序更敏感

## 一个更成熟的 bring-up 顺序

很多设备较稳妥的启动顺序通常是：

1. 打开 regulator
2. 使能必要 clock
3. 按要求控制 reset
4. 等待硬件稳定
5. 访问寄存器并初始化

而停止路径通常要做镜像式回收。  
这里的关键不是所有设备都完全相同，而是：

- 要有明确顺序
- 要能失败回滚
- 要能与 runtime PM / suspend-resume 对接

## 与 PM 状态机的关系

clock/reset/regulator 真正难的地方，在于它们并不只出现在 probe/remove。  
它们还会进入：

- runtime suspend/resume
- 系统 suspend/resume
- error recovery
- reset/reinit 流程

所以驱动代码如果把这些资源调用散落各处，很快就会变得不可维护。  
高级工程师更倾向于把它们封装成：

- power_on()
- power_off()
- hw_init()
- hw_deinit()

之类的清晰边界。

## 常见反模式

1. probe 里硬编码一串 enable 调用，失败路径不对称
2. reset 和寄存器初始化交错混乱
3. runtime PM 和系统 PM 分别各写一套不同的资源顺序
4. 依赖“某个平台默认已经把时钟开好”

## 一句经验总结

clock、reset、regulator 不是零散资源，而是设备上电与恢复状态机的基础。  
真正高级的驱动设计，会把它们封装成对称、可回滚、可复用的资源序列，而不是把 enable/disable 调用散在各个角落。
