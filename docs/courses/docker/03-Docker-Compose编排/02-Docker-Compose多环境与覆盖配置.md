---
title: Docker Compose 多环境与覆盖配置
author: EASYZOOM
date: 2026/04/25 10:00
categories:
 - Docker的开发与实践
tags:
 - Docker
 - Compose
 - 多环境
---

# Docker Compose 多环境与覆盖配置

## 前言

**C：** 一个实际项目至少有开发、测试、生产三个环境，它们的数据库密码、端口映射、日志级别、副本数量都不一样。如果每种环境都维护一份完整的 `docker-compose.yml`，大量重复配置会导致维护噩梦。Docker Compose 的 `extends` 和多文件合并机制就是解决这个问题的。本篇讲解如何用一套基础配置 + 多个覆盖文件来优雅地管理多环境。

<!-- more -->

## 问题场景

| 配置项 | 开发环境 | 生产环境 |
| --- | --- | --- |
| 数据库密码 | `dev123` | 生产密钥 |
| 端口映射 | `3000:3000` | `80:3000` |
| 日志级别 | `DEBUG` | `WARN` |
| 副本数 | 1 | 3 |
| 挂载路径 | `./src:/app` | 无 |
| 资源限制 | 无 | CPU 2核、内存 4GB |

## 方案1：多 Compose 文件合并

### 文件结构

```text
project/
├── docker-compose.yml          # 基础配置（共享）
├── docker-compose.dev.yml      # 开发环境覆盖
├── docker-compose.prod.yml     # 生产环境覆盖
├── .env                        # 默认环境变量
├── .env.dev                    # 开发环境变量
└── .env.prod                   # 生产环境变量
```

### 基础配置

```yaml
# docker-compose.yml（所有环境共享的配置）
services:
  web:
    build: .
    restart: unless-stopped
    depends_on:
      db:
        condition: service_healthy
    networks:
      - appnet

  db:
    image: postgres:15-alpine
    environment:
      POSTGRES_USER: ${DB_USER:-admin}
      POSTGRES_PASSWORD: ${DB_PASSWORD}
      POSTGRES_DB: ${DB_NAME:-mydb}
    volumes:
      - pgdata:/var/lib/postgresql/data
    healthcheck:
      test: ["CMD-SHELL", "pg_isready -U ${DB_USER:-admin}"]
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

volumes:
  pgdata:
  redisdata:

networks:
  appnet:
    driver: bridge
```

### 开发环境覆盖

```yaml
# docker-compose.dev.yml
services:
  web:
    ports:
      - "3000:3000"
    volumes:
      - ./src:/app/src          # 挂载源码，实时同步
    environment:
      - NODE_ENV=development
      - LOG_LEVEL=DEBUG

  db:
    ports:
      - "5432:5432"             # 暴露端口方便本地工具连接
    environment:
      POSTGRES_PASSWORD: dev123
```

### 生产环境覆盖

```yaml
# docker-compose.prod.yml
services:
  web:
    ports:
      - "80:3000"
    environment:
      - NODE_ENV=production
      - LOG_LEVEL=WARN
    deploy:
      replicas: 3
      resources:
        limits:
          cpus: "2.0"
          memory: 4G
        reservations:
          cpus: "1.0"
          memory: 2G

  db:
    environment:
      POSTGRES_PASSWORD: ${DB_PASSWORD}   # 从 .env.prod 读取
```

### 启动命令

```bash
# 开发环境
docker compose -f docker-compose.yml -f docker-compose.dev.yml up -d

# 生产环境
docker compose -f docker-compose.yml -f docker-compose.prod.yml up -d
```

## 合并规则

当多个 Compose 文件合并时，遵循以下规则：

| 字段 | 合并行为 | 示例 |
| --- | --- | --- |
| `image` | 覆盖 | prod 中的 image 替换 base 的 |
| `ports` | 追加 | base 的端口 + prod 的端口 |
| `environment` | 追加 | base 的变量 + prod 的变量，同名 prod 覆盖 |
| `volumes` | 追加 | base 的卷 + prod 的卷 |
| `depends_on` | 追加 | base 的依赖 + prod 的依赖 |
| `command` | 覆盖 | prod 的 command 替换 base 的 |
| `restart` | 覆盖 | prod 的 restart 替换 base 的 |
| `networks` | 追加 | base 的网络 + prod 的网络 |

