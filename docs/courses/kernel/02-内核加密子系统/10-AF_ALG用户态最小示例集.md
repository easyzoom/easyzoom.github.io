---
title: AF_ALG 用户态最小示例集
author: EASYZOOM
date: 2026/04/21 12:00
categories:
 - Linux内核开发
tags:
 - Linux
 - 内核
 - 加密
---

# `AF_ALG` 用户态最小示例集

> 目标:给 `AF_ALG` 的 4 类常见接口各放一个尽量小、能直接抄去编译的 C 示例:
>
> - `hash`
> - `skcipher`
> - `aead`
> - `rng`
>
> 配套阅读:

---

## 0. 先记住 `AF_ALG` 的共同套路

不管是 `hash` / `skcipher` / `aead` / `rng`,用户态骨架都一样:

1. `socket(AF_ALG, SOCK_SEQPACKET, 0)`
2. `bind(fd, { .salg_type = "...", .salg_name = "..." })`
3. 按类型做 `setsockopt(SOL_ALG, ...)`
4. `accept(fd, NULL, 0)` 拿到 op socket
5. 对 op socket 做 `sendmsg()/recvmsg()` 或 `read()/write()`

其中:

- `bind` 选“算法类别 + 算法名”
- `setsockopt` 设 key / authsize / entropy
- `accept` 后返回的 **op socket** 才是真正用来收发数据的 fd

---

## 1. 头文件与编译

如果系统头文件比较老,可能没有导出 `AF_ALG` / `SOL_ALG`,可以自己补:

```c
#ifndef AF_ALG
#define AF_ALG 38
#endif

#ifndef SOL_ALG
#define SOL_ALG 279
#endif
```

通用头文件通常如下:

```c
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <linux/if_alg.h>
```

编译方式:

```bash
gcc -O2 -Wall demo.c -o demo
```

---

## 2. 一个公共小工具:打印十六进制

后面示例都会用到:

```c
static void dump_hex(const char *tag, const uint8_t *buf, size_t len)
{
    size_t i;

    printf("%s (%zu bytes): ", tag, len);
    for (i = 0; i < len; i++)
        printf("%02x", buf[i]);
    printf("\n");
}
```

---

## 3. 示例一:`hash` — 计算 `sha256("hello af_alg")`

这个例子最简单,也是最适合先跑通 `AF_ALG` 的。

### 3.1 要点

- `salg_type = "hash"`
- `salg_name = "sha256"`
- `send()` 送待摘要数据
- `recv()` 取 digest

### 3.2 代码

```c
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <linux/if_alg.h>

#ifndef AF_ALG
#define AF_ALG 38
#endif
#ifndef SOL_ALG
#define SOL_ALG 279
#endif

static void dump_hex(const char *tag, const uint8_t *buf, size_t len)
{
    size_t i;
    printf("%s (%zu bytes): ", tag, len);
    for (i = 0; i < len; i++)
        printf("%02x", buf[i]);
    printf("\n");
}

int main(void)
{
    int tfd = -1, opfd = -1;
    uint8_t digest[32];
    const char *msg = "hello af_alg";
    struct sockaddr_alg sa = {
        .salg_family = AF_ALG,
        .salg_type = "hash",
        .salg_name = "sha256",
    };

    tfd = socket(AF_ALG, SOCK_SEQPACKET, 0);
    if (tfd < 0) {
        perror("socket(AF_ALG)");
        return 1;
    }

    if (bind(tfd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        perror("bind(hash)");
        close(tfd);
        return 1;
    }

    opfd = accept(tfd, NULL, 0);
    if (opfd < 0) {
        perror("accept(hash)");
        close(tfd);
        return 1;
    }

    if (send(opfd, msg, strlen(msg), 0) < 0) {
        perror("send(hash)");
        goto out;
    }

    if (recv(opfd, digest, sizeof(digest), 0) != (ssize_t)sizeof(digest)) {
        perror("recv(hash)");
        goto out;
    }

    dump_hex("sha256", digest, sizeof(digest));

out:
    close(opfd);
    close(tfd);
    return 0;
}
```

