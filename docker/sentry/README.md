# 自建 Sentry 服务(免费版)

本目录说明如何用官方 `getsentry/self-hosted` 在自己的服务器上搭建免费的 Sentry 服务,
供 `crash_sentry` 后端上报崩溃。

## 硬件要求(官方)

- 4 核 CPU / 8GB 内存 / 20GB 磁盘(起步)
- Docker + Docker Compose

## 部署步骤

```bash
# 1. 拉官方 self-hosted 仓库(找个有资源的 Linux 服务器,不要在 2核小 VM 上跑)
git clone https://github.com/getsentry/self-hosted.git
cd self-hosted

# 2. 一键安装(下载镜像、初始化 Postgres/Redis/Kafka/ClickHouse,约 10~20 分钟)
./install.sh

# 3. 启动
docker compose up -d

# 4. 创建管理员账号
docker compose run --rm web createuser --email admin@example.com --password <密码> --superuser
```

启动后访问 `http://<服务器IP>:9000`,登录后:

1. 新建 Project,平台选 **Native**。
2. 拿到 **DSN**,形如 `http://<key>@<服务器IP>:9000/<project-id>`。

## 配置到 BPLC_STA_Monitor

DSN 通过配置文件或环境变量交给程序(二选一):

- 配置文件 `config.ini`:
  ```ini
  [crash]
  backend=sentry
  dsn=http://<key>@<服务器IP>:9000/<project-id>
  ```
- 或环境变量:
  ```bash
  export SENTRY_DSN="http://<key>@<服务器IP>:9000/<project-id>"
  ./BPLC_STA_Monitor
  ```

不配 DSN 时 sentry 后端仍会生成 minidump 到本地 `crashpad_db/`,只是不上报。

## 本地无 Docker 时的验证

本机 VM 无 Docker,上传链路用 `3rdparty/mock_sentry.py` 验证:

```bash
python3 3rdparty/mock_sentry.py 9000 &
export SENTRY_DSN="http://testkey@127.0.0.1:9000/1"
# 跑崩溃测试,到 3rdparty/received/ 看收到的 envelope
```

mock 只验证"发得出",生产上报请用上面真正的 self-hosted。
