---
title: /proc/crypto 工作原理
author: EASYZOOM
date: 2026/04/21 12:00
categories:
 - Linux内核开发
tags:
 - Linux
 - 内核
 - 加密
---

# `/proc/crypto` 里到底是什么?是怎么被"查"出来的?

> 配套阅读:
>
> 同步源码位置:
> - `kernel/kernel-5.10/crypto/proc.c`(这一文件总共 101 行,很小)
> - `kernel/kernel-5.10/crypto/api.c`(维护 `crypto_alg_list`)
> - `kernel/kernel-5.10/crypto/algapi.c`(`crypto_register_alg` 入口)
> - `kernel/kernel-5.10/crypto/skcipher.c / shash.c / ahash.c / aead.c / rng.c / akcipher.c / kpp.c / scompress.c / acompress.c`(各类算法的 `show` 回调)

---

## 0. 一句话定位

`cat /proc/crypto` 看到的是 **Linux 内核 Crypto API 子系统内部那张全局 "已注册算法表" 的一份人类可读快照**。

- **不是硬件探测结果**
- **不是 `.config` 里 `CONFIG_CRYPTO_*` 的列表**
- **不是 `/sys` 或某个 kobject 的反射**
- **也和 `AF_ALG` / `algif_*` 无关**(它们是这张表的消费者,不是生产者)

它就是 `crypto/` 目录下所有算法模块在 `module_init` 里调用 `crypto_register_alg()` / `crypto_register_skcipher()` / `crypto_register_shash()` 等函数时,**把自己挂到一条内核链表上**。每次你 `cat /proc/crypto`,内核遍历这条链表,把每个节点打印成一段多行文字。

---

## 1. 你看到的那段文本,字段来自哪里

先回顾你之前看到的一条典型条目:

```
name         : aes
driver       : aes-generic
module       : kernel
priority     : 100
refcnt       : 2
selftest     : passed
internal     : no
type         : cipher
blocksize    : 16
min keysize  : 16
max keysize  : 32
```

每一行在源码里是一句 `seq_printf`,完全对得上:

```c
/* kernel/kernel-5.10/crypto/proc.c:L36-50 */
static int c_show(struct seq_file *m, void *p)
{
	struct crypto_alg *alg = list_entry(p, struct crypto_alg, cra_list);

	seq_printf(m, "name         : %s\n", alg->cra_name);
	seq_printf(m, "driver       : %s\n", alg->cra_driver_name);
	seq_printf(m, "module       : %s\n", module_name(alg->cra_module));
	seq_printf(m, "priority     : %d\n", alg->cra_priority);
	seq_printf(m, "refcnt       : %u\n", refcount_read(&alg->cra_refcnt));
	seq_printf(m, "selftest     : %s\n",
		   (alg->cra_flags & CRYPTO_ALG_TESTED) ?
		   "passed" : "unknown");
	seq_printf(m, "internal     : %s\n",
		   (alg->cra_flags & CRYPTO_ALG_INTERNAL) ?
		   "yes" : "no");
```

### 1.1 前 7 个字段(所有算法共有)

