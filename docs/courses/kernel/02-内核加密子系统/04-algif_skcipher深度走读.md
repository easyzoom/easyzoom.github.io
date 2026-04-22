---
title: algif_skcipher 深度走读
author: EASYZOOM
date: 2026/04/21 12:00
categories:
 - Linux内核开发
tags:
 - Linux
 - 内核
 - 加密
---

# algif_skcipher 深度走读:`af_alg_sendmsg` + `af_alg_pull_tsgl` + `crypto_skcipher_encrypt` 三件套如何拼起来

> 配套上一篇:(讲 `af_alg.c` 骨架)。
>
> 本文盯死一个目标:**把 `crypto/algif_skcipher.c` 整份文件掰开揉碎,让你看清楚用户态一次 `sendmsg + recvmsg` 的加密请求,是如何经由 `af_alg_sendmsg` 堆积到 TX SGL、再由 `af_alg_pull_tsgl` 转交到 per-request SGL、最终塞进 `crypto_skcipher_encrypt` 的。**
>
> 同步阅读位置:
> - `kernel/kernel-5.10/crypto/algif_skcipher.c`(全文 389 行)
> - `kernel/kernel-5.10/crypto/af_alg.c`
> - `kernel/kernel-5.10/include/crypto/if_alg.h`
> - `kernel/kernel-5.10/include/crypto/skcipher.h`

---

## 0. 文件头注释先定个调

`algif_skcipher.c` 开头 15 行的注释比任何复述都准:

```
 * The kernel maintains two SGLs, the TX SGL and the RX SGL. The TX SGL is
 * filled by user space with the data submitted via sendpage/sendmsg. Filling
 * up the TX SGL does not cause a crypto operation -- the data will only be
 * tracked by the kernel. Upon receipt of one recvmsg call, the caller must
 * provide a buffer which is tracked with the RX SGL.
 *
 * During the processing of the recvmsg operation, the cipher request is
 * allocated and prepared. As part of the recvmsg operation, the processed
 * TX buffers are extracted from the TX SGL into a separate SGL.
```

记住三件事:

1. **sendmsg 只堆数据,不触发运算**——所有明文/密文都先落进 `ctx->tsgl_list`。
2. **recvmsg 才是触发点**——一次 recvmsg == 一次 skcipher 请求。
3. **每次运算都要把本次消费的那段 TX page 从全局 `tsgl_list` "摘下来"变成 per-request SGL**,这个动作就是 `af_alg_pull_tsgl(sk, len, areq->tsgl, 0)`。

整篇文章就是把这三句话展开。

---

## 1. 模块注册:和 `af_alg.c` 的握手

```c
static const struct af_alg_type algif_type_skcipher = {
    .bind         = skcipher_bind,
    .release      = skcipher_release,
    .setkey       = skcipher_setkey,
    .accept       = skcipher_accept_parent,
    .accept_nokey = skcipher_accept_parent_nokey,
    .ops          = &algif_skcipher_ops,
    .ops_nokey    = &algif_skcipher_ops_nokey,
    .name         = "skcipher",
    .owner        = THIS_MODULE
};

static int __init algif_skcipher_init(void)
{
    return af_alg_register_type(&algif_type_skcipher);
}
```

几件事同时发生:

- 把 `"skcipher"` 这个 type 挂到 `af_alg.c` 里的 `alg_types` 链表。以后用户态 `bind` 时 `salg_type == "skcipher"` 就能命中这条。
- `.bind`、`.accept`、`.setkey` 三个函数指针把"类别(skcipher)"落到"具体算法(cbc(aes))"的动作。
- `.ops` 和 `.ops_nokey`:op socket accept 之后真正挂上的 `proto_ops`(对应"有 key / 没 key 两种使用姿态")。

### 1.1 `skcipher_bind`:一行代码完成选 driver

```c
static void *skcipher_bind(const char *name, u32 type, u32 mask)
{
    return crypto_alloc_skcipher(name, type, mask);
}
```

就这一句。**这是"软件 vs 硬件 / cbc(aes) vs xts(aes)" 的所有秘密所在**:

- 调用 `crypto_alloc_skcipher("cbc(aes)", 0, 0)`,内核 Crypto API 会去遍历所有注册的 skcipher 实现,按 `cra_priority` 选最高的。
- 如果系统里只有 `cbc(aes-generic)`,就返回一个指向"cbc template 包裹 aes_generic"的 tfm。
- 如果哪天注册了 `ingenic-aes-cbc`(priority=300),它就会被选上,整个 `algif_skcipher` 的上层代码**一行都不用改**,就获得了硬件加速。

返回的 `void *private` 实际上是 `struct crypto_skcipher *` 指针,保存在 control socket `alg_sock::private`。

### 1.2 `skcipher_accept_parent`:为每个 op socket 建上下文

