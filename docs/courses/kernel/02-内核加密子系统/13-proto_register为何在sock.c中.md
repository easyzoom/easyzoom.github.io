---
title: proto_register 为何在 sock.c 中
author: EASYZOOM
date: 2026/04/21 12:00
categories:
 - Linux内核开发
tags:
 - Linux
 - 内核
 - 加密
---

# 为什么 `proto_register()` 放在 `net/core/sock.c` 里

> 相关源码:
> - `kernel/kernel-5.10/include/net/sock.h`
> - `kernel/kernel-5.10/net/core/sock.c`
> - `kernel/kernel-5.10/net/socket.c`
> - `kernel/kernel-5.10/crypto/af_alg.c`

---

## 0. 先说结论

`proto_register()` 放在 `sock.c` 里,不是因为它只服务 TCP/UDP,而是因为它注册的对象 `struct proto` 本来就是 **socket 核心层(`struct sock`)的公共基础设施**。

它负责的不是:

- 地址族注册(`AF_INET` / `AF_UNIX` / `PF_ALG`)
- 文件描述符层的 `socket->ops`

而是更底层的:

- `struct sock` 对象怎么分配/释放
- 每种协议自己的 slab cache
- request socket / timewait socket 的附属 cache
- 每协议的内存统计、inuse 统计
- 每协议在 `/proc/net/protocols` 里的可见性

这些能力都属于 **socket core**,而 `sock.c` 正是 socket core 里管理 `struct sock` 生命周期和公共资源的地方,所以 `proto_register()` 自然放在这里。

---

## 1. Linux socket 栈里其实有三层注册,最容易混

很多人第一次看会把下面三样东西混在一起:

### 1.1 `sock_register()` 注册的是 `net_proto_family`

定义在 `net/socket.c`:

```c
/* kernel/kernel-5.10/net/socket.c:L2969-2978 */
/**
 *	sock_register - add a socket protocol handler
 *	@ops: description of protocol
 *
 *	This function is called by a protocol handler that wants to
 *	advertise its address family, and have it linked into the
 *	socket interface. The value ops->family corresponds to the
 *	socket system call protocol family.
 */
int sock_register(const struct net_proto_family *ops)
```

它解决的问题是:

> `socket(AF_xxx, type, proto)` 进来之后,到底该找谁的 `.create()`?

也就是把:

- `AF_INET`
- `AF_UNIX`
- `PF_ALG`
- `AF_NETLINK`

这些 **地址族 / protocol family** 注册到 `socket()` 系统调用入口。

---

### 1.2 `proto_register()` 注册的是 `struct proto`

`struct proto` 在 `include/net/sock.h` 里的注释写得很直白:

```c
/* kernel/kernel-5.10/include/net/sock.h:L1163-1166 */
/* Networking protocol blocks we attach to sockets.
 * socket layer -> transport layer interface
 */
struct proto {
```

也就是说 `struct proto` 是:

> **socket layer 到具体协议实现之间的内部接口块**

它不是给用户态 `socket()` 直接查的,而是给 `struct sock` 用的。

---

### 1.3 `struct proto_ops` 是文件描述符层的 socket 操作表

比如 `AF_ALG` 里:

```c
/* kernel/kernel-5.10/crypto/af_alg.c:L344-362 */
static const struct proto_ops alg_proto_ops = {
	.family		=	PF_ALG,
	.owner		=	THIS_MODULE,
	.connect	=	sock_no_connect,
	.socketpair	=	sock_no_socketpair,
	.getname	=	sock_no_getname,
	.ioctl		=	sock_no_ioctl,
	.listen		=	sock_no_listen,
	.shutdown	=	sock_no_shutdown,
	.mmap		=	sock_no_mmap,
	.sendpage	=	sock_no_sendpage,
	.sendmsg	=	sock_no_sendmsg,
	.recvmsg	=	sock_no_recvmsg,
	.bind		=	alg_bind,
	.release	=	af_alg_release,
	.setsockopt	=	alg_setsockopt,
	.accept		=	alg_accept,
};
```

它解决的问题是:

> `read/sendmsg/recvmsg/bind/accept` 这些 **socket fd 操作** 应该跳到哪组函数?

---

## 2. 一句话区分三层