| 输出字段 | 源字段 | 含义 |
|---|---|---|
| `name` | `alg->cra_name` | **用户可见名**,一般是"抽象算法名",比如 `"aes"`、`"cbc(aes)"`、`"sha256"`、`"stdrng"`。同一个 name 可以有多个实现(软件 + 硬件),互相按 priority 竞争。 |
| `driver` | `alg->cra_driver_name` | **实现标识**,带平台/实现后缀,比如 `"aes-generic"`、`"cbc(aes-generic)"`、`"drbg_nopr_hmac_sha256"`、硬件实现可能是 `"ingenic-aes"`。同一个 driver 名全系统唯一。 |
| `module` | `module_name(alg->cra_module)` | 这个实现所在内核模块。**内建进 vmlinuz 的算法显示成 `kernel`**(如你现在看到的);编成 `.ko` 的会显示成对应模块名,比如 `aes_generic`、`sha256_generic`。 |
| `priority` | `alg->cra_priority` | 同名算法多个实现的选择依据。**数字越大越优先**。惯例:通用软件 `100`;架构特化软件 `200~300`;硬件 `300+`。 |
| `refcnt` | `refcount_read(&alg->cra_refcnt)` | 当前有多少个 tfm(或模板组合)引用着它。**这个字段每次 `cat` 都可能不一样**,比如你 openssl 正在跑 AES,就会临时涨一些。 |
| `selftest` | `cra_flags & CRYPTO_ALG_TESTED` | 是否通过了 `crypto/testmgr.c` 里的自测向量。测试管理器在注册时会异步跑一遍 KAT(Known Answer Test),通过才置位。未测的显示 `unknown`。 |
| `internal` | `cra_flags & CRYPTO_ALG_INTERNAL` | 是否是"只给模板内部用"的中间算法(不打算被用户直接 `crypto_alloc_*` 使用)。大多数常见算法是 `no`。 |

### 1.2 后面的字段(按 type 分叉)

这里开始就"因类而异":

```c
/* kernel/kernel-5.10/crypto/proc.c:L58-78 */
	if (alg->cra_type && alg->cra_type->show) {
		alg->cra_type->show(m, alg);
		goto out;
	}

	switch (alg->cra_flags & CRYPTO_ALG_TYPE_MASK) {
	case CRYPTO_ALG_TYPE_CIPHER:
		seq_printf(m, "type         : cipher\n");
		seq_printf(m, "blocksize    : %u\n", alg->cra_blocksize);
		seq_printf(m, "min keysize  : %u\n",
					alg->cra_cipher.cia_min_keysize);
		seq_printf(m, "max keysize  : %u\n",
					alg->cra_cipher.cia_max_keysize);
		break;
	case CRYPTO_ALG_TYPE_COMPRESS:
		seq_printf(m, "type         : compression\n");
		break;
	default:
		seq_printf(m, "type         : unknown\n");
		break;
	}
```

逻辑优先级:

1. **优先走 `alg->cra_type->show(m, alg)` 回调**(针对用 `crypto_register_skcipher/shash/aead/...` 注册的"新 API"算法)
2. **没有回调再走那个 `switch`**(只处理最老式的 `cipher` 和 `compression`)

各类 `show` 回调输出的额外字段如下(这就是为什么同一个 `/proc/crypto` 里不同条目看起来格式不一样):

| type | `show` 函数 | 额外字段 | 来源文件 |
|---|---|---|---|
| `skcipher` | `crypto_skcipher_show` | `async`, `blocksize`, `min keysize`, `max keysize`, `ivsize`, `chunksize`, `walksize` | `crypto/skcipher.c:692` |
| `shash` | `crypto_shash_show` | `blocksize`, `digestsize` | `crypto/shash.c:485` |
| `ahash` | `crypto_ahash_show` | `async`, `blocksize`, `digestsize` | `crypto/ahash.c:522` |
| `aead` | `crypto_aead_show` | `async`, `blocksize`, `ivsize`, `maxauthsize`, `geniv` | `crypto/aead.c:171` |
| `rng` | `crypto_rng_show` | `seedsize` | `crypto/rng.c:91` |
| `akcipher` | `crypto_akcipher_show` | (打印 "akcipher" 标识) | `crypto/akcipher.c:45` |
| `kpp` | `crypto_kpp_show` | (打印 "kpp" 标识) | `crypto/kpp.c:44` |
| `scomp` | `crypto_scomp_show` | (打印 "scomp" 标识) | `crypto/scompress.c:63` |
| `acomp` | `crypto_acomp_show` | (打印 "acomp" 标识) | `crypto/acompress.c` |
| `cipher`(老式) | (走 switch) | `blocksize`, `min keysize`, `max keysize` | `proto.c` 本身 |
| `compression`(老式) | (走 switch) | 无额外字段 | `proto.c` 本身 |