```c
if (crypto_skcipher_get_flags(tfm) & CRYPTO_TFM_NEED_KEY)
    return -ENOKEY;          /* 没 setkey 不让 accept,走 accept_nokey */
return skcipher_accept_parent_nokey(private, sk);
```

`_nokey` 版本做真正的分配:

```c
ctx = sock_kmalloc(sk, sizeof(*ctx), GFP_KERNEL);
ctx->iv = sock_kmalloc(sk, crypto_skcipher_ivsize(tfm), GFP_KERNEL);
INIT_LIST_HEAD(&ctx->tsgl_list);
crypto_init_wait(&ctx->wait);
ask->private = ctx;                         /* op socket 的私有上下文 */
sk->sk_destruct = skcipher_sock_destruct;
```

**注意:`private` 指针在 control 和 op 上是两种东西**:
- control socket:`alg_sk(control)->private == struct crypto_skcipher *tfm`
- op socket:`alg_sk(op)->private == struct af_alg_ctx *ctx`(指向 TX/RX 状态)
- 但 op 要拿 tfm,它是通过 `ask->parent` 去 control 上取(见所有函数开头那几行 `psk = ask->parent; pask = alg_sk(psk); tfm = pask->private;`)

这就是为什么 tfm(连带 key)**一次设置对所有 op 生效**。

### 1.3 `skcipher_sock_destruct`:反向清理

```c
af_alg_pull_tsgl(sk, ctx->used, NULL, 0);           /* 把全局 tsgl 全 drain 掉 */
sock_kzfree_s(sk, ctx->iv, crypto_skcipher_ivsize(tfm)); /* IV 清零释放 */
sock_kfree_s(sk, ctx, ctx->len);
af_alg_release_parent(sk);                          /* 最后一个 op 落地再放掉 control */
```

`af_alg_pull_tsgl(..., NULL, 0)` 的 `dst=NULL` 代表**不转交,直接释放**——把所有挂在 `ctx->tsgl_list` 上但还没被消费的 page `put_page` 掉。这是 op socket close 时避免泄漏那些"用户已 sendmsg 进来但 recvmsg 还没 drain"的内存的关键。

### 1.4 两套 `proto_ops`:key / nokey

```c
static struct proto_ops algif_skcipher_ops = {
    .sendmsg  = skcipher_sendmsg,
    .sendpage = af_alg_sendpage,      /* 零拷贝路径直接复用 af_alg */
    .recvmsg  = skcipher_recvmsg,
    .poll     = af_alg_poll,
    .release  = af_alg_release,
    ...
};
static struct proto_ops algif_skcipher_ops_nokey = {
    .sendmsg  = skcipher_sendmsg_nokey,   /* 每个都先 skcipher_check_key() */
    .sendpage = skcipher_sendpage_nokey,
    .recvmsg  = skcipher_recvmsg_nokey,
    ...
};
```

`_nokey` 变体的存在原因:Crypto API 允许某些算法(比如 hash-only、或 AEAD 只做验证)在没有 key 时就能 accept。`algif_skcipher` 也复用了这个机制,但每次真正读写之前再调 `skcipher_check_key()` 补检一次——如果 setkey 最终还是没做,就返回 `-ENOKEY`。

---

## 2. 发送侧:`skcipher_sendmsg` —— 把数据堆进 TX SGL

```c
static int skcipher_sendmsg(struct socket *sock, struct msghdr *msg, size_t size)
{
    struct crypto_skcipher *tfm = pask->private;
    unsigned ivsize = crypto_skcipher_ivsize(tfm);
    return af_alg_sendmsg(sock, msg, size, ivsize);
}
```

整个 skcipher 自己的发送路径就这 5 行。它把一切交给 `af_alg_sendmsg`,只负责**告诉通用层:本算法的 IV 长度是多少(用于校验用户传进来的 cmsg ALG_SET_IV 大小)**。

### 2.1 `af_alg_sendmsg` 做的事(快速回顾上一篇)

```
1) 解析 msg 的 cmsg: ALG_SET_OP / ALG_SET_IV / ALG_SET_AEAD_ASSOCLEN
      -> 写进 ctx->enc / ctx->iv / ctx->aead_assoclen
2) 循环 while (size):
   a) ctx->merge == 1 -> 向最后一页剩余空间追加 memcpy_from_msg,更新 sg->length
   b) 否则:
      - 反压: af_alg_writable() 不够就 af_alg_wait_for_wmem()
      - af_alg_alloc_tsgl(): 需要时新开一个 af_alg_tsgl 节点,挂到 ctx->tsgl_list
      - for 每个新页:
           alloc_page(GFP_KERNEL); memcpy_from_msg(page, iov, plen);
           sg_assign_page(); sg[i].length = plen;
           ctx->used += plen;
3) ctx->more = msg->msg_flags & MSG_MORE
4) af_alg_data_wakeup(): 叫醒可能在 poll/recv 上睡的进程
```

关键产物:**一组 `struct page` + 与之对应的 SG entry**,全部挂在 `ctx->tsgl_list` 上,累计字节数记在 `ctx->used`。

