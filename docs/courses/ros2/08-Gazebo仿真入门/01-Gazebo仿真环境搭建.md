---
title: Gazebo 仿真环境搭建
author: EASYZOOM
date: 2026/04/24 10:00
categories:
 - ROS2入门与实践
tags:
 - ROS2
 - Gazebo
 - 仿真
---

# Gazebo 仿真环境搭建

## 前言

**C：** 真机调试成本高、风险大、可重复性差。仿真让你在虚拟环境中测试算法、验证逻辑、排查问题，是机器人开发不可或缺的一环。ROS 2 主流使用 Gazebo（新版称为 Gazebo Sim / Ignition）作为仿真器。本篇从 Gazebo 的安装和基本使用讲起，带你把一个 URDF 机器人模型加载到仿真环境中，并用插件让轮子转起来。

<!-- more -->

## Gazebo 版本说明

ROS 2 不同发行版搭配不同的 Gazebo 版本：

| ROS 2 发行版 | Gazebo Classic | Gazebo Sim (Ignition) |
| --- | --- | --- |
| Humble | Gazebo 11 | Gazebo 6 (Edifice) |
| Iron | Gazebo 11 | Gazebo 6 (Edifice) |
| Jazzy | 不推荐 | Gazebo Harmonic |

::: tip 笔者说
Gazebo Classic（gazebo11）是 ROS 2 Humble/Iron 下生态最成熟的版本，社区插件和教程最多。新项目可以考虑 Gazebo Sim（Ignition），但需要注意很多 ROS 2 插件还在迁移中。本篇以 Gazebo Classic + Humble 为主要示例。
:::

## 安装 Gazebo

```bash
# 安装 Gazebo Classic + ROS2 集成包
sudo apt install ros-humble-gazebo-ros-pkgs ros-humble-gazebo-ros2-control

# 安装常用插件
sudo apt install ros-humble-gazebo-plugins ros-humble-gazebo-ros

# 验证安装
gazebo --version
ros2 run gazebo_ros gazebo.launch.py
```

## Gazebo 启动方式

### 方式1：直接启动空环境

```bash
# 启动空世界
ros2 run gazebo_ros gazebo.launch.py

# 启动带特定世界文件
ros2 run gazebo_ros gazebo.launch.py world:=/path/to/my_world.world
```

### 方式2：通过 Launch 文件启动

```python
# launch/simulation.launch.py
from launch import LaunchDescription
from launch.actions import ExecuteProcess, IncludeLaunchDescription
from launch_ros.substitutions import FindPackageShare
from launch.launch_description_sources import PythonLaunchDescriptionSource

def generate_launch_description():
    gazebo = IncludeLaunchDescription(
        PythonLaunchDescriptionSource([
            FindPackageShare('gazebo_ros'),
            '/launch/gazebo.launch.py'
        ]),
        launch_arguments={
            'world': FindPackageShare('my_robot') + '/worlds/empty_world.world'
        }.items(),
    )

    # 在 Gazebo 中生成机器人
    spawn_entity = Node(
        package='gazebo_ros',
        executable='spawn_entity.py',
        arguments=[
            '-topic', 'robot_description',
            '-entity', 'diff_bot',
        ],
        output='screen',
    )

    return LaunchDescription([gazebo, spawn_entity])
```

## 世界文件基础

Gazebo 世界文件（`.world`）定义仿真环境：

```xml
<?xml version="1.0" ?>
<sdf version="1.6">
  <world name="empty_world">
    <!-- 物理引擎配置 -->
    <physics type="ode">
      <max_step_size>0.001</max_step_size>
      <real_time_factor>1.0</real_time_factor>
    </physics>

    <!-- 光照 -->
    <light name="sun" type="directional">
      <cast_shadows>true</cast_shadows>
      <pose>0 0 10 0 0 0</pose>
      <diffuse>0.8 0.8 0.8 1</diffuse>
    </light>

    <!-- 地面 -->
    <model name="ground_plane">
      <static>true</static>
      <link name="link">
        <collision name="collision">
          <geometry>
            <plane>
              <normal>0 0 1</normal>
              <size>100 100</size>
            </plane>
          </geometry>
        </collision>
        <visual name="visual">
          <geometry>
            <plane>
              <normal>0 0 1</normal>
              <size>100 100</size>
            </plane>
          </geometry>
          <material>
            <ambient>0.8 0.8 0.8 1</ambient>
          </material>
        </visual>
      </link>
    </model>
  </world>
</df>
```

