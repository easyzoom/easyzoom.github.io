---
title: AF_ALG 架构与实现解析
author: EASYZOOM
date: 2026/04/21 12:00
categories:
 - Linux内核开发
tags:
 - Linux
 - 内核
 - 加密
---

# AF_ALG 架构与实现解析(kernel/kernel-5.10/crypto/af_alg.c)

> 目标:把 `af_alg.c` 这一份文件讲清楚 —— 它在内核 Crypto 栈里的位置、核心数据结构、socket 生命周期、数据通路(TX/RX SGL)、与具体算法模块(`algif_skcipher / algif_hash / algif_aead / algif_rng`)的协作方式,以及在 x2600 这种无硬件加密引擎平台上的实际意义。
>
> 同步阅读位置:
> - `kernel/kernel-5.10/crypto/af_alg.c`
> - `kernel/kernel-5.10/include/crypto/if_alg.h`
> - `kernel/kernel-5.10/crypto/algif_skcipher.c` / `algif_hash.c` / `algif_aead.c` / `algif_rng.c`

---

## 0. 一句话定位

`af_alg.c` 是 Linux 内核 Crypto API 的**用户空间 socket 前端**。它本身**不做任何加解密运算**,只做两件事:

1. 把内核里已注册的对称加密(`skcipher`)、哈希(`hash`)、AEAD、RNG 等算法以 `AF_ALG` 协议族 socket 的形式暴露给用户态;
2. 管理用户态提交的明文/密文缓冲区(TX/RX SGL)、IV、AAD 等上下文,把请求转成内核 Crypto API 能理解的 `scatterlist + request`,并把结果回传给用户态。

真正的算法实现由 `crypto/aes_generic.c`、`crypto/sha256_generic.c`、`crypto/gcm.c`、`drivers/crypto/*` 等模块提供。对 x2600 这套当前仅启用软件 crypto 的 SDK 来说,`af_alg` 的下层最终全部落在 CPU 上的 `-generic` 实现。

---

## 1. 整体分层

```
+---------------------------------------------------------------+
|                       User Space                              |
|   socket(AF_ALG) + bind(sockaddr_alg) + setsockopt(KEY/IV)    |
|   accept() -> opfd; sendmsg/recvmsg/sendpage on opfd          |
+---------------------------------------------------------------+
                              |  syscalls (net/socket.c dispatch)
                              v
+---------------------------------------------------------------+
|  af_alg.c (this file)                                         |
|  - PF_ALG proto family / proto_ops                            |
|  - alg_bind: 按 salg_type 选 algif-*,按 salg_name 绑定算法   |
|  - alg_accept: 创建 op socket,切到具体 algif 的 proto_ops    |
|  - alg_setsockopt: SET_KEY / SET_AEAD_AUTHSIZE / SET_DRBG_ENTROPY
|  - 通用 TX/RX SGL、CMSG、poll、AIO 回调等共用原语            |
+---------------------------------------------------------------+
                              |  af_alg_register_type()
                              v
+---------------------------------------------------------------+
|  algif_skcipher.c / algif_hash.c / algif_aead.c / algif_rng.c |
|  - 具体 "algif-*" type,实现 bind/setkey/accept/ops           |
|  - 把 af_alg_ctx + TX/RX SGL 翻译成 crypto_skcipher_request 等|
+---------------------------------------------------------------+
                              |  crypto_alloc_*() / crypto_*_encrypt()
                              v
+---------------------------------------------------------------+
|  Linux Crypto API 核心(crypto/api.c, algapi.c, ...)          |
|  - template: cbc()/ctr()/gcm()/hmac()/... 组合出复合算法      |
|  - 按 cra_priority 在多个实现里选一个                          |
+---------------------------------------------------------------+
                              |
                              v
+----------------------------------+   +------------------------+
|  纯软件实现                      |   |  硬件驱动(本 SDK 未启用)
|  crypto/aes_generic.c            |   |  drivers/crypto/*      |
|  crypto/sha256_generic.c         |   |  (Ingenic AES/SHA/...) |
|  crypto/gcm.c, cbc.c, cmac.c ... |   |                        |
+----------------------------------+   +------------------------+
```

`af_alg.c` 只是最上面第二层,它和"软件还是硬件算"没有关系 —— 它只把用户态请求送进 Crypto API,具体落到哪个 driver 由优先级决定。

