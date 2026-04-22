---
title: ROS2 实时性支持与 PREEMPT_RT
author: EASYZOOM
date: 2026/04/24 10:00
categories:
 - ROS2入门与实践
tags:
 - ROS2
 - 实时性
 - PREEMPT_RT
 - 嵌入式
---

# ROS2 实时性支持与 PREEMPT_RT

## 前言

**C：** 普通的 Ubuntu 系统不是实时操作系统——进程调度由内核决定，你的控制循环可能被其他进程打断几百毫秒。对于普通的桌面机器人来说，这通常不是问题（偶尔的延迟被机器人的惯性吸收了）。但对于高速无人机、四足机器人、工业机械臂等对时序要求严格的场景，普通 Linux 的调度不确定性就可能导致控制失败甚至安全事故。本篇讲解 ROS 2 的实时性支持和 PREEMPT_RT 补丁的使用方法。

<!-- more -->

## 什么是实时性

| 特性 | 普通系统 | 硬实时系统 |
| --- | --- | --- |
| 调度保证 | 尽力而为 | 保证最大延迟 |
| 超时后果 | 可能卡顿 | 可能事故 |
| 典型延迟 | 1ms ~ 100ms | < 100μs |
| 适用场景 | 桌面、一般服务器 | 工业、航空、机器人控制 |

实时性分两级：

- **软实时**：偶尔超时可以接受，但平均延迟要低（如视频处理）
- **硬实时**：每次都必须在截止时间内完成（如电机控制、安全系统）

## PREEMPT_RT 概述

PREEMPT_RT 是 Linux 内核的实时补丁集，由 Thomas Gleixner 等人维护。它将标准 Linux 内核改造为具备硬实时能力的系统：

### 补丁的核心修改

| 修改项 | 标准 Linux | PREEMPT_RT |
| --- | --- | --- |
| 自旋锁 | 中断中持有自旋锁不释放 | 大部分替换为可抢占的 mutex |
| 中断处理 | 硬中断上下文不可抢占 | 中断线程化，可被抢占 |
| 优先级反转 | 可能发生 | 使用优先级继承协议（PI） |
| 定时器精度 | ~1ms | ~1μs |

### 实时性能指标

| 指标 | 标准 Linux | PREEMPT_RT |
| --- | --- | --- |
| 最大调度延迟 | ~10ms | ~50μs |
| 中断响应延迟 | ~100μs | ~5μs |
| 定时器精度 | ~1ms | ~1μs |

## 安装 PREEMPT_RT

### Ubuntu 22.04（推荐）

```bash
# 安装 PREEMPT_RT 内核
sudo apt install linux-image-$(uname -r)-rt-amd64 \
                 linux-headers-$(uname -r)-rt-amd64

# 或者安装最新版本
sudo apt install linux-image-rt-amd64 \
                 linux-headers-rt-amd64

# 更新 GRUB
sudo update-grub

# 重启并选择 RT 内核
sudo reboot

# 验证
uname -a
# 应包含 PREEMPT_RT
cat /sys/kernel/realtime
# 输出 1 表示 RT 内核已启用
```

### 从源码编译

```bash
# 下载内核源码
wget https://cdn.kernel.org/pub/linux/kernel/v5.x/linux-5.15.tar.xz
tar xf linux-5.15.tar.xz
cd linux-5.15

# 下载 PREEMPT_RT 补丁
wget https://cdn.kernel.org/pub/linux/kernel/projects/rt/5.15/patch-5.15.xx-rt.patch.xz
xz -d patch-5.15.xx-rt.patch.xz
patch -p1 < patch-5.15.xx-rt.patch

# 配置
make menuconfig
# → Processor type and features → Preemption Model → Fully Preemptible Kernel (Real-Time)

# 编译
make -j$(nproc)
sudo make modules_install
sudo make install
sudo update-grub
```

## 配置实时优先级

### 设置进程优先级

```bash
# 查看当前调度策略
chrt -p $$

# 设置 FIFO 实时调度，优先级 80
chrt -f 80 ros2 run my_pkg my_realtime_node

# 设置 RR（时间片轮转）实时调度
chrt -r 80 ros2 run my_pkg my_realtime_node
```

实时优先级范围：

| 调度策略 | 优先级范围 | 说明 |
| --- | --- | --- |
| SCHED_FIFO | 1 ~ 99 | 先进先出，不时间片 |
| SCHED_RR | 1 ~ 99 | 时间片轮转 |
| SCHED_OTHER | 0 | 默认分时调度 |

::: warning 注意
实时优先级 99 是系统最高优先级，不要随便给所有节点都设为 99——这会饿死系统关键进程。一般控制循环设为 50~80 即可。
:::

### 锁定内存页

实时进程不应该被换出到磁盘：

```bash
# 使用 mlockall 锁定所有内存页
# 在 C++ 中：
#include <sys/mman.h>
mlockall(MCL_CURRENT | MCL_FUTURE);
```

### 设置 CPU 亲和性

将实时进程绑定到特定 CPU 核心：

```bash
# 绑定到 CPU 2 和 3
taskset -c 2,3 ros2 run my_pkg my_realtime_node

# 查看当前亲和性
taskset -p $$
```

在 Launch 文件中：

```python
import os
os.environ['OMP_NUM_THREADS'] = '2'
os.environ['GOMP_CPU_AFFINITY'] = '2,3'
```

