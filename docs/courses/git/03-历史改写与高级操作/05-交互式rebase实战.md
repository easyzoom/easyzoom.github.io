---
title: 交互式 rebase 实战
author: EASYZOOM
date: 2026/04/24 19:00
categories:
 - Git入门与实践
tags:
 - Git
 - rebase
 - 提交历史
---

# 交互式 rebase 实战

## 前言

**C：** 普通的 `git rebase` 只是把提交"搬"到另一个基点上，而交互式 rebase（`git rebase -i`）则让你像编辑文档一样编排提交历史——合并、重排、修改信息、拆分提交，甚至删除不需要的提交。掌握它，你的提交历史就能始终保持干净整洁。

<!-- more -->

## 基本用法

```shell
# 对最近 3 个提交进行交互式变基
git rebase -i HEAD~3

# 对某个提交之后的所有提交进行交互式变基
git rebase -i <commit-hash>

# 对某个分支进行交互式变基
git rebase -i main
```

::: tip 笔者说
`HEAD~3` 表示"当前提交往前 3 个提交"。这里不包含目标提交本身，只处理它之后的提交。
:::

执行后会打开编辑器（默认使用 Git 配置的编辑器），显示类似如下内容：

```
pick a1b2c3d feat: add user login page
pick d4e5f6g feat: add login validation
pick h7i8j9k fix: correct login API endpoint
pick l0m1n2o docs: update README
```

## 操作命令详解

### pick — 保留提交

```shell
pick a1b2c3d feat: add user login page
```

保留该提交不变。这是默认行为，什么也不做。

### reword — 修改提交信息

```shell
reword a1b2c3d feat: add user login page
```

保留提交内容，但会弹出编辑器让你修改提交信息。

**使用场景：** 提交信息写错了，或者想规范化。

### edit — 暂停以修改内容

```shell
edit a1b2c3d feat: add user login page
```

在该提交处暂停变基，让你修改文件内容。

**使用场景：** 提交时漏掉了一个文件，或者发现代码有问题需要修改。

**操作流程：**

```shell
# 1. Git 在该提交处暂停
# 2. 修改文件
git add changed-file.js
git commit --amend --no-edit

# 3. 继续变基
git rebase --continue

# 如果想放弃
git rebase --abort
```

### squash — 合并提交（保留信息）

```shell
squash d4e5f6g feat: add login validation
```

将该提交与前一个 `pick` 的提交合并，保留两个提交的信息。

**使用场景：** 连续几个小提交属于同一功能，想合并为一个。

### fixup — 合并提交（丢弃信息）

```shell
fixup h7i8j9k fix: correct login API endpoint
```

类似 squash，但丢弃该提交的提交信息，只保留前一个提交的信息。

**使用场景：** `squash` 合并时你又要编辑提交信息，如果只想用前一条信息，用 `fixup` 更省事。

::: tip 笔者说
如果你还没决定好用 squash 还是 fixup，可以用 `squash`，这样不会丢失提交信息。在合并后的编辑器中你可以自由编辑最终信息。
:::

### drop — 删除提交

```shell
drop l0m1n2o docs: update README
```

直接丢弃该提交。

**使用场景：** 提交了不该提交的东西（如调试代码），想从历史中移除。

## 实战场景

### 场景一：合并连续的小提交

开发时你可能产生很多零碎提交：

```
pick a1b2c3d WIP: login page structure
pick d4e5f6g WIP: add form fields
pick h7i8j9k WIP: add submit button
pick l0m1n2o WIP: connect API
```

将这些合并为一个整洁的提交：

```
pick a1b2c3d feat: add user login page
fixup d4e5f6g WIP: add form fields
fixup h7i8j9k WIP: add submit button
fixup l0m1n2o WIP: connect API
```

保存退出后，Git 会弹出编辑器让你编辑最终的提交信息：

```
# This is a combination of 4 commits.
# The first commit's message is:
feat: add user login page

# Please enter the commit message for your changes.
feat: add user login page

- Page layout and form structure
- Form validation
- API integration
```

### 场景二：修改提交顺序

有时候先写测试再写实现，但想让实现排在前面：

```
# 修改前
pick a1b2c3d test: add login test cases
pick d4e5f6g feat: add user login page

# 修改后（交换顺序）
pick d4e5f6g feat: add user login page
pick a1b2c3d test: add login test cases
```

