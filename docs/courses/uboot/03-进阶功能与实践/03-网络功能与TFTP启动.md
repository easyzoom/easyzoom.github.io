---
title: 网络功能与 TFTP 启动
author: EASYZOOM
date: 2026/04/24 20:00
categories:
  - U-Boot从入门到精通
tags:
  - U-Boot
  - 网络
  - TFTP
  - NFS
---

# 网络功能与 TFTP 启动

## 前言

**C：** 嵌入式开发中最幸福的事之一就是不用每次都把内核拷到 SD 卡——直接通过网络加载内核、设备树、根文件系统，改一行代码重启就能看到效果。U-Boot 的网络功能就是干这个的。本篇从基础配置讲起，覆盖 DHCP、TFTP、NFS 和 PXE 网络启动，最后给一些网络相关的排错技巧。

<!-- more -->

## 网络基础配置

### 必需的环境变量

```bash
# 本机 IP
setenv ipaddr 192.168.1.101

# TFTP / NFS 服务器 IP
setenv serverip 192.168.1.100

# 子网掩码
setenv netmask 255.255.255.0

# 网关
setenv gatewayip 192.168.1.1

# MAC 地址（有些 SoC 有 eFuse 存储的默认 MAC）
setenv ethaddr 02:xx:xx:xx:xx:xx

# 保存
saveenv
```

### 自动获取 IP：DHCP

```bash
# DHCP 自动获取 IP
dhcp

# 成功后自动设置 ipaddr、serverip、gatewayip、netmask
printenv ipaddr
printenv serverip
```

DHCP 成功后，如果需要覆盖某些值：

```bash
dhcp; setenv serverip 192.168.1.50; saveenv
```

### 物理连接

确保开发板和 TFTP 服务器在同一个网段：

```
[开发板] ---网线--- [交换机] ---网线--- [PC/服务器]
   192.168.1.101                   192.168.1.100
```

::: tip 笔者说

很多板子只有一个网口，直接用网线连 PC 也可以（省掉交换机）。此时需要 PC 端手动配一个同网段 IP。

:::

## TFTP 服务端搭建

### Linux 主机（Ubuntu/Debian）

```bash
# 安装
sudo apt install tftpd-hpa

# 配置
sudo vim /etc/default/tftpd-hpa
# TFTP_DIRECTORY="/srv/tftp"

# 创建目录
sudo mkdir -p /srv/tftp
sudo chmod 777 /srv/tftp

# 放入文件
cp uImage /srv/tftp/
cp board.dtb /srv/tftp/
cp rootfs.cpio.gz /srv/tftp/

# 重启服务
sudo systemctl restart tftpd-hpa
sudo systemctl enable tftpd-hpa
```

### 验证 TFTP 服务

```bash
# 从 Linux 主机测试
tftp 127.0.0.1 -c get uImage
ls -l uImage
```

### Windows 主机

推荐使用 TFTPD64（免费）：

1. 下载 TFTPD64
2. 设置 TFTP 目录
3. 放入内核和设备树文件
4. 确保防火墙放行 UDP 69 端口

## U-Boot TFTP 操作

### 基本用法

```bash
# 下载文件到指定内存地址
tftp ${kernel_addr_r} uImage
tftp ${fdt_addr_r} board.dtb
tftp ${initrd_addr} initramfs.cpio.gz

# 下载后自动设置 filesize 变量
echo "Downloaded ${filesize} bytes"
```

### 完整的网络启动 bootcmd

```bash
# ARM64: booti
setenv bootcmd_net 'dhcp; tftp ${kernel_addr_r} Image; tftp ${fdt_addr_r} board.dtb; setenv bootargs console=ttymxc0,115200 root=/dev/ram rdinit=/init ip=dhcp; booti ${kernel_addr_r} - ${fdt_addr_r}'

# ARM32: bootz
setenv bootcmd_net 'dhcp; tftp ${kernel_addr_r} zImage; tftp ${fdt_addr_r} board.dtb; setenv bootargs console=ttymxc0,115200 root=/dev/ram rdinit=/init ip=dhcp; bootz ${kernel_addr_r} - ${fdt_addr_r}'

# 带 initramfs
setenv bootcmd_net 'dhcp; tftp ${kernel_addr_r} Image; tftp ${initrd_addr} initramfs.cpio.gz; tftp ${fdt_addr_r} board.dtb; booti ${kernel_addr_r} ${initrd_addr}:${filesize} ${fdt_addr_r}'
```

