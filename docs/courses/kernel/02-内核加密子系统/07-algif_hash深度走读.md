---
title: algif_hash 深度走读
author: EASYZOOM
date: 2026/04/21 12:00
categories:
 - Linux内核开发
tags:
 - Linux
 - 内核
 - 加密
---

# `algif_hash.c` 深度走读:哈希/MAC 在 `AF_ALG` 里是怎么工作的

> 目标:把 `kernel/kernel-5.10/crypto/algif_hash.c` 单独讲透,重点回答:
>
> 1. 它为什么和 `algif_skcipher.c` / `algif_aead.c` 的结构明显不同?
> 2. `sendmsg(MSG_MORE)` / `recvmsg()` 在 hash 语义下分别意味着什么?
> 3. `crypto_ahash_init/update/final/finup/digest` 在这个文件里是怎么被映射出来的?
> 4. `accept()` 为什么还能“克隆”一份中间 hash 状态?
>
> 配套阅读:

---

## 0. 一句话定位

`algif_hash.c` 是 `AF_ALG` 体系里负责 **hash / MAC** 这一类算法的前端适配层。

它把用户态 `type = "hash"` 的 socket 请求翻译成:

- `crypto_alloc_ahash()`
- `crypto_ahash_init()`
- `crypto_ahash_update()`
- `crypto_ahash_final()`
- `crypto_ahash_finup()`
- `crypto_ahash_digest()`

和 `skcipher` / `aead` 相比,它最大的不同不是“用的 API 不一样”这么简单,而是 **问题模型根本不一样**:

- `skcipher/aead` 是“输入一段数据,产出一段变换后的数据”
- `hash` 是“维护一份滚动状态,最后产出固定长度 digest”

所以 `algif_hash.c` 的核心是:

> **把 socket 的“多次 sendmsg + 一次 recvmsg”翻译成 ahash 的“init + 多次 update + final”状态机。**

---

## 1. 先看和 `skcipher/aead` 的根本差异

把三类放在一起看最容易理解:

| 类别 | 数据模型 | 核心 request | 输入/输出关系 |
|---|---|---|---|
| `skcipher` | 明文/密文块变换 | `skcipher_request` | 输入 N 字节,输出 N 字节 |
| `aead` | AAD + payload + tag | `aead_request` | 输入输出不对称,有 tag |
| `hash` | 流式累积状态 | `ahash_request` | 输入任意长度,输出固定 digest |

这直接导致实现结构不同:

- `skcipher/aead` 都很依赖 `af_alg_sendmsg()` 的全局 TX SGL 累积机制
- `hash` **几乎不需要**全局 TX SGL
- 它是每次 `sendmsg` 立刻把一段数据喂给 `crypto_ahash_update()`
- 最后一轮(或 `recvmsg`)再执行 `final`

所以你会发现:

> `algif_hash.c` 没有像 `skcipher/aead` 那样围着 `ctx->tsgl_list / af_alg_pull_tsgl / af_alg_get_rsgl` 打转,而是走了一条更直接的“边收边 update”路径。

---

## 2. 文件自己的私有上下文是什么

开头定义:

```c
/* kernel/kernel-5.10/crypto/algif_hash.c:L19-30 */
struct hash_ctx {
	struct af_alg_sgl sgl;

	u8 *result;

	struct crypto_wait wait;

	unsigned int len;
	bool more;

	struct ahash_request req;
};
```

这份上下文和 `af_alg_ctx` 很不一样,值得逐项看:

### 2.1 `sgl`

只有一个 `struct af_alg_sgl`,不是像 `skcipher/aead` 那样一整条 `tsgl_list`。

原因很简单:

- hash 的 `sendmsg` 收到一段数据后,立刻就可以 `update`
- 不需要像 `skcipher/aead` 那样先把很多页累积起来,等 `recvmsg` 时再组一份完整 request

### 2.2 `result`

指向最终摘要缓冲区。

- digest 只有在“最终阶段”才会真正生成
- 所以这个缓冲区按需分配,用完清零释放

### 2.3 `wait`

