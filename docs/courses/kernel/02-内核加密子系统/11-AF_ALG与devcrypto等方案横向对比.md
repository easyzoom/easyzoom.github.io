---
title: AF_ALG 与 devcrypto 等方案横向对比
author: EASYZOOM
date: 2026/04/21 12:00
categories:
 - Linux内核开发
tags:
 - Linux
 - 内核
 - 加密
---

# `AF_ALG` 与 `/dev/crypto`、OpenSSL、`libkcapi` 的关系对比

> 目标:把 Linux 里最容易混的 4 个名字一次讲清楚:
>
> - `AF_ALG`
> - `/dev/crypto`
> - OpenSSL
> - `libkcapi`
>
> 重点回答:
>
> 1. 它们分别处在什么层?
> 2. 谁是内核接口,谁是用户态库,谁只是封装?
> 3. 在 x2600 当前这套环境里,它们最终会不会走到硬件?
>
> 配套阅读:

---

## 0. 一句话结论

先给最短答案:

- **`AF_ALG`**:Linux 主线内核自带的 **用户态内核加密接口**
- **`/dev/crypto`**:通常指 **另一套字符设备风格的加密接口**，在 Linux 世界里很多时候来自 `cryptodev-linux`，**不是主线标准接口**
- **OpenSSL**:用户态密码库，本身有自己的软件/汇编/CPU 指令实现，**默认不等于 `AF_ALG`**
- **`libkcapi`**:用户态库，**本质上就是 `AF_ALG` 的封装器**

如果再压缩成一句:

> `AF_ALG` 是内核接口，`libkcapi` 是它的用户态包装，OpenSSL 是独立的密码库，而 `/dev/crypto` 往往是另一条并行接口，不是 `AF_ALG` 的别名。

---

## 1. 先分清“层级”

可以先把这四个对象放到一张层级图里:

```text
用户应用
  |
  +-- 直接调用 OpenSSL EVP_* API
  |
  +-- 直接调用 AF_ALG socket API
  |      socket/bind/accept/sendmsg/recvmsg
  |
  +-- 调 libkcapi
  |      (内部仍然走 AF_ALG)
  |
  +-- 调 /dev/crypto ioctl/read/write
         (如果系统提供了这套设备接口)

-------------------------------------------

内核接口层
  |
  +-- AF_ALG  (PF_ALG + algif_*)
  |
  +-- /dev/crypto (常见是 cryptodev-linux 或平台专用节点)

-------------------------------------------

内核 Crypto API / 具体实现
  |
  +-- crypto_alloc_* / crypto_* request
  |
  +-- 软件实现: aes-generic / sha256-generic / drbg_* / ...
  |
  +-- 硬件实现: drivers/crypto/* (如果存在)
```

从这张图里先抓住两个事实:

1. **OpenSSL 和 `libkcapi` 都是用户态库**
2. **`AF_ALG` 和 `/dev/crypto` 才是内核导出的接口**

---

## 2. `AF_ALG` 是什么

`AF_ALG` 是 Linux 主线内核自带的 **socket 风格**加密接口。

它的基本使用方式是:

1. `socket(AF_ALG, SOCK_SEQPACKET, 0)`
2. `bind({ .salg_type = "...", .salg_name = "..." })`
3. `accept()`
4. `sendmsg()/recvmsg()` 或 `read()/write()`

支持的主要类型:

- `hash`
- `skcipher`
- `aead`
- `rng`

也就是你前面已经梳理过的:

- `af_alg.c`
- `algif_hash.c`
- `algif_skcipher.c`
- `algif_aead.c`
- `algif_rng.c`

### 它的特点

- 属于 **Linux 主线内核**的一部分
- 统一挂在内核 Crypto API 上
- 调用者不需要关心最终是软件还是硬件
- 接口风格偏低层,用起来像“特殊 socket”

### 它最适合谁

- 想直接调用内核 Crypto API 的用户态程序
- 想复用内核已有算法注册表(`/proc/crypto`)与优先级选择逻辑
- 想避免自己维护大量算法实现的用户态工具

---

## 3. `libkcapi` 是什么

`libkcapi` 是一个 **用户态库**，专门把 `AF_ALG` 封装成更正常的 C API。

上游文档 `Documentation/crypto/userspace-if.rst` 也直接说了:

> `libkcapi` 是 `AF_ALG` 用户态接口的一个工作示例和封装库。

换句话说:

> `libkcapi` 和 `AF_ALG` 的关系，类似 `libcurl` 和 `socket` 的关系。  
> `libkcapi` 不是另一套后端，它的后端仍然是 `AF_ALG`。

### 它解决什么问题

直接用 `AF_ALG` 原始 socket API 写代码会比较烦:

- 要自己写 `struct sockaddr_alg`
- 要自己拼 `cmsg`
- 要管理 `accept()` 返回的 op socket
- 要处理 AEAD 的 AAD/tag/IV 布局

`libkcapi` 把这些都封装掉了,对用户暴露的是更接近“普通函数调用”的 API。

### 它最适合谁

- 想用内核 Crypto API,但不想手写 `AF_ALG` 协议细节
- 写测试工具、benchmark、命令行工具
- 做 Linux-only 用户态加密调用