### 2.2 IV 的"粘性语义"

`ALG_SET_IV` 一次写入 `ctx->iv` 后,会**一直用到下次 SET_IV**。这个细节被很多使用者忽略——它带来两个重要含义:

- 如果你做一次加密后想继续"接着上次的 IV 链"做,不需要再塞 IV,直接 `sendmsg(plain) + recvmsg(cipher)` 就行(相当于 CTR/CBC 的连续流模式)。
- 但是!对 CBC 这种算法,每次 `crypto_skcipher_encrypt` 返回后,底层 cbc template 会**把最后一个密文块写回 req->iv**(即 `ctx->iv`),自然就变成了"下一次加密的起始 IV"。对用户态来说,这意味着 IV 是一个受内核 driver 更新的状态,不是由你单方面决定。

---

## 3. 接收侧:`skcipher_recvmsg` —— 所有魔法集中在这里

```c
static int skcipher_recvmsg(struct socket *sock, struct msghdr *msg,
                            size_t ignored, int flags)
{
    lock_sock(sk);
    while (msg_data_left(msg)) {             /* 用户可能提供多段 iovec 一次读 */
        int err = _skcipher_recvmsg(sock, msg, ignored, flags);
        if (err <= 0) { ... goto out; }
        ret += err;
    }
out:
    af_alg_wmem_wakeup(sk);                   /* 收完让出 TX 写空间,叫醒 sendmsg */
    release_sock(sk);
    return ret;
}
```

外层只是在 `msg_iter` 剩余数据上循环调用 `_skcipher_recvmsg`,每次返回本次加密的字节数,累加返回。真正的动作在 `_skcipher_recvmsg` 里。

下面我们**一段一段**地把 `_skcipher_recvmsg` 看完。

### 3.1 准备 tfm 和常量

```c
struct crypto_skcipher *tfm = pask->private;
unsigned int bs = crypto_skcipher_chunksize(tfm);
```

`chunksize` 是"**下一个请求要处理的字节数必须是这个值的整数倍**"的约束:

- AES 纯 cipher:`chunksize = 16`(AES 块大小)
- XTS:`chunksize = 16`(XTS 在 skcipher 层每次以 16B 为单位)
- CTR:`chunksize = 16`(内部 counter 按块推进)
- ChaCha20:`chunksize = 1`(流密码,逐字节)

等会你会看到它用来决定"残留字节怎么处理"。

### 3.2 等数据够

```c
if (!ctx->init || (ctx->more && ctx->used < bs)) {
    err = af_alg_wait_for_data(sk, flags, bs);
    if (err) return err;
}
```

只有两种情况需要等:

1. **`!ctx->init`**:控制消息(OP/IV)都没送过,不知道加解密、没 IV,等。
2. **`ctx->more && ctx->used < bs`**:用户表示"后面还有"(上次 sendmsg 带了 `MSG_MORE`),并且目前积累的字节数还不够凑一个 chunksize,等。

否则(`!ctx->more` 或 `ctx->used >= bs`)直接继续 —— 哪怕 used=0 也没关系,后面会自然返回 0 或处理已有数据。

### 3.3 分配 areq(请求句柄)

```c
areq = af_alg_alloc_areq(sk, sizeof(struct af_alg_async_req) +
                             crypto_skcipher_reqsize(tfm));
```

这里就是上一篇提到的"一次分配得到 areq + request + request 私有 ctx"。布局是:

```
+---------------------------+
| struct af_alg_async_req   |  (含 cra_u.skcipher_req —— 这是一个嵌入的 skcipher_request)
+---------------------------+
| tfm 实现的 private ctx    |  <—— 尾部额外 crypto_skcipher_reqsize(tfm) 字节
|  (e.g. cbc_request_ctx,  |
|   里面存 walk 状态等)     |
+---------------------------+
```

`skcipher_reqsize` 是算法实现告诉上层"我的 request 后面还要这么多字节 private"。把它和 areq 一起分配,避免每次加解密两次 kmalloc。

### 3.4 把用户输出 iovec 转成 RX SGL

```c
err = af_alg_get_rsgl(sk, msg, flags, areq, ctx->used, &len);
```

第 5 个参数传 `ctx->used`,意思是"**最多处理这么多字节**"(TX 里堆了多少,RX 最多就装多少)。`af_alg_get_rsgl` 内部会:

- 一次次 `iov_iter_get_pages()` pin 住用户页,填进 `af_alg_sgl`;
- 用 `af_alg_link_sg()` 把多段 rsgl 链成一条大 SGL;
- `atomic_add(err, &ctx->rcvused)` 计量 RX 反压;
- 返回 `*outlen = len`:**实际能往用户缓冲区写多少字节**(等于 "min(用户 iov 总长, ctx->used, 剩余 RX 预算)")。

出来之后 `len` 就是本次请求**打算处理的字节数的上限**。