同步等待 `ahash` 请求完成,和其他 `algif_*` 一样。

### 2.4 `more`

这是整个文件最重要的状态位之一:

- `true`：后面还会继续喂数据,当前只做 `update`
- `false`：这一轮结束,可以/应该做 `final`

换句话说,`more` 基本上就是 `MSG_MORE` 在 hash 状态机里的内核镜像。

### 2.5 `req`

一份持久存在于 socket 上下文里的 `struct ahash_request`。

这和 `skcipher/aead` 又不同:

- `skcipher/aead` 每次 `recvmsg` 都新分配一份 `areq + request`
- `hash` 把 `ahash_request` 直接放进 `hash_ctx`

因为 hash 的关键是“跨多次 sendmsg 持续维护同一个中间状态”。

---

## 3. bind/release/setkey/accept 四件套

## 3.1 `hash_bind()`

```c
/* kernel/kernel-5.10/crypto/algif_hash.c:L394-397 */
static void *hash_bind(const char *name, u32 type, u32 mask)
{
	return crypto_alloc_ahash(name, type, mask);
}
```

这一步把:

- `sha256`
- `hmac(sha256)`
- `cmac(aes)`

这类用户态 `salg_name` 绑定成真正的 `struct crypto_ahash *tfm`。

## 3.2 `hash_release()`

```c
/* kernel/kernel-5.10/crypto/algif_hash.c:L399-402 */
static void hash_release(void *private)
{
	crypto_free_ahash(private);
}
```

## 3.3 `hash_setkey()`

```c
/* kernel/kernel-5.10/crypto/algif_hash.c:L404-407 */
static int hash_setkey(void *private, const u8 *key, unsigned int keylen)
{
	return crypto_ahash_setkey(private, key, keylen);
}
```

注意:

- 普通 `sha256` 不需要 key
- `hmac(sha256)` / `cmac(aes)` 需要 key

所以 `hash` 类型同样需要 `nokey` 路径。

## 3.4 `hash_accept_parent_nokey()`

```c
/* kernel/kernel-5.10/crypto/algif_hash.c:L419-443 */
static int hash_accept_parent_nokey(void *private, struct sock *sk)
{
	struct crypto_ahash *tfm = private;
	...
	unsigned int len = sizeof(*ctx) + crypto_ahash_reqsize(tfm);

	ctx = sock_kmalloc(sk, len, GFP_KERNEL);
	...
	ctx->result = NULL;
	ctx->len = len;
	ctx->more = false;
	crypto_init_wait(&ctx->wait);

	ask->private = ctx;

	ahash_request_set_tfm(&ctx->req, tfm);
	ahash_request_set_callback(&ctx->req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				   crypto_req_done, &ctx->wait);
	...
}
```

和 `skcipher/aead` 的 accept 相比,这里有两个很显著的不同:

1. **没有 `iv`**
2. **没有 `tsgl_list`**

但有一个很重要的点:

3. `sizeof(*ctx) + crypto_ahash_reqsize(tfm)`

这说明 `ahash_request` 的私有尾部空间也是和 `ctx` 一起一次性分配的。

---

## 4. `result` 缓冲为什么单独管理

两个小函数:

```c
/* kernel/kernel-5.10/crypto/algif_hash.c:L32-47 */
static int hash_alloc_result(struct sock *sk, struct hash_ctx *ctx)
{
	unsigned ds;
	...
	ds = crypto_ahash_digestsize(crypto_ahash_reqtfm(&ctx->req));
	ctx->result = sock_kmalloc(sk, ds, GFP_KERNEL);
	...
	memset(ctx->result, 0, ds);
}
```

```c
/* kernel/kernel-5.10/crypto/algif_hash.c:L50-60 */
static void hash_free_result(struct sock *sk, struct hash_ctx *ctx)
{
	unsigned ds;
	...
	ds = crypto_ahash_digestsize(crypto_ahash_reqtfm(&ctx->req));
	sock_kzfree_s(sk, ctx->result, ds);
	ctx->result = NULL;
}
```

这里体现两个设计点:

### 4.1 digest 长度是算法决定的