拿你 `/proc/crypto` 里的几条回对照:

- **`aes`** → type 是 `cipher`(老式 API,没有 `cra_type`),所以走 switch,打印 `blocksize / min keysize / max keysize`。
- **`sha256`** → type 是 `shash`,走 `crypto_shash_show`,所以打印 `blocksize / digestsize`,**没有** `async` 字段。
- **`cmac(aes)`** → 也是 `shash`(cmac 是以 shash 包装的 MAC),所以字段是 `blocksize=16 / digestsize=16`。
- **`ecb(cipher_null)`** → type 是 `skcipher`,所以有 `async / blocksize / min/max keysize / ivsize / chunksize / walksize`。
- **`stdrng`** → type 是 `rng`,只有 `seedsize` 这一个额外字段。
- **`zstd-scomp`** → type 是 `scomp`,简单打 "scomp";`zstd-generic` 没有 `cra_type`,落到 switch 的 `CRYPTO_ALG_TYPE_COMPRESS` 分支,打成 "compression"。

看清楚这一点你就能**光凭 `/proc/crypto` 的字段组合反推一个算法是走哪种 API 注册**——这在调试时很有用。

---

## 2. 这张"算法表"的数据结构

### 2.1 链表主干:`crypto_alg_list`

```c
/* kernel/kernel-5.10/crypto/api.c:L25-28 */
LIST_HEAD(crypto_alg_list);
EXPORT_SYMBOL_GPL(crypto_alg_list);
DECLARE_RWSEM(crypto_alg_sem);
EXPORT_SYMBOL_GPL(crypto_alg_sem);
```

- `crypto_alg_list`:全局 `struct list_head`。整个内核里**所有**已注册的加密算法实例(包括 larval,见 §3)都挂在这条链表上。
- `crypto_alg_sem`:读写信号量,保护这条链表。读(查找、`/proc/crypto` 遍历)用 `down_read`,写(register/unregister)用 `down_write`。

### 2.2 链表节点:`struct crypto_alg`

每个算法实现注册时提供一个 `struct crypto_alg`(或被包在 `skcipher_alg`、`shash_alg` 等外壳里),关键字段:

```c
struct crypto_alg {
    struct list_head cra_list;         /* 挂进 crypto_alg_list 的钩子 */
    struct list_head cra_users;

    u32  cra_flags;                    /* type mask + ASYNC/INTERNAL/TESTED/LARVAL ... */
    unsigned int cra_blocksize;
    unsigned int cra_ctxsize;
    unsigned int cra_alignmask;

    int cra_priority;                  /* /proc/crypto 里 priority */
    refcount_t cra_refcnt;             /* /proc/crypto 里 refcnt */

    char cra_name[CRYPTO_MAX_ALG_NAME];        /* /proc/crypto 里 name */
    char cra_driver_name[CRYPTO_MAX_ALG_NAME]; /* /proc/crypto 里 driver */

    const struct crypto_type *cra_type;        /* 决定 show/report/init_tfm 行为 */

    union { ... } cra_u;                        /* 老式 API:cipher / compress 联合体 */

    int  (*cra_init)(struct crypto_tfm *tfm);
    void (*cra_exit)(struct crypto_tfm *tfm);
    void (*cra_destroy)(struct crypto_alg *alg);

    struct module *cra_module;                 /* /proc/crypto 里 module */
};
```

`cra_flags & CRYPTO_ALG_TYPE_MASK` 决定"大类":
- `CRYPTO_ALG_TYPE_CIPHER / COMPRESS`:老式 API 原生就有的,没有 `cra_type`。
- `CRYPTO_ALG_TYPE_SKCIPHER / SHASH / AHASH / AEAD / RNG / AKCIPHER / KPP / SCOMP / ACOMP`:都通过 `cra_type` 回调打印(`cra_type->show`)。

### 2.3 `cra_type`:多态分派