## 方案2：extends 继承

```yaml
# docker-compose.yml
services:
  web-base:                     # 基础模板（加 - 后缀表示不直接运行）
    build: .
    restart: unless-stopped
    depends_on:
      - db
    networks:
      - appnet

  web-dev:
    extends:
      service: web-base
    ports:
      - "3000:3000"
    volumes:
      - ./src:/app/src

  web-prod:
    extends:
      service: web-base
    ports:
      - "80:3000"
    deploy:
      replicas: 3
```

::: tip 笔者说
`extends` 适合简单的继承场景，复杂的多环境配置推荐用多文件合并（方案1），更灵活。
:::

## 环境变量管理

### .env 文件优先级

```bash
# 优先级从高到低：
# 1. shell 环境变量（docker compose run -e）
# 2. docker compose up --env-file 指定的文件
# 3. 当前目录的 .env 文件
# 4. docker-compose.yml 中的默认值 ${VAR:-default}
```

### 多环境 .env

```bash
# 开发
cp .env.dev .env
docker compose up -d

# 生产
cp .env.prod .env
docker compose -f docker-compose.yml -f docker-compose.prod.yml up -d
```

或者使用 `--env-file` 指定：

```bash
docker compose --env-file .env.dev up -d
docker compose --env-file .env.prod -f docker-compose.yml -f docker-compose.prod.yml up -d
```

## 方案3：使用 profiles（Compose 规范）

```yaml
# docker-compose.yml
services:
  web:
    build: .
    profiles: ["prod"]           # 只在 prod profile 中运行

  db:
    image: postgres:15
    profiles: ["prod"]           # 生产用独立数据库

  dev-db:
    image: postgres:15           # 开发用 SQLite，可选 Postgres
    profiles: ["dev"]

  redis:
    image: redis:7               # 始终运行
```

```bash
# 默认启动（不含任何 profile）
docker compose up -d             # 只启动 redis

# 启动开发 profile
docker compose --profile dev up -d   # 启动 redis + dev-db

# 启动生产 profile
docker compose --profile prod up -d  # 启动 web + db + redis
```

## 生产部署建议

### 资源限制

```yaml
services:
  web:
    deploy:
      resources:
        limits:
          cpus: "2.0"
          memory: 4G
        reservations:
          memory: 2G
```

### 日志配置

```yaml
services:
  web:
    logging:
      driver: json-file
      options:
        max-size: "50m"
        max-file: "5"
```

### 健康检查

```yaml
services:
  web:
    healthcheck:
      test: ["CMD", "curl", "-f", "http://localhost:3000/health"]
      interval: 30s
      timeout: 5s
      retries: 3
      start_period: 10s
```

### 只读根文件系统

```yaml
services:
  web:
    read_only: true
    tmpfs:
      - /tmp
      - /app/uploads
```

## 常见问题

### 环境变量在 docker-compose.yml 中不生效

```bash
# 调试：查看合并后的配置
docker compose -f docker-compose.yml -f docker-compose.prod.yml config

# 确认 .env 文件路径正确
# 默认在与 docker-compose.yml 同目录查找
```

### 多文件合并后 ports 冲突

检查是否有多个覆盖文件定义了同一个端口映射。使用 `docker compose config` 查看最终配置。

### 变量未定义导致报错

```yaml
# 使用默认值防止报错
POSTGRES_PASSWORD: ${DB_PASSWORD:?请设置 DB_PASSWORD 环境变量}
```

## 小结

多环境配置要点：

1. **多文件合并**：`-f base.yml -f override.yml` 最灵活
2. **合并规则**：列表字段追加，标量字段覆盖
3. **环境变量**：`.env` 文件 + `${VAR:-default}` 默认值
4. **profiles**：按需启动不同服务组合
5. **验证配置**：`docker compose config` 查看最终合并结果
