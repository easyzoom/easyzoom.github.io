---
title: Linux基础
author: EASYZOOM
date: 2026/04/20 13:00
categories:
 - Linux基础
tags:
 - Linux
 - Shell
 - 基础
---

# Linux基础

## 前言

**C：** 本册面向所有以 Linux 为主要开发/运维环境的工程师，收录日常命令速查、shell 小技巧、常见环境配置，以及一些"只有踩过坑才会记得"的小知识。偏**手边备忘**，而非系统教程；偏底层体系化内容请看同一分组下的「Linux驱动开发」「Linux内核开发」。

<!-- more -->

## 本册内容范围

计划围绕下列方向展开（随写作进度持续调整）：

- 日常命令速查：文件、进程、网络、权限、打包与压缩
- 环境配置：代理、yum/apt、systemd 单元、语言/工具链
- 排障手段：`ss` / `lsof` / `strace` / `dmesg` 等工具的常见用法
- Shell 脚本惯用法：管道、子 shell、错误处理、异步
- 发行版差异：Ubuntu / CentOS / Debian / Alpine 的常见坑

## 学习建议

- 发行版差异较大，建议以本机 `cat /etc/os-release` 与 `uname -a` 的输出为准。
- 涉及系统级改动（网络、`systemd`、`iptables`）前先做 snapshot 或备份，避免把开发机搞炸。
- 对底层"为什么"感兴趣时，记得顺手翻一下 `man` 与 `Documentation/`，比复制粘贴命令更划算。

::: tip 持续更新中

章节与示例会陆续补充；若你发现疏漏或与当前版本不符之处，欢迎评论交流。

:::
