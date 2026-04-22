---
title: Docker Compose 基础入门
author: EASYZOOM
date: 2026/04/25 10:00
categories:
 - Docker的开发与实践
tags:
 - Docker
 - Compose
 - 容器编排
---

# Docker Compose 基础入门

## 前言

**C：** 单个 `docker run` 命令启动一个容器没什么问题。但当你的应用需要 Web 服务 + 数据库 + 缓存 + 消息队列等多个容器协作时，每次手动输入一大串参数既容易出错也不方便复用。Docker Compose 就是用 YAML 文件定义和管理多容器应用的工具——一条命令启动整套环境。本篇从基础语法讲起，带你写出一个完整的 Compose 配置。

<!-- more -->

## Compose vs 手动 docker run

```bash
# 手动方式：每次都要输入完整参数
docker run -d --name web -p 3000:3000 --network mynet myapp:1.0
docker run -d --name db -p 5432:5432 -e POSTGRES_PASSWORD=secret postgres:15
docker run -d --name redis -p 6379:6379 redis:7-alpine

# Compose 方式：一条命令搞定全部
docker compose up -d
```

## 基本结构

一个典型的 `docker-compose.yml`：

```yaml
services:
  web:
    image: nginx:alpine
    ports:
      - "80:80"
    restart: always

  db:
    image: postgres:15
    environment:
      POSTGRES_PASSWORD: secret
    volumes:
      - pgdata:/var/lib/postgresql/data

volumes:
  pgdata:
```

## 服务配置详解

### image —— 镜像

```yaml
services:
  app:
    image: myapp:1.0              # 从仓库拉取
    # image: localhost:5000/myapp  # 私有仓库
```

### build —— 从 Dockerfile 构建

```yaml
services:
  app:
    build:
      context: .                  # 构建上下文路径
      dockerfile: Dockerfile.dev  # Dockerfile 文件名
      args:                       # 构建参数
        APP_ENV: development
    image: myapp:dev              # 构建后的镜像名（可选）
```

### ports —— 端口映射

```yaml
services:
  web:
    ports:
      - "80:80"           # 宿主机:容器
      - "443:443"
      - "8080-8090:8080-8090"  # 端口范围映射

  db:
    ports:
      - "127.0.0.1:5432:5432"   # 只绑定 localhost
```

### environment —— 环境变量

```yaml
services:
  db:
    environment:
      POSTGRES_USER: admin
      POSTGRES_PASSWORD: secret      # 直接写
      POSTGRES_DB: mydb

  # 方式2：从 .env 文件读取
  app:
    env_file:
      - .env
      - .env.local
```

`.env` 文件：

```env
APP_ENV=development
DB_HOST=db
DB_PASSWORD=secret
REDIS_URL=redis://redis:6379
```

在 Compose 中引用环境变量：

```yaml
services:
  app:
    image: myapp:${APP_VERSION:-latest}
    environment:
      - DB_PASSWORD=${DB_PASSWORD}
```

### volumes —— 数据卷

```yaml
services:
  db:
    volumes:
      # 命名卷（推荐，Docker 管理）
      - pgdata:/var/lib/postgresql/data
      # 绑定挂载（指定宿主机路径）
      - ./init.sql:/docker-entrypoint-initdb.d/init.sql
      # 只读挂载
      - ./config:/etc/app:ro

volumes:
  pgdata:   # 声明命名卷
```

| 类型 | 语法 | 数据存储位置 | 适用场景 |
| --- | --- | --- | --- |
| 命名卷 | `pgdata:/data` | `/var/lib/docker/volumes/` | 持久化数据 |
| 绑定挂载 | `./src:/app` | 宿主机指定路径 | 开发时同步代码 |
| 匿名卷 | `/data` | Docker 自动管理 | 临时数据 |

### depends_on —— 依赖关系

```yaml
services:
  web:
    depends_on:
      - db
      - redis
  db:
    image: postgres:15
  redis:
    image: redis:7-alpine
```

```yaml
# 高级：等待依赖就绪（Compose v2.20+）
services:
  web:
    depends_on:
      db:
        condition: service_healthy    # 等待健康检查通过
      redis:
        condition: service_started    # 等待启动即可
```

### restart —— 重启策略