### 3.5 按 chunksize 取整:保证 skcipher 约束

```c
/* If more buffers are to be expected to be processed, process only
 * full block size buffers.
 */
if (ctx->more || len < ctx->used)
    len -= len % bs;
```

两个条件:

- `ctx->more`:用户还会送,剩下的残渣留给下一轮 recvmsg/crypto 请求。
- `len < ctx->used`:用户 RX 缓冲不够大,只能处理其中一部分,剩的也留下。

只要属于这两种,就把 `len` 向下对齐到 `bs` 的整数倍。例如 AES,`len = 1000` → `len = 992`,剩下 8 字节原封不动保留在 `ctx->tsgl_list` 里,等下一次 recvmsg 再攒齐。

只有一种情况**允许不对齐**:`!ctx->more && len == ctx->used`——"用户明确说没后续了,而且我的 RX 缓冲足够装下全部"。这一刻算法实现自己(cbc/xts 等)必须能处理整条 SGL(通常这也要求调用方送进来的总量本来就是 `bs` 的整数倍,否则底层会返回 `-EINVAL`)。

这一段是 af_alg 流式处理的核心约束,如果你调用 skcipher 得到过 "返回字节数比我想的少" 的奇怪现象,根源几乎都在这里。

### 3.6 构造 per-request TX SGL —— 魔法时刻

```c
areq->tsgl_entries = af_alg_count_tsgl(sk, len, 0);
if (!areq->tsgl_entries) areq->tsgl_entries = 1;
areq->tsgl = sock_kmalloc(sk, array_size(sizeof(*areq->tsgl),
                                         areq->tsgl_entries),
                          GFP_KERNEL);
sg_init_table(areq->tsgl, areq->tsgl_entries);

af_alg_pull_tsgl(sk, len, areq->tsgl, 0);
```

逐步拆开:

1. **`af_alg_count_tsgl(sk, len, 0)`**:数一下"从全局 `ctx->tsgl_list` 的头开始,累计 `len` 字节需要占多少个 SG entry"。注意 SG 不是"页数",而是"对 page 的引用 + 偏移 + 长度"。一个 page 里可能被拆成多个 SG(虽然 af_alg 很少这样做),或者多个小段被合并成一个 SG。
2. **`sock_kmalloc` + `sg_init_table`**:给这次请求单独开一个 SG 数组,大小刚好装得下。
3. **`af_alg_pull_tsgl(sk, len, areq->tsgl, 0)`**:这是 `af_alg.c` 里"把全局 SGL 的前 `len` 字节迁移到 `areq->tsgl`"的那个函数。关键动作在上一篇讲过,这里复述一下所有权转移的语义:

   ```
   迁移前:
     ctx->tsgl_list:  [tsgl_A: sg0..sg126] -> [tsgl_B: sg0..sg30]
                       每个 sg 都 sg_get_page() 指向 alloc_page 得到的内核页
   迁移 len 字节后(假设 tsgl_A 整条 + tsgl_B 前若干 sg 被吃掉):
     ctx->tsgl_list:  [tsgl_B: sg_n..sg30]  (剩下的继续留着)
     areq->tsgl:      sg0..sg(k-1)           (pull 出来的这段)
     全局页引用计数:   **page 从 ctx 转移到 areq,没有 put_page 也没有新 get_page**
                      (代码里直接 sg_set_page 到新 sg,源 sg 的 page 也清空)
     ctx->used      -= len
   ```

   这是个**零拷贝的所有权搬家**:page 不动,指针挪了个地方。后面 `af_alg_free_resources(areq)` 里再遍历 `areq->tsgl` 做 `put_page`,把页放掉。

   迁移完后,`ctx->used` 减少,若整个 tsgl 页空了还会把 `af_alg_tsgl` 节点本身也 `sock_kfree_s` 掉。迁移末尾会做:

   ```c
   if (!ctx->used) ctx->merge = 0;
   ctx->init = ctx->more;    /* 如果数据吃完且 more==false,则 init 归零,下一次 recvmsg 会要求重新设置 OP/IV */
   ```

### 3.7 组装 skcipher_request

```c
skcipher_request_set_tfm(&areq->cra_u.skcipher_req, tfm);
skcipher_request_set_crypt(&areq->cra_u.skcipher_req,
                           areq->tsgl,              /* src: 刚 pull 出来的 TX SGL */
                           areq->first_rsgl.sgl.sg, /* dst: RX SGL(用户页) */
                           len,                     /* 处理字节数 */
                           ctx->iv);                /* IV —— 底层 driver 可能会覆写 */
```

两点值得强调:

- **src 和 dst 是不同 SGL**:源是"内核自己 alloc_page 的 TX 页",目标是"pin 住的用户页"。所以 skcipher 底层是**out-of-place 加密**,不改用户送进来的明文(也不改 TX 页内容,但我们反正马上就要释放它们了)。
- **`ctx->iv` 直接被 driver 引用**:CBC/CTR 的内部实现里会 `memcpy` 到自己的 walk 状态,完事再更新回 `req->iv`。所以同一个 op socket 连续多次 recvmsg,IV 是"随算法状态一起流动"的,这正是前文"粘性 IV"的实现机制。

