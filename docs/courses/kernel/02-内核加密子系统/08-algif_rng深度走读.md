---
title: algif_rng 深度走读
author: EASYZOOM
date: 2026/04/21 12:00
categories:
 - Linux内核开发
tags:
 - Linux
 - 内核
 - 加密
---

# `algif_rng.c` 深度走读:RNG/DRBG 在 `AF_ALG` 里是怎么导出给用户态的

> 目标:把 `kernel/kernel-5.10/crypto/algif_rng.c` 单独讲透,重点回答:
>
> 1. 它为什么和 `algif_skcipher` / `algif_aead` / `algif_hash` 三类都不一样?
> 2. 普通 `rng` 路径里为什么几乎没有 `sendmsg`,只有 `recvmsg`?
> 3. `setkey`、`setentropy`、`additional input` 分别对应 RNG 语义里的什么动作?
> 4. 为什么这份代码里会有一套专门的 `test_ops`?
>
> 配套阅读:

---

## 0. 一句话定位

`algif_rng.c` 是 `AF_ALG` 体系里负责 **RNG / DRBG** 的前端适配层。

它把用户态 `type = "rng"` 的 socket 请求翻译成:

- `crypto_alloc_rng()`
- `crypto_rng_reset()`
- `crypto_rng_generate()`

和前面三类相比,它的最大不同是:

> **它不是“把用户送进来的数据做变换”,而是“基于内核里已有的 RNG 状态,直接向用户态生成随机字节”。**

所以它的核心路径不是:

- `sendmsg -> 收数据`
- `recvmsg -> 处理数据`

而是:

- `setkey/setentropy -> 影响 RNG 状态`
- `recvmsg -> 真正产出随机字节`

---

## 1. 先看整体分层

```text
用户态:
  socket(AF_ALG)
  bind({ type = "rng", name = "stdrng" / "jitterentropy_rng" / ... })
  [可选] setsockopt(ALG_SET_KEY, seed)
  [可选] setsockopt(ALG_SET_DRBG_ENTROPY, entropy)   // CAVP test only
  accept()
  recvmsg(random_bytes)
  [测试模式下可选] sendmsg(additional_input)
      |
      v
af_alg.c
  - 公共 bind/setsockopt/accept 骨架
      |
      v
algif_rng.c
  - bind -> crypto_alloc_rng()
  - setkey -> crypto_rng_reset()
  - setentropy -> 算法特定 set_ent()
  - recvmsg -> crypto_rng_generate()
      |
      v
下层 RNG 实现
  - jitterentropy_rng
  - drbg_*
  - krng
  - 其他硬件/软件 RNG
```

最关键的一句话:

> `algif_rng.c` 不是“前端收输入、后端算输出”的批处理器,而是“socket 外壳包了一层 RNG 取样接口”。

---

## 2. 它和前三类最本质的区别

放在一起看最容易明白:

| 类别 | 用户主要送进去什么 | 内核主要返回什么 | 核心 request 模型 |
|---|---|---|---|
| `skcipher` | 明文/密文 | 明文/密文 | `skcipher_request` |
| `aead` | AAD + payload + tag | payload + tag / 明文 | `aead_request` |
| `hash` | 待摘要数据 | digest | `ahash_request` |
| `rng` | 种子/熵/测试附加输入(可选) | 随机字节 | `crypto_rng` handle |

所以 `algif_rng.c` 里你会明显看到:

- **没有** `af_alg_sendmsg()` 的全局 TX SGL 玩法
- **没有** `af_alg_get_rsgl()` / `af_alg_pull_tsgl()`
- **没有** `struct af_alg_ctx` 里的 `iv/aead_assoclen/used/more` 那套逻辑
- **没有** `skcipher_request` / `aead_request` / `ahash_request`

取而代之的是非常简单直接的模型:

> `recvmsg(len)` -> 调一次 `crypto_rng_generate(..., len)`

---

