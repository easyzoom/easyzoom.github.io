---
title: Docker 日志管理与监控
author: EASYZOOM
date: 2026/04/25 10:00
categories:
 - Docker的开发与实践
tags:
 - Docker
 - 日志
 - 监控
---

# Docker 日志管理与监控

## 前言

**C：** 容器跑了几天之后，磁盘突然满了——一看是日志占了几十个 GB。这种事在生产环境很常见。Docker 默认将容器日志存储为 JSON 文件，不限制大小，时间一长就是磁盘杀手。本篇讲解 Docker 日志的管理、限制、收集，以及常用的容器监控方案。

<!-- more -->

## Docker 日志基础

### 查看容器日志

```bash
# 基本用法
docker logs <容器名/ID>

# 常用选项
docker logs -f <容器名>             # 实时跟踪
docker logs --tail 100 <容器名>     # 最后 100 行
docker logs --since 1h <容器名>     # 最近 1 小时
docker logs --since "2026-04-01" <容器名>  # 指定日期
docker logs --until 1h <容器名>     # 1 小时之前
docker logs -t <容器名>             # 显示时间戳

# Compose
docker compose logs -f web
docker compose logs --tail=50 db
```

### 日志存储位置

```bash
# 默认 JSON 文件日志存储在
/var/lib/docker/containers/<容器ID>/<容器ID>-json.log

# 查看单个容器日志文件大小
docker inspect --format='{{.LogPath}}' <容器名>
ls -lh $(docker inspect --format='{{.LogPath}}' <容器名>)
```

## 日志驱动

Docker 支持多种日志驱动：

| 驱动 | 说明 | 适用场景 |
| --- | --- | --- |
| `json-file` | JSON 文件（默认） | 开发、单机 |
| `local` | 优化的文件存储 | 生产（自动压缩） |
| `syslog` | 发送到 syslog | 统一日志收集 |
| `journald` | systemd journal | systemd 系统 |
| `fluentd` | 发送到 Fluentd | 日志收集平台 |
| `gelf` | 发送到 Graylog/Logstash | ELK/GELF |
| `none` | 禁用日志 | 不需要日志的场景 |

### 配置日志驱动

**全局配置**（`/etc/docker/daemon.json`）：

```json
{
  "log-driver": "json-file",
  "log-opts": {
    "max-size": "50m",
    "max-file": "5"
  }
}
```

**单容器配置**：

```bash
docker run -d \
    --log-driver json-file \
    --log-opt max-size=50m \
    --log-opt max-file=5 \
    nginx
```

**Compose 配置**：

```yaml
services:
  web:
    image: nginx
    logging:
      driver: json-file
      options:
        max-size: "50m"
        max-file: "5"
```

::: warning 注意
全局配置只对新容器生效，已有容器需要重建。建议在 `daemon.json` 中全局设置 `max-size` 和 `max-file`，这是最重要的 Docker 配置之一。
:::

## 推荐的日志配置

```json
{
  "log-driver": "local",
  "log-opts": {
    "max-size": "50m",
    "max-file": "5",
    "compress": "true"
  }
}
```

`local` 驱动相比 `json-file`：
- 自动压缩旧日志
- 占用更少磁盘空间
- 性能更好

## 集中式日志收集

### 方案1：Fluentd

```yaml
# docker-compose.yml
services:
  app:
    image: myapp:1.0
    logging:
      driver: fluentd
      options:
        fluentd-address: "localhost:24224"
        tag: "app.{{.Name}}"

  fluentd:
    image: fluent/fluentd:v1.16
    ports:
      - "24224:24224"
      - "24224:24224/udp"
    volumes:
      - ./fluent.conf:/fluentd/etc/fluent.conf:ro
```

### 方案2：ELK Stack