### 单步调试网络启动

```bash
# 第1步：获取 IP
dhcp
# 应该看到：
# BOOTP broadcast 1
# DHCP client bound to 192.168.1.101

# 第2步：测试网络连通性
ping 192.168.1.100
# host 192.168.1.100 is alive

# 第3步：下载内核
tftp ${kernel_addr_r} Image
# Bytes transferred = 8765432 (x85d578)
echo ${filesize}

# 第4步：下载设备树
tftp ${fdt_addr_r} board.dtb

# 第5步：设置 bootargs 并启动
setenv bootargs 'console=ttymxc0,115200 root=/dev/nfs nfsroot=192.168.1.100:/nfs/rootfs ip=dhcp'
booti ${kernel_addr_r} - ${fdt_addr_r}
```

## NFS 根文件系统

### 为什么用 NFS

- **开发效率极高**：修改根文件系统中的文件立即生效，无需重新烧录
- **节省 Flash 寿命**：不写入 eMMC/NAND
- **调试方便**：可以直接在 PC 上编辑文件

### 服务端配置

```bash
# 安装 NFS 服务
sudo apt install nfs-kernel-server

# 创建 NFS 导出目录
sudo mkdir -p /nfs/rootfs
# 解压根文件系统到此目录
sudo tar -xzf rootfs.tar.gz -C /nfs/rootfs

# 配置导出
sudo vim /etc/exports
# /nfs/rootfs *(rw,sync,no_subtree_check,no_root_squash)

# 重启 NFS
sudo exportfs -ra
sudo systemctl restart nfs-kernel-server

# 验证
showmount -e localhost
```

### U-Boot 中的 NFS 启动

```bash
# 方式 1：通过 bootargs 让内核挂载 NFS
setenv bootargs 'console=ttymxc0,115200 root=/dev/nfs nfsroot=192.168.1.100:/nfs/rootfs,v3,tcp ip=192.168.1.101:192.168.1.100:192.168.1.1:255.255.255.0::eth0:off'
dhcp; tftp ${kernel_addr_r} Image; tftp ${fdt_addr_r} board.dtb; booti ${kernel_addr_r} - ${fdt_addr_r}

# 方式 2：使用 NFS 协议直接加载内核（不常用）
nfs ${kernel_addr_r} 192.168.1.100:/nfs/rootfs/boot/Image
nfs ${fdt_addr_r} 192.168.1.100:/nfs/rootfs/boot/board.dtb
```

### 常见 NFS 参数

| 参数 | 说明 |
|------|------|
| `nfsroot=IP:/path` | NFS 服务器地址和路径 |
| `nfsroot=IP:/path,v3,tcp` | 使用 NFSv3 + TCP |
| `ip=dhcp` | 内核也通过 DHCP 获取 IP |
| `ip=CLIENT:SERVER:GW:MASK:HOST:DEV:OFF` | 静态 IP 配置 |

::: warning 注意

使用 NFS rootfs 时，根文件系统中必须有 NFS 客户端支持。BusyBox 默认编译通常已包含。如果挂载失败，检查 PC 端的 exports 配置和防火墙。

:::

## PXE 网络启动

PXE（Preboot eXecution Environment）允许完全无盘启动，U-Boot 通过 DHCP 获取配置文件，再自动下载内核和设备树。

### 服务端配置

```bash
# DHCP 配置（/etc/dhcp/dhcpd.conf）
host evb-rk3399 {
    hardware ethernet 02:xx:xx:xx:xx:xx;
    fixed-address 192.168.1.101;
    option bootfile-name "pxelinux.0";
    next-server 192.168.1.100;
}

# TFTP 目录结构
/srv/tftp/
├── pxelinux.0
├── pxelinux.cfg/
│   ├── default          # 默认配置
│   └── 02-xx-xx-xx-xx-xx  # 按 MAC 地址匹配
├── Image
└── board.dtb
```

### U-Boot 端