| 层级 | 注册函数 | 注册对象 | 回答的问题 |
|---|---|---|---|
| 地址族层 | `sock_register()` | `struct net_proto_family` | `socket(AF_xxx)` 应该由谁 `.create()` |
| `struct sock` 核心层 | `proto_register()` | `struct proto` | 这种 socket 的底层 `struct sock` 怎么分配、统计、管理 |
| fd 操作层 | 直接赋值 `socket->ops = ...` | `struct proto_ops` | 这个 socket fd 的 `sendmsg/bind/accept` 怎么实现 |

`proto_register()` 明显属于第二层,所以它跟 `sock.c` 绑定,而不是跟某个具体族(`af_inet.c`、`af_alg.c`)绑定。

---

## 3. `struct proto` 到底管什么,为什么这些都归 `sock.c`

看 `struct proto` 的字段就很容易明白它为什么不适合放在某个具体协议文件里:

```c
/* kernel/kernel-5.10/include/net/sock.h:L1166-1268 */
struct proto {
	void			(*close)(struct sock *sk, long timeout);
	int			(*connect)(struct sock *sk, struct sockaddr *uaddr, int addr_len);
	struct sock *		(*accept)(struct sock *sk, int flags, int *err, bool kern);
	int			(*ioctl)(struct sock *sk, int cmd, unsigned long arg);
	int			(*init)(struct sock *sk);
	void			(*destroy)(struct sock *sk);
	void			(*shutdown)(struct sock *sk, int how);
	int			(*setsockopt)(struct sock *sk, int level, int optname, sockptr_t optval, unsigned int optlen);
	int			(*getsockopt)(struct sock *sk, int level, int optname, char __user *optval, int __user *option);
	int			(*sendmsg)(struct sock *sk, struct msghdr *msg, size_t len);
	int			(*recvmsg)(struct sock *sk, struct msghdr *msg, size_t len, int noblock, int flags, int *addr_len);
	...
	atomic_long_t		*memory_allocated;
	struct percpu_counter	*sockets_allocated;
	unsigned long		*memory_pressure;
	int			*sysctl_wmem;
	int			*sysctl_rmem;
	...
	struct kmem_cache	*slab;
	unsigned int		obj_size;
	...
	struct request_sock_ops	*rsk_prot;
	struct timewait_sock_ops *twsk_prot;
	...
	struct module		*owner;
	char			name[32];
	struct list_head	node;
```

这不是一个\"协议逻辑函数表\"那么简单,它同时承载了:

1. **协议回调**
2. **内存/压力统计**
3. **slab/cache 元数据**
4. **request_sock / timewait_sock 支撑**
5. **模块引用**
6. **全局协议链表节点**

这些事情都不是某个地址族私有的,而是所有使用 `struct sock` 的协议共同依赖的公共机制。

而 `sock.c` 恰好就是这些公共机制的主人。

---

## 4. `sock.c` 已经天然拥有 `proto_register()` 需要的一切

### 4.1 `sock.c` 负责按 `struct proto` 分配 `struct sock`

`sk_alloc()` 最终就是按传进来的 `prot` 来分配:

```c
/* kernel/kernel-5.10/net/core/sock.c:L1677-1699 */
static struct sock *sk_prot_alloc(struct proto *prot, gfp_t priority,
		int family)
{
	struct sock *sk;
	struct kmem_cache *slab;

	slab = prot->slab;
	if (slab != NULL) {
		sk = kmem_cache_alloc(slab, priority & ~__GFP_ZERO);
		...
	} else
		sk = kmalloc(prot->obj_size, priority);

	...
	if (!try_module_get(prot->owner))
		goto out_free_sec;
```

接着 `sk_alloc()` 会把这份 `prot` 直接挂进 socket:

```c
/* kernel/kernel-5.10/net/core/sock.c:L1740-1753 */
struct sock *sk_alloc(struct net *net, int family, gfp_t priority,
		      struct proto *prot, int kern)
{
	...
	sk = sk_prot_alloc(prot, priority | __GFP_ZERO, family);
	if (sk) {
		sk->sk_family = family;
		sk->sk_prot = sk->sk_prot_creator = prot;
		sk->sk_kern_sock = kern;
```

所以 `proto_register()` 要初始化的 `prot->slab`、`prot->obj_size`、`prot->owner` 等信息,**本来就是 `sock.c` 这套分配逻辑要直接消费的**。

这已经足够说明:它不该放到 `socket.c`,也不该放到某个具体协议里。

