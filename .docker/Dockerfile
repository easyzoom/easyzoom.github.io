# Node 版本与 CI（deploy-pages.yml）保持一致
FROM node:20-bookworm-slim

WORKDIR /app

# VitePress 构建会调用 git（如 lastUpdated / Git 信息）；slim 镜像默认无 git
RUN apt-get update \
  && apt-get install -y --no-install-recommends git \
  && rm -rf /var/lib/apt/lists/* \
  && git --version

RUN corepack enable && corepack prepare pnpm@latest --activate

# 构建层缓存依赖；本地 compose 挂载源码后仍会在启动时再 install 一次
COPY package.json pnpm-lock.yaml ./
RUN pnpm install --frozen-lockfile

EXPOSE 5173

# 挂载卷会覆盖镜像内 /app，故启动时再 install；需监听 0.0.0.0 才能在宿主机访问
CMD ["sh", "-c", "pnpm install --frozen-lockfile && pnpm exec vitepress dev docs --host 0.0.0.0 --port 5173"]