- `sha256` -> 32
- `sha1` -> 20
- `cmac(aes)` -> 16

所以不能把 `result` 写死在结构体里。

### 4.2 用完立即清零

尤其对于 HMAC / CMAC 场景,digest 本身可能也是敏感数据,所以这里用 `sock_kzfree_s()` 清零释放。

---

## 5. `hash_sendmsg()`:这是 `init + update + 可能 final`

`hash_sendmsg()` 是整份文件的第一核心函数。

### 5.1 如果这是新一轮 hash,先 `init`

```c
/* kernel/kernel-5.10/crypto/algif_hash.c:L76-84 */
	lock_sock(sk);
	if (!ctx->more) {
		if ((msg->msg_flags & MSG_MORE))
			hash_free_result(sk, ctx);

		err = crypto_wait_req(crypto_ahash_init(&ctx->req), &ctx->wait);
		if (err)
			goto unlock;
	}
```

语义是:

- 如果上一轮已经结束(`ctx->more == false`),那这次 `sendmsg` 代表开启一轮新的 hash
- 于是先 `crypto_ahash_init()`

顺带注意这一句:

```c
if ((msg->msg_flags & MSG_MORE))
    hash_free_result(sk, ctx);
```

意思是:

> 如果之前已经算出过 digest,但这次用户又想继续把它当“中间状态”往后喂数据,那旧 result 必须作废。

这是 hash 状态机语义里很重要的一条。

### 5.2 每一段数据都立刻 `update`

```c
/* kernel/kernel-5.10/crypto/algif_hash.c:L88-109 */
	while (msg_data_left(msg)) {
		int len = msg_data_left(msg);
		...
		len = af_alg_make_sg(&ctx->sgl, &msg->msg_iter, len);
		...
		ahash_request_set_crypt(&ctx->req, ctx->sgl.sg, NULL, len);

		err = crypto_wait_req(crypto_ahash_update(&ctx->req),
				      &ctx->wait);
		af_alg_free_sg(&ctx->sgl);
		...
		copied += len;
		iov_iter_advance(&msg->msg_iter, len);
	}
```

这就是它和 `skcipher/aead` 最大的实现差别:

- `af_alg_make_sg()` 只是把这一次 sendmsg 的一段 iovec 临时 pin 成 SG
- 立刻 `crypto_ahash_update()`
- 立刻 `af_alg_free_sg()`
- 然后继续下一段

换句话说:

> `algif_hash` 不维护一个长期存在的 TX 页队列,而是“收到就 update,用完就放”。

因为 hash 中间状态本来就保存在 `ahash_request` / tfm 内部,不需要回头再读原始输入。

### 5.3 最后一段如果没带 `MSG_MORE`,直接 `final`

```c
/* kernel/kernel-5.10/crypto/algif_hash.c:L114-123 */
	ctx->more = msg->msg_flags & MSG_MORE;
	if (!ctx->more) {
		err = hash_alloc_result(sk, ctx);
		...
		ahash_request_set_crypt(&ctx->req, NULL, ctx->result, 0);
		err = crypto_wait_req(crypto_ahash_final(&ctx->req),
				      &ctx->wait);
	}
```

也就是说:

- **带 `MSG_MORE`**:只做 `update`
- **不带 `MSG_MORE`**:做完最后一次 `update` 后,立刻 `final`

这一点和 `skcipher/aead` 很不一样:

- `skcipher/aead` 通常是 `sendmsg` 只堆数据,`recvmsg` 才触发真正运算
- `hash` 则是 `sendmsg` 就已经在干活了

`recvmsg` 很多时候只是把已经算好的摘要取出来。

---

## 6. `hash_sendpage()`:为什么会用 `digest/finup/update` 三件套

`sendpage` 路径稍微更“优化”一点:

```c
/* kernel/kernel-5.10/crypto/algif_hash.c:L153-169 */
	ahash_request_set_crypt(&ctx->req, ctx->sgl.sg, ctx->result, size);

	if (!(flags & MSG_MORE)) {
		if (ctx->more)
			err = crypto_ahash_finup(&ctx->req);
		else
			err = crypto_ahash_digest(&ctx->req);
	} else {
		if (!ctx->more) {
			err = crypto_ahash_init(&ctx->req);
			...
		}

		err = crypto_ahash_update(&ctx->req);
	}
```