## 在 Gazebo 中加载 URDF 模型

要让 URDF 模型在 Gazebo 中正常仿真，需要在 URDF 中添加 **Gazebo 专用标签**。纯 URDF 只描述运动学，Gazebo 还需要知道摩擦力、碰撞参数、传感器插件等信息。

### 基本 Gazebo 标签

```xml
<!-- 为 link 设置仿真属性 -->
<gazebo reference="base_link">
  <material>Gazebo/Blue</material>
  <mu1>0.1</mu1>   <!-- 摩擦系数 -->
  <mu2>0.1</mu2>
</gazebo>
```

### 差速驱动插件

```xml
<!-- 差速驱动控制器 -->
<gazebo>
  <plugin name="diff_drive" filename="libgazebo_ros_diff_drive.so">
    <left_joint>left_wheel_joint</left_joint>
    <right_joint>right_wheel_joint</right_joint>

    <wheel_separation>0.45</wheel_separation>
    <wheel_diameter>0.2</wheel_diameter>

    <max_wheel_torque>20</max_wheel_torque>
    <max_wheel_acceleration>1.0</max_wheel_acceleration>

    <!-- 发布的话题 -->
    <ros>
      <namespace>/</namespace>
      <remapping>cmd_vel:=cmd_vel</remapping>
      <remapping>odom:=odom</remapping>
      <remapping>odom_tf:=odom_tf</remapping>
    </ros>

    <!-- 轮子之间的轮距 -->
    <odometry_source>world</odometry_source>

    <update_rate>50</update_rate>
  </plugin>
</gazebo>
```

### 激光雷达插件

```xml
<gazebo reference="laser_link">
  <sensor name="lidar" type="ray">
    <update_rate>10</update_rate>
    <ray>
      <scan>
        <horizontal>
          <samples>360</samples>
          <resolution>1</resolution>
          <min_angle>-3.14159</min_angle>
          <max_angle>3.14159</max_angle>
        </horizontal>
      </scan>
      <range>
        <min>0.1</min>
        <max>12.0</max>
        <resolution>0.01</resolution>
      </range>
      <noise>
        <type>gaussian</type>
        <mean>0.0</mean>
        <stddev>0.01</stddev>
      </noise>
    </ray>
    <plugin name="lidar_plugin" filename="libgazebo_ros_ray_sensor.so">
      <ros>
        <namespace>/</namespace>
        <remapping>out:=scan</remapping>
      </ros>
      <frame_name>laser_link</frame_name>
    </plugin>
  </sensor>
</gazebo>
```

## 完整仿真 Launch 文件

将 Gazebo、URDF 模型、控制器、RViz 组合在一起：

```python
# launch/simulation.launch.py
import os
from launch import LaunchDescription
from launch.actions import (
    DeclareLaunchArgument, ExecuteProcess, IncludeLaunchDescription,
    RegisterEventHandler
)
from launch.substitutions import LaunchConfiguration, Command
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.event_handlers import OnProcessStart

def generate_launch_description():

    use_sim = LaunchConfiguration('use_sim', default='true')

    # 1. 启动 Gazebo
    gazebo = IncludeLaunchDescription(
        PythonLaunchDescriptionSource([
            FindPackageShare('gazebo_ros'), '/launch/gazebo.launch.py'
        ]),
    )

    # 2. 从 Xacro 生成 robot_description
    pkg_share = FindPackageShare('my_robot').find('my_robot')
    xacro_file = os.path.join(pkg_share, 'urdf', 'diff_bot.xacro')
    robot_description = Command([
        'xacro ', xacro_file, ' use_sim:=', use_sim
    ])

    robot_state_publisher = Node(
        package='robot_state_publisher',
        executable='robot_state_publisher',
        parameters=[{'robot_description': robot_description, 'use_sim_time': use_sim}],
        output='screen',
    )

    # 3. 在 Gazebo 中生成机器人
    spawn_entity = Node(
        package='gazebo_ros',
        executable='spawn_entity.py',
        arguments=['-topic', 'robot_description', '-entity', 'diff_bot'],
        output='screen',
    )

    # 4. 启动 RViz
    rviz = Node(
        package='rviz2',
        executable='rviz2',
        arguments=['-d', os.path.join(pkg_share, 'config', 'simulation.rviz')],
        condition=IfCondition(LaunchConfiguration('use_rviz', default='true')),
    )

    # 5. 关节状态发布器（非仿真时用 GUI）
    joint_state_publisher = Node(
        package='joint_state_publisher_gui',
        executable='joint_state_publisher_gui',
        condition=UnlessCondition(use_sim),
    )

    return LaunchDescription([
        DeclareLaunchArgument('use_sim', default_value='true'),
        DeclareLaunchArgument('use_rviz', default_value='true'),
        gazebo,
        robot_state_publisher,
        spawn_entity,
        rviz,
        joint_state_publisher,
    ])
```

