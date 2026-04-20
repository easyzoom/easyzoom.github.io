---
title: ioctl 设计与用户内核数据交互
author: EASYZOOM
date: 2026/04/07 13:00
categories:
 - Linux驱动开发
tags:
 - ioctl
 - copy_to_user
 - copy_from_user
 - 字符设备
---

# ioctl 设计与用户内核数据交互

## 前言

**C：** `read` 和 `write` 很适合处理“字节流”式的数据交换，但很多驱动并不只是收发数据，它们还需要接受控制命令，例如：设置模式、清空缓冲、查询状态、读取统计信息。这个时候，`ioctl` 就登场了。它本质上是字符设备里的一条“控制通道”，用来承载那些不适合塞进普通读写语义里的操作。

<!-- more -->

## `ioctl` 控制通道流程图

```mermaid
flowchart LR
  A[用户态 ioctl(fd, cmd, arg)] --> B[unlocked_ioctl]
  B --> C{命令分发}
  C --> D[RESET]
  C --> E[GETLEN]
  C --> F[SETMSG]
  E --> G[copy_to_user]
  F --> H[copy_from_user]
```

## 什么时候该考虑 `ioctl`

一个经验判断是：

- **连续数据流**，优先考虑 `read` / `write`
- **控制命令或状态查询**，更适合 `ioctl`

例如：

- 清空缓冲区
- 查询设备当前模式
- 设置某个工作参数
- 获取统计计数

这些操作如果硬塞进 `write` 里，语义通常会比较别扭。

## `ioctl` 的基本接口

用户空间调用形式通常是：

```c
ioctl(fd, cmd, arg);
```

对应到驱动里，字符设备通常实现：

```c
.unlocked_ioctl = ez_unlocked_ioctl,
```

对应函数原型：

```c
static long ez_unlocked_ioctl(struct file *filp,
			      unsigned int cmd,
			      unsigned long arg)
```

这里最重要的两个参数是：

- `cmd`：命令号
- `arg`：用户传来的参数地址或数值

## 命令号不要乱写

`ioctl` 命令号通常不是拍脑袋写一个整数，而是通过内核提供的宏来生成。常见宏有：

- `_IO`
- `_IOR`
- `_IOW`
- `_IOWR`

它们分别表示：

- 无数据传输
- 从内核读到用户
- 从用户写到内核
- 双向传输

例如：

```c
#define EZ_IOC_MAGIC  'E'
#define EZ_IOC_RESET  _IO(EZ_IOC_MAGIC, 0)
#define EZ_IOC_GETLEN _IOR(EZ_IOC_MAGIC, 1, int)
#define EZ_IOC_SETMSG _IOW(EZ_IOC_MAGIC, 2, struct ez_ioc_msg)
```

## 一个自洽的 `ioctl` 示例

下面我们在前一篇字符设备基础上，增加三个命令：

- `EZ_IOC_RESET`：清空缓冲区
- `EZ_IOC_GETLEN`：获取当前缓冲区长度
- `EZ_IOC_SETMSG`：从用户态传入一段字符串

### 共享头文件定义

驱动和用户程序最好共用同一份命令定义头文件，例如 `ez_ioctl.h`。  
内核模块里通常包含 `<linux/ioctl.h>`；如果用户态编译环境不方便引入内核头，也可以在用户程序侧改用 `<sys/ioctl.h>`，但命令号定义必须保持一致。

```c
#ifndef _EZ_IOCTL_H
#define _EZ_IOCTL_H

#include <linux/ioctl.h>

#define EZ_IOC_MAGIC  'E'

struct ez_ioc_msg {
	unsigned int len;
	char data[64];
};

#define EZ_IOC_RESET  _IO(EZ_IOC_MAGIC, 0)
#define EZ_IOC_GETLEN _IOR(EZ_IOC_MAGIC, 1, int)
#define EZ_IOC_SETMSG _IOW(EZ_IOC_MAGIC, 2, struct ez_ioc_msg)

#endif
```

### 驱动侧完整实现

