---
title: 第一个 UEFI 应用程序 HelloWorld
author: EASYZOOM
date: 2026/04/25 10:00
categories:
  - UEFI从入门到精通
tags:
  - UEFI
  - EDK II
  - HelloWorld
  - QEMU
---

# 第一个 UEFI 应用程序 HelloWorld

## 前言

**C：** 这篇文章带你从零写一个 UEFI 应用程序，在 QEMU 虚拟机上跑起来。写代码、配编译、运行看效果，一气呵成——这就是你 UEFI 开发的"Hello World"时刻。

<!-- more -->

## 整体流程

```mermaid
flowchart LR
    Create["创建包目录<br/>MyHelloPkg"] --> WriteC["编写 HelloWorld.c"]
    WriteC --> WriteINF["编写 HelloWorld.inf"]
    WriteINF --> WriteDSC["修改 DSC 文件<br/>添加组件"]
    WriteDSC --> Build["执行构建<br/>build 命令"]
    Build --> Run["QEMU 运行<br/>OVMF.fd + HelloWorld.efi"]
    Run --> Output["屏幕输出<br/>Hello, UEFI World!"]
```

## 第一步：创建包目录结构

在 edk2 工作空间下创建你自己的包：

```bash
cd ~/edk2
mkdir -p MyHelloPkg/HelloWorld
```

最终的目录结构：

```
edk2/
├── MdePkg/
├── MdeModulePkg/
├── OvmfPkg/
├── ...
└── MyHelloPkg/                 # 你的自定义包
    ├── MyHelloPkg.dec          # 包描述文件
    ├── MyHelloPkg.dsc          # 包级 DSC
    └── HelloWorld/
        ├── HelloWorld.c        # 源代码
        └── HelloWorld.inf      # 模块描述文件
```

## 第二步：编写 DEC 文件

DEC（Package Description）文件是包的"身份证"，声明包的 GUID 等信息：

```bash
cat > MyHelloPkg/MyHelloPkg.dec << 'EOF'
## @file
# MyHelloPkg Package Description
# Copyright (c) 2026, EASYZOOM. All rights reserved.
##
[Defines]
  PACKAGE_NAME                  = MyHelloPkg
  PACKAGE_GUID                  = A1B2C3D4-E5F6-7890-ABCD-EF1234567890
  PACKAGE_VERSION               = 0.1
  DEC_SPECIFICATION             = 0x00010005
[Includes]
  Include
EOF
```

::: tip 关于 GUID
`PACKAGE_GUID` 需要全局唯一。用 `uuidgen` 生成后转大写即可，上面是示例值。
:::

## 第三步：编写 HelloWorld.c

```c
#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  Print (L"Hello, UEFI World!\n");
  Print (L"UEFI 规范版本: %d.%d\n",
         SystemTable->Hdr.Revision >> 16,
         SystemTable->Hdr.Revision & 0xFFFF);
  Print (L"配置表数量: %d\n", gST->NumberOfTableEntries);
  Print (L"========================================\n");
  Print (L"  欢迎来到 UEFI 开发的世界！\n");
  Print (L"========================================\n");
  return EFI_SUCCESS;
}
```

::: details 关于 UefiMain 入口点
- UEFI 应用程序的入口点**不叫 `main`**，而是叫 `UefiMain`
- 函数必须使用 `EFIAPI` 调用约定
- 返回值类型为 `EFI_STATUS`，成功返回 `EFI_SUCCESS`
- 两个参数由 UEFI 固件自动传入
:::

## 第四步：编写 INF 文件

INF 文件告诉构建系统如何编译这个模块：

```ini
## @file
# HelloWorld.inf - Hello World UEFI Application
# Copyright (c) 2026, EASYZOOM. All rights reserved.
##
[Defines]
  INF_VERSION                    = 0x00010005
  BASE_NAME                      = HelloWorld
  FILE_GUID                      = B2C3D4E5-F6A7-8901-BCDE-F12345678901
  MODULE_TYPE                    = UEFI_APPLICATION
  VERSION_STRING                 = 0.1
  ENTRY_POINT                    = UefiMain

[Sources]
  HelloWorld.c

[Packages]
  MdePkg/MdePkg.dec
  MyHelloPkg/MyHelloPkg.dec

[LibraryClasses]
  UefiApplicationEntryPoint
  UefiLib
  UefiBootServicesTableLib
```

INF 文件关键字段说明：

| 字段 | 说明 |
|------|------|
| `MODULE_TYPE` | `UEFI_APPLICATION` 表示可执行应用 |
| `ENTRY_POINT` | 入口函数名，必须与 C 代码一致 |
| `FILE_GUID` | 模块唯一标识符，全局不可重复 |
| `UefiApplicationEntryPoint` | 提供 UEFI 应用入口点包装逻辑的库 |

## 第五步：编写 DSC 文件

创建独立的 DSC 文件来构建你的应用：