```c
struct crypto_type {
    unsigned int (*extsize)(struct crypto_alg *alg);
    int  (*init_tfm)(struct crypto_tfm *tfm);
    void (*show)(struct seq_file *m, struct crypto_alg *alg);    /* ← /proc/crypto 用这个 */
    int  (*report)(struct sk_buff *skb, struct crypto_alg *alg); /* ← CRYPTO_USER netlink 用这个 */
    void (*free)(struct crypto_instance *inst);
    unsigned int type;
    unsigned int maskset;
    unsigned int maskclear;
    unsigned int tfmsize;
};
```

每种算法类别有一个全局 `const struct crypto_type crypto_<type>_type = { ... }`,在算法实现注册时被挂到每个 `crypto_alg::cra_type` 上。`/proc/crypto` 和 `crypto_user` netlink 用的是同一套多态表里的不同字段(`show` vs `report`)。

---

## 3. 算法是**什么时候**、**怎么**加进去的

### 3.1 入口:`crypto_register_alg`

最底层的注册函数:

```c
int crypto_register_alg(struct crypto_alg *alg)
{
    struct crypto_larval *larval;

    ...
    down_write(&crypto_alg_sem);
    larval = __crypto_register_alg(alg);      /* 真正 list_add 到 crypto_alg_list */
    up_write(&crypto_alg_sem);

    if (IS_ERR(larval)) return PTR_ERR(larval);

    crypto_wait_for_test(larval);             /* 调 testmgr 跑自测,成功置 CRYPTO_ALG_TESTED */
    return 0;
}
```

几件事一步到位:

1. `__crypto_register_alg`:**冲突检查**(`cra_driver_name` 必须全局唯一,不可有两个 `"aes-generic"`)、**larval 机制**(见 §3.3)、最后 `list_add(&alg->cra_list, &crypto_alg_list)`。
2. `crypto_wait_for_test`:异步触发 `testmgr.c` 里对应向量集的自测,完成时再通过一次 `down_write(&crypto_alg_sem)` 把 `CRYPTO_ALG_TESTED` 打上(这一刻你 `cat /proc/crypto` 才会看到 `selftest : passed`)。

**以 `aes_generic.c` 为例**:

```c
subsys_initcall(aes_init);
static int __init aes_init(void) { return crypto_register_alg(&aes_alg); }
```

开机阶段,`subsys_initcall` 被调用时 `aes-generic` 就被挂到 `crypto_alg_list`。**你 `/proc/crypto` 里看到 `aes / aes-generic` 这条,就是这一行代码的结果**。

### 3.2 各类封装入口

"新式"算法类别各自提供更高层的注册函数,内部都会走到 `crypto_register_alg`:

| 类别 | 注册入口 | 样例源文件 |
|---|---|---|
| skcipher | `crypto_register_skcipher(struct skcipher_alg *)` | `crypto/skcipher.c` |
| shash | `crypto_register_shash(struct shash_alg *)` | `crypto/shash.c` |
| ahash | `crypto_register_ahash` | `crypto/ahash.c` |
| aead | `crypto_register_aead` | `crypto/aead.c` |
| rng | `crypto_register_rng` | `crypto/rng.c` |
| akcipher | `crypto_register_akcipher` | `crypto/akcipher.c` |
| kpp | `crypto_register_kpp` | `crypto/kpp.c` |
| scomp/acomp | `crypto_register_scomp` / `_acomp` | `crypto/scompress.c` / `acompress.c` |

这些封装的共同点:帮你把 `cra_type` 指向对应的 `crypto_<type>_type`、把 `cra_flags` 的 type 位设好、最后统一调 `crypto_register_alg`。

### 3.3 模板组合(template)动态注册 —— 这解释了 `cbc(aes)` 这种"复合条目"

你会发现 `/proc/crypto` 里有些条目长这样:

```
name   : cmac(aes)
driver : cmac(aes-generic)
```

或者(当你用 `AF_ALG` 绑 `"cbc(aes)"` 之后)出现:

```
name   : cbc(aes)
driver : cbc(aes-generic)
```

这类条目**并非某个文件静态注册**,而是由 **template** 按需组合出来:

- `crypto/cbc.c` 里是一个 `struct crypto_template cbc_tmpl`。模板本身不注册任何算法,只在 `crypto_alloc_skcipher("cbc(aes)", ...)` 时被解析(`"cbc"` 匹配到模板,`"aes"` 作为内部算法实例化)。
- 解析时内部会:
  1. 找到 `aes`(`aes-generic`)
  2. 分配一个新的 `struct skcipher_instance`(组合体)
  3. 调 `crypto_register_skcipher` 把它加到 `crypto_alg_list`
  4. 这一条"复合算法"也就出现在 `/proc/crypto` 里

所以 `/proc/crypto` 的长度会**随使用而变化**:

- 刚启动、没人用过 `cbc(aes)`,列表里就**没有** `cbc(aes)` 条目
- 一旦某个 tfm(比如 dm-crypt、AF_ALG、IPsec)第一次分配 `cbc(aes)`,模板就会组装并注册一条
- 用完 tfm 释放、`refcnt` 归零一段时间后,template 实例化出来的可能会被清理掉(某些模板会,某些常驻)

这也是为什么 "第一次看" 和 "跑完一次 benchmark 再看" 能看到的条目数不一样。

### 3.4 larval(幼体)机制

在 `__crypto_register_alg` 里你会看到一个有趣现象:**注册过程中会先挂一个 "larval" 到链表上**,等自测通过才用真正的算法替换它:

- 目的:**并发注册时允许其他线程等这个算法"长大"**。比如线程 A 正在注册 `aes-generic`,线程 B 这时候调用 `crypto_alloc_skcipher("aes", ...)`,B 会看到 larval 挂在那里,`crypto_larval_wait()` 睡等 A 跑完 selftest。
- 对 `/proc/crypto` 的影响:**大多数情况下你看不到 larval**,因为它存在时间很短。但如果你赶在一个算法自测超时(60s)的瞬间去 `cat`,理论上会看到一条 `type : larval` + `flags : 0x...`(`proc.c` 里专门为 larval 加了分支,见 §1 那块代码 52~55 行)。

---

## 4. `/proc/crypto` 的生成:`proc.c` 全景

整个文件只有 101 行,做了三件小事:

### 4.1 创建 proc 节点

```c
/* kernel/kernel-5.10/crypto/proc.c:L92-95 */
void __init crypto_init_proc(void)
{
	proc_create_seq("crypto", 0, NULL, &crypto_seq_ops);
}
```

- `proc_create_seq` 是 procfs 给 seq_file 准备的便捷封装,一句话把 `/proc/crypto` 这个只读节点和 `crypto_seq_ops` 绑起来。
- 调用者是谁?—— `algapi.c` 的 `crypto_algapi_init()`:

  ```c
  static int __init crypto_algapi_init(void)
  {
      crypto_init_proc();
      return 0;
  }
  ```

  也就是说 **`/proc/crypto` 节点本身在 Crypto API 模块初始化时就创建好了**,和有没有算法无关。如果某一瞬间链表是空的,你 `cat` 就会得到**空文件**。

### 4.2 seq_file 的三板斧:start / next / stop

```c
/* kernel/kernel-5.10/crypto/proc.c:L20-34 */
static void *c_start(struct seq_file *m, loff_t *pos)
{
	down_read(&crypto_alg_sem);
	return seq_list_start(&crypto_alg_list, *pos);
}

static void *c_next(struct seq_file *m, void *p, loff_t *pos)
{
	return seq_list_next(p, &crypto_alg_list, pos);
}

static void c_stop(struct seq_file *m, void *p)
{
	up_read(&crypto_alg_sem);
}
```

- `c_start`:用户开始读时,抓 `crypto_alg_sem` 读锁,`seq_list_start` 按位置 `*pos` 返回链表里对应的节点指针。
- `c_next`:**完全通用的链表迭代**,没有任何过滤。你读到第几条就给第几条。
- `c_stop`:用户 `read()` 结束时释放读锁。

