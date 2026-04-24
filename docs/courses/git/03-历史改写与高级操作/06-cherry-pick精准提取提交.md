---
title: cherry-pick 精准提取提交
author: EASYZOOM
date: 2026/04/24 19:00
categories:
 - Git入门与实践
tags:
 - Git
 - cherry-pick
---

# cherry-pick 精准提取提交

## 前言

**C：** 有时候你不需要合并整个分支，只需要某一个特定的提交——比如在 bugfix 分支修了一个 Bug，想单独把这个修复应用到 main 分支上。`git cherry-pick` 就是干这个的，它像摘樱桃一样，从一个分支精准地挑选提交到另一个分支。

<!-- more -->

## 基本用法

### 挑选单个提交

```shell
# 切换到目标分支
git switch main

# 从其他分支挑选一个提交
git cherry-pick a1b2c3d
```

`a1b2c3d` 是你想"摘取"的提交哈希值。

### 挑选多个提交

```shell
# 挑选连续范围的提交
git cherry-pick start-commit..end-commit

# 示例：挑选 A 到 B 之间的提交（不包含 A）
git cherry-pick d4e5f6g..l0m1n2o

# 包含 A（A 的写法）
git cherry-pick d4e5f6g^..l0m1n2o

# 挑选多个不连续的提交
git cherry-pick a1b2c3d h7i8j9k p3q4r5s
```

### 从其他分支挑选

```shell
# 不需要先知道提交哈希，直接指定分支和位置
git cherry-pick feature/login
# 这相当于 cherry-pick feature/login 分支的最新提交

# 指定分支上的某个提交
git cherry-pick feature-login~2
```

## 工作原理

cherry-pick 的本质是在当前分支上创建一个**新的提交**，内容和原始提交相同，但是一个全新的提交对象（哈希值不同）：

```mermaid
gitGraph
    commit id: "C1"
    branch feature
    checkout feature
    commit id: "C2"
    commit id: "C3: bug fix" tag: "原始提交"
    checkout main
    commit id: "C4"
    cherry-pick id: "C3': bug fix" tag: "cherry-pick"
```

::: warning 注意
因为 cherry-pick 创建的是新提交，所以原始提交和 cherry-pick 后的提交没有关联关系。对原始提交的 `revert` 不会影响 cherry-pick 的提交，反之亦然。
:::

## 实战场景

### 场景一：将 hotfix 应用到多个分支

你在一个 release 分支上紧急修复了一个 Bug，需要同时应用到 main 和其他活跃分支：

```shell
# 1. 在 release/1.0 上修复了 Bug
# 提交哈希：a1b2c3d

# 2. 应用到 main
git switch main
git cherry-pick a1b2c3d
git push origin main

# 3. 应用到 develop
git switch develop
git cherry-pick a1b2c3d
git push origin develop

# 4. 应用到其他 release 分支
git switch release/1.1
git cherry-pick a1b2c3d
git push origin release/1.1
```

### 场景二：从别人的分支"借"提交

```shell
# 队友在他的分支上修了一个编译错误
# 你需要这个修复才能继续开发

# 1. 先 fetch 获取最新的远程提交
git fetch origin

# 2. 查看队友分支的提交
git log origin/teammate-branch --oneline
# 输出：a1b2c3d fix: resolve compilation error

# 3. cherry-pick 到自己的分支
git cherry-pick a1b2c3d
```

### 场景三：遗漏的提交

你在 feature 分支上开发，不小心在 main 分支上提交了东西：

```shell
# 在 main 上提交了一个小修复
git commit -m "fix: typo in config"

# 把它移到 feature 分支
git switch feature
git cherry-pick main

# 然后撤销 main 上的提交
git switch main
git reset --soft HEAD~1
```

## 处理冲突

cherry-pick 时也可能产生冲突：

```shell
git cherry-pick a1b2c3d
# error: could not apply a1b2c3d... fix: resolve bug
# hint: after resolving the conflicts, mark the corrected paths
```

### 解决冲突的步骤

```shell
# 1. 查看冲突文件
git status

# 2. 手动解决冲突
vim conflicted-file.js

# 3. 添加解决后的文件
git add conflicted-file.js

# 4. 继续 cherry-pick（使用原始提交信息）
git cherry-pick --continue

# 如果想放弃
git cherry-pick --abort
```

### 批量 cherry-pick 时跳过冲突提交

```shell
# 挑选多个提交时，某个冲突解决了但想跳过
git cherry-pick --skip

# 继续处理下一个
git cherry-pick --continue
```

## 常用选项

| 选项 | 说明 |
|------|------|
| `--no-commit` | 应用更改但不创建提交，让你手动提交 |
| `--signoff` | 添加 Signed-off-by 行 |
| `-x` | 在提交信息中附加原始提交的哈希，便于追踪 |
| `--edit` | 允许编辑 cherry-pick 的提交信息 |
| `-n` | 和 `--no-commit` 一样 |
| `--ff` | 如果可以快进则快进（多个连续 cherry-pick 时） |

### 推荐使用 -x 追踪来源

```shell
git cherry-pick -x a1b2c3d
```

提交信息会自动附加：

```
fix: resolve compilation error

(cherry picked from commit a1b2c3d)
```

::: tip 笔者说
在团队协作中，`-x` 选项非常实用。它能让你和队友一眼看出这个提交是从哪里 cherry-pick 来的，方便追溯和排查问题。
:::

### 使用 --no-commit 批量应用

```shell
# 应用多个提交但不自动提交，最后统一提交
git cherry-pick --no-commit a1b2c3d d4e5f6g h7i8j9k
git commit -m "feat: cherry-pick login and registration fixes"
```

## cherry-pick 与 merge/rebase 的对比

| 操作 | 适用场景 | 优点 | 缺点 |
|------|---------|------|------|
| `merge` | 合并整个分支 | 保留完整历史，有合并记录 | 会引入不需要的提交 |
| `rebase` | 整合分支到另一分支 | 线性历史 | 改写提交历史 |
| `cherry-pick` | 精准提取特定提交 | 不引入无关提交 | 容易丢失关联关系 |

::: warning 注意
如果一个提交依赖它前面的多个提交（如重构后的代码），单独 cherry-pick 可能导致代码不完整或编译失败。这种情况下应该使用 merge 或 rebase。
:::

## 常见问题

### cherry-pick 后后悔了怎么办

```shell
# cherry-pick 刚完成，还没有 push
git reset --hard HEAD~1

# 或者使用 revert
git revert HEAD
```

### cherry-pick 了 merge commit

默认情况下 `cherry-pick` 不会处理 merge commit（有多个父提交的提交）。如果需要：

```shell
# cherry-pick 一个 merge commit，指定主分支为第一个父提交
git cherry-pick -m 1 <merge-commit-hash>
```

### 查看已 cherry-pick 的提交

没有内置命令可以直接查看，但可以借助提交信息中的 `(cherry picked from ...)` 标记：

```shell
# 搜索 cherry-pick 来源
git log --grep="cherry picked from"
```

## 小结

- **cherry-pick** 用于从其他分支精准提取提交到当前分支
- 它创建的是**新提交**，哈希值与原始提交不同
- 推荐使用 `-x` 选项标记来源，便于追溯
- 注意提交之间的依赖关系，独立的小修复最适合 cherry-pick
- 处理冲突时可以使用 `--continue`、`--abort`、`--skip`

下一篇我们学习 `git revert`，它是撤销提交的安全方式。
