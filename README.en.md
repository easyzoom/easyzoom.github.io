English | [中文](./README.md)

# EASYZOOM's Knowledge Base

<a href="http://creativecommons.org/licenses/by-sa/4.0/" target="_blank">
    <img alt="Post License: CC 4.0 BY-SA" src="https://img.shields.io/badge/Post%20License-CC%204.0%20BY--SA-blue.svg">
</a>
<a href="./LICENSE" target="_blank">
    <img alt="Code License: MIT" src="https://img.shields.io/badge/Code%20License-MIT-green.svg">
</a>
<img alt="VitePress 1.6.x" src="https://img.shields.io/badge/VitePress-1.6.x-42b983.svg">
<img alt="Vue 3.5.x" src="https://img.shields.io/badge/Vue-3.5.x-35495e.svg">

📝 **EASYZOOM's personal technology knowledge base — a place to record & share fragmented, structured and systematic technical notes.**

🎯 Focus · 🔍 Insight · 🤝 Share — centered on **Embedded Systems & Linux Drivers**, also covering programming languages, RTOS, board bring-up, DevOps and AI.

🐢 Live site: [https://easyzoom.github.io](https://easyzoom.github.io)

## Sections

| Section | Description |
| --- | --- |
| Linux & Embedded | Linux basics, driver development, kernel, U-Boot, MCU, FPGA |
| Languages | C / C++ / Python / Rust / Java / Shell / Database |
| RTOS & Robotics | uC/OS, FreeRTOS, Zephyr, ROS2 |
| Boards & Tools | NXP / RK / NVIDIA board practice; Docker, Git, OSS libs, MySQL |
| AI | Fundamentals, agent tools, Vibe Coding |
| Essays | [Solutions Chronicles](./docs/categories/solutions/index.md) · [Tools Talks](./docs/categories/tools/index.md) |

## Repository Layout

```text
docs/
├── .vitepress/            # VitePress config, theme and components
│   ├── config/            # Nav, sidebar, markdown, theme, etc.
│   └── theme/             # Custom theme, components and styles
├── about/                 # About the site / about the author
├── categories/            # Essays (organized as YYYY年/MM月/DD日)
│   ├── solutions/         # Solutions Chronicles
│   └── tools/             # Tools Talks
├── courses/               # Topic books (organized as NN-group/NN-xxx.md)
├── public/                # Static assets (images, etc.)
├── archives.md            # Archive page
├── tags.md                # Tag page
└── index.md               # Home page
```

## Run Locally

```bash
# 1. Clone the repo
git clone git@github.com:easyzoom/easyzoom.github.io.git
cd easyzoom.github.io

# 2. Install PNPM (skip if already installed)
npm install pnpm -g

# 3. (Optional) Use a faster registry mirror
pnpm config set registry https://registry.npmmirror.com/

# 4. Install dependencies
pnpm install

# 5. Start the dev server at http://localhost:5173
pnpm dev

# 6. Build for production (output: docs/.vitepress/dist)
pnpm build

# 7. Preview the production build locally
pnpm preview
```

## Deployment

This site is published via the **official GitHub Pages deployment flow** powered by GitHub Actions. See [`.github/workflows/deploy-pages.yml`](./.github/workflows/deploy-pages.yml):

- Every push to `main` triggers a build and publishes to GitHub Pages.
- Pull requests against `main` only run the build as a validation step and do not deploy.
- You can also trigger the workflow manually from the Actions tab (`workflow_dispatch`).

> On first setup, go to **Settings → Pages → Build and deployment → Source** and choose **GitHub Actions** (a dedicated `pages` branch is no longer needed).

When forking, you can also deploy to other platforms:

- Vercel / Netlify / Cloudflare Pages
- Any static host or personal server (just upload `docs/.vitepress/dist`)

## Contributing an Essay

Essays live under two categories and are auto-discovered by [`docs/.vitepress/config/sidebar.ts`](./docs/.vitepress/config/sidebar.ts):

- **Solutions Chronicles**: `docs/categories/solutions/YYYY年/MM月/DD日/xxx.md`
- **Tools Talks**: `docs/categories/tools/YYYY年/MM月/DD日/xxx.md`

Each article needs a small Frontmatter block, e.g.:

```md
---
title: Your Article Title
author: EASYZOOM
date: 2026/04/20 16:30
categories:
 - Solutions Chronicles
tags:
 - Shell
 - Linux
---
```

Topic books instead live at `docs/courses/<topic>/<NN-group>/<NN-xxx>.md`, following the same rules defined in `sidebar.ts`.

## License

- Articles are licensed under [CC 4.0 BY-SA](http://creativecommons.org/licenses/by-sa/4.0/). When reproducing, please include a link to the original and preserve the notice.
- Source code is licensed under [MIT](./LICENSE).
- Copyright © 2024-2026 EASYZOOM
