---
title: algif 系列模块总览
author: EASYZOOM
date: 2026/04/21 12:00
categories:
 - Linux内核开发
tags:
 - Linux
 - 内核
 - 加密
---

# `algif_aead.c` / `algif_hash.c` / `algif_rng.c` / `algif_skcipher.c` 总览

> 目标:把 `AF_ALG` 体系里这 4 个 `algif_*` 文件一次讲清楚,回答三个问题:
> 1. 它们各自负责哪一类算法?
> 2. 它们和 `af_alg.c`、下层 Crypto API 的分工是什么?
> 3. 它们彼此的结构为什么长得很像,但处理流程又哪里不同?
>
> 配套阅读:

---

## 0. 一句话定位

这 4 个文件本质上都是:

> **`AF_ALG` 的“按算法类别分发的前端适配层”**

它们不是:

- 具体算法实现(不是 `aes_generic.c`、`sha256_generic.c`)
- 硬件驱动(不是 `drivers/crypto/*`)
- `PF_ALG` socket 骨架(那是 `af_alg.c`)

它们做的事情是:

1. 把用户态 `socket(AF_ALG)` 请求里 `salg_type` 这一层分类接住;
2. 把这一类 socket 操作翻译成对应的内核 Crypto API 请求;
3. 把结果再经由 `AF_ALG` socket 返回给用户态。

---

## 1. 先看整体分层

```text
用户态
  socket(AF_ALG)
  bind({ type = "skcipher"/"hash"/"aead"/"rng", name = "..." })
  setsockopt(KEY / AUTHSIZE / ENTROPY)
  accept()
  sendmsg/recvmsg
      |
      v
af_alg.c
  - PF_ALG family 注册
  - bind/accept/setsockopt 公共骨架
  - TX/RX SGL、poll、AIO 回调等通用逻辑
      |
      v
algif_skcipher.c / algif_hash.c / algif_aead.c / algif_rng.c
  - 按 type 分发
  - bind -> crypto_alloc_*
  - setkey/setauthsize/setentropy
  - accept -> 建立该类型自己的 ctx
  - sendmsg/recvmsg -> 组 request 并调用 crypto_* API
      |
      v
Crypto API
  - crypto_alloc_skcipher / ahash / aead / rng
  - crypto_skcipher_encrypt / crypto_ahash_* / crypto_aead_* / crypto_rng_*
      |
      v
具体实现
  - 软件: crypto/*.c
  - 硬件: drivers/crypto/*
```

最关键的一句是:

> `af_alg.c` 负责“socket 骨架”,`algif_*` 负责“把不同算法类别翻译成不同 Crypto API 类型”。

---

## 2. 四个文件分别对应什么类型

四个文件都在末尾注册了一个 `struct af_alg_type`,名字就是用户态 `sockaddr_alg.salg_type` 里要填的字符串:

| 文件 | `af_alg_type.name` | 面向的 Crypto API 类别 | 用户态典型 `salg_name` |
|---|---|---|---|
| `algif_skcipher.c` | `"skcipher"` | `struct crypto_skcipher` | `cbc(aes)`、`ctr(aes)`、`xts(aes)` |
| `algif_hash.c` | `"hash"` | `struct crypto_ahash` | `sha256`、`hmac(sha256)`、`cmac(aes)` |
| `algif_aead.c` | `"aead"` | `struct crypto_aead` | `gcm(aes)`、`ccm(aes)`、`authenc(...)` |
| `algif_rng.c` | `"rng"` | `struct crypto_rng` | `stdrng`、`jitterentropy_rng` |

也就是说,用户态真正传进来的:

```c
sa.salg_type = "skcipher";
sa.salg_name = "cbc(aes)";
```

和:

```c
sa.salg_type = "hash";
sa.salg_name = "sha256";
```

虽然都走 `AF_ALG`,但会在 `af_alg.c::alg_bind()` 里命中不同的 `algif_*` 模块。

---

## 3. 它们共同的结构模板

这 4 个文件虽然代码细节不同,但结构几乎都是一个模子刻出来的:

### 3.1 `*_bind`

负责把用户态给的 `salg_name` 变成对应的 Crypto API tfm:

- `skcipher_bind` -> `crypto_alloc_skcipher(name, type, mask)`
- `hash_bind` -> `crypto_alloc_ahash(name, type, mask)`
- `aead_bind` -> `crypto_alloc_aead(name, type, mask)`
- `rng_bind` -> `crypto_alloc_rng(name, type, mask)`

这一步是真正把:

- `cbc(aes)`
- `sha256`
- `gcm(aes)`
- `stdrng`

这些“用户可见算法名”绑定到内核注册表里的实际实现上。

### 3.2 `*_release`

负责 `crypto_free_*()`。

### 3.3 `*_setkey` / `*_setauthsize` / `*_setentropy`

负责把 `setsockopt(SOL_ALG, ...)` 转成具体 Crypto API 调用。

### 3.4 `*_accept_parent`

为 op socket 分配该类别自己的运行时上下文 `ctx`。

### 3.5 `*_ops`

提供该类别的 `sendmsg` / `recvmsg` / `poll` / `release` 行为。

### 3.6 `algif_type_*`

把上述 bind/release/accept/ops 打包成一个 `struct af_alg_type`,注册到 `af_alg.c` 的 `alg_types` 链表。

### 3.7 `module_init(...)`

最终调用 `af_alg_register_type(&algif_type_*)`。

所以从架构上看,它们像是同一个接口规范下的 4 个插件。

---

## 4. 四个文件分别干什么

## 4.1 `algif_skcipher.c`:对称加解密前端

文件开头已经直接说明:

```text
algif_skcipher: User-space interface for skcipher algorithms
This file provides the user-space API for symmetric key ciphers.
```

它面向的是 **同步对称密钥加密** 这一类算法,例如:

- `cbc(aes)`
- `ctr(aes)`
- `xts(aes)`
- `ecb(aes)`

### 它做的事情

1. `bind("cbc(aes)")` 时分配 `struct crypto_skcipher *tfm`
2. `ALG_SET_KEY` 时调用 `crypto_skcipher_setkey`
3. `sendmsg` 时复用 `af_alg_sendmsg()` 把数据堆进 TX SGL
4. `recvmsg` 时:
   - 组 `skcipher_request`
   - 从全局 TX SGL 里 `af_alg_pull_tsgl()` 把本次输入“搬”到 per-request SGL
   - 组 RX SGL
   - 调 `crypto_skcipher_encrypt()` / `crypto_skcipher_decrypt()`

### 它的特点

- 输入长度通常需要满足块/chunksize 约束
- 输出长度通常与输入长度相同
- 典型 use case 是“明文 -> 密文”或“密文 -> 明文”

### 你已经有的配套深挖

这部分已经单独展开过:

那篇重点讲了:

- `af_alg_sendmsg`
- `af_alg_pull_tsgl`
- `skcipher_request_set_crypt`
- `crypto_skcipher_encrypt`

怎么串成一次完整请求。

---

## 4.2 `algif_hash.c`:哈希 / MAC 前端

文件头:

```text
algif_hash: User-space interface for hash algorithms
This file provides the user-space API for hash algorithms.
```

它面向的是:

- 普通哈希:`sha1`、`sha256`、`sha512`
- 带 key 的 MAC:`hmac(sha256)`、`cmac(aes)` 等

### 它做的事情

1. `bind("sha256")` -> `crypto_alloc_ahash("sha256", ...)`
2. 如果是 HMAC/CMAC 这类带 key 的算法,`ALG_SET_KEY` -> `crypto_ahash_setkey()`
3. `sendmsg` 时持续喂数据块
4. `recvmsg` 时输出最终 digest

### 它和 `skcipher` 最大的不同

`skcipher` 的模式是:

> 输入 N 字节,输出 N 字节

`hash` 的模式是:

> 输入任意长度数据流,输出固定长度摘要

所以它的上下文里会专门维护:

- `result` 缓冲
- digestsize
- `ahash_request`

而不是像 `skcipher` 那样更关注 IV、块对齐、in-place/out-of-place 加解密。

### 用户态视角

例如:

```c
type = "hash"
name = "sha256"
```

用户态可能 `sendmsg` 多次送入文件分片,最后一次 `recvmsg` 拿 32 字节摘要。