### 3.8 两种提交路径:同步 / AIO

#### (a) 同步路径

```c
skcipher_request_set_callback(&areq->cra_u.skcipher_req,
                              CRYPTO_TFM_REQ_MAY_SLEEP |
                              CRYPTO_TFM_REQ_MAY_BACKLOG,
                              crypto_req_done, &ctx->wait);
err = crypto_wait_req(ctx->enc ?
    crypto_skcipher_encrypt(&areq->cra_u.skcipher_req) :
    crypto_skcipher_decrypt(&areq->cra_u.skcipher_req),
    &ctx->wait);
```

- `CRYPTO_TFM_REQ_MAY_SLEEP`:允许底层 driver 在处理中睡眠(比如纯软件路径里 `cond_resched`,或者硬件 driver 里等 DMA/中断)。
- `CRYPTO_TFM_REQ_MAY_BACKLOG`:允许硬件驱动把请求排队,必要时返回 `-EBUSY`/`-EAGAIN`。
- `crypto_req_done` + `ctx->wait`:标准等待模式。`crypto_skcipher_encrypt` 可能:
  - 直接同步返回 `0`(软件实现):`crypto_wait_req` 不睡直接返回;
  - 返回 `-EINPROGRESS`(硬件):`crypto_wait_req` 在 `ctx->wait.completion` 上睡,底层完成时 `crypto_req_done` 调 `complete_all`。
  - 返回 `-EBUSY`(硬件 backlog 满):`crypto_wait_req` 也会 wait on completion,内核会在 slot 腾出来时继续。
- 返回值写进 `err`,最终 `free` 路径会 `af_alg_free_resources(areq)` 释放。

#### (b) AIO 路径

```c
if (msg->msg_iocb && !is_sync_kiocb(msg->msg_iocb)) {
    sock_hold(sk);                    /* 保活 socket,等异步完成 */
    areq->iocb = msg->msg_iocb;
    areq->outlen = len;               /* AIO callback 要用 */
    skcipher_request_set_callback(...,
                                  CRYPTO_TFM_REQ_MAY_SLEEP,
                                  af_alg_async_cb, areq);
    err = ctx->enc ? encrypt(...) : decrypt(...);
    if (err == -EINPROGRESS)
        return -EIOCBQUEUED;          /* 告诉上层:请挂起,稍后 ki_complete */
    sock_put(sk);                     /* 没真的异步进就回滚 hold */
}
```

- 注意这里**不用 `CRYPTO_TFM_REQ_MAY_BACKLOG`**:AIO 场景不想被 backlog 拖住,宁可直接 `-EBUSY` 让用户重试。
- 回调换成 `af_alg_async_cb`,那个函数会负责 `af_alg_free_resources` + `sock_put` + `iocb->ki_complete(iocb, resultlen, 0)`。
- 返回 `-EIOCBQUEUED` 是 AIO 的标准语义——外层 `skcipher_recvmsg` 看到这个就会直接 break 不继续循环(见 3.9)。

### 3.9 外层循环对 AIO 的特殊处理

```c
while (msg_data_left(msg)) {
    int err = _skcipher_recvmsg(sock, msg, ignored, flags);
    if (err <= 0) {
        if (err == -EIOCBQUEUED || !ret) ret = err;
        goto out;
    }
    ret += err;
}
```

- 正常同步路径:每轮 `_skcipher_recvmsg` 返回本次处理的字节数 `len`(>0),累加后继续下一段 iov。
- 一旦某一轮返回 `-EIOCBQUEUED`,说明 AIO 已经排队——这时候**已经有一个请求在飞,不能再发第二个**(会破坏 IV 的顺序性和 TX SGL 的语义),外层直接 break 返回 `-EIOCBQUEUED`。

这就是注释里说的 "we can only handle one AIO request" 的由来。

### 3.10 错误 / 完成路径

```c
free:
    af_alg_free_resources(areq);
    return err ? err : len;
```

`af_alg_free_resources` 会:

1. 释放所有 RX SGL(`put_page` pin 住的用户页);
2. 遍历 `areq->tsgl` 把 TX 页 `put_page`(就是之前 pull 过来的那些);
3. `sock_kfree_s(sk, areq, areq->areqlen)` 释放请求结构本身;
4. 减少 `ctx->rcvused` 计量。

注意**同步路径在这里释放**,AIO 路径在 `af_alg_async_cb` 里释放(早早 `return -EIOCBQUEUED` 跳过了这段)。

外层 `skcipher_recvmsg` 最后再调 `af_alg_wmem_wakeup(sk)`,把可能挂在 `af_alg_wait_for_wmem` 上的 sendmsg 唤醒。

---