### 3.3 变体

- 分块摘要:前几次 `send(..., MSG_MORE)`，最后一次不带 `MSG_MORE`
- HMAC:在 `accept()` 前先 `setsockopt(tfd, SOL_ALG, ALG_SET_KEY, key, keylen)`

---

## 4. 示例二:`skcipher` — `cbc(aes)` 加密 32 字节数据

### 4.1 要点

- `salg_type = "skcipher"`
- `salg_name = "cbc(aes)"`
- 必须先 `ALG_SET_KEY`
- `sendmsg()` 里通过 `cmsg` 传:
  - `ALG_SET_OP`
  - `ALG_SET_IV`
- `recv()` 取密文

### 4.2 代码

```c
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <linux/if_alg.h>

#ifndef AF_ALG
#define AF_ALG 38
#endif
#ifndef SOL_ALG
#define SOL_ALG 279
#endif

static void dump_hex(const char *tag, const uint8_t *buf, size_t len)
{
    size_t i;
    printf("%s (%zu bytes): ", tag, len);
    for (i = 0; i < len; i++)
        printf("%02x", buf[i]);
    printf("\n");
}

int main(void)
{
    int tfd = -1, opfd = -1;
    uint8_t key[16] = {0};
    uint8_t iv[16] = {0};
    uint8_t plain[32] = {
        0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,
        0x38,0x39,0x61,0x62,0x63,0x64,0x65,0x66,
        0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,
        0x49,0x4a,0x4b,0x4c,0x4d,0x4e,0x4f,0x50
    };
    uint8_t cipher[32];
    struct sockaddr_alg sa = {
        .salg_family = AF_ALG,
        .salg_type = "skcipher",
        .salg_name = "cbc(aes)",
    };

    struct iovec iov = {
        .iov_base = plain,
        .iov_len = sizeof(plain),
    };

    struct {
        struct cmsghdr cmsghdr;
        uint32_t op;
    } cmsg_op;

    struct {
        struct cmsghdr cmsghdr;
        struct af_alg_iv ivmsg;
        uint8_t ivbuf[16];
    } cmsg_iv;

    struct msghdr msg;
    char control[CMSG_SPACE(sizeof(uint32_t)) +
                 CMSG_SPACE(sizeof(struct af_alg_iv) + 16)];

    memset(&msg, 0, sizeof(msg));
    memset(control, 0, sizeof(control));
    memset(cipher, 0, sizeof(cipher));

    tfd = socket(AF_ALG, SOCK_SEQPACKET, 0);
    if (tfd < 0) {
        perror("socket(AF_ALG)");
        return 1;
    }

    if (bind(tfd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        perror("bind(skcipher)");
        close(tfd);
        return 1;
    }

    if (setsockopt(tfd, SOL_ALG, ALG_SET_KEY, key, sizeof(key)) < 0) {
        perror("setsockopt(ALG_SET_KEY)");
        close(tfd);
        return 1;
    }

    opfd = accept(tfd, NULL, 0);
    if (opfd < 0) {
        perror("accept(skcipher)");
        close(tfd);
        return 1;
    }

    cmsg_op.cmsghdr.cmsg_level = SOL_ALG;
    cmsg_op.cmsghdr.cmsg_type = ALG_SET_OP;
    cmsg_op.cmsghdr.cmsg_len = CMSG_LEN(sizeof(uint32_t));
    cmsg_op.op = ALG_OP_ENCRYPT;

    cmsg_iv.cmsghdr.cmsg_level = SOL_ALG;
    cmsg_iv.cmsghdr.cmsg_type = ALG_SET_IV;
    cmsg_iv.cmsghdr.cmsg_len = CMSG_LEN(sizeof(struct af_alg_iv) + sizeof(iv));
    cmsg_iv.ivmsg.ivlen = sizeof(iv);
    memcpy(cmsg_iv.ivbuf, iv, sizeof(iv));

    memcpy(control, &cmsg_op, CMSG_SPACE(sizeof(uint32_t)));
    memcpy(control + CMSG_SPACE(sizeof(uint32_t)),
           &cmsg_iv,
           CMSG_SPACE(sizeof(struct af_alg_iv) + sizeof(iv)));

    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);

    if (sendmsg(opfd, &msg, 0) < 0) {
        perror("sendmsg(skcipher)");
        goto out;
    }

    if (recv(opfd, cipher, sizeof(cipher), 0) != (ssize_t)sizeof(cipher)) {
        perror("recv(skcipher)");
        goto out;
    }

    dump_hex("plain ", plain, sizeof(plain));
    dump_hex("cipher", cipher, sizeof(cipher));

out:
    close(opfd);
    close(tfd);
    return 0;
}
```

