#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/workqueue.h>

struct ez_irq_demo {
	int irq;
	struct work_struct slow_work;
};

static void ez_slow_work(struct work_struct *work)
{
	/*
	 * 这里代表可睡眠的慢路径：
	 * 重新初始化、恢复、较重日志、状态修复等。
	 */
	pr_info("ez_irq_demo: workqueue runs slow path\n");
}

static irqreturn_t ez_irq_top(int irq, void *data)
{
	/*
	 * top half 只做最小工作：
	 * 1. 确认中断
	 * 2. 摘取关键状态
	 * 3. 唤醒线程化 handler
	 */
	return IRQ_WAKE_THREAD;
}

static irqreturn_t ez_irq_thread(int irq, void *data)
{
	struct ez_irq_demo *demo = data;

	/*
	 * 线程化 IRQ 适合处理中等重量、但仍较敏感的路径。
	 * 再慢、再复杂的恢复工作可以继续丢到 workqueue。
	 */
	schedule_work(&demo->slow_work);
	return IRQ_HANDLED;
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("EASYZOOM");
MODULE_DESCRIPTION("Threaded IRQ and workqueue skeleton");