## 3. 这份文件自己的上下文

文件里有两个私有结构:

```c
/* kernel/kernel-5.10/crypto/algif_rng.c:L53-64 */
struct rng_ctx {
#define MAXSIZE 128
	unsigned int len;
	struct crypto_rng *drng;
	u8 *addtl;
	size_t addtl_len;
};

struct rng_parent_ctx {
	struct crypto_rng *drng;
	u8 *entropy;
};
```

这两个结构正好对应 `AF_ALG` 里一贯的“parent socket / child(op) socket”分工。

### 3.1 `rng_parent_ctx`

这是 control socket 上挂的 `private`,主要保存:

- `drng`
- `entropy`

也就是:

- 真正的 RNG handle
- 测试模式下人工注入的 entropy 缓冲

### 3.2 `rng_ctx`

这是 op socket 上挂的 `private`,主要保存:

- `drng`
- `addtl`
- `addtl_len`

也就是:

- 当前 child socket 关联到的 RNG handle
- 测试模式下一次性使用的 additional input

这里有个很重要的事实:

> **parent 和 child 都指向同一个 `struct crypto_rng *drng`。**

这点后面会单独解释,因为它和 `skcipher/hash/aead` 的直觉不太一样。

---

## 4. `bind/release/accept/setkey/setentropy` 五件套

## 4.1 `rng_bind()`

```c
/* kernel/kernel-5.10/crypto/algif_rng.c:L202-219 */
static void *rng_bind(const char *name, u32 type, u32 mask)
{
	struct rng_parent_ctx *pctx;
	struct crypto_rng *rng;

	pctx = kzalloc(sizeof(*pctx), GFP_KERNEL);
	...
	rng = crypto_alloc_rng(name, type, mask);
	...
	pctx->drng = rng;
	return pctx;
}
```

它把:

- `stdrng`
- `jitterentropy_rng`
- 其他 RNG/DRBG 名称

绑定成真正的 `struct crypto_rng *`。

### 为什么这里叫 `drng`

变量名用了 `drng`,更多是作者习惯上的泛称,不代表它一定是 NIST DRBG。  
对 `jitterentropy_rng`、`krng` 这类也一样照用。

---

## 4.2 `rng_release()`

```c
/* kernel/kernel-5.10/crypto/algif_rng.c:L221-229 */
static void rng_release(void *private)
{
	struct rng_parent_ctx *pctx = private;
	...
	crypto_free_rng(pctx->drng);
	kfree_sensitive(pctx->entropy);
	kfree_sensitive(pctx);
}
```

这里有两个细节:

1. `entropy` 用 `kfree_sensitive()`
2. `pctx` 也用 `kfree_sensitive()`

说明作者明确把测试注入熵也视为敏感数据处理。

---

## 4.3 `rng_accept_parent()`

```c
/* kernel/kernel-5.10/crypto/algif_rng.c:L242-275 */
static int rng_accept_parent(void *private, struct sock *sk)
{
	struct rng_ctx *ctx;
	struct rng_parent_ctx *pctx = private;
	...
	ctx = sock_kmalloc(sk, len, GFP_KERNEL);
	...
	ctx->addtl = NULL;
	ctx->addtl_len = 0;
	...
	ctx->drng = pctx->drng;
	ask->private = ctx;
	sk->sk_destruct = rng_sock_destruct;
	...
	if (IS_ENABLED(CONFIG_CRYPTO_USER_API_RNG_CAVP) && pctx->entropy)
		sk->sk_socket->ops = &algif_rng_test_ops;
	return 0;
}
```

这一段的注释非常重要:

```c
/* kernel/kernel-5.10/crypto/algif_rng.c:L257-260 */
	/*
	 * No seeding done at that point -- if multiple accepts are
	 * done on one RNG instance, each resulting FD points to the same
	 * state of the RNG.
	 */
```

也就是说:

> **多个 accept 出来的 child socket 共享同一个 RNG 状态。**