---

### 4.2 `sock.c` 还负责协议级统计和 `/proc/net/protocols`

`proto_register()` 会把每个 `struct proto` 挂进 `proto_list`:

```c
/* kernel/kernel-5.10/net/core/sock.c:L142-143 */
static DEFINE_MUTEX(proto_list_mutex);
static LIST_HEAD(proto_list);
```

注册时:

```c
/* kernel/kernel-5.10/net/core/sock.c:L3519-3526 */
	mutex_lock(&proto_list_mutex);
	ret = assign_proto_idx(prot);
	...
	list_add(&prot->node, &proto_list);
	mutex_unlock(&proto_list_mutex);
```

这条链表后面直接被 `sock.c` 自己拿去做 `/proc/net/protocols`:

```c
/* kernel/kernel-5.10/net/core/sock.c:L3583-3664 */
static void *proto_seq_start(struct seq_file *seq, loff_t *pos)
{
	mutex_lock(&proto_list_mutex);
	return seq_list_start_head(&proto_list, *pos);
}
...
static int proto_seq_show(struct seq_file *seq, void *v)
{
	...
	else
		proto_seq_printf(seq, list_entry(v, struct proto, node));
```

`proto_seq_printf()` 会打印:

- protocol 名称
- 对象大小
- sockets 数
- memory 使用
- pressure
- slab 是否存在
- module 名
- 这一协议实现了哪些 `close/connect/sendmsg/...` 方法

也就是说,**`proto_register()` 并不只是分配一个 slab**;它还把协议纳入了 socket core 的可观测性体系。

这件事当然也该由 `sock.c` 管。

---

### 4.3 `sock.c` 负责 request socket / timewait socket 的公共支撑

`proto_register()` 里还有一段很关键:

```c
/* kernel/kernel-5.10/net/core/sock.c:L3484-3516 */
	if (alloc_slab) {
		prot->slab = kmem_cache_create_usercopy(...);
		...
		if (req_prot_init(prot))
			goto out_free_request_sock_slab;

		if (prot->twsk_prot != NULL) {
			prot->twsk_prot->twsk_slab_name = kasprintf(...);
			...
			prot->twsk_prot->twsk_slab =
				kmem_cache_create(...);
```

这说明 `proto_register()` 还会统一处理:

- `struct sock` 自己的 slab
- `request_sock` 的 slab
- `timewait_sock` 的 slab

这些都是 TCP/INET 家族特别常见,但它们是 **socket core 对各种传输协议的通用支撑能力**,不是 IPv4 专属逻辑。

换句话说:

> `proto_register()` 注册的不是\"一个网络号\"或\"一个族\",而是一个 **可被 `struct sock` 框架承载的协议实现模板**。

---

## 5. 所以它为什么不放在 `socket.c`

因为 `socket.c` 关注的是 **socket 系统调用入口和地址族分发**。

它管的是:

- `__sys_socket()`
- `sock_create()`
- `net_families[]`
- `sock_register()`

例如:

```c
/* kernel/kernel-5.10/net/socket.c:L2970-2978 */
 *	sock_register - add a socket protocol handler
 *	@ops: description of protocol
 *
 *	This function is called by a protocol handler that wants to
 *	advertise its address family, and have it linked into the
 *	socket interface. The value ops->family corresponds to the
 *	socket system call protocol family.
 */
```

`socket.c` 的关注点是:

> 用户态 `socket(AF_XXX, ...)` 进来后,怎么找到 `net_proto_family.create`

而 `proto_register()` 的关注点是:

> 找到 `.create` 之后,具体协议的 `struct sock` 要怎么被 socket core 管理

所以这两个注册函数虽然名字都带 `register`,但职责完全不同。

---

## 6. 所以它为什么也不放在 `af_inet.c` 或 `af_alg.c`

因为:

- `af_inet.c`
- `af_unix.c`
- `af_netlink.c`
- `crypto/af_alg.c`

这些文件只是 **某一个具体 family 的使用者**

它们只是在模块初始化时调用:

```c
proto_register(&xxx_prot, ...);
sock_register(&xxx_family);
```

例如 IPv4:

```c
/* kernel/kernel-5.10/net/ipv4/af_inet.c:L1940-1952 */
	rc = proto_register(&tcp_prot, 1);
	...
	rc = proto_register(&udp_prot, 1);
	...
	rc = proto_register(&raw_prot, 1);
	...
	rc = proto_register(&ping_prot, 1);
```

