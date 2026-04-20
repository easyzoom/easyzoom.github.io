# 06 runtime pm skeleton

这个骨架展示 runtime PM 最关键的几个边界：

- `runtime_suspend` / `runtime_resume`
- autosuspend delay
- probe/remove 中启停 runtime PM

## 阅读重点

1. 访问硬件前要先保证设备处于 active
2. 请求结束后要正确归还活跃引用
3. runtime PM 与系统 PM、错误恢复要保持对称

## 适合对应的文章

- 《runtimePM与设备空闲管理》
- 《suspend与resume流程及常见陷阱》
