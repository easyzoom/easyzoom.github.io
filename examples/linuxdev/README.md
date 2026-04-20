# Linux 驱动实验源码

当前提供 7 组与文章配套的最小实验/骨架：

- `01-hello-lkm/`：对应《第一个内核模块与 Makefile》
- `02-ezpipe/`：对应《open、read、write、release 的最小实现》
- `03-dma-mapping-skeleton/`：对应 DMA、cache 一致性、SG 映射相关高级文章
- `04-threaded-irq-workqueue/`：对应 IRQ、workqueue、线程化 IRQ 相关高级文章
- `05-tracing-debug-playbook/`：对应 ftrace、perf、lockdep、现场排障相关文章
- `06-runtime-pm-skeleton/`：对应 runtime PM、suspend/resume 相关文章
- `07-regmap-pinctrl-skeleton/`：对应 regmap、pinctrl 与寄存器抽象相关文章

建议使用与当前运行内核匹配的头文件环境进行编译，例如：

```bash
uname -r
ls -d /lib/modules/$(uname -r)/build
```

然后进入对应目录执行：

```bash
make
```

用户态测试程序请在源码目录中单独编译，例如：

```bash
gcc user_ezpipe.c -o user_ezpipe
```

高级专题中的部分目录属于“讲解骨架”而非真实硬件驱动，它们更强调：

- 生命周期示意
- API 组合方式
- 排障命令与操作顺序

因此阅读这些目录时，请重点看注释、时序和目录说明，而不是追求直接上板运行。