---

## 2. 关键数据结构速览

全部在 `include/crypto/if_alg.h`。

### 2.1 `struct alg_sock`(每个 AF_ALG socket 的控制块)

```c
struct alg_sock {
    struct sock sk;                 /* 必须是首成员,方便 sk <-> alg_sk 转换 */
    struct sock *parent;            /* op socket 指向 control socket */
    atomic_t refcnt;
    atomic_t nokey_refcnt;
    const struct af_alg_type *type; /* skcipher / hash / aead / rng */
    void *private;                  /* type->bind() 返回的私有上下文 */
};
```

- 一个绑定后的 "control socket" 通过 `accept()` 衍生出一组 "op socket";所有 op socket 的 `parent` 指向 control socket。
- `refcnt` 追踪 op socket 数量,最后一个 op 释放时才会真正释放底层 tfm。
- `nokey_refcnt` 用于"没有 key 也允许部分操作"的算法(比如 AEAD 在特定场景)。

### 2.2 `struct af_alg_type`(一类算法的注册表项)

```c
struct af_alg_type {
    void *(*bind)(const char *name, u32 type, u32 mask);
    void (*release)(void *private);
    int (*setkey)(void *private, const u8 *key, unsigned int keylen);
    int (*setentropy)(void *private, sockptr_t entropy, unsigned int len);
    int (*accept)(void *private, struct sock *sk);
    int (*accept_nokey)(void *private, struct sock *sk);
    int (*setauthsize)(void *private, unsigned int authsize);
    struct proto_ops *ops;         /* op socket 用的 ops(sendmsg/recvmsg/...)*/
    struct proto_ops *ops_nokey;   /* 无 key 场景下的 ops */
    struct module *owner;
    char name[14];                 /* "skcipher" / "hash" / "aead" / "rng" */
};
```

`algif_skcipher.c` 等四个文件各自定义一个这样的结构体,通过 `af_alg_register_type()` 注册进来。

### 2.3 `struct af_alg_ctx`(socket 存活期内的请求上下文)

```c
struct af_alg_ctx {
    struct list_head tsgl_list;   /* 发送数据链式存放的多个 TX SGL */
    void *iv;
    size_t aead_assoclen;
    struct crypto_wait wait;
    size_t used;                  /* 已放进 tsgl_list 的总字节数 */
    atomic_t rcvused;             /* 已占用的 RX 缓冲 */
    bool more;                    /* MSG_MORE,还有后续数据 */
    bool merge;                   /* 新数据可否并入最后一个 SG */
    bool enc;                     /* 本次 op 是加密还是解密 */
    bool init;                    /* 控制消息是否已送达 */
    unsigned int len;
};
```

这个结构体是连接"用户态陆续 `sendmsg` 过来的数据"和"算法模块一次 `encrypt/decrypt` 消费"的核心桥梁。

### 2.4 TX/RX SGL

- **TX SGL**(用户 → 内核,明文/密文 + AAD):`struct af_alg_tsgl`。
  - 每个 `af_alg_tsgl` 是一块动态分配的"SG 数组页",最多 `MAX_SGL_ENTS`(约 `(4096 - header)/sizeof(sg) - 1 ≈ 126` 个 SG)。
  - 多个 `af_alg_tsgl` 之间通过 `sg_chain()` 串起来,挂在 `ctx->tsgl_list`。
  - 一次 sendmsg 写入会先尝试合并进最后一页(`ctx->merge`),空间不够再 `alloc_page(GFP_KERNEL)` 新开页,数据 `memcpy_from_msg` 进去。
- **RX SGL**(内核 → 用户,输出结果):`struct af_alg_rsgl`。
  - 每个 rsgl 背后是 `af_alg_sgl{ sg[ALG_MAX_PAGES+1], pages[ALG_MAX_PAGES] }`,由 `iov_iter_get_pages()` 把用户态 iovec 的用户页 pin 住。
  - 多个 rsgl 组成链表,通过 `af_alg_link_sg()` 用 `sg_chain` 串成一条 SGL 给底层算法。

这种设计让 **RX 侧对用户缓冲区直接做 DMA / 原地加密成为可能**(至少在 API 层是可以的,具体是否发生 DMA 取决于下层 driver)。TX 侧则必须 copy 进内核页,以避免用户态在加密途中修改明文。