## ROS2 实时性最佳实践

### 1. 控制循环独立于回调

```cpp
// 好的做法：独立的实时控制循环
void control_loop() {
    mlockall(MCL_CURRENT | MCL_FUTURE);

    auto period = std::chrono::nanoseconds(1000000);  // 1ms
    auto next = std::chrono::steady_clock::now();

    while (rclcpp::ok()) {
        // 读取传感器
        auto state = read_hardware();

        // 计算控制量
        auto cmd = compute_control(state);

        // 写入硬件
        write_hardware(cmd);

        // 精确休眠到下一个周期
        next += period;
        std::this_thread::sleep_until(next);
    }
}
```

### 2. 避免在实时路径上做阻塞操作

在实时控制循环中**不要做**以下事情：

| 禁止操作 | 原因 | 替代方案 |
| --- | --- | --- |
| `malloc`/`new` | 内存分配不可预测 | 预分配内存池 |
| `printf`/`std::cout` | I/O 操作可能阻塞 | 无锁日志队列 |
| `mutex.lock()` | 优先级反转 | 使用优先级继承 mutex |
| 文件 I/O | 磁盘操作极慢 | 异步 I/O + 共享内存 |
| 网络调用 | 不可预测延迟 | 共享内存通信 |

### 3. 无锁通信

在实时节点和非实时节点之间，使用共享内存或无锁队列：

```cpp
#include <atomic>

// 无锁的单生产者-单消费者队列
template<typename T, size_t N>
class LockFreeRingBuffer {
    std::array<T, N> buffer_;
    std::atomic<size_t> head_{0};
    std::atomic<size_t> tail_{0};

public:
    bool push(const T& item) {
        size_t head = head_.load(std::memory_order_relaxed);
        size_t next = (head + 1) % N;
        if (next == tail_.load(std::memory_order_acquire)) return false;
        buffer_[head] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool pop(T& item) {
        size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) return false;
        item = buffer_[tail];
        tail_.store((tail + 1) % N, std::memory_order_release);
        return true;
    }
};
```

### 4. 使用 rt-tests 验证实时性

```bash
# 安装测试工具
sudo apt install rt-tests stress-ng

# cyclictest：测量调度延迟
sudo cyclictest -m -p 80 -a 2,3 -t 4 -l 100000 -h 400

# 典型输出（PREEMPT_RT）：
# Max: ~50μs, Avg: ~10μs

# 典型输出（标准 Linux）：
# Max: ~10ms, Avg: ~100μs
```

### 5. 中断线程化配置

```bash
# 查看中断线程化状态
cat /proc/interrupts | head

# 确认中断处理被线程化
# 在 /sys/kernel/debug/preemptirq_timing 中可以看到

# 设置中断优先级
echo 50 > /proc/irq/<irq_num>/smp_affinity
```

## 减少内核噪声

```bash
# 隔离 CPU 核心（不让普通进程使用）
# 编辑 GRUB 配置：GRUB_CMDLINE_LINUX="isolcpus=2,3 nohz_full=2,3 rcu_nocbs=2,3"

# 关闭不需要的服务
sudo systemctl disable bluetooth
sudo systemctl disable NetworkManager-wait-online.service

# 减少 CPU 频率切换（锁定最高频率）
sudo cpupower frequency-set -g performance
```

## 什么时候需要 PREEMPT_RT

| 场景 | 是否需要 | 原因 |
| --- | --- | --- |
| 桌面差速机器人 | 不需要 | 10~50ms 延迟可接受 |
| 室内 SLAM/导航 | 不需要 | 视觉和激光处理不需要硬实时 |
| 工业机械臂 | 推荐 | 安全相关 |
| 四足机器人 | 推荐 | 高频平衡控制 |
| 无人机 | 必须 | 飞控需要硬实时 |
| 高速运动控制 | 必须 | 1kHz+ 控制频率 |

## 常见问题

### PREEMPT_RT 内核安装后系统不稳定

- 某些显卡驱动不兼容 PREEMPT_RT
- 无线网卡驱动可能有问题
- 尝试更新驱动或使用有线网络

### cyclictest 延迟仍然很高

```bash
# 检查是否有非实时进程占用 CPU
top -H

# 检查 SMIs (System Management Interrupts)
sudo rdmsr -p0 0x342

# 确认 isolcpus 生效
cat /proc/cmdline | grep isolcpus
```

### ROS2 节点无法使用实时优先级

普通用户需要权限：

```bash
# 添加实时权限
sudo usermod -aG realtime $USER

# 配置 rlimits
echo "@realtime - rtprio 99" | sudo tee /etc/security/limits.d/realtime.conf
echo "@realtime - memlock unlimited" | sudo tee -a /etc/security/limits.d/realtime.conf
```

## 小结

ROS 2 实时性支持要点：

1. **PREEMPT_RT**：将标准 Linux 改造为硬实时系统
2. **安装验证**：`cat /sys/kernel/realtime` 输出 1
3. **实时调度**：`chrt -f 80` 设置 FIFO 实时优先级
4. **内存锁定**：`mlockall()` 防止页面换出
5. **实时循环**：独立线程、预分配、无阻塞操作
6. **性能测试**：`cyclictest` 测量调度延迟
7. **CPU 隔离**：`isolcpus` 隔离 CPU 核心给实时任务