### 在你这套 SDK 里的迹象

Buildroot 里明确有这个包:

```text
buildroot/package/Config.in
  source "package/libkcapi/Config.in"
```

所以这套系统从构建层面就是认识 `libkcapi` 的。

---

## 4. OpenSSL 是什么

OpenSSL 首先是一个 **用户态密码库**。

它的主身份是:

- 提供 `libcrypto`
- 提供 `EVP_*`、`AES_*`、`SHA*`、`HMAC` 等 API
- 提供 TLS/PKI 相关功能

### 4.1 默认情况下,OpenSSL 并不等于 `AF_ALG`

这是最容易误解的点。

默认情况下,OpenSSL 做密码运算通常优先使用:

1. 自己的软件实现
2. 自己的汇编优化
3. CPU 指令加速(AES-NI、ARMv8 CE、SHA extensions 等)

也就是说:

> **OpenSSL 默认是一套独立的用户态密码实现栈**，不是“Linux 内核 Crypto API 的前端”。

### 4.2 OpenSSL 可以“对接”内核接口,但那是可选集成

OpenSSL 在某些版本/配置里可以通过 engine/provider 风格机制去接:

- `AF_ALG`
- `/dev/crypto`
- 特定硬件加速后端

但这属于 **可选 backend**，不是 OpenSSL 的默认定义。

所以正确说法应该是:

- “OpenSSL **可以配置成** 使用 `AF_ALG` 或 `/dev/crypto`”
- 而不是 “OpenSSL **就是** `AF_ALG`” 或 “OpenSSL 天然走内核加密”

### 4.3 在嵌入式里常见的现实情况

很多嵌入式系统里:

- OpenSSL 仍用自己的软件实现
- 某些发行版/产品再额外挂一个 engine
- 或者直接完全不让 OpenSSL 走内核接口

原因包括:

- 用户态实现更成熟
- 算法覆盖更全
- TLS 栈整合更自然
- 性能不一定比内核接口差

特别是在像 x2600 这种**没有启用硬件 Crypto 驱动**的场景下:

> OpenSSL 直接用自己的软件 AES/SHA,未必比绕一圈 `AF_ALG -> aes-generic` 更差,甚至常常更快。

---

## 5. `/dev/crypto` 到底是什么

这是最容易被说乱的一个词。

### 5.1 大家平时说的 `/dev/crypto`

在 Linux 语境里,很多人说 `/dev/crypto`,实际指的是:

> **`cryptodev-linux` 这一类字符设备接口**

它通常表现为:

- 一个字符设备节点(`/dev/crypto`)
- 用户态通过 `open/ioctl/read/write/mmap` 访问
- 接口风格更像传统 device driver,不是 socket

这套接口在 Linux 里很常见,但**它不是 Linux 主线内核标准用户态加密接口**。  
很多系统是额外打补丁或通过外部模块提供它。

你这套 Buildroot 里也能看到它作为单独包存在:

```text
buildroot/package/Config.in
  source "package/cryptodev-linux/Config.in"
```

这本身就说明:

> 在这套系统里,`cryptodev-linux` 被当成“可选外部组件”,而不是主线内核自带基础设施。

### 5.2 `/dev/crypto` 也可能指某个专用设备节点

这个名字还有第二层容易混淆的地方:

你这棵树里其实也能看到一种 **专用设备节点风格** 的 `/dev/crypto/...`,例如:

- `/dev/crypto/nx-gzip`

那是 powerpc VAS/NX gzip 的专用接口,不是通用 `cryptodev-linux` API。

所以更准确地说:

> `/dev/crypto` 这个词本身并不总指同一套 ABI。  
> 有时指通用 `cryptodev-linux`，有时只是某个平台把专用硬件节点挂在 `/dev/crypto/*` 名字空间下。

### 5.3 它和 `AF_ALG` 的本质区别

| 维度 | `AF_ALG` | `/dev/crypto`(常指 cryptodev-linux) |
|---|---|---|
| 形式 | socket 接口 | 字符设备/ioctl 接口 |
| 主线内核地位 | 主线自带 | 通常不是主线标准 ABI |
| 绑定内核 Crypto API | 强绑定 | 可能绑定,也可能是另一套驱动层包装 |
| 调用风格 | `socket/bind/accept/sendmsg/recvmsg` | `open/ioctl/read/write` |
| Linux 特有性 | 很强 | 也很强 |

所以:

> `/dev/crypto` 不是 `AF_ALG` 的别名，它是另一种设计路线。

---

## 6. 四者关系放到一张表里

| 名字 | 它是什么 | 位于哪层 | 默认是否直接等于内核 Crypto API | 主要风格 |
|---|---|---|---|---|
| `AF_ALG` | Linux 主线用户态加密接口 | 内核接口 | 是 | socket |
| `libkcapi` | `AF_ALG` 的用户态封装库 | 用户态库 | 通过 `AF_ALG` 间接等于 | 函数库 |
| OpenSSL | 通用用户态密码库 | 用户态库 | 否 | 函数库 |
| `/dev/crypto` | 另一类设备接口(常见是 cryptodev-linux) | 内核接口 | 不一定 | char device/ioctl |

