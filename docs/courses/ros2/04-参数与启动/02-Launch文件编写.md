---
title: Launch 文件编写
author: EASYZOOM
date: 2026/04/24 10:00
categories:
 - ROS2入门与实践
tags:
 - ROS2
 - Launch
 - 启动配置
---

# Launch 文件编写

## 前言

**C：** 手动一个个 `ros2 run` 启动节点，在调试时还行，但一旦节点多了、参数多了，每次启动都要敲一长串命令，既容易出错又难以复现。Launch 文件就是用来解决这个问题的——它可以把多个节点、参数配置、环境变量等组织成一个文件，一条命令启动整个系统。本篇从最简单的 Launch 文件讲起，逐步覆盖常用的配置技巧。

<!-- more -->

## Launch 系统概述

ROS 2 提供了三种编写 Launch 文件的方式：

| 方式 | 语言 | 特点 |
| --- | --- | --- |
| Python（推荐） | `.launch.py` | 灵活、可编程、支持条件逻辑和变量替换 |
| XML | `.launch.xml` | ROS 1 兼容风格，功能较有限 |
| YAML | `.launch.yaml` | 声明式风格，适合简单场景 |

::: tip 笔者说
新项目建议使用 Python 格式的 Launch 文件。XML 和 YAML 格式主要用于兼容 ROS 1 迁移的旧项目。
:::

## 目录结构

Launch 文件通常放在功能包的 `launch/` 目录下：

```text
my_robot/
├── launch/
│   ├── robot.launch.py
│   ├── simulation.launch.py
│   └── navigation.launch.py
├── src/
├── CMakeLists.txt（或 setup.py）
└── package.xml
```

## Python Launch 文件基础

### 最小 Launch 文件

```python
# launch/robot.launch.py
from launch import LaunchDescription
from launch_ros.actions import Node

def generate_launch_description():
    return LaunchDescription([
        Node(
            package='my_pkg',
            executable='my_node',
            name='my_node',
            output='screen',
        ),
    ])
```

运行：

```bash
ros2 launch my_pkg robot.launch.py
```

### 安装 Launch 文件

**ament_cmake 包**（CMakeLists.txt）：

```cmake
install(DIRECTORY launch/
  DESTINATION share/${PROJECT_NAME}/launch
)
```

**ament_python 包**（setup.py）：

```python
import os
from glob import glob
from setuptools import setup

setup(
    # ...其他配置...
    data_files=[
        ('share/' + package_name + '/launch', glob('launch/*.py')),
    ],
)
```

## 启动多个节点

```python
from launch import LaunchDescription
from launch_ros.actions import Node

def generate_launch_description():
    return LaunchDescription([
        # 传感器驱动节点
        Node(
            package='lidar_driver',
            executable='lidar_node',
            name='lidar_front',
            namespace='sensors',
            output='screen',
            parameters=[{
                'frame_id': 'lidar_front_link',
                'scan_frequency': 10.0,
            }],
        ),

        # 数据处理节点
        Node(
            package='pointcloud_filter',
            executable='filter_node',
            name='voxel_filter',
            namespace='processing',
            output='screen',
            parameters=['config/filter_params.yaml'],
            remappings=[
                ('input', 'sensors/lidar/scan'),
                ('output', 'processing/filtered_scan'),
            ],
        ),

        # 可视化节点（可选）
        Node(
            package='rviz2',
            executable='rviz2',
            name='rviz2',
            arguments=['-d', 'config/robot.rviz'],
        ),
    ])
```

## 参数加载

Launch 文件中有三种方式加载参数：

### 1. 内联字典

```python
Node(
    package='my_pkg',
    executable='my_node',
    parameters=[{
        'param1': 'value1',
        'param2': 42,
    }],
)
```

### 2. YAML 文件

```python
Node(
    package='my_pkg',
    executable='my_node',
    parameters=['config/params.yaml'],
)
```

### 3. 文件 + 覆盖（先加载文件，再用字典覆盖）

```python
Node(
    package='my_pkg',
    executable='my_node',
    parameters=[
        'config/params.yaml',     # 先加载文件
        {'debug_mode': True},      # 再覆盖特定值
    ],
)
```

::: warning 注意
Python 字典中的值会覆盖 YAML 文件中同名的参数。顺序很重要——先写 YAML 文件路径，后写覆盖字典。
:::

## 节点重映射

重映射用于在 Launch 层面修改节点使用的话题名、服务名等：

```python
Node(
    package='my_pkg',
    executable='my_node',
    remappings=[
        ('/old_topic', '/new_topic'),          # 话题重映射
        ('/old_service', '/new_service'),       # 服务重映射
        ('__node', 'my_custom_name'),          # 节点名重映射
        ('__ns', '/my_namespace'),             # 命名空间重映射
    ],
)
```

这在以下场景中非常有用：
- 同一个包的不同实例使用不同的话题
- 集成第三方包但需要修改其默认话题名
- 多机器人系统中隔离不同机器人的话题