这和你可能直觉想到的“每个 fd 一份独立状态”不同。

举个例子:

```text
control bind("stdrng")
accept -> fd1
accept -> fd2

fd1 recvmsg(32)   // 消耗 RNG 状态
fd2 recvmsg(32)   // 从“已经前进后的状态”继续取
```

所以这里的多个 child 并不是“克隆同一初始状态”,而是“共享同一个生成器实例”。

---

## 4.4 `rng_setkey()`:这里的 key 实际上是 seed

```c
/* kernel/kernel-5.10/crypto/algif_rng.c:L277-285 */
static int rng_setkey(void *private, const u8 *seed, unsigned int seedlen)
{
	struct rng_parent_ctx *pctx = private;
	...
	return crypto_rng_reset(pctx->drng, seed, seedlen);
}
```

虽然 `AF_ALG` 统一用的是 `ALG_SET_KEY`,  
但在 RNG 语义里它更准确的含义是:

> **reseed / reinitialize / reset RNG state**

底层调用的也不是“setkey”,而是:

- `crypto_rng_reset()`

在 `rng.h` 里它的定义就是:

```c
/* kernel/kernel-5.10/include/crypto/rng.h:L167-184 */
/**
 * crypto_rng_reset() - re-initialize the RNG
 * @tfm: cipher handle
 * @seed: seed input data
 * ...
 */
int crypto_rng_reset(struct crypto_rng *tfm, const u8 *seed,
		     unsigned int slen);
```

所以这里一定要从“RNG 复位/灌种子”去理解,不要机械套用“密钥”这个词。

---

## 4.5 `rng_setentropy()`:只服务 CAVP 测试

```c
/* kernel/kernel-5.10/crypto/algif_rng.c:L287-315 */
static int __maybe_unused rng_setentropy(void *private, sockptr_t entropy,
					 unsigned int len)
{
	...
	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	...
	if (len > MAXSIZE)
		return -EMSGSIZE;
	...
	crypto_rng_alg(pctx->drng)->set_ent(pctx->drng, kentropy, len);
	...
	pctx->entropy = kentropy;
	return 0;
}
```

这个接口不是普通业务路径要用的,而是典型的:

> **CAVP / NIST 验证测试接口**

几个信号很明显:

- 只在 `CONFIG_CRYPTO_USER_API_RNG_CAVP` 下编译
- 需要 `CAP_SYS_ADMIN`
- 直接调底层算法的 `set_ent`
- 还会把后续 child socket 的 `ops` 切换到 `algif_rng_test_ops`

所以可以把它理解为:

> 一条“让用户态测试框架精确控制 RNG 熵输入”的特权后门

而不是普通应用 API。

---

## 5. 普通路径为什么几乎只有 `recvmsg`

先看普通 `ops`:

```c
/* kernel/kernel-5.10/crypto/algif_rng.c:L164-181 */
static struct proto_ops algif_rng_ops = {
	.family		=	PF_ALG,
	...
	.bind		=	sock_no_bind,
	.accept		=	sock_no_accept,
	.sendmsg	=	sock_no_sendmsg,
	.sendpage	=	sock_no_sendpage,
	.release	=	af_alg_release,
	.recvmsg	=	rng_recvmsg,
};
```

这里已经写得很绝对了:

- 正常 `rng` child socket **不能 `sendmsg`**
- 正常 `rng` child socket **只能 `recvmsg`**

这和前几类完全不同。

为什么?

因为普通 RNG 语义下,op socket 的核心动作只有一件事:

> “给我生成 N 个随机字节”

这件事天然对应 `recvmsg`,而不是 `sendmsg`。

---

## 6. `_rng_recvmsg()`:整个文件的主路径几乎都在这里

### 6.1 代码本体