```bash
cat > MyHelloPkg/MyHelloPkg.dsc << 'EOF'
## @file
# MyHelloPkg Platform Description
##
[Defines]
  PLATFORM_NAME                  = MyHelloPkg
  PLATFORM_GUID                  = C3D4E5F6-A7B8-9012-CDEF-123456789012
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010005
  SUPPORTED_ARCHITECTURES        = IA32|X64
  BUILD_TARGETS                  = DEBUG|RELEASE
  SKUID_IDENTIFIER               = DEFAULT

[LibraryClasses]
  UefiLib|MdePkg/Library/UefiLib/UefiLib.inf
  UefiApplicationEntryPoint|MdePkg/Library/UefiApplicationEntryPoint/UefiApplicationEntryPoint.inf
  UefiBootServicesTableLib|MdePkg/Library/UefiBootServicesTableLib/UefiBootServicesTableLib.inf

[Components]
  MyHelloPkg/HelloWorld/HelloWorld.inf
EOF
```

## 第六步：构建

```bash
cd ~/edk2
source edksetup.sh    # 如果之前没配置的话
build -a X64 -p MyHelloPkg/MyHelloPkg.dsc -t GCC5 -b DEBUG
```

成功后，EFI 文件位于：

```
Build/MyHelloPkg/DEBUG_GCC5/X64/HelloWorld.efi
```

## 第七步：在 QEMU 中运行

### 安装 QEMU

```bash
sudo apt install qemu-system-x86
```

### 构建并运行

首先确保 OVMF 固件已构建（如果之前没构建过）：

```bash
build -a X64 -p OvmfPkg/OvmfPkgX64.dsc -t GCC5 -b DEBUG
```

然后准备 FAT 磁盘并启动 QEMU：

```bash
# 创建虚拟磁盘并复制 EFI 文件
mkdir -p /tmp/efi_disk/EFI/BOOT
cp Build/MyHelloPkg/DEBUG_GCC5/X64/HelloWorld.efi /tmp/efi_disk/EFI/BOOT/

# 启动 QEMU
qemu-system-x86_64 \
    -drive if=pflash,format=raw,readonly=on,file=Build/OvmfX64/DEBUG_GCC5/FV/OVMF_CODE.fd \
    -drive if=pflash,format=raw,file=Build/OvmfX64/DEBUG_GCC5/FV/OVMF_VARS.fd \
    -drive format=raw,file=fat:rw:/tmp/efi_disk \
    -net none -nographic
```

### 在 UEFI Shell 中执行

QEMU 启动后进入 UEFI Shell，输入以下命令：

```
Shell> fs0:
FS0:\> cd EFI/BOOT
FS0:\EFI\BOOT> HelloWorld.efi
Hello, UEFI World!
UEFI 规范版本: 2.10
配置表数量: 53
========================================
  欢迎来到 UEFI 开发的世界！
========================================
```

::: tip 关于 OVMF 分区
OVMF 使用两个 pflash 分区：`OVMF_CODE.fd`（固件代码，只读）和 `OVMF_VARS.fd`（NVRAM 变量，可读写）。变量分区可以保留，这样你的设置不会丢失。
:::

## Print 函数族详解

UEFI 提供了几个输出函数：

| 函数 | 头文件 | 说明 |
|------|--------|------|
| `Print()` | `UefiLib.h` | 基本格式化输出，最常用 |
| `PrintEx()` | `UefiLib.h` | 扩展格式化，支持错误码等 |
| `AsciiPrint()` | `UefiLib.h` | 输出 ASCII 字符串 |
| `DebugPrint()` | `DebugLib.h` | 调试输出（串口），生产环境不显示 |

```c
// Print 支持的格式化符号
Print (L"字符串: %s\n", L"Hello");
Print (L"十六进制: %x\n", 0xDEADBEEF);
Print (L"十进制: %d\n", 42);
Print (L"指针: %p\n", (VOID *)SystemTable);
Print (L"GUID: %g\n", &SomeGuid);
```

::: warning 格式化字符串注意
UEFI 的 Print 使用**宽字符**（`CHAR16`，即 UTF-16），字符串字面量要加 `L` 前缀：`L"Hello"`，而不是 C 标准的 `"Hello"`。
:::

## 常见问题

**构建时报错找不到 INF：** 确认 `PACKAGES_PATH` 包含了你的包所在目录：`echo $PACKAGES_PATH`

**QEMU 中找不到 EFI 文件：** 确认 FAT 磁盘目录结构正确：`ls -la /tmp/efi_disk/EFI/BOOT/`

**运行后没有输出：** 确保使用 `-nographic` 或 `-serial stdio` 参数。

## 小结

恭喜你完成了第一个 UEFI 应用程序！回顾关键步骤：

1. **创建包结构**：`MyHelloPkg/HelloWorld/` 下放 `.c` 和 `.inf` 文件
2. **编写 C 代码**：入口点是 `EFIAPI UefiMain()`，使用 `Print()` 输出
3. **编写 INF 文件**：声明 `MODULE_TYPE = UEFI_APPLICATION`，指定入口点
4. **编写 DSC 文件**：引入依赖库，添加组件
5. **构建**：`build -a X64 -p MyHelloPkg/MyHelloPkg.dsc`
6. **运行**：QEMU + OVMF 启动，在 UEFI Shell 中执行 `.efi` 文件

有了这个基础，接下来你可以尝试更复杂的操作：读取 PCI 配置、调用 UEFI Protocol、编写 DXE 驱动等等。UEFI 开发的世界，从此打开了！