## 使用 Launch 配置

`LaunchConfiguration` 允许从命令行传入参数，控制 Launch 文件的行为：

```python
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, LogInfo
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node

def generate_launch_description():
    # 声明可通过命令行传入的参数
    use_sim_arg = DeclareLaunchArgument(
        'use_sim',
        default_value='false',
        description='是否使用仿真模式'
    )

    robot_model_arg = DeclareLaunchArgument(
        'robot_model',
        default_value='turtlebot3',
        description='机器人模型名称'
    )

    # 启动节点，根据参数选择配置
    return LaunchDescription([
        use_sim_arg,
        robot_model_arg,

        LogInfo(msg=['启动模式: ', LaunchConfiguration('use_sim')]),
        LogInfo(msg=['机器人模型: ', LaunchConfiguration('robot_model')]),

        Node(
            package='robot_driver',
            executable='driver_node',
            parameters=[LaunchConfiguration('use_sim')],
            condition=IfCondition(LaunchConfiguration('use_sim')),
        ),
    ])
```

运行时传入参数：

```bash
ros2 launch my_pkg robot.launch.py use_sim:=true robot_model:=custom_bot
```

## 条件启动

使用 `IfCondition` 和 `UnlessCondition` 控制节点是否启动：

```python
from launch.conditions import IfCondition, UnlessCondition
from launch.substitutions import LaunchConfiguration

Node(
    package='rviz2',
    executable='rviz2',
    condition=IfCondition(LaunchConfiguration('use_rviz')),
),

Node(
    package='fake_sensor',
    executable='fake_lidar',
    condition=UnlessCondition(LaunchConfiguration('use_real_hw')),
),
```

## 包含其他 Launch 文件

大型项目中，通常会把不同功能模块的 Launch 文件拆开，然后用一个顶层 Launch 文件组合：

```python
from launch.actions import IncludeLaunchDescription
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch_ros.substitutions import FindPackageShare

def generate_launch_description():
    return LaunchDescription([
        # 包含传感器 Launch 文件
        IncludeLaunchDescription(
            PythonLaunchDescriptionSource(
                [FindPackageShare('sensor_pkg'), '/launch/sensors.launch.py']
            ),
            launch_arguments={'use_sim': 'true'}.items(),
        ),

        # 包含导航 Launch 文件
        IncludeLaunchDescription(
            PythonLaunchDescriptionSource(
                [FindPackageShare('nav_pkg'), '/launch/navigation.launch.py']
            ),
        ),

        # 包含可视化
        IncludeLaunchDescription(
            PythonLaunchDescriptionSource(
                [FindPackageShare('viz_pkg'), '/launch/visualization.launch.py']
            ),
        ),
    ])
```

## 常用 Launch 工具

| 类 | 作用 |
| --- | --- |
| `TimerAction` | 延迟启动某个动作 |
| `RegisterEventHandler` + `OnProcessStart` | 在某个节点启动后触发动作 |
| `SetEnvironmentVariable` | 设置环境变量 |
| `GroupAction` | 对一组节点统一设置命名空间或参数 |
| `PushRosNamespace` | 在 Group 中设置命名空间 |

示例——延迟启动 + 环境变量：

```python
from launch.actions import TimerAction, SetEnvironmentVariable

SetEnvironmentVariable(name='ROS_DOMAIN_ID', value='30'),

TimerAction(
    period=3.0,
    actions=[
        Node(package='my_pkg', executable='delayed_node'),
    ],
),
```

## 常见问题

### Launch 文件修改后不生效

确保执行了 `colcon build` 并重新 `source setup.bash`。Launch 文件被安装在 `install/` 目录下，源码中的修改不会直接生效。

### 参数传递不正确

检查参数类型是否匹配。YAML 中的 `true`/`false` 是布尔值，而 `"true"` 是字符串。如果节点期望布尔值，不要加引号。

### 如何调试 Launch 文件

```bash
# 查看启动了哪些节点
ros2 node list

# 查看 Launch 过程的详细信息
ros2 launch my_pkg robot.launch.py --show-args

# 以调试模式运行（打印更详细的日志）
ros2 launch my_pkg robot.launch.py -v
```

## 小结

Launch 文件是 ROS 2 项目中不可或缺的组织工具，要点：

1. **推荐使用 Python 格式**（`.launch.py`），灵活且功能强大
2. `Node` 动作启动节点，支持参数、重映射、命名空间等配置
3. `LaunchConfiguration` 实现命令行参数化，支持条件启动
4. `IncludeLaunchDescription` 组合多个子 Launch 文件，实现模块化管理
5. YAML 文件适合批量参数管理，字典覆盖适合临时调试
6. 把 Launch 文件安装到 `share/package_name/launch/` 目录

下一篇进入 ROS 2 常用命令行工具与调试技巧的总结。