```c
/* kernel/kernel-5.10/crypto/algif_rng.c:L73-104 */
static int _rng_recvmsg(struct crypto_rng *drng, struct msghdr *msg, size_t len,
			u8 *addtl, size_t addtl_len)
{
	int err = 0;
	int genlen = 0;
	u8 result[MAXSIZE];

	if (len == 0)
		return 0;
	if (len > MAXSIZE)
		len = MAXSIZE;
	...
	memset(result, 0, len);
	...
	genlen = crypto_rng_generate(drng, addtl, addtl_len, result, len);
	if (genlen < 0)
		return genlen;

	err = memcpy_to_msg(msg, result, len);
	memzero_explicit(result, len);

	return err ? err : len;
}
```

整个普通 RNG 导出路径就是:

1. 用户要求 `len` 字节
2. 最多截成 `MAXSIZE=128`
3. 在栈上临时开 `result[128]`
4. 调 `crypto_rng_generate()`
5. 拷给用户态
6. 把栈上结果清零

### 6.2 为什么有 `MAXSIZE = 128`

这是个很值得注意的小约束:

```c
/* kernel/kernel-5.10/crypto/algif_rng.c:L53-59 */
struct rng_ctx {
#define MAXSIZE 128
	...
};
```

也就是说:

> **单次 `recvmsg()` 最多只会给你 128 字节。**

如果用户请求更大:

```c
recvmsg(..., len = 4096)
```

内核这里只会:

- 截成 128
- 返回 128

所以用户态如果想取大块随机数,必须循环多次 `recvmsg()`。

这和 `/dev/urandom` 一次读任意大块的直觉不一样,是 `AF_ALG rng` 的一个显著特征。

### 6.3 为什么结果放栈上

因为:

- 最大只有 128 字节
- 每次只用一次
- 用完就清零

这比每次 `kmalloc/kfree` 更轻量。

---

## 7. `rng_recvmsg()`:普通路径没有 additional input

```c
/* kernel/kernel-5.10/crypto/algif_rng.c:L107-115 */
static int rng_recvmsg(struct socket *sock, struct msghdr *msg, size_t len,
		       int flags)
{
	...
	return _rng_recvmsg(ctx->drng, msg, len, NULL, 0);
}
```

普通路径非常直接:

- 不带 additional input
- 单纯从 `ctx->drng` 生成随机字节

所以普通 `algif_rng` 可以理解成:

> `recvmsg(len)` == `crypto_rng_generate(drng, NULL, 0, ..., len)`

---

## 8. 为什么还要有 `algif_rng_test_ops`

看测试模式 `ops`:

```c
/* kernel/kernel-5.10/crypto/algif_rng.c:L183-200 */
static struct proto_ops __maybe_unused algif_rng_test_ops = {
	.family		=	PF_ALG,
	...
	.release	=	af_alg_release,
	.recvmsg	=	rng_test_recvmsg,
	.sendmsg	=	rng_test_sendmsg,
};
```

和普通 `ops` 的唯一区别是:

- 允许 `sendmsg`
- `recvmsg` 走的是 test 版本

这就是 CAVP test 模式。

---

## 9. `rng_test_sendmsg()`:它送进去的不是“要处理的数据”,而是 additional input

```c
/* kernel/kernel-5.10/crypto/algif_rng.c:L133-161 */
static int rng_test_sendmsg(struct socket *sock, struct msghdr *msg, size_t len)
{
	...
	if (len > MAXSIZE) {
		err = -EMSGSIZE;
		goto unlock;
	}

	rng_reset_addtl(ctx);
	ctx->addtl = kmalloc(len, GFP_KERNEL);
	...
	err = memcpy_from_msg(ctx->addtl, msg, len);
	...
	ctx->addtl_len = len;
	...
	return err ? err : len;
}
```

它的语义不是:

> “把这段数据拿去做 hash/encrypt”

而是:

> “把这段数据暂存起来,作为下一次 `crypto_rng_generate()` 的 additional input”

也就是 DRBG 世界里的:

- nonce/additional_input/personalization string

