---
title: Docker 生产环境部署指南
author: EASYZOOM
date: 2026/04/25 10:00
categories:
 - Docker的开发与实践
tags:
 - Docker
 - 生产部署
 - 最佳实践
---

# Docker 生产环境部署指南

## 前言

**C：** 开发环境 `docker compose up -d` 就完事了，但生产环境远比这复杂——日志管理、健康检查、滚动更新、故障恢复、监控告警，这些都是必须考虑的。本篇系统梳理 Docker 生产部署的各个环节，帮你从"能跑"到"可靠运行"。

<!-- more -->

## 生产部署清单

```mermaid
flowchart TD
    A[镜像] --> B[多阶段构建]
    A --> C[版本锁定]
    A --> D[安全扫描]
    E[部署] --> F[健康检查]
    E --> G[资源限制]
    E --> H[日志管理]
    E --> I[数据持久化]
    J[运维] --> K[监控告警]
    J --> L[滚动更新]
    J --> M[备份恢复]
    J --> N[故障排查]
```

## 一、镜像准备

### 多阶段构建

```dockerfile
FROM node:20-alpine AS builder
WORKDIR /app
COPY package*.json ./
RUN npm ci --production
COPY . .
RUN npm run build

FROM node:20-alpine
RUN addgroup -S appgroup && adduser -S appuser -G appgroup
WORKDIR /app
COPY --from=builder --chown=appuser:appgroup /app/dist ./dist
COPY --from=builder --chown=appuser:appgroup /app/node_modules ./node_modules
COPY --from=builder --chown=appuser:appgroup /app/package.json ./
USER appuser
HEALTHCHECK --interval=30s --timeout=3s --retries=3 \
    CMD wget -qO- http://localhost:3000/health || exit 1
EXPOSE 3000
CMD ["node", "dist/server.js"]
```

### 版本策略

```bash
# 构建时打标签
TAG=$(date +%Y%m%d)-$(git rev-parse --short HEAD)
docker build -t registry.example.com/myapp:${TAG} .
docker tag registry.example.com/myapp:${TAG} registry.example.com/myapp:latest

# 推送到私有仓库
docker push registry.example.com/myapp:${TAG}
docker push registry.example.com/myapp:latest
```

## 二、Compose 生产配置

### 完整的生产 Compose 文件

```yaml
services:
  web:
    image: registry.example.com/myapp:latest
    ports:
      - "80:3000"
    environment:
      - NODE_ENV=production
      - DATABASE_URL=postgresql://app:${DB_PASSWORD}@db:5432/appdb
      - REDIS_URL=redis://redis:6379/0
    depends_on:
      db:
        condition: service_healthy
      redis:
        condition: service_started
    restart: unless-stopped
    deploy:
      replicas: 3
      resources:
        limits:
          cpus: "1.0"
          memory: 512M
        reservations:
          memory: 256M
    healthcheck:
      test: ["CMD", "wget", "-qO-", "http://localhost:3000/health"]
      interval: 30s
      timeout: 3s
      retries: 3
      start_period: 10s
    logging:
      driver: local
      options:
        max-size: "50m"
        max-file: "5"
    read_only: true
    tmpfs:
      - /tmp
      - /app/uploads
    networks:
      - frontend
      - backend
    user: appuser

  db:
    image: postgres:15-alpine
    environment:
      POSTGRES_USER: app
      POSTGRES_PASSWORD: ${DB_PASSWORD}
      POSTGRES_DB: appdb
    volumes:
      - pgdata:/var/lib/postgresql/data
    healthcheck:
      test: ["CMD-SHELL", "pg_isready -U app -d appdb"]
      interval: 5s
      timeout: 3s
      retries: 5
    restart: unless-stopped
    deploy:
      resources:
        limits:
          memory: 2G
    logging:
      driver: local
      options:
        max-size: "50m"
        max-file: "3"
    networks:
      - backend

  redis:
    image: redis:7-alpine
    volumes:
      - redisdata:/data
    restart: unless-stopped
    deploy:
      resources:
        limits:
          memory: 256M
    logging:
      driver: local
      options:
        max-size: "20m"
        max-file: "3"
    networks:
      - backend

  nginx:
    image: nginx:alpine
    ports:
      - "443:443"
    volumes:
      - ./nginx/nginx.conf:/etc/nginx/nginx.conf:ro
      - ./nginx/ssl:/etc/nginx/ssl:ro
    depends_on:
      web:
        condition: service_healthy
    restart: unless-stopped
    networks:
      - frontend

volumes:
  pgdata:
  redisdata:

networks:
  frontend:
  backend:
    internal: true
```

