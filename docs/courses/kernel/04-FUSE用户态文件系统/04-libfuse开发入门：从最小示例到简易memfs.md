---
title: libfuse 开发入门：从最小示例到简易 memfs
author: EASYZOOM
date: 2026/04/22 22:00
categories:
 - Linux内核开发
tags:
 - Linux
 - 内核
 - FUSE
 - libfuse
---

# libfuse 开发入门：从最小示例到简易 memfs

## 前言

**C：** 讲了这么多协议，不动手写点东西心里不踏实。这一章我们从"就 5 个回调"的 `hello` 起步，逐步做一个能 `mkdir / touch / echo > / cat` 的内存文件系统 `memfs`。目标不是造一个生产级的 FS，而是把 libfuse 的 API 手感练出来——看懂官方 `example/` 目录里的大多数代码。

<!-- more -->

## 准备环境

```bash
# Debian / Ubuntu
sudo apt install libfuse3-dev pkg-config build-essential

# Fedora / RHEL
sudo dnf install fuse3-devel pkgconf-pkg-config gcc make

# Arch
sudo pacman -S fuse3 pkgconf gcc make

# 验证
pkg-config --modversion fuse3
# 3.x.x 即可
```

libfuse 有两个版本并存：**2.x 和 3.x**。教程里一律用 3.x，API 更清晰，支持新特性。编译时用：

```bash
gcc hello.c $(pkg-config fuse3 --cflags --libs) -o hello
```

::: tip 挂载点坏了怎么办
初学时 FUSE 进程崩溃或被 Ctrl-C 后，挂载点会变成 "Transport endpoint is not connected"。执行 `fusermount3 -u /tmp/mnt` 即可恢复；如果报 busy，用 `sudo umount -l /tmp/mnt` 懒卸载。
:::

## 第一步：极简 hello world

文件内容就两个，一个叫 `/hello` 的文件读出来是 `Hello, FUSE!\n`。

```c
// hello.c
#define FUSE_USE_VERSION 31
#include <fuse.h>
#include <string.h>
#include <errno.h>
#include <stddef.h>

static const char *hello_path = "/hello";
static const char *hello_str  = "Hello, FUSE!\n";

static int hello_getattr(const char *path, struct stat *st,
                         struct fuse_file_info *fi) {
    (void) fi;
    memset(st, 0, sizeof(*st));
    if (strcmp(path, "/") == 0) {
        st->st_mode  = S_IFDIR | 0755;
        st->st_nlink = 2;
        return 0;
    }
    if (strcmp(path, hello_path) == 0) {
        st->st_mode  = S_IFREG | 0444;
        st->st_nlink = 1;
        st->st_size  = strlen(hello_str);
        return 0;
    }
    return -ENOENT;
}

static int hello_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi,
                         enum fuse_readdir_flags flags) {
    (void) offset; (void) fi; (void) flags;
    if (strcmp(path, "/") != 0) return -ENOENT;
    filler(buf, ".",  NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);
    filler(buf, hello_path + 1, NULL, 0, 0);
    return 0;
}

static int hello_open(const char *path, struct fuse_file_info *fi) {
    if (strcmp(path, hello_path) != 0) return -ENOENT;
    if ((fi->flags & O_ACCMODE) != O_RDONLY) return -EACCES;
    return 0;
}

static int hello_read(const char *path, char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi) {
    (void) fi;
    if (strcmp(path, hello_path) != 0) return -ENOENT;
    size_t len = strlen(hello_str);
    if ((size_t)offset >= len) return 0;
    if (offset + size > len) size = len - offset;
    memcpy(buf, hello_str + offset, size);
    return size;
}

static const struct fuse_operations hello_ops = {
    .getattr = hello_getattr,
    .readdir = hello_readdir,
    .open    = hello_open,
    .read    = hello_read,
};

int main(int argc, char *argv[]) {
    return fuse_main(argc, argv, &hello_ops, NULL);
}
```

编译、挂载、测试：

```bash
gcc hello.c $(pkg-config fuse3 --cflags --libs) -o hello

mkdir /tmp/mnt
./hello -f /tmp/mnt          # -f 前台运行, 看日志方便

# 另开一个终端
ls -la /tmp/mnt
# total 4
# drwxr-xr-x 2 root root    0 Apr 22 22:00 .
# drwxrwxrwt 1 root root 4096 Apr 22 22:00 ..
# -r--r--r-- 1 root root   13 Apr 22 22:00 hello

cat /tmp/mnt/hello
# Hello, FUSE!

# 卸载(原终端 Ctrl-C 也行)
fusermount3 -u /tmp/mnt
```