### 2.5 `struct af_alg_async_req`(单次加解密请求的句柄)

```c
struct af_alg_async_req {
    struct kiocb *iocb;                /* 若是 AIO,指向用户 iocb */
    struct sock *sk;
    struct af_alg_rsgl first_rsgl;     /* 内联第一个 rsgl,免一次 kmalloc */
    struct af_alg_rsgl *last_rsgl;
    struct list_head rsgl_list;

    struct scatterlist *tsgl;          /* 从 ctx->tsgl_list 摘出来的专属 TX SGL */
    unsigned int tsgl_entries;

    unsigned int outlen;
    unsigned int areqlen;

    union {
        struct aead_request aead_req;
        struct skcipher_request skcipher_req;
    } cra_u;
    /* request 的 private ctx 紧跟在这个结构体后面,尾部扩展 */
};
```

每一次 `recvmsg`(= 触发一次加解密)会分配一个 areq,完成或出错时通过 `af_alg_async_cb()` / `af_alg_free_resources()` 释放。

---

## 3. 协议注册与 socket 生命周期

### 3.1 模块初始化

```c
module_init(af_alg_init);
static int __init af_alg_init(void)
{
    int err = proto_register(&alg_proto, 0);        // 注册 proto
    if (err) return err;
    err = sock_register(&alg_family);               // 注册 PF_ALG 协议族
    ...
}
```

- `alg_proto`:内存计量用的 `struct proto`,`obj_size = sizeof(struct alg_sock)`,这样 `sk_alloc(PF_ALG, ..., &alg_proto)` 一次分配既能得到 `sock` 又能得到 `alg_sock`。
- `alg_family.create = alg_create`:用户态 `socket(AF_ALG, SOCK_SEQPACKET, 0)` 最终走到它。

`alg_create` 只做两件事:
1. 要求 `SOCK_SEQPACKET`,`protocol == 0`;
2. `sk_alloc()` + `sock->ops = &alg_proto_ops`(control socket 的最小 ops),绑定析构 `alg_sock_destruct`。

### 3.2 control socket 的最小 ops

```c
static const struct proto_ops alg_proto_ops = {
    .family     = PF_ALG,
    .connect    = sock_no_connect,   /* 不支持 */
    ...
    .sendmsg    = sock_no_sendmsg,   /* control socket 自己不能发数据 */
    .recvmsg    = sock_no_recvmsg,
    .bind       = alg_bind,
    .release    = af_alg_release,
    .setsockopt = alg_setsockopt,
    .accept     = alg_accept,
};
```

要点:**control socket 只能 bind/setsockopt/accept**,不能直接读写数据。只有 `accept()` 产出的 op socket 才能 sendmsg/recvmsg/sendpage,并且其 `ops` 会被替换为具体 algif 模块提供的 `proto_ops`(见 §3.5)。

### 3.3 `alg_bind()`:按 type+name 绑定一个算法实例

关键流程:

```c
type = alg_get_type(sa->salg_type);                /* 例如 "skcipher"         */
if (PTR_ERR(type) == -ENOENT) {
    request_module("algif-%s", sa->salg_type);     /* 按需 modprobe algif-skcipher */
    type = alg_get_type(sa->salg_type);
}
if (IS_ERR(type)) return PTR_ERR(type);

private = type->bind(sa->salg_name, feat, mask);   /* 例:crypto_alloc_skcipher("cbc(aes)", ...) */
if (IS_ERR(private)) { module_put(...); return; }

lock_sock(sk);
if (atomic_read(&ask->refcnt))                      /* 已经 accept 过不允许重绑 */
    goto unlock;
swap(ask->type, type);                              /* 原子替换 */
swap(ask->private, private);
release_sock(sk);

alg_do_release(type, private);                      /* 如果被替换下来的旧值非空,释放它 */
```

几点要注意:

- `sa->salg_type`:类别名,就是 `af_alg_type.name` —— `"skcipher"/"hash"/"aead"/"rng"`。
- `sa->salg_name`:算法名,交给 Crypto API,例如 `"cbc(aes)"`、`"hmac(sha256)"`、`"gcm(aes)"`、`"stdrng"`。
- `request_module("algif-%s", ...)`:支持把 `algif_skcipher` 等做成 `.ko` 模块,按需自动加载。
- `atomic_read(&ask->refcnt)`:一旦已经有 op socket accept 过,control 不允许再次 bind(语义锁定)。
- 对标志位 `salg_feat/salg_mask` 只允许 `CRYPTO_ALG_KERN_DRIVER_ONLY`(用户态只能控制这一个维度,防止乱填 bit)。