```c
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>

#include "ez_ioctl.h"

#define DEV_NAME "ezctl"
#define EZ_BUF_SIZE 128

static dev_t ez_devno;
static struct cdev ez_cdev;
static struct class *ez_class;

static char ez_buf[EZ_BUF_SIZE];
static int ez_len;
static DEFINE_MUTEX(ez_lock);

static long ez_unlocked_ioctl(struct file *filp, unsigned int cmd,
			      unsigned long arg)
{
	int len;
	struct ez_ioc_msg msg;

	switch (cmd) {
	case EZ_IOC_RESET:
		mutex_lock(&ez_lock);
		ez_len = 0;
		ez_buf[0] = '\0';
		mutex_unlock(&ez_lock);
		return 0;

	case EZ_IOC_GETLEN:
		mutex_lock(&ez_lock);
		len = ez_len;
		mutex_unlock(&ez_lock);

		if (copy_to_user((void __user *)arg, &len, sizeof(len)))
			return -EFAULT;
		return 0;

	case EZ_IOC_SETMSG:
		if (copy_from_user(&msg, (void __user *)arg, sizeof(msg)))
			return -EFAULT;

		if (msg.len >= sizeof(msg.data) || msg.len >= EZ_BUF_SIZE)
			return -EINVAL;

		mutex_lock(&ez_lock);
		memcpy(ez_buf, msg.data, msg.len);
		ez_buf[msg.len] = '\0';
		ez_len = msg.len;
		mutex_unlock(&ez_lock);
		return 0;

	default:
		return -ENOTTY;
	}
}

static int ez_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static int ez_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static const struct file_operations ez_fops = {
	.owner = THIS_MODULE,
	.open = ez_open,
	.release = ez_release,
	.unlocked_ioctl = ez_unlocked_ioctl,
};

static int __init ez_init(void)
{
	int ret;

	ret = alloc_chrdev_region(&ez_devno, 0, 1, DEV_NAME);
	if (ret)
		return ret;

	cdev_init(&ez_cdev, &ez_fops);
	ez_cdev.owner = THIS_MODULE;

	ret = cdev_add(&ez_cdev, ez_devno, 1);
	if (ret)
		goto err_unregister;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
	ez_class = class_create(DEV_NAME "_class");
#else
	ez_class = class_create(THIS_MODULE, DEV_NAME "_class");
#endif
	if (IS_ERR(ez_class)) {
		ret = PTR_ERR(ez_class);
		goto err_cdev;
	}

	device_create(ez_class, NULL, ez_devno, NULL, DEV_NAME);
	mutex_init(&ez_lock);
	ez_len = 0;
	pr_info("ezctl: major=%d minor=%d\n", MAJOR(ez_devno), MINOR(ez_devno));
	return 0;

err_cdev:
	cdev_del(&ez_cdev);
err_unregister:
	unregister_chrdev_region(ez_devno, 1);
	return ret;
}

static void __exit ez_exit(void)
{
	device_destroy(ez_class, ez_devno);
	class_destroy(ez_class);
	cdev_del(&ez_cdev);
	unregister_chrdev_region(ez_devno, 1);
}

module_init(ez_init);
module_exit(ez_exit);

MODULE_LICENSE("GPL");
```

## 这段实现里最重要的点

### 第一，命令号要统一

用户态和内核态必须使用同一套命令号定义。  
如果两边的 `cmd` 不一致，最常见的表现就是：

- 调用了 `ioctl`
- 驱动走到了 `default`
- 返回 `-ENOTTY`

### 第二，`arg` 不是普通内核指针

`arg` 往往来自用户空间，所以：

- 不能直接强转后随便解引用
- 要用 `copy_from_user`
- 要用 `copy_to_user`

这是 `ioctl` 里最容易踩的一类坑。

### 第三，数据结构要尽量简单稳定

入门阶段，建议 `ioctl` 里传递的结构体尽量：

- 字段少
- 长度固定
- 避免嵌套复杂指针

例如上面的：

```c
struct ez_ioc_msg {
	unsigned int len;
	char data[64];
};
```

这就比“结构体里再套一个用户空间指针”更适合初学者理解和验证。

### 第四，这一篇现在可以独立验证

因为上面的代码已经包含了字符设备注册、节点创建和模块入口/出口，所以你可以把它单独整理成一个最小工程来验证，而不必再依赖前一篇的示例骨架。

## 用户态测试程序

下面是一段可以直接验证上述 `ioctl` 行为的用户态代码：

```c
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "ez_ioctl.h"

int main(void)
{
	int fd;
	int len = 0;
	struct ez_ioc_msg msg;

	fd = open("/dev/ezctl", O_RDWR);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	if (ioctl(fd, EZ_IOC_RESET) < 0) {
		perror("EZ_IOC_RESET");
		return 1;
	}

	memset(&msg, 0, sizeof(msg));
	strcpy(msg.data, "hello ioctl");
	msg.len = strlen(msg.data);

	if (ioctl(fd, EZ_IOC_SETMSG, &msg) < 0) {
		perror("EZ_IOC_SETMSG");
		return 1;
	}

	if (ioctl(fd, EZ_IOC_GETLEN, &len) < 0) {
		perror("EZ_IOC_GETLEN");
		return 1;
	}

	printf("current len = %d\n", len);
	close(fd);
	return 0;
}
```

## 验证步骤

### 第一步：编译并加载驱动

```bash
make
sudo insmod ezctl.ko
```

### 第二步：确认节点存在

```bash
ls -l /dev/ezctl
```

### 第三步：编译用户态程序

```bash
gcc user_ezctl.c -o user_ezctl
```

### 第四步：执行测试

```bash
./user_ezctl
```

如果一切正常，应能看到：

```text
current len = 11
```

## 常见问题

### `ioctl` 返回 `Inappropriate ioctl for device`

这通常对应内核侧的 `-ENOTTY`，说明：

- 设备文件不是你以为的那个
- 驱动没有实现对应命令
- 用户态和内核态命令号定义不一致

### 为什么 `EZ_IOC_GETLEN` 要用 `copy_to_user`

因为这是把数据从内核返回给用户态。  
即使只是一个整数，也应该走这条路径。

### 为什么不建议一开始就让 `ioctl` 传复杂指针

因为复杂嵌套结构会让你同时面对：

- 用户空间地址合法性问题
- 长度校验问题
- 拷贝边界问题
- 对齐和兼容性问题

入门阶段先把固定结构体的模型吃透，更稳。

## 小结

`ioctl` 是字符设备里的控制面接口，适合承载“设置参数、查询状态、执行命令”这类操作。学习它的关键不在于记住多少宏，而在于建立三条基本纪律：**命令号统一、用户内核数据显式拷贝、传输结构尽量简单稳定**。到这里为止，字符设备主线里的数据通道和控制通道都已经搭起来了，接下来我们就可以进入平台驱动与设备树这条更贴近嵌入式实战的路线。