你已经写完了一个 FUSE 文件系统。关键就是**实现 `fuse_operations` 里你关心的那些回调，其它的留 NULL**——libfuse 会给出合理默认。

## 把它讲清楚：fuse_operations 的分工

| 字段 | 何时调用 | 不实现会怎样 |
|------|----------|-------------|
| `getattr` | 任何需要 stat 的操作，**必须**实现 | mount 不成功 |
| `readdir` | `ls`、`getdents` | 看不到任何条目 |
| `open`/`release` | `open/close` | libfuse 默认直接成功 |
| `read`/`write` | I/O | 返回 -ENOSYS |
| `create` | `open(O_CREAT)` | 退化成 mknod+open |
| `mknod` | 不常用（特殊设备） | -ENOSYS |
| `unlink`/`rename`/`mkdir`/`rmdir` | 对应 syscall | -ENOSYS → 应用看到 EROFS |
| `truncate` | `truncate/ftruncate` | 应用 write 时会失败 |
| `utimens` | `touch`、mtime 修改 | touch 会报错 |
| `chmod`/`chown` | 权限操作 | 应用相应 syscall 失败 |
| `statfs` | `df` | 空值 |
| `flush` | 每次 close 前 | 安全但不刷新 |
| `fsync` | `fsync/fdatasync` | 不保证持久化 |
| `*xattr` | xattr 操作 | -ENOTSUP |
| `init` | 挂载建立时 | 没机会做初始化 |
| `destroy` | 卸载时 | 资源不释放 |

HL API 的一个隐藏特性：你每个回调里都拿到**完整路径 `path`**，libfuse 在底层帮你维护了 nodeid → 路径的映射。这省事，但对大量 rename 场景性能不太友好——到 LL API 就换成 `fuse_ino_t` 了。

## 第二步：可写 memfs

现在做一个基于内存的读写 FS，支持：

- `mkdir` / `rmdir`
- `touch` / `rm`
- `echo hi > file` / `cat file`
- `mv`

采用路径到节点的 hashmap 建树。为了简洁，我们用纯 C，不做锁细化（一个全局 mutex），也不做 LRU——只为把 API 跑通。