### 3.4 `alg_setsockopt()`:下发 KEY / AUTHSIZE / ENTROPY

```c
case ALG_SET_KEY:          err = alg_setkey(sk, optval, optlen);       break;
case ALG_SET_AEAD_AUTHSIZE: err = type->setauthsize(ask->private, optlen); break;
case ALG_SET_DRBG_ENTROPY: err = type->setentropy(ask->private, optval, optlen);
```

两点共性:
- 必须在 `SS_CONNECTED` 之前(即 accept 之前)设置,否则返回 `-EBUSY/-ENOPROTOOPT`。
- `alg_setkey` 先 `sock_kmalloc()` 拷贝用户 key,调用 `type->setkey` 后用 `sock_kzfree_s()` **清零释放**,避免 key 泄漏在 slab 里。

### 3.5 `alg_accept() / af_alg_accept()`:派生 op socket

```c
sk2 = sk_alloc(sock_net(sk), PF_ALG, GFP_KERNEL, &alg_proto, kern);
sock_init_data(newsock, sk2);
newsock->ops = type->ops;              /* 切到 algif-* 提供的真正 ops */
err = type->accept(ask->private, sk2); /* 由 algif 分配它自己的 ctx 挂到 sk2 */
if (err == -ENOKEY && type->accept_nokey) err = type->accept_nokey(...);

atomic_inc(&ask->refcnt);              /* 第一个 op 让 control 获得一次引用 */
alg_sk(sk2)->parent = sk;
alg_sk(sk2)->type   = type;
newsock->state = SS_CONNECTED;
if (nokey) newsock->ops = type->ops_nokey;
```

含义:

- **每个 op socket 都代表一次/一路并发的加解密会话**,它有自己的 `af_alg_ctx`(`sk2->private`)。
- op socket 的 `ops` 指向算法专属的 `proto_ops`(比如 `algif_skcipher_ops`),所以它的 `sendmsg/recvmsg/sendpage` 调用的是 algif 模块的实现;但这些实现内部会复用 `af_alg_sendmsg/af_alg_sendpage/af_alg_poll/af_alg_wait_for_data` 等共用原语。
- 所有 op socket 共享 control 里的那一份 `type->bind()` 私有上下文(通常就是 `struct crypto_skcipher_tfm*` 之类),因此 key、authsize 只在 control 上设置一次即可对所有 op 生效。

### 3.6 `af_alg_release_parent()`:op socket 最后释放时解引用 control

```c
if (atomic_dec_and_test(&ask->refcnt)) sock_put(sk);
```

保证 control socket 活到"最后一个 op socket 释放后"才真正销毁。

---

## 4. TX 侧:`sendmsg/sendpage` 如何堆数据

### 4.1 CMSG 控制消息

`af_alg_cmsg_send()` 识别三类 `SOL_ALG` cmsg:

| `cmsg_type` | 作用 | 使用者 |
|---|---|---|
| `ALG_SET_IV` | 本次操作的 IV(CBC/CTR/GCM 都要用) | skcipher / aead |
| `ALG_SET_OP` | `ALG_OP_ENCRYPT` / `ALG_OP_DECRYPT` | skcipher / aead |
| `ALG_SET_AEAD_ASSOCLEN` | AAD 长度(AEAD 专用) | aead |

这三项会落进 `af_alg_control con`,再由 `af_alg_sendmsg` 写入 `ctx->enc` / `ctx->iv` / `ctx->aead_assoclen`。

### 4.2 `af_alg_sendmsg()` 的逻辑骨架

精简版:

```c
if (msg->msg_controllen) af_alg_cmsg_send(...);      /* 吸收 IV/OP/ASSOCLEN */

lock_sock(sk);
if (ctx->init && !ctx->more) {                        /* 上一轮已经结束但没消费 */
    if (ctx->used) err = -EINVAL; goto unlock;
}
ctx->init = true;
if (init) { ctx->enc = enc; memcpy(ctx->iv, con.iv->iv, ivsize); ctx->aead_assoclen = ...; }

while (size) {
    if (ctx->merge) {
        /* 在最后一页的末尾继续追加,直到本页写满 */
        sg->length += len; ctx->used += len; ...
        continue;
    }
    if (!af_alg_writable(sk)) af_alg_wait_for_wmem(sk, flags); /* SNDBUF 反压 */
    af_alg_alloc_tsgl(sk);                            /* 需要时新开一个 tsgl 节点 */
    do {
        sg_assign_page(sg+i, alloc_page(GFP_KERNEL));
        memcpy_from_msg(page_address(...), msg, plen);
        sg[i].length = plen;
        ctx->used  += plen;
    } while (len && sgl->cur < MAX_SGL_ENTS);
    ...
    ctx->merge = plen & (PAGE_SIZE - 1);              /* 尾部还有空闲就允许合并 */
}
ctx->more = msg->msg_flags & MSG_MORE;
af_alg_data_wakeup(sk);                               /* 通知 recv 侧可以读了 */
```

要点:

- **reactive 反压**:`af_alg_writable()` 判断基于 `sk->sk_sndbuf`(默认取自 `net.core.wmem_default`,按 `PAGE_MASK` 向下对齐),超过则让出睡眠 —— 防止用户态疯狂灌数据 OOM。
- **合并优化(ctx->merge)**:如果上一轮 sendmsg 结束时最后一页还有剩余空间(没满 PAGE),下一次 sendmsg 可直接往那个空闲处 copy,避免为每次小包都 `alloc_page`。
- **MSG_MORE 语义**:置位表示"还会继续送",不要立即消费;最后一次不带 `MSG_MORE` 或带空 cmsg 结尾触发本轮结束(`ctx->more = 0`,`recvmsg` 随后可以开跑)。

### 4.3 `af_alg_sendpage()` 的差异

- **零拷贝的 TX 路径**:用户态用 `sendfile()` 或 `splice()` 时,走 `sendpage`,内核直接 `get_page(page)` 把该 page 挂到 TX SGL,不再 copy。
- 要求 `!ctx->more && ctx->used` 不并存(不能在未消费状态下又突然切到 sendpage)。
- `ctx->merge = 0` —— sendpage 过来的页属于别人,不能往里追加 sendmsg 的数据。

---

## 5. RX 侧:`recvmsg` 如何触发一次加解密

`af_alg.c` 本身**不直接**提供 `recvmsg` 实现(交给 algif_* 模块),但它提供了若干关键原语:

### 5.1 `af_alg_alloc_areq()` — 准备一次请求

为 `struct af_alg_async_req + struct <alg>_request + request 私有尾部` 一次性分配,免得三次 kmalloc。

### 5.2 `af_alg_get_rsgl()` — 把用户态输出 iovec 变成 SGL

逐段调 `af_alg_make_sg()`:

```c
n = iov_iter_get_pages(iter, sgl->pages, len, ALG_MAX_PAGES, &off);
/* pin 住用户页,填进 sgl->sg[],链尾 sg_mark_end() */
```

多段之间用 `af_alg_link_sg()` 做 `sg_chain()`,最终把所有 rsgl 串成一条**逻辑上的大 SGL**,直接作为算法输出缓冲。底层 skcipher/aead 处理完后就是"用户态页里本身就是明文/密文",避免再 copy。

`atomic_add(err, &ctx->rcvused)` 计量已占用的 RX 缓冲,配合 `af_alg_readable()` 做反压(超过 `sk_rcvbuf` 就停手)。

### 5.3 `af_alg_pull_tsgl()` — 从 TX 链表里"扣"出本次要消费的数据

调用方(algif_skcipher/aead)给出 "本次处理 @used 字节",并可选提供 `dst` SGL 把这些 page 重新指派过去(移交所有权)。处理完后:

- `ctx->used -= plen`
- 如 SG 空了就 `put_page(page)` + `sg_assign_page(NULL)`
- 整个 `af_alg_tsgl` 节点全空就从 `tsgl_list` 里摘下来并 `sock_kfree_s()`
- 最终若 `ctx->used == 0`,重置 `ctx->merge = 0`,`ctx->init = ctx->more`(下一轮重新开始)

### 5.4 同步 vs. 异步(AIO)

