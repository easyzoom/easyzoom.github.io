[English](./README.en.md) | 中文

# EASYZOOM的知识库

<a href="http://creativecommons.org/licenses/by-sa/4.0/" target="_blank">
    <img alt="文章 License: CC 4.0 BY-SA" src="https://img.shields.io/badge/文章%20License-CC%204.0%20BY--SA-blue.svg">
</a>
<a href="./LICENSE" target="_blank">
    <img alt="源码 License: MIT" src="https://img.shields.io/badge/源码%20License-MIT-green.svg">
</a>
<img alt="VitePress 1.6.x" src="https://img.shields.io/badge/VitePress-1.6.x-42b983.svg">
<img alt="Vue 3.5.x" src="https://img.shields.io/badge/Vue-3.5.x-35495e.svg">

📝 **EASYZOOM 的个人技术知识库，记录 & 分享个人碎片化、结构化、体系化的技术知识内容。**

🎯 专注 · 🔍 洞察 · 🤝 分享 —— 以 **嵌入式 & Linux 驱动** 为主线，兼顾编程语言、RTOS、板卡实践、DevOps、AI 等方向。

🐢 在线阅读：[https://easyzoom.github.io](https://easyzoom.github.io)

## 内容板块

| 板块 | 说明 |
| --- | --- |
| Linux & 嵌入式 | Linux 基础、驱动开发、内核开发、U-Boot、MCU、FPGA |
| 编程语言 | C / C++ / Python / Rust / Java / Shell / 数据库 |
| RTOS & 机器人 | uC/OS、FreeRTOS、Zephyr、ROS2 |
| 板卡 & 工具 | NXP / RK / NVIDIA 板卡实践，Docker、Git、开源库、MySQL |
| AI | AI 基础与原理、智能体工具、Vibe Coding |
| 随笔 | [方案春秋志](./docs/categories/solutions/index.md) · [工具四海谈](./docs/categories/tools/index.md) |

## 目录结构

```text
docs/
├── .vitepress/            # VitePress 配置、主题、组件
│   ├── config/            # 导航、侧边栏、Markdown、主题等配置
│   └── theme/             # 自定义主题、组件、样式
├── about/                 # 关于知识库 / 关于我
├── categories/            # 随笔分类（按 YYYY年/MM月/DD日 组织）
│   ├── solutions/         # 方案春秋志
│   └── tools/             # 工具四海谈
├── courses/               # 各主题小册（按 序号-分组/序号-xxx.md 组织）
├── public/                # 静态资源（图片等）
├── archives.md            # 归档
├── tags.md                # 标签
└── index.md               # 首页
```

## 本地运行

```bash
# 1. 克隆本仓库
git clone git@github.com:easyzoom/easyzoom.github.io.git
cd easyzoom.github.io

# 2. 安装 PNPM（若已安装可跳过）
npm install pnpm -g

# 3. 设置镜像源（可选，国内建议）
pnpm config set registry https://registry.npmmirror.com/

# 4. 安装依赖
pnpm install

# 5. 启动开发服务，访问 http://localhost:5173
pnpm dev

# 6. 生产构建，产物位于 docs/.vitepress/dist
pnpm build

# 7. 本地预览生产构建
pnpm preview
```

## 部署

本仓库采用 **GitHub Actions + GitHub Pages 官方部署方式**自动发布，工作流配置见 [`.github/workflows/deploy-pages.yml`](./.github/workflows/deploy-pages.yml)：

- 每次推送到 `main` 分支，会自动触发构建并发布到 GitHub Pages。
- 向 `main` 发起 Pull Request 时，只跑构建校验，不会部署，避免未合并代码影响线上。
- 也可在 Actions 页面手动触发（`workflow_dispatch`）。

> 首次启用时，请在仓库的 **Settings → Pages → Build and deployment → Source** 里选择 **GitHub Actions**（不再使用独立的 `pages` 分支）。

如果你 Fork 后希望部署到其他平台，也可以选择：

- Vercel / Netlify / Cloudflare Pages
- 个人虚拟主机、个人服务器（上传 `docs/.vitepress/dist` 即可）

## 如何投稿一篇随笔

随笔分两类，都由 [`docs/.vitepress/config/sidebar.ts`](./docs/.vitepress/config/sidebar.ts) 自动扫描生成侧边栏：

- **方案春秋志**：`docs/categories/solutions/YYYY年/MM月/DD日/xxx.md`
- **工具四海谈**：`docs/categories/tools/YYYY年/MM月/DD日/xxx.md`

每篇文章需要一段 Frontmatter，例如：

```md
---
title: 文章标题
author: EASYZOOM
date: 2026/04/20 16:30
categories:
 - 方案春秋志
tags:
 - Shell
 - Linux
---
```

课程类小册则放在 `docs/courses/<topic>/<序号-分组>/<序号-xxx>.md` 目录下，约定同样写在 `sidebar.ts` 里。

## License

- 文章遵循 [CC 4.0 BY-SA](http://creativecommons.org/licenses/by-sa/4.0/) 版权协议，转载请附上原文出处链接和声明
- 源码遵循 [MIT](./LICENSE) 许可协议
- Copyright © 2024-2026 EASYZOOM
