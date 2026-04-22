---
title: algif_aead 深度走读
author: EASYZOOM
date: 2026/04/21 12:00
categories:
 - Linux内核开发
tags:
 - Linux
 - 内核
 - 加密
---

# `algif_aead.c` 深度走读:AEAD 在 `AF_ALG` 里到底是怎么跑起来的

> 目标:把 `kernel/kernel-5.10/crypto/algif_aead.c` 单独讲透,重点回答:
>
> 1. 它和 `algif_skcipher.c` 相比到底多了什么复杂度?
> 2. `AAD`、`ciphertext/plaintext`、`tag` 在 TX/RX SGL 里是怎么摆放的?
> 3. 为什么文件里要额外申请一个 `null_tfm`?
> 4. `aead_request_set_crypt()` / `aead_request_set_ad()` 最终是怎么组出来的?
>
> 配套阅读:

---

## 0. 一句话定位

`algif_aead.c` 是 `AF_ALG` 体系里负责 **AEAD(Authenticated Encryption with Associated Data)** 的类别适配层。

它做的不是具体算法运算,而是:

> 把用户态通过 `type = "aead"` 发来的 socket 请求,翻译成 `struct aead_request` + `crypto_aead_encrypt/decrypt()`。

和 `skcipher` 相比,AEAD 额外要处理三样东西:

1. `AAD`(附加认证数据,参与认证但不加密)
2. `tag/authsize`(认证标签)
3. 加/解密两种方向下,输入输出长度不相等的现实:
   - 加密:输出 = AAD + 密文 + tag
   - 解密:输出 = AAD + 明文(输入里带来的 tag 被消费掉)

这就是 `algif_aead.c` 明显比 `algif_skcipher.c` 更复杂的根源。

---

## 1. 先建立一个整体图

```text
用户态:
  socket(AF_ALG)
  bind({ type = "aead", name = "gcm(aes)" })
  setsockopt(ALG_SET_KEY)
  setsockopt(ALG_SET_AEAD_AUTHSIZE)
  accept()
  sendmsg(cmsg = OP / IV / ASSOCLEN, data = AAD || PT/CT || [Tag])
  recvmsg(out = AAD || CT || Tag   或   AAD || PT)
      |
      v
af_alg.c
  - 公共 bind/setsockopt/accept
  - af_alg_sendmsg() 累积 TX SGL
  - af_alg_get_rsgl() 组 RX SGL
  - af_alg_pull_tsgl() 从全局 TX SGL 迁移数据
      |
      v
algif_aead.c
  - bind -> crypto_alloc_aead()
  - setkey -> crypto_aead_setkey()
  - setauthsize -> crypto_aead_setauthsize()
  - recvmsg 时布局 AAD / payload / tag
  - 组 aead_request
  - 调 crypto_aead_encrypt() / decrypt()
      |
      v
下层实现
  - software: gcm(aes-generic), ccm(aes-generic), authenc(...)
  - hardware: drivers/crypto/* 里的 AEAD 实现(如果系统有)
```

---

## 2. 它注册成什么类型

文件末尾和其他 `algif_*` 一样,注册一个 `struct af_alg_type`:

```c
/* kernel/kernel-5.10/crypto/algif_aead.c:L577-587 */
static const struct af_alg_type algif_type_aead = {
	.bind		=	aead_bind,
	.release	=	aead_release,
	.setkey		=	aead_setkey,
	.setauthsize	=	aead_setauthsize,
	.accept		=	aead_accept_parent,
	.accept_nokey	=	aead_accept_parent_nokey,
	.ops		=	&algif_aead_ops,
	.ops_nokey	=	&algif_aead_ops_nokey,
	.name		=	"aead",
	.owner		=	THIS_MODULE
};
```

这意味着用户态如果写:

```c
sa.salg_type = "aead";
sa.salg_name = "gcm(aes)";
```

`af_alg.c::alg_bind()` 就会把请求交给 `algif_aead`。

---

## 3. 这个文件私有的核心数据结构

`af_alg.c` 的公共上下文 `struct af_alg_ctx` 里已经有:

- `tsgl_list`
- `iv`
- `aead_assoclen`
- `used`
- `more`
- `enc`
- `wait`

所以 `algif_aead.c` 不需要重新发明一套 socket 级上下文。  
它唯一额外定义的是:

```c
/* kernel/kernel-5.10/crypto/algif_aead.c:L39-42 */
struct aead_tfm {
	struct crypto_aead *aead;
	struct crypto_sync_skcipher *null_tfm;
};
```