这类“参与本次生成但不成为长期状态”的额外参数。

---

## 10. `rng_test_recvmsg()`:生成一次后就把 additional input 清掉

```c
/* kernel/kernel-5.10/crypto/algif_rng.c:L117-130 */
static int rng_test_recvmsg(struct socket *sock, struct msghdr *msg, size_t len,
			    int flags)
{
	...
	lock_sock(sock->sk);
	ret = _rng_recvmsg(ctx->drng, msg, len, ctx->addtl, ctx->addtl_len);
	rng_reset_addtl(ctx);
	release_sock(sock->sk);

	return ret;
}
```

这里的语义特别明确:

1. 当前缓存在 `ctx->addtl` 里的 additional input 只用一次
2. 一次 `recvmsg()` 生成结束后立刻 `rng_reset_addtl()`

所以测试模式的交互节奏大概是:

```text
sendmsg(additional_input_1)
recvmsg(random_1)

sendmsg(additional_input_2)
recvmsg(random_2)
```

不是持续累积,而是“一发一收”。

---

## 11. `setkey`、`setentropy`、`addtl` 三者不要混

这是读 `algif_rng.c` 时最容易绕的地方。

它们分别对应 RNG/DRBG 里的三个不同概念:

| 接口 | 代码入口 | 语义 | 生命周期 |
|---|---|---|---|
| `ALG_SET_KEY` | `rng_setkey -> crypto_rng_reset()` | seed / reseed / reinitialize | 改变 RNG 长期状态 |
| `ALG_SET_DRBG_ENTROPY` | `rng_setentropy -> set_ent()` | 测试用显式熵注入 | 主要给 CAVP |
| `sendmsg()`(仅 test ops) | `rng_test_sendmsg` | additional input | 只影响下一次 generate |

一句话区分:

- `setkey` = **重灌种子**
- `setentropy` = **特权测试注熵**
- `addtl` = **本次生成附加输入**

---

## 12. `rng_reset_addtl()` 为什么这么小却很关键

```c
/* kernel/kernel-5.10/crypto/algif_rng.c:L66-71 */
static void rng_reset_addtl(struct rng_ctx *ctx)
{
	kfree_sensitive(ctx->addtl);
	ctx->addtl = NULL;
	ctx->addtl_len = 0;
}
```

这段函数做了三件重要的事:

1. `kfree_sensitive` 清理 additional input
2. 防止下一次误复用旧参数
3. 让 test 模式的行为变成明确的一次性语义

它在两个地方被调用:

- 每次 test `recvmsg()` 之后
- socket 析构时

所以即使用户态异常退出,之前送进去的 additional input 也不会残留太久。

---

## 13. `rng_sock_destruct()`:child 只清自己的附加输入,不销毁 RNG 本体

```c
/* kernel/kernel-5.10/crypto/algif_rng.c:L232-240 */
static void rng_sock_destruct(struct sock *sk)
{
	...
	rng_reset_addtl(ctx);
	sock_kfree_s(sk, ctx, ctx->len);
	af_alg_release_parent(sk);
}
```

注意这里**没有** `crypto_free_rng(ctx->drng)`。

原因就是前面说过的:

> child socket 只是共享 parent 的 RNG handle

真正的 `crypto_free_rng()` 在 parent 的 `rng_release()` 里。

所以生命周期关系是:

- parent control socket 持有真正的 RNG 实例
- child op socket 只是引用它
- 最后一个 child 释放后,再由 parent 生命周期收尾

---

## 14. 它为什么不需要 `nokey` 路径

你会发现 `algif_rng.c` 没有:

- `ops_nokey`
- `check_key`
- `accept_nokey`

原因很自然:

1. 不是每个 RNG 都要求显式 seed
2. 有些 RNG 会自动自播种(比如系统 RNG/DRBG)
3. 是否“已正确播种”由底层实现自己判断

文件里也明确写了:

```c
/* kernel/kernel-5.10/crypto/algif_rng.c:L91-99 */
	/*
	 * The enforcement of a proper seeding of an RNG is done within an
	 * RNG implementation. Some RNGs (DRBG, krng) do not need specific
	 * seeding as they automatically seed. The X9.31 DRNG will return
	 * an error if it was not seeded properly.
	 */
	genlen = crypto_rng_generate(drng, addtl, addtl_len, result, len);
```

也就是说:

> `algif_rng.c` 不在前端做“有没有 key/seed”的语义检查,而是把这个责任完全交给具体 RNG 实现。

这和 `hash`/`skcipher`/`aead` 明显不同。

---

## 15. 和其他三个 `algif_*` 的最终对照

| 维度 | `skcipher` | `aead` | `hash` | `rng` |
|---|---|---|---|---|
| 主要动作 | 变换输入数据 | 变换并认证输入数据 | 累积状态后输出摘要 | 直接生成随机字节 |
| 普通 child `sendmsg` | 有 | 有 | 有 | **没有** |
| 普通 child `recvmsg` | 触发加/解密 | 触发 AEAD | 取摘要/触发 final | **直接生成随机数** |
| 是否依赖 TX/RX SGL 大骨架 | 强 | 强 | 很弱 | 几乎没有 |
| 是否需要 `MSG_MORE` 状态机 | 常见 | 常见 | 关键 | 无 |
| 是否有 `nokey` 路径 | 有 | 有 | 有 | **无** |
| 特有概念 | IV/chunksize | AAD/tag/authsize | digest/export/import | seed/entropy/additional input |

一句话:

> `algif_rng.c` 是四个 `algif_*` 里最不像“数据变换器”的一个,它更像“给内核 RNG 套了一个 socket 抽头”。 

---

## 16. 对 x2600 当前系统意味着什么

在你当前系统里,`/proc/crypto` 看到的 RNG 相关条目主要是:

- `stdrng`
- `jitterentropy_rng`
- 各种 `drbg_*`

那么 `algif_rng.c` 在你系统上的作用就是:

1. 用户态 `bind("stdrng")`
2. `rng_bind()` -> `crypto_alloc_rng("stdrng", ...)`
3. `recvmsg(32)` -> `_rng_recvmsg()` -> `crypto_rng_generate(..., 32)`
4. 底层可能由 `drbg_*` 或 `jitterentropy_rng` 产生字节
5. 最后拷回用户态

这里同样:

> `algif_rng.c` 不决定硬件还是软件 RNG,它只是前端导出接口。

如果以后 x2600 有硬件 RNG 驱动注册进 Crypto API,用户态 `AF_ALG rng` 调用路径照旧,只是 `crypto_alloc_rng()` 可能会选到硬件实现。

---

## 17. 最实用的记忆法

如果只记三句话:

1. **普通 `rng` child socket 只有 `recvmsg`,没有 `sendmsg`**
2. **`recvmsg(len)` 本质就是 `crypto_rng_generate(..., len)`**
3. **`setkey` 是 reseed,`setentropy` 和 `addtl` 主要服务测试模式**

那这份文件的 80% 你就已经理解对了。

---

## TL;DR

- `algif_rng.c` 是 `AF_ALG` 的 RNG/DRBG 前端
- 普通路径:
  - `bind` -> `crypto_alloc_rng`
  - `setkey` -> `crypto_rng_reset`
  - `recvmsg` -> `crypto_rng_generate`
- 普通 child socket 不支持 `sendmsg`,因为 RNG 语义本来就是“取随机数”
- 单次 `recvmsg()` 最多只返回 `128` 字节,想取更多要循环调用
- `setentropy` 和 `test_ops` 是 CAVP 测试相关路径
- 多个 accept 出来的 child 共享同一个 RNG 状态,不是各自独立副本
- 在当前 x2600 上,它最终会落到 `stdrng` / `jitterentropy_rng` / `drbg_*` 这类软件实现
