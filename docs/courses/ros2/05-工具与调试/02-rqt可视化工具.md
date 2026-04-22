---
title: rqt 可视化工具
author: EASYZOOM
date: 2026/04/24 10:00
categories:
 - ROS2入门与实践
tags:
 - ROS2
 - rqt
 - 可视化
 - 调试
---

# rqt 可视化工具

## 前言

**C：** 命令行工具虽然高效，但某些场景下一张图胜过千行字——比如想看整个系统的节点和话题连接关系、想实时查看传感器数据的曲线、想监控参数变化。rqt 是 ROS 2 自带的 GUI 调试工具集，插件化的设计让它在不同场景下都能派上用场。本篇介绍 rqt 的核心插件和常用操作，帮助你从纯命令行调试升级到可视化调试。

<!-- more -->

## rqt 概述

rqt 是一个基于 Qt 的插件化 GUI 框架。它有两种使用方式：

1. **独立运行插件**：`ros2 run rqt_plugin_name rqt_plugin_name`
2. **在 rqt 主界面中组合使用**：`ros2 run rqt_gui rqt_gui` 或直接 `rqt`

```bash
# 启动 rqt 主界面（空面板）
rqt

# 直接启动某个插件
ros2 run rqt_graph rqt_graph
ros2 run rqt_topic rqt_topic
ros2 run rqt_plot rqt_plot
```

## rqt_graph：系统通信图

这是使用频率最高的 rqt 插件，用于可视化节点和话题之间的连接关系。

```bash
# 直接启动
ros2 run rqt_graph rqt_graph

# 或在 rqt 主界面中通过菜单添加：Plugins → Introspection → Node Graph
```

### 核心功能

- **节点关系图**：显示所有节点和话题的连接
- **隐藏/显示**：可以隐藏 dead sinks、隐藏 debug 节点
- **刷新**：右上角可以选择自动刷新或手动刷新
- **节点信息**：双击节点可以查看详细信息

### 实用技巧

```bash
# 启动时刷新频率设高一些
# 在 rqt_graph 界面中：勾选 "Refresh every" 并设置间隔

# 如果节点太多，可以在节点名上右键 "Hide"
# 或者使用过滤器只显示特定命名空间下的节点
```

::: tip 笔者说
排查"为什么节点 A 收不到节点 B 的数据"时，rqt_graph 是第一步——看两个节点之间是否真的有连线。没有连线就说明话题名不匹配或 QoS 不兼容。
:::

## rqt_topic：话题监控

```bash
ros2 run rqt_topic rqt_topic
```

功能：
- **话题列表**：显示所有活跃话题、类型、发布频率、带宽
- **实时数据预览**：选中话题可以实时查看消息内容
- **排序和过滤**：按名称、频率、带宽排序

适合快速了解系统中所有话题的运行状态，确认数据流是否正常。

## rqt_plot：实时数据绘图

```bash
ros2 run rqt_plot rqt_plot

# 直接指定要绘制的话题字段
ros2 run rqt_plot rqt_plot /topic/field_name
```

使用场景：
- 观察传感器数据的变化趋势
- 监控控制输出的稳定性
- 调试 PID 参数时观察响应曲线

操作方法：
1. 在上方输入框中输入话题字段路径，如 `/sensor_data/temperature`
2. 可以同时绘制多个字段（用空格分隔）
3. 支持暂停、缩放、清空等操作

::: warning 注意
`rqt_plot` 适合低频数据（< 50Hz）。高频率数据（如 100Hz 以上的 IMU 数据）建议先降采样，否则绘图界面会卡顿。
:::

## rqt_publisher：消息发布器

```bash
ros2 run rqt_publisher rqt_publisher
```

提供一个 GUI 界面来发布消息，不需要记命令行语法：
1. 选择话题或输入话题名
2. 选择消息类型
3. 在表单中填写字段值
4. 点击 "Publish" 发送

适合不熟悉消息字段结构的开发者快速测试。

## rqt_service_caller：服务调用器

```bash
ros2 run rqt_service_caller rqt_service_caller
```

与服务发布器的用法类似，GUI 方式调用服务：
1. 选择服务
2. 填写请求字段
3. 点击 "Call" 并查看响应

## rqt_console：日志查看器

```bash
ros2 run rqt_console rqt_console
```

