---
title: git bisect 二分查找引入 Bug 的提交
author: EASYZOOM
date: 2026/04/24 19:00
categories:
 - Git入门与实践
tags:
 - Git
 - bisect
 - 调试
---

# git bisect 二分查找引入 Bug 的提交

## 前言

**C：** 测试发现一个 Bug，但不知道是哪次提交引入的。几百个提交里逐个排查？太慢了。`git bisect` 用二分查找法，能在 O(log n) 次测试内精准定位罪魁祸首。1000 个提交，最多只需 10 次测试。

<!-- more -->

## 基本原理

`git bisect` 的核心思想就是二分查找：

```mermaid
flowchart TD
    A["标记一个'坏'提交（有 Bug）"] --> B["标记一个'好'提交（无 Bug）"]
    B --> C["Git 检出中间的提交"]
    C --> D{测试：Bug 存在吗？}
    D -->|是| E[标记为"坏"]
    D -->|否| F[标记为"好"]
    E --> G[继续缩小范围]
    F --> G
    G --> H{只剩一个提交？}
    H -->|否| C
    H -->|是| I["🎯 找到引入 Bug 的提交！"]
```

1000 个提交 → 最多 10 次测试
500 个提交 → 最多 9 次测试
100 个提交 → 最多 7 次测试

## 手动使用 bisect

### 启动 bisect

```shell
# 启动 bisect
git bisect start

# 标记当前提交为"坏"（有 Bug）
git bisect bad HEAD

# 标记一个已知的"好"提交（无 Bug）
git bisect good v2.0.0

# Git 会输出类似：
# Bisecting: 125 revisions left to test after this (roughly 7 steps)
# [a1b2c3d...] some commit message
```

### 逐步测试

```shell
# Git 自动检出一个中间提交
# 你运行测试，检查 Bug 是否存在

# 如果 Bug 存在（当前提交是"坏"的）
git bisect bad

# 如果 Bug 不存在（当前提交是"好"的）
git bisect good

# 每次标记后 Git 会自动缩小范围
# Bisecting: 62 revisions left to test after this (roughly 6 steps)
# [d4e5f6g...] some other commit message
```

### 找到目标

```shell
# 当 Git 确定了引入 Bug 的提交
# a1b2c3d is the first bad commit
# commit a1b2c3d
# Author: ...
# Date: ...
#     feat: add new payment module

# 结束 bisect
git bisect reset
```

::: tip 笔者说
`git bisect reset` 会将 HEAD 恢复到 bisect 开始前的状态。忘记 reset 也没关系，bisect 期间做的标记不会影响提交历史。
:::

## 一行命令启动

```shell
# 简写：一步完成 start、bad、good
git bisect start HEAD v2.0.0
# 等价于：
# git bisect start
# git bisect bad HEAD
# git bisect good v2.0.0
```

## 自动化 bisect

如果可以编写一个测试脚本来检测 Bug，bisect 可以全自动运行：

### 编写测试脚本

```shell
# 创建一个测试脚本
cat > test-bug.sh << 'EOF'
#!/bin/bash
# 编译项目
make build
if [ $? -ne 0 ]; then
    echo "Build failed"
    exit 1
fi

# 运行测试
./run-tests --suite regression
if [ $? -ne 0 ]; then
    echo "Tests failed"
    exit 1
fi

echo "All good"
exit 0
EOF

chmod +x test-bug.sh
```

### 运行自动化 bisect

```shell
# 启动自动化 bisect
git bisect start HEAD v2.0.0
git bisect run ./test-bug.sh
```

Git 会自动对每个中间提交运行脚本：
- 脚本返回 `0`（成功）→ 标记为 good
- 脚本返回非 `0`（失败）→ 标记为 bad
- 脚本返回 `125` → 跳过该提交

```shell
# 运行结束后
# a1b2c3d is the first bad commit
# bisect run success
```

### 跳过无法测试的提交

```shell
# 某些提交无法编译（缺少依赖等）
# 在手动模式下
git bisect skip

# 在自动模式下
# 脚本返回 125 表示跳过
# exit 125
```

## 实战场景

### 场景一：定位编译错误引入的提交

```shell
# 当前代码编译失败，但上周还好
git bisect start HEAD HEAD~50

# 每次检出一个提交后尝试编译
make

# 如果编译成功
git bisect good

# 如果编译失败
git bisect bad

# 重复直到找到
# a1b2c3d is the first bad commit
#     fix: update build config (删掉了一个重要的编译选项)
```

### 场景二：定位回归 Bug

```shell
# v1.5 功能正常，v2.0 有回归 Bug
git bisect start v2.0 v1.5

# 编写自动化测试脚本
cat > check-feature.sh << 'EOF'
#!/bin/bash
# 检查特定功能是否正常
python3 test_specific_feature.py
EOF
chmod +x check-feature.sh

# 全自动查找
git bisect run ./check-feature.sh

# a1b2c3d is the first bad commit
git bisect reset
```

### 场景三：跳过无法测试的提交

```shell
# 有些提交缺少编译环境
# 手动标记跳过
git bisect skip

# Git 会继续在剩余的范围内查找
# Bisecting: 8 revisions left to test after this (roughly 3 steps)
# (some revisions skipped)
```

## bisect 的实用选项

### 查看当前 bisect 状态

```shell
# 查看当前 bisect 的进度
git bisect log

# 查看当前 bisect 的可视化
git bisect visualize
# 或
git bisect view
```

### 只在特定文件中查找

如果知道 Bug 与某个文件有关，可以缩小范围：

```shell
# 只在修改了 src/payment.c 的提交中查找
git bisect start -- src/payment.c
git bisect bad HEAD
git bisect good v2.0.0
```

### 保存和恢复 bisect 状态

```shell
# 保存当前 bisect 状态到文件
git bisect log > bisect-log.txt

# 恢复 bisect 状态
git bisect replay bisect-log.txt
```

这在调试需要中断时很有用——保存进度，下次继续。

## bisect 的注意事项

| 注意事项 | 说明 |
|---------|------|
| 测试环境一致性 | 确保"好"和"坏"的判断标准一致 |
| 跳过提交过多 | 跳过太多提交可能影响定位精度 |
| 中断恢复 | 使用 `git bisect log` 保存进度 |
| 多个 Bug | bisect 每次只能定位一个 Bug |
| 依赖外部状态 | 如果 Bug 依赖外部环境（数据库、网络），确保环境一致 |

## 小结

- `git bisect` 用二分查找快速定位引入 Bug 的提交
- 手动模式：`start` → 标记 `good/bad` → 重复 → `reset`
- 自动模式：`git bisect run <test-script>`
- 跳过不可测提交：`git bisect skip`
- 可以限定范围：`git bisect start -- <path>`
- 保存恢复：`git bisect log` / `git bisect replay`

bisect 是排查疑难 Bug 的利器，当你不知道是哪次提交引入问题时，它能帮你精准定位。

到这里，历史改写与高级操作章节就全部完成了。下一篇我们将进入远程协作与工作流，讨论远程仓库管理和 Code Review 流程。