- **同步**:algif 模块调用 `crypto_wait_req(crypto_skcipher_encrypt(&req), &ctx->wait)`,当前线程在 `crypto_wait.completion` 上睡,底层 driver(可能是硬件)完成时 `crypto_req_done` 唤醒。
- **异步(AIO)**:algif 调用 `areq->iocb` 非空的路径,把 `areq` 作为 request 的 data,回调指针 `af_alg_async_cb`:

```c
void af_alg_async_cb(struct crypto_async_request *_req, int err)
{
    struct af_alg_async_req *areq = _req->data;
    unsigned int resultlen = areq->outlen;
    af_alg_free_resources(areq);
    sock_put(sk);
    iocb->ki_complete(iocb, err ? err : (int)resultlen, 0);  /* 回到用户态 AIO 完成 */
}
```

`af_alg_async_cb` 统一负责 **释放 SGL、放 page、减 socket 引用、通知 AIO**,是所有 algif 异步路径共用的回调。

### 5.5 `af_alg_poll()` — epoll/select 语义

```c
if (!ctx->more || ctx->used) mask |= EPOLLIN | EPOLLRDNORM;  /* 可 recv */
if (af_alg_writable(sk))     mask |= EPOLLOUT | ...;         /* 可 send  */
```

"可读"定义为:**要么本轮结束了(`!more`),要么还有未消费的数据(`used > 0`)**。和常规 socket 不太一样,因为 recvmsg 是"拉动 crypto 运算"而不是"读缓冲"。

---

## 6. 与 `algif_*` 模块的分工(以 skcipher 为例)

回到顶层视角,让 `cbc(aes)` 的一次加密走完全程:

```
用户态:
  cs = socket(AF_ALG); bind(cs, {"skcipher","cbc(aes)"});
  setsockopt(cs, SOL_ALG, ALG_SET_KEY, key, 16);
  ops = accept(cs);
  sendmsg(ops, {CMSG(ALG_SET_OP=ENCRYPT), CMSG(ALG_SET_IV), iov=plain});
  recvmsg(ops, iov=cipher);

内核态:
  alg_bind()         -> type="skcipher" -> algif_skcipher_bind("cbc(aes)")
                        -> crypto_alloc_skcipher("cbc(aes)", 0, 0)   [**]
  alg_setsockopt()   -> algif_skcipher setkey -> crypto_skcipher_setkey()
  alg_accept()       -> algif_skcipher accept -> 给 sk2->private 挂 af_alg_ctx,
                        ops 切到 algif_skcipher_ops
  ops->sendmsg = algif_skcipher_sendmsg
                     -> af_alg_sendmsg(...)  [本文件]
                        -> 累积数据到 ctx->tsgl_list, 设 enc/iv
  ops->recvmsg = algif_skcipher_recvmsg
                     -> af_alg_alloc_areq()
                     -> af_alg_get_rsgl()   把用户 iov 变成 RX SGL
                     -> 把 ctx->tsgl_list 拼成 TX SGL 填到 skcipher_request
                     -> crypto_skcipher_encrypt(&req)
                          \__ 选中 cbc(aes-generic):cbc.c + aes_generic.c 在 CPU 上算
                     -> 同步 wait / 异步 af_alg_async_cb
                     -> copy/pin 完成,返回字节数给用户
```

`[**]` 这一步就是"软件还是硬件"的分叉点。内核会在所有注册了 `cbc(aes)` 的实现中按 `cra_priority` 选最高的。在当前 x2600 上,`/proc/crypto` 只能看到 `aes-generic`,所以走的是软件路径。

`algif_hash.c` / `algif_aead.c` / `algif_rng.c` 也遵循同样的分工:

| 模块 | type 名 | 典型 name | 主要 request 类型 |
|---|---|---|---|
| `algif_skcipher.c` | `"skcipher"` | `cbc(aes)`, `ctr(aes)`, `xts(aes)` | `skcipher_request` |
| `algif_aead.c`     | `"aead"`    | `gcm(aes)`, `ccm(aes)`, `authenc(hmac(sha256),cbc(aes))` | `aead_request` |
| `algif_hash.c`     | `"hash"`    | `sha256`, `hmac(sha256)`, `cmac(aes)` | `ahash_request` |
| `algif_rng.c`      | `"rng"`    | `stdrng`, `jitterentropy_rng` | `rng` API |

