---
title: Git 诊断与性能优化
author: EASYZOOM
date: 2026/04/24 19:00
categories:
 - Git入门与实践
tags:
 - Git
 - 性能优化
 - gc
 - fsck
---

# Git 诊断与性能优化

## 前言

**C：** 用久了你会发现 Git 越来越慢？仓库体积越来越大？`.git` 目录占用好几个 GB？本文介绍 Git 的诊断工具和优化方法，帮你保持仓库的健康和高效。

<!-- more -->

## 仓库健康检查

### git fsck

`fsck`（file system check）检查 Git 对象库的完整性和连通性：

```shell
# 基本检查
git fsck

# 检查所有对象
git fsck --full

# 检查 dangling 对象（未被引用的对象）
git fsck --dangling

# 只检查 dangling commit
git fsck --dangling 2>&1 | grep "dangling commit"

# 只检查 dangling blob
git fsck --dangling 2>&1 | grep "dangling blob"

# 输出示例：
# dangling commit a1b2c3d4e5f6g7h8i9j0k1l2m3n4o5p6
# dangling blob e5f6g7h8i9j0k1l2m3n4o5p6q7r8s9t0
```

::: tip 笔者说
`dangling` 对象不一定是问题。它们可能是你刚 add 但还没 commit 的文件，或者是被 `git stash drop` 的 stash。只有在你确定不需要时才清理它们。
:::

### 检查仓库统计信息

```shell
# 查看仓库中对象数量
git count-objects -v
# count: 1250                  # 松散对象数量
# size: 15.52 MiB              # 松散对象大小
# in-pack: 3200                # 打包对象数量
# packs: 3                     # 打包文件数量
# size-pack: 45.23 MiB         # 打包文件大小
# prune-packable: 0            # 可被清理的松散对象
# garbage: 0                   # 垃圾文件数量
# size-garbage: 0 bytes        # 垃圾文件大小

# 查看仓库总大小
du -sh .git
du -sh .git/objects
```

## 垃圾回收与优化

### git gc

```shell
# 执行垃圾回收
git gc

# 激进式 gc（压缩率更高，但耗时更长）
git gc --aggressive

# gc 后再次检查
git count-objects -v
```

`git gc` 会做以下事情：
1. 将松散对象打包为 `.pack` 文件
2. 删除过期的 reflog 条目
3. 删除无法访问的对象
4. 压缩 pack 文件

### git prune

```shell
# 清理无法访问的松散对象（超过 2 周的）
git prune

# 立即清理所有无法访问的松散对象（危险！）
git prune --expire=now
```

::: warning 注意
`git prune --expire=now` 会删除所有无法访问的松散对象，包括 reflog 中可以恢复的提交。除非你确定不需要恢复任何东西，否则不要使用。
:::

### reflog 清理

```shell
# 查看过期设置
git config --get gc.reflogExpire
# 90.days

git config --get gc.reflogExpireUnreachable
# 30.days

# 清理过期的 reflog 条目
git reflog expire --expire=30.days --all

# 清理所有 reflog（危险！）
git reflog expire --expire=now --all
```

## 仓库瘦身

### 查找大文件

```shell
# 查看历史中所有文件的大小
git rev-list --objects --all | \
  git cat-file --batch-check='%(objecttype) %(objectname) %(objectsize) %(rest)' | \
  awk '/^blob/ {print $3, $4}' | \
  sort --numeric-sort --key=1 --reverse | \
  head -20

# 查看当前提交中最大的文件
git ls-tree -r -l HEAD | sort -k 4 -n -r | head -20
```

### 从历史中删除大文件

```shell
# 方法一：使用 git filter-repo（推荐）
pip install git-filter-repo

# 删除特定文件
git filter-repo --path large-file.zip --invert-paths

# 删除超过 10MB 的文件
git filter-repo --strip-blobs-bigger-than 10M

# 方法二：使用 BFG Repo-Cleaner（更快）
# 下载 bfg.jar
java -jar bfg.jar --strip-blobs-bigger-than 10M repo.git
```

::: warning 严重警告
从历史中删除文件会改写所有相关的提交哈希。操作后需要 `git push --force --all`，且必须通知所有团队成员重新 clone。
:::

### 清理后优化

```shell
# 删除大文件后执行完整优化
git reflog expire --expire=now --all
git gc --prune=now --aggressive
git repack -a -d -f --depth=250 --window=250
```

## 性能优化

### 加速 git status

```shell
# 使用 untracked cache（Git 2.0+）
git config core.untrackedCache true

# 使用 fsmonitor（文件系统监控）
git config core.fsmonitor true

# 或使用 watchman
git config core.fsmonitor .git/hooks/fsmonitor-watchman
```

### 加速 git log

```shell
# 使用 commit graph 加速遍历
git commit-graph write --reachable

# 启用 commit graph
git config core.commitGraph true
```

### 并行操作

```shell
# 启用多线程 pack 操作
git config pack.threads 4

# 或根据 CPU 核心数设置
git config pack.threads 0  # 自动检测

# 设置 pack 压缩级别（0-9，默认 -1）
git config pack.compression 9  # 最高压缩，最慢
git config pack.compression 1  # 最低压缩，最快
```

### 浅克隆（节省空间和时间）

