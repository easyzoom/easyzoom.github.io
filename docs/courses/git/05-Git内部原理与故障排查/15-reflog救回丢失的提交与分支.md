---
title: reflog 救回丢失的提交与分支
author: EASYZOOM
date: 2026/04/24 19:00
categories:
 - Git入门与实践
tags:
 - Git
 - reflog
 - 灾难恢复
---

# reflog 救回丢失的提交与分支

## 前言

**C：** `git reset --hard` 之后发现切错了分支？`git rebase` 搞砸了？不小心删除了分支？别慌，Git 几乎不会真正"丢失"你的代码——只要你知道怎么用 `reflog`。本文就是你的 Git 事故急救手册。

<!-- more -->

## reflog 是什么

reflog（reference log）记录了 HEAD 和分支指针的所有移动历史。每次 checkout、commit、merge、rebase、reset 操作都会被记录。

```shell
# 查看 HEAD 的操作日志
git reflog

# 输出示例：
# a1b2c3d HEAD@{0}: reset: moving to HEAD~2
# e5f6g7h HEAD@{1}: commit: add login validation
# d4e5f6g HEAD@{2}: commit: add login page
# b3c4d5e HEAD@{3}: checkout: moving from main to feature
# a1b2c3d HEAD@{4}: commit: initial commit
```

::: tip 笔者说
reflog 默认保留 90 天（对合并等操作）和 30 天（对其他操作）。在这段时间内，任何通过 reflog 能找到的提交都不会被垃圾回收删除。
:::

## 理解 reflog 的输出

```shell
git reflog show HEAD
```

| 字段 | 含义 |
|------|------|
| `a1b2c3d` | 提交哈希 |
| `HEAD@{0}` | 引用名称和索引（0 是最新的） |
| `reset: moving to HEAD~2` | 操作描述 |

`HEAD@{0}` 中的数字表示时间顺序：`{0}` 是最近一次操作，`{1}` 是上一次，以此类推。

## 实战场景

### 场景一：reset --hard 后恢复

```shell
# 你不小心硬重置了
git reset --hard HEAD~3
# 现在最近的 3 个提交"不见了"

# 1. 查看 reflog
git reflog
# a1b2c3d HEAD@{0}: reset: moving to HEAD~3
# e5f6g7h HEAD@{1}: commit: third commit
# d4e5f6g HEAD@{2}: commit: second commit
# b3c4d5e HEAD@{3}: commit: first commit
# a1b2c3d HEAD@{4}: commit: initial commit

# 2. 找到 reset 之前的位置
# HEAD@{1} 就是 reset 之前的状态

# 3. 恢复到之前的状态
git reset --hard HEAD@{1}
# 或直接指定哈希
git reset --hard e5f6g7h
```

### 场景二：误删分支后恢复

```shell
# 不小心删除了分支
git branch -D feature-login
# Deleted branch feature-login (was e5f6g7h).

# 1. 查看 reflog 找到分支最后的提交
git reflog
# ... 找到 feature-login 分支的最后提交 ...
# e5f6g7h HEAD@{5}: commit: add login validation

# 2. 恢复分支
git switch -c feature-login e5f6g7h
# 或
git branch feature-login e5f6g7h
```

### 场景三：rebase 后想回退

```shell
# rebase 搞砸了
git rebase main
# 大量冲突，或者结果不对

# 1. 查看操作前
git reflog
# b3c4d5e HEAD@{0}: rebase finished: returning to refs/heads/feature
# b3c4d5e HEAD@{1}: rebase: fix validation
# e5f6g7h HEAD@{2}: rebase: add login page
# a1b2c3d HEAD@{3}: checkout: moving from feature to main

# 注意：reflog 中 rebase 前的原始提交可能不在这个列表中
# 需要使用 feature 分支的 reflog

# 2. 查看分支的 reflog
git reflog show feature-login

# 3. 恢复到 rebase 前
git reset --hard feature-login@{5}
```

### 场景四：merge 后想撤销

