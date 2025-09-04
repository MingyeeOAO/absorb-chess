# 西洋棋遊戲部署指南 (Chess Game Deployment Guide)

## 部署到您的網域

### 方法一：使用 VPS/雲端伺服器

#### 1. 準備伺服器
```bash
# 更新系統
sudo apt update && sudo apt upgrade -y

# 安裝 Python 3 和 pip
sudo apt install python3 python3-pip -y

# 安裝 Nginx (網頁伺服器)
sudo apt install nginx -y

# 安裝 Node.js (用於前端建構，可選)
curl -fsSL https://deb.nodesource.com/setup_18.x | sudo -E bash -
sudo apt install nodejs -y
```

#### 2. 上傳程式碼
```bash
# 建立專案目錄
sudo mkdir -p /var/www/chess-game
sudo chown $USER:$USER /var/www/chess-game

# 上傳所有檔案到 /var/www/chess-game/
# 或使用 git clone
git clone <your-repo-url> /var/www/chess-game
```

#### 3. 設定 Python 環境
```bash
cd /var/www/chess-game

# 建立虛擬環境
python3 -m venv venv
source venv/bin/activate

# 安裝依賴
pip install -r requirements.txt
```

#### 4. 設定系統服務
建立服務檔案：
```bash
sudo nano /etc/systemd/system/chess-game.service
```

內容：
```ini
[Unit]
Description=Chess Game WebSocket Server
After=network.target

[Service]
Type=simple
User=www-data
WorkingDirectory=/var/www/chess-game
Environment=PATH=/var/www/chess-game/venv/bin
ExecStart=/var/www/chess-game/venv/bin/python server.py
Restart=always

[Install]
WantedBy=multi-user.target
```

啟動服務：
```bash
sudo systemctl daemon-reload
sudo systemctl enable chess-game
sudo systemctl start chess-game
```

#### 5. 設定 Nginx
建立 Nginx 設定檔：
```bash
sudo nano /etc/nginx/sites-available/chess-game
```

內容：
```nginx
server {
    listen 80;
    server_name your-domain.com;  # 替換為您的網域

    # 靜態檔案
    location / {
        root /var/www/chess-game;
        index index.html;
        try_files $uri $uri/ =404;
    }

    # WebSocket 代理
    location /ws {
        proxy_pass http://127.0.0.1:8765;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
    }
}
```

啟用設定：
```bash
sudo ln -s /etc/nginx/sites-available/chess-game /etc/nginx/sites-enabled/
sudo nginx -t
sudo systemctl restart nginx
```

#### 6. 設定 SSL (HTTPS)
```bash
# 安裝 Certbot
sudo apt install certbot python3-certbot-nginx -y

# 取得 SSL 憑證
sudo certbot --nginx -d your-domain.com

# 自動更新憑證
sudo crontab -e
# 加入這行：
# 0 12 * * * /usr/bin/certbot renew --quiet
```

### 方法二：使用 Docker

#### 1. 建立 Dockerfile
```dockerfile
FROM python:3.9-slim

WORKDIR /app

COPY requirements.txt .
RUN pip install -r requirements.txt

COPY . .

EXPOSE 8765

CMD ["python", "server.py"]
```

#### 2. 建立 docker-compose.yml
```yaml
version: '3.8'

services:
  chess-game:
    build: .
    ports:
      - "8765:8765"
    restart: unless-stopped
    
  nginx:
    image: nginx:alpine
    ports:
      - "80:80"
      - "443:443"
    volumes:
      - ./nginx.conf:/etc/nginx/nginx.conf
      - ./:/usr/share/nginx/html
    depends_on:
      - chess-game
```

#### 3. 建立 nginx.conf
```nginx
events {
    worker_connections 1024;
}

http {
    include /etc/nginx/mime.types;
    default_type application/octet-stream;

    server {
        listen 80;
        server_name your-domain.com;

        location / {
            root /usr/share/nginx/html;
            index index.html;
        }

        location /ws {
            proxy_pass http://chess-game:8765;
            proxy_http_version 1.1;
            proxy_set_header Upgrade $http_upgrade;
            proxy_set_header Connection "upgrade";
        }
    }
}
```

