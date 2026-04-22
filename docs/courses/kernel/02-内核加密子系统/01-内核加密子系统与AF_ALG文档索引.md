---
title: 内核加密子系统与 AF_ALG 文档索引
author: EASYZOOM
date: 2026/04/21 12:00
categories:
 - Linux内核开发
tags:
 - Linux
 - 内核
 - 加密
---

# `AF_ALG` / Crypto API 文档索引：按阅读顺序编号

> 这组文档的目标，不是把 Linux Crypto API 所有代码一次讲完，而是帮你从
> `AF_ALG` 的整体骨架出发，逐步走到 `skcipher / aead / hash / rng` 四类前端，
> 再落到用户态示例、性能对比、`/proc/crypto` 和具体调用链。
>
> 当前目录的文件名也已经按阅读顺序重排:本文是 `01-内核加密子系统与AF_ALG文档索引.md`，
> 后续文档顺延为 `02-13`；下面列表里的序号表示阅读步骤，不表示重要性排序。

---

## 目录

1. [这组文档解决什么问题](#1-这组文档解决什么问题)
2. [推荐阅读顺序](#2-推荐阅读顺序)
3. [按主题分组的文档清单](#3-按主题分组的文档清单)
4. [不同目标下怎么跳读](#4-不同目标下怎么跳读)
5. [建议先读哪几篇](#5-建议先读哪几篇)

---

## 1. 这组文档解决什么问题

很多人第一次看 `AF_ALG` / Linux Crypto API 时，会同时卡在 4 个层面：

1. 不知道 `af_alg.c`、`algif_*`、`crypto/api.c`、具体算法实现各自负责什么。
2. 不知道 `skcipher`、`cipher`、`aead`、`hash`、`rng` 这些类型是什么关系。
3. 不知道用户态一次 `bind / setsockopt / sendmsg / recvmsg` 到底落到哪里。
4. 不知道应该怎么验证系统里到底有没有某个算法、某条路径是不是软件实现。

这组文档就是按这个问题链来排顺序的：

- 先看总览，建立整体地图
- 再看 `af_alg.c` 骨架，搞清 socket 这层
- 再分别看四个 `algif_*`
- 再看具体调用链、用户态示例和性能测试

---

## 2. 推荐阅读顺序

### 路线 A：第一次系统看 `AF_ALG`

按下面编号顺序读最顺：

1. `02-algif系列模块总览.md`
2. `03-AF_ALG架构与实现解析.md`
3. `04-algif_skcipher深度走读.md`
4. `05-algif_skcipher到aes_generic调用链路.md`
5. `06-algif_aead深度走读.md`
6. `07-algif_hash深度走读.md`
7. `08-algif_rng深度走读.md`
8. `09-proc-crypto工作原理.md`
9. `10-AF_ALG用户态最小示例集.md`
10. `11-AF_ALG与devcrypto等方案横向对比.md`
11. `12-x2600平台三条路径性能实测.md`
12. `13-proto_register为何在sock.c中.md`

### 路线 B：你现在只关心 `skcipher`

1. `02-algif系列模块总览.md`
2. `03-AF_ALG架构与实现解析.md`
3. `04-algif_skcipher深度走读.md`
4. `05-algif_skcipher到aes_generic调用链路.md`
5. `10-AF_ALG用户态最小示例集.md`
6. `09-proc-crypto工作原理.md`

### 路线 C：你现在只想跑用户态验证 / benchmark

1. `10-AF_ALG用户态最小示例集.md`
2. `11-AF_ALG与devcrypto等方案横向对比.md`
3. `12-x2600平台三条路径性能实测.md`
4. `09-proc-crypto工作原理.md`

---

## 3. 按主题分组的文档清单

### A. 入口 / 地图类

#### `02-algif系列模块总览.md`

把 `algif_aead.c` / `algif_hash.c` / `algif_rng.c` / `algif_skcipher.c` 这 4 个前端文件先放到一张总图里看，适合建立第一层心智模型。

#### `03-AF_ALG架构与实现解析.md`

专讲 `kernel/kernel-5.10/crypto/af_alg.c`，回答 `PF_ALG` 的 socket 骨架、TX/RX SGL、AIO、反压、与 `algif_*` 的分工。

### B. `skcipher` 主线

#### `04-algif_skcipher深度走读.md`

把 `algif_skcipher.c` 的 `sendmsg + recvmsg + af_alg_pull_tsgl + crypto_skcipher_encrypt` 主路径整份掰开。

#### `05-algif_skcipher到aes_generic调用链路.md`

专门回答“`algif_skcipher.c` 是怎么走到 `cbc(aes)`，再怎么落到 `aes_generic.c`”。

### C. 其他三类 `algif_*`

#### `06-algif_aead深度走读.md`

讲 `AEAD` 路径，重点是 `AAD / plaintext / tag` 三者在收发路径里怎么组织。

#### `07-algif_hash深度走读.md`

讲 `hash / MAC` 路径，重点是它为什么和 `skcipher/aead` 的 TX/RX 骨架差异这么大。

#### `08-algif_rng深度走读.md`

讲 `rng / drbg` 路径，重点是为什么它几乎只有 `recvmsg` 主路径，以及 `setkey / setentropy / addtl` 的边界。

### D. 运行时观测 / 算法注册

#### `09-proc-crypto工作原理.md`

回答 `/proc/crypto` 里到底是什么、这些字段从哪里来、是怎么被遍历出来的。

### E. 用户态使用与对比

#### `10-AF_ALG用户态最小示例集.md`

给出 `hash / skcipher / aead / rng` 四类最小用户态示例，适合边看代码边验证。

#### `11-AF_ALG与devcrypto等方案横向对比.md`

把 `AF_ALG`、`/dev/crypto`、OpenSSL、`libkcapi` 四者放到同一层级图里对比。

#### `12-x2600平台三条路径性能实测.md`

讲怎么在 x2600 上把 `AF_ALG`、OpenSSL、`libkcapi` 三条路径做成一组可比较的 benchmark。

### F. 延伸背景

#### `13-proto_register为何在sock.c中.md`

这个不是 `AF_ALG` 算法路径本身，但能帮你把 `proto_register()`、`sock.c`、`AF_ALG` 在 socket 栈里的位置彻底串起来。

---

## 4. 不同目标下怎么跳读

### 目标 A：想知道“用户态一次 `sendmsg/recvmsg` 到底发生了什么”

1. `03-AF_ALG架构与实现解析.md`
2. `04-algif_skcipher深度走读.md`
3. `05-algif_skcipher到aes_generic调用链路.md`

### 目标 B：想知道“系统里为什么只有 `*-generic`，有没有硬件加速”

1. `09-proc-crypto工作原理.md`
2. `05-algif_skcipher到aes_generic调用链路.md`
3. `11-AF_ALG与devcrypto等方案横向对比.md`

### 目标 C：想知道“怎么在板子上最快跑通一个例子”

1. `10-AF_ALG用户态最小示例集.md`
2. `09-proc-crypto工作原理.md`

### 目标 D：想知道“怎么做一组像样的性能对比”

1. `11-AF_ALG与devcrypto等方案横向对比.md`
2. `12-x2600平台三条路径性能实测.md`

---

## 5. 建议先读哪几篇

如果你现在只想先抓主线，我建议先读这 4 篇：

1. `02-algif系列模块总览.md`
2. `03-AF_ALG架构与实现解析.md`
3. `04-algif_skcipher深度走读.md`
4. `05-algif_skcipher到aes_generic调用链路.md`

这 4 篇读完之后，你对下面这些问题通常就已经有答案了：

- `AF_ALG` 到底是不是算法实现本体
- `algif_*` 和 `af_alg.c` 的分工是什么
- `cbc(aes)` 为什么不是直接跳到 `aes_generic.c`
- 为什么 `sendmsg` 只堆数据，`recvmsg` 才真正触发运算