这非常关键:

- `aead`：真正的 AEAD tfm,比如 `gcm(aes)`
- `null_tfm`：一个同步的 **null skcipher**,只拿来做“按 scatterlist 拷贝数据”

后面你会看到,`null_tfm` 的存在正是这份文件最“巧”的地方。

---

## 4. `bind/release/setkey/setauthsize` 四件套

## 4.1 `aead_bind()`

```c
/* kernel/kernel-5.10/crypto/algif_aead.c:L467-493 */
static void *aead_bind(const char *name, u32 type, u32 mask)
{
	struct aead_tfm *tfm;
	struct crypto_aead *aead;
	struct crypto_sync_skcipher *null_tfm;

	tfm = kzalloc(sizeof(*tfm), GFP_KERNEL);
	...
	aead = crypto_alloc_aead(name, type, mask);
	...
	null_tfm = crypto_get_default_null_skcipher();
	...
	tfm->aead = aead;
	tfm->null_tfm = null_tfm;
	return tfm;
}
```

它一次性准备两样东西:

1. 真正的 AEAD tfm
2. 一个 `null skcipher`

`null skcipher` 不是为了加密,而是为了 **借用 skcipher 的 scatterlist → scatterlist 拷贝能力**。  
后面 `crypto_aead_copy_sgl()` 就是干这个的。

## 4.2 `aead_release()`

```c
/* kernel/kernel-5.10/crypto/algif_aead.c:L496-502 */
static void aead_release(void *private)
{
	struct aead_tfm *tfm = private;

	crypto_free_aead(tfm->aead);
	crypto_put_default_null_skcipher();
	kfree(tfm);
}
```

## 4.3 `aead_setkey()`

```c
/* kernel/kernel-5.10/crypto/algif_aead.c:L512-516 */
static int aead_setkey(void *private, const u8 *key, unsigned int keylen)
{
	struct aead_tfm *tfm = private;
	return crypto_aead_setkey(tfm->aead, key, keylen);
}
```

## 4.4 `aead_setauthsize()`

```c
/* kernel/kernel-5.10/crypto/algif_aead.c:L505-509 */
static int aead_setauthsize(void *private, unsigned int authsize)
{
	struct aead_tfm *tfm = private;
	return crypto_aead_setauthsize(tfm->aead, authsize);
}
```

这就是 AEAD 和 `skcipher` 第一个显著差别:

- `skcipher` 只有 `setkey`
- `aead` 除了 `setkey`,还必须支持 `authsize`

因为 GCM/CCM 这类算法 tag 长度通常可配置。

---

## 5. `accept`:给 op socket 建上下文

和 `skcipher` 很像,`accept_parent_nokey()` 给 child/op socket 分配:

- `struct af_alg_ctx`
- `ctx->iv`
- `crypto_wait`
- `ctx->tsgl_list`

```c
/* kernel/kernel-5.10/crypto/algif_aead.c:L535-564 */
static int aead_accept_parent_nokey(void *private, struct sock *sk)
{
	...
	ctx = sock_kmalloc(sk, len, GFP_KERNEL);
	...
	ctx->iv = sock_kmalloc(sk, ivlen, GFP_KERNEL);
	...
	INIT_LIST_HEAD(&ctx->tsgl_list);
	ctx->len = len;
	crypto_init_wait(&ctx->wait);
	ask->private = ctx;
	sk->sk_destruct = aead_sock_destruct;
	return 0;
}
```

如果底层 tfm 还没 `setkey`,普通 `accept` 会先返回 `-ENOKEY`,然后走 `accept_nokey` 分支,把 child socket 暂时挂到 `ops_nokey` 上,等真正发 `sendmsg/recvmsg` 时再补检 key。

这个模式和 `algif_skcipher` 基本一致。

---

## 6. sendmsg 很简单,真正难的是 recvmsg

## 6.1 `aead_sendmsg()`

```c
/* kernel/kernel-5.10/crypto/algif_aead.c:L61-71 */
static int aead_sendmsg(struct socket *sock, struct msghdr *msg, size_t size)
{
	...
	struct crypto_aead *tfm = aeadc->aead;
	unsigned int ivsize = crypto_aead_ivsize(tfm);

	return af_alg_sendmsg(sock, msg, size, ivsize);
}
```

它只是把 `ivsize` 交给公共层 `af_alg_sendmsg()`。  
`AF_ALG` AEAD 相关的控制消息还是由 `af_alg.c` 统一解析:

- `ALG_SET_OP`
- `ALG_SET_IV`
- `ALG_SET_AEAD_ASSOCLEN`

并落入 `ctx->enc` / `ctx->iv` / `ctx->aead_assoclen`。

所以 **真正的 AEAD 核心逻辑几乎全在 `_aead_recvmsg()`**。

---

## 7. `_aead_recvmsg()` 的第一步:先算清楚输入输出长度

### 7.1 先判断“数据是否足够”

```c
/* kernel/kernel-5.10/crypto/algif_aead.c:L44-59 */
static inline bool aead_sufficient_data(struct sock *sk)
{
	...
	unsigned int as = crypto_aead_authsize(tfm);
	return ctx->used >= ctx->aead_assoclen + (ctx->enc ? 0 : as);
}
```

这里体现了 AEAD 的最小输入约束:

- **加密**时:最少要有 `AAD`
- **解密**时:最少要有 `AAD + tag`

因为解密连 tag 都没有的话,根本没法验真。

`_aead_recvmsg()` 一开始就做双保险:

```c
/* kernel/kernel-5.10/crypto/algif_aead.c:L109-131 */
	if (!ctx->init || ctx->more) {
		err = af_alg_wait_for_data(sk, flags, 0);
		...
	}
	...
	if (!aead_sufficient_data(sk))
		return -EINVAL;
```

### 7.2 `used` 和 `outlen` 的计算

```c
/* kernel/kernel-5.10/crypto/algif_aead.c:L119-150 */
	used = ctx->used;

	if (ctx->enc)
		outlen = used + as;
	else
		outlen = used - as;

	used -= ctx->aead_assoclen;
```

这几行很关键,里面混了三个不同语义的长度:

1. `ctx->used`
   - 当前 TX SGL 里总共有多少字节
   - 这个长度包含 `AAD`、payload,解密时还包含 `tag`

2. `outlen`
   - 用户态最终应该在 RX 缓冲里拿到多少字节
   - 加密时多出 `tag`
   - 解密时去掉 `tag`

3. `used`
   - 传给 `aead_request_set_crypt()` 的 **payload 长度**
   - 不包含 `AAD`
   - 加密时是 `PT`
   - 解密时是 `CT + Tag`

也就是说:

### 加密路径

```text
TX:  AAD || PT
used(initial) = aad + pt
outlen        = aad + pt + tag
used(final)   = pt
```

### 解密路径

```text
TX:  AAD || CT || Tag
used(initial) = aad + ct + tag
outlen        = aad + ct
used(final)   = ct + tag
```

所以 AEAD 里最容易看乱的就是:

> `used` 在函数里先代表“总输入”,后来又变成“传给 AEAD payload 的 cryptlen”。

---

## 8. 为什么它要用 `null_tfm` 做一次“伪加密”

专门有个辅助函数:

```c
/* kernel/kernel-5.10/crypto/algif_aead.c:L74-86 */
static int crypto_aead_copy_sgl(struct crypto_sync_skcipher *null_tfm,
				struct scatterlist *src,
				struct scatterlist *dst, unsigned int len)
{
	SYNC_SKCIPHER_REQUEST_ON_STACK(skreq, null_tfm);
	...
	skcipher_request_set_crypt(skreq, src, dst, len, NULL);
	return crypto_skcipher_encrypt(skreq);
}
```

这不是拿 null cipher 去“加密”,而是:

> 借用 `null skcipher` 的 scatterlist 处理路径,做一次 **src SGL → dst SGL 的纯拷贝**

为什么不自己 `memcpy`?

因为这里的源和目标都是 SG 链:

- 源可能是 TX SGL 链
- 目标是 RX SGL 链
- 两边都可能跨页、跨多个 SG entry

自己手写“多段 SG 到多段 SG 的拷贝器”既麻烦又容易错。  
用 `null skcipher` 相当于复用内核现成的 scatterwalk 逻辑。

这就是 `aead_bind()` 里额外申请 `null_tfm` 的根本原因。

---

## 9. 加密路径:为什么它要先把 `AAD || PT` 拷到 RX SGL

文件里的注释已经把思路写得非常清楚:

```c
/* kernel/kernel-5.10/crypto/algif_aead.c:L215-230 */
	if (ctx->enc) {
		/*
		 * Encryption operation - The in-place cipher operation is
		 * achieved by the following operation:
		 *
		 * TX SGL: AAD || PT
		 *	    |	   |
		 *	    | copy |
		 *	    v	   v
		 * RX SGL: AAD || PT || Tag
		 */
		err = crypto_aead_copy_sgl(null_tfm, tsgl_src,
					   areq->first_rsgl.sgl.sg, processed);
		...
		af_alg_pull_tsgl(sk, processed, NULL, 0);
	}
```

### 它在做什么

1. 把 `AAD || PT` 从 TX SGL 拷到 RX SGL 前部
2. 之后把 RX SGL 既当 **src** 又当 **dst**
3. 让底层 AEAD 在 RX buffer 上原地把 `PT` 变成 `CT`,并在尾部追加 `Tag`

### 为什么这么做

因为 AAD 本来就是“认证但不加密”,输出也必须把它原样保留。  
所以最方便的布局是:

```text
RX 先长这样: AAD || PT || [预留 tag 空间]
AEAD 原地处理后: AAD || CT || Tag
```

这就是代码里说的 **in-place cipher operation**。

### `af_alg_pull_tsgl(..., NULL, 0)` 为什么是丢弃

因为加密路径下,TX 里的 `AAD || PT` 已经被拷进 RX 了,后续 AEAD 运算只在 RX 上原地改。  
所以 TX 那部分页就可以直接消费掉,不需要再转交到 `areq->tsgl`。

---

## 10. 解密路径:真正最难的地方是 tag 怎么拼进去

解密注释:

```c
/* kernel/kernel-5.10/crypto/algif_aead.c:L231-276 */
	} else {
		/*
		 * Decryption operation - To achieve an in-place cipher
		 * operation, the following  SGL structure is used:
		 *
		 * TX SGL: AAD || CT || Tag
		 *	    |	   |	 ^
		 *	    | copy |	 | Create SGL link.
		 *	    v	   v	 |
		 * RX SGL: AAD || CT ----+
		 */
```

这段是整份文件最核心的技巧。

### 10.1 先拷 `AAD || CT` 到 RX

```c
/* kernel/kernel-5.10/crypto/algif_aead.c:L243-246 */
		err = crypto_aead_copy_sgl(null_tfm, tsgl_src,
					   areq->first_rsgl.sgl.sg, outlen);
```

这里的 `outlen` 在解密路径下等于:

```text
AAD + CT
```

也就是故意 **不拷 tag**。

### 10.2 再把 tag 从 TX 抽出来,挂成一条额外 TX SGL

```c
/* kernel/kernel-5.10/crypto/algif_aead.c:L249-264 */
		areq->tsgl_entries = af_alg_count_tsgl(sk, processed,
						       processed - as);
		...
		sg_init_table(areq->tsgl, areq->tsgl_entries);

		/* Release TX SGL, except for tag data and reassign tag data. */
		af_alg_pull_tsgl(sk, processed, areq->tsgl, processed - as);
```

这一步的语义是:

- 从全局 TX SGL 里消费 `processed = AAD + CT + Tag`
- 但只把 **最后 `as` 字节(tag)** 重新指派给 `areq->tsgl`
- 前面的 `AAD || CT` 已经复制到 RX,所以对应的 TX 页直接释放

这里 `dst_offset = processed - as` 非常巧妙,它表示:

> 前 `AAD + CT` 这段不要搬,只从最后的 `tag` 开始重新挂到 `areq->tsgl`

### 10.3 再把 tag 那条链接到 RX 后面

```c
/* kernel/kernel-5.10/crypto/algif_aead.c:L266-276 */
		if (usedpages) {
			struct af_alg_sgl *sgl_prev = &areq->last_rsgl->sgl;
			sg_unmark_end(sgl_prev->sg + sgl_prev->npages - 1);
			sg_chain(sgl_prev->sg, sgl_prev->npages + 1,
				 areq->tsgl);
		} else
			rsgl_src = areq->tsgl;
```

最终形成的 **源 SGL** 是:

```text
src = RX里的 [AAD || CT]  +  TX里抽出来的 [Tag]
```

而 **目标 SGL** 仍然只是 RX:

```text
dst = RX里的 [AAD || CT]
```

于是传给底层 AEAD 的语义就完全对上了:

- 输入 src = `AAD || CT || Tag`
- 输出 dst = `AAD || PT`

这就是为什么解密比加密麻烦得多:  
**tag 不应该出现在输出里,但又必须参与输入认证**,所以只能单独从 TX 抽出来再链到源 SGL 尾部。

---

## 11. 最终 request 是怎么组出来的