重要含义:

1. **读 `/proc/crypto` 时整个算法注册表被读锁住**。意味着你 `cat` 时有人在另一边 `crypto_register_alg` 会被 `down_write` 阻塞。不过这个锁持有时间很短(每次 `read()` 内才持有,不是从 `open` 到 `close`),基本不会影响系统。
2. **没有任何 filter**:输出严格等于 `crypto_alg_list` 的顺序。该顺序由 `__crypto_register_alg` 里 `list_add(...)`(头插)决定,所以你看到的**排列顺序大致是"后注册的在前面"**。

### 4.3 `c_show`:每读一个节点打几行字

上面 §1 已经贴过。本质就是一个 switch + 可选的 `cra_type->show` 回调。所以**整个 `/proc/crypto` 的输出就是这几十行 `seq_printf` 组合出来的**——没有硬件探测、没有网络 IO、没有固件查询,**纯内存数据转字符串**,非常快。

---

## 5. 从"用户态敲 `cat`"到"拿到文本"的完整链路

把 §2~§4 串起来:

```
用户态:
    cat /proc/crypto
        = open("/proc/crypto", O_RDONLY) + read() loop

内核态(VFS + procfs):
    procfs 查到 "/proc/crypto" 这个节点是 seq_file 操作的
    -> 调用 crypto_seq_ops.start/next/stop/show

crypto/proc.c:
    c_start:
        down_read(&crypto_alg_sem)
        返回 crypto_alg_list.next 指向的第一个 cra_list 节点

    每次 c_next:
        返回下一个 cra_list 节点(遍历 crypto_alg_list)

    c_show(节点 p):
        alg = container_of(p, struct crypto_alg, cra_list);
        打通用 7 行(name/driver/module/priority/refcnt/selftest/internal)
        如果是 larval: 打 "type: larval" 后 return
        如果 alg->cra_type->show 非空: 走它(skcipher/shash/...)
        否则: 进 switch,打 cipher / compression / unknown

    c_stop:
        up_read(&crypto_alg_sem)

最终合并成文本流通过 seq_file -> read() -> 用户态
```

那么这张表里的**每一条** = 某个 `module_init` / 某次 template 实例化 / 某次 `crypto_register_*` 调用的结果。

---

## 6. 两个近邻接口顺带讲清楚

很多人以为 `/proc/crypto` 就是唯一入口。其实不是,内核提供了两个查询算法表的途径:

### 6.1 `/proc/crypto`(本篇主角)
- 走 procfs + seq_file,**只读、文本、人类友好**。
- 唯一数据源就是 `crypto_alg_list`。

### 6.2 `AF_ALG` + `CRYPTO_USER` netlink(`crypto/crypto_user_base.c`)
- `socket(AF_NETLINK, SOCK_DGRAM, NETLINK_CRYPTO)`,发特定命令。
- 对应的打印函数叫 `xxx_report`(和 `/proc/crypto` 用的 `xxx_show` 是同源兄弟函数,就差一个把 `seq_printf` 换成 `nla_put` 填 netlink 属性)。
- 用户态工具 `libkcapi`、`crconf` 基于这个接口。
- 数据源也是 `crypto_alg_list`,但**还能触发自测、删算法、动态修改 priority** 等,权限要求 CAP_NET_ADMIN。

这两者是"同一张表、两种姿势读"。对 `AF_ALG` socket 本身(`algif_skcipher/hash/aead/rng`)来说,它们的注册/使用**不会**往 `crypto_alg_list` 里插内容——它们是表的消费者。

---

## 7. 回到 x2600:读你的 `/proc/crypto` 能得到什么结论?

结合前面的机制,你上次贴的那份输出可以精确解读成:

1. **条目来源**:全部来自 `crypto/*.c`(软件实现)和若干 template 实例化出的复合算法。**没有任何一条来自 `drivers/crypto/*`**——因为这份 SDK 没有启用 Ingenic 硬件 crypto 驱动。
2. **`module : kernel`**:所有算法都是编进 vmlinuz 的(built-in),不是 `.ko`,所以 `module_name(alg->cra_module) == "kernel"`。
3. **没有 `async : yes` 的条目**:硬件驱动通常会带 `CRYPTO_ALG_ASYNC`,你这里都没有,再次印证是纯软件。
4. **`priority` 全在 100~207**:软件 DRBG 最高 207,其他算法都在 100。硬件驱动如果上来一般会 >= 300,你这里没有。
5. **`cmac(aes)` 是 shash**(字段只有 blocksize/digestsize)、**`aes` 是老式 cipher**(字段只有 blocksize/min/max keysize)—— 说明你这台设备上的 AES 只有"单块同步 cipher API",想做 CBC 要靠 template(`cbc` + `aes-generic`),想异步要靠 AF_ALG 上层自己包装。
6. **看不到 `cbc(aes)` 条目**:这说明**到你 `cat` 的那一刻为止,系统还没有任何人 `crypto_alloc_skcipher("cbc(aes)")` 过**。一旦你跑一次基于 AF_ALG 的 AES-CBC 加密,再看 `/proc/crypto`,就会多出一条 `cbc(aes) / cbc(aes-generic) / skcipher` 条目。

这两条(尤其第 6)是理解 `/proc/crypto` 最大的两个误区——列表是**动态的**、**懒加载的**、**反映当下注册表**,不是"编译进内核的所有算法清单"。

---

## 8. 调试 cheat sheet

| 想知道…… | 查看方式 |
|---|---|
| 这个算法被谁实现?有没有硬件? | 看同名多条里 `priority` 最大的那条的 `driver` 和 `module` |
| 这个算法是 sync 还是 async? | `type : skcipher / ahash / aead` 条目里的 `async` 字段 |
| 算法自测有没有过? | `selftest : passed / unknown` |
| 自己的驱动为啥没被选上? | 对比 `cra_priority` 数值是否比软件高;`cra_flags & CRYPTO_ALG_INTERNAL` 是否误开 |
| 加了 `CONFIG_CRYPTO_X`,`cat` 看不到? | 可能是 **template**,需要先 `crypto_alloc_*("X(Y)")` 触发实例化 |
| 加了驱动,`cat` 看不到? | 驱动的 `cra_driver_name` 冲突会注册失败;或者 `init` 阶段太早(`crypto_init_proc` 还没跑),算法进链表但 `/proc/crypto` 节点还没创建(后者会自行修正)|
| 同时开多实现,哪个会被用? | 同 `name` 里 priority 最高的 |
| 统计当前有多少算法? | `cat /proc/crypto \| grep -c ^name` |
| 找某算法的所有实现? | `cat /proc/crypto \| awk '/^$/{f=0} /^name[[:space:]]*: aes$/{f=1} f'` |

---

## 9. 半句话总结

- **是什么**:`/proc/crypto` = 内核 Crypto API 维护的 `crypto_alg_list` 的文本快照。
- **怎么查**:`proc.c` 里 100 行 `seq_file` 代码 + 读锁遍历 + 分类打印。
- **数据源**:各算法文件 `module_init` 调 `crypto_register_{alg,skcipher,shash,...}` 挂上去,template 在第一次被请求时现场生成组合算法挂上去。
- **和 AF_ALG 的关系**:AF_ALG 是这张表的**消费者**,不往里面添加条目。所以你读 `/proc/crypto` 永远看不到 `"algif_*"` 之类的东西。
- **对 x2600 的直接意义**:这份表完全由软件 `crypto/*.c` 模块提供,也就解释了为什么"所有加密运算最终都在 CPU 上跑"——参阅同目录的 `03-AF_ALG架构与实现解析.md` §9 和 `04-algif_skcipher深度走读.md` §4 的时序图。