### 4.3 约束

- `cbc(aes)` 明文长度必须是 16 字节整数倍
- 如果想解密,把 `ALG_OP_ENCRYPT` 改成 `ALG_OP_DECRYPT`

---

## 5. 示例三:`aead` — `gcm(aes)` 加密 `AAD || plaintext`

### 5.1 先说明一个重要点

**以下示例以你这棵 `kernel-5.10/crypto/algif_aead.c` 的实现为准。**

这份实现里:

- 加密输入: `AAD || plaintext`
- 加密输出缓冲建议按 **`AAD + ciphertext + tag`** 分配

这和有些上游文档里“输出只看 `ciphertext || tag`”的口径不完全一致。  
你如果是跟这份 SDK 内核交互,建议按当前源码实现理解和验证。

### 5.2 要点

- `salg_type = "aead"`
- `salg_name = "gcm(aes)"`
- 必须先:
  - `ALG_SET_KEY`
  - `ALG_SET_AEAD_AUTHSIZE`
- `sendmsg()` 里通过 `cmsg` 传:
  - `ALG_SET_OP`
  - `ALG_SET_IV`
  - `ALG_SET_AEAD_ASSOCLEN`
- 输入按 `AAD || plaintext`

### 5.3 代码

```c
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <linux/if_alg.h>

#ifndef AF_ALG
#define AF_ALG 38
#endif
#ifndef SOL_ALG
#define SOL_ALG 279
#endif

static void dump_hex(const char *tag, const uint8_t *buf, size_t len)
{
    size_t i;
    printf("%s (%zu bytes): ", tag, len);
    for (i = 0; i < len; i++)
        printf("%02x", buf[i]);
    printf("\n");
}

int main(void)
{
    int tfd = -1, opfd = -1;
    uint8_t key[16] = {0};
    uint8_t iv[12] = {0};
    uint8_t aad[8] = { 'A','A','D','-','T','E','S','T' };
    uint8_t plain[16] = { 'p','l','a','i','n','-','t','e','x','t','-','1','2','3','4','5' };
    uint8_t inbuf[sizeof(aad) + sizeof(plain)];
    uint8_t outbuf[sizeof(aad) + sizeof(plain) + 16];

    struct sockaddr_alg sa = {
        .salg_family = AF_ALG,
        .salg_type = "aead",
        .salg_name = "gcm(aes)",
    };

    struct iovec iov = {
        .iov_base = inbuf,
        .iov_len = sizeof(inbuf),
    };

    struct {
        struct cmsghdr cmsghdr;
        uint32_t op;
    } cmsg_op;

    struct {
        struct cmsghdr cmsghdr;
        struct af_alg_iv ivmsg;
        uint8_t ivbuf[12];
    } cmsg_iv;

    struct {
        struct cmsghdr cmsghdr;
        uint32_t assoclen;
    } cmsg_assoc;

    struct msghdr msg;
    char control[CMSG_SPACE(sizeof(uint32_t)) +
                 CMSG_SPACE(sizeof(struct af_alg_iv) + 12) +
                 CMSG_SPACE(sizeof(uint32_t))];

    memcpy(inbuf, aad, sizeof(aad));
    memcpy(inbuf + sizeof(aad), plain, sizeof(plain));
    memset(outbuf, 0, sizeof(outbuf));
    memset(&msg, 0, sizeof(msg));
    memset(control, 0, sizeof(control));

    tfd = socket(AF_ALG, SOCK_SEQPACKET, 0);
    if (tfd < 0) {
        perror("socket(AF_ALG)");
        return 1;
    }

    if (bind(tfd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        perror("bind(aead)");
        close(tfd);
        return 1;
    }

    if (setsockopt(tfd, SOL_ALG, ALG_SET_KEY, key, sizeof(key)) < 0) {
        perror("setsockopt(ALG_SET_KEY)");
        close(tfd);
        return 1;
    }

    {
        uint32_t authsize = 16;
        if (setsockopt(tfd, SOL_ALG, ALG_SET_AEAD_AUTHSIZE,
                       &authsize, sizeof(authsize)) < 0) {
            perror("setsockopt(ALG_SET_AEAD_AUTHSIZE)");
            close(tfd);
            return 1;
        }
    }

    opfd = accept(tfd, NULL, 0);
    if (opfd < 0) {
        perror("accept(aead)");
        close(tfd);
        return 1;
    }

    cmsg_op.cmsghdr.cmsg_level = SOL_ALG;
    cmsg_op.cmsghdr.cmsg_type = ALG_SET_OP;
    cmsg_op.cmsghdr.cmsg_len = CMSG_LEN(sizeof(uint32_t));
    cmsg_op.op = ALG_OP_ENCRYPT;

    cmsg_iv.cmsghdr.cmsg_level = SOL_ALG;
    cmsg_iv.cmsghdr.cmsg_type = ALG_SET_IV;
    cmsg_iv.cmsghdr.cmsg_len = CMSG_LEN(sizeof(struct af_alg_iv) + sizeof(iv));
    cmsg_iv.ivmsg.ivlen = sizeof(iv);
    memcpy(cmsg_iv.ivbuf, iv, sizeof(iv));

    cmsg_assoc.cmsghdr.cmsg_level = SOL_ALG;
    cmsg_assoc.cmsghdr.cmsg_type = ALG_SET_AEAD_ASSOCLEN;
    cmsg_assoc.cmsghdr.cmsg_len = CMSG_LEN(sizeof(uint32_t));
    cmsg_assoc.assoclen = sizeof(aad);

    memcpy(control, &cmsg_op, CMSG_SPACE(sizeof(uint32_t)));
    memcpy(control + CMSG_SPACE(sizeof(uint32_t)),
           &cmsg_iv,
           CMSG_SPACE(sizeof(struct af_alg_iv) + sizeof(iv)));
    memcpy(control + CMSG_SPACE(sizeof(uint32_t)) +
                  CMSG_SPACE(sizeof(struct af_alg_iv) + sizeof(iv)),
           &cmsg_assoc,
           CMSG_SPACE(sizeof(uint32_t)));

    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);

    if (sendmsg(opfd, &msg, 0) < 0) {
        perror("sendmsg(aead)");
        goto out;
    }

    if (recv(opfd, outbuf, sizeof(outbuf), 0) < 0) {
        perror("recv(aead)");
        goto out;
    }

    dump_hex("aad   ", aad, sizeof(aad));
    dump_hex("plain ", plain, sizeof(plain));
    dump_hex("output", outbuf, sizeof(outbuf));

out:
    close(opfd);
    close(tfd);
    return 0;
}
```