## 从命令行控制机器人

启动仿真后，在另一个终端中：

```bash
# 发送速度命令让机器人前进
ros2 topic pub /cmd_vel geometry_msgs/Twist \
  "{linear: {x: 0.5}, angular: {z: 0.0}}"

# 原地旋转
ros2 topic pub /cmd_vel geometry_msgs/Twist \
  "{linear: {x: 0.0}, angular: {z: 0.5}}"

# 停止
ros2 topic pub /cmd_vel geometry_msgs/Twist \
  "{linear: {x: 0.0}, angular: {z: 0.0}}"

# 查看传感器数据
ros2 topic echo /scan
ros2 topic echo /odom
```

## use_sim_time 参数

仿真环境中时间由 Gazebo 控制，而不是系统时钟。所有需要时间戳的节点必须设置 `use_sim_time:=true`：

```bash
# 命令行方式
ros2 run my_pkg my_node --ros-args -p use_sim_time:=true

# Launch 文件方式
Node(
    package='my_pkg',
    executable='my_node',
    parameters=[{'use_sim_time': True}],
)
```

::: warning 注意
如果仿真环境中某些节点不设置 `use_sim_time`，它们会使用系统时钟，导致 TF 查询失败（Extrapolation Into Future 错误）。这是仿真中最常见的坑之一。
:::

## 常用 Gazebo 操作

| 操作 | 快捷键 / 命令 |
| --- | --- |
| 平移视角 | 鼠标中键拖拽 |
| 旋转视角 | 鼠标左键拖拽 |
| 缩放 | 鼠标滚轮 |
| 移动模型 | 右键模型 → Move |
| 旋转模型 | 右键模型 → Rotate |
| 暂停仿真 | 空格键 |
| 单步仿真 | 暂停后按 S 键 |
| 重置仿真 | Edit → Reset Model Poses |
| 插入模型 | 左侧面板 → Insert |

## 常见问题

### Gazebo 启动黑屏或闪退

```bash
# 检查 GPU 驱动
nvidia-smi

# 如果没有 GPU 或驱动问题，使用软件渲染
export LIBGL_ALWAYS_SOFTWARE=1
ros2 run gazebo_ros gazebo.launch.py
```

### 模型在 Gazebo 中穿透地面

检查 URDF 中的 inertial 属性和 gazebo 标签：
1. base_link 的 z=0 是否正确（底盘底部不应低于 z=0）
2. 碰撞体尺寸是否正确
3. 轮子的 collision 是否正确设置

### 插件加载失败

```
Error [Plugin.cc:198] Failed to load plugin libgazebo_ros_diff_drive.so
```

检查：
1. 插件包是否安装：`ros2 pkg list | grep gazebo`
2. Gazebo 版本与插件版本是否匹配
3. 插件文件名是否正确（Humble 用 `libgazebo_ros_xxx.so`，不是 `libgazebo_ros_plugins_xxx.so`）

## 小结

Gazebo 仿真是 ROS 2 开发的核心环节，要点：

1. **版本对应**：Humble/Iron → Gazebo 11 (Classic)，Jazzy → Gazebo Sim (Harmonic)
2. **Gazebo 标签**：URDF 中用 `<gazebo>` 标签添加仿真属性和插件
3. **三大插件**：差速驱动（diff_drive）、激光雷达（ray_sensor）、IMU
4. **spawn_entity**：用 `spawn_entity.py` 将 URDF 模型加载到 Gazebo 中
5. **use_sim_time**：仿真环境中所有节点必须设置 `use_sim_time:=true`
6. **调试流程**：先在 RViz 中验证 URDF 显示正确，再加载到 Gazebo 中验证物理行为

下一篇深入 Gazebo 仿真插件，讲解自定义世界构建和高级传感器配置。