## 4. 关联三件套:用一张时序图把全局串起来

```
用户态                                 内核态 / algif_skcipher + af_alg              Crypto API 栈
-------                                 ------------------------------              -------------
cs = socket(AF_ALG)                    alg_create()
bind(cs, {"skcipher","cbc(aes)"})      alg_bind() -> skcipher_bind()                crypto_alloc_skcipher("cbc(aes)")
                                                                                     -> 选 cbc + aes-generic, 得 tfm
setsockopt(ALG_SET_KEY, key)           alg_setsockopt() -> skcipher_setkey()        crypto_skcipher_setkey(tfm, key)
ops = accept(cs)                       alg_accept() -> skcipher_accept_parent()
                                       分配 af_alg_ctx、IV 缓冲
                                       op->ops = algif_skcipher_ops

sendmsg(ops, cmsg={OP=ENC,IV=...},     skcipher_sendmsg()
         iov=plain[4096], MSG_MORE)       -> af_alg_sendmsg(sock, msg, 4096, 16)
                                             解析 cmsg: ctx->enc=1, memcpy ctx->iv
                                             循环: alloc_page + memcpy_from_msg
                                             把 16 个 page 挂进 ctx->tsgl_list[0]
                                             ctx->used = 4096; ctx->more = 1
sendmsg(ops, iov=plain[4096])          同上 -> ctx->used = 8192; ctx->more = 0(未带 MSG_MORE)

recvmsg(ops, iov=cipher[8192])         skcipher_recvmsg()
                                         lock_sock(sk)
                                         _skcipher_recvmsg():
                                           wait_for_data -> 立即返回(数据够,!more)
                                           alloc_areq(sizeof(...) + reqsize)
                                           af_alg_get_rsgl() -> pin 用户 cipher[] 8192B
                                             len = 8192
                                           !more && len==used  -> 不做 bs 对齐
                                           count_tsgl = N
                                           sock_kmalloc(N*sizeof(sg))
                                           af_alg_pull_tsgl(sk, 8192, areq->tsgl)    <=== 这里!
                                             page 从 ctx->tsgl_list 搬到 areq->tsgl
                                             ctx->used -= 8192 = 0
                                           skcipher_request_set_tfm(tfm)
                                           skcipher_request_set_crypt(
                                               src=areq->tsgl,
                                               dst=areq->first_rsgl.sgl.sg,
                                               len=8192,
                                               iv=ctx->iv)
                                           set_callback(crypto_req_done, &ctx->wait)
                                           crypto_skcipher_encrypt(req)   ----------> cbc_encrypt() [crypto/cbc.c]
                                                                                       walk = skcipher_walk_virt(req)
                                                                                       while (nbytes) {
                                                                                           aes_encrypt(ctx,ot,it^iv)
                                                                                             [aes_generic.c: crypto_aes_encrypt]
                                                                                           iv = ot
                                                                                           advance walk
                                                                                       }
                                           同步返回 0
                                           -> crypto_wait_req(0, wait) 直接过
                                           af_alg_free_resources(areq):
                                               put_page(tx pages)
                                               put_page(rx pages)
                                               sock_kfree_s(areq)
                                           return 8192
                                         ret += 8192 -> msg_data_left==0 退出
                                         af_alg_wmem_wakeup(sk)
                                         release_sock(sk)
                                         return 8192
                                                                             [cipher[8192] 已被 dst SGL 原地填入密文]
```

---

## 5. 几个容易翻车的细节

### 5.1 "IV 必须每轮重发?"——不必

`ctx->iv` 在 accept 时分配并清零,`ALG_SET_IV` cmsg 只是覆盖它。即便后续 sendmsg 不带任何 cmsg,只要 `ctx->init` 为 true,`_skcipher_recvmsg` 就会直接用 `ctx->iv`。真正要注意的是:**如果底层 driver 修改了 IV(CBC 一定会),你连续发起下一段加密时用的其实是"刚算完那块的 IV"**,这是正确的流式语义,但和"每段独立加密"的用法不兼容,需要显式再 `ALG_SET_IV` 一次才能归位。

### 5.2 sendmsg 的 `MSG_MORE` 语义

- 带 `MSG_MORE`:`ctx->more = 1`,告诉 recvmsg 侧"后面还有,先等着或者只处理整 chunksize 的前缀"。
- 不带 `MSG_MORE`:`ctx->more = 0`,recvmsg 可以消费所有累计数据,哪怕不是 chunksize 整数倍(交给底层检查和报错)。
- 上一轮 `ctx->more = 0` 且 `ctx->init` 还在但 `ctx->used == 0` 的状态下,再发空的 cmsg 不带 `MSG_MORE` 会触发内核 `pr_info_once("%s sent an empty control message without MSG_MORE.\n", ...)`——提示用户态用法不对,但不致命。

### 5.3 chunksize 残留字节"消失了"?