它们共同调用 `af_alg_register_type()` 把自己挂到 `alg_types` 链表,让 `alg_bind()` 能按 `salg_type` 找到。

---

## 7. 并发、锁、反压、安全

### 7.1 锁

- **`alg_types_sem`**(`rwsem`):保护 `alg_types` 链表(algif 注册/查询)。读多写少,用 `down_read/down_write`。
- **`lock_sock(sk)`**:保护 `alg_sock` / `af_alg_ctx` 上所有可变字段。`alg_bind / alg_setsockopt / alg_accept / af_alg_sendmsg / af_alg_sendpage` 都走这把锁。
- **`ask->refcnt` / `ask->nokey_refcnt`**:原子计数,跨 socket 生命周期同步父子关系。

### 7.2 反压

- TX 侧:`af_alg_writable()` = `af_alg_sndbuf() >= PAGE_SIZE`;否则 `af_alg_wait_for_wmem()` 用 `SOCKWQ_ASYNC_NOSPACE` 睡,等 `af_alg_wmem_wakeup()`(由 recvmsg 消费 TX 后触发)。
- RX 侧:`af_alg_readable()` = `af_alg_rcvbuf() >= PAGE_SIZE`;`af_alg_get_rsgl()` 循环里检查,防止一次 recv 占用过多内核页。
- `af_alg_wait_for_data()` 等的是 `ctx->init && (!ctx->more || (min && ctx->used >= min))`,支持"部分读"(比如哈希的 update/final 风格)。

### 7.3 安全

- **key 清零**:`alg_setkey` 用 `sock_kmalloc` + `sock_kzfree_s()`,离开函数前把 key 内存清零,降低 slab 内残留风险。
- **控制位白名单**:`CRYPTO_ALG_KERN_DRIVER_ONLY` 是唯一允许在 `salg_feat/salg_mask` 打的 bit,其他一律 `-EINVAL`。
- **socket-level 内存计量**:所有附加分配走 `sock_kmalloc(sk, ...)` / `sock_kfree_s(sk, ...)`,统计到 `sk->sk_omem_alloc`,受 `sysctl_optmem_max` 约束,防止单个 socket 把内核撑爆。
- **TX 必须 copy,RX 可原地**:TX 从用户态 `memcpy_from_msg` 进内核页;RX 用 `iov_iter_get_pages` pin 用户页原地加/解密。这样即便攻击者在加密过程中改用户态内存,也影响不了 TX 侧的明文一致性。
- **没有 sendmsg 自己 recvmsg 的路径**:`alg_proto_ops` 里 control socket 的 `.sendmsg/.recvmsg = sock_no_*`,防止忘了 accept 直接写数据。

### 7.4 引用计数模型

- **control socket**:`sk_alloc()` 产生,`sk->sk_destruct = alg_sock_destruct`(调 `alg_do_release` 放掉 `type->release(private)` + `module_put`)。
- **第一个 op accept 成功** → `atomic_inc(&ask->refcnt)` 同时 `sock_hold(sk)`,锚定 control 不会先于 op 死。
- **每个 op close** → `af_alg_release_parent()` 递减 refcnt,最后一个才 `sock_put(sk)`。
- **module**:`alg_get_type()` 里 `try_module_get(type->owner)`,`alg_do_release()` 里 `module_put(type->owner)`,保证 `algif_skcipher.ko` 在使用期间不会被卸载。

---

## 8. 把本文件读懂的一个函数级索引