---

## 4.3 `algif_aead.c`:认证加密前端

文件头:

```text
algif_aead: User-space interface for AEAD algorithms
This file provides the user-space API for AEAD ciphers.
```

它面向的是 **AEAD(Authenticated Encryption with Associated Data)**:

- `gcm(aes)`
- `ccm(aes)`
- `authenc(...)`

### 它做的事情

1. `bind("gcm(aes)")` -> `crypto_alloc_aead("gcm(aes)", ...)`
2. `ALG_SET_KEY` -> `crypto_aead_setkey()`
3. `ALG_SET_AEAD_AUTHSIZE` -> `crypto_aead_setauthsize()`
4. `sendmsg` 时接收:
   - AAD 长度
   - IV
   - op(encrypt/decrypt)
   - 待处理数据
5. `recvmsg` 时组 `aead_request`,最终调用 `crypto_aead_encrypt()` / `crypto_aead_decrypt()`

### 它为什么比 `skcipher` 更复杂

因为 AEAD 比普通对称加解密多了两类额外语义:

1. **AAD**(附加认证数据,不加密但参与认证)
2. **tag/authsize**(认证标签)

所以 `algif_aead.c` 要同时处理:

- AAD 多长
- 输入里哪段是 payload
- 输出里 tag 放哪
- 解密时 tag 校验失败如何报错

因此 4 个 `algif_*` 里,`aead` 一般是最难读的一个。

### 常见理解方式

可以把它看成:

> `skcipher` + `hash/MAC` 的组合前端,但底层不是手工拼两个 request,而是直接走 `crypto_aead` 这一类 API。

---

## 4.4 `algif_rng.c`:随机数生成器前端

文件头:

```text
algif_rng: User-space interface for random number generators
This file provides the user-space API for random number generators.
```

它面向的是:

- `stdrng`
- `jitterentropy_rng`
- 各种 DRBG

### 它做的事情

1. `bind("stdrng")` -> `crypto_alloc_rng("stdrng", ...)`
2. 某些 RNG 允许:
   - `setkey`
   - `setentropy`
3. `recvmsg` 时从 RNG 生成随机字节返回给用户态

### 它和前三个最大的区别

前三个都属于“用户喂数据进去 -> 内核处理 -> 用户取结果”。

`rng` 更像是:

> 用户向内核请求随机数据,由内核 Crypto RNG 子系统产出

所以它通常:

- `sendmsg` 语义弱很多,甚至很多路径不需要
- 更关注 `recvmsg` 产出随机字节
- 不涉及 TX/RX 长度对等这种概念

---

## 5. 四者的差异一张表看清

| 文件 | 主要数据对象 | 典型 request 类型 | 是否需要 KEY | 是否需要 IV | 是否有 authsize/AAD | 输出特征 |
|---|---|---|---|---|---|---|
| `algif_skcipher` | 明文/密文流 | `skcipher_request` | 常见 | 常见 | 无 | 与输入等长 |
| `algif_hash` | 待摘要数据流 | `ahash_request` | 仅 HMAC/CMAC 等需要 | 无 | 无 | 固定 digest 长度 |
| `algif_aead` | AAD + payload + tag | `aead_request` | 常见 | 常见 | 有 | 密文/明文 + tag |
| `algif_rng` | 随机数输出请求 | `crypto_rng` 接口 | 某些 DRBG 需要 | 无 | 可能有 entropy | 纯输出随机字节 |

---

## 6. 它们和 `af_alg.c` 的具体分工

这是最容易混的一点。

### `af_alg.c` 做的

- 注册 `PF_ALG`
- `socket(AF_ALG)` 的创建
- `alg_bind()` 按 `salg_type` 找到对应 `algif_*`
- `alg_setsockopt()` 统一分发 `ALG_SET_KEY` / `ALG_SET_AEAD_AUTHSIZE` / `ALG_SET_DRBG_ENTROPY`
- `alg_accept()` 创建 op socket
- 通用 TX/RX SGL 管理
- poll / wait / AIO 回调

### `algif_*` 做的

- 该 type 的 `bind/release/accept`
- 该 type 的 `sendmsg/recvmsg`
- 把本类别 socket 操作翻译成下层 `crypto_*` 调用