看 3.5:`len -= len % bs` 之后,`af_alg_pull_tsgl` 只搬走 `len` 字节,余下的还挂在 `ctx->tsgl_list`。对用户态表现是:

```c
sendmsg(plain[1000], MSG_MORE);          // used=1000
recvmsg(cipher, maxlen=1000);            // 返回 992(AES, bs=16)
// 此时 ctx->used = 8,tsgl 里还有一个 sg 指向某个 page 的 offset=992..1000
sendmsg(plain[16]);                      // 不带 MSG_MORE;used=24
recvmsg(cipher, maxlen=24);              // 返回 24
```

用户态必须明白"一次 recvmsg 返回值可能小于缓冲区长度",不能假设一定会全部处理。

### 5.4 RX SGL 是用户页 pin 住的 —— 别在并发里一边加密一边改

`af_alg_get_rsgl -> iov_iter_get_pages` 会 pin 住页,加密结果直接写到这些物理页上。用户态在 recvmsg 返回前读写这段内存会看到部分更新/未初始化内容,不要这么做。

### 5.5 `-EBUSY` vs `-EAGAIN` vs `-EIOCBQUEUED`

- 同步路径用 `MAY_BACKLOG`,硬件 driver 就算满也会"排队+完成后回调",`crypto_wait_req` 等得到结果,正常返回 0;
- AIO 路径**不带 MAY_BACKLOG**,driver 满了会同步返回 `-EBUSY` / `-EAGAIN`,外层直接传给用户态;
- AIO 成功排队时返回 `-EINPROGRESS`,`_skcipher_recvmsg` 把它翻译成 `-EIOCBQUEUED` 让 VFS AIO 层识别。

这三者不能混用,别在用户态写 "if errno == EBUSY retry" 然后又用 io_submit。

### 5.6 单 AIO in-flight 限制

注释里已经明说:**一个 op socket 在同一时刻只允许一个 AIO 请求在飞**。想并发,要么开多个 op socket(并发 accept 就行,control 是共享的,key 也共享),要么改用同步 + 线程池。原因就是 IV 和 TX SGL 都是 socket 级状态,多个请求排队会乱。

---

## 6. `af_alg_pull_tsgl` 的所有权语义总结(重点)

这是整份文件最容易看糊涂的一段,单独拎出来强调:

```c
void af_alg_pull_tsgl(struct sock *sk, size_t used,
                      struct scatterlist *dst, size_t dst_offset);
```

三种调用姿态:

| 调用点 | dst | 含义 |
|---|---|---|
| `_skcipher_recvmsg` 正常流程 | `areq->tsgl` | **把 `used` 字节从全局 tsgl_list "搬家"到 areq->tsgl。page 引用计数不变,最终由 af_alg_free_resources 释放。** |
| `skcipher_sock_destruct`(socket 关闭) | `NULL` | **直接丢弃剩余 `ctx->used` 字节,每个 page 立即 `put_page`。** |
| `aead` 类似用法 | `areq->tsgl` + dst_offset | AEAD 要把 AAD 区段空出来,给 dst_offset 让出前面的空间 |

关键不变量:

- 任何时刻,**一个 page 要么挂在 `ctx->tsgl_list` 里,要么挂在某个 `areq->tsgl` 里,要么已经 `put_page`**。不会两头都挂。
- `ctx->used` 精确反映"当前还在 `ctx->tsgl_list` 里 **没有被 pull 走** 的字节数"。

如果哪天你去移植一个硬件驱动,看到 `af_alg_pull_tsgl` 之后页居然被再动了,那就是 bug。

---

## 7. 对比:同一套骨架下的 `algif_aead` 有何不同

为了让你更体会"`af_alg.c` 的通用层确实是复用的",顺手对比一下 AEAD:

| 动作 | skcipher | aead |
|---|---|---|
| bind | `crypto_alloc_skcipher()` | `crypto_alloc_aead()` |
| setkey | `crypto_skcipher_setkey` | `crypto_aead_setkey` |
| setauthsize | 无 | `crypto_aead_setauthsize`(tag 长度) |
| sendmsg | `af_alg_sendmsg(sock, msg, size, ivsize)` | 同样复用,多一个 `aead_assoclen` |
| recvmsg 的 pull_tsgl | src 直接从头开始 | 需要把 AAD 和 plain/cipher **一起**挑出来,src/dst 有 `offset` 设定 |
| request 类型 | `skcipher_request` | `aead_request`(多 `aead_request_set_ad`) |
| chunksize 对齐 | `crypto_skcipher_chunksize(tfm)` | 由 aead 决定;GCM 通常不是块对齐,只要 AAD+plain+tag 凑齐就行 |

整体骨架和本文讲的几乎一致,只是"如何把 TX 里的字节切成 AAD + 数据 + tag"那里复杂一些。想深挖 aead 时对着这张表看 `algif_aead.c` 会比零基础看轻松很多。

---

## 8. 一条最短可运行的用户态示例

给你一段 C 代码,跑一下就能看到本文讲的每一步在实际跑:

```c
#include <sys/socket.h>
#include <linux/if_alg.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

int main(void) {
    int cs = socket(AF_ALG, SOCK_SEQPACKET, 0);
    struct sockaddr_alg sa = {
        .salg_family = AF_ALG,
        .salg_type   = "skcipher",
        .salg_name   = "cbc(aes)",
    };
    bind(cs, (struct sockaddr *)&sa, sizeof(sa));

    unsigned char key[16] = {0};
    setsockopt(cs, SOL_ALG, ALG_SET_KEY, key, sizeof(key));

    int ops = accept(cs, NULL, 0);

    unsigned char iv[16] = {0};
    unsigned char plain[32] = "hello world! this is afalg tst.";  /* 31+NUL -> 32 */
    unsigned char cipher[32] = {0};

    /* sendmsg with cmsg: OP=ENCRYPT + IV + plain */
    struct {
        struct cmsghdr hdr;
        int op;
    } cop = { .hdr = { .cmsg_len = CMSG_LEN(sizeof(int)),
                       .cmsg_level = SOL_ALG,
                       .cmsg_type = ALG_SET_OP },
              .op = ALG_OP_ENCRYPT };
    struct {
        struct cmsghdr hdr;
        struct af_alg_iv iv;
        unsigned char iv_data[16];
    } civ = { .hdr = { .cmsg_len = CMSG_LEN(sizeof(struct af_alg_iv) + 16),
                       .cmsg_level = SOL_ALG,
                       .cmsg_type = ALG_SET_IV },
              .iv.ivlen = 16 };
    memcpy(civ.iv_data, iv, 16);

    char cmsg_buf[CMSG_SPACE(sizeof(int)) + CMSG_SPACE(sizeof(struct af_alg_iv) + 16)];
    memset(cmsg_buf, 0, sizeof(cmsg_buf));
    memcpy(cmsg_buf, &cop, sizeof(cop));
    memcpy(cmsg_buf + CMSG_SPACE(sizeof(int)), &civ, sizeof(civ));

    struct iovec iov = { plain, sizeof(plain) };
    struct msghdr msg = {
        .msg_iov = &iov, .msg_iovlen = 1,
        .msg_control = cmsg_buf, .msg_controllen = sizeof(cmsg_buf),
    };
    sendmsg(ops, &msg, 0);

    read(ops, cipher, sizeof(cipher));     /* 等价 recvmsg 简化版 */

    for (int i = 0; i < 32; ++i) printf("%02x", cipher[i]);
    puts("");
    return 0;
}
```

你在 x2600 上跑这段,内核里走过的就是本文讲的整套流程。把输出和 `openssl enc -aes-128-cbc -K 00..00 -iv 00..00` 对比应当一致。

---

## 9. 什么时候你该修这个文件?

几乎没有。它的设计目标就是"成为稳定的玻璃":

- **加新算法**:不用改它,直接注册到 Crypto API 就会被选上。
- **硬件加速**:一样不用动,在 `drivers/crypto/` 加 driver 并把 `cra_priority` 抬高即可。
- **新增 SET_* cmsg**:可能要在 `af_alg.c` 的 `af_alg_cmsg_send` 里加分支,而不是改本文件。
- **AEAD 风格的扩展**:改 `algif_aead.c`,别碰 `algif_skcipher.c`。

真正要改这个文件的场景大概只有两类:
1. 发现某个边界条件(比如 chunksize 取整、`ctx->more` 判定)的上游 bug 修复要 backport;
2. 你自己在做安全加固/审计,比如把 `CRYPTO_TFM_REQ_MAY_SLEEP` 关掉,或者给某些算法加额外的权限检查。

---

**TL;DR 再精简一次**:

- `skcipher_sendmsg` → `af_alg_sendmsg`:**把用户明文按 PAGE 粒度搬进内核 TX 页,挂到 `ctx->tsgl_list`,记账 `ctx->used`。不跑加密。**
- `skcipher_recvmsg` → `_skcipher_recvmsg`:**pin 住用户 RX 页变 RX SGL;用 `af_alg_pull_tsgl` 把 TX 页按 `len` 零拷贝搬到 `areq->tsgl`;`skcipher_request_set_crypt(src=TX, dst=RX, iv=ctx->iv)`;同步 `crypto_skcipher_encrypt` + `crypto_wait_req` 跑完;`af_alg_free_resources` 放掉所有 page 和 areq。**
- `crypto_skcipher_encrypt` 内部:按 `cra_priority` 选到的 driver(当前 x2600 上 = `cbc + aes_generic`)在 CPU 上跑完查表 + 异或,写结果到 dst SGL,即用户页。

一次用户态 `sendmsg + recvmsg` 的加密,就是把数据"灌进内核页 → 搬给 request → 交给 driver 原地写用户页 → 放回页"这样一趟链条,`af_alg_pull_tsgl` 是其中最关键的"所有权转移"开关。