AF_ALG 也是一样:

```c
/* kernel/kernel-5.10/crypto/af_alg.c:L1190-1198 */
static int __init af_alg_init(void)
{
	int err = proto_register(&alg_proto, 0);
	...
	err = sock_register(&alg_family);
```

这恰好反过来证明:

> `proto_register()` 是一个跨协议族、跨子系统复用的公共 API。

既然 TCP、UDP、RAW、PING、AF_ALG 都要用,它当然应该待在公共的 `sock.c`,而不是某一个使用者文件里。

---

## 7. `AF_ALG` 是最能说明问题的例子

很多人看到 `crypto/af_alg.c` 里也调了 `proto_register(&alg_proto, 0)` 会疑惑:

> 这不是 crypto 吗,为什么还要去 net/core/sock.c 注册 `proto`?

答案是:

> 因为 `AF_ALG` 虽然语义上是 \"加密接口\",但实现形式上它依旧是 **socket**。

它仍然走:

1. `socket(AF_ALG, SOCK_SEQPACKET, 0)`
2. `sock_register(&alg_family)` 让 `socket()` 能找到 `alg_create`
3. `alg_create()` 内部调用:

```c
/* kernel/kernel-5.10/crypto/af_alg.c:L384-389 */
	sk = sk_alloc(net, PF_ALG, GFP_KERNEL, &alg_proto, kern);
	...
	sock->ops = &alg_proto_ops;
	sock_init_data(sock, sk);
```

而 `sk_alloc()` 又依赖 `struct proto`:

- `prot->obj_size`
- `prot->slab`
- `prot->owner`

所以 `AF_ALG` 必须先 `proto_register(&alg_proto, 0)`。

这里只是 `alloc_slab = 0`,因为 `alg_proto` 的对象很小、直接 `kmalloc(sizeof(struct alg_sock))` 就够了:

```c
/* kernel/kernel-5.10/crypto/af_alg.c:L30-35 */
static struct proto alg_proto = {
	.name			= "ALG",
	.owner			= THIS_MODULE,
	.memory_allocated	= &alg_memory_allocated,
	.obj_size		= sizeof(struct alg_sock),
};
```

也就是说:

- `sock_register(&alg_family)` 解决 **怎么创建 AF_ALG socket**
- `proto_register(&alg_proto, 0)` 解决 **AF_ALG 的 `struct sock/alg_sock` 交给谁管理**

这两个层次缺一不可。

---

## 8. 一个更准确的理解: `struct proto` 不是\"协议号\",而是\"sock 类的元数据 + 方法表\"

如果把 Linux socket 栈类比成面向对象模型:

- `net_proto_family` 像是 **构造器入口注册表**
- `proto_ops` 像是 **用户态 fd 可见的方法表**
- `proto` 像是 **底层 socket 对象类(`struct sock`)的虚表 + allocator + accounting 元数据**

那 `proto_register()` 就不是\"把协议号挂进一个表\"这么简单,而是:

> 把一种 `struct sock` 风格的对象类型,正式接入 socket core 的对象管理框架

这正是 `sock.c` 的领域。

---

## 9. `proto_register()` 放在 `sock.c` 的直接工程收益

把它放在公共 socket core,会带来几个很现实的好处:

### 9.1 统一对象分配和释放

所有协议都走一套:

- `sk_prot_alloc()`
- `sk_alloc()`
- `sk_prot_free()`

避免每个协议自己手写 slab、自己做 module 引用计数、自己处理 usercopy 白名单。

### 9.2 统一协议级统计

`memory_allocated`、`sockets_allocated`、`memory_pressure`、`inuse_idx` 都可以被 socket core 统一维护。

### 9.3 统一观测接口

`/proc/net/protocols` 只需要遍历 `proto_list` 就能把所有协议列出来。

### 9.4 统一扩展点

以后 socket core 想给所有协议加:

- 新的统计项
- 新的 debug 字段
- 新的 slab 选项
- 新的安全/内存约束

只要改 `struct proto` 和 `proto_register()` 一处,所有协议一起受益。

---

## 10. 回到你的问题:一句话答案