```c
// memfs.c (简化版, 约 200 行核心)
#define FUSE_USE_VERSION 31
#include <fuse.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/stat.h>
#include <pthread.h>
#include <time.h>

enum node_type { NT_DIR, NT_FILE };

struct node {
    char  *name;
    enum node_type type;
    mode_t mode;
    uid_t  uid;
    gid_t  gid;
    struct timespec atime, mtime, ctime;
    struct node *parent;
    struct node *child_head;   // 目录: 孩子链表
    struct node *sibling;
    char  *data;               // 文件: 原始数据
    size_t size;
    size_t cap;
};

static struct node *root;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

/* ---- helpers ---- */
static struct node *node_lookup(struct node *dir, const char *name) {
    for (struct node *c = dir->child_head; c; c = c->sibling)
        if (strcmp(c->name, name) == 0) return c;
    return NULL;
}

static struct node *resolve(const char *path) {
    if (strcmp(path, "/") == 0) return root;
    if (path[0] != '/') return NULL;
    struct node *cur = root;
    char *dup = strdup(path + 1), *save = NULL;
    for (char *seg = strtok_r(dup, "/", &save); seg;
              seg = strtok_r(NULL, "/", &save)) {
        if (!cur || cur->type != NT_DIR) { cur = NULL; break; }
        cur = node_lookup(cur, seg);
    }
    free(dup);
    return cur;
}

static struct node *node_new(const char *name, enum node_type t, mode_t mode) {
    struct node *n = calloc(1, sizeof(*n));
    n->name = strdup(name);
    n->type = t;
    n->mode = mode;
    struct fuse_context *c = fuse_get_context();
    n->uid = c->uid;
    n->gid = c->gid;
    clock_gettime(CLOCK_REALTIME, &n->atime);
    n->mtime = n->ctime = n->atime;
    return n;
}

static void node_link(struct node *parent, struct node *child) {
    child->parent = parent;
    child->sibling = parent->child_head;
    parent->child_head = child;
}

static void node_unlink(struct node *n) {
    struct node **pp = &n->parent->child_head;
    while (*pp && *pp != n) pp = &(*pp)->sibling;
    if (*pp) *pp = n->sibling;
}

static void node_free(struct node *n) {
    free(n->name);
    free(n->data);
    free(n);
}

/* 分离父目录与basename */
static int split_parent(const char *path, struct node **pdir, const char **name) {
    char *dup = strdup(path);
    char *slash = strrchr(dup, '/');
    const char *base;
    if (slash == dup) { base = path + 1; *pdir = root; }
    else {
        *slash = 0;
        *pdir = resolve(dup);
        base = path + (slash - dup) + 1;
    }
    if (!*pdir || (*pdir)->type != NT_DIR) { free(dup); return -ENOENT; }
    *name = strdup(base);
    free(dup);
    return 0;
}

/* ---- ops ---- */
static int fs_getattr(const char *path, struct stat *st,
                      struct fuse_file_info *fi) {
    (void) fi;
    pthread_mutex_lock(&g_lock);
    struct node *n = resolve(path);
    if (!n) { pthread_mutex_unlock(&g_lock); return -ENOENT; }
    memset(st, 0, sizeof(*st));
    st->st_mode  = (n->type == NT_DIR ? S_IFDIR : S_IFREG) | n->mode;
    st->st_nlink = (n->type == NT_DIR) ? 2 : 1;
    st->st_uid   = n->uid;
    st->st_gid   = n->gid;
    st->st_size  = (n->type == NT_FILE) ? n->size : 0;
    st->st_atim = n->atime; st->st_mtim = n->mtime; st->st_ctim = n->ctime;
    pthread_mutex_unlock(&g_lock);
    return 0;
}

static int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                      off_t off, struct fuse_file_info *fi,
                      enum fuse_readdir_flags flags) {
    (void) off; (void) fi; (void) flags;
    pthread_mutex_lock(&g_lock);
    struct node *n = resolve(path);
    if (!n || n->type != NT_DIR) { pthread_mutex_unlock(&g_lock); return -ENOENT; }
    filler(buf, ".",  NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);
    for (struct node *c = n->child_head; c; c = c->sibling)
        filler(buf, c->name, NULL, 0, 0);
    pthread_mutex_unlock(&g_lock);
    return 0;
}

static int fs_mkdir(const char *path, mode_t mode) {
    struct node *parent; const char *base;
    int rc = split_parent(path, &parent, &base);
    if (rc) return rc;
    pthread_mutex_lock(&g_lock);
    if (node_lookup(parent, base)) {
        pthread_mutex_unlock(&g_lock); free((void *)base); return -EEXIST;
    }
    struct node *n = node_new(base, NT_DIR, mode);
    node_link(parent, n);
    pthread_mutex_unlock(&g_lock);
    free((void *)base);
    return 0;
}

static int fs_rmdir(const char *path) {
    pthread_mutex_lock(&g_lock);
    struct node *n = resolve(path);
    if (!n || n->type != NT_DIR) { pthread_mutex_unlock(&g_lock); return -ENOENT; }
    if (n->child_head) { pthread_mutex_unlock(&g_lock); return -ENOTEMPTY; }
    node_unlink(n);
    node_free(n);
    pthread_mutex_unlock(&g_lock);
    return 0;
}

static int fs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    (void) fi;
    struct node *parent; const char *base;
    int rc = split_parent(path, &parent, &base);
    if (rc) return rc;
    pthread_mutex_lock(&g_lock);
    if (node_lookup(parent, base)) {
        pthread_mutex_unlock(&g_lock); free((void *)base); return -EEXIST;
    }
    struct node *n = node_new(base, NT_FILE, mode);
    node_link(parent, n);
    pthread_mutex_unlock(&g_lock);
    free((void *)base);
    return 0;
}

static int fs_unlink(const char *path) {
    pthread_mutex_lock(&g_lock);
    struct node *n = resolve(path);
    if (!n || n->type != NT_FILE) { pthread_mutex_unlock(&g_lock); return -ENOENT; }
    node_unlink(n);
    node_free(n);
    pthread_mutex_unlock(&g_lock);
    return 0;
}

static int fs_read(const char *path, char *buf, size_t size, off_t off,
                   struct fuse_file_info *fi) {
    (void) fi;
    pthread_mutex_lock(&g_lock);
    struct node *n = resolve(path);
    if (!n || n->type != NT_FILE) { pthread_mutex_unlock(&g_lock); return -ENOENT; }
    if ((size_t)off >= n->size) { pthread_mutex_unlock(&g_lock); return 0; }
    if (off + size > n->size) size = n->size - off;
    memcpy(buf, n->data + off, size);
    pthread_mutex_unlock(&g_lock);
    return size;
}

static int fs_write(const char *path, const char *buf, size_t size, off_t off,
                    struct fuse_file_info *fi) {
    (void) fi;
    pthread_mutex_lock(&g_lock);
    struct node *n = resolve(path);
    if (!n || n->type != NT_FILE) { pthread_mutex_unlock(&g_lock); return -ENOENT; }
    size_t end = off + size;
    if (end > n->cap) {
        size_t nc = n->cap ? n->cap : 256;
        while (nc < end) nc *= 2;
        n->data = realloc(n->data, nc);
        n->cap  = nc;
    }
    memcpy(n->data + off, buf, size);
    if (end > n->size) n->size = end;
    clock_gettime(CLOCK_REALTIME, &n->mtime);
    pthread_mutex_unlock(&g_lock);
    return size;
}

static int fs_truncate(const char *path, off_t size, struct fuse_file_info *fi) {
    (void) fi;
    pthread_mutex_lock(&g_lock);
    struct node *n = resolve(path);
    if (!n || n->type != NT_FILE) { pthread_mutex_unlock(&g_lock); return -ENOENT; }
    if ((size_t)size > n->cap) {
        n->data = realloc(n->data, size);
        memset(n->data + n->size, 0, size - n->size);
        n->cap = size;
    }
    n->size = size;
    pthread_mutex_unlock(&g_lock);
    return 0;
}

static int fs_rename(const char *from, const char *to, unsigned int flags) {
    if (flags) return -EINVAL;            // 忽略 RENAME_NOREPLACE 等
    pthread_mutex_lock(&g_lock);
    struct node *src = resolve(from);
    if (!src) { pthread_mutex_unlock(&g_lock); return -ENOENT; }
    struct node *new_parent; const char *new_name;
    int rc = split_parent(to, &new_parent, &new_name);
    if (rc) { pthread_mutex_unlock(&g_lock); return rc; }
    struct node *existing = node_lookup(new_parent, new_name);
    if (existing) { node_unlink(existing); node_free(existing); }
    node_unlink(src);
    free(src->name);
    src->name = strdup(new_name);
    node_link(new_parent, src);
    pthread_mutex_unlock(&g_lock);
    free((void *)new_name);
    return 0;
}

static int fs_utimens(const char *path, const struct timespec tv[2],
                      struct fuse_file_info *fi) {
    (void) fi;
    pthread_mutex_lock(&g_lock);
    struct node *n = resolve(path);
    if (!n) { pthread_mutex_unlock(&g_lock); return -ENOENT; }
    n->atime = tv[0];
    n->mtime = tv[1];
    pthread_mutex_unlock(&g_lock);
    return 0;
}

static void *fs_init(struct fuse_conn_info *conn, struct fuse_config *cfg) {
    (void) conn;
    cfg->kernel_cache = 1;         // 允许内核缓存数据页
    root = node_new("/", NT_DIR, 0755);
    return NULL;
}

static void fs_destroy(void *userdata) {
    (void) userdata;
    /* 简版: 进程退出会自动释放, 生产代码要递归 free */
}

static const struct fuse_operations memfs_ops = {
    .getattr  = fs_getattr,
    .readdir  = fs_readdir,
    .mkdir    = fs_mkdir,
    .rmdir    = fs_rmdir,
    .create   = fs_create,
    .unlink   = fs_unlink,
    .read     = fs_read,
    .write    = fs_write,
    .truncate = fs_truncate,
    .rename   = fs_rename,
    .utimens  = fs_utimens,
    .init     = fs_init,
    .destroy  = fs_destroy,
};

int main(int argc, char *argv[]) {
    return fuse_main(argc, argv, &memfs_ops, NULL);
}
```