```yaml
services:
  elasticsearch:
    image: elasticsearch:8.12.0
    environment:
      - discovery.type=single-node
      - xpack.security.enabled=false
    ports:
      - "9200:9200"
    volumes:
      - esdata:/usr/share/elasticsearch/data

  logstash:
    image: logstash:8.12.0
    ports:
      - "5044:5044"
    volumes:
      - ./logstash.conf:/usr/share/logstash/pipeline/logstash.conf:ro
    depends_on:
      - elasticsearch

  kibana:
    image: kibana:8.12.0
    ports:
      - "5601:5601"
    environment:
      - ELASTICSEARCH_HOSTS=http://elasticsearch:9200
    depends_on:
      - elasticsearch

  filebeat:
    image: elastic/filebeat:8.12.0
    volumes:
      - /var/lib/docker/containers:/var/lib/docker/containers:ro
      - /var/run/docker.sock:/var/run/docker.sock:ro
      - ./filebeat.yml:/usr/share/filebeat/filebeat.yml:ro
    depends_on:
      - logstash

volumes:
  esdata:
```

### 方案3：Loki + Grafana（轻量级）

```yaml
services:
  app:
    image: myapp:1.0
    logging:
      driver: loki
      options:
        loki-url: "http://loki:3100/loki/api/v1/push"

  loki:
    image: grafana/loki:2.9
    ports:
      - "3100:3100"
    volumes:
      - lokidata:/loki

  grafana:
    image: grafana/grafana:10.3
    ports:
      - "3000:3000"
    environment:
      - GF_AUTH_ANONYMOUS_ENABLED=true
    volumes:
      - grafanadata:/var/lib/grafana

volumes:
  lokidata:
  grafanadata:
```

## 容器监控

### 方案1：cAdvisor + Prometheus + Grafana

```yaml
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
    environment:
      - GF_AUTH_ANONYMOUS_ENABLED=true
    volumes:
      - grafanadata:/var/lib/grafana

  cadvisor:
    image: gcr.io/cadvisor/cadvisor:v0.47
    ports:
      - "8080:8080"
    volumes:
      - /:/rootfs:ro
      - /var/run:/var/run:ro
      - /sys:/sys:ro
      - /var/lib/docker/:/var/lib/docker:ro

  node-exporter:
    image: prom/node-exporter:v1.7
    ports:
      - "9100:9100"
    volumes:
      - /proc:/host/proc:ro
      - /sys:/host/sys:ro
      - /:/rootfs:ro

volumes:
  promdata:
  grafanadata:
```

### Prometheus 配置

```yaml
# prometheus.yml
global:
  scrape_interval: 15s

scrape_configs:
  - job_name: 'cadvisor'
    static_configs:
      - targets: ['cadvisor:8080']

  - job_name: 'node-exporter'
    static_configs:
      - targets: ['node-exporter:9100']

  - job_name: 'prometheus'
    static_configs:
      - targets: ['localhost:9090']
```

### 方案2：Portainer（管理面板）

```yaml
services:
  portainer:
    image: portainer/portainer-ce:2.19
    ports:
      - "9000:9000"
      - "9443:9443"
    volumes:
      - /var/run/docker.sock:/var/run/docker.sock
      - portainerdata:/data
    restart: unless-stopped

volumes:
  portainerdata:
```

访问 `http://localhost:9000` 即可管理所有容器、镜像、网络、卷。

## 快速排查命令

```bash
# 查看所有容器的资源使用
docker stats

# 查看磁盘使用
docker system df

# 查看详细信息
docker system df -v

# 清理未使用的资源
docker system prune        # 容器、网络、镜像、构建缓存
docker system prune -a     # 包括未使用的镜像
docker system prune -a --volumes  # 包括未使用的卷

# 查看单个容器资源
docker stats --no-stream web
```

## 常见问题

### 磁盘被日志占满

```bash
# 1. 设置日志大小限制
# 2. 清理现有日志
truncate -s 0 /var/lib/docker/containers/*/*-json.log

# 3. 或使用 docker system prune
docker system prune -a
```

### 日志时间戳不对

```bash
# 容器时区设置
docker run -e TZ=Asia/Shanghai myapp

# Compose
environment:
  - TZ=Asia/Shanghai
```

## 小结

日志与监控要点：

1. **日志限制**：`daemon.json` 中配置 `max-size` 和 `max-file`，这是最重要的配置
2. **日志驱动**：生产推荐 `local`，集中收集用 `fluentd` 或 `loki`
3. **日志收集三件套**：Fluentd/Filebeat → Elasticsearch/Loki → Grafana/Kibana
4. **容器监控**：cAdvisor + Prometheus + Grafana（完整方案）或 Portainer（简单方案）
5. **定期清理**：`docker system prune` 清理未使用资源
