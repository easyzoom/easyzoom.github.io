---
title: Gazebo 世界构建与模型管理
author: EASYZOOM
date: 2026/04/24 10:00
categories:
 - ROS2入门与实践
tags:
 - ROS2
 - Gazebo
 - 仿真
 - 世界文件
---

# Gazebo 世界构建与模型管理

## 前言

**C：** 空旷的世界只能验证轮子能不能转，要做导航、避障、SLAM 等测试，需要把环境搭建起来——墙壁、障碍物、家具、门这些场景。Gazebo 的世界文件（`.world`）和模型库就是用来做这件事的。本篇讲解如何创建自定义世界、使用 Gazebo 模型库、以及一些让仿真更贴近真实的高级配置。

<!-- more -->

## 世界文件结构

一个完整的 Gazebo 世界文件（SDF 格式）：

```xml
<?xml version="1.0" ?>
<sdf version="1.6">
  <world name="my_world">

    <!-- 1. 物理引擎 -->
    <physics type="ode">
      <max_step_size>0.001</max_step_size>
      <real_time_factor>1.0</real_time_factor>
      <real_time_update_rate>1000</real_time_update_rate>
      <gravity>0 0 -9.8</gravity>
    </physics>

    <!-- 2. 场景设置 -->
    <scene>
      <ambient>0.4 0.4 0.4 1</ambient>
      <background>0.7 0.7 0.7 1</background>
      <shadows>true</shadows>
    </scene>

    <!-- 3. 光照 -->
    <light name="sun" type="directional">
      <cast_shadows>true</cast_shadows>
      <pose>0 0 10 0 0 0</pose>
      <diffuse>0.8 0.8 0.8 1</diffuse>
      <specular>0.2 0.2 0.2 1</specular>
      <attenuation>
        <range>1000</range>
        <constant>0.9</constant>
        <linear>0.01</linear>
        <quadratic>0.001</quadratic>
      </attenuation>
    </light>

    <!-- 4. 地面 -->
    <model name="ground_plane">
      <static>true</static>
      <link name="link">
        <collision name="collision">
          <geometry>
            <plane><normal>0 0 1</normal><size>100 100</size></plane>
          </geometry>
          <surface>
            <friction>
              <ode><mu>0.6</mu><mu2>0.6</mu2></ode>
            </friction>
          </surface>
        </collision>
        <visual name="visual">
          <geometry>
            <plane><normal>0 0 1</normal><size>100 100</size></plane>
          </geometry>
          <material>
            <ambient>0.8 0.8 0.8 1</ambient>
          </material>
        </visual>
      </link>
    </model>

    <!-- 5. 自定义模型和障碍物（见下文） -->

  </world>
</sdf>
```

## 添加障碍物

### 简单障碍物（墙壁）

```xml
<model name="wall_1">
  <static>true</static>
  <pose>2 0 0.5 0 0 0</pose>
  <link name="link">
    <collision name="collision">
      <geometry>
        <box><size>0.1 3 1</size></box>
      </geometry>
    </collision>
    <visual name="visual">
      <geometry>
        <box><size>0.1 3 1</size></box>
      </geometry>
      <material><ambient>0.7 0.7 0.7 1</ambient></material>
    </visual>
  </link>
</model>
```

### 圆柱形障碍物

```xml
<model name="pillar_1">
  <static>true</static>
  <pose>3 2 0.25 0 0 0</pose>
  <link name="link">
    <collision name="collision">
      <geometry>
        <cylinder><radius>0.15</radius><length>0.5</length></cylinder>
      </geometry>
    </collision>
    <visual name="visual">
      <geometry>
        <cylinder><radius>0.15</radius><length>0.5</length></cylinder>
      </geometry>
      <material><ambient>0.8 0.2 0.2 1</ambient></material>
    </visual>
  </link>
</model>
```

### 从模型库插入

Gazebo 自带了一个模型库，可以通过世界文件引用：

```xml
<!-- 插入 Gazebo 自带模型 -->
<include>
  <uri>model://cafe</uri>
  <pose>-2 -2 0 0 0 0</pose>
</include>

<include>
  <uri>model://bookshelf</uri>
  <pose>5 3 0 0 0 1.57</pose>
</include>
```

在 Gazebo GUI 中也可以通过左侧 Insert 面板直接拖拽模型到场景中。

## 自定义 Gazebo 模型

复杂的模型建议保存为独立的模型文件，放在模型库路径下：

```text
~/.gazebo/models/my_obstacle/
├── model.config
└── model.sdf
```

**model.config：**

```xml
<?xml version="1.0"?>
<model>
  <name>my_obstacle</name>
  <version>1.0</version>
  <sdf version="1.6">model.sdf</sdf>
  <author>
    <name>EASYZOOM</name>
  </author>
  <description>自定义障碍物模型</description>
</model>
```

**model.sdf：**

```xml
<?xml version="1.0" ?>
<sdf version="1.6">
  <model name="my_obstacle">
    <static>true</static>
    <link name="link">
      <visual name="visual">
        <geometry>
          <box><size>0.5 0.5 0.5</size></box>
        </geometry>
        <material><ambient>0.0 0.6 0.0 1</ambient></material>
      </visual>
      <collision name="collision">
        <geometry>
          <box><size>0.5 0.5 0.5</size></box>
        </geometry>
      </collision>
    </link>
  </model>
</sdf>
```

