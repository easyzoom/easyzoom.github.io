---
title: 第一个内核模块与 Makefile
author: EASYZOOM
date: 2026/04/07 13:00
categories:
 - Linux驱动开发
tags:
 - Linux
 - 内核模块
 - Makefile
 - Kbuild
---

# 第一个内核模块与 Makefile

## 前言

**C：** 驱动学习最适合的第一步，不是上来就写 GPIO、I2C、SPI，而是先写一个最小内核模块，让它能够被顺利编译、加载、卸载，并在内核日志中留下自己的痕迹。这个过程看似简单，却会把后续所有驱动开发都要用到的基础动作串起来：源码组织、Kbuild、模块元数据、内核日志和加载验证。

<!-- more -->

## 构建与加载流程图

```mermaid
flowchart LR
  A[hello.c + Makefile] --> B[make]
  B --> C[hello.ko]
  C --> D[insmod]
  D --> E[dmesg 验证]
  E --> F[rmmod]
```

## 为什么先学内核模块

在 Linux 里，很多驱动都可以以 **可加载内核模块** 的形式存在，也就是我们常说的 `.ko` 文件。  
模块的好处很明显：

- 不用每次都重编整个内核。
- 可以运行时加载与卸载，便于调试。
- 更适合作为驱动开发和验证的第一步。

对初学者而言，一个最小模块能帮你确认三件最关键的事：

1. 当前系统具备编译外部模块的环境。
2. 你知道如何用内核提供的构建系统来编译它。
3. 你能通过 `dmesg` 看到模块的运行痕迹。

## 最小模块源码

先在一个空目录里创建 `hello.c`，内容如下：

```c
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

static int __init hello_init(void)
{
	pr_info("hello_lkm: init\n");
	return 0;
}

static void __exit hello_exit(void)
{
	pr_info("hello_lkm: exit\n");
}

module_init(hello_init);
module_exit(hello_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("EASYZOOM");
MODULE_DESCRIPTION("A minimal hello world Linux kernel module");
```

## 这段代码都在做什么

### `hello_init` 和 `hello_exit`

- `hello_init`：模块加载时执行。
- `hello_exit`：模块卸载时执行。

这两个函数本质上就是模块的“入口”和“出口”。

### `module_init` 和 `module_exit`

这两个宏是把你的函数注册给内核，让内核知道：

- 加载模块时该调用谁
- 卸载模块时该调用谁

### `pr_info`

`pr_info` 是内核里常用的日志输出接口，比直接写 `printk(KERN_INFO "...")` 更简洁。  
后面我们会大量用它来观察驱动行为。

### `MODULE_LICENSE`

这一行不要忽略。  
模块许可证不仅影响元数据展示，也会影响某些符号是否允许被模块使用。教程示例通常使用 `GPL`。

## 用 Kbuild 编译，而不是自己拿 gcc 硬编

很多第一次接触内核模块的同学，第一反应是：

```bash
gcc hello.c -o hello
```

这是不对的。  
内核模块不是普通用户态程序，必须通过 Linux 内核自己的构建系统 **Kbuild** 来编译。

::: tip 配套源码
本文对应的可运行示例已经放到仓库里的 `examples/linuxdev/01-hello-lkm/`，你可以直接对照 `hello.c` 和 `Makefile` 阅读与实验。
:::

创建同目录下的 `Makefile`：

```makefile
obj-m += hello.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD  := $(CURDIR)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
```

## 这份 Makefile 的关键点

### `obj-m += hello.o`

表示当前目录下要构建一个模块，模块源文件对应 `hello.c`，最终会生成 `hello.ko`。

### `KDIR`

指向当前运行内核对应的构建目录：

```bash
/lib/modules/$(uname -r)/build
```

这里非常关键，必须和当前运行内核版本一致，否则很容易出现模块格式不匹配。

### `M=$(PWD)`

告诉内核构建系统：当前这是一个**外部模块目录**，请到这里来找源文件并完成构建。

## 编译模块

在源码目录中执行：

```bash
make
```

正常情况下会看到类似输出，并在当前目录生成一组文件：

- `hello.o`
- `hello.mod`
- `hello.mod.c`
- `hello.ko`
- `Module.symvers`

我们最关心的是最终产物 `hello.ko`。

可以再用 `modinfo` 看看模块元数据：

```bash
modinfo hello.ko
```

输出里应能看到 `description`、`author`、`license` 等信息。

## 加载与卸载模块

加载：

```bash
sudo insmod hello.ko
```

查看日志：

```bash
sudo dmesg -T | tail -n 20
```

你应该能看到类似：

```text
hello_lkm: init
```

卸载：

```bash
sudo rmmod hello
sudo dmesg -T | tail -n 20
```

这时应看到：

```text
hello_lkm: exit
```

## 常用辅助命令

### 看模块是否已加载

```bash
lsmod | grep hello
```

### 看模块详情

```bash
modinfo hello.ko
```

### 看当前内核版本

```bash
uname -r
```

这些命令在后面排错时会非常常用。

## 验证步骤

建议你完整走一次下面的流程：

1. 确认构建目录存在：

```bash
ls -d /lib/modules/$(uname -r)/build
```

2. 执行：

```bash
make
```

3. 验证 `hello.ko` 生成成功：

```bash
ls hello.ko
```

4. 加载模块并查看日志：

```bash
sudo insmod hello.ko
sudo dmesg -T | tail -n 20
```

5. 卸载模块并再次查看日志：

```bash
sudo rmmod hello
sudo dmesg -T | tail -n 20
```

如果这 5 步都走通，说明你的模块开发基本环境已经搭好了。

## 常见问题

### `fatal error: linux/init.h: No such file or directory`

一般说明你不是通过 Kbuild 编译，或者内核头文件 / 构建目录没有安装完整。

### `Invalid module format`

通常表示模块与当前运行内核不匹配。  
比如你用一个内核版本的 headers 编译，却在另一个版本的系统上加载。

### `Operation not permitted`

一般先检查是否用了 `sudo`。  
如果已经使用 root 仍失败，则要考虑 Secure Boot、模块签名策略等系统限制。

### `dmesg` 里没看到日志

先确认模块是否真的成功加载，可用：

```bash
lsmod | grep hello
```

如果已加载但看不到日志，再去看 `journalctl -k` 或下一篇介绍的日志级别控制。

## 小结

最小内核模块的意义，不在于它做了多少功能，而在于它帮你跑通了 Linux 驱动开发最基础的闭环：**源码 -> Kbuild -> `.ko` -> `insmod` -> `dmesg` -> `rmmod`**。只要这个闭环是通的，后面再往模块里增加字符设备、中断处理和资源管理，思路就会顺很多。下一篇，我们把注意力放到内核日志和常用调试工具上，为后续驱动排错打基础。