编译、跑、测：

```bash
gcc memfs.c $(pkg-config fuse3 --cflags --libs) -lpthread -o memfs
./memfs -f /tmp/mnt

# 另一终端
cd /tmp/mnt
mkdir a
echo "hi from memfs" > a/greet.txt
cat a/greet.txt          # hi from memfs
mv a/greet.txt a/hello.txt
ls a                     # hello.txt
rm a/hello.txt
rmdir a
```

200 行左右你就有了一个能用的内存文件系统。

## 调试技巧

### 1. 前台 + 调试日志

```bash
./memfs -f -d /tmp/mnt
```

- `-f`：不 daemon 化，便于 Ctrl-C；
- `-d`：libfuse debug，打印每个回调；
- 再加你自己的 printf 就完事了。

### 2. 常见挂载选项

```bash
./memfs -o allow_other,default_permissions /tmp/mnt
```

- `allow_other`：非挂载者也能访问（要在 `/etc/fuse.conf` 打开）；
- `default_permissions`：**让内核做权限检查**（基于 getattr 返回的 mode/uid/gid），不用自己实现；
- `auto_unmount`：进程退出时自动卸载（很有用）；
- `debug`：等同 `-d`；
- `direct_io` / `kernel_cache` / `auto_cache`：控制缓存策略。

### 3. 单步调试