所有前面的铺垫,最后都收束到这 4 行:

```c
/* kernel/kernel-5.10/crypto/algif_aead.c:L279-283 */
	aead_request_set_crypt(&areq->cra_u.aead_req, rsgl_src,
			       areq->first_rsgl.sgl.sg, used, ctx->iv);
	aead_request_set_ad(&areq->cra_u.aead_req, ctx->aead_assoclen);
	aead_request_set_tfm(&areq->cra_u.aead_req, tfm);
```

三件事:

1. `src = rsgl_src`
   - 加密时:就是 RX 里那份 `AAD || PT || [tag space]` 的前半段
   - 解密时:是“RX里的 `AAD || CT` + TX尾部 tag 链接”后的复合源

2. `dst = areq->first_rsgl.sgl.sg`
   - 永远是 RX SGL

3. `cryptlen = used`
   - 注意这里是 **payload 长度**
   - 加密时 = `PT`
   - 解密时 = `CT + Tag`

再额外告诉 AEAD:

4. `assoclen = ctx->aead_assoclen`
   - 告诉底层:前多少字节是 AAD

这就是 `aead_request` 和 `skcipher_request` 最大的接口差异:

- `skcipher_request` 只有 `crypt(src,dst,len,iv)`
- `aead_request` 还要显式告诉它 `assoclen`

---

## 12. 同步 / AIO 提交路径

和 `skcipher` 几乎一样:

### AIO

```c
/* kernel/kernel-5.10/crypto/algif_aead.c:L293-300 */
		aead_request_set_callback(&areq->cra_u.aead_req,
					  CRYPTO_TFM_REQ_MAY_SLEEP,
					  af_alg_async_cb, areq);
		err = ctx->enc ? crypto_aead_encrypt(&areq->cra_u.aead_req) :
				 crypto_aead_decrypt(&areq->cra_u.aead_req);
		if (err == -EINPROGRESS)
			return -EIOCBQUEUED;
```

### 同步

```c
/* kernel/kernel-5.10/crypto/algif_aead.c:L306-313 */
		aead_request_set_callback(&areq->cra_u.aead_req,
					  CRYPTO_TFM_REQ_MAY_SLEEP |
					  CRYPTO_TFM_REQ_MAY_BACKLOG,
					  crypto_req_done, &ctx->wait);
		err = crypto_wait_req(ctx->enc ?
				crypto_aead_encrypt(&areq->cra_u.aead_req) :
				crypto_aead_decrypt(&areq->cra_u.aead_req),
				&ctx->wait);
```

### AEAD 特有的一点

`aead_recvmsg()` 外层在处理错误时把 `-EBADMSG` 也当成特殊错误保留:

```c
/* kernel/kernel-5.10/crypto/algif_aead.c:L341-344 */
		if (err <= 0) {
			if (err == -EIOCBQUEUED || err == -EBADMSG || !ret)
				ret = err;
			goto out;
		}
```

这是因为:

> 对 AEAD 解密而言,`-EBADMSG` 往往代表 tag 校验失败

这在语义上比一般 I/O 错误更重要,不能像普通“已处理部分数据”那样被吞掉。

---

## 13. `nokey` 路径为什么也存在

和 `skcipher` 一样,AEAD 也支持:

- `algif_aead_ops`
- `algif_aead_ops_nokey`

`nokey` 版本的 `sendmsg/sendpage/recvmsg` 会先做一次:

```c
/* kernel/kernel-5.10/crypto/algif_aead.c:L393-401 */
	err = -ENOKEY;
	...
	if (crypto_aead_get_flags(tfm->aead) & CRYPTO_TFM_NEED_KEY)
		goto unlock;
	...
	err = 0;
```

也就是:

> 如果底层 tfm 还标着 `CRYPTO_TFM_NEED_KEY`,那所有真正的数据路径都先拒绝。

这样做的好处是:

- accept 阶段可以尽量宽松
- 真正使用前仍保证没 key 就不能算

---

## 14. 和 `algif_skcipher` 对比,难点到底多在哪

| 维度 | `algif_skcipher` | `algif_aead` |
|---|---|---|
| 输入组成 | 明文/密文 | AAD + 明文/密文 + 可能还有 tag |
| 输出组成 | 与输入等长 | 加密多 tag,解密少 tag |
| 额外 socket 选项 | KEY | KEY + AUTHSIZE + AADASSOCLEN |
| request 类型 | `skcipher_request` | `aead_request` |
| `request_set_*` | 只设 `crypt` | 既设 `crypt` 又设 `ad` |
| 数据布局 | 直接 pull TX 当 src | 要先 copy AAD/payload,再特殊拼 tag |
| 特有错误 | 普通 I/O/crypto 错误 | 还要处理 `-EBADMSG`(tag 失败) |
| 额外 helper | 基本不需要 | 需要 `null_tfm` 做 SG 到 SG 拷贝 |