```shell
# 不小心合并了错误的分支
git merge wrong-feature

# 撤销合并
git reset --hard HEAD@{1}

# 或者使用 revert（更安全）
git revert -m 1 HEAD
```

### 场景五：checkout 丢失的 stash

```shell
# 不小心 stash drop 了
git stash drop stash@{0}

# 1. 查看 stash 相关的 reflog
git reflog --no-refs | grep stash
# 或者更直接：
git fsck --dangling 2>&1 | grep commit

# 2. 找到 dangling commit（stash 本质上是一个 commit）
# dangling commit a1b2c3d...

# 3. 恢复 stash
git stash apply a1b2c3d
```

## 查看分支的 reflog

```shell
# 查看 main 分支的操作日志
git reflog show main

# 查看任意分支
git reflog show feature-login

# 简写
git reflog main
```

## reflog 的过期与清理

```shell
# 查看 reflog 配置
git config --get gc.reflogExpire
# 90 days

git config --get gc.reflogExpireUnreachable
# 30 days

# 修改保留时间（不建议缩短）
git config --global gc.reflogExpire 180.days
git config --global gc.reflogExpireUnreachable 90.days

# 手动清理过期的 reflog 条目
git reflog expire --expire=now --all

# 清理后执行 gc 删除真正无引用的对象
git gc --prune=now
```

::: warning 严重警告
执行 `git reflog expire --expire=now --all && git gc --prune=now` 后，所有 reflog 中过期的提交将无法恢复。除非你确定不再需要，否则不要执行此操作。
:::

## reflog 与其他恢复方式的对比

| 恢复方式 | 适用场景 | 优点 | 缺点 |
|---------|---------|------|------|
| `git reflog` | 知道操作历史 | 最通用，可精确定位 | 需要手动查找 |
| `git fsck --dangling` | 提交完全无引用 | 能找到被遗忘的提交 | 需要逐个检查 |
| `git reset --hard` | 最近一次操作 | 最快速 | 只能回到上一步 |
| `git revert` | 公共分支 | 不改写历史 | 创建额外提交 |

## reflog 高级用法

### 查看某段时间内的操作

```shell
# 查看最近 1 小时的操作
git reflog --date=relative

# 查看指定日期之后的操作
git reflog --after="2026-04-01"

# 查看指定日期之前的操作
git reflog --before="2026-04-24"
```

### 格式化输出

```shell
# 自定义输出格式
git reflog --format="%H %gd %gs %ci"

# - %H：完整哈希
# - %gd：reflog 标识（HEAD@{0}）
# - %gs：操作描述
# - %ci：提交日期
```

### 基于时间恢复

```shell
# 恢复到昨天这个时候的状态
git reset --hard 'HEAD@{yesterday}'

# 恢复到 2 小时前
git reset --hard 'HEAD@{2.hours.ago}'

# 恢复到指定时间
git reset --hard 'HEAD@{2026-04-24 10:00:00}'
```

## reflog 的局限性

1. **只在本地有效**：reflog 不会推送到远程。如果本地仓库损坏，远程没有 reflog 信息。
2. **有过期时间**：默认 30-90 天后，未被引用的提交可能被 GC 删除。
3. **不记录文件内容的变化**：reflog 只记录指针移动，不记录每次操作的具体文件变更。
4. **force push 后远程的不同**：如果有人 force push 了远程分支，你的 reflog 和远程的历史会产生分歧。

## 小结

- **reflog** 是 Git 的"撤销按钮"，记录所有指针移动历史
- `git reflog` 查看日志，`git reset --hard HEAD@{n}` 恢复
- 支持 `@{yesterday}`、`@{2.hours.ago}` 等时间语法
- 默认保留 30-90 天，不要轻易清理
- 可以恢复误删的分支、误操作的 reset/rebase、丢失的提交
- 配合 `git fsck --dangling` 可以找到更隐蔽的"丢失"提交

掌握了 reflog，下一篇我们将面对各种 Git 灾难场景，综合运用所学知识进行故障恢复。