> `proto_register()` 放在 `sock.c`,因为它注册的 `struct proto` 属于 **socket core 的公共对象管理层**,而不是某个具体地址族或某个具体协议文件的私有逻辑。

更具体地说,`sock.c` 本身就拥有并负责:

- `struct proto` 的核心定义和使用场景
- `sk_alloc/sk_prot_alloc/sk_prot_free`
- 协议级 slab / request_sock / timewait_sock 初始化
- `proto_list` 和 `/proc/net/protocols`
- 每协议 inuse / memory / pressure 统计

所以 `proto_register()` 天然就该放在这里。

而 `socket.c` 只负责地址族分发(`sock_register`),`af_inet.c` / `af_alg.c` 这些只是具体使用者,不应该拥有这个通用注册器。

---

## 11. 最后用 AF_ALG 串一遍

`AF_ALG` 初始化时做了两件事:

1. `proto_register(&alg_proto, 0)`
   - 告诉 socket core:`PF_ALG` 的底层 `struct alg_sock` 用这份 `struct proto` 管
   - 提供 `obj_size = sizeof(struct alg_sock)`
   - 接入 socket core 的 memory accounting

2. `sock_register(&alg_family)`
   - 告诉 `socket(AF_ALG, ...)` 的入口分发:这个 family 的 `.create = alg_create`

然后:

3. `alg_create()` 调 `sk_alloc(..., &alg_proto, ...)`
4. `sk_alloc()` 在 `sock.c` 里按 `alg_proto` 分配对象
5. `sock->ops = &alg_proto_ops`

三层各司其职:

| 层 | AF_ALG 对应对象 |
|---|---|
| family 分发 | `alg_family` |
| `struct sock` 底层对象模板 | `alg_proto` |
| fd 操作表 | `alg_proto_ops` |

把这三层分开看,`proto_register()` 在 `sock.c` 这件事就会非常自然。

---

## 附录: 用 `/proc/net/protocols` 运行时验证 `af_alg` 是否注册

前面几节都是源码层面的推导。实际上内核还给了一个**运行时观察点**可以直接验证这件事——就是 `/proc/net/protocols`。

### A.1 这个文件是什么

`/proc/net/protocols` 是 `sock.c` 里 `proto_list` 这条全局链表的 seq_file 展示。每调一次 `proto_register()` 就会多出一行,每调一次 `proto_unregister()` 就会摘掉一行。所以它就是"当前内核里注册了哪些 `struct proto`"的**运行时快照**。

打印逻辑前面 4.2 节已经引过,入口是 `sock.c` 的 `proto_seq_start()` / `proto_seq_show()` / `proto_seq_printf()`。

### A.2 一个典型输出长这样

```
protocol  size sockets  memory press maxhdr  slab module     cl co di ac io in de sh ss gs se re sp bi br ha uh gp em
PACKET    1184      3      -1   NI       0   no   kernel      n  n  n  n  n  n  n  n  n  n  n  n  n  n  n  n  n  n  n
UDPv6     1024      1      72   NI       0   yes  kernel      y  y  y  n  y  y  y  n  y  y  y  y  n  n  n  y  y  y  n
TCPv6     1992     49       1   no     224   yes  kernel      y  y  y  y  y  y  y  y  y  y  y  y  y  n  y  y  y  y  y
UNIX       832     19      -1   NI       0   yes  kernel      n  n  n  n  n  n  n  n  n  n  n  n  n  n  n  n  n  n  n
UDP        896     50      72   NI       0   yes  kernel      y  y  y  n  y  y  y  n  y  y  y  y  y  n  n  y  y  y  n
TCP       1880     34       1   no     224   yes  kernel      y  y  y  y  y  y  y  y  y  y  y  y  y  n  y  y  y  y  y
NETLINK    872      6      -1   NI       0   no   kernel      n  n  n  n  n  n  n  n  n  n  n  n  n  n  n  n  n  n  n
```

每一列对应 `proto_seq_printf()` 打印的字段,其中前几列的含义:

| 列 | 来源 | 说明 |
|---|---|---|
| `protocol` | `prot->name` | 协议名,就是 `struct proto.name` |
| `size` | `prot->obj_size` | 一个 `struct sock` 对象的字节数,`sk_alloc` 分配这么大 |
| `sockets` | `sock_prot_inuse_get()` | 当前这种 proto 活着的 socket 数 |
| `memory` | `prot->memory_allocated` 指向的 atomic | 协议级页数,`-1` 表示没接入 memory accounting |
| `press` | `prot->memory_pressure`  / `enter_memory_pressure` | `yes`/`no`/`NI`(未实现) |
| `maxhdr` | `prot->max_header` | 协议头最大字节数 |
| `slab` | `prot->slab != NULL` | `yes` 表示 `proto_register(..., alloc_slab=1)` 建了独立 slab |
| `module` | `prot->owner` | `kernel` = built-in;如果是 ko 就是模块名 |

再往后的 19 个 y/n 列是 `proto_method_implemented()` 一个个检查 `struct proto` 里函数指针是否为 `NULL` 得出的:

`cl co di ac io in de sh ss gs se re sp bi br ha uh gp em`

分别对应 `close / connect / disconnect / accept / ioctl / init / destroy / shutdown / setsockopt / getsockopt / sendmsg / recvmsg / sendpage / bind / backlog_rcv / hash / unhash / get_port / enter_memory_pressure`。

所以看上面这份输出:

- **TCP / TCPv6** 几乎全 `y`,因为它是面向连接、有端口、有 backlog、有内存压力管理的"全功能"协议,`struct proto` 的回调基本都用上了
- **UDP** `ac`(accept)、`br`(backlog_rcv)、`em` 是 `n`,因为无连接、没有 TCP 那套 accept queue
- **UNIX / NETLINK / PACKET** 后面整排 `n`:这些协议几乎不用 `struct proto` 里的回调,业务逻辑全都在 `struct proto_ops`(socket fd 层)里自己实现,`struct proto` 只是走注册流程、给 `sk_alloc` 提供 `obj_size`、挂上 `proto_list` 而已

这正好从反面印证了本文的核心观点:

> `proto_register()` 的本质职责不是注册一堆回调,而是**把这种 `struct sock` 风格的对象类型接入 socket core 的对象管理框架**——`obj_size`、slab、module、`proto_list`、accounting 这几件事才是硬需求,那 19 个回调有没有其实是可选的。

### A.3 `af_alg` 注册成功后应该是什么样

根据 `crypto/af_alg.c` 里的定义:

```c
/* kernel/kernel-5.10/crypto/af_alg.c:L30-35 */
static struct proto alg_proto = {
	.name			= "ALG",
	.owner			= THIS_MODULE,
	.memory_allocated	= &alg_memory_allocated,
	.obj_size		= sizeof(struct alg_sock),
};
```

并且注册时:

```c
/* kernel/kernel-5.10/crypto/af_alg.c:L1190-1198 */
static int __init af_alg_init(void)
{
	int err = proto_register(&alg_proto, 0);
```

`alloc_slab = 0`,也没有填任何函数指针。所以注册成功后在 `/proc/net/protocols` 里看到的 `ALG` 行会有这些特征:

| 列 | 预期值 | 原因 |
|---|---|---|
| `protocol` | `ALG` | `.name = "ALG"` |
| `size` | `sizeof(struct alg_sock)` | 一般是几十到一百多字节 |
| `memory` | 看 `alg_memory_allocated` 当前值 | 有接入 memory accounting |
| `slab` | `no` | 传的是 `alloc_slab=0`,走 `kmalloc(obj_size)` |
| `module` | `kernel` 或 `af_alg` | 取决于 `CONFIG_CRYPTO_USER_API` 是 `=y` 还是 `=m` |
| 后 19 列 | 基本全 `n` | `alg_proto` 没填任何回调,业务全在 `alg_proto_ops` |

所以**只要这一行存在,就说明 `af_alg_init()` 里那句 `proto_register(&alg_proto, 0)` 已经成功执行**,`PF_ALG` 的 `struct alg_sock` 已经正式被 socket core 接管。

### A.4 实操:一条命令做验证

```bash
# 1. 看看 ALG 是不是已经在里面了
cat /proc/net/protocols | grep -E '^(protocol|ALG)'

# 2. 如果没有,看一下 CONFIG 是怎么配的
zcat /proc/config.gz 2>/dev/null | grep CRYPTO_USER_API
# 或者
grep CRYPTO_USER_API /boot/config-$(uname -r)

# 3. 如果是 =m,手动加载看效果
modprobe af_alg
cat /proc/net/protocols | grep ALG
```