这里非常值得单独讲:

### 6.1 `crypto_ahash_digest()`

语义:

> 一次性完成 `init + update + final`

所以当:

- 这是第一块数据(`!ctx->more`)
- 同时也是最后一块(`!(flags & MSG_MORE)`)

那就可以直接 `digest()` 一步到位。

### 6.2 `crypto_ahash_finup()`

语义:

> 在已有中间状态基础上,完成“最后一次 update + final”

所以当:

- 之前已经 `update` 过(`ctx->more == true`)
- 当前这块是最后一块(`!(flags & MSG_MORE)`)

最自然的就是 `finup()`。

### 6.3 `crypto_ahash_update()`

当当前块还不是最后一块时,就只是继续 `update()`。

所以 `hash_sendpage()` 其实非常漂亮地把 ahash API 的三种常见模式都用上了:

| 场景 | API |
|---|---|
| 单块消息,一次算完 | `crypto_ahash_digest()` |
| 已有前缀状态,最后一块收尾 | `crypto_ahash_finup()` |
| 中间块 | `crypto_ahash_update()` |

这比 `sendmsg` 路径更“贴近 ahash 原语本义”。

---

## 7. `hash_recvmsg()`:这是“取结果”,不是“真正开始算”

`hash_recvmsg()` 是第二核心函数。

### 7.1 先把长度裁成 digestsize

```c
/* kernel/kernel-5.10/crypto/algif_hash.c:L189-196 */
	unsigned ds = crypto_ahash_digestsize(crypto_ahash_reqtfm(&ctx->req));
	...
	if (len > ds)
		len = ds;
	else if (len < ds)
		msg->msg_flags |= MSG_TRUNC;
```

这说明:

- 用户读多了也只能拿到 digest 长度
- 用户给的 buffer 太小,就打 `MSG_TRUNC`

### 7.2 如果还没生成结果,这里补 `final`

```c
/* kernel/kernel-5.10/crypto/algif_hash.c:L198-218 */
	result = ctx->result;
	err = hash_alloc_result(sk, ctx);
	...
	ahash_request_set_crypt(&ctx->req, NULL, ctx->result, 0);

	if (!result && !ctx->more) {
		err = crypto_wait_req(crypto_ahash_init(&ctx->req),
				      &ctx->wait);
		...
	}

	if (!result || ctx->more) {
		ctx->more = false;
		err = crypto_wait_req(crypto_ahash_final(&ctx->req),
				      &ctx->wait);
		...
	}
```

这段逻辑很巧,要分几种情况看:

### 情况 A: 前面的 `sendmsg` 已经做过 final

也就是:

- `ctx->result != NULL`
- `ctx->more == false`

那这里基本什么都不用算,直接把现成 digest 拷给用户。

### 情况 B: 用户一直 `sendmsg(MSG_MORE)`,还没收尾就来 `recvmsg`

这时:

- `ctx->result == NULL`
- `ctx->more == true`

内核会把这次 `recvmsg` 视作“隐式收尾”:

- 先 `final`
- 再返回 digest

这就是为什么在 hash 语义里:

> `recvmsg()` 既可能只是“取已经算好的结果”,也可能充当“final 触发器”。

### 情况 C: 从没 sendmsg,直接 recvmsg

这时:

- `ctx->result == NULL`
- `ctx->more == false`

代码会先 `init()`,再 `final()`。  
也就是返回 **空消息的摘要**。

这正是 hash 语义上合理的行为:

- `sha256("")`
- `hmac(key, "")`

都是有定义的。

### 7.3 复制给用户后立即丢掉 result

```c
/* kernel/kernel-5.10/crypto/algif_hash.c:L221-224 */
	err = memcpy_to_msg(msg, ctx->result, len);
...
	hash_free_result(sk, ctx);
```

所以 `result` 是一次性的“读缓存”,不是长期保留。

这也意味着:

> 连续两次 `recvmsg()` 如果中间没有新 `sendmsg`,第二次会重新走一遍 `init+final` 空消息摘要路径。

---

## 8. `MSG_MORE` 在 hash 世界里的真正含义

很多人容易把它机械理解成“还有更多网络数据”。  
在 `algif_hash.c` 里,它更准确的语义是:

> **当前 digest 还没结束,继续保留中间哈希状态**

所以:

- `sendmsg(..., MSG_MORE)` = `update()`
- `sendmsg(..., !MSG_MORE)` = “这是最后一块”,因此 `final()`
- `recvmsg()` 在 `ctx->more == true` 时 = “如果你非要现在拿结果,那我就帮你 `final()` 一下”

把它翻译成 ahash 状态机就是:

```text
初始:
  more = false

sendmsg(MSG_MORE):
  if !more: init()
  update()
  more = true

sendmsg(!MSG_MORE):
  if !more: init()
  update()
  final()
  more = false

recvmsg():
  if more: final(), more = false
  copy result
```

---

## 9. `accept()` 为什么能复制中间 hash 状态

这是 `algif_hash.c` 最特别、也最容易被忽略的一点。

### 9.1 代码本体

```c
/* kernel/kernel-5.10/crypto/algif_hash.c:L230-270 */
static int hash_accept(struct socket *sock, struct socket *newsock, int flags,
		       bool kern)
{
	...
	bool more;
	char state[HASH_MAX_STATESIZE];
	...
	lock_sock(sk);
	more = ctx->more;
	err = more ? crypto_ahash_export(req, state) : 0;
	release_sock(sk);
	...
	err = af_alg_accept(ask->parent, newsock, kern);
	...
	ctx2->more = more;

	if (!more)
		return err;

	err = crypto_ahash_import(&ctx2->req, state);
	...
}
```

### 9.2 这段在做什么

如果当前 socket 上已经有一份进行到中途的 hash 状态(`ctx->more == true`),  
那么 `accept()` 新出来的 child socket 会:

1. 用 `crypto_ahash_export()` 把当前中间状态导出到 `state[]`
2. 创建新的 op socket
3. 用 `crypto_ahash_import()` 把这份中间状态导入到新 socket 的 `ctx2->req`

效果就是:

> **新 socket 继承了旧 socket 当前的哈希进度**

### 9.3 为什么这很有用

这相当于“从某个前缀状态分叉”。

举例:

```text
先 update("prefix:")
然后 accept() 出两个 child
child1 再 update("A") -> final
child2 再 update("B") -> final
```

这样就可以避免重复计算公共前缀。

这在:

- HMAC 前缀复用
- 分支消息认证
- 测试/benchmark

这类场景里很有价值。

这也是 `hash` 类型比 `skcipher/aead` 更“状态机化”的直接体现。

---

## 10. `nokey` 路径

和其他 `algif_*` 一样,`hash` 也有:

- `algif_hash_ops`
- `algif_hash_ops_nokey`

`hash_check_key()` 会检查:

```c
/* kernel/kernel-5.10/crypto/algif_hash.c:L309-317 */
	err = -ENOKEY;
	...
	if (crypto_ahash_get_flags(tfm) & CRYPTO_TFM_NEED_KEY)
		goto unlock;
	...
	err = 0;
```

也就是说:

- 普通 hash(`sha256`) 不需要 key
- HMAC/CMAC 这类如果底层 tfm 还标着 `NEED_KEY`,那 `sendmsg/recvmsg/accept` 都会拒绝

因此 `hash` 类型和 `skcipher/aead` 一样,也有“先 accept,后补检 key”的机制。

---

## 11. 它为什么基本不用 `af_alg.c` 的 TX/RX 大骨架

回想一下前两篇:

- `skcipher/aead` 大量依赖:
  - `af_alg_sendmsg()`
  - `af_alg_get_rsgl()`
  - `af_alg_pull_tsgl()`

而 `algif_hash.c` 几乎没有这些。

原因就在数据模型:

### `skcipher/aead`

需要在“真正开始算”之前把:

- 全部输入
- 输出目标
- AAD/tag/IV

摆成一份完整 request。

### `hash`

只需要把每一段输入:

1. pin 成 SG
2. `update`
3. 放掉

最终摘要只要在末尾拿到一个 `result` 缓冲即可。

所以 `algif_hash.c` 更像:

> `socket` 外壳包着一个流式 `ahash` 状态机

而不是:

> 一次 recvmsg 驱动一个完整 crypto request 的“批处理模型”

这也是读这个文件时最需要切换的脑回路。

---

## 12. 和用户态操作一一对应

### 12.1 单次 hash

用户态:

```text
sendmsg("hello", no MSG_MORE)
recvmsg(digest)
```

内核:

```text
hash_sendmsg:
  init
  update("hello")
  final -> ctx->result

hash_recvmsg:
  直接拷 ctx->result 给用户
  释放 result
```

### 12.2 分块 hash

用户态:

```text
sendmsg("he", MSG_MORE)
sendmsg("llo", MSG_MORE)
recvmsg(digest)
```

内核:

```text
第1次 sendmsg:
  init
  update("he")
  more = true

第2次 sendmsg:
  update("llo")
  more = true

recvmsg:
  final
  copy digest
  more = false
```

### 12.3 `sendpage` 优化路径

用户态走 splice/sendfile 类路径时:

- 单块 + 最后一块 -> `digest()`
- 前面已有状态 + 最后一块 -> `finup()`
- 中间块 -> `update()`

---

## 13. 对 x2600 当前系统意味着什么

在你当前系统上:

- `bind("sha256")` 最终会拿到 `sha256-generic`
- `bind("hmac(sha256)")` 会拿到 HMAC template + `sha256-generic`
- `bind("cmac(aes)")` 会拿到 CMAC template + `aes-generic`

所以 `algif_hash.c` 负责的是:

1. 把用户态多次 `sendmsg` 转成 `init/update/final`
2. 下层真正的压缩函数/摘要运算
   - `sha256-generic`
   - `ghash-generic`
   - `cmac(aes-generic)` 等
3. 最终摘要再返回用户态

仍然是:

> `algif_hash.c` 自身不决定硬件还是软件,它只是前端翻译层。

---

## 14. 一个最实用的记忆法

如果只记一句:

> `algif_hash.c` = “把 `MSG_MORE` 解释成 ahash 的中间态,把 `recvmsg()` 解释成 final 后取 digest”

如果再加一句:

> 它不是“收集全部输入再统一处理”,而是“收到一段就 update 一段”

那这个文件的 80% 你就已经理解对了。

---

## 15. 和 `skcipher/aead` 最终对照

| 维度 | `algif_skcipher` | `algif_aead` | `algif_hash` |
|---|---|---|---|
| 是否依赖全局 TX SGL | 强依赖 | 强依赖 | 基本不依赖 |
| 真正运算主要在何时触发 | `recvmsg` | `recvmsg` | `sendmsg` 和/或 `recvmsg` |
| 结果长度 | 与输入等长 | 加/解密不对称 | 固定 digest 长度 |
| 状态机核心 | 页缓冲布局 | AAD/tag 布局 | `more` + `ahash` 中间态 |
| 特有技巧 | `af_alg_pull_tsgl` | `null_tfm` copy + tag 链接 | `export/import` 克隆中间态 |

---

## TL;DR

- `algif_hash.c` 是 `AF_ALG` 的 hash/MAC 前端
- 它把 socket 语义翻译成 `crypto_ahash_init/update/final/finup/digest`
- 和 `skcipher/aead` 最大不同:
  - 不维护长期 TX 页队列
  - 收到一段数据就直接 `update`
  - `MSG_MORE` 表示“保留中间 hash 状态”
  - `recvmsg()` 可以触发 `final` 并取 digest
- `sendpage` 路径会聪明地选 `digest / finup / update`
- `accept()` 还能用 `crypto_ahash_export/import` 克隆中间 hash 状态
- 在当前 x2600 上,它最终还是把请求交给 `sha256-generic`、`cmac(aes-generic)` 这类软件实现
