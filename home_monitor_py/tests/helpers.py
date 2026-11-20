"""Shared test helpers."""


class RecordingDispatcher:
    """Stands in for notify.Dispatcher: records every Notification instead of sending it."""

    def __init__(self, cfg=None):
        self.notifications = []
        self.channels = []

    @property
    def sent(self):
        return [n.subject for n in self.notifications]

    async def send(self, n):
        self.notifications.append(n)

    async def send_once(self, n):
        self.notifications.append(n)
        return ["recorded"]


# --- throwaway servers for testing real delivery ---------------------------------------------
import email
import http.server
import json
import socketserver
import threading


class SMTPHandler(socketserver.StreamRequestHandler):
    def handle(self):
        def reply(text):
            self.wfile.write(text.encode() + b"\r\n")
            self.wfile.flush()
        reply("220 test")
        rcpts, sender = [], ""
        while True:
            line = self.rfile.readline().decode().strip()
            if not line:
                return
            cmd = line.upper()
            if cmd.startswith("MAIL FROM"):
                sender = line.split(":", 1)[1].strip()
                reply("250 ok")
            elif cmd.startswith("RCPT TO"):
                rcpts.append(line.split(":", 1)[1].strip().strip("<>"))
                reply("250 ok")
            elif cmd == "DATA":
                reply("354 go")
                lines = []
                while (data := self.rfile.readline().decode()) != ".\r\n":
                    lines.append(data)
                self.server.messages.append((sender, rcpts, email.message_from_string("".join(lines))))
                rcpts = []
                reply("250 ok")
            elif cmd == "QUIT":
                reply("221 bye")
                return
            else:
                reply("250 ok")


class SMTPServer:
    """Accepts mail on a free local port and keeps it in .messages as (sender, recipients, Message)."""

    def __enter__(self):
        self.server = socketserver.ThreadingTCPServer(("127.0.0.1", 0), SMTPHandler)
        self.server.daemon_threads = True
        self.server.messages = []
        threading.Thread(target=self.server.serve_forever, kwargs={"poll_interval": 0.02}, daemon=True).start()
        self.port = self.server.server_address[1]
        return self

    def __exit__(self, *exc):
        self.server.shutdown()
        self.server.server_close()

    @property
    def messages(self):
        return self.server.messages

    def options(self, **extra):
        return {"smtp_host": "127.0.0.1", "smtp_port": self.port, "from_addr": "monitor@test", "starttls": False, **extra}


class NtfyServer:
    """A small stand-in for an ntfy server.

    * POST / with JSON publishes (what the monitor does); POST /<topic> with a text body publishes
      (what a phone's "http" action button does).
    * GET /<topic>/json?poll=1&since=<all | unix time | message id> returns that topic's messages.
    * ``requests`` / ``payloads`` record the JSON publishes; ``messages[topic]`` holds everything per topic.
    * Topics in ``fail_topics`` get a 500 on JSON publish; ``fail_polls`` makes the next N polls fail.
    """

    def __init__(self, fail_topics=()):
        self.fail_topics = set(fail_topics)
        self.fail_polls = 0
        self.requests = []
        self.messages = {}
        self._n = 0

    def _store(self, topic, **fields):
        self._n += 1
        msg = {"id": f"id{self._n:08d}", "time": int(__import__("time").time()), "event": "message", "topic": topic, **fields}
        self.messages.setdefault(topic, []).append(msg)

    def __enter__(self):
        outer = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def _reply(self, code, body=b"{}"):
                self.send_response(code)
                self.end_headers()
                self.wfile.write(body)

            def do_POST(self):
                raw = self.rfile.read(int(self.headers["Content-Length"]))
                if self.path == "/":
                    body = json.loads(raw)
                    outer.requests.append((self.path, dict(self.headers), body))
                    if body.get("topic") in outer.fail_topics:
                        return self._reply(500)
                    outer._store(body["topic"], **{k: v for k, v in body.items() if k != "topic"})
                else:   # a phone's button: the body text is the message
                    outer._store(self.path.strip("/"), message=raw.decode())
                self._reply(200)

            def do_GET(self):
                from urllib.parse import parse_qs, urlparse
                url = urlparse(self.path)
                topic = url.path.strip("/").removesuffix("/json")
                if outer.fail_polls:
                    outer.fail_polls -= 1
                    return self._reply(500)
                since = parse_qs(url.query).get("since", ["all"])[0]
                msgs = outer.messages.get(topic, [])
                if since == "all":
                    out = msgs
                elif since.isdigit():
                    out = [m for m in msgs if m["time"] >= int(since)]
                else:
                    ids = [m["id"] for m in msgs]
                    out = msgs[ids.index(since) + 1:] if since in ids else msgs
                self._reply(200, "".join(json.dumps(m) + "\n" for m in out).encode())

        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        threading.Thread(target=self.server.serve_forever, kwargs={"poll_interval": 0.02}, daemon=True).start()
        self.url = f"http://127.0.0.1:{self.server.server_address[1]}"
        return self

    def __exit__(self, *exc):
        self.server.shutdown()
        self.server.server_close()

    @property
    def payloads(self):
        return [p for _, _, p in self.requests]

    def press(self, action):
        """Do what the phone does when a button is tapped: POST its body to its url."""
        import urllib.request
        req = urllib.request.Request(action["url"], data=action["body"].encode(), method=action["method"])
        urllib.request.urlopen(req, timeout=5).close()