#### 4. 部署
```bash
docker-compose up -d
```

### 方法三：使用雲端平台

#### Heroku
1. 建立 `Procfile`：
```
web: python server.py
```

2. 建立 `runtime.txt`：
```
python-3.9.0
```

3. 部署：
```bash
heroku create your-app-name
git push heroku main
```

#### Railway
1. 連接 GitHub 倉庫
2. 設定環境變數
3. 自動部署

#### DigitalOcean App Platform
1. 上傳程式碼
2. 設定建構指令
3. 設定環境變數

### 修改前端連線設定

更新 `app.js` 中的 WebSocket 連線：
```javascript
// 將這行：
this.websocket = new WebSocket('ws://localhost:8765');

// 改為：
this.websocket = new WebSocket('wss://your-domain.com/ws');
```

### 環境變數設定

建立 `.env` 檔案：
```
HOST=0.0.0.0
PORT=8765
DEBUG=False
```

修改 `server.py`：
```python
import os
from dotenv import load_dotenv

load_dotenv()

HOST = os.getenv('HOST', 'localhost')
PORT = int(os.getenv('PORT', 8765))

# 在 main() 函數中：
async with websockets.serve(server.register_client, HOST, PORT):
```

### 監控和日誌

#### 查看服務狀態
```bash
sudo systemctl status chess-game
sudo journalctl -u chess-game -f
```

#### 查看 Nginx 日誌
```bash
sudo tail -f /var/log/nginx/access.log
sudo tail -f /var/log/nginx/error.log
```

### 防火牆設定

```bash
# 允許 HTTP 和 HTTPS
sudo ufw allow 80
sudo ufw allow 443

# 允許 SSH (如果使用)
sudo ufw allow 22

# 啟用防火牆
sudo ufw enable
```

### 備份和維護

#### 定期備份
```bash
# 建立備份腳本
#!/bin/bash
DATE=$(date +%Y%m%d_%H%M%S)
tar -czf /backup/chess-game-$DATE.tar.gz /var/www/chess-game
```

#### 更新程式碼
```bash
cd /var/www/chess-game
git pull origin main
sudo systemctl restart chess-game
```

### 效能優化

#### 1. 啟用 Gzip 壓縮
在 Nginx 設定中加入：
```nginx
gzip on;
gzip_types text/plain text/css application/json application/javascript text/xml application/xml application/xml+rss text/javascript;
```

#### 2. 設定快取
```nginx
location ~* \.(css|js|png|jpg|jpeg|gif|ico|svg)$ {
    expires 1y;
    add_header Cache-Control "public, immutable";
}
```

#### 3. 限制連線數
```nginx
limit_conn_zone $binary_remote_addr zone=conn_limit_per_ip:10m;
limit_req_zone $binary_remote_addr zone=req_limit_per_ip:10m rate=5r/s;

server {
    limit_conn conn_limit_per_ip 10;
    limit_req zone=req_limit_per_ip burst=10 nodelay;
}
```

### 故障排除

#### 常見問題
1. **WebSocket 連線失敗**：檢查 Nginx 代理設定
2. **服務無法啟動**：檢查 Python 路徑和依賴
3. **SSL 憑證問題**：確認網域 DNS 設定正確

#### 除錯指令
```bash
# 檢查端口使用
sudo netstat -tlnp | grep :8765

# 檢查服務日誌
sudo journalctl -u chess-game --since "1 hour ago"

# 測試 WebSocket 連線
wscat -c ws://your-domain.com/ws
```

### 安全建議

1. **定期更新系統和依賴**
2. **使用強密碼和 SSH 金鑰**
3. **設定防火牆規則**
4. **啟用 HTTPS**
5. **定期備份資料**
6. **監控系統資源使用**

完成以上設定後，您的西洋棋遊戲就可以在您的網域上運行了！