| 函数 | 类别 | 一句话说明 |
|---|---|---|
| `alg_get_type` | 注册表 | 按 `name` 查 `af_alg_type`,并 `try_module_get` |
| `af_alg_register_type` / `unregister_type` | 注册表 | algif-* 模块注册/注销入口 |
| `alg_create` | proto family | `socket(AF_ALG)` 的创建回调 |
| `alg_bind` | control socket | 绑定算法,调用 `type->bind` |
| `alg_setsockopt` / `alg_setkey` | control socket | 下发 KEY/AUTHSIZE/ENTROPY |
| `alg_accept` / `af_alg_accept` | op socket | 派生 op socket,`ops` 切到 algif 的实现 |
| `af_alg_release_parent` / `alg_sock_destruct` | 生命周期 | 引用计数释放链 |
| `af_alg_cmsg_send` | TX | 解析 `ALG_SET_IV/OP/ASSOCLEN` |
| `af_alg_alloc_tsgl` / `af_alg_count_tsgl` / `af_alg_pull_tsgl` | TX | TX SGL 分配/计数/消费 |
| `af_alg_sendmsg` / `af_alg_sendpage` | TX | 通用发送路径(copy / 零拷贝) |
| `af_alg_wait_for_wmem` / `af_alg_wmem_wakeup` | TX | 发送缓冲反压与唤醒 |
| `af_alg_wait_for_data` / `af_alg_data_wakeup` | RX | 接收侧等数据/通知 |
| `af_alg_make_sg` / `af_alg_free_sg` / `af_alg_link_sg` | RX | 用户 iovec ↔ scatterlist |
| `af_alg_alloc_areq` / `af_alg_free_resources` | 请求 | 单次请求的生命周期 |
| `af_alg_get_rsgl` | RX | 把用户输出缓冲组装成 RX SGL |
| `af_alg_async_cb` | AIO | 异步完成后的统一清理 + `ki_complete` |
| `af_alg_poll` | 事件 | 给 epoll/select 的 EPOLLIN/EPOLLOUT 判定 |
| `af_alg_init` / `af_alg_exit` | 模块 | `proto_register` + `sock_register`(PF_ALG) |

---

## 9. 对 x2600 这套 SDK 的具体意义

回到这台板子的现状(参考 `/proc/crypto` 里全是 `-generic`、没有 Ingenic `drivers/crypto/*`):

1. **`af_alg` 模块本身大概率是编进内核的**(依赖它的 `cryptsetup`、`iwd`、部分 TLS 内核态和一些用户态加密工具会 `socket(AF_ALG)`)。如果不用这些工具,可以把 `CONFIG_CRYPTO_USER_API*` 关掉来省内核 size,但不会影响内核自身的 `dm-crypt`、内核 TLS 等,它们走的是内核内 crypto API,**不经过 af_alg**。
2. **走 af_alg 的用户态加解密 ≈ 走 aes-generic / sha256-generic 等软件实现**,完全是 CPU 在算,对 XBurst MIPS 核来说吞吐就是几十 MB/s 量级,大量数据密集型用例(全盘加密、网络流量加密)会明显吃 CPU。
3. **若要提速**,方向不在 `af_alg.c`,而在下层:
   - 在 `kernel/kernel-5.10/drivers/crypto/` 增加 Ingenic AES/SHA 引擎的平台驱动(`platform_driver + DT` + DMA + clk),`crypto_register_skcipher`/`crypto_register_ahash` 时把 `cra_priority` 设到 300+。
   - 保持 `af_alg.c` 和 `algif_*` 一行不动,`/proc/crypto` 里就会出现 `driver : ingenic-aes`、`ingenic-sha256` 等条目并被优先选中,用户态无感升级。
4. **安全与审计**:因为 AF_ALG 允许**任何有网络 socket 权限的用户态进程**使用内核加密原语,若安全策略收紧,可用 `CONFIG_CRYPTO_USER_API_ENABLE_OBSOLETE` 限制、或者干脆禁用这个协议族(不启用 `CRYPTO_USER_API`),看产品定位决定。

---

## 10. 延伸阅读

- `crypto/algif_skcipher.c`:看一下 `_sendmsg/_recvmsg` 怎么复用本文件里的原语,尤其 `af_alg_pull_tsgl()` 把 TX 数据原地喂给 skcipher 的那段。
- `Documentation/crypto/userspace-if.rst`(上游内核):官方用法与 cmsg 格式。
- `tools/crypto/` 或 `libkcapi`:常见用户态包装。
- `crypto/api.c` 里 `crypto_alg_match / crypto_alg_lookup`:`cra_priority` 如何决定最终绑定哪个 driver。

---

**TL;DR**:`af_alg.c` 是 PF_ALG 的骨架 —— 负责 socket 生命周期、TX/RX SGL 管理、反压与 AIO 回调;真正的算法实现由 `algif_*` 模块 + 内核 Crypto API 栈提供。它只是"路由器",软/硬件加密由下层决定。在当前 x2600 SDK 上,它最终把请求全部转给了 `*-generic` 的纯 CPU 软件实现。