---

## 7. 谁会最终用到内核 Crypto API

### 7.1 一定会

- `AF_ALG`
- `libkcapi`

因为 `libkcapi` 本来就是 `AF_ALG` 封装。

### 7.2 不一定会

- OpenSSL
- `/dev/crypto`

原因不同:

- **OpenSSL**:默认通常走自己的实现,只有配置/集成后才可能转给内核接口
- **`/dev/crypto`**:取决于它背后那套驱动/模块的实现,未必和主线 Crypto API 完全同构

---

## 8. 在 x2600 当前环境里该怎么理解

结合你前面已经确认过的事实:

- `/proc/crypto` 里主要是:
  - `aes-generic`
  - `sha256-generic`
  - `ghash-generic`
  - `drbg_*`
  - `jitterentropy_rng`
- 没看到 Ingenic 硬件 crypto driver

那么四者在你这里的现实含义大致是:

### 8.1 `AF_ALG`

如果你直接用 `AF_ALG`:

- 最终会走内核 Crypto API
- 而当前大概率落到 `*-generic` 软件实现
- 也就是 CPU 算

### 8.2 `libkcapi`

如果你用 `libkcapi`:

- 本质上还是 `AF_ALG`
- 所以结果和上面一样
- 只是开发体验更好

### 8.3 OpenSSL

如果你直接用 OpenSSL:

- 默认更可能走 OpenSSL 自己的实现
- 不一定调用内核 Crypto API
- 不一定和 `/proc/crypto` 里的优先级选择一致

### 8.4 `/dev/crypto`

如果你在系统里额外启用了 `cryptodev-linux` 或某个平台专用 `/dev/crypto/*`:

- 它是另一条并行路径
- 不应自动假设它和 `AF_ALG` 结果完全一样
- 也不应自动假设它一定比 OpenSSL 快

一句话:

> 在你当前 x2600 这套环境里,`AF_ALG/libkcapi` 和 OpenSSL 很可能最终都跑在 CPU 上,只是调用路径不同。  
> 真正决定“会不会上硬件”的,仍然是系统里有没有注册对应硬件 Crypto driver。

---

## 9. 什么时候选哪个

### 9.1 选 `AF_ALG`

适合:

- 想直接测试/使用内核 Crypto API
- 想严格跟随 `/proc/crypto` 当前注册表和优先级
- 做 Linux-only 的系统工具

不太适合:

- 追求跨平台
- 想要更高层、更稳定的用户态库体验

### 9.2 选 `libkcapi`

适合:

- 你明确想走内核 Crypto API
- 但不想手写 `socket/bind/accept/cmsg`
- 想要 `AF_ALG` 的正确封装和现成工具

通常是:

> **“想用 `AF_ALG` 时的首选工程化接口”**

### 9.3 选 OpenSSL

适合:

- 跨平台应用
- TLS/证书/PKI/通用密码学需求
- 不想绑死 Linux 内核接口
- 想利用 OpenSSL 自己成熟的算法实现和生态

通常是:

> **应用层首选**

### 9.4 选 `/dev/crypto`

适合:

- 你所在系统明确已经选定了这套 ABI
- 驱动/生态围绕它建设
- 某些特定硬件或遗留方案只能通过它访问

不太适合:

- 你想写可移植、主线友好的 Linux-only 程序

---

## 10. 最常见的误解

### 误解 1: OpenSSL 就是 AF_ALG

错。  
OpenSSL 是用户态库，`AF_ALG` 是内核 socket 接口。

### 误解 2: `libkcapi` 是另一套后端

错。  
`libkcapi` 只是 `AF_ALG` 的封装。

### 误解 3: `/dev/crypto` 是 Linux 标准接口

很多时候不对。  
它在 Linux 世界很常见，但常常不是主线标准 ABI，而是额外模块/补丁/专用设备接口。

### 误解 4: 只要用了内核接口就一定更快

不对。  
如果系统没有硬件 Crypto 驱动，那么:

- `AF_ALG` 可能最终走 `aes-generic`
- OpenSSL 可能走自己的汇编/CPU 指令优化

这时性能未必是内核接口更占优。

---

## 11. 一个最实用的记忆法

如果只记一句:

> **`AF_ALG` 是“Linux 内核加密 socket 接口”，`libkcapi` 是它的胶水层，OpenSSL 是独立密码库，`/dev/crypto` 是另一套设备风格接口。**

如果再记一句:

> **在没有硬件驱动时，它们很可能最终都只是在不同位置调用 CPU 做软件加密。**

---

## TL;DR

- `AF_ALG`：Linux 主线内核自带的用户态加密接口，socket 风格
- `libkcapi`：`AF_ALG` 的用户态封装库
- OpenSSL：独立用户态密码库，默认不等于 `AF_ALG`
- `/dev/crypto`：另一类字符设备风格接口，Linux 里常见但通常不是主线标准 ABI
- 在当前 x2600 环境里，`AF_ALG/libkcapi` 大概率都会落到 `*-generic` 软件实现；OpenSSL 默认也更可能走自己的软件路径
