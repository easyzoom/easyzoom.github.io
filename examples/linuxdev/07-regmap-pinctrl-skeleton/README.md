# 07 regmap pinctrl skeleton

这个骨架不是完整设备驱动，而是强调两个抽象层的职责边界：

- `regmap`：统一寄存器访问与位更新
- `pinctrl`：统一运行态/休眠态引脚状态切换

## 阅读重点

1. 驱动逻辑应更多面向“状态切换”，而不是散落裸寄存器读写
2. pinctrl 状态适合与 PM 过程协同
3. regmap/pinctrl 更适合做长期维护和多平台适配

## 适合对应的文章

- 《regmap、pinctrl与寄存器访问抽象》
- 《clock、reset、regulator协同设计》
