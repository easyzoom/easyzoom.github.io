# 04 threaded irq workqueue

这个骨架用来表达三层职责边界：

- hardirq：只做最小确认
- threaded IRQ：处理中等重量、对时延敏感的逻辑
- workqueue：处理可睡眠的慢路径

## 阅读重点

1. 为什么 top half 不应该做重活
2. 为什么 thread handler 也不应该无限膨胀
3. 为什么恢复、重试、复杂清理更适合 workqueue

## 适合对应的文章

- 《中断上下文、软中断、workqueue与线程化IRQ》
- 《PREEMPT_RT下驱动设计注意事项》