```yaml
services:
  web:
    restart: always          # 总是重启
    # restart: unless-stopped # 除非手动停止
    # restart: on-failure     # 只在失败时重启
    # restart: no             # 不重启（默认）
```

### networks —— 网络

```yaml
services:
  web:
    networks:
      - frontend
      - backend
  db:
    networks:
      - backend

networks:
  frontend:
  backend:
    driver: bridge
```

### command —— 覆盖启动命令

```yaml
services:
  web:
    image: nginx
    command: ["nginx", "-g", "daemon off;"]
    # command: nginx -g 'daemon off;'
```

### healthcheck —— 健康检查

```yaml
services:
  db:
    image: postgres:15
    healthcheck:
      test: ["CMD-SHELL", "pg_isready -U postgres"]
      interval: 5s
      timeout: 3s
      retries: 5
      start_period: 10s
```

## 常用命令

```bash
# 启动（后台）
docker compose up -d

# 启动并强制重建
docker compose up -d --build

# 查看状态
docker compose ps

# 查看日志
docker compose logs -f web
docker compose logs --tail=100

# 停止
docker compose stop

# 停止并删除容器、网络
docker compose down

# 停止并删除容器、网络、卷
docker compose down -v

# 重启某个服务
docker compose restart web

# 查看配置（验证 YAML 是否正确）
docker compose config

# 扩缩容
docker compose up -d --scale worker=3
```

## 完整示例：Web 应用 + 数据库 + 缓存

```yaml
version: "3.8"

services:
  web:
    build: .
    ports:
      - "3000:3000"
    environment:
      - DATABASE_URL=postgresql://admin:secret@db:5432/mydb
      - REDIS_URL=redis://redis:6379
    depends_on:
      db:
        condition: service_healthy
      redis:
        condition: service_started
    restart: unless-stopped
    networks:
      - appnet

  db:
    image: postgres:15-alpine
    environment:
      POSTGRES_USER: admin
      POSTGRES_PASSWORD: secret
      POSTGRES_DB: mydb
    volumes:
      - pgdata:/var/lib/postgresql/data
      - ./init.sql:/docker-entrypoint-initdb.d/init.sql:ro
    healthcheck:
      test: ["CMD-SHELL", "pg_isready -U admin"]
      interval: 5s
      timeout: 3s
      retries: 5
    restart: unless-stopped
    networks:
      - appnet

  redis:
    image: redis:7-alpine
    volumes:
      - redisdata:/data
    restart: unless-stopped
    networks:
      - appnet

  adminer:
    image: adminer
    ports:
      - "8080:8080"
    depends_on:
      - db
    networks:
      - appnet

volumes:
  pgdata:
  redisdata:

networks:
  appnet:
    driver: bridge
```

## Compose 文件版本

| Compose 版本 | Docker 版本 | 说明 |
| --- | --- | --- |
| v2 | Docker Engine 1.10+ | 旧格式 |
| v3.x | Docker Engine 1.13+ | Swarm 兼容 |
| 无版本号 | Docker Compose v2+ | **推荐**，自动使用最新格式 |

::: tip 笔者说
Docker Compose v2 使用 `docker compose`（没有连字符），v1 使用 `docker-compose`（有连字符）。新版本建议使用 `docker compose`。如果没有版本号字段，Compose 会使用最新的特性。
:::

## 常见问题

### 容器间通信失败

```bash
# 检查容器是否在同一网络
docker network ls
docker network inspect <网络名>

# 容器间使用服务名作为主机名
# 正确：postgres://db:5432/mydb
# 错误：postgres://localhost:5432/mydb
```

### depends_on 不等于"就绪"

`depends_on` 只保证容器启动，不保证服务就绪（如数据库可能还在初始化）。使用 `condition: service_healthy` 或在应用中加重试逻辑。

### 环境变量未生效

```bash
# 调试：查看实际配置
docker compose config

# 确认 .env 文件位置正确（与 docker-compose.yml 同目录）
```

## 小结

Docker Compose 核心要点：

1. **一个 YAML 文件**定义多个服务的完整配置
2. **一条命令** `docker compose up -d` 启动整套环境
3. **服务间通信**使用服务名作为主机名
4. **数据持久化**使用命名卷或绑定挂载
5. **依赖管理** `depends_on` + `condition: service_healthy`
6. **环境变量** `environment` 或 `.env` 文件
