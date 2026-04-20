---
title: tldr 让 man 手册薄一点
author: EASYZOOM
date: 2026/04/20 17:00
categories:
 - 工具四海谈
tags:
 - CLI
 - Linux
 - 效率工具
---

# tldr 让 man 手册薄一点

## 前言

**C：** 每次想起 `tar` 的参数组合，我都得 `man tar` 翻上两屏；`find -exec` 的那点固定套路，脑子记不住，手也懒得翻。man 手册一向厚道——凡是能写的都写了——但人在命令行前，往往只想要一句“我该怎么敲”。

于是就有了 [`tldr`](https://tldr.sh/)：它不取代 man，它补的是“我就想抄一个例子”的那块空白。

<!-- more -->

## 工具简介

`tldr` 是一个社区维护的命令行速查手册，用一组精选示例替代长篇大论的 man 页。项目名字本身就来自网络梗 "Too Long; Didn't Read"。

它的核心只有两件事：

- **只给例子，不讲语法糖**：每条命令挑 5～8 个最常用的写法。
- **社区驱动**：示例收录在 [tldr-pages/tldr](https://github.com/tldr-pages/tldr) 仓库里，任何人都能提 PR。

## 安装

笔者日常用的是 Rust 实现版本 `tlrc`，速度和体验都比 Node 版好一些。

::: code-group

```sh [Ubuntu / Debian]
sudo apt install tldr
tldr --update
```

```sh [macOS]
brew install tlrc
```

```sh [通用 (npm)]
npm install -g tldr
```

:::

第一次使用前建议先拉一次离线缓存：

```sh
tldr --update
```

## 使用示例

### 查一个命令怎么用

```sh
$ tldr tar

  tar

  Archiving utility.
  Often combined with a compression method, such as gzip or bzip2.

  - Create an archive from files:
    tar cf target.tar file1 file2 file3

  - Create a gzipped archive:
    tar czf target.tar.gz file1 file2 file3

  - Extract a (compressed) archive into the current directory:
    tar xf source.tar[.gz|.bz2|.xz]

  - List the contents of a tar file:
    tar tvf source.tar
```

瞬间比 `man tar` 友好多了。

### 查一个子命令

有些工具（比如 Git）子命令很多，`tldr` 也支持直接查：

```sh
tldr git rebase
tldr docker run
```

### 离线使用

`tldr` 的所有示例都会缓存到本地（通常在 `~/.cache/tldr/`），断网也能用。只要偶尔 `tldr --update` 刷新一下即可。

## 什么时候用 tldr，什么时候用 man

| 场景 | 推荐 |
| --- | --- |
| 只想要一条能直接粘贴的命令 | `tldr` |
| 需要搞清楚某个参数的确切含义 | `man` |
| 查某个工具的设计哲学、返回码约定 | `man` / `info` |
| 给新人演示“这东西大概怎么用” | `tldr` |

笔者的习惯是：**先 `tldr`，再 `man`**。`tldr` 帮你把问题缩小到“我关心的那一小段”，然后再去 man 里验证参数细节，效率会高不少。

## 后记

**C：** `tldr` 本身不算什么高科技，胜在它把“写命令行时那点尴尬”处理得很轻巧。工具四海谈开张头一篇，就先写这个——毕竟再好的工具，查起来磕磕绊绊，也难变成肌肉记忆。