可以把职责边界记成:

> `af_alg.c` 关心“socket 怎么活”,`algif_*` 关心“这一类算法怎么算”。

---

## 7. 从用户态 `bind()` 视角看四者的差别

### 7.1 `skcipher`

```c
sa.salg_type = "skcipher";
sa.salg_name = "cbc(aes)";
```

命中:

- `af_alg.c::alg_bind`
- `algif_skcipher::skcipher_bind`
- `crypto_alloc_skcipher("cbc(aes)")`

### 7.2 `hash`

```c
sa.salg_type = "hash";
sa.salg_name = "sha256";
```

命中:

- `algif_hash::hash_bind`
- `crypto_alloc_ahash("sha256")`

### 7.3 `aead`

```c
sa.salg_type = "aead";
sa.salg_name = "gcm(aes)";
```

命中:

- `algif_aead::aead_bind`
- `crypto_alloc_aead("gcm(aes)")`

### 7.4 `rng`

```c
sa.salg_type = "rng";
sa.salg_name = "stdrng";
```

命中:

- `algif_rng::rng_bind`
- `crypto_alloc_rng("stdrng")`

所以 `salg_type` 实际上就是:

> 在 `AF_ALG` 世界里选择“要走哪一个 `algif_*` 插件”。

---

## 8. 为什么它们不放到 `drivers/crypto/`

因为它们是:

- 面向 **用户态 socket 接口** 的
- 面向 **Crypto API 类别抽象** 的
- 与任何具体硬件无关

无论下层最后选到的是:

- `aes-generic`
- `gcm(aes-generic)`
- 某个硬件 AES/GCM 驱动

这 4 个 `algif_*` 都不需要改。

换句话说:

> `algif_*` 属于 `crypto/` 目录下的“上层框架/接口适配层”,不是硬件驱动层。

---

## 9. 对 x2600 当前这套系统意味着什么

结合你前面已经确认过的事实:

- `/proc/crypto` 里主要是 `aes-generic`、`sha256-generic`、`ghash-generic`
- 没看到 Ingenic 硬件 crypto driver

那么这 4 个 `algif_*` 在你当前系统上的行为就是:

1. 用户态通过 `AF_ALG` 调用 `skcipher/hash/aead/rng`
2. `algif_*` 组装对应的 `crypto_*` request
3. 下层最终落到 **软件实现**
4. 实际运算由 CPU 完成

也就是说,`algif_*` 自身不决定软硬件。  
它们只是“前端翻译器”,真正决定软/硬件的是 `crypto_alloc_*()` 最终拿到的那个 tfm 对应的 driver。

---

## 10. 和 `/proc/crypto`、`AF_ALG`、`algif_*` 三者关系

这三层经常被混在一起,可以用一句话串起来:

1. `/proc/crypto`
   - 告诉你系统当前注册了哪些算法实现
2. `AF_ALG`
   - 提供用户态 socket 入口
3. `algif_*`
   - 把用户态按 type 分类的请求,转成下层 Crypto API 请求

顺序就是:

```text
/proc/crypto 里的算法实现
        ^
        |   被 crypto_alloc_*() 查找
        |
algif_*  <-- af_alg.c <-- 用户态 AF_ALG socket
```

---

## 11. 一个最实用的记忆法

如果你以后再打开这四个文件,可以直接套这个口诀:

- `algif_skcipher` = **加密器**
- `algif_hash` = **摘要器**
- `algif_aead` = **加密+认证器**
- `algif_rng` = **随机数出口**

再加一条:

> `af_alg.c` = **总路由器**

这样整套 `AF_ALG` 结构就很好记。

---

## 12. TL;DR

- 这四个文件都是 `AF_ALG` 的类别适配层,不是具体算法实现
- `algif_skcipher` 对应对称加解密
- `algif_hash` 对应哈希 / HMAC / CMAC
- `algif_aead` 对应认证加密(AEAD)
- `algif_rng` 对应随机数生成器
- `af_alg.c` 管通用 socket 骨架,`algif_*` 管按类别翻译到 `crypto_*` API
- 在当前 x2600 系统上,它们最终都会把请求转交给 `*-generic` 的软件实现