保存后在 Gazebo 的 Insert 面板中就能看到 `my_obstacle` 模型。

## 在 Launch 文件中使用自定义世界

```python
import os
from launch_ros.substitutions import FindPackageShare

world_file = os.path.join(
    FindPackageShare('my_robot').find('my_robot'),
    'worlds', 'obstacle_world.world'
)

gazebo = IncludeLaunchDescription(
    PythonLaunchDescriptionSource([
        FindPackageShare('gazebo_ros'), '/launch/gazebo.launch.py'
    ]),
    launch_arguments={
        'world': world_file,
        'pause': 'false',
    }.items(),
)
```

## 从 SDF 模型生成 URDF 节省手动建模

如果不想手写 URDF，可以从现有的 Gazebo 模型（SDF 格式）生成 URDF：

```bash
# 将 SDF 模型转换为 URDF
gz sdf -p model.sdf > model.urdf
```

不过自动转换的 URDF 通常需要手动调整（尤其是 gazebo 标签和插件配置）。

## 仿真性能调优

### 物理引擎参数

```xml
<physics type="ode">
  <!-- 步长越小越精确，但越耗性能 -->
  <max_step_size>0.001</max_step_size>

  <!-- 实时因子 1.0 表示仿真速度与真实时间一致 -->
  <real_time_factor>1.0</real_time_factor>

  <!-- 更新频率 -->
  <real_time_update_rate>1000</real_time_update_rate>
</physics>
```

| 场景 | max_step_size | 说明 |
| --- | --- | --- |
| 精确仿真 | 0.0005 ~ 0.001 | 适合碰撞检测要求高的场景 |
| 一般仿真 | 0.001 ~ 0.003 | 平衡精度和性能 |
| 快速验证 | 0.003 ~ 0.005 | 粗略验证逻辑正确性 |

### GPU 加速

```bash
# 确认 GPU 加速是否启用
glxinfo | grep "OpenGL renderer"

# 如果使用 NVIDIA GPU，通常自动启用
# 软件渲染（性能较差）
export LIBGL_ALWAYS_SOFTWARE=1
```

### 降低渲染负担

```xml
<!-- 在模型中关闭阴影 -->
<scene>
  <shadows>false</shadows>
</scene>

<!-- 简化碰撞体 -->
<collision>
  <geometry>
    <box size="..."/>  <!-- 用简单几何体代替 mesh -->
  </geometry>
</collision>
```

## 录制与回放

```bash
# 录制仿真状态
ros2 service call /gazebo/recording/start std_srvs/srv/Empty

# 停止录制
ros2 service call /gazebo/recording/stop std_srvs/srv/Empty

# 从命令行截图
gz world -s  # Gazebo Classic
```

使用 rosbag 录制话题数据：

```bash
# 录制所有话题
ros2 bag record -a

# 录制指定话题
ros2 bag record /scan /odom /cmd_vel /tf

# 回放
ros2 bag play my_recording
```

## 多机器人仿真

在一个世界中运行多个机器人：

```python
# 为每个机器人使用不同的命名空间
robot_1 = IncludeLaunchDescription(
    PythonLaunchDescriptionSource([robot_launch_file]),
    launch_arguments={
        'namespace': 'robot_1',
        'x_pose': '-2.0',
        'y_pose': '0.0',
    }.items(),
)

robot_2 = IncludeLaunchDescription(
    PythonLaunchDescriptionSource([robot_launch_file]),
    launch_arguments={
        'namespace': 'robot_2',
        'x_pose': '2.0',
        'y_pose': '0.0',
    }.items(),
)
```

注意多机器人仿真需要：
- 不同命名空间隔离话题和服务
- 不同 `ROS_DOMAIN_ID` 或配置 DDS Discovery
- TF 坐标系使用前缀区分

## 常见问题

### 世界文件中 include 的模型找不到

检查模型路径：

```bash
# 查看 Gazebo 模型路径
echo $GAZEBO_MODEL_PATH

# 添加自定义路径
export GAZEBO_MODEL_PATH=$GAZEBO_MODEL_PATH:~/my_models
```

### 仿真速度很慢

1. 降低 `real_time_update_rate`
2. 增大 `max_step_size`
3. 关闭阴影
4. 减少传感器采样率
5. 检查 GPU 是否被正确使用

### 机器人掉出世界

检查地面碰撞体是否正确配置，以及机器人的初始 z 坐标是否使轮子在地面上方。

## 小结

Gazebo 世界构建要点：

1. **世界文件（SDF）**：定义物理引擎、场景、光照、地面、障碍物
2. **障碍物**：直接在世界文件中用 `<model>` 定义，或引用模型库
3. **自定义模型**：`model.config` + `model.sdf`，放入 `~/.gazebo/models/` 或包内
4. **性能调优**：调整物理步长、关闭阴影、简化碰撞体
5. **多机器人**：使用命名空间隔离话题和 TF
6. **录制回放**：Gazebo 录制 + rosbag 话题录制
