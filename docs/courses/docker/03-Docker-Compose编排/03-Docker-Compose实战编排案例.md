---
title: Docker Compose 实战编排案例
author: EASYZOOM
date: 2026/04/25 10:00
categories:
 - Docker的开发与实践
tags:
 - Docker
 - Compose
 - 实战案例
---

# Docker Compose 实战编排案例

## 前言

**C：** 前两篇讲了 Compose 的语法和多环境配置，本篇通过三个贴近实际生产的案例来巩固：一个 Python Flask 全栈应用、一个 Next.js 前端 + Node.js 后端 + MySQL 的项目、一个 WordPress 博客站。每个案例都是可以直接 `docker compose up -d` 运行的完整配置。

<!-- more -->

## 案例一：Python Flask + PostgreSQL + Redis

### 项目结构

```text
flask-app/
├── app.py
├── requirements.txt
├── Dockerfile
├── docker-compose.yml
├── .env
└── init.sql
```

### Dockerfile

```dockerfile
FROM python:3.12-slim

WORKDIR /app

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY . .

EXPOSE 5000

CMD ["gunicorn", "-w", "4", "-b", "0.0.0.0:5000", "app:app"]
```

### docker-compose.yml

```yaml
services:
  web:
    build: .
    ports:
      - "5000:5000"
    environment:
      - FLASK_ENV=production
      - DATABASE_URL=postgresql://admin:${DB_PASSWORD}@db:5432/flaskdb
      - REDIS_URL=redis://redis:6379/0
      - SECRET_KEY=${SECRET_KEY}
    depends_on:
      db:
        condition: service_healthy
      redis:
        condition: service_started
    restart: unless-stopped
    networks:
      - appnet
    healthcheck:
      test: ["CMD", "curl", "-f", "http://localhost:5000/health"]
      interval: 30s
      timeout: 3s
      retries: 3

  db:
    image: postgres:15-alpine
    environment:
      POSTGRES_USER: admin
      POSTGRES_PASSWORD: ${DB_PASSWORD}
      POSTGRES_DB: flaskdb
    volumes:
      - pgdata:/var/lib/postgresql/data
      - ./init.sql:/docker-entrypoint-initdb.d/init.sql:ro
    healthcheck:
      test: ["CMD-SHELL", "pg_isready -U admin -d flaskdb"]
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
    image: adminer:latest
    ports:
      - "8081:8080"
    depends_on:
      - db
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

### .env

```env
DB_PASSWORD=your_secure_password_here
SECRET_KEY=your-secret-key-here
```

### init.sql

```sql
CREATE TABLE IF NOT EXISTS users (
    id SERIAL PRIMARY KEY,
    username VARCHAR(80) UNIQUE NOT NULL,
    email VARCHAR(120) UNIQUE NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

### 启动

```bash
# 构建并启动
docker compose up -d --build

# 查看状态
docker compose ps

# 查看日志
docker compose logs -f web

# 停止并清理
docker compose down -v
```

## 案例二：Next.js + Node.js API + MySQL

### 项目结构

```text
fullstack/
├── frontend/
│   ├── Dockerfile
│   └── ...
├── backend/
│   ├── Dockerfile
│   └── ...
├── docker-compose.yml
├── docker-compose.dev.yml
└── .env
```

### docker-compose.yml

```yaml
services:
  # ===== 前端 =====
  frontend:
    build:
      context: ./frontend
      dockerfile: Dockerfile
    ports:
      - "3000:3000"
    environment:
      - NEXT_PUBLIC_API_URL=http://backend:4000
    depends_on:
      - backend
    restart: unless-stopped
    networks:
      - appnet

  # ===== 后端 API =====
  backend:
    build:
      context: ./backend
      dockerfile: Dockerfile
    ports:
      - "4000:4000"
    environment:
      - DATABASE_URL=mysql://root:${MYSQL_ROOT_PASSWORD}@db:3306/appdb
      - REDIS_URL=redis://redis:6379/0
      - JWT_SECRET=${JWT_SECRET}
    depends_on:
      db:
        condition: service_healthy
      redis:
        condition: service_started
    restart: unless-stopped
    networks:
      - appnet

  # ===== 数据库 =====
  db:
    image: mysql:8.0
    environment:
      MYSQL_ROOT_PASSWORD: ${MYSQL_ROOT_PASSWORD}
      MYSQL_DATABASE: appdb
      MYSQL_USER: appuser
      MYSQL_PASSWORD: ${MYSQL_PASSWORD}
    volumes:
      - mysqldata:/var/lib/mysql
    healthcheck:
      test: ["CMD", "mysqladmin", "ping", "-h", "localhost"]
      interval: 5s
      timeout: 3s
      retries: 5
    restart: unless-stopped
    networks:
      - appnet

  # ===== 缓存 =====
  redis:
    image: redis:7-alpine
    volumes:
      - redisdata:/data
    restart: unless-stopped
    networks:
      - appnet

  # ===== 数据库管理 =====
  phpmyadmin:
    image: phpmyadmin:latest
    ports:
      - "8082:80"
    environment:
      PMA_HOST: db
      PMA_PORT: 3306
    depends_on:
      - db
    restart: unless-stopped
    networks:
      - appnet

volumes:
  mysqldata:
  redisdata:

networks:
  appnet:
    driver: bridge
```

### 开发环境覆盖

```yaml
# docker-compose.dev.yml
services:
  frontend:
    volumes:
      - ./frontend/src:/app/src      # 热更新
      - ./frontend/public:/app/public
    environment:
      - NEXT_PUBLIC_API_URL=http://localhost:4000

  backend:
    volumes:
      - ./backend/src:/app/src       # 热更新
    environment:
      - NODE_ENV=development

  db:
    ports:
      - "3306:3306"
```

### 启动

```bash
# 开发
docker compose -f docker-compose.yml -f docker-compose.dev.yml up -d --build

# 生产
docker compose up -d --build
```

## 案例三：WordPress 博客

```yaml
# docker-compose.yml
services:
  # ===== WordPress =====
  wordpress:
    image: wordpress:6.5-php8.2-apache
    ports:
      - "80:80"
    environment:
      WORDPRESS_DB_HOST: db
      WORDPRESS_DB_USER: wpuser
      WORDPRESS_DB_PASSWORD: ${WP_DB_PASSWORD}
      WORDPRESS_DB_NAME: wordpress
    volumes:
      - wpdata:/var/www/html
    depends_on:
      db:
        condition: service_healthy
    restart: unless-stopped
    networks:
      - wpnet

  # ===== 数据库 =====
  db:
    image: mysql:8.0
    environment:
      MYSQL_ROOT_PASSWORD: ${MYSQL_ROOT_PASSWORD}
      MYSQL_DATABASE: wordpress
      MYSQL_USER: wpuser
      MYSQL_PASSWORD: ${WP_DB_PASSWORD}
    volumes:
      - mysqldata:/var/lib/mysql
    healthcheck:
      test: ["CMD", "mysqladmin", "ping", "-h", "localhost"]
      interval: 5s
      timeout: 3s
      retries: 5
    restart: unless-stopped
    networks:
      - wpnet

  # ===== Redis 对象缓存 =====
  redis:
    image: redis:7-alpine
    volumes:
      - redisdata:/data
    restart: unless-stopped
    networks:
      - wpnet

  # ===== Nginx 反向代理（可选） =====
  nginx:
    image: nginx:alpine
    ports:
      - "443:443"
    volumes:
      - ./nginx.conf:/etc/nginx/nginx.conf:ro
      - ./ssl:/etc/nginx/ssl:ro
    depends_on:
      - wordpress
    restart: unless-stopped
    networks:
      - wpnet

volumes:
  wpdata:
  mysqldata:
  redisdata:

networks:
  wpnet:
    driver: bridge
```

### Nginx 配置

```nginx
# nginx.conf
events {
    worker_connections 1024;
}

http {
    upstream wordpress {
        server wordpress:80;
    }

    server {
        listen 443 ssl;
        server_name your-domain.com;

        ssl_certificate /etc/nginx/ssl/cert.pem;
        ssl_certificate_key /etc/nginx/ssl/key.pem;

        location / {
            proxy_pass http://wordpress;
            proxy_set_header Host $host;
            proxy_set_header X-Real-IP $remote_addr;
            proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
            proxy_set_header X-Forwarded-Proto $scheme;
        }
    }
}
```

## 数据备份与恢复

```bash
# 备份 MySQL 数据库
docker compose exec db mysqldump -u root -p${MYSQL_ROOT_PASSWORD} appdb > backup.sql

# 恢复数据库
docker compose exec -T db mysql -u root -p${MYSQL_ROOT_PASSWORD} appdb < backup.sql

# 备份卷数据
docker run --rm -v fullstack_mysqldata:/data -v $(pwd):/backup \
    alpine tar czf /backup/mysqldata.tar.gz /data

# 恢复卷数据
docker run --rm -v fullstack_mysqldata:/data -v $(pwd):/backup \
    alpine tar xzf /backup/mysqldata.tar.gz -C /
```

## 常见问题

### 容器启动顺序问题

使用 `depends_on` + `condition: service_healthy` 确保依赖服务就绪后再启动：

```yaml
depends_on:
  db:
    condition: service_healthy
```

### 服务间 DNS 解析失败

```bash
# 检查网络
docker compose exec web ping db

# 确保所有服务在同一网络
docker network inspect <项目名>_appnet
```

### 修改配置后不生效

```bash
# 需要强制重建
docker compose up -d --build --force-recreate

# 或先停止再启动
docker compose down
docker compose up -d --build
```

## 小结

Compose 实战要点：

1. **服务拆分**：Web + API + DB + Cache 各自独立
2. **网络隔离**：所有服务放在自定义 bridge 网络中
3. **数据持久化**：数据库使用命名卷，代码使用绑定挂载（开发）
4. **健康检查**：确保依赖服务真正就绪
5. **管理工具**：Adminer、phpMyAdmin 等辅助服务按需加入
6. **备份恢复**：`mysqldump` + `tar` 备份卷数据
