#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>

static int ez_runtime_suspend(struct device *dev)
{
	/* 这里通常关闭时钟、停止硬件队列、保存必要上下文。 */
	dev_info(dev, "runtime suspend\n");
	return 0;
}

static int ez_runtime_resume(struct device *dev)
{
	/* 这里通常恢复时钟、重新建立必要硬件状态。 */
	dev_info(dev, "runtime resume\n");
	return 0;
}

static const struct dev_pm_ops ez_pm_ops = {
	SET_RUNTIME_PM_OPS(ez_runtime_suspend, ez_runtime_resume, NULL)
};

static int ez_probe(struct platform_device *pdev)
{
	pm_runtime_enable(&pdev->dev);
	pm_runtime_set_autosuspend_delay(&pdev->dev, 200);
	pm_runtime_use_autosuspend(&pdev->dev);

	/*
	 * 真正驱动里，硬件访问前一般会通过 pm_runtime_resume_and_get()
	 * 保证设备处于 active；访问结束后再 mark last busy + put autosuspend。
	 */
	dev_info(&pdev->dev, "runtime pm skeleton probe\n");
	return 0;
}

static int ez_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
	return 0;
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("EASYZOOM");
MODULE_DESCRIPTION("Runtime PM skeleton for teaching");