```shell
# 只克隆最近一次提交（无历史）
git clone --depth=1 https://github.com/user/repo.git

# 克隆最近 10 次提交
git clone --depth=10 https://github.com/user/repo.git

# 后续可以加深历史
git fetch --deepen=10

# 取消浅克隆，获取完整历史
git fetch --unshallow

# 只克隆单个分支
git clone --single-branch --branch main https://github.com/user/repo.git

# 只克隆特定目录（稀疏检出）
git clone --filter=blob:none --sparse https://github.com/user/repo.git
cd repo
git sparse-checkout set src/components
```

## 大文件管理：Git LFS

### 安装和初始化

```shell
# 安装 Git LFS
# macOS
brew install git-lfs
# Ubuntu
apt install git-lfs
# Windows
# 下载安装包：https://git-lfs.github.com/

# 初始化
git lfs install
```

### 跟踪大文件

```shell
# 跟踪特定类型
git lfs track "*.psd"
git lfs track "*.zip"
git lfs track "*.mp4"

# 这会生成/更新 .gitattributes
# *.psd filter=lfs diff=lfs merge=lfs -text
# *.zip filter=lfs diff=lfs merge=lfs -text

# 提交 .gitattributes
git add .gitattributes
git commit -m "chore: configure Git LFS"
```

### 常用命令

```shell
# 查看被 LFS 跟踪的文件
git lfs track

# 查看所有 LFS 文件
git lfs ls-files

# 迁移已有的大文件到 LFS
git lfs migrate import --include="*.psd,*.zip" --everything

# 检查 LFS 状态
git lfs status

# 拉取 LFS 文件
git lfs pull

# 跳过 LFS 文件的下载（节省时间）
GIT_LFS_SKIP_SMUDGE=1 git clone https://github.com/user/repo.git
```

## 诊断常见问题

### 仓库很大但实际文件不多

```shell
# 可能原因：大文件残留在历史中
git rev-list --objects --all | \
  git cat-file --batch-check='%(objecttype) %(objectsize)' | \
  awk '/^blob/{sum+=$2} END {print "Total blob size:", sum/1024/1024, "MB"}'

# 可能原因：大量 reflog 或 dangling 对象
git reflog | wc -l
git fsck --dangling 2>&1 | wc -l

# 解决方案：gc
git gc --aggressive
```

### git status 很慢

```shell
# 可能原因：仓库中有大量文件或子目录
# 解决方案：
git config core.untrackedCache true
git config core.fsmonitor true

# 可能原因：.gitignore 规则过于复杂
git check-ignore -v some-file
# 优化 .gitignore 规则
```

### clone 很慢

```shell
# 使用浅克隆
git clone --depth=1 --single-branch https://github.com/user/repo.git

# 使用协议镜像
# 国内加速
git clone https://gitee.com/mirrors/repo.git

# 使用 SSH 替代 HTTPS（通常更快）
git clone git@github.com:user/repo.git
```

### push 被拒绝（文件过大）

```shell
# GitHub 限制单个文件不超过 100MB
# 错误信息：remote: error: File xxx is 156.23 MB; this exceeds GitHub's file size limit of 100.00 MB

# 解决方案一：使用 Git LFS
git lfs track "*.bin"
git add .gitattributes
git commit -m "use LFS for binary files"

# 解决方案二：从历史中删除大文件
git filter-repo --strip-blobs-bigger-than 100M
```

## 日常维护脚本

```shell
#!/bin/bash
# git-maintenance.sh — 定期运行的仓库维护脚本

echo "=== Git 仓库维护 ==="

# 1. 清理过期的 reflog（保留 30 天）
echo "清理 reflog..."
git reflog expire --expire=30.days --all

# 2. 垃圾回收
echo "执行 gc..."
git gc --auto

# 3. 删除无用的远程跟踪分支
echo "清理远程跟踪分支..."
git fetch --prune

# 4. 检查仓库健康
echo "检查仓库健康..."
git fsck --no-dangling

# 5. 显示仓库统计
echo ""
echo "仓库统计："
git count-objects -v
du -sh .git
```

::: tip 笔者说
大多数情况下 `git gc --auto` 就够了——它会在松散对象超过一定数量时自动触发 gc，不需要你手动干预。
:::

## 小结

| 工具 | 用途 | 频率 |
|------|------|------|
| `git fsck` | 检查对象完整性 | 发现异常时 |
| `git gc` | 垃圾回收和压缩 | 定期或自动 |
| `git prune` | 清理无法访问的对象 | 配合 gc 使用 |
| `git count-objects` | 查看仓库统计 | 诊断问题时 |
| `git filter-repo` | 从历史中删除文件 | 仓库瘦身时 |
| `git lfs` | 管理大文件 | 项目初始化时 |
| `git commit-graph` | 加速日志遍历 | 大型仓库 |

- 大多数仓库 `git gc --auto` 就足够了
- 仓库体积异常时用 `git count-objects -v` 诊断
- 使用 Git LFS 管理二进制大文件
- 浅克隆和单分支克隆可以加速 clone
- 删除历史中的大文件需要 force push 和团队协调

---

到这里，Git 入门与实践系列的 20 篇文章全部完成！从日常命令到内部原理，从个人使用到团队协作，希望能帮助你全面提升 Git 技能。