```bash
gdb --args ./memfs -f -s /tmp/mnt
# -s 单线程, 调试无并发干扰
(gdb) b fs_read
(gdb) run
```

在另一个终端 `cat /tmp/mnt/foo`，会直接命中断点。

### 4. 崩了怎么办

FUSE 进程如果段错误，挂载点变成"Transport endpoint is not connected"。手动清理：

```bash
fusermount3 -u /tmp/mnt      # 首选
sudo umount -l /tmp/mnt      # 懒卸载, 强力
```

## 低层 API 速览

上面的代码是高层 API。相同功能用 **低层 API** 写的话，入口是：

```c
static const struct fuse_lowlevel_ops ll_ops = {
    .init     = ll_init,
    .lookup   = ll_lookup,
    .forget   = ll_forget,
    .getattr  = ll_getattr,
    .readdir  = ll_readdir,
    // ...
};

int main(int argc, char *argv[]) {
    struct fuse_session *se;
    struct fuse_cmdline_opts opts;
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

    fuse_parse_cmdline(&args, &opts);
    se = fuse_session_new(&args, &ll_ops, sizeof(ll_ops), NULL);
    fuse_session_mount(se, opts.mountpoint);
    fuse_set_signal_handlers(se);

    int rc = opts.singlethread
             ? fuse_session_loop(se)
             : fuse_session_loop_mt(se, &(struct fuse_loop_config){});

    fuse_session_unmount(se);
    fuse_session_destroy(se);
    return rc;
}
```

回调看起来像这样：

```c
static void ll_getattr(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi) {
    struct stat st = my_build_stat(ino);
    fuse_reply_attr(req, &st, 1.0);     // attr_valid = 1s
}

static void ll_lookup(fuse_req_t req, fuse_ino_t parent, const char *name) {
    fuse_ino_t ino = my_lookup(parent, name);
    if (!ino) { fuse_reply_err(req, ENOENT); return; }
    struct fuse_entry_param e = { .ino = ino, .generation = 1,
                                  .attr_timeout = 1.0, .entry_timeout = 1.0 };
    my_build_stat_into(&e.attr, ino);
    fuse_reply_entry(req, &e);
}
```

核心差异：

- 你拿到 `fuse_req_t`，可以**异步**处理，想什么时候 reply 都行；
- 参数全是 `fuse_ino_t`，**必须**自己维护 ino ↔ 后端对象的映射和引用计数（`forget` 回调很关键）；
- 缓存时长 `attr_timeout / entry_timeout` 直接由你指定；
- 支持 `fuse_reply_iov` / `fuse_reply_buf_zerocopy` 做零拷贝回复。

实际生产代码基本都是 LL API，因为性能和控制力远超 HL。

## 学习路径建议

1. **`hello.c` + `passthrough.c`**（libfuse examples）：看高层 API 的全貌；
2. 自己写一个 memfs（本文这种）：熟悉回调；
3. **`passthrough_ll.c`**：看低层 API 的标准写法；
4. **`passthrough_hp.cc`**：看工业级 FUSE（多线程、clone fd、splice、BPF）；
5. 开源项目：`virtiofsd`（Rust）、`JuiceFS`（Go）、`s3fs-fuse`（C++）；
6. 读 `fs/fuse/*.c`：从用户态反过来看内核。

## 本章小结

- libfuse 给你两套 API：HL 以路径为中心、LL 以 inode 为中心；
- 最小 FUSE 只要实现 `getattr/readdir/open/read` 四个回调；
- 一个 200 行 memfs 就能涵盖 mkdir/rmdir/create/unlink/read/write/rename 的主要路径；
- 调试三板斧：`-f -d`、gdb `-s`、`auto_unmount`；
- 要做性能和特性就转到 LL API，并且把 `/etc/fuse.conf` 和挂载选项理清楚。

## 练习

1. **改 hello.c**：加一个 `/world` 文件，内容是当前时间戳（每次 `cat /tmp/mnt/world` 看到的时间不同）。
2. **给 memfs 加 symlink**：实现 `fs_symlink` 和 `fs_readlink`，让 `ln -s` 能用。提示：在 `node` 里增加一个 `NT_LINK` 类型。
3. **挂载 + 压力测试**：用 `fio --name=t --directory=/tmp/mnt --rw=randwrite --bs=4k --numjobs=4 --size=1m --time_based --runtime=10` 跑 10 秒，观察 memfs 的 CPU 占用和有没有死锁。

下一章我们把重心切到性能：writeback cache、splice、多线程、io_uring、passthrough 依次展开，看 FUSE 是怎么从 "5 倍慢" 变成 "接近原生" 的。