## 三、滚动更新

```bash
# 拉取最新镜像
docker compose pull

# 滚动重启（每次一个容器）
docker compose up -d --no-deps --build web

# 或使用 deploy replicas 实现零停机
docker compose up -d --scale web=4
docker compose up -d --no-deps --build web
docker compose up -d --scale web=3
```

## 四、健康检查

### 应用层健康检查

```python
# Flask 示例
@app.route('/health')
def health():
    try:
        db.session.execute(text('SELECT 1'))
        return {'status': 'healthy'}, 200
    except Exception:
        return {'status': 'unhealthy'}, 503
```

### Docker 层健康检查

```yaml
healthcheck:
  test: ["CMD", "curl", "-f", "http://localhost:3000/health"]
  interval: 30s
  timeout: 3s
  retries: 3
  start_period: 10s
```

### Nginx 负载均衡 + 健康检查

```nginx
upstream web {
    server web_1:3000 max_fails=3 fail_timeout=30s;
    server web_2:3000 max_fails=3 fail_timeout=30s;
    server web_3:3000 max_fails=3 fail_timeout=30s;
}

server {
    listen 443 ssl;
    server_name example.com;

    ssl_certificate /etc/nginx/ssl/cert.pem;
    ssl_certificate_key /etc/nginx/ssl/key.pem;

    location / {
        proxy_pass http://web;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_next_upstream error timeout http_503;
    }

    location /health {
        proxy_pass http://web;
    }
}
```

## 五、备份策略

### 数据库备份

```bash
# 定时备份脚本（crontab）
# 每天凌晨 2 点备份
0 2 * * * docker exec db pg_dump -U app appdb | gzip > /backup/db-$(date +\%Y\%m\%d).sql.gz
0 2 * * * find /backup -name "*.sql.gz" -mtime +30 -delete
```

### 应用数据备份

```bash
# 备份数据卷
docker run --rm \
    -v myapp_pgdata:/data \
    -v /backup:/backup \
    alpine tar czf /backup/pgdata-$(date +%Y%m%d).tar.gz /data
```

### 配置备份

```bash
# 备份 Compose 配置和应用配置
tar czf backup-config-$(date +%Y%m%d).tar.gz \
    docker-compose.yml \
    .env \
    nginx/
```

## 六、监控告警

```yaml
# docker-compose.monitoring.yml
services:
  prometheus:
    image: prom/prometheus:v2.49
    ports:
      - "9090:9090"
    volumes:
      - ./prometheus.yml:/etc/prometheus/prometheus.yml:ro
      - promdata:/prometheus

  grafana:
    image: grafana/grafana:10.3
    ports:
      - "3000:3000"
    volumes:
      - grafanadata:/var/lib/grafana

  alertmanager:
    image: prom/alertmanager:v0.27
    ports:
      - "9093:9093"
    volumes:
      - ./alertmanager.yml:/etc/alertmanager/alertmanager.yml:ro

  cadvisor:
    image: gcr.io/cadvisor/cadvisor:v0.47
    ports:
      - "8080:8080"
    volumes:
      - /:/rootfs:ro
      - /var/run:/var/run:ro
      - /sys:/sys:ro
      - /var/lib/docker:/var/lib/docker:ro

volumes:
  promdata:
  grafanadata:
```

## 七、故障排查

```bash
# 1. 检查容器状态
docker compose ps

# 2. 查看日志
docker compose logs --tail=100 web

# 3. 检查资源使用
docker stats --no-stream

# 4. 检查网络连通
docker compose exec web curl -s http://db:5432 || echo "DB unreachable"

# 5. 进入容器排查
docker compose exec web sh

# 6. 检查磁盘空间
df -h
docker system df

# 7. 检查 Docker 事件
docker events --since 1h
```

## 常见问题

### 容器频繁重启

```bash
# 查看重启原因
docker inspect <容器> | jq '.[0].State'

# 常见原因：OOM、退出码非零、健康检查失败
```

### 磁盘空间不足

```bash
# 清理
docker system prune -a --volumes

# 配置日志限制（预防）
# /etc/docker/daemon.json
{
  "log-opts": {
    "max-size": "50m",
    "max-file": "5"
  }
}
```

## 小结

生产部署要点：

1. **镜像**：多阶段构建 + 版本锁定 + 安全扫描
2. **配置**：资源限制 + 健康检查 + 日志管理 + 非 root 运行
3. **更新**：拉取新镜像 + 滚动重启
4. **备份**：数据库 + 数据卷 + 配置文件
5. **监控**：Prometheus + Grafana + 告警
6. **排查**：日志 + 资源 + 网络 + 事件