功能强大的日志查看工具，比终端直接看 `ros2 run` 的输出更方便：
- 按日志级别过滤（DEBUG、INFO、WARN、ERROR、FATAL）
- 按节点名过滤
- 按时间范围筛选
- 日志消息高亮显示
- 支持正则表达式搜索

::: tip 笔者说
`rqt_console` 在排查复杂系统问题时特别有用。你可以先过滤出所有 ERROR 级别的日志，再逐步放宽条件，缩小问题范围。
:::

## rqt_logger_level：日志级别设置

```bash
ros2 run rqt_logger_level rqt_logger_level
```

可以在 GUI 中动态修改节点的日志输出级别：

| 级别 | 说明 |
| --- | --- |
| DEBUG | 所有日志（最详细） |
| INFO | 一般信息及以上 |
| WARN | 警告及以上 |
| ERROR | 仅错误 |
| FATAL | 仅致命错误 |

这在调试时非常实用——默认 INFO 级别可能刷屏太快，临时调高到 WARN 可以更清楚地看到问题。

## rqt_reconfigure：动态参数调参

```bash
ros2 run rqt_reconfigure rqt_reconfigure
```

提供 GUI 界面来动态调整节点的参数（需要节点使用 `dynamic_reconfigure` 或 `rclpy`/`rclcpp` 的参数回调）：
- 以树形结构展示所有参数
- 可以直接拖动滑块、勾选复选框修改参数
- 修改后立即生效，无需重启节点

::: warning 注意
`rqt_reconfigure` 需要 `ros2_control` 或 `rqt_gui_cpp` 等额外依赖，某些发行版可能需要手动安装。
:::

## rqt_robot_steering：手动控制

```bash
ros2 run rqt_robot_steering rqt_robot_steering
```

提供一个简单的键盘/滑块界面来发送 `cmd_vel` 话题，可以用来手动控制移动机器人。

## 插件管理

在 `rqt` 主界面中，可以通过菜单管理插件：

```
Plugins 菜单
├── Introspection
│   ├── Node Graph (rqt_graph)
│   ├── Process Monitor
│   └── Package Graph
├── Topics
│   ├── Message Publisher (rqt_publisher)
│   ├── Message Type Browser
│   └── Topic Monitor (rqt_topic)
├── Services
│   ├── Service Caller (rqt_service_caller)
│   └── Service Type Browser
├── Logging
│   ├── Console (rqt_console)
│   └── Logger Level (rqt_logger_level)
└── Visualization
    ├── Plot (rqt_plot)
    ├── Image View
    └── Robot Steering
```

可以同时打开多个插件，拖拽排列在同一个窗口中，保存为自定义布局。

## 常见问题

### rqt 启动报错 "Could not find plugin"

通常是因为缺少对应的包：

```bash
# 安装常用 rqt 插件
sudo apt install ros-humble-rqt-graph ros-humble-rqt-topic ros-humble-rqt-plot \
                 ros-humble-rqt-publisher ros-humble-rqt-service-caller \
                 ros-humble-rqt-console ros-humble-rqt-robot-steering
```

### rqt 图是空的

检查 ROS 2 节点是否在运行、`ROS_DOMAIN_ID` 是否一致。rqt 本身也是一个 ROS 2 节点，需要和目标节点在同一个 DDS 域中才能发现。

### rqt_plot 数据显示不正常

确认话题字段路径是否正确。可以在 `rqt_topic` 中先查看消息结构，确认字段名后再输入到 `rqt_plot`。

## 小结

rqt 是 ROS 2 可视化调试的核心工具集，常用插件：

| 插件 | 用途 | 命令 |
| --- | --- | --- |
| rqt_graph | 系统通信图 | `ros2 run rqt_graph rqt_graph` |
| rqt_topic | 话题监控 | `ros2 run rqt_topic rqt_topic` |
| rqt_plot | 数据绘图 | `ros2 run rqt_plot rqt_plot` |
| rqt_publisher | 消息发布 | `ros2 run rqt_publisher rqt_publisher` |
| rqt_service_caller | 服务调用 | `ros2 run rqt_service_caller rqt_service_caller` |
| rqt_console | 日志查看 | `ros2 run rqt_console rqt_console` |
| rqt_logger_level | 日志级别 | `ros2 run rqt_logger_level rqt_logger_level` |

建议养成"先 rqt_graph 看全局，再 rqt_topic/echo 看细节"的调试习惯。下一篇总结 ROS 2 日志系统与常见问题排查方法。