::: warning 注意
修改提交顺序可能导致冲突，因为后面的提交可能依赖前面的更改。
:::

### 场景三：拆分一个提交

如果一个提交包含了太多不相关的改动，可以拆分它：

```shell
# 1. 开始交互式 rebase
git rebase -i HEAD~2

# 2. 将需要拆分的提交标记为 edit
pick a1b2c3d feat: add user login page
edit d4e5f6g feat: add user registration and fix bug

# 3. Git 在 d4e5f6g 处暂停
# 4. 重置该提交（保留文件修改）
git reset HEAD~1

# 5. 现在所有改动都回到了工作区
# 6. 分别提交
git add user-register.js
git commit -m "feat: add user registration"

git add bugfix.js
git commit -m "fix: correct validation logic"

# 7. 继续变基
git rebase --continue
```

### 场景四：修改某个历史提交

```shell
# 1. 开始交互式 rebase
git rebase -i HEAD~5

# 2. 找到需要修改的提交，将 pick 改为 edit
pick a1b2c3d commit 1
pick d4e5f6g commit 2
edit h7i8j9k commit 3    # 修改这个提交
pick l0m1n2o commit 4
pick p3q4r5s commit 5

# 3. Git 在 h7i8j9k 处暂停
# 4. 修改文件
vim some-file.js

# 5. 暂存并修正提交
git add some-file.js
git commit --amend --no-edit

# 6. 继续变基
git rebase --continue
```

## 处理冲突

交互式 rebase 中处理冲突的方式和普通 rebase 相同，但有以下技巧：

### 跳过某个提交

```shell
# 如果某个提交的改动不再需要，可以跳过
git rebase --skip
```

### 编辑器配置

```shell
# 指定编辑器（如果默认编辑器不趁手）
git config --global core.editor "vim"

# 或者使用 VS Code
git config --global core.editor "code --wait"

# 或者使用 nano
git config --global core.editor "nano"
```

### rebase 时的自动冲突解决策略

```shell
# 使用递归策略
git rebase -i --strategy=recursive HEAD~5

# 使用 ours 策略（以当前分支为准，谨慎使用）
git rebase -i --strategy=ours HEAD~5
```

## rebase 的终止操作

```shell
# 继续变基（解决冲突后）
git rebase --continue

# 放弃变基，回到操作前的状态
git rebase --abort

# 跳过当前提交，继续处理下一个
git rebase --skip
```

::: tip 笔者说
不确定要不要继续时，先 `--abort` 回到安全状态，想好了再重新开始。不要在犹豫不决的状态下继续操作。
:::

## 高级技巧

### 不用编辑器的 rebase

```shell
# 使用 --autosquash 自动整理 fixup 提交
git rebase -i --autosquash HEAD~10

# 配合 commit 使用 --fixup 创建自动合并标记
# 开发时：
git commit --fixup=<commit-hash>

# 整理时：
git rebase -i --autosquash HEAD~10
# Git 会自动将 fixup 的提交排列到目标提交下方
```

### rebase 到任意分支

```shell
# 将当前分支的提交变基到目标分支上
git rebase -i feature-base

# 变基到远程分支
git rebase -i origin/main
```

### 在 rebase 过程中执行命令

```shell
# 使用 --exec 在每个提交后执行命令（如运行测试）
git rebase -i --exec "npm test" HEAD~10
```

::: warning 注意
`--exec` 会在每个提交被重新应用后执行命令。如果命令失败，rebase 会暂停，让你修复问题。这在确保变基后每个提交都能通过测试时非常有用。
:::

## 小结

| 命令 | 用途 |
|------|------|
| `pick` | 保留提交 |
| `reword` | 修改提交信息 |
| `edit` | 暂停修改提交内容 |
| `squash` | 合并提交（保留信息） |
| `fixup` | 合并提交（丢弃信息） |
| `drop` | 删除提交 |
| `--autosquash` | 自动排列 fixup 提交 |
| `--exec` | 每个提交后执行命令 |

- 交互式 rebase 只用于**个人本地分支**，不要对已推送的公共分支使用
- `--abort` 是你的安全网，不确定就放弃重来
- `--autosquash` + `--fixup` 可以自动整理零碎提交

下一篇我们来学习 cherry-pick，它可以从其他分支精准"摘取"某个提交。