一句话:

> `skcipher` 是“纯变换”,`aead` 是“带格式约束的变换 + 认证”,所以它必须先把输入输出在 SGL 层面摆成底层 AEAD 能理解的样子。

---

## 15. 用一张时序图把加密 / 解密串起来

### 15.1 AEAD 加密

```text
用户 sendmsg:  AAD || PT
ctx->used = aad + pt
ctx->aead_assoclen = aad
ctx->enc = true

recvmsg:
  outlen = aad + pt + tag
  used   = pt
  af_alg_get_rsgl()            -> RX 目标缓冲
  copy_sgl(TX -> RX, aad+pt)   -> RX 先变成 AAD || PT || [tag space]
  af_alg_pull_tsgl(..., NULL)  -> 消费掉 TX
  aead_request_set_crypt(src=RX, dst=RX, len=pt, iv)
  aead_request_set_ad(aad)
  crypto_aead_encrypt()

结果:
  RX = AAD || CT || Tag
```

### 15.2 AEAD 解密

```text
用户 sendmsg:  AAD || CT || Tag
ctx->used = aad + ct + tag
ctx->aead_assoclen = aad
ctx->enc = false

recvmsg:
  outlen = aad + ct
  used   = ct + tag
  af_alg_get_rsgl()                 -> RX 目标缓冲
  copy_sgl(TX -> RX, aad+ct)        -> RX 先变成 AAD || CT
  af_alg_pull_tsgl(..., dst=tsgl,
                   dst_offset=aad+ct)
                                     -> 只把 tag 从 TX 搬到 areq->tsgl
  chain(RX, tag_sgl)                -> src = [AAD || CT] + [Tag]
  aead_request_set_crypt(src=src, dst=RX, len=ct+tag, iv)
  aead_request_set_ad(aad)
  crypto_aead_decrypt()

结果:
  RX = AAD || PT
  如果 tag 错 -> -EBADMSG
```

---

## 16. 对 x2600 当前系统意味着什么

在你当前 x2600 上,`algif_aead.c` 做完这些前端拼装后,`crypto_alloc_aead("gcm(aes)", ...)` 最终大概率会选到:

- `gcm` template
- `ghash-generic`
- `aes-generic`

也就是说:

1. `algif_aead` 负责把 `AAD/PT/CT/Tag` 摆成 SGL
2. 下层软件 AEAD 实现负责真正的 GHASH + AES CTR/GCM 运算
3. 实际仍然是 CPU 在跑

如果以后你接入了硬件 GCM/CCM/AEAD driver,这份 `algif_aead.c` 完全不用改。

这再次说明:

> `algif_aead.c` 是“用户态接口翻译层”,不是算法实现层。

---

## 17. 最后记忆法

如果只记住 `algif_aead.c` 的三个关键词,就记:

1. **AAD**
2. **Tag**
3. **null_tfm copy**

其中最关键的一句话是:

> `algif_aead.c` 的难点不在“调用 `crypto_aead_encrypt()`”,而在于 **在调用之前,把 AAD / payload / tag 在 scatterlist 里摆成正确的输入输出形状**。

---

## TL;DR

- `algif_aead.c` 是 `AF_ALG` 的 AEAD 适配层
- 它把用户态 `type = "aead"` 的请求翻译成 `aead_request`
- `sendmsg` 只是借 `af_alg_sendmsg()` 累积数据
- 真正复杂度全部在 `_aead_recvmsg()`:
  - 先计算 `AAD/payload/tag` 长度关系
  - 用 `null_tfm` 做 SG→SG 拷贝
  - 加密时把 `AAD||PT` 拷到 RX,再原地加密生成 tag
  - 解密时把 `AAD||CT` 拷到 RX,再把 TX 尾部 tag 单独抽出来链到源 SGL
  - 最终 `aead_request_set_crypt + aead_request_set_ad + crypto_aead_encrypt/decrypt`
- 它比 `algif_skcipher` 复杂,根本原因是 AEAD 既有 AAD 又有 tag,而且加密/解密两边输入输出长度不对称
