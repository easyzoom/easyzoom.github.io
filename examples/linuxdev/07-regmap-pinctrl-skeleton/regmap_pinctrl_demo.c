#include <linux/module.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

struct ez_demo {
	struct regmap *regmap;
	struct pinctrl *pct;
	struct pinctrl_state *state_default;
	struct pinctrl_state *state_sleep;
};

static int ez_select_default_state(struct ez_demo *demo)
{
	if (IS_ERR_OR_NULL(demo->state_default))
		return 0;
	return pinctrl_select_state(demo->pct, demo->state_default);
}

static int ez_select_sleep_state(struct ez_demo *demo)
{
	if (IS_ERR_OR_NULL(demo->state_sleep))
		return 0;
	return pinctrl_select_state(demo->pct, demo->state_sleep);
}

static void ez_update_ctrl(struct ez_demo *demo)
{
	/* 真实驱动里这里通常会用 regmap_update_bits() 修改控制寄存器。 */
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("EASYZOOM");
MODULE_DESCRIPTION("regmap and pinctrl skeleton for teaching");
