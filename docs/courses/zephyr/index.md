---
title: ZEPHYR快速入门
author: EASYZOOM
date: 2026/04/07 12:00
categories:
 - ZEPHYR快速入门
tags:
 - Zephyr
 - RTOS
 - 嵌入式
---

# ZEPHYR快速入门

## 前言

**C：** 本系列介绍 Zephyr RTOS：工程化构建、Kconfig/DeviceTree、驱动模型与网络栈入门，适合在资源受限设备上需要可裁剪、可测试系统的场景。

<!-- more -->

## 本册内容范围

计划围绕下列方向展开（随写作进度持续调整）：

- `west` 工作流与板级支持包（BSP）
- Kconfig 与 `prj.conf` 配置习惯
- Devicetree 与硬件描述
- 线程、同步原语与电源管理入门
- 与蓝牙、网络等子系统的衔接（视平台）

## 学习建议

- Zephyr 版本升级快，示例会尽量注明 **LTS 或具体版本号**。
- 官方文档与 `samples/` 目录是最权威的第一手资料，本册作为路径与笔记补充。

::: tip 持续更新中

章节与示例会陆续补充；若你发现疏漏或与当前 Zephyr 版本不符之处，欢迎评论交流。

:::