### 5.4 你要知道的坑

- 这棵树里 `recv()` 的输出布局按当前 `algif_aead.c` 理解
- 解密时如果 tag 校验失败,常见错误是 `-EBADMSG`

---

## 6. 示例四:`rng` — 从 `stdrng` 读 32 字节随机数

### 6.1 要点

- `salg_type = "rng"`
- `salg_name = "stdrng"` 或 `jitterentropy_rng`
- 普通路径下 **不需要 `sendmsg`**
- 直接 `recv()` 就会生成随机字节

### 6.2 代码

```c
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <linux/if_alg.h>

#ifndef AF_ALG
#define AF_ALG 38
#endif
#ifndef SOL_ALG
#define SOL_ALG 279
#endif

static void dump_hex(const char *tag, const uint8_t *buf, size_t len)
{
    size_t i;
    printf("%s (%zu bytes): ", tag, len);
    for (i = 0; i < len; i++)
        printf("%02x", buf[i]);
    printf("\n");
}

int main(void)
{
    int tfd = -1, opfd = -1;
    uint8_t rnd[32];
    struct sockaddr_alg sa = {
        .salg_family = AF_ALG,
        .salg_type = "rng",
        .salg_name = "stdrng",
    };

    tfd = socket(AF_ALG, SOCK_SEQPACKET, 0);
    if (tfd < 0) {
        perror("socket(AF_ALG)");
        return 1;
    }

    if (bind(tfd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        perror("bind(rng)");
        close(tfd);
        return 1;
    }

    opfd = accept(tfd, NULL, 0);
    if (opfd < 0) {
        perror("accept(rng)");
        close(tfd);
        return 1;
    }

    if (recv(opfd, rnd, sizeof(rnd), 0) != (ssize_t)sizeof(rnd)) {
        perror("recv(rng)");
        goto out;
    }

    dump_hex("random", rnd, sizeof(rnd));

out:
    close(opfd);
    close(tfd);
    return 0;
}
```