用这套"先看 `/proc/net/protocols` → 再看 `CONFIG` → 再 `modprobe`"的组合,可以一眼判断:

1. `af_alg` 到底有没有编进内核
2. 有没有真正被 `proto_register()` 挂上
3. `obj_size` 是不是和源码里 `sizeof(struct alg_sock)` 一致(可用来验证内核版本)

### A.5 跟 `/proc/crypto` 不是一回事

最后提醒一下,**`/proc/net/protocols` 和 `/proc/crypto` 是两条完全不同的链表**,不要混:

| 路径 | 遍历的链表 | 回答的问题 | 属于哪层 |
|---|---|---|---|
| `/proc/net/protocols` | `proto_list`(`sock.c`) | 系统里有哪些 **socket 协议**(`struct proto`) | socket core |
| `/proc/crypto` | `crypto_alg_list`(`crypto/api.c`) | 系统里有哪些 **加密算法实现**(`struct crypto_alg`) | crypto core |

- `af_alg` 注册是否成功 → 看 `/proc/net/protocols` 里有没有 `ALG`
- 具体有哪些 skcipher/hash/aead 算法可用 → 看 `/proc/crypto`

两个合起来,才能完整回答"用户态 `AF_ALG` 到底能用哪些算法"这件事。

---

## 12. 三层关系图(补充版)

如果只想记一个最小脑图,记下面这张就够了:

```text
用户态:
    socket(AF_ALG, SOCK_SEQPACKET, 0)
         |
         v
+-------------------------------+
| 1. net_proto_family           |
|    alg_family                 |
|    由 sock_register() 注册    |
|    作用: 让 socket(AF_ALG)    |
|          能找到 .create()     |
+-------------------------------+
         |
         v
+-------------------------------+
| 2. struct proto              |
|    alg_proto                 |
|    由 proto_register() 注册  |
|    作用: 让 sk_alloc() 知道   |
|          怎么分配/统计/管理   |
|          struct alg_sock     |
+-------------------------------+
         |
         v
+-------------------------------+
| 3. struct proto_ops          |
|    alg_proto_ops             |
|    在 alg_create() 里挂到     |
|    sock->ops                 |
|    作用: 实现 bind/accept/    |
|          setsockopt/...       |
+-------------------------------+
```

对应关系可以强行背成一句话:

- `net_proto_family` = **怎么创建**
- `proto` = **底层对象怎么养**
- `proto_ops` = **这个 fd 怎么用**

---

## 13. 用 `AF_ALG` 串一遍最短调用链

把前面的抽象落到你现在看的 `af_alg.c`,最短链路就是:

```text
af_alg_init()
 ├─ proto_register(&alg_proto, 0)
 │    -> 把 ALG 这类 sock 对象模板挂进 socket core
 │    -> 之后 sk_alloc(..., &alg_proto, ...) 才合法
 │
 └─ sock_register(&alg_family)
      -> 把 PF_ALG 这个 family 挂进 socket() 分发表

用户态 socket(AF_ALG, SOCK_SEQPACKET, 0)
 -> sock_create()
 -> 找到 alg_family.create = alg_create
 -> alg_create()
    -> sk_alloc(net, PF_ALG, GFP_KERNEL, &alg_proto, kern)
         -> sock.c 按 alg_proto 分配 struct alg_sock
    -> sock->ops = &alg_proto_ops
```

所以你以后看到:

```c
proto_register(&xxx_prot, ...);
sock_register(&xxx_family);
sock->ops = &xxx_proto_ops;
```

就可以立刻把它翻译成:

1. 先把底层 `struct sock` 模板接入 socket core
2. 再把地址族接到 `socket()` 入口
3. 最后给具体 socket fd 挂操作表

这也是为什么 `proto_register()` 必须在 `sock.c`,而不能和 `sock_register()` 混在同一层理解。

---

## TL;DR

- `sock_register()` 在 `socket.c`:注册地址族(`AF_INET` / `PF_ALG`)
- `proto_register()` 在 `sock.c`:注册 `struct sock` 背后的协议对象模板(`struct proto`)
- `proto_ops` 在各协议文件里:注册 socket fd 的具体操作

`proto_register()` 之所以放在 `sock.c`,是因为它管理的是 **所有 socket 协议共享的 `struct sock` 生命周期、slab、统计、观测和附属对象**,而这些正是 `sock.c` 的职责边界。