```bash
# 通过 DHCP 获取 PXE 配置
dhcp

# U-Boot 会自动尝试下载 pxelinux.cfg 中的配置
# 然后按照配置加载内核和设备树
```

## U-Boot 网络命令速查

```bash
# 网卡操作
usb start                 # 启动 USB 网卡（如果用 USB-ETH）
usb net                   # 切换到 USB 网络
ethact                    # 查看当前活动网卡
ethrotate                 # 轮询多个网卡

# IP 配置
setenv ipaddr 192.168.1.101
setenv serverip 192.168.1.100
setenv gatewayip 192.168.1.1
setenv netmask 255.255.255.0
dhcp

# 连通性测试
ping 192.168.1.100

# 文件传输
tftp ${addr} filename
nfs ${addr} server:/path/filename

# 网络启动
bootp ${addr}            # BOOTP 协议
rarpboot ${addr} filename # RARP 协议（古老，基本不用了）

# 查看 DNS（如果启用）
dns ${addr} hostname
```

## 网络排错指南

### 1. ping 不通服务器

```
ping failed; host 192.168.1.100 is not alive
```

排查步骤：

```bash
# 确认网线已连接
# 确认 IP 在同一网段
printenv ipaddr serverip netmask

# 确认 MAC 地址正确
printenv ethaddr

# 确认网卡已初始化
ethact

# 尝试重新 DHCP
dhcp

# 从服务器端 ping 开发板
ping 192.168.1.101
```

### 2. TFTP 下载失败

```
TFTP error: 'Access violation'
```

- 检查 TFTP 目录权限：`ls -la /srv/tftp/`
- 检查文件是否存在
- 检查文件名大小写（Linux 区分大小写）

```
Timeout waiting for Ethernet link
```

- 网线未连接或链路协商失败
- 检查网口灯是否亮

### 3. TFTP 速度很慢

```
Bytes transferred = 8765432 (10 s)
```

U-Boot 默认使用较小的 TFTP 块大小（512B），可以增大：

```c
// defconfig 或 menuconfig
CONFIG_TFTP_BLOCKSIZE=1468    // 增大块大小
CONFIG_TFTP_WINDOWSIZE=64     // 启用窗口传输
```

或者环境变量控制：

```bash
setenv tftpblocksize 1468
```

### 4. NFS 挂载失败

内核报错 `VFS: Unable to mount root fs via NFS`：

- 检查 NFS 服务是否运行：`systemctl status nfs-kernel-server`
- 检查 exports 配置：`cat /etc/exports`
- 检查防火墙：`sudo ufw allow from 192.168.1.0/24`
- 确认内核编译了 NFS 客户端（CONFIG_NFS_FS=y）

### 5. 多网卡环境

```bash
# 查看所有网卡
ethact

# 切换活动网卡
setenv ethact ethernet@fe300000

# 或者 U-Boot 支持的话
setenv ethrotate yes
```

## 高级：自动检测网络启动

```bash
# 完整的自动启动脚本
setenv boot_targets 'mmc0 mmc1 usb0 dhcp'

# 先尝试本地存储，都失败再尝试网络
setenv bootcmd '
    for target in ${boot_targets}; do
        echo "Trying ${target}...";
        if run bootcmd_${target}; then
            echo "Booted from ${target}";
            exit;
        fi;
    done;
    echo "No boot device found!";
    reset;
'

setenv bootcmd_dhcp 'dhcp; tftp ${kernel_addr_r} Image; tftp ${fdt_addr_r} board.dtb; booti ${kernel_addr_r} - ${fdt_addr_r}'
```

## 小结

本篇介绍了 U-Boot 的网络功能：

- 基础配置：ipaddr / serverip / DHCP
- TFTP 服务端搭建和 U-Boot 端操作
- NFS 根文件系统：最高效的开发调试方式
- PXE 网络启动：无盘启动方案
- 网络排错：ping 失败、TFTP 超时、NFS 挂载失败
- 优化：TFTP 块大小和窗口传输

下一篇我们讲解 SPL 框架，了解 U-Boot 在极有限资源下的启动策略。

::: tip 持续更新中

章节与示例会陆续补充；若你发现疏漏或与所用 U-Boot 版本不符之处，欢迎评论交流。

:::