### 6.3 约束

- 这棵内核实现里单次最多生成 128 字节
- 要更多随机数就循环多次 `recv()`

---

## 7. 常见问题速查

### 7.1 为什么 `accept()` 后还要用新 fd?

因为:

- `bind()` 的那个 fd 是 control socket
- `accept()` 返回的才是 op socket
- 真正的数据 I/O 都应该走 op socket

### 7.2 为什么 `skcipher/aead` 要 `sendmsg`,但 `rng` 直接 `recv`?

因为:

- `skcipher/aead` 需要输入数据 + 元信息(IV/op/AAD)
- `rng` 普通语义只是“给我随机字节”

### 7.3 为什么 `hash` 有时 `send()` 完就算好了,有时 `recv()` 才收尾?

因为 `MSG_MORE` 控制的是 hash 状态机:

- 带 `MSG_MORE` -> `update`
- 不带 `MSG_MORE` -> `final`
- 如果你一直没收尾就 `recv()`,内核也会帮你 `final`

### 7.4 `AF_ALG` 走的是硬件还是软件?

取决于 `crypto_alloc_*()` 最后选到的实现。  
当前你这套 x2600 环境里,从 `/proc/crypto` 看基本都是 `*-generic` 软件实现。

---

## 8. 调试建议

### 8.1 先看算法在不在

```bash
cat /proc/crypto
```

确认你要的:

- `sha256`
- `cbc(aes)`
- `gcm(aes)`
- `stdrng`

至少有对应实现。

### 8.2 常见错误码

| 错误 | 常见原因 |
|---|---|
| `-EINVAL` | 长度不对、IV 长度不对、AEAD AAD/tag 布局不对 |
| `-ENOKEY` | 需要 key 却没 `ALG_SET_KEY` |
| `-EBADMSG` | AEAD 解密时 tag 校验失败 |
| `-EAGAIN` | 非阻塞模式下数据/空间不足 |

---

## 9. 最后一句经验话

如果你只是想快速验证内核里的某个算法:

1. 先用 `hash` 示例确认 `AF_ALG` 通路通了
2. 再用 `skcipher` 示例确认 `key + iv + sendmsg/recvmsg` 没问题
3. 最后再碰 `aead`

因为 `aead` 的输入输出布局最容易写错。

---

## TL;DR

- `AF_ALG` 四类最小示例都遵循 `socket -> bind -> setsockopt -> accept -> send/recv`
- `hash` 最简单,最适合先验通路
- `skcipher` 关键是 `ALG_SET_KEY + ALG_SET_IV + ALG_SET_OP`
- `aead` 关键是 `ALG_SET_AEAD_AUTHSIZE + ALG_SET_AEAD_ASSOCLEN + 正确的数据布局`
- `rng` 普通路径只有 `recvmsg`,单次最多 128 字节
